#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "server/CGameHandler.h"
#include "lib/networkPacks/SetStackEffect.h"
#include "lib/bonuses/OwnedBonusGraphSession.h"
#include "lib/bonuses/BonusParameters.h"
#include "lib/bonuses/Propagators.h"
#include "lib/bonuses/Updaters.h"
namespace
{
Bonus effect(){Bonus b(BonusDuration::N_TURNS,BonusType::STACKS_SPEED,BonusSource::SPELL_EFFECT,7,SpellID(SpellID::HASTE));b.turnsRemain=2;return b;}
std::shared_ptr<const Bonus> nested(const std::shared_ptr<const Bonus>&b){return std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(b->parameters->toCustom<BonusParametersOnCombatEvent>().effects.at(0)).bonus;}
}
class OwnedBonusGraphSessionTest:public BattleTestFixture
{
public:
 void SetUp()override{BattleTestFixture::SetUp();startGame();startBattle();}
 CStack* add(){return addStack(BattleSide::ATTACKER,creatureByName("core:woodElf"),BattleHex(5,5),10);}
 void packet(CStack*s,const Bonus&b,bool update){SetStackEffect p;p.battleID=BattleID(0);auto &items=update?p.toUpdate:p.toAdd;items.emplace_back(s->unitId(),std::vector<Bonus>{b});gameHandler->sendAndApply(p);}
 static size_t find(const BonusEvaluationSnapshot&e,size_t node,const CSelector&select){for(auto id:e.graph().nodes().at(node).exported)if(select(e.graph().bonuses().at(id).get()))return id;throw std::logic_error("Missing exported test bonus");}
};
TEST_F(OwnedBonusGraphSessionTest, DurationAndFreshRootShareExactLogicalGraphValues)
{
 auto*s=add();auto b=effect();packet(s,b,false);auto selected=Selector::source(BonusSource::SPELL_EFFECT,SpellID(SpellID::HASTE));auto live=s->getExportedBonusList().getFirst(selected);
 Bonus root(BonusDuration::PERMANENT,BonusType::FLYING,BonusSource::OTHER,1,ArtifactID(721));BonusParametersOnCombatEvent p;p.effects.emplace_back(BonusParametersOnCombatEvent::CombatEffectBonus{live,false});root.parameters=std::make_shared<BonusParameters>(p);packet(s,root,false);
 auto e=BonusEvaluationSnapshot::capture({s});auto node=e.graph().roots()[0];auto leafId=find(e,node,selected),rootId=find(e,node,Selector::source(BonusSource::OTHER,ArtifactID(721)));auto parent=OwnedBonusGraphSession::capture(e);auto event=parent.eventFromBonuses({rootId});auto child=parent.fork();auto sibling=parent.fork();child.setTurnsRemain(leafId,8);auto newId=child.addEventRoot(node,event,0);auto current=child.evaluation();
 EXPECT_EQ(nested(current.graph().bonuses()[rootId]),current.graph().bonuses()[leafId]);EXPECT_EQ(nested(current.graph().bonuses()[newId]),current.graph().bonuses()[leafId]);EXPECT_EQ(current.graph().bonuses()[newId]->parameters,current.graph().bonuses()[rootId]->parameters);EXPECT_NE(current.graph().bonuses()[newId],current.graph().bonuses()[rootId]);EXPECT_EQ(nested(parent.evaluation().graph().bonuses()[rootId])->turnsRemain,2);EXPECT_EQ(nested(sibling.evaluation().graph().bonuses()[rootId])->turnsRemain,2);
 b.turnsRemain=8;b.val=99;packet(s,b,true);packet(s,root,false);EXPECT_EQ(live->turnsRemain,current.graph().bonuses()[leafId]->turnsRemain);EXPECT_EQ(live->val,current.graph().bonuses()[leafId]->val);
 auto own=current.getAllBonuses(node,selected);ASSERT_EQ(own->size(),1);EXPECT_EQ(own->front()->turnsRemain,8);EXPECT_EQ(own->front()->val,7);
}
TEST_F(OwnedBonusGraphSessionTest, PropagationOutputsAreRegisteredWithoutReplayingHistoricalValues)
{
 auto*s=add();Bonus b(BonusDuration::PERMANENT,BonusType::STACKS_SPEED,BonusSource::OTHER,7,ArtifactID(722));b.propagator=std::make_shared<CPropagatorNodeType>(BonusNodeType::HERO);b.propagationUpdater=std::make_shared<TimesStackSizeUpdater>(std::numeric_limits<int>::min(),std::numeric_limits<int>::max(),1,3);packet(s,b,false);auto sel=Selector::source(BonusSource::OTHER,ArtifactID(722));auto historical=attackerSideHero->getAllBonuses(sel);ASSERT_EQ(historical->size(),1);ASSERT_EQ(historical->front()->val,30);int64_t damage=s->getMaxHealth();s->damage(damage);ASSERT_EQ(s->getCount(),9);
 auto e=BonusEvaluationSnapshot::capture({s,attackerSideHero});auto source=e.graph().roots()[0],hero=e.graph().roots()[1];auto id=find(e,source,sel);auto session=OwnedBonusGraphSession::capture(e);auto event=session.eventFromBonuses({id});auto oldCount=e.graph().bonuses().size();auto added=session.addEventRoot(source,event,0);packet(s,b,false);auto current=session.evaluation();EXPECT_GE(current.graph().bonuses().size(),oldCount+2);EXPECT_GE(added,oldCount);
 auto own=current.getAllBonuses(hero,sel),live=attackerSideHero->getAllBonuses(sel);ASSERT_EQ(own->size(),2);ASSERT_EQ(live->size(),2);for(size_t i=0;i<2;++i)EXPECT_EQ((*own)[i]->val,(*live)[i]->val);EXPECT_EQ((*own)[0]->propagationUpdater,(*own)[1]->propagationUpdater);EXPECT_NE((*own)[0],(*own)[1]);EXPECT_EQ(e.getAllBonuses(hero,sel)->front()->val,30);
 session.setTurnsRemain(added,9);auto again=session.evaluation();EXPECT_EQ(again.graph().bonuses()[added]->turnsRemain,9);auto materials=again.getAllBonuses(hero,sel);for(const auto &v:*materials)EXPECT_EQ(v->turnsRemain,0); // Materializations are distinct Bonus values.
}
TEST_F(OwnedBonusGraphSessionTest, EarlierCommittedItemSurvivesLaterInvalidOperation)
{
 auto*s=add();auto e=BonusEvaluationSnapshot::capture({s});auto node=e.graph().roots()[0];auto session=OwnedBonusGraphSession::capture(e);auto b=effect();auto event=session.makeEvent({b});auto first=session.addEventRoot(node,event,0);auto revision=session.revision();auto current=session.evaluation();
 EXPECT_THROW(session.addEventRoot(current.graph().nodes().size(),event,0),std::out_of_range);
 EXPECT_EQ(session.revision(),revision);EXPECT_EQ(session.evaluation().graph().bonuses()[first],current.graph().bonuses()[first]);
 EXPECT_THROW(session.setTurnsRemain(current.graph().bonuses().size(),9),std::out_of_range);
 EXPECT_EQ(session.revision(),revision);session.setTurnsRemain(first,2);EXPECT_EQ(session.revision(),revision+1);EXPECT_EQ(session.evaluation().graph().bonuses()[first]->turnsRemain,2);
}
TEST_F(OwnedBonusGraphSessionTest, FreshScalarRootCopiesEveryFieldAndRejectsSiblingEvent)
{
 auto*s=add();auto e=BonusEvaluationSnapshot::capture({s});auto node=e.graph().roots()[0];auto parent=OwnedBonusGraphSession::capture(e);auto child=parent.fork(),sibling=parent.fork();auto b=effect();b.stacking="logical-copy";b.bonusOwner=PlayerColor(1);b.hidden=true;b.targetSourceType=BonusSource::ARTIFACT;b.targetSourceID=ArtifactID(4);auto event=child.makeEvent({b});auto id=child.addEventRoot(node,event,0);auto copied=child.evaluation().graph().bonuses()[id];EXPECT_EQ(copied->toJsonNode(),b.toJsonNode());EXPECT_EQ(copied->bonusOwner,b.bonusOwner);EXPECT_EQ(copied->hidden,b.hidden);EXPECT_EQ(copied->targetSourceID,b.targetSourceID);
 EXPECT_THROW(sibling.addEventRoot(node,event,0),std::invalid_argument);
 EXPECT_THROW(parent.addEventRoot(node,event,0),std::invalid_argument);
 EXPECT_EQ(parent.evaluation().graph().bonuses().size(),e.graph().bonuses().size());
}

TEST_F(OwnedBonusGraphSessionTest, ColdUpdaterQueryKeepsRetainedMembershipAfterFailedRecomputation)
{
 auto * source = add();
 auto scaled = effect();
 scaled.val = 1;
 scaled.updater = std::make_shared<TimesStackSizeUpdater>();
 packet(source, scaled, false);
 const auto selected = Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::HASTE));
 auto evaluation = BonusEvaluationSnapshot::capture({source});
 const auto node = evaluation.graph().roots().front();
 auto parent = OwnedBonusGraphSession::capture(evaluation);
 parent.invalidateNodeAndDescendants(node);
 const auto membership = parent.queryMembership(node, selected, "derived");
 ASSERT_EQ(membership.size(), 1);
 EXPECT_EQ(parent.resolveMembership(membership)->front()->val, 10);
 auto child = parent.fork();
 auto sibling = parent.fork();
 child.invalidateNodeAndDescendants(node);
 const auto version = child.nodeVersion(node);
 CSelector throwing = [&](const Bonus * value) -> bool
 {
  if(selected(value))
  {
   EXPECT_EQ(value->val, 10);
   const_cast<Bonus *>(value)->val = 999;
   throw std::runtime_error("derived selector failure");
  }
  return false;
 };
 EXPECT_THROW(child.queryMembership(node, throwing, "failed"), std::runtime_error);
 EXPECT_EQ(child.nodeVersion(node), version);
 const auto failed = child.evaluation();
 EXPECT_FALSE(failed.graph().nodeCaches().at(node).valid);
 EXPECT_FALSE(failed.graph().nodeCaches().at(node).requests.contains("failed"));
 for(auto * branch : {&parent, &child, &sibling})
 {
  const auto retained = branch->resolveMembership(membership);
  ASSERT_EQ(retained->size(), 1);
  EXPECT_EQ(retained->front()->val, 10);
  const auto recomputed = branch->query(node, selected, "failed");
  ASSERT_EQ(recomputed->size(), 1);
  EXPECT_EQ(recomputed->front()->val, 10);
 }
 EXPECT_EQ(source->getAllBonuses(selected)->front()->val, 10);
}
