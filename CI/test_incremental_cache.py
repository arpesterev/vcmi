#!/usr/bin/env python3
"""Unit and real compiler dependency-graph tests (Ninja when installed, Make fallback)."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest

spec = importlib.util.spec_from_file_location('cache', Path(__file__).with_name('incremental_cache.py'))
cache = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cache)


class CacheTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name).resolve()
        self.build = self.root / 'build'
        self.build.mkdir()
        self.state = self.root / 'state.json'
        self.fp = self.root / 'fingerprint.json'
        self.fp.write_text('{"compiler":"fixture-1","sdk":"fixture"}')
        self.run_command('git', 'init', '-q')
        (self.root / 'a.cpp').write_text('int a;\n')
        self.run_command('git', 'add', 'a.cpp')
        (self.build / 'a.o').write_bytes(b'object')

    def tearDown(self):
        self.tmp.cleanup()

    def run_command(self, *args):
        return subprocess.check_output(args, cwd=self.root, stderr=subprocess.STDOUT, text=True)

    def save(self):
        return cache.snapshot(self.root, self.build, self.state, self.fp)

    def load(self):
        return cache.restore(self.root, self.build, self.state, self.fp)

    def change_saved(self, callback):
        data = json.loads(self.state.read_text())
        callback(data)
        self.state.write_text(json.dumps(data))

    def test_unchanged_checkout_and_archive_timestamps_restored(self):
        original = (self.root / 'a.cpp').stat().st_mtime_ns
        output = (self.build / 'a.o').stat().st_mtime_ns
        self.save()
        os.utime(self.root / 'a.cpp', ns=(1, original + 10_000))
        os.utime(self.build / 'a.o', ns=(1, output // 1_000_000_000 * 1_000_000_000))
        report = self.load()
        self.assertEqual(report['unchanged_sources'], 1)
        self.assertEqual((self.root / 'a.cpp').stat().st_mtime_ns, original)
        self.assertEqual((self.build / 'a.o').stat().st_mtime_ns, output)

    def test_changed_input_newer_than_output_even_old_checkout_time(self):
        self.save()
        (self.root / 'a.cpp').write_text('int b;\n')
        os.utime(self.root / 'a.cpp', (1, 1))
        report = self.load()
        self.assertEqual(report['changed_sources'], ['a.cpp'])
        self.assertGreater((self.root / 'a.cpp').stat().st_mtime_ns,
                           (self.build / 'a.o').stat().st_mtime_ns)

    def test_new_and_deleted_files_reported(self):
        self.save()
        (self.root / 'a.cpp').unlink()
        (self.root / 'b.cpp').write_text('int b;')
        self.run_command('git', 'add', 'b.cpp')
        report = self.load()
        self.assertEqual(report['changed_sources'], ['b.cpp'])
        self.assertEqual(report['removed_sources'], ['a.cpp'])

    def test_incompatible_fingerprint_rejected_without_timestamp_mutation(self):
        self.save()
        before = (self.root / 'a.cpp').stat().st_mtime_ns
        self.fp.write_text('{"compiler":"fixture-2"}')
        with self.assertRaisesRegex(ValueError, 'fingerprint'):
            self.load()
        self.assertEqual((self.root / 'a.cpp').stat().st_mtime_ns, before)

    def test_missing_state_rejected(self):
        with self.assertRaisesRegex(ValueError, 'metadata missing'):
            self.load()

    def test_modified_output_rejected(self):
        self.save()
        (self.build / 'a.o').write_bytes(b'bad')
        with self.assertRaisesRegex(ValueError, 'content/mode mismatch'):
            self.load()

    def test_missing_or_extra_output_rejected(self):
        self.save()
        (self.build / 'new.o').write_bytes(b'new')
        with self.assertRaisesRegex(ValueError, 'inventory mismatch'):
            self.load()
        (self.build / 'new.o').unlink()
        (self.build / 'a.o').unlink()
        with self.assertRaisesRegex(ValueError, 'inventory mismatch'):
            self.load()

    def test_parent_traversal_rejected(self):
        self.save()
        self.change_saved(lambda d: d['sources'].update({'../outside': d['sources']['a.cpp']}))
        with self.assertRaisesRegex(ValueError, 'Unsafe cached path'):
            self.load()

    def test_dot_path_rejected(self):
        self.save()
        self.change_saved(lambda d: d['sources'].update({'.': d['sources']['a.cpp']}))
        with self.assertRaisesRegex(ValueError, 'Unsafe cached path'):
            self.load()

    def test_absolute_path_rejected(self):
        self.save()
        self.change_saved(lambda d: d['build_files'].update({'/etc/passwd': d['build_files']['a.o']}))
        with self.assertRaisesRegex(ValueError, 'Unsafe cached path'):
            self.load()

    def test_symlink_parent_rejected(self):
        self.save()
        (self.root / 'linked').symlink_to(self.build, target_is_directory=True)
        self.change_saved(lambda d: d['sources'].update({'linked/a.o': d['sources']['a.cpp']}))
        with self.assertRaisesRegex(ValueError, 'Symlink ancestor'):
            self.load()

    def test_symlink_timestamp_does_not_touch_target(self):
        outside = self.root / 'outside'
        outside.write_text('do not touch')
        (self.root / 'link').symlink_to(outside)
        self.run_command('git', 'add', 'link')
        self.save()
        timestamp = outside.stat().st_mtime_ns
        os.utime(self.root / 'link', ns=(1, 1), follow_symlinks=False)
        self.load()
        self.assertEqual(outside.stat().st_mtime_ns, timestamp)

    def test_future_output_rejected(self):
        self.save()
        self.change_saved(lambda d: d['build_files']['a.o'].update(mtime_ns=time.time_ns() + 100_000_000_000))
        with self.assertRaisesRegex(ValueError, 'Future-dated'):
            self.load()

    def test_external_dependency_timestamps_rehydrated(self):
        dependencies = self.root / 'dependencies'
        generated = self.root / 'conan-generated'
        dependencies.mkdir()
        generated.mkdir()
        header = dependencies / 'library.h'
        toolchain = generated / 'toolchain.cmake'
        header.write_text('#define DEP 1')
        toolchain.write_text('set(OPTION ON)')
        target = self.root / 'external-target'
        target.write_text('untouched')
        (dependencies / 'link').symlink_to(target)
        target_time = target.stat().st_mtime_ns
        originals = [header.stat().st_mtime_ns, toolchain.stat().st_mtime_ns]
        roots = [dependencies, generated]
        cache.snapshot(self.root, self.build, self.state, self.fp, roots)
        for path in [header, toolchain]:
            ns = path.stat().st_mtime_ns // 1_000_000_000 * 1_000_000_000
            os.utime(path, ns=(ns, ns))
        report = cache.restore(self.root, self.build, self.state, self.fp, list(reversed(roots)))
        self.assertEqual(report['verified_external_files'], 3)
        self.assertEqual([header.stat().st_mtime_ns, toolchain.stat().st_mtime_ns], originals)
        self.assertEqual(target.stat().st_mtime_ns, target_time)

    def test_corrupt_external_dependency_fails_before_mutation(self):
        dependencies = self.root / 'dependencies'
        dependencies.mkdir()
        header = dependencies / 'library.h'
        header.write_text('#define DEP 1')
        roots = [dependencies]
        cache.snapshot(self.root, self.build, self.state, self.fp, roots)
        source = self.root / 'a.cpp'
        output = self.build / 'a.o'
        os.utime(source, ns=(1, 1))
        os.utime(output, ns=(2, 2))
        header.write_text('#define DEP 2')
        with self.assertRaisesRegex(ValueError, 'External dependency content/inventory mismatch'):
            cache.restore(self.root, self.build, self.state, self.fp, roots)
        self.assertEqual(source.stat().st_mtime_ns, 1)
        self.assertEqual(output.stat().st_mtime_ns, 2)

    def test_external_root_identity_and_symlink_rejected(self):
        dependencies = self.root / 'dependencies'
        dependencies.mkdir()
        cache.snapshot(self.root, self.build, self.state, self.fp, [dependencies])
        with self.assertRaisesRegex(ValueError, 'external_roots'):
            self.load()
        linked = self.root / 'linked'
        linked.symlink_to(dependencies, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'non-canonical'):
            cache.restore(self.root, self.build, self.state, self.fp, [linked])
        with self.assertRaisesRegex(ValueError, 'overlaps'):
            cache.snapshot(self.root, self.build, self.state, self.fp, [self.build])

    def test_other_workspace_rejected(self):
        self.save()
        self.change_saved(lambda d: d.update(source_root='/another/checkout'))
        with self.assertRaisesRegex(ValueError, 'source_root'):
            self.load()

    def test_submodule_sources_included(self):
        child = self.root / 'child'
        child.mkdir()
        subprocess.check_call(['git', 'init', '-q', str(child)])
        (child / 'dep.h').write_text('#define X 1\n')
        subprocess.check_call(['git', '-C', str(child), 'add', 'dep.h'])
        subprocess.check_call(['git', '-C', str(child), '-c', 'user.name=Fixture', '-c',
                               'user.email=fixture@example.invalid', 'commit', '-qm', 'fixture'])
        self.run_command('git', '-c', 'protocol.file.allow=always', 'submodule', 'add', './child', 'deps/child')
        data = cache.tracked(self.root)
        self.assertIn('deps/child/dep.h', data)

    @unittest.skipUnless(shutil.which('gcc') and (shutil.which('ninja') or shutil.which('make')), 'compiler/build tool unavailable')
    def test_real_incremental_dependency_graph(self):
        # Client and AI share a public header, but not each other's translation unit.
        (self.build / 'a.o').unlink()
        (self.root / 'shared.h').write_text('#define VALUE 1\n')
        (self.root / 'client.c').write_text('#include "shared.h"\nint client(void) { return VALUE; }\n')
        (self.root / 'ai.c').write_text('#include "shared.h"\nint ai(void) { return VALUE; }\n')
        ninja = shutil.which('ninja')
        if ninja:
            (self.build / 'build.ninja').write_text('rule cc\n  command = gcc -MMD -MF $out.d -c $in -o $out\n  depfile = $out.d\n  deps = gcc\nbuild client.o: cc ../client.c\nbuild ai.o: cc ../ai.c\ndefault client.o ai.o\n')
            command = [ninja, '-C', 'build', '-d', 'explain']
        else:
            (self.root / 'Makefile').write_text('all: build/client.o build/ai.o\nbuild/%.o: %.c shared.h\n\tgcc -MMD -MF $@.d -c $< -o $@\n-include build/*.d\n')
            command = ['make', '--no-print-directory']
        self.run_command('git', 'add', 'shared.h', 'client.c', 'ai.c')
        if not ninja:
            self.run_command('git', 'add', 'Makefile')
        self.run_command(*command)
        objects = lambda: [(self.build / name).stat().st_mtime_ns for name in ('client.o', 'ai.o')]
        first = objects()
        self.save()
        for name in cache.tracked(self.root):
            os.utime(self.root / name, None)
        self.load()
        self.run_command(*command)
        self.assertEqual(first, objects(), 'No-op rebuilt objects')
        (self.root / 'client.c').write_text('#include "shared.h"\nint client(void) { return VALUE+1; }\n')
        os.utime(self.root / 'client.c', (1, 1))
        self.load()
        self.run_command(*command)
        client_changed = objects()
        self.assertGreater(client_changed[0], first[0])
        self.assertEqual(client_changed[1], first[1], 'Client-only edit rebuilt AI')
        self.save()
        (self.root / 'ai.c').write_text('#include "shared.h"\nint ai(void) { return VALUE+1; }\n')
        self.load()
        self.run_command(*command)
        ai_changed = objects()
        self.assertEqual(ai_changed[0], client_changed[0], 'AI-only edit rebuilt client')
        self.assertGreater(ai_changed[1], client_changed[1])
        self.save()
        (self.root / 'shared.h').write_text('#define VALUE 2\n')
        self.load()
        self.run_command(*command)
        shared_changed = objects()
        self.assertTrue(all(a > b for a, b in zip(shared_changed, ai_changed)), 'Public-header dependents not rebuilt')
        self.save()
        # Simulate a tar restore losing nanoseconds on the complete Ninja state.
        for path in self.build.iterdir():
            if path.is_file():
                ns = path.stat().st_mtime_ns // 1_000_000_000 * 1_000_000_000
                os.utime(path, ns=(ns, ns))
        for name in cache.tracked(self.root):
            os.utime(self.root / name, None)
        self.load()
        self.run_command(*command)
        self.assertEqual(shared_changed, objects(), 'Restored Ninja state rebuilt unchanged objects')
        print('REAL_DEPENDENCY_GRAPH_PASS backend=' + ('ninja' if ninja else 'make'))


if __name__ == '__main__':
    unittest.main(verbosity=2)
