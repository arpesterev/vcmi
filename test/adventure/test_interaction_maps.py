import json
import os
from pathlib import Path
import tempfile
import unittest
import zipfile
from generate_interaction_maps import generate

REPO = Path(os.environ.get('VCMI_SCENARIO_SOURCE', Path(__file__).resolve().parents[2]))

class PublicInteractionMapTest(unittest.TestCase):
    def load_map(self, directory):
        evidence = generate(REPO, directory)
        with zipfile.ZipFile(directory / 'defence_economy_three_turns.vmap') as archive:
            return evidence, json.loads(archive.read('header.json')), json.loads(archive.read('objects.json'))

    def test_generation_is_deterministic_and_marked_unexecuted(self):
        with tempfile.TemporaryDirectory() as root:
            a, b = Path(root) / 'a', Path(root) / 'b'
            first, _, _ = self.load_map(a)
            second, _, _ = self.load_map(b)
            self.assertEqual(first, second)
            self.assertEqual(first['status'], 'draft_unexecuted')
            self.assertEqual((a / 'defence_economy_three_turns.vmap').read_bytes(), (b / 'defence_economy_three_turns.vmap').read_bytes())

    def test_declared_retreat_has_two_visible_endpoints_and_no_army_mutation(self):
        with tempfile.TemporaryDirectory() as root:
            evidence, header, objects = self.load_map(Path(root))
            self.assertEqual(header['mapLevels']['surface']['height'], 48)
            self.assertEqual(evidence['blue_scripted_retreat_anchors'], [[45, 25, 0], [45, 40, 0]])
            self.assertEqual(objects['hero_1']['options']['army'], [{'type': 'core:pikeman', 'amount': 30}])
            self.assertEqual(objects['retreat_observer']['options'], {'owner': 'red'})
            self.assertEqual(objects['retreat_observer_far']['options'], {'owner': 'red'})
            self.assertEqual(header['events'][0]['resources'], {'gold': -8800})

    def test_controls_have_explicit_positions_and_deadline_not_tuned_to_result(self):
        with tempfile.TemporaryDirectory() as root:
            _, header, _ = self.load_map(Path(root))
            victory = header['triggeredEvents']['objectiveVictory']['condition']
            self.assertEqual(victory[1], ['daysPassed', {'value': 3}])
            self.assertEqual([part[1]['position'] for part in victory[2:]], [[1, 3, 0], [43, 7, 0], [39, 9, 0]])
            self.assertEqual(header['triggeredEvents']['evaluationDeadline']['condition'], ['daysPassed', {'value': 4}])

if __name__ == '__main__':
    unittest.main()
