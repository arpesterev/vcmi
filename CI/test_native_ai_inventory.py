#!/usr/bin/env python3
"""Portable inventory contracts; synthetic inputs are not native execution evidence."""

import unittest
import tempfile
from test_macos_native_ai import write_xml
import re
from pathlib import Path

import check_native_ai_inventory as checker
import run_macos_native_ai as runner


def temporary_bonus_ranges(source):
    """Structural convention lint only, not a C++ parser or sanitizer proof."""
    # Ignore comments and ordinary literals while preserving diagnostic lines.
    source = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda match: "\n" * match.group().count("\n"), source)
    pattern = r'\bfor\s*\(\s*(?:const\s+)?auto\b[^;{}]*?:\s*\*\s*[^;{}]*?\bgetAllBonuses\s*\('
    return [source.count("\n", 0, match.start()) + 1
            for match in re.finditer(pattern, source)]


class BonusRangeOwnershipConvention(unittest.TestCase):
    def test_direct_temporary_ranges_are_rejected(self):
        for sample in (
            "for(const auto & b : *unit.getAllBonuses(selector)) use(b);",
            "for (auto & b : *unit->getAllBonuses(\nSelector::all)) { use(b); }",
        ):
            with self.subTest(sample=sample):
                self.assertEqual(temporary_bonus_ranges(sample), [1])

    def test_retained_owners_and_full_expression_uses_are_allowed(self):
        for sample in (
            "const auto bonuses = unit.getAllBonuses(s); for(auto & b : *bonuses) use(b);",
            "for(const auto owner = unit.getAllBonuses(s); const auto & b : *owner) use(b);",
            "bool empty = unit.getAllBonuses(s)->empty();",
            "// for(auto & b : *unit.getAllBonuses(s))\n",
            'const char * text = "for(auto & b : *unit.getAllBonuses(s))";',
        ):
            with self.subTest(sample=sample):
                self.assertEqual(temporary_bonus_ranges(sample), [])

    def test_integration_ranges_retain_bonus_list_owners(self):
        root = Path(__file__).resolve().parents[1]
        files = sorted((root / "AI/Integration/Ports").rglob("*.h"))
        files += sorted((root / "test/aiIntegration").glob("Native*.inc"))
        files += sorted((root / "test/aiIntegration").glob("Native*.cpp"))
        failures = [f"{path.relative_to(root)}:{line}" for path in files
                    for line in temporary_bonus_ranges(path.read_text())]
        self.assertEqual(failures, [], "Retain the returned shared_ptr throughout iteration")


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
        self.identities = {f"AIIntegrationNativeBattle.case{x}" for x in range(298)} | runner.GATED_NATIVE | runner.SPEED_NATIVE | runner.PURSUIT_NATIVE | runner.APPROACH_NATIVE | runner.SHIELD_NATIVE | runner.PEER_DEFENSE_NATIVE | runner.CURSE_NATIVE | runner.DISABLE_REMOVAL_NATIVE | runner.BLIND_WINNER_NATIVE | runner.SPEED_WINNER_NATIVE | runner.ATTACK_SKILL_WINNER_NATIVE | runner.WIDE_CAUTIOUS_NATIVE | runner.TIED_LANDING_NATIVE
        self.identities |= {f"HoMM3ForecastQueueNative.case{x}" for x in range(8)}
        self.identities |= {f"HoMM3ForecastLifecycleNative.case{x}" for x in range(10)}
        self.workflow = "run: runner --expected-native-count 370 --expected-forecast-count 18 --expected-lifecycle-count 10"

    def test_compiled_counts_match_reviewed_workflow_arguments(self):
        report = checker.check_inventory(listing(self.identities), self.workflow)
        self.assertEqual(report["compiled_cases"], 408)
        self.assertEqual(report["selected_non_neural_cases"], 388)
        self.assertEqual(report["expected_counts"], {"native": 370, "forecast": 18, "lifecycle": 10})
        self.assertEqual(report["evidence"], "compiled-inventory-only")

    def test_new_suites_and_renamed_landing_are_required(self):
        for name in runner.WIDE_CAUTIOUS_NATIVE | runner.TIED_LANDING_NATIVE:
            with self.subTest(name=name), self.assertRaises(ValueError):
                checker.check_inventory(listing(self.identities - {name}), self.workflow)
        for suite in ("FHeroes2WideCautious", "HoMM3TiedLanding"):
            with self.subTest(suite=suite), self.assertRaises(ValueError):
                checker.check_inventory(listing(self.identities | {suite + ".unreviewed"}), self.workflow)
        renamed = "AIIntegrationNativeForecastApproach.tiedOffRowLandingPreservesBoundedForecast"
        stale = (self.identities - {renamed}) | {"AIIntegrationNativeForecastApproach.tiedOffRowLandingRemainsUnavailable"}
        with self.assertRaisesRegex(ValueError, "approach inventory"):
            checker.check_inventory(listing(stale), self.workflow)
        self.assertTrue(all(runner.matches_filter(name, runner.NATIVE_FILTER)
                            for name in runner.WIDE_CAUTIOUS_NATIVE | runner.TIED_LANDING_NATIVE))

    def test_native_xml_must_match_non_neural_inventory(self):
        with tempfile.TemporaryDirectory() as directory:
            xml = Path(directory) / "native.xml"
            selected = self.identities - runner.GATED_NATIVE
            write_xml(xml, selected)
            result = checker.check_inventory(listing(self.identities), self.workflow, xml)
            self.assertEqual(result["executed_cases"], len(selected))
            self.assertEqual(result["evidence"], "compiled-inventory-and-native-xml")
            for changed in (selected - {next(iter(runner.WIDE_CAUTIOUS_NATIVE))},
                            selected | {next(iter(runner.GATED_NATIVE))},
                            (selected - {next(iter(runner.TIED_LANDING_NATIVE))}) | {"HoMM3TiedLanding.unreviewed"}):
                write_xml(xml, changed)
                with self.subTest(changed=changed), self.assertRaises(ValueError):
                    checker.check_inventory(listing(self.identities), self.workflow, xml)

    def test_stale_native_or_forecast_count_fails(self):
        for flag, old, new in (("native", "370", "312"), ("forecast", "18", "8"), ("lifecycle", "10", "9")):
            with self.subTest(flag=flag), self.assertRaises(ValueError):
                checker.check_inventory(listing(self.identities), self.workflow.replace(f"--expected-{flag}-count {old}", f"--expected-{flag}-count {new}"))

    def test_missing_lifecycle_cannot_hide_behind_extra_queue_tests(self):
        changed = {name for name in self.identities if not name.startswith("HoMM3ForecastLifecycleNative.")}
        changed |= {f"HoMM3ForecastQueueNative.extra{x}" for x in range(10)}
        with self.assertRaisesRegex(ValueError, "candidate counts"):
            checker.check_inventory(listing(changed), self.workflow)

    def test_missing_unknown_or_duplicate_workflow_values_fail(self):
        for workflow in ("", self.workflow + " --expected-native-count 370",
                         self.workflow.replace("370", "$EXPECTED_NATIVE"),
                         self.workflow.replace("--expected-lifecycle-count 10", "--expected-lifecycle-count 19")):
            with self.subTest(workflow=workflow), self.assertRaises(ValueError):
                checker.workflow_counts(workflow)

    def test_unreviewed_namespace_and_gates_fail(self):
        for changed in (self.identities | {"HoMM3ForecastLifecycleNativeExtra.case0"},
                        self.identities - {next(iter(runner.GATED_NATIVE))},
                        self.identities - {next(iter(runner.SHIELD_NATIVE))},
                        self.identities - {next(iter(runner.CURSE_NATIVE))},
                        self.identities - {next(iter(runner.DISABLE_REMOVAL_NATIVE))},
                        self.identities - {next(iter(runner.BLIND_WINNER_NATIVE))},
                        self.identities - {next(iter(runner.SPEED_WINNER_NATIVE))},
                        self.identities - {next(iter(runner.PEER_DEFENSE_NATIVE))},
                        self.identities | {"AIIntegrationNativePeerDefense.unreviewed"}):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                checker.check_inventory(listing(changed), self.workflow)


if __name__ == "__main__":
    unittest.main()
