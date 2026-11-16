"""Portable selection contracts; not ARM64 execution evidence."""
import json
from pathlib import Path
import unittest
import run_macos_native_ai as runner

class A53Selection(unittest.TestCase):
    def setUp(self):
        self.manifest = json.loads(Path(__file__).with_name('macos_ai_a53_inventory.json').read_text())
        self.identities = set(self.manifest['selected_tests'])

    def test_exact_identities(self):
        runner.verify_reviewed_inventory(self.identities, self.manifest)
        name = next(iter(self.identities))
        for changed in (self.identities - {name}, self.identities | {'Unexpected.test'}, self.identities - {name} | {'Unexpected.test'}):
            with self.assertRaises(ValueError):
                runner.verify_reviewed_inventory(changed, self.manifest)

    def test_disjoint_complete_partitions(self):
        groups = {}
        for group in ('core', 'slow'):
            groups[group] = set()
            for _, selected, count, _ in runner.selected_suites(471, 18, group, 209):
                cases = {n for n in self.identities if runner.matches_filter(n, selected)}
                self.assertEqual(len(cases), count)
                self.assertFalse(groups[group] & cases)
                groups[group] |= cases
        self.assertFalse(groups['core'] & groups['slow'])
        self.assertEqual(groups['core'] | groups['slow'], self.identities)
        self.assertEqual(len(groups['core']), 746)
        self.assertEqual(len(groups['slow']), 145)

    def test_current_ranged_melee_coverage(self):
        for prefix in ('AIIntegrationFHeroesMeleeSource.', 'AIIntegrationFHeroesRanged.'):
            self.assertEqual(sum(n.startswith(prefix) for n in self.identities), 7)

    def test_current_cautious_advance_coverage(self):
        self.assertEqual(sum(n.startswith('AIIntegrationFHeroesCautiousSource.') for n in self.identities), 5)

    def test_current_mirth_cancellation_coverage(self):
        self.assertEqual(sum(n.startswith('AIIntegrationNativeMirth.') for n in self.identities), 22)
        self.assertNotIn('AIIntegrationNativeMirth.negativeMoraleRemainsOutsideBound', self.identities)
        self.assertIn('AIIntegrationNativeMirth.dispelsMirthOverNegativeMoraleAndCompletesBattle', self.identities)
