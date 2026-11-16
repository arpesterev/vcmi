/* Native packet boundary oracles for the explicit store. GPL-2.0-or-later. */
#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "AI/BattleAI/OrdinaryUnitViewStore.h"
#include "lib/serializer/JsonSerializer.h"

class OrdinaryUnitStoreLifecycleTest : public BattleTestFixture
{
public:
 void SetUp() override { BattleTestFixture::SetUp(); startGame(); startBattle(); }
 CStack * add(int x = 5)
 { return addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(x, 5), 10); }
 static JsonNode dead(CStack * unit)
 {
  auto result = unit->save(); auto health = unit->health; health.reset();
  JsonNode serialized; JsonSerializer serializer(nullptr, serialized); serializer.serializeStruct("health", health);
  result["state"]["health"] = serialized["health"]; return result;
 }
 void kill(CStack * unit) { battle()->updateUnit(unit->unitId(), dead(unit), -1); }
 static std::shared_ptr<Bonus> spell(SpellID id, int value)
 { return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, value, id); }
 static CSelector haste() { return Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::HASTE)); }
 static CSelector ray() { return Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::DISRUPTING_RAY)); }
 void same(const OrdinaryUnitViewStore & store)
 {
  for(const auto & unit : battle()->stacks)
  {
   auto local = store.unit(unit->unitId()); ASSERT_TRUE(local); EXPECT_EQ(local->save(), unit->save());
   auto a = local->getAllBonuses(Selector::all), b = unit->getAllBonuses(Selector::all); ASSERT_EQ(a->size(), b->size());
   for(size_t i = 0; i < a->size(); ++i) EXPECT_EQ((*a)[i]->toJsonNode(), (*b)[i]->toJsonNode());
  }
 }
 void apply(OrdinaryUnitViewStore & store, CStack * unit, const JsonNode & data, int64_t delta)
 { store.updateUnit(unit->unitId(), data, delta); battle()->updateUnit(unit->unitId(), data, delta); same(store); }
};

TEST_F(OrdinaryUnitStoreLifecycleTest, BlockedPositiveResurrectionPreservesStateBonusesAndEpoch)
{
 auto * corpse = add(); auto alive = corpse->save(); kill(corpse); add();
 corpse->addNewBonus(spell(SpellID(SpellID::HASTE), 7));
 auto store = OrdinaryUnitViewStore::captureFixture(*battle()); auto retained = store->unit(corpse->unitId());
 const auto before = retained->save(); const auto epoch = retained->getTreeVersion();
 apply(*store, corpse, alive, 1); EXPECT_EQ(retained->save(), before); EXPECT_EQ(retained->getTreeVersion(), epoch);
 EXPECT_TRUE(retained->hasBonus(haste())); EXPECT_FALSE(retained->alive());
}

TEST_F(OrdinaryUnitStoreLifecycleTest, LegalResurrectionExpiresNonpersistentButKeepsPersistentSpell)
{
 auto * corpse = add(); auto alive = corpse->save(); kill(corpse);
 corpse->addNewBonus(spell(SpellID(SpellID::HASTE), 7)); corpse->addNewBonus(spell(SpellID(SpellID::DISRUPTING_RAY), 3));
 auto store = OrdinaryUnitViewStore::captureFixture(*battle()); auto retained = store->unit(corpse->unitId());
 apply(*store, corpse, alive, 1); EXPECT_TRUE(retained->alive()); EXPECT_FALSE(retained->hasBonus(haste()));
 EXPECT_TRUE(retained->hasBonus(ray())); EXPECT_EQ(store->unit(corpse->unitId()).get(), retained.get());
}

TEST_F(OrdinaryUnitStoreLifecycleTest, DeathExpiresNonpersistentButKeepsPersistentSpell)
{
 auto * source = add(); source->addNewBonus(spell(SpellID(SpellID::HASTE), 7));
 source->addNewBonus(spell(SpellID(SpellID::DISRUPTING_RAY), 3));
 auto store = OrdinaryUnitViewStore::captureFixture(*battle()); apply(*store, source, dead(source), -1);
 EXPECT_FALSE(store->unit(source->unitId())->hasBonus(haste())); EXPECT_TRUE(store->unit(source->unitId())->hasBonus(ray()));
}

TEST_F(OrdinaryUnitStoreLifecycleTest, ZeroDeltaDeadCloneClearsEveryReverseLinkWithoutDeathExpiry)
{
 auto * first = add(4); auto * second = add(8); auto * clone = add(12); clone->cloned = true; kill(clone);
 first->cloneID = second->cloneID = clone->unitId(); clone->addNewBonus(spell(SpellID(SpellID::HASTE), 7));
 auto store = OrdinaryUnitViewStore::captureFixture(*battle()); apply(*store, clone, clone->save(), 0);
 EXPECT_EQ(store->unit(first->unitId())->cloneID, -1); EXPECT_EQ(store->unit(second->unitId())->cloneID, -1);
 EXPECT_TRUE(store->unit(clone->unitId())->hasBonus(haste()));
}

TEST_F(OrdinaryUnitStoreLifecycleTest, DeathWithMissingLinkedCloneClearsLinkAndCommits)
{
 auto * source = add(); source->cloneID = 999999;
 auto store = OrdinaryUnitViewStore::captureFixture(*battle()); apply(*store, source, dead(source), -1);
 EXPECT_FALSE(store->unit(source->unitId())->alive()); EXPECT_EQ(store->unit(source->unitId())->cloneID, -1);
}

TEST_F(OrdinaryUnitStoreLifecycleTest, DeathWithAlreadyDeadLinkedCloneMatchesNativeGhostPhase)
{
 auto * source = add(); auto * clone = add(9); clone->cloned = true; kill(clone); source->cloneID = clone->unitId();
 auto store = OrdinaryUnitViewStore::captureFixture(*battle()); apply(*store, source, dead(source), -1);
 EXPECT_EQ(store->unit(source->unitId())->cloneID, -1); EXPECT_FALSE(store->unit(clone->unitId())->alive());
 EXPECT_EQ(store->unit(clone->unitId())->ghostPending, clone->ghostPending);
}

TEST_F(OrdinaryUnitStoreLifecycleTest, LivingCloneDeathClearsAllReverseLinks)
{
 auto * first = add(4); auto * second = add(8); auto * clone = add(12); clone->cloned = true;
 first->cloneID = second->cloneID = clone->unitId(); auto store = OrdinaryUnitViewStore::captureFixture(*battle());
 apply(*store, clone, dead(clone), -1);
 EXPECT_EQ(store->unit(first->unitId())->cloneID, -1); EXPECT_EQ(store->unit(second->unitId())->cloneID, -1);
}

TEST_F(OrdinaryUnitStoreLifecycleTest, DirectRemovalClearsAllReverseLinksWithoutTouchingSibling)
{
 auto * first = add(4); auto * second = add(8); auto * clone = add(12); clone->cloned = true;
 first->cloneID = second->cloneID = clone->unitId(); auto store = OrdinaryUnitViewStore::captureFixture(*battle()); auto sibling = store->fork();
 store->removeUnit(clone->unitId()); battle()->removeUnit(clone->unitId()); same(*store);
 EXPECT_EQ(store->unit(first->unitId())->cloneID, -1); EXPECT_EQ(store->unit(second->unitId())->cloneID, -1);
 EXPECT_EQ(sibling->unit(first->unitId())->cloneID, clone->unitId()); EXPECT_FALSE(sibling->unit(clone->unitId())->ghost);
}

TEST_F(OrdinaryUnitStoreLifecycleTest, ScalarCopyDefersCloneAndSpellLifecycleUntilPacketBoundary)
{
 auto * source = add(); auto * clone = add(9); clone->cloned = true; source->cloneID = clone->unitId();
 source->addNewBonus(spell(SpellID(SpellID::HASTE), 7)); auto store = OrdinaryUnitViewStore::captureFixture(*battle());
 auto produced = store->unit(source->unitId())->acquireState(); produced->health.reset();
 const auto epoch = store->unit(source->unitId())->getTreeVersion(); store->copyUnitState(source->unitId(), *produced);
 EXPECT_FALSE(store->unit(source->unitId())->alive()); EXPECT_TRUE(store->unit(clone->unitId())->alive());
 EXPECT_EQ(store->unit(source->unitId())->cloneID, clone->unitId()); EXPECT_TRUE(store->unit(source->unitId())->hasBonus(haste()));
 EXPECT_EQ(store->unit(source->unitId())->getTreeVersion(), epoch); EXPECT_TRUE(source->alive());
 auto packetStore = OrdinaryUnitViewStore::captureFixture(*battle()); apply(*packetStore, source, dead(source), -1);
 EXPECT_FALSE(packetStore->unit(clone->unitId())->alive()); EXPECT_EQ(packetStore->unit(source->unitId())->cloneID, -1);
 EXPECT_FALSE(packetStore->unit(source->unitId())->hasBonus(haste()));
}
