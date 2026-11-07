import copy
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from xml.etree import ElementTree as ET

import check_mmai_v13_compatibility as check


class V13CompatibilityChecks(unittest.TestCase):
    def test_tensor_contract(self):
        tensors = [{"name": name, "type": dtype, "shape": shape}
                   for name, dtype, shape in check.OUTPUTS]
        check.require_tensors(tensors, check.OUTPUTS)
        for field, value in (("name", "wrong"), ("type", 9), ("shape", [165, 4])):
            bad = copy.deepcopy(tensors)
            bad[4][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                check.require_tensors(bad, check.OUTPUTS)
        with self.assertRaises(ValueError):
            check.require_tensors(tensors[:-1], check.OUTPUTS)

    def test_metadata_contract(self):
        table = [[[-1] * 165 for _ in range(165)] for _ in range(4)]
        table[0][0][0] = 1
        metadata = {"version": "13", "side": "0", "action_table": json.dumps(table)}
        self.assertEqual(check.require_metadata(metadata, "attacker")["side"], "0")
        for changes in ({"version": "15"}, {"side": "1"}, {"version": "13junk"},
                        {"action_table": "[]"}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                check.require_metadata({**metadata, **changes}, "attacker")
        for value in (True, 1.5, 2312, -(2 ** 31) - 1):
            table[2][42][42] = value
            with self.subTest(value=value), self.assertRaises(ValueError):
                check.require_metadata({**metadata, "action_table": json.dumps(table)}, "attacker")

    def test_corrupt_download_rejected_before_onnx_parse(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            (directory / check.MODELS["attacker"]["name"]).write_bytes(b"not the released model")
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                check.verified_model("attacker", directory)

    @staticmethod
    def write_report(path, names, status=None):
        root = ET.Element("testsuites")
        suite = ET.SubElement(root, "testsuite")
        for name in names:
            case = ET.SubElement(suite, "testcase", name=name)
            if status:
                ET.SubElement(case, status)
        ET.ElementTree(root).write(path)

    def test_missing_skipped_failed_duplicate_battles_are_not_success(self):
        with tempfile.TemporaryDirectory() as temp:
            report = Path(temp) / "report.xml"
            self.write_report(report, check.EXPECTED_TESTS)
            check.require_battle_report(report)
            for status in ("failure", "error", "skipped"):
                self.write_report(report, check.EXPECTED_TESTS, status)
                with self.subTest(status=status), self.assertRaises(ValueError):
                    check.require_battle_report(report)
            for names in ([], list(check.EXPECTED_TESTS)[:-1], list(check.EXPECTED_TESTS) * 2):
                self.write_report(report, names)
                with self.assertRaises(ValueError):
                    check.require_battle_report(report)

    def test_independent_processes_and_exact_preset_restoration(self):
        for fail in (False, True):
            with self.subTest(fail=fail), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                models = root / "models"
                models.mkdir()
                for spec in check.MODELS.values():
                    (models / spec["name"]).write_bytes(b"fixture for orchestration unit test only")
                preset = root / "config/vcmi/testModSettings.json"
                preset.parent.mkdir(parents=True)
                original = b'{"activePreset":"test", "presets":{"test":{"mods":["vcmi","vcmi-test","mmai"]}}}\n'
                preset.write_bytes(original)
                calls = []

                def run(command, *, cwd, env, timeout, check):
                    calls.append(command)
                    mounted = json.loads(preset.read_bytes())["presets"]["test"]["mods"]
                    self.assertNotIn("mmai", mounted)
                    self.assertIn("mmai-v13-test", mounted)
                    if fail:
                        raise subprocess.CalledProcessError(1, command)
                    names = next(arg.split("=", 1)[1] for arg in command if arg.startswith("--gtest_filter="))
                    report = next(arg.split("xml:", 1)[1] for arg in command if arg.startswith("--gtest_output="))
                    self.write_report(report, [name.split(".", 1)[1] for name in names.split(":")])

                with patch.dict(os.environ, {"XDG_DATA_HOME": str(root / "data"),
                                             "XDG_CONFIG_HOME": str(root / "config")}), \
                        patch.object(subprocess, "run", side_effect=run):
                    if fail:
                        with self.assertRaises(subprocess.CalledProcessError):
                            check.run_battles(root / "vcmitest", models)
                    else:
                        check.run_battles(root / "vcmitest", models)
                self.assertEqual(preset.read_bytes(), original)
                self.assertEqual(len(calls), 1 if fail else 4)


if __name__ == "__main__":
    unittest.main()
