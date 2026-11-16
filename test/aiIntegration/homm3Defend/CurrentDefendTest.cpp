// GPL-2.0-or-later. Real server packets; current scalar, not no-proc forecast.
#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "../../../AI/Integration/Ports/HoMM3CurrentDefendWitness.h"
#include "../../../AI/Integration/Ports/HoMM3NativeRangedCensus.h"
#include "../../../AI/Integration/Ports/HoMM3SpellPolicy.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/networkPacks/StackLocation.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/callback/CCallback.h"
#include "../../../lib/callback/AIFactory.h"
#include "../../../lib/callback/CBattleGameInterface.h"
#define PinnedExchange CurrentDefendPinnedExchange
#include "../homm3Exchange/PinnedExchange.h"
#undef PinnedExchange
#include <vcmi/Environment.h>
using namespace AIIntegration::Ports::HoMM3;
namespace
{
class CurrentRecorder : public RecordingGameServer
{
public:
 CurrentDefendSession * observer=nullptr;
 CBattleGameInterface * backend=nullptr;
 std::optional<BattleAction> current;
 std::optional<SetStackEffect> lastEffect;
 bool omitStart=false,duplicateEffect=false;
 std::vector<uint32_t> activations;
 void applyPack(CPackForClient & p) override
 {
  if(auto * s=dynamic_cast<StartAction *>(&p))
  {current=s->ba;if(observer && !omitStart) observer->start(s->ba);if(backend) backend->actionStarted(s->battleID,s->ba);}
  if(observer && (dynamic_cast<BattleAttack *>(&p) || dynamic_cast<StacksInjured *>(&p)
   || dynamic_cast<BattleStackMoved *>(&p) || dynamic_cast<BattleTriggerEffect *>(&p)
   || dynamic_cast<BattleSpellCast *>(&p) || dynamic_cast<BattleUnitsChanged *>(&p)
   || dynamic_cast<BattleObstaclesChanged *>(&p) || dynamic_cast<BattleNextRound *>(&p))) observer->invalidate();
  RecordingGameServer::applyPack(p);
  if(auto * a=dynamic_cast<BattleSetActiveStack *>(&p)) activations.push_back(a->stack);
  if(auto * e=dynamic_cast<SetStackEffect *>(&p))
  {lastEffect=*e;if(observer) {observer->effects(*e);if(duplicateEffect) observer->effects(*e);}if(backend) backend->battleStacksEffectsSet(e->battleID,*e);}
  if(auto * end=dynamic_cast<EndAction *>(&p);end && current) {if(observer) observer->finish(*current);if(backend) backend->actionFinished(end->battleID,*current);current.reset();}
 }
};
class CurrentDecisionCallback : public CBattleCallback
{
public:
 explicit CurrentDecisionCallback(PlayerColor player):CBattleCallback(player,nullptr){}
 std::optional<BattleAction> selected;bool spell=false;
 std::function<void()> duringGetBattle;
 std::shared_ptr<CPlayerBattleCallback> getBattle(const BattleID & id) override
 {
  if(duringGetBattle) {auto call=std::move(duringGetBattle);duringGetBattle={};call();}
  return CBattleCallback::getBattle(id);
 }
 void battleMakeUnitAction(const BattleID &,const BattleAction & a) override {selected=a;spell=false;}
 void battleMakeSpellAction(const BattleID &,const BattleAction & a) override {selected=a;spell=true;}
 void battleMakeTacticAction(const BattleID &,const BattleAction & a) override {selected=a;spell=false;}
 std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID &,const BattleStateInfoForRetreat &) override {return std::nullopt;}
};
class CurrentEnvironment : public Environment
{
 std::shared_ptr<CCallback> gameCb;
 std::shared_ptr<CBattleCallback> battleCb;
public:
 CurrentEnvironment(std::shared_ptr<CCallback> g,std::shared_ptr<CBattleCallback> b):gameCb(std::move(g)),battleCb(std::move(b)){}
 const Services * services() const override {return LIBRARY;}
 const GameCb * game() const override {return gameCb.get();}
 const BattleCb * battle(const BattleID & id) const override {return battleCb->getBattle(id).get();}
};
}
class NativeCurrentDefendTest : public BattleTestFixture
{
protected:
 CurrentRecorder recorder;
 std::shared_ptr<CurrentDecisionCallback> callback;
 std::shared_ptr<CBattleGameInterface> controller;
 std::shared_ptr<CPlayerBattleCallback> view;
 std::unique_ptr<CurrentDefendSession> session;
 CStack * shooter=nullptr,*peer=nullptr,*enemy=nullptr;
 void prepare(TerrainId terrain=ETerrainId::SAND,int heroDefense=0,int peerCreature=0)
 {
  startGame();attackerSideHero->setPrimarySkill(PrimarySkill::DEFENSE,heroDefense,ChangeValueMode::ABSOLUTE);gameHandler.reset();recorder.gameState=gameState();
  gameHandler=std::make_shared<CGameHandler>(recorder,gameState());gameHandler->randomizer->setSeed(seed);
  ASSERT_TRUE(gameHandler->changeStackType(StackLocation(attackerSideHero->id,SlotID(0)),CreatureID(2).toCreature()));
  ASSERT_TRUE(gameHandler->changeStackCount(StackLocation(attackerSideHero->id,SlotID(0)),150,ChangeValueMode::ABSOLUTE));
  ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(attackerSideHero->id,SlotID(1)),CreatureID(peerCreature).toCreature(),10));
  ASSERT_TRUE(gameHandler->changeStackType(StackLocation(defenderSideHero->id,SlotID(0)),CreatureID(6).toCreature()));
  ASSERT_TRUE(gameHandler->changeStackCount(StackLocation(defenderSideHero->id,SlotID(0)),15,ChangeValueMode::ABSOLUTE));
  startBattle(terrain);
  for(auto & u:battle()->stacks)
   if(u->unitSide()==BattleSide::DEFENDER) enemy=u.get();
   else if(u->unitSlot()==SlotID(0)) shooter=u.get(); else peer=u.get();
  ASSERT_NE(peer,nullptr);ASSERT_NE(shooter,nullptr);ASSERT_NE(enemy,nullptr);
  shooter->setPosition(BattleHex(86));peer->setPosition(BattleHex(35));enemy->setPosition(BattleHex(94));
  callback=std::make_shared<CurrentDecisionCallback>(attackerSideHero->getOwner());
  callback->onBattleStarted(battle());view=callback->getBattle(battle()->getBattleID());
  session=std::make_unique<CurrentDefendSession>(callback,battle()->getBattleID());recorder.observer=session.get();
 }
 void observeDefend(CStack * u=nullptr) {ASSERT_TRUE(act(BattleAction::makeDefend(u?u:peer)));}
 void TearDown() override {recorder.backend=nullptr;controller.reset();recorder.observer=nullptr;session.reset();callback.reset();BattleTestFixture::TearDown();}
};
TEST_F(NativeCurrentDefendTest, RealDefendCurrentDefenseAndStatelessRejection)
{
 prepare();ASSERT_FALSE(HasFatalFailure());EXPECT_FALSE(session->capture());
 const int d=peer->getDefense(false);observeDefend();ASSERT_FALSE(HasFatalFailure());
 const auto witness=session->capture();ASSERT_TRUE(witness);
 EXPECT_EQ(witness->bonus(*view,*peer),std::optional<int32_t>(std::max(d/5,1)));
 EXPECT_FALSE(NativeExchangeFacts::modifiers(*view,*peer));
 const auto facts=NativeExchangeFacts::unit(*view,*peer,0,0,&*witness);ASSERT_TRUE(facts);
 EXPECT_EQ(peer->getDefense(false),d+std::max(d/5,1));
}
TEST_F(NativeCurrentDefendTest, MissingStartCannotCertifyExistingStance)
{prepare();ASSERT_FALSE(HasFatalFailure());recorder.omitStart=true;observeDefend();EXPECT_FALSE(session->capture());}
TEST_F(NativeCurrentDefendTest, DuplicateExpectedEffectInvalidates)
{prepare();ASSERT_FALSE(HasFatalFailure());recorder.duplicateEffect=true;observeDefend();EXPECT_FALSE(session->capture());}
TEST_F(NativeCurrentDefendTest, DuplicateEndInvalidatesAndRevokesSnapshot)
{prepare();ASSERT_FALSE(HasFatalFailure());observeDefend();auto w=session->capture();ASSERT_TRUE(w);session->finish(BattleAction::makeDefend(peer));EXPECT_FALSE(session->capture());EXPECT_FALSE(w->bonus(*view,*peer));}
TEST_F(NativeCurrentDefendTest, InterveningActionInvalidates)
{prepare();ASSERT_FALSE(HasFatalFailure());observeDefend();ASSERT_TRUE(session->capture());session->start(BattleAction::makeWait(shooter));EXPECT_FALSE(session->capture());}
TEST_F(NativeCurrentDefendTest, UnexpectedEffectInvalidates)
{prepare();ASSERT_FALSE(HasFatalFailure());observeDefend();ASSERT_TRUE(session->capture());SetStackEffect e;e.battleID=battle()->getBattleID();session->effects(e);EXPECT_FALSE(session->capture());}
TEST_F(NativeCurrentDefendTest, RoundChangeRejectsEvenIfCallbackMissing)
{prepare();ASSERT_FALSE(HasFatalFailure());observeDefend();ASSERT_TRUE(session->capture());battle()->nextRound();EXPECT_FALSE(session->capture());}
TEST_F(NativeCurrentDefendTest, PreviewMismatchDoesNotRevokeRealWitness)
{
 prepare();ASSERT_FALSE(HasFatalFailure());observeDefend();auto w=session->capture();ASSERT_TRUE(w);
 CurrentEnvironment env(makeCallback(attackerSideHero->getOwner()),callback);
 auto preview=std::make_shared<HypotheticBattle>(&env,view);
 EXPECT_TRUE(w->bonus(*preview,*preview->battleGetUnitByID(peer->unitId())));
 preview->addUnitBonus(peer->unitId(),{Bonus(BonusDuration::PERMANENT,BonusType::PRIMARY_SKILL,BonusSource::OTHER,1,BonusSourceID(),BonusSubtypeID(PrimarySkill::DEFENSE))});
 EXPECT_FALSE(w->bonus(*preview,*preview->battleGetUnitByID(peer->unitId())));EXPECT_TRUE(w->bonus(*view,*peer));EXPECT_TRUE(session->capture());
}
TEST_F(NativeCurrentDefendTest, BaselineCensusMatchesPinnedOracleAfterRealDefend)
{
 prepare();ASSERT_FALSE(HasFatalFailure());observeDefend();auto w=session->capture();ASSERT_TRUE(w);
 const auto census=captureNativeRangedCensus(*view,BattleSide::ATTACKER,false,&*w);ASSERT_TRUE(census);
 EXPECT_FALSE(captureNativeRangedCensus(*view,BattleSide::ATTACKER));
 ASSERT_TRUE(census->rows[0]);ASSERT_TRUE(census->rows[0]->target);
 namespace Ref=CurrentDefendPinnedExchange;
 Ref::manager={};Ref::hero hero;Ref::type_AI_combat_parameters params;
 params.m_lowestAttack=0;params.m_lowestDefense=0;
 Ref::akCreatureTypeTraits[2]={static_cast<double>(CreatureID(2).toCreature()->getFightValue()),6,3};
 Ref::akCreatureTypeTraits[6]={static_cast<double>(CreatureID(6).toCreature()->getFightValue()),10,12};
 Ref::akCreatureTypeTraits[0]={static_cast<double>(CreatureID(0).toCreature()->getFightValue()),4,5};
 Ref::army actor,target,defended;
 actor.m_creatureType=2;actor.controller=&hero;actor.flags=Ref::creatureShootingArmy;actor.shooting=true;
 actor.m_numTroops=150;actor.m_monInfo.hitPoints=10;actor.m_monInfo.damageLowBound=2;actor.m_monInfo.damageHighBound=3;actor.m_monInfo.attackSkill=6;actor.m_monInfo.defenseSkill=3;
 target.m_creatureType=6;target.controller=&hero;target.m_numTroops=15;target.m_monInfo.hitPoints=35;target.m_monInfo.damageLowBound=6;target.m_monInfo.damageHighBound=9;target.m_monInfo.attackSkill=10;target.m_monInfo.defenseSkill=12;
 defended.m_creatureType=0;defended.controller=&hero;defended.m_numTroops=10;defended.m_monInfo.hitPoints=10;defended.m_monInfo.damageLowBound=1;defended.m_monInfo.damageHighBound=3;defended.m_monInfo.attackSkill=4;defended.m_monInfo.defenseSkill=6; // original D5 + fixed B1
 Ref::manager.distancePenalty=false;
 EXPECT_EQ(census->rows[0]->target->exchangeValue,params.getSimpleAttackEffect(actor,target,true,1));
 const auto f=NativeExchangeFacts::unit(*view,*peer,0,0,&*w);ASSERT_TRUE(f);
 EXPECT_DOUBLE_EQ(f->lossUnitValue[0],defended.getUnitCombatValue(0,0,false,nullptr));
 EXPECT_DOUBLE_EQ(f->lossUnitValue[1],defended.getUnitCombatValue(0,0,true,nullptr));
}
TEST_F(NativeCurrentDefendTest, RealDefendThenRayOnUndefendedEnemySelectedAndAccepted)
{
 prepare();ASSERT_FALSE(HasFatalFailure());
 for(const auto & spell:LIBRARY->spellh->objects) attackerSideHero->removeSpellFromSpellbook(spell->id);
 teachSpell(attackerSideHero,SpellID::DISRUPTING_RAY);setMagicMastery(attackerSideHero,MasteryLevel::NONE);
 enemy->setTurnStatus(battle::UnitTurnStatus::ACTED);
 recorder.activations.clear();observeDefend();ASSERT_FALSE(HasFatalFailure());
 ASSERT_FALSE(recorder.activations.empty());EXPECT_EQ(recorder.activations.back(),shooter->unitId());
 ASSERT_EQ(battle()->activeStack,shooter->unitId());
 auto w=session->capture();ASSERT_TRUE(w);
 CurrentEnvironment env(makeCallback(attackerSideHero->getOwner()),callback);
 EXPECT_FALSE(AIIntegration::HoMM3SpellPolicy::choose(view,&env,*shooter));
 const auto selected=AIIntegration::HoMM3SpellPolicy::choose(view,&env,*shooter,{},false,&*w);
 ASSERT_TRUE(selected);EXPECT_EQ(selected->spell,SpellID(SpellID::DISRUPTING_RAY));
 const auto targets=selected->getTarget(view.get());ASSERT_FALSE(targets.empty());
 EXPECT_TRUE(enemy->getHexes().contains(targets.front().hexValue));
 const auto mana=attackerSideHero->mana;const auto before=enemy->getDefense(false);
 ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),attackerSideHero->getOwner(),*selected));
 EXPECT_LT(attackerSideHero->mana,mana);EXPECT_LT(enemy->getDefense(false),before);
 EXPECT_FALSE(session->capture());EXPECT_FALSE(w->bonus(*view,*peer));
}
TEST_F(NativeCurrentDefendTest, DefendedRayTargetRemainsExplicitlyRejected)
{
 prepare();ASSERT_FALSE(HasFatalFailure());observeDefend(enemy);auto w=session->capture();ASSERT_TRUE(w);
 peer->setTurnStatus(battle::UnitTurnStatus::ACTED);
 for(const auto & spell:LIBRARY->spellh->objects) attackerSideHero->removeSpellFromSpellbook(spell->id);
 teachSpell(attackerSideHero,SpellID::DISRUPTING_RAY);setMagicMastery(attackerSideHero,MasteryLevel::NONE);
 CurrentEnvironment env(makeCallback(attackerSideHero->getOwner()),callback);battle()->activeStack=shooter->unitId();
 const auto baseline=captureNativeRangedCensus(*view,BattleSide::ATTACKER,false,&*w);ASSERT_TRUE(baseline);ASSERT_TRUE(baseline->rows[0]);
 EXPECT_FALSE(AIIntegration::HoMM3SpellPolicy::choose(view,&env,*shooter,{},false,&*w));
 EXPECT_TRUE(session->capture());
}
TEST_F(NativeCurrentDefendTest, NestedStartAndMismatchedEndCannotCertify)
{
 prepare();ASSERT_FALSE(HasFatalFailure());session->start(BattleAction::makeDefend(peer));
 session->start(BattleAction::makeDefend(peer));ASSERT_FALSE(session->capture());
 session->finish(BattleAction::makeDefend(shooter));EXPECT_FALSE(session->capture());
 observeDefend();ASSERT_TRUE(session->capture());
 session->finish(BattleAction::makeWait(peer));EXPECT_FALSE(session->capture());
}
TEST_F(NativeCurrentDefendTest, ExtraActorAndMalformedEffectCannotCertify)
{
 prepare();ASSERT_FALSE(HasFatalFailure());observeDefend();ASSERT_TRUE(session->capture());ASSERT_TRUE(recorder.lastEffect);
 auto e=*recorder.lastEffect;e.toUpdate.emplace_back(shooter->unitId(),e.toUpdate.front().second);
 session->effects(e);EXPECT_FALSE(session->capture());
}
TEST_F(NativeCurrentDefendTest, ChangedRealDefenseRejectsCurrentWitness)
{
 prepare();ASSERT_FALSE(HasFatalFailure());observeDefend();auto w=session->capture();ASSERT_TRUE(w);
 peer->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::PRIMARY_SKILL,BonusSource::OTHER,1,BonusSourceID(),BonusSubtypeID(PrimarySkill::DEFENSE)));
 EXPECT_FALSE(session->capture());EXPECT_FALSE(w->bonus(*view,*peer));
}
TEST_F(NativeCurrentDefendTest, InactiveConditionalDefenseBecomingActiveRejectsCapture)
{
 prepare();ASSERT_FALSE(HasFatalFailure());
 const int d=peer->getDefense(false);
 auto conditional=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::PRIMARY_SKILL,BonusSource::OTHER,2,BonusSourceID(),BonusSubtypeID(PrimarySkill::DEFENSE));
 conditional->limiter=std::make_shared<HasAnotherBonusLimiter>(BonusType::MAGIC_MIRROR);
 peer->addNewBonus(conditional);
 ASSERT_EQ(peer->getDefense(false),d);ASSERT_TRUE(NativeDefendFacts::extract(*peer,1));
 observeDefend();auto w=session->capture();ASSERT_TRUE(w);
 peer->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_MIRROR,BonusSource::OTHER,1,BonusSourceID()));
 ASSERT_GT(peer->getDefense(false),d+std::max(d/5,1));
 EXPECT_FALSE(session->capture());EXPECT_FALSE(w->bonus(*view,*peer));
}
TEST_F(NativeCurrentDefendTest, RealControllerCallbackChainSelectsBaselineRay)
{
 prepare();ASSERT_FALSE(HasFatalFailure());
 for(const auto & spell:LIBRARY->spellh->objects) attackerSideHero->removeSpellFromSpellbook(spell->id);
 teachSpell(attackerSideHero,SpellID::DISRUPTING_RAY);setMagicMastery(attackerSideHero,MasteryLevel::NONE);
 enemy->setTurnStatus(battle::UnitTurnStatus::ACTED);
 auto env=std::make_shared<CurrentEnvironment>(makeCallback(attackerSideHero->getOwner()),callback);
 controller=AIFactory::createBattleAI("HoMM3OriginalTactics");controller->initBattleInterface(env,callback);
 controller->battleStart(battle()->getBattleID(),attackerSideHero,defenderSideHero,int3(),attackerSideHero,defenderSideHero,BattleSide::ATTACKER,false);
 recorder.backend=controller.get();observeDefend();ASSERT_FALSE(HasFatalFailure());ASSERT_EQ(battle()->activeStack,shooter->unitId());
 controller->activeStack(battle()->getBattleID(),shooter);
 ASSERT_TRUE(callback->selected);ASSERT_TRUE(callback->spell);EXPECT_EQ(callback->selected->spell,SpellID(SpellID::DISRUPTING_RAY));
 const auto selected=*callback->selected;const auto before=enemy->getDefense(false);
 ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),attackerSideHero->getOwner(),selected));
 EXPECT_LT(enemy->getDefense(false),before);
}
TEST_F(NativeCurrentDefendTest, RealControllerUnexpectedTriggerInvalidatesBaselineRay)
{
 prepare();ASSERT_FALSE(HasFatalFailure());
 for(const auto & spell:LIBRARY->spellh->objects) attackerSideHero->removeSpellFromSpellbook(spell->id);
 teachSpell(attackerSideHero,SpellID::DISRUPTING_RAY);setMagicMastery(attackerSideHero,MasteryLevel::NONE);
 enemy->setTurnStatus(battle::UnitTurnStatus::ACTED);
 auto env=std::make_shared<CurrentEnvironment>(makeCallback(attackerSideHero->getOwner()),callback);
 controller=AIFactory::createBattleAI("HoMM3OriginalTactics");controller->initBattleInterface(env,callback);
 controller->battleStart(battle()->getBattleID(),attackerSideHero,defenderSideHero,int3(),attackerSideHero,defenderSideHero,BattleSide::ATTACKER,false);
 recorder.backend=controller.get();observeDefend();ASSERT_FALSE(HasFatalFailure());ASSERT_EQ(battle()->activeStack,shooter->unitId());
 controller->activeStack(battle()->getBattleID(),shooter);
 ASSERT_TRUE(callback->selected);ASSERT_TRUE(callback->spell); // accepted baseline, no send
 callback->selected.reset();callback->spell=false;
 BattleTriggerEffect event;event.battleID=battle()->getBattleID();event.stackID=peer->unitId();
 controller->battleTriggerEffect(battle()->getBattleID(),event);
 controller->activeStack(battle()->getBattleID(),shooter);
 ASSERT_TRUE(callback->selected);EXPECT_FALSE(callback->spell);
}

class NativeTerrainDefendMatrix : public NativeCurrentDefendTest, public ::testing::WithParamInterface<std::tuple<bool,int,int,int,int>> {};
TEST_P(NativeTerrainDefendMatrix, OriginalTroopRealDefendHasSourceTerrainBeforeFixedBonus)
{
 const auto [grass,heroDefense,creature,expectedD,expectedB]=GetParam();
 prepare(grass?ETerrainId::GRASS:ETerrainId::SAND,heroDefense,creature);ASSERT_FALSE(HasFatalFailure());
 const bool native=grass && creature!=28;
 EXPECT_EQ(peer->unitType()->isNativeTerrain(view->battleTerrainType()),native);
 const auto pre=NativeDefendFacts::extract(*view,*peer,1);ASSERT_TRUE(pre);
 EXPECT_EQ(pre->underlyingDefense,expectedD);EXPECT_EQ(peer->getDefense(false),expectedD);
 EXPECT_EQ(static_cast<bool>(NativeDefendFacts::extract(*peer,1)),!native);
 observeDefend();ASSERT_FALSE(HasFatalFailure());auto w=session->capture();ASSERT_TRUE(w);
 const auto post=NativeDefendFacts::extract(*view,*peer,1);ASSERT_TRUE(post);
 EXPECT_EQ(post->underlyingDefense,expectedD);EXPECT_EQ(post->displayedDefense,expectedD+expectedB);
 EXPECT_EQ(w->bonus(*view,*peer),std::optional<int32_t>(expectedB));
 EXPECT_EQ(post->stance,expectedD<5?DefendUnitFacts::Stance::CanonicalAddOneZeroExtra:DefendUnitFacts::Stance::CanonicalPercent20ZeroExtra);
}
INSTANTIATE_TEST_SUITE_P(SourceThresholds,NativeTerrainDefendMatrix,::testing::Values(
 std::tuple{false,4,0,9,1},std::tuple{true,4,0,10,2},
 std::tuple{false,1,2,4,1},std::tuple{true,1,2,5,1},
 std::tuple{false,0,28,3,1},std::tuple{true,0,28,3,1},
 std::tuple{false,0,0,5,1},std::tuple{true,0,0,6,1},std::tuple{true,0,2,4,1}));
namespace
{
// Fixture-only effective-record source. Native CUnitStateDetached computes values;
// this does NOT claim server limiter evaluation/import support for malformed data.
// Baseline is copied from the actual native unit, then exactly one metadata record
// is replaced. No production preview component-import guard is bypassed.
class EffectiveTerrainRecords final : public IBonusBearer
{
public:
 std::vector<std::shared_ptr<Bonus>> values;
 explicit EffectiveTerrainRecords(const battle::Unit & unit)
 {const auto all=unit.getAllBonuses(Selector::all);for(const auto & b:*all) values.push_back(std::make_shared<Bonus>(*b));}
 TConstBonusListPtr getAllBonuses(const CSelector & selector,const std::string & = "") const override
 {auto result=std::make_shared<BonusList>();for(const auto & b:values) if(selector(b.get())) result->push_back(b);return result;}
 int32_t getTreeVersion() const override {return 1;}
};
class WrongTerrainContext : public CPlayerBattleCallback
{
 TerrainId terrain;
public:
 WrongTerrainContext(const CPlayerBattleCallback & real,TerrainId value)
  :CPlayerBattleCallback(real.getBattle(),*real.getPlayerID()),terrain(value){}
 TerrainId battleTerrainType() const override {return terrain;}
};
}
TEST_F(NativeCurrentDefendTest, TerrainWitnessRejectsWrongContextAndPreservesLivePreview)
{
 prepare(ETerrainId::GRASS,4);ASSERT_FALSE(HasFatalFailure());observeDefend();auto w=session->capture();ASSERT_TRUE(w);
 WrongTerrainContext wrong(*view,ETerrainId::SAND);
 EXPECT_FALSE(NativeDefendFacts::extract(wrong,*peer,1));EXPECT_FALSE(w->bonus(wrong,*peer));
 CurrentEnvironment env(makeCallback(attackerSideHero->getOwner()),callback);auto preview=std::make_shared<HypotheticBattle>(&env,view);
 EXPECT_EQ(w->bonus(*preview,*preview->battleGetUnitByID(peer->unitId())),std::optional<int32_t>(2));
 preview->addUnitBonus(peer->unitId(),{Bonus(BonusDuration::PERMANENT,BonusType::PRIMARY_SKILL,BonusSource::OTHER,1,BonusSourceID(),BonusSubtypeID(PrimarySkill::DEFENSE))});
 EXPECT_FALSE(w->bonus(*preview,*preview->battleGetUnitByID(peer->unitId())));
 EXPECT_EQ(w->bonus(*view,*peer),std::optional<int32_t>(2));EXPECT_TRUE(session->capture());
 session->invalidate();EXPECT_FALSE(w->bonus(*view,*peer));
}
TEST_F(NativeCurrentDefendTest, NonnativeWitnessStillRequiresCapturedTerrainContext)
{
 prepare(ETerrainId::GRASS,0,28);ASSERT_FALSE(HasFatalFailure());observeDefend();auto w=session->capture();ASSERT_TRUE(w);
 WrongTerrainContext wrong(*view,ETerrainId::SAND);
 ASSERT_TRUE(NativeDefendFacts::extract(wrong,*peer,1)); // no terrain record, same current scalar
 EXPECT_FALSE(w->bonus(wrong,*peer));EXPECT_TRUE(w->bonus(*view,*peer));
}
class NativeTerrainDefendMalformed : public NativeCurrentDefendTest,public ::testing::WithParamInterface<int> {};
TEST_P(NativeTerrainDefendMalformed, EffectiveMalformedTerrainReplacementRejected)
{
 prepare(ETerrainId::GRASS);ASSERT_FALSE(HasFatalFailure());ASSERT_TRUE(NativeDefendFacts::extract(*view,*peer,1));
 const auto records=peer->getAllBonuses(Selector::typeSubtype(BonusType::PRIMARY_SKILL,BonusSubtypeID(PrimarySkill::DEFENSE)));
 const Bonus * canonical=nullptr;for(const auto & b:*records) if(b->source==BonusSource::TERRAIN_NATIVE) canonical=b.get();
 ASSERT_NE(canonical,nullptr);ASSERT_TRUE(NativeTerrainFacts::canonical(*view,*peer,*canonical));
 Bonus changed=*canonical;
 switch(GetParam())
 {
 case 0:changed.val=2;break;
 case 1:changed.sid=BonusSourceID(CreatureID(0));break;
 case 2:changed.duration=BonusDuration::PERMANENT;break;
 case 3:changed.turnsRemain=1;break;
 case 4:changed.valType=BonusValueType::BASE_NUMBER;break;
 case 5:changed.limiter.reset();break;
 case 6:{auto all=std::make_shared<AllOfLimiter>();all->add(std::make_shared<TerrainLimiter>());changed.limiter=all;break;}
 case 7:{auto all=std::make_shared<AllOfLimiter>();all->add(std::make_shared<TerrainLimiter>());all->add(std::make_shared<CreatureLevelLimiter>());all->add(std::make_shared<TerrainLimiter>());changed.limiter=all;break;}
 case 8:{auto all=std::make_shared<AllOfLimiter>();all->add(std::make_shared<TerrainLimiter>(ETerrainId::GRASS));all->add(std::make_shared<CreatureLevelLimiter>());changed.limiter=all;break;}
 case 9:{auto all=std::make_shared<AllOfLimiter>();all->add(std::make_shared<TerrainLimiter>());all->add(std::make_shared<CreatureLevelLimiter>(1));changed.limiter=all;break;}
 case 10:changed.bonusOwner=attackerSideHero->getOwner();break;
 case 11:changed.effectRange=BonusLimitEffect::ONLY_MELEE_FIGHT;break;
 case 12:changed.stacking="malformed-terrain";break;
 default:FAIL()<<"unknown case";
 }
 EXPECT_FALSE(NativeTerrainFacts::canonical(*view,*peer,changed));
 EffectiveTerrainRecords baseline(*peer);
 battle::CUnitStateDetached baselineUnit(peer,&baseline);baselineUnit=*peer;
 ASSERT_TRUE(NativeDefendFacts::extract(*view,baselineUnit,1));
 EffectiveTerrainRecords modified(*peer);
 int replaced=0;
 for(auto & b:modified.values)
  if(b->source==BonusSource::TERRAIN_NATIVE && b->type==BonusType::PRIMARY_SKILL && b->subtype==BonusSubtypeID(PrimarySkill::DEFENSE)) {b=std::make_shared<Bonus>(changed);++replaced;}
 ASSERT_EQ(replaced,1);
 battle::CUnitStateDetached unit(peer,&modified);unit=*peer;
 const auto effective=unit.getAllBonuses(Selector::typeSubtype(BonusType::PRIMARY_SKILL,BonusSubtypeID(PrimarySkill::DEFENSE)));
 ASSERT_EQ(std::count_if(effective->begin(),effective->end(),[](const auto & b){return b->source==BonusSource::TERRAIN_NATIVE;}),1);
 EXPECT_FALSE(NativeDefendFacts::extract(*view,unit,1));
 EXPECT_TRUE(NativeDefendFacts::extract(*view,*peer,1));
}
INSTANTIATE_TEST_SUITE_P(ExactShape,NativeTerrainDefendMalformed,::testing::Range(0,13));
TEST_F(NativeCurrentDefendTest, TerrainDuplicateAndMissingPairRejected)
{
 prepare(ETerrainId::GRASS);ASSERT_FALSE(HasFatalFailure());ASSERT_TRUE(NativeDefendFacts::extract(*view,*peer,1));
 auto defense=peer->getBonus(Selector::source(BonusSource::TERRAIN_NATIVE,BonusSourceID()).And(Selector::typeSubtype(BonusType::PRIMARY_SKILL,BonusSubtypeID(PrimarySkill::DEFENSE))));
 auto attack=peer->getBonus(Selector::source(BonusSource::TERRAIN_NATIVE,BonusSourceID()).And(Selector::typeSubtype(BonusType::PRIMARY_SKILL,BonusSubtypeID(PrimarySkill::ATTACK))));
 ASSERT_TRUE(defense);ASSERT_TRUE(attack);
 EffectiveTerrainRecords duplicate(*peer);duplicate.values.push_back(std::make_shared<Bonus>(*defense));
 battle::CUnitStateDetached duplicateUnit(peer,&duplicate);duplicateUnit=*peer;
 EXPECT_FALSE(NativeDefendFacts::extract(*view,duplicateUnit,1));
 EffectiveTerrainRecords unpaired(*peer);
 std::erase_if(unpaired.values,[](const auto & b){return b->source==BonusSource::TERRAIN_NATIVE && b->type==BonusType::PRIMARY_SKILL && b->subtype==BonusSubtypeID(PrimarySkill::ATTACK);});
 battle::CUnitStateDetached unpairedUnit(peer,&unpaired);unpairedUnit=*peer;
 EXPECT_FALSE(NativeDefendFacts::extract(*view,unpairedUnit,1));
}
TEST_F(NativeCurrentDefendTest, NativeTerrainDoesNotAdmitOtherConditionalDefense)
{
 prepare(ETerrainId::GRASS);ASSERT_FALSE(HasFatalFailure());
 auto conditional=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::PRIMARY_SKILL,BonusSource::OTHER,2,BonusSourceID(),BonusSubtypeID(PrimarySkill::DEFENSE));
 conditional->limiter=std::make_shared<HasAnotherBonusLimiter>(BonusType::MAGIC_MIRROR);peer->addNewBonus(conditional);
 ASSERT_TRUE(NativeDefendFacts::extract(*view,*peer,1));observeDefend();auto w=session->capture();ASSERT_TRUE(w);
 const auto old=peer->getDefense(false);peer->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_MIRROR,BonusSource::OTHER,1,BonusSourceID()));
 ASSERT_GT(peer->getDefense(false),old);EXPECT_FALSE(session->capture());EXPECT_FALSE(w->bonus(*view,*peer));
}
TEST_F(NativeCurrentDefendTest, NativeGrassDefendSourceOracleAndControllerRay)
{
 prepare(ETerrainId::GRASS,4);ASSERT_FALSE(HasFatalFailure());
 for(const auto & spell:LIBRARY->spellh->objects) attackerSideHero->removeSpellFromSpellbook(spell->id);
 teachSpell(attackerSideHero,SpellID::DISRUPTING_RAY);setMagicMastery(attackerSideHero,MasteryLevel::NONE);
 enemy->setTurnStatus(battle::UnitTurnStatus::ACTED);
 auto env=std::make_shared<CurrentEnvironment>(makeCallback(attackerSideHero->getOwner()),callback);
 controller=AIFactory::createBattleAI("HoMM3OriginalTactics");controller->initBattleInterface(env,callback);
 controller->battleStart(battle()->getBattleID(),attackerSideHero,defenderSideHero,int3(),attackerSideHero,defenderSideHero,BattleSide::ATTACKER,false);
 recorder.backend=controller.get();observeDefend();ASSERT_FALSE(HasFatalFailure());ASSERT_EQ(battle()->activeStack,shooter->unitId());
 auto w=session->capture();ASSERT_TRUE(w);ASSERT_EQ(peer->getDefense(false),12);EXPECT_EQ(w->bonus(*view,*peer),std::optional<int32_t>(2));
 const auto census=captureNativeRangedCensus(*view,BattleSide::ATTACKER,false,&*w);ASSERT_TRUE(census);ASSERT_TRUE(census->rows[0]);ASSERT_TRUE(census->rows[0]->target);
 namespace Ref=CurrentDefendPinnedExchange;Ref::manager={};Ref::hero hero;Ref::type_AI_combat_parameters params;
 params.m_lowestAttack=1;params.m_lowestDefense=1;
 Ref::akCreatureTypeTraits[2]={static_cast<double>(CreatureID(2).toCreature()->getFightValue()),6,3};
 Ref::akCreatureTypeTraits[6]={static_cast<double>(CreatureID(6).toCreature()->getFightValue()),10,12};
 Ref::akCreatureTypeTraits[0]={static_cast<double>(CreatureID(0).toCreature()->getFightValue()),4,5};
 Ref::army actor,target,defended;
 actor.m_creatureType=2;actor.controller=&hero;actor.flags=Ref::creatureShootingArmy;actor.shooting=true;
 actor.m_numTroops=150;actor.m_monInfo.hitPoints=10;actor.m_monInfo.damageLowBound=2;actor.m_monInfo.damageHighBound=3;actor.m_monInfo.attackSkill=7;actor.m_monInfo.defenseSkill=8;
 target.m_creatureType=6;target.controller=&hero;target.m_numTroops=15;target.m_monInfo.hitPoints=35;target.m_monInfo.damageLowBound=6;target.m_monInfo.damageHighBound=9;target.m_monInfo.attackSkill=11;target.m_monInfo.defenseSkill=13;
 defended.m_creatureType=0;defended.controller=&hero;defended.m_numTroops=10;defended.m_monInfo.hitPoints=10;defended.m_monInfo.damageLowBound=1;defended.m_monInfo.damageHighBound=3;defended.m_monInfo.attackSkill=5;defended.m_monInfo.defenseSkill=12;
 Ref::manager.distancePenalty=false;
 EXPECT_EQ(census->rows[0]->target->exchangeValue,params.getSimpleAttackEffect(actor,target,true,1));
 const auto f=NativeExchangeFacts::unit(*view,*peer,1,1,&*w);ASSERT_TRUE(f);
 EXPECT_DOUBLE_EQ(f->lossUnitValue[0],defended.getUnitCombatValue(1,1,false,nullptr));
 EXPECT_DOUBLE_EQ(f->lossUnitValue[1],defended.getUnitCombatValue(1,1,true,nullptr));
 controller->activeStack(battle()->getBattleID(),shooter);ASSERT_TRUE(callback->selected);ASSERT_TRUE(callback->spell);
 EXPECT_EQ(callback->selected->spell,SpellID(SpellID::DISRUPTING_RAY));
 const auto selected=*callback->selected;const auto before=enemy->getDefense(false);
 ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),attackerSideHero->getOwner(),selected));EXPECT_LT(enemy->getDefense(false),before);
}

// Affinity tests serialize object lifetimes with join. They do NOT establish
// concurrent destruction, GUI autofight, native state or decision commit safety.
static_assert(!std::is_copy_constructible_v<CurrentDefendSession>);
static_assert(!std::is_move_constructible_v<CurrentDefendSession>);
class NativeCurrentDefendThreadGate : public NativeCurrentDefendTest,public ::testing::WithParamInterface<int> {};
TEST_P(NativeCurrentDefendThreadGate, ForeignEntryPermanentlyRevokesSessionAndOldSnapshot)
{
 prepare();ASSERT_FALSE(HasFatalFailure());observeDefend();auto witness=session->capture();ASSERT_TRUE(witness);
 ASSERT_TRUE(witness->matchesContext(*view));ASSERT_TRUE(witness->bonus(*view,*peer));ASSERT_TRUE(recorder.lastEffect);
 const auto action=BattleAction::makeDefend(peer);const auto effect=*recorder.lastEffect;
 const int entry=GetParam();
 bool foreignResult=false;
 std::thread foreign([&]
 {
  switch(entry)
  {
  case 0:foreignResult=session->capture().has_value();break;
  case 1:session->start(action);break;
  case 2:session->effects(effect);break;
  case 3:session->finish(action);break;
  case 4:session->invalidate();break;
  case 5:foreignResult=witness->matchesContext(*view);break;
  case 6:foreignResult=witness->bonus(*view,*peer).has_value();break;
  }
 });foreign.join();
 EXPECT_FALSE(foreignResult);EXPECT_FALSE(session->capture());EXPECT_FALSE(witness->matchesContext(*view));EXPECT_FALSE(witness->bonus(*view,*peer));
 session->invalidate(); // ordinary invalidation cannot reset affinity violation
 observeDefend(shooter);ASSERT_FALSE(HasFatalFailure()); // even a new real Defend cannot reset it
 EXPECT_FALSE(session->capture());EXPECT_FALSE(witness->bonus(*view,*peer));
}
INSTANTIATE_TEST_SUITE_P(EveryEntry,NativeCurrentDefendThreadGate,::testing::Range(0,7));
namespace
{
class GuardCountingContext final : public CPlayerBattleCallback
{
public:
 mutable std::atomic<int> reads{0};
 mutable std::function<void()> duringRead;
 explicit GuardCountingContext(const CPlayerBattleCallback & original):CPlayerBattleCallback(original.getBattle(),*original.getPlayerID()){}
 const IBattleInfo * getBattle() const override
 {
  ++reads;
  if(duringRead) {auto call=std::move(duringRead);duringRead={};call();}
  return CPlayerBattleCallback::getBattle();
 }
};
class GuardCountingBonuses final : public IBonusBearer
{
 const battle::Unit & original;
public:
 mutable std::atomic<int> reads{0};
 mutable std::function<void()> duringRead;
 explicit GuardCountingBonuses(const battle::Unit & u):original(u){}
 TConstBonusListPtr getAllBonuses(const CSelector & selector,const std::string & cache="") const override
 {
  ++reads;
  if(duringRead) {auto call=std::move(duringRead);duringRead={};call();}
  return original.getAllBonuses(selector,cache);
 }
 int32_t getTreeVersion() const override {++reads;return original.getTreeVersion();}
};
}
TEST_F(NativeCurrentDefendTest, ForeignSnapshotChecksReadNeitherContextNorUnit)
{
 prepare();ASSERT_FALSE(HasFatalFailure());observeDefend();auto w=session->capture();ASSERT_TRUE(w);
 GuardCountingContext context(*view);GuardCountingBonuses bonuses(*peer);
 battle::CUnitStateDetached unit(peer,&bonuses);unit=*peer;
 context.reads=0;bonuses.reads=0;
 bool result=true;
 std::thread foreign([&]{result=w->bonus(context,unit).has_value();});foreign.join();
 EXPECT_FALSE(result);EXPECT_EQ(context.reads,0);EXPECT_EQ(bonuses.reads,0);EXPECT_FALSE(session->capture());
}
TEST_F(NativeCurrentDefendTest, SnapshotPostContextCheckRejectsConcurrentRevocation)
{
 prepare();ASSERT_FALSE(HasFatalFailure());observeDefend();auto w=session->capture();ASSERT_TRUE(w);
 GuardCountingContext context(*view);
 context.duringRead=[&]{std::thread foreign([&]{session->invalidate();});foreign.join();};
 EXPECT_FALSE(w->matchesContext(context));EXPECT_GT(context.reads,0);EXPECT_FALSE(session->capture());
}
TEST_F(NativeCurrentDefendTest, SnapshotPostLeafCheckRejectsConcurrentRevocation)
{
 prepare();ASSERT_FALSE(HasFatalFailure());observeDefend();auto w=session->capture();ASSERT_TRUE(w);
 GuardCountingBonuses bonuses(*peer);battle::CUnitStateDetached unit(peer,&bonuses);unit=*peer;
 ASSERT_TRUE(w->bonus(*view,unit));
 bonuses.duringRead=[&]{std::thread foreign([&]{session->invalidate();});foreign.join();};
 EXPECT_FALSE(w->bonus(*view,unit));EXPECT_FALSE(session->capture());
}
TEST_F(NativeCurrentDefendTest, OwnerThreadInvalidateAndNewDefendRemainAvailable)
{
 prepare();ASSERT_FALSE(HasFatalFailure());observeDefend();auto old=session->capture();ASSERT_TRUE(old);
 session->invalidate();EXPECT_FALSE(old->bonus(*view,*peer));EXPECT_FALSE(session->capture());
 observeDefend(shooter);auto current=session->capture();ASSERT_TRUE(current);EXPECT_TRUE(current->bonus(*view,*shooter));
 EXPECT_FALSE(old->bonus(*view,*peer));
}
TEST_F(NativeCurrentDefendTest, CapturePostCheckRejectsRevocationDuringAuthoritativeLookup)
{
 prepare();ASSERT_FALSE(HasFatalFailure());observeDefend();auto old=session->capture();ASSERT_TRUE(old);
 bool lookupEntered=false;
 callback->duringGetBattle=[&]
 {
  lookupEntered=true;
  std::thread foreign([&]{session->invalidate();});foreign.join();
 };
 EXPECT_FALSE(session->capture());EXPECT_TRUE(lookupEntered);
 ASSERT_TRUE(NativeDefendFacts::extract(*view,*peer,1)); // scalar stayed valid; guard caused rejection
 EXPECT_FALSE(old->bonus(*view,*peer));EXPECT_FALSE(session->capture());
}

class NativeStoneSkinDefendTest : public NativeCurrentDefendTest
{
protected:
 void stone(CStack * target,int mastery=MasteryLevel::BASIC)
 {
  teachSpell(attackerSideHero,SpellID::STONE_SKIN);
  attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,4,ChangeValueMode::ABSOLUTE);
  setMagicMastery(attackerSideHero,mastery);
  ASSERT_TRUE(castOn(attackerSideHero,SpellID::STONE_SKIN,target));
 }
};
TEST_F(NativeStoneSkinDefendTest, OriginalArcherChangesWeakToPercentBranch)
{
 prepare();ASSERT_FALSE(HasFatalFailure());ASSERT_EQ(shooter->getDefense(false),3);
 stone(shooter);ASSERT_FALSE(HasFatalFailure());ASSERT_EQ(shooter->getDefense(false),6);
 ASSERT_TRUE(NativeDefendFacts::extract(*view,*shooter,1));observeDefend(shooter);
 auto w=session->capture();ASSERT_TRUE(w);EXPECT_EQ(shooter->getDefense(false),7);
 EXPECT_EQ(w->bonus(*view,*shooter),std::optional<int32_t>(1));
 EXPECT_EQ(NativeDefendFacts::extract(*view,*shooter,1)->stance,DefendUnitFacts::Stance::CanonicalPercent20ZeroExtra);
}
TEST_F(NativeStoneSkinDefendTest, ExpertBeforeDefendUsesSpellInFixedSourceBasis)
{
 prepare(ETerrainId::SAND,1);ASSERT_FALSE(HasFatalFailure());ASSERT_EQ(shooter->getDefense(false),4);
 stone(shooter,MasteryLevel::EXPERT);ASSERT_FALSE(HasFatalFailure());ASSERT_EQ(shooter->getDefense(false),10);
 observeDefend(shooter);auto w=session->capture();ASSERT_TRUE(w);
 EXPECT_EQ(shooter->getDefense(false),12);EXPECT_EQ(w->bonus(*view,*shooter),std::optional<int32_t>(2));
 const auto f=NativeExchangeFacts::unit(*view,*shooter,0,0,&*w);ASSERT_TRUE(f);
 namespace Ref=CurrentDefendPinnedExchange;Ref::hero hero;Ref::army a;
 Ref::akCreatureTypeTraits[2]={static_cast<double>(CreatureID(2).toCreature()->getFightValue()),6,3};
 a.m_creatureType=2;a.controller=&hero;a.flags=Ref::creatureShootingArmy;a.shooting=true;
 a.m_numTroops=150;a.m_monInfo.hitPoints=10;a.m_monInfo.damageLowBound=2;a.m_monInfo.damageHighBound=3;
 a.m_monInfo.attackSkill=6;a.m_monInfo.defenseSkill=12;
 for(int channel=0;channel<2;++channel) EXPECT_DOUBLE_EQ(f->lossUnitValue[channel],a.getUnitCombatValue(0,0,channel,nullptr));
}
TEST_F(NativeStoneSkinDefendTest, NativeTerrainCrossesNineToTenFixedBasis)
{
 prepare(ETerrainId::GRASS,1);ASSERT_FALSE(HasFatalFailure());ASSERT_EQ(peer->getDefense(false),7);
 stone(peer);ASSERT_FALSE(HasFatalFailure());ASSERT_EQ(peer->getDefense(false),10);
 observeDefend();auto w=session->capture();ASSERT_TRUE(w);EXPECT_EQ(peer->getDefense(false),12);
 EXPECT_EQ(w->bonus(*view,*peer),std::optional<int32_t>(2));
}
TEST_F(NativeStoneSkinDefendTest, RemainingDurationPreviewMismatchPreservesLiveWitness)
{
 prepare();ASSERT_FALSE(HasFatalFailure());stone(peer);ASSERT_FALSE(HasFatalFailure());observeDefend();
 auto w=session->capture();ASSERT_TRUE(w);
 EffectiveTerrainRecords records(*peer);bool found=false;
 for(auto & b:records.values) if(NativeSpellDefenseFacts::stoneSkin(*b)) {++b->turnsRemain;found=true;}
 ASSERT_TRUE(found);battle::CUnitStateDetached changed(peer,&records);changed=*peer;
 ASSERT_TRUE(NativeDefendFacts::extract(*view,changed,1));EXPECT_EQ(changed.getDefense(false),peer->getDefense(false));
 EXPECT_FALSE(w->bonus(*view,changed));EXPECT_TRUE(w->bonus(*view,*peer));EXPECT_TRUE(session->capture());
}
TEST_F(NativeStoneSkinDefendTest, RealDispelAfterDefendRevokesWitness)
{
 prepare();ASSERT_FALSE(HasFatalFailure());stone(peer);ASSERT_FALSE(HasFatalFailure());observeDefend();
 auto w=session->capture();ASSERT_TRUE(w);
 teachSpell(defenderSideHero,SpellID::DISPEL);setMagicMastery(defenderSideHero,MasteryLevel::EXPERT);
 ASSERT_TRUE(castOn(defenderSideHero,SpellID::DISPEL,peer));
 EXPECT_FALSE(session->capture());EXPECT_FALSE(w->bonus(*view,*peer));
}
TEST_F(NativeStoneSkinDefendTest, ExpirationAndRoundCannotRetainWitness)
{
 prepare();ASSERT_FALSE(HasFatalFailure());stone(peer);ASSERT_FALSE(HasFatalFailure());observeDefend();
 auto w=session->capture();ASSERT_TRUE(w);
 for(int i=0;i<8;++i) battle()->nextRound();
 EXPECT_FALSE(peer->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::STONE_SKIN)))));
 EXPECT_FALSE(session->capture());EXPECT_FALSE(w->bonus(*view,*peer));
}
class NativeStoneSkinDefendMalformed : public NativeStoneSkinDefendTest,public ::testing::WithParamInterface<int> {};
TEST_P(NativeStoneSkinDefendMalformed, AcceptedRealBaselineThenMalformedRecordRejected)
{
 prepare();ASSERT_FALSE(HasFatalFailure());stone(peer);ASSERT_FALSE(HasFatalFailure());
 ASSERT_TRUE(NativeDefendFacts::extract(*view,*peer,1));EffectiveTerrainRecords records(*peer);
 std::shared_ptr<Bonus> spell;for(auto & b:records.values) if(NativeSpellDefenseFacts::stoneSkin(*b)) spell=b;
 ASSERT_TRUE(spell);
 switch(GetParam())
 {
 case 0:spell->val=4;break;
 case 1:spell->duration=BonusDuration::PERMANENT;break;
 case 2:spell->turnsRemain=0;break;
 case 3:spell->turnsRemain=-1;break;
 case 4:spell->sid=BonusSourceID(SpellID(SpellID::PRAYER));break;
 case 5:spell->source=BonusSource::OTHER;break;
 case 6:spell->valType=BonusValueType::PERCENT_TO_ALL;break;
 case 7:spell->effectRange=BonusLimitEffect::ONLY_MELEE_FIGHT;break;
 case 8:spell->limiter=std::make_shared<TerrainLimiter>();break;
 case 9:spell->stacking="custom";break;
 case 10:spell->bonusOwner=attackerSideHero->getOwner();break;
 case 11:records.values.push_back(std::make_shared<Bonus>(*spell));break;
 }
 battle::CUnitStateDetached changed(peer,&records);changed=*peer;
 EXPECT_FALSE(NativeDefendFacts::extract(*view,changed,1));
}
INSTANTIATE_TEST_SUITE_P(StoneSkinRecords,NativeStoneSkinDefendMalformed,::testing::Range(0,12));
TEST_F(NativeStoneSkinDefendTest, RealPrecastDefendControllerRayOnUndefendedEnemy)
{
 prepare();ASSERT_FALSE(HasFatalFailure());stone(peer);ASSERT_FALSE(HasFatalFailure());
 // A prior real spell must not make the same-round later hero action illegal.
 // Advance one real round before creating the fresh Defend witness.
 battle()->nextRound();
 ASSERT_TRUE(NativeDefendFacts::extract(*view,*peer,1));ASSERT_EQ(peer->getDefense(false),8);
 for(const auto & spell:LIBRARY->spellh->objects) attackerSideHero->removeSpellFromSpellbook(spell->id);
 teachSpell(attackerSideHero,SpellID::DISRUPTING_RAY);setMagicMastery(attackerSideHero,MasteryLevel::NONE);
 enemy->setTurnStatus(battle::UnitTurnStatus::ACTED);
 auto env=std::make_shared<CurrentEnvironment>(makeCallback(attackerSideHero->getOwner()),callback);
 controller=AIFactory::createBattleAI("HoMM3OriginalTactics");controller->initBattleInterface(env,callback);
 controller->battleStart(battle()->getBattleID(),attackerSideHero,defenderSideHero,int3(),attackerSideHero,defenderSideHero,BattleSide::ATTACKER,false);
 recorder.backend=controller.get();observeDefend();ASSERT_FALSE(HasFatalFailure());ASSERT_EQ(battle()->activeStack,shooter->unitId());
 auto w=session->capture();ASSERT_TRUE(w);ASSERT_EQ(peer->getDefense(false),9);
 const auto census=captureNativeRangedCensus(*view,BattleSide::ATTACKER,false,&*w);ASSERT_TRUE(census);
 ASSERT_TRUE(census->rows[0]);ASSERT_TRUE(census->rows[0]->target);
 namespace Ref=CurrentDefendPinnedExchange;Ref::manager={};Ref::hero hero;Ref::type_AI_combat_parameters params;
 params.m_lowestAttack=0;params.m_lowestDefense=0;
 Ref::akCreatureTypeTraits[2]={static_cast<double>(CreatureID(2).toCreature()->getFightValue()),6,3};
 Ref::akCreatureTypeTraits[6]={static_cast<double>(CreatureID(6).toCreature()->getFightValue()),10,12};
 Ref::akCreatureTypeTraits[0]={static_cast<double>(CreatureID(0).toCreature()->getFightValue()),4,5};
 Ref::army actor,target,defended;
 actor.m_creatureType=2;actor.controller=&hero;actor.flags=Ref::creatureShootingArmy;actor.shooting=true;
 actor.m_numTroops=150;actor.m_monInfo.hitPoints=10;actor.m_monInfo.damageLowBound=2;actor.m_monInfo.damageHighBound=3;actor.m_monInfo.attackSkill=6;actor.m_monInfo.defenseSkill=3;
 target.m_creatureType=6;target.controller=&hero;target.m_numTroops=15;target.m_monInfo.hitPoints=35;target.m_monInfo.damageLowBound=6;target.m_monInfo.damageHighBound=9;target.m_monInfo.attackSkill=10;target.m_monInfo.defenseSkill=12;
 defended.m_creatureType=0;defended.controller=&hero;defended.m_numTroops=10;defended.m_monInfo.hitPoints=10;defended.m_monInfo.damageLowBound=1;defended.m_monInfo.damageHighBound=3;defended.m_monInfo.attackSkill=4;defended.m_monInfo.defenseSkill=9;
 Ref::manager.distancePenalty=false;
 EXPECT_EQ(census->rows[0]->target->exchangeValue,params.getSimpleAttackEffect(actor,target,true,1));
 const auto f=NativeExchangeFacts::unit(*view,*peer,0,0,&*w);ASSERT_TRUE(f);
 for(int channel=0;channel<2;++channel) EXPECT_DOUBLE_EQ(f->lossUnitValue[channel],defended.getUnitCombatValue(0,0,channel,nullptr));
 controller->activeStack(battle()->getBattleID(),shooter);ASSERT_TRUE(callback->selected);ASSERT_TRUE(callback->spell);
 EXPECT_EQ(callback->selected->spell,SpellID(SpellID::DISRUPTING_RAY));
 const auto selected=*callback->selected;const auto before=enemy->getDefense(false);
 ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),attackerSideHero->getOwner(),selected));
 EXPECT_LT(enemy->getDefense(false),before);EXPECT_FALSE(session->capture());
}
