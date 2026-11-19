"""Reviewed integration inventory contracts; not ARM64 execution evidence."""
import json
from pathlib import Path
import unittest

import run_macos_native_ai as runner


class CombinedCandidateSelection(unittest.TestCase):
    def setUp(self):
        root = Path(__file__).parent
        self.old = json.loads((root / 'macos_ai_a53_inventory.json').read_text())
        self.manifest = json.loads((root / 'macos_ai_f398624_inventory.json').read_text())
        self.identities = set(self.manifest['selected_tests'])

    def test_reviewed_delta(self):
        additions = {
            'AIIntegrationNativeForecastApproach.tiedOffRowLandingPreservesBoundedForecast',
            'FHeroes2WideCautious.blockedEnemyAllowsFullWideAdvanceAcceptedByServer',
            'FHeroes2WideCautious.blockedTailPreventsPassageDespiteClearHead',
            'FHeroes2WideCautious.unsupportedThreatRetainsShorterNativeAdvanceAcceptedByServer',
            'HoMM3TiedLanding.DefenderSideMirroredSourceLandingAndServerExchange',
            'HoMM3TiedLanding.SourceGeometryAcrossParitiesAndDirections',
            'HoMM3TiedLanding.SourceOrderedTiesProjectLethalAndSuppressControllerSpell',
            'HoMM3TiedLanding.SurvivingTargetKeepsSpellAndServerCast',
            'HoMM3TiedLanding.ThreatRangeAndSpecialAttackRemainUnavailable',
        }
        old = set(self.old['selected_tests'])
        self.assertEqual(self.identities - old, additions)
        self.assertEqual(old - self.identities, {
            'AIIntegrationNativeForecastApproach.tiedOffRowLandingRemainsUnavailable'})

    def test_exact_inventory_and_historical_negative_control(self):
        runner.verify_reviewed_inventory(self.identities, self.manifest)
        with self.assertRaisesRegex(ValueError, 'exact reviewed candidate inventory'):
            runner.verify_reviewed_inventory(self.identities, self.old)
        with self.assertRaisesRegex(ValueError, 'exact reviewed candidate inventory'):
            runner.verify_reviewed_inventory(set(self.old['selected_tests']), self.manifest)
        for name in runner.WIDE_CAUTIOUS_NATIVE | runner.TIED_LANDING_NATIVE:
            with self.assertRaises(ValueError):
                runner.verify_reviewed_inventory(self.identities - {name}, self.manifest)

    def test_reviewed_filter_and_model_gates(self):
        self.assertEqual(len(self.identities), 899)
        self.assertTrue(all(runner.matches_filter(n, runner.REVIEWED_FILTER)
                            for n in self.identities))
        self.assertTrue(all(not runner.matches_filter(n, runner.REVIEWED_FILTER)
                            for n in runner.GATED_NATIVE))
        self.assertEqual(self.old['excluded_gated_tests'], self.manifest['excluded_gated_tests'])

    def test_complete_disjoint_partitions(self):
        groups = {}
        for group in ('core', 'slow'):
            groups[group] = set()
            for _, selected, count, _ in runner.selected_suites(479, 18, group, 209):
                cases = {n for n in self.identities if runner.matches_filter(n, selected)}
                self.assertEqual(len(cases), count)
                self.assertFalse(groups[group] & cases)
                groups[group] |= cases
        self.assertFalse(groups['core'] & groups['slow'])
        self.assertEqual(groups['core'] | groups['slow'], self.identities)
        self.assertEqual(len(groups['core']), 754)
        self.assertEqual(len(groups['slow']), 145)


if __name__ == '__main__':
    unittest.main()
