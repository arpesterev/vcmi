#include "BattleBaselineTest.inc"
#include "AI/BattleAI/StackWithBonuses.h"
class OwnedFrameTest : public OwnedAmmoTest {};
TEST_F(OwnedFrameTest, ExistingHypotheticEntryPointsDetachInheritedControl)
{
 auto *shooter=add(5); attackerSideHero->addNewBonus(bonus(BonusType::HYPNOTIZED,1));
 ASSERT_TRUE(shooter->isHypnotized());
 auto frame=HypotheticBattle::captureOwnedFixture(*battle());
 auto unit=frame->getForUpdate(shooter->unitId()); ASSERT_TRUE(unit->isHypnotized());
 frame->removeUnit(unit->unitId()); EXPECT_FALSE(unit->isHypnotized());
 EXPECT_TRUE(shooter->isHypnotized()); battle()->removeUnit(shooter->unitId());
 EXPECT_EQ(unit->isHypnotized(),shooter->isHypnotized());
 EXPECT_EQ(unit->health.getCount(),shooter->health.getCount());
}
TEST_F(OwnedFrameTest, DetachedAcquisitionRetainsChangingOwnedArenaWithoutCycles)
{
 auto *shooter=add(5); auto *supply=cart(BattleSide::ATTACKER,7);
 auto frame=HypotheticBattle::captureOwnedFixture(*battle());
 auto unit=frame->getForUpdate(shooter->unitId()); std::weak_ptr<StackWithBonuses> lifetime=unit;
 auto detached=unit->acquireState(); ASSERT_FALSE(detached->shots.isLimited());
 frame->removeUnit(supply->unitId()); EXPECT_TRUE(detached->shots.isLimited());
 EXPECT_FALSE(shooter->shots.isLimited()); unit.reset(); frame.reset();
 EXPECT_FALSE(lifetime.expired()); EXPECT_TRUE(detached->shots.isLimited());
 auto grandchild=detached->acquireState(); detached.reset();
 EXPECT_FALSE(lifetime.expired()); EXPECT_TRUE(grandchild->shots.isLimited());
 grandchild.reset(); EXPECT_TRUE(lifetime.expired());
}
TEST_F(OwnedFrameTest, ForkedUnitCachesAndEnvironmentUseOnlyChildArena)
{
 auto *shooter=add(5); auto *supply=cart(BattleSide::ATTACKER,7);
 auto parent=HypotheticBattle::captureOwnedFixture(*battle()); auto child=parent->forkOwnedFixture();
 auto p=parent->getForUpdate(shooter->unitId()); auto c=child->getForUpdate(shooter->unitId());
 ASSERT_FALSE(p->shots.isLimited()); ASSERT_FALSE(c->shots.isLimited());
 child->getForUpdate(supply->unitId())->health.reset();
 EXPECT_TRUE(c->shots.isLimited()); EXPECT_FALSE(p->shots.isLimited()); EXPECT_FALSE(shooter->shots.isLimited());
 parent.reset(); EXPECT_TRUE(c->shots.isLimited());
}
TEST_F(OwnedFrameTest, MutationPreservesEnumerationAndRemovesReverseCloneLinks)
{
 auto *first=add(5);auto *second=add(7); auto *third=add(9); first->cloneID=third->unitId(); second->cloneID=third->unitId();
 auto frame=HypotheticBattle::captureOwnedFixture(*battle());
 auto all=[](const battle::Unit *){return true;};auto before=frame->getUnitsIf(all);
 frame->getForUpdate(first->unitId())->position=BattleHex(4,5);
 auto after=frame->getUnitsIf(all);ASSERT_EQ(before.size(),after.size());
 for(size_t i=0;i<before.size();++i) EXPECT_EQ(before[i]->unitId(),after[i]->unitId());
 frame->removeUnit(third->unitId()); EXPECT_EQ(frame->getForUpdate(first->unitId())->cloneID,-1); EXPECT_EQ(frame->getForUpdate(second->unitId())->cloneID,-1);
 EXPECT_EQ(first->cloneID,third->unitId()); EXPECT_EQ(second->cloneID,third->unitId());
}
TEST_F(OwnedFrameTest, UnsupportedFacadeNeverReturnsNativePointers)
{
 add(5); battle()->addNewBonus(bonus(BonusType::BLOCK_MAGIC_ABOVE,3));
 const auto *native=battle()->getBonusBearer(); const auto epoch=native->getTreeVersion();
 auto frame=HypotheticBattle::captureOwnedFixture(*battle());
 EXPECT_THROW(frame->getStacksIf([](const CStack *){return true;}),std::logic_error);
 EXPECT_THROW(frame->getSideHero(BattleSide::ATTACKER),std::logic_error);
 EXPECT_THROW(frame->getSideArmy(BattleSide::ATTACKER),std::logic_error);
 const auto *owned=frame->getBonusBearer(); ASSERT_NE(owned,nullptr);
 EXPECT_NE(owned,native); EXPECT_EQ(frame->getBonusBearer(),owned);
 EXPECT_EQ(owned->getTreeVersion(),epoch);
 const auto expected=native->getAllBonuses(selector()); const auto actual=owned->getAllBonuses(selector());
 ASSERT_EQ(expected->size(),1); ASSERT_EQ(actual->size(),1);
 EXPECT_NE(actual->front().get(),expected->front().get());
 EXPECT_EQ(actual->front()->type,expected->front()->type);
 EXPECT_EQ(actual->front()->source,expected->front()->source); EXPECT_EQ(actual->front()->sid,expected->front()->sid);
 EXPECT_EQ(expected->front()->val,3); EXPECT_EQ(actual->front()->val,3);
 EXPECT_EQ(owned->valOfBonuses(selector()),3);
 EXPECT_EQ(owned->getTreeVersion(),epoch); EXPECT_EQ(native->getTreeVersion(),epoch);
 EXPECT_THROW(frame->getAllObstacles(),std::logic_error);
}
