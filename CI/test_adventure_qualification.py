import unittest
from adventure_qualification import validate_plan

class ReferenceBudgetTest(unittest.TestCase):
    def test_new_server_and_reference_objects_allowed(self):
        plan = '\n'.join(['[1/2] Building CXX object server/CMakeFiles/vcmiservercommon_battles.dir/battles/BattleProcessor.cpp.o',
                          '[2/2] Building CXX object test/CMakeFiles/vcmi_forced_battle_reference.dir/battle/ForcedBattleReference.cpp.o'])
        self.assertEqual(len(validate_plan(plan, 48)), 2)
    def test_core_or_client_rebuild_is_rejected(self):
        for path in ('lib/CMakeFiles/vcmi_battle.dir/battle/BattleInfo.cpp.o', 'client/CMakeFiles/vcmiclient.dir/main.cpp.o'):
            with self.assertRaises(ValueError):
                validate_plan('[1/1] Building CXX object ' + path, 48)
    def test_large_reference_plan_rejected(self):
        with self.assertRaises(ValueError):
            validate_plan('\n'.join('[1/49] Building CXX object server/CMakeFiles/vcmiservercommon.dir/a%d.cpp.o' % i for i in range(49)), 48)
    def test_precompiled_headers_count_and_unknown_compile_rejected(self):
        plan = '[1/1] Building CXX precompiled header test/CMakeFiles/vcmi_forced_battle_reference.dir/cmake_pch.hxx.gch'
        self.assertEqual(len(validate_plan(plan, 48)), 1)
        with self.assertRaises(ValueError):
            validate_plan(plan, 0)
        with self.assertRaises(ValueError):
            validate_plan('[1/1] Building UNKNOWN source strange.output', 48)
        with self.assertRaises(ValueError):
            validate_plan('[1/1] Building CXX object test/CMakeFiles/vcmi_forced_battle_reference.dir/../../other.cpp.o', 48)

    def test_noop_allowed(self):
        self.assertEqual(validate_plan('ninja: no work to do.', 48), [])

    def test_actual_generated_version_metadata_object_is_allowed(self):
        path = 'lib/CMakeFiles/vcmi.dir/__/Version.cpp.o'
        self.assertEqual(validate_plan('[1/1] Building CXX object ' + path, 48), [path])

    def test_version_exception_does_not_allow_other_core_or_traversal_paths(self):
        for path in ('lib/CMakeFiles/vcmi.dir/Version.cpp.o',
                     'lib/CMakeFiles/vcmi.dir/__/Other.cpp.o',
                     'lib/CMakeFiles/vcmi.dir/../../Version.cpp.o',
                     'client/CMakeFiles/vcmiclient.dir/__/Version.cpp.o'):
            with self.assertRaises(ValueError):
                validate_plan('[1/1] Building CXX object ' + path, 48)

class ReferenceCrashDiagnosticTest(unittest.TestCase):
    def test_missing_debugger_does_not_install_or_pass(self):
        from unittest.mock import patch
        from pathlib import Path
        from adventure_qualification import capture_first_case_backtrace
        with patch('adventure_qualification.shutil.which', return_value=None), patch('adventure_qualification.run') as run:
            result = capture_first_case_backtrace(Path('/bin/reference'), Path('/evidence'), Path('/bin'))
        self.assertFalse(result['attempted'])
        self.assertFalse(result['scenario_passed'])
        run.assert_not_called()

    def test_existing_lldb_only_runs_fixed_case_and_bounded_backtrace(self):
        from unittest.mock import patch
        from pathlib import Path
        from adventure_qualification import capture_first_case_backtrace
        with patch('adventure_qualification.shutil.which', return_value='/usr/bin/lldb-18'), patch('adventure_qualification.run', return_value=0.1) as run:
            result = capture_first_case_backtrace(Path('/bin/reference'), Path('/evidence'), Path('/bin'))
        self.assertFalse(result['scenario_passed'])
        self.assertGreater(run.call_args.args[2], 0)
        self.assertLessEqual(run.call_args.args[2], 30)
        self.assertEqual(run.call_args_list[0].args[0], ['/usr/bin/lldb-18', '--no-lldbinit', '--help'])
        command = run.call_args.args[0]
        self.assertIn('thread backtrace all', command)
        self.assertIn('--no-lldbinit', command)
        self.assertIn('-k', command)
        self.assertIn('--gtest_filter=ForcedBattleReference.OrdinaryMelee', command)
        self.assertNotIn('memory read', ' '.join(command))

    def test_debugger_failure_cannot_replace_original_failure_with_pass(self):
        from unittest.mock import patch
        from pathlib import Path
        from adventure_qualification import capture_first_case_backtrace
        with patch('adventure_qualification.shutil.which', return_value='/usr/bin/lldb-18'), patch('adventure_qualification.run', side_effect=RuntimeError('diagnostic timeout')):
            result = capture_first_case_backtrace(Path('/bin/reference'), Path('/evidence'), Path('/bin'))
        self.assertFalse(result['scenario_passed'])
        self.assertEqual(result['reason'], 'diagnostic timeout')


class InterruptedReferenceDiagnosticTest(unittest.TestCase):
    def test_selects_last_started_declared_case_not_previous_pass(self):
        from adventure_qualification import last_started_reference_case
        log = '[ RUN      ] ForcedBattleReference.OrdinaryMelee\n[       OK ] ForcedBattleReference.OrdinaryMelee\n[ RUN      ] ForcedBattleReference.RangedAgainstSlowMelee\n'
        self.assertEqual(last_started_reference_case(log), 'ForcedBattleReference.RangedAgainstSlowMelee')
        for malformed in ('', '[ RUN      ] malicious:*\n', log + '[ RUN      ] unknown\n'):
            self.assertIsNone(last_started_reference_case(malformed))

    def test_retargets_bounded_debugger_and_rejects_unknown_case(self):
        from unittest.mock import patch
        from pathlib import Path
        from adventure_qualification import capture_first_case_backtrace
        with patch('adventure_qualification.shutil.which', return_value='/usr/bin/lldb-18'), patch('adventure_qualification.run', return_value=0.1) as run:
            result = capture_first_case_backtrace(Path('/bin/reference'), Path('/evidence'), Path('/bin'), 'ForcedBattleReference.RangedAgainstSlowMelee')
            self.assertIn('--gtest_filter=ForcedBattleReference.RangedAgainstSlowMelee', run.call_args.args[0])
            self.assertFalse(result['scenario_passed'])
            run.reset_mock()
            result = capture_first_case_backtrace(Path('/bin/reference'), Path('/evidence'), Path('/bin'), None)
            run.assert_not_called()
            self.assertFalse(result['attempted'])
