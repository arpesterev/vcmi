/*
 * GatherArmyBehaviorTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "AI/Nullkiller2/AIGateway.h"
#include "AI/Nullkiller2/Behaviors/DefenceBehavior.h"
#include "AI/Nullkiller2/Behaviors/GatherArmyBehavior.h"
#include "AI/Nullkiller2/Engine/Nullkiller.h"

#include "mock/GameHandlerTestClient.h"
#include "mock/TinyH3MBuilder.h"
#include "nullkiller2/NullkillerTest.h"

#include "lib/callback/CCallback.h"
#include "lib/gameState/CGameState.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/CGTownInstance.h"
#include "lib/networkPacks/PacksForClient.h"

namespace
{
const PlayerColor PLAYER(0);
const PlayerColor ENEMY(1);

TinyH3M::TinyH3MBuilder makeGarrisonUpgradeMap()
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder
		.size(36, false)
		.name("NK2GarrisonUpgrade")
		.playerActive(PLAYER)
		.playerActive(ENEMY)
		.town({9, 5, 0}, FactionID::CASTLE, PLAYER)
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("orrin")), PLAYER)
		.town({30, 30, 0}, FactionID::CASTLE, ENEMY);

	return builder;
}

TinyH3M::TinyH3MBuilder makeSharedUpgradeDestinationMap()
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder
		.size(36, false)
		.name("NK2SharedUpgradeDestination")
		.playerActive(PLAYER)
		.playerActive(ENEMY)
		.town({9, 5, 0}, FactionID::CASTLE, PLAYER)
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("orrin")), PLAYER)
		.hero({13, 5, 0}, HeroTypeID(HeroTypeID::decode("valeska")), PLAYER)
		.town({30, 30, 0}, FactionID::CASTLE, ENEMY);

	return builder;
}

class Nullkiller2_Behaviors_GatherArmyBehavior : public NullkillerTest
{
protected:
	void putHeroInGarrison(const CGHeroInstance & hero, const CGTownInstance & town) const
	{
		ChangeObjPos moveHero;
		moveHero.objid = hero.id;
		moveHero.nPos = town.visitablePos();
		moveHero.initiator = PLAYER;
		gameState()->apply(moveHero);

		SetHeroesInTown setHeroes;
		setHeroes.tid = town.id;
		setHeroes.visiting = ObjectInstanceID::NONE;
		setHeroes.garrison = hero.id;
		gameState()->apply(setHeroes);
	}

	void startOccupiedGarrison(int garrisonCount = 200, int visitorCount = 1)
	{
		ASSERT_NO_FATAL_FAILURE(startWithMap(makeSharedUpgradeDestinationMap()));
		auto * town = findFirst<CGTownInstance>();
		auto * garrison = findHeroAt({5, 5, 0});
		auto * visitor = findHeroAt({13, 5, 0});
		ASSERT_NE(town, nullptr);
		ASSERT_NE(garrison, nullptr);
		ASSERT_NE(visitor, nullptr);
		garrison->clearSlots();
		visitor->clearSlots();
		ASSERT_TRUE(garrison->setCreature(SlotID(0), CreatureID::ARCHER, garrisonCount));
		ASSERT_TRUE(visitor->setCreature(SlotID(0), CreatureID::ARCHER, visitorCount));
		garrison->setMovementPoints(2000);
		visitor->setMovementPoints(2000);
		for(const auto * hero : {garrison, visitor})
		{
			ChangeObjPos move;
			move.objid = hero->id;
			move.nPos = town->visitablePos();
			move.initiator = PLAYER;
			gameState()->apply(move);
		}
		SetHeroesInTown setHeroes;
		setHeroes.tid = town->id;
		setHeroes.garrison = garrison->id;
		setHeroes.visiting = visitor->id;
		gameState()->apply(setHeroes);
		ASSERT_EQ(town->getGarrisonHero(), garrison);
		ASSERT_EQ(town->getVisitingHero(), visitor);
		revealMap(PLAYER);
	}

	NK2AI::Goals::TGoalVec extractionGoals(NK2AI::AIGateway & gateway)
	{
		auto goals = NK2AI::Goals::DefenceBehavior().decompose(gateway.nullkiller.get());
		std::erase_if(goals, [](const auto & goal)
		{
			return goal->goalType != NK2AI::Goals::EXCHANGE_SWAP_TOWN_HEROES;
		});
		return goals;
	}
};
}

TEST_F(Nullkiller2_Behaviors_GatherArmyBehavior, upgradesPikemenCarriedByGarrisonHero)
{
	startWithMap(makeGarrisonUpgradeMap());

	auto * hero = findHeroByOwner(PLAYER);
	auto * town = findFirst<CGTownInstance>();
	ASSERT_NE(hero, nullptr);
	ASSERT_NE(town, nullptr);
	ASSERT_EQ(town->getFactionID(), FactionID::CASTLE);

	const CreatureID pikeman(CreatureID::decode("pikeman"));
	const CreatureID halberdier(CreatureID::decode("halberdier"));
	town->addBuilding(BuildingID::DWELL_LVL_1_UP);
	town->creatures.at(0).second.push_back(halberdier);
	hero->clearSlots();
	ASSERT_TRUE(hero->setCreature(SlotID(0), pikeman, 1000));
	putHeroInGarrison(*hero, *town);
	grantResources(PLAYER, GameResID(GameResID::GOLD), 1000000);

	GameHandlerTestClient client(gameState(), PLAYER);
	const auto gateway = makeGateway(PLAYER, &client);
	gateway->nullkiller->makeTurn();

	ASSERT_NE(hero->getStackPtr(SlotID(0)), nullptr);
	EXPECT_EQ(hero->getStackPtr(SlotID(0))->getCreatureID(), halberdier);
}

TEST_F(Nullkiller2_Behaviors_GatherArmyBehavior, reservesTownAcrossCompetingUpgradeRoutes)
{
	startWithMap(makeSharedUpgradeDestinationMap());

	auto * town = findFirst<CGTownInstance>();
	ASSERT_NE(town, nullptr);
	const CreatureID pikeman(CreatureID::decode("pikeman"));
	const CreatureID halberdier(CreatureID::decode("halberdier"));
	town->addBuilding(BuildingID::DWELL_LVL_1_UP);
	town->creatures.at(0).second.push_back(halberdier);
	grantResources(PLAYER, GameResID(GameResID::GOLD), 1000000);

	const auto gateway = makeGateway(PLAYER);
	const auto heroes = gateway->cc->getHeroesInfo();
	ASSERT_EQ(heroes.size(), 2);
	for(auto * hero : heroes)
	{
		auto * mutableHero = gameState()->getHero(hero->id);
		mutableHero->clearSlots();
		ASSERT_TRUE(mutableHero->setCreature(SlotID(0), pikeman, 1000));
		mutableHero->setMovementPoints(2500);
	}

	preparePlanning(*gateway);

	const auto goals = NK2AI::Goals::GatherArmyBehavior().decompose(gateway->nullkiller.get());
	NK2AI::TaskPlan plan;
	int upgradeRoutes = 0;
	for(const auto & goal : goals)
	{
		const auto parts = goal->decompose(gateway->nullkiller.get());
		const bool isUpgradeRoute = std::ranges::any_of(parts, [](const auto & part)
		{
			return part->goalType == NK2AI::Goals::ARMY_UPGRADE;
		});
		if(!isUpgradeRoute)
			continue;

		++upgradeRoutes;
		EXPECT_TRUE(goal->asTask()->isObjectAffected(town->id))
			<< "every route to the same town must claim that destination";
		goal->asTask()->priority = static_cast<float>(100 - upgradeRoutes);
		plan.mergeAndFilter(goal);
	}

	ASSERT_GE(upgradeRoutes, 2);
	EXPECT_EQ(plan.getTasks().size(), 1)
		<< "only one upgrade route may reserve a town in a planning batch";
}

TEST_F(Nullkiller2_Behaviors_GatherArmyBehavior, releasesStrongGarrisonWithOccupiedVisitorSlot)
{
	ASSERT_NO_FATAL_FAILURE(startOccupiedGarrison());
	auto * town = findFirst<CGTownInstance>();
	const auto * strongHero = town->getGarrisonHero();
	const auto * weakHero = town->getVisitingHero();
	ASSERT_GT(strongHero->estimateHeroCombatValue(), weakHero->estimateHeroCombatValue());
	GameHandlerTestClient client(gameState(), PLAYER);
	const auto gateway = makeGateway(PLAYER, &client);
	NK2AI::NullkillerTestAccess::prepareState(*gateway->nullkiller);

	const auto goals = extractionGoals(*gateway);
	ASSERT_EQ(goals.size(), 1u) << "a weak visitor must not trap the main army in a safe town";
	goals.front()->asTask()->accept(gateway.get());
	EXPECT_EQ(town->getVisitingHero(), strongHero);
	EXPECT_EQ(town->getGarrisonHero(), weakHero);
	EXPECT_FALSE(strongHero->isGarrisoned());
	EXPECT_FALSE(gateway->nullkiller->isHeroLocked(strongHero));
	EXPECT_TRUE(extractionGoals(*gateway).empty()) << "do not swap the same heroes back on the next pass";
}

TEST_F(Nullkiller2_Behaviors_GatherArmyBehavior, keepsStrongerVisitorMobile)
{
	ASSERT_NO_FATAL_FAILURE(startOccupiedGarrison(1, 200));
	const auto gateway = makeGateway(PLAYER);
	NK2AI::NullkillerTestAccess::prepareState(*gateway->nullkiller);
	EXPECT_TRUE(extractionGoals(*gateway).empty());
}

TEST_F(Nullkiller2_Behaviors_GatherArmyBehavior, occupiedGarrisonExtractionRespectsVisitorLock)
{
	ASSERT_NO_FATAL_FAILURE(startOccupiedGarrison());
	const auto gateway = makeGateway(PLAYER);
	NK2AI::NullkillerTestAccess::prepareState(*gateway->nullkiller);
	gateway->nullkiller->lockHero(findFirst<CGTownInstance>()->getVisitingHero(), NK2AI::HeroLockedReason::HERO_CHAIN);
	EXPECT_TRUE(extractionGoals(*gateway).empty());
}

TEST_F(Nullkiller2_Behaviors_GatherArmyBehavior, occupiedGarrisonExtractionRespectsDefenceLock)
{
	ASSERT_NO_FATAL_FAILURE(startOccupiedGarrison());
	const auto gateway = makeGateway(PLAYER);
	NK2AI::NullkillerTestAccess::prepareState(*gateway->nullkiller);
	gateway->nullkiller->lockHero(findFirst<CGTownInstance>()->getGarrisonHero(), NK2AI::HeroLockedReason::DEFENCE);
	EXPECT_TRUE(extractionGoals(*gateway).empty());
}

TEST_F(Nullkiller2_Behaviors_GatherArmyBehavior, occupiedGarrisonExtractionNeedsMovement)
{
	ASSERT_NO_FATAL_FAILURE(startOccupiedGarrison());
	const auto gateway = makeGateway(PLAYER);
	NK2AI::NullkillerTestAccess::prepareState(*gateway->nullkiller);
	gameState()->getHero(findFirst<CGTownInstance>()->getGarrisonHero()->id)->setMovementPoints(0);
	EXPECT_TRUE(extractionGoals(*gateway).empty());
}
