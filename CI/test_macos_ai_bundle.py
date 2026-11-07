#!/usr/bin/env python3
"""Portable negative and positive tests of the macOS bundle audit."""

import plistlib
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import check_macos_ai_bundle as checker


SHA = "830fdfeb844e30494fab613a370073c3cdff470c"


def loads(dependencies=(), rpaths=()):
    result = []
    for value in dependencies:
        result.extend(("Load command 0", " cmd LC_LOAD_DYLIB", " cmdsize 88",
                       f" name {value} (offset 24)"))
    for value in rpaths:
        result.extend(("Load command 1", " cmd LC_RPATH", " cmdsize 64",
                       f" path {value} (offset 12)"))
    return "\n".join(result)


class BundleAuditTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.app = self.root / "VCMI.app"
        self.mac = self.app / "Contents/MacOS"
        self.frameworks = self.app / "Contents/Frameworks"
        self.mac.mkdir(parents=True)
        self.frameworks.mkdir()
        self.info = {"CFBundleVersion": SHA, "CFBundleExecutable": "vcmilauncher",
                     "CFBundleShortVersionString": "1.8"}
        self.save_info()
        self.cache = self.root / "CMakeCache.txt"
        self.cache.write_text("\n".join(f"{name}:BOOL=ON" for name in checker.ENABLED)
                              + "\nCMAKE_OSX_ARCHITECTURES:STRING=arm64\n")
        self.commands = {}
        self.archs = {}
        for name in ("vcmilauncher", "vcmiclient", "vcmiserver"):
            self.make_image(self.mac / name, ("@rpath/libvcmi.dylib",),
                            ("@executable_path/../Frameworks",))
        self.facade = self.frameworks / "libvcmi.dylib"
        self.make_image(self.facade, ("/usr/lib/libSystem.B.dylib",))
        self.facade.write_bytes(self.facade.read_bytes() + "\0".join(checker.BACKENDS).encode())
        self.mock = patch.object(checker, "run", self.run_tool)
        self.mock.start()
        self.addCleanup(self.mock.stop)

    def save_info(self):
        (self.app / "Contents/Info.plist").write_bytes(plistlib.dumps(self.info))

    def make_image(self, path, dependencies=(), rpaths=()):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(bytes.fromhex("cffaedfe"))
        self.commands[path] = loads(dependencies, rpaths)
        self.archs[path] = "arm64"

    def run_tool(self, tool, *args):
        path = Path(args[-1])
        if tool.endswith("lipo"):
            return self.archs[path]
        if tool.endswith("otool"):
            return self.commands[path]
        if tool.endswith("codesign"):
            return ""
        raise AssertionError(tool)

    def audit(self):
        return checker.audit(self.app, self.cache, SHA)

    def test_bundle_and_system_dependencies(self):
        self.assertEqual(self.audit()["source_commit"], SHA)

    def test_bundle_under_symlinked_temporary_directory(self):
        with tempfile.TemporaryDirectory() as target:
            alias = self.root / "temporary-directory-alias"
            alias.symlink_to(target, target_is_directory=True)
            directory = tempfile.TemporaryDirectory(dir=alias)
            fixture = BundleAuditTest()
            try:
                with patch.object(tempfile, "TemporaryDirectory", return_value=directory):
                    fixture.setUp()
                aliased_app = Path(directory.name) / "VCMI.app"
                self.assertNotEqual(aliased_app, aliased_app.resolve())
                result = checker.audit(aliased_app, fixture.cache, SHA)
                self.assertEqual(result["source_commit"], SHA)
                self.assertEqual(len(result["images"]), 4)
                fixture.archs[fixture.facade] = "x86_64"
                with self.assertRaisesRegex(ValueError, "Missing arm64"):
                    checker.audit(aliased_app, fixture.cache, SHA)
            finally:
                fixture.doCleanups()
                directory.cleanup()

    def test_plugin_inherits_launcher_rpath(self):
        self.make_image(self.app / "Contents/PlugIns/platforms/libqcocoa.dylib",
                        ("@rpath/libvcmi.dylib",))
        self.assertEqual(len(self.audit()["images"]), 5)

    def test_loader_relative_dependency(self):
        self.make_image(self.frameworks / "libonnxruntime.dylib")
        self.commands[self.facade] = loads(("@loader_path/libonnxruntime.dylib",))
        self.assertEqual(len(self.audit()["images"]), 5)

    def test_x86_only_dependency_fails(self):
        self.archs[self.facade] = "x86_64"
        with self.assertRaisesRegex(ValueError, "Missing arm64"):
            self.audit()

    def test_missing_runtime_dependency_fails(self):
        self.commands[self.facade] = loads(("@rpath/libonnxruntime.dylib",))
        with self.assertRaisesRegex(ValueError, "Unresolved"):
            self.audit()

    def test_build_machine_dependency_fails(self):
        self.commands[self.facade] = loads(("/opt/homebrew/lib/libonnxruntime.dylib",))
        with self.assertRaisesRegex(ValueError, "external dependency"):
            self.audit()

    def test_external_rpath_fails_even_if_library_is_bundled(self):
        self.commands[self.facade] = loads((), ("/Users/runner/.conan2/lib",))
        with self.assertRaisesRegex(ValueError, "External build-machine rpath"):
            self.audit()

    def test_wrong_commit_fails(self):
        self.info["CFBundleVersion"] = "0" * 40
        self.save_info()
        with self.assertRaisesRegex(ValueError, "exact source commit"):
            self.audit()

    def test_disabled_mmai_fails(self):
        self.cache.write_text(self.cache.read_text().replace("ENABLE_MMAI:BOOL=ON", "ENABLE_MMAI:BOOL=OFF"))
        with self.assertRaisesRegex(ValueError, "ENABLE_MMAI"):
            self.audit()

    def test_wrong_configured_architecture_fails(self):
        self.cache.write_text(self.cache.read_text().replace("STRING=arm64", "STRING=x86_64"))
        with self.assertRaisesRegex(ValueError, "explicitly for arm64"):
            self.audit()

    def test_missing_backend_identity_fails(self):
        self.facade.write_bytes(bytes.fromhex("cffaedfe") + b"MMAI")
        with self.assertRaisesRegex(ValueError, "Missing backend identity"):
            self.audit()

    def test_external_symlink_fails(self):
        (self.frameworks / "outside").symlink_to(self.cache)
        with self.assertRaisesRegex(ValueError, "External or broken bundle symlink"):
            self.audit()

    def test_invalid_signature_fails(self):
        def invalid(tool, *args):
            if tool.endswith("codesign"):
                raise checker.subprocess.CalledProcessError(1, [tool, *args])
            return self.run_tool(tool, *args)
        with patch.object(checker, "run", invalid):
            with self.assertRaises(checker.subprocess.CalledProcessError):
                self.audit()

    def test_model_weights_are_not_bundled(self):
        (self.app / "model.onnx").write_bytes(b"model")
        with self.assertRaisesRegex(ValueError, "Neural weights"):
            self.audit()

    def test_game_archives_are_not_bundled(self):
        (self.app / "H3bitmap.lod").write_bytes(b"archive")
        with self.assertRaisesRegex(ValueError, "Heroes III game archives"):
            self.audit()

    def test_self_install_id_is_not_a_dependency(self):
        deps, rpaths = checker.parse_load_commands("cmd LC_ID_DYLIB\nname @rpath/libvcmi.dylib (offset 24)")
        self.assertEqual((deps, rpaths), ([], []))


if __name__ == "__main__":
    unittest.main()
