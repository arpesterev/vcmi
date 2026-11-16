#include "OwnedAmmoTest.inc"
#include "lib/GameLibrary.h"
#include "lib/CCreatureHandler.h"
class BattleBaselineTest : public OwnedAmmoTest {};
TEST_F(BattleBaselineTest, CapturesEveryExistingCreatureDefinitionForFutureUnits)
{
 add(5);auto model=OwnedBattleBonusModel::capture(*battle());size_t count=0;LIBRARY->creatures()->forEach([&](const Creature * creature,bool &){auto node=model.creatureNode(creature->getId());EXPECT_EQ(model.evaluation().graph().nodes().at(node).type,BonusNodeType::CREATURE);auto expected=creature->getBonusBearer()->getAllBonuses(Selector::all);auto actual=model.evaluation().getAllBonuses(node,Selector::all);ASSERT_EQ(actual->size(),expected->size());for(size_t i=0;i<actual->size();++i){EXPECT_EQ((*actual)[i]->type,(*expected)[i]->type);EXPECT_EQ((*actual)[i]->val,(*expected)[i]->val);}++count;});EXPECT_GT(count,100);std::cout<<"BASELINE_CAPTURE definitions="<<count<<" nodes="<<model.evaluation().graph().nodes().size()<<" bonuses="<<model.evaluation().graph().bonuses().size()<<'\n';
}
TEST_F(BattleBaselineTest, DetachedQueryControlIsDistinctFromEnvironmentCandidateControl)
{
 auto *shooter=add(5);giveArtifact(defenderSideHero,ArtifactID::AMMO_CART,ArtifactPosition::MACH2);auto model=OwnedBattleBonusModel::capture(*battle());EXPECT_FALSE(model.unitHasAmmoCart(shooter->unitId()));CBonusSystemNode controlled(BonusNodeType::GLOBAL_EFFECTS);controlled.addNewBonus(bonus(BonusType::HYPNOTIZED,1));battle::CUnitStateDetached query(shooter,&controlled,true);query.localInit(shooter);query=*shooter;ASSERT_TRUE(query.isHypnotized());EXPECT_TRUE(shooter->unitHasAmmoCart(&query));EXPECT_EQ(model.ammoForUnit(query.unitId(),query.unitSide(),query.isHypnotized()),shooter->unitHasAmmoCart(&query));EXPECT_EQ(model.ownerForUnit(query.unitSide(),query.isHypnotized()),shooter->unitEffectiveOwner(&query));EXPECT_FALSE(model.unitHasAmmoCart(shooter->unitId()));
}
