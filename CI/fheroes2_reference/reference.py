#!/usr/bin/env python3
"""Build unchanged FHeroes2, run its planner/scheduler, and pair four properties."""
import argparse
import collections
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.request
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
SCENARIOS = ('melee', 'archers', 'mixed', 'flying', 'wide', 'spell_damage', 'spell_control', 'human_auto')
SEEDS = (1, 42, 123456, 4294967295)
WRAPS = ('_ZN2AI13BattlePlanner10BattleTurnERN6Battle5ArenaERKNS1_4UnitERNS1_7ActionsE',
         '_ZN6Battle5Arena11ApplyActionERNS_7CommandE')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def file_hash(path):
    return digest(path.read_bytes())


def read_json(path):
    return json.loads(path.read_text())


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def checkout_identity(repository, local=False):
    if local:
        return {'status': 'local_unbound', 'commit': None, 'tree': None}
    def git(*args):
        return subprocess.check_output(['git', '-C', str(repository), *args], text=True).strip()
    require(not git('status', '--porcelain', '--untracked-files=no'), 'Tracked checkout has local changes')
    return {'status': 'clean_checkout', 'commit': git('rev-parse', 'HEAD'), 'tree': git('rev-parse', 'HEAD^{tree}')}


def harness_hashes():
    return {p.name: file_hash(p) for p in sorted(HERE.iterdir()) if p.is_file()}


def sanitize(text, paths):
    for path, label in sorted(paths, key=lambda x: len(str(x[0])), reverse=True):
        text = text.replace(str(path), label)
    return text


def extract_source(archive, source, lock):
    require(file_hash(archive) == lock['archive_sha256'], 'Upstream archive SHA-256 mismatch')
    expected_root = 'fheroes2-' + lock['commit']
    with tarfile.open(archive, 'r:gz') as bundle:
        members = bundle.getmembers()
        names = set()
        for member in members:
            path = PurePosixPath(member.name)
            require(bool(path.parts) and not path.is_absolute() and '..' not in path.parts and path.parts[0] == expected_root,
                    'Unsafe or unexpected archive path: ' + member.name)
            require(member.isdir() or member.isfile(), 'Archive links and special entries are not accepted')
            require(member.name not in names, 'Duplicate archive entry: ' + member.name)
            names.add(member.name)
        for member in members:
            relative = PurePosixPath(member.name).relative_to(expected_root)
            target = source.joinpath(*relative.parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with bundle.extractfile(member) as stream:
                    target.write_bytes(stream.read())
    return verify_source(source, lock)


def verify_source(source, lock):
    actual_paths = {p.relative_to(source).as_posix() for p in source.rglob('*') if p.is_file()}
    require(actual_paths == set(lock['files']), 'Source file set does not match pinned manifest')
    rows = []
    for name, expected in lock['files'].items():
        data = (source / name).read_bytes()
        actual = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
        require(actual == expected, 'Upstream source changed: ' + name)
        rows.append({'path': name, 'git_blob_sha1': actual, 'sha256': digest(data), 'bytes': len(data)})
    return rows


def unit(row, uid):
    return next(u for u in row['units'] if u['uid'] == uid)


def trace_properties(scenario, rows):
    require(bool(rows), 'Empty source trace')
    require(rows[0]['phase'] == 'initial' and rows[-1]['phase'] == 'final', 'Missing trace boundary')
    require([r['event'] for r in rows] == list(range(len(rows))), 'Noncontiguous event indices')
    phases = collections.Counter(r['phase'] for r in rows)
    require(phases['planner_before'] == phases['planner_after'] > 0, 'Original planner did not execute')
    require(phases['action_before'] == phases['action_after'] > 0, 'Original action executor did not execute')
    require(all(r['ambient_rng'] == rows[0]['ambient_rng'] for r in rows), 'Ambient RNG changed during battle')
    active = None
    spells, transfers, ranged = [], [], []
    for i, row in enumerate(rows):
        if row['phase'] == 'planner_before':
            require(rows[i + 1]['phase'] == 'planner_after' and rows[i + 1]['active_uid'] == row['active_uid'],
                    'Unpaired original planner call')
        if row['phase'] == 'planner_after':
            active = row['active_uid']
        if row['phase'] != 'action_before':
            continue
        after = rows[i + 1]
        require(after['phase'] == 'action_after', 'Unpaired original engine action')
        command = row['command']
        args = command['constructor_args']
        if command['type'] == 'SPELLCAST':
            spells.append(args[0])
            if args[0] == 32:
                for target in row['units']:
                    if args[1] in (target['head'], target['tail']):
                        changed = unit(after, target['uid'])
                        if changed['army_color'] != changed['current_color'] == unit(row, active)['current_color']:
                            transfers.append(row['event'])
        if command['type'] == 'ATTACK' and unit(after, args[0])['shots'] < unit(row, args[0])['shots']:
            ranged.append(row['event'])
    checks = {}
    if scenario == 'spell_damage':
        checks['damage_spell_chosen'] = 3 in spells
    elif scenario == 'spell_control':
        checks['hypnotize_chosen_and_control_transferred'] = 32 in spells and bool(transfers)
    elif scenario == 'human_auto':
        hero = rows[0]['commanders'][0]
        checks['spell_permission_gate_respected'] = not spells and hero['control'] == 1 and hero['mana'] == 100 and 3 in hero['spells']
    elif scenario == 'archers':
        checks['ranged_attack_chosen'] = bool(ranged)
    require(all(checks.values()), 'Source property failed: ' + json.dumps(checks))
    return {'checks': checks, 'spells': spells, 'hypnotize_transfer_events': transfers,
            'ranged_attack_events': ranged, 'planner_calls': phases['planner_before'],
            'applied_commands': phases['action_before'], 'trace_events': len(rows)}


def compile_engine(args, source, work, evidence, paths):
    build = work / 'build'
    build.mkdir()
    includes = [source / 'src/engine', source / 'src/thirdparty/libsmacker']
    includes += sorted(p for p in (source / 'src/fheroes2').iterdir() if p.is_dir())
    flags = ['-O0', '-g1', '-fsigned-char', '-pthread', '-DWITH_DEBUG', '-D_REENTRANT']
    flags += ['-I' + str(p) for p in includes]
    env = os.environ.copy()
    if args.dependency_prefix:
        prefix = args.dependency_prefix.resolve()
        lib = prefix / 'usr/lib/x86_64-linux-gnu'
        flags += ['-I' + str(prefix / p) for p in ('usr/include', 'usr/include/SDL2', 'usr/include/x86_64-linux-gnu')]
        link_flags = ['-L' + str(lib), '-Wl,-rpath,' + str(lib), '-Wl,-rpath-link,' + str(lib),
                      '-lSDL2_mixer', '-l:libSDL2-2.0.so.0', '-lz', '-pthread']
        env['LD_LIBRARY_PATH'] = str(lib)
        dependencies = {'mode': 'local_dependency_prefix', 'files': {p.name: file_hash(p) for p in sorted(lib.glob('*')) if p.is_file()}}
    else:
        flags += shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'sdl2', 'SDL2_mixer'], text=True))
        link_flags = shlex.split(subprocess.check_output(['pkg-config', '--libs', 'sdl2', 'SDL2_mixer'], text=True)) + ['-lz', '-pthread']
        dependencies = {'mode': 'system_pkg_config', 'versions': subprocess.check_output(
            ['pkg-config', '--modversion', 'sdl2', 'SDL2_mixer', 'zlib'], text=True).splitlines()}
    tools = {'cc': subprocess.check_output([args.cc, '--version'], text=True),
             'cxx': subprocess.check_output([args.cxx, '--version'], text=True), 'dependencies': dependencies}
    save(evidence / 'toolchain.json', tools)
    sources = sorted((source / 'src/fheroes2').glob('*/*.cpp')) + sorted((source / 'src/engine').glob('*.cpp'))
    sources += [source / 'src/thirdparty/libsmacker/smacker.c']
    sources = [p for p in sources if p.name != 'fheroes2.cpp']
    require(len(sources) == 249, 'Translation-unit inventory changed')
    def compile_one(p):
        name = p.relative_to(source).as_posix()
        obj = build / (name.replace('/', '__') + '.o')
        command = ([args.cc] if p.suffix == '.c' else [args.cxx, '-std=c++17']) + flags + ['-c', str(p), '-o', str(obj)]
        started = time.monotonic()
        try:
            proc = subprocess.run(command, capture_output=True, text=True, timeout=180)
            log, code = proc.stdout + proc.stderr, proc.returncode
        except subprocess.TimeoutExpired as error:
            log, code = str(error), 124
        (evidence / (obj.name + '.log')).write_text(sanitize(log, paths))
        return {'source': name, 'object': obj.name, 'returncode': code, 'seconds': time.monotonic() - started,
                'command': [sanitize(x, paths) for x in command]}
    start = time.monotonic()
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        units = list(pool.map(compile_one, sources))
    save(evidence / 'compilation.json', {'seconds': time.monotonic() - start, 'units': units})
    require(all(r['returncode'] == 0 for r in units), 'Original-source compilation failed; see per-object logs')
    objects = {p.name: file_hash(p) for p in sorted(build.glob('*.o'))}
    save(evidence / 'objects.json', objects)
    links = []
    for mode in ('baseline', 'observed'):
        extra = ['-DORACLE_OBSERVE'] + ['-Wl,--wrap=' + symbol for symbol in WRAPS] if mode == 'observed' else []
        command = [args.cxx, '-std=c++17'] + flags + extra + [str(HERE / 'oracle.cpp')]
        command += [str(build / name) for name in objects] + link_flags + ['-o', str(build / ('oracle-' + mode))]
        start = time.monotonic()
        proc = subprocess.run(command, capture_output=True, text=True, timeout=180)
        (evidence / ('link-' + mode + '.log')).write_text(sanitize(proc.stdout + proc.stderr, paths))
        links.append({'mode': mode, 'returncode': proc.returncode, 'seconds': time.monotonic() - start,
                      'command': [sanitize(x, paths) for x in command]})
        save(evidence / 'link.json', links)
        require(proc.returncode == 0, 'Reference link failed: ' + mode)
    save(evidence / 'binaries.json', {p.name: file_hash(p) for p in build.glob('oracle-*')})
    return env, objects


def run_source(args, report):
    work = args.work_dir.resolve()
    require(not work.exists() or not any(work.iterdir()), 'Work directory must be new or empty')
    evidence = work / 'evidence'
    evidence.mkdir(parents=True)
    paths = [(work, '<work>'), (args.repository.resolve(), '<repository>'), (HERE, '<harness>')]
    if args.dependency_prefix:
        paths.append((args.dependency_prefix.resolve(), '<dependency-prefix>'))
    report.update(checkout=checkout_identity(args.repository, args.local), harness=harness_hashes())
    lock = read_json(HERE / 'upstream.json')
    report['upstream_commit'] = lock['commit']
    save(evidence / 'status.json', report)
    archive = work / 'source.tar.gz'
    if args.archive:
        shutil.copyfile(args.archive, archive)
    else:
        with urllib.request.urlopen(lock['archive_url'], timeout=120) as stream, archive.open('wb') as output:
            shutil.copyfileobj(stream, output)
    source = work / 'source'
    initial = extract_source(archive, source, lock)
    save(evidence / 'source-before.json', initial)
    env, objects = compile_engine(args, source, work, evidence, paths)
    env.pop('ORACLE_TRACE', None)
    corpus = evidence / 'corpus'
    corpus.mkdir()
    results, process_runs = [], 0
    try:
        for scenario in SCENARIOS:
            for seed in SEEDS:
                outputs, traces, runs = [], [], []
                for mode in ('baseline', 'observed-a', 'observed-b'):
                    stem = f'{scenario}-{seed}-{mode}'
                    trace_path = corpus / (stem + '.jsonl')
                    run_env = dict(env)
                    if mode != 'baseline':
                        run_env['ORACLE_TRACE'] = str(trace_path)
                    exe = work / 'build' / ('oracle-baseline' if mode == 'baseline' else 'oracle-observed')
                    start = time.monotonic()
                    try:
                        proc = subprocess.run([str(exe), scenario, str(seed)], env=run_env, capture_output=True, timeout=30)
                        stdout, stderr, code = proc.stdout, proc.stderr, proc.returncode
                    except subprocess.TimeoutExpired as error:
                        stdout, stderr, code = error.stdout or b'', error.stderr or b'', 124
                    process_runs += 1
                    (corpus / (stem + '.json')).write_bytes(stdout)
                    (corpus / (stem + '.stderr')).write_text(sanitize(stderr.decode(errors='replace'), paths))
                    runs.append({'mode': mode, 'returncode': code, 'seconds': time.monotonic() - start})
                    save(evidence / 'current-case.json', {'scenario': scenario, 'seed': seed, 'runs': runs})
                    require(code == 0, f'Original-engine process failed: {stem}, exit {code}')
                    outputs.append(stdout)
                    if mode != 'baseline':
                        traces.append(trace_path.read_bytes())
                require(outputs[0] == outputs[1] == outputs[2], f'Observer/repeat outcome mismatch: {scenario}/{seed}')
                require(traces[0] == traces[1], f'Repeated trace mismatch: {scenario}/{seed}')
                properties = trace_properties(scenario, [json.loads(line) for line in traces[0].splitlines()])
                results.append({'scenario': scenario, 'seed': seed, 'runs': runs, 'properties': properties,
                                'outcome_sha256': digest(outputs[0]), 'trace_sha256': digest(traces[0]),
                                'observer_equivalent': True, 'repeat_trace_byte_identical': True})
                print(f'{scenario}/{seed}: PASS', flush=True)
    finally:
        report.update(results=results, cases=len(results), process_runs=process_runs)
        save(evidence / 'verification.json', report)
    require(verify_source(source, lock) == initial, 'Source provenance changed during execution')
    require({p.name: file_hash(p) for p in (work / 'build').glob('*.o')} == objects, 'Original objects changed during execution')
    require(checkout_identity(args.repository, args.local) == report['checkout'], 'Checkout changed during execution')
    require(harness_hashes() == report['harness'], 'Harness changed during execution')
    report.update(status='passed', source_blobs_verified=len(initial), original_objects_verified=len(objects), upstream_changes=0)
    save(evidence / 'verification.json', report)


def native_test_hashes(repository, mapping):
    source = (repository / mapping['native_source']).read_text()
    hashes = {}
    for prop in mapping['properties'].values():
        test = prop['native_test']
        match = re.search(r'TEST_F\(AIIntegrationNativeBattle, ' + re.escape(test) + r'\)\s*\n\{.*?^\}', source, re.M | re.S)
        require(match is not None, 'Missing native test: ' + test)
        hashes[test] = digest(match.group(0).encode())
        require(hashes[test] == prop['native_test_sha256'], 'Native test changed; review property mapping: ' + test)
    require(re.search(r'void runToCompletion\([^\{]+backendName = "FHeroes2Tactics"', source) is not None,
            'Native default backend changed; review property mapping')
    return hashes


def xml_results(path, mapping):
    root = ET.parse(path).getroot()
    expected = {v['native_test'] for v in mapping['properties'].values()}
    require(int(root.get('tests', '-1')) == len(expected) and all(int(root.get(x, '-1')) == 0 for x in ('failures', 'errors', 'disabled')),
            'Native result summary is incomplete or not clean')
    cases = [c for c in root.iter('testcase') if c.get('classname') == mapping['native_suite']]
    names = [c.get('name') for c in cases]
    require(len(names) == len(set(names)) and set(names) == expected, 'Missing, duplicate or unexpected native tests')
    return {c.get('name'): c.get('status') == 'run' and c.get('result') == 'completed'
            and not any(c.find(x) is not None for x in ('failure', 'error', 'skipped')) for c in cases}


def normalize_native_xml(path, native_source):
    tree = ET.parse(path)
    paths = []
    for case in tree.getroot().iter('testcase'):
        if case.get('file'):
            filename = case.get('file').replace('\\', '/')
            require(filename == native_source or filename.endswith('/' + native_source), 'Unexpected native test source in XML')
            prefix = filename[:-len(native_source)].rstrip('/')
            if prefix:
                paths.append((prefix, '<compiled-source>'))
            case.set('file', native_source)
    tree.write(path, encoding='unicode', xml_declaration=True)
    return paths


def run_native(args, report):
    work = args.work_dir.resolve()
    require(not work.exists() or not any(work.iterdir()), 'Native work directory must be new or empty')
    work.mkdir(parents=True)
    repository = args.repository.resolve()
    binary = args.test_binary.resolve()
    mapping = read_json(HERE / 'properties.json')
    report.update(checkout=checkout_identity(repository, args.local), harness=harness_hashes(),
                  upstream_commit=read_json(HERE / 'upstream.json')['commit'],
                  native_test_hashes=native_test_hashes(repository, mapping), native_binary_sha256=file_hash(binary),
                  native_test_file_sha256=file_hash(repository / mapping['native_source']))
    report['binding'] = ('Unbound local execution of a supplied binary; this capture does not establish binary/source binding.'
                         if args.local else 'Same-checkout native build followed by execution; CI build logs remain required provenance.')
    report['ci'] = {key: os.environ.get(key) for key in ('GITHUB_SHA', 'GITHUB_RUN_ID', 'GITHUB_RUN_ATTEMPT')}
    libraries = sorted(p for p in binary.parent.glob('*.so*') if p.is_file())
    report['adjacent_libraries'] = {p.name: file_hash(p) for p in libraries}
    save(work / 'native.json', report)
    tests = ':'.join(mapping['native_suite'] + '.' + v['native_test'] for v in mapping['properties'].values())
    xml = work / 'native.xml'
    try:
        proc = subprocess.run([str(binary), '--gtest_filter=' + tests, '--gtest_output=xml:' + str(xml)],
                              cwd=binary.parent, capture_output=True, text=True, timeout=120)
        log, code = proc.stdout + proc.stderr, proc.returncode
    except subprocess.TimeoutExpired as error:
        log, code = str(error), 124
    paths = [(work, '<work>'), (repository, '<repository>'), (binary.parent, '<native-bin>'), (Path.home(), '<home>')]
    report['returncode'] = code
    if xml.exists():
        paths += normalize_native_xml(xml, mapping['native_source'])
    (work / 'native.log').write_text(sanitize(log, paths))
    require(code == 0, 'Native property tests failed; see native.log and native.xml')
    report['tests'] = xml_results(xml, mapping)
    require(all(report['tests'].values()), 'Native property test failed or skipped')
    require(file_hash(binary) == report['native_binary_sha256'], 'Native binary changed during execution')
    require({p.name: file_hash(p) for p in libraries} == report['adjacent_libraries'], 'Native libraries changed during execution')
    require(checkout_identity(repository, args.local) == report['checkout'], 'Checkout changed during native execution')
    require(harness_hashes() == report['harness'], 'Harness changed during native execution')
    report.update(status='passed', native_xml_sha256=file_hash(xml))
    save(work / 'native.json', report)


def compare(args, report):
    mapping = read_json(HERE / 'properties.json')
    report.update(scope=mapping['scope'], properties={p: 'missing_evidence' for p in mapping['properties']})
    source_path = args.source_evidence / 'verification.json'
    native_path = args.native_evidence / 'native.json'
    require(source_path.is_file() and native_path.is_file(), 'Source or native evidence is missing')
    source, native = read_json(source_path), read_json(native_path)
    report['inputs'] = {'source_sha256': file_hash(source_path), 'native_sha256': file_hash(native_path)}
    require(source.get('status') == native.get('status') == 'passed', 'Source or native execution did not pass')
    require(source['checkout'].get('status') == native['checkout'].get('status') == 'clean_checkout',
            'Local or unbound evidence cannot pass property pairing')
    require(source['checkout'] == native['checkout'] == checkout_identity(args.repository), 'Checkout provenance mismatch')
    require(source['harness'] == native['harness'] == harness_hashes(), 'Harness provenance mismatch')
    pin = read_json(HERE / 'upstream.json')['commit']
    require(source['upstream_commit'] == native['upstream_commit'] == pin, 'Upstream pin mismatch')
    require(source['source_blobs_verified'] == 965 and source['original_objects_verified'] == 249 and source['upstream_changes'] == 0,
            'Incomplete original-source provenance')
    require(source['cases'] == 32 and source['process_runs'] == 96, 'Incomplete source execution')
    require(native['native_test_hashes'] == native_test_hashes(args.repository, mapping), 'Native test mapping mismatch')
    require(native['native_test_file_sha256'] == file_hash(args.repository / mapping['native_source']), 'Native test source mismatch')
    native_xml = args.native_evidence / 'native.xml'
    require(file_hash(native_xml) == native['native_xml_sha256'], 'Native XML changed')
    tests = xml_results(native_xml, mapping)
    require(tests == native['tests'] and all(tests.values()), 'Native test status mismatch')
    cases = source['results']
    require(len(cases) == 32 and {(r['scenario'], r['seed']) for r in cases} == {(s, n) for s in SCENARIOS for n in SEEDS},
            'Missing or duplicate source cases')
    for row in cases:
        stem = f"{row['scenario']}-{row['seed']}"
        corpus = args.source_evidence / 'corpus'
        outputs = [(corpus / (stem + '-' + mode + '.json')).read_bytes() for mode in ('baseline', 'observed-a', 'observed-b')]
        traces = [(corpus / (stem + '-' + mode + '.jsonl')).read_bytes() for mode in ('observed-a', 'observed-b')]
        require(outputs[0] == outputs[1] == outputs[2] and digest(outputs[0]) == row['outcome_sha256'], 'Source outcome changed')
        require(traces[0] == traces[1] and digest(traces[0]) == row['trace_sha256'], 'Source trace changed')
        require(trace_properties(row['scenario'], [json.loads(line) for line in traces[0].splitlines()]) == row['properties'],
                'Source properties changed')
    for prop, pair in mapping['properties'].items():
        rows = [r for r in cases if r['scenario'] == pair['source_scenario']]
        require(len(rows) == len(SEEDS) and all(r['properties']['checks'].get(prop) is True for r in rows), 'Source property missing: ' + prop)
        require(tests[pair['native_test']], 'Native property missing: ' + prop)
        report['properties'][prop] = 'mapped_property_agrees'
    report.update(status='passed', checkout=source['checkout'], upstream_commit=pin)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    for name in ('source', 'native', 'compare'):
        cmd = sub.add_parser(name)
        cmd.add_argument('--repository', type=Path, default=HERE.parents[1])
        if name != 'compare':
            cmd.add_argument('--work-dir', type=Path, required=True)
            cmd.add_argument('--local', action='store_true', help='Allow an unbound local run; comparator will reject it')
        if name == 'source':
            cmd.add_argument('--archive', type=Path, help='Optional existing official archive, still hash-checked')
            cmd.add_argument('--jobs', type=int, choices=range(1, 17), default=2)
            cmd.add_argument('--cc', default='gcc')
            cmd.add_argument('--cxx', default='g++')
            cmd.add_argument('--dependency-prefix', type=Path, help='Local Linux x86_64 dependency sysroot, not used in CI')
        elif name == 'native':
            cmd.add_argument('--test-binary', type=Path, required=True)
        else:
            cmd.add_argument('--source-evidence', type=Path, required=True)
            cmd.add_argument('--native-evidence', type=Path, required=True)
            cmd.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.command != 'compare' and args.work_dir.exists() and any(args.work_dir.iterdir()):
        print('Work directory must be new or empty; existing evidence was not changed', file=sys.stderr)
        return 1
    report = {'schema': 1, 'stage': args.command, 'status': 'running'}
    started = time.monotonic()
    if args.command == 'source':
        output = args.work_dir / 'evidence/status.json'
    elif args.command == 'native':
        output = args.work_dir / 'native.json'
    else:
        output = args.output
    try:
        {'source': run_source, 'native': run_native, 'compare': compare}[args.command](args, report)
    except (OSError, ValueError, KeyError, IndexError, TypeError, StopIteration, subprocess.SubprocessError, ET.ParseError, tarfile.TarError) as error:
        paths = [(args.repository.resolve(), '<repository>'), (HERE, '<harness>')]
        for key in ('work_dir', 'archive', 'dependency_prefix', 'test_binary', 'source_evidence', 'native_evidence'):
            if getattr(args, key, None):
                paths.append((getattr(args, key).resolve(), '<' + key.replace('_', '-') + '>'))
        report.update(status='failed', error=sanitize(str(error), paths))
    finally:
        report['seconds'] = time.monotonic() - started
        save(output, report)
    print(json.dumps({k: v for k, v in report.items() if k in ('status', 'error', 'seconds', 'cases', 'process_runs', 'properties')}, indent=2))
    return 0 if report['status'] == 'passed' else 1


if __name__ == '__main__':
    sys.exit(main())
