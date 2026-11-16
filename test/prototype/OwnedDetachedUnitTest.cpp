#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "lib/battle/OwnedDetachedUnit.h"
#include "lib/bonuses/Updaters.h"
#include "lib/bonuses/BonusParameters.h"
#include "lib/bonuses/Limiters.h"
#include "lib/entities/faction/CFaction.h"
#include "lib/GameLibrary.h"
#include "lib/mapObjects/CGHeroInstance.h"

static_assert(!std::is_constructible_v<OwnedUnitEnvironmentHandle, std::shared_ptr<const battle::IUnitEnvironment>>);
static_assert(!std::is_constructible_v<OwnedUnitEnvironmentHandle, const battle::IUnitEnvironment *>);
static_assert(!std::is_copy_constructible_v<battle::OwnedDetachedUnit>);

class OwnedDetachedUnitTest : public BattleTestFixture
{
public:
 void SetUp() override { BattleTestFixture::SetUp(); startGame(); startBattle(); }
 CStack * add(int x, int count = 10) { return addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(x, 5), count); }
 static std::shared_ptr<Bonus> effect(BonusType type, int val)
 { return std::make_shared<Bonus>(BonusDuration::PERMANENT, type, BonusSource::OTHER, val, ArtifactID(777)); }
 static CSelector selected() { return Selector::source(BonusSource::OTHER, ArtifactID(777)); }
 static std::shared_ptr<const OwnedCreature> descriptor(const battle::CUnitState & state, const OwnedBattleBonusModel & model)
 {
  const auto & faction = dynamic_cast<const CFaction &>(*state.getFactionID().toEntity(LIBRARY));
  auto values = OwnedCreature::captureValues(*state.unitType(), {faction.nativeTerrains.begin(), faction.nativeTerrains.end()});
  return std::make_shared<const OwnedCreature>(std::move(values), model.evaluation(), model.creatureNode(state.creatureId()));
 }
 static std::shared_ptr<battle::OwnedDetachedUnit> detached(const battle::CUnitState & state, const OwnedBattleBonusModel & model)
 {
  return battle::OwnedDetachedUnit::capture(state, model, state.unitId(), descriptor(state, model), OwnedUnitEnvironmentSnapshot::capture(model));
 }
};

TEST_F(OwnedDetachedUnitTest, EvaluatedUpdaterAndLimiterRemainFrozenAfterDamageMoveAndRecursiveAcquire)
{
 auto * source = add(5);
 auto value = effect(BonusType::STACKS_SPEED, 3);
 value->updater = std::make_shared<TimesStackSizeUpdater>(std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), 1, 3);
 value->limiter = std::make_shared<UnitOnHexLimiter>(source->getHexes());
 source->addNewBonus(value);
 auto model = OwnedBattleBonusModel::capture(*battle());
 const auto revision = source->getTreeVersion();
 auto native = source->acquireState();
 auto owned = detached(*source, model);
 EXPECT_EQ(source->getTreeVersion(), revision);
 ASSERT_EQ(native->getAllBonuses(selected())->front()->val, 30);
 ASSERT_EQ(owned->getAllBonuses(selected())->front()->val, 30);
 int64_t nativeDamage = native->getMaxHealth(), ownedDamage = owned->getMaxHealth();
 native->damage(nativeDamage); owned->damage(ownedDamage);
 ASSERT_EQ(owned->getCount(), 9); ASSERT_EQ(native->getCount(), 9);
 owned->setPosition(BattleHex(10, 5)); native->setPosition(BattleHex(10, 5));
 EXPECT_EQ(owned->getAllBonuses(selected())->front()->val, 30);
 EXPECT_EQ(owned->getAllBonuses(selected())->front()->val, native->getAllBonuses(selected())->front()->val);
 auto child = owned->acquireState();
 auto grandchild = child->acquire();
 owned.reset(); child.reset();
 EXPECT_EQ(grandchild->getCount(), 9);
 EXPECT_EQ(grandchild->getAllBonuses(selected())->front()->val, 30);
 EXPECT_EQ(grandchild->getTreeVersion(), 1);
}

TEST_F(OwnedDetachedUnitTest, FrozenBonusResultsCannotMutateLaterQueriesOrNativeGraph)
{
 auto * source = add(5);
 auto value = effect(BonusType::STACKS_SPEED, 17);
 auto parameters = std::make_shared<BonusParameters>(7);
 value->parameters = parameters;
 source->addNewBonus(value);
 auto model = OwnedBattleBonusModel::capture(*battle());
 auto owned = detached(*source, model);
 value->val = 99; parameters->toCustom<int32_t>() = 888;
 auto result = owned->getAllBonuses(selected());
 ASSERT_EQ(result->size(), 1);
 EXPECT_EQ(result->front()->val, 17);
 EXPECT_EQ(result->front()->parameters->toNumber(), 7);
 result->front()->val = 555;
 result->front()->parameters = std::make_shared<const BonusParameters>(999);
 auto again = owned->getAllBonuses(selected());
 EXPECT_EQ(again->front()->val, 17); EXPECT_EQ(again->front()->parameters->toNumber(), 7);
 auto child = owned->acquireState();
 EXPECT_EQ(child->getAllBonuses(selected())->front()->val, 17);
 EXPECT_EQ(value->val, 99); EXPECT_EQ(parameters->toNumber(), 888);
}

TEST_F(OwnedDetachedUnitTest, SurvivesNativeWorldAndIntermediateStateDestruction)
{
 auto * source = add(5);
 auto model = OwnedBattleBonusModel::capture(*battle());
 auto sourceState = source->acquireState();
 auto owned = detached(*sourceState, model);
 const auto id = source->unitId(); const auto owner = source->unitOwner();
 const auto creature = source->creatureId(); const auto level = source->unitLevel();
 const auto expectedHealth = source->getAvailableHealth();
 auto recursive = owned->acquireState();
 std::weak_ptr<CGameState> lifetime = gameState();
 sourceState.reset(); owned.reset();
 server.gameState.reset(); BattleTestFixture::TearDown();
 ASSERT_TRUE(lifetime.expired());
 EXPECT_EQ(recursive->unitId(), id); EXPECT_EQ(recursive->unitOwner(), owner);
 EXPECT_EQ(recursive->unitLevel(), level); EXPECT_EQ(recursive->creatureId(), creature);
 EXPECT_EQ(recursive->getAvailableHealth(), expectedHealth);
 EXPECT_EQ(recursive->getCasterOwner(), owner);
 EXPECT_TRUE(recursive->shots.isLimited());
 EXPECT_GT(recursive->counterAttacks.total(), 0);
 EXPECT_EQ(recursive->unitType()->getId(), creature);
}

TEST_F(OwnedDetachedUnitTest, AssignmentKeepsDestinationIdentityBonusCacheAndEnvironment)
{
 auto * first = add(5, 10); auto * second = add(7, 8);
 auto before = OwnedBattleBonusModel::capture(*battle());
 auto destination = detached(*first, before);
 giveArtifact(attackerSideHero, ArtifactID::AMMO_CART, ArtifactPosition::MACH2);
 auto after = OwnedBattleBonusModel::capture(*battle());
 auto source = detached(*second, after);
 ASSERT_TRUE(destination->shots.isLimited()); ASSERT_FALSE(source->shots.isLimited());
 const auto id = destination->unitId();
 *destination = static_cast<const battle::CUnitState &>(*source);
 source.reset();
 EXPECT_EQ(destination->unitId(), id); EXPECT_EQ(destination->unitBaseAmount(), 10);
 EXPECT_EQ(destination->getCount(), 8); EXPECT_TRUE(destination->shots.isLimited());
 EXPECT_TRUE(destination->acquireState()->shots.isLimited());
}

TEST_F(OwnedDetachedUnitTest, OriginalOwnerAndSuppliedDetachedHypnosisControlRemainDistinct)
{
 auto * source = add(5);
 giveArtifact(defenderSideHero, ArtifactID::AMMO_CART, ArtifactPosition::MACH2);
 auto environmentBeforeHypnosis = OwnedBattleBonusModel::capture(*battle());
 hypnotize(source);
 auto model = OwnedBattleBonusModel::capture(*battle());
 auto owned = battle::OwnedDetachedUnit::capture(*source, model, source->unitId(), descriptor(*source, model),
  OwnedUnitEnvironmentSnapshot::capture(environmentBeforeHypnosis));
 EXPECT_TRUE(owned->isHypnotized()); EXPECT_EQ(owned->unitOwner(), source->unitOwner());
 EXPECT_EQ(owned->getCasterOwner(), source->unitEffectiveOwner(source));
 EXPECT_NE(owned->getCasterOwner(), owned->unitOwner());
 EXPECT_FALSE(owned->shots.isLimited());
 EXPECT_FALSE(environmentBeforeHypnosis.unitHasAmmoCart(source->unitId()));
}

TEST_F(OwnedDetachedUnitTest, ExplicitSnapshotEnvironmentFreezesCartCandidates)
{
 auto * source = add(5);
 auto * cart = addStack(BattleSide::ATTACKER, CreatureID::AMMO_CART, BattleHex(7, 5), 1);
 auto model = OwnedBattleBonusModel::capture(*battle());
 auto owned = detached(*source, model);
 auto native = source->acquireState();
 ASSERT_FALSE(owned->shots.isLimited()); ASSERT_FALSE(native->shots.isLimited());
 ASSERT_TRUE(model.removeUnit(cart->unitId()));
 battle()->removeUnit(cart->unitId());
 EXPECT_FALSE(owned->shots.isLimited()); EXPECT_TRUE(native->shots.isLimited());
 auto later = detached(*source, model);
 EXPECT_TRUE(later->shots.isLimited());
 EXPECT_FALSE(owned->acquireState()->shots.isLimited());
}

TEST_F(OwnedDetachedUnitTest, RejectsUnsynchronizedMutableState)
{
 auto * source = add(5);
 auto model = OwnedBattleBonusModel::capture(*battle());
 int64_t damage = source->getMaxHealth(); source->damage(damage);
 EXPECT_THROW(detached(*source, model), std::invalid_argument);
}

TEST_F(OwnedDetachedUnitTest, PreservesNativeUnitLevelInsteadOfRecomputingDefinitionLevel)
{
 auto * source = addStack(BattleSide::ATTACKER, CreatureID::AMMO_CART, BattleHex(7, 5), 1);
 auto model = OwnedBattleBonusModel::capture(*battle());
 auto owned = detached(*source, model);
 EXPECT_EQ(owned->unitLevel(), source->unitLevel());
 EXPECT_EQ(owned->acquireState()->unitLevel(), source->unitLevel());
 EXPECT_EQ(owned->unitBaseAmount(), source->unitBaseAmount());
 EXPECT_EQ(owned->unitSlot(), source->unitSlot());
}
