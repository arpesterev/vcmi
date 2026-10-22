/*
 * DwellingGrowthRegression.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "../StdInc.h"
#include "StrategyCallbackStub.h"

#include "../../AI/Nullkiller2/Engine/Nullkiller.h"
#include "../../lib/mapObjects/CGDwelling.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

namespace test
{

class Nullkiller2DwellingGrowthRegression : public ::testing::Test
{
protected:
	std::shared_ptr<StrategyCallbackStub> callback = std::make_shared<StrategyCallbackStub>();
	NK2AI::Nullkiller ai;
	CGHeroInstance hero{callback.get()};
	CGDwelling dwelling{callback.get()};

	void SetUp() override
	{
		ai.cc = callback;
		ai.playerID = PlayerColor(0);
		hero.tempOwner = PlayerColor(0);
		dwelling.tempOwner = PlayerColor(1);
		dwelling.ID = Obj::CREATURE_GENERATOR4;
	}

	uint64_t evaluate(std::initializer_list<CreatureID> creatures)
	{
		dwelling.creatures.clear();
		for(const auto & creature : creatures)
			dwelling.creatures.push_back({1, {creature}});
		// The actual production evaluator dispatches to getDwellingArmyGrowth.
		return NK2AI::RewardEvaluator(&ai).getArmyGrowth(&dwelling, &hero, nullptr);
	}
};

TEST_F(Nullkiller2DwellingGrowthRegression, MultipleEntriesEqualSumOfIndividualRewards)
{
	const CreatureID first(0);
	const CreatureID second(2);
	const auto firstOnly = evaluate({first});
	const auto secondOnly = evaluate({second});
	ASSERT_GT(firstOnly, 0u);
	ASSERT_GT(secondOnly, 0u);
	ASSERT_NE(firstOnly, secondOnly);

	// Metamorphic oracle: independent dwelling entries must be additive.
	// Old code reweights firstOnly when it processes the second entry.
	EXPECT_EQ(evaluate({first, second}), firstOnly + secondOnly);
	EXPECT_EQ(evaluate({second, first}), firstOnly + secondOnly);
}

TEST_F(Nullkiller2DwellingGrowthRegression, MultipleEntriesAreInvariantToOrder)
{
	const auto forward = evaluate({CreatureID(0), CreatureID(2)});
	const auto reverse = evaluate({CreatureID(2), CreatureID(0)});
	EXPECT_EQ(forward, reverse);
}

TEST_F(Nullkiller2DwellingGrowthRegression, SingleEntryKeepsDayWeighting)
{
	callback->dayOfWeek = 1;
	const auto dayOne = evaluate({CreatureID(0)});
	ASSERT_GT(dayOne, 0u);
	callback->dayOfWeek = 7;
	EXPECT_EQ(evaluate({CreatureID(0)}), 7 * dayOne);
}

TEST_F(Nullkiller2DwellingGrowthRegression, AccumulatingDwellingsIgnoreDayOfWeek)
{
	callback->setAccumulation(true);
	callback->dayOfWeek = 1;
	const auto dayOne = evaluate({CreatureID(0), CreatureID(2)});
	ASSERT_GT(dayOne, 0u);
	callback->dayOfWeek = 7;
	EXPECT_EQ(evaluate({CreatureID(0), CreatureID(2)}), dayOne);
	EXPECT_EQ(evaluate({CreatureID(2), CreatureID(0)}), dayOne);
}

TEST_F(Nullkiller2DwellingGrowthRegression, EmptyAndAlreadyOwnedDwellingsHaveNoGrowthReward)
{
	EXPECT_EQ(evaluate({}), 0u);
	dwelling.tempOwner = hero.tempOwner;
	EXPECT_EQ(evaluate({CreatureID(0), CreatureID(2)}), 0u);
}

}
