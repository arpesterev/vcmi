#!/usr/bin/env python3
"""Bounded explicit server-reference target on an already verified warm tree."""
import argparse
import hashlib
import importlib.util
from collections import Counter
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time
import xml.etree.ElementTree as ET

OBJECT = re.compile(r'Building (?:CXX|C|ASM) object\s+(.+\.(?:o|obj))\s*$')
ALLOWED = re.compile(r'^(?:server/CMakeFiles/vcmiservercommon(?:_battles|_queries|_processors)?\.dir/|test/CMakeFiles/vcmi_forced_battle_reference\.dir/)')


def validate_plan(text, limit):
    objects = {match.group(1) for line in text.splitlines() if (match := OBJECT.search(line))}
    unexpected = sorted(obj for obj in objects if not ALLOWED.match(obj) and obj != 'lib/CMakeFiles/vcmi.dir/__/Version.cpp.o')
    if unexpected:
        raise ValueError('Reference target unexpectedly rebuilds existing core/AI/client objects: ' + repr(unexpected))
    if len(objects) > limit:
        raise ValueError(f'Reference target needs {len(objects)} objects, budget is {limit}')
    return sorted(objects)


def run(command, logfile, seconds, cwd=None):
    started = time.monotonic()
    process = subprocess.Popen(command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, start_new_session=True)
    timed_out = False
    try:
        output, _ = process.communicate(timeout=seconds)
    except subprocess.TimeoutExpired:
        timed_out = True
        os.killpg(process.pid, signal.SIGTERM)
        try:
            output, _ = process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            output, _ = process.communicate()
    logfile.write_text(output)
    print(output, end='')
    if timed_out or process.returncode:
        raise RuntimeError(f'{command[0]} failed: returncode={process.returncode}, timeout={timed_out}; see {logfile}')
    return time.monotonic() - started


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--cache-validation', type=Path, required=True)
    parser.add_argument('--data-archive', type=Path, required=True)
    parser.add_argument('--resource-root', type=Path, required=True)
    args = parser.parse_args()
    config = json.loads((Path(__file__).with_name('adventure-qualification.json')).read_text())
    restored = json.loads(args.cache_validation.read_text())
    if restored.get('operation') != 'restore':
        raise SystemExit('Reference qualification requires successful strict warm-cache restoration')
    build = args.build.resolve()
    evidence = args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=False)
    target = config['reference_target']
    if not (build / 'build.ninja').is_file() or not (build / 'CMakeCache.txt').is_file():
        raise SystemExit('Missing warm Ninja build; cold bootstrap is forbidden')
    archive_digest = hashlib.sha256(args.data_archive.read_bytes()).hexdigest()
    if archive_digest != '64247aeee0342c6f349576ba700a81e2de46de7b9aa19ca4881b8d59f5e0ced8':
        raise SystemExit('Public test-data archive checksum mismatch')
    source = Path(__file__).resolve().parent.parent
    fixture_paths = ['test/battle/ForcedBattleReference.cpp', 'AI/Nullkiller2/Engine/Experimental/DetachedDuelRollout.cpp',
                     'AI/Nullkiller2/Engine/Experimental/DetachedDuelRollout.h',
                     'test/testdata/MiniTest/header.json', 'test/testdata/MiniTest/objects.json',
                     'test/testdata/MiniTest/surface_terrain.json']
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=source, text=True, timeout=10).strip()
    result = {'status': 'incomplete', 'target': target, 'cold_fallback': False,
              'source_commit': commit, 'baseline_commit': '8fda7bdc69887e8c86d27a149681fc598c3cd8d1',
              'runner_image': os.environ.get('ImageVersion'), 'public_data_sha256': archive_digest,
              'fixture_sha256': {name: hashlib.sha256((source / name).read_bytes()).hexdigest() for name in fixture_paths}}
    spec = importlib.util.spec_from_file_location('adventure_asset_inventory', Path(__file__).parent / 'adventure_eval/asset_inventory.py')
    assets = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(assets)
    inventory = assets.inspect([args.resource_root])
    inventory.update(graphical_launch_attempted=False,
                     qualification_status='incomplete' if inventory['preflight_passed'] else 'blocked_render_assets')
    (evidence / 'asset-inventory.json').write_text(json.dumps(inventory, indent=2) + '\n')
    result['graphical_smoke'] = inventory['qualification_status']
    try:
        plan = subprocess.check_output(['ninja', '-C', str(build), '-n', target], text=True, stderr=subprocess.STDOUT, timeout=30)
        (evidence / 'build-plan.txt').write_text(plan)
        result['planned_objects'] = validate_plan(plan, config['reference_max_object_edges'])
        run(['python3', str(Path(__file__).with_name('incremental_launcher.py').resolve()),
             '--build', str(build), '--evidence', str(evidence), target], evidence / 'launcher.log', 30)
        if (build / '.ninja_log').exists():
            (evidence / 'ninja-before.log').write_bytes((build / '.ninja_log').read_bytes())
        result['compile_seconds'] = run(['cmake', '--build', str(build), '--target', target, '--parallel', '2', '--', '-d', 'explain'],
                                        evidence / 'build.log', config['reference_compile_seconds'])
        (evidence / 'ninja-after.log').write_bytes((build / '.ninja_log').read_bytes())
        result['executable_sha256'] = hashlib.sha256((build / 'bin' / target).read_bytes()).hexdigest()
        run(['cmake', '--build', str(build), '--target', target, '--parallel', '2'], evidence / 'noop.log', 30)
        noop = (evidence / 'noop.log').read_text()
        if re.search(r'\[\d+/\d+\].*(?:Building .* object|Linking |Archiving )', noop):
            raise RuntimeError('Immediate reference rebuild was not a no-op')
        result['execution_seconds'] = run([str(build / 'bin' / target), '--gmock_verbose=error',
                                           '--gtest_filter=ForcedBattleReference.*', '--gtest_output=xml:' + str(evidence / 'tests.xml')],
                                          evidence / 'tests.log', config['reference_execution_seconds'], build / 'bin')
        cases = ET.parse(evidence / 'tests.xml').getroot().findall('.//testcase')
        names = {case.attrib['classname'] + '.' + case.attrib['name'] for case in cases}
        if not set(config['reference_required_tests']).issubset(names) or any(
                case.find('failure') is not None or case.find('skipped') is not None
                or case.attrib.get('status') != 'run' for case in cases):
            raise RuntimeError('Reference suite missing, skipped, or failed')
        reference_rows = [line for line in (evidence / 'tests.log').read_text().splitlines()
                          if line.startswith('FORCED_BATTLE_REFERENCE ')]
        counts = Counter(int(value) for row in reference_rows for value in re.findall(r'forecast_status=(\d+)', row))
        agreements = Counter(int(value) for row in reference_rows for value in re.findall(r'forecast_winner_agrees=(\d+)', row))
        if len(reference_rows) != len(config['reference_required_tests']) or sum(counts.values()) != 2 * len(reference_rows) or set(counts) - {0, 1, 2} or sum(agreements.values()) != counts[2]:
            raise RuntimeError('Reference/forecast evidence rows are incomplete or inconsistent')
        result.update(status='completed', executed_tests=sorted(names), reference_encounters=len(reference_rows),
                      forecast_support={'unsupported': counts[0], 'budget_exhausted': counts[1], 'finished': counts[2]},
                      finished_forecast_winner_agreement={'agreed': agreements[1], 'disagreed': agreements[0]},
                      qualification_scope='Fixed-policy server references, not calibrated adventure strength or production integration')
    except Exception as error:
        result['reason'] = str(error)
        raise
    finally:
        (evidence / 'qualification.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
