/*
 * TownRecruitmentRegression.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "../StdInc.h"

#include "../../AI/Nullkiller2/AIGateway.h"
#include "../../AI/Nullkiller2/Engine/Nullkiller.h"
#include "../../AI/Nullkiller2/Goals/BuyArmy.h"
#include "../../AI/Nullkiller2/Goals/ExchangeSwapTownHeroes.h"
#include "../../lib/GameSettings.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapping/TerrainTile.h"

VCMI_LIB_USING_NAMESPACE

namespace test
{
namespace
{

class TownRecruitmentCallback final : public CCallback
{
public:
	using CCallback::getResourceAmount;
	using CCallback::moveHero;
	TResources resources;
	GameSettings settings;
	std::vector<SlotID> dismissed;
	std::vector<CreatureID> recruited;
	const CGHeroInstance * hero = nullptr;
	TerrainTile tile;
	int moves = 0;
	int swaps = 0;
	bool applyUpgrades = false;
	bool applyRecruitment = true;
	int upgrades = 0;
	int upgradeRequestLimit = 100;

	TownRecruitmentCallback() : CCallback(nullptr, std::nullopt, nullptr)
	{
		resources += 100000;
	}

	const IGameSettings & getSettings() const override { return settings; }
	TResources getResourceAmount() const override { return resources; }
	const CGObjectInstance * getObj(ObjectInstanceID id, bool = true) const override
	{
		return hero && hero->id == id ? hero : nullptr;
	}
	const TerrainTile * getTile(int3, bool = true) const override { return &tile; }
	void moveHero(const CGHeroInstance *, const int3 &, bool) override { ++moves; }
	void swapGarrisonHero(const CGTownInstance *) override { ++swaps; }
	bool upgradeCreature(const CArmedInstance * army, SlotID slot, CreatureID creature) override
	{
		if(++upgrades > upgradeRequestLimit)
			throw std::runtime_error("Repeated upgrade without expected progress");
		if(applyUpgrades)
			const_cast<CArmedInstance *>(army)->setCreature(slot, creature, army->getStackCount(slot));
		// The real callback returns false even after sending an upgrade.
		return false;
	}
	int getDate(Date mode) const override
	{
		EXPECT_EQ(mode, Date::DAY_OF_WEEK);
		return 1;
	}
	bool dismissCreature(const CArmedInstance * army, SlotID slot) override
	{
		dismissed.push_back(slot);
		const_cast<CArmedInstance *>(army)->eraseStack(slot);
		return true;
	}
	void recruitCreatures(const CGDwelling *, const CArmedInstance * army, CreatureID creature, ui32 amount, si32) override
	{
		recruited.push_back(creature);
		if(!applyRecruitment)
			return;
		const auto slot = army->getSlotFor(creature);
		ASSERT_TRUE(slot.validSlot());
		const_cast<CArmedInstance *>(army)->addToSlot(slot, creature, amount);
		resources -= creature.toCreature()->getFullRecruitCost() * amount;
	}
};

class RecruitmentTown final : public CGTownInstance
{
public:
	explicit RecruitmentTown(IGameInfoCallback * callback) : CGTownInstance(callback) {}
	int3 visitablePos() const override { return int3(0, 0, 0); }
};

class RecruitmentHero final : public CGHeroInstance
{
public:
	explicit RecruitmentHero(IGameInfoCallback * callback) : CGHeroInstance(callback) {}
	int3 visitablePos() const override { return int3(0, 0, 0); }
	int3 getVisitableOffset() const override { return int3(0, 0, 0); }
};

}

class Nullkiller2TownRecruitmentRegression : public ::testing::Test
{
protected:
	std::shared_ptr<TownRecruitmentCallback> callback = std::make_shared<TownRecruitmentCallback>();
	NK2AI::AIGateway gateway;
	RecruitmentTown town{callback.get()};

	void SetUp() override
	{
		gateway.cc = callback;
		gateway.nullkiller->cc = callback;
		gateway.nullkiller->armyManager = std::make_unique<NK2AI::ArmyManager>(callback.get(), gateway.nullkiller.get());
		town.ID = Obj::TOWN;
		town.subID = MapObjectSubID(0);
		town.tempOwner = PlayerColor(0);
		town.setCustomName("Recruitment regression");
	}

	void buy(CreatureID creature)
	{
		// The real availability estimator and BuyArmy::accept are both called.
		// No upgraded creatures are offered, so makePossibleUpgrades is a no-op.
		town.creatures = {{10, {creature}}};
		NK2AI::Goals::BuyArmy goal(&town, 1000000);
		goal.accept(&gateway);
	}
};

TEST_F(Nullkiller2TownRecruitmentRegression, ReinforcingExistingStackDoesNotDismissForeignTroops)
{
	town.setCreature(SlotID(0), CreatureID(0), 5);
	for(int slot = 1; slot < GameConstants::ARMY_SIZE; ++slot)
		town.setCreature(SlotID(slot), CreatureID(13 + slot * 2), 1); // Six distinct Rampart upgrades.
	ASSERT_EQ(town.stacksCount(), GameConstants::ARMY_SIZE);

	EXPECT_NO_THROW(buy(CreatureID(0)));
	EXPECT_TRUE(callback->dismissed.empty());
	EXPECT_THAT(callback->recruited, ::testing::ElementsAre(CreatureID(0)));
	EXPECT_EQ(town.stacksCount(), GameConstants::ARMY_SIZE);
	EXPECT_EQ(town.getStackCount(SlotID(0)), 15);
	for(int slot = 1; slot < GameConstants::ARMY_SIZE; ++slot)
	{
		ASSERT_TRUE(town.hasStackAtSlot(SlotID(slot)));
		EXPECT_EQ(town.getStackCount(SlotID(slot)), 1);
	}
}

TEST_F(Nullkiller2TownRecruitmentRegression, NewCreatureMayStillReplaceCheaperForeignStack)
{
	for(int slot = 0; slot < GameConstants::ARMY_SIZE - 1; ++slot)
		town.setCreature(SlotID(slot), CreatureID(slot * 2), 1);
	town.setCreature(SlotID(6), CreatureID(14), 1); // The sole foreign stack is a centaur.

	EXPECT_NO_THROW(buy(CreatureID(12))); // Angels need the one foreign slot.
	EXPECT_THAT(callback->dismissed, ::testing::ElementsAre(SlotID(6)));
	EXPECT_THAT(callback->recruited, ::testing::ElementsAre(CreatureID(12)));
	EXPECT_EQ(town.stacksCount(), GameConstants::ARMY_SIZE);
	ASSERT_NE(town.getCreature(SlotID(6)), nullptr);
	EXPECT_EQ(town.getCreature(SlotID(6))->getId(), CreatureID(12));
}

TEST_F(Nullkiller2TownRecruitmentRegression, EmptyArmyRecruitmentDoesNotDismissAnyStack)
{
	EXPECT_NO_THROW(buy(CreatureID(0)));
	EXPECT_TRUE(callback->dismissed.empty());
	EXPECT_THAT(callback->recruited, ::testing::ElementsAre(CreatureID(0)));
	EXPECT_EQ(town.stacksCount(), 1);
	EXPECT_EQ(town.getStackCount(town.getSlotFor(CreatureID(0))), 10);
}

TEST_F(Nullkiller2TownRecruitmentRegression, IncompatibleGarrisonExchangePreservesTownArmy)
{
	RecruitmentHero hero{callback.get()};
	hero.id = ObjectInstanceID(2);
	hero.tempOwner = PlayerColor(0);
	hero.setHeroType(HeroTypeID(0));
	hero.setCreature(SlotID(0), CreatureID(14), 1);
	callback->hero = &hero;
	for(int slot = 0; slot < GameConstants::ARMY_SIZE; ++slot)
		town.setCreature(SlotID(slot), CreatureID(slot * 2), 20);
	ASSERT_FALSE(hero.canBeMergedWith(town));

	NK2AI::Goals::ExchangeSwapTownHeroes goal(&town, &hero);
	// The real movement wrapper temporarily releases the shared game-state
	// lock. Mirror its caller's locking contract even with a no-op move.
	std::shared_lock lock(CGameState::mutex);
	EXPECT_THROW(goal.accept(&gateway), NK2AI::cannotFulfillGoalException);
	EXPECT_EQ(callback->moves, 1);
	EXPECT_EQ(callback->swaps, 0);
	EXPECT_TRUE(callback->dismissed.empty());
	EXPECT_EQ(town.stacksCount(), GameConstants::ARMY_SIZE);
	for(int slot = 0; slot < GameConstants::ARMY_SIZE; ++slot)
		EXPECT_EQ(town.getStackCount(SlotID(slot)), 20);
	EXPECT_EQ(hero.getStackCount(SlotID(0)), 1);
}

TEST_F(Nullkiller2TownRecruitmentRegression, MergeableGarrisonExchangeStillRequestsSwap)
{
	RecruitmentHero hero{callback.get()};
	hero.id = ObjectInstanceID(2);
	hero.tempOwner = PlayerColor(0);
	hero.setHeroType(HeroTypeID(0));
	hero.setCreature(SlotID(0), CreatureID(0), 1);
	callback->hero = &hero;
	town.setCreature(SlotID(0), CreatureID(0), 20);
	ASSERT_TRUE(hero.canBeMergedWith(town));

	NK2AI::Goals::ExchangeSwapTownHeroes goal(&town, &hero);
	std::shared_lock lock(CGameState::mutex);
	EXPECT_NO_THROW(goal.accept(&gateway));
	EXPECT_EQ(callback->moves, 1);
	EXPECT_EQ(callback->swaps, 1); // Request contract; no claim that a server applied it.
	EXPECT_TRUE(callback->dismissed.empty());
}

TEST_F(Nullkiller2TownRecruitmentRegression, RejectedCreatureUpgradeDoesNotRepeatIndefinitely)
{
	town.setCreature(SlotID(0), CreatureID(0), 5);
	town.creatures = {{10, {CreatureID(0), CreatureID(1)}}};
	callback->upgradeRequestLimit = 1;
	EXPECT_FALSE(gateway.makePossibleUpgrades(&town));
	EXPECT_EQ(callback->upgrades, 1);
	EXPECT_EQ(town.getCreature(SlotID(0))->getId(), CreatureID(0));
	EXPECT_EQ(town.getStackCount(SlotID(0)), 5);
}

TEST_F(Nullkiller2TownRecruitmentRegression, AppliedCreatureUpgradeUsesStateNotCallbackReturn)
{
	town.setCreature(SlotID(0), CreatureID(0), 5);
	town.setCreature(SlotID(1), CreatureID(2), 7);
	town.creatures = {{10, {CreatureID(0), CreatureID(1)}}, {10, {CreatureID(2), CreatureID(3)}}};
	callback->applyUpgrades = true;
	EXPECT_TRUE(gateway.makePossibleUpgrades(&town));
	EXPECT_EQ(callback->upgrades, 2);
	EXPECT_EQ(town.getCreature(SlotID(0))->getId(), CreatureID(1));
	EXPECT_EQ(town.getCreature(SlotID(1))->getId(), CreatureID(3));
	EXPECT_EQ(town.getStackCount(SlotID(0)), 5);
	EXPECT_EQ(town.getStackCount(SlotID(1)), 7);
}

TEST_F(Nullkiller2TownRecruitmentRegression, UnaffordableCreatureUpgradeDoesNotSendRequest)
{
	town.setCreature(SlotID(0), CreatureID(0), 5);
	town.creatures = {{10, {CreatureID(0), CreatureID(1)}}};
	callback->resources = TResources();
	EXPECT_FALSE(gateway.makePossibleUpgrades(&town));
	EXPECT_EQ(callback->upgrades, 0);
	EXPECT_EQ(town.getCreature(SlotID(0))->getId(), CreatureID(0));
}

TEST_F(Nullkiller2TownRecruitmentRegression, DefensivePurchaseHonorsExactBudgetWithoutOptionalUpgrades)
{
	const auto creature = CreatureID(0);
	const auto cost = creature.toCreature()->getFullRecruitCost();
	town.setCreature(SlotID(0), creature, 5);
	town.creatures = {{10, {creature}}};
	const auto before = callback->resources;
	NK2AI::Goals::BuyArmy goal(&town, 1000000, cost * 2);
	EXPECT_NO_THROW(goal.accept(&gateway));
	EXPECT_EQ(town.getStackCount(SlotID(0)), 7);
	EXPECT_EQ(callback->resources, before - cost * 2);
	EXPECT_EQ(callback->upgrades, 0);
}

TEST_F(Nullkiller2TownRecruitmentRegression, DefensivePurchaseDoesNotScrapTroopsForNewStack)
{
	for(int slot = 0; slot < GameConstants::ARMY_SIZE - 1; ++slot)
		town.setCreature(SlotID(slot), CreatureID(slot * 2), 1);
	town.setCreature(SlotID(6), CreatureID(14), 1);
	town.creatures = {{10, {CreatureID(12)}}};
	NK2AI::Goals::BuyArmy goal(&town, 1000000, callback->resources);
	EXPECT_THROW(goal.accept(&gateway), NK2AI::cannotFulfillGoalException);
	EXPECT_TRUE(callback->dismissed.empty());
	EXPECT_TRUE(callback->recruited.empty());
	EXPECT_EQ(town.getCreature(SlotID(6))->getId(), CreatureID(14));
}

TEST_F(Nullkiller2TownRecruitmentRegression, DefensivePurchaseKeepsExactPlannedSpeciesAndCount)
{
	const auto creature = CreatureID(0);
	const auto cost = creature.toCreature()->getFullRecruitCost();
	town.setCreature(SlotID(0), creature, 5);
	town.creatures = {{10, {creature}}, {10, {CreatureID(2)}}};
	const auto before = callback->resources;
	const std::vector<NK2AI::creInfo> plan = {{2, creature, 0}};
	NK2AI::Goals::BuyArmy goal(&town, 1000000, cost * 2, plan);
	EXPECT_NO_THROW(goal.accept(&gateway));
	EXPECT_THAT(callback->recruited, ::testing::ElementsAre(creature));
	EXPECT_EQ(town.getStackCount(SlotID(0)), 7);
	EXPECT_EQ(callback->resources, before - cost * 2);
}

TEST_F(Nullkiller2TownRecruitmentRegression, DefensivePurchaseRechecksDepletedStock)
{
	const auto creature = CreatureID(0);
	town.creatures = {{0, {creature}}};
	const std::vector<NK2AI::creInfo> plan = {{2, creature, 0}};
	NK2AI::Goals::BuyArmy goal(&town, 1000000, callback->resources, plan);
	EXPECT_THROW(goal.accept(&gateway), NK2AI::cannotFulfillGoalException);
	EXPECT_TRUE(callback->recruited.empty());
}

TEST_F(Nullkiller2TownRecruitmentRegression, RejectedRecruitmentReportsFailureWithoutRepeatingRequest)
{
	callback->applyRecruitment = false;
	town.creatures = {{10, {CreatureID(0)}}};
	const auto before = callback->resources;
	NK2AI::Goals::BuyArmy goal(&town, 1000000, callback->resources);
	EXPECT_THROW(goal.accept(&gateway), NK2AI::cannotFulfillGoalException);
	EXPECT_EQ(callback->recruited.size(), 1u);
	EXPECT_EQ(town.stacksCount(), 0);
	EXPECT_EQ(callback->resources, before);
}

}
