#!/usr/bin/env python3
"""Tests of the match oracle; these do not substitute for the native match."""
import copy
import unittest

from run_ai_match import MatchEvidence


class MatchOracleTest(unittest.TestCase):
    def setUp(self):
        self.evidence = MatchEvidence()
        self.objects = [
            dict(id=1, kind='town', owner=0, x=4, y=8, z=0, buildings=2, stock=10),
            dict(id=2, kind='town', owner=1, x=28, y=26, z=0, buildings=2, stock=10),
            dict(id=3, kind='resource', owner=255, x=9, y=9, z=0),
            dict(id=4, kind='monster', owner=255, x=17, y=18, z=0),
            dict(id=5, kind='artifact', owner=255, x=18, y=18, z=0),
        ]
        self.send('NewTurn')

    def send(self, name, **fields):
        self.evidence.accept(dict(seq=self.evidence.last_seq + 1, event=name,
                                  day=2, objects=copy.deepcopy(self.objects), **fields))

    def finish_valid_match(self, combat=True, attacker_won=True):
        self.send('PlayerEndsTurn', player=0)
        self.send('PlayerEndsTurn', player=1)
        self.objects = [x for x in self.objects if x['id'] != 3]
        self.send('RemoveObject', object=3)
        self.objects[0]['buildings'] = 3
        self.send('NewStructures', town=1)
        self.objects[0]['stock'] = 5
        self.send('SetAvailableCreatures', town=1)
        self.send('BattleStart', battle=7, defender=4)
        if combat:
            self.send('BattleAttack', battle=7, damage=100)
        self.send('BattleResultAccepted', battle=7, attacker_won=attacker_won)
        self.objects = [x for x in self.objects if x['id'] != 4]
        self.send('RemoveObject', object=4)
        self.objects = [x for x in self.objects if x['id'] != 5]
        self.send('RemoveObject', object=5)
        self.objects[1]['owner'] = 0
        self.send('PlayerEndsGame', player=1, victory=False)
        self.send('PlayerEndsGame', player=0, victory=True)

    def test_real_evidence_passes(self):
        self.finish_valid_match()
        self.assertEqual(self.evidence.failures(0, None), [])

    def test_empty_or_crashed_client_is_not_success(self):
        self.assertTrue(MatchEvidence().failures(0, None))
        self.finish_valid_match()
        self.assertIn('client did not exit normally', self.evidence.failures(-11, None))

    def test_timeout_is_not_success_even_after_victory(self):
        self.finish_valid_match()
        self.assertIn('client did not exit normally', self.evidence.failures(0, 'wall-timeout'))

    def test_fake_battle_result_does_not_prove_guard_combat(self):
        self.finish_valid_match(combat=False)
        self.assertFalse(self.evidence.guarded_artifacts)
        self.assertIn('no real combat damage', self.evidence.failures(0, None))

    def test_artifact_before_combat_is_not_guard_clearance(self):
        self.objects = [x for x in self.objects if x['id'] != 5]
        self.send('RemoveObject', object=5)
        self.send('BattleStart', battle=7, defender=4)
        self.send('BattleAttack', battle=7, damage=100)
        self.send('BattleResultAccepted', battle=7, attacker_won=True)
        self.assertFalse(self.evidence.guarded_artifacts)

    def test_losing_to_guard_does_not_prove_guard_clearance(self):
        self.finish_valid_match(attacker_won=False)
        self.assertFalse(self.evidence.guarded_artifacts)
        self.assertIn('guarded artifact not collected after a real guard battle',
                      self.evidence.failures(0, None))

    def test_stale_or_duplicate_records_are_rejected(self):
        with self.assertRaises(ValueError):
            self.evidence.accept(self.evidence.last)

    def test_idle_players_fail_behavior_requirements(self):
        for _ in range(4):
            self.send('PlayerEndsTurn', player=0)
            self.send('PlayerEndsTurn', player=1)
        self.assertIn('no resource pile collected', self.evidence.failures(-15, 'day-limit'))
        self.assertIn('no town construction', self.evidence.failures(-15, 'day-limit'))
        self.assertFalse(self.evidence.winners)


if __name__ == '__main__':
    unittest.main()
