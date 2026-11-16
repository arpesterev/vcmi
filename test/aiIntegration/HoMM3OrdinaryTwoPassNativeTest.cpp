/* GPL-2.0-or-later. Public native shadow capture; source oracle, not native damage parity.
 * Callbacks use real pack apply order, not the GUI/network dispatcher. */
#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "../../AI/Integration/Ports/HoMM3Forecast/NativeLifecycle.h"
#include "../../AI/Integration/Ports/HoMM3Forecast/NativeOrdinaryTwoPass.h"
#include "homm3TwoPass/PinnedTwoPass.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/StackLocation.h"
#include "../../server/CGameHandler.h"
#include "../../server/battles/BattleProcessor.h"

namespace {
namespace F=HoMM3Forecast;
namespace Ref=PinnedTwoPass;
class ShadowEventServer : public RecordingGameServer
{
public:
 std::function<void(CPackForClient&)> before,after;
 unsigned packs=0;
 void applyPack(CPackForClient& pack) override {if(before)before(pack);RecordingGameServer::applyPack(pack);++packs;if(after)after(pack);}
};
class HoMM3OrdinaryShadowNative : public BattleTestFixture
{
protected:
 ShadowEventServer eventServer;
 std::array<std::unique_ptr<CPlayerBattleCallback>,2> views;
 std::array<std::unique_ptr<F::NativeLifecycle>,2> lifecycle;
 std::array<Ref::hero,2> heroes;
 bool far=true, groundPeer=false;
 int attackBonus[2]{},defenseBonus[2]{};
 std::vector<CStack*> original;
 void prepare(int attackers=2,int defenders=2,bool distant=true,TerrainId terrain=ETerrainId::SAND)
 {
  far=distant;startGame();ASSERT_FALSE(HasFatalFailure());eventServer.gameState=gameState();
  gameHandler=std::make_shared<CGameHandler>(eventServer,gameState());gameHandler->randomizer->setSeed(seed);
  for(int side=0;side<2;++side){auto *h=side?defenderSideHero:attackerSideHero;
   h->setPrimarySkill(PrimarySkill::ATTACK,attackBonus[side],ChangeValueMode::ABSOLUTE);h->setPrimarySkill(PrimarySkill::DEFENSE,defenseBonus[side],ChangeValueMode::ABSOLUTE);
   const auto type=creatureByName(side?"core:woodElf":(groundPeer?"core:pikeman":"core:archer"));
   ASSERT_TRUE(gameHandler->changeStackType(StackLocation(h->id,SlotID(0)),type.toCreature()));
   ASSERT_TRUE(gameHandler->changeStackCount(StackLocation(h->id,SlotID(0)),20,ChangeValueMode::ABSOLUTE));
   const int count=side?defenders:attackers;
   for(int i=1;i<count;++i)ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(h->id,SlotID(i)),type.toCreature(),20+i*3));
  }
  startBattle(terrain);ASSERT_FALSE(HasFatalFailure());
  for(const auto &u:battle()->stacks){ASSERT_TRUE(u->unitSlot().validSlot());const int side=static_cast<int>(u->unitSide());u->setPosition(BattleHex((u->unitSlot().getNum()+1)*17+(side?(far?14:9):(far?2:6))));original.push_back(u.get());}
  std::sort(original.begin(),original.end(),[](const auto*a,const auto*b){return std::pair(a->unitSide(),a->unitSlot())<std::pair(b->unitSide(),b->unitSlot());});
  for(int s=0;s<2;++s){views[s]=std::make_unique<CPlayerBattleCallback>(battle(),battle()->sideToPlayer(static_cast<BattleSide>(s)));lifecycle[s]=std::make_unique<F::NativeLifecycle>(*views[s]);lifecycle[s]->battleStart(battle()->getBattleID(),nullptr,nullptr,int3(),nullptr,nullptr,static_cast<BattleSide>(s),false);}
  eventServer.before=[this](CPackForClient&p){if(auto*a=dynamic_cast<StartAction*>(&p))for(auto&l:lifecycle)l->actionStarted(a->battleID,a->ba);};
  eventServer.after=[this](CPackForClient&p){if(auto*r=dynamic_cast<BattleNextRound*>(&p))for(auto&l:lifecycle)l->battleNewRound(r->battleID);if(auto*a=dynamic_cast<BattleSetActiveStack*>(&p))if(a->reason!=BattleUnitTurnReason::AUTOMATIC_ACTION){const auto*u=views[0]->battleActiveUnit();lifecycle.at(static_cast<int>(u->controllingSide()))->ownActivation(a->battleID);}};
  beginCombat();ASSERT_FALSE(HasFatalFailure());
 }
 int activeSide() const {return static_cast<int>(views[0]->battleActiveUnit()->controllingSide());}
 std::string observation() const
 {
  std::ostringstream o;o<<eventServer.packs<<':'<<eventServer.casts.size()<<':'<<eventServer.battleResults.size()<<':'<<attackerSideHero->mana<<':'<<defenderSideHero->mana;
  for(auto*u:original){o<<'|'<<u->unitId()<<','<<u->getAvailableHealth()<<','<<u->getCount()<<','<<u->shots.available()<<','<<u->shots.total()<<','<<u->getPosition().toInt()<<','<<u->moved()<<','<<u->waited()<<','<<u->defended();const auto bonuses=u->getAllBonuses(Selector::all);for(const auto&b:*bonuses)o<<';'<<static_cast<int>(b->type)<<','<<b->subtype.getNum()<<','<<b->sid.getNum()<<','<<b->val<<','<<static_cast<int>(b->valType)<<','<<b->duration<<','<<b->turnsRemain;}
  return o.str();
 }
 void sourceOracle(const F::NativeForecastPolicy &policy,int requested,bool secondPass,bool secondPhase=false,int terrainBonus=0)
 {
  Ref::manager=Ref::combatManager{};Ref::game.m_setup.m_difficulty=policy.difficulty;Ref::manager.IsHuman=policy.human;Ref::manager.m_actingSide=activeSide();Ref::manager.secondPhase=secondPhase;Ref::manager.distancePenalty=far;
  for(int s=0;s<2;++s){heroes[s]=Ref::hero{};heroes[s].m_id=s;}
  for(const auto*u:original){const int side=static_cast<int>(u->unitSide()),index=u->unitSlot().getNum();auto&a=Ref::manager.m_armies[side][index];++Ref::manager.m_numArmies[side];a.id=u->unitId();a.index=index;a.group=side;a.m_creatureType=side;a.controller=&heroes[side];a.shooting=u->alive();a.flags=Ref::creatureShootingArmy;
   if(!u->alive())a.flags|=Ref::creatureImmobilized;
   if(u->moved()||u->defended())a.flags|=Ref::creatureDone;
   if(u->waited()&&!secondPhase)a.flags|=Ref::creatureWaiting;
   // Fixture-known ordinary content constants, checked against loaded native definitions.
   const int hp=side?15:10,lo=side?3:2,hi=side?5:3,attack=side?9:6,defense=side?5:3,speed=side?6:4;
   ASSERT_EQ(u->unitType()->getMaxHealth(),hp);ASSERT_EQ(u->unitType()->getMinDamage(false),lo);ASSERT_EQ(u->unitType()->getMaxDamage(false),hi);ASSERT_EQ(u->unitType()->getAttack(false),attack);ASSERT_EQ(u->unitType()->getDefense(false),defense);
   a.speed=speed+terrainBonus;ASSERT_EQ(u->getInitiative(0),a.speed);
   a.m_monInfo.hitPoints=hp;a.m_numTroops=u->getCount();a.m_topCreatureDamage=a.m_numTroops*hp-u->getAvailableHealth();a.m_monInfo.damageLowBound=lo;a.m_monInfo.damageHighBound=hi;a.m_monInfo.attackSkill=attack+attackBonus[side]+terrainBonus;a.m_monInfo.defenseSkill=defense+defenseBonus[side]+terrainBonus;
   Ref::akCreatureTypeTraits[side].baseFightValue=u->unitType()->getFightValue();Ref::akCreatureTypeTraits[side].attackSkill=attack;Ref::akCreatureTypeTraits[side].defenseSkill=defense;
   a.m_gridIndex=u->getPosition().toInt();if(u->alive())Ref::manager.m_cells[a.m_gridIndex].value=&a;
  }
  Ref::manager.simulateCombat(requested,secondPass);
 }
 void compare(int requested,bool secondPass,const F::NativeForecastPolicy&policy={2,{true,true}},bool secondPhase=false,int terrainBonus=0)
 {
  const auto before=observation();const int side=activeSide();
  const auto captured=F::captureNativeOrdinaryTwoPass(*views[side],*lifecycle[side],policy,static_cast<F::Side>(requested),secondPass);ASSERT_TRUE(captured)<<"ordinary native public capture must be admitted";
  const auto result=F::evaluateNativeOrdinaryTwoPass(*views[side],*lifecycle[side],policy,static_cast<F::Side>(requested),secondPass);ASSERT_TRUE(result);EXPECT_EQ(observation(),before);
  sourceOracle(policy,requested,secondPass,secondPhase,terrainBonus);ASSERT_FALSE(HasFatalFailure());
  EXPECT_EQ(result->order,Ref::manager.fixedOrder);ASSERT_EQ(result->passes,Ref::manager.stops.size());for(size_t i=0;i<result->passes;++i)EXPECT_EQ(result->stoppedAt[i],Ref::manager.stops[i]);
  ASSERT_EQ(result->actions.size(),Ref::manager.trace.size());for(size_t i=0;i<result->actions.size();++i){const auto&a=result->actions[i];const auto&b=Ref::manager.trace[i];EXPECT_EQ(a.actor,b.actor);EXPECT_EQ(a.target,b.target);EXPECT_EQ(a.damage,b.damage);EXPECT_EQ(a.killsOnly,b.killsOnly);EXPECT_EQ(a.retriedWithoutKillsOnly,b.retried);EXPECT_EQ(a.expectedDamageAfter,b.ledger);auto ranks=[&](const auto&x,const auto&y){ASSERT_EQ(x.size(),y.size());for(size_t j=0;j<x.size();++j){EXPECT_EQ(x[j].id,y[j].id);EXPECT_EQ(x[j].value,y[j].value);}};ranks(a.initialCandidates,b.initialCandidates);ranks(a.candidates,b.candidates);}
  for(int s=0;s<2;++s)for(int i=0;i<Ref::manager.m_numArmies[s];++i){const auto&u=Ref::manager.m_armies[s][i];EXPECT_EQ(result->expectedDamage.at(u.id),u.AI_expected_damage);}
  EXPECT_EQ(observation(),before);
 }
};
}
TEST_F(HoMM3OrdinaryShadowNative, PublicTwoByTwoMatchesPinnedTwoPassAndDoesNotMutate){prepare();ASSERT_FALSE(HasFatalFailure());for(int side=0;side<2;++side)for(bool second:{false,true})compare(side,second);}
TEST_F(HoMM3OrdinaryShadowNative, RejectsDifferentCallbackBindingAndUnobservedLifecycle){prepare();ASSERT_FALSE(HasFatalFailure());const int side=activeSide();CPlayerBattleCallback other(battle(),battle()->sideToPlayer(static_cast<BattleSide>(side)));F::NativeLifecycle late(*views[side]);EXPECT_FALSE(F::captureNativeOrdinaryTwoPass(other,*lifecycle[side],{},F::Side::ATTACKER,true));EXPECT_FALSE(F::captureNativeOrdinaryTwoPass(*views[side],late,{},F::Side::ATTACKER,true));}

// Every roster/geometry fixture checks both requested owners and both source pass modes.
class HoMM3OrdinaryShadowMatrix : public HoMM3OrdinaryShadowNative, public ::testing::WithParamInterface<int> {};
TEST_P(HoMM3OrdinaryShadowMatrix, PublicRosterAndGeometryMatchOriginalBodies)
{
 const int n=GetParam(), a=2+n%6, d=2+(n/6)%6;
 const bool distant=n>=36;
 attackBonus[0]=n%4;attackBonus[1]=(n/4)%4;
 defenseBonus[0]=(n/3)%4;defenseBonus[1]=(n/7)%4;
 const bool grass=n%2;
 prepare(a,d,distant,grass?ETerrainId::GRASS:ETerrainId::SAND);ASSERT_FALSE(HasFatalFailure());
 for(int difficulty=0;difficulty<5;++difficulty)for(int human=0;human<4;++human)
  for(int requested=0;requested<2;++requested)for(bool second:{false,true})
  {
   SCOPED_TRACE(::testing::Message()<<"difficulty="<<difficulty<<" human="<<human<<" requested="<<requested<<" second="<<second);
   compare(requested,second,{difficulty,{bool(human&1),bool(human&2)}},false,grass?1:0);
   ASSERT_FALSE(HasFatalFailure());
  }
}
INSTANTIATE_TEST_SUITE_P(TwoThroughSevenNearAndFar,HoMM3OrdinaryShadowMatrix,::testing::Range(0,72));

TEST_F(HoMM3OrdinaryShadowNative, ActualWaitTransitionsAndSecondPhaseMatchOriginalBodies)
{
 prepare();ASSERT_FALSE(HasFatalFailure());
 for(int step=0;step<4;++step)
 {
  const auto *u=views[0]->battleActiveUnit();ASSERT_NE(u,nullptr);ASSERT_FALSE(u->waited());
  ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),battle()->sideToPlayer(u->controllingSide()),BattleAction::makeWait(u)));
  const bool phase=step==3;
  ASSERT_EQ(lifecycle[activeSide()]->secondPhase(),std::optional<bool>(phase));
  for(int requested=0;requested<2;++requested)for(bool second:{false,true})compare(requested,second,{2,{true,true}},phase);
  ASSERT_FALSE(HasFatalFailure());
 }
}
TEST_F(HoMM3OrdinaryShadowNative, WoundedAndRetainedCorpseSlotsMatchOriginalBodies)
{
 prepare(3,3);ASSERT_FALSE(HasFatalFailure());
 int64_t injury=17;original[0]->damage(injury);ASSERT_EQ(original[0]->getAvailableHealth(),183);
 auto *corpse=original[1];int64_t fatal=corpse->getAvailableHealth();corpse->damage(fatal);corpse->ghost=true;ASSERT_FALSE(corpse->alive());
 for(int requested=0;requested<2;++requested)for(bool second:{false,true})compare(requested,second);
}
class HoMM3OrdinaryShadowRejection : public HoMM3OrdinaryShadowNative, public ::testing::WithParamInterface<int> {};
TEST_P(HoMM3OrdinaryShadowRejection, UnsupportedNativeFactsRemainUnavailableAndUnchanged)
{
 const int kind=GetParam();groundPeer=kind==3;prepare();ASSERT_FALSE(HasFatalFailure());
 if(!groundPeer){ASSERT_TRUE(F::captureNativeOrdinaryTwoPass(*views[activeSide()],*lifecycle[activeSide()],{},F::Side::ATTACKER,true));}
 if(kind==0){const auto*u=views[0]->battleActiveUnit();ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),battle()->sideToPlayer(u->controllingSide()),BattleAction::makeDefend(u)));ASSERT_TRUE(u->defended());}
 if(kind==1){original.back()->setPosition(BattleHex(1*17+3));ASSERT_FALSE(views[0]->battleCanShoot(original[0]));}
 if(kind==2){original[0]->shots.use(original[0]->shots.total());ASSERT_EQ(original[0]->shots.available(),0);}
 if(kind==4||kind==5){auto bonus=std::make_shared<Bonus>(BonusDuration::PERMANENT,kind==4?BonusType::JOUSTING:BonusType::STACKS_SPEED,BonusSource::OTHER,5,BonusSourceID());original[0]->addNewBonus(bonus);}
 const auto before=observation();
 EXPECT_FALSE(F::captureNativeOrdinaryTwoPass(*views[activeSide()],*lifecycle[activeSide()],{},F::Side::ATTACKER,true));
 EXPECT_FALSE(F::evaluateNativeOrdinaryTwoPass(*views[activeSide()],*lifecycle[activeSide()],{},F::Side::ATTACKER,true));
 EXPECT_EQ(observation(),before);
}
INSTANTIATE_TEST_SUITE_P(DefendBlockedEmptyAmmoGroundUnknownBonusCustomSpeed,HoMM3OrdinaryShadowRejection,::testing::Range(0,6));
