import importlib.util
import json
from pathlib import Path
import re
import tempfile
import unittest
from unittest.mock import Mock, patch
import adventure_interaction_qualification as qualification
from adventure_bridge_qualification import execute_cases, validate_restore, validate_xml

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('interaction_test_generator', ROOT / 'test/adventure/generate_interaction_maps.py')
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


def case_xml(name=qualification.CASES[0]):
    suite, test = name.split('.')
    return f'<testsuites failures="0"><testsuite><testcase classname="{suite}" name="{test}" status="run" result="completed"/></testsuite></testsuites>'


class InteractionContractTest(unittest.TestCase):
    def test_only_interaction_server_and_exact_version_edges_allowed(self):
        for path in ('test/CMakeFiles/vcmi_adventure_interaction_reference.dir/adventure/AdventureInteractionScenarios.cpp.o',
                     'test/CMakeFiles/vcmi_adventure_interaction_reference.dir/cmake_pch.hxx.gch',
                     'server/CMakeFiles/vcmiservercommon_processors.dir/processors/TurnOrderProcessor.cpp.o',
                     'lib/CMakeFiles/vcmi.dir/__/Version.cpp.o'):
            self.assertEqual(qualification.validate_plan('Building CXX object ' + path), [path])
        for path in ('test/CMakeFiles/vcmi_adventure_scenario_reference.dir/main.cpp.o',
                     'test/CMakeFiles/vcmi_forced_battle_reference.dir/main.cpp.o',
                     'lib/CMakeFiles/vcmi.dir/__/Other.cpp.o',
                     'AI/Nullkiller2/CMakeFiles/Nullkiller2.dir/AIGateway.cpp.o',
                     'client/CMakeFiles/vcmiclient.dir/main.cpp.o',
                     'server/CMakeFiles/vcmiservercommon.dir/../../evil.o'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                qualification.validate_plan('Building CXX object ' + path)

    def test_twelve_compile_edges_includes_pch_and_unknown_edges_fail(self):
        lines = ['Building CXX object test/CMakeFiles/vcmi_adventure_interaction_reference.dir/a%d.o' % i for i in range(11)]
        lines.append('Building CXX precompiled header test/CMakeFiles/vcmi_adventure_interaction_reference.dir/cmake_pch.hxx.gch')
        self.assertEqual(len(qualification.validate_plan('\n'.join(lines))), 12)
        with self.assertRaises(ValueError):
            qualification.validate_plan('\n'.join(lines + ['Building CXX object server/CMakeFiles/vcmiservercommon.dir/extra.o']))
        with self.assertRaises(ValueError):
            qualification.validate_plan('Building mystery target')
        self.assertEqual(qualification.validate_plan('ninja: no work to do.'), [])

    def test_strict_restore_shared_without_weaker_override(self):
        self.assertIs(qualification.validate_restore, validate_restore)
        with self.assertRaises(ValueError):
            qualification.validate_restore({'operation': 'restore'})
        self.assertEqual(qualification.DATA_HASH, '64247aeee0342c6f349576ba700a81e2de46de7b9aa19ca4881b8d59f5e0ced8')
        self.assertEqual(qualification.MAX_OBJECT_EDGES, 12)
        self.assertEqual(qualification.COMPILE_SECONDS, 180)

    def test_normative_interaction_has_its_own_process_and_exact_xml(self):
        calls = []
        def run(command, logfile, seconds, cwd):
            calls.append(command)
            self.assertEqual(seconds, 180)
            xml = Path(next(arg.removeprefix('--gtest_output=xml:') for arg in command if arg.startswith('--gtest_output=')))
            case = next(arg.split('=', 1)[1] for arg in command if arg.startswith('--gtest_filter='))
            xml.write_text(case_xml(case))
            return 0.1
        with tempfile.TemporaryDirectory() as temporary:
            results, _ = execute_cases(Path('/unused'), Path(temporary), Path(temporary), run_case=run, cases=qualification.CASES)
            self.assertEqual([result['status'] for result in results], ['completed'])
            self.assertEqual(len(calls), 1)
            for index, case in enumerate(qualification.CASES):
                self.assertIn('--gtest_filter=' + case, calls[index])
                self.assertIn('--gtest_output=xml:' + str(Path(temporary) / f'test-{index}.xml'), calls[index])
            file = Path(temporary) / 'test-0.xml'
            file.write_text(case_xml().replace('status="run"', 'status="notrun"'))
            with self.assertRaises(ValueError):
                validate_xml(file, qualification.CASES[0])

    def test_shared_total_budget_is_not_reset_between_cases(self):
        # Synthetic supervisor inputs preserve the shared budget contract; not normative scenarios.
        clock = Mock(side_effect=[0, 0, 590, 601])
        run = Mock(side_effect=RuntimeError('timeout=True'))
        with tempfile.TemporaryDirectory() as temporary:
            results, elapsed = execute_cases(Path('/unused'), Path(temporary), Path(temporary),
                run_case=run, clock=clock, cases=('Harness.First', 'Harness.Second'))
        self.assertEqual([call.args[2] for call in run.call_args_list], [180, 10])
        self.assertEqual([result['status'] for result in results], ['incomplete', 'incomplete'])
        self.assertEqual(elapsed, 601)

    def test_map_and_source_provenance_must_match(self):
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            directory = build / 'bin/test/testdata/AdventureInteractions'
            original = generator.generate(ROOT, directory)
            hashes, evidence, _ = qualification.map_hashes(build, ROOT)
            self.assertEqual(hashes[qualification.MAP_NAME], original['map_sha256'])
            self.assertEqual(evidence, original)
            for update in ({'map_sha256': 'wrong'}, {'source_sha256': {}}, {'seed': 1},
                           {'difficulty': 1}, {'blue_scripted_retreat_anchors': [[45, 25, 0]]}):
                (directory / 'interaction-provenance.json').write_text(json.dumps(dict(original, **update)))
                with self.subTest(update=update), self.assertRaises(ValueError):
                    qualification.map_hashes(build, ROOT)


class InteractionRunnerTest(unittest.TestCase):
    def exercise(self, mode):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build, evidence = root / 'build', root / 'evidence'
            (build / 'bin').mkdir(parents=True)
            for name in ('CMakeCache.txt', 'build.ninja', '.ninja_log'):
                (build / name).write_text('warm build')
            restore = root / 'restore.json'
            restore.write_text(json.dumps(dict(operation='restore', unchanged_sources=1,
                verified_build_files=1, verified_external_files=0, changed_sources=[], removed_sources=[])))
            archive = root / 'data.7z'
            archive.write_bytes(b'test archive')
            if mode == 'cold':
                (build / 'build.ninja').unlink()
            elif mode == 'invalid-restore':
                restore.write_text('{"operation":"restore"}')
            obj = 'test/CMakeFiles/vcmi_adventure_interaction_reference.dir/main.cpp.o'
            plan = 'Building CXX object ' + obj
            if mode == 'unrelated-build':
                plan = 'Building CXX object client/CMakeFiles/vcmiclient.dir/main.cpp.o'
            commands, case_calls = [], []
            def check_output(command, **kwargs):
                if command == ['git', 'rev-parse', 'HEAD']:
                    return 'commit'
                self.assertEqual(command, ['ninja', '-C', str(build), '-n', qualification.TARGET])
                return plan

            def run(command, logfile, seconds, cwd=None):
                commands.append(command)
                binary = build / 'bin' / qualification.TARGET
                if command[0] == 'python3':
                    self.assertEqual(Path(command[1]).name, 'incremental_launcher.py')
                    logfile.write_text('launcher verified')
                elif command[0] == 'cmake':
                    self.assertIn(qualification.TARGET, command)
                    if '-d' in command:
                        self.assertEqual(seconds, 180)
                        logfile.write_text(plan)
                        if mode == 'compile-timeout':
                            raise RuntimeError('timeout=True')
                        binary.write_bytes(b'compiled interaction binary')
                        generator.generate(ROOT, build / 'bin/test/testdata/AdventureInteractions')
                    else:
                        self.assertEqual(seconds, 30)
                        logfile.write_text('ninja: no work to do.')
                        if mode == 'noop-links':
                            logfile.write_text('[1/1] Linking CXX executable bin/' + qualification.TARGET)
                        elif mode == 'noop-mutates-binary':
                            binary.write_bytes(b'changed binary')
                        elif mode == 'noop-mutates-provenance':
                            provenance = build / 'bin/test/testdata/AdventureInteractions/interaction-provenance.json'
                            provenance.write_text(provenance.read_text() + '\n')
                else:
                    self.assertEqual(command[0], str(binary))
                    self.assertEqual(seconds, 180)
                    case = next(arg.split('=', 1)[1] for arg in command if arg.startswith('--gtest_filter='))
                    self.assertEqual(case, qualification.CASES[len(case_calls)])
                    case_calls.append(case)
                    logfile.write_text('interaction attempt')
                    xml = Path(next(arg.removeprefix('--gtest_output=xml:') for arg in command if arg.startswith('--gtest_output=')))
                    if mode != 'missing-xml':
                        xml.write_text(case_xml(case))
                    if mode == 'superseded-case-xml':
                        xml.write_text(case_xml('AdventureInteractionScenarios.UrgentTownPurchaseThenThreeTurnRecovery'))
                    elif mode == 'skipped-case-xml':
                        xml.write_text(case_xml(case).replace('status="run"', 'status="notrun"'))
                    elif mode == 'failed-case-xml':
                        xml.write_text(case_xml(case).replace('/>', '><failure/></testcase>'))
                    if mode == 'case-timeout':
                        raise RuntimeError('timeout=True')
                    if mode == 'case-exit-failure':
                        raise RuntimeError('exit=1')
                return 0.1

            def run_cases(executable, directory, cwd, cases):
                return execute_cases(executable, directory, cwd, cases=cases, run_case=run)

            argv = ['interaction', '--build', str(build), '--evidence', str(evidence),
                    '--cache-validation', str(restore), '--data-archive', str(archive)]
            with patch('sys.argv', argv), patch.object(qualification, 'DATA_HASH', qualification.digest(archive)), \
                 patch.object(qualification.subprocess, 'check_output', side_effect=check_output), \
                 patch.object(qualification, 'run', side_effect=run), \
                 patch.object(qualification, 'execute_cases', side_effect=run_cases):
                if mode == 'success':
                    qualification.main()
                else:
                    with self.assertRaises((RuntimeError, ValueError)):
                        qualification.main()
            result = json.loads((evidence / 'qualification.json').read_text())
            self.assertEqual(result['status'], 'completed' if mode == 'success' else 'incomplete')
            self.assertFalse(result['cold_fallback'])
            self.assertEqual(result['case_seconds_limit'], 180)
            self.assertEqual(result['total_execution_seconds_limit'], 600)
            self.assertEqual(result['compile_seconds_limit'], 180)
            self.assertEqual(result['max_object_edges'], 12)
            if mode in ('cold', 'invalid-restore', 'unrelated-build'):
                self.assertEqual(commands, [])
            if mode == 'success':
                self.assertEqual(result['executed_tests'], list(qualification.CASES))
                self.assertTrue(result['noop_verified'])
            elif mode in ('case-timeout', 'case-exit-failure', 'missing-xml', 'superseded-case-xml',
                          'skipped-case-xml', 'failed-case-xml'):
                self.assertEqual(result['executed_tests'], [])
                self.assertEqual(result['case_results'][0]['status'], 'incomplete')
                self.assertEqual(case_calls, list(qualification.CASES))
            else:
                self.assertNotIn('case_results', result)

    def test_success_requires_warm_compile_noop_and_exact_completed_case(self):
        self.exercise('success')

    def test_every_incomplete_stage_remains_failed_without_fallback(self):
        for mode in ('cold', 'invalid-restore', 'unrelated-build', 'compile-timeout', 'noop-links',
                     'noop-mutates-binary', 'noop-mutates-provenance', 'missing-xml', 'case-timeout',
                     'case-exit-failure', 'superseded-case-xml', 'skipped-case-xml', 'failed-case-xml'):
            with self.subTest(mode=mode):
                self.exercise(mode)


class FollowOnRegistrationTest(unittest.TestCase):
    def test_exact_normative_case_requires_actual_defence_on_return(self):
        self.assertEqual(qualification.CASES, (
            'AdventureInteractionScenarios.CoveredEconomyThenActualDefenceOnEnemyApproach',))
        fixture = (ROOT / 'test/adventure/AdventureInteractionScenarios.cpp').read_text()
        names = re.findall(r'TEST_F\(AdventureInteractionScenarios,\s*(\w+)\)', fixture)
        self.assertEqual(set(qualification.CASES), {'AdventureInteractionScenarios.' + name for name in names})

    def test_exact_new_ordinary_tests_are_registered_in_static_nk2_suite(self):
        required = json.loads((ROOT / 'CI/ai-full-required-tests.json').read_text())
        names = []
        for path in ('PlanningRiskEvidenceTest.cpp', 'PlanningRiskTableTest.cpp'):
            for suite, name in re.findall(r'TEST\(\s*(\w+)\s*,\s*(\w+)\s*\)', (ROOT / 'test/nullkiller2' / path).read_text()):
                if suite in ('PlanningRiskEvidence', 'DetachedDefendBonuses'):
                    names.append(suite + '.' + name)
        self.assertEqual(len(names), 8)
        self.assertTrue(set(names) <= set(required['required_tests']))
        self.assertEqual(len(required['required_tests']), len(set(required['required_tests'])))
        self.assertGreaterEqual(required['minimum_executed_tests'], 429)
        cmake = (ROOT / 'test/CMakeLists.txt').read_text()
        static_block = cmake.split('if(ENABLE_NULLKILLER2_AI AND ENABLE_STATIC_LIBS)')[1].split('endif()')[0]
        self.assertIn('nullkiller2/PlanningRiskEvidenceTest.cpp', static_block)
        self.assertEqual(cmake.count('include(adventure/AdventureInteractionReference.cmake)'), 1)
        target = (ROOT / 'test/adventure/AdventureInteractionReference.cmake').read_text()
        self.assertIn('add_executable(vcmi_adventure_interaction_reference EXCLUDE_FROM_ALL', target)
        self.assertNotIn('add_test(', target)

    def test_failed_references_do_not_hide_interaction_or_promote_outputs(self):
        import yaml
        workflow = yaml.safe_load((ROOT / '.github/workflows/vcmi-incremental.yml').read_text())
        steps = workflow['jobs']['linux']['steps']
        ordinary = next(step for step in steps if step.get('id') == 'ordinary_regressions')
        model = next(step for step in steps if step.get('id') == 'battle_references')
        defend = next(step for step in steps if step.get('id') == 'defend_references')
        interaction = next(step for step in steps if step.get('id') == 'adventure_interaction')
        snapshot = next(step for step in steps if step.get('name') == 'Snapshot only successful build outputs, with bounded upload size')
        for step in (defend, interaction):
            self.assertIn('!cancelled()', step['if'])
            self.assertIn("steps.ordinary_regressions.outcome == 'success'", step['if'])
            self.assertIn("'[vcmi-warm] '", step['if'])
            self.assertIn("'[adventure-qualify]'", step['if'])
            self.assertNotIn('steps.battle_references.outcome', step['if'])
            self.assertNotIn('success()', step['if'])
            self.assertNotIn('continue-on-error', step)
            self.assertLess(steps.index(ordinary), steps.index(step))
            self.assertLess(steps.index(step), steps.index(snapshot))
        self.assertLess(steps.index(model), steps.index(defend))
        self.assertNotIn('cmake', defend['run'])
        self.assertNotIn('continue-on-error', model)
        self.assertNotIn('if', snapshot)
        self.assertEqual(interaction['timeout-minutes'], 15)
        save = next(step for step in steps if step.get('name') == 'Persist new cache generation (not a frozen first-build key)')
        self.assertNotRegex(save['if'], r'always\(|failure\(|!cancelled\(')
        self.assertIn("needs.linux.result == 'success'", workflow['jobs']['macos']['if'])
        self.assertIn('linux', workflow['jobs']['macos']['needs'])
        preflight = workflow['jobs']['preflight']['steps'][-1]['run']
        self.assertIn('test_adventure_defend_qualification.py', preflight)
        self.assertIn('test_adventure_interaction_qualification.py', preflight)
        self.assertIn('test_interaction_maps.py', preflight)


if __name__ == '__main__':
    unittest.main()
