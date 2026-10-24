import copy
import hashlib
import importlib.util
import json
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location('linux_cache_compatibility', ROOT / 'linux_cache_compatibility.py')
COMPAT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COMPAT)
PINS = {
    'runner': 'e5f3db8a3395709a5a60c47b55d56a59910fca4ad7b24500a0ca18f13bf6dcb1',
    'dependency': 'aeb32a29af25fe67dfefd742c9cee60e32a493abb60c71d9d74705db61d9b132',
    'toolchain': 'e923c33704e900860b291feb4ad31b5d3049137100f6511e561f74809d233b8d',
}
OLD_IMAGE = '20261012.334.1'
NEW_IMAGE = '20261023.334.3'


class CompatibilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.baseline_bytes = {phase: (ROOT / 'linux-diagnostic-baseline' / (phase + '-identity.json')).read_bytes()
                              for phase in PINS}
        cls.baselines = {phase: json.loads(data) for phase, data in cls.baseline_bytes.items()}

    def observed(self, phase, image=NEW_IMAGE):
        value = copy.deepcopy(self.baselines[phase])
        value['runner_image'] = image
        return value

    def certify(self, phase, value):
        return COMPAT.certify(phase, json.dumps(value).encode())

    def assert_rejected(self, phase, value, field=None):
        canonical, report = self.certify(phase, value)
        self.assertIsNone(canonical)
        self.assertEqual(report['status'], 'rejected')
        self.assertFalse(report['accepted'])
        self.assertFalse(report['alias_applied'])
        self.assertIsNone(report['canonical_sha256'])
        self.assertIsNone(report['canonical_image'])
        if field is not None:
            self.assertIn(field, report['differing_fields'])
        return report

    def test_baseline_byte_pins_are_independent_and_exact(self):
        self.assertEqual(COMPAT.BASELINE_SHA256, PINS)
        for phase, data in self.baseline_bytes.items():
            with self.subTest(phase=phase):
                self.assertEqual(hashlib.sha256(data).hexdigest(), PINS[phase])

    def test_both_named_images_produce_exact_original_baseline_bytes(self):
        for phase in PINS:
            for image in (OLD_IMAGE, NEW_IMAGE):
                with self.subTest(phase=phase, image=image):
                    observed = self.observed(phase, image)
                    before = copy.deepcopy(observed)
                    raw = json.dumps(observed, sort_keys=False, separators=(',', ':')).encode()
                    canonical, report = COMPAT.certify(phase, raw)
                    self.assertEqual(observed, before)
                    self.assertEqual(canonical, self.baseline_bytes[phase])
                    self.assertEqual(report['observed_sha256'], hashlib.sha256(raw).hexdigest())
                    self.assertEqual(report['canonical_sha256'], PINS[phase])
                    self.assertEqual(report['baseline_sha256'], PINS[phase])
                    self.assertEqual(report['actual_image'], image)
                    self.assertEqual(report['canonical_image'], OLD_IMAGE)
                    self.assertEqual(report['alias_applied'], image == NEW_IMAGE)
                    self.assertEqual(report['status'], 'accepted')
                    self.assertTrue(report['accepted'])
                    self.assertEqual(report['requires_installed_toolchain_check'], phase != 'toolchain')

    def test_every_field_value_is_strict_on_both_images_and_all_phases(self):
        for phase in PINS:
            for image in (OLD_IMAGE, NEW_IMAGE):
                for field, value in self.baselines[phase].items():
                    if field == 'runner_image':
                        continue
                    with self.subTest(phase=phase, image=image, field=field):
                        observed = self.observed(phase, image)
                        observed[field] = value + 1 if type(value) is int else value + '-changed'
                        self.assert_rejected(phase, observed, field)

    def test_every_field_type_is_strict_including_python_equal_types(self):
        for phase in PINS:
            for field, value in self.baselines[phase].items():
                wrong_types = (None, True, 1.0, [], {}, 1) if type(value) is str else (True, 1.0, '1', None, [], {})
                for replacement in wrong_types:
                    with self.subTest(phase=phase, field=field, replacement=replacement):
                        observed = self.observed(phase)
                        observed[field] = replacement
                        self.assert_rejected(phase, observed, None if field == 'runner_image' else field)

    def test_missing_every_field_is_rejected(self):
        for phase in PINS:
            for field in self.baselines[phase]:
                with self.subTest(phase=phase, field=field):
                    observed = self.observed(phase)
                    del observed[field]
                    self.assert_rejected(phase, observed, None if field == 'runner_image' else field)

    def test_extra_fields_are_never_ignored(self):
        for phase in PINS:
            for image in (OLD_IMAGE, NEW_IMAGE):
                for value in (None, '', 1, True, [], {}, {'schema': 1}):
                    with self.subTest(phase=phase, image=image, value=value):
                        observed = self.observed(phase, image)
                        observed['unreviewed_field'] = value
                        self.assert_rejected(phase, observed, 'unreviewed_field')

    def test_unknown_images_are_not_treated_as_a_generic_ubuntu_alias(self):
        for phase in PINS:
            for image in ('', 'unknown', '20261024.334.3', '20261012.334.2', '20261023.334.1',
                          '20261023.334.3\n', ' 20261023.334.3', 'ubuntu-24.04', 20261023, None):
                with self.subTest(phase=phase, image=image):
                    self.assert_rejected(phase, self.observed(phase, image))

    def test_package_changes_additions_removals_order_and_whitespace_fail(self):
        original = self.baselines['toolchain']['packages']
        rows = original.splitlines()
        alternatives = [original + '\n', original + '\nnew-package=1', '\n'.join(rows[1:]),
                        '\n'.join(reversed(rows)), original + '\n' + rows[0],
                        original.replace(rows[0], rows[0] + '+changed', 1), original.replace('\n', '\r\n')]
        self.assertEqual(hashlib.sha256(original.encode()).hexdigest(),
                         'ec8787c0714c3ecf0cd2eb3b828fb3f7d9c8c3290da4dfe3a929b6f7ace2c2d8')
        for changed in alternatives:
            observed = self.observed('toolchain')
            observed['packages'] = changed
            self.assert_rejected('toolchain', observed, 'packages')

    def test_baseline_formatting_and_content_tampering_fail_byte_pin(self):
        for phase in PINS:
            alternatives = [self.baseline_bytes[phase] + b'\n', json.dumps(self.baselines[phase]).encode(), b'{}']
            for changed in alternatives:
                with self.subTest(phase=phase, changed_length=len(changed)):
                    with tempfile.TemporaryDirectory() as directory:
                        baseline = pathlib.Path(directory) / (phase + '-identity.json')
                        baseline.write_bytes(changed)
                        canonical, report = COMPAT.certify(phase, json.dumps(self.observed(phase)).encode(), directory)
                    self.assertIsNone(canonical)
                    self.assertFalse(report['accepted'])
                    self.assertIn('Baseline byte hash', report['reason'])
                    self.assertEqual(report['baseline_sha256'], hashlib.sha256(changed).hexdigest())

    def test_missing_baseline_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            canonical, report = COMPAT.certify('runner', json.dumps(self.observed('runner')).encode(), directory)
        self.assertIsNone(canonical)
        self.assertFalse(report['accepted'])
        self.assertIsNone(report['canonical_sha256'])

    def test_duplicate_json_keys_are_rejected_even_if_the_values_match(self):
        for phase in PINS:
            observed = self.observed(phase)
            for field, value in observed.items():
                with self.subTest(phase=phase, field=field):
                    raw = ('{' + json.dumps(field) + ':' + json.dumps(value) + ',' + json.dumps(observed)[1:]).encode()
                    canonical, report = COMPAT.certify(phase, raw)
                    self.assertIsNone(canonical)
                    self.assertIn('Duplicate JSON key', report['reason'])

    def test_invalid_json_constants_encoding_and_nonobjects_fail(self):
        malformed = (b'{', b'{} trailing', b'\xff', b'null', b'true', b'1', b'[]', b'"identity"',
                     b'{"schema":NaN}', b'{"schema":Infinity}', b'{"schema":-Infinity}')
        for raw in malformed:
            with self.subTest(raw=raw):
                canonical, report = COMPAT.certify('runner', raw)
                self.assertIsNone(canonical)
                self.assertFalse(report['accepted'])
                self.assertEqual(report['observed_sha256'], hashlib.sha256(raw).hexdigest())

    def test_phase_cannot_be_substituted_or_inferred(self):
        for phase in PINS:
            for other in PINS:
                if phase != other:
                    self.assert_rejected(phase, self.observed(other))
        with self.assertRaises(ValueError):
            COMPAT.certify('unknown', b'{}')

    def test_evidence_is_specific_and_does_not_claim_whole_os_equality(self):
        _, report = self.certify('toolchain', self.observed('toolchain'))
        provenance = report['evidence_provenance']
        self.assertEqual(provenance['run_id'], '40092736702')
        self.assertEqual(provenance['artifact_id'], '12560010876')
        self.assertEqual(provenance['commit'], '4356d0a99a1894520e8dfa1455e5c99dc3ce5f8c')
        self.assertEqual(provenance['artifact_zip_sha256'],
                         '309b8daeb4e6d3af10cb15eeccc15b686fea9a97e0e5a285b11ed233ed88c1f4')
        self.assertEqual(provenance['installed_identity_sha256'],
                         'acbe7fbe607ac3818d5d51eb55c5188cd5d97599727db1348b41a6ff03972c65')
        self.assertIn('not whole-OS byte identity', provenance['limitation'])
        self.assertEqual(report['approved_image_pair'], [OLD_IMAGE, NEW_IMAGE])


class CompatibilityCliTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = pathlib.Path(self.directory.name)
        self.observed = self.root / 'raw.json'
        self.output = self.root / 'canonical.json'
        self.report = self.root / 'report.json'
        self.baseline_bytes = (ROOT / 'linux-diagnostic-baseline/runner-identity.json').read_bytes()
        value = json.loads(self.baseline_bytes)
        value['runner_image'] = NEW_IMAGE
        self.raw = json.dumps(value).encode()
        self.observed.write_bytes(self.raw)

    def arguments(self, observed=None, output=None, report=None):
        return ['runner', '--observed', str(observed or self.observed),
                '--output', str(output or self.output), '--report', str(report or self.report)]

    def run_cli(self, **paths):
        return subprocess.run([sys.executable, str(ROOT / 'linux_cache_compatibility.py')] + self.arguments(**paths),
                              capture_output=True, text=True)

    def test_cli_keeps_raw_evidence_and_writes_exact_bytes_and_audit(self):
        result = self.run_cli()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), PINS['runner'])
        self.assertEqual(self.observed.read_bytes(), self.raw)
        self.assertEqual(self.output.read_bytes(), self.baseline_bytes)
        report = json.loads(self.report.read_bytes())
        self.assertEqual(report['observed_path'], str(self.observed))
        self.assertEqual(report['canonical_path'], str(self.output))
        self.assertEqual(report['actual_image'], NEW_IMAGE)
        self.assertTrue(report['alias_applied'])

    def test_cli_rejection_removes_stale_canonical_and_reports_reason(self):
        self.assertEqual(self.run_cli().returncode, 0)
        raw = self.raw.replace(NEW_IMAGE.encode(), b'20269999.999.9')
        self.observed.write_bytes(raw)
        result = self.run_cli()
        self.assertEqual(result.returncode, 1)
        self.assertFalse(self.output.exists())
        self.assertEqual(self.observed.read_bytes(), raw)
        report = json.loads(self.report.read_bytes())
        self.assertEqual(report['status'], 'rejected')
        self.assertIsNone(report['canonical_sha256'])
        self.assertIn('unreviewed runner_image', report['reason'])

    def test_parse_failure_is_reported_without_canonical_output(self):
        self.observed.write_bytes(b'{')
        result = self.run_cli()
        self.assertEqual(result.returncode, 1)
        self.assertFalse(self.output.exists())
        self.assertFalse(json.loads(self.report.read_bytes())['accepted'])
        self.assertEqual(self.observed.read_bytes(), b'{')

    def test_output_or_report_cannot_overwrite_raw_by_path_or_links(self):
        for destination in ('output', 'report'):
            for kind in ('same', 'symlink', 'hardlink'):
                with self.subTest(destination=destination, kind=kind):
                    alias = self.root / (destination + '-' + kind)
                    if kind == 'symlink':
                        alias.symlink_to(self.observed)
                    elif kind == 'hardlink':
                        os.link(self.observed, alias)
                    else:
                        alias = self.observed
                    result = self.run_cli(**{destination: alias})
                    self.assertEqual(result.returncode, 1)
                    self.assertEqual(self.observed.read_bytes(), self.raw)
                    self.assertFalse(self.output.exists())

    def test_destinations_cannot_overwrite_any_pinned_baseline(self):
        for phase in PINS:
            baseline = ROOT / 'linux-diagnostic-baseline' / (phase + '-identity.json')
            before = baseline.read_bytes()
            for destination in ('output', 'report'):
                with self.subTest(phase=phase, destination=destination):
                    self.assertEqual(self.run_cli(**{destination: baseline}).returncode, 1)
                    self.assertEqual(baseline.read_bytes(), before)
                    self.assertEqual(self.observed.read_bytes(), self.raw)

    def test_report_and_output_paths_must_be_distinct_including_hardlinks(self):
        self.assertEqual(self.run_cli(report=self.output).returncode, 1)
        self.output.write_bytes(b'original')
        os.link(self.output, self.report)
        self.assertEqual(self.run_cli().returncode, 1)
        self.assertEqual(self.output.read_bytes(), b'original')
        self.assertEqual(self.observed.read_bytes(), self.raw)

    def test_report_write_failure_cleans_canonical_and_temporary_files(self):
        actual_write = COMPAT.atomic_write
        def fail_report(path, data):
            if path == self.report:
                raise OSError('simulated report I/O failure')
            actual_write(path, data)
        with mock.patch.object(COMPAT, 'atomic_write', side_effect=fail_report):
            self.assertEqual(COMPAT.main(self.arguments()), 1)
        self.assertFalse(self.output.exists())
        self.assertEqual(self.observed.read_bytes(), self.raw)
        self.assertEqual(list(self.root.glob('.*')), [])

    def test_atomic_replace_failure_leaves_no_partial_or_stale_canonical(self):
        self.output.write_bytes(b'stale')
        self.report.write_bytes(b'{"accepted":true}')
        with mock.patch.object(COMPAT.os, 'replace', side_effect=OSError('simulated replace failure')):
            self.assertEqual(COMPAT.main(self.arguments()), 1)
        self.assertFalse(self.output.exists())
        self.assertFalse(self.report.exists())
        self.assertEqual(self.observed.read_bytes(), self.raw)
        self.assertEqual(list(self.root.glob('.*')), [])

    def test_cli_has_no_baseline_or_image_override(self):
        result = subprocess.run([sys.executable, str(ROOT / 'linux_cache_compatibility.py')]
                                + self.arguments() + ['--baseline', str(self.observed)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertFalse(self.output.exists())


class WorkflowOrderingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        workflow = (ROOT.parent / '.github/workflows/vcmi-incremental.yml').read_text()
        cls.linux = workflow.split('\n  linux:\n', 1)[1].split('\n  macos:\n', 1)[0]
        cls.macos = workflow.split('\n  macos:\n', 1)[1]

    def test_raw_runner_and_environment_evidence_precede_alias_guards(self):
        guard = self.linux.index('python3 CI/linux_cache_compatibility.py runner ')
        for marker in ('--output incremental-evidence/runner-identity.json',
                       '--output incremental-evidence/dependency-identity.json',
                       "pathlib.Path('incremental-evidence/runner-environment.json').write_text(encoded)",
                       "print(pathlib.Path('incremental-evidence/runner-identity.json').read_text())"):
            self.assertLess(self.linux.index(marker), guard)

    def test_both_preinstall_aliases_precede_object_archive_restore(self):
        restore = self.linux.index('uses: actions/cache/restore@')
        self.assertLess(self.linux.index('python3 CI/linux_cache_compatibility.py runner '), restore)
        self.assertLess(self.linux.index('python3 CI/linux_cache_compatibility.py dependency '), restore)
        self.assertIn('key=$(sha256sum incremental-evidence/cache-runner-identity.json', self.linux[:restore])
        self.assertIn('depkey=$(sha256sum incremental-evidence/cache-dependency-identity.json', self.linux[:restore])

    def test_installed_alias_precedes_existing_strict_restore_and_build(self):
        observed = self.linux.index('python3 CI/incremental_identity.py linux-gcc-test --installed --output incremental-evidence/toolchain-identity.json')
        guard = self.linux.index('python3 CI/linux_cache_compatibility.py toolchain ')
        strict_restore = self.linux.index('python3 CI/incremental_cache.py restore ')
        build = self.linux.index('run: bash CI/incremental-build.sh linux-gcc-test')
        self.assertLess(observed, guard)
        self.assertLess(guard, strict_restore)
        self.assertLess(strict_restore, build)
        self.assertNotIn('continue-on-error', self.linux[observed:build])
        for line in self.linux.splitlines():
            if 'python3 CI/incremental_cache.py ' in line:
                self.assertIn('--fingerprint incremental-evidence/cache-toolchain-identity.json', line)

    def test_alias_commands_never_overwrite_raw_and_remain_qualification_scoped(self):
        guards = [line.strip() for line in self.linux.splitlines() if 'python3 CI/linux_cache_compatibility.py ' in line]
        self.assertEqual(len(guards), 3)
        for line in guards:
            args = shlex.split(line)
            phase = args[2]
            self.assertIn(phase, PINS)
            self.assertEqual(args[args.index('--observed') + 1], 'incremental-evidence/' + phase + '-identity.json')
            self.assertEqual(args[args.index('--output') + 1], 'incremental-evidence/cache-' + phase + '-identity.json')
            self.assertEqual(args[args.index('--report') + 1], 'incremental-evidence/' + phase + '-compatibility.json')
            prefix = self.linux[:self.linux.index(line)]
            last_if = prefix.rsplit('if [[ ', 1)[1]
            self.assertIn("contains(github.event.head_commit.message, '[adventure-qualify]')", last_if)
            self.assertNotIn('\n        fi', last_if)
            self.assertNotIn('||', line)
        for line in self.linux.splitlines():
            if 'python3 CI/compiler_recovery.py ' in line:
                self.assertIn('--identity incremental-evidence/cache-toolchain-identity.json', line)

    def test_installed_alias_and_strict_restore_precede_recovery_smoke_and_scope(self):
        guard = self.linux.index('python3 CI/linux_cache_compatibility.py toolchain ')
        strict_restore = self.linux.index('python3 CI/incremental_cache.py restore ')
        smoke = self.linux.index("python3 -m unittest discover -s CI -p 'test_compiler_recovery.py'")
        scope = self.linux.index('python3 CI/compiler_recovery.py prepare ')
        supplement = self.linux.index('id: compiler_cache\n')
        self.assertLess(guard, strict_restore)
        self.assertLess(strict_restore, smoke)
        self.assertLess(smoke, scope)
        self.assertLess(scope, supplement)
        # A failed strict alias/restore must not become a successful recovery gate.
        gate = self.linux[guard:scope]
        self.assertNotIn('continue-on-error', gate)
        self.assertNotIn('||', gate)
        self.assertNotIn('always()', gate)
        self.assertIn("if: steps.verified_cache.outcome == 'success' && startsWith(github.event.head_commit.message, '[vcmi-warm] ') && contains(github.event.head_commit.message, '[adventure-qualify]')", gate)

    def test_recovery_still_requires_exact_pinned_installed_identity_on_both_images(self):
        baseline = (ROOT / 'linux-diagnostic-baseline/toolchain-identity.json').read_bytes()
        for image in (OLD_IMAGE, NEW_IMAGE):
            observed = json.loads(baseline)
            observed['runner_image'] = image
            canonical, report = COMPAT.certify('toolchain', json.dumps(observed).encode())
            self.assertTrue(report['accepted'])
            self.assertEqual(hashlib.sha256(canonical).hexdigest(), PINS['toolchain'])
            observed['compiler_sha256'] = '0' * 64
            canonical, report = COMPAT.certify('toolchain', json.dumps(observed).encode())
            self.assertIsNone(canonical)
            self.assertFalse(report['accepted'])
        observed = json.loads(baseline)
        observed['runner_image'] = '20261024.334.3'
        canonical, report = COMPAT.certify('toolchain', json.dumps(observed).encode())
        self.assertIsNone(canonical)
        self.assertFalse(report['accepted'])

    def test_macos_retains_direct_exact_fingerprints_and_no_alias(self):
        self.assertNotIn('linux_cache_compatibility.py', self.macos)
        self.assertNotIn('cache-toolchain-identity.json', self.macos)
        self.assertIn('python3 CI/incremental_identity.py macos-arm-conan-ninja-release --installed --output incremental-evidence/toolchain-identity.json', self.macos)
        for line in self.macos.splitlines():
            if 'python3 CI/incremental_cache.py ' in line:
                self.assertIn('--fingerprint incremental-evidence/toolchain-identity.json', line)


if __name__ == '__main__':
    unittest.main()
