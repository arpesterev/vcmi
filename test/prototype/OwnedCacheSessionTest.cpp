#include "StdInc.h"
#include "lib/bonuses/OwnedBonusGraphSession.h"
#include "lib/bonuses/BonusGraphMutationState.h"
#include "lib/bonuses/CBonusSystemNode.h"
#include "lib/bonuses/BonusParameters.h"
#include "lib/bonuses/Propagators.h"
namespace
{
BonusEvaluationSnapshot cacheEvaluation(const std::vector<const CBonusSystemNode *> & nodes)
{
 return BonusEvaluationSnapshot::capture(nodes, [](const CBonusSystemNode & node)
 {
  BonusLimiterFacts facts; facts.type=node.getNodeType(); facts.originalOwner=node.getOwner(); return facts;
 });
}
std::shared_ptr<Bonus> timedCacheValue()
{
 auto value=std::make_shared<Bonus>(BonusDuration::N_TURNS,BonusType::STACKS_SPEED,BonusSource::OTHER,7,ArtifactID(798));
 value->turnsRemain=2; return value;
}
}
TEST(OwnedCacheSessionTest, RequestMembershipSurvivesDurationAliasChangesUntilInvalidation)
{
 CBonusSystemNode native(BonusNodeType::GLOBAL_EFFECTS);
 auto value=timedCacheValue(); native.addNewBonus(value);
 CSelector two=[](const Bonus * b){return b->turnsRemain==2;};
 ASSERT_EQ(native.getAllBonuses(two,"duration")->size(),1);
 auto evaluation=cacheEvaluation({&native}); auto node=evaluation.graph().roots().front();
 auto owned=OwnedBonusGraphSession::capture(evaluation);
 auto id=evaluation.graph().nodes().at(node).exported.front();
 auto version=owned.nodeVersion(node);
 value->turnsRemain=1; owned.setTurnsRemain(id,1);
 EXPECT_EQ(owned.nodeVersion(node),version);
 auto hit=owned.query(node,two,"duration"); ASSERT_EQ(hit->size(),1); EXPECT_EQ(hit->front()->turnsRemain,1);
 EXPECT_EQ(hit->size(),native.getAllBonuses(two,"duration")->size());
 EXPECT_TRUE(owned.query(node,two,"new-duration")->empty());
 EXPECT_TRUE(native.getAllBonuses(two,"new-duration")->empty());
 native.nodeHasChanged(); owned.invalidateNodeAndDescendants(node);
 EXPECT_TRUE(owned.query(node,two,"duration")->empty());
 EXPECT_TRUE(native.getAllBonuses(two,"duration")->empty());
 EXPECT_NE(owned.nodeVersion(node),version);
}
TEST(OwnedCacheSessionTest, KeyIdentityOverridesSelectorAndEmptyHitsRemainWarm)
{
 CBonusSystemNode native(BonusNodeType::GLOBAL_EFFECTS); native.addNewBonus(timedCacheValue());
 CSelector reject=[](const Bonus *){return false;}; CSelector accept=[](const Bonus *){return true;};
 native.getAllBonuses(reject,"empty");
 auto evaluation=cacheEvaluation({&native}); auto node=evaluation.graph().roots().front(); auto owned=OwnedBonusGraphSession::capture(evaluation);
 EXPECT_TRUE(owned.query(node,accept,"empty")->empty()); EXPECT_TRUE(native.getAllBonuses(accept,"empty")->empty());
 EXPECT_EQ(owned.query(node,accept)->size(),native.getAllBonuses(accept)->size());
}
TEST(OwnedCacheSessionTest, ForkWarmthAndInvalidationAreBranchLocal)
{
 CBonusSystemNode native(BonusNodeType::GLOBAL_EFFECTS); native.addNewBonus(timedCacheValue());
 auto evaluation=cacheEvaluation({&native}); auto node=evaluation.graph().roots().front(); auto parent=OwnedBonusGraphSession::capture(evaluation);
 auto child=parent.fork(); auto sibling=parent.fork();
 CSelector reject=[](const Bonus *){return false;}; CSelector accept=[](const Bonus *){return true;};
 EXPECT_TRUE(child.query(node,reject,"branch")->empty());
 EXPECT_EQ(parent.query(node,accept,"branch")->size(),1); EXPECT_EQ(sibling.query(node,accept,"branch")->size(),1);
 auto parentVersion=parent.nodeVersion(node); child.invalidateNodeAndDescendants(node);
 EXPECT_EQ(parent.nodeVersion(node),parentVersion); EXPECT_EQ(sibling.nodeVersion(node),parentVersion); EXPECT_NE(child.nodeVersion(node),parentVersion);
 EXPECT_EQ(child.query(node,accept,"branch")->size(),1);
}
TEST(OwnedCacheSessionTest, DescendantInvalidationDoesNotReachUnrelatedNodes)
{
 CBonusSystemNode ancestor(BonusNodeType::GLOBAL_EFFECTS), child(BonusNodeType::GLOBAL_EFFECTS), unrelated(BonusNodeType::GLOBAL_EFFECTS);
 child.attachTo(ancestor); ancestor.addNewBonus(timedCacheValue()); unrelated.addNewBonus(timedCacheValue());
 auto evaluation=cacheEvaluation({&ancestor,&child,&unrelated}); auto roots=evaluation.graph().roots(); auto owned=OwnedBonusGraphSession::capture(evaluation);
 auto a=owned.nodeVersion(roots[0]), c=owned.nodeVersion(roots[1]), u=owned.nodeVersion(roots[2]);
 owned.invalidateNodeAndDescendants(roots[0]); ancestor.nodeHasChanged();
 EXPECT_NE(owned.nodeVersion(roots[0]),a); EXPECT_NE(owned.nodeVersion(roots[1]),c); EXPECT_EQ(owned.nodeVersion(roots[2]),u);
 child.detachFrom(ancestor);
}
TEST(OwnedCacheSessionTest, RetainedInternalMembershipResolvesBranchLocalAliases)
{
 CBonusSystemNode native(BonusNodeType::GLOBAL_EFFECTS); native.addNewBonus(timedCacheValue());
 auto evaluation=cacheEvaluation({&native}); auto node=evaluation.graph().roots().front(); auto owned=OwnedBonusGraphSession::capture(evaluation);
 CSelector accept=[](const Bonus *){return true;}; auto ids=owned.queryMembership(node,accept); auto sibling=owned.fork();
 auto detached=owned.resolveMembership(ids); auto id=evaluation.graph().nodes().at(node).exported.front(); owned.setTurnsRemain(id,1);
 EXPECT_EQ(owned.resolveMembership(ids)->front()->turnsRemain,1); EXPECT_EQ(sibling.resolveMembership(ids)->front()->turnsRemain,2);
 EXPECT_EQ(detached->front()->turnsRemain,2);
 detached->front()->val=999; EXPECT_EQ(owned.resolveMembership(ids)->front()->val,7);
}
TEST(OwnedCacheSessionTest, DetachInvalidationsUseCallbackTopologyIncludingSharedDescendants)
{
 CBonusSystemNode root(BonusNodeType::GLOBAL_EFFECTS), left(BonusNodeType::GLOBAL_EFFECTS), right(BonusNodeType::GLOBAL_EFFECTS), leaf(BonusNodeType::GLOBAL_EFFECTS), other(BonusNodeType::GLOBAL_EFFECTS);
 left.attachTo(root); right.attachTo(root); leaf.attachTo(left); leaf.attachTo(right);
 auto evaluation=cacheEvaluation({&root,&left,&right,&leaf,&other}); auto ids=evaluation.graph().roots();
 std::vector<int32_t> before{root.getTreeVersion(),left.getTreeVersion(),right.getTreeVersion(),leaf.getTreeVersion(),other.getTreeVersion()};
 auto mutation=evaluation.mutations(); mutation.detach(ids[1],ids[0]); left.detachFrom(root);
 auto owned=OwnedBonusGraphSession::capture(evaluation.withGraph(mutation.snapshot()));
 std::vector<int32_t> after{root.getTreeVersion(),left.getTreeVersion(),right.getTreeVersion(),leaf.getTreeVersion(),other.getTreeVersion()};
 for(size_t i=0;i<ids.size();++i) EXPECT_EQ(owned.nodeVersion(ids[i])!=before[i],after[i]!=before[i]);
 leaf.detachFrom(left); leaf.detachFrom(right); right.detachFrom(root);
}
TEST(OwnedCacheSessionTest, SourceOnlyEdgeInvalidatesRecipientWithoutInventingChildEdges)
{
 CBonusSystemNode source(BonusNodeType::CREATURE), recipient(BonusNodeType::GLOBAL_EFFECTS), other(BonusNodeType::GLOBAL_EFFECTS);
 auto evaluation=cacheEvaluation({&source,&recipient,&other}); auto ids=evaluation.graph().roots();
 auto mutation=evaluation.mutations(); mutation.attachSource(ids[1],ids[0]); recipient.attachToSource(source);
 auto owned=OwnedBonusGraphSession::capture(evaluation.withGraph(mutation.snapshot()));
 EXPECT_EQ(owned.nodeVersion(ids[0]),evaluation.graph().nodeCaches().at(ids[0]).epoch);
 EXPECT_NE(owned.nodeVersion(ids[1]),evaluation.graph().nodeCaches().at(ids[1]).epoch);
 EXPECT_EQ(owned.nodeVersion(ids[2]),evaluation.graph().nodeCaches().at(ids[2]).epoch);
 auto recipientVersion=owned.nodeVersion(ids[1]); owned.invalidateNodeAndDescendants(ids[0]); source.nodeHasChanged();
 EXPECT_EQ(owned.nodeVersion(ids[1]),recipientVersion);
 recipient.detachFromSource(source);
}
TEST(OwnedCacheSessionTest, ThrowingSelectorCannotMutateInternalNestedValuesOrSibling)
{
 CBonusSystemNode native(BonusNodeType::GLOBAL_EFFECTS); auto value=timedCacheValue(); auto nested=timedCacheValue();
 BonusParametersOnCombatEvent parameters; parameters.effects.emplace_back(BonusParametersOnCombatEvent::CombatEffectBonus{nested,false});
 value->parameters=std::make_shared<BonusParameters>(parameters); native.addNewBonus(value);
 auto evaluation=cacheEvaluation({&native}); auto node=evaluation.graph().roots().front(); auto parent=OwnedBonusGraphSession::capture(evaluation); auto sibling=parent.fork();
 CSelector corrupt=[](const Bonus * value)->bool
 {
  const auto & effect=std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(value->parameters->toCustom<BonusParametersOnCombatEvent>().effects.at(0));
  const_cast<Bonus *>(effect.bonus.get())->val=999;
  throw std::runtime_error("selector failure");
 };
 EXPECT_THROW(parent.query(node,corrupt,"failed"),std::runtime_error);
 CSelector all=[](const Bonus *){return true;};
 for(auto * session:{&parent,&sibling})
 {
  auto result=session->query(node,all,"failed"); ASSERT_EQ(result->size(),1);
  const auto & effect=std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(result->front()->parameters->toCustom<BonusParametersOnCombatEvent>().effects.at(0));
  EXPECT_EQ(effect.bonus->val,7);
 }
 EXPECT_EQ(nested->val,7);
}
TEST(OwnedCacheSessionTest, RecapturedEvaluationExportsCurrentCacheAliases)
{
 CBonusSystemNode native(BonusNodeType::GLOBAL_EFFECTS); native.addNewBonus(timedCacheValue());
 auto evaluation=cacheEvaluation({&native}); auto node=evaluation.graph().roots().front(); auto original=OwnedBonusGraphSession::capture(evaluation);
 CSelector all=[](const Bonus *){return true;}; original.query(node,all,"held");
 auto id=evaluation.graph().nodes().at(node).exported.front(); original.setTurnsRemain(id,1);
 auto restored=OwnedBonusGraphSession::capture(original.evaluation());
 EXPECT_EQ(restored.query(node,all,"held")->front()->turnsRemain,1);
 restored.setTurnsRemain(id,4); EXPECT_EQ(restored.query(node,all,"held")->front()->turnsRemain,4);
 EXPECT_EQ(original.query(node,all,"held")->front()->turnsRemain,1);
}
TEST(OwnedCacheSessionTest, HypotheticalChildDoesNotAcquireNativeDescendantInvalidation)
{
 CBonusSystemNode root(BonusNodeType::GLOBAL_EFFECTS), child(BonusNodeType::GLOBAL_EFFECTS,true);
 child.attachTo(root);
 auto evaluation=cacheEvaluation({&root,&child}); auto ids=evaluation.graph().roots(); auto owned=OwnedBonusGraphSession::capture(evaluation);
 auto before=owned.nodeVersion(ids[1]); auto nativeBefore=child.getTreeVersion();
 root.nodeHasChanged(); owned.invalidateNodeAndDescendants(ids[0]);
 EXPECT_EQ(child.getTreeVersion(),nativeBefore); EXPECT_EQ(owned.nodeVersion(ids[1]),before);
 child.detachFrom(root);
}
TEST(OwnedCacheSessionTest, UnpropagationWithoutMaterializedMatchDoesNotInvalidate)
{
 CBonusSystemNode root(BonusNodeType::GLOBAL_EFFECTS); auto value=timedCacheValue();
 value->propagator=std::make_shared<CPropagatorNodeType>(BonusNodeType::HERO); root.addNewBonus(value);
 auto evaluation=cacheEvaluation({&root}); auto node=evaluation.graph().roots().front();
 auto mutation=evaluation.mutations(); auto id=evaluation.graph().nodes().at(node).exported.front();
 auto before=root.getTreeVersion(); mutation.removeBonus(node,id); root.removeBonus(value);
 auto owned=OwnedBonusGraphSession::capture(evaluation.withGraph(mutation.snapshot()));
 EXPECT_EQ(root.getTreeVersion(),before); EXPECT_EQ(owned.nodeVersion(node),before);
}
