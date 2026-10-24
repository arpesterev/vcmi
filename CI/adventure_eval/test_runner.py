import importlib.util
import json
from pathlib import Path
import tempfile
import os
import sys
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('adventure_runner', Path(__file__).with_name('run.py'))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class DraftHarnessTests(unittest.TestCase):
    def test_map_deterministic_and_public(self):
        repo = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as temporary:
            first, second = Path(temporary) / 'a.vmap', Path(temporary) / 'b.vmap'
            runner.generate(repo, first, 'battle_smoke')
            runner.generate(repo, second, 'battle_smoke')
            self.assertEqual(first.read_bytes(), second.read_bytes())
            with zipfile.ZipFile(first) as archive:
                self.assertEqual(set(archive.namelist()), {'header.json', 'objects.json', 'surface_terrain.json'})
                header = json.loads(archive.read('header.json'))
                self.assertEqual(header['triggeredEvents']['evaluationDeadline']['condition'],
                                 ['daysPassed', {'value': 2}])
                objects = json.loads(archive.read('objects.json'))
                self.assertEqual(objects['hero_0']['options']['army'][0]['amount'], 200)
                self.assertEqual(objects['hero_1']['options']['army'][0]['amount'], 1)

    def test_gameplay_drafts_are_deterministic_and_poststate_gated(self):
        repo = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as temporary:
            for name in runner.scenarios.NAMES:
                a, b = Path(temporary) / (name + '.vmap'), Path(temporary) / (name + '-again.vmap')
                evidence = runner.generate(repo, a, name)
                runner.generate(repo, b, name)
                self.assertEqual(a.read_bytes(), b.read_bytes())
                self.assertEqual(evidence['gameplay_manifest']['status'], 'draft_unexecuted')
                with zipfile.ZipFile(a) as archive:
                    header = json.loads(archive.read('header.json'))
                    self.assertEqual(header['mapLevels']['surface']['width'], 30)
                    self.assertEqual(header['triggeredEvents']['objectiveVictory']['condition'][0], 'allOf')
                    self.assertNotIn('standardVictory', header['triggeredEvents'])
                    objects = json.loads(archive.read('objects.json'))
                    def verify_controls(expression):
                        if expression[0] == 'control':
                            data = expression[1]
                            self.assertEqual(expression, runner.scenarios.control_object(objects, data['object'], data['type']))
                        elif expression[0] in ('allOf', 'anyOf', 'noneOf'):
                            for child in expression[1:]:
                                verify_controls(child)
                    verify_controls(header['triggeredEvents']['objectiveVictory']['condition'])
                    self.assertTrue(all(0 <= obj['x'] < 30 and 0 <= obj['y'] < 20 for obj in objects.values()))
                    if name == 'urgent_two_town_defence':
                        self.assertEqual(header['events'][0]['firstOccurrence'], 0)
                        self.assertEqual(header['events'][0]['resources']['gold'], -8800)

    def test_control_object_uses_exact_visitable_position(self):
        objects = {'mine': {'type': 'mine', 'x': 9, 'y': 5, 'l': 0,
                            'template': {'mask': ['VVVV', 'VBAB']}}}
        condition = runner.scenarios.control_object(objects, 'mine', 'mine')
        self.assertEqual(condition, ['control', {'object': 'mine', 'type': 'mine', 'position': [8, 5, 0]}])
        with self.assertRaises(KeyError):
            runner.scenarios.control_object(objects, 'missing', 'mine')
        with self.assertRaises(ValueError):
            runner.scenarios.control_object(objects, 'mine', 'hero')
        for mask in (['VV'], ['AA'], ['A', 'VV']):
            objects['mine']['template']['mask'] = mask
            with self.assertRaises(ValueError):
                runner.scenarios.control_object(objects, 'mine', 'mine')

    def test_asset_gate_rejects_empty_and_placeholder_lods(self):
        spec = importlib.util.spec_from_file_location('assets', Path(__file__).with_name('asset_inventory.py'))
        assets = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(assets)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'H3bitmap.lod').write_bytes(b'fake')
            inventory = assets.inspect([root])
            self.assertFalse(inventory['preflight_passed'])
            self.assertIn('players.pal', inventory['missing'])
            self.assertTrue(inventory['archives'][0]['empty_or_placeholder'])

    def test_controlled_teardown_distinct_from_crash(self):
        text = 'Info about turn 3 has been sent!\nRed player lost. Ending game.'
        self.assertEqual(runner.classify(text, -15, False, 'termination_smoke', True)['status'],
                         'game_complete_controlled_teardown')
        self.assertEqual(runner.classify(text, -15, False, 'termination_smoke', False)['status'], 'incomplete')
        self.assertEqual(runner.classify(text, -11, False, 'termination_smoke', True)['status'], 'incomplete')
        self.assertEqual(runner.classify(text, -15, True, 'termination_smoke', True)['status'], 'incomplete')

    def test_supervisor_observes_then_stops(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            code, timeout, controlled, elapsed = runner.supervise(
                [sys.executable, '-c', "import time; print('Info about turn 3 has been sent!\\nRed player lost. Ending game.', flush=True); time.sleep(20)"],
                os.environ.copy(), root / 'console.log', root, 2, 'termination_smoke')
            self.assertFalse(timeout)
            self.assertTrue(controlled)
            self.assertEqual(code, -15)
            self.assertLess(elapsed, 2)

    def test_supervisor_timeout_is_incomplete(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            code, timeout, controlled, _ = runner.supervise(
                [sys.executable, '-c', 'import time; time.sleep(20)'],
                os.environ.copy(), root / 'console.log', root, 0.1, 'termination_smoke')
            self.assertTrue(timeout)
            self.assertFalse(controlled)
            self.assertEqual(runner.classify('', code, timeout, 'termination_smoke', controlled)['status'], 'incomplete')

    def test_timeout_or_crash_never_passes(self):
        line = 'Info about turn 3 has been sent!\nRed player lost. Ending game.'
        for code, timeout in [(0, True), (1, False), (-9, False)]:
            self.assertEqual(runner.classify(line, code, timeout, 'termination_smoke')['status'], 'incomplete')

    def test_battle_requires_observed_win_and_movement(self):
        terminal = 'Red player won. Ending game.'
        self.assertEqual(runner.classify(terminal, 0, False, 'battle_smoke')['status'], 'incomplete')
        text = terminal + '\nPlayer 0 (red): I won the Starting battle of Christian attacking target!\nHero Christian moved from (1 1 0) to (2 1 0). Returning 1.'
        self.assertEqual(runner.classify(text, 0, False, 'battle_smoke')['status'], 'game_complete_graceful_exit')
        self.assertEqual(runner.classify(text.replace('I won', 'I lost'), 0, False, 'battle_smoke')['status'], 'incomplete')

    def test_unexpected_victory_does_not_prove_deadline(self):
        self.assertEqual(runner.classify('Red player won. Ending game.', 0, False,
                                        'termination_smoke')['status'], 'incomplete')

    def test_deadline_loss_does_not_imply_battle(self):
        text = 'Info about turn 3 has been sent!\nRed player lost. Ending game.'
        self.assertEqual(runner.classify(text, 0, False, 'termination_smoke')['status'], 'game_complete_graceful_exit')
        self.assertEqual(runner.classify(text, 0, False, 'battle_smoke')['status'], 'incomplete')


if __name__ == '__main__':
    unittest.main()
