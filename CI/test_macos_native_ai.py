#!/usr/bin/env python3
"""Portable synthetic/mocked runner contracts, never native Mac evidence."""

import contextlib
import hashlib
import io
import json
import os
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path
from unittest.mock import MagicMock, patch

import run_macos_native_ai as runner


def listing_fixture():
    directories = [f"Path = {name}\nSize = 0\nAttributes = D\nEncrypted = -"
                   for name in sorted(runner.FIXTURE_ROOTS)]
    return "\n\n".join(directories + [
        "Path = Data/example.txt\nSize = 3\nAttributes = A\nEncrypted = -"])


def write_xml(path, names):
    root = ET.Element("testsuites", tests=str(len(names)), failures="0", errors="0", disabled="0")
    for identity in sorted(names):
        classname, name = identity.split(".", 1)
        ET.SubElement(root, "testcase", classname=classname, name=name, status="run", result="completed")
    ET.ElementTree(root).write(path)
    return root


class FixtureContracts(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def test_archive_copy_requires_pinned_bytes_and_hash(self):
        source = self.root / "source.7z"
        source.write_bytes(b"fixture")
        with patch.object(runner, "FIXTURE_BYTES", 7), patch.object(
                runner, "FIXTURE_SHA256", hashlib.sha256(b"fixture").hexdigest()):
            runner.copy_verified_archive(source, self.root / "verified.7z")
            self.assertEqual((self.root / "verified.7z").read_bytes(), b"fixture")
            source.write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                runner.copy_verified_archive(source, self.root / "wrong.7z")
            source.write_bytes(b"too large")
            with self.assertRaisesRegex(ValueError, "size"):
                runner.copy_verified_archive(source, self.root / "large.7z")

    def test_binary_hash_is_read_only_and_matches_actual_bytes(self):
        path = self.root / "synthetic-binary"
        path.write_bytes(b"synthetic executable bytes")
        before = path.read_bytes()
        self.assertEqual(runner.sha256_file(path), hashlib.sha256(before).hexdigest())
        self.assertEqual(path.read_bytes(), before)

    def check_listing(self, listing):
        with patch.object(runner, "FIXTURE_FILES", 1), patch.object(runner, "FIXTURE_UNPACKED_BYTES", 3):
            return runner.check_archive_listing(listing)

    def test_listing_has_exact_roots_files_and_sizes(self):
        entries = self.check_listing(listing_fixture())
        self.assertEqual(entries["Data/example.txt"], (False, 3))
        self.assertEqual(len(entries), 6)

    def test_listing_rejects_unsafe_paths_and_duplicates(self):
        for name in ("../escaped", "/tmp/escaped", "Data/../escaped", "Data//file",
                     "Data\\file", "Data/file:stream", "Other/file", "Data/a/b"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.check_listing(listing_fixture().replace("Data/example.txt", name))
        with self.assertRaisesRegex(ValueError, "duplicate"):
            self.check_listing(listing_fixture() + "\n\nPath = Data/example.txt\nSize = 0")

    def test_listing_rejects_links_encryption_malformed_and_wrong_totals(self):
        additions = ("\nSymbolic Link = /tmp/target", "\nHard Link = Data/target",
                     "\nSize = 3")
        for addition in additions:
            with self.subTest(addition=addition), self.assertRaises(ValueError):
                self.check_listing(listing_fixture() + addition)
        for text in (listing_fixture().replace("Attributes = A", "Attributes = A_ lrwxrwxrwx"),
                     listing_fixture().replace("Encrypted = -", "Encrypted = +"),
                     listing_fixture().replace("Size = 3", "Size = 4"),
                     listing_fixture().replace("Size = 3", "Size = -1"),
                     "not a technical archive listing", ""):
            with self.subTest(text=text[:50]), self.assertRaises(ValueError):
                self.check_listing(text)

    def test_extraction_uses_private_macos_home_and_separate_test_preset(self):
        home, scratch = self.root / "home", self.root / "tmp"
        home.mkdir()
        scratch.mkdir()
        env = runner.isolated_environment(home, scratch)
        data = home / "Library/Application Support/vcmi"

        def fake_capture(command, **kwargs):
            self.assertEqual(kwargs["env"], env)
            if command[1] == "l":
                return listing_fixture()
            self.assertEqual(command[1:3], ["x", "-y"])
            for name in runner.FIXTURE_ROOTS:
                (data / name).mkdir()
            (data / "Data/example.txt").write_bytes(b"abc")
            return "Everything is Ok"

        with patch.object(runner, "copy_verified_archive"), \
                patch.object(runner, "capture", side_effect=fake_capture), \
                patch.object(runner, "FIXTURE_FILES", 1), patch.object(runner, "FIXTURE_UNPACKED_BYTES", 3):
            runner.prepare_fixture(self.root / "supplied.7z", self.root, env, "/installed/7zz")
        preset = json.loads((data / "config/testModSettings.json").read_text())
        self.assertEqual(preset["presets"]["default"]["mods"], ["vcmi", "vcmi-test"])
        self.assertFalse((data / "config/modSettings.json").exists())

    def test_missing_archiver_fails_before_extraction(self):
        with patch.object(runner.shutil, "which", return_value=None), \
                patch.object(runner, "capture") as capture, self.assertRaisesRegex(ValueError, "No installed"):
            runner.discover_archiver()
        capture.assert_not_called()


class InventoryAndXMLContracts(unittest.TestCase):
    def test_parse_inventory_and_reject_empty_duplicate_or_malformed(self):
        text = "Note: Google Test filter = Suite.*\nSuite.\n  First\n  Second\n"
        self.assertEqual(runner.parse_test_inventory(text), {"Suite.First", "Suite.Second"})
        for listing in ("", "Suite.\n", "Suite.\n  First\n  First\n", "First\n", "  Orphan\n"):
            with self.subTest(listing=listing), self.assertRaises(ValueError):
                runner.parse_test_inventory(listing)

    def test_next_candidate_inventory_requires_exact_gates_and_forecasts(self):
        selected = {f"AIIntegrationNativeBattle.case{x}" for x in range(306)}
        selected |= {f"HoMM3ForecastQueueNative.case{x}" for x in range(8)}
        full = selected | runner.GATED_NATIVE
        self.assertEqual(len(runner.GATED_NATIVE), 20)
        self.assertEqual(runner.verify_native_inventory(full, 306, 8), selected)
        self.assertTrue(all(runner.matches_filter(x, runner.NATIVE_FILTER) for x in selected))
        self.assertFalse(any(runner.matches_filter(x, runner.NATIVE_FILTER) for x in runner.GATED_NATIVE))
        with self.assertRaisesRegex(ValueError, "candidate counts"):
            runner.verify_native_inventory(full, 304, 0)
        with self.assertRaisesRegex(ValueError, "20 gated"):
            runner.verify_native_inventory(full - {next(iter(runner.GATED_NATIVE))}, 305, 8)
        with self.assertRaisesRegex(ValueError, "20 gated"):
            runner.verify_native_inventory(full | {"AIIntegrationNativeBattle.mmaiUnreviewedRealBattle"}, 307, 8)

    def test_lifecycle_namespace_is_selected_and_counted_exactly(self):
        selected = {f"AIIntegrationNativeBattle.case{x}" for x in range(306)}
        selected |= {f"HoMM3ForecastQueueNative.case{x}" for x in range(8)}
        selected |= {f"HoMM3ForecastLifecycleNative.case{x}" for x in range(10)}
        full = selected | runner.GATED_NATIVE
        self.assertEqual(runner.verify_native_inventory(full, 306, 18, 10), selected)
        self.assertTrue(all(runner.matches_filter(x, runner.NATIVE_FILTER) for x in selected))
        self.assertEqual(runner.selected_suites(306, 18)[0][2], 324)
        with self.assertRaisesRegex(ValueError, "candidate counts"):
            runner.verify_native_inventory(full, 306, 18, 9)
        with self.assertRaisesRegex(ValueError, "candidate counts"):
            runner.verify_native_inventory(full, 306, 8, 0)
        with self.assertRaisesRegex(ValueError, "candidate counts"):
            runner.verify_native_inventory(full | {"HoMM3ForecastLifecycleNativeExtra.case0"}, 306, 19, 10)

    def test_base_inventory_is_an_explicit_separate_mode(self):
        selected = {f"AIIntegrationNativeBattle.case{x}" for x in range(304)}
        self.assertEqual(runner.verify_native_inventory(selected | runner.GATED_NATIVE, 304, 0), selected)
        suites = runner.selected_suites(306, 8)
        self.assertEqual([s[2] for s in suites], [314, 48, 145])
        self.assertEqual([s[3] for s in suites], [300, 90, 900])

    def test_xml_matches_actual_selected_inventory(self):
        with tempfile.TemporaryDirectory() as directory:
            xml = Path(directory) / "report.xml"
            names = {"Suite.One", "Suite.Two"}
            write_xml(xml, names)
            self.assertEqual(runner.validate_report(xml, names, "Suite.*"), 2)
            with self.assertRaisesRegex(ValueError, "identities"):
                runner.validate_report(xml, {"Suite.One", "Suite.Other"}, "Suite.*")

    def test_missing_malformed_empty_failed_skipped_duplicate_or_unrun_xml_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            xml = Path(directory) / "report.xml"
            names = {"Suite.One", "Suite.Two"}
            with self.assertRaisesRegex(ValueError, "Missing or malformed"):
                runner.validate_report(xml, names, "Suite.*")
            xml.write_text("<broken")
            with self.assertRaisesRegex(ValueError, "Missing or malformed"):
                runner.validate_report(xml, names, "Suite.*")
            mutations = [
                lambda r: r.set("tests", "0"),
                lambda r: r.set("failures", "1"),
                lambda r: r.set("errors", "1"),
                lambda r: r.set("disabled", "1"),
                lambda r: r.set("skipped", "1"),
                lambda r: ET.SubElement(r[0], "failure"),
                lambda r: ET.SubElement(r[0], "error"),
                lambda r: ET.SubElement(r[0], "skipped"),
                lambda r: r[1].set("name", "One"),
                lambda r: r[0].set("status", "notrun"),
                lambda r: r[0].set("result", "suppressed"),
                lambda r: r[0].set("classname", "Unselected"),
                lambda r: r.remove(r[0]),
            ]
            for mutation in mutations:
                root = write_xml(xml, names)
                mutation(root)
                ET.ElementTree(root).write(xml)
                with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                    runner.validate_report(xml, names, "Suite.*")


class ProcessAndBoundaryContracts(unittest.TestCase):
    def test_environment_allowlist_does_not_inherit_dyld_or_google_test_controls(self):
        with patch.dict(os.environ, {"DYLD_LIBRARY_PATH": "/bad", "GTEST_REPEAT": "-1",
                                    "VCMI_AI_MMAI_NEURAL_TEST": "1", "SECRET": "do not copy"}):
            env = runner.isolated_environment(Path("/isolated/home"), Path("/isolated/tmp"))
        self.assertEqual(set(env), {"HOME", "TMPDIR", "PATH", "LANG", "LC_ALL"})
        self.assertEqual(env["HOME"], "/isolated/home")
        self.assertEqual(env["PATH"], "/usr/bin:/bin:/usr/sbin:/sbin")
        self.assertEqual(env["LANG"], "C")
        self.assertEqual(env["LC_ALL"], "C")

    def test_other_hosts_rejected_before_any_command(self):
        with patch.object(runner.platform, "system", return_value="Linux"), \
                patch.object(runner, "capture") as capture, self.assertRaisesRegex(ValueError, "macOS ARM64"):
            runner.check_binary(Path("/missing"), {})
        capture.assert_not_called()

    def test_universal_binary_rejected_before_signature_check(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "vcmitest"
            binary.write_text("synthetic, not executable code")
            binary.chmod(0o700)
            for name in ("config/filesystem.json", "Mods/vcmi/mod.json",
                         "scripts/damage/damageCalculator.lua", "test/testdata/vcmi-test/mod.json"):
                path = binary.parent / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            with patch.object(runner.platform, "system", return_value="Darwin"), \
                    patch.object(runner.platform, "machine", return_value="arm64"), \
                    patch.object(runner, "capture", return_value="x86_64 arm64\n") as capture, \
                    self.assertRaisesRegex(ValueError, "one arm64"):
                runner.check_binary(binary, {})
            self.assertEqual(capture.call_count, 1)

    def test_process_failure_cannot_become_pass_from_valid_xml(self):
        self.check_process(returncode=3, timeout=False)

    def test_synthetic_success_requires_zero_exit_and_exact_xml(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            names = {"Suite.One"}
            write_xml(output / "synthetic.xml", names)
            process = MagicMock()
            process.__enter__.return_value = process
            process.returncode = 0
            process.wait.return_value = 0
            with patch.object(runner.subprocess, "Popen", return_value=process):
                result = runner.run_suite(Path("/synthetic/vcmitest"), output, {},
                                          ("synthetic", "Suite.*", 1, 1), names)
            self.assertEqual(result["status"], "passed")
            self.assertEqual(result["passed_cases"], 1)

    def test_zero_exit_without_xml_is_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            process = MagicMock()
            process.__enter__.return_value = process
            process.returncode = 0
            process.wait.return_value = 0
            with patch.object(runner.subprocess, "Popen", return_value=process):
                result = runner.run_suite(Path("/synthetic/vcmitest"), Path(directory), {},
                                          ("missing", "Suite.*", 1, 1), {"Suite.One"})
            self.assertEqual(result["status"], "failed")
            self.assertNotIn("passed_cases", result)

    def test_capture_preserves_signature_failure(self):
        error = subprocess.CalledProcessError(1, ["codesign", "--verify"], stderr="invalid signature")
        with patch.object(runner.subprocess, "run", side_effect=error), \
                self.assertRaises(subprocess.CalledProcessError):
            runner.capture(["/usr/bin/codesign", "--verify", "/synthetic/vcmitest"], env={})

    def test_timeout_kills_process_group_and_is_not_a_pass(self):
        self.check_process(returncode=-9, timeout=True)

    def check_process(self, returncode, timeout):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            names = {"Suite.One"}
            write_xml(output / "synthetic.xml", names)
            process = MagicMock()
            process.__enter__.return_value = process
            process.pid = 12345
            process.returncode = returncode
            process.wait.side_effect = ([subprocess.TimeoutExpired("synthetic", 1), returncode]
                                        if timeout else [returncode])
            with patch.object(runner.subprocess, "Popen", return_value=process) as popen, \
                    patch.object(runner.os, "killpg") as killpg:
                result = runner.run_suite(Path("/synthetic/vcmitest"), output, {"HOME": "/synthetic"},
                                          ("synthetic", "Suite.*", 1, 1), names)
            self.assertEqual(result["status"], "failed")
            self.assertNotIn("passed_cases", result)
            self.assertTrue(result["errors"])
            self.assertEqual(popen.call_args.args[0][:2], ["/usr/bin/arch", "-arm64"])
            self.assertTrue(popen.call_args.kwargs["start_new_session"])
            if timeout:
                killpg.assert_called_once_with(12345, runner.signal.SIGKILL)
            else:
                killpg.assert_not_called()

    def test_setup_failure_has_failed_summary_without_native_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "output"
            with contextlib.redirect_stdout(io.StringIO()):
                status = runner.main(["--test-binary", "/missing-vcmitest",
                                      "--fixture-archive", "/missing-fixture", "--source-sha", "a" * 40,
                                      "--expected-native-count", "306", "--expected-forecast-count", "8",
                                      "--output-dir", str(output)])
            report = json.loads((output / "native-ai-summary.json").read_text())
            self.assertEqual(status, 1)
            self.assertEqual(report["status"], "failed")
            self.assertEqual(report["suites"], [])


if __name__ == "__main__":
    unittest.main()
