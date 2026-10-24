import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch
import adventure_path_risk_qualification as qualification
from adventure_bridge_qualification import execute_cases, validate_xml

ROOT = Path(__file__).resolve().parents[1]


def generate_maps(source, build):
    directory = build / 'bin/test/testdata/AdventureEvaluation'
    # Python-only public map generation, never configure or compile native code.
    for script in ('generate_maps.py', 'generate_path_risk_map.py'):
        subprocess.run([sys.executable, str(source / 'test/adventure' / script), '--repo', str(source),
                        '--output', str(directory)], check=True, capture_output=True, text=True, timeout=30)
    return directory


def case_xml(name):
    suite, test = name.split('.')
    return f'<testsuites failures="0"><testsuite><testcase classname="{suite}" name="{test}" status="run" result="completed"/></testsuite></testsuites>'


class PathRiskBudgetTest(unittest.TestCase):
    def test_only_selected_target_server_and_exact_version_compile_edges(self):
        for path in ('test/CMakeFiles/vcmi_path_risk_reference.dir/adventure/AdventurePathRiskReference.cpp.o',
                     'test/CMakeFiles/vcmi_path_risk_reference.dir/cmake_pch.hxx.gch',
                     'server/CMakeFiles/vcmiservercommon_processors.dir/processors/TurnOrderProcessor.cpp.o',
                     'lib/CMakeFiles/vcmi.dir/__/Version.cpp.o'):
            self.assertEqual(qualification.validate_plan('Building CXX object ' + path), [path])
        for path in ('test/CMakeFiles/vcmi_adventure_scenario_reference.dir/main.cpp.o',
                     'test/CMakeFiles/vcmi_adventure_interaction_reference.dir/main.cpp.o',
                     'test/CMakeFiles/vcmi_forced_battle_reference.dir/main.cpp.o',
                     'AI/Nullkiller2/CMakeFiles/Nullkiller2.dir/AIGateway.cpp.o',
                     'lib/CMakeFiles/vcmi.dir/__/Other.cpp.o',
                     'server/CMakeFiles/vcmiservercommon.dir/../../evil.o'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                qualification.validate_plan('Building CXX object ' + path)

    def test_twelve_edge_cap_counts_pch_and_rejects_unknown_compile(self):
        lines = [f'Building CXX object test/CMakeFiles/vcmi_path_risk_reference.dir/{i}.o' for i in range(11)]
        lines.append('Building CXX precompiled header test/CMakeFiles/vcmi_path_risk_reference.dir/cmake_pch.hxx.gch')
        self.assertEqual(len(qualification.validate_plan('\n'.join(lines))), 12)
        with self.assertRaises(ValueError):
            qualification.validate_plan('\n'.join(lines + ['Building CXX object server/CMakeFiles/vcmiservercommon.dir/extra.o']))
        with self.assertRaises(ValueError): qualification.validate_plan('Building unrecognized source')
        self.assertEqual(qualification.validate_plan('ninja: no work to do.'), [])

    def test_each_case_has_own_process_xml_log_and_180_seconds_within_360(self):
        calls = []
        def run(command, logfile, seconds, cwd):
            case = next(arg.split('=', 1)[1] for arg in command if arg.startswith('--gtest_filter='))
            calls.append((command, logfile, seconds))
            xml = Path(next(arg.removeprefix('--gtest_output=xml:') for arg in command if arg.startswith('--gtest_output=')))
            xml.write_text(case_xml(case))
            return 0.1
        with tempfile.TemporaryDirectory() as temporary:
            results, _ = execute_cases(Path('/unused'), Path(temporary), Path(temporary), run_case=run,
                clock=Mock(side_effect=[0, 0, 181, 359]), cases=qualification.CASES, total_seconds=360)
        self.assertEqual([call[2] for call in calls], [180, 179])
        self.assertEqual([call[1].name for call in calls], ['test-0.log', 'test-1.log'])
        self.assertEqual([record['test'] for record in results], list(qualification.CASES))
        self.assertTrue(all(record['status'] == 'completed' for record in results))

    def test_failure_does_not_hide_second_case_but_total_timeout_stops_it(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            run = Mock(side_effect=RuntimeError('timeout=True'))
            results, _ = execute_cases(Path('/unused'), directory, directory, run_case=run,
                clock=Mock(side_effect=[0, 0, 100, 110]), cases=qualification.CASES, total_seconds=360)
            self.assertEqual(run.call_count, 2)
            self.assertTrue(all(record['status'] == 'incomplete' for record in results))
            run.reset_mock()
            results, _ = execute_cases(Path('/unused'), directory, directory, run_case=run,
                clock=Mock(side_effect=[0, 0, 361, 361]), cases=qualification.CASES, total_seconds=360)
            self.assertEqual(run.call_count, 1)
            self.assertFalse(results[1]['attempted'])
            for invalid in (0, -1, 601, True, 360.0):
                with self.assertRaises(ValueError):
                    execute_cases(Path('/unused'), directory, directory, total_seconds=invalid)

    def test_exact_case_xml_rejects_sibling_and_duplicates(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'test.xml'
            for xml in (case_xml(qualification.CASES[1]), '<testsuites/>',
                        case_xml(qualification.CASES[0]).replace('result="completed"', 'result="skipped"'),
                        case_xml(qualification.CASES[0]).replace('/>', '><error/></testcase>')):
                path.write_text(xml)
                with self.assertRaises(ValueError): validate_xml(path, qualification.CASES[0])

    def test_actual_public_maps_deterministic_and_source_bound(self):
        with tempfile.TemporaryDirectory() as temporary:
            first, second = Path(temporary) / 'first', Path(temporary) / 'second'
            directory = generate_maps(ROOT, first)
            generate_maps(ROOT, second)
            self.assertEqual(qualification.map_hashes(first, ROOT), qualification.map_hashes(second, ROOT))
            hashes, _, _ = qualification.map_hashes(first, ROOT)
            self.assertIn('blocked_goal_alternative', hashes)
            self.assertIn('risk_earlier_guard', hashes)
            provenance_path = directory / 'provenance.json'
            provenance = json.loads(provenance_path.read_text())
            provenance['blocked_goal_alternative']['source_sha256']['header.json'] = 'wrong'
            provenance_path.write_text(json.dumps(provenance))
            with self.assertRaisesRegex(ValueError, 'current public sources'):
                qualification.map_hashes(first, ROOT)


class PathRiskRunnerTest(unittest.TestCase):
    def exercise(self, mode):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, build, evidence = (root / name for name in ('source', 'build', 'evidence'))
            # Missing not-yet-frozen model files are synthetic provenance inputs
            # here only. The real runner never skips or substitutes a source.
            for name in qualification.SOURCES:
                target = source / name
                target.parent.mkdir(parents=True, exist_ok=True)
                if (ROOT / name).is_file(): shutil.copyfile(ROOT / name, target)
                else: target.write_text('isolated provenance input ' + name)
            (build / 'bin').mkdir(parents=True)
            for name in ('build.ninja', 'CMakeCache.txt', '.ninja_log'): (build / name).write_text('warm tree')
            restore = root / 'restore.json'
            restore.write_text(json.dumps(dict(operation='restore', unchanged_sources=1,
                verified_build_files=1, verified_external_files=0, changed_sources=[], removed_sources=[])))
            archive = root / 'data.7z'
            archive.write_bytes(b'synthetic test data archive')
            if mode == 'cold': (build / 'build.ninja').unlink()
            if mode == 'invalid-restore': restore.write_text('{"operation":"restore"}')
            if mode == 'missing-model': (source / 'AI/Nullkiller2/Engine/Experimental/PlanningRiskDecision.cpp').unlink()
            plan = 'Building CXX object test/CMakeFiles/vcmi_path_risk_reference.dir/main.cpp.o'
            if mode == 'unrelated-build': plan = 'Building CXX object AI/Nullkiller2/CMakeFiles/Nullkiller2.dir/AIGateway.cpp.o'
            commands, case_calls = [], []

            def check_output(command, **kwargs):
                if command == ['git', 'rev-parse', 'HEAD']: return 'commit'
                self.assertEqual(command, ['ninja', '-C', str(build), '-n', qualification.TARGET])
                return plan

            def run(command, logfile, seconds, cwd=None):
                commands.append(command)
                binary = build / 'bin' / qualification.TARGET
                if command[0] == 'python3':
                    self.assertEqual(Path(command[1]).name, 'incremental_launcher.py')
                    logfile.write_text('ccache verified')
                elif command[0] == 'cmake':
                    self.assertIn(qualification.TARGET, command)
                    if '-d' in command:
                        self.assertEqual(seconds, 180)
                        logfile.write_text(plan)
                        if mode == 'compile-timeout': raise RuntimeError('timeout=True')
                        binary.write_bytes(b'compiled path-risk test')
                        generate_maps(source, build)
                    else:
                        logfile.write_text('ninja: no work to do.')
                        if mode == 'noop-links': logfile.write_text('[1/1] Linking CXX executable bin/' + qualification.TARGET)
                        if mode == 'noop-binary': binary.write_bytes(b'changed')
                        if mode == 'noop-map':
                            (build / 'bin/test/testdata/AdventureEvaluation/risk_earlier_guard.vmap').write_bytes(b'changed')
                        if mode == 'noop-source':
                            (source / 'AI/Nullkiller2/Engine/Experimental/DecisionRiskBudget.h').write_text('changed')
                else:
                    self.assertEqual(command[0], str(binary))
                    self.assertLessEqual(seconds, 180)
                    case = next(arg.split('=', 1)[1] for arg in command if arg.startswith('--gtest_filter='))
                    case_calls.append(case)
                    logfile.write_text('path protocol attempt')
                    xml = Path(next(arg.removeprefix('--gtest_output=xml:') for arg in command if arg.startswith('--gtest_output=')))
                    if mode != 'missing-xml' or len(case_calls) == 2: xml.write_text(case_xml(case))
                    if mode == 'first-case-timeout' and len(case_calls) == 1: raise RuntimeError('timeout=True')
                return 0.1

            def run_cases(executable, directory, cwd, cases, total_seconds):
                self.assertEqual(total_seconds, 360)
                return execute_cases(executable, directory, cwd, cases=cases, total_seconds=total_seconds, run_case=run)

            argv = ['path', '--build', str(build), '--evidence', str(evidence),
                    '--cache-validation', str(restore), '--data-archive', str(archive)]
            with patch('sys.argv', argv), patch.object(qualification, '__file__', str(source / 'CI/adventure_path_risk_qualification.py')), \
                 patch.object(qualification, 'DATA_HASH', qualification.digest(archive)), \
                 patch.object(qualification.subprocess, 'check_output', side_effect=check_output), \
                 patch.object(qualification, 'run', side_effect=run), \
                 patch.object(qualification, 'execute_cases', side_effect=run_cases):
                if mode == 'success': qualification.main()
                else:
                    with self.assertRaises((ValueError, RuntimeError, FileNotFoundError)): qualification.main()
            result = json.loads((evidence / 'qualification.json').read_text())
            self.assertEqual(result['status'], 'completed' if mode == 'success' else 'incomplete')
            self.assertFalse(result['cold_fallback'])
            self.assertFalse(result['activation_approved'])
            if mode in ('cold', 'invalid-restore', 'missing-model', 'unrelated-build'): self.assertEqual(commands, [])
            if mode in ('success', 'first-case-timeout', 'missing-xml'):
                self.assertEqual(case_calls, list(qualification.CASES))
                self.assertTrue(result['noop_verified'])
                self.assertEqual(result['case_results'][1]['status'], 'completed')
            else: self.assertEqual(case_calls, [])

    def test_success_requires_warm_build_noop_sources_and_two_exact_cases(self):
        self.exercise('success')

    def test_incomplete_stages_never_promote_and_second_case_remains_independent(self):
        for mode in ('cold', 'invalid-restore', 'missing-model', 'unrelated-build', 'compile-timeout',
                     'noop-links', 'noop-binary', 'noop-map', 'noop-source', 'missing-xml', 'first-case-timeout'):
            with self.subTest(mode=mode): self.exercise(mode)


class PathRiskWorkflowTest(unittest.TestCase):
    def test_exact_two_fixture_tests_target_and_after_scenario_maps_registration(self):
        fixture = (ROOT / 'test/adventure/AdventurePathRiskReference.cpp').read_text()
        names = re.findall(r'TEST_F\(AdventurePathRiskReference,\s*(\w+)\)', fixture)
        self.assertEqual(set(qualification.CASES), {'AdventurePathRiskReference.' + name for name in names})
        cmake = (ROOT / 'test/CMakeLists.txt').read_text()
        self.assertLess(cmake.index('include(adventure/AdventureScenarioReference.cmake)'), cmake.index('include(adventure/AdventurePathRiskReference.cmake)'))
        static = cmake.split('if(ENABLE_NULLKILLER2_AI AND ENABLE_STATIC_LIBS)')[1].split('endif()')[0]
        for name in ('DecisionRiskBudgetTest.cpp', 'PlanningRiskDecisionTest.cpp', 'PlanningRiskConsumerRegression.cpp'):
            self.assertEqual(static.count('nullkiller2/' + name), 1)
        target = (ROOT / 'test/adventure/AdventurePathRiskReference.cmake').read_text()
        self.assertIn('vcmi_path_risk_reference EXCLUDE_FROM_ALL', target)
        self.assertNotIn('add_test(', target)
        self.assertTrue(set(qualification.RISK_BUILD_SOURCES) <= set(qualification.SOURCES))

    def test_independent_gates_cannot_promote_failure_or_change_prior_gates(self):
        import yaml
        workflow = yaml.safe_load((ROOT / '.github/workflows/vcmi-incremental.yml').read_text())
        steps = workflow['jobs']['linux']['steps']
        ordinary = next(step for step in steps if step.get('id') == 'ordinary_regressions')
        reference = next(step for step in steps if step.get('id') == 'battle_references')
        holdout = next(step for step in steps if step.get('id') == 'decision_holdouts')
        path = next(step for step in steps if step.get('id') == 'path_risk_reference')
        snapshot = next(step for step in steps if step.get('name') == 'Snapshot only successful build outputs, with bounded upload size')
        for step in (holdout, path):
            self.assertIn('!cancelled()', step['if'])
            self.assertIn("steps.ordinary_regressions.outcome == 'success'", step['if'])
            self.assertIn("'[vcmi-warm] '", step['if'])
            self.assertIn("'[adventure-qualify]'", step['if'])
            self.assertNotIn('steps.battle_references.outcome', step['if'])
            self.assertNotIn('success()', step['if'])
            self.assertNotIn('continue-on-error', step)
            self.assertLess(steps.index(ordinary), steps.index(step))
            self.assertLess(steps.index(reference), steps.index(step))
            self.assertLess(steps.index(step), steps.index(snapshot))
        self.assertEqual(holdout['timeout-minutes'], 2)
        self.assertEqual(path['timeout-minutes'], 11)
        self.assertNotIn('cmake', holdout['run'])
        self.assertNotIn('if', snapshot)
        self.assertIn("needs.linux.result == 'success'", workflow['jobs']['macos']['if'])
        for name in ('test_adventure_holdout_qualification.py', 'test_adventure_path_risk_qualification.py', 'test_decision_holdout_evidence.py'):
            self.assertIn(name, workflow['jobs']['preflight']['steps'][-1]['run'])


if __name__ == '__main__':
    unittest.main()
