/*
 * ArmyManagerTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "AI/Nullkiller2/Analyzers/ArmyManager.h"

#include "mock/GameHandlerTestClient.h"
#include "mock/TinyH3MBuilder.h"
#include "nullkiller2/NullkillerTest.h"

#include "lib/CPlayerState.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/CGTownInstance.h"
#include "lib/networkPacks/PacksForClient.h"

TEST(Nullkiller2_Analyzers_ArmyManager, armyWithoutOneSourceCreatureDoesNotTakeWholeSource)
{
	CGHeroInstance source(nullptr);
	ASSERT_TRUE(source.setCreature(SlotID(0), CreatureID(0), 1));
	ASSERT_TRUE(source.setCreature(SlotID(1), CreatureID(14), 5));

	// e.g. the best army kept only one faction, so creature 14 stays in the source
	const std::vector<NK2AI::SlotInfo> partial = { { CreatureID(0).toCreature(), 1, 1 } };
	EXPECT_FALSE(NK2AI::ArmyManager::takesWholeSource(&source, partial));

	const std::vector<NK2AI::SlotInfo> whole = { { CreatureID(0).toCreature(), 1, 1 }, { CreatureID(14).toCreature(), 5, 5 } };
	EXPECT_TRUE(NK2AI::ArmyManager::takesWholeSource(&source, whole));
}

using Nullkiller2_Analyzers_ArmyManagerMap = NullkillerTest;

TEST_F(Nullkiller2_Analyzers_ArmyManagerMap, targetKeepsOwnArmyWhenOnlyWantedUnitMustStayInSource)
{
	const PlayerColor player(0);

	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false)
		.playerActive(player)
		.hero({5, 5, 0}, HeroTypeID(0), player)
		.heroGarrison({{CreatureID::SKELETON, 1}})
		.hero({6, 5, 0}, HeroTypeID(1), player)
		.heroGarrison({{CreatureID(13), 1}});
	startWithMap(std::move(builder));

	const auto gateway = makeGateway(player);
	const auto * target = findHeroAt({5, 5, 0});
	const auto * source = findHeroAt({6, 5, 0});
	ASSERT_NE(target, nullptr);
	ASSERT_NE(source, nullptr);

	// undead would only lower morale of the archangel (13), which is the last unit of its hero
	const auto army = gateway->nullkiller->armyManager->getBestArmy(target, target, source, TerrainId::GRASS);

	ASSERT_EQ(army.size(), 1) << "an empty army makes the AI move away the last unit of the target";
	EXPECT_EQ(army.front().creature->getId(), CreatureID::SKELETON);
}

namespace
{
const PlayerColor PLAYER(0);
const PlayerColor ENEMY(1);

class Nullkiller2_RecruitmentPlanning : public NullkillerTest
{
protected:
	void startRecruitmentMap()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NK2AffordableRecruitment")
			.playerActive(PLAYER).playerActive(ENEMY)
			.town({9, 5, 0}, FactionID::CASTLE, PLAYER)
			.hero({5, 5, 0}, HeroTypeID(0), PLAYER)
			.town({30, 30, 0}, FactionID::CASTLE, ENEMY);
		startWithMap(std::move(builder));
		town = nullptr;
		for(auto * candidate : findAll<CGTownInstance>())
			if(candidate->getOwner() == PLAYER)
				town = candidate;
		ASSERT_NE(town, nullptr);

		town->addBuilding(BuildingID::CITY_HALL);
		town->addBuilding(BuildingID::DWELL_LVL_1);
		town->clearSlots();
		const std::array<std::string, 6> existing = {"archer", "griffin", "swordsman", "monk", "cavalier", "angel"};
		for(size_t i = 0; i < existing.size(); ++i)
			ASSERT_TRUE(town->setCreature(SlotID(static_cast<int>(i)), CreatureID(CreatureID::decode(existing[i])), 1));

		for(auto & level : town->creatures)
			level.first = 0;
		town->creatures.at(0) = {10, {CreatureID(CreatureID::decode("pikeman"))}};
		town->creatures.at(6) = {1, {CreatureID(CreatureID::decode("archangel"))}};
		revealMap(PLAYER);
	}

	CGTownInstance * town = nullptr;
};
}

TEST_F(Nullkiller2_RecruitmentPlanning, unaffordableGoldCostDoesNotConsumeTheLastSlot)
{
	ASSERT_NO_FATAL_FAILURE(startRecruitmentMap());
	const CreatureID cheap(CreatureID::decode("pikeman"));
	const auto budget = cheap.toCreature()->getFullRecruitCost() * 2;
	ASSERT_FALSE(budget.canAfford(CreatureID(CreatureID::decode("archangel")).toCreature()->getFullRecruitCost()));
	const auto gateway = makeGateway(PLAYER);

	const auto purchases = gateway->nullkiller->armyManager->getArmyAvailableToBuy(town, town, budget);
	ASSERT_EQ(purchases.size(), 1u);
	EXPECT_EQ(purchases.front().creID, cheap);
	EXPECT_EQ(purchases.front().count, 2);
	EXPECT_EQ(town->stacksCount(), 6) << "planning must not mutate the army";
}

TEST_F(Nullkiller2_RecruitmentPlanning, missingRareResourceDoesNotSuppressCheapRecruitment)
{
	ASSERT_NO_FATAL_FAILURE(startRecruitmentMap());
	const auto * expensive = CreatureID(CreatureID::decode("archangel")).toCreature();
	// Public regression data do not guarantee the original creature's rare-resource cost.
	struct RestoreCost
	{
		TResources & cost;
		TResources original;
		~RestoreCost() { cost = original; }
	} restore{const_cast<TResources &>(expensive->getFullRecruitCost()), expensive->getFullRecruitCost()};
	restore.cost[GameResID::GEMS] = 1;
	TResources budget;
	budget[GameResID::GOLD] = 100000;
	ASSERT_FALSE(budget.canAfford(expensive->getFullRecruitCost()));
	const auto gateway = makeGateway(PLAYER);

	const auto purchases = gateway->nullkiller->armyManager->getArmyAvailableToBuy(town, town, budget);
	ASSERT_EQ(purchases.size(), 1u);
	EXPECT_EQ(purchases.front().creID, CreatureID(CreatureID::decode("pikeman")));
	EXPECT_EQ(purchases.front().count, 10);
}

TEST_F(Nullkiller2_RecruitmentPlanning, completeTurnBuysAffordableTroopsWithOneSlotLeft)
{
	ASSERT_NO_FATAL_FAILURE(startRecruitmentMap());
	const CreatureID cheap(CreatureID::decode("pikeman"));
	gameState()->players.at(PLAYER).resources = cheap.toCreature()->getFullRecruitCost() * 2;
	const auto * hero = findHeroByOwner(PLAYER);
	ASSERT_NE(hero, nullptr);
	SetMovePoints exhausted(hero->id, 0);
	gameState()->apply(exhausted);
	GameHandlerTestClient client(gameState(), PLAYER);
	const auto gateway = makeGateway(PLAYER, &client);
	town->built = gateway->cc->getSettings().getInteger(EGameSettings::TOWNS_BUILDINGS_PER_TURN_CAP);

	// No manually chosen BuyArmy task: exercise the complete turn planner.
	gateway->nullkiller->makeTurn();

	const auto slot = town->getSlotFor(cheap);
	ASSERT_TRUE(town->hasStackAtSlot(slot));
	EXPECT_EQ(town->getStackCount(slot), 2);
	EXPECT_EQ(town->creatures.at(0).first, 8);
	EXPECT_EQ(town->creatures.at(6).first, 1);
}
