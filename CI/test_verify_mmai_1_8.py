#!/usr/bin/env python3
"""Test model-package verification with small synthetic byte fixtures."""

import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import verify_mmai_1_8 as checker


class ModelPackageVerificationTest(unittest.TestCase):
    def setUp(self):
        self.files = {"mod.json": json.dumps({"compatibility": {"min": "1.8.0", "max": "1.8.9"}}).encode(),
                      "config/mmai-settings.json": json.dumps({"models": checker.PIN["models"]}).encode()}
        self.files.update({"models/" + value: ("fixture-" + role).encode()
                           for role, value in checker.PIN["models"].items()})
        fixture_pin = dict(checker.PIN, files={name: hashlib.sha256(value).hexdigest()
                                             for name, value in self.files.items()})
        self.mock = patch.object(checker, "PIN", fixture_pin)
        self.mock.start()
        self.addCleanup(self.mock.stop)

    def test_exact_files_and_compatible_engine(self):
        for engine in ("1.8.0", "1.8.9"):
            self.assertEqual(checker.verify(self.files.__getitem__, engine)["engine_version"], engine)

    def test_incompatible_engine_fails(self):
        for engine in ("1.7.9", "1.9.0", "1.8.10"):
            with self.subTest(engine=engine), self.assertRaisesRegex(ValueError, "does not support"):
                checker.verify(self.files.__getitem__, engine)

    def test_altered_model_fails(self):
        self.files["models/" + checker.PIN["models"]["attacker"]] = b"changed"
        with self.assertRaisesRegex(ValueError, "differs or is damaged"):
            checker.verify(self.files.__getitem__, "1.8.0")

    def test_missing_defender_fails(self):
        del self.files["models/" + checker.PIN["models"]["defender"]]
        with self.assertRaises(KeyError):
            checker.verify(self.files.__getitem__, "1.8.0")

    def test_altered_settings_fail(self):
        self.files["config/mmai-settings.json"] = b'{"models":{}}'
        with self.assertRaisesRegex(ValueError, "config/mmai-settings.json"):
            checker.verify(self.files.__getitem__, "1.8.0")

    def test_unpinned_archive_fails_before_opening(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "mod.zip"
            archive.write_bytes(b"not a zip")
            with self.assertRaisesRegex(ValueError, "Unexpected archive size"):
                checker.verify_archive(archive, "1.8.0")


if __name__ == "__main__":
    unittest.main()
