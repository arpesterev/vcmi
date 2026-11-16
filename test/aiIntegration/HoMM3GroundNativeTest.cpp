/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "StdInc.h"
#include "homm3InvariantOrder/PinnedGroundPermutation.h"
#include "server/battles/BattleTestFixture.h"
#include "../../AI/Integration/Ports/HoMM3Forecast/NativeOrdinaryGround.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/battle/BattleLayout.h"
#include "../../lib/modding/IdentifierStorage.h"
#include "../../lib/modding/ModScope.h"
#include "../../AI/Integration/Ports/HoMM3Forecast/NativeLiveMeleeDecision.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/StackLocation.h"
#include "../../server/CGameHandler.h"

namespace
{
namespace F=HoMM3Forecast;
class GroundEventServer : public RecordingGameServer
{
public:
 std::function<void(CPackForClient &)> before,after;
 unsigned packs=0;
 void applyPack(CPackForClient & pack) override
 {if(before)before(pack);RecordingGameServer::applyPack(pack);++packs;if(after)after(pack);}
};
class HoMM3GroundNative : public BattleTestFixture
{
protected:
 GroundEventServer events;
 std::array<std::unique_ptr<CPlayerBattleCallback>,2> views;
 std::array<std::unique_ptr<F::NativeLifecycle>,2> lifecycle;
 std::vector<CStack *> units;
 void prepare(int count=2,bool near=false,bool tied=false,bool mixed=false)
 {
  startGame();ASSERT_FALSE(HasFatalFailure());events.gameState=gameState();
  gameHandler=std::make_shared<CGameHandler>(events,gameState());gameHandler->randomizer->setSeed(seed);
  const auto creature=creatureByName("core:pikeman");
  gameHandler->createWanderingMonster({8,8,0},creature,20);
  auto defenderType=[&](int index){return tied||(mixed&&((count==2&&index==0)||(count==3&&index==2)))?creature:creatureByName("core:halberdier");};
  gameHandler->createWanderingMonster({9,9,0},defenderType(0),20);
  std::vector<CGCreature *> armies;
  for(const auto & object:map()->objects) if(auto * monster=dynamic_cast<CGCreature *>(object.get())) armies.push_back(monster);
  ASSERT_EQ(armies.size(),2u);
  for(int side=0;side<2;++side)
  {
   armies[side]->setOwner(PlayerColor(side));
   for(int i=1;i<count;++i) ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(armies[side]->id,SlotID(i)),(side?defenderType(i):creature).toCreature(),20+i));
  }
  const auto layout=BattleLayout::createDefaultLayout(*gameState(),armies[0],armies[1]);
  const std::string fieldName="core:sand_shore";
  BattleField field(*LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(),"battlefield",fieldName));
  BattleStart start;start.info=BattleInfo::setupBattle(gameState().get(),int3(4,4,0),ETerrainId::SAND,field,
   {armies[0],armies[1]},{nullptr,nullptr},layout,nullptr);start.battleID=BattleID(0);gameHandler->sendAndApply(start);
  ASSERT_EQ(gameState()->currentBattles.size(),1u);battle()->tacticDistance=0;battle()->obstacles.clear();
  for(const auto & unit:battle()->stacks)
  {
   const int side=static_cast<int>(unit->unitSide());
   unit->setPosition(BattleHex((unit->unitSlot().getNum()+1)*34+(side?(near?8:14):(near?7:2))));
   units.push_back(unit.get());
  }
  for(int side=0;side<2;++side)
  {
   views[side]=std::make_unique<CPlayerBattleCallback>(battle(),PlayerColor(side));
   lifecycle[side]=std::make_unique<F::NativeLifecycle>(*views[side]);
   lifecycle[side]->attach(battle()->getBattleID());
  }
  events.before=[this](CPackForClient & pack){if(auto * action=dynamic_cast<StartAction *>(&pack)) for(auto & item:lifecycle)item->actionStarted(action->battleID,action->ba);};
  events.after=[this](CPackForClient & pack){
   if(auto * round=dynamic_cast<BattleNextRound *>(&pack))for(auto & item:lifecycle)item->battleNewRound(round->battleID);
   if(auto * activation=dynamic_cast<BattleSetActiveStack *>(&pack))if(activation->reason!=BattleUnitTurnReason::AUTOMATIC_ACTION)
    lifecycle.at(static_cast<int>(views[0]->battleActiveUnit()->controllingSide()))->ownActivation(activation->battleID);
  };
  beginCombat();ASSERT_FALSE(HasFatalFailure());
 }
 void loadIndependentSource(int side)
 {
  namespace R=PinnedGroundPermutation;
  R::manager=R::combatManager{};R::sourceTiePermutation.clear();R::game.m_setup.m_difficulty=2;
  const auto snapshot=lifecycle[side]->snapshot();R::manager.IsHuman={true,true};
  R::manager.m_actingSide=static_cast<int>(snapshot.actingSide);R::manager.m_currentSide=side;
  R::manager.secondPhase=R::manager.m_inSecondPhase=snapshot.secondPhase;
  PinnedForecastPath::manager.setupAdjacencyArray();
  for(int h=0;h<187;++h)for(int d=0;d<6;++d)R::manager.m_adjacentCells[h][d]=PinnedForecastPath::manager.m_adjacentCells[h][d];
  for(const auto * native:units)
  {
   const int owner=static_cast<int>(native->unitSide()),index=native->unitSlot().getNum();
   auto & original=R::manager.m_armies[owner][index];R::manager.m_numArmies[owner]=std::max(R::manager.m_numArmies[owner],index+1);
   original.id=native->unitId();original.group=owner;original.index=index;
   original.m_creatureType=native->unitType()->getAttack(false)==6?1:0;
   original.m_numTroops=native->getCount();original.m_monInfo.hitPoints=native->unitType()->getMaxHealth();
   original.m_topCreatureDamage=original.m_numTroops*original.m_monInfo.hitPoints-native->getAvailableHealth();
   original.m_monInfo.damageLowBound=native->unitType()->getMinDamage(false);original.m_monInfo.damageHighBound=native->unitType()->getMaxDamage(false);
   original.m_monInfo.attackSkill=native->unitType()->getAttack(false);original.m_monInfo.defenseSkill=native->unitType()->getDefense(false);
   original.speed=native->getMovementRange();
   auto & trait=R::akCreatureTypeTraits[original.m_creatureType];trait.baseFightValue=native->unitType()->getFightValue();trait.attackSkill=original.m_monInfo.attackSkill;trait.defenseSkill=original.m_monInfo.defenseSkill;
   original.m_gridIndex=native->getPosition().toInt();R::manager.m_cells[original.m_gridIndex].value=&original;
   original.retaliates=native->counterAttacks.available()>0;original.m_retaliationCount=native->counterAttacks.available();
   if(native->moved()||native->defended())original.flags|=R::creatureDone;
   if(native->waited())original.flags|=R::creatureWaiting;
  }
 }
 void compareEverySourcePermutation(const F::OrdinaryLiveMeleeInput & input,const std::vector<std::vector<uint32_t>> & permutations)
 {
  namespace R=PinnedGroundPermutation;
  const auto chosen=F::chooseOrdinaryLiveMelee(input);ASSERT_TRUE(chosen);
  const int side=static_cast<int>(views[0]->battleActiveUnit()->controllingSide());
  for(const auto & permutation:permutations)
  {
   loadIndependentSource(side);R::sourceTiePermutation=permutation;R::draws=input.state.randomFactors;R::drawCursor=0;
   R::army * active=nullptr;for(int s=0;s<2;++s)for(int i=0;i<R::manager.m_numArmies[s];++i)if(R::manager.m_armies[s][i].id==input.activeId)active=&R::manager.m_armies[s][i];ASSERT_NE(active,nullptr);
   R::manager.chooseMeleeAction(active,false,false,side);ASSERT_FALSE(R::manager.trace.empty());const auto & expected=R::manager.trace.back();const auto & actual=chosen->choice;
   EXPECT_EQ(actual.actor,expected.actor);EXPECT_EQ(static_cast<int>(actual.order),expected.order);EXPECT_EQ(actual.target,expected.target);EXPECT_EQ(actual.value,expected.value);EXPECT_EQ(actual.danger,expected.danger);EXPECT_EQ(actual.dangerousEnemies,expected.marked);
   if(expected.order==2||expected.order==6) { EXPECT_EQ(actual.destination,expected.destination); }
   if(expected.order==6) { EXPECT_EQ(actual.attackHex,expected.hex); }
   EXPECT_EQ(chosen->drawsConsumed,R::drawCursor);
  }
 }
 std::vector<uint32_t> originalOrder(int side,const std::vector<uint32_t> & permutation)
 {
  namespace R=PinnedGroundPermutation;loadIndependentSource(side);R::sourceTiePermutation=permutation;
  std::vector<R::army *> order;R::manager.findMoveOrder(&order);std::vector<uint32_t> ids;for(const auto *unit:order)ids.push_back(unit->id);return ids;
 }
 uint32_t unitID(int side,int slot)const
 {return (*std::find_if(units.begin(),units.end(),[&](const auto*u){return static_cast<int>(u->unitSide())==side&&u->unitSlot().getNum()==slot;}))->unitId();}
 std::optional<F::OrdinaryLiveMeleeInput> capture(bool known=true)
 {
  const auto * active=views[0]->battleActiveUnit();const int side=static_cast<int>(active->controllingSide());
  return F::captureNativeOrdinaryGround(*views[side],*lifecycle[side],{2,{true,true}},*active,std::vector<int>(21,100),known);
 }
};
TEST_F(HoMM3GroundNative, CapturesEveryOrderedPairIncludingSelfAndSameSide)
{
 prepare();ASSERT_FALSE(HasFatalFailure());const auto packs=events.packs;
 const auto input=capture();ASSERT_TRUE(input);EXPECT_EQ(events.packs,packs);
 ASSERT_EQ(input->state.common.units.size(),4u);EXPECT_EQ(input->state.melee.size(),16u);EXPECT_EQ(input->state.canRetaliate.size(),16u);
 for(const auto * a:units) for(const auto * b:units)
 {
  const auto key=std::pair{a->unitId(),b->unitId()};ASSERT_TRUE(input->state.melee.contains(key));
  EXPECT_EQ(input->state.melee.at(key).attack,a->unitSide()==BattleSide::ATTACKER?4:6);EXPECT_EQ(input->state.melee.at(key).defense,5);
  EXPECT_TRUE(input->state.canRetaliate.at(key));
 }
 const auto choice=F::chooseOrdinaryLiveMelee(*input);ASSERT_TRUE(choice);EXPECT_EQ(choice->choice.actor,views[0]->battleActiveUnit()->unitId());
}
TEST_F(HoMM3GroundNative, RetaliationRowsBelongToDefenderNotCurrentAttacker)
{
 prepare();ASSERT_FALSE(HasFatalFailure());auto * exhausted=units.at(0);exhausted->counterAttacks.use();
 const auto input=capture();ASSERT_TRUE(input);
 for(const auto * a:units)for(const auto * b:units)
  EXPECT_EQ(input->state.canRetaliate.at({a->unitId(),b->unitId()}),a!=exhausted);
 EXPECT_EQ(input->state.ground.at(exhausted->unitId()).retaliationCount,0);
}
TEST_F(HoMM3GroundNative, RejectsMissingMapCertificateAndWrongActive)
{
 prepare();ASSERT_FALSE(HasFatalFailure());EXPECT_FALSE(capture(false));
 const auto * active=views[0]->battleActiveUnit();const int side=static_cast<int>(active->controllingSide());
 const auto * other=*std::find_if(units.begin(),units.end(),[&](const auto * unit){return unit!=active;});
 EXPECT_FALSE(F::captureNativeOrdinaryGround(*views[side],*lifecycle[side],{2,{true,true}},*other,std::vector<int>(21,100),true));
}
TEST_F(HoMM3GroundNative, RejectsCreatureCastMechanics)
{
 prepare();ASSERT_FALSE(HasFatalFailure());units.front()->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::SPELLCASTER,BonusSource::OTHER,1,BonusSourceID()));
 EXPECT_FALSE(capture());
}
TEST_F(HoMM3GroundNative, RejectsFightingHeroInsteadOfInventingZeroSpellThreat)
{
 prepare();ASSERT_FALSE(HasFatalFailure());battle()->getSide(BattleSide::ATTACKER).heroID=attackerSideHero->id;
 EXPECT_FALSE(capture());
}
TEST_F(HoMM3GroundNative, RejectsUnresolvedSourceComparatorTies)
{
 // Stable test ID retained. a31 rejected all comparator ties; a32 now proves
 // this mirrored case invariant and compares the actual choice to ALL source
 // tie orientations. Genuinely unresolved mixed ties remain rejected below.
 prepare(2,false,true);ASSERT_FALSE(HasFatalFailure());
 BattleSetActiveStack activate;activate.battleID=battle()->getBattleID();activate.stack=unitID(0,1);activate.reason=BattleUnitTurnReason::TURN_QUEUE;gameHandler->sendAndApply(activate);
 const auto input=capture();ASSERT_TRUE(input);
 std::vector<std::vector<uint32_t>> permutations;
 for(int mask=0;mask<4;++mask){std::vector<uint32_t> order;for(int i=0;i<2;++i){int first=(mask>>i)&1;order.push_back(unitID(first,i));order.push_back(unitID(1-first,i));}permutations.push_back(order);}
 compareEverySourcePermutation(*input,permutations);ASSERT_FALSE(HasFatalFailure());
 const auto order=originalOrder(0,permutations.front());ASSERT_FALSE(order.empty());EXPECT_NE(order.front(),input->activeId);
 const auto * active=views[0]->battleActiveUnit();std::minstd_rand rng(31),expected=rng;
 const auto decision=F::chooseNativeOrdinaryLiveMelee(*views[0],*lifecycle[0],{2,{true,true}},*active,true,rng);ASSERT_TRUE(decision);
 std::uniform_int_distribution<int> distribution(75,100);for(size_t i=0;i<decision->drawsConsumed;++i)distribution(expected);EXPECT_EQ(rng,expected);EXPECT_TRUE(act(decision->action));
}
TEST_F(HoMM3GroundNative, RejectsNonInvariantMixedComparatorTiesWithoutConsumingRandom)
{
 prepare(3,false,false,true);ASSERT_FALSE(HasFatalFailure());
 const int side=static_cast<int>(views[0]->battleActiveUnit()->controllingSide());
 std::vector<uint32_t> first{unitID(1,0),unitID(1,1),unitID(0,0),unitID(0,1),unitID(0,2),unitID(1,2)};auto second=first;std::swap(second[4],second[5]);
 EXPECT_NE(originalOrder(side,first),originalOrder(side,second));EXPECT_FALSE(capture());
 std::minstd_rand rng(31),before=rng;EXPECT_FALSE(F::chooseNativeOrdinaryLiveMelee(*views[side],*lifecycle[side],{2,{true,true}},*views[0]->battleActiveUnit(),true,rng));EXPECT_EQ(rng,before);
}
TEST_F(HoMM3GroundNative, MixedInvariantNativeOrderRequiresItsExplicitOrientationProofBudget)
{
 prepare(2,false,false,true);ASSERT_FALSE(HasFatalFailure());const auto input=capture();ASSERT_TRUE(input);
 F::MoveOrderInput order;order.actingSide=input->state.common.actingSide;order.secondPhase=input->state.common.secondPhase;for(const auto & unit:input->state.common.units)order.units.push_back(unit.state.order);
 EXPECT_TRUE(F::projectInvariantMoveOrder(order));EXPECT_FALSE(F::projectInvariantMoveOrder(order,0));
 std::vector<uint32_t> first{unitID(1,1),unitID(0,0),unitID(1,0),unitID(0,1)};auto second=first;std::swap(second[1],second[2]);compareEverySourcePermutation(*input,{first,second});
}
TEST_F(HoMM3GroundNative, ConsumerCommitsOnlyUsedRandomDrawsAndExecutesNativeAction)
{
 prepare(2,true);ASSERT_FALSE(HasFatalFailure());
 const auto * active=views[0]->battleActiveUnit();const int side=static_cast<int>(active->controllingSide());
 std::minstd_rand rng(31),expected(31);
 const auto decision=F::chooseNativeOrdinaryLiveMelee(*views[side],*lifecycle[side],{2,{true,true}},*active,true,rng);
 ASSERT_TRUE(decision);EXPECT_EQ(decision->sourceChoice.actor,active->unitId());
 std::uniform_int_distribution<int> dist(75,100);for(size_t i=0;i<decision->drawsConsumed;++i)dist(expected);
 EXPECT_EQ(rng,expected);EXPECT_TRUE(act(decision->action));
}
TEST_F(HoMM3GroundNative, ConsumerRejectionPreservesRandomStream)
{
 prepare();ASSERT_FALSE(HasFatalFailure());
 const auto * active=views[0]->battleActiveUnit();const int side=static_cast<int>(active->controllingSide());
 std::minstd_rand rng(31),initial=rng;
 EXPECT_FALSE(F::chooseNativeOrdinaryLiveMelee(*views[side],*lifecycle[side],{2,{true,true}},*active,false,rng));EXPECT_EQ(rng,initial);
 const auto * other=*std::find_if(units.begin(),units.end(),[&](const auto * unit){return unit!=active;});
 EXPECT_FALSE(F::chooseNativeOrdinaryLiveMelee(*views[side],*lifecycle[side],{2,{true,true}},*other,true,rng));EXPECT_EQ(rng,initial);
}

class HoMM3GroundNativeOracle : public HoMM3GroundNative, public ::testing::WithParamInterface<int> {};
TEST_P(HoMM3GroundNativeOracle, ActualActiveMatchesPinnedLiveChooserNotForecastFront)
{
 namespace R=PinnedGroundPermutation;
 const int scenario=GetParam(),count=1+scenario%3,side=(scenario/3)%2;
 prepare(count,scenario>=6);ASSERT_FALSE(HasFatalFailure());
 auto it=std::find_if(units.begin(),units.end(),[&](const auto * u){return static_cast<int>(u->unitSide())==side && u->unitSlot().getNum()==count-1;});ASSERT_NE(it,units.end());
 BattleSetActiveStack activate;activate.battleID=battle()->getBattleID();activate.stack=(*it)->unitId();activate.reason=BattleUnitTurnReason::TURN_QUEUE;gameHandler->sendAndApply(activate);
 const auto input=capture();ASSERT_TRUE(input);const auto actual=F::chooseOrdinaryLiveMelee(*input);ASSERT_TRUE(actual);
 R::manager=R::combatManager{};R::sourceTiePermutation.clear();R::game.m_setup.m_difficulty=2;
 R::manager.IsHuman={true,true};R::manager.m_actingSide=side;R::manager.m_currentSide=side;
 R::manager.secondPhase=false;R::manager.m_inSecondPhase=false;
 PinnedForecastPath::manager.setupAdjacencyArray();
 for(int hex=0;hex<187;++hex)for(int direction=0;direction<6;++direction)
  R::manager.m_adjacentCells[hex][direction]=PinnedForecastPath::manager.m_adjacentCells[hex][direction];
 R::army * originalActive=nullptr;
 for(const auto * native:units)
 {
  const int owner=static_cast<int>(native->unitSide()),index=native->unitSlot().getNum();
  auto & original=R::manager.m_armies[owner][index];++R::manager.m_numArmies[owner];
  original.id=native->unitId();original.group=owner;original.index=index;original.m_creatureType=owner;
  original.m_numTroops=native->getCount();original.m_monInfo.hitPoints=10;
  EXPECT_EQ(native->getMaxHealth(),10u);original.m_topCreatureDamage=original.m_numTroops*10-native->getAvailableHealth();
  original.m_monInfo.damageLowBound=native->unitType()->getMinDamage(false);original.m_monInfo.damageHighBound=native->unitType()->getMaxDamage(false);
  original.m_monInfo.attackSkill=owner?6:4;original.m_monInfo.defenseSkill=5;original.speed=owner?5:4;
  EXPECT_EQ(native->getMovementRange(),static_cast<uint32_t>(original.speed));
  R::akCreatureTypeTraits[owner].baseFightValue=native->unitType()->getFightValue();
  R::akCreatureTypeTraits[owner].attackSkill=original.m_monInfo.attackSkill;R::akCreatureTypeTraits[owner].defenseSkill=5;
  original.m_gridIndex=native->getPosition().toInt();R::manager.m_cells[original.m_gridIndex].value=&original;
  original.retaliates=native->counterAttacks.available()>0;original.m_retaliationCount=native->counterAttacks.available();
  if(native->moved()||native->defended())original.flags|=R::creatureDone;
  if(native->waited())original.flags|=R::creatureWaiting;
  if(native==*it)originalActive=&original;
 }
 ASSERT_NE(originalActive,nullptr);R::draws=std::vector<int>(21,100);R::drawCursor=0;
 R::manager.chooseMeleeAction(originalActive,false,false,side);ASSERT_FALSE(R::manager.trace.empty());
 const auto & expected=R::manager.trace.back();const auto & chosen=actual->choice;
 EXPECT_EQ(chosen.actor,originalActive->id);EXPECT_EQ(static_cast<int>(chosen.order),expected.order);
 EXPECT_EQ(chosen.target,expected.target);EXPECT_EQ(chosen.value,expected.value);
 EXPECT_EQ(chosen.danger,expected.danger);EXPECT_EQ(chosen.dangerousEnemies,expected.marked);
 if(expected.order==2||expected.order==6) { EXPECT_EQ(chosen.destination,expected.destination); }
 if(expected.order==6) { EXPECT_EQ(chosen.attackHex,expected.hex); }
 EXPECT_EQ(actual->drawsConsumed,R::drawCursor);
 for(const auto & [id,damage]:chosen.expectedDamageAfter){(void)id;EXPECT_EQ(damage,0);}
 std::vector<R::army *> order;R::manager.findMoveOrder(&order);ASSERT_FALSE(order.empty());
 if(count>1) { EXPECT_NE(order.front()->id,chosen.actor); }
 std::minstd_rand random(31);const auto decision=F::chooseNativeOrdinaryLiveMelee(*views[side],*lifecycle[side],{2,{true,true}},**it,true,random);
 ASSERT_TRUE(decision);EXPECT_EQ(decision->action.stackNumber,(*it)->unitId());EXPECT_TRUE(act(decision->action));
}
INSTANTIATE_TEST_SUITE_P(RostersAndPositions,HoMM3GroundNativeOracle,::testing::Range(0,12));

}
