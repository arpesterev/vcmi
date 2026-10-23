#!/usr/bin/env python3
"""Opt-in, bounded, reversible C++ dependency graph probes on a warm Ninja tree.

Never save build caches while this tool is running. Cache save/package must be
conditional on successful completion and a fresh source-clean check. A failed
cleanup is reported, not hidden. No game code changes are committed.

Coverage is bounded: the selected shared header has at most --max-objects
consumers (32 by default). These probes do not validate every header or the
fanout of global headers.
"""
import argparse
import collections
import hashlib
import json
import os
import pathlib
import re
import shlex
import shutil
import signal
import subprocess
import tempfile
import time


def within(path, root):
    try: path.relative_to(root); return True
    except ValueError: return False


def output_name(path, build):
    path = pathlib.Path(path)
    if not path.is_absolute(): path = build / path
    path = path.resolve()
    if not within(path, build): raise ValueError('Object output outside build: ' + str(path))
    return path.relative_to(build).as_posix()


def commands(build, source):
    objects = {}
    for entry in json.loads((build / 'compile_commands.json').read_text()):
        directory = pathlib.Path(entry['directory']).resolve()
        file = pathlib.Path(entry['file'])
        file = (file if file.is_absolute() else directory / file).resolve()
        # Count every TU, including generated Qt and external sources.
        # Only candidate mutation paths are restricted to project source.
        args = entry.get('arguments') or shlex.split(entry['command'])
        out = entry.get('output')
        if not out:
            try: out = args[args.index('-o') + 1]
            except (ValueError, IndexError): raise ValueError('Missing compiler output: ' + str(file))
        outpath = pathlib.Path(out)
        outpath = outpath if outpath.is_absolute() else directory / outpath
        name = output_name(outpath, build)
        if name in objects and objects[name] != file: raise ValueError('Conflicting compile output')
        objects[name] = file
    if not objects: raise ValueError('No compiler commands found')
    return objects


def parse_deps(text, build, objects):
    deps = collections.defaultdict(set)
    active = None
    for line in text.splitlines():
        if ': #deps ' in line:
            name, tail = line.split(': #deps ', 1)
            active = output_name(name, build)
            if active not in objects: active = None
            elif '(VALID)' not in tail: raise ValueError('Invalid Ninja dependency record: ' + name)
        elif line.startswith('    ') and active:
            file = pathlib.Path(line[4:])
            file = (file if file.is_absolute() else build / file).resolve()
            deps[file].add(active)
    for obj, file in objects.items(): deps[file].add(obj)
    return deps


def family(obj):
    name = re.search(r'(?:^|/)CMakeFiles/([^/]+)\.dir/', obj)
    name = name.group(1) if name else obj.split('/')[0]
    if name.startswith('vcmiclient'): return 'client'
    if name.startswith(('BattleAI', 'Nullkiller', 'StupidAI', 'EmptyAI', 'MMAI')): return 'ai'
    if name.startswith('vcmi_') or name == 'vcmi': return 'core'
    return name


def select_probes(source, build, objects, deps, maximum):
    def safe(file):
        if not within(file, source) or within(file, build): return False
        rel = file.relative_to(source).as_posix()
        if rel.startswith(('client/lib/', 'test/', 'dependencies/')): return False
        if file.is_symlink() or not file.is_file(): return False
        return all(not parent.is_symlink() for parent in file.parents if within(parent, source))
    def choose(candidates, label):
        candidates = [(file, set(consumers)) for file, consumers in candidates if safe(file) and 0 < len(consumers) <= maximum]
        if not candidates: raise ValueError('No bounded valid candidate for ' + label)
        return min(candidates, key=lambda item: (len(item[1]), item[0].as_posix()))
    items = list(deps.items())
    specs = []
    for label, prefix in [('client-source', 'client/'), ('battle-ai-source', 'AI/BattleAI/')]:
        specs.append((label, *choose([(p, d) for p, d in items if p.suffix == '.cpp' and within(p, source) and p.relative_to(source).as_posix().startswith(prefix)], label)))
    specs.append(('private-client-header', *choose([(p, d) for p, d in items if p.suffix in ('.h','.hpp') and within(p, source) and p.relative_to(source).as_posix().startswith('client/') and {family(o) for o in d} == {'client'}], 'private client header')))
    public = [(p, d) for p, d in items if p.suffix in ('.h','.hpp') and within(p, source) and p.relative_to(source).as_posix().startswith('lib/') and len({family(o) for o in d} & {'core','client','ai'}) >= 2]
    three = [(p,d) for p,d in public if len({family(o) for o in d} & {'core','client','ai'}) == 3 and len(d) <= maximum and safe(p)]
    specs.append(('public-shared-header', *choose(three or public, 'public shared header')))
    return specs


def log_entries(build):
    result = {}
    for line in (build/'.ninja_log').read_text().splitlines():
        if line.startswith('#'): continue
        fields = line.split('\t')
        if len(fields) == 5: result[fields[3]] = fields
    return result


def changed_objects(before, after, objects):
    return {name for name, row in after.items() if name in objects and before.get(name) != row}


def binary_outputs(ninja, build):
    """Use Ninja's rule graph, including extensionless executable outputs."""
    text = subprocess.check_output([ninja, '-C', str(build), '-t', 'targets', 'all'],
                                   text=True, timeout=30)
    outputs = set()
    for line in text.splitlines():
        if ': ' not in line: continue
        name, rule = line.rsplit(': ', 1)
        if re.search(r'(?:^|_)(?:LINKER|ARCHIVER)(?:_|$)', rule):
            outputs.add(output_name(name, build))
    if not outputs: raise ValueError('No archive/link outputs found in Ninja graph')
    return outputs


def require_noop(before, after, objects, binaries, label):
    changed = changed_objects(before, after, set(objects) | binaries)
    if changed:
        raise RuntimeError(label + ': object/archive/link edges rebuilt: ' +
                           ', '.join(sorted(changed)))


def execute(command, cwd, logfile, seconds):
    if seconds <= 0: raise TimeoutError('Probe work budget exhausted')
    start = time.monotonic()
    with logfile.open('w') as stream:
        proc = subprocess.Popen(command, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
        try: status = proc.wait(timeout=seconds)
        except BaseException:
            os.killpg(proc.pid, signal.SIGTERM)
            try: proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL); proc.wait()
            raise
    if status: raise RuntimeError('Command failed; see ' + str(logfile))
    return time.monotonic() - start


def fresh_write(path, data):
    # Real filesystem mtime, not time.time_ns, matches Ninja v7's start clock.
    with path.open('wb') as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def ccache_stats(evidence, label):
    tool = shutil.which('ccache')
    if not tool: return {'available': False}
    output = subprocess.check_output([tool, '--print-stats'], text=True)
    (evidence/(label+'-ccache.tsv')).write_text(output)
    return {'available': True, 'file': label+'-ccache.tsv'}


def run_probes(source, build, evidence, ninja, seconds, cleanup_seconds, maximum, parallel):
    source, build, evidence = source.resolve(), build.resolve(), evidence.resolve()
    if not within(build, source): raise ValueError('Build must be inside source checkout')
    evidence.mkdir(parents=True, exist_ok=True)
    start = time.monotonic(); deadline = start + seconds
    objects = commands(build, source)
    binaries = binary_outputs(ninja, build)
    deptext = subprocess.check_output([ninja,'-C',str(build),'-t','deps'], text=True, timeout=30)
    deps = parse_deps(deptext, build, objects)
    specs = select_probes(source, build, objects, deps, maximum)
    plan = [{'probe': name,'path':str(path.relative_to(source)),'expected_objects':sorted(expected),'families':sorted({family(o) for o in expected})} for name,path,expected in specs]
    (evidence/'probe-plan.json').write_text(json.dumps(plan,indent=2)+'\n')
    report = {'status':'incomplete','plan':plan,'probes':[],'clean_sources':False,'gameplay_changes':False,
              'coverage_note':'Selected bounded shared header only; not every header or global-header fanout.',
              'noop_binary_outputs':sorted(binaries)}
    reportpath = evidence/'probe-report.json'
    def write_report(): reportpath.write_text(json.dumps(report,indent=2)+'\n')
    write_report()
    command = [ninja,'-C',str(build),'-j',str(parallel),'-d','explain','all']
    before = log_entries(build)
    execute(command, source, evidence/'initial-noop.log', min(60, deadline-time.monotonic()))
    require_noop(before, log_entries(build), objects, binaries, 'Tree was not warm before probes')
    originals = {}
    try:
        for index,(label,path,expected) in enumerate(specs):
            original=path.read_bytes();info=path.stat();originals[path]=(original,info.st_atime_ns,info.st_mtime_ns)
            row={'probe':label,'path':str(path.relative_to(source)),'source_sha256':hashlib.sha256(original).hexdigest(),'expected_objects':sorted(expected),'source_restored':False}
            report['probes'].append(row);write_report()
            try:
                before=log_entries(build);row['ccache_before']=ccache_stats(evidence,label+'-before')
                fresh_write(path,original+b'\nstatic_assert(true, "VCMI bounded incremental graph probe '+str(index).encode()+b'");\n')
                row['build_seconds']=execute(command,source,evidence/(label+'.log'),deadline-time.monotonic())
                actual=changed_objects(before,log_entries(build),objects)
                row['actual_objects']=sorted(actual);row['ccache_after']=ccache_stats(evidence,label+'-after');write_report()
                if actual != expected:
                    raise AssertionError(label+': unexpected/missing object edges: '+str({'extra':sorted(actual-expected),'missing':sorted(expected-actual)}))
            finally:
                # Original mtime alone would leave probe objects newer than source.
                # Rebuild original bytes with a fresh timestamp first.
                fresh_write(path,original)
                try:
                    row['cleanup_seconds']=execute(command,source,evidence/(label+'-restore.log'),cleanup_seconds)
                    os.utime(path,ns=(info.st_atime_ns,info.st_mtime_ns))
                    row['source_restored']=path.read_bytes()==original
                finally: write_report()
        finalbefore=log_entries(build)
        execute(command,source,evidence/'final-noop.log',min(60,deadline-time.monotonic()))
        require_noop(finalbefore, log_entries(build), objects, binaries, 'Final clean tree was not a no-op')
        report['clean_sources']=all(path.read_bytes()==data for path,(data,_,_) in originals.items())
        if not report['clean_sources']: raise RuntimeError('Source restoration failed')
        report['status']='passed'
    except BaseException as exc:
        report['error']=str(exc)
        # Restore source bytes even when cleanup build cannot finish. Leave
        # current mtime in that failure case, forcing a later safe rebuild.
        for path,(data,atime,mtime) in originals.items():
            if path.read_bytes()!=data: fresh_write(path,data)
        report['clean_sources']=all(path.read_bytes()==data for path,(data,_,_) in originals.items())
        raise
    finally:
        report['total_seconds']=time.monotonic()-start
        write_report()
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=pathlib.Path,required=True)
    parser.add_argument('--build',type=pathlib.Path,required=True)
    parser.add_argument('--evidence',type=pathlib.Path,required=True)
    parser.add_argument('--ninja',default='ninja')
    parser.add_argument('--seconds',type=int,default=600)
    parser.add_argument('--cleanup-seconds',type=int,default=120)
    parser.add_argument('--max-objects',type=int,default=32)
    parser.add_argument('--parallel',type=int,default=2)
    args=parser.parse_args()
    if not (1<=args.parallel<=2 and 1<=args.max_objects<=128 and 1<=args.seconds<=1200 and 1<=args.cleanup_seconds<=180): parser.error('Probe resource limits exceeded')
    def interrupted(signum, frame):
        raise RuntimeError('Probe interrupted by signal ' + str(signum))
    signal.signal(signal.SIGTERM, interrupted)
    try: print(json.dumps(run_probes(args.source,args.build,args.evidence,args.ninja,args.seconds,args.cleanup_seconds,args.max_objects,args.parallel),indent=2))
    except Exception as exc: parser.exit(1,'INCREMENTAL_PROBE_ERROR: '+str(exc)+'\n')
if __name__=='__main__':main()
