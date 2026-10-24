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
import shutil
import subprocess
import time
import xml.etree.ElementTree as ET

OBJECT = re.compile(r'Building (?:CXX|C|ASM) (?:object|precompiled header)\s+(.+\.(?:o|obj|gch|pch))\s*$')
ALLOWED = re.compile(r'^(?:server/CMakeFiles/vcmiservercommon(?:_battles|_queries|_processors)?\.dir/|test/CMakeFiles/vcmi_forced_battle_reference\.dir/)')
# Required even before the decision model is eligible for production activation.
# Independent holdouts must bind the exact linked producer and deadline policy.
RISK_BUILD_SOURCES = (
    'AI/Nullkiller2/Engine/Experimental/PlanningRiskDecision.cpp',
    'AI/Nullkiller2/Engine/Experimental/PlanningRiskDecision.h',
    'AI/Nullkiller2/Engine/Experimental/DecisionRiskBudget.h',
    'AI/Nullkiller2/Engine/Experimental/PlanningRiskEvidence.h',
    'AI/Nullkiller2/Engine/Experimental/PlanningRiskTable.cpp',
    'AI/Nullkiller2/Engine/Experimental/PlanningRiskTable.h',
    'AI/Nullkiller2/Engine/Experimental/PlanningBattleSnapshot.h',
    'AI/Nullkiller2/Engine/Experimental/SampledBattleRollout.h',
    'AI/Nullkiller2/AIGateway.cpp', 'AI/Nullkiller2/AIGateway.h',
    'AI/Nullkiller2/Engine/Nullkiller.cpp', 'AI/Nullkiller2/Engine/Nullkiller.h',
    'AI/Nullkiller2/Engine/ArmyLossPolicy.h',
    'AI/Nullkiller2/Engine/Settings.cpp', 'AI/Nullkiller2/Engine/Settings.h',
    'config/ai/nk2ai/nk2ai-settings.json',
)


def validate_plan(text, limit):
    objects = set()
    for line in text.splitlines():
        if 'Building ' not in line:
            continue
        match = OBJECT.search(line)
        if not match:
            raise ValueError('Unrecognized compile edge: ' + line)
        objects.add(match.group(1))
    unexpected = sorted(obj for obj in objects if obj != 'lib/CMakeFiles/vcmi.dir/__/Version.cpp.o' and (not ALLOWED.match(obj) or '..' in Path(obj).parts))
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



def last_started_reference_case(log):
    from forced_battle_evidence import EXPECTED_TEST_NAMES
    names = re.findall(r'^\[ RUN      \] ([^\r\n]+)$', log, re.MULTILINE)
    if not names or names[-1] not in EXPECTED_TEST_NAMES:
        return None
    return names[-1]


def capture_first_case_backtrace(executable, evidence, cwd, selected_case='ForcedBattleReference.OrdinaryMelee'):
    """Diagnostic only: no debugger outcome can validate the failed suite."""
    debugger = shutil.which('lldb-18') or shutil.which('lldb') or shutil.which('gdb')
    result = {'attempted': False, 'scenario_passed': False, 'seconds_limit': 30}
    if not debugger:
        result['reason'] = 'No existing debugger; no packages were installed'
        return result
    from forced_battle_evidence import EXPECTED_TEST_NAMES
    if selected_case not in EXPECTED_TEST_NAMES:
        result['reason'] = 'No declared interrupted reference test found; no diagnostic launched'
        return result
    result['selected_case'] = selected_case
    case = '--gtest_filter=' + selected_case
    if Path(debugger).name.startswith('lldb'):
        command = [debugger, '--no-lldbinit', '--batch', '-o', 'settings set target.load-script-from-symbol-file false',
                   '-o', 'settings set target.disable-aslr false',
                   '-o', 'run', '-k', 'thread backtrace all', '--', str(executable), case]
    else:
        command = [debugger, '--nx', '--nh', '--batch', '-iex', 'set auto-load off',
                   '-ex', 'set disable-randomization off',
                   '-ex', 'run', '-ex', 'thread apply all bt', '--args', str(executable), case]
    result.update(attempted=True, debugger=debugger, log='crash-backtrace.log')
    try:
        started = time.monotonic()
        startup_flags = ['--no-lldbinit'] if Path(debugger).name.startswith('lldb') else ['--nx', '--nh']
        run([debugger, *startup_flags, '--help'], evidence / 'debugger-help.log', 5, cwd)
        remaining = max(0.1, 30 - (time.monotonic() - started))
        result['seconds'] = run(command, evidence / 'crash-backtrace.log', remaining, cwd)
        result['reason'] = 'Diagnostic command completed; original suite failure remains'
    except Exception as error:
        result['reason'] = str(error)
    return result


def record_battle_evidence(result, log):
    from forced_battle_evidence import summarize_evidence
    summary = summarize_evidence(log, require_smokes=False)
    result['battle_evidence'] = summary
    if summary['supported_smoke_failures']:
        raise RuntimeError('Predeclared supported smoke incomplete: ' + ', '.join(summary['supported_smoke_failures']))


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
                     'AI/Nullkiller2/Engine/Experimental/DetachedDefendBonuses.h',
                     'test/battle/PlanningBattleSnapshotChecks.h',
                     'AI/Nullkiller2/Engine/Experimental/PlanningBattleSnapshot.cpp',
                     'AI/Nullkiller2/Engine/Experimental/SampledBattleRollout.cpp',
                     'test/testdata/MiniTest/header.json', 'test/testdata/MiniTest/objects.json',
                     'test/testdata/MiniTest/surface_terrain.json', *RISK_BUILD_SOURCES]
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=source, text=True, timeout=10).strip()
    result = {'status': 'incomplete', 'target': target, 'cold_fallback': False,
              'source_commit': commit, 'baseline_commit': '8fda7bdc69887e8c86d27a149681fc598c3cd8d1',
              'runner_image': os.environ.get('ImageVersion'), 'public_data_sha256': archive_digest,
              'restore_evidence_sha256': hashlib.sha256(args.cache_validation.read_bytes()).hexdigest(),
              'fixture_sha256': {name: hashlib.sha256((source / name).read_bytes()).hexdigest() for name in fixture_paths}}
    spec = importlib.util.spec_from_file_location('adventure_asset_inventory', Path(__file__).parent / 'adventure_eval/asset_inventory.py')
    assets = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(assets)
    inventory = assets.inspect([args.resource_root])
    inventory.update(graphical_launch_attempted=False,
                     qualification_status='incomplete' if inventory['preflight_passed'] else 'blocked_render_assets')
    (evidence / 'asset-inventory.json').write_text(json.dumps(inventory, indent=2) + '\n')
    result['graphical_smoke'] = inventory['qualification_status']
    reference_started = False
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
        result['compiled_objects'] = validate_plan((evidence / 'build.log').read_text(), config['reference_max_object_edges'])
        (evidence / 'ninja-after.log').write_bytes((build / '.ninja_log').read_bytes())
        result['executable_sha256'] = hashlib.sha256((build / 'bin' / target).read_bytes()).hexdigest()
        run(['cmake', '--build', str(build), '--target', target, '--parallel', '2'], evidence / 'noop.log', 30)
        noop = (evidence / 'noop.log').read_text()
        if validate_plan(noop, config['reference_max_object_edges']) or re.search(r'\[\d+/\d+\].*(?:Linking |Archiving )', noop):
            raise RuntimeError('Immediate reference rebuild was not a no-op')
        if hashlib.sha256((build / 'bin' / target).read_bytes()).hexdigest() != result['executable_sha256']:
            raise RuntimeError('Immediate reference rebuild changed the executable')
        # A later independent mechanics gate may reuse this binary even when the
        # model smoke fails, but never after a partial build or failed no-op.
        result['noop_verified'] = True
        reference_started = True
        result['execution_seconds'] = run([str(build / 'bin' / target), '--gmock_verbose=error',
                                           '--gtest_filter=' + config['reference_filter'], '--gtest_output=xml:' + str(evidence / 'tests.xml')],
                                          evidence / 'tests.log', config['reference_execution_seconds'], build / 'bin')
        cases = ET.parse(evidence / 'tests.xml').getroot().findall('.//testcase')
        names = {case.attrib['classname'] + '.' + case.attrib['name'] for case in cases}
        if set(config['reference_required_tests']) != names or len(cases) != len(names) or any(
                case.find('failure') is not None or case.find('skipped') is not None
                or case.attrib.get('status') != 'run' for case in cases):
            raise RuntimeError('Reference suite missing, skipped, or failed')
        result['executed_tests'] = sorted(names)
        record_battle_evidence(result, (evidence / 'tests.log').read_text())
        result.update(status='completed',
                      qualification_scope='Fixed-policy server references, not calibrated adventure strength or production integration')

    except Exception as error:
        result['reason'] = str(error)
        if reference_started and re.search(r'returncode=-(?:11|6),', str(error)):
            result['crash_diagnostic'] = capture_first_case_backtrace(build / 'bin' / target, evidence, build / 'bin',
                last_started_reference_case((evidence / 'tests.log').read_text()))
        raise
    finally:
        (evidence / 'qualification.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
