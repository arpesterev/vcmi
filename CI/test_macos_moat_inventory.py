"""Exact moat admission contracts; these tests do not execute macOS binaries."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import run_macos_native_ai as runner
from test_macos_native_ai import write_xml


class MoatCandidateSelection(unittest.TestCase):
    def setUp(self):
        self.root = Path(__file__).parent
        self.previous = json.loads((self.root / 'macos_ai_f398624_inventory.json').read_text())
        self.manifest = json.loads((self.root / 'macos_ai_a102f718_inventory.json').read_text())
        self.ids = set(self.manifest['selected_tests'])

    def test_only_ten_additions_with_unchanged_gates(self):
        old = set(self.previous['selected_tests'])
        self.assertEqual(self.ids - old, runner.MOAT_NATIVE)
        self.assertFalse(old - self.ids)
        self.assertEqual(len(self.ids), 909)
        self.assertEqual(self.previous['excluded_gated_tests'], self.manifest['excluded_gated_tests'])
        self.assertEqual(set(self.manifest['excluded_gated_tests']), runner.GATED_NATIVE)
        self.assertTrue(all(runner.matches_filter(n, runner.REVIEWED_FILTER) for n in self.ids))

    def test_missing_extra_or_replaced_moat_identity_rejected(self):
        runner.verify_moat_inventory(runner.MOAT_NATIVE)
        for name in runner.MOAT_NATIVE:
            for changed in (runner.MOAT_NATIVE - {name},
                            (runner.MOAT_NATIVE - {name}) | {'FHeroes2MoatContinuation.unreviewed'}):
                with self.subTest(name=name), self.assertRaises(ValueError):
                    runner.verify_moat_inventory(changed)
        with self.assertRaises(ValueError):
            runner.verify_moat_inventory(runner.MOAT_NATIVE | {'Footprint/HoMM3MoatApproach.extra/Wide'})

    def test_previous_manifest_and_missing_new_case_rejected(self):
        runner.verify_reviewed_inventory(self.ids, self.manifest)
        with self.assertRaises(ValueError):
            runner.verify_reviewed_inventory(self.ids, self.previous)
        with self.assertRaises(ValueError):
            runner.verify_reviewed_inventory(set(self.previous['selected_tests']), self.manifest)
        for name in runner.MOAT_NATIVE:
            with self.subTest(name=name), self.assertRaises(ValueError):
                runner.verify_reviewed_inventory(self.ids - {name}, self.manifest)

    def test_disjoint_complete_partitions_preserve_timeouts(self):
        groups = {}
        for group in ('core', 'slow'):
            groups[group] = set()
            for name, selected, count, timeout in runner.selected_suites(479, 18, group, 209, include_moat=True):
                cases = {n for n in self.ids if runner.matches_filter(n, selected)}
                self.assertEqual(len(cases), count)
                self.assertFalse(groups[group] & cases)
                groups[group] |= cases
                if name == 'moat-route-regressions':
                    self.assertEqual(cases, runner.MOAT_NATIVE)
                    self.assertEqual(timeout, 300)
                if name == 'nullkiller-regressions':
                    self.assertEqual(timeout, 900)
        self.assertFalse(groups['core'] & groups['slow'])
        self.assertEqual(groups['core'] | groups['slow'], self.ids)
        self.assertEqual(len(groups['core']), 764)
        self.assertEqual(len(groups['slow']), 145)
        self.assertFalse(runner.MOAT_NATIVE & groups['slow'])

    def test_xml_must_execute_all_ten_reviewed_cases(self):
        with tempfile.TemporaryDirectory() as directory:
            xml = Path(directory) / 'moat.xml'
            write_xml(xml, runner.MOAT_NATIVE)
            self.assertEqual(runner.validate_report(xml, runner.MOAT_NATIVE, runner.MOAT_FILTER), 10)
            changed = (runner.MOAT_NATIVE - {next(iter(runner.MOAT_NATIVE))}) | {'FHeroes2MoatContinuation.extra'}
            write_xml(xml, changed)
            with self.assertRaises(ValueError):
                runner.validate_report(xml, runner.MOAT_NATIVE, runner.MOAT_FILTER)

    def test_workflow_uses_new_manifest_and_unchanged_native_counts(self):
        workflow = (self.root.parent / '.github/workflows/ai-macos-arm64.yml').read_text()
        self.assertIn('CI/macos_ai_a102f718_inventory.json', workflow)
        self.assertNotIn('CI/macos_ai_f398624_inventory.json', workflow)
        self.assertIn('--expected-native-count 479 --expected-forecast-count 18 --expected-lifecycle-count 10', workflow)

    def test_production_orchestration_requires_moat_before_execution(self):
        # Mock only platform/fixture execution: prove runner wiring, not an ARM64 run.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / 'vcmitest'
            binary.write_bytes(b'unit-test-placeholder')
            (root / 'fixture').write_bytes(b'unit-test-placeholder')
            def inventory(_binary, _out, _env, _name, selected):
                return {n for n in self.ids | runner.GATED_NATIVE if runner.matches_filter(n, selected)}
            for stale in (False, True):
                manifest = self.root / ('macos_ai_f398624_inventory.json' if stale else 'macos_ai_a102f718_inventory.json')
                with patch.object(runner, 'check_binary'), patch.object(runner, 'discover_archiver', return_value='7z'), \
                     patch.object(runner, 'capture', return_value=''), patch.object(runner, 'prepare_fixture'), \
                     patch.object(runner, 'read_inventory', side_effect=inventory), \
                     patch.object(runner, 'run_suite', return_value={'status': 'passed'}) as execute, \
                     contextlib.redirect_stdout(io.StringIO()):
                    result = runner.main(['--test-binary', str(binary), '--fixture-archive', str(root / 'fixture'),
                                          '--source-sha', '0' * 40, '--expected-native-count', '479',
                                          '--expected-forecast-count', '18', '--expected-lifecycle-count', '10',
                                          '--reviewed-inventory', str(manifest), '--suite-group', 'core',
                                          '--output-dir', str(root / str(stale))])
                self.assertEqual(result, 1 if stale else 0)
                if stale:
                    execute.assert_not_called()
                else:
                    self.assertEqual([c.args[3][0] for c in execute.call_args_list].count('moat-route-regressions'), 1)


if __name__ == '__main__':
    unittest.main()
