import copy
import importlib.util
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location('linux_identity_diagnostic', ROOT / 'linux_identity_diagnostic.py')
DIAG = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DIAG)
IMAGE = '20261023.334.3'


class DiagnosticTests(unittest.TestCase):
    def setUp(self):
        self.baseline = DIAG.read_json(ROOT / 'linux-diagnostic-baseline/toolchain-identity.json')
        self.current = copy.deepcopy(self.baseline)
        self.current['runner_image'] = IMAGE

    def report(self):
        return DIAG.compare(self.baseline, self.current, IMAGE)

    def test_only_image_difference_is_observational_not_cache_approval(self):
        report = self.report()
        self.assertTrue(report['equal_except_runner_image'])
        self.assertTrue(report['diagnostic_only'])
        self.assertFalse(report['cache_reuse_authorized'])
        self.assertEqual(report['differing_fields'], [])
        self.assertEqual(report['runner_image']['baseline'], '20261012.334.1')

    def test_every_other_field_remains_strict(self):
        for field in self.baseline:
            if field in ('runner_image', 'schema', 'profile', 'os', 'packages'):
                continue
            with self.subTest(field=field):
                current = copy.deepcopy(self.current)
                current[field] += '-changed'
                report = DIAG.compare(self.baseline, current, IMAGE)
                self.assertFalse(report['equal_except_runner_image'])
                self.assertEqual(report['differing_fields'], [field])

    def test_real_compiler_binary_change_is_rejected(self):
        self.current['compiler_sha256'] = '0' * 64
        self.assertEqual(self.report()['differing_fields'], ['compiler_sha256'])

    def test_real_package_version_change_is_rejected_and_named(self):
        old = self.current['packages'].splitlines()[0]
        name, version = old.split('=', 1)
        self.current['packages'] = self.current['packages'].replace(old, name + '=' + version + '+changed', 1)
        report = self.report()
        self.assertEqual(report['differing_fields'], ['packages'])
        self.assertEqual(report['package_changes'], [
            {'package': name, 'baseline': version, 'current': version + '+changed'}])

    def test_added_and_removed_packages_are_reported(self):
        removed = self.current['packages'].splitlines()[0].split('=', 1)[0]
        self.current['packages'] = '\n'.join(self.current['packages'].splitlines()[1:] + ['zz-diagnostic-example=1'])
        changes = {item['package']: item for item in self.report()['package_changes']}
        self.assertIsNone(changes[removed]['current'])
        self.assertIsNone(changes['zz-diagnostic-example']['baseline'])

    def test_package_order_is_not_silently_normalized(self):
        self.current['packages'] = '\n'.join(reversed(self.current['packages'].splitlines()))
        self.assertEqual(self.report()['differing_fields'], ['packages'])

    def test_missing_and_added_identity_fields_fail(self):
        del self.current['compiler']
        self.current['new_field'] = 'not reviewed'
        self.assertEqual(self.report()['differing_fields'], ['compiler', 'new_field'])

    def test_unknown_missing_or_unreviewed_image_fails_closed(self):
        for image in ('unknown', '', None, '20261024.1.1'):
            with self.subTest(image=image):
                self.current['runner_image'] = image
                with self.assertRaises(ValueError):
                    self.report()
        del self.current['runner_image']
        with self.assertRaises(ValueError):
            self.report()

    def test_invalid_schema_or_platform_fails_closed(self):
        for field, value in [('schema', True), ('schema', 2), ('profile', 'macos'), ('os', 'Darwin')]:
            with self.subTest(field=field, value=value):
                current = copy.deepcopy(self.current)
                current[field] = value
                with self.assertRaises(ValueError):
                    DIAG.compare(self.baseline, current, IMAGE)

    def test_duplicate_packages_and_json_keys_fail_closed(self):
        self.current['packages'] += '\n' + self.current['packages'].splitlines()[0]
        with self.assertRaises(ValueError):
            self.report()
        with self.assertRaises(ValueError):
            DIAG.unique_object([('schema', 1), ('schema', 1)])

    def test_cli_emits_full_exact_differences_and_nonzero_status(self):
        self.current['compiler_sha256'] = 'f' * 64
        with tempfile.TemporaryDirectory() as directory:
            current = pathlib.Path(directory) / 'current.json'
            output = pathlib.Path(directory) / 'report.json'
            current.write_text(json.dumps(self.current))
            result = subprocess.run([
                sys.executable, str(ROOT / 'linux_identity_diagnostic.py'), 'compare',
                '--baseline', str(ROOT / 'linux-diagnostic-baseline/toolchain-identity.json'),
                '--current', str(current), '--expected-image', IMAGE, '--output', str(output),
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 1, result.stderr)
            report = json.loads(output.read_text())
            self.assertEqual(report['differences'][0]['baseline'], self.baseline['compiler_sha256'])
            self.assertEqual(report['differences'][0]['current'], 'f' * 64)
            self.assertEqual(report['baseline_file_sha256'], 'e923c33704e900860b291feb4ad31b5d3049137100f6511e561f74809d233b8d')

    def test_binary_inventory_refuses_home_and_non_system_paths(self):
        with tempfile.NamedTemporaryFile() as temporary:
            with self.assertRaises(ValueError):
                DIAG.binary_record(temporary.name)


if __name__ == '__main__':
    unittest.main()
