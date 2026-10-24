#!/usr/bin/env python3
"""Independent bounded DEFEND mechanics gate; reuse a verified binary, never build."""
import argparse
import json
import math
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
from adventure_bridge_qualification import DATA_HASH, digest, validate_restore
from adventure_qualification import run

TARGET = 'vcmi_forced_battle_reference'
FILTER = 'ForcedDefendReference.*'
CASES = (
    'ForcedDefendReference.DefendNativeMinimumDefense',
    'ForcedDefendReference.DefendNativePercentageAndMorale',
    'ForcedDefendReference.DefendExplicitStanceMechanicsOnly',
)
EXECUTION_SECONDS = 60
REQUIRED_BUILD_SOURCES = (
    'test/battle/ForcedBattleReference.cpp',
    'AI/Nullkiller2/Engine/Experimental/DetachedDefendBonuses.h',
    'AI/Nullkiller2/Engine/Experimental/SampledBattleRollout.cpp',
)
SOURCES = (
    'test/battle/ForcedBattleReference.cpp', 'test/battle/ForcedDefendReference.md',
    'AI/Nullkiller2/Engine/Experimental/DetachedDefendBonuses.h',
    'AI/Nullkiller2/Engine/Experimental/SampledBattleRollout.cpp',
    'CI/adventure_defend_qualification.py', 'CI/adventure_qualification.py',
    'CI/adventure_bridge_qualification.py',
)


def validate_reference(reference, executable, source_commit, restore_hash, source):
    """A failed runtime may be reused; an incomplete build/no-op may not."""
    if reference.get('target') != TARGET or reference.get('cold_fallback') is not False:
        raise ValueError('Wrong or unverified reference target')
    if reference.get('source_commit') != source_commit:
        raise ValueError('Reference source commit does not match this checkout')
    if reference.get('restore_evidence_sha256') != restore_hash:
        raise ValueError('Reference used different strict restore evidence')
    if reference.get('public_data_sha256') != DATA_HASH:
        raise ValueError('Reference used different public data')
    seconds = reference.get('compile_seconds')
    if type(seconds) not in (int, float) or not math.isfinite(seconds) or seconds < 0:
        raise ValueError('Reference has no completed compilation evidence')
    if not isinstance(reference.get('planned_objects'), list) or not isinstance(reference.get('compiled_objects'), list):
        raise ValueError('Reference compile-edge validation is missing')
    if reference.get('noop_verified') is not True:
        raise ValueError('Reference immediate no-op was not verified')
    if not executable.is_file() or reference.get('executable_sha256') != digest(executable):
        raise ValueError('Reference binary is missing or changed after verified build/no-op')
    fixtures = reference.get('fixture_sha256')
    if not isinstance(fixtures, dict) or not set(REQUIRED_BUILD_SOURCES) <= set(fixtures):
        raise ValueError('Reference fixture identity is missing')
    for name, expected in fixtures.items():
        path = Path(name)
        if path.is_absolute() or '..' in path.parts or digest(source / path) != expected:
            raise ValueError('Reference source changed: ' + name)
    return reference['executable_sha256']


def validate_xml(path):
    root = ET.parse(path).getroot()
    cases = root.findall('.//testcase')
    names = [case.get('classname', '') + '.' + case.get('name', '') for case in cases]
    if len(names) != len(CASES) or set(names) != set(CASES) or any(
            case.get('status') != 'run' or case.get('result') not in (None, 'completed')
            or any(case.find(tag) is not None for tag in ('failure', 'error', 'skipped')) for case in cases):
        raise ValueError('DEFEND suite missing, duplicate, skipped or failed required cases')
    for element in root.iter():
        for field in ('failures', 'errors', 'disabled'):
            if field in element.attrib and int(element.attrib[field]) != 0:
                raise ValueError('Nonzero DEFEND XML ' + field)
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
              'scope': 'Native DEFEND mechanics controls only; not model calibration or opponent strength',
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
            raise ValueError('Reference binary changed during DEFEND qualification')
        result['status'] = 'completed'
    except Exception as error:
        result['reason'] = str(error)
        raise
    finally:
        (evidence / 'qualification.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
