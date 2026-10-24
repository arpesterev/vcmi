#!/usr/bin/env python3
"""Optional warm-only three-turn defence interaction; no cold fallback or install."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
from adventure_bridge_qualification import DATA_HASH, digest, execute_cases, validate_restore
from adventure_qualification import OBJECT, run

TARGET = 'vcmi_adventure_interaction_reference'
CASES = ('AdventureInteractionScenarios.CoveredEconomyThenActualDefenceOnEnemyApproach',)
MAX_OBJECT_EDGES = 12
COMPILE_SECONDS = 180
ALLOWED = re.compile(r'^(?:server/CMakeFiles/vcmiservercommon(?:_battles|_queries|_processors)?\.dir/|test/CMakeFiles/vcmi_adventure_interaction_reference\.dir/)')
MAP_NAME = 'defence_economy_three_turns'
MAP_INPUTS = (
    'test/testdata/MiniTest/header.json', 'test/testdata/MiniTest/objects.json',
    'test/testdata/MiniTest/surface_terrain.json', 'test/testdata/ObjectPropertyTest/objects.json',
    'test/testdata/ObjectPropertyTest/surface_terrain.json', 'CI/adventure_eval/scenarios.py',
)
SOURCES = MAP_INPUTS + (
    'test/adventure/AdventureInteractionBridge.h', 'test/adventure/AdventureInteractionScenarios.cpp',
    'test/adventure/AdventureInteractionReference.cmake', 'test/adventure/AdventureInteractionScenarios.md',
    'test/adventure/generate_interaction_maps.py', 'test/adventure/test_interaction_maps.py',
    'test/CMakeLists.txt', 'CI/adventure_interaction_qualification.py',
    'CI/adventure_bridge_qualification.py', 'CI/adventure_qualification.py',
)


def validate_plan(text, limit=MAX_OBJECT_EDGES):
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
            raise ValueError('Unexpected interaction rebuild: ' + obj)
        objects.add(obj)
    if len(objects) > limit:
        raise ValueError(f'{len(objects)} new compile edges exceed budget {limit}')
    return sorted(objects)


def map_hashes(build, source):
    directory = build / 'bin/test/testdata/AdventureInteractions'
    provenance_path = directory / 'interaction-provenance.json'
    provenance = json.loads(provenance_path.read_text())
    hashes = {MAP_NAME: digest(directory / (MAP_NAME + '.vmap'))}
    if provenance.get('map_sha256') != hashes[MAP_NAME]:
        raise ValueError('Generated interaction map/provenance mismatch')
    if provenance.get('source_sha256') != {name: digest(source / name) for name in MAP_INPUTS}:
        raise ValueError('Interaction map provenance does not match current public sources')
    if provenance.get('seed') != 17041 or provenance.get('difficulty') != 3:
        raise ValueError('Interaction seed or difficulty changed')
    if provenance.get('blue_scripted_retreat_anchors') != [[45, 25, 0], [45, 40, 0]]:
        raise ValueError('Interaction retreat geometry changed')
    return hashes, provenance, digest(provenance_path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ('build', 'evidence', 'cache-validation', 'data-archive'):
        parser.add_argument('--' + arg, type=Path, required=True)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=False)
    result = {'status': 'incomplete', 'target': TARGET, 'cold_fallback': False,
              'scope': 'Three-turn production planner/transaction interaction; not active-opponent survival or full-game strength',
              'executed_tests': [], 'seed': 17041, 'difficulty': 3,
              'max_object_edges': MAX_OBJECT_EDGES, 'compile_seconds_limit': COMPILE_SECONDS,
              'case_seconds_limit': 180, 'total_execution_seconds_limit': 600}
    try:
        validate_restore(json.loads(args.cache_validation.read_text()))
        result['restore_evidence_sha256'] = digest(args.cache_validation)
        build = args.build.resolve()
        if not (build / 'build.ninja').is_file() or not (build / 'CMakeCache.txt').is_file():
            raise ValueError('Missing warm configured Ninja tree; cold fallback forbidden')
        if digest(args.data_archive) != DATA_HASH:
            raise ValueError('Public archive checksum mismatch')
        result['public_data_sha256'] = DATA_HASH
        source = Path(__file__).resolve().parent.parent
        result['source_commit'] = subprocess.check_output(
            ['git', 'rev-parse', 'HEAD'], cwd=source, text=True, timeout=10).strip()
        result['source_sha256'] = {name: digest(source / name) for name in SOURCES}
        result['runner_image'] = os.environ.get('ImageVersion')
        plan = subprocess.check_output(['ninja', '-C', str(build), '-n', TARGET],
                                       text=True, stderr=subprocess.STDOUT, timeout=30)
        (evidence / 'build-plan.txt').write_text(plan)
        result['planned_objects'] = validate_plan(plan)
        run(['python3', str(source / 'CI/incremental_launcher.py'), '--build', str(build),
             '--evidence', str(evidence), TARGET], evidence / 'launcher.log', 30)
        (evidence / 'ninja-before.log').write_bytes((build / '.ninja_log').read_bytes())
        result['compile_seconds'] = run(['cmake', '--build', str(build), '--target', TARGET,
            '--parallel', '2', '--', '-d', 'explain'], evidence / 'build.log', COMPILE_SECONDS)
        result['compiled_objects'] = validate_plan((evidence / 'build.log').read_text())
        (evidence / 'ninja-after.log').write_bytes((build / '.ninja_log').read_bytes())
        executable = build / 'bin' / TARGET
        result['binary_sha256'] = digest(executable)
        result['map_sha256'], provenance, result['map_provenance_sha256'] = map_hashes(build, source)
        (evidence / 'map-provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
        run(['cmake', '--build', str(build), '--target', TARGET, '--parallel', '2'], evidence / 'noop.log', 30)
        noop = (evidence / 'noop.log').read_text()
        if validate_plan(noop) or re.search(r'\[\d+/\d+\].*(?:Linking |Archiving )', noop):
            raise ValueError('Immediate interaction rebuild compiled or linked instead of no-op')
        hashes, _, provenance_hash = map_hashes(build, source)
        if digest(executable) != result['binary_sha256'] or hashes != result['map_sha256'] or provenance_hash != result['map_provenance_sha256']:
            raise ValueError('No-op changed binary, generated map or provenance')
        result['noop_verified'] = True
        result['case_results'], result['execution_seconds'] = execute_cases(executable, evidence, build / 'bin', cases=CASES)
        result['executed_tests'] = [case['test'] for case in result['case_results'] if case['status'] == 'completed']
        if len(result['executed_tests']) != len(CASES):
            raise RuntimeError('Required interaction failed or was incomplete; see case_results')
        result['status'] = 'completed'
    except Exception as error:
        result['reason'] = str(error)
        raise
    finally:
        (evidence / 'qualification.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
