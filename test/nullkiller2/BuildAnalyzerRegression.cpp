/*
 * BuildAnalyzerRegression.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "../StdInc.h"

#include "../../AI/Nullkiller2/Engine/Nullkiller.h"
#include "../../AI/Nullkiller2/Engine/ResourceTrader.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/GameSettings.h"
#include "../../lib/StartInfo.h"
#include "../../lib/entities/building/CBuilding.h"
#include "../../lib/entities/faction/CTown.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/mapObjects/CGTownInstance.h"

VCMI_LIB_USING_NAMESPACE

namespace test
{
namespace
{

// Legacy game data does not guarantee a rare-resource price for archangels.
// Change only the gem price through the supported configuration API, and restore
// the loaded value even when a fatal assertion exits a test early.
class ScopedGemRecruitCost final : private boost::noncopyable
{
	CCreature & creature;
	const TResource originalGemCost;

	void setGemCost(TResource gems)
	{
		JsonNode patch;
		patch["config"]["cost"]["gems"].Integer() = gems;
		creature.updateFrom(patch);
	}

public:
	ScopedGemRecruitCost(CreatureID id, TResource gems)
		: creature(*LIBRARY->creh->objects.at(id.getNum()))
		, originalGemCost(creature.getFullRecruitCost()[EGameResID::GEMS])
	{
		setGemCost(gems);
	}

	~ScopedGemRecruitCost()
	{
		setGemCost(originalGemCost);
	}
};

// Keep the real canBuildStructure, creatureGrowth, dailyIncome and ArmyManager.
// Only the state boundary is substituted; there is no copied AI algorithm.
class BuildAnalysisCallback final : public CCallback
{
public:
	using CCallback::getResourceAmount;
	using CCallback::trade;
	std::vector<const CGTownInstance *> towns;
	GameSettings settings;
	PlayerState player{this};
	PlayerSettings playerSettings;
	bool applyTrades = false;
	int tradeRequests = 0;
	int tradeRequestLimit = 100;

	BuildAnalysisCallback() : CCallback(nullptr, std::nullopt, nullptr)
	{
		player.color = PlayerColor(0);
		setDailyBuildCap(1);
	}

	void setDailyBuildCap(int cap)
	{
		JsonNode value;
		value.Integer() = cap;
		settings.addOverride(EGameSettings::TOWNS_BUILDINGS_PER_TURN_CAP, value);
	}

	const IGameSettings & getSettings() const override { return settings; }
	std::vector<const CGTownInstance *> getTownsInfo(bool = true) const override { return towns; }
	std::vector<const CGObjectInstance *> getMyObjects() const override { return {}; }
	TResources getResourceAmount() const override { return player.resources; }
	const PlayerState * getPlayerState(PlayerColor, bool = true) const override { return &player; }
	const PlayerSettings * getPlayerSettings(PlayerColor) const override { return &playerSettings; }
	const CGObjectInstance * getObj(ObjectInstanceID id, bool = true) const override
	{
		for(const auto * town : towns)
			if(town->id == id)
				return town;
		return nullptr;
	}
	void trade(ObjectInstanceID, EMarketMode mode, TradeItemSell sold, TradeItemBuy bought, ui32 amount, const CGHeroInstance *) override
	{
		EXPECT_EQ(mode, EMarketMode::RESOURCE_RESOURCE);
		if(++tradeRequests > tradeRequestLimit)
			throw std::runtime_error("Repeated trade without expected progress");
		if(applyTrades)
		{
			player.resources[sold.as<GameResID>()] -= amount;
			player.resources[bought.as<GameResID>()] += amount;
		}
	}
	int getDate(Date mode) const override
	{
		EXPECT_EQ(mode, Date::DAY_OF_WEEK);
		return 1;
	}
};

class FixedRateMarketTown final : public CGTownInstance
{
public:
	explicit FixedRateMarketTown(IGameInfoCallback * callback) : CGTownInstance(callback) {}
	bool getOffer(int, int, int & given, int & received, EMarketMode) const override
	{
		given = received = 1;
		return true;
	}
};

}

class Nullkiller2BuildAnalyzerRegression : public ::testing::Test
{
protected:
	std::shared_ptr<BuildAnalysisCallback> callback = std::make_shared<BuildAnalysisCallback>();
	NK2AI::Nullkiller ai;
	FixedRateMarketTown first{callback.get()};
	CGTownInstance second{callback.get()};

	void initializeTown(CGTownInstance & town, int id)
	{
		town.ID = Obj::TOWN;
		town.subID = MapObjectSubID(0); // Base-game Castle.
		town.id = ObjectInstanceID(id);
		town.tempOwner = PlayerColor(0);
		town.setCustomName("Build analyzer regression");
		town.creatures.resize(town.getTown()->creatures.size());
		town.addBuilding(BuildingID::VILLAGE_HALL);
		town.addBuilding(BuildingID::TAVERN);

		for(int level : {0, 6})
		{
			town.addBuilding(BuildingID::getDwellingFromLevel(level, 0));
			const int upgrade = level == 6 ? 1 : 0;
			if(upgrade)
				town.addBuilding(BuildingID::getDwellingFromLevel(level, upgrade));
			town.creatures[level] = {1, {town.getTown()->creatures.at(level).at(upgrade)}};
		}

		// Isolate existing production from construction. This is also a real
		// map-forbidden-buildings case, not an all-ALLOWED callback stub.
		for(const auto & building : town.getTown()->buildings)
			if(!town.hasBuilt(building.first))
				town.forbiddenBuildings.insert(building.first);
	}

	void SetUp() override
	{
		ai.cc = callback;
		ai.playerID = PlayerColor(0);
		ai.armyManager = std::make_unique<NK2AI::ArmyManager>(callback.get(), &ai);
		ai.buildAnalyzer = std::make_unique<NK2AI::BuildAnalyzer>(&ai);
		initializeTown(first, 0);
		initializeTown(second, 1);
		callback->towns = {&first};
	}

	TResources expectedArmyCost(const CGTownInstance & town) const
	{
		TResources cost;
		for(int level : {0, 6})
			cost += town.creatures[level].second.back().toCreature()->getFullRecruitCost() * town.creatureGrowth(level);
		return cost;
	}
};

TEST_F(Nullkiller2BuildAnalyzerRegression, DailyBuildLimitKeepsArmyBudgetAndKnownBuildings)
{
	ScopedGemRecruitCost rareResourceCost(first.creatures[6].second.back(), 3);
	first.built = 1;
	const auto expected = expectedArmyCost(first);
	ASSERT_GT(expected[EGameResID::GOLD], 0);
	ASSERT_GT(expected[EGameResID::GEMS], 0);
	ai.buildAnalyzer->update();

	const auto & infos = ai.buildAnalyzer->getDevelopmentInfo();
	ASSERT_EQ(infos.size(), 1u);
	EXPECT_EQ(infos.front().town, &first);
	EXPECT_EQ(infos.front().built.size(), 2u); // Upgraded dwelling replaces its base, not an extra cost.
	EXPECT_EQ(infos.front().armyCost, expected);
	EXPECT_GT(infos.front().armyStrength, 0u);
	EXPECT_TRUE(infos.front().toBuild.empty());
	EXPECT_TRUE(infos.front().townDevelopmentCost.empty());
	EXPECT_EQ(ai.buildAnalyzer->getMissingResourcesNow(1), expected);
	EXPECT_EQ(ai.buildAnalyzer->getMissingResourcesInTotal(1), expected);
	EXPECT_TRUE(ai.buildAnalyzer->isBuilt(first.getFactionID(), BuildingID::VILLAGE_HALL));
	EXPECT_FALSE(ai.buildAnalyzer->isBuilt(first.getFactionID(), BuildingID::CAPITOL));

	const auto income = first.dailyIncome();
	EXPECT_EQ(ai.buildAnalyzer->getDailyIncome(), income);
	const float days = LIBRARY->engineSettings()->getInteger(EGameSettings::GENERAL_DAYS_PER_WEEK);
	EXPECT_FLOAT_EQ(ai.buildAnalyzer->getGoldPressure(), expected[EGameResID::GOLD] / (1.0f + days * income[EGameResID::GOLD]));
}

TEST_F(Nullkiller2BuildAnalyzerRegression, RareResourceCostIsExplicitAndRestoredAfterAnalysis)
{
	first.built = 1;
	const auto creature = first.creatures[6].second.back();
	const auto loadedCost = creature.toCreature()->getFullRecruitCost();
	const auto loadedArmyCost = expectedArmyCost(first);
	const int growth = first.creatureGrowth(6);
	ASSERT_GT(growth, 0);

	for(TResource gems : {0, 3})
	{
		ASSERT_EQ(creature.toCreature()->getFullRecruitCost(), loadedCost);
		ScopedGemRecruitCost rareResourceCost(creature, gems);
		auto configuredCost = loadedCost;
		configuredCost[EGameResID::GEMS] = gems;
		ASSERT_EQ(creature.toCreature()->getFullRecruitCost(), configuredCost);

		auto expected = loadedArmyCost;
		expected[EGameResID::GEMS] += (gems - loadedCost[EGameResID::GEMS]) * growth;
		ai.buildAnalyzer->update();
		ASSERT_EQ(ai.buildAnalyzer->getDevelopmentInfo().size(), 1u);
		EXPECT_EQ(ai.buildAnalyzer->getDevelopmentInfo().front().armyCost, expected);
		EXPECT_EQ(ai.buildAnalyzer->getMissingResourcesNow(1), expected);
		EXPECT_EQ(ai.buildAnalyzer->getMissingResourcesInTotal(1), expected);
	}

	EXPECT_EQ(creature.toCreature()->getFullRecruitCost(), loadedCost);
	ai.buildAnalyzer->update();
	EXPECT_EQ(ai.buildAnalyzer->getMissingResourcesNow(1), loadedArmyCost);
}

TEST_F(Nullkiller2BuildAnalyzerRegression, CompletingDailyBuildDoesNotEraseExistingArmyDemand)
{
	ai.buildAnalyzer->update();
	const auto missing = ai.buildAnalyzer->getMissingResourcesNow(1);
	const auto pressure = ai.buildAnalyzer->getGoldPressure();
	ASSERT_EQ(missing, expectedArmyCost(first));

	first.built = 1;
	ai.buildAnalyzer->update();
	EXPECT_EQ(ai.buildAnalyzer->getMissingResourcesNow(1), missing);
	EXPECT_EQ(ai.buildAnalyzer->getMissingResourcesInTotal(1), missing);
	EXPECT_FLOAT_EQ(ai.buildAnalyzer->getGoldPressure(), pressure);
}

TEST_F(Nullkiller2BuildAnalyzerRegression, MixedCappedAndUncappedTownsAreAdditiveAndOrderIndependent)
{
	first.built = 1;
	callback->towns = {&first, &second};
	const auto expected = expectedArmyCost(first) + expectedArmyCost(second);
	ai.buildAnalyzer->update();
	EXPECT_EQ(ai.buildAnalyzer->getDevelopmentInfo().size(), 2u);
	EXPECT_EQ(ai.buildAnalyzer->getMissingResourcesNow(1), expected);
	const auto pressure = ai.buildAnalyzer->getGoldPressure();

	callback->towns = {&second, &first};
	ai.buildAnalyzer->update();
	EXPECT_EQ(ai.buildAnalyzer->getMissingResourcesNow(1), expected);
	EXPECT_FLOAT_EQ(ai.buildAnalyzer->getGoldPressure(), pressure);
}

TEST_F(Nullkiller2BuildAnalyzerRegression, ArmyResourcesStayReservedForTradeAfterDailyBuild)
{
	first.built = 1;
	const auto expected = expectedArmyCost(first);
	callback->player.resources = expected * 2;
	ai.buildAnalyzer->update();
	EXPECT_EQ(ai.buildAnalyzer->getFreeResourcesAfterMissingTotal(1), expected);
	EXPECT_TRUE(ai.buildAnalyzer->getMissingResourcesNow(1).empty());
}

TEST_F(Nullkiller2BuildAnalyzerRegression, ConfiguredBuildCapStopsOnlyConstruction)
{
	callback->setDailyBuildCap(2);
	first.built = 1;
	first.forbiddenBuildings.erase(BuildingID::MARKETPLACE);
	callback->player.resources += 100000;
	ASSERT_EQ(callback->canBuildStructure(&first, BuildingID::MARKETPLACE), EBuildingState::ALLOWED);
	ai.buildAnalyzer->update();
	ASSERT_EQ(ai.buildAnalyzer->getDevelopmentInfo().size(), 1u);
	EXPECT_THAT(ai.buildAnalyzer->getDevelopmentInfo().front().toBuild,
		::testing::Contains(::testing::Field(&NK2AI::BuildingInfo::id, BuildingID::MARKETPLACE)));

	first.built = 2;
	ASSERT_EQ(callback->canBuildStructure(&first, BuildingID::MARKETPLACE), EBuildingState::CANT_BUILD_TODAY);
	ai.buildAnalyzer->update();
	ASSERT_EQ(ai.buildAnalyzer->getDevelopmentInfo().size(), 1u);
	EXPECT_TRUE(ai.buildAnalyzer->getDevelopmentInfo().front().toBuild.empty());
	EXPECT_EQ(ai.buildAnalyzer->getDevelopmentInfo().front().armyCost, expectedArmyCost(first));
}

TEST_F(Nullkiller2BuildAnalyzerRegression, RepeatedUpdateReplacesRatherThanAccumulatesTownState)
{
	first.built = 1;
	ai.buildAnalyzer->update();
	ai.buildAnalyzer->update();
	EXPECT_EQ(ai.buildAnalyzer->getMissingResourcesNow(1), expectedArmyCost(first));

	callback->towns.clear();
	ai.buildAnalyzer->update();
	EXPECT_TRUE(ai.buildAnalyzer->getDevelopmentInfo().empty());
	EXPECT_TRUE(ai.buildAnalyzer->getMissingResourcesNow(1).empty());
	EXPECT_TRUE(ai.buildAnalyzer->getDailyIncome().empty());
	EXPECT_FLOAT_EQ(ai.buildAnalyzer->getGoldPressure(), 0);
	EXPECT_FALSE(ai.buildAnalyzer->isBuilt(first.getFactionID(), BuildingID::VILLAGE_HALL));
}

TEST_F(Nullkiller2BuildAnalyzerRegression, ForbiddenBuildingsDoNotReserveDevelopmentResources)
{
	ASSERT_EQ(callback->canBuildStructure(&first, BuildingID::MARKETPLACE), EBuildingState::FORBIDDEN);
	ai.buildAnalyzer->update();
	ASSERT_EQ(ai.buildAnalyzer->getDevelopmentInfo().size(), 1u);
	const auto & info = ai.buildAnalyzer->getDevelopmentInfo().front();
	EXPECT_TRUE(info.toBuild.empty());
	EXPECT_TRUE(info.townDevelopmentCost.empty());
	EXPECT_TRUE(info.requiredResources.empty());
	EXPECT_EQ(ai.buildAnalyzer->getMissingResourcesInTotal(1), expectedArmyCost(first));
}

TEST_F(Nullkiller2BuildAnalyzerRegression, UnaffordableAllowedBuildingStillReservesItsCost)
{
	first.forbiddenBuildings.erase(BuildingID::MARKETPLACE);
	ASSERT_EQ(callback->canBuildStructure(&first, BuildingID::MARKETPLACE), EBuildingState::NO_RESOURCES);
	ai.buildAnalyzer->update();
	ASSERT_EQ(ai.buildAnalyzer->getDevelopmentInfo().size(), 1u);
	const auto cost = first.getBuildingCost(BuildingID::MARKETPLACE);
	const auto & info = ai.buildAnalyzer->getDevelopmentInfo().front();
	ASSERT_EQ(info.toBuild.size(), 1u);
	EXPECT_TRUE(info.toBuild.front().isMissingResources);
	EXPECT_EQ(info.requiredResources, cost);
	EXPECT_EQ(info.townDevelopmentCost, cost);
	EXPECT_EQ(ai.buildAnalyzer->getMissingResourcesNow(1), expectedArmyCost(first) + cost);
}

TEST_F(Nullkiller2BuildAnalyzerRegression, AdventureResourceRewardsPreferActualShortages)
{
	first.forbiddenBuildings.erase(BuildingID::MARKETPLACE);
	ai.buildAnalyzer->update();
	NK2AI::RewardEvaluator evaluator(&ai);
	TResources reward;
	reward[EGameResID::WOOD] = 1;
	ASSERT_GT(ai.buildAnalyzer->getMissingResourcesNow()[EGameResID::WOOD], 0);
	EXPECT_FLOAT_EQ(evaluator.getCombinedResourceRequirementStrength(reward), 1.0f);

	callback->player.resources += 100000;
	ai.buildAnalyzer->update();
	EXPECT_FLOAT_EQ(evaluator.getCombinedResourceRequirementStrength(reward), NK2AI::MINIMUM_STRATEGICAL_VALUE_NON_TOWN);
}

TEST_F(Nullkiller2BuildAnalyzerRegression, RejectedMarketTradeStopsWithoutRepeatedRequests)
{
	first.addBuilding(BuildingID::MARKETPLACE);
	callback->player.resources[EGameResID::WOOD] = 100;
	callback->tradeRequestLimit = 1; // Fail the old loop promptly instead of hanging the test.
	ASSERT_TRUE(first.hasBuiltResourceMarketplace());
	const auto before = callback->player.resources;
	EXPECT_FALSE(NK2AI::ResourceTrader::trade(*ai.buildAnalyzer, *callback, before));
	EXPECT_EQ(callback->tradeRequests, 1);
	EXPECT_EQ(callback->player.resources, before);
}

TEST_F(Nullkiller2BuildAnalyzerRegression, SuccessfulMarketTradesContinueWhileResourcesChange)
{
	first.addBuilding(BuildingID::MARKETPLACE);
	callback->player.resources[EGameResID::WOOD] = 100;
	callback->applyTrades = true;
	const auto before = callback->player.resources;
	EXPECT_TRUE(NK2AI::ResourceTrader::trade(*ai.buildAnalyzer, *callback, before));
	EXPECT_GT(callback->tradeRequests, 1);
	EXPECT_EQ(callback->player.resources[EGameResID::WOOD], 0);
	EXPECT_GT(callback->player.resources[EGameResID::GOLD], 0);
}

TEST_F(Nullkiller2BuildAnalyzerRegression, NoMarketplaceDoesNotSendTradeRequest)
{
	callback->player.resources[EGameResID::WOOD] = 100;
	EXPECT_FALSE(NK2AI::ResourceTrader::trade(*ai.buildAnalyzer, *callback, callback->player.resources));
	EXPECT_EQ(callback->tradeRequests, 0);
}

}
