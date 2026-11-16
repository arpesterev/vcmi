#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "lib/battle/OwnedBattleBonusModel.h"
#include "lib/battle/Unit.h"
#include "lib/bonuses/BonusGraphMutationState.h"
#include "lib/bonuses/Propagators.h"
#include "lib/bonuses/Updaters.h"
#include "lib/CStack.h"
#include "lib/GameLibrary.h"
#include "lib/CCreatureHandler.h"

class OwnedLeafAppendTest : public BattleTestFixture
{
public:
 void SetUp() override { BattleTestFixture::SetUp(); startGame(); startBattle(); }
 battle::UnitInfo info(BattleSide side = BattleSide::ATTACKER)
 {
  battle::UnitInfo value;
  value.id = battle()->battleNextUnitId(); value.count = 10;
  value.type = creatureByName("core:woodElf"); value.side = side;
  value.position = BattleHex(5,5); value.summoned = true;
  return value;
 }
 CStack * addNative(battle::UnitInfo value)
 {
  JsonNode json; value.save(json); battle()->addUnit(value.id,json);
  return battle()->getStack(value.id);
 }
 static std::shared_ptr<Bonus> value(int amount)
 {
  return std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::STACKS_SPEED,BonusSource::OTHER,amount,ArtifactID(777));
 }
 static CSelector selected() { return Selector::source(BonusSource::OTHER,ArtifactID(777)); }
 static void orderedEqual(const BonusList & actual,const BonusList & expected)
 {
  ASSERT_EQ(actual.size(),expected.size());
  for(size_t i=0;i<actual.size();++i)
  {
   EXPECT_EQ(actual[i]->type,expected[i]->type); EXPECT_EQ(actual[i]->subtype,expected[i]->subtype);
   EXPECT_EQ(actual[i]->val,expected[i]->val); EXPECT_EQ(actual[i]->source,expected[i]->source);
   EXPECT_EQ(actual[i]->sid,expected[i]->sid);
  }
 }
};

TEST_F(OwnedLeafAppendTest, AppendUsesNativeParentOrderAndPreservesCaptureIsolation)
{
 auto added=info(); auto parent=OwnedBattleBonusModel::capture(*battle()); auto child=parent;
 const auto before=parent.evaluation().graph().nodes().size();
 child.addUnit(added);
 ASSERT_EQ(child.units().size(),parent.units().size()+1);
 const auto node=child.units().back().node;
 EXPECT_EQ(node,before);
 const auto & graph=child.evaluation().graph(); const auto & leaf=graph.nodes().at(node);
 ASSERT_EQ(leaf.inheritanceParents.size(),2); ASSERT_EQ(leaf.propagationParents.size(),1);
 EXPECT_EQ(leaf.inheritanceParents.front(),leaf.propagationParents.front());
 EXPECT_EQ(leaf.inheritanceParents.back(),child.creatureNode(added.type));
 EXPECT_EQ(leaf.type,BonusNodeType::STACK_BATTLE); EXPECT_FALSE(leaf.hypothetical);
 EXPECT_TRUE(leaf.children.empty()); EXPECT_TRUE(leaf.exported.empty());
 const auto & army=graph.nodes().at(leaf.inheritanceParents.front());
 EXPECT_EQ(std::count(army.children.begin(),army.children.end(),node),1);
 const auto & creature=graph.nodes().at(leaf.inheritanceParents.back());
 EXPECT_EQ(std::count(creature.children.begin(),creature.children.end(),node),0);
 EXPECT_TRUE(graph.sameOrigin(parent.evaluation().graph()));
 EXPECT_EQ(graph.roots(),parent.evaluation().graph().roots());
 EXPECT_EQ(parent.evaluation().graph().nodes().size(),before);
 EXPECT_THROW(parent.evaluation().withGraph(graph),std::invalid_argument);
 auto * native=addNative(added);
 orderedEqual(*child.getAllBonuses(added.id,selected()),*native->getAllBonuses(selected()));
}

TEST_F(OwnedLeafAppendTest, ExistingSourcePropagationRetainsMaterializationOrder)
{
 auto added=info(); auto * creature=LIBRARY->creh->objects.at(added.type.getNum()).get();
 auto first=value(7); first->propagator=std::make_shared<CPropagatorNodeType>(BonusNodeType::STACK_BATTLE);
 auto second=value(11); second->propagator=std::make_shared<CPropagatorNodeType>(BonusNodeType::STACK_BATTLE);
 creature->addNewBonus(first); creature->addNewBonus(second);
 auto parent=OwnedBattleBonusModel::capture(*battle()); auto child=parent;
 child.addUnit(added); auto * native=addNative(added);
 const auto & graph=child.evaluation().graph(); const auto & leaf=graph.nodes().at(child.units().back().node);
 BonusList raw;
 for(auto id:leaf.materialized) if(graph.bonuses().at(id)->sid==ArtifactID(777)) raw.push_back(std::make_shared<Bonus>(*graph.bonuses().at(id)));
 auto captured=BonusGraphSnapshot::capture({native}); BonusList expected;
 for(auto id:captured.nodes().at(captured.roots().front()).materialized) if(captured.bonuses().at(id)->sid==ArtifactID(777)) expected.push_back(std::make_shared<Bonus>(*captured.bonuses().at(id)));
 orderedEqual(raw,expected);
 orderedEqual(*child.getAllBonuses(added.id,selected()),*native->getAllBonuses(selected()));
 EXPECT_EQ(parent.evaluation().graph().nodes().size()+1,graph.nodes().size());
 creature->removeBonus(first); creature->removeBonus(second);
}

TEST_F(OwnedLeafAppendTest, DuplicateIdentityAndUnknownCreatureRejectWithoutPublication)
{
 auto added=info(); auto child=OwnedBattleBonusModel::capture(*battle()); child.addUnit(added);
 const auto units=child.units(); const auto revision=child.revision();
 const auto nodeCount=child.evaluation().graph().nodes().size();
 EXPECT_THROW(child.addUnit(added),std::invalid_argument);
 EXPECT_EQ(child.units(),units); EXPECT_EQ(child.revision(),revision);
 EXPECT_EQ(child.evaluation().graph().nodes().size(),nodeCount);
 added.id++; added.type=CreatureID(999999);
 EXPECT_ANY_THROW(child.addUnit(added));
 EXPECT_EQ(child.units(),units); EXPECT_EQ(child.revision(),revision);
 EXPECT_EQ(child.evaluation().graph().nodes().size(),nodeCount);
}

TEST_F(OwnedLeafAppendTest, EmptySideRootAndRepeatedAddsRemainDistinctLeaves)
{
 auto child=OwnedBattleBonusModel::capture(*battle());
 auto first=info(BattleSide::DEFENDER); auto second=first; second.id++; second.position=BattleHex(8,5);
 const auto army=child.sideArmyNode(BattleSide::DEFENDER);
 child.addUnit(first); child.addUnit(second);
 const auto & units=child.units(); const auto a=units.at(units.size()-2).node; const auto b=units.back().node;
 EXPECT_NE(a,b); EXPECT_EQ(child.evaluation().graph().nodes().at(a).propagationParents,std::vector<size_t>{army});
 EXPECT_EQ(child.evaluation().graph().nodes().at(b).propagationParents,std::vector<size_t>{army});
 auto parent=child; EXPECT_TRUE(child.removeUnit(first.id));
 EXPECT_TRUE(child.evaluation().graph().nodes().at(a).inheritanceParents.empty());
 EXPECT_EQ(child.evaluation().graph().nodes().at(b).inheritanceParents.size(),2);
 EXPECT_EQ(parent.evaluation().graph().nodes().at(a).inheritanceParents.size(),2);
}

TEST_F(OwnedLeafAppendTest, BoundUpdaterFactsMatchNativeAndRemainIsolated)
{
 auto added=info(); auto * creature=LIBRARY->creh->objects.at(added.type.getNum()).get();
 auto bonus=value(3); bonus->updater=std::make_shared<TimesStackSizeUpdater>(); creature->addNewBonus(bonus);
 auto model=OwnedBattleBonusModel::capture(*battle()); auto parent=model; model.addUnit(added);
 auto * native=addNative(added);
 const auto node=model.units().back().node;
 EXPECT_EQ(model.evaluation().updaterContext(node).nodeType(),BonusNodeType::STACK_BATTLE);
 EXPECT_EQ(model.evaluation().updaterContext(node).owner(),native->unitOwner());
 EXPECT_EQ(model.evaluation().updaterContext(node).stackCount(),native->getCount());
 EXPECT_EQ(model.evaluation().updaterContext(node).stackLevel(),creature->getLevel());
 orderedEqual(*model.getAllBonuses(added.id,selected()),*native->getAllBonuses(selected()));
 EXPECT_EQ(parent.evaluation().graph().nodes().size()+1,model.evaluation().graph().nodes().size());
 creature->removeBonus(bonus);
}
