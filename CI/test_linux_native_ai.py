#!/usr/bin/env python3
"""Portable Linux runner contracts. Mock execution is not native engine evidence."""
import contextlib
import io
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import MagicMock, patch

import run_linux_native_ai as runner
import reseal_linux_native_source as resealer
import run_macos_native_ai as shared
from test_macos_native_ai import listing_fixture, write_xml


class LinuxContracts(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def test_shared_safety_helpers_are_unchanged(self):
        for name in ('copy_verified_archive', 'check_archive_listing', 'validate_report',
                     'verify_native_inventory', 'parse_test_inventory', 'selected_suites'):
            self.assertIs(getattr(runner, name), getattr(shared, name))

    def test_environment_is_allowlist_and_xdg_is_private(self):
        with patch.dict(os.environ, {'LD_PRELOAD': '/bad', 'GTEST_REPEAT': '-1',
                                    'VCMI_AI_MMAI_NEURAL_TEST': '1', 'XDG_DATA_HOME': '/bad'}):
            env = runner.isolated_environment(self.root / 'home', self.root / 'tmp')
        self.assertFalse({'LD_PRELOAD', 'GTEST_REPEAT', 'VCMI_AI_MMAI_NEURAL_TEST'} & env.keys())
        for key in ('XDG_DATA_HOME', 'XDG_CONFIG_HOME', 'XDG_CACHE_HOME', 'XDG_STATE_HOME',
                    'XDG_DATA_DIRS', 'XDG_CONFIG_DIRS'):
            self.assertTrue(Path(env[key]).is_relative_to(self.root / 'home'))
        self.assertEqual(len(env), 11)

    def test_linux_fixture_uses_separate_xdg_config_and_rejects_bad_extraction(self):
        for mutation in ('none', 'extra', 'symlink', 'hardlink', 'wrong-size'):
            with self.subTest(mutation=mutation):
                temporary = self.root / mutation
                temporary.mkdir()
                env = runner.isolated_environment(temporary / 'home', temporary)
                data = Path(env['XDG_DATA_HOME']) / 'vcmi'
                def capture(command, **kwargs):
                    self.assertEqual(kwargs['env'], env)
                    if command[1] == 'l':
                        return listing_fixture()
                    for name in runner.FIXTURE_ROOTS:
                        (data / name).mkdir()
                    path = data / 'Data/example.txt'
                    path.write_bytes(b'bad-size' if mutation == 'wrong-size' else b'abc')
                    if mutation == 'extra': (data / 'Data/extra').touch()
                    if mutation == 'symlink': (data / 'Data/link').symlink_to(path)
                    if mutation == 'hardlink': os.link(path, data / 'Data/link')
                    return ''
                with patch.object(runner, 'copy_verified_archive'), patch.object(runner, 'capture', side_effect=capture), \
                        patch.object(shared, 'FIXTURE_FILES', 1), patch.object(shared, 'FIXTURE_UNPACKED_BYTES', 3):
                    if mutation != 'none':
                        with self.assertRaises(ValueError):
                            runner.prepare_fixture(temporary / 'input', temporary, env, '/7zz')
                    else:
                        runner.prepare_fixture(temporary / 'input', temporary, env, '/7zz')
                        preset = Path(env['XDG_CONFIG_HOME']) / 'vcmi/testModSettings.json'
                        self.assertEqual(json.loads(preset.read_text())['presets']['default']['mods'], ['vcmi', 'vcmi-test'])
                        self.assertFalse((data / 'config').exists())

    def make_binary(self):
        binary = self.root / 'bin/vcmitest'
        binary.parent.mkdir(exist_ok=True)
        header = bytearray(64)
        header[:7] = b'\x7fELF\x02\x01\x01'
        header[16:18] = (3).to_bytes(2, 'little')
        header[18:20] = (62).to_bytes(2, 'little')
        header[20:24] = (1).to_bytes(4, 'little')
        binary.write_bytes(header)
        binary.chmod(0o700)
        for name in ('config/filesystem.json', 'Mods/vcmi/mod.json',
                     'scripts/damage/damageCalculator.lua', 'test/testdata/vcmi-test/mod.json'):
            path = binary.parent / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        return binary

    def test_elf_architecture_permissions_and_resources(self):
        binary = self.make_binary()
        with patch.object(runner.platform, 'system', return_value='Linux'), \
                patch.object(runner.platform, 'machine', return_value='x86_64'):
            runner.check_binary(binary, {})
            good = binary.read_bytes()
            for data in (b'script', good[:18] + b'\xb7\x00' + good[20:], b'\x7fELF\x01' + good[5:]):
                binary.write_bytes(data)
                with self.assertRaises(ValueError): runner.check_binary(binary, {})
            binary.write_bytes(good)
            binary.chmod(0o600)
            with self.assertRaises(ValueError): runner.check_binary(binary, {})
        with patch.object(runner.platform, 'system', return_value='Darwin'):
            with self.assertRaisesRegex(ValueError, 'Linux'): runner.check_binary(binary, {})

    def test_listing_direct_command_and_unselected_inventory(self):
        with patch.object(runner, 'capture', return_value='Suite.\n  One\n') as capture:
            result = runner.read_inventory(Path('/build/bin/vcmitest'), self.root, {}, 'list', 'Suite.*')
        self.assertEqual(result, {'Suite.One'})
        self.assertEqual(capture.call_args.args[0], ['/build/bin/vcmitest', '--gtest_list_tests', '--gtest_filter=Suite.*'])
        self.assertEqual(capture.call_args.kwargs['cwd'], Path('/build/bin'))
        with patch.object(runner, 'capture', return_value='Other.\n  One\n'):
            with self.assertRaises(ValueError): runner.read_inventory(Path('/binary'), self.root, {}, 'bad', 'Suite.*')

    def test_suite_zero_nonzero_timeout_and_spawn_failure(self):
        for mode in ('success', 'nonzero', 'timeout', 'spawn', 'missing-xml'):
            with self.subTest(mode=mode):
                output = self.root / mode
                output.mkdir()
                if mode != 'missing-xml': write_xml(output / 'mock.xml', {'Suite.One'})
                process = MagicMock()
                process.__enter__.return_value = process
                process.pid = 12345
                process.returncode = 3 if mode == 'nonzero' else (-9 if mode == 'timeout' else 0)
                process.wait.side_effect = ([subprocess.TimeoutExpired('mock', 1), -9]
                                            if mode == 'timeout' else [process.returncode])
                with patch.object(runner.subprocess, 'Popen', return_value=process,
                                  side_effect=OSError('synthetic spawn failure') if mode == 'spawn' else None) as popen, \
                        patch.object(runner.os, 'killpg') as kill:
                    result = runner.run_suite(Path('/not-an-engine'), output, {}, ('mock', 'Suite.*', 1, 1), {'Suite.One'})
                self.assertEqual(result['status'], 'passed' if mode == 'success' else 'failed')
                self.assertEqual(popen.call_args.args[0][0], '/not-an-engine')
                self.assertTrue(popen.call_args.kwargs['start_new_session'])
                if mode == 'timeout': kill.assert_called_once_with(12345, runner.signal.SIGKILL)

    def test_capture_timeout_and_error_preserved(self):
        process = MagicMock()
        process.__enter__.return_value = process
        process.pid = 12345
        process.communicate.side_effect = [subprocess.TimeoutExpired('mock', 1), ('', '')]
        with patch.object(runner.subprocess, 'Popen', return_value=process), patch.object(runner.os, 'killpg') as kill:
            with self.assertRaises(subprocess.TimeoutExpired): runner.capture(['/mock'], timeout=1)
        kill.assert_called_once_with(12345, runner.signal.SIGKILL)
        process.communicate.side_effect = None
        process.communicate.return_value = ('out', 'diagnostic')
        process.returncode = 2
        with patch.object(runner.subprocess, 'Popen', return_value=process):
            with self.assertRaises(subprocess.CalledProcessError) as caught: runner.capture(['/mock'])
        self.assertEqual(caught.exception.stderr, 'diagnostic')

    def test_frozen_source_and_counts(self):
        source = Path(runner.__file__).resolve().parent.parent
        checks = runner.verify_source(source)
        self.assertEqual(checks['base_commit'], runner.SOURCE_COMMIT)
        counts = runner.workflow_counts((source / '.github/workflows/ai-macos-arm64.yml').read_text())
        self.assertGreater(counts['native'], 0)
        self.assertLessEqual(counts['lifecycle'], counts['forecast'])
        self.assertEqual([s[2] for s in runner.selected_suites(counts['native'], counts['forecast'])],
                         [counts['native'] + counts['forecast'], 48, 145])
        with patch.object(runner, 'SOURCE_MANIFEST_SHA256', '0' * 64):
            with self.assertRaisesRegex(ValueError, 'manifest'): runner.verify_source(source)

    def test_pinned_target_metadata_keeps_48_mmai_and_145_nullkiller_cases(self):
        source = Path(runner.__file__).resolve().parent.parent
        contracts = (source / 'test/aiIntegration/MMAINNOutputValidationTest.cpp').read_text()
        self.assertEqual(len(re.findall(r'\bTEST\(MMAINNOutputValidation,', contracts)), 48)
        cmake = (source / 'test/CMakeLists.txt').read_text()
        identities = set()
        for path in (source / 'test/nullkiller2').rglob('*.cpp'):
            if path.relative_to(source / 'test').as_posix() not in cmake:
                continue
            for suite, name in re.findall(r'\bTEST(?:_F)?\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)', path.read_text()):
                if suite.startswith('Nullkiller2_') or suite == 'ResourceTradingTurnTest':
                    identities.add(suite + '.' + name)
        self.assertEqual(len(identities), 145)

    def test_reseal_has_no_manifest_or_patch_digest_cycle(self):
        source = self.root / 'source'
        (source / 'CI').mkdir(parents=True)
        (source / 'test/googletest').mkdir(parents=True)
        (source / 'CMakeLists.txt').write_text('synthetic source fixture')
        (source / 'test/googletest/CMakeLists.txt').write_text('synthetic fixture')
        runner_file = source / 'CI/run_linux_native_ai.py'
        runner_file.write_text('SOURCE_COMMIT = "' + 'a' * 40 + '"\nSOURCE_MANIFEST_SHA256 = "' + '0' * 64 + '"\n')
        overlay = self.root / 'ai-only.patch'
        overlay.write_text('--- a/CMakeLists.txt\n+++ b/CMakeLists.txt\n')
        first = resealer.reseal(source, overlay)
        second = resealer.reseal(source, overlay)
        self.assertEqual(first, second)
        manifest = json.loads((source / 'CI/linux_native_source_manifest.json').read_text())
        self.assertFalse(set(manifest['files']) & resealer.INFRASTRUCTURE)
        self.assertIn(first['manifest_sha256'], runner_file.read_text())
        for content in (
                '--- /dev/null\n+++ b/CI/linux_native_source_manifest.json\n',
                '--- a/CI/run_linux_native_ai.py\n+++ /dev/null\n',
                '--- CI/run_linux_native_ai.py\t2026-11-08 00:00:00\n+++ /dev/null\n',
                'diff --git a/CI/reseal_linux_native_source.py b/CI/renamed.py\n',
                'rename from CI/test_linux_native_ai.py\nrename to CI/other.py\n'):
            overlay.write_text(content)
            with self.assertRaisesRegex(ValueError, 'exclude'): resealer.reseal(source, overlay)
        with self.assertRaisesRegex(ValueError, 'outside'): resealer.reseal(source, source / 'CMakeLists.txt')

    def test_source_tamper_is_rejected(self):
        source = Path(runner.__file__).resolve().parent.parent
        real_hash = runner.sha256_file
        def tampered(path):
            return '0' * 64 if path.name == 'CMakeLists.txt' else real_hash(path)
        with patch.object(runner, 'sha256_file', side_effect=tampered):
            with self.assertRaisesRegex(ValueError, 'differs'): runner.verify_source(source)

    def test_build_cache_rejects_wrong_source_tree_modes_and_duplicates(self):
        source = self.root / 'source'
        source.mkdir()
        binary = self.make_binary()
        values = {'CMAKE_HOME_DIRECTORY': str(source), 'CMAKE_CACHEFILE_DIR': str(self.root),
                  'CMAKE_BUILD_TYPE': 'Release', 'COPY_CONFIG_ON_BUILD': 'ON', 'INSTALL_GTEST': 'OFF'}
        for key in ('SERVER', 'TEST', 'BATTLE_AI', 'STUPID_AI', 'NULLKILLER2_AI', 'MMAI'):
            values['ENABLE_' + key] = 'ON'
        for key in ('CLIENT', 'LAUNCHER', 'EDITOR', 'TRANSLATIONS', 'VIDEO', 'DISCORD',
                    'LOBBY', 'MINIMAL_LIB', 'MMAI_TEST', 'CCACHE', 'LTO', 'GITVERSION'):
            values['ENABLE_' + key] = 'OFF'
        cache = self.root / 'CMakeCache.txt'
        def write(changes=None, suffix=''):
            items = dict(values, **(changes or {}))
            cache.write_text(''.join(key + ':STRING=' + value + '\n' for key, value in items.items()) + suffix)
        write()
        self.assertEqual(runner.check_build_cache(binary, source)['sha256'], runner.sha256_file(cache))
        for changes in ({'CMAKE_HOME_DIRECTORY': '/wrong'}, {'CMAKE_CACHEFILE_DIR': '/wrong'},
                        {'ENABLE_MMAI': 'OFF'}, {'ENABLE_NULLKILLER2_AI': 'OFF'},
                        {'ENABLE_TEST': 'OFF'}, {'COPY_CONFIG_ON_BUILD': 'OFF'},
                        {'CMAKE_BUILD_TYPE': 'Debug'}, {'ENABLE_CLIENT': 'ON'}):
            write(changes)
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                runner.check_build_cache(binary, source)
        write(suffix='ENABLE_MMAI:BOOL=ON\n')
        with self.assertRaisesRegex(ValueError, 'Duplicate'): runner.check_build_cache(binary, source)

    def test_missing_source_returns_failed_summary(self):
        output = self.root / 'out'
        with contextlib.redirect_stdout(io.StringIO()):
            result = runner.main(['--test-binary', '/missing', '--fixture-archive', '/missing',
                                  '--source-dir', '/missing', '--output-dir', str(output)])
        self.assertEqual(result, 1)
        report = json.loads((output / 'native-ai-summary.json').read_text())
        self.assertEqual(report['suites'], [])
        self.assertEqual(report['status'], 'failed')

    def test_nonempty_and_symlink_output_rejected(self):
        (self.root / 'existing').touch()
        (self.root / 'link').symlink_to(self.root, target_is_directory=True)
        for output in (self.root, self.root / 'link'):
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                runner.main(['--test-binary', '/missing', '--fixture-archive', '/missing',
                             '--source-dir', '/missing', '--output-dir', str(output)])


if __name__ == '__main__':
    unittest.main()
