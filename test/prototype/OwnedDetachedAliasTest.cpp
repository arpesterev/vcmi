#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "AI/BattleAI/StackWithBonuses.h"
#include "lib/bonuses/BonusParameters.h"
#include "lib/bonuses/Updaters.h"
class OwnedDetachedAliasTest:public BattleTestFixture
{
public:
 void SetUp()override{BattleTestFixture::SetUp();startGame();startBattle();}
 CStack*add(){return addStack(BattleSide::ATTACKER,creatureByName("core:woodElf"),BattleHex(5,5),10);}
 static std::shared_ptr<const Bonus> nested(const std::shared_ptr<const Bonus>&b){return std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(b->parameters->toCustom<BonusParametersOnCombatEvent>().effects.at(0)).bonus;}
 static std::shared_ptr<Bonus> rootOf(const TConstBonusListPtr&list,BonusType type){for(const auto &b:*list)if(b->type==type)return b;throw std::logic_error("Missing alias fixture value");}
 static CSelector selected(){return Selector::source(BonusSource::OTHER,ArtifactID(723));}
 static std::shared_ptr<Bonus> install(CStack*s,bool updater=false)
 {
  auto leaf=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::STACKS_SPEED,BonusSource::OTHER,7,ArtifactID(723));if(updater)leaf->updater=std::make_shared<TimesStackSizeUpdater>();
  auto root=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::FLYING,BonusSource::OTHER,1,ArtifactID(723));BonusParametersOnCombatEvent p;p.effects.emplace_back(BonusParametersOnCombatEvent::CombatEffectBonus{leaf,false});root->parameters=std::make_shared<BonusParameters>(p);s->addNewBonus(root);s->addNewBonus(leaf);return leaf;
 }
};
TEST_F(OwnedDetachedAliasTest, CaptureAndQueryPreserveRootNestedAliasesAndIsolateWrites)
{
 auto*s=add();auto live=install(s);auto frame=HypotheticBattle::captureOwnedFixture(*battle());auto unit=frame->getForUpdate(s->unitId());auto frameList=unit->getAllBonuses(selected());ASSERT_EQ(nested(rootOf(frameList,BonusType::FLYING)).get(),rootOf(frameList,BonusType::STACKS_SPEED).get());
 auto detached=unit->acquireState();auto first=detached->getAllBonuses(selected());auto root=rootOf(first,BonusType::FLYING),leaf=rootOf(first,BonusType::STACKS_SPEED);EXPECT_EQ(nested(root).get(),leaf.get());leaf->val=99;EXPECT_EQ(nested(root)->val,99);EXPECT_EQ(live->val,7);
 auto second=detached->getAllBonuses(selected());EXPECT_EQ(rootOf(second,BonusType::STACKS_SPEED)->val,7);EXPECT_EQ(nested(rootOf(second,BonusType::FLYING)).get(),rootOf(second,BonusType::STACKS_SPEED).get());EXPECT_NE(rootOf(second,BonusType::STACKS_SPEED),leaf);EXPECT_EQ(rootOf(unit->getAllBonuses(selected()),BonusType::STACKS_SPEED)->val,7);
 live->val=55;EXPECT_EQ(rootOf(detached->getAllBonuses(selected()),BonusType::STACKS_SPEED)->val,7);
}
TEST_F(OwnedDetachedAliasTest, UpdaterOutputStaysDistinctFromRawNestedTemplate)
{
 auto*s=add();install(s,true);auto native=s->acquireState();auto nativeList=native->getAllBonuses(selected());ASSERT_EQ(rootOf(nativeList,BonusType::STACKS_SPEED)->val,10);ASSERT_EQ(nested(rootOf(nativeList,BonusType::FLYING))->val,7);ASSERT_NE(nested(rootOf(nativeList,BonusType::FLYING)).get(),rootOf(nativeList,BonusType::STACKS_SPEED).get());
 auto frame=HypotheticBattle::captureOwnedFixture(*battle());auto detached=frame->getForUpdate(s->unitId())->acquireState();auto first=detached->getAllBonuses(selected());auto second=detached->getAllBonuses(selected());auto leaf=rootOf(first,BonusType::STACKS_SPEED);auto templateLeaf=nested(rootOf(first,BonusType::FLYING));EXPECT_EQ(leaf->val,10);EXPECT_EQ(templateLeaf->val,7);EXPECT_NE(leaf.get(),templateLeaf.get());EXPECT_EQ(leaf->updater,templateLeaf->updater);EXPECT_EQ(leaf->updater,rootOf(second,BonusType::STACKS_SPEED)->updater);EXPECT_NE(leaf->updater,rootOf(nativeList,BonusType::STACKS_SPEED)->updater);
 int64_t damage=detached->getMaxHealth();detached->damage(damage);EXPECT_EQ(detached->getCount(),9);EXPECT_EQ(rootOf(detached->getAllBonuses(selected()),BonusType::STACKS_SPEED)->val,10);
}
TEST_F(OwnedDetachedAliasTest, RecursiveAcquisitionRetainsAliasAndChangingArenaEnvironment)
{
 auto*s=add();install(s);auto*c=addStack(BattleSide::ATTACKER,CreatureID::AMMO_CART,BattleHex(7,5),1);auto frame=HypotheticBattle::captureOwnedFixture(*battle());auto detached=frame->getForUpdate(s->unitId())->acquireState();auto child=detached->acquireState();auto grandchild=child->acquireState();ASSERT_FALSE(grandchild->shots.isLimited());frame->removeUnit(c->unitId());EXPECT_TRUE(grandchild->shots.isLimited());EXPECT_FALSE(s->shots.isLimited());detached.reset();child.reset();frame.reset();auto list=grandchild->getAllBonuses(selected());EXPECT_EQ(nested(rootOf(list,BonusType::FLYING)).get(),rootOf(list,BonusType::STACKS_SPEED).get());EXPECT_TRUE(grandchild->shots.isLimited());EXPECT_EQ(grandchild->getCount(),10);
}
