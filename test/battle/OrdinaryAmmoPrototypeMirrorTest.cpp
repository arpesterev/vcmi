/* Ordinary hypothetical ammunition parity diagnostics. GPL-2.0-or-later. */
#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "mock/mock_Environment.h"
#include "AI/BattleAI/StackWithBonuses.h"
#include "AI/BattleAI/OrdinaryUnitViewStore.h"
#include "AI/Integration/Ports/FHeroes2ArcherClassification.h"
#include "lib/battle/CPlayerBattleCallback.h"
#include "lib/battle/BattleLayout.h"
#include "lib/bonuses/Propagators.h"
#include "lib/json/JsonNode.h"
#include "lib/mapObjects/CGHeroInstance.h"

// Only generic units, their environments and mutations are store-owned. World
// metadata and getStacksIf remain borrowed from the live, fixture-owned battle.
// This test-only adapter is neither a production facade nor a lifetime contract.
class PrototypeAmmoFrame final : public BattleProxy, public battle::IUnitEnvironment
{
 std::shared_ptr<OrdinaryUnitViewStore> store;
 [[noreturn]] static void unsupported() { throw std::logic_error("Unsupported prototype mirror mutation"); }
public:
 PrototypeAmmoFrame(Subject native, std::shared_ptr<OrdinaryUnitViewStore> local)
  : BattleProxy(std::move(native)), store(std::move(local)) {}
 std::shared_ptr<PrototypeAmmoFrame> fork() const
 { return std::make_shared<PrototypeAmmoFrame>(subject, store->fork()); }
 std::shared_ptr<StackWithBonuses> getForUpdate(uint32_t id) { return store->getForUpdate(id); }
 battle::Units getUnitsIf(const battle::UnitFilter & predicate) const override { return store->units(predicate); }
 const IBonusBearer * getBonusBearer() const override { return store->getBonusBearer(); }
 bool unitHasAmmoCart(const battle::Unit * unit) const override { return store->unitHasAmmoCart(unit); }
 PlayerColor unitEffectiveOwner(const battle::Unit * unit) const override { return store->unitEffectiveOwner(unit); }
 void removeUnit(uint32_t id) override { store->removeUnit(id); }
 void updateUnit(uint32_t id, const JsonNode & data, int64_t delta) override { store->updateUnit(id, data, delta); }
 void addUnitBonus(uint32_t id, const std::vector<Bonus> & values) override { store->getForUpdate(id)->addUnitBonus(values); }
 void updateUnitBonus(uint32_t id, const std::vector<Bonus> & values) override { store->getForUpdate(id)->updateUnitBonus(values); }
 void removeUnitBonus(uint32_t id, const std::vector<Bonus> & values) override { store->getForUpdate(id)->removeUnitBonus(values); }
 uint32_t nextUnitId() const override { return store->nextUnitId(); }
 BattleID getBattleID() const override { return subject->getBattle()->getBattleID(); }
 const scripting::Pool & getScriptContextPool() const override { return subject->getScriptContextPool(); }
 std::vector<SpellID> getUsedSpells(BattleSide side) const override { return subject->getBattle()->getUsedSpells(side); }
 int64_t getActualDamage(const DamageRange & range, int32_t count, vstd::RNG & rng) const override
 { return subject->getBattle()->getActualDamage(range, count, rng); }
 int3 getLocation() const override { return subject->getBattle()->getLocation(); }
 BattleLayout getLayout() const override { return subject->getBattle()->getLayout(); }
 void nextRound() override { unsupported(); }
 void nextTurn(uint32_t, BattleUnitTurnReason) override { unsupported(); }
 void addUnit(uint32_t, const JsonNode &) override { unsupported(); }
 void moveUnit(uint32_t, const BattleHex &) override { unsupported(); }
 void setWallState(EWallPart, EWallState) override { unsupported(); }
 void addObstacle(const ObstacleChanges &) override { unsupported(); }
 void updateObstacle(const ObstacleChanges &) override { unsupported(); }
 void removeObstacle(uint32_t) override { unsupported(); }
};

class OrdinaryAmmoPrototypeMirrorTest : public BattleTestFixture
{
public:
	testing::StrictMock<EnvironmentMock> environment;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		startGame();
		startBattle();
	}

	std::shared_ptr<PrototypeAmmoFrame> project()
	{
		return std::make_shared<PrototypeAmmoFrame>(
			std::make_shared<CPlayerBattleCallback>(battle(), battle()->sideToPlayer(BattleSide::ATTACKER)),
			OrdinaryUnitViewStore::captureFixture(*battle()));
	}

	CStack * archer(BattleSide side = BattleSide::ATTACKER, int x = 5)
	{
		return addStack(side, creatureByName("core:woodElf"), BattleHex(x, 5), 10);
	}

	CStack * cart(BattleSide side = BattleSide::ATTACKER, int x = 7)
	{
		return addStack(side, CreatureID::AMMO_CART, BattleHex(x, 5), 1);
	}

	static std::shared_ptr<Bonus> control()
	{
		return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::HYPNOTIZED,
			BonusSource::OTHER, 1, BonusSourceID());
	}

	static JsonNode deadState(const CStack * unit)
	{
		auto state = unit->acquireState();
		state->health.reset();
		return state->save();
	}
};

TEST_F(OrdinaryAmmoPrototypeMirrorTest, NoCartSpendsFiniteAmmunition)
{
	auto * source = archer();
	ASSERT_FALSE(source->unitHasAmmoCart(source));
	ASSERT_GT(source->shots.available(), 0);
	const auto before = source->save();
	auto frame = project();
	auto unit = frame->getForUpdate(source->unitId());
	const auto available = source->shots.available();
	unit->afterAttack(true, false);
	EXPECT_TRUE(unit->shots.isLimited());
	EXPECT_EQ(unit->shots.available(), available - 1);
	unit->shots.use(unit->shots.available());
	EXPECT_FALSE(AIIntegration::fheroes2IsArcher(*unit));
	EXPECT_EQ(source->save(), before);
}

TEST_F(OrdinaryAmmoPrototypeMirrorTest, LiveCartPreservesUnlimitedAmmunitionAfterMaterialization)
{
	auto * source = archer();
	cart();
	ASSERT_FALSE(source->shots.isLimited());
	const auto before = source->save();
	auto frame = project();
	EXPECT_FALSE(frame->battleGetUnitByID(source->unitId())->acquireState()->shots.isLimited());
	auto unit = frame->getForUpdate(source->unitId());
	EXPECT_TRUE(frame->unitHasAmmoCart(unit.get()));
	EXPECT_FALSE(unit->shots.isLimited());
	EXPECT_FALSE(unit->acquireState()->shots.isLimited());
	EXPECT_EQ(source->save(), before);
}

TEST_F(OrdinaryAmmoPrototypeMirrorTest, SpentSuppliedArcherRemainsRangedInFHeroes2)
{
	auto * source = archer();
	source->shots.use(source->shots.available());
	ASSERT_EQ(source->shots.available(), 0);
	ASSERT_FALSE(AIIntegration::fheroes2IsArcher(*source));
	cart();
	ASSERT_TRUE(AIIntegration::fheroes2IsArcher(*source));
	auto frame = project();
	EXPECT_TRUE(AIIntegration::fheroes2IsArcher(*frame->battleGetUnitByID(source->unitId())));
	auto unit = frame->getForUpdate(source->unitId());
	EXPECT_EQ(unit->shots.available(), 0);
	EXPECT_TRUE(AIIntegration::fheroes2IsArcher(*unit));
	EXPECT_TRUE(AIIntegration::fheroes2IsArcher(static_cast<const battle::Unit &>(*unit)));
	EXPECT_TRUE(AIIntegration::fheroes2IsArcher(*unit->acquireState()));
	EXPECT_TRUE(AIIntegration::fheroes2IsArcher(*source));
}

TEST_F(OrdinaryAmmoPrototypeMirrorTest, SpentSupplyPreservesBattleCanShoot)
{
	auto * source = archer();
	source->shots.use(source->shots.available());
	cart();
	ASSERT_EQ(source->shots.available(), 0);
	ASSERT_TRUE(battle()->battleCanShoot(source));
	auto frame = project();
	EXPECT_TRUE(frame->battleCanShoot(frame->battleGetUnitByID(source->unitId())));
	auto unit = frame->getForUpdate(source->unitId());
	EXPECT_TRUE(frame->battleCanShoot(unit.get()));
	EXPECT_TRUE(battle()->battleCanShoot(source));
}

TEST_F(OrdinaryAmmoPrototypeMirrorTest, CartSupplyPreventsProjectedShotConsumption)
{
	auto * source = archer();
	cart();
	ASSERT_FALSE(source->shots.isLimited());
	const auto available = source->shots.available();
	const auto before = source->save();
	auto frame = project();
	auto unit = frame->getForUpdate(source->unitId());
	unit->afterAttack(true, false);
	EXPECT_EQ(unit->shots.available(), available);
	EXPECT_EQ(source->save(), before);
}

TEST_F(OrdinaryAmmoPrototypeMirrorTest, DeadFirstCartSuppressesLaterCartAndArtifact)
{
	auto * source = archer();
	auto * first = cart();
	cart(BattleSide::ATTACKER, 9);
	first->health.reset();
	giveArtifact(attackerSideHero, ArtifactID::AMMO_CART, ArtifactPosition::MACH2);
	ASSERT_FALSE(source->unitHasAmmoCart(source));
	auto frame = project();
	auto unit = frame->getForUpdate(source->unitId());
	EXPECT_FALSE(frame->unitHasAmmoCart(unit.get()));
	EXPECT_TRUE(unit->shots.isLimited());
}

TEST_F(OrdinaryAmmoPrototypeMirrorTest, RemovedFirstCartSuppressesLaterCartAndArtifact)
{
	auto * source = archer();
	auto * first = cart();
	cart(BattleSide::ATTACKER, 9);
	giveArtifact(attackerSideHero, ArtifactID::AMMO_CART, ArtifactPosition::MACH2);
	ASSERT_TRUE(source->unitHasAmmoCart(source));
	auto frame = project();
	auto unit = frame->getForUpdate(source->unitId());
	frame->removeUnit(first->unitId());
	const bool projectedSupply = frame->unitHasAmmoCart(unit.get());
	EXPECT_FALSE(first->ghost);
	EXPECT_TRUE(source->unitHasAmmoCart(source));
	battle()->removeUnit(first->unitId());
	ASSERT_FALSE(source->unitHasAmmoCart(source));
	EXPECT_EQ(projectedSupply, source->unitHasAmmoCart(source));
}

TEST_F(OrdinaryAmmoPrototypeMirrorTest, HypnotizedShooterUsesOriginalSideCart)
{
	auto * source = archer();
	hypnotize(source);
	cart();
	ASSERT_TRUE(source->unitHasAmmoCart(source));
	auto frame = project();
	auto unit = frame->getForUpdate(source->unitId());
	EXPECT_TRUE(frame->unitHasAmmoCart(unit.get()));
	EXPECT_FALSE(unit->shots.isLimited());
}

TEST_F(OrdinaryAmmoPrototypeMirrorTest, HypnotizedEnemyCartSuppliesShooter)
{
	auto * source = archer();
	auto * supply = cart(BattleSide::DEFENDER, 9);
	hypnotize(supply);
	ASSERT_TRUE(source->unitHasAmmoCart(source));
	auto frame = project();
	auto unit = frame->getForUpdate(source->unitId());
	EXPECT_TRUE(frame->unitHasAmmoCart(unit.get()));
	EXPECT_FALSE(unit->shots.isLimited());
}

TEST_F(OrdinaryAmmoPrototypeMirrorTest, ArtifactFallbackUsesEffectiveShooterOwner)
{
	auto * source = archer();
	hypnotize(source);
	giveArtifact(defenderSideHero, ArtifactID::AMMO_CART, ArtifactPosition::MACH2);
	ASSERT_TRUE(source->unitHasAmmoCart(source));
	auto frame = project();
	auto unit = frame->getForUpdate(source->unitId());
	EXPECT_TRUE(frame->unitHasAmmoCart(unit.get()));
	EXPECT_FALSE(unit->shots.isLimited());
}

TEST_F(OrdinaryAmmoPrototypeMirrorTest, SameUnitIdentityPrecedesEffectiveOwnerMismatch)
{
	auto * supply = cart();
	hypnotize(supply);
	ASSERT_TRUE(supply->unitHasAmmoCart(supply));
	auto frame = project();
	auto unit = frame->getForUpdate(supply->unitId());
	EXPECT_TRUE(frame->unitHasAmmoCart(unit.get()));
}

TEST_F(OrdinaryAmmoPrototypeMirrorTest, ProjectedRemovalDoesNotChangeLiveOrSiblingSupply)
{
	auto * source = archer();
	auto * supply = cart();
	const auto sourceBefore = source->save();
	const auto cartBefore = supply->save();
	auto frame = project();
	auto sibling = project();
	auto unit = frame->getForUpdate(source->unitId());
	auto peer = sibling->getForUpdate(source->unitId());
	EXPECT_FALSE(unit->shots.isLimited());
	EXPECT_FALSE(peer->shots.isLimited());
	frame->removeUnit(supply->unitId());
	EXPECT_TRUE(unit->shots.isLimited());
	EXPECT_FALSE(peer->shots.isLimited());
	EXPECT_FALSE(source->shots.isLimited());
	EXPECT_EQ(source->save(), sourceBefore);
	EXPECT_EQ(supply->save(), cartBefore);
}

TEST_F(OrdinaryAmmoPrototypeMirrorTest, PublicCartHealthMutationUpdatesSupply)
{
	auto * source = archer();
	auto * supply = cart();
	auto frame = project();
	auto unit = frame->getForUpdate(source->unitId());
	auto projectedCart = frame->getForUpdate(supply->unitId());
	EXPECT_FALSE(unit->shots.isLimited());
	projectedCart->health.reset();
	EXPECT_TRUE(unit->shots.isLimited());
	EXPECT_FALSE(source->shots.isLimited());
	EXPECT_TRUE(supply->alive());
}

TEST_F(OrdinaryAmmoPrototypeMirrorTest, RemovedHypnotizedCartNoLongerMasksLaterFriendlyCart)
{
	auto * source = archer();
	auto * first = cart(BattleSide::DEFENDER, 9);
	cart(BattleSide::ATTACKER, 7);
	defenderSideHero->addNewBonus(control());
	ASSERT_TRUE(first->isHypnotized());
	ASSERT_TRUE(source->unitHasAmmoCart(source));
	auto frame = project();
	auto unit = frame->getForUpdate(source->unitId());
	frame->removeUnit(first->unitId());
	const bool projectedControl = frame->getForUpdate(first->unitId())->isHypnotized();
	const bool projectedSupply = frame->unitHasAmmoCart(unit.get());
	EXPECT_TRUE(first->isHypnotized());
	EXPECT_FALSE(first->ghost);
	battle()->removeUnit(first->unitId());
	ASSERT_FALSE(first->isHypnotized());
	ASSERT_TRUE(source->unitHasAmmoCart(source));
	EXPECT_EQ(projectedControl, first->isHypnotized());
	EXPECT_EQ(projectedSupply, source->unitHasAmmoCart(source));
}

TEST_F(OrdinaryAmmoPrototypeMirrorTest, RemovingPropagatingSourceWithdrawsCartControl)
{
	auto * source = archer();
	auto * grantor = archer(BattleSide::DEFENDER, 10);
	auto * supply = cart(BattleSide::DEFENDER, 12);
	auto effect = control();
	effect->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::HERO);
	grantor->addNewBonus(effect);
	defenderSideHero->nodeHasChanged();
	ASSERT_TRUE(supply->isHypnotized());
	ASSERT_TRUE(source->unitHasAmmoCart(source));
	auto frame = project();
	auto unit = frame->getForUpdate(source->unitId());
	frame->removeUnit(grantor->unitId());
	const bool projectedControl = frame->getForUpdate(supply->unitId())->isHypnotized();
	const bool projectedSupply = frame->unitHasAmmoCart(unit.get());
	EXPECT_FALSE(grantor->ghost);
	EXPECT_TRUE(supply->isHypnotized());
	battle()->removeUnit(grantor->unitId());
	ASSERT_FALSE(supply->isHypnotized());
	ASSERT_FALSE(source->unitHasAmmoCart(source));
	EXPECT_EQ(projectedControl, supply->isHypnotized());
	EXPECT_EQ(projectedSupply, source->unitHasAmmoCart(source));
}

TEST_F(OrdinaryAmmoPrototypeMirrorTest, CloneCartDeathClearsAllReverseLinks)
{
	auto * source = archer();
	auto * second = archer(BattleSide::ATTACKER, 9);
	auto * supply = cart();
	source->cloneID = supply->unitId();
	second->cloneID = supply->unitId();
	supply->cloned = true;
	const auto data = deadState(supply);
	const auto delta = -supply->getAvailableHealth();
	auto frame = project();
	frame->updateUnit(supply->unitId(), data, delta);
	const auto sourceLink = frame->getForUpdate(source->unitId())->cloneID;
	const auto secondLink = frame->getForUpdate(second->unitId())->cloneID;
	EXPECT_TRUE(supply->alive());
	EXPECT_EQ(source->cloneID, supply->unitId());
	battle()->updateUnit(supply->unitId(), data, delta);
	ASSERT_EQ(source->cloneID, -1);
	ASSERT_EQ(second->cloneID, -1);
	EXPECT_EQ(sourceLink, source->cloneID);
	EXPECT_EQ(secondLink, second->cloneID);
}

TEST_F(OrdinaryAmmoPrototypeMirrorTest, OriginalDeathKillsLinkedCartClone)
{
	auto * source = archer();
	auto * supply = cart();
	source->cloneID = supply->unitId();
	supply->cloned = true;
	const auto data = deadState(source);
	const auto delta = -source->getAvailableHealth();
	auto frame = project();
	frame->updateUnit(source->unitId(), data, delta);
	auto projectedCart = frame->getForUpdate(supply->unitId());
	const bool projectedAlive = projectedCart->alive();
	const bool projectedPending = projectedCart->ghostPending;
	const auto projectedLink = frame->getForUpdate(source->unitId())->cloneID;
	EXPECT_TRUE(source->alive());
	EXPECT_TRUE(supply->alive());
	battle()->updateUnit(source->unitId(), data, delta);
	ASSERT_FALSE(supply->alive());
	ASSERT_TRUE(supply->ghostPending);
	ASSERT_EQ(source->cloneID, -1);
	EXPECT_EQ(projectedAlive, supply->alive());
	EXPECT_EQ(projectedPending, supply->ghostPending);
	EXPECT_EQ(projectedLink, source->cloneID);
}

TEST_F(OrdinaryAmmoPrototypeMirrorTest, DirectCloneRemovalClearsAllReverseLinks)
{
	auto * source = archer();
	auto * second = archer(BattleSide::ATTACKER, 9);
	auto * supply = cart();
	source->cloneID = supply->unitId();
	second->cloneID = supply->unitId();
	supply->cloned = true;
	auto frame = project();
	frame->removeUnit(supply->unitId());
	const auto sourceLink = frame->getForUpdate(source->unitId())->cloneID;
	const auto secondLink = frame->getForUpdate(second->unitId())->cloneID;
	const bool projectedGhost = frame->getForUpdate(supply->unitId())->ghost;
	EXPECT_FALSE(supply->ghost);
	EXPECT_EQ(source->cloneID, supply->unitId());
	battle()->removeUnit(supply->unitId());
	ASSERT_EQ(source->cloneID, -1);
	ASSERT_EQ(second->cloneID, -1);
	EXPECT_EQ(sourceLink, source->cloneID);
	EXPECT_EQ(secondLink, second->cloneID);
	EXPECT_EQ(projectedGhost, supply->ghost);
}

TEST_F(OrdinaryAmmoPrototypeMirrorTest, UntouchedSpentArcherObservesProjectedCartRemoval)
{
	auto * source = archer();
	source->shots.use(source->shots.available());
	auto * supply = cart();
	ASSERT_EQ(source->shots.available(), 0);
	ASSERT_TRUE(AIIntegration::fheroes2IsArcher(*source));
	ASSERT_TRUE(battle()->battleCanShoot(source));
	const auto sourceBefore = source->save();
	const auto cartBefore = supply->save();
	auto frame = project();
	auto sibling = project();
	frame->removeUnit(supply->unitId());
	const auto * untouched = frame->battleGetUnitByID(source->unitId());
	ASSERT_NE(untouched, nullptr);
	const bool projectedRanged = AIIntegration::fheroes2IsArcher(*untouched);
	const bool projectedDetachedRanged = AIIntegration::fheroes2IsArcher(*untouched->acquireState());
	const bool projectedCanShoot = frame->battleCanShoot(untouched);
	EXPECT_EQ(source->save(), sourceBefore);
	EXPECT_EQ(supply->save(), cartBefore);
	EXPECT_TRUE(AIIntegration::fheroes2IsArcher(*sibling->battleGetUnitByID(source->unitId())));
	battle()->removeUnit(supply->unitId());
	ASSERT_FALSE(AIIntegration::fheroes2IsArcher(*source));
	ASSERT_FALSE(battle()->battleCanShoot(source));
	EXPECT_EQ(projectedRanged, AIIntegration::fheroes2IsArcher(*source));
	EXPECT_EQ(projectedDetachedRanged, AIIntegration::fheroes2IsArcher(*source));
	EXPECT_EQ(projectedCanShoot, battle()->battleCanShoot(source));
}

TEST_F(OrdinaryAmmoPrototypeMirrorTest, UntouchedSpentArcherObservesMutableCartHealth)
{
	auto * source = archer();
	source->shots.use(source->shots.available());
	auto * supply = cart();
	ASSERT_EQ(source->shots.available(), 0);
	ASSERT_TRUE(AIIntegration::fheroes2IsArcher(*source));
	ASSERT_TRUE(battle()->battleCanShoot(source));
	auto frame = project();
	auto sibling = project();
	frame->getForUpdate(supply->unitId())->health.reset();
	const auto * untouched = frame->battleGetUnitByID(source->unitId());
	ASSERT_NE(untouched, nullptr);
	const bool projectedRanged = AIIntegration::fheroes2IsArcher(*untouched);
	const bool projectedDetachedRanged = AIIntegration::fheroes2IsArcher(*untouched->acquireState());
	const bool projectedCanShoot = frame->battleCanShoot(untouched);
	EXPECT_TRUE(supply->alive());
	EXPECT_TRUE(AIIntegration::fheroes2IsArcher(*source));
	EXPECT_TRUE(AIIntegration::fheroes2IsArcher(*sibling->battleGetUnitByID(source->unitId())));
	supply->health.reset();
	ASSERT_FALSE(AIIntegration::fheroes2IsArcher(*source));
	ASSERT_FALSE(battle()->battleCanShoot(source));
	EXPECT_EQ(projectedRanged, AIIntegration::fheroes2IsArcher(*source));
	EXPECT_EQ(projectedDetachedRanged, AIIntegration::fheroes2IsArcher(*source));
	EXPECT_EQ(projectedCanShoot, battle()->battleCanShoot(source));
}

TEST_F(OrdinaryAmmoPrototypeMirrorTest, ChildSupplyIsIndependentOfUntouchedParentShooter)
{
	auto * source = archer();
	source->shots.use(source->shots.available());
	auto * supply = cart();
	ASSERT_TRUE(AIIntegration::fheroes2IsArcher(*source));
	auto parent = project();
	auto parentUnit = parent->getForUpdate(source->unitId());
	auto child = parent->fork();
	auto sibling = parent->fork();
	child->removeUnit(supply->unitId());
	const auto * untouched = child->battleGetUnitByID(source->unitId());
	ASSERT_NE(untouched, nullptr);
	const bool projectedRanged = AIIntegration::fheroes2IsArcher(*untouched);
	const bool projectedDetachedRanged = AIIntegration::fheroes2IsArcher(*untouched->acquireState());
	EXPECT_TRUE(AIIntegration::fheroes2IsArcher(*parentUnit));
	EXPECT_TRUE(AIIntegration::fheroes2IsArcher(*sibling->battleGetUnitByID(source->unitId())));
	EXPECT_TRUE(AIIntegration::fheroes2IsArcher(*source));
	EXPECT_TRUE(supply->alive());
	battle()->removeUnit(supply->unitId());
	ASSERT_FALSE(AIIntegration::fheroes2IsArcher(*source));
	EXPECT_EQ(projectedRanged, AIIntegration::fheroes2IsArcher(*source));
	EXPECT_EQ(projectedDetachedRanged, AIIntegration::fheroes2IsArcher(*source));
}
