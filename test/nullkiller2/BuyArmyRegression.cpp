/*
 * BuyArmyRegression.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "../StdInc.h"
#include "StrategyCallbackStub.h"

#include "../../AI/Nullkiller2/Behaviors/BuyArmyBehavior.h"
#include "../../AI/Nullkiller2/Engine/Nullkiller.h"
#include "../../lib/mapObjects/CGTownInstance.h"

namespace test
{

class PositionedTown final : public CGTownInstance
{
	int3 location;

public:
	PositionedTown(IGameInfoCallback * callback, int x)
		: CGTownInstance(callback), location(x, 0, 0)
	{
		ID = Obj::TOWN;
		subID = MapObjectSubID(0); // The real base-game Castle definition.
		id = ObjectInstanceID(x);
		tempOwner = PlayerColor(0);
	}

	int3 visitablePos() const override { return location; }
};

class HighGoldPressure final : public NK2AI::BuildAnalyzer
{
public:
	explicit HighGoldPressure(NK2AI::Nullkiller * ai) : BuildAnalyzer(ai) {}
	bool isGoldPressureOverMax() const override { return true; }
};

class ArmyAvailabilityProbe final : public NK2AI::ArmyManager
{
	CCallback * callback;

public:
	mutable std::vector<const CGDwelling *> queried;
	ArmyAvailabilityProbe(CCallback * callback, const NK2AI::Nullkiller * ai)
		: ArmyManager(callback, ai), callback(callback)
	{
	}

	std::shared_ptr<CCreatureSet> getArmyAvailableToBuyAsCCreatureSet(
		const CGDwelling * dwelling, TResources) const override
	{
		queried.push_back(dwelling);
		return std::make_shared<CGTownInstance>(callback);
	}
};

class Nullkiller2BuyArmyRegression : public ::testing::Test
{
protected:
	std::shared_ptr<StrategyCallbackStub> callback = std::make_shared<StrategyCallbackStub>();
	NK2AI::Nullkiller ai;
	PositionedTown savingTown{callback.get(), 0};
	PositionedTown otherTown{callback.get(), 1};
	CGHeroInstance hero{callback.get()};
	ArmyAvailabilityProbe * probe = nullptr;

	void SetUp() override
	{
		ai.cc = callback;
		ai.playerID = PlayerColor(0);
		hero.id = ObjectInstanceID(2);
		hero.tempOwner = ai.playerID;
		callback->heroes = {&hero};
		callback->towns = {&savingTown, &otherTown};

		// Preserve the actual canBuildStructure implementation. The first town
		// lacks city hall; the second is exempt from the economy-saving gate.
		otherTown.addBuilding(BuildingID::CITY_HALL);
		ASSERT_EQ(callback->canBuildStructure(&savingTown, BuildingID::CITY_HALL),
			EBuildingState::PREREQUIRES);

		ai.buildAnalyzer = std::make_unique<HighGoldPressure>(&ai);
		ai.armyManager = std::make_unique<ArmyAvailabilityProbe>(callback.get(), &ai);
		probe = static_cast<ArmyAvailabilityProbe *>(ai.armyManager.get());

		// A real empty hero-role map classifies the hero as SCOUT. We assert
		// whether town recruitment is evaluated, not whether a goal is issued.
		ai.heroManager = std::make_unique<NK2AI::HeroManager>(callback.get(), &ai);

		// Initialize the real private threat map through its public API.
		// Empty AI memory means no enemy paths or game simulation are needed.
		ai.dangerHitMap = std::make_unique<NK2AI::DangerHitMapAnalyzer>(&ai);
		ai.dangerHitMap->updateHitMap();
		ASSERT_GE(ai.dangerHitMap->getTileThreat(savingTown.visitablePos()).fastestDanger.turn, 2);
	}

	void evaluate()
	{
		probe->queried.clear();
		NK2AI::Goals::BuyArmyBehavior behavior;
		const auto goals = behavior.decompose(&ai);
		EXPECT_TRUE(goals.empty()); // SCOUT control, not a recruitment-goal claim.
	}
};

TEST_F(Nullkiller2BuyArmyRegression, EconomyGateDoesNotSkipLaterTownEvaluation)
{
	evaluate();
	EXPECT_THAT(probe->queried, ::testing::ElementsAre(&otherTown));
}

TEST_F(Nullkiller2BuyArmyRegression, TownEvaluationDoesNotDependOnTownOrder)
{
	evaluate();
	const auto firstOrder = probe->queried;
	callback->towns = {&otherTown, &savingTown};
	evaluate();
	EXPECT_THAT(probe->queried, ::testing::ElementsAre(&otherTown));
	EXPECT_EQ(firstOrder, probe->queried);
}

TEST_F(Nullkiller2BuyArmyRegression, NoHeroesDoesNotEvaluateRecruitment)
{
	callback->heroes.clear();
	evaluate();
	EXPECT_TRUE(probe->queried.empty());
}

TEST_F(Nullkiller2BuyArmyRegression, AllSavingTownsStayExcluded)
{
	otherTown.removeBuilding(BuildingID::CITY_HALL);
	evaluate();
	EXPECT_TRUE(probe->queried.empty());
}

}
