#!/usr/bin/env python3
"""Portable inventory contracts; synthetic inputs are not native execution evidence."""

import unittest

import check_native_ai_inventory as checker
import run_macos_native_ai as runner


def listing(identities):
    result = []
    suites = {}
    for identity in sorted(identities):
        suite, name = identity.split(".")
        suites.setdefault(suite, []).append(name)
    for suite, names in suites.items():
        result.append(suite + ".")
        result.extend("  " + name for name in names)
    return "\n".join(result) + "\n"


class CandidateInventoryContracts(unittest.TestCase):
    def setUp(self):
        self.identities = {f"AIIntegrationNativeBattle.case{x}" for x in range(306)} | runner.GATED_NATIVE
        self.identities |= {f"HoMM3ForecastQueueNative.case{x}" for x in range(8)}
        self.identities |= {f"HoMM3ForecastLifecycleNative.case{x}" for x in range(10)}
        self.workflow = "run: runner --expected-native-count 306 --expected-forecast-count 18 --expected-lifecycle-count 10"

    def test_compiled_counts_match_reviewed_workflow_arguments(self):
        report = checker.check_inventory(listing(self.identities), self.workflow)
        self.assertEqual(report["compiled_cases"], 344)
        self.assertEqual(report["selected_non_neural_cases"], 324)
        self.assertEqual(report["expected_counts"], {"native": 306, "forecast": 18, "lifecycle": 10})
        self.assertEqual(report["evidence"], "compiled-inventory-only")

    def test_stale_native_or_forecast_count_fails(self):
        for flag, old, new in (("native", "306", "312"), ("forecast", "18", "8"), ("lifecycle", "10", "9")):
            with self.subTest(flag=flag), self.assertRaises(ValueError):
                checker.check_inventory(listing(self.identities), self.workflow.replace(f"--expected-{flag}-count {old}", f"--expected-{flag}-count {new}"))

    def test_missing_lifecycle_cannot_hide_behind_extra_queue_tests(self):
        changed = {name for name in self.identities if not name.startswith("HoMM3ForecastLifecycleNative.")}
        changed |= {f"HoMM3ForecastQueueNative.extra{x}" for x in range(10)}
        with self.assertRaisesRegex(ValueError, "candidate counts"):
            checker.check_inventory(listing(changed), self.workflow)

    def test_missing_unknown_or_duplicate_workflow_values_fail(self):
        for workflow in ("", self.workflow + " --expected-native-count 306",
                         self.workflow.replace("306", "$EXPECTED_NATIVE"),
                         self.workflow.replace("--expected-lifecycle-count 10", "--expected-lifecycle-count 19")):
            with self.subTest(workflow=workflow), self.assertRaises(ValueError):
                checker.workflow_counts(workflow)

    def test_unreviewed_namespace_and_gates_fail(self):
        for changed in (self.identities | {"HoMM3ForecastLifecycleNativeExtra.case0"},
                        self.identities - {next(iter(runner.GATED_NATIVE))}):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                checker.check_inventory(listing(changed), self.workflow)


if __name__ == "__main__":
    unittest.main()
