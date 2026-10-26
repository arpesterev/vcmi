/*
 * MapTacticsScoringRegression.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "../StdInc.h"
#include "../../AI/Nullkiller2/Goals/ExploreNeighbourTile.h"
#include "../../AI/Nullkiller2/Analyzers/DangerHitMapAnalyzer.h"
#include <limits>

namespace test
{
TEST(Nullkiller2MapTacticsScoring, ScoutPrioritizesCheapMovementAtEqualDiscovery)
{
	EXPECT_GT(NK2AI::scoreScoutExplorationStep(10, 0.25f),
		NK2AI::scoreScoutExplorationStep(10, 0.75f));
	EXPECT_FLOAT_EQ(NK2AI::scoreScoutExplorationStep(10, 0.5f), 20.0f);
}

TEST(Nullkiller2MapTacticsScoring, ScoutRejectsInvalidInputs)
{
	EXPECT_FLOAT_EQ(NK2AI::scoreScoutExplorationStep(10, -1), 0);
	EXPECT_FLOAT_EQ(NK2AI::scoreScoutExplorationStep(0, 0.5f), 0);
	EXPECT_FLOAT_EQ(NK2AI::scoreScoutExplorationStep(10,
		std::numeric_limits<float>::quiet_NaN()), 0);
}

TEST(Nullkiller2MapTacticsScoring, EnemyUrgencyIsNeverNegativeOnLongRoutes)
{
	EXPECT_FLOAT_EQ(NK2AI::distanceDiscountedThreat(1000, 0), 1000);
	EXPECT_FLOAT_EQ(NK2AI::distanceDiscountedThreat(1000, 1), 500);
	EXPECT_FLOAT_EQ(NK2AI::distanceDiscountedThreat(1000, 3), 250);
	EXPECT_GT(NK2AI::distanceDiscountedThreat(1000, 10), 0);
}

TEST(Nullkiller2MapTacticsScoring, InvalidThreatMovementCostCannotHideDanger)
{
	EXPECT_FLOAT_EQ(NK2AI::distanceDiscountedThreat(1000, -1), 1000);
	EXPECT_FLOAT_EQ(NK2AI::distanceDiscountedThreat(1000,
		std::numeric_limits<float>::quiet_NaN()), 1000);
}
}
