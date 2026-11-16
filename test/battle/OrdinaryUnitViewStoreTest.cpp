/* Explicit ordinary Unit store prototype contracts. GPL-2.0-or-later. */
#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "AI/BattleAI/OrdinaryUnitViewStore.h"
#include "AI/Integration/Ports/FHeroes2ArcherClassification.h"
#include "lib/battle/CPlayerBattleCallback.h"
#include "lib/bonuses/Propagators.h"
#include "lib/bonuses/Updaters.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "mock/mock_IBattleInfoCallback.h"
#include "mock/mock_battle_Unit.h"
#include <type_traits>

// This factory supports an explicitly protected native fixture, not callback/mock inference.
static_assert(std::is_invocable_v<decltype(&OrdinaryUnitViewStore::captureFixture), const BattleInfo &>);
static_assert(!std::is_invocable_v<decltype(&OrdinaryUnitViewStore::captureFixture), const CPlayerBattleCallback &>);
static_assert(!std::is_invocable_v<decltype(&OrdinaryUnitViewStore::captureFixture), const battle::Unit &>);

static_assert(!std::is_invocable_v<decltype(&OrdinaryUnitViewStore::captureFixture), const IBattleInfoCallbackMock &>);
static_assert(!std::is_invocable_v<decltype(&OrdinaryUnitViewStore::captureFixture), const UnitMock &>);

class OrdinaryUnitViewStoreTest : public BattleTestFixture
{
public:
 void SetUp() override { BattleTestFixture::SetUp(); startGame(); startBattle(); }
 CStack * archer(int x = 5, BattleSide side = BattleSide::ATTACKER)
 { return addStack(side, creatureByName("core:woodElf"), BattleHex(x, 5), 10); }
 CStack * cart(int x = 7, BattleSide side = BattleSide::ATTACKER)
 { return addStack(side, CreatureID::AMMO_CART, BattleHex(x, 5), 1); }
 static bool all(const battle::Unit *) { return true; }
 static Bonus effect(int value = 7, int turns = 2)
 {
  Bonus result(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, value, SpellID(SpellID::HASTE));
  result.turnsRemain = turns; return result;
 }
 static CSelector selected() { return Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::HASTE)); }
 battle::UnitInfo added(uint32_t id)
 {
  battle::UnitInfo info; info.id = id; info.count = 4; info.type = creatureByName("core:woodElf");
  info.side = BattleSide::ATTACKER; info.position = BattleHex(11, 5); info.summoned = true; return info;
 }
};

TEST_F(OrdinaryUnitViewStoreTest, ReadAndWriteLookupShareStableLocalIdentity)
{
 auto * source = archer(); auto store = OrdinaryUnitViewStore::captureFixture(*battle());
 auto read = store->unit(source->unitId()); ASSERT_TRUE(read);
 EXPECT_NE(static_cast<const battle::Unit *>(read.get()), static_cast<const battle::Unit *>(source));
 EXPECT_TRUE(store->stackStates.empty());
 auto write = store->getForUpdate(source->unitId()); EXPECT_EQ(read.get(), write.get());
 EXPECT_EQ(store->unit(source->unitId()).get(), read.get()); ASSERT_EQ(store->stackStates.size(), 1);
 EXPECT_EQ(store->stackStates.at(source->unitId()).get(), read.get());
 EXPECT_EQ(read->creatureId(), source->creatureId()); EXPECT_NE(read->unitType(), source->unitType());
 EXPECT_EQ(read->unitType(), store->unit(source->unitId())->unitType());
}

TEST_F(OrdinaryUnitViewStoreTest, EveryReentrantPredicateSeesCompleteRosterWithoutWriteRegistration)
{
 auto * source = archer(); auto * supply = cart();
 auto store = OrdinaryUnitViewStore::captureFixture(*battle()); const auto expected = battle()->stacks.size();
 const auto epoch = store->getBonusBearer()->getTreeVersion(); size_t calls = 0;
 auto units = store->units([&](const battle::Unit * current)
 {
  ++calls; EXPECT_EQ(store->units(all).size(), expected);
  auto shooter = store->unit(source->unitId()); auto cartView = store->unit(supply->unitId());
  EXPECT_TRUE(shooter); EXPECT_TRUE(cartView); EXPECT_EQ(store->unit(current->unitId()).get(), current);
  EXPECT_TRUE(store->unitHasAmmoCart(shooter.get()));
  EXPECT_EQ(store->unitEffectiveOwner(current), current->unitOwner());
  EXPECT_TRUE(store->stackStates.empty()); return true;
 });
 EXPECT_EQ(units.size(), expected); EXPECT_EQ(calls, expected); EXPECT_TRUE(store->stackStates.empty());
 EXPECT_EQ(store->getBonusBearer()->getTreeVersion(), epoch);
}

TEST_F(OrdinaryUnitViewStoreTest, AcquiredAndReacquiredStatesKeepLeaseWithoutCycle)
{
 auto * source = archer(); cart(); auto store = OrdinaryUnitViewStore::captureFixture(*battle());
 auto lease = store->environmentLease(); auto alias = store->getForUpdate(source->unitId());
 auto acquired = alias->acquireState(); auto reacquired = acquired->acquireState(); auto generic = acquired->acquire();
 store.reset(); ASSERT_FALSE(lease.expired()); EXPECT_FALSE(alias->shots.isLimited());
 alias.reset(); acquired.reset(); ASSERT_FALSE(lease.expired());
 EXPECT_FALSE(reacquired->shots.isLimited()); EXPECT_TRUE(generic->canShoot());
 reacquired.reset(); ASSERT_FALSE(lease.expired()); generic.reset(); EXPECT_TRUE(lease.expired());
}

TEST_F(OrdinaryUnitViewStoreTest, ScalarAssignmentKeepsDestinationIdentityEnvironmentAndGraph)
{
 auto * source = archer(); auto * supply = cart();
 source->addNewBonus(std::make_shared<Bonus>(effect()));
 auto store = OrdinaryUnitViewStore::captureFixture(*battle()); auto destination = store->unit(source->unitId());
 auto detached = destination->acquireState(); const auto type = destination->unitType(); const auto id = destination->unitId();
 const auto epoch = destination->getTreeVersion(); auto donor = store->unit(supply->unitId())->acquireState(); donor->health.reset();
 auto result = store->copyUnitState(id, *donor); EXPECT_EQ(result.get(), destination.get());
 EXPECT_EQ(destination->unitId(), id); EXPECT_EQ(destination->unitType(), type);
 EXPECT_EQ(destination->getTreeVersion(), epoch); EXPECT_TRUE(destination->hasBonus(selected()));
 EXPECT_TRUE(store->unit(supply->unitId())->alive()); EXPECT_FALSE(destination->shots.isLimited());
 *detached = *donor; EXPECT_EQ(detached->unitId(), id); EXPECT_EQ(detached->unitType(), type);
 EXPECT_FALSE(detached->shots.isLimited()); EXPECT_TRUE(detached->hasBonus(selected())); EXPECT_TRUE(source->alive());
}

TEST_F(OrdinaryUnitViewStoreTest, ForkCapturesCurrentAddedRemovedScalarBonusAndAllocatorState)
{
 auto * source = archer(); auto * supply = cart(); auto parent = OrdinaryUnitViewStore::captureFixture(*battle());
 auto view = parent->getForUpdate(source->unitId()); view->addUnitBonus({effect()});
 int64_t damage = view->getMaxHealth(); view->damage(damage); parent->removeUnit(supply->unitId());
 const auto info = added(parent->nextUnitId()); parent->addUnit(info); const auto next = parent->nextUnitId();
 auto child = parent->fork(); auto sibling = parent->fork(); EXPECT_TRUE(child->stackStates.empty());
 EXPECT_EQ(child->nextUnitId(), next + 1); EXPECT_EQ(child->unit(info.id)->getCount(), info.count);
 EXPECT_EQ(child->unit(source->unitId())->save(), view->save()); EXPECT_TRUE(child->unit(source->unitId())->hasBonus(selected()));
 EXPECT_TRUE(child->unit(supply->unitId())->ghost); EXPECT_TRUE(child->unit(source->unitId())->shots.isLimited());
 EXPECT_NE(child->unit(source->unitId()).get(), view.get());
 child->getForUpdate(source->unitId())->removeUnitBonus(selected()); child->getForUpdate(info.id)->health.reset();
 child->addUnit(added(child->nextUnitId())); EXPECT_GT(child->nextUnitId(), next);
 EXPECT_EQ(parent->nextUnitId(), next + 1); EXPECT_EQ(sibling->nextUnitId(), next + 1);
 EXPECT_TRUE(view->hasBonus(selected())); EXPECT_TRUE(sibling->unit(source->unitId())->hasBonus(selected()));
 EXPECT_TRUE(parent->unit(info.id)->alive()); EXPECT_TRUE(sibling->unit(info.id)->alive()); EXPECT_TRUE(supply->alive());
}

TEST_F(OrdinaryUnitViewStoreTest, ForkDoesNotBorrowLaterNativeOrParentChanges)
{
 auto * source = archer(); auto * supply = cart(); auto parent = OrdinaryUnitViewStore::captureFixture(*battle());
 auto child = parent->fork(); auto acquired = child->unit(source->unitId())->acquireState();
 source->health.reset(); supply->health.reset(); parent->getForUpdate(supply->unitId())->health.reset(); parent.reset();
 EXPECT_TRUE(child->unit(source->unitId())->alive()); EXPECT_FALSE(acquired->shots.isLimited());
 EXPECT_FALSE(child->unit(source->unitId())->shots.isLimited());
}

TEST_F(OrdinaryUnitViewStoreTest, DuplicateAppendRejectsBeforeAnyPublication)
{
 auto * source = archer(); auto store = OrdinaryUnitViewStore::captureFixture(*battle()); auto view = store->unit(source->unitId());
 const auto count = store->units(all).size(); const auto next = store->nextUnitId(); const auto state = view->save();
 const auto epoch = view->getTreeVersion(); EXPECT_THROW(store->addUnit(added(source->unitId())), std::invalid_argument);
 EXPECT_EQ(store->units(all).size(), count); EXPECT_EQ(store->nextUnitId(), next + 1); EXPECT_EQ(view->save(), state);
 EXPECT_EQ(view->getTreeVersion(), epoch); EXPECT_TRUE(store->stackStates.empty()); EXPECT_EQ(store->unit(source->unitId()).get(), view.get());
}

TEST_F(OrdinaryUnitViewStoreTest, DirectNonvirtualAddUpdateAndRemoveUseLocalGraph)
{
 auto * source = archer(); auto store = OrdinaryUnitViewStore::captureFixture(*battle()); auto sibling = store->fork();
 std::shared_ptr<StackWithBonuses> view = store->getForUpdate(source->unitId());
 view->addUnitBonus({effect(7, 2)}); battle()->addUnitBonus(source->unitId(), {effect(7, 2)});
 view->updateUnitBonus({effect(7, 6)}); battle()->updateUnitBonus(source->unitId(), {effect(7, 6)});
 auto local = view->getAllBonuses(selected()); auto native = source->getAllBonuses(selected()); ASSERT_EQ(local->size(), 1); ASSERT_EQ(native->size(), 1);
 EXPECT_EQ(local->front()->toJsonNode(), native->front()->toJsonNode()); EXPECT_EQ(local->front()->turnsRemain, 6);
 EXPECT_FALSE(sibling->unit(source->unitId())->hasBonus(selected()));
 view->removeUnitBonus(std::vector<Bonus>{effect(7, 6)}); EXPECT_FALSE(view->hasBonus(selected())); EXPECT_TRUE(source->hasBonus(selected()));
 EXPECT_TRUE(view->bonusesToAdd.empty()); EXPECT_TRUE(view->bonusesToUpdate.empty()); EXPECT_TRUE(view->bonusesToRemove.empty());
}

TEST_F(OrdinaryUnitViewStoreTest, DirectSelectorExpiryWithdrawsPropagatedControlAndSupply)
{
 auto * source = archer(); auto * grantor = archer(10, BattleSide::DEFENDER); auto * supply = cart(12, BattleSide::DEFENDER);
 auto control = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
 control->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::HERO);
 grantor->addNewBonus(control); defenderSideHero->nodeHasChanged(); ASSERT_TRUE(supply->isHypnotized());
 auto store = OrdinaryUnitViewStore::captureFixture(*battle()); auto sibling = store->fork(); auto detached = store->unit(source->unitId())->acquireState();
 ASSERT_FALSE(detached->shots.isLimited());
 store->getForUpdate(grantor->unitId())->removeUnitBonus(Selector::type()(BonusType::HYPNOTIZED));
 EXPECT_FALSE(store->unit(supply->unitId())->isHypnotized()); EXPECT_TRUE(detached->shots.isLimited());
 EXPECT_TRUE(sibling->unit(supply->unitId())->isHypnotized()); EXPECT_FALSE(sibling->unit(source->unitId())->shots.isLimited());
 EXPECT_TRUE(supply->isHypnotized()); EXPECT_FALSE(source->shots.isLimited());
}

TEST_F(OrdinaryUnitViewStoreTest, GenericAndAcquiredSpentShooterObserveCartRemoval)
{
 auto * source = archer(); source->shots.use(source->shots.available()); auto * supply = cart();
 auto store = OrdinaryUnitViewStore::captureFixture(*battle()); auto sibling = store->fork();
 auto view = store->unit(source->unitId()); const battle::Unit & generic = *view; auto acquired = generic.acquireState();
 ASSERT_TRUE(AIIntegration::fheroes2IsArcher(generic)); ASSERT_TRUE(AIIntegration::fheroes2IsArcher(*acquired));
 store->removeUnit(supply->unitId());
 EXPECT_FALSE(AIIntegration::fheroes2IsArcher(generic)); EXPECT_FALSE(AIIntegration::fheroes2IsArcher(*acquired));
 EXPECT_TRUE(AIIntegration::fheroes2IsArcher(*sibling->unit(source->unitId()))); EXPECT_TRUE(AIIntegration::fheroes2IsArcher(*source));
 EXPECT_EQ(view->shots.available(), 0); EXPECT_EQ(acquired->shots.available(), 0); EXPECT_TRUE(store->stackStates.empty());
}

TEST_F(OrdinaryUnitViewStoreTest, UntouchedSpentShooterObservesPublicCartHealthReset)
{
 auto * source = archer(); source->shots.use(source->shots.available()); auto * supply = cart();
 auto store = OrdinaryUnitViewStore::captureFixture(*battle()); auto view = store->unit(source->unitId()); auto acquired = view->acquireState();
 ASSERT_TRUE(view->canShoot()); store->getForUpdate(supply->unitId())->health.reset();
 EXPECT_FALSE(view->canShoot()); EXPECT_FALSE(acquired->canShoot()); EXPECT_TRUE(source->canShoot());
 ASSERT_EQ(store->stackStates.size(), 1); EXPECT_EQ(store->stackStates.count(source->unitId()), 0);
}

TEST_F(OrdinaryUnitViewStoreTest, NullNativeSeedRejectsWithoutPublishingOrChangingExistingStore)
{
 auto * source = archer(); auto existing = OrdinaryUnitViewStore::captureFixture(*battle());
 auto retained = existing->unit(source->unitId()); const auto before = retained->save();
 std::shared_ptr<OrdinaryUnitViewStore> published;
 battle()->stacks.push_back(nullptr);
 EXPECT_THROW(published = OrdinaryUnitViewStore::captureFixture(*battle()), std::invalid_argument);
 battle()->stacks.pop_back();
 EXPECT_FALSE(published); EXPECT_EQ(existing->unit(source->unitId()).get(), retained.get());
 EXPECT_EQ(retained->save(), before); EXPECT_TRUE(existing->stackStates.empty());
}

TEST_F(OrdinaryUnitViewStoreTest, DirectMutatorsRejectForeignComponentsBeforeChangingGraph)
{
 auto * source = archer(); auto store = OrdinaryUnitViewStore::captureFixture(*battle()); auto view = store->getForUpdate(source->unitId());
 auto foreign = effect(); foreign.propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::HERO);
 const auto epoch = view->getTreeVersion(); const auto before = view->save();
 EXPECT_THROW(view->addUnitBonus({effect(), foreign}), std::invalid_argument);
 EXPECT_THROW(view->updateUnitBonus({effect(), foreign}), std::invalid_argument);
 EXPECT_THROW(view->removeUnitBonus(std::vector<Bonus>{effect(), foreign}), std::invalid_argument);
 EXPECT_FALSE(view->hasBonus(selected())); EXPECT_EQ(view->getTreeVersion(), epoch); EXPECT_EQ(view->save(), before);
 EXPECT_FALSE(source->hasBonus(selected()));
}
