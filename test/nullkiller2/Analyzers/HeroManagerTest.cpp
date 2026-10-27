/*
 * HeroManagerTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "AI/Nullkiller2/Analyzers/HeroManager.h"

TEST(Nullkiller2_Analyzers_HeroManager, armyCarrierBeatsBetterHeroProfileForMainRole)
{
	constexpr uint64_t strongestArmy = 10000;

	EXPECT_GT(
		NK2AI::evaluateMainHeroRoleScore(5.0f, strongestArmy, strongestArmy),
		NK2AI::evaluateMainHeroRoleScore(40.0f, strongestArmy / 20, strongestArmy))
		<< "the hero carrying the army should not be treated as a scout just because another hero has better skills";
}

TEST(Nullkiller2_Analyzers_HeroManager, heroProfileBreaksSimilarArmyTies)
{
	constexpr uint64_t strongestArmy = 10000;

	EXPECT_GT(
		NK2AI::evaluateMainHeroRoleScore(30.0f, strongestArmy, strongestArmy),
		NK2AI::evaluateMainHeroRoleScore(20.0f, strongestArmy, strongestArmy))
		<< "normal hero profile scoring should still decide between comparable army carriers";
}

TEST(Nullkiller2_Analyzers_HeroManager, strongestCarrierAndDifferentFrontAreMains)
{
	const auto chosen = NK2AI::selectMainHeroIndicesByFront({100.f, 90.f, 85.f}, {1, 1, 2}, 2);
	EXPECT_EQ(chosen, (std::vector<size_t>{0, 2}));
}

TEST(Nullkiller2_Analyzers_HeroManager, keepsStrongestHeroWithoutTowns)
{
	const auto chosen = NK2AI::selectMainHeroIndicesByFront({5.f, 20.f, 10.f}, {-1, -1, -1}, 1);
	EXPECT_EQ(chosen, (std::vector<size_t>{1}));
}

TEST(Nullkiller2_Analyzers_HeroManager, fillsAvailableSlotsFromSameFrontWhenNecessary)
{
	const auto chosen = NK2AI::selectMainHeroIndicesByFront({100.f, 95.f, 90.f}, {3, 3, 3}, 2);
	EXPECT_EQ(chosen, (std::vector<size_t>{0, 1}));
}
