/* Ordinary native preview integration contracts. GPL-2.0-or-later. */
#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "mock/mock_Environment.h"
#include "AI/BattleAI/StackWithBonuses.h"
#include "AI/Integration/Ports/FHeroes2ArcherClassification.h"
#include "lib/battle/CPlayerBattleCallback.h"
#include "lib/json/JsonNode.h"

class OrdinaryAmmoRoutingTest : public BattleTestFixture
{
public:
 testing::StrictMock<EnvironmentMock> environment;
 void SetUp() override { BattleTestFixture::SetUp(); startGame(); startBattle(); }
 CStack * archer(int x = 5)
 { return addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(x, 5), 10); }
 CStack * cart()
 { return addStack(BattleSide::ATTACKER, CreatureID::AMMO_CART, BattleHex(7, 5), 1); }
 std::shared_ptr<HypotheticBattle> project()
 {
  return std::make_shared<HypotheticBattle>(&environment,
   std::make_shared<CPlayerBattleCallback>(battle(), battle()->sideToPlayer(BattleSide::ATTACKER)));
 }
};

TEST_F(OrdinaryAmmoRoutingTest, ReadLookupIsLocalWithoutRegisteringWritableAlias)
{
 auto * source = archer(); cart(); auto frame = project();
 const auto * read = frame->battleGetUnitByID(source->unitId());
 ASSERT_NE(read, nullptr);
 EXPECT_NE(read, static_cast<const battle::Unit *>(source));
 EXPECT_EQ(read->unitType(), source->unitType());
 EXPECT_TRUE(frame->stackStates.empty());
 auto write = frame->getForUpdate(source->unitId());
 EXPECT_EQ(read, write.get());
 ASSERT_EQ(frame->stackStates.size(), 1);
 EXPECT_EQ(frame->stackStates.at(source->unitId()), write);
 EXPECT_FALSE(write->shots.isLimited());
}

TEST_F(OrdinaryAmmoRoutingTest, LocalAliasesAndReacquiredStatesOutliveFrameWithoutOwningSource)
{
 auto * source = archer(); auto * supply = cart(); auto frame = project();
 auto alias = frame->getForUpdate(source->unitId());
 auto cartAlias = frame->getForUpdate(supply->unitId());
 auto acquired = alias->acquireState(); auto reacquired = acquired->acquireState();
 frame.reset(); alias.reset(); acquired.reset();
 EXPECT_EQ(reacquired->unitType(), source->unitType());
 EXPECT_FALSE(reacquired->shots.isLimited());
 cartAlias->health.reset();
 EXPECT_TRUE(reacquired->shots.isLimited());
 EXPECT_TRUE(supply->alive());
}

TEST_F(OrdinaryAmmoRoutingTest, CallbackWrappingOrdinaryParentForksLocalState)
{
 auto * source = archer(); auto * supply = cart(); auto parent = project();
 auto unit = parent->getForUpdate(source->unitId());
 int64_t damage = unit->getMaxHealth(); unit->damage(damage);
 auto wrapper = std::make_shared<CPlayerBattleCallback>(parent.get(), source->unitOwner());
 auto child = std::make_shared<HypotheticBattle>(&environment, wrapper);
 auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
 EXPECT_EQ(child->battleGetUnitByID(source->unitId())->getCount(), unit->getCount());
 EXPECT_EQ(child->battleGetUnitByID(source->unitId())->unitType(), source->unitType());
 child->removeUnit(supply->unitId());
 EXPECT_TRUE(child->battleGetUnitByID(source->unitId())->acquireState()->shots.isLimited());
 EXPECT_FALSE(unit->shots.isLimited());
 EXPECT_FALSE(sibling->battleGetUnitByID(source->unitId())->acquireState()->shots.isLimited());
 EXPECT_TRUE(supply->alive());
}

TEST_F(OrdinaryAmmoRoutingTest, ChildForkPreservesAddedUnitsAndAllocator)
{
 archer(); auto parent = project();
 battle::UnitInfo added;
 added.id = parent->nextUnitId(); added.count = 4; added.type = creatureByName("core:woodElf");
 added.side = BattleSide::ATTACKER; added.position = BattleHex(11, 5); added.summoned = true;
 JsonNode data; added.save(data); parent->addUnit(added.id, data);
 const auto consumed = parent->nextUnitId();
 auto child = std::make_shared<HypotheticBattle>(&environment, parent);
 EXPECT_EQ(child->nextUnitId(), consumed + 1);
 auto childUnit = child->getForUpdate(added.id);
 ASSERT_NE(childUnit, nullptr);
 EXPECT_EQ(childUnit->getCount(), added.count);
 EXPECT_EQ(childUnit->unitType(), parent->getForUpdate(added.id)->unitType());
 childUnit->health.reset();
 EXPECT_TRUE(parent->getForUpdate(added.id)->alive());
 EXPECT_EQ(parent->nextUnitId(), consumed + 1);
}

TEST_F(OrdinaryAmmoRoutingTest, CloneProducerPayloadUsesLocalNativeLifecycle)
{
 auto * original = archer(); auto * clone = archer(9); auto frame = project();
 auto cloneState = frame->getForUpdate(clone->unitId())->acquireState();
 cloneState->cloned = true;
 frame->updateUnit(clone->unitId(), cloneState->save(), 0);
 auto originalState = frame->getForUpdate(original->unitId())->acquireState();
 originalState->cloneID = clone->unitId();
 frame->updateUnit(original->unitId(), originalState->save(), 0);
 originalState->health.reset();
 frame->updateUnit(original->unitId(), originalState->save(), -original->getAvailableHealth());
 EXPECT_FALSE(frame->getForUpdate(clone->unitId())->alive());
 EXPECT_TRUE(frame->getForUpdate(clone->unitId())->ghostPending);
 EXPECT_EQ(frame->getForUpdate(original->unitId())->cloneID, -1);
 EXPECT_TRUE(original->alive()); EXPECT_TRUE(clone->alive()); EXPECT_FALSE(clone->cloned);
}

#include "AI/BattleAI/BattleExchangeVariant.h"

TEST_F(OrdinaryAmmoRoutingTest, ReadOnlyReachabilityPreservesQueueIdentitiesAcrossTurnsAndHexes)
{
 auto * source = archer(); cart(); auto frame = project();
 auto callback = std::make_shared<CPlayerBattleCallback>(battle(), source->unitOwner());
 const auto * local = frame->battleGetUnitByID(source->unitId());
 ASSERT_TRUE(callback->battleCanShoot(local));
 const auto before = source->save();
 const std::vector<battle::Units> order{{local}, {local}};
 ReachabilityMapCache reachability;
 reachability.update(order, frame);
 for(const auto hex : {BattleHex(11, 5), BattleHex(12, 5), BattleHex(11, 5)})
 {
  const auto & units = reachability.getOneTurnReachableUnits(callback, order, 0, hex);
  ASSERT_EQ(units.size(), 2);
  EXPECT_EQ(units[0], local); EXPECT_EQ(units[1], local);
 }
 EXPECT_EQ(source->save(), before);
 EXPECT_TRUE(frame->stackStates.empty());
}

TEST_F(OrdinaryAmmoRoutingTest, NativeSourcePreviewStaysSeparateFromProjectedReachabilityState)
{
 auto * source = archer(); auto * supply = cart(); auto projected = project();
 projected->removeUnit(supply->unitId());
 auto callback = std::make_shared<CPlayerBattleCallback>(battle(), source->unitOwner());
 auto env = std::shared_ptr<Environment>(&environment, [](Environment *) {});
 BattleExchangeEvaluator evaluator(callback, env, 1.0f, 2);
 evaluator.updateReachabilityMap(projected);
 auto first = evaluator.forkSourceBattle();
 EXPECT_FALSE(first->getForUpdate(source->unitId())->shots.isLimited());
 EXPECT_TRUE(projected->getForUpdate(source->unitId())->shots.isLimited());
 first->removeUnit(supply->unitId());
 EXPECT_NO_THROW(evaluator.checkPositionBlocksOurStacks(*projected,
  projected->battleGetUnitByID(source->unitId()), BattleHex(6, 5)));
 auto second = evaluator.forkSourceBattle();
 EXPECT_FALSE(second->getForUpdate(source->unitId())->shots.isLimited());
 EXPECT_TRUE(second->battleGetUnitByID(supply->unitId())->alive());
 EXPECT_EQ(second->battleGetUnitByID(source->unitId())->getPosition(), source->getPosition());
 EXPECT_TRUE(supply->alive());
}

TEST_F(OrdinaryAmmoRoutingTest, ProjectedSourcePreviewRetainsItsLocalChangesAcrossAlternatives)
{
 auto * source = archer(); auto * supply = cart(); auto projected = project();
 projected->removeUnit(supply->unitId());
 auto env = std::shared_ptr<Environment>(&environment, [](Environment *) {});
 BattleExchangeEvaluator evaluator(projected, env, 1.0f, 2);
 auto first = evaluator.forkSourceBattle();
 EXPECT_TRUE(first->getForUpdate(source->unitId())->shots.isLimited());
 auto firstUnit = first->getForUpdate(source->unitId());
 firstUnit->health.reset();
 auto second = evaluator.forkSourceBattle();
 EXPECT_TRUE(second->getForUpdate(source->unitId())->alive());
 EXPECT_TRUE(second->getForUpdate(source->unitId())->shots.isLimited());
 EXPECT_TRUE(projected->getForUpdate(source->unitId())->alive());
 EXPECT_TRUE(source->alive()); EXPECT_TRUE(supply->alive());
}
