#!/usr/bin/env python3
"""Regression tests for fail-closed, compiler-only failure recovery."""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock


spec = importlib.util.spec_from_file_location('compiler_recovery', Path(__file__).with_name('compiler_recovery.py'))
recovery = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recovery)


class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.home = self.root / 'home'
        self.home.mkdir()
        self.cache = self.home / '.cache/vcmi-incremental/linux-gcc-test'
        self.cache.mkdir(parents=True)
        self.config = self.cache / 'ccache.conf'
        self.config.write_text('compiler_check = content\nsloppiness =\nmax_size = 768.0 MB\n')
        self.binary = self.root / 'ccache'
        self.binary.write_bytes(b'fixture executable')
        self.identity = self.root / 'identity.json'
        self.validation = self.root / 'validation.json'
        identity = {name: 'fixture' for name in recovery.IDENTITY_STRINGS}
        identity.update({name: 'a' * 64 for name in identity if name.endswith('_sha256')})
        identity.update(schema=1, profile=recovery.PROFILE, os='Linux', home=str(self.home),
                        workspace=str(self.root), runner_image='20261012.334.1',
                        dependency_commit='b' * 40,
                        ccache_sha256=hashlib.sha256(self.binary.read_bytes()).hexdigest())
        self.identity.write_text(json.dumps(identity))
        self.validation.write_text(json.dumps({
            'operation': 'restore', 'verified_build_files': 1288,
            'verified_external_files': 0, 'unchanged_sources': 10,
            'changed_sources': ['AI/Nullkiller2/example.cpp'], 'removed_sources': [],
        }))
        self.addCleanup(mock.patch.stopall)
        mock.patch.object(recovery.Path, 'home', return_value=self.home).start()
        mock.patch.object(recovery.Path, 'cwd', return_value=self.root).start()
        mock.patch.object(recovery.shutil, 'which', return_value=str(self.binary)).start()
        self.run = mock.patch.object(recovery.subprocess, 'run', side_effect=self.fake_ccache).start()
        self.entry = self.add_entry('abcd' + '1' * 29 + 'R', b'first completed result')

    def fake_ccache(self, arguments, **kwargs):
        if '--inspect' in arguments:
            kind = '0 (result)' if arguments[-1].endswith('R') else '1 (manifest)'
            return subprocess.CompletedProcess(arguments, 0, 'Entry type: ' + kind + '\nSelf-contained: yes\n', '')
        return subprocess.CompletedProcess(arguments, 0, '', '')

    def add_entry(self, name, data, depth=2):
        path = self.cache.joinpath(*name[:depth], name[depth:])
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def change_json(self, path, **changes):
        value = json.loads(path.read_text())
        value.update(changes)
        path.write_text(json.dumps(value))

    def execute(self, operation='check', baseline=None):
        return recovery.recover(operation, self.identity, self.validation, baseline)

    def baseline(self):
        path = self.root / 'baseline.json'
        path.write_text(json.dumps(self.execute()))
        return path

    def test_prepare_hashes_exact_complete_installed_identity_without_executing_ccache(self):
        result = self.execute('prepare')
        self.assertEqual(result['fingerprint'], hashlib.sha256(self.identity.read_bytes()).hexdigest())
        self.assertEqual(result['cache_dir'], str(self.cache))
        self.assertNotIn('result_entries', result)
        self.run.assert_not_called()
        self.identity.write_bytes(self.identity.read_bytes() + b'\n')
        self.assertNotEqual(result['fingerprint'], self.execute('prepare')['fingerprint'])

    def test_every_installed_identity_field_is_required(self):
        original = self.identity.read_bytes()
        for field in recovery.IDENTITY_STRINGS:
            with self.subTest(field=field):
                value = json.loads(original)
                del value[field]
                self.identity.write_text(json.dumps(value))
                with self.assertRaisesRegex(ValueError, 'Incomplete installed identity'):
                    self.execute('prepare')
        self.run.assert_not_called()

    def test_rejects_bad_identity_profile_paths_and_hashes(self):
        original = self.identity.read_bytes()
        for key, value in [('schema', True), ('schema', 2), ('profile', 'macos'),
                           ('os', 'Darwin'), ('home', '/other'), ('workspace', '/other'),
                           ('runner_image', 'unknown'), ('compiler_sha256', 'not-a-hash'),
                           ('dependency_commit', 'main')]:
            with self.subTest(key=key):
                self.identity.write_bytes(original)
                self.change_json(self.identity, **{key: value})
                with self.assertRaises(ValueError):
                    self.execute('prepare')

    def test_requires_successful_strict_restore_not_snapshot_empty_or_partial(self):
        original = self.validation.read_bytes()
        for key, value in [('operation', 'snapshot'), ('operation', 'failed'),
                           ('verified_build_files', 0), ('verified_build_files', True),
                           ('verified_external_files', -1), ('unchanged_sources', None),
                           ('changed_sources', 'not-a-list'), ('removed_sources', [3])]:
            with self.subTest(key=key):
                self.validation.write_bytes(original)
                self.change_json(self.validation, **{key: value})
                with self.assertRaises(ValueError):
                    self.execute()
        self.run.assert_not_called()

    def test_missing_and_duplicate_evidence_fail_closed(self):
        self.validation.unlink()
        with self.assertRaises(OSError):
            self.execute('prepare')
        self.validation.write_text('{"operation":"restore","operation":"snapshot"}')
        with self.assertRaisesRegex(ValueError, 'Duplicate JSON key'):
            self.execute('prepare')

    def test_standard_ccache_tree_and_contents_are_validated(self):
        (self.cache / 'tmp').mkdir()
        (self.cache / 'tmp/.cleaned').touch()
        (self.cache / 'lock').mkdir()
        (self.cache / 'a/stats').write_text('0\n123\n5\n')
        (self.cache / 'a/b/stats').write_text('1\n2\n')
        (self.cache / 'a/CACHEDIR.TAG').write_text(
            'Signature: 8a477f597d28d172789f06886806bc55\n'
            '# This file is a cache directory tag created by ccache.\n'
            '# For information about cache directory tags, see:\n'
            '#\thttps://bford.info/cachedir/\n')
        self.add_entry('1234' + 'v' * 29 + 'M', b'manifest', depth=4)
        result = self.execute()
        self.assertEqual(len(result['result_entries']), 1)
        self.assertFalse(result['save_needed'])
        self.assertEqual(result['logical_bytes'], sum(p.stat().st_size for p in self.cache.rglob('*') if p.is_file()))
        self.assertEqual(self.run.call_count, 2)

    def test_rejects_unapproved_file_names_and_unfinished_files(self):
        for name in ['.env', 'build.ninja', '.ninja_log', 'source.cpp', 'secret',
                     'tmp/output.o', 'tmp/.cleaned', 'lock/subdir_ab',
                     'a/b/result.o', 'a/b/' + '1' * 31 + 'W']:
            with self.subTest(name=name):
                path = self.cache / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('unexpected')
                with self.assertRaises(ValueError):
                    self.execute('bound')
                path.unlink()
        self.run.assert_not_called()

    def test_rejects_unapproved_directories(self):
        for name in ['out', '.ssh', 'tmp/secrets', 'g', 'a/b/c/d/e']:
            with self.subTest(name=name):
                path = self.cache / name
                path.mkdir(parents=True, exist_ok=True)
                with self.assertRaises(ValueError):
                    self.execute()
                path.rmdir()

    def test_rejects_symlink_file_directory_root_and_ancestor(self):
        outside = self.root / 'outside'
        outside.mkdir()
        for path, target in [(self.cache / 'secret', self.identity),
                             (self.cache / '0', outside)]:
            path.symlink_to(target)
            with self.assertRaises(ValueError):
                self.execute()
            path.unlink()
        moved = self.cache.with_name('moved')
        self.cache.rename(moved)
        self.cache.symlink_to(moved)
        with self.assertRaisesRegex(ValueError, 'Symlink'):
            self.execute()
        self.cache.unlink()
        moved.rename(self.cache)
        ancestor = self.home / '.cache'
        relocated = self.home / 'relocated'
        ancestor.rename(relocated)
        ancestor.symlink_to(relocated)
        with self.assertRaisesRegex(ValueError, 'Symlink'):
            self.execute('prepare')
        self.run.assert_not_called()

    def test_rejects_evidence_symlink(self):
        other = self.identity.with_name('other.json')
        self.identity.rename(other)
        self.identity.symlink_to(other)
        with self.assertRaisesRegex(ValueError, 'Symlink'):
            self.execute('prepare')

    def test_rejects_hardlinks_and_fifo(self):
        hardlink = self.cache / 'hardlink'
        os.link(self.identity, hardlink)
        with self.assertRaisesRegex(ValueError, 'regular files'):
            self.execute()
        hardlink.unlink()
        pipe = self.cache / 'pipe'
        os.mkfifo(pipe)
        with self.assertRaisesRegex(ValueError, 'regular files'):
            self.execute()
        self.run.assert_not_called()

    def test_root_is_not_caller_expandable(self):
        with self.assertRaisesRegex(ValueError, 'Only the existing profile'):
            recovery.inventory(self.home)

    def test_unreadable_directory_is_not_silently_omitted(self):
        def unreadable(root, *, followlinks, onerror):
            onerror(PermissionError('unreadable cache directory'))
        with mock.patch.object(recovery.os, 'walk', side_effect=unreadable):
            with self.assertRaises(PermissionError):
                self.execute()
        self.run.assert_not_called()

    def test_rejects_risky_config_before_ccache_execution(self):
        original = self.config.read_text()
        for key in ['remote_storage', 'prefix_command', 'compiler', 'extra_files_to_hash',
                    'cache_dir', 'hard_link', 'ignore_options', 'log_file', 'sloppiness']:
            with self.subTest(key=key):
                self.config.write_text(original + key + ' = dangerous\n')
                with self.assertRaises(ValueError):
                    self.execute('bound')
        self.run.assert_not_called()

    def test_rejects_wrong_existing_config_values_and_comments(self):
        original = self.config.read_text()
        for text in [original.replace('content', 'none'), original.replace('768.0 MB', '0'),
                     original.replace('sloppiness =', 'sloppiness = time_macros'),
                     original + '# saved secret\n', 'compiler_check = content\n']:
            self.config.write_text(text)
            with self.assertRaises(ValueError):
                self.execute()
        self.run.assert_not_called()

    def test_missing_config_rejected(self):
        self.config.unlink()
        with self.assertRaisesRegex(ValueError, 'configuration missing'):
            self.execute()

    def test_bad_metadata_and_empty_result_rejected(self):
        for relative, data in [('a/stats', 'secret\n'), ('a/CACHEDIR.TAG', 'secret\n')]:
            path = self.cache / relative
            path.write_text(data)
            with self.assertRaises(ValueError):
                self.execute()
            path.unlink()
        self.entry.write_bytes(b'')
        with self.assertRaisesRegex(ValueError, 'Invalid/oversized'):
            self.execute()

    def test_bound_rechecks_logical_bytes_not_ccache_statistics(self):
        baseline = self.baseline()
        with mock.patch.object(recovery, 'MAX_BYTES', 512):
            with self.entry.open('wb') as stream:
                stream.truncate(400)
            second = self.add_entry('eeee' + '2' * 29 + 'R', b'x' * 400)
            with self.assertRaisesRegex(ValueError, 'exceeds 768 MiB'):
                self.execute()
            with self.assertRaisesRegex(ValueError, 'exceeds 768 MiB'):
                self.execute('bound', baseline)
            self.assertTrue(second.exists())  # The helper does not delete arbitrary files.

    def test_oversized_individual_entry_rejected_before_inspection(self):
        with self.entry.open('wb') as stream:
            stream.truncate(recovery.MAX_BYTES + 1)
        with self.assertRaisesRegex(ValueError, 'oversized'):
            self.execute('bound')
        self.run.assert_not_called()

    def test_incompatible_ccache_executable_never_runs(self):
        self.binary.write_bytes(b'changed executable')
        with self.assertRaisesRegex(ValueError, 'binary does not match'):
            self.execute('bound')
        self.run.assert_not_called()

    def test_environment_overrides_are_removed_and_config_is_fixed(self):
        with mock.patch.dict(os.environ, {'CCACHE_PREFIX': 'bad', 'CCACHE_REMOTE_STORAGE': 'secret',
                                         'CCACHE_DIR': '/other', 'CCACHE_CONFIGPATH': '/other.conf'}):
            self.execute()
        env = self.run.call_args.kwargs['env']
        self.assertEqual({k: v for k, v in env.items() if k.startswith('CCACHE_')}, {
            'CCACHE_DIR': str(self.cache), 'CCACHE_CONFIGPATH': str(self.config)})

    def test_inspection_failure_or_nonselfcontained_result_rejected(self):
        self.run.side_effect = subprocess.CalledProcessError(1, ['ccache', '--inspect'])
        with self.assertRaises(subprocess.CalledProcessError):
            self.execute()
        self.run.side_effect = None
        self.run.return_value = subprocess.CompletedProcess([], 0, 'Entry type: 0 (result)\nSelf-contained: no\n', '')
        with self.assertRaisesRegex(ValueError, 'self-contained'):
            self.execute()

    def test_cleanup_failure_is_not_success(self):
        def fail_cleanup(arguments, **kwargs):
            if '--cleanup' in arguments:
                raise subprocess.CalledProcessError(1, arguments)
            return self.fake_ccache(arguments, **kwargs)
        self.run.side_effect = fail_cleanup
        with self.assertRaises(subprocess.CalledProcessError):
            self.execute('bound')

    def test_bound_validates_new_contents_after_cleanup(self):
        def inject_during_cleanup(arguments, **kwargs):
            if '--cleanup' in arguments:
                (self.cache / '.env').write_text('not allowed')
            return self.fake_ccache(arguments, **kwargs)
        self.run.side_effect = inject_during_cleanup
        with self.assertRaisesRegex(ValueError, 'Non-ccache'):
            self.execute('bound')

    def test_stats_config_manifest_only_or_removed_results_do_not_trigger_save(self):
        baseline = self.baseline()
        (self.cache / 'a/stats').write_text('1234\n')
        self.config.write_text(self.config.read_text().replace('768.0 MB', '768M'))
        self.add_entry('1234' + 'v' * 29 + 'M', b'new manifest')
        self.assertFalse(self.execute('bound', baseline)['save_needed'])
        self.entry.unlink()
        self.assertFalse(self.execute('bound', baseline)['save_needed'])

    def test_changed_and_new_result_content_trigger_save_even_after_failed_build(self):
        baseline = self.baseline()
        # There is intentionally no build/test-success prerequisite for bound.
        self.entry.write_bytes(b'new successful result in otherwise failed build')
        self.add_entry('1234' + 'u' * 29 + 'R', b'another completed result')
        result = self.execute('bound', baseline)
        self.assertTrue(result['save_needed'])
        self.assertEqual(result['new_or_changed_results'], 2)

    def test_cache_level_movement_alone_does_not_trigger_save(self):
        baseline = self.baseline()
        name = ''.join(self.entry.relative_to(self.cache).parts)
        data = self.entry.read_bytes()
        self.entry.unlink()
        self.add_entry(name, data, depth=3)
        self.assertFalse(self.execute('bound', baseline)['save_needed'])

    def test_missing_or_incompatible_baseline_never_approves_save(self):
        self.assertFalse(self.execute('bound')['save_needed'])
        baseline = self.baseline()
        self.change_json(baseline, fingerprint='0' * 64)
        self.run.reset_mock()
        with self.assertRaisesRegex(ValueError, 'Baseline'):
            self.execute('bound', baseline)
        self.run.assert_not_called()

    def test_cli_outputs_only_after_success(self):
        output = self.root / 'github-output'
        report = self.root / 'report.json'
        args = ['prepare', '--identity', str(self.identity), '--validation', str(self.validation),
                '--github-output', str(output), '--report', str(report)]
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(recovery.main(args), 0)
        result = json.loads(report.read_text())
        self.assertEqual(output.read_text(), 'fingerprint=' + result['fingerprint'] + '\nsave-needed=false\n')
        output.unlink()
        report.unlink()
        self.validation.unlink()
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(recovery.main(args), 2)
        self.assertFalse(output.exists())
        self.assertFalse(report.exists())


@unittest.skipUnless(shutil.which('ccache') and (shutil.which('c++') or shutil.which('g++')),
                     'Native ccache and C++ compiler required')
class NativeCcacheTests(unittest.TestCase):
    def test_native_success_failure_layout_checksum_and_cleanup(self):
        """Exercise the installed ccache format, not a hand-built binary fixture."""
        ccache = shutil.which('ccache')
        compiler = shutil.which('g++-14') or shutil.which('c++') or shutil.which('g++')
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            cache = root / '.cache/vcmi-incremental/linux-gcc-test'
            cache.mkdir(parents=True)
            environment = {key: value for key, value in os.environ.items() if not key.startswith('CCACHE_')}
            environment.update(CCACHE_DIR=str(cache), CCACHE_CONFIGPATH=str(cache / 'ccache.conf'))

            def native(*arguments, check=True):
                return subprocess.run([ccache, *arguments], check=check, env=environment,
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=root)

            def hits():
                # IDs also present in retained Linux ccache 4.9.1 --print-stats evidence.
                counters = dict(line.split('\t', 1) for line in native('--print-stats').stdout.splitlines())
                return int(counters['direct_cache_hit']) + int(counters['preprocessed_cache_hit'])

            native('--set-config=compiler_check=content')
            native('--set-config=sloppiness=')
            native('--max-size=768M')
            source = root / 'fixture.cpp'
            source.write_text('int fixture() { return 1; }\n')
            compile_arguments = (compiler, '-std=c++20', '-c', str(source), '-o', str(root / 'fixture.o'))
            native(*compile_arguments)
            identity = {name: 'fixture' for name in recovery.IDENTITY_STRINGS}
            identity.update({name: 'a' * 64 for name in identity if name.endswith('_sha256')})
            identity.update(schema=1, profile=recovery.PROFILE, os='Linux', home=str(root),
                            workspace=str(root), runner_image='fixture', dependency_commit='b' * 40,
                            ccache_sha256=recovery.sha256(Path(ccache)))
            identity_path = root / 'identity.json'
            identity_path.write_text(json.dumps(identity))
            validation_path = root / 'validation.json'
            validation_path.write_text(json.dumps({
                'operation': 'restore', 'verified_build_files': 1, 'verified_external_files': 0,
                'unchanged_sources': 1, 'changed_sources': [], 'removed_sources': [],
            }))
            with mock.patch.object(recovery.Path, 'home', return_value=root), \
                    mock.patch.object(recovery.Path, 'cwd', return_value=root):
                baseline = recovery.recover('check', identity_path, validation_path)
                self.assertGreater(len(baseline['result_entries']), 0)
                baseline_path = root / 'baseline.json'
                baseline_path.write_text(json.dumps(baseline))
                hits_before = hits()
                native(*compile_arguments)
                self.assertEqual(hits(), hits_before + 1, 'Identical C++ compile must use a real ccache hit')
                unchanged = recovery.recover('bound', identity_path, validation_path, baseline_path)
                self.assertFalse(unchanged['save_needed'])
                self.assertEqual(unchanged['new_or_changed_results'], 0)
                source.write_text('int fixture() { return 2; }\n')
                native(*compile_arguments)
                source.write_text('This must fail to compile.\n')
                self.assertNotEqual(native(*compile_arguments, check=False).returncode, 0)
                result = recovery.recover('bound', identity_path, validation_path, baseline_path)
                self.assertTrue(result['save_needed'])
                self.assertLessEqual(result['logical_bytes'], recovery.MAX_BYTES)
                entry = next(path for path in cache.rglob('*R') if path.is_file())
                data = entry.read_bytes()
                entry.write_bytes(data[:-1] + bytes([data[-1] ^ 1]))
                with self.assertRaises(subprocess.CalledProcessError):
                    recovery.recover('check', identity_path, validation_path)


class WorkflowContractTests(unittest.TestCase):
    """Check the narrow workflow gates without adding a YAML dependency."""
    @classmethod
    def setUpClass(cls):
        path = Path(__file__).resolve().parents[1] / '.github/workflows/vcmi-incremental.yml'
        cls.workflow = path.read_text()
        cls.linux, cls.macos = cls.workflow.split('  linux:\n', 1)[1].split('  macos:\n', 1)
        cls.steps = re.split(r'(?=^    - name: )', cls.linux, flags=re.MULTILINE)

    def step(self, text):
        matches = [step for step in self.steps if text in step]
        self.assertEqual(len(matches), 1, text)
        return matches[0]

    def test_recovery_follows_strict_restore_and_precedes_build(self):
        ordered = ['id: verified_cache\n', 'id: compiler_scope\n', 'id: compiler_cache\n',
                   'id: compiler_ready\n', 'id: build\n', 'id: compiler_save_ready\n']
        positions = [self.linux.index(text) for text in ordered]
        self.assertEqual(positions, sorted(positions))
        scope = self.step('id: compiler_scope\n')
        self.assertIn("steps.verified_cache.outcome == 'success'", scope)
        self.assertIn("'[vcmi-warm] '", scope)
        self.assertIn("'[adventure-qualify]'", scope)
        self.assertIn('compiler_recovery.py prepare --identity', scope)
        self.assertIn('--validation incremental-evidence/cache-validation.json', scope)
        self.assertNotIn('always()', scope)
        verified = self.step('id: verified_cache\n')
        self.assertIn('incremental_identity.py linux-gcc-test --installed', verified)
        self.assertIn('incremental_cache.py restore', verified)

    def test_restore_is_ccache_only_exact_identity_and_helper_protocol(self):
        restore = self.step('id: compiler_cache\n')
        self.assertIn("if: steps.compiler_scope.outcome == 'success'", restore)
        self.assertIn('uses: actions/cache/restore@', restore)
        self.assertIn('path: ~/.cache/vcmi-incremental/linux-gcc-test\n', restore)
        self.assertNotIn('out/build', restore)
        self.assertNotIn('.ci/', restore)
        prefix = "vcmi-compiler-v1-linux-${{ steps.compiler_scope.outputs.fingerprint }}-${{ hashFiles('CI/compiler_recovery.py') }}-"
        self.assertIn('key: ' + prefix + '${{ github.sha }}-${{ github.run_id }}-${{ github.run_attempt }}', restore)
        self.assertIn('restore-keys: ' + prefix + '\n', restore)
        self.assertEqual(restore.count('restore-keys:'), 1)
        self.assertNotIn('fail-on-cache-miss: true', restore)
        self.assertNotIn('continue-on-error', restore)
        check = self.step('id: compiler_ready\n')
        self.assertIn('compiler_recovery.py check', check)
        self.assertIn('--report incremental-evidence/compiler-recovery-restored.json', check)
        self.assertNotIn('continue-on-error', check)

    def test_failure_recovery_requires_validation_non_cancelled_build_and_real_delta(self):
        bound = self.step('id: compiler_save_ready\n')
        for required in ['always() && !cancelled()', "steps.verified_cache.outcome == 'success'",
                         "steps.compiler_ready.outcome == 'success'",
                         "(steps.build.outcome == 'success' || steps.build.outcome == 'failure')",
                         'compiler_recovery.py bound',
                         '--baseline incremental-evidence/compiler-recovery-restored.json']:
            self.assertIn(required, bound)
        save = self.step('name: Retain successful compiler entries without promoting incomplete build state')
        for required in ['always() && !cancelled()', "steps.compiler_save_ready.outcome == 'success'",
                         "steps.compiler_save_ready.outputs.save-needed == 'true'",
                         'path: ~/.cache/vcmi-incremental/linux-gcc-test\n',
                         'key: ${{ steps.compiler_cache.outputs.cache-primary-key }}']:
            self.assertIn(required, save)
        self.assertNotIn('out/build', save)
        self.assertNotIn('.ci/', save)

    def test_full_build_snapshot_and_save_still_success_only(self):
        snapshot = self.step('name: Snapshot only successful build outputs, with bounded upload size')
        full_save = self.step('name: Persist new cache generation (not a frozen first-build key)')
        for step in [snapshot, full_save]:
            self.assertNotIn('always()', step)
            self.assertNotIn('failure()', step)
            self.assertNotIn('continue-on-error', step)
        self.assertIn('incremental_cache.py snapshot', snapshot)
        self.assertIn('out/build/linux-gcc-test', full_save)
        self.assertIn('.ci/incremental-state.json', full_save)
        self.assertIn('key: ${{ steps.cache.outputs.cache-primary-key }}', full_save)

    def test_recovery_is_linux_only_and_is_tested_in_preflight(self):
        self.assertNotIn('compiler_recovery.py', self.macos)
        self.assertNotIn('vcmi-compiler-v1', self.macos)
        self.assertIn('python3 -m unittest discover -s CI -p "test_compiler_recovery.py" -v',
                      self.workflow.split('  linux:\n', 1)[0])


if __name__ == '__main__':
    unittest.main()
