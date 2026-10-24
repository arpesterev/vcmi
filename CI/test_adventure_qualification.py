import unittest
from adventure_qualification import validate_plan

class ReferenceBudgetTest(unittest.TestCase):
    def test_new_server_and_reference_objects_allowed(self):
        plan = '\n'.join(['[1/2] Building CXX object server/CMakeFiles/vcmiservercommon_battles.dir/battles/BattleProcessor.cpp.o',
                          '[2/2] Building CXX object test/CMakeFiles/vcmi_forced_battle_reference.dir/battle/ForcedBattleReference.cpp.o'])
        self.assertEqual(len(validate_plan(plan, 48)), 2)
    def test_core_or_client_rebuild_is_rejected(self):
        for path in ('lib/CMakeFiles/vcmi_battle.dir/battle/BattleInfo.cpp.o', 'client/CMakeFiles/vcmiclient.dir/main.cpp.o'):
            with self.assertRaises(ValueError):
                validate_plan('[1/1] Building CXX object ' + path, 48)
    def test_large_reference_plan_rejected(self):
        with self.assertRaises(ValueError):
            validate_plan('\n'.join('[1/49] Building CXX object server/CMakeFiles/vcmiservercommon.dir/a%d.cpp.o' % i for i in range(49)), 48)
    def test_noop_allowed(self):
        self.assertEqual(validate_plan('ninja: no work to do.', 48), [])
