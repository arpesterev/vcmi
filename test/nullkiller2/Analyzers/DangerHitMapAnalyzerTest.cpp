/* GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "AI/Nullkiller2/Analyzers/DangerHitMapAnalyzer.h"

TEST(Nullkiller2_Analyzers_DangerHitMap, urgencyCannotBeNegativeOnLongRoutes)
{
	EXPECT_FLOAT_EQ(NK2AI::discountedEnemyThreat(1000, 0), 1000);
	EXPECT_FLOAT_EQ(NK2AI::discountedEnemyThreat(1000, 1), 500);
	EXPECT_GT(NK2AI::discountedEnemyThreat(1000, 10), 0);
	EXPECT_FLOAT_EQ(NK2AI::discountedEnemyThreat(1000, std::numeric_limits<float>::quiet_NaN()), 1000);
}

TEST(Nullkiller2_Analyzers_DangerHitMap, checksBothKnownEnemiesAtEndOfTurn)
{
	NK2AI::HitMapNode map;
	map.fastestDanger.turn = 0;
	map.fastestDanger.danger = 2000;
	map.maximumDanger.turn = 0;
	map.maximumDanger.danger = 4000;
	CGHeroInstance hero(nullptr);
	ASSERT_TRUE(hero.setCreature(SlotID(0), CreatureID::ARCHER, 1));
	EXPECT_TRUE(NK2AI::isKnownUnsafeAtEndOfTurn(map, &hero, &hero, 0, 1.1f));
	map.fastestDanger.turn = 2;
	map.maximumDanger.turn = 2;
	EXPECT_FALSE(NK2AI::isKnownUnsafeAtEndOfTurn(map, &hero, &hero, 0, 1.1f));
}

TEST(Nullkiller2_Analyzers_DangerHitMap, findsPhysicalOvernightStopBeforeTurnChange)
{
	CGPathNode origin;
	origin.coord = int3(1, 1, 0);
	origin.turns = 0;
	CGPathNode dayZeroStop;
	dayZeroStop.coord = int3(3, 1, 0);
	dayZeroStop.turns = 0;
	dayZeroStop.theNodeBefore = &origin;
	CGPathNode followingMorning;
	followingMorning.coord = int3(4, 1, 0);
	followingMorning.turns = 1;
	followingMorning.theNodeBefore = &dayZeroStop;
	CGPathNode destination;
	destination.coord = int3(6, 1, 0);
	destination.turns = 1;
	destination.theNodeBefore = &followingMorning;

	const auto stops = NK2AI::collectPlannedOvernightStops(&destination);
	ASSERT_EQ(stops.size(), 1u);
	EXPECT_EQ(stops.front().first, dayZeroStop.coord);
	EXPECT_EQ(stops.front().second, 0);
}
