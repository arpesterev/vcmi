#!/usr/bin/env python3
"""Mac-only recovery contracts, pinned real identity, and required native smoke."""
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
import sys
import tempfile
import unittest
from unittest import mock


HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('macos_compiler_recovery', HERE / 'macos_compiler_recovery.py')
recovery = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recovery)
FIXTURE = HERE / 'macos-compiler-recovery-baseline/toolchain-identity.json'


def restore_evidence():
    # Counts from qualification9-mac-portability's successful strict restore.
    return dict(operation='restore', verified_build_files=1297, verified_external_files=22450,
                unchanged_sources=4688, changed_sources=['AI/Nullkiller2/example.cpp'], removed_sources=[])


def installed_identity(home, workspace, ccache):
    identity = json.loads(FIXTURE.read_bytes())
    identity.update(home=str(home), workspace=str(workspace),
                    conanfile_sha256={str(workspace / 'conanfile.py'): 'a' * 64},
                    ccache_sha256=recovery.core.sha256(ccache))
    return identity


class MacRecoveryTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.home = self.root / 'home'
        self.cache = self.home / '.cache/vcmi-incremental' / recovery.PROFILE
        self.cache.mkdir(parents=True)
        self.config = self.cache / 'ccache.conf'
        self.config.write_text('compiler_check = content\nsloppiness =\nmax_size = 768.0 MB\n')
        self.binary = self.root / 'ccache'
        self.binary.write_bytes(b'fixture executable')
        self.identity = self.root / 'identity.json'
        self.identity.write_text(json.dumps(installed_identity(self.home, self.root, self.binary)))
        self.validation = self.root / 'validation.json'
        self.validation.write_text(json.dumps(restore_evidence()))
        self.addCleanup(mock.patch.stopall)
        mock.patch.object(Path, 'home', return_value=self.home).start()
        mock.patch.object(Path, 'cwd', return_value=self.root).start()
        mock.patch.object(recovery.core.shutil, 'which', return_value=str(self.binary)).start()
        self.run = mock.patch.object(subprocess, 'run', side_effect=self.fake_ccache).start()
        self.entry = self.add_entry('abcd' + '1' * 36, b'result')

    def add_entry(self, key, data=b'result', depth=2):
        path = self.cache.joinpath(*key[:depth], key[depth:])
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def fake_ccache(self, arguments, **kwargs):
        output = ''
        if '--inspect' in arguments:
            kind = '1 (manifest)' if Path(arguments[-1]).read_bytes().startswith(b'manifest') else '0 (result)'
            output = 'Entry type: ' + kind + '\nSelf-contained: yes\n'
        return subprocess.CompletedProcess(arguments, 0, output, '')

    def change_json(self, path, **changes):
        value = json.loads(path.read_bytes())
        value.update(changes)
        path.write_text(json.dumps(value))

    def execute(self, operation='check', baseline=None):
        return recovery.recover(operation, self.identity, self.validation, baseline)

    def baseline(self):
        path = self.root / 'baseline.json'
        path.write_text(json.dumps(self.execute()))
        return path

    def inode_index(self):
        path = self.cache / 'tmp' / recovery.INODE_CACHE_NAME
        path.parent.mkdir(exist_ok=True)
        with path.open('wb') as stream:
            stream.write(b'\x03\x00\x00\x00')
            stream.truncate(recovery.INODE_CACHE_BYTES)
        return path

    def test_real_installed_fixture_and_full_byte_identity(self):
        installed = json.loads(FIXTURE.read_bytes())
        self.assertEqual(installed['ccache'].splitlines()[0], 'ccache version 4.14')
        self.assertEqual(installed['runner_image'], '20260907.0337.1')
        with mock.patch.object(Path, 'home', return_value=Path(installed['home'])), \
                mock.patch.object(Path, 'cwd', return_value=Path(installed['workspace'])):
            prepared = recovery.recover('prepare', FIXTURE, self.validation)
        self.assertEqual(prepared['fingerprint'], hashlib.sha256(FIXTURE.read_bytes()).hexdigest())
        result = self.execute('prepare')
        self.identity.write_bytes(self.identity.read_bytes() + b'\n')
        self.assertNotEqual(result['fingerprint'], self.execute('prepare')['fingerprint'])
        self.run.assert_not_called()

    def test_every_installed_identity_field_required(self):
        original = self.identity.read_bytes()
        for field in (*recovery.IDENTITY_STRINGS, 'conanfile_sha256'):
            with self.subTest(field=field):
                value = json.loads(original)
                del value[field]
                self.identity.write_text(json.dumps(value))
                with self.assertRaises(ValueError):
                    self.execute('prepare')
        self.run.assert_not_called()

    def test_unknown_wrong_platform_paths_format_and_invalid_hashes_fail(self):
        original = self.identity.read_bytes()
        for key, value in [('schema', True), ('schema', 2), ('profile', 'linux-gcc-test'),
                           ('os', 'Linux'), ('arch', 'x86_64'), ('runner_image', 'unknown'),
                           ('home', '/elsewhere'), ('workspace', '/elsewhere'),
                           ('ccache', 'ccache version 4.15'), ('compiler_sha256', 'bad'),
                           ('conan_packages_sha256', 'bad'), ('conan_generated_sha256', ''),
                           ('sdk_path', 'relative'), ('dependency_commit', 'main'),
                           ('conanfile_sha256', {}), ('conanfile_sha256', {'/other/conanfile.py': 'a'*64})]:
            with self.subTest(key=key, value=value):
                self.identity.write_bytes(original)
                self.change_json(self.identity, **{key: value})
                with self.assertRaises(ValueError):
                    self.execute('prepare')
        self.run.assert_not_called()

    def test_no_cold_or_partial_restore_or_snapshot(self):
        original = self.validation.read_bytes()
        for key, value in [('operation', 'snapshot'), ('operation', 'failed'),
                           ('verified_build_files', 0), ('verified_build_files', True),
                           ('verified_external_files', 0), ('verified_external_files', -1),
                           ('unchanged_sources', None), ('changed_sources', ''), ('removed_sources', [3])]:
            with self.subTest(key=key):
                self.validation.write_bytes(original)
                self.change_json(self.validation, **{key: value})
                with self.assertRaises(ValueError):
                    self.execute()
        self.run.assert_not_called()

    def test_duplicate_or_missing_evidence_rejected(self):
        self.identity.write_text('{"schema":1,"schema":1}')
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            self.execute('prepare')
        self.identity.unlink()
        with self.assertRaises(OSError):
            self.execute('prepare')
        self.run.assert_not_called()

    def test_base_hash_and_linux_import_are_unchanged(self):
        base = HERE / 'compiler_recovery.py'
        self.assertEqual(recovery.core.sha256(base), recovery.BASE_SHA256)
        changed = self.root / 'modified.py'
        changed.write_bytes(base.read_bytes() + b'\n')
        with self.assertRaisesRegex(ValueError, 'explicit review'):
            recovery.load_core(changed)
        spec = importlib.util.spec_from_file_location('linux_recovery_isolation_test', base)
        linux = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(linux)
        self.assertEqual(linux.PROFILE, 'linux-gcc-test')
        self.assertTrue(linux.ENTRY_NAME.fullmatch('abcd' + '1' * 29 + 'R'))
        self.assertFalse(linux.ENTRY_NAME.fullmatch('abcd' + '1' * 36))

    def test_only_native_typed_hex40_entries_and_known_housekeeping(self):
        self.add_entry('bcde' + '2' * 36, b'manifest', depth=4)
        (self.cache / 'a/stats').write_text('0\n12\n')
        (self.cache / 'a/CACHEDIR.TAG').write_text(
            'Signature: 8a477f597d28d172789f06886806bc55\n'
            '# This file is a cache directory tag created by ccache.\n'
            '# For information about cache directory tags, see:\n'
            '#\thttp://www.brynosaurus.com/cachedir/\n')
        (self.cache / 'tmp').mkdir()
        (self.cache / 'tmp/.cleaned').touch()
        (self.cache / 'lock').mkdir()
        result = self.execute()
        self.assertEqual(list(result['result_entries']), ['abcd' + '1' * 36])
        self.assertEqual(result['max_bytes'], 768 * 1024**2)

    def test_unknown_files_old_format_raw_objects_and_secrets_rejected_before_ccache(self):
        for name in ['build.ninja', '.env', 'a/b/' + 'c'*38 + '_00', 'a/b/' + 'c'*31 + 'R',
                     'tmp/unfinished', 'lock/active', 'a/b/ccache.conf', 'CACHEDIR.TAG']:
            with self.subTest(name=name):
                path = self.cache / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('unexpected')
                with self.assertRaises(ValueError):
                    self.execute('bound')
                path.unlink()
        self.run.assert_not_called()

    def test_known_inode_index_is_discarded_before_inspection_and_never_saved(self):
        baseline = self.baseline()
        inode = self.cache / 'tmp' / recovery.INODE_CACHE_NAME
        for operation in ('check', 'bound'):
            with self.subTest(operation=operation):
                self.inode_index()
                result = self.execute(operation, baseline if operation == 'bound' else None)
                self.assertFalse(inode.exists())
                self.assertEqual(result['discarded_inode_cache_bytes'], 5898272)
                self.assertFalse(result['save_needed'])
                self.assertNotIn(recovery.INODE_CACHE_NAME, json.dumps(result['result_entries']))
        self.assertFalse(any(recovery.INODE_CACHE_NAME in str(call.args[0])
                             for call in self.run.call_args_list))

    def test_inode_index_links_special_files_versions_and_oversize_remain_rejected(self):
        inode = self.cache / 'tmp' / recovery.INODE_CACHE_NAME
        inode.parent.mkdir()
        for mode in ('symlink', 'hardlink', 'fifo', 'directory', 'empty', 'short', 'bad_version', 'oversize'):
            if mode == 'symlink':
                inode.symlink_to(self.entry)
            elif mode == 'hardlink':
                os.link(self.entry, inode)
            elif mode == 'fifo':
                os.mkfifo(inode)
            elif mode == 'directory':
                inode.mkdir()
            elif mode == 'oversize':
                with inode.open('wb') as stream:
                    stream.write(b'\x03\x00\x00\x00')
                    stream.truncate(recovery.INODE_CACHE_BYTES + 1)
            elif mode == 'bad_version':
                self.inode_index()
                with inode.open('r+b') as stream:
                    stream.write(b'\x04\x00\x00\x00')
            else:
                inode.write_bytes(b'' if mode == 'empty' else b'\x03\x00\x00\x00')
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                self.execute('bound')
            self.assertTrue(inode.exists() or inode.is_symlink())
            inode.rmdir() if inode.is_dir() else inode.unlink()
        self.run.assert_not_called()

    def test_inode_index_wrong_owner_is_not_removed(self):
        inode = self.inode_index()
        with mock.patch.object(recovery.core.os, 'getuid', return_value=os.getuid() + 1):
            with self.assertRaisesRegex(ValueError, 'owner/size'):
                self.execute('bound')
        self.assertTrue(inode.exists())
        self.run.assert_not_called()

    def test_inode_near_names_unknown_tmp_and_symlink_ancestor_remain_rejected(self):
        temporary = self.cache / 'tmp'
        temporary.mkdir()
        for name in ('inode-cache-32.v3', 'inode-cache-64.v2', 'inode-cache-64.v4',
                     'inode-cache-64.v3.tmp', 'inode-cache-64.v3.secret'):
            path = temporary / name
            path.write_bytes(b'\x03\x00\x00\x00')
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.execute()
            self.assertTrue(path.exists())
            path.unlink()
        temporary.rmdir()
        outside = self.home / 'outside'
        outside.mkdir()
        target = outside / recovery.INODE_CACHE_NAME
        target.write_bytes(b'\x03\x00\x00\x00')
        temporary.symlink_to(outside, target_is_directory=True)
        with self.assertRaises(ValueError):
            self.execute()
        self.assertTrue(target.exists())
        with self.assertRaises(ValueError):
            recovery.discard_inode_cache(outside)
        self.run.assert_not_called()

    def test_symlinks_hardlinks_special_files_and_wrong_root_rejected(self):
        for mode in ('symlink', 'hardlink', 'fifo'):
            path = self.cache / ('a/b/' + 'f'*38)
            if mode == 'symlink':
                path.symlink_to(self.entry)
            elif mode == 'hardlink':
                os.link(self.entry, path)
            else:
                os.mkfifo(path)
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                self.execute()
            path.unlink()
        other = self.home / 'other'
        other.mkdir()
        with self.assertRaises(ValueError):
            recovery.core.inventory(other)
        moved = self.cache.with_name('moved')
        self.cache.rename(moved)
        self.cache.symlink_to(moved, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'Symlink'):
            self.execute()
        self.run.assert_not_called()

    def test_unsafe_configuration_cannot_reach_native_ccache(self):
        original = self.config.read_text()
        for extra in ['remote_storage = https://example.invalid\n', 'prefix_command = bad\n',
                      'compiler = bad\n', 'extra_files_to_hash = /secret\n',
                      'sloppiness = time_macros\n', 'file_clone = true\n', 'hard_link = true\n',
                      'safe_dirs = *\n', 'cache_dir = /other\n']:
            with self.subTest(extra=extra):
                self.config.write_text(original + extra)
                with self.assertRaises(ValueError):
                    self.execute('bound')
        self.run.assert_not_called()

    def test_native_binary_hash_and_sanitized_environment(self):
        self.binary.write_bytes(b'changed executable')
        with self.assertRaisesRegex(ValueError, 'binary'):
            self.execute()
        self.run.assert_not_called()
        self.change_json(self.identity, ccache_sha256=recovery.core.sha256(self.binary))
        with mock.patch.dict(os.environ, {'CCACHE_PREFIX': 'bad', 'CCACHE_REMOTE_STORAGE': 'bad',
                                         'CCACHE_CONFIGPATH': '/other', 'CCACHE_SAFE_DIRS': '*'}):
            self.execute()
        environment = self.run.call_args.kwargs['env']
        self.assertEqual({k: v for k, v in environment.items() if k.startswith('CCACHE_')},
                         {'CCACHE_DIR': str(self.cache), 'CCACHE_CONFIGPATH': str(self.config)})

    def test_inspection_checksum_failure_unknown_type_and_nonselfcontained_rejected(self):
        self.run.side_effect = subprocess.CalledProcessError(1, ['ccache', '--inspect'])
        with self.assertRaises(subprocess.CalledProcessError):
            self.execute()
        self.run.side_effect = None
        for output in ['Entry type: 9 (unknown)\nSelf-contained: yes\n',
                       'Entry type: 0 (result)\nSelf-contained: no\n',
                       'Entry type: 0 (result)\nEntry type: 1 (manifest)\nSelf-contained: yes\n']:
            self.run.return_value = subprocess.CompletedProcess([], 0, output, '')
            with self.assertRaises(ValueError):
                self.execute()

    def test_bound_enforces_limit_before_save_and_rechecks_after_cleanup(self):
        with mock.patch.object(recovery.core, 'MAX_BYTES', 80):
            self.entry.write_bytes(b'x' * 75)
            with self.assertRaisesRegex(ValueError, '768 MiB'):
                self.execute()
            with self.assertRaisesRegex(ValueError, '768 MiB'):
                self.execute('bound')
            self.assertTrue(any('--cleanup' in call.args[0] for call in self.run.call_args_list))
        self.run.reset_mock()
        def inject(arguments, **kwargs):
            if '--cleanup' in arguments:
                (self.cache / '.env').write_text('unexpected')
            return self.fake_ccache(arguments, **kwargs)
        self.run.side_effect = inject
        with self.assertRaises(ValueError):
            self.execute('bound')

    def test_cleanup_failure_never_approves_save(self):
        baseline = self.baseline()
        def fail(arguments, **kwargs):
            if '--cleanup' in arguments:
                raise subprocess.TimeoutExpired(arguments, 60)
            return self.fake_ccache(arguments, **kwargs)
        self.run.side_effect = fail
        with self.assertRaises(subprocess.TimeoutExpired):
            self.execute('bound', baseline)

    def test_stats_manifest_deletions_and_depth_changes_are_not_meaningful_delta(self):
        baseline = self.baseline()
        (self.cache / 'a/stats').write_text('12\n')
        self.add_entry('f'*40, b'manifest')
        name = ''.join(self.entry.relative_to(self.cache).parts)
        self.entry.unlink()
        moved = self.add_entry(name, depth=3)
        self.assertFalse(self.execute('bound', baseline)['save_needed'])
        moved.unlink()
        self.assertFalse(self.execute('bound', baseline)['save_needed'])

    def test_completed_result_delta_can_survive_a_failed_build(self):
        baseline = self.baseline()
        self.entry.write_bytes(b'changed successful result')
        self.add_entry('f'*40, b'new successful result')
        result = self.execute('bound', baseline)
        self.assertTrue(result['save_needed'])
        self.assertEqual(result['new_or_changed_results'], 2)
        self.assertFalse(self.execute('bound')['save_needed'])

    def test_baseline_identity_profile_schema_and_entry_names_are_strict(self):
        path = self.baseline()
        original = path.read_bytes()
        for key, value in [('fingerprint', '0'*64), ('profile', 'linux-gcc-test'), ('schema', True),
                           ('cache_dir', '/other'), ('operation', 'bound'), ('max_bytes', 2**30),
                           ('result_entries', {'abcd' + '1'*29 + 'R': 'a'*64})]:
            path.write_bytes(original)
            self.change_json(path, **{key: value})
            self.run.reset_mock()
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'Baseline'):
                self.execute('bound', path)
            self.run.assert_not_called()

    def test_cli_outputs_only_after_success(self):
        output, report = self.root / 'github-output', self.root / 'report.json'
        args = ['prepare', '--identity', str(self.identity), '--validation', str(self.validation),
                '--report', str(report), '--github-output', str(output)]
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(recovery.main(args), 0)
        self.assertIn('save-needed=false\n', output.read_text())
        output.unlink()
        report.unlink()
        self.validation.unlink()
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(recovery.main(args), 2)
        self.assertFalse(output.exists())
        self.assertFalse(report.exists())


@unittest.skipUnless(sys.platform == 'darwin', 'Native Mac ccache and Xcode smoke runs on the Mac runner')
class NativeMacCcacheTests(unittest.TestCase):
    def test_native_cpp_hit_delta_failure_cleanup_and_checksum(self):
        # Missing tools on Mac FAIL, not skip. The workflow asserts Darwin first.
        ccache = shutil.which('ccache')
        self.assertIsNotNone(ccache)
        compiler = subprocess.check_output(['xcrun', '--find', 'clang++'], text=True).strip()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()  # macOS /var is a symlink to /private/var.
            cache = root / '.cache/vcmi-incremental' / recovery.PROFILE
            cache.mkdir(parents=True)
            environment = {key: value for key, value in os.environ.items() if not key.startswith('CCACHE_')}
            environment.update(CCACHE_DIR=str(cache), CCACHE_CONFIGPATH=str(cache / 'ccache.conf'))
            def native(*args, check=True):
                return subprocess.run([ccache, *args], check=check, env=environment, cwd=root,
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=30)
            def hits():
                # ccache v4.14 src/ccache/core/statistics.cpp public counter IDs.
                counters = dict(line.split('\t', 1) for line in native('--print-stats').stdout.splitlines())
                return int(counters['direct_cache_hit']) + int(counters['preprocessed_cache_hit'])
            native('--set-config=compiler_check=content')
            native('--set-config=sloppiness=')
            native('--max-size=768M')
            source = root / 'fixture.cpp'
            source.write_text('int fixture() { return 1; }\n')
            arguments = (compiler, '-std=c++20', '-c', str(source), '-o', str(root / 'fixture.o'))
            native(*arguments)
            inode = cache / 'tmp' / recovery.INODE_CACHE_NAME
            self.assertTrue(inode.is_file(), 'Native APFS smoke must exercise the inode index')
            identity = installed_identity(root, root, Path(ccache))
            identity['ccache'] = native('--version').stdout.strip()
            identity['compiler_sha256'] = recovery.core.sha256(Path(compiler))
            identity_path, validation_path = root / 'identity.json', root / 'validation.json'
            identity_path.write_text(json.dumps(identity))
            validation_path.write_text(json.dumps(restore_evidence()))
            with mock.patch.object(Path, 'home', return_value=root), mock.patch.object(Path, 'cwd', return_value=root):
                baseline = recovery.recover('check', identity_path, validation_path)
                self.assertFalse(inode.exists())
                self.assertGreater(baseline['discarded_inode_cache_bytes'], 0)
                self.assertGreater(len(baseline['result_entries']), 0)
                baseline_path = root / 'baseline.json'
                baseline_path.write_text(json.dumps(baseline))
                before = hits()
                native(*arguments)
                self.assertEqual(hits(), before + 1)
                unchanged = recovery.recover('bound', identity_path, validation_path, baseline_path)
                self.assertFalse(unchanged['save_needed'])
                self.assertEqual(unchanged['new_or_changed_results'], 0)
                source.write_text('int fixture() { return 2; }\n')
                native(*arguments)
                source.write_text('This must fail to compile.\n')
                self.assertNotEqual(native(*arguments, check=False).returncode, 0)
                result = recovery.recover('bound', identity_path, validation_path, baseline_path)
                self.assertTrue(result['save_needed'])
                self.assertFalse(inode.exists())
                self.assertLessEqual(result['logical_bytes'], 768 * 1024**2)
                _, entries = recovery.core.inventory(cache)
                entry = next(path for name, path in entries.items()
                             if name.replace('/', '') in result['result_entries'])
                data = entry.read_bytes()
                entry.write_bytes(data[:-1] + bytes([data[-1] ^ 1]))
                with self.assertRaises(subprocess.CalledProcessError):
                    recovery.recover('check', identity_path, validation_path)


class MacWorkflowContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workflow = (HERE.parent / '.github/workflows/vcmi-incremental.yml').read_text()
        cls.linux, cls.macos = cls.workflow.split('  linux:\n', 1)[1].split('  macos:\n', 1)
        cls.steps = re.split(r'(?=^    - name: )', cls.macos, flags=re.MULTILINE)

    def step(self, name):
        matches = [step for step in self.steps if name in step]
        self.assertEqual(len(matches), 1, name)
        return matches[0]

    def test_strict_build_and_conan_restore_precedes_recovery_and_native_smoke(self):
        ordered = ['id: verified_cache\n', 'id: mac_compiler_scope\n', 'id: mac_compiler_cache\n',
                   'id: mac_compiler_ready\n', 'id: build\n', 'id: mac_compiler_save_ready\n']
        positions = [self.macos.index(item) for item in ordered]
        self.assertEqual(positions, sorted(positions))
        verified = self.step('id: verified_cache\n')
        for required in ['incremental_identity.py macos-arm-conan-ninja-release --installed',
                         'incremental_cache.py restore', '--fingerprint incremental-evidence/toolchain-identity.json',
                         '--external-root "$HOME/.conan2/p"', '--external-root "$GITHUB_WORKSPACE/conan-generated"']:
            self.assertIn(required, verified)
        scope = self.step('id: mac_compiler_scope\n')
        for required in ["steps.verified_cache.outcome == 'success'", "'[vcmi-warm] '", "'[adventure-qualify]'",
                         'command -v ccache', 'xcrun --find clang++', 'assert sys.platform == "darwin"',
                         "-p 'test_macos_compiler_recovery.py' -v", 'timeout-minutes: 2']:
            self.assertIn(required, scope)
        self.assertNotIn('always()', scope)
        self.assertNotIn('continue-on-error', scope)

    def test_optional_restore_is_only_mac_ccache_with_exact_identity_and_both_helpers(self):
        restore = self.step('id: mac_compiler_cache\n')
        prefix = "vcmi-compiler-v1-macos-${{ steps.mac_compiler_scope.outputs.fingerprint }}-${{ hashFiles('CI/macos_compiler_recovery.py', 'CI/compiler_recovery.py') }}-"
        self.assertIn('key: ' + prefix + '${{ github.sha }}-${{ github.run_id }}-${{ github.run_attempt }}', restore)
        self.assertIn('restore-keys: ' + prefix + '\n', restore)
        self.assertEqual(restore.count('restore-keys:'), 1)
        self.assertIn("if: steps.mac_compiler_scope.outcome == 'success'", restore)
        self.assertIn('path: ~/.cache/vcmi-incremental/macos-arm-conan-ninja-release\n', restore)
        for forbidden in ['out/build', '.ci/', '~/.conan2', 'conan-generated', 'continue-on-error', 'fail-on-cache-miss: true']:
            self.assertNotIn(forbidden, restore)
        check = self.step('id: mac_compiler_ready\n')
        self.assertIn('macos_compiler_recovery.py check', check)
        self.assertNotIn('continue-on-error', check)

    def test_all_operations_use_unchanged_full_installed_identity_not_linux_alias(self):
        commands = [line for line in self.macos.splitlines() if 'python3 CI/macos_compiler_recovery.py ' in line]
        self.assertEqual(len(commands), 3)
        for operation in ('prepare', 'check', 'bound'):
            line, = [line for line in commands if 'macos_compiler_recovery.py ' + operation + ' ' in line]
            self.assertIn('--identity incremental-evidence/toolchain-identity.json ', line)
            self.assertIn('--validation incremental-evidence/cache-validation.json ', line)
        self.assertNotIn('linux_cache_compatibility', self.macos)
        self.assertNotIn('cache-toolchain-identity.json', self.macos)

    def test_failure_recovery_requires_completed_build_not_cancelled_and_real_delta(self):
        bound = self.step('id: mac_compiler_save_ready\n')
        for required in ['always() && !cancelled()', "steps.verified_cache.outcome == 'success'",
                         "steps.mac_compiler_ready.outcome == 'success'",
                         "(steps.build.outcome == 'success' || steps.build.outcome == 'failure')",
                         '--baseline incremental-evidence/mac-compiler-recovery-restored.json', 'timeout-minutes: 4']:
            self.assertIn(required, bound)
        save = self.step('name: Retain successful Mac compiler entries without promoting incomplete build state')
        for required in ['always() && !cancelled()', "steps.mac_compiler_save_ready.outcome == 'success'",
                         "steps.mac_compiler_save_ready.outputs.save-needed == 'true'", 'timeout-minutes: 3',
                         'path: ~/.cache/vcmi-incremental/macos-arm-conan-ninja-release\n',
                         'key: ${{ steps.mac_compiler_cache.outputs.cache-primary-key }}']:
            self.assertIn(required, save)
        self.assertNotIn('out/build', save)
        self.assertNotIn('.ci/', save)

    def test_full_state_packaging_and_job_budgets_not_weakened(self):
        for name in ['Snapshot only successful build outputs, with bounded upload size',
                     'Persist new cache generation (not a frozen first-build key)',
                     'Package app without DMG/XProtect workaround', 'Verify extracted ARM64 app and signatures',
                     'Upload candidate binary']:
            step = self.step('name: ' + name)
            for forbidden in ['always()', 'failure()', 'continue-on-error']:
                self.assertNotIn(forbidden, step)
        self.assertIn("timeout-minutes: ${{ startsWith(github.event.head_commit.message, '[vcmi-bootstrap] ') && 90 || 40 }}", self.macos)
        self.assertIn("timeout-minutes: ${{ startsWith(github.event.head_commit.message, '[vcmi-bootstrap] ') && 65 || 20 }}", self.step('id: build\n'))
        self.assertIn("needs.linux.result == 'success'", self.macos)
        self.assertNotIn('macos_compiler_recovery.py', self.linux)
        self.assertIn('python3 -m unittest discover -s CI -p "test_macos_compiler_recovery.py" -v',
                      self.workflow.split('  linux:\n', 1)[0])


if __name__ == '__main__':
    unittest.main()
