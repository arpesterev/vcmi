import tempfile
import sys
import unittest
from pathlib import Path
from adventure_bridge_qualification import CASES, validate_plan, validate_restore, validate_xml

class BridgeQualificationTest(unittest.TestCase):
    def test_only_target_server_and_exact_version_allowed(self):
        for path in ('test/CMakeFiles/vcmi_adventure_scenario_reference.dir/adventure/AdventureScenarioBridge.cpp.o',
                     'server/CMakeFiles/vcmiservercommon_processors.dir/processors/TurnOrderProcessor.cpp.o',
                     'lib/CMakeFiles/vcmi.dir/__/Version.cpp.o'):
            self.assertEqual(validate_plan('Building CXX object ' + path), [path])

    def test_core_ai_other_target_and_traversal_rejected(self):
        for path in ('AI/Nullkiller2/CMakeFiles/Nullkiller2.dir/AIGateway.cpp.o',
                     'lib/CMakeFiles/vcmi.dir/__/Other.cpp.o',
                     'test/CMakeFiles/vcmi_forced_battle_reference.dir/main.cpp.o',
                     'server/CMakeFiles/vcmiservercommon.dir/../../evil.o'):
            with self.assertRaises(ValueError):
                validate_plan('Building CXX object ' + path)

    def test_cap_counts_precompiled_headers(self):
        lines = ['Building CXX object test/CMakeFiles/vcmi_adventure_scenario_reference.dir/a%d.o' % i for i in range(12)]
        self.assertEqual(len(validate_plan('\n'.join(lines))), 12)
        with self.assertRaises(ValueError):
            validate_plan('\n'.join(lines + ['Building CXX precompiled header test/CMakeFiles/vcmi_adventure_scenario_reference.dir/cmake_pch.hxx.gch']))

    def test_unknown_compile_edge_rejected_noop_allowed(self):
        with self.assertRaises(ValueError):
            validate_plan('Building mystery target')
        self.assertEqual(validate_plan('ninja: no work to do.'), [])

    def test_restore_requires_positive_verified_outputs(self):
        valid = dict(operation='restore', unchanged_sources=1, verified_build_files=1,
                     verified_external_files=0, changed_sources=[], removed_sources=[])
        validate_restore(valid)
        for patch in ({'operation': 'snapshot'}, {'verified_build_files': 0}, {'unchanged_sources': True},
                      {'changed_sources': None}):
            with self.assertRaises(ValueError):
                validate_restore(dict(valid, **patch))
        with self.assertRaises(ValueError):
            validate_restore({'operation': 'restore'})

    def check_xml(self, text):
        with tempfile.TemporaryDirectory() as directory:
            file = Path(directory) / 'tests.xml'
            file.write_text(text)
            return validate_xml(file, CASES[0])

    def test_xml_requires_exact_completed_case(self):
        name = CASES[0].split('.')[1]
        valid = f'<testsuites failures="0"><testsuite><testcase classname="AdventureScenarioBridge" name="{name}" status="run" result="completed"/></testsuite></testsuites>'
        self.assertEqual(self.check_xml(valid), CASES[0])
        for bad in (valid.replace('status="run"', 'status="notrun"'),
                    valid.replace('result="completed"', 'result="skipped"'),
                    valid.replace('failures="0"', 'failures="1"'),
                    valid.replace('/></testsuite>', '><failure/></testcase></testsuite>'),
                    valid.replace(name, 'Unknown'), '<testsuites/>'):
            with self.assertRaises(ValueError):
                self.check_xml(bad)

    def test_supervisor_timeout_is_failure_even_if_output_claims_success(self):
        from adventure_bridge_qualification import run
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(RuntimeError, 'timeout=True'):
                run([sys.executable, '-c', 'import time; print("PASS", flush=True); time.sleep(5)'],
                    Path(directory) / 'timeout.log', 0.05)

    def test_missing_xml_cannot_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(FileNotFoundError):
                validate_xml(Path(directory) / 'absent.xml', CASES[0])

class BridgeWorkflowGateTest(unittest.TestCase):
    def test_reference_failure_does_not_hide_bridge_and_cannot_promote_cache(self):
        from pathlib import Path
        import yaml
        workflow = yaml.safe_load((Path(__file__).parents[1] / '.github/workflows/vcmi-incremental.yml').read_text())
        steps = workflow['jobs']['linux']['steps']
        ordinary = next(s for s in steps if s.get('id') == 'ordinary_regressions')
        bridge = next(s for s in steps if s.get('name') == 'Qualify bounded real planner turns on public synthetic maps')
        snapshot = next(s for s in steps if s.get('name') == 'Snapshot only successful build outputs, with bounded upload size')
        self.assertIn("!cancelled()", bridge['if'])
        self.assertIn("steps.ordinary_regressions.outcome == 'success'", bridge['if'])
        self.assertNotIn('continue-on-error', bridge)
        self.assertNotIn('continue-on-error', ordinary)
        self.assertNotIn('if', snapshot)  # GitHub's default success() remains required.
        self.assertLess(steps.index(ordinary), steps.index(bridge))
        self.assertLess(steps.index(bridge), steps.index(snapshot))
        self.assertIn('linux', workflow['jobs']['macos']['needs'])

if __name__ == '__main__':
    unittest.main()
