#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "AI/BattleAI/StackWithBonuses.h"
#include "lib/battle/BattleInfo.h"
#include "lib/battle/BattleLayout.h"
class OwnedUnsupportedFacadeTest : public BattleTestFixture
{
public:
 void SetUp() override { BattleTestFixture::SetUp();startGame();startBattle(); }
};
TEST_F(OwnedUnsupportedFacadeTest, EveryUnsupportedInheritedPointerQueryThrows)
{
 auto f=HypotheticBattle::captureOwnedFixture(*battle());
 EXPECT_EQ(f->getBattle(),f.get());
 EXPECT_THROW(f->getStacksIf([](const CStack *){return true;}),std::logic_error);
 EXPECT_THROW(f->getSideArmy(BattleSide::ATTACKER),std::logic_error);
 EXPECT_THROW(f->getSideHero(BattleSide::DEFENDER),std::logic_error);
 EXPECT_THROW(f->getDefendedTown(),std::logic_error);
 ASSERT_NE(f->getBonusBearer(),nullptr);
 EXPECT_NE(f->getBonusBearer(),battle()->getBonusBearer());
 EXPECT_EQ(f->getBonusBearer()->getTreeVersion(),battle()->getBonusBearer()->getTreeVersion());
 EXPECT_THROW(f->getAllObstacles(),std::logic_error);
}
TEST_F(OwnedUnsupportedFacadeTest, UnsupportedWorldScalarsAndWallsThrowBeforeNullSubject)
{
 auto f=HypotheticBattle::captureOwnedFixture(*battle());
 EXPECT_THROW(f->getPlayerID(),std::logic_error);
 EXPECT_THROW(f->getBattlefieldType(),std::logic_error);
 EXPECT_THROW(f->getTerrainType(),std::logic_error);
 EXPECT_EQ(f->getTacticDist(),battle()->getTacticDist());
 EXPECT_EQ(f->getTacticsSide(),battle()->getTacticsSide());
 EXPECT_EQ(f->getRound(),battle()->getRound());
 EXPECT_THROW(f->getWallState(static_cast<EWallPart>(0)),std::logic_error);
 EXPECT_THROW(f->getGateState(),std::logic_error);
 EXPECT_EQ(f->getCastSpells(BattleSide::ATTACKER),battle()->getCastSpells(BattleSide::ATTACKER));
 EXPECT_EQ(f->getEnchanterCounter(BattleSide::DEFENDER),battle()->getEnchanterCounter(BattleSide::DEFENDER));
 EXPECT_EQ(f->getBattleID(),battle()->getBattleID());
 EXPECT_EQ(f->getSidePlayer(BattleSide::ATTACKER),battle()->getSidePlayer(BattleSide::ATTACKER));
 EXPECT_EQ(f->getSidePlayer(BattleSide::DEFENDER),battle()->getSidePlayer(BattleSide::DEFENDER));
}
TEST_F(OwnedUnsupportedFacadeTest, ScriptAndLocalFacadeAccessThrowsBeforeNullEnvironment)
{
 auto f=HypotheticBattle::captureOwnedFixture(*battle());
 EXPECT_THROW(f->getScriptContextPool(),std::logic_error);
 EXPECT_THROW(f->getServerCallback(),std::logic_error);
 EXPECT_EQ(f->getUsedSpells(BattleSide::ATTACKER),battle()->getUsedSpells(BattleSide::ATTACKER));
 EXPECT_THROW(f->getLocation(),std::logic_error);
 EXPECT_THROW(f->getLayout(),std::logic_error);
 const auto originalRound = f->getRound();
 auto sibling = f->forkOwnedFixture();
 f->nextRound();
 battle()->nextRound();
 EXPECT_EQ(f->getRound(), originalRound + 1);
 EXPECT_EQ(f->getRound(), battle()->getRound());
 EXPECT_EQ(sibling->getRound(), originalRound);
 for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
 {
  EXPECT_EQ(f->getCastSpells(side), battle()->getCastSpells(side));
  EXPECT_EQ(f->getEnchanterCounter(side), battle()->getEnchanterCounter(side));
  EXPECT_EQ(f->getUsedSpells(side), battle()->getUsedSpells(side));
 }
 for(const auto * unit : battle()->getUnitsIf([](const battle::Unit *) { return true; }))
  EXPECT_EQ(f->getForUpdate(unit->unitId())->save(), unit->acquireState()->save());
 const auto units = battle()->getUnitsIf([](const battle::Unit * unit) { return unit->alive(); });
 ASSERT_FALSE(units.empty());
 const auto id = units.front()->unitId();
 const auto siblingActive = sibling->getActiveStackID();
 const auto siblingState = sibling->getForUpdate(id)->save();
 f->nextTurn(id, BattleUnitTurnReason::MORALE);
 battle()->nextTurn(id, BattleUnitTurnReason::MORALE);
 EXPECT_EQ(f->getActiveStackID(), battle()->getActiveStackID());
 EXPECT_EQ(f->getForUpdate(id)->save(), units.front()->acquireState()->save());
 EXPECT_EQ(sibling->getActiveStackID(), siblingActive);
 EXPECT_EQ(sibling->getForUpdate(id)->save(), siblingState);
}
