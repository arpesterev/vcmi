"""Synthetic checker tests, NOT gameplay or upstream-conformance evidence."""
import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from check_ai_backend_acceptance import PROVIDERS, SCENARIOS, validate


class AcceptanceCheckerTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.report = {"schema": 1, "providers": []}
        for provider in sorted(PROVIDERS):
            entry = {"id": provider, "selected_in_vcmi": True,
                     "source_revision": "a" * 40, "adapter_revision": "b" * 40, "cases": []}
            for scenario in sorted(SCENARIOS):
                rows = [
                    {"seq": 1, "event": "backend_loaded", "provider": provider,
                     "source_revision": "a" * 40, "initial_state_sha256": "c" * 64,
                     "model_sha256": "d" * 64, "ruleset": "synthetic-unit-test", "seed": 1},
                    {"seq": 2, "event": "decision", "provider": provider,
                     "source_revision": "a" * 40, "state_sha256": "e" * 64,
                     "action": "attack", "legal_actions": ["attack", "defend"]},
                    {"seq": 3, "event": "damage_applied", "amount": 5},
                    {"seq": 4, "event": "battle_finished", "winner": "attacker"},
                ]
                case = {"scenario": scenario, "initial_state_sha256": "c" * 64,
                        "ruleset": "synthetic-unit-test", "seed": 1}
                for side in ("upstream", "vcmi"):
                    name = f"{provider}-{scenario}-{side}.jsonl"
                    case[side] = self.save(name, rows)
                entry["cases"].append(case)
            self.report["providers"].append(entry)

    def save(self, name, rows):
        raw = ("\n".join(json.dumps(row, sort_keys=True) for row in rows) + "\n").encode()
        (self.root / name).write_bytes(raw)
        return {"path": name, "sha256": hashlib.sha256(raw).hexdigest()}

    def mutate(self, change, provider=None):
        entry = next(p for p in self.report["providers"] if provider is None or p["id"] == provider)
        case = entry["cases"][0]
        name = case["vcmi"]["path"]
        rows = [json.loads(line) for line in (self.root / name).read_text().splitlines()]
        change(rows)
        case["vcmi"] = self.save(name, rows)

    def test_synthetic_valid_evidence(self):
        self.assertEqual(validate(self.report, self.root), [])

    def test_missing_provider(self):
        self.report["providers"].pop()
        self.assertTrue(validate(self.report, self.root))

    def test_renamed_same_backend(self):
        self.mutate(lambda rows: rows[1].update(provider="BattleAI"))
        self.assertTrue(validate(self.report, self.root))

    def test_not_selectable(self):
        self.report["providers"][0]["selected_in_vcmi"] = False
        self.assertTrue(validate(self.report, self.root))

    def test_different_decisions(self):
        self.mutate(lambda rows: rows[1].update(action="defend"))
        self.assertTrue(validate(self.report, self.root))

    def test_fallback(self):
        self.mutate(lambda rows: rows[2].update(event="fallback"))
        self.assertTrue(validate(self.report, self.root))

    def test_no_combat(self):
        self.mutate(lambda rows: rows[2].update(amount=0))
        self.assertTrue(validate(self.report, self.root))

    def test_missing_model(self):
        self.mutate(lambda rows: rows[0].pop("model_sha256"), "MMAI")
        self.assertTrue(validate(self.report, self.root))

    def test_model_mismatch(self):
        self.mutate(lambda rows: rows[0].update(model_sha256="f" * 64), "MMAI")
        self.assertTrue(validate(self.report, self.root))

    def test_changed_trace_hash(self):
        self.report["providers"][0]["cases"][0]["vcmi"]["sha256"] = "0" * 64
        self.assertTrue(validate(self.report, self.root))

    def test_incomplete_trace(self):
        self.mutate(lambda rows: rows.pop())
        self.assertTrue(validate(self.report, self.root))

    def test_wrong_seed(self):
        self.mutate(lambda rows: rows[0].update(seed=2))
        self.assertTrue(validate(self.report, self.root))

    def test_duplicate_provider(self):
        self.report["providers"][0] = copy.deepcopy(self.report["providers"][1])
        self.assertTrue(validate(self.report, self.root))

    def test_malformed_report(self):
        self.assertTrue(validate([], self.root))
        self.report["providers"][0] = {"id": []}
        self.assertTrue(validate(self.report, self.root))

    def test_self_comparison_is_not_independent_evidence(self):
        case = self.report["providers"][0]["cases"][0]
        case["vcmi"] = case["upstream"]
        self.assertTrue(validate(self.report, self.root))

    def test_path_escape(self):
        self.report["providers"][0]["cases"][0]["vcmi"]["path"] = "../outside.jsonl"
        self.assertTrue(validate(self.report, self.root))


if __name__ == "__main__":
    unittest.main()
