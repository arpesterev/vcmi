#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "AI/BattleAI/StackWithBonuses.h"
#include "lib/gameState/GameStatePackVisitor.h"
#include "lib/networkPacks/SetStackEffect.h"
#include "lib/bonuses/BonusGraphSnapshot.h"
#include "lib/bonuses/Limiters.h"
#include "lib/bonuses/Updaters.h"
#include "lib/bonuses/Propagators.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/battle/CObstacleInstance.h"
#include "lib/serializer/JsonSerializer.h"
#include "lib/GameLibrary.h"
#include <initializer_list>

class OwnedRoundLifecycleTest : public BattleTestFixture
{
public:
 using BattleTestFixture::apply;
 void SetUp() override
 {
  BattleTestFixture::SetUp(); startGame(); startBattle();
  while(battle()->getRound() < 0) { auto value = next(); native(value); }
 }
 CStack * add(int x = 5, CreatureID type = CreatureID::NONE, BattleSide side = BattleSide::ATTACKER)
 { return addStack(side, type == CreatureID::NONE ? creatureByName("core:woodElf") : type, BattleHex(x, 5), 10); }
 void native(CPackForClient & value) { GameStatePackVisitor visitor(*server.gameState); value.visit(visitor); }
 BattleNextRound next() { BattleNextRound p; p.battleID = battle()->getBattleID(); return p; }
 void nonopening() { auto p = next(); native(p); }
 void removeNativeEffect(uint32_t id, const Bonus & bonus)
 { SetStackEffect p; p.battleID = battle()->getBattleID(); p.toRemove.emplace_back(id, std::vector<Bonus>{bonus}); native(p); }
 static std::shared_ptr<Bonus> effect(BonusType type = BonusType::STACKS_SPEED, int turns = 3, int value = 7)
 {
  auto b = std::make_shared<Bonus>(BonusDuration::N_TURNS, type, BonusSource::SPELL_EFFECT, value, SpellID(SpellID::HASTE));
  b->turnsRemain = turns; return b;
 }
 static std::shared_ptr<Bonus> marker(int turns = 1)
 { auto b = effect(BonusType::NONE, turns, 0); b->sid = SpellID(SpellID::CLONE); return b; }
 static UnitChanges remove(uint32_t id) { return UnitChanges(id, BattleChanges::EOperation::REMOVE); }
 UnitChanges addition(uint32_t id, int x, int count = 10)
 {
  battle::UnitInfo info; info.id = id; info.count = count; info.type = creatureByName("core:woodElf");
  info.side = BattleSide::ATTACKER; info.position = BattleHex(x, 5); info.summoned = true;
  UnitChanges result(id, BattleChanges::EOperation::ADD); info.save(result.data); return result;
 }
 static UnitChanges death(CStack * unit)
 {
  UnitChanges result(unit->unitId(), BattleChanges::EOperation::UPDATE); result.data = unit->save(); result.healthDelta = -1;
  auto health = unit->health; health.reset(); JsonNode data; JsonSerializer serializer(nullptr, data);
  serializer.serializeStruct("health", health); result.data["state"]["health"] = data["health"]; return result;
 }
 BattleUnitsChanged changes(std::vector<UnitChanges> entries)
 { BattleUnitsChanged p; p.battleID = battle()->getBattleID(); p.changedStacks = std::move(entries); return p; }
 static std::vector<uint32_t> ids(HypotheticBattle & frame)
 {
  std::vector<uint32_t> result;
  for(auto * u : frame.getUnitsIf([](const battle::Unit *) { return true; })) result.push_back(u->unitId());
  return result;
 }
 static void cacheScalar(const BonusCacheScalarState & a, const BonusCacheScalarState & b)
 { EXPECT_EQ(a.version, b.version); EXPECT_EQ(a.value, b.value); }
 static void cacheTurn(const BonusCachePerTurn::State & a, const BonusCachePerTurn::State & b)
 {
  EXPECT_EQ(a.listVersion, b.listVersion); EXPECT_EQ(a.list, b.list); EXPECT_EQ(a.membership, b.membership);
  for(size_t i = 0; i < a.entries.size(); ++i) cacheScalar(a.entries[i], b.entries[i]);
 }
 static void caches(const battle::CUnitState::BonusCaches & a, const battle::CUnitState::BonusCaches & b)
 {
  for(size_t i = 0; i < a.unit.size(); ++i) cacheScalar(a.unit[i], b.unit[i]);
  cacheScalar(a.combat.entry, b.combat.entry); EXPECT_EQ(a.combat.variant, b.combat.variant);
  cacheTurn(a.speed, b.speed); cacheTurn(a.immobilized, b.immobilized);
  cacheScalar(a.shots, b.shots); cacheScalar(a.casts, b.casts); cacheScalar(a.retaliation, b.retaliation); cacheScalar(a.shooter, b.shooter);
  cacheScalar(a.retaliationFlags.noRetaliation, b.retaliationFlags.noRetaliation);
  cacheScalar(a.retaliationFlags.unlimited, b.retaliationFlags.unlimited);
 }
 static void markerCache(const battle::CUnitState & owned, const CStack & source)
 {
  const auto a = owned.captureBonusCaches().unit.at(UnitBonusValuesProxy::CLONE_MARKER);
  const auto b = source.captureBonusCaches().unit.at(UnitBonusValuesProxy::CLONE_MARKER);
  EXPECT_EQ(a.version == owned.getTreeVersion(), b.version == source.getTreeVersion()); EXPECT_EQ(a.value, b.value);
 }
 void same(std::initializer_list<HypotheticBattle *> frames)
 {
  // Retaliation queries change serialized state; compare every cold branch first.
  for(auto * branch : frames)
  {
   SCOPED_TRACE("round raw before observer queries");
   auto & frame = *branch;
   EXPECT_EQ(frame.getRound(), battle()->getRound()); EXPECT_EQ(frame.getActiveStackID(), battle()->getActiveStackID());
   EXPECT_EQ(frame.getTacticDist(), battle()->getTacticDist()); EXPECT_EQ(frame.getTacticsSide(), battle()->getTacticsSide());
   for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
   {
    EXPECT_EQ(frame.getCastSpells(side), battle()->getCastSpells(side));
    EXPECT_EQ(frame.getEnchanterCounter(side), battle()->getEnchanterCounter(side)); EXPECT_EQ(frame.getUsedSpells(side), battle()->getUsedSpells(side));
   }
   auto order = ids(frame); ASSERT_EQ(order.size(), battle()->stacks.size());
   for(size_t i = 0; i < order.size(); ++i)
   {
    const auto & source = battle()->stacks[i]; ASSERT_EQ(order[i], source->unitId()); auto owned = frame.getForUpdate(order[i]);
    EXPECT_EQ(owned->save(), source->save());
   }
  }
  for(auto * branch : frames)
  {
   auto & frame = *branch;
   auto order = ids(frame);
   for(size_t i = 0; i < order.size(); ++i)
   {
    const auto & source = battle()->stacks[i]; auto owned = frame.getForUpdate(order[i]);
    EXPECT_EQ(owned->getCasterOwner(), source->getCasterOwner());
    EXPECT_EQ(frame.unitEffectiveOwner(owned.get()), source->unitEffectiveOwner(source.get()));
    EXPECT_EQ(frame.unitHasAmmoCart(owned.get()), source->unitHasAmmoCart(source.get()));
    EXPECT_EQ(owned->shots.isLimited(), source->shots.isLimited()); EXPECT_EQ(owned->shots.available(), source->shots.available());
    EXPECT_EQ(owned->casts.available(), source->casts.available()); EXPECT_EQ(owned->counterAttacks.available(), source->counterAttacks.available());
    const auto a = owned->getAllBonuses(Selector::all), b = source->getAllBonuses(Selector::all); ASSERT_EQ(a->size(), b->size());
    std::multiset<int> av, bv;
    for(size_t n = 0; n < a->size(); ++n) { EXPECT_EQ((*a)[n]->toJsonNode(), (*b)[n]->toJsonNode()); av.insert((*a)[n]->val); bv.insert((*b)[n]->val); }
    EXPECT_EQ(av, bv);
   }
  }
  for(auto * branch : frames)
  {
   SCOPED_TRACE("round raw after observer queries");
   auto & frame = *branch;
   auto order = ids(frame); ASSERT_EQ(order.size(), battle()->stacks.size());
   for(size_t i = 0; i < order.size(); ++i)
   {
    const auto & source = battle()->stacks[i]; ASSERT_EQ(order[i], source->unitId()); auto owned = frame.getForUpdate(order[i]);
    EXPECT_EQ(owned->save(), source->save());
   }
  }
 }
 void same(HypotheticBattle & frame) { same({&frame}); }
 void advance(HypotheticBattle & frame) { auto p = next(); frame.applyOwnedStatePacket(p); native(p); }
 void apply(HypotheticBattle & frame, BattleUnitsChanged & p) { frame.applyOwnedStatePacket(p); native(p); same(frame); }
 static void dirty(CStack * unit)
 {
  unit->setTurnStatus(battle::UnitTurnStatus::WAITING); unit->setTurnStatus(battle::UnitTurnStatus::DEFENDED);
  unit->hadMorale = unit->fear = unit->drainedMana = true; unit->setCastSpellThisTurn(true); unit->counterAttacks.use(1);
 }
 static std::shared_ptr<Bonus> adjacency(CStack * source, BonusType type = BonusType::STACKS_SPEED)
 {
  auto b = effect(type, 9, 101); b->duration = BonusDuration::PERMANENT;
  auto limiter = std::make_shared<UnitAdjacentLimiter>(); limiter->targetUnit = source->creatureId(); b->limiter = limiter; return b;
 }
 static void cold(CStack * unit)
 {
  unit->nodeHasChanged(); auto graph = BonusGraphSnapshot::capture({unit}); EXPECT_FALSE(graph.nodeCaches().at(graph.roots().front()).valid);
 }
};

TEST_F(OwnedRoundLifecycleTest, DeadPendingAndRemovedUnitsResetWithoutDisappearing)
{
 auto * living = add(3); auto * corpse = add(5); auto * pending = add(7); auto * tombstone = add(9);
 corpse->health.reset(); corpse->cloneID = 999999; pending->makeGhost();
 auto removal = changes({remove(tombstone->unitId())}); native(removal);
 for(auto * u : {living, corpse, pending, tombstone})
 { u->addNewBonus(effect()); u->addNewBonus(effect(BonusType::CASTS, 8, 4)); u->addNewBonus(effect(BonusType::SHOTS, 8, 4)); dirty(u); u->shots.use(1); u->casts.use(1); }
 auto parent = HypotheticBattle::captureOwnedFixture(*battle()); auto frame = parent->forkOwnedFixture(); auto sibling = parent->forkOwnedFixture();
 auto retained = frame->getForUpdate(corpse->unitId()); const auto before = retained->save();
 const auto originalIDs = ids(*frame);
 for(int i = 0; i < 3; ++i)
 {
  advance(*frame); same(*frame); EXPECT_EQ(ids(*frame), originalIDs); EXPECT_EQ(frame->getForUpdate(corpse->unitId()).get(), retained.get());
  EXPECT_EQ(retained->cloneID, 999999); EXPECT_TRUE(frame->getForUpdate(pending->unitId())->ghostPending);
  EXPECT_TRUE(frame->getForUpdate(tombstone->unitId())->ghost);
 }
 EXPECT_EQ(parent->getForUpdate(corpse->unitId())->save(), before); EXPECT_EQ(sibling->getForUpdate(corpse->unitId())->save(), before);
}

TEST_F(OwnedRoundLifecycleTest, OpeningRoundSkipsDurationsButStillChecksCloneMarker)
{
 auto * original = add(3); auto * marked = add(5); auto * other = add(7); auto * unmarked = add(9);
 marked->cloned = unmarked->cloned = true; original->cloneID = marked->unitId(); other->cloneID = unmarked->unitId();
 auto mark = marker(); marked->addNewBonus(mark); auto unrelated = effect(); unmarked->addNewBonus(unrelated);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle());
 advance(*frame); markerCache(*frame->getForUpdate(unmarked->unitId()), *unmarked);
 EXPECT_TRUE(marked->alive()); EXPECT_EQ(mark->turnsRemain, 1); EXPECT_TRUE(frame->getForUpdate(marked->unitId())->alive());
 EXPECT_TRUE(frame->getForUpdate(unmarked->unitId())->ghostPending); EXPECT_FALSE(frame->getForUpdate(unmarked->unitId())->ghost); same(*frame);
 advance(*frame); EXPECT_TRUE(frame->getForUpdate(marked->unitId())->ghostPending); EXPECT_FALSE(frame->getForUpdate(marked->unitId())->ghost);
 EXPECT_EQ(frame->getForUpdate(original->unitId())->cloneID, marked->unitId()); EXPECT_EQ(frame->getForUpdate(other->unitId())->cloneID, unmarked->unitId()); same(*frame);
 advance(*frame); same(*frame);
}

TEST_F(OwnedRoundLifecycleTest, MarkerSelectorAndInheritedMarkerUseNativePresence)
{
 auto * valid = add(3); auto * wrongType = add(5); auto * wrongSource = add(7); auto * wrongSpell = add(9);
 auto * inherited = add(11, CreatureID::NONE, BattleSide::DEFENDER);
 valid->addNewBonus(marker()); auto type = marker(); type->type = BonusType::STACKS_SPEED; wrongType->addNewBonus(type);
 auto source = marker(); source->source = BonusSource::OTHER; source->sid = ArtifactID(ArtifactID::SPELLBOOK); wrongSource->addNewBonus(source);
 auto spell = marker(); spell->sid = SpellID(SpellID::HASTE); wrongSpell->addNewBonus(spell);
 defenderSideHero->addNewBonus(marker());
 for(auto * u : {valid, wrongType, wrongSource, wrongSpell, inherited})
 { u->cloned = true; u->addNewBonus(effect()); auto persistent = effect(); persistent->sid = SpellID(SpellID::DISRUPTING_RAY); persistent->duration = BonusDuration::PERMANENT; u->addNewBonus(persistent); }
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); advance(*frame);
 for(auto * u : {valid, inherited}) EXPECT_TRUE(frame->getForUpdate(u->unitId())->alive());
 for(auto * u : {wrongType, wrongSource, wrongSpell}) EXPECT_TRUE(frame->getForUpdate(u->unitId())->ghostPending);
 same(*frame); advance(*frame); EXPECT_TRUE(frame->getForUpdate(inherited->unitId())->alive()); EXPECT_TRUE(frame->getForUpdate(valid->unitId())->ghostPending); same(*frame);
}

TEST_F(OwnedRoundLifecycleTest, InterleavedStorageOrderDefersEarlierCloneExpiry)
{
 nonopening(); auto p = changes({addition(900, 5), addition(100, 9)}); native(p);
 auto * clone = battle()->getStack(900, false); auto * later = battle()->getStack(100, false); clone->cloned = true;
 auto mark = marker(); mark->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::HERO); later->addNewBonus(mark); attackerSideHero->nodeHasChanged();
 ASSERT_GT(clone->unitId(), later->unitId()); ASSERT_TRUE(clone->hasBonus(Selector::type()(BonusType::NONE).And(Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::CLONE)))));
 cold(clone); auto frame = HypotheticBattle::captureOwnedFixture(*battle()); advance(*frame);
 EXPECT_TRUE(clone->alive()); EXPECT_TRUE(frame->getForUpdate(900)->alive());
 const auto select = Selector::type()(BonusType::NONE).And(Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::CLONE)));
 EXPECT_FALSE(clone->hasBonus(select)); EXPECT_FALSE(frame->getForUpdate(900)->hasBonus(select)); same(*frame);
 advance(*frame); EXPECT_TRUE(frame->getForUpdate(900)->ghostPending); same(*frame);
}

TEST_F(OwnedRoundLifecycleTest, SharedMarkerAliasesAndRecursiveDeadRootsAgeNatively)
{
 nonopening(); auto * parent = add(3); auto * child = add(5); auto * clone = add(7); auto * removed = add(9);
 child->health.reset(); clone->cloned = true; auto p = changes({remove(removed->unitId())}); native(p);
 child->attachTo(*parent); struct Detach { CStack * child; CStack * parent; ~Detach() { child->detachFrom(*parent); } } detach{child, parent};
 auto shared = marker(4); parent->addNewBonus(shared); child->addNewBonus(shared); clone->addNewBonus(shared);
 auto local = effect(BonusType::STACKS_SPEED, 2); removed->addNewBonus(local);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto sibling = frame->forkOwnedFixture();
 for(int i = 0; i < 3; ++i) { advance(*frame); same(*frame); }
 EXPECT_EQ(sibling->getRound(), 1); EXPECT_TRUE(sibling->getForUpdate(clone->unitId())->alive());
}

TEST_F(OwnedRoundLifecycleTest, CloneGhostingRetainsLinksAndEffectsUntilExplicitRemove)
{
 auto * original = add(3); auto * clone = add(5); auto * reverse = add(7);
 clone->cloned = true; original->cloneID = reverse->cloneID = clone->unitId(); clone->addNewBonus(effect());
 auto inherited = effect(); inherited->duration = BonusDuration::PERMANENT; inherited->sid = SpellID(SpellID::SLOW); attackerSideHero->addNewBonus(inherited);
 const auto select = Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::SLOW));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto retained = frame->getForUpdate(clone->unitId());
 advance(*frame); EXPECT_TRUE(retained->ghostPending); EXPECT_FALSE(retained->ghost); EXPECT_TRUE(retained->hasBonus(select));
 EXPECT_EQ(frame->getForUpdate(original->unitId())->cloneID, clone->unitId()); EXPECT_EQ(frame->getForUpdate(reverse->unitId())->cloneID, clone->unitId()); same(*frame);
 auto p = changes({remove(clone->unitId())}); apply(*frame, p);
 EXPECT_TRUE(retained->ghost); EXPECT_FALSE(retained->ghostPending); EXPECT_FALSE(retained->hasBonus(select)); EXPECT_TRUE(retained->hasBonus(Bonus::NTurns));
 EXPECT_EQ(frame->getForUpdate(original->unitId())->cloneID, -1); EXPECT_EQ(frame->getForUpdate(reverse->unitId())->cloneID, -1);
 advance(*frame); same(*frame); EXPECT_EQ(frame->getForUpdate(clone->unitId()).get(), retained.get());
}

TEST_F(OwnedRoundLifecycleTest, RemovingOriginalAfterRoundCascadesPendingClone)
{
 auto * original = add(3); auto * clone = add(5); original->cloneID = clone->unitId(); clone->cloned = true;
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); advance(*frame); same(*frame);
 auto p = changes({remove(original->unitId())}); apply(*frame, p);
 EXPECT_TRUE(frame->getForUpdate(clone->unitId())->ghost); EXPECT_FALSE(frame->getForUpdate(clone->unitId())->ghostPending);
 advance(*frame); same(*frame);
}

TEST_F(OwnedRoundLifecycleTest, CloneMarkerQueryPreservesNativeWarmCountValues)
{
 auto * clone = add(); clone->cloned = true;
 auto value = effect(); value->duration = BonusDuration::PERMANENT; value->updater = std::make_shared<TimesStackSizeUpdater>(0, 10, 1, 7); clone->addNewBonus(value); cold(clone);
 const auto selected = Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::HASTE));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto owned = frame->getForUpdate(clone->unitId());
 const auto ne = clone->getTreeVersion(), oe = owned->getTreeVersion(); advance(*frame);
 markerCache(*owned, *clone); EXPECT_EQ(clone->getTreeVersion(), ne); EXPECT_EQ(owned->getTreeVersion(), oe);
 EXPECT_EQ(owned->getCount(), 0); EXPECT_TRUE(owned->ghostPending);
 ASSERT_EQ(clone->getAllBonuses(selected)->size(), 1); EXPECT_EQ(clone->getAllBonuses(selected)->front()->val, 70);
 EXPECT_EQ(owned->getAllBonuses(selected)->front()->val, clone->getAllBonuses(selected)->front()->val); same(*frame);
 auto event = frame->ownedEventFromUnitExports(clone->unitId(), selected); frame->removeUnitBonus(clone->unitId(), event); removeNativeEffect(clone->unitId(), *value);
 EXPECT_NE(owned->getTreeVersion(), oe); EXPECT_NE(clone->getTreeVersion(), ne); same(*frame);
}

TEST_F(OwnedRoundLifecycleTest, NeighborCloneDeathKeepsWarmAndColdAdjacencyDistinct)
{
 auto * early = add(5, creatureByName("core:pikeman")); auto * warm = add(6); auto * coldUnit = add(4); auto * later = add(5);
 later->position = BattleHex(5, 4); early->cloned = later->cloned = true;
 warm->addNewBonus(adjacency(early)); coldUnit->addNewBonus(adjacency(early));
 auto mark = marker(); mark->duration = BonusDuration::PERMANENT; mark->limiter = adjacency(early)->limiter; later->addNewBonus(mark);
 const CSelector selected = [](const Bonus * b) { return b->val == 101; };
 ASSERT_TRUE(warm->hasBonus(selected)); cold(coldUnit); cold(later);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto ow = frame->getForUpdate(warm->unitId()), oc = frame->getForUpdate(coldUnit->unitId());
 const auto nw = warm->getTreeVersion(), nc = coldUnit->getTreeVersion(), ew = ow->getTreeVersion(), ec = oc->getTreeVersion();
 advance(*frame);
 EXPECT_EQ(warm->getTreeVersion(), nw); EXPECT_EQ(coldUnit->getTreeVersion(), nc); EXPECT_EQ(ow->getTreeVersion(), ew); EXPECT_EQ(oc->getTreeVersion(), ec);
 EXPECT_TRUE(warm->hasBonus(selected)); EXPECT_TRUE(ow->hasBonus(selected)); EXPECT_FALSE(coldUnit->hasBonus(selected)); EXPECT_FALSE(oc->hasBonus(selected));
 EXPECT_TRUE(later->ghostPending); EXPECT_TRUE(frame->getForUpdate(later->unitId())->ghostPending); markerCache(*frame->getForUpdate(later->unitId()), *later); same(*frame);
}

TEST_F(OwnedRoundLifecycleTest, HypnosisExpiryChangesEffectiveOwnerAndFirstCartOutcome)
{
 nonopening(); auto * shooter = add(3); auto * first = add(5, CreatureID::AMMO_CART, BattleSide::DEFENDER);
 auto * dead = add(7, CreatureID::AMMO_CART); add(9, CreatureID::AMMO_CART); dead->health.reset();
 giveArtifact(attackerSideHero, ArtifactID::AMMO_CART, ArtifactPosition::MACH2);
 first->addNewBonus(effect(BonusType::HYPNOTIZED, 1, 1)); const auto owner = first->unitOwner(); const auto side = first->unitSide();
 ASSERT_TRUE(shooter->unitHasAmmoCart(shooter)); first->getCasterOwner(); shooter->shots.available();
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); advance(*frame);
 EXPECT_EQ(first->unitOwner(), owner); EXPECT_EQ(first->unitSide(), side); EXPECT_EQ(frame->getForUpdate(first->unitId())->unitOwner(), owner);
 EXPECT_FALSE(shooter->unitHasAmmoCart(shooter)); EXPECT_FALSE(frame->unitHasAmmoCart(frame->getForUpdate(shooter->unitId()).get())); same(*frame);
}

TEST_F(OwnedRoundLifecycleTest, MixedAddKillRemoveRoundSequenceMatchesNative)
{
 auto * killed = add(3); auto * removed = add(5); auto frame = HypotheticBattle::captureOwnedFixture(*battle());
 auto retained = frame->getForUpdate(killed->unitId()); auto sibling = frame->forkOwnedFixture(); const auto before = retained->save();
 auto p = changes({addition(900, 7), death(killed), remove(removed->unitId()), addition(100, 9, 0), addition(500, 11), remove(500)}); apply(*frame, p);
 auto order = ids(*frame); EXPECT_EQ(order.at(order.size()-3), 900); EXPECT_EQ(order.at(order.size()-2), 100); EXPECT_EQ(order.back(), 500);
 advance(*frame); same(*frame); advance(*frame); same(*frame);
 auto added = frame->getForUpdate(900); p = changes({addition(200, 12)}); apply(*frame, p);
 EXPECT_EQ(frame->getForUpdate(killed->unitId()).get(), retained.get()); EXPECT_EQ(frame->getForUpdate(900).get(), added.get());
 EXPECT_EQ(sibling->getForUpdate(killed->unitId())->save(), before);
}

namespace
{
struct RoundHeaderWriter
{
 bool saving = true;
 bool written = false;
 int32_t value;
 template<typename T> RoundHeaderWriter & operator&(T &) { return *this; }
 RoundHeaderWriter & operator&(int32_t & scalar)
 { if(!written) { scalar = value; written = true; } return *this; }
};
void setRoundHeader(BattleInfo & battle, int32_t value)
{ RoundHeaderWriter writer; writer.value = value; battle.serialize(writer); EXPECT_TRUE(writer.written); EXPECT_EQ(battle.getRound(), value); }
struct RoundSnapshot
{
 std::vector<uint32_t> order;
 std::vector<JsonNode> states;
 std::vector<battle::CUnitState::BonusCaches> caches;
 std::vector<int64_t> epochs;
 std::vector<const void *> addresses;
 std::array<std::vector<SpellID>, 2> used;
 BattleSide tactics;
 int distance;
 int64_t revision;
 int round;
 int active;
 std::array<int, 2> casts, enchanters;
 explicit RoundSnapshot(HypotheticBattle & frame)
  : order(OwnedRoundLifecycleTest::ids(frame)), revision(frame.getTreeVersion()), round(frame.getRound()), active(frame.getActiveStackID())
 {
  tactics = frame.getTacticsSide(); distance = frame.getTacticDist();
  for(auto id : order) { auto u = frame.getForUpdate(id); addresses.push_back(u.get()); states.push_back(u->save()); caches.push_back(u->captureBonusCaches()); epochs.push_back(u->getTreeVersion()); }
  int i = 0; for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER}) { used[i] = frame.getUsedSpells(side); casts[i] = frame.getCastSpells(side); enchanters[i++] = frame.getEnchanterCounter(side); }
 }
 void check(HypotheticBattle & frame) const
 {
  EXPECT_EQ(frame.getTacticsSide(), tactics); EXPECT_EQ(frame.getTacticDist(), distance);
  EXPECT_EQ(order, OwnedRoundLifecycleTest::ids(frame)); EXPECT_EQ(frame.getTreeVersion(), revision); EXPECT_EQ(frame.getRound(), round); EXPECT_EQ(frame.getActiveStackID(), active);
  for(size_t i = 0; i < order.size(); ++i)
  { auto u = frame.getForUpdate(order[i]); EXPECT_EQ(u.get(), addresses[i]); EXPECT_EQ(u->save(), states[i]); EXPECT_EQ(u->getTreeVersion(), epochs[i]); OwnedRoundLifecycleTest::caches(u->captureBonusCaches(), caches[i]); }
  int i = 0; for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER}) { EXPECT_EQ(frame.getUsedSpells(side), used[i]); EXPECT_EQ(frame.getCastSpells(side), casts[i]); EXPECT_EQ(frame.getEnchanterCounter(side), enchanters[i++]); }
 }
};
}

TEST_F(OwnedRoundLifecycleTest, LateHealthExpiryRollsBackEarlierCloneGhosting)
{
 nonopening(); auto * original = add(3); auto * early = add(5); auto * late = add(9);
 original->cloneID = early->unitId(); early->cloned = true; early->addNewBonus(marker()); auto health = effect(BonusType::STACK_HEALTH, 1, 2); late->addNewBonus(health);
 dirty(early); battle()->getSide(BattleSide::ATTACKER).castSpellsCount = 3; battle()->getSide(BattleSide::ATTACKER).enchanterCounter = 2;
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto sibling = frame->forkOwnedFixture(); auto retained = frame->getForUpdate(early->unitId());
 const auto selected = Selector::type()(BonusType::STACK_HEALTH).And(Bonus::NTurns);
 auto correction = frame->ownedEventFromUnitExports(late->unitId(), selected); auto siblingCorrection = sibling->ownedEventFromUnitExports(late->unitId(), selected);
 const auto definitions = retained->unitType()->getBonusBearer()->getAllBonuses(Selector::all)->totalValue();
 RoundSnapshot before(*frame), untouched(*sibling); auto p = next();
 EXPECT_THROW(frame->applyOwnedStatePacket(p), std::invalid_argument);
 EXPECT_THROW(sibling->nextRound(), std::invalid_argument);
 before.check(*frame); untouched.check(*sibling); EXPECT_EQ(frame->getForUpdate(early->unitId()).get(), retained.get()); EXPECT_TRUE(retained->alive());
 EXPECT_EQ(retained->unitType()->getBonusBearer()->getAllBonuses(Selector::all)->totalValue(), definitions);
 frame->removeUnitBonus(late->unitId(), correction); sibling->removeUnitBonus(late->unitId(), siblingCorrection); removeNativeEffect(late->unitId(), *health);
 frame->applyOwnedStatePacket(p); sibling->applyOwnedStatePacket(p); native(p); same({frame.get(), sibling.get()}); EXPECT_TRUE(retained->ghostPending);
}

TEST_F(OwnedRoundLifecycleTest, LateDurationUnderflowRollsBackCompletedLifecyclePrefix)
{
 nonopening(); auto * early = add(5); auto * late = add(9); early->cloned = true; early->addNewBonus(marker()); late->health.reset();
 auto invalid = effect(BonusType::STACKS_SPEED, std::numeric_limits<si16>::min()); late->addNewBonus(invalid); dirty(late);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto sibling = frame->forkOwnedFixture(); auto retained = frame->getForUpdate(early->unitId());
 const auto selected = Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::HASTE));
 auto correction = frame->ownedEventFromUnitExports(late->unitId(), selected); auto siblingCorrection = sibling->ownedEventFromUnitExports(late->unitId(), selected);
 RoundSnapshot before(*frame), untouched(*sibling); auto p = next();
 EXPECT_THROW(frame->applyOwnedStatePacket(p), std::invalid_argument);
 EXPECT_THROW(sibling->nextRound(), std::invalid_argument);
 before.check(*frame); untouched.check(*sibling); EXPECT_EQ(frame->getForUpdate(early->unitId()).get(), retained.get()); EXPECT_TRUE(retained->alive());
 frame->removeUnitBonus(late->unitId(), correction); sibling->removeUnitBonus(late->unitId(), siblingCorrection); removeNativeEffect(late->unitId(), *invalid);
 frame->applyOwnedStatePacket(p); sibling->applyOwnedStatePacket(p); native(p); same({frame.get(), sibling.get()});
}

TEST_F(OwnedRoundLifecycleTest, FailedRoundDiagnosticsDoNotWarmObservers)
{
 nonopening(); auto * early = add(5, creatureByName("core:pikeman")); auto * recipient = add(6); auto * late = add(10);
 early->cloned = true; early->addNewBonus(marker()); recipient->addNewBonus(adjacency(early)); late->addNewBonus(effect(BonusType::STACK_HEALTH, 1, 2)); cold(recipient);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto sibling = frame->forkOwnedFixture(); RoundSnapshot before(*frame);
 auto p = next(); EXPECT_THROW(frame->applyOwnedStatePacket(p), std::invalid_argument); before.check(*frame);
 auto dead = changes({death(early)}); frame->applyOwnedStatePacket(dead); sibling->applyOwnedStatePacket(dead); native(dead);
 const CSelector selected = [](const Bonus * b) { return b->val == 101; };
 EXPECT_FALSE(recipient->hasBonus(selected)); EXPECT_FALSE(frame->getForUpdate(recipient->unitId())->hasBonus(selected)); EXPECT_FALSE(sibling->getForUpdate(recipient->unitId())->hasBonus(selected)); same(*frame);
}

TEST_F(OwnedRoundLifecycleTest, SuccessfulRoundDiagnosticsOnlyWarmNativeCloneQueries)
{
 auto * clone = add(5, creatureByName("core:pikeman")); auto * recipient = add(6); clone->cloned = true; recipient->addNewBonus(adjacency(clone)); cold(recipient); cold(clone);
 auto * dead = add(10); dead->health.reset(); auto * living = add(12); cold(dead); cold(living);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto owned = frame->getForUpdate(recipient->unitId());
 const auto od = frame->getForUpdate(dead->unitId())->captureBonusCaches(), nd = dead->captureBonusCaches();
 const auto ol = frame->getForUpdate(living->unitId())->captureBonusCaches(), nl = living->captureBonusCaches();
 auto cloneBefore = frame->getForUpdate(clone->unitId())->captureBonusCaches(), nativeCloneBefore = clone->captureBonusCaches();
 auto oc = owned->captureBonusCaches(), nc = recipient->captureBonusCaches(); const auto epoch = owned->getTreeVersion();
 advance(*frame); caches(owned->captureBonusCaches(), oc); caches(recipient->captureBonusCaches(), nc); EXPECT_EQ(owned->getTreeVersion(), epoch);
 caches(frame->getForUpdate(dead->unitId())->captureBonusCaches(), od); caches(dead->captureBonusCaches(), nd);
 caches(frame->getForUpdate(living->unitId())->captureBonusCaches(), ol); caches(living->captureBonusCaches(), nl);
 auto cloneAfter = frame->getForUpdate(clone->unitId())->captureBonusCaches(), nativeCloneAfter = clone->captureBonusCaches();
 cloneBefore.unit.at(UnitBonusValuesProxy::CLONE_MARKER) = cloneAfter.unit.at(UnitBonusValuesProxy::CLONE_MARKER);
 nativeCloneBefore.unit.at(UnitBonusValuesProxy::CLONE_MARKER) = nativeCloneAfter.unit.at(UnitBonusValuesProxy::CLONE_MARKER);
 caches(cloneAfter, cloneBefore); caches(nativeCloneAfter, nativeCloneBefore);
 markerCache(*frame->getForUpdate(clone->unitId()), *clone);
 const auto cache = frame->getForUpdate(clone->unitId())->captureBonusCaches().unit.at(UnitBonusValuesProxy::CLONE_MARKER);
 EXPECT_EQ(cache.version, frame->getForUpdate(clone->unitId())->getTreeVersion());
 const CSelector selected = [](const Bonus * b) { return b->val == 101; };
 EXPECT_FALSE(recipient->hasBonus(selected)); EXPECT_FALSE(owned->hasBonus(selected)); same(*frame);
}

TEST_F(OwnedRoundLifecycleTest, RoundRemoveAndForkSurviveNativeAndLibraryDestruction)
{
 auto * original = add(3); auto * clone = add(5); clone->cloned = true; original->cloneID = clone->unitId(); clone->addNewBonus(effect(BonusType::STACKS_SPEED, 2));
 const auto id = clone->unitId(); auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto retained = frame->getForUpdate(id); auto detached = retained->acquireState();
 auto roundPacket = next(); auto removal = changes({remove(id)});
 struct Checkpoint { std::vector<JsonNode> state; std::vector<int> speed; int round; };
 auto checkpoint = [&]() { Checkpoint c; c.round = battle()->getRound(); for(const auto & u : battle()->stacks) { c.state.push_back(u->save()); c.speed.push_back(u->getMovementRange()); } return c; };
 native(roundPacket); const auto afterRound = checkpoint(); native(removal); const auto afterRemove = checkpoint(); native(roundPacket); const auto afterNext = checkpoint(); native(roundPacket); const auto branchExpected = checkpoint();
 std::weak_ptr<CGameState> lifetime = gameState(); server.gameState.reset(); BattleTestFixture::TearDown(); ASSERT_TRUE(lifetime.expired());
 struct NoLibrary { GameLibrary * saved = LIBRARY; NoLibrary() { LIBRARY = nullptr; } ~NoLibrary() { LIBRARY = saved; } } unavailable;
 auto sibling = frame->forkOwnedFixture();
 auto check = [&](HypotheticBattle & target, const Checkpoint & c)
 {
  EXPECT_EQ(target.getRound(), c.round); auto order = ids(target); ASSERT_EQ(order.size(), c.state.size());
  for(size_t i = 0; i < order.size(); ++i) { auto u = target.getForUpdate(order[i]); EXPECT_EQ(u->save(), c.state[i]); EXPECT_EQ(u->getMovementRange(), c.speed[i]); }
 };
 frame->applyOwnedStatePacket(roundPacket); check(*frame, afterRound); frame->applyOwnedStatePacket(removal); check(*frame, afterRemove);
 frame->applyOwnedStatePacket(roundPacket); check(*frame, afterNext); auto branch = frame->forkOwnedFixture(); branch->applyOwnedStatePacket(roundPacket); check(*branch, branchExpected); check(*frame, afterNext);
 EXPECT_TRUE(sibling->getForUpdate(id)->alive()); EXPECT_EQ(frame->getForUpdate(id).get(), retained.get()); frame.reset();
 EXPECT_TRUE(retained->ghost); EXPECT_FALSE(retained->ghostPending); EXPECT_EQ(retained->unitType()->getId(), detached->unitType()->getId()); EXPECT_TRUE(retained->hasBonus(Bonus::NTurns));
}

TEST_F(OwnedRoundLifecycleTest, UnsupportedRoundDomainsRemainAtomic)
{
 auto * unit = add(); auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto retained = frame->getForUpdate(unit->unitId());
 auto reject = [&](HypotheticBattle & target, CPackForClient & packet) { RoundSnapshot before(target); EXPECT_THROW(target.applyOwnedStatePacket(packet), std::invalid_argument); before.check(target); };
 auto foreign = next(); foreign.battleID = BattleID::NONE; reject(*frame, foreign);
 struct Derived : BattleNextRound {}; Derived derived; derived.battleID = battle()->getBattleID(); reject(*frame, derived);
 auto obstacle = std::make_shared<SpellCreatedObstacle>(); obstacle->turnsRemaining = 2; battle()->obstacles.push_back(obstacle);
 auto obstructed = HypotheticBattle::captureOwnedFixture(*battle()); auto p = next(); reject(*obstructed, p); RoundSnapshot blocked(*obstructed); EXPECT_THROW(obstructed->nextRound(), std::invalid_argument); blocked.check(*obstructed); EXPECT_EQ(obstacle->turnsRemaining, 2); battle()->obstacles.clear();
 for(int value : {-1, std::numeric_limits<int32_t>::max()})
 { setRoundHeader(*battle(), value); auto bounded = HypotheticBattle::captureOwnedFixture(*battle()); reject(*bounded, p); RoundSnapshot before(*bounded); EXPECT_THROW(bounded->nextRound(), std::invalid_argument); before.check(*bounded); }
 setRoundHeader(*battle(), 0); unit->cloneID = -2; auto malformed = HypotheticBattle::captureOwnedFixture(*battle()); reject(*malformed, p); unit->cloneID = -1;
 battle()->getSide(BattleSide::ATTACKER).enchanterCounter = -1; auto badCounter = HypotheticBattle::captureOwnedFixture(*battle()); reject(*badCounter, p); RoundSnapshot counterBefore(*badCounter); EXPECT_THROW(badCounter->nextRound(), std::invalid_argument); counterBefore.check(*badCounter); battle()->getSide(BattleSide::ATTACKER).enchanterCounter = 0;
 auto direct = frame->forkOwnedFixture(); direct->nextRound(); frame->applyOwnedStatePacket(p); native(p); same({frame.get(), direct.get()}); EXPECT_EQ(frame->getForUpdate(unit->unitId()).get(), retained.get());
}

TEST_F(OwnedRoundLifecycleTest, IndependentGhostAndCloneFlagsRetainNativeCountPredicate)
{
 auto * marked = add(3); auto * unmarked = add(5); auto * ordinary = add(7); auto * dead = add(9);
 marked->ghost = unmarked->ghost = ordinary->ghost = true; marked->cloned = unmarked->cloned = dead->cloned = true;
 marked->addNewBonus(marker()); dead->health.reset(); dead->cloneID = 999999;
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto direct = frame->forkOwnedFixture(); auto p = next();
 frame->applyOwnedStatePacket(p); direct->nextRound(); native(p);
 EXPECT_TRUE(frame->getForUpdate(marked->unitId())->alive()); EXPECT_TRUE(frame->getForUpdate(ordinary->unitId())->alive());
 EXPECT_TRUE(frame->getForUpdate(unmarked->unitId())->ghost); EXPECT_TRUE(frame->getForUpdate(unmarked->unitId())->ghostPending);
 EXPECT_EQ(frame->getForUpdate(dead->unitId())->cloneID, 999999); same({frame.get(), direct.get()});
}

TEST_F(OwnedRoundLifecycleTest, MalformedHealthScalarsRejectBothEntryRoutesWithoutPublication)
{
 auto * source = add(); auto baseline = HypotheticBattle::captureOwnedFixture(*battle()); const auto valid = source->save();
 std::vector<JsonNode> invalid;
 for(const auto & entry : std::vector<std::pair<std::string, int64_t>>{{"fullUnits", -2}, {"firstHPleft", -1}, {"resurrected", -1}, {"resurrected", 11}})
 { auto state = valid; state["state"]["health"][entry.first] = JsonNode(entry.second); invalid.push_back(state); }
 auto missingHP = valid; missingHP["state"]["health"]["firstHPleft"] = JsonNode(int64_t(0)); invalid.push_back(missingHP);
 for(const auto & state : invalid)
 {
  auto packetFrame = baseline->forkOwnedFixture(), directFrame = baseline->forkOwnedFixture();
  for(auto frame : {packetFrame, directFrame}) frame->getForUpdate(source->unitId())->load(state);
  auto packetUnit = packetFrame->getForUpdate(source->unitId()), directUnit = directFrame->getForUpdate(source->unitId());
  const auto packetState = packetUnit->save(), directState = directUnit->save();
  const auto packetCaches = packetUnit->captureBonusCaches(), directCaches = directUnit->captureBonusCaches();
  const auto packetRound = packetFrame->getRound(), directRound = directFrame->getRound();
  auto packet = next();
  EXPECT_THROW(packetFrame->applyOwnedStatePacket(packet), std::invalid_argument);
  EXPECT_THROW(directFrame->nextRound(), std::invalid_argument);
  EXPECT_EQ(packetUnit->save(), packetState); EXPECT_EQ(directUnit->save(), directState);
  caches(packetUnit->captureBonusCaches(), packetCaches); caches(directUnit->captureBonusCaches(), directCaches);
  EXPECT_EQ(packetFrame->getRound(), packetRound); EXPECT_EQ(directFrame->getRound(), directRound);
  EXPECT_EQ(packetFrame->getForUpdate(source->unitId()).get(), packetUnit.get()); EXPECT_EQ(directFrame->getForUpdate(source->unitId()).get(), directUnit.get());
  EXPECT_EQ(baseline->getForUpdate(source->unitId())->save(), valid);
 }
}

TEST_F(OwnedRoundLifecycleTest, CurrentHealthAboveMaximumIsNotAnExtraRoundRejection)
{
 auto * unit = add(); auto state = unit->save();
 state["state"]["health"]["firstHPleft"] = JsonNode(int64_t(unit->getMaxHealth()) + 7); unit->load(state);
 ASSERT_GT(unit->health.getFirstHPleft(), unit->getMaxHealth());
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto direct = frame->forkOwnedFixture(); auto p = next();
 frame->applyOwnedStatePacket(p); direct->nextRound(); native(p); same({frame.get(), direct.get()});
 EXPECT_GT(frame->getForUpdate(unit->unitId())->health.getFirstHPleft(), frame->getForUpdate(unit->unitId())->getMaxHealth());
}
