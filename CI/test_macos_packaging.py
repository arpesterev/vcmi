#!/usr/bin/env python3
"""Portable tests of staged-bundle cleanup and the CMake signing sequence."""

import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import test_macos_ai_bundle as bundle_fixture


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("normalize_bundle_rpaths", ROOT / "osx/normalize_bundle_rpaths.py")
normalizer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(normalizer)


def loads(rpaths, dependency="@rpath/libQt5Core.5.dylib"):
    result = ["cmd LC_ID_DYLIB", "name @rpath/libQt5Widgets.5.dylib (offset 24)",
              "cmd LC_LOAD_DYLIB", f"name {dependency} (offset 24)"]
    for value in rpaths:
        result += ["Load command 1", "cmd LC_RPATH", "cmdsize 64", f"path {value} (offset 12)"]
    return "\n".join(result)


class NormalizeTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name).resolve()
        self.app = self.root / "staged app/VCMI.app"
        (self.app / "Contents/MacOS").mkdir(parents=True)
        self.image = self.app / "Contents/Frameworks/libQt5Widgets.5.dylib"
        self.image.parent.mkdir()
        self.image.write_bytes(bytes.fromhex("cffaedfe"))
        self.external = "/Users/runner/.conan2/p/b/libpne865d24ed7b31/p/lib"
        self.rpaths = [self.external, "@loader_path", "@executable_path/../Frameworks",
                       "/usr/lib/swift", "/System/Library/Frameworks", "relative/lib"]
        self.dependency = "@rpath/libQt5Core.5.dylib"
        self.calls = []

    def run_tool(self, tool, *args):
        self.calls.append((tool, *args))
        self.assertEqual(Path(args[-1]), self.image)
        if tool == "/usr/bin/lipo":
            self.assertEqual(args[:-1], ("-archs",))
            return "arm64"
        if tool == "/usr/bin/otool":
            self.assertEqual(args[:-1], ("-arch", "all", "-l"))
            return loads(self.rpaths, self.dependency)
        self.assertEqual(tool, "/usr/bin/install_name_tool")
        self.assertEqual(args[0], "-delete_rpath")
        self.rpaths = [value for value in self.rpaths if value != args[1]]
        return ""

    def normalize(self):
        with patch.object(normalizer, "run", self.run_tool):
            return normalizer.normalize(self.app)

    def test_remove_only_non_system_absolute_rpaths(self):
        before = self.rpaths[1:]
        self.assertEqual(self.normalize(), 1)
        self.assertEqual(self.rpaths, before)
        self.assertEqual(self.dependency, "@rpath/libQt5Core.5.dylib")
        self.assertEqual(self.calls[2], ("/usr/bin/install_name_tool", "-delete_rpath",
                                        self.external, str(self.image)))

    def test_system_classification_uses_normalized_component_boundaries(self):
        for path in ("/usr/lib", "/usr/lib/../lib/swift", "/System/Library/Frameworks"):
            with self.subTest(path=path):
                self.assertFalse(normalizer.remove_rpath(path))
        for path in ("/usr/library", "/System/LibraryExtra", "/usr/lib/../../opt/homebrew/lib"):
            with self.subTest(path=path):
                self.assertTrue(normalizer.remove_rpath(path))

    def test_spaces_and_duplicate_rpath_entries(self):
        self.external = "/Users/runner/build cache/lib (debug)"
        self.rpaths = [self.external, self.external, "@loader_path"]
        self.normalize()
        self.assertEqual(self.rpaths, ["@loader_path"])
        self.assertEqual(sum(call[0].endswith("install_name_tool") for call in self.calls), 1)

    def test_load_dependencies_and_install_id_are_preserved(self):
        self.dependency = "/Users/runner/cache/libMissing.dylib"
        self.normalize()
        self.assertEqual(self.dependency, "/Users/runner/cache/libMissing.dylib")
        self.assertTrue(all("-change" not in call and "-id" not in call for call in self.calls))

    def test_no_cleanup_does_not_mutate(self):
        self.rpaths = self.rpaths[1:]
        self.assertEqual(self.normalize(), 0)
        self.assertEqual(len(self.calls), 1)

    def test_malformed_rpath_is_rejected(self):
        for output in ("cmd LC_RPATH", "cmd LC_RPATH\npath /broken",
                       "cmd LC_RPATH\ncmd LC_LOAD_DYLIB"):
            with self.subTest(output=output), self.assertRaisesRegex(ValueError, "Malformed"):
                normalizer.parse_load_commands(output)

    def test_external_symlink_stops_before_mutation(self):
        outside = self.root / "conan-cache.dylib"
        outside.write_bytes(self.image.read_bytes())
        (self.image.parent / "z-external.dylib").symlink_to(outside)
        with self.assertRaisesRegex(ValueError, "External or broken"):
            self.normalize()
        self.assertFalse(any(call[0].endswith("install_name_tool") for call in self.calls))
        self.assertEqual(outside.read_bytes(), self.image.read_bytes())

    def test_broken_symlink_stops_before_mutation(self):
        (self.image.parent / "z-broken.dylib").symlink_to(self.root / "missing")
        with self.assertRaisesRegex(ValueError, "External or broken"):
            self.normalize()
        self.assertFalse(any(call[0].endswith("install_name_tool") for call in self.calls))

    def test_hard_link_to_cache_is_rejected(self):
        os.link(self.image, self.root / "cache-image")
        with self.assertRaisesRegex(ValueError, "hard-linked"):
            self.normalize()
        self.assertEqual(self.calls, [])

    def test_internal_symlink_is_not_processed_twice(self):
        (self.image.parent / "libQt5Widgets.dylib").symlink_to(self.image.name)
        self.assertEqual(self.normalize(), 1)
        self.assertEqual(len(self.calls), 4)

    def test_external_app_symlink_is_rejected(self):
        alias = self.root / "alias.app"
        alias.symlink_to(self.app, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "must not be a symlink"):
            normalizer.normalize(alias)

    def test_tool_failure_is_not_ignored(self):
        def fail(tool, *args):
            if tool.endswith("install_name_tool"):
                raise subprocess.CalledProcessError(1, [tool, *args])
            return self.run_tool(tool, *args)
        with patch.object(normalizer, "run", fail), self.assertRaises(subprocess.CalledProcessError):
            normalizer.normalize(self.app)

    def test_ineffective_cleanup_is_rejected(self):
        with patch.object(normalizer, "run", lambda *args: "arm64" if args[0].endswith("lipo") else loads(self.rpaths)):
            with self.assertRaisesRegex(ValueError, "Unexpected load commands"):
                normalizer.normalize(self.app)

    def test_changed_dependency_is_rejected(self):
        def corrupt(tool, *args):
            result = self.run_tool(tool, *args)
            if tool.endswith("install_name_tool"):
                self.dependency = "@rpath/changed.dylib"
            return result
        with patch.object(normalizer, "run", corrupt):
            with self.assertRaisesRegex(ValueError, "Unexpected load commands"):
                normalizer.normalize(self.app)

    def test_universal_with_different_rpaths_fails_before_any_mutation(self):
        universal = self.image.parent / "z-universal.dylib"
        universal.write_bytes(bytes.fromhex("cafebabe"))

        def universal_tool(tool, *args):
            if Path(args[-1]) == universal:
                self.calls.append((tool, *args))
                if tool.endswith("lipo"):
                    return "x86_64 arm64"
                self.assertTrue(tool.endswith("otool"))
                return ("image (architecture x86_64):\n" + loads(["@loader_path"])
                        + "\nimage (architecture arm64):\n" + loads([self.external]))
            return self.run_tool(tool, *args)

        with patch.object(normalizer, "run", universal_tool):
            with self.assertRaisesRegex(ValueError, "single arm64 slice"):
                normalizer.normalize(self.app)
        self.assertFalse(any(call[0].endswith("install_name_tool") for call in self.calls))

    def test_intel_image_needing_cleanup_is_rejected(self):
        def intel_tool(tool, *args):
            return "x86_64" if tool.endswith("lipo") else self.run_tool(tool, *args)
        with patch.object(normalizer, "run", intel_tool):
            with self.assertRaisesRegex(ValueError, "single arm64 slice"):
                normalizer.normalize(self.app)
        self.assertFalse(any(call[0].endswith("install_name_tool") for call in self.calls))

    def test_universal_without_external_rpaths_is_not_mutated(self):
        self.image.write_bytes(bytes.fromhex("cafebabe"))
        self.rpaths = ["@loader_path", "/usr/lib/swift"]
        self.assertEqual(self.normalize(), 0)
        self.assertEqual(len(self.calls), 1)


class BundleRelocationTest(unittest.TestCase):
    def test_cleanup_preserves_strict_dependency_audit(self):
        fixture = bundle_fixture.BundleAuditTest()
        fixture.setUp()
        self.addCleanup(fixture.doCleanups)
        dependency = "/usr/lib/libSystem.B.dylib"
        fixture.commands[fixture.facade] = bundle_fixture.loads((dependency,),
            ("/Users/runner/.conan2/p/b/libpne865d24ed7b31/p/lib", "@loader_path"))
        with self.assertRaisesRegex(ValueError, "External build-machine rpath"):
            fixture.audit()

        def run_tool(tool, *args):
            image = Path(args[-1])
            if tool.endswith("lipo"):
                return "arm64"
            if tool.endswith("otool"):
                return fixture.commands[image]
            self.assertEqual(tool, "/usr/bin/install_name_tool")
            self.assertEqual(args[0], "-delete_rpath")
            dependencies, rpaths = bundle_fixture.checker.parse_load_commands(fixture.commands[image])
            fixture.commands[image] = bundle_fixture.loads(dependencies,
                [value for value in rpaths if value != args[1]])
            return ""

        with patch.object(normalizer, "run", run_tool):
            self.assertEqual(normalizer.normalize(fixture.app), 1)
        self.assertEqual(fixture.audit()["source_commit"], bundle_fixture.SHA)
        fixture.commands[fixture.facade] = bundle_fixture.loads(("/opt/homebrew/lib/missing.dylib",))
        with self.assertRaisesRegex(ValueError, "external dependency"):
            fixture.audit()


@unittest.skipUnless(shutil.which("cmake"), "CMake is required for packaging-sequence tests")
class PackagingSequenceTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name).resolve()
        osx = self.root / "source/osx"
        osx.mkdir(parents=True)
        shutil.copy(ROOT / "osx/CMakeLists.txt", osx)
        self.events = self.root / "events.jsonl"
        event_script = '''import json, os, sys
phase = sys.argv[1]
args = sys.argv[2:]
with open(os.environ["PACKAGING_EVENTS"], "a") as out:
    out.write(json.dumps([phase, *args]) + "\\n")
failure = os.environ.get("PACKAGING_FAILURE", "")
if failure == phase or (phase == "codesign" and failure and failure in args[-1]):
    sys.exit(1)
if phase == "codesign" and failure == "verify" and "--verify" in args:
    sys.exit(1)
'''
        self.event_script = self.root / "event.py"
        self.event_script.write_text(event_script)
        (osx / "normalize_bundle_rpaths.py").write_text(event_script.replace('phase = sys.argv[1]', 'phase = "normalize"'))
        bin_dir = self.root / "bin"
        bin_dir.mkdir()
        codesign = bin_dir / "codesign"
        codesign.write_text(f'#!{sys.executable}\nimport runpy, sys\nsys.argv.insert(1, "codesign")\nrunpy.run_path({str(self.event_script)!r}, run_name="__main__")\n')
        codesign.chmod(0o755)
        self.env = dict(os.environ, PATH=str(bin_dir) + os.pathsep + os.environ["PATH"],
                        PACKAGING_EVENTS=str(self.events))
        self.env.pop("MACOS_CODE_SIGN_IDENTITY", None)
        (self.root / "source/CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.19)
project(PackagingSequence NONE)
set(MACOS ON)
set(APP_BUNDLE_DIR VCMI.app)
set(CMAKE_SYSTEM_PROCESSOR ${{PACKAGING_PROCESSOR}})
set(ENABLE_LAUNCHER ON)
function(vcmi_deploy_qt)
  install(CODE "execute_process(COMMAND \\"{sys.executable}\\" \\"{self.event_script}\\" deploy COMMAND_ERROR_IS_FATAL ANY)")
endfunction()
add_subdirectory(osx)
''')
        self.app = self.root / "stage/VCMI.app"
        for name in ("MacOS/vcmibuilder", "MacOS/vcmiserver", "MacOS/vcmiclient",
                     "MacOS/vcmilauncher", "Frameworks/libQt5Widgets.5.dylib"):
            path = self.app / "Contents" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        self.configure()

    def configure(self, processor="arm64"):
        subprocess.run(["cmake", "-S", str(self.root / "source"), "-B", str(self.root / "build"),
                        f"-DPython3_EXECUTABLE={sys.executable}", f"-DPACKAGING_PROCESSOR={processor}"], check=True, env=self.env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    def install(self, failure=""):
        result = subprocess.run(["cmake", "--install", str(self.root / "build"),
                                 "--prefix", str(self.app.parent)],
                                env=dict(self.env, PACKAGING_FAILURE=failure),
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        events = [json.loads(line) for line in self.events.read_text().splitlines()]
        return result, events

    def test_deploy_cleanup_sign_and_final_verify_order(self):
        result, events = self.install()
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual([event[0] for event in events[:2]], ["deploy", "normalize"])
        self.assertEqual(events[1][-1], str(self.app))
        self.assertEqual(events[-1], ["codesign", "--verify", "--deep", "--strict", str(self.app)])
        self.assertEqual(len(events[2:-1]), 5)
        for event in events[2:-1]:
            self.assertEqual(event[0], "codesign")
            self.assertIn("--sign", event)
            self.assertIn("-", event)

    def test_cleanup_failure_stops_before_signing(self):
        result, events = self.install("normalize")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual([event[0] for event in events], ["deploy", "normalize"])

    def test_library_signing_failure_is_fatal(self):
        result, events = self.install("libQt5Widgets.5.dylib")
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(events[-1][-1].endswith("libQt5Widgets.5.dylib"))
        self.assertFalse(any("--verify" in event for event in events))

    def test_executable_signing_failure_is_fatal(self):
        result, events = self.install("vcmiclient")
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(events[-1][-1].endswith("vcmiclient"))

    def test_final_bundle_verification_failure_is_fatal(self):
        result, events = self.install("verify")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(events[-1], ["codesign", "--verify", "--deep", "--strict", str(self.app)])

    def test_intel_without_identity_does_not_clean_or_sign(self):
        self.configure("x86_64")
        result, events = self.install()
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(events, [["deploy"]])

    def test_intel_with_identity_retains_signing_without_cleanup(self):
        self.configure("x86_64")
        self.env["MACOS_CODE_SIGN_IDENTITY"] = "test-identity"
        result, events = self.install()
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(events[0], ["deploy"])
        self.assertEqual(len(events), 7)
        self.assertEqual(events[-1][-1], str(self.app))
        for event in events[1:]:
            self.assertEqual(event[0], "codesign")
            self.assertIn("test-identity", event)
            self.assertNotIn("--verify", event)


if __name__ == "__main__":
    unittest.main()
