#!/usr/bin/env python3
"""Warm-only, bounded production planner-turn integration; not a full game."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
import xml.etree.ElementTree as ET
from adventure_qualification import run

TARGET = 'vcmi_adventure_scenario_reference'
DATA_HASH = '64247aeee0342c6f349576ba700a81e2de46de7b9aa19ca4881b8d59f5e0ced8'
CASES = tuple('AdventureScenarioBridge.' + name for name in (
    'RealCallbackMoveConsumesMovementAndChangesPosition', 'ProductionPlannerCapturesReachableMine',
    'StableVisibleGoalsMakeMultiTurnProgress', 'GuardedObjectiveDoesNotPreventSafeAlternative'))
OBJECT = re.compile(r'Building (?:CXX|C|ASM) (?:object|precompiled header)\s+(.+\.(?:o|obj|gch|pch))\s*$')
ALLOWED = re.compile(r'^(?:server/CMakeFiles/vcmiservercommon(?:_battles|_queries|_processors)?\.dir/|test/CMakeFiles/vcmi_adventure_scenario_reference\.dir/)')
SOURCES = ('test/adventure/AdventureScenarioBridge.cpp', 'test/adventure/AdventureScenarioReference.cmake',
           'test/adventure/generate_maps.py', 'CI/adventure_eval/run.py', 'CI/adventure_eval/scenarios.py',
           'CI/adventure_bridge_qualification.py', 'CI/adventure_qualification.py',
           'test/testdata/MiniTest/header.json', 'test/testdata/MiniTest/objects.json',
           'test/testdata/MiniTest/surface_terrain.json', 'test/testdata/ObjectPropertyTest/objects.json',
           'test/testdata/ObjectPropertyTest/surface_terrain.json')
MAPS = ('productive_move', 'stable_goal_progress', 'blocked_goal_alternative')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_restore(restored):
    if restored.get('operation') != 'restore':
        raise ValueError('Strict warm-cache restore is required; cold fallback forbidden')
    for name in ('unchanged_sources', 'verified_build_files', 'verified_external_files'):
        value = restored.get(name)
        if type(value) is not int or value < (1 if name != 'verified_external_files' else 0):
            raise ValueError('Missing/invalid strict restore evidence: ' + name)
    if not isinstance(restored.get('changed_sources'), list) or not isinstance(restored.get('removed_sources'), list):
        raise ValueError('Missing source reconciliation in restore evidence')


def validate_plan(text, limit=12):
    objects = set()
    for line in text.splitlines():
        if 'Building ' not in line:
            continue
        match = OBJECT.search(line)
        if not match:
            raise ValueError('Unrecognized compile edge: ' + line)
        obj = match.group(1)
        exact_version = obj == 'lib/CMakeFiles/vcmi.dir/__/Version.cpp.o'
        if not exact_version and (not ALLOWED.match(obj) or '..' in Path(obj).parts):
            raise ValueError('Unexpected rebuild: ' + obj)
        objects.add(obj)
    if len(objects) > limit:
        raise ValueError(f'{len(objects)} new compile edges exceed budget {limit}')
    return sorted(objects)


def validate_xml(path, expected):
    root = ET.parse(path).getroot()
    cases = root.findall('.//testcase')
    names = [case.get('classname', '') + '.' + case.get('name', '') for case in cases]
    if names != [expected] or any(case.find('failure') is not None or case.find('error') is not None
                                or case.find('skipped') is not None or case.get('status') != 'run'
                                or case.get('result') not in (None, 'completed') for case in cases):
        raise ValueError('Missing, duplicate, skipped or failed required test: ' + expected)
    for element in root.iter():
        for field in ('failures', 'errors', 'disabled'):
            if field in element.attrib and int(element.attrib[field]) != 0:
                raise ValueError('Nonzero XML ' + field)
    return expected


def map_hashes(build):
    directory = build / 'bin/test/testdata/AdventureEvaluation'
    provenance = json.loads((directory / 'provenance.json').read_text())
    hashes = {name: digest(directory / (name + '.vmap')) for name in MAPS}
    if any(provenance[name]['map_sha256'] != value for name, value in hashes.items()):
        raise ValueError('Generated map/provenance mismatch')
    return hashes, provenance


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ('build', 'evidence', 'cache-validation', 'data-archive'):
        parser.add_argument('--' + arg, type=Path, required=True)
    parser.add_argument('--resource-root', type=Path)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=False)
    result = {'status': 'incomplete', 'target': TARGET, 'cold_fallback': False,
              'scope': 'Production planner-turn integration; not full client-server games or measured opponent quality',
              'executed_tests': [], 'seed': 17041, 'difficulty': 3}
    try:
        restored = json.loads(args.cache_validation.read_text())
        validate_restore(restored)
        result['restore_evidence_sha256'] = digest(args.cache_validation)
        build = args.build.resolve()
        if not (build / 'build.ninja').is_file() or not (build / 'CMakeCache.txt').is_file():
            raise ValueError('Missing warm configured Ninja tree')
        if (args.resource_root is not None and not args.resource_root.is_dir()) or digest(args.data_archive) != DATA_HASH:
            raise ValueError('Missing public resource root or public archive checksum mismatch')
        result['public_data_sha256'] = DATA_HASH
        source = Path(__file__).resolve().parent.parent
        result['source_commit'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=source, text=True, timeout=10).strip()
        result['source_sha256'] = {name: digest(source / name) for name in SOURCES}
        result['runner_image'] = os.environ.get('ImageVersion')
        plan = subprocess.check_output(['ninja', '-C', str(build), '-n', TARGET], text=True, stderr=subprocess.STDOUT, timeout=30)
        (evidence / 'build-plan.txt').write_text(plan)
        result['planned_objects'] = validate_plan(plan)
        run(['python3', str(source / 'CI/incremental_launcher.py'), '--build', str(build),
             '--evidence', str(evidence), TARGET], evidence / 'launcher.log', 30)
        (evidence / 'ninja-before.log').write_bytes((build / '.ninja_log').read_bytes())
        result['compile_seconds'] = run(['cmake', '--build', str(build), '--target', TARGET, '--parallel', '2', '--', '-d', 'explain'], evidence / 'build.log', 180)
        result['compiled_objects'] = validate_plan((evidence / 'build.log').read_text())
        (evidence / 'ninja-after.log').write_bytes((build / '.ninja_log').read_bytes())
        executable = build / 'bin' / TARGET
        result['binary_sha256'] = digest(executable)
        result['map_sha256'], provenance = map_hashes(build)
        (evidence / 'map-provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
        run(['cmake', '--build', str(build), '--target', TARGET, '--parallel', '2'], evidence / 'noop.log', 30)
        noop = (evidence / 'noop.log').read_text()
        if validate_plan(noop) or re.search(r'\[\d+/\d+\].*(?:Linking |Archiving )', noop):
            raise ValueError('Immediate rebuild compiled or linked instead of no-op')
        if digest(executable) != result['binary_sha256'] or map_hashes(build)[0] != result['map_sha256']:
            raise ValueError('No-op changed binary or generated maps')
        result['noop_verified'] = True
        started = time.monotonic()
        for index, case in enumerate(CASES):
            remaining = 600 - (time.monotonic() - started)
            if remaining <= 0:
                raise RuntimeError('Total execution budget exhausted')
            xml = evidence / f'test-{index}.xml'
            run([str(executable), '--gmock_verbose=error', '--gtest_filter=' + case,
                 '--gtest_output=xml:' + str(xml)], evidence / f'test-{index}.log', min(180, remaining), build / 'bin')
            result['executed_tests'].append(validate_xml(xml, case))
        result.update(status='completed', execution_seconds=time.monotonic() - started)
    except Exception as error:
        result['reason'] = str(error)
        raise
    finally:
        (evidence / 'qualification.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
