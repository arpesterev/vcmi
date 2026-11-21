/* GPL-2.0-or-later. FHeroes2 siege fallback on native VCMI geometry. */
#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "lib/GameLibrary.h"
#include "lib/battle/BattleAction.h"
#include "lib/battle/BattleLayout.h"
#include "lib/battle/CObstacleInstance.h"
#include "lib/mapObjects/CGTownInstance.h"
#include "lib/modding/IdentifierStorage.h"
#include "lib/modding/ModScope.h"
#include "lib/battle/CPlayerBattleCallback.h"
#include "lib/callback/AIFactory.h"
#include "lib/callback/CBattleCallback.h"
#include "lib/callback/CBattleGameInterface.h"
#include "lib/callback/CCallback.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/networkPacks/StackLocation.h"
#include "server/CGameHandler.h"
#include "server/battles/BattleProcessor.h"
#include <vcmi/Environment.h>

namespace
{
class SiegeDecisionCallback final : public CBattleCallback
{
public:
 explicit SiegeDecisionCallback(PlayerColor player) : CBattleCallback(player, nullptr) {}
 std::optional<BattleAction> action;
 void battleMakeUnitAction(const BattleID &, const BattleAction & value) override
 {
  if(action) throw std::logic_error("Duplicate siege action");
  action = value;
 }
 std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID &, const BattleStateInfoForRetreat &) override
 {
  return std::nullopt;
 }
};
class SiegeEnvironment final : public Environment
{
 std::shared_ptr<CCallback> gameCallback;
 std::shared_ptr<SiegeDecisionCallback> callback;
public:
 SiegeEnvironment(std::shared_ptr<CCallback> game, std::shared_ptr<SiegeDecisionCallback> cb)
  : gameCallback(std::move(game)), callback(std::move(cb)) {}
 const Services * services() const override { return LIBRARY; }
 const GameCb * game() const override { return gameCallback.get(); }
 const BattleCb * battle(const BattleID & id) const override { return callback->getBattle(id).get(); }
};
}

class FHeroes2SiegeFallback : public BattleTestFixture, public ::testing::WithParamInterface<bool>
{
protected:
 CStack * actor = nullptr;
 CStack * enemy = nullptr;
 std::shared_ptr<SiegeDecisionCallback> callback;
 std::shared_ptr<CPlayerBattleCallback> view;
 void prepare(int origin, int target)
 {
  startGame(); ASSERT_FALSE(HasFatalFailure());
  const auto * town = findFirst<CGTownInstance>(); ASSERT_NE(town,nullptr);
  if(!town->hasBuilt(BuildingID::FORT)) { ASSERT_TRUE(gameHandler->buildStructure(town->id,BuildingID::FORT,true)); }
  ASSERT_FALSE(town->hasBuilt(BuildingID::CITADEL)); ASSERT_FALSE(town->hasBuilt(BuildingID::CASTLE));
  ASSERT_TRUE(gameHandler->changeStackType(StackLocation(attackerSideHero->id,SlotID(0)),CreatureID(6).toCreature()));
  attackerSideHero->setStackCount(SlotID(0),40);
  ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(town->id,SlotID(0)),CreatureID(0).toCreature(),100));
  BattleLayout layout;
  layout.units[BattleSide::ATTACKER][0]=BattleHex(origin);
  layout.units[BattleSide::DEFENDER][0]=BattleHex(target);
  const std::string battlefieldName="core:sand_shore";
  BattleField battlefield(*LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(),"battlefield",battlefieldName));
  BattleStart packet; packet.battleID=BattleID(0);
  packet.info=BattleInfo::setupBattle(gameState().get(),town->visitablePos(),ETerrainId::SAND,battlefield,
   {attackerSideHero,town},{attackerSideHero,nullptr},layout,town);
  gameHandler->sendAndApply(packet);
  ASSERT_EQ(battle()->stacks.size(),2u);
  ASSERT_TRUE(battle()->obstacles.empty());
  for(const auto & unit:battle()->stacks) (unit->unitSide()==BattleSide::ATTACKER?actor:enemy)=unit.get();
  ASSERT_NE(actor,nullptr); ASSERT_NE(enemy,nullptr);
  ASSERT_FALSE(actor->isCatapult()); ASSERT_FALSE(enemy->isTurret());
  ASSERT_FALSE(actor->doubleWide()); ASSERT_FALSE(actor->hasBonusOfType(BonusType::FLYING));
  ASSERT_EQ(actor->getPosition(),BattleHex(origin)); ASSERT_EQ(enemy->getPosition(),BattleHex(target));
  ASSERT_EQ(actor->getMovementRange(),5u); ASSERT_EQ(enemy->getMovementRange(),4u);
  callback=std::make_shared<SiegeDecisionCallback>(actor->unitOwner()); callback->onBattleStarted(battle());
  view=callback->getBattle(battle()->getBattleID());
 }
 void activate()
 {
  beginCombat(); ASSERT_FALSE(HasFatalFailure());
  ASSERT_EQ(battle()->activeStack,actor->unitId());
  std::vector<battle::Units> order; view->battleGetTurnOrder(order,2,1);
  ASSERT_FALSE(order.empty()); ASSERT_EQ(order.front().size(),2u); ASSERT_EQ(order.front().front(),actor);
 }
 void choose()
 {
  auto policy=AIFactory::createBattleAI("FHeroes2Tactics"); ASSERT_NE(policy,nullptr);
  const auto player=view->battleGetOwner(actor);
  auto environment=std::make_shared<SiegeEnvironment>(makeCallback(player),callback);
  AutocombatPreferences prefs; prefs.enableSpellsUsage=false;
  policy->playerID=player; policy->initBattleInterface(environment,callback,prefs);
  policy->battleStart(battle()->getBattleID(),battle()->getSideArmy(BattleSide::ATTACKER),battle()->getSideArmy(BattleSide::DEFENDER),battle()->getLocation(),attackerSideHero,nullptr,view->playerToSide(player),false);
  policy->activeStack(battle()->getBattleID(),actor); ASSERT_TRUE(callback->action);
 }
};
TEST_P(FHeroes2SiegeFallback,upperWallSeparatesWallApproachFromRealAttack)
{
 prepare(44,46); ASSERT_FALSE(HasFatalFailure());
 ASSERT_EQ(view->wallPartToBattleHex(EWallPart::UPPER_WALL),BattleHex(29));
 ASSERT_EQ(view->battleGetWallState(EWallPart::UPPER_WALL),EWallState::INTACT);
 if(GetParam())
 {
  CatapultAttack packet; packet.battleID=battle()->getBattleID();packet.attackedPart=EWallPart::UPPER_WALL;packet.damageDealt=100;
  gameHandler->sendAndApply(packet);
  ASSERT_EQ(view->battleGetWallState(EWallPart::UPPER_WALL),EWallState::DESTROYED);
 }
 activate(); ASSERT_FALSE(HasFatalFailure());
 const auto reach=view->getReachability(actor);
 const auto available=view->battleGetAvailableHexes(reach,actor,false);
 ASSERT_FALSE(reach.isReachable(BattleHex(45)));
 ASSERT_EQ(reach.isReachable(BattleHex(29)),GetParam());
 ASSERT_TRUE(available.contains(BattleHex(28)));
 if(!GetParam())
 {
  const auto initial=BattleHex::getDistance(actor->getPosition(),enemy->getPosition());
  for(const auto & hex:available) EXPECT_GE(BattleHex::getDistance(hex,enemy->getPosition()),initial);
  for(const auto & hex:enemy->getAttackableHexes(actor))
   if(view->isMeleeAttackPossible(actor,enemy,hex)) { EXPECT_FALSE(reach.isReachable(hex)); }
 }
 else { ASSERT_TRUE(available.contains(BattleHex(29))); ASSERT_TRUE(view->isMeleeAttackPossible(actor,enemy,BattleHex(29))); }
 const auto health=enemy->getAvailableHealth(); choose(); ASSERT_FALSE(HasFatalFailure());
 EXPECT_EQ(actor->getPosition(),BattleHex(44)); EXPECT_EQ(enemy->getAvailableHealth(),health);
 ASSERT_EQ(callback->action->actionType,GetParam()?EActionType::WALK_AND_ATTACK:EActionType::WALK);
 ASSERT_FALSE(callback->action->target.empty()); EXPECT_EQ(callback->action->target.front().hexValue,BattleHex(GetParam()?29:28));
 ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),actor->unitOwner(),*callback->action));
 EXPECT_EQ(actor->getPosition(),BattleHex(GetParam()?29:28));
 if(GetParam()) {EXPECT_LT(enemy->getAvailableHealth(),health);} else {EXPECT_EQ(enemy->getAvailableHealth(),health);}
}
class FHeroes2SiegeGateState : public FHeroes2SiegeFallback {};
TEST_P(FHeroes2SiegeGateState,gateStateControlsNativeApproachAndAttack)
{
 prepare(93,97); ASSERT_FALSE(HasFatalFailure());
 activate(); ASSERT_FALSE(HasFatalFailure());
 ASSERT_EQ(view->battleGetWallState(EWallPart::GATE),EWallState::INTACT);
 BattleUpdateGateState packet;packet.battleID=battle()->getBattleID();packet.state=GetParam()?EGateState::OPENED:EGateState::CLOSED;
 gameHandler->sendAndApply(packet);
 ASSERT_EQ(view->battleGetGateState(),packet.state);
 const auto reach=view->getReachability(actor);
 ASSERT_EQ(reach.isReachable(BattleHex(BattleHex::GATE_OUTER)),GetParam());
 ASSERT_EQ(reach.isReachable(BattleHex(BattleHex::GATE_INNER)),GetParam());
 if(GetParam())
 {
  BattleHexArray path; BattleHex step(79);
  for(int i=0;i<GameConstants::BFIELD_SIZE && step.isValid() && step!=actor->getPosition();++i)
  {path.insert(step);step=reach.predecessors[step.toInt()];}
  ASSERT_TRUE(path.contains(BattleHex(BattleHex::GATE_INNER)));
  ASSERT_TRUE(path.contains(BattleHex(BattleHex::GATE_OUTER)));
 }
 const auto health=enemy->getAvailableHealth(); choose(); ASSERT_FALSE(HasFatalFailure());
 ASSERT_EQ(callback->action->actionType,GetParam()?EActionType::WALK_AND_ATTACK:EActionType::WALK);
 ASSERT_FALSE(callback->action->target.empty());
 EXPECT_EQ(callback->action->target.front().hexValue,BattleHex(GetParam()?79:44));
 EXPECT_EQ(actor->getPosition(),BattleHex(93)); EXPECT_EQ(enemy->getAvailableHealth(),health);
 ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),actor->unitOwner(),*callback->action));
 EXPECT_EQ(actor->getPosition(),BattleHex(GetParam()?79:44));
 if(GetParam()) {EXPECT_LT(enemy->getAvailableHealth(),health);} else {EXPECT_EQ(enemy->getAvailableHealth(),health);}
 EXPECT_EQ(view->battleGetWallState(EWallPart::GATE),EWallState::INTACT);
}
INSTANTIATE_TEST_SUITE_P(WallState,FHeroes2SiegeFallback,::testing::Bool(),[](const auto & info){return info.param?"Destroyed":"Standing";});
INSTANTIATE_TEST_SUITE_P(GateState,FHeroes2SiegeGateState,::testing::Bool(),[](const auto & info){return info.param?"Opened":"Closed";});

class FHeroes2SiegeController : public FHeroes2SiegeFallback {};
TEST_P(FHeroes2SiegeController,currentControllerDeterminesSiegeWallApproach)
{
 prepare(18,44); ASSERT_FALSE(HasFatalFailure());
 auto * originalAttacker=actor; auto * controlled=enemy;
 auto * reserve=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(46),100); ASSERT_NE(reserve,nullptr);
 beginCombat(); ASSERT_FALSE(HasFatalFailure());
 ASSERT_EQ(battle()->activeStack,originalAttacker->unitId());
 if(GetParam())
 {
  teachSpell(attackerSideHero,SpellID::HYPNOTIZE);
  attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,100,ChangeValueMode::ABSOLUTE);
  const auto spell=BattleAction::makeHeroSpellcast(BattleSide::ATTACKER,battle::Target{battle::Destination(controlled)},SpellID::HYPNOTIZE);
  ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),originalAttacker->unitOwner(),spell));
  ASSERT_TRUE(controlled->isHypnotized());
 }
 for(size_t turn=0;turn<battle()->stacks.size()*3 && battle()->battleActiveUnit()!=controlled;++turn)
 {
  const auto * active=battle()->battleActiveUnit(); ASSERT_NE(active,nullptr);
  ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),battle()->battleGetOwner(active),BattleAction::makeNoAction(active)));
 }
 ASSERT_EQ(battle()->activeStack,controlled->unitId());
 actor=controlled; enemy=GetParam()?reserve:originalAttacker;
 const auto player=battle()->battleGetOwner(actor);
 ASSERT_EQ(actor->unitSide(),BattleSide::DEFENDER);
 ASSERT_EQ(actor->controllingSide(),GetParam()?BattleSide::ATTACKER:BattleSide::DEFENDER);
 callback=std::make_shared<SiegeDecisionCallback>(player);callback->onBattleStarted(battle());view=callback->getBattle(battle()->getBattleID());
 const auto reach=view->getReachability(actor);const auto available=view->battleGetAvailableHexes(reach,actor,false);
 ASSERT_EQ(reach.params.canUseGate,!GetParam());
 ASSERT_TRUE(available.contains(BattleHex(28)));
 if(GetParam())
 {
  ASSERT_FALSE(reach.isReachable(BattleHex(29))); ASSERT_FALSE(reach.isReachable(BattleHex(45)));
  const auto initial=BattleHex::getDistance(actor->getPosition(),enemy->getPosition());
  for(const auto & hex:available) EXPECT_GE(BattleHex::getDistance(hex,enemy->getPosition()),initial);
  for(const auto & hex:enemy->getAttackableHexes(actor))
   if(view->isMeleeAttackPossible(actor,enemy,hex)) { EXPECT_FALSE(reach.isReachable(hex)); }
 }
 const auto health=enemy->getAvailableHealth(); choose(); ASSERT_FALSE(HasFatalFailure());
 EXPECT_EQ(callback->action->actionType,EActionType::WALK);
 if(callback->action->actionType==EActionType::WALK)
 {
  ASSERT_FALSE(callback->action->target.empty());
  if(GetParam()) {EXPECT_EQ(callback->action->target.front().hexValue,BattleHex(28));}
  else
  {
   EXPECT_NE(callback->action->target.front().hexValue,BattleHex(28));
   EXPECT_LT(BattleHex::getDistance(callback->action->target.front().hexValue,enemy->getPosition()),BattleHex::getDistance(actor->getPosition(),enemy->getPosition()));
  }
 }
 EXPECT_EQ(actor->getPosition(),BattleHex(44));
 ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),player,*callback->action));
 if(GetParam()) {EXPECT_EQ(actor->getPosition(),BattleHex(28));}
 EXPECT_EQ(enemy->getAvailableHealth(),health);
}
INSTANTIATE_TEST_SUITE_P(Control,FHeroes2SiegeController,::testing::Bool(),[](const auto & info){return info.param?"HypnotizedAttacker":"OriginalDefender";});
