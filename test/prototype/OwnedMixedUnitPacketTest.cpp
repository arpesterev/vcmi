#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "AI/BattleAI/StackWithBonuses.h"
#include "lib/gameState/GameStatePackVisitor.h"
#include "lib/GameLibrary.h"
#include "lib/CCreatureHandler.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/bonuses/Updaters.h"
#include "lib/bonuses/Limiters.h"
#include "lib/bonuses/OwnedBonusLogicalDomain.h"
#include "lib/networkPacks/SetStackEffect.h"
#include <vcmi/Artifact.h>
#include <vcmi/ArtifactService.h>

class OwnedMixedUnitPacketTest : public BattleTestFixture
{
public:
 using BattleTestFixture::apply;
 void SetUp() override
 {
  BattleTestFixture::SetUp(); startGame(); startBattle();
  const auto * primary = LIBRARY->artifacts()->getById(ArtifactID(ArtifactID::SPELLBOOK));
  const auto * secondary = LIBRARY->artifacts()->getById(ArtifactID(ArtifactID::SPELL_SCROLL));
  ASSERT_NE(primary, nullptr); ASSERT_NE(secondary, nullptr);
  ASSERT_EQ(primary->getId(), ArtifactID(ArtifactID::SPELLBOOK)); ASSERT_EQ(primary->getJsonKey(), "core:spellBook");
  ASSERT_EQ(secondary->getId(), ArtifactID(ArtifactID::SPELL_SCROLL)); ASSERT_EQ(secondary->getJsonKey(), "core:spellScroll");
 }
 CStack * add(int x = 5) { return addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(x, 5), 10); }
 UnitChanges addition(uint32_t id, int x = 8, CreatureID type = CreatureID::NONE, BattleSide side = BattleSide::ATTACKER)
 {
  battle::UnitInfo info; info.id = id; info.count = 10; info.type = type == CreatureID::NONE ? creatureByName("core:woodElf") : type;
  info.side = side; info.position = BattleHex(x, 5); info.summoned = true;
  UnitChanges result(id, BattleChanges::EOperation::ADD); info.save(result.data); return result;
 }
 static UnitChanges update(uint32_t id, const JsonNode & data, int64_t delta = -1)
 { UnitChanges result(id, BattleChanges::EOperation::UPDATE); result.data = data; result.healthDelta = delta; return result; }
 static UnitChanges removal(uint32_t id) { return UnitChanges(id, BattleChanges::EOperation::REMOVE); }
 BattleUnitsChanged packet(std::vector<UnitChanges> changes)
 { BattleUnitsChanged result; result.battleID = battle()->getBattleID(); result.changedStacks = std::move(changes); return result; }
 void native(CPackForClient & value) { GameStatePackVisitor visitor(*server.gameState); value.visit(visitor); }
 static std::vector<uint32_t> ids(const HypotheticBattle & frame)
 {
  std::vector<uint32_t> result;
  for(const auto * unit : frame.getUnitsIf([](const battle::Unit *) { return true; })) result.push_back(unit->unitId());
  return result;
 }
 void same(HypotheticBattle & frame)
 {
  auto units = frame.getUnitsIf([](const battle::Unit *) { return true; }); ASSERT_EQ(units.size(), battle()->stacks.size());
  for(size_t i = 0; i < units.size(); ++i)
  {
   const auto & source = battle()->stacks[i]; ASSERT_EQ(units[i]->unitId(), source->unitId());
   auto owned = frame.getForUpdate(source->unitId()); ASSERT_TRUE(owned); EXPECT_EQ(owned->save(), source->save());
   EXPECT_EQ(owned->getMaxHealth(), source->getMaxHealth()); EXPECT_EQ(owned->getCasterOwner(), source->getCasterOwner());
   EXPECT_EQ(frame.unitEffectiveOwner(owned.get()), source->unitEffectiveOwner(source.get()));
   EXPECT_EQ(frame.unitHasAmmoCart(owned.get()), source->unitHasAmmoCart(source.get()));
   EXPECT_EQ(owned->shots.available(), source->shots.available()); EXPECT_EQ(owned->casts.available(), source->casts.available());
   const auto a = owned->getAllBonuses(Selector::all), b = source->getAllBonuses(Selector::all); ASSERT_EQ(a->size(), b->size());
   for(size_t n = 0; n < a->size(); ++n) EXPECT_EQ((*a)[n]->toJsonNode(), (*b)[n]->toJsonNode());
  }
 }
 void apply(HypotheticBattle & frame, BattleUnitsChanged & value) { frame.applyOwnedStatePacket(value); native(value); same(frame); }
};

TEST_F(OwnedMixedUnitPacketTest, AddUpdateRemoveInOnePacketRetainsStorageAndExistingAliases)
{
 auto * source = add(); auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto sibling = frame->forkOwnedFixture();
 auto retained = frame->getForUpdate(source->unitId()); const auto before = ids(*frame);
 auto addEntry = addition(100); auto value = packet({addEntry}); native(value);
 auto moved = battle()->getStack(100, false)->save(); moved["state"]["position"] = JsonNode(int64_t(BattleHex(11, 5).toInt()));
 auto updateEntry = update(100, moved); value = packet({updateEntry, removal(100)}); native(value);
 value = packet({addEntry, updateEntry, removal(100)}); frame->applyOwnedStatePacket(value); same(*frame);
 EXPECT_EQ(frame->getForUpdate(source->unitId()).get(), retained.get());
 EXPECT_TRUE(frame->getForUpdate(100)->ghost); EXPECT_FALSE(frame->getForUpdate(100)->alive());
 auto expected = before; expected.push_back(100); EXPECT_EQ(ids(*frame), expected); EXPECT_EQ(ids(*sibling), before);
}

TEST_F(OwnedMixedUnitPacketTest, OriginalRemovalCascadesAndClearsEveryReverseLink)
{
 auto * original = add(4); auto * clone = add(8); auto * other = add(12);
 original->cloneID = clone->unitId(); other->cloneID = clone->unitId(); clone->cloned = true;
 ASSERT_TRUE(clone->getAllBonuses(Selector::source(BonusSource::OTHER, ArtifactID(ArtifactID::SPELLBOOK)))->empty());
 auto local = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 17, ArtifactID(ArtifactID::SPELLBOOK)); clone->addNewBonus(local);
 ASSERT_EQ(local->toJsonNode()["sourceID"].String(), "core:spellBook");
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto retained = frame->getForUpdate(clone->unitId());
 auto value = packet({removal(original->unitId())}); apply(*frame, value);
 EXPECT_TRUE(retained->ghost); EXPECT_FALSE(retained->ghostPending); EXPECT_EQ(retained->cloneID, -1);
 EXPECT_EQ(frame->getForUpdate(other->unitId())->cloneID, -1); EXPECT_TRUE(retained->hasBonus(Selector::source(BonusSource::OTHER, ArtifactID(ArtifactID::SPELLBOOK))));
 EXPECT_EQ(frame->getForUpdate(clone->unitId()).get(), retained.get());
}

TEST_F(OwnedMixedUnitPacketTest, MissingCascadeCommitsPartialRemovalAndContinuesNextEntry)
{
 auto * original = add(); original->cloneID = 999999; auto * other = add(11);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto moved = other->save(); moved["state"]["position"] = JsonNode(int64_t(BattleHex(12, 5).toInt()));
 auto value = packet({removal(original->unitId()), addition(100), update(other->unitId(), moved)}); apply(*frame, value);
 EXPECT_TRUE(frame->getForUpdate(original->unitId())->ghost); EXPECT_EQ(frame->getForUpdate(original->unitId())->cloneID, -1);
 EXPECT_EQ(frame->getForUpdate(other->unitId())->save(), moved); EXPECT_TRUE(frame->getForUpdate(100)->alive());
}

TEST_F(OwnedMixedUnitPacketTest, AlreadyGhostRemovalSkipsCascadeAndReverseLinkCleanup)
{
 auto * original = add(); auto * clone = add(8); auto * reverse = add(12);
 original->ghost = true; original->cloneID = clone->unitId(); reverse->cloneID = original->unitId();
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto before = original->save();
 auto value = packet({removal(original->unitId()), addition(100)}); apply(*frame, value);
 EXPECT_EQ(frame->getForUpdate(original->unitId())->save(), before); EXPECT_TRUE(clone->alive()); EXPECT_EQ(reverse->cloneID, original->unitId());
}

TEST_F(OwnedMixedUnitPacketTest, TombstoneDuplicateRejectsWithoutUndoingEarlierCommittedRemoval)
{
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto value = packet({addition(100), removal(100)}); apply(*frame, value);
 auto retained = frame->getForUpdate(100); const auto before = retained->save(); const auto order = ids(*frame); const auto revision = frame->getTreeVersion();
 value = packet({addition(101), addition(100)}); EXPECT_THROW(frame->applyOwnedStatePacket(value), std::invalid_argument);
 EXPECT_EQ(ids(*frame), order); EXPECT_EQ(frame->getTreeVersion(), revision); EXPECT_EQ(retained->save(), before); EXPECT_EQ(frame->getForUpdate(100).get(), retained.get());
}

TEST_F(OwnedMixedUnitPacketTest, FirstRemovedCartSuppressesLaterLivingCartInInsertionOrder)
{
 auto * shooter = add(); auto frame = HypotheticBattle::captureOwnedFixture(*battle());
 auto value = packet({addition(101, 8, CreatureID::AMMO_CART), addition(100, 10, CreatureID::AMMO_CART)}); apply(*frame, value);
 auto owned = frame->getForUpdate(shooter->unitId()); ASSERT_TRUE(frame->unitHasAmmoCart(owned.get()));
 auto order = ids(*frame); ASSERT_GE(order.size(), 2); EXPECT_EQ(order[order.size()-2], 101); EXPECT_EQ(order.back(), 100);
 value = packet({removal(101)}); apply(*frame, value); EXPECT_FALSE(frame->unitHasAmmoCart(owned.get())); EXPECT_TRUE(frame->getForUpdate(100)->alive());
}

TEST_F(OwnedMixedUnitPacketTest, AddedEnemyCartUsesInheritedEffectiveOwner)
{
 auto * shooter = add();
 auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:woodElf"), BattleHex(12, 5), 10);
 ASSERT_TRUE(defenderSideHero->getAllBonuses(Selector::source(BonusSource::OTHER, ArtifactID(ArtifactID::SPELLBOOK)))->empty());
 auto hypnosis = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::HYPNOTIZED, BonusSource::OTHER, 1, ArtifactID(ArtifactID::SPELLBOOK));
 // An army export also applies to subsequently added units.
 ASSERT_EQ(hypnosis->toJsonNode()["sourceID"].String(), "core:spellBook");
 defenderSideHero->addNewBonus(hypnosis);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto value = packet({addition(100, 10, CreatureID::AMMO_CART, BattleSide::DEFENDER)}); apply(*frame, value);
 EXPECT_NE(frame->unitEffectiveOwner(frame->getForUpdate(100).get()), enemy->unitOwner());
 EXPECT_TRUE(frame->unitHasAmmoCart(frame->getForUpdate(shooter->unitId()).get()));
}

TEST_F(OwnedMixedUnitPacketTest, LateMalformedUpdateRollsBackGrowthRemovalAndWarmAliases)
{
 auto * source = add(); auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto sibling = frame->forkOwnedFixture();
 auto retained = frame->getForUpdate(source->unitId()); retained->getAllBonuses(Selector::all); retained->getMovementRange(); retained->shots.available(); retained->getMaxHealth();
 const auto cacheBefore = retained->captureBonusCaches();
 const auto before = retained->save(); const auto order = ids(*frame); const auto revision = frame->getTreeVersion(), epoch = retained->getTreeVersion();
 auto malformed = source->save(); malformed["state"]["unexpected"] = JsonNode(true);
 auto value = packet({addition(100), removal(source->unitId()), update(source->unitId(), malformed)});
 EXPECT_THROW(frame->applyOwnedStatePacket(value), std::invalid_argument);
 EXPECT_EQ(ids(*frame), order); EXPECT_EQ(frame->getTreeVersion(), revision); EXPECT_EQ(retained->getTreeVersion(), epoch); EXPECT_EQ(retained->save(), before);
 const auto cacheAfter = retained->captureBonusCaches();
 EXPECT_EQ(cacheAfter.speed.listVersion, cacheBefore.speed.listVersion); EXPECT_EQ(cacheAfter.speed.membership, cacheBefore.speed.membership);
 for(size_t i = 0; i < cacheBefore.speed.entries.size(); ++i)
 { EXPECT_EQ(cacheAfter.speed.entries[i].version, cacheBefore.speed.entries[i].version); EXPECT_EQ(cacheAfter.speed.entries[i].value, cacheBefore.speed.entries[i].value); }
 for(size_t i = 0; i < cacheBefore.unit.size(); ++i)
 { EXPECT_EQ(cacheAfter.unit[i].version, cacheBefore.unit[i].version); EXPECT_EQ(cacheAfter.unit[i].value, cacheBefore.unit[i].value); }
 EXPECT_EQ(cacheAfter.shots.version, cacheBefore.shots.version); EXPECT_EQ(cacheAfter.shots.value, cacheBefore.shots.value);
 EXPECT_EQ(sibling->getForUpdate(source->unitId())->save(), before); EXPECT_EQ(ids(*sibling), order);
 value.changedStacks.pop_back(); apply(*frame, value); EXPECT_EQ(frame->getForUpdate(source->unitId()).get(), retained.get());
}

TEST_F(OwnedMixedUnitPacketTest, NewlyPublishedUnitRemainsBoundAcrossLaterGrowthAndMutation)
{
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto value = packet({addition(100)}); apply(*frame, value);
 auto retained = frame->getForUpdate(100); auto sibling = frame->forkOwnedFixture(); const auto before = retained->save();
 value = packet({addition(101, 11), addition(102, 12, CreatureID::AMMO_CART)}); apply(*frame, value);
 auto moved = battle()->getStack(100, false)->save(); moved["state"]["position"] = JsonNode(int64_t(BattleHex(7, 5).toInt()));
 value = packet({update(100, moved), removal(101)}); apply(*frame, value);
 EXPECT_EQ(frame->getForUpdate(100).get(), retained.get()); EXPECT_EQ(retained->save(), moved); EXPECT_TRUE(frame->unitHasAmmoCart(retained.get()));
 EXPECT_EQ(sibling->getForUpdate(100)->save(), before); EXPECT_NE(sibling->getForUpdate(100).get(), retained.get());
}

TEST_F(OwnedMixedUnitPacketTest, MixedReplayAndForkSurviveNativeAndLibraryDestruction)
{
 auto * source = add(); const auto id = source->unitId(); auto frame = HypotheticBattle::captureOwnedFixture(*battle());
 auto value = packet({addition(100), removal(id), addition(101, 11, CreatureID::AMMO_CART)}); native(value);
 std::vector<JsonNode> expected; for(const auto & unit : battle()->stacks) expected.push_back(unit->save());
 std::weak_ptr<CGameState> lifetime = gameState(); server.gameState.reset(); BattleTestFixture::TearDown(); ASSERT_TRUE(lifetime.expired());
 struct NoLibrary { GameLibrary * saved = LIBRARY; NoLibrary() { LIBRARY = nullptr; } ~NoLibrary() { LIBRARY = saved; } } unavailable;
 auto fork = frame->forkOwnedFixture(); frame->applyOwnedStatePacket(value); fork->applyOwnedStatePacket(value);
 for(auto target : {frame, fork})
 {
  auto order = ids(*target); ASSERT_EQ(order.size(), expected.size());
  for(size_t i = 0; i < order.size(); ++i) EXPECT_EQ(target->getForUpdate(order[i])->save(), expected[i]);
  auto added = target->getForUpdate(100); EXPECT_TRUE(target->unitHasAmmoCart(added.get()));
  auto descendant = target->forkOwnedFixture(); auto removeCart = value; removeCart.changedStacks = {removal(101)};
  target->applyOwnedStatePacket(removeCart); EXPECT_FALSE(target->unitHasAmmoCart(added.get())); EXPECT_TRUE(descendant->unitHasAmmoCart(descendant->getForUpdate(100).get()));
 }
}

TEST_F(OwnedMixedUnitPacketTest, MalformedLateAddRollsBackCanonicalValidationFailures)
{
 auto * source = add(); auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto retained = frame->getForUpdate(source->unitId());
 const auto before = retained->save(); const auto order = ids(*frame); const auto revision = frame->getTreeVersion();
 std::vector<UnitChanges> invalid;
 for(const auto & field : {"count", "side", "position"})
 {
  auto entry = addition(101); entry.data[field] = JsonNode(int64_t(9999999999LL)); invalid.push_back(entry);
  entry.data[field] = JsonNode(1.5); invalid.push_back(entry);
 }
 auto entry = addition(101); entry.data["type"] = JsonNode(std::string("core:missingCreature")); invalid.push_back(entry);
 entry = addition(101); entry.data["type"] = JsonNode(int64_t(1)); invalid.push_back(entry);
 entry = addition(101); entry.data["unexpected"] = JsonNode(true); invalid.push_back(entry);
 entry = addition(101); entry.data["summoned"] = JsonNode(int64_t(1)); invalid.push_back(entry);
 for(const auto & bad : invalid)
 {
  auto value = packet({addition(100), removal(source->unitId()), bad}); EXPECT_THROW(frame->applyOwnedStatePacket(value), std::invalid_argument);
  EXPECT_EQ(ids(*frame), order); EXPECT_EQ(frame->getTreeVersion(), revision); EXPECT_EQ(retained->save(), before); EXPECT_EQ(frame->getForUpdate(source->unitId()).get(), retained.get());
 }
}

TEST_F(OwnedMixedUnitPacketTest, AddedHealthInitializationPrecedesPositionAndRetainsUpdaterContext)
{
 auto * creature = LIBRARY->creh->objects.at(creatureByName("core:woodElf").getNum()).get();
 ASSERT_TRUE(creature->getAllBonuses(Selector::source(BonusSource::OTHER, ArtifactID(ArtifactID::SPELLBOOK)))->empty());
 ASSERT_TRUE(creature->getAllBonuses(Selector::source(BonusSource::OTHER, ArtifactID(ArtifactID::SPELL_SCROLL)))->empty());
 auto health = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH, BonusSource::OTHER, 1, ArtifactID(ArtifactID::SPELLBOOK));
 health->updater = std::make_shared<TimesStackSizeUpdater>();
 auto positionHealth = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH, BonusSource::OTHER, 31, ArtifactID(ArtifactID::SPELL_SCROLL));
 positionHealth->limiter = std::make_shared<UnitOnHexLimiter>(BattleHexArray{BattleHex(8, 5)});
 ASSERT_EQ(health->toJsonNode()["sourceID"].String(), "core:spellBook");
 ASSERT_EQ(positionHealth->toJsonNode()["sourceID"].String(), "core:spellScroll");
 creature->addNewBonus(health); creature->addNewBonus(positionHealth);
 struct Cleanup { CCreature * creature; std::shared_ptr<Bonus> a, b; ~Cleanup() { creature->removeBonus(a); creature->removeBonus(b); } } cleanup{creature, health, positionHealth};
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto value = packet({addition(100)}); apply(*frame, value);
 auto retained = frame->getForUpdate(100); auto * nativeAdded = battle()->getStack(100, false);
 EXPECT_EQ(retained->health.getFirstHPleft(), nativeAdded->health.getFirstHPleft()); EXPECT_LT(retained->health.getFirstHPleft(), retained->getMaxHealth());
 const auto selected = Selector::source(BonusSource::OTHER, ArtifactID(ArtifactID::SPELLBOOK));
 EXPECT_EQ(retained->getAllBonuses(selected)->totalValue(), nativeAdded->getAllBonuses(selected)->totalValue());
 auto sibling = frame->forkOwnedFixture(); const auto old = retained->save();
 int64_t loss = nativeAdded->getMaxHealth(); auto damaged = nativeAdded->acquireState(); damaged->damage(loss);
 value = packet({addition(101, 11), update(100, damaged->save())}); apply(*frame, value);
 EXPECT_EQ(frame->getForUpdate(100).get(), retained.get());
 EXPECT_EQ(retained->getAllBonuses(selected)->totalValue(), nativeAdded->getAllBonuses(selected)->totalValue()); EXPECT_EQ(sibling->getForUpdate(100)->save(), old);
}

TEST_F(OwnedMixedUnitPacketTest, EarlierAddBlocksRevivalAndEarlierRemoveFreesSameHex)
{
 auto * corpse = add(8); auto alive = corpse->save(); corpse->health.reset();
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto retained = frame->getForUpdate(corpse->unitId());
 auto value = packet({addition(100, 8), update(corpse->unitId(), alive, 1), addition(101, 12)}); apply(*frame, value);
 EXPECT_FALSE(retained->alive()); EXPECT_TRUE(frame->getForUpdate(101)->alive());
 value = packet({removal(100), update(corpse->unitId(), alive, 1)}); apply(*frame, value);
 EXPECT_TRUE(retained->alive()); EXPECT_EQ(frame->getForUpdate(corpse->unitId()).get(), retained.get());
}

TEST_F(OwnedMixedUnitPacketTest, DuplicateWithinPacketRollsBackAddedIdentityAndEarlierUpdate)
{
 auto * source = add(); auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto retained = frame->getForUpdate(source->unitId());
 const auto before = retained->save(); const auto order = ids(*frame); const auto revision = frame->getTreeVersion();
 auto moved = before; moved["state"]["position"] = JsonNode(int64_t(BattleHex(7, 5).toInt()));
 auto value = packet({update(source->unitId(), moved), addition(100), removal(100), addition(100)});
 EXPECT_THROW(frame->applyOwnedStatePacket(value), std::invalid_argument);
 EXPECT_EQ(ids(*frame), order); EXPECT_EQ(retained->save(), before); EXPECT_EQ(frame->getTreeVersion(), revision);
 value.changedStacks.pop_back(); apply(*frame, value); EXPECT_EQ(frame->getForUpdate(source->unitId()).get(), retained.get());
}

TEST_F(OwnedMixedUnitPacketTest, MissingTopLevelRemoveIsNoopAndContinuesAdd)
{
 auto * source = add(); auto frame = HypotheticBattle::captureOwnedFixture(*battle()); const auto before = source->save();
 auto value = packet({removal(999999), addition(100)}); apply(*frame, value);
 EXPECT_EQ(frame->getForUpdate(source->unitId())->save(), before); EXPECT_TRUE(frame->getForUpdate(100)->alive());
}

TEST_F(OwnedMixedUnitPacketTest, LateUnknownOperationRollsBackAddedAndRemovedUnits)
{
 auto * source = add(); auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto retained = frame->getForUpdate(source->unitId());
 const auto before = retained->save(); const auto order = ids(*frame); const auto revision = frame->getTreeVersion();
 UnitChanges unknown(source->unitId(), static_cast<BattleChanges::EOperation>(127));
 auto value = packet({addition(100), removal(source->unitId()), unknown}); EXPECT_THROW(frame->applyOwnedStatePacket(value), std::invalid_argument);
 EXPECT_EQ(retained->save(), before); EXPECT_EQ(ids(*frame), order); EXPECT_EQ(frame->getTreeVersion(), revision);
}

TEST_F(OwnedMixedUnitPacketTest, AddedDescriptorTracksLaterCertifiedSharedDefinitionEdit)
{
 auto * source = add(); auto creature = LIBRARY->creh->objects.at(source->creatureId().getNum());
 auto shared = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, 7, SpellID(SpellID::HASTE)); shared->turnsRemain = 2;
 creature->addNewBonus(shared); source->addNewBonus(shared);
 struct Cleanup { std::shared_ptr<CCreature> creature; std::shared_ptr<Bonus> bonus; ~Cleanup() { creature->removeBonus(bonus); } } cleanup{creature, shared};
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto value = packet({addition(100)}); apply(*frame, value);
 auto retained = frame->getForUpdate(100); auto sibling = frame->forkOwnedFixture(); auto detached = retained->acquireState();
 const auto selected = Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::HASTE));
 auto change = *shared; change.turnsRemain = 8;
 frame->updateUnitBonus(source->unitId(), frame->makeOwnedBonusEvent({change}));
 SetStackEffect effect; effect.battleID = battle()->getBattleID(); effect.toUpdate.emplace_back(source->unitId(), std::vector<Bonus>{change}); native(effect);
 EXPECT_EQ(retained->unitType()->getBonusBearer()->getAllBonuses(selected)->front()->turnsRemain, 8);
 EXPECT_EQ(detached->unitType()->getBonusBearer()->getAllBonuses(selected)->front()->turnsRemain, 8);
 EXPECT_EQ(sibling->getForUpdate(100)->unitType()->getBonusBearer()->getAllBonuses(selected)->front()->turnsRemain, 2);
 value = packet({addition(101, 11)}); apply(*frame, value);
 EXPECT_EQ(frame->getForUpdate(100).get(), retained.get());
 EXPECT_EQ(retained->unitType()->getBonusBearer()->getAllBonuses(selected)->front()->turnsRemain, creature->getAllBonuses(selected)->front()->turnsRemain);
}
