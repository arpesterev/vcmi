#include "GraphEvaluationIntegrationTest.inc"
#include "lib/battle/OwnedBattleBonusModel.h"
class OwnedUnitLifecycleTest : public EvaluationSnapshotTest
{
public:
 CStack * add(int x){return addStack(BattleSide::ATTACKER,creatureByName("core:woodElf"),BattleHex(x,5),10);}
 void compare(const OwnedBattleBonusModel & owned)
 {
  auto native=battle()->getUnitsIf([](const battle::Unit*){return true;});ASSERT_EQ(native.size(),owned.units().size());
  for(size_t i=0;i<native.size();++i){auto state=native[i]->acquireState();const auto &u=owned.units()[i];EXPECT_EQ(u.id,native[i]->unitId());EXPECT_EQ(u.count,native[i]->getCount());EXPECT_EQ(u.alive,native[i]->alive());EXPECT_EQ(u.ghost,state->ghost);EXPECT_EQ(u.ghostPending,state->ghostPending);EXPECT_EQ(u.cloneID,state->cloneID);}
 }
 void cloneCase(bool original,bool ghost,bool multiple)
 {
  auto *a=add(5),*b=add(7),*c=add(9);a->cloneID=b->unitId();b->cloned=true;if(multiple)c->cloneID=b->unitId();if(ghost)b->ghost=true;
  auto parent=OwnedBattleBonusModel::capture(*battle());auto child=parent;auto id=original?a->unitId():b->unitId();auto version=a->getTreeVersion();ASSERT_TRUE(child.removeUnit(id));EXPECT_EQ(a->cloneID,b->unitId());EXPECT_EQ(a->getTreeVersion(),version);compare(parent);battle()->removeUnit(id);compare(child);
 }
};
TEST_F(OwnedUnitLifecycleTest, DirectClone){cloneCase(false,false,false);}
TEST_F(OwnedUnitLifecycleTest, OriginalCascade){cloneCase(true,false,false);}
TEST_F(OwnedUnitLifecycleTest, AllReverseLinks){cloneCase(false,false,true);}
TEST_F(OwnedUnitLifecycleTest, GhostGuard){cloneCase(false,true,true);}
TEST_F(OwnedUnitLifecycleTest, CascadeReverseLinks){cloneCase(true,false,true);}
TEST_F(OwnedUnitLifecycleTest, RemovalZeroesLocalUpdaterCount)
{
 auto *source=add(5);auto value=bonus(BonusType::STACKS_SPEED,1);value->updater=std::make_shared<TimesStackSizeUpdater>();source->addNewBonus(value);auto owned=OwnedBattleBonusModel::capture(*battle());auto sel=selector().And(Selector::type()(BonusType::STACKS_SPEED));ASSERT_EQ(owned.getAllBonuses(source->unitId(),sel)->front()->val,10);
 ASSERT_TRUE(owned.removeUnit(source->unitId()));battle()->removeUnit(source->unitId());compare(owned);auto native=source->getAllBonuses(sel);auto projected=owned.getAllBonuses(source->unitId(),sel);ASSERT_EQ(native->size(),1);ASSERT_EQ(projected->size(),1);EXPECT_EQ(native->front()->val,0);EXPECT_EQ(projected->front()->val,native->front()->val);
}
TEST_F(OwnedUnitLifecycleTest, RemovingAdjacentUnitChangesOtherLimiter)
{
 auto *source=add(5);auto *recipient=add(6);auto value=bonus(BonusType::STACKS_SPEED,101);auto limiter=std::make_shared<UnitAdjacentLimiter>();limiter->targetUnit=source->creatureId();value->limiter=limiter;recipient->addNewBonus(value);
 auto owned=OwnedBattleBonusModel::capture(*battle());auto sel=selector().And(Selector::type()(BonusType::STACKS_SPEED));ASSERT_EQ(recipient->getAllBonuses(sel)->size(),1);ASSERT_EQ(owned.getAllBonuses(recipient->unitId(),sel)->size(),1);
 ASSERT_TRUE(owned.removeUnit(source->unitId()));EXPECT_TRUE(owned.getAllBonuses(recipient->unitId(),sel)->empty());battle()->removeUnit(source->unitId());EXPECT_TRUE(recipient->getAllBonuses(sel)->empty());
}
TEST_F(OwnedUnitLifecycleTest, MissingCloneCommitsSameNativePartialRemoval)
{
 auto *source=add(5);source->cloneID=999999;auto owned=OwnedBattleBonusModel::capture(*battle());EXPECT_FALSE(owned.removeUnit(source->unitId()));battle()->removeUnit(source->unitId());compare(owned);
}
