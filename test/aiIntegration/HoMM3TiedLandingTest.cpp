/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "StdInc.h"
#define PinnedForecastPath TiedLandingPinnedPath
#define PinnedGroundTwoPass TiedLandingPinnedGroundTwoPass
#include "homm3GroundTwoPass/PinnedGroundTwoPass.h"
#undef PinnedGroundTwoPass
#undef PinnedForecastPath
#include "server/battles/BattleTestFixture.h"
#include "../../AI/Integration/Ports/HoMM3TacticsBattleAI.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/StackLocation.h"
#include "../../server/CGameHandler.h"
#include "../../server/battles/BattleProcessor.h"
#include <vcmi/Environment.h>

namespace
{
class LandingCallback final : public CBattleCallback
{
public:
 explicit LandingCallback(PlayerColor player) : CBattleCallback(player, nullptr) {}
 std::optional<BattleAction> action, spell;
 void battleMakeUnitAction(const BattleID &, const BattleAction & value) override { action = value; }
 void battleMakeSpellAction(const BattleID &, const BattleAction & value) override { spell = value; }
};
class LandingEnvironment final : public Environment
{
 std::shared_ptr<CCallback> gameCb;
 std::shared_ptr<LandingCallback> battleCb;
public:
 LandingEnvironment(std::shared_ptr<CCallback> game, std::shared_ptr<LandingCallback> battle)
  : gameCb(std::move(game)), battleCb(std::move(battle)) {}
 const Services * services() const override { return LIBRARY; }
 const GameCb * game() const override { return gameCb.get(); }
 const BattleCb * battle(const BattleID & id) const override { return battleCb->getBattle(id).get(); }
};
class HoMM3TiedLanding : public BattleTestFixture
{
protected:
 CStack * actor = nullptr;
 CStack * enemy = nullptr;
 std::shared_ptr<LandingCallback> callback;
 std::shared_ptr<CPlayerBattleCallback> view;
 std::shared_ptr<LandingEnvironment> environment;
 std::unique_ptr<HoMM3Forecast::NativeLifecycle> lifecycle;
 std::unique_ptr<AIIntegration::HoMM3TacticsBattleAI> controller;
 void prepare(int count = 1, bool defending = false)
 {
  startGame(); ASSERT_FALSE(HasFatalFailure());
  gameState()->getStartInfo()->difficulty = static_cast<uint8_t>(EMapDifficulty::NORMAL);
  const auto pikeman = creatureByName("core:pikeman");
  ASSERT_TRUE(gameHandler->changeStackType(StackLocation(attackerSideHero->id, SlotID(0)), pikeman.toCreature()));
  ASSERT_TRUE(gameHandler->changeStackCount(StackLocation(attackerSideHero->id, SlotID(0)), 100, ChangeValueMode::ABSOLUTE));
  attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 0, ChangeValueMode::ABSOLUTE);
  attackerSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 0, ChangeValueMode::ABSOLUTE);
  for(const auto & spell : LIBRARY->spellh->objects) attackerSideHero->removeSpellFromSpellbook(spell->id);
  teachSpell(attackerSideHero, SpellID(SpellID::MAGIC_ARROW));
  attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 6, ChangeValueMode::ABSOLUTE);
  gameHandler->createWanderingMonster({9, 9, 0}, pikeman, count);
  CGCreature * neutral = nullptr;
  for(const auto & object : map()->objects)
   if(auto * monster = dynamic_cast<CGCreature *>(object.get())) neutral = monster;
  ASSERT_NE(neutral, nullptr);
  map()->getTile(neutral->visitablePos()).terrainType = ETerrainId::SAND;
  map()->getTile(attackerSideHero->visitablePos()).terrainType = ETerrainId::SAND;
  if(defending)
  {
   neutral->setOwner(defenderSideHero->getOwner());
   gameHandler->battles->startBattle(neutral, attackerSideHero);
  }
  else gameHandler->battles->startBattle(attackerSideHero, neutral);
  ASSERT_EQ(gameState()->currentBattles.size(), 1u);
  battle()->obstacles.clear();
  ASSERT_EQ(battle()->stacks.size(), 2u);
  for(const auto & stack : battle()->stacks)
   (stack->unitSide() == (defending ? BattleSide::DEFENDER : BattleSide::ATTACKER) ? actor : enemy) = stack.get();
  actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
   BonusType::STACKS_SPEED, BonusSource::CREATURE_ABILITY, 1, BonusSourceID(pikeman)));
  actor->setPosition(BattleHex(92)); enemy->setPosition(BattleHex(112));
  const auto id = battle()->getBattleID();
  callback = std::make_shared<LandingCallback>(attackerSideHero->getOwner());
  callback->onBattleStarted(battle()); view = callback->getBattle(id);
  environment = std::make_shared<LandingEnvironment>(makeCallback(attackerSideHero->getOwner()), callback);
  lifecycle = std::make_unique<HoMM3Forecast::NativeLifecycle>(*view); lifecycle->attach(id);
  controller = std::make_unique<AIIntegration::HoMM3TacticsBattleAI>();
  AutocombatPreferences preferences; preferences.enableSpellsUsage = true;
  controller->initBattleInterface(environment, callback, preferences);
  controller->battleStart(id, nullptr, nullptr, int3(), nullptr, nullptr, view->battleGetMySide(), false);
  beginCombat(); ASSERT_FALSE(HasFatalFailure());
  lifecycle->battleNewRound(id); controller->battleNewRound(id);
  ASSERT_EQ(view->battleActiveUnit(), actor);
  lifecycle->ownActivation(id);
 }
 int sourceLanding()
 {
  namespace R = TiedLandingPinnedGroundTwoPass;
  R::manager = R::combatManager{}; R::game.m_setup.m_difficulty = 1;
  TiedLandingPinnedPath::manager.setupAdjacencyArray();
  for(int hex = 0; hex < 187; ++hex) for(int direction = 0; direction < 6; ++direction)
   R::manager.m_adjacentCells[hex][direction] = TiedLandingPinnedPath::manager.m_adjacentCells[hex][direction];
  for(const auto * native : {actor, enemy})
  {
   const int side = static_cast<int>(native->unitSide());
   auto & original = R::manager.m_armies[side][0]; R::manager.m_numArmies[side] = 1;
   original.group = side; original.index = 0; original.id = native->unitId();
   original.m_gridIndex = native->getPosition().toInt(); original.speed = native->getMovementRange();
   original.m_numTroops = native->getCount(); original.m_monInfo.hitPoints = native->getMaxHealth();
   original.m_monInfo.damageLowBound = native->unitType()->getMinDamage(false);
   original.m_monInfo.damageHighBound = native->unitType()->getMaxDamage(false);
   original.m_monInfo.attackSkill = native->getAttack(false); original.m_monInfo.defenseSkill = native->getDefense(false);
   original.m_topCreatureDamage = native->getMaxHealth() - native->getFirstHPleft();
   original.retaliates = native->ableToRetaliate();
   R::akCreatureTypeTraits[0] = {double(native->unitType()->getFightValue()), native->getAttack(false), native->getDefense(false)};
   R::manager.m_cells[original.m_gridIndex].value = &original;
  }
  const int own = static_cast<int>(actor->unitSide());
  R::manager.m_currentSide = own;
  const auto & originalActor = R::manager.m_armies[own][0];
  const auto & originalEnemy = R::manager.m_armies[1 - own][0];
  R::type_AI_combat_parameters estimate(&R::manager, own);
  R::searchArray path;
  path.seedCombatPosition(&originalActor, own, 127, 0, originalActor.speed);
  std::array<int32_t, 187> threat{};
  int32_t possible = 0;
  R::manager.markEnemyAttacks(&originalActor, threat.data(), possible, estimate);
  R::manager.markFriendlyArmies(&originalActor, threat.data(), possible, estimate);
  R::type_AI_attack_hex_chooser chooser(&originalActor, &originalEnemy, threat.data(), &path, &estimate);
  if(!chooser.findAttackHex()) throw std::logic_error("source landing unavailable");
  EXPECT_EQ(chooser.getHexValue(), 0);
  const auto nativeReach = view->getReachability(actor);
  const auto directions = AIIntegration::Ports::HoMM3::GroundPathDetail::adjacent(enemy->getPosition().toInt());
  int expected = -1;
  for(int direction = 0; direction < 6; ++direction)
  {
   const int hex = R::manager.m_adjacentCells[originalEnemy.m_gridIndex][direction];
   if(hex < 0 || hex % 17 == 0 || hex % 17 == 16) { EXPECT_EQ(directions[direction], -1); continue; }
   EXPECT_EQ(directions[direction], hex);
   EXPECT_EQ(path.getHex(hex)->m_visited, nativeReach.isReachable(BattleHex(hex)));
   if(!path.getHex(hex)->m_visited) continue;
   EXPECT_EQ(path.getHex(hex)->m_cost, nativeReach.distances.at(hex));
   if(expected < 0 || nativeReach.distances.at(hex) < nativeReach.distances.at(expected)) expected = hex;
  }
  EXPECT_EQ(chooser.getBestHex(), expected);
  return chooser.getBestHex();
 }
 void checkForecast()
 {
  const auto actorHP = actor->getAvailableHealth(), enemyHP = enemy->getAvailableHealth();
  const auto actorPosition = actor->getPosition(), enemyPosition = enemy->getPosition();
  const auto remainingRetaliations = enemy->counterAttacks.available();
  const int landing = sourceLanding();
  const auto reach = view->getReachability(actor);
  const auto distance = BattleHex::getDistance(actorPosition, enemyPosition);
  ASSERT_EQ(reach.distances.at(landing), distance - 1);
  unsigned ties = 0;
  for(const auto hex : enemyPosition.getNeighbouringTiles())
   if(hex.isAvailable() && reach.distances.at(hex.toInt()) == distance - 1) ++ties;
  ASSERT_GE(ties, 2u);
  const auto forecast = HoMM3Forecast::nativeShooterPrefix(*view, *lifecycle, true);
  ASSERT_TRUE(forecast.outcome) << forecast.unavailable;
  ASSERT_EQ(forecast.outcome->melees.size(), 1u);
  auto projectedActor = actor->acquireState(), projectedEnemy = enemy->acquireState();
  projectedActor->setPosition(BattleHex(landing));
  auto hit = view->battleEstimateDamage(BattleAttackInfo(projectedActor.get(), projectedEnemy.get(), 0, false));
  int64_t damage = hit.damage.min + (hit.damage.max - hit.damage.min) / 2; projectedEnemy->damage(damage);
  if(projectedEnemy->alive() && remainingRetaliations)
  {
   hit = view->battleEstimateDamage(BattleAttackInfo(projectedEnemy.get(), projectedActor.get(), 0, false));
   damage = hit.damage.min + (hit.damage.max - hit.damage.min) / 2; projectedActor->damage(damage);
  }
  EXPECT_EQ(forecast.outcome->melees[0].damage.targetDamage, enemyHP - projectedEnemy->getAvailableHealth());
  EXPECT_EQ(forecast.outcome->melees[0].damage.actorDamage, actorHP - projectedActor->getAvailableHealth());
  EXPECT_EQ(actor->getPosition(), actorPosition); EXPECT_EQ(enemy->getPosition(), enemyPosition);
  EXPECT_EQ(actor->getAvailableHealth(), actorHP); EXPECT_EQ(enemy->getAvailableHealth(), enemyHP);
  EXPECT_EQ(enemy->counterAttacks.available(), remainingRetaliations);
 }
};
}

TEST_F(HoMM3TiedLanding, SourceOrderedTiesProjectLethalAndSuppressControllerSpell)
{
 prepare(); ASSERT_FALSE(HasFatalFailure());
 checkForecast(); ASSERT_FALSE(HasFatalFailure());
 const auto withoutForecast = AIIntegration::HoMM3SpellPolicy::choose(view, environment.get(), *actor);
 ASSERT_TRUE(withoutForecast); EXPECT_EQ(withoutForecast->spell, SpellID::MAGIC_ARROW);
 const auto mana = attackerSideHero->mana;
 controller->activeStack(battle()->getBattleID(), actor);
 EXPECT_FALSE(callback->spell); ASSERT_TRUE(callback->action);
 EXPECT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK);
 EXPECT_EQ(attackerSideHero->mana, mana);
 ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(), attackerSideHero->getOwner(), *callback->action));
 ASSERT_EQ(server.battleResults.size(), 1u); EXPECT_EQ(server.battleResults.back().winner, BattleSide::ATTACKER);
 EXPECT_EQ(attackerSideHero->mana, mana);
}

TEST_F(HoMM3TiedLanding, SurvivingTargetKeepsSpellAndServerCast)
{
 prepare(100); ASSERT_FALSE(HasFatalFailure());
 checkForecast(); ASSERT_FALSE(HasFatalFailure());
 const auto forecast = HoMM3Forecast::nativeShooterPrefix(*view, *lifecycle, true);
 ASSERT_TRUE(forecast.outcome); EXPECT_FALSE(forecast.outcome->winLikely); EXPECT_FALSE(forecast.outcome->spellsNotRequired);
 const auto mana = attackerSideHero->mana;
 const auto enemyHP = enemy->getAvailableHealth();
 controller->activeStack(battle()->getBattleID(), actor);
 ASSERT_TRUE(callback->spell); EXPECT_EQ(callback->spell->spell, SpellID::MAGIC_ARROW); EXPECT_FALSE(callback->action);
 ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(), attackerSideHero->getOwner(), *callback->spell));
 EXPECT_FALSE(server.castsOf(SpellID(SpellID::MAGIC_ARROW)).empty());
 EXPECT_LT(attackerSideHero->mana, mana);
 EXPECT_LT(enemy->getAvailableHealth(), enemyHP);
}

TEST_F(HoMM3TiedLanding, SourceGeometryAcrossParitiesAndDirections)
{
 prepare(100); ASSERT_FALSE(HasFatalFailure());
 unsigned checked = 0;
 for(const int origin : {74, 75, 91, 92})
 {
  actor->setPosition(BattleHex(origin));
  for(int destination = 0; destination < GameConstants::BFIELD_SIZE; ++destination)
  {
   const BattleHex hex(destination);
   if(!hex.isAvailable()) continue;
   const auto distance = BattleHex::getDistance(actor->getPosition(), hex);
   if(distance < 2 || distance > enemy->getMovementRange()) continue;
   enemy->setPosition(hex);
   const auto reach = view->getReachability(actor); unsigned ties = 0;
   for(const auto adjacent : hex.getNeighbouringTiles())
    if(adjacent.isAvailable() && reach.distances.at(adjacent.toInt()) == distance - 1) ++ties;
   if(ties < 2) continue;
   SCOPED_TRACE(origin);
   SCOPED_TRACE(destination);
   checkForecast(); ASSERT_FALSE(HasFatalFailure()); ++checked;
  }
 }
 EXPECT_GT(checked, 40u);
 RecordProperty("geometry_cases", checked);
}

TEST_F(HoMM3TiedLanding, ThreatRangeAndSpecialAttackRemainUnavailable)
{
 prepare(); ASSERT_FALSE(HasFatalFailure());
 ASSERT_TRUE(HoMM3Forecast::nativeShooterPrefix(*view, *lifecycle, true).outcome);
 EXPECT_FALSE(HoMM3Forecast::nativeShooterPrefix(*view, *lifecycle, false).outcome);
 auto slow = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED,
  BonusSource::CREATURE_ABILITY, -3, BonusSourceID(creatureByName("core:pikeman")));
 enemy->addNewBonus(slow);
 EXPECT_FALSE(HoMM3Forecast::nativeShooterPrefix(*view, *lifecycle, true).outcome);
 enemy->removeBonus(slow);
 enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::RANGED_RETALIATION,
  BonusSource::CREATURE_ABILITY, 1, BonusSourceID(creatureByName("core:pikeman"))));
 EXPECT_FALSE(HoMM3Forecast::nativeShooterPrefix(*view, *lifecycle, true).outcome);
 controller->activeStack(battle()->getBattleID(), actor);
 ASSERT_TRUE(callback->spell); EXPECT_EQ(callback->spell->spell, SpellID::MAGIC_ARROW);
}

TEST_F(HoMM3TiedLanding, DefenderSideMirroredSourceLandingAndServerExchange)
{
 prepare(100, true); ASSERT_FALSE(HasFatalFailure());
 actor->setPosition(BattleHex(112)); enemy->setPosition(BattleHex(92));
 checkForecast(); ASSERT_FALSE(HasFatalFailure());
 const auto actorHP = actor->getAvailableHealth(), enemyHP = enemy->getAvailableHealth();
 const auto landing = sourceLanding();
 ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(), attackerSideHero->getOwner(),
  BattleAction::makeMeleeAttack(actor, enemy, BattleHex(landing))));
 EXPECT_LT(actor->getAvailableHealth(), actorHP); EXPECT_LT(enemy->getAvailableHealth(), enemyHP);
}
