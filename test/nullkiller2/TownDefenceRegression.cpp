/*
 * TownDefenceRegression.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "../StdInc.h"
#include "../../AI/Nullkiller2/Behaviors/DefenceBehavior.h"
#include "../../AI/Nullkiller2/Engine/Nullkiller.h"
#include "../../AI/Nullkiller2/Goals/BuyArmy.h"
#include "../../AI/Nullkiller2/Markers/DefendTown.h"
#include "../../lib/GameSettings.h"
#include "../../lib/mapObjects/CGTownInstance.h"

VCMI_LIB_USING_NAMESPACE

namespace test
{
namespace
{
class DefenceCallback final : public CCallback
{
public:
	using CCallback::getResourceAmount;
	std::vector<const CGTownInstance *> towns;
	std::vector<const CGHeroInstance *> heroes;
	const CGHeroInstance * enemy = nullptr;
	TResources resources;
	GameSettings settings;
	DefenceCallback() : CCallback(nullptr, std::nullopt, nullptr) {}
	TResources getResourceAmount() const override { return resources; }
	const IGameSettings & getSettings() const override { return settings; }
	int getDate(Date) const override { return 1; }
	int3 getMapSize() const override { return int3(2, 1, 1); }
	std::vector<const CGTownInstance *> getTownsInfo(bool = true) const override { return towns; }
	std::vector<const CGHeroInstance *> getHeroesInfo() const override { return heroes; }
	const CGObjectInstance * getObj(ObjectInstanceID id, bool = true) const override
	{
		return enemy && enemy->id == id ? enemy : nullptr;
	}
};
class DefenceTown final : public CGTownInstance
{
	int x;
public:
	DefenceTown(IGameInfoCallback * callback, int x) : CGTownInstance(callback), x(x)
	{
		ID = Obj::TOWN;
		subID = MapObjectSubID(0);
		id = ObjectInstanceID(x);
		tempOwner = PlayerColor(0);
	}
	int3 visitablePos() const override { return int3(x, 0, 0); }
};
}

class Nullkiller2TownDefenceRegression : public ::testing::Test
{
protected:
	std::shared_ptr<DefenceCallback> callback = std::make_shared<DefenceCallback>();
	NK2AI::Nullkiller ai;
	DefenceTown first{callback.get(), 0};
	DefenceTown second{callback.get(), 1};
	CGHeroInstance hero{callback.get()};
	CGHeroInstance enemy{callback.get()};
	using Defence = NK2AI::Goals::DefenceBehavior;
	void SetUp() override
	{
		ai.cc = callback;
		ai.playerID = PlayerColor(0);
		ai.armyManager = std::make_unique<NK2AI::ArmyManager>(callback.get(), &ai);
		ai.dangerHitMap = std::make_unique<NK2AI::DangerHitMapAnalyzer>(&ai);
		callback->towns = {&first, &second};
		callback->enemy = &enemy;
		enemy.id = ObjectInstanceID(3);
		enemy.tempOwner = PlayerColor(1);
		hero.setHeroType(HeroTypeID(0));
		hero.setCreature(SlotID(0), CreatureID(0), 1);
		first.setCreature(SlotID(0), CreatureID(0), 10);
		second.setCreature(SlotID(0), CreatureID(0), 10);
		first.creatures = second.creatures = {{20, {CreatureID(0)}}};
		ai.dangerHitMap->updateHitMap();
	}
	NK2AI::HitMapInfo threat(uint64_t power, uint8_t turns = 0) const
	{
		NK2AI::HitMapInfo result;
		result.danger = power;
		result.turn = turns;
		result.heroPtr = NK2AI::HeroPtr(&enemy, callback.get());
		return result;
	}
	void threaten(const CGTownInstance & town, uint64_t power, uint8_t turns = 0)
	{
		// Seed the real analyzer's already allocated cache; behavior and army
		// availability below are production implementations, not test formulas.
		auto & node = const_cast<NK2AI::HitMapNode &>(ai.dangerHitMap->getObjectThreat(&town));
		node.fastestDanger = node.maximumDanger = threat(power, turns);
	}
};

TEST_F(Nullkiller2TownDefenceRegression, WeakTavernHeroCanLeadExistingGarrison)
{
	first.addBuilding(BuildingID::TAVERN);
	callback->resources[EGameResID::GOLD] = GameConstants::HERO_GOLD_COST;
	const auto danger = threat(Defence::combinedDefenceStrength(&first, &hero));
	ASSERT_LT(hero.getTotalStrength(), danger.danger);
	EXPECT_EQ(Defence::selectDefender(&first, danger, {&hero}, callback->resources), &hero);
	NK2AI::Goals::DefendTown marker(&first, danger, &hero, Defence::combinedDefenceStrength(&first, &hero));
	EXPECT_GE(marker.getDefenceStrength(), danger.danger);
	EXPECT_FALSE(marker.isCounterAttack());
}

TEST_F(Nullkiller2TownDefenceRegression, HireRequiresFundsAndNearArrival)
{
	first.addBuilding(BuildingID::TAVERN);
	const auto power = Defence::combinedDefenceStrength(&first, &hero);
	EXPECT_EQ(Defence::selectDefender(&first, threat(power), {&hero}, callback->resources), nullptr);
	callback->resources[EGameResID::GOLD] = GameConstants::HERO_GOLD_COST;
	EXPECT_EQ(Defence::selectDefender(&first, threat(power, 1), {&hero}, callback->resources), &hero);
	EXPECT_EQ(Defence::selectDefender(&first, threat(power, 2), {&hero}, callback->resources), nullptr);
}

TEST_F(Nullkiller2TownDefenceRegression, IncompatibleEighthStackCannotCreateDefender)
{
	first.addBuilding(BuildingID::TAVERN);
	for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
		first.setCreature(SlotID(i), CreatureID(i * 2), 10);
	hero.clearSlots();
	hero.setCreature(SlotID(0), CreatureID(14), 1);
	callback->resources[EGameResID::GOLD] = GameConstants::HERO_GOLD_COST;
	ASSERT_FALSE(hero.canBeMergedWith(first));
	EXPECT_EQ(Defence::selectDefender(&first, threat(1), {&hero}, callback->resources), nullptr);
}

TEST_F(Nullkiller2TownDefenceRegression, CompetingTownsShareOneBudgetWithoutPersistentReservation)
{
	const auto * creature = CreatureID(0).toCreature();
	callback->resources = creature->getFullRecruitCost() * 5;
	threaten(first, first.getArmyStrength() + creature->getAIValue() * 5);
	threaten(second, second.getArmyStrength() + creature->getAIValue() * 5);
	const auto before = callback->resources;
	for(int pass = 0; pass < 2; ++pass)
	{
		const auto tasks = Defence::decomposeUrgentPurchases(&ai);
		ASSERT_EQ(tasks.size(), 1u);
		EXPECT_NE(dynamic_cast<NK2AI::Goals::BuyArmy *>(tasks.front().get()), nullptr);
		EXPECT_EQ(tasks.front()->town, &first);
		EXPECT_EQ(callback->resources, before);
		EXPECT_FALSE(ai.getLockedResources().nonZero());
	}
}

TEST_F(Nullkiller2TownDefenceRegression, IndefensibleTownDoesNotStarveSavableTown)
{
	const auto * creature = CreatureID(0).toCreature();
	callback->resources = creature->getFullRecruitCost() * 5;
	threaten(first, first.getArmyStrength() + creature->getAIValue() * 1000);
	threaten(second, second.getArmyStrength() + creature->getAIValue() * 5);
	const auto tasks = Defence::decomposeUrgentPurchases(&ai);
	ASSERT_EQ(tasks.size(), 1u);
	EXPECT_EQ(tasks.front()->town, &second);
}

TEST_F(Nullkiller2TownDefenceRegression, DistantThreatAndUnaffordableDefenseDoNotFreezeEconomy)
{
	const auto * creature = CreatureID(0).toCreature();
	callback->resources = creature->getFullRecruitCost() * 5;
	threaten(first, first.getArmyStrength() + creature->getAIValue() * 5, 2);
	EXPECT_TRUE(Defence::decomposeUrgentPurchases(&ai).empty());
	threaten(first, first.getArmyStrength() + creature->getAIValue() * 5, 0);
	callback->resources = TResources();
	EXPECT_TRUE(Defence::decomposeUrgentPurchases(&ai).empty());
}

TEST_F(Nullkiller2TownDefenceRegression, UnreachableRemoteArmyIsNotCountedAsLocalDefence)
{
	hero.setCreature(SlotID(0), CreatureID(0), 10000);
	callback->heroes = {&hero}; // No pathfinder or reachable path exists.
	const auto * creature = CreatureID(0).toCreature();
	callback->resources = creature->getFullRecruitCost() * 5;
	threaten(first, first.getArmyStrength() + creature->getAIValue() * 5);
	EXPECT_EQ(Defence::decomposeUrgentPurchases(&ai).size(), 1u);
}
}
