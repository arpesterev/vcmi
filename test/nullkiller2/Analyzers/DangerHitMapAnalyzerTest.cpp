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
