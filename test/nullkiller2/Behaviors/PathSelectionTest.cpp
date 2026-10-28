/*
 * PathSelectionTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "AI/Nullkiller2/Behaviors/CaptureObjectsBehavior.h"
#include "AI/Nullkiller2/Behaviors/GatherArmyBehavior.h"
#include "AI/Nullkiller2/Goals/ExecuteHeroChain.h"
#include "nullkiller2/NullkillerTest.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/CGTownInstance.h"

namespace
{
const PlayerColor PLAYER(0);
const PlayerColor ENEMY(1);

class Nullkiller2_PathSelection : public NullkillerTest
{
protected:
	const CGHeroInstance * strongHero = nullptr;
	const CGHeroInstance * weakHero = nullptr;
	const CGTownInstance * enemyTown = nullptr;

	void startSelectionMap()
	{
		const CreatureID pikeman(CreatureID::decode("pikeman"));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NK2PathSelection")
			.playerActive(PLAYER).playerActive(ENEMY)
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("orrin")), PLAYER)
			.heroPrimary(50, 50, 1, 1).heroGarrison({{pikeman, 100}})
			.hero({8, 5, 0}, HeroTypeID(HeroTypeID::decode("valeska")), PLAYER)
			.heroPrimary(0, 0, 1, 1).heroGarrison({{pikeman, 100}})
			.town({30, 30, 0}, FactionID::CASTLE, ENEMY);
		startWithMap(std::move(builder));
		revealMap(PLAYER);

		const auto heroes = findAll<CGHeroInstance>();
		ASSERT_EQ(heroes.size(), 2u);
		strongHero = heroes.front();
		weakHero = heroes.back();
		if(NK2AI::getNormalizedHeroStrength(strongHero) < NK2AI::getNormalizedHeroStrength(weakHero))
			std::swap(strongHero, weakHero);
		ASSERT_GT(NK2AI::getNormalizedHeroStrength(strongHero), NK2AI::getNormalizedHeroStrength(weakHero));
		enemyTown = findFirst<CGTownInstance>();
		ASSERT_NE(enemyTown, nullptr);
		ASSERT_EQ(enemyTown->getOwner(), ENEMY);
	}

	NK2AI::AIPath route(const CGHeroInstance & hero, float cost = 0.5f) const
	{
		NK2AI::AIPath path;
		path.targetHero = &hero;
		path.heroArmy = &hero;
		path.exchangeCount = 1;
		path.chainMask = 1;
		path.nodes.push_back(pathNode(hero, enemyTown->visitablePos()));
		path.nodes.front().cost = cost;
		return path;
	}
};
}

TEST(Nullkiller2_PathDefaults, initializesAllPathState)
{
	NK2AI::AIPath path;
	EXPECT_TRUE(path.nodes.empty());
	EXPECT_EQ(path.targetObjectDanger, 0u);
	EXPECT_EQ(path.armyLoss, 0u);
	EXPECT_EQ(path.targetObjectArmyLoss, 0u);
	EXPECT_EQ(path.targetHero, nullptr);
	EXPECT_EQ(path.heroArmy, nullptr);
	EXPECT_EQ(path.chainMask, 0u);
	EXPECT_EQ(path.exchangeCount, 0u);
	EXPECT_EQ(path.getTotalDanger(), 0u);
}

TEST(Nullkiller2_PathDefaults, initializesNodesWithoutValueInitialization)
{
	NK2AI::AIPathNodeInfo node;
	EXPECT_FLOAT_EQ(node.cost, 0.0f);
	EXPECT_EQ(node.turns, 0u);
	EXPECT_FALSE(node.coord.isValid());
	EXPECT_EQ(node.layer, EPathfindingLayer::LAND);
	EXPECT_EQ(node.danger, 0u);
	EXPECT_EQ(node.targetHero, nullptr);
	EXPECT_EQ(node.parentIndex, -1);
	EXPECT_EQ(node.chainMask, 0u);
	EXPECT_FALSE(node.specialAction);
	EXPECT_FALSE(node.actionIsBlocked);
}

TEST_F(Nullkiller2_PathSelection, enemyTownKeepsBestRouteAtOriginalIndex)
{
	ASSERT_NO_FATAL_FAILURE(startSelectionMap());
	auto gateway = makeGateway(PLAYER);
	gateway->nullkiller->heroManager->update();
	auto invalid = route(*strongHero);
	invalid.nodes.front().cost = std::numeric_limits<float>::quiet_NaN();
	const auto goals = NK2AI::Goals::CaptureObjectsBehavior::getVisitGoals(
		{route(*strongHero, 0.8f), invalid, route(*strongHero, 0.2f)},
		gateway->nullkiller.get(), enemyTown, true);

	ASSERT_EQ(goals.size(), 3u);
	EXPECT_TRUE(goals[0]->invalid());
	EXPECT_TRUE(goals[1]->invalid());
	const auto * selected = dynamic_cast<const NK2AI::Goals::ExecuteHeroChain *>(goals[2].get());
	ASSERT_NE(selected, nullptr);
	EXPECT_FLOAT_EQ(selected->getPath().movementCost(), 0.2f);
	EXPECT_FLOAT_EQ(selected->closestWayRatio, 1.0f);
}

TEST_F(Nullkiller2_PathSelection, enemyTownKeepsOneRoutePerHeroInInputOrder)
{
	ASSERT_NO_FATAL_FAILURE(startSelectionMap());
	auto gateway = makeGateway(PLAYER);
	gateway->nullkiller->heroManager->update();
	const auto goals = NK2AI::Goals::CaptureObjectsBehavior::getVisitGoals(
		{route(*weakHero, 0.2f), route(*strongHero, 0.3f), route(*weakHero, 0.5f)},
		gateway->nullkiller.get(), enemyTown, true);

	ASSERT_EQ(goals.size(), 3u);
	const auto * first = dynamic_cast<const NK2AI::Goals::ExecuteHeroChain *>(goals[0].get());
	const auto * second = dynamic_cast<const NK2AI::Goals::ExecuteHeroChain *>(goals[1].get());
	ASSERT_NE(first, nullptr);
	ASSERT_NE(second, nullptr);
	EXPECT_EQ(first->getPath().targetHero, weakHero);
	EXPECT_EQ(second->getPath().targetHero, strongHero);
	EXPECT_TRUE(goals[2]->invalid());
}

TEST_F(Nullkiller2_PathSelection, unsafeShortcutDoesNotHideSafeTownRoute)
{
	ASSERT_NO_FATAL_FAILURE(startSelectionMap());
	auto gateway = makeGateway(PLAYER);
	gateway->nullkiller->heroManager->update();
	auto dangerous = route(*strongHero, 0.1f);
	dangerous.targetObjectDanger = std::numeric_limits<uint64_t>::max() / 4;
	const auto goals = NK2AI::Goals::CaptureObjectsBehavior::getVisitGoals(
		{dangerous, route(*strongHero, 0.5f)}, gateway->nullkiller.get(), enemyTown, true);

	ASSERT_EQ(goals.size(), 2u);
	EXPECT_TRUE(goals[0]->invalid());
	EXPECT_FALSE(goals[1]->invalid());
}

TEST_F(Nullkiller2_PathSelection, invalidCostsAndEmptyPathDoNotHideValidRoute)
{
	ASSERT_NO_FATAL_FAILURE(startSelectionMap());
	auto gateway = makeGateway(PLAYER);
	gateway->nullkiller->heroManager->update();
	const auto goals = NK2AI::Goals::CaptureObjectsBehavior::getVisitGoals(
		{route(*strongHero, std::numeric_limits<float>::quiet_NaN()),
		 route(*strongHero, std::numeric_limits<float>::infinity()),
		 route(*strongHero, -1.0f), NK2AI::AIPath(), route(*strongHero, 0.5f)},
		gateway->nullkiller.get(), enemyTown, true);

	ASSERT_EQ(goals.size(), 5u);
	for(size_t i = 0; i < 4; ++i)
		EXPECT_TRUE(goals[i]->invalid()) << i;
	EXPECT_FALSE(goals[4]->invalid());
}

TEST_F(Nullkiller2_PathSelection, zeroCostTownRouteHasFiniteClosestWayRatio)
{
	ASSERT_NO_FATAL_FAILURE(startSelectionMap());
	auto gateway = makeGateway(PLAYER);
	gateway->nullkiller->heroManager->update();
	const auto goals = NK2AI::Goals::CaptureObjectsBehavior::getVisitGoals(
		{route(*strongHero, 0.0f)}, gateway->nullkiller.get(), enemyTown, true);

	ASSERT_EQ(goals.size(), 1u);
	const auto * selected = dynamic_cast<const NK2AI::Goals::ExecuteHeroChain *>(goals[0].get());
	ASSERT_NE(selected, nullptr);
	EXPECT_TRUE(std::isfinite(selected->closestWayRatio));
	EXPECT_FLOAT_EQ(selected->closestWayRatio, 1.0f);
}

TEST_F(Nullkiller2_PathSelection, strongRecipientCannotMakeWeakCourierSafe)
{
	ASSERT_NO_FATAL_FAILURE(startSelectionMap());
	constexpr float safeAttackRatio = 1.2f;
	auto path = route(*weakHero);
	path.targetObjectDanger = NK2AI::getHeroFightingStrength(weakHero, path.heroArmy);
	ASSERT_GT(path.targetObjectDanger, 0u);
	ASSERT_TRUE(NK2AI::isSafeToVisit(strongHero, path.heroArmy, path.getTotalDanger(), safeAttackRatio));
	ASSERT_FALSE(NK2AI::isSafeToVisit(weakHero, path.heroArmy, path.getTotalDanger(), safeAttackRatio));
	EXPECT_FALSE(NK2AI::isSafeArmyDeliveryPath(path, safeAttackRatio));
}

TEST_F(Nullkiller2_PathSelection, strongCourierSafetyDoesNotDependOnWeakRecipient)
{
	ASSERT_NO_FATAL_FAILURE(startSelectionMap());
	constexpr float safeAttackRatio = 1.2f;
	auto path = route(*strongHero);
	path.targetObjectDanger = NK2AI::getHeroFightingStrength(weakHero, path.heroArmy);
	ASSERT_GT(path.targetObjectDanger, 0u);
	ASSERT_FALSE(NK2AI::isSafeToVisit(weakHero, path.heroArmy, path.getTotalDanger(), safeAttackRatio));
	ASSERT_TRUE(NK2AI::isSafeToVisit(strongHero, path.heroArmy, path.getTotalDanger(), safeAttackRatio));
	EXPECT_TRUE(NK2AI::isSafeArmyDeliveryPath(path, safeAttackRatio));
}

TEST(Nullkiller2_PathDefaults, incompleteDeliveryRouteIsNotSafe)
{
	EXPECT_FALSE(NK2AI::isSafeArmyDeliveryPath(NK2AI::AIPath(), 1.2f));
}
