#!/usr/bin/env python3
"""Portable smoke-runner contracts; no native Mac execution is simulated as proof."""

import plistlib
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import smoke_macos_ai_bundle as runner


SHA = "bbc23d095500a9a05fe5d6f33df37fd9fddfd645"


class PackagedSmokeTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name).resolve()
        self.app = self.root / "VCMI.app"
        self.server = self.app / "Contents/MacOS/vcmiserver"
        self.server.parent.mkdir(parents=True)
        self.server.touch()
        self.plist = self.app / "Contents/Info.plist"
        self.plist.write_bytes(plistlib.dumps({"CFBundleVersion": SHA}))
        self.calls = []
        self.home = None
        self.help = "Allowed options\n  --help\n  --version\n"
        self.version_sha = SHA
        self.exit_code = 0
        self.bad_path = False

    def run_process(self, arguments, **kwargs):
        self.calls.append((arguments, kwargs))
        if arguments[0] == "/usr/bin/codesign":
            return subprocess.CompletedProcess(arguments, 0)
        self.assertEqual(arguments[:3], ["/usr/bin/arch", "-arm64", str(self.server)])
        self.assertEqual(kwargs["cwd"], self.server.parent)
        self.assertEqual(kwargs["timeout"], 30)
        self.assertEqual(kwargs["stdin"], subprocess.DEVNULL)
        self.assertNotIn("shell", kwargs)
        environment = kwargs["env"]
        self.assertEqual(set(environment), {"HOME", "TMPDIR", "PATH", "LANG", "LC_ALL"})
        self.assertEqual(environment["PATH"], "/usr/bin:/bin")
        self.home = Path(environment["HOME"])
        self.assertTrue(self.home.is_dir())
        self.assertTrue((self.home / "tmp").is_dir())
        data = self.home / "Library/Application Support/vcmi"
        paths = {"user data": data, "user cache": data, "user config": data / "config",
                 "user logs": self.home / "Library/Logs/vcmi", "user saves": data / "Saves",
                 "user extracted": data / "extracted"}
        if self.bad_path:
            paths["user data"] = self.root / "outside"
        version = "1.8.0." + self.version_sha + "\n"
        version += "\n".join(f"  {label}:\t\t{value}" for label, value in paths.items()) + "\n"
        output = self.help if arguments[-1] == "--help" else version
        return subprocess.CompletedProcess(arguments, self.exit_code, output, "")

    def smoke(self):
        with patch.object(runner.subprocess, "run", self.run_process):
            return runner.smoke(self.app, SHA)

    def test_native_server_options_signature_and_isolated_home(self):
        result = self.smoke()
        self.assertEqual(result["source_commit"], SHA)
        self.assertEqual(result["server_version"], "1.8.0." + SHA)
        self.assertEqual(self.calls[0][0], ["/usr/bin/codesign", "--verify", "--deep", "--strict", str(self.app)])
        self.assertEqual([call[0][-1] for call in self.calls[1:]], ["--help", "--version"])
        self.assertEqual(result["isolated_home_paths"], "verified")
        self.assertNotIn(str(self.home), str(result))
        self.assertFalse(self.home.exists())
        self.assertIn("not run", result["launcher_cli"])

    def test_empty_help_is_not_success(self):
        self.help = ""
        with self.assertRaisesRegex(ValueError, "expected help"):
            self.smoke()
        self.assertFalse(self.home.exists())

    def test_wrong_binary_commit_fails(self):
        self.version_sha = "0" * 40
        with self.assertRaisesRegex(ValueError, "report the exact source commit"):
            self.smoke()

    def test_wrong_bundle_commit_never_executes(self):
        self.plist.write_bytes(plistlib.dumps({"CFBundleVersion": "0" * 40}))
        with self.assertRaisesRegex(ValueError, "Bundle version"):
            self.smoke()
        self.assertEqual(self.calls, [])

    def test_invalid_expected_commit_never_executes(self):
        with patch.object(runner.subprocess, "run") as process:
            with self.assertRaisesRegex(ValueError, "complete lowercase"):
                runner.smoke(self.app, "short-sha")
            process.assert_not_called()

    def test_nonzero_process_exit_is_not_success(self):
        self.exit_code = -9
        with self.assertRaisesRegex(ValueError, "exited -9"):
            self.smoke()
        self.assertFalse(self.home.exists())

    def test_paths_outside_isolated_home_fail(self):
        self.bad_path = True
        with self.assertRaisesRegex(ValueError, "escaped the isolated HOME"):
            self.smoke()

    def test_missing_server_never_executes(self):
        self.server.unlink()
        with self.assertRaisesRegex(ValueError, "Missing or external"):
            self.smoke()
        self.assertEqual(self.calls, [])

    def test_external_server_symlink_never_executes(self):
        self.server.unlink()
        external = self.root / "outside-server"
        external.touch()
        self.server.symlink_to(external)
        with self.assertRaisesRegex(ValueError, "Missing or external"):
            self.smoke()
        self.assertEqual(self.calls, [])

    def test_signature_failure_prevents_execution(self):
        with patch.object(runner.subprocess, "run", side_effect=subprocess.CalledProcessError(1, "codesign")) as process:
            with self.assertRaises(subprocess.CalledProcessError):
                runner.smoke(self.app, SHA)
            self.assertEqual(process.call_count, 1)

    def test_timeout_is_not_success(self):
        def timeout(arguments, **kwargs):
            if arguments[0].endswith("codesign"):
                return self.run_process(arguments, **kwargs)
            self.run_process(arguments, **kwargs)
            raise subprocess.TimeoutExpired(arguments, kwargs["timeout"])
        with patch.object(runner.subprocess, "run", timeout):
            with self.assertRaises(subprocess.TimeoutExpired):
                runner.smoke(self.app, SHA)
        self.assertFalse(self.home.exists())


if __name__ == "__main__":
    unittest.main()
