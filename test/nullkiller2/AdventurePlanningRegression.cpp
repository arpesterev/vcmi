/*
 * AdventurePlanningRegression.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "../StdInc.h"
#include "StrategyCallbackStub.h"

#include "../../AI/Nullkiller2/Behaviors/CaptureObjectsBehavior.h"
#include "../../AI/Nullkiller2/Engine/Nullkiller.h"
#include "../../AI/Nullkiller2/Goals/ExecuteHeroChain.h"
#include "../../AI/Nullkiller2/Pathfinding/Actions/SpecialAction.h"

// This existing production function is used by DefenceBehavior::evaluateDefence.
// The static-AI test target does not require changing DLL export visibility.
namespace NK2AI
{
bool isThreatUnderControl(const CGTownInstance *, const HitMapInfo &,
	const Nullkiller *, const std::vector<AIPath> &);
}

namespace test
{
namespace
{

NK2AI::AIPath makePath(const CGHeroInstance * hero, float cost, uint8_t turns = 0)
{
	NK2AI::AIPath path;
	path.targetHero = hero;
	path.heroArmy = hero;
	path.targetObjectDanger = 0;
	path.armyLoss = 0;
	path.targetObjectArmyLoss = 0;
	path.chainMask = 1;
	path.exchangeCount = 1;

	NK2AI::AIPathNodeInfo node{};
	node.cost = cost;
	node.turns = turns;
	node.coord = int3(0, 0, 0);
	node.layer = EPathfindingLayer::LAND;
	node.targetHero = hero;
	node.parentIndex = -1;
	node.chainMask = 1;
	path.nodes.push_back(node);
	return path;
}

class BlockedAction final : public NK2AI::SpecialAction
{
public:
	std::string toString() const override { return "Blocked regression route"; }
};

}

class Nullkiller2AdventurePlanningRegression : public ::testing::Test
{
protected:
	std::shared_ptr<StrategyCallbackStub> callback = std::make_shared<StrategyCallbackStub>();
	NK2AI::Nullkiller ai;
	CGHeroInstance first{callback.get()};
	CGHeroInstance second{callback.get()};
	CGTownInstance town{callback.get()};

	void SetUp() override
	{
		ai.cc = callback;
		ai.playerID = PlayerColor(0);
		ai.settings = std::make_unique<NK2AI::Settings>(0);
		ai.heroManager = std::make_unique<NK2AI::HeroManager>(callback.get(), &ai);
		ai.buildAnalyzer = std::make_unique<NK2AI::BuildAnalyzer>(&ai);
		first.id = ObjectInstanceID(1);
		second.id = ObjectInstanceID(2);
		first.tempOwner = second.tempOwner = ai.playerID;
		first.setHeroType(HeroTypeID(0));
		second.setHeroType(HeroTypeID(0));
		ASSERT_TRUE(first.setCreature(SlotID(0), CreatureID(0), 100));
		ASSERT_TRUE(second.setCreature(SlotID(0), CreatureID(0), 100));
		callback->dayOfWeek = 1;
	}

	NK2AI::HitMapInfo laterThreat() const
	{
		NK2AI::HitMapInfo threat;
		threat.turn = 2;
		threat.danger = 1;
		return threat;
	}
};

TEST_F(Nullkiller2AdventurePlanningRegression, ZeroMovementRouteHasFiniteClosestWayRatio)
{
	const auto path = makePath(&first, 0);
	// Exercise the actual capture-goal producer, not a duplicate formula.
	const auto goals = NK2AI::Goals::CaptureObjectsBehavior::getVisitGoals({path}, &ai);
	ASSERT_EQ(goals.size(), 1u);
	const auto * capture = dynamic_cast<NK2AI::Goals::ExecuteHeroChain *>(goals.front().get());
	ASSERT_NE(capture, nullptr);
	EXPECT_TRUE(std::isfinite(capture->closestWayRatio));
	EXPECT_FLOAT_EQ(capture->closestWayRatio, 1.0f);
}

TEST_F(Nullkiller2AdventurePlanningRegression, PositiveMovementRatiosRemainUnchanged)
{
	const auto closer = makePath(&first, 0.5f);
	const auto farther = makePath(&second, 2.0f);
	const auto goals = NK2AI::Goals::CaptureObjectsBehavior::getVisitGoals({closer, farther}, &ai);
	ASSERT_EQ(goals.size(), 2u);
	const auto * firstGoal = dynamic_cast<NK2AI::Goals::ExecuteHeroChain *>(goals[0].get());
	const auto * secondGoal = dynamic_cast<NK2AI::Goals::ExecuteHeroChain *>(goals[1].get());
	ASSERT_NE(firstGoal, nullptr);
	ASSERT_NE(secondGoal, nullptr);
	EXPECT_FLOAT_EQ(firstGoal->closestWayRatio, 1.0f);
	EXPECT_FLOAT_EQ(secondGoal->closestWayRatio, 0.25f);
}

TEST_F(Nullkiller2AdventurePlanningRegression, LockedClosestHeroDoesNotZeroAvailableRoute)
{
	const auto locked = makePath(&first, 0);
	const auto available = makePath(&second, 0.5f);
	ai.lockHero(&first, NK2AI::HeroLockedReason::DEFENCE);
	const auto goals = NK2AI::Goals::CaptureObjectsBehavior::getVisitGoals({locked, available}, &ai);
	// Preserve one entry per path: counterattack planning indexes this vector.
	ASSERT_EQ(goals.size(), 2u);
	EXPECT_TRUE(goals[0]->invalid());
	const auto * capture = dynamic_cast<NK2AI::Goals::ExecuteHeroChain *>(goals[1].get());
	ASSERT_NE(capture, nullptr);
	EXPECT_FLOAT_EQ(capture->closestWayRatio, 1.0f);
}

TEST_F(Nullkiller2AdventurePlanningRegression, AvailableStrongRouteCanCoverLaterThreat)
{
	const auto path = makePath(&first, 0.5f);
	ASSERT_GT(path.getHeroStrength(), 2 * laterThreat().danger);
	EXPECT_TRUE(NK2AI::isThreatUnderControl(&town, laterThreat(), &ai, {path}));
}

TEST_F(Nullkiller2AdventurePlanningRegression, LockedRouteCannotSuppressTownDefence)
{
	const auto path = makePath(&first, 0.5f);
	ai.lockHero(&first, NK2AI::HeroLockedReason::DEFENCE);
	EXPECT_FALSE(NK2AI::isThreatUnderControl(&town, laterThreat(), &ai, {path}));
}

TEST_F(Nullkiller2AdventurePlanningRegression, BlockedRouteCannotSuppressTownDefence)
{
	auto path = makePath(&first, 0.5f);
	path.nodes.front().specialAction = std::make_shared<BlockedAction>();
	path.nodes.front().actionIsBlocked = true;
	EXPECT_FALSE(NK2AI::isThreatUnderControl(&town, laterThreat(), &ai, {path}));

	// A special action which can execute is not itself a reason to reject.
	path.nodes.front().actionIsBlocked = false;
	EXPECT_TRUE(NK2AI::isThreatUnderControl(&town, laterThreat(), &ai, {path}));
}

TEST_F(Nullkiller2AdventurePlanningRegression, LateOrWeakRouteCannotCoverThreat)
{
	EXPECT_FALSE(NK2AI::isThreatUnderControl(&town, laterThreat(), &ai,
		{makePath(&first, 3.0f, 3)}));
	auto threat = laterThreat();
	threat.danger = first.getTotalStrength();
	EXPECT_FALSE(NK2AI::isThreatUnderControl(&town, threat, &ai,
		{makePath(&first, 0.5f)}));
}

TEST_F(Nullkiller2AdventurePlanningRegression, UnneededResourcesKeepDocumentedMinimumValue)
{
	TResources reward;
	reward[EGameResID::WOOD] = 1;
	const NK2AI::RewardEvaluator evaluator(&ai);
	ASSERT_FLOAT_EQ(evaluator.getNowResourceRequirementStrength(EGameResID::WOOD), 0);
	ASSERT_FLOAT_EQ(evaluator.getTotalResourceRequirementStrength(EGameResID::WOOD), 0);
	EXPECT_FLOAT_EQ(evaluator.getCombinedResourceRequirementStrength(reward),
		NK2AI::MINIMUM_STRATEGICAL_VALUE_NON_TOWN);

	// A surplus is not a shortage bonus, and quantity does not multiply the
	// existing per-resource strategic score.
	callback->resources[EGameResID::WOOD] = 10000;
	reward[EGameResID::WOOD] = 1000;
	EXPECT_FLOAT_EQ(evaluator.getCombinedResourceRequirementStrength(reward),
		NK2AI::MINIMUM_STRATEGICAL_VALUE_NON_TOWN);
}

TEST_F(Nullkiller2AdventurePlanningRegression, ResourceCostsDoNotBecomeRewards)
{
	TResources reward;
	const NK2AI::RewardEvaluator evaluator(&ai);
	EXPECT_FLOAT_EQ(evaluator.getCombinedResourceRequirementStrength(reward), 0);
	reward[EGameResID::GOLD] = -1000;
	EXPECT_FLOAT_EQ(evaluator.getCombinedResourceRequirementStrength(reward), 0);
	reward[EGameResID::WOOD] = 2;
	EXPECT_FLOAT_EQ(evaluator.getCombinedResourceRequirementStrength(reward),
		NK2AI::MINIMUM_STRATEGICAL_VALUE_NON_TOWN);
	reward[EGameResID::ORE] = 2;
	EXPECT_FLOAT_EQ(evaluator.getCombinedResourceRequirementStrength(reward),
		2 * NK2AI::MINIMUM_STRATEGICAL_VALUE_NON_TOWN);
}

TEST_F(Nullkiller2AdventurePlanningRegression, EmptyAndTypeOnlyCaptureFiltersAreReflexive)
{
	const NK2AI::Goals::CaptureObjectsBehavior any;
	EXPECT_TRUE(any == any);
	NK2AI::Goals::CaptureObjectsBehavior mines;
	mines.ofType(Obj::MINE);
	EXPECT_TRUE(mines == mines);
}

TEST_F(Nullkiller2AdventurePlanningRegression, OverlappingCaptureFiltersAreNotEquivalent)
{
	NK2AI::Goals::CaptureObjectsBehavior firstFilter;
	firstFilter.ofType(Obj::MINE).ofType(Obj::RESOURCE);
	NK2AI::Goals::CaptureObjectsBehavior secondFilter;
	secondFilter.ofType(Obj::MINE).ofType(Obj::TOWN);
	EXPECT_FALSE(firstFilter == secondFilter);
	EXPECT_FALSE(secondFilter == firstFilter);
}

TEST_F(Nullkiller2AdventurePlanningRegression, CaptureFilterOrderAndDuplicatesDoNotMatter)
{
	NK2AI::Goals::CaptureObjectsBehavior firstFilter;
	firstFilter.ofType(Obj::MINE).ofType(Obj::RESOURCE);
	NK2AI::Goals::CaptureObjectsBehavior secondFilter;
	secondFilter.ofType(Obj::RESOURCE).ofType(Obj::MINE).ofType(Obj::MINE);
	EXPECT_TRUE(firstFilter == secondFilter);
	EXPECT_TRUE(secondFilter == firstFilter);

	NK2AI::Goals::CaptureObjectsBehavior subtypeOne;
	subtypeOne.ofType(Obj::MINE, 0);
	NK2AI::Goals::CaptureObjectsBehavior subtypeTwo;
	subtypeTwo.ofType(Obj::MINE, 1);
	EXPECT_FALSE(subtypeOne == subtypeTwo);
}

TEST_F(Nullkiller2AdventurePlanningRegression, OverlappingExplicitObjectSetsAreNotEquivalent)
{
	using Objects = std::vector<const CGObjectInstance *>;
	const NK2AI::Goals::CaptureObjectsBehavior firstSet(Objects{&first, &second});
	const NK2AI::Goals::CaptureObjectsBehavior secondSet(Objects{&first, &town});
	EXPECT_FALSE(firstSet == secondSet);
	EXPECT_FALSE(secondSet == firstSet);
	const NK2AI::Goals::CaptureObjectsBehavior reordered(Objects{&second, &first});
	EXPECT_TRUE(firstSet == reordered);
}

}
