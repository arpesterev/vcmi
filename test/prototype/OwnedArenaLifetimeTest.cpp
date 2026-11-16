#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "AI/BattleAI/StackWithBonuses.h"
#include "lib/bonuses/Updaters.h"
#include "lib/bonuses/Propagators.h"
#include "lib/mapObjects/CGHeroInstance.h"
class OwnedArenaLifetimeTest : public BattleTestFixture
{
public:
 void SetUp()override{BattleTestFixture::SetUp();startGame();startBattle();}
 CStack*add(int x){return addStack(BattleSide::ATTACKER,creatureByName("core:woodElf"),BattleHex(x,5),10);}
 static CSelector selected(){return Selector::source(BonusSource::OTHER,ArtifactID(777));}
};
TEST_F(OwnedArenaLifetimeTest, DetachedShotsObserveLaterSameFrameCartRemoval)
{
 auto*s=add(5);auto*c=addStack(BattleSide::ATTACKER,CreatureID::AMMO_CART,BattleHex(7,5),1);auto frame=HypotheticBattle::captureOwnedFixture(*battle());auto detached=frame->battleGetUnitByID(s->unitId())->acquireState();ASSERT_FALSE(detached->shots.isLimited());frame->removeUnit(c->unitId());EXPECT_TRUE(detached->shots.isLimited());EXPECT_FALSE(s->shots.isLimited());EXPECT_FALSE(c->ghost);
}
TEST_F(OwnedArenaLifetimeTest, DetachedStatePinsArenaAfterOwnerRelease)
{
 auto*s=add(5);addStack(BattleSide::ATTACKER,CreatureID::AMMO_CART,BattleHex(7,5),1);auto frame=HypotheticBattle::captureOwnedFixture(*battle());auto detached=frame->battleGetUnitByID(s->unitId())->acquireState();frame.reset();EXPECT_FALSE(detached->shots.isLimited());EXPECT_EQ(detached->getCount(),10);int64_t damage=detached->getMaxHealth();detached->damage(damage);EXPECT_EQ(detached->getCount(),9);EXPECT_EQ(s->getCount(),10);
}
TEST_F(OwnedArenaLifetimeTest, ForkedDetachedEnvironmentsStayBoundToOwnArena)
{
 auto*s=add(5);auto*c=addStack(BattleSide::ATTACKER,CreatureID::AMMO_CART,BattleHex(7,5),1);auto parent=HypotheticBattle::captureOwnedFixture(*battle());auto child=parent->forkOwnedFixture();auto sibling=parent->forkOwnedFixture();auto p=parent->battleGetUnitByID(s->unitId())->acquireState();auto a=child->battleGetUnitByID(s->unitId())->acquireState();auto b=sibling->battleGetUnitByID(s->unitId())->acquireState();child->removeUnit(c->unitId());EXPECT_TRUE(a->shots.isLimited());EXPECT_FALSE(p->shots.isLimited());EXPECT_FALSE(b->shots.isLimited());child.reset();EXPECT_TRUE(a->shots.isLimited());parent.reset();sibling.reset();EXPECT_FALSE(p->shots.isLimited());EXPECT_FALSE(b->shots.isLimited());EXPECT_FALSE(s->shots.isLimited());
}
TEST_F(OwnedArenaLifetimeTest, DetachedHealthDoesNotRerunFrozenBonusUpdater)
{
 auto*s=add(5);auto effect=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::STACKS_SPEED,BonusSource::OTHER,1,ArtifactID(777));effect->updater=std::make_shared<TimesStackSizeUpdater>();s->addNewBonus(effect);auto frame=HypotheticBattle::captureOwnedFixture(*battle());auto detached=frame->battleGetUnitByID(s->unitId())->acquireState();auto before=detached->getAllBonuses(selected());ASSERT_EQ(before->size(),1);EXPECT_EQ(before->front()->val,10);int64_t damage=detached->getMaxHealth();detached->damage(damage);ASSERT_EQ(detached->getCount(),9);auto after=detached->getAllBonuses(selected());ASSERT_EQ(after->size(),1);EXPECT_EQ(after->front()->val,10);EXPECT_EQ(frame->getForUpdate(s->unitId())->getCount(),10);EXPECT_EQ(s->getCount(),10);
}
TEST_F(OwnedArenaLifetimeTest, MaterializedUpdaterAliasesAreOwnedNotLiveHandles)
{
 auto*s=add(5);auto*a=add(7);auto*b=add(9);auto effect=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::STACKS_SPEED,BonusSource::OTHER,1,ArtifactID(777));effect->propagator=std::make_shared<CPropagatorNodeType>(BonusNodeType::HERO);effect->propagationUpdater=std::make_shared<TimesStackSizeUpdater>();s->addNewBonus(effect);attackerSideHero->nodeHasChanged();auto native=a->getAllBonuses(selected());ASSERT_EQ(native->size(),1);auto frame=HypotheticBattle::captureOwnedFixture(*battle());auto first=frame->getForUpdate(a->unitId())->getAllBonuses(selected());auto second=frame->getForUpdate(b->unitId())->getAllBonuses(selected());ASSERT_EQ(first->size(),1);ASSERT_EQ(second->size(),1);EXPECT_EQ(first->front()->val,native->front()->val);EXPECT_EQ(first->front()->propagationUpdater.get(),second->front()->propagationUpdater.get());EXPECT_NE(first->front()->propagationUpdater.get(),native->front()->propagationUpdater.get());
}
