#include "EvaluationSnapshotTest.inc"
#include "lib/bonuses/BonusGraphMutationState.h"
#include "lib/bonuses/Propagators.h"
#include "lib/mapObjects/CGHeroInstance.h"
class GraphEvaluationIntegrationTest : public EvaluationSnapshotTest
{
public:
 static BonusEvaluationSnapshot capture(const CBonusSystemNode & node) {return BonusEvaluationSnapshot::capture({&node});}
 static BonusGraphMutationState mutations(const BonusEvaluationSnapshot & eval)
 {
  return eval.mutations();
 }
 static void detachAll(BonusGraphMutationState & state,size_t node)
 {
  while(!state.nodes().at(node).propagationParents.empty())state.detach(node,state.nodes().at(node).propagationParents.front());
  while(!state.nodes().at(node).inheritanceParents.empty())state.detachSource(node,state.nodes().at(node).inheritanceParents.front());
 }
};
TEST_F(GraphEvaluationIntegrationTest, NativeRemovalDetachesInheritedHypnotize)
{
 auto *stack=addStack(BattleSide::ATTACKER,creatureByName("core:woodElf"),BattleHex(5,5),10);
 attackerSideHero->addNewBonus(bonus(BonusType::HYPNOTIZED,1));ASSERT_TRUE(stack->isHypnotized());
 auto eval=capture(*stack);auto root=eval.graph().roots().front();auto state=mutations(eval);auto select=Selector::type()(BonusType::HYPNOTIZED);
 ASSERT_FALSE(eval.getAllBonuses(root,select)->empty());auto version=stack->getTreeVersion();detachAll(state,root);auto changed=eval.withGraph(state.snapshot());
 EXPECT_TRUE(changed.getAllBonuses(root,select)->empty());EXPECT_FALSE(eval.getAllBonuses(root,select)->empty());EXPECT_TRUE(stack->isHypnotized());EXPECT_EQ(stack->getTreeVersion(),version);
 battle()->removeUnit(stack->unitId());EXPECT_FALSE(stack->isHypnotized());EXPECT_EQ(changed.getAllBonuses(root,select)->size(),stack->getAllBonuses(select)->size());
}
TEST_F(GraphEvaluationIntegrationTest, NativeSourceRemovalWithdrawsOtherRecipientControl)
{
 auto *source=addStack(BattleSide::ATTACKER,creatureByName("core:woodElf"),BattleHex(5,5),10);
 auto *recipient=addStack(BattleSide::ATTACKER,creatureByName("core:woodElf"),BattleHex(7,5),10);
 auto effect=bonus(BonusType::HYPNOTIZED,1);effect->propagator=std::make_shared<CPropagatorNodeType>(BonusNodeType::HERO);
 source->addNewBonus(effect);ASSERT_TRUE(recipient->isHypnotized());
 auto eval=BonusEvaluationSnapshot::capture({source,recipient});auto sourceId=eval.graph().roots()[0],recipientId=eval.graph().roots()[1];auto state=mutations(eval);auto select=Selector::type()(BonusType::HYPNOTIZED);
 detachAll(state,sourceId);auto changed=eval.withGraph(state.snapshot());EXPECT_TRUE(changed.getAllBonuses(recipientId,select)->empty());EXPECT_TRUE(recipient->isHypnotized());EXPECT_FALSE(eval.getAllBonuses(recipientId,select)->empty());
 battle()->removeUnit(source->unitId());EXPECT_FALSE(recipient->isHypnotized());EXPECT_EQ(changed.getAllBonuses(recipientId,select)->size(),recipient->getAllBonuses(select)->size());
}
TEST_F(GraphEvaluationIntegrationTest, CaptureOriginRejectsUnrelatedGraphAndNestedBranchRemainsIsolated)
{
 CBonusSystemNode node(BonusNodeType::GLOBAL_EFFECTS),parent(BonusNodeType::GLOBAL_EFFECTS);parent.addNewBonus(bonus(BonusType::STACKS_SPEED,17));node.attachTo(parent);
 auto eval=capture(node);auto unrelated=capture(node);EXPECT_THROW(eval.withGraph(unrelated.graph()),std::invalid_argument);
 auto state=mutations(eval);auto id=eval.graph().roots().front();detachAll(state,id);auto branch=eval.withGraph(state.snapshot());auto nested=mutations(branch);auto unchanged=branch.withGraph(nested.snapshot());
 EXPECT_TRUE(branch.getAllBonuses(id,selector())->empty());EXPECT_TRUE(unchanged.getAllBonuses(id,selector())->empty());EXPECT_EQ(eval.getAllBonuses(id,selector())->size(),1);
}

static_assert(!std::is_constructible_v<BonusGraphMutationState, const BonusGraphSnapshot &, std::vector<SnapshotBonusUpdaterContext>>, "Foreign updater contexts must not be publicly injectable");
TEST_F(GraphEvaluationIntegrationTest, BoundMutationContextsCannotBeReplacedByLaterCapture)
{
 auto *source=addStack(BattleSide::ATTACKER,creatureByName("core:woodElf"),BattleHex(5,5),10);
 auto *recipient=addStack(BattleSide::ATTACKER,creatureByName("core:woodElf"),BattleHex(7,5),10);
 auto effect=bonus(BonusType::STACKS_SPEED,1);effect->propagator=std::make_shared<CPropagatorNodeType>(BonusNodeType::HERO);effect->propagationUpdater=std::make_shared<TimesStackSizeUpdater>();source->addNewBonus(effect);
 auto first=BonusEvaluationSnapshot::capture({source,recipient});int64_t damage=source->getMaxHealth();source->damage(damage);ASSERT_EQ(source->getCount(),9);auto later=BonusEvaluationSnapshot::capture({source,recipient});
 auto reexport=[](const BonusEvaluationSnapshot & eval){auto state=eval.mutations();auto node=eval.graph().roots()[0];auto parents=state.nodes()[node].propagationParents;for(auto p:parents)state.detach(node,p);for(auto p:parents)state.attach(node,p);return eval.withGraph(state.snapshot());};
 auto a=reexport(first);auto b=reexport(later);auto select=selector().And(Selector::type()(BonusType::STACKS_SPEED));
 auto x=a.getAllBonuses(a.graph().roots()[1],select);auto y=b.getAllBonuses(b.graph().roots()[1],select);ASSERT_EQ(x->size(),1);ASSERT_EQ(y->size(),1);EXPECT_EQ(x->front()->val,10);EXPECT_EQ(y->front()->val,9);
 EXPECT_THROW(first.withGraph(b.graph()),std::invalid_argument);
}
