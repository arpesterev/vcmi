#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "AI/BattleAI/StackWithBonuses.h"
#include "lib/gameState/GameStatePackVisitor.h"
#include "lib/battle/CObstacleInstance.h"
#include "lib/bonuses/Limiters.h"
#include "lib/bonuses/Updaters.h"
#include "lib/serializer/JsonSerializer.h"
#include "lib/GameLibrary.h"
#include "lib/ObstacleHandler.h"

class OwnedResurrectionPacketTest : public BattleTestFixture
{
public:
 void SetUp() override { BattleTestFixture::SetUp(); startGame(); startBattle(); }
 CStack * add(int x = 5, BattleSide side = BattleSide::ATTACKER, bool wide = false)
 { return addStack(side, creatureByName(wide ? "core:greenDragon" : "core:woodElf"), BattleHex(x, 5), 10); }
 static UnitChanges state(CStack * unit, const JsonNode & data, int64_t delta = 1)
 { UnitChanges result(unit->unitId(), BattleChanges::EOperation::UPDATE); result.data = data; result.healthDelta = delta; return result; }
 static JsonNode dead(CStack * unit)
 {
  auto result = unit->save(); auto health = unit->health; health.reset();
  JsonNode serialized; JsonSerializer serializer(nullptr, serialized); serializer.serializeStruct("health", health);
  result["state"]["health"] = serialized["health"]; return result;
 }
 BattleUnitsChanged packet(std::vector<UnitChanges> changes)
 { BattleUnitsChanged result; result.battleID = battle()->getBattleID(); result.changedStacks = std::move(changes); return result; }
 void native(CPackForClient & value) { GameStatePackVisitor visitor(*server.gameState); value.visit(visitor); }
 void kill(CStack * unit) { auto value = packet({state(unit, dead(unit), -1)}); native(value); }
 void same(HypotheticBattle & frame)
 {
  for(const auto & unit : battle()->stacks)
  {
   auto owned = frame.getForUpdate(unit->unitId()); ASSERT_TRUE(owned); EXPECT_EQ(owned->save(), unit->save());
   const auto a = owned->getAllBonuses(Selector::all), b = unit->getAllBonuses(Selector::all);
   ASSERT_EQ(a->size(), b->size());
   for(size_t i = 0; i < a->size(); ++i) EXPECT_EQ((*a)[i]->toJsonNode(), (*b)[i]->toJsonNode());
   EXPECT_EQ(owned->getMaxHealth(), unit->getMaxHealth()); EXPECT_EQ(owned->getCasterOwner(), unit->getCasterOwner());
   EXPECT_EQ(owned->shots.available(), unit->shots.available()); EXPECT_EQ(owned->casts.available(), unit->casts.available());
  }
 }
 std::shared_ptr<SpellCreatedObstacle> obstacle(BattleHex hex, bool blocking)
 {
  auto result = std::make_shared<SpellCreatedObstacle>(); result->hidden = true; result->passable = !blocking;
  result->customSize.insert(hex); battle()->obstacles.push_back(result); return result;
 }
 static std::shared_ptr<Bonus> speed(int value, SpellID spell = SpellID(SpellID::HASTE))
 { return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, value, spell); }
};

TEST_F(OwnedResurrectionPacketTest, LivingPositiveLoadsBypassOccupancyAndPreserveAttackedEffects)
{
 auto * unit = add(); add(); obstacle(unit->position, true);
 auto full = unit->save(); int64_t loss = unit->getMaxHealth() + 2; unit->damage(loss);
 auto effect = speed(7); effect->duration = BonusDuration::UNTIL_BEING_ATTACKED; unit->addNewBonus(effect);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto retained = frame->getForUpdate(unit->unitId());
 for(auto delta : {int64_t(1), int64_t(0), int64_t(200)})
 {
  auto value = packet({state(unit, full, delta)}); frame->applyOwnedStatePacket(value); native(value); same(*frame);
  EXPECT_TRUE(retained->hasBonus(Bonus::UntilBeingAttacked)); EXPECT_EQ(frame->getForUpdate(unit->unitId()).get(), retained.get());
 }
}

TEST_F(OwnedResurrectionPacketTest, OldDeadPositiveGateSkipsMalformedPayloadAndContinues)
{
 auto * corpse = add(); auto alive = corpse->save(); kill(corpse); add(); auto * other = add(10);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); const auto before = corpse->save();
 auto malformed = alive; malformed["state"]["unexpected"] = JsonNode(true);
 auto moved = other->save(); moved["state"]["position"] = JsonNode(int64_t(BattleHex(11, 5).toInt()));
 auto value = packet({state(corpse, malformed), state(other, moved)});
 frame->applyOwnedStatePacket(value); native(value); same(*frame);
 EXPECT_EQ(frame->getForUpdate(corpse->unitId())->save(), before); EXPECT_EQ(other->save(), moved);
}

TEST_F(OwnedResurrectionPacketTest, BlockedCorpsePositiveNoopDoesNotManufactureInvalidation)
{
 auto * corpse = add(); kill(corpse); obstacle(corpse->position, true);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto owned = frame->getForUpdate(corpse->unitId());
 const auto revision = frame->getTreeVersion(), epoch = owned->getTreeVersion(), nativeEpoch = corpse->getTreeVersion();
 auto value = packet({state(corpse, corpse->save())}); frame->applyOwnedStatePacket(value); native(value);
 EXPECT_EQ(frame->getTreeVersion(), revision); EXPECT_EQ(owned->getTreeVersion(), epoch); EXPECT_EQ(corpse->getTreeVersion(), nativeEpoch); same(*frame);
}

TEST_F(OwnedResurrectionPacketTest, ZeroAndNegativeRevivalBypassBlockerAndCleanSpells)
{
 for(auto delta : {int64_t(0), int64_t(-1)})
 {
  auto * corpse = add(); auto alive = corpse->save(); kill(corpse); obstacle(corpse->position, true);
  corpse->addNewBonus(speed(17)); corpse->addNewBonus(speed(19, SpellID(SpellID::DISRUPTING_RAY)));
  auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto value = packet({state(corpse, alive, delta)});
  frame->applyOwnedStatePacket(value); native(value); same(*frame); EXPECT_TRUE(corpse->alive());
  EXPECT_FALSE(frame->getForUpdate(corpse->unitId())->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::HASTE))));
  EXPECT_TRUE(frame->getForUpdate(corpse->unitId())->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::DISRUPTING_RAY))));
 }
}

TEST_F(OwnedResurrectionPacketTest, HiddenBlockingVersusAffectedOnlyObstacleUsesCapturedGeometry)
{
 for(bool blocking : {false, true})
 {
  auto * corpse = add(blocking ? 9 : 5); auto alive = corpse->save(); kill(corpse);
  auto source = obstacle(corpse->position, blocking);
  auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto fork = frame->forkOwnedFixture();
  auto value = packet({state(corpse, alive)}); native(value); const auto expected = corpse->save();
  source->passable = blocking; source->customSize.clear();
  frame->applyOwnedStatePacket(value); fork->applyOwnedStatePacket(value);
  EXPECT_EQ(frame->getForUpdate(corpse->unitId())->save(), expected); EXPECT_EQ(fork->getForUpdate(corpse->unitId())->save(), expected);
  EXPECT_EQ(corpse->alive(), !blocking);
 }
}

TEST_F(OwnedResurrectionPacketTest, GateUsesOldPositionNotLoadedDestination)
{
 auto * corpse = add(); auto alive = corpse->save(); kill(corpse); auto * blocker = add();
 alive["state"]["position"] = JsonNode(int64_t(BattleHex(9, 5).toInt()));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto value = packet({state(corpse, alive)});
 frame->applyOwnedStatePacket(value); native(value); same(*frame); EXPECT_FALSE(corpse->alive());
 auto blockerMove = blocker->save(); blockerMove["state"]["position"] = alive["state"]["position"];
 value = packet({state(blocker, blockerMove), state(corpse, alive)});
 frame->applyOwnedStatePacket(value); native(value); same(*frame); EXPECT_TRUE(corpse->alive()); EXPECT_EQ(corpse->position, blocker->position);
}

TEST_F(OwnedResurrectionPacketTest, SharedCorpseHexAndEarlierDeathUseSequentialOccupancy)
{
 auto * first = add(); auto * second = add(); auto firstAlive = first->save(), secondAlive = second->save(); kill(first); kill(second);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle());
 auto value = packet({state(first, firstAlive), state(second, secondAlive)}); frame->applyOwnedStatePacket(value); native(value); same(*frame);
 EXPECT_TRUE(first->alive()); EXPECT_FALSE(second->alive());
 value = packet({state(first, dead(first), -1), state(second, secondAlive)}); frame->applyOwnedStatePacket(value); native(value); same(*frame);
 EXPECT_FALSE(first->alive()); EXPECT_TRUE(second->alive());
}

TEST_F(OwnedResurrectionPacketTest, BothWideSidesCheckSecondaryHex)
{
 for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
 {
  auto * corpse = add(side == BattleSide::ATTACKER ? 5 : 11, side, true); auto alive = corpse->save(); kill(corpse);
  auto * blocker = add(side == BattleSide::ATTACKER ? 4 : 12);
  ASSERT_TRUE(corpse->coversPos(blocker->position));
  auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto value = packet({state(corpse, alive)});
  frame->applyOwnedStatePacket(value); native(value); same(*frame); EXPECT_FALSE(corpse->alive());
 }
}

TEST_F(OwnedResurrectionPacketTest, IndirectPrepassBeforeRepeatedDeathAndBlockedRevivalStillContinues)
{
 auto * corpse = add(); auto alive = corpse->save(); add(); auto * other = add(10);
 auto effect = speed(7); effect->duration = BonusDuration::UNTIL_TAKING_INDIRECT_DAMAGE; corpse->addNewBonus(effect);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle());
 StacksInjured value; value.battleID = battle()->getBattleID(); BattleStackAttacked death, blocked, next;
 death.stackAttacked = corpse->unitId(); death.newState = state(corpse, dead(corpse), -1);
 blocked.stackAttacked = corpse->unitId(); blocked.newState = state(corpse, alive);
 next.stackAttacked = other->unitId(); auto moved = other->save(); moved["state"]["position"] = JsonNode(int64_t(BattleHex(11, 5).toInt())); next.newState = state(other, moved);
 value.stacks = {death, blocked, next}; frame->applyOwnedStatePacket(value); native(value); same(*frame);
 EXPECT_FALSE(corpse->alive()); EXPECT_EQ(other->save(), moved);
 EXPECT_FALSE(frame->getForUpdate(corpse->unitId())->hasBonus(Bonus::UntilTakingIndirectDamage));
}

TEST_F(OwnedResurrectionPacketTest, WarmAndColdNeighborsFollowNativeInvalidationAfterRevival)
{
 for(bool warm : {false, true})
 {
  auto * corpse = add(warm ? 10 : 5); auto alive = corpse->save(); kill(corpse); auto * neighbor = add(warm ? 11 : 6);
  auto effect = speed(101); auto limiter = std::make_shared<UnitAdjacentLimiter>(); limiter->targetUnit = corpse->creatureId(); effect->limiter = limiter; neighbor->addNewBonus(effect);
  const CSelector selected = [](const Bonus * bonus) { return bonus->val == 101; };
  if(warm) ASSERT_FALSE(neighbor->hasBonus(selected));
  else neighbor->nodeHasChanged();
  auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto owned = frame->getForUpdate(neighbor->unitId());
  const auto epoch = owned->getTreeVersion(), nativeEpoch = neighbor->getTreeVersion();
  auto value = packet({state(corpse, alive)}); frame->applyOwnedStatePacket(value); native(value);
  EXPECT_EQ(owned->getTreeVersion(), epoch); EXPECT_EQ(neighbor->getTreeVersion(), nativeEpoch);
  EXPECT_EQ(owned->hasBonus(selected), !warm); EXPECT_EQ(neighbor->hasBonus(selected), !warm);
  value = packet({state(neighbor, neighbor->save(), -1)}); frame->applyOwnedStatePacket(value); native(value);
  EXPECT_TRUE(owned->hasBonus(selected)); EXPECT_TRUE(neighbor->hasBonus(selected)); same(*frame);
 }
}

TEST_F(OwnedResurrectionPacketTest, PositiveCountLoadRetainsWarmUpdaterUntilNegativeDelta)
{
 auto * unit = add(); auto full = unit->save(); int64_t loss = unit->getMaxHealth(); unit->damage(loss);
 auto effect = speed(1, SpellID(SpellID::DISRUPTING_RAY)); effect->updater = std::make_shared<TimesStackSizeUpdater>(); unit->addNewBonus(effect);
 const auto selected = Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::DISRUPTING_RAY));
 ASSERT_EQ(unit->getAllBonuses(selected)->front()->val, 9);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto owned = frame->getForUpdate(unit->unitId());
 auto value = packet({state(unit, full)}); frame->applyOwnedStatePacket(value); native(value);
 EXPECT_EQ(owned->getCount(), 10); EXPECT_EQ(unit->getAllBonuses(selected)->front()->val, 9); EXPECT_EQ(owned->getAllBonuses(selected)->front()->val, 9);
 value = packet({state(unit, full, -1)}); frame->applyOwnedStatePacket(value); native(value);
 EXPECT_EQ(unit->getAllBonuses(selected)->front()->val, 10); EXPECT_EQ(owned->getAllBonuses(selected)->front()->val, 10); same(*frame);
}

TEST_F(OwnedResurrectionPacketTest, LateFailureRollsBackRevivalAndCorrectedReplayKeepsAliases)
{
 auto * corpse = add(); auto alive = corpse->save(); kill(corpse); auto * other = add(10); corpse->addNewBonus(speed(17));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto sibling = frame->forkOwnedFixture(); auto retained = frame->getForUpdate(corpse->unitId());
 auto before = retained->save(); const auto epoch = retained->getTreeVersion();
 auto malformed = other->save(); malformed["state"]["unexpected"] = JsonNode(true);
 auto value = packet({state(corpse, alive), state(other, malformed)});
 EXPECT_THROW(frame->applyOwnedStatePacket(value), std::invalid_argument);
 EXPECT_EQ(retained->save(), before); EXPECT_EQ(retained->getTreeVersion(), epoch); EXPECT_EQ(sibling->getForUpdate(corpse->unitId())->save(), before);
 value.changedStacks.pop_back(); frame->applyOwnedStatePacket(value); native(value); same(*frame);
 EXPECT_EQ(frame->getForUpdate(corpse->unitId()).get(), retained.get()); EXPECT_EQ(sibling->getForUpdate(corpse->unitId())->save(), before);
}

TEST_F(OwnedResurrectionPacketTest, CapturedBlockerSurvivesNativeDestructionAndRegistryFreeFork)
{
 auto * corpse = add(); const auto id = corpse->unitId(); auto alive = corpse->save(); kill(corpse); obstacle(corpse->position, true);
 auto value = packet({state(corpse, alive)}); auto frame = HypotheticBattle::captureOwnedFixture(*battle()); native(value); const auto expected = corpse->save();
 std::weak_ptr<CGameState> lifetime = gameState(); server.gameState.reset(); BattleTestFixture::TearDown(); ASSERT_TRUE(lifetime.expired());
 struct NoLibrary { GameLibrary * saved = LIBRARY; NoLibrary() { LIBRARY = nullptr; } ~NoLibrary() { LIBRARY = saved; } } unavailable;
 auto fork = frame->forkOwnedFixture(); frame->applyOwnedStatePacket(value); fork->applyOwnedStatePacket(value);
 EXPECT_EQ(frame->getForUpdate(id)->save(), expected); EXPECT_EQ(fork->getForUpdate(id)->save(), expected);
}

TEST_F(OwnedResurrectionPacketTest, SentinelAndSideColumnFootprintsMatchNativeWithoutMovementValidation)
{
 for(auto position : {int16_t(-4), int16_t(-3), int16_t(-2), int16_t(-1), int16_t(0), int16_t(GameConstants::BFIELD_SIZE - 1)})
 {
  auto * corpse = add(5); corpse->position = BattleHex(position); auto alive = corpse->save(); kill(corpse);
  auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto value = packet({state(corpse, alive)});
  frame->applyOwnedStatePacket(value); native(value); same(*frame); EXPECT_TRUE(corpse->alive());
  // Remove occupancy before the next boundary fixture without altering identity.
  value = packet({state(corpse, dead(corpse), -1)}); frame->applyOwnedStatePacket(value); native(value); same(*frame);
 }
}

TEST_F(OwnedResurrectionPacketTest, RevivalRemovesDescendantExportsButKeepsPersistentEffects)
{
 auto * corpse = add(); auto alive = corpse->save(); kill(corpse); auto * child = add(8);
 child->attachTo(*corpse);
 struct Detach { CStack * child; CStack * parent; ~Detach() { child->detachFrom(*parent); } } detach{child, corpse};
 corpse->addNewBonus(speed(17)); child->addNewBonus(speed(19));
 corpse->addNewBonus(speed(23, SpellID(SpellID::DISRUPTING_RAY)));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto value = packet({state(corpse, alive)});
 frame->applyOwnedStatePacket(value); native(value); same(*frame);
 const auto selected = Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::HASTE));
 EXPECT_FALSE(frame->getForUpdate(corpse->unitId())->hasBonus(selected)); EXPECT_FALSE(frame->getForUpdate(child->unitId())->hasBonus(selected));
 EXPECT_TRUE(frame->getForUpdate(corpse->unitId())->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::DISRUPTING_RAY))));
}

TEST_F(OwnedResurrectionPacketTest, AttackAttackerChangesReviveBeforeOrderedVictimsAndFinalExpiry)
{
 auto * attacker = add(10); auto * corpse = add(); auto alive = corpse->save(); kill(corpse);
 auto * blockedCorpse = add(8); auto blockedAlive = blockedCorpse->save(); kill(blockedCorpse); add(8);
 auto effect = speed(7); effect->duration = BonusDuration::UNTIL_ATTACK; attacker->addNewBonus(effect);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle());
 BattleAttack value; value.battleID = battle()->getBattleID(); value.attackerChanges.battleID = value.battleID; value.stackAttacking = attacker->unitId();
 value.attackerChanges.changedStacks = {state(corpse, alive)};
 BattleStackAttacked blocked, victim; blocked.stackAttacked = blockedCorpse->unitId(); blocked.newState = state(blockedCorpse, blockedAlive);
 victim.stackAttacked = corpse->unitId(); victim.newState = state(corpse, dead(corpse), -1); value.bsa = {blocked, victim};
 frame->applyOwnedStatePacket(value); native(value); same(*frame);
 EXPECT_FALSE(corpse->alive()); EXPECT_FALSE(blockedCorpse->alive()); EXPECT_FALSE(frame->getForUpdate(attacker->unitId())->hasBonus(Bonus::UntilAttack));
}

TEST_F(OwnedResurrectionPacketTest, CloneRevivalAndRepeatedDeathPreserveNativeReverseLinkPhases)
{
 auto * original = add(10); auto * clone = add(); clone->cloned = true; auto alive = clone->save(); kill(clone);
 original->cloneID = clone->unitId();
 auto frame = HypotheticBattle::captureOwnedFixture(*battle());
 auto value = packet({state(clone, alive)}); frame->applyOwnedStatePacket(value); native(value); same(*frame);
 EXPECT_TRUE(clone->alive()); EXPECT_EQ(frame->getForUpdate(original->unitId())->cloneID, original->cloneID);
 value = packet({state(clone, dead(clone), -1), state(clone, dead(clone), 0)});
 frame->applyOwnedStatePacket(value); native(value); same(*frame);
 EXPECT_EQ(original->cloneID, -1); EXPECT_EQ(frame->getForUpdate(original->unitId())->cloneID, -1);
}

TEST_F(OwnedResurrectionPacketTest, RevivalHealthSpellCleanupDoesNotClampProducedFirstHealth)
{
 auto * corpse = add(); const auto baseMaximum = corpse->getMaxHealth(); auto alive = corpse->save(); kill(corpse);
 auto health = speed(100); health->type = BonusType::STACK_HEALTH; corpse->addNewBonus(health);
 alive["state"]["health"]["firstHPleft"] = JsonNode(int64_t(baseMaximum + 50));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto value = packet({state(corpse, alive)});
 frame->applyOwnedStatePacket(value); native(value); same(*frame);
 auto owned = frame->getForUpdate(corpse->unitId());
 EXPECT_GT(owned->health.getFirstHPleft(), owned->getMaxHealth()); EXPECT_EQ(owned->health.getFirstHPleft(), corpse->health.getFirstHPleft());
 EXPECT_EQ(owned->getMaxHealth(), baseMaximum);
}

TEST_F(OwnedResurrectionPacketTest, LivingGhostPendingBlocksButDeadAndGhostOccupantsDoNot)
{
 for(int mode = 0; mode < 3; ++mode)
 {
  const int x = 4 + mode * 4; auto * corpse = add(x); auto alive = corpse->save(); kill(corpse); auto * occupant = add(x);
  if(mode == 0) occupant->ghostPending = true;
  if(mode == 1) kill(occupant);
  if(mode == 2) occupant->ghost = true;
  auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto value = packet({state(corpse, alive)});
  frame->applyOwnedStatePacket(value); native(value); same(*frame); EXPECT_EQ(corpse->alive(), mode != 0);
 }
}

TEST_F(OwnedResurrectionPacketTest, DefenderWideSentinelCoversZeroAndWideSideColumnRemainsOccupied)
{
 auto * corpse = add(); corpse->position = BattleHex(0); auto alive = corpse->save(); kill(corpse);
 auto * blocker = add(10, BattleSide::DEFENDER, true); blocker->position = BattleHex(-1);
 ASSERT_TRUE(blocker->coversPos(BattleHex(0))); ASSERT_TRUE(blocker->getHexes().empty());
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto value = packet({state(corpse, alive)});
 frame->applyOwnedStatePacket(value); native(value); same(*frame); EXPECT_FALSE(corpse->alive());
 auto * wide = add(1, BattleSide::ATTACKER, true); wide->position = BattleHex(1); auto wideAlive = wide->save(); kill(wide);
 ASSERT_TRUE(wide->coversPos(BattleHex(0)));
 frame = HypotheticBattle::captureOwnedFixture(*battle()); value = packet({state(wide, wideAlive)});
 frame->applyOwnedStatePacket(value); native(value); same(*frame); EXPECT_FALSE(wide->alive());
}

TEST_F(OwnedResurrectionPacketTest, LoadedCloneLinkGhostsOnlyNewLinkedUnitAndCanReviveThatClone)
{
 auto * original = add(4); auto * oldClone = add(8); auto * newClone = add(12);
 oldClone->cloned = newClone->cloned = true; original->cloneID = oldClone->unitId();
 auto cloneAlive = newClone->save(); auto originalDeath = dead(original); originalDeath["state"]["cloneID"] = JsonNode(int64_t(newClone->unitId()));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto value = packet({state(original, originalDeath, -1)});
 frame->applyOwnedStatePacket(value); native(value); same(*frame);
 EXPECT_TRUE(oldClone->alive()); EXPECT_FALSE(newClone->alive()); EXPECT_TRUE(newClone->ghostPending);
 value = packet({state(newClone, cloneAlive)}); frame->applyOwnedStatePacket(value); native(value); same(*frame);
 EXPECT_TRUE(newClone->alive()); EXPECT_FALSE(newClone->ghostPending);
}

TEST_F(OwnedResurrectionPacketTest, RegistryBackedNormalAndAbsoluteBlockersReplayAfterNativeDestruction)
{
 std::array<std::shared_ptr<CObstacleInstance>, 2> obstacles;
 LIBRARY->obstacles()->forEach([&](const ObstacleInfo * definition, bool & stop)
 {
  const size_t index = definition->isAbsoluteObstacle ? 1 : 0;
  if(obstacles[index] || definition->blockedTiles.empty()) return;
  auto instance = std::make_shared<CObstacleInstance>();
  instance->ID = definition->getIndex();
  instance->obstacleType = definition->isAbsoluteObstacle ? CObstacleInstance::ABSOLUTE_OBSTACLE : CObstacleInstance::USUAL;
  instance->pos = definition->isAbsoluteObstacle ? BattleHex(-1) : BattleHex(8, 8);
  if(instance->getBlockedTiles().empty()) return;
  obstacles[index] = std::move(instance);
  stop = obstacles[0] && obstacles[1];
 });
 ASSERT_TRUE(obstacles[0]); ASSERT_TRUE(obstacles[1]);
 std::vector<uint32_t> ids; std::vector<JsonNode> expected; std::vector<UnitChanges> changes;
 std::vector<std::weak_ptr<CObstacleInstance>> obstacleLifetimes;
 for(auto & instance : obstacles)
 {
  const auto blocked = instance->getBlockedTiles(); ASSERT_FALSE(blocked.empty());
  auto * corpse = add(); corpse->position = blocked.front(); auto alive = corpse->save(); kill(corpse);
  ids.push_back(corpse->unitId()); expected.push_back(corpse->save()); changes.push_back(state(corpse, alive));
  obstacleLifetimes.push_back(instance); battle()->obstacles.push_back(instance);
 }
 auto value = packet(changes); auto frame = HypotheticBattle::captureOwnedFixture(*battle());
 native(value);
 for(size_t i = 0; i < ids.size(); ++i) EXPECT_EQ(battle()->getStack(ids[i], false)->save(), expected[i]);
 for(auto & instance : obstacles) instance.reset();
 std::weak_ptr<CGameState> lifetime = gameState(); server.gameState.reset(); BattleTestFixture::TearDown(); ASSERT_TRUE(lifetime.expired());
 for(const auto & obstacleLifetime : obstacleLifetimes) EXPECT_TRUE(obstacleLifetime.expired());
 struct NoLibrary { GameLibrary * saved = LIBRARY; NoLibrary() { LIBRARY = nullptr; } ~NoLibrary() { LIBRARY = saved; } } unavailable;
 auto fork = frame->forkOwnedFixture(); frame->applyOwnedStatePacket(value); fork->applyOwnedStatePacket(value);
 for(size_t i = 0; i < ids.size(); ++i)
 {
  EXPECT_EQ(frame->getForUpdate(ids[i])->save(), expected[i]); EXPECT_EQ(fork->getForUpdate(ids[i])->save(), expected[i]);
 }
}

TEST_F(OwnedResurrectionPacketTest, CanonicalTemporaryAndPermanentResurrectedCountersMatchNative)
{
 for(auto power : {EHealPower::ONE_BATTLE, EHealPower::PERMANENT})
 {
  auto * corpse = add(power == EHealPower::ONE_BATTLE ? 5 : 10); kill(corpse);
  auto health = corpse->health; int64_t amount = corpse->getMaxHealth() * 3 + 1;
  health.heal(amount, EHealLevel::RESURRECT, power);
  JsonNode serialized; JsonSerializer serializer(nullptr, serialized); serializer.serializeStruct("health", health);
  auto revived = corpse->save(); revived["state"]["health"] = serialized["health"];
  auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto value = packet({state(corpse, revived, amount)});
  frame->applyOwnedStatePacket(value); native(value); same(*frame);
  auto owned = frame->getForUpdate(corpse->unitId());
  EXPECT_EQ(owned->health.getResurrected(), corpse->health.getResurrected());
  EXPECT_EQ(owned->health.getResurrected(), power == EHealPower::ONE_BATTLE ? owned->getCount() : 0);
  EXPECT_EQ(owned->save(), revived);
 }
}
