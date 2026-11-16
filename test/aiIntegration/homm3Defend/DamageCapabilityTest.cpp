// GPL-2.0-or-later. Capability is mutation-only, not source damage/privacy parity.
#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/scripting/ScriptService.h"
#include "../../../lib/combatScripts/IDamageCalculatorScript.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../AI/Integration/Ports/HoMM3DefendProvenance.h"

namespace
{
class UnknownNativeCalculator final : public IDamageCalculatorScript
{
public:
 DamageEstimation calculate(const CBattleInfoCallback &,DamageAttackInfo &) const override { return {}; }
};
}
TEST(DamageCalculatorCapability, UnknownNativeImplementationDefaultsFalse)
{
 UnknownNativeCalculator calculator;
 EXPECT_FALSE(calculator.hasReadOnlyBattleStateAccess());
 AIIntegration::Ports::HoMM3::DefendOrdinaryDomain unknown;
 EXPECT_FALSE(unknown.established());
 // Even this one capability/interval clause does not establish the full domain.
 unknown.noUnmodeledMutationPaths=true;
 EXPECT_FALSE(unknown.established());
}

class NativeDamageCalculatorCapabilityTest : public BattleTestFixture {};
TEST_F(NativeDamageCalculatorCapabilityTest, ActualLoadedLuaCalculatorReportsReadOnlyAndEvaluationDoesNotMutateUnits)
{
 startGame(); startBattle();
 const auto * calculator=LIBRARY->scriptTypes()->getDamageCalculator();
 ASSERT_NE(calculator,nullptr);
 ASSERT_TRUE(calculator->hasReadOnlyBattleStateAccess());
 auto * attacker=addStack(BattleSide::ATTACKER,creatureByName("core:pikeman"),BattleHex(leftHex),20);
 auto * defender=addStack(BattleSide::DEFENDER,creatureByName("core:pikeman"),BattleHex(rightHex),20);
 ASSERT_NE(attacker,nullptr); ASSERT_NE(defender,nullptr);
 const auto attackerHealth=attacker->getAvailableHealth(), defenderHealth=defender->getAvailableHealth();
 const auto defense=defender->getDefense(false);
 const auto position=defender->getPosition();
 const auto before=defender->getAllBonuses(Selector::all);
 const auto bonusCount=before->size();
 const BattleAttackInfo info(attacker,defender,0,false);
 const auto first=battle()->calculateDmgRange(info);
 const auto second=battle()->calculateDmgRange(info);
 EXPECT_EQ(first.damage.min,second.damage.min);
 EXPECT_EQ(first.damage.max,second.damage.max);
 EXPECT_EQ(attacker->getAvailableHealth(),attackerHealth);
 EXPECT_EQ(defender->getAvailableHealth(),defenderHealth);
 EXPECT_EQ(defender->getDefense(false),defense);
 EXPECT_EQ(defender->getPosition(),position);
 const auto after=defender->getAllBonuses(Selector::all);
 EXPECT_EQ(after->size(),bonusCount);
 // This does not assert that Lua's query visibility is player-scoped.
}
