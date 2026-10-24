#!/usr/bin/env python3
"""Independent bounded movement controls; reuse the verified policy-v2 binary, never build."""
import argparse
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
from adventure_bridge_qualification import DATA_HASH, digest, validate_restore
from adventure_qualification import run
from adventure_defend_qualification import validate_reference as validate_built_reference

TARGET = 'vcmi_forced_battle_reference'
FILTER = 'ForcedMovementReference.*'
CASES = (
    'ForcedMovementReference.NativeObstacleDetourProgress',
    'ForcedMovementReference.NativeDistantFlyerAdvances',
    'ForcedMovementReference.NativeDoubleWideAdvanceLegal',
)
EXECUTION_SECONDS = 60
REQUIRED_BUILD_SOURCES = (
    'test/battle/ForcedBattleReference.cpp',
    'AI/Nullkiller2/Engine/Experimental/PlanningBattleMovement.h',
    'AI/Nullkiller2/Engine/Experimental/DetachedDefendBonuses.h',
    'AI/Nullkiller2/Engine/Experimental/SampledBattleRollout.cpp',
    'AI/Nullkiller2/Engine/Experimental/SampledBattleRollout.h',
)
SOURCES = REQUIRED_BUILD_SOURCES + (
    'CI/adventure_movement_qualification.py', 'CI/adventure_defend_qualification.py',
    'CI/adventure_qualification.py', 'CI/adventure_bridge_qualification.py',
)


def validate_reference(reference, executable, source_commit, restore_hash, source):
    fixtures = reference.get('fixture_sha256')
    if not isinstance(fixtures, dict) or not set(REQUIRED_BUILD_SOURCES) <= set(fixtures):
        raise ValueError('Movement reference fixture identity is missing')
    return validate_built_reference(reference, executable, source_commit, restore_hash, source)


def validate_xml(path):
    root = ET.parse(path).getroot()
    cases = root.findall('.//testcase')
    names = [case.get('classname', '') + '.' + case.get('name', '') for case in cases]
    if len(names) != len(CASES) or set(names) != set(CASES) or any(
            case.get('status') != 'run' or case.get('result') not in (None, 'completed')
            or any(case.find(tag) is not None for tag in ('failure', 'error', 'skipped')) for case in cases):
        raise ValueError('Movement suite missing, duplicate, skipped or failed required cases')
    for element in root.iter():
        for field in ('failures', 'errors', 'disabled'):
            if field in element.attrib and int(element.attrib[field]) != 0:
                raise ValueError('Nonzero movement XML ' + field)
    return sorted(names)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ('build', 'evidence', 'reference-evidence', 'cache-validation', 'data-archive'):
        parser.add_argument('--' + arg, type=Path, required=True)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=False)
    result = {'status': 'incomplete', 'target': TARGET, 'filter': FILTER,
              'cold_fallback': False, 'build_attempted': False,
              'scope': 'Native fixed-policy movement controls; not model calibration or activation approval',
              'policy_version': 2, 'activation_approved': False,
              'executed_tests': [], 'execution_seconds_limit': EXECUTION_SECONDS}
    try:
        validate_restore(json.loads(args.cache_validation.read_text()))
        result['restore_evidence_sha256'] = digest(args.cache_validation)
        if digest(args.data_archive) != DATA_HASH:
            raise ValueError('Public archive checksum mismatch')
        result['public_data_sha256'] = DATA_HASH
        source = Path(__file__).resolve().parent.parent
        result['source_commit'] = subprocess.check_output(
            ['git', 'rev-parse', 'HEAD'], cwd=source, text=True, timeout=10).strip()
        result['source_sha256'] = {name: digest(source / name) for name in SOURCES}
        reference_file = args.reference_evidence / 'qualification.json'
        reference = json.loads(reference_file.read_text())
        result['model_reference_evidence_sha256'] = digest(reference_file)
        result['model_reference_status'] = reference.get('status')
        result['model_reference_reason'] = reference.get('reason')
        build = args.build.resolve()
        executable = build / 'bin' / TARGET
        result['binary_sha256'] = validate_reference(reference, executable, result['source_commit'],
                                                     result['restore_evidence_sha256'], source)
        result['execution_seconds'] = run([str(executable), '--gmock_verbose=error',
            '--gtest_filter=' + FILTER, '--gtest_output=xml:' + str(evidence / 'tests.xml')],
            evidence / 'tests.log', EXECUTION_SECONDS, build / 'bin')
        result['executed_tests'] = validate_xml(evidence / 'tests.xml')
        if digest(executable) != result['binary_sha256']:
            raise ValueError('Reference binary changed during movement qualification')
        result['status'] = 'completed'
    except Exception as error:
        result['reason'] = str(error)
        raise
    finally:
        (evidence / 'qualification.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
