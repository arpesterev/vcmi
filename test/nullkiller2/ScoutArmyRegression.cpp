/*
 * ScoutArmyRegression.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "../StdInc.h"
#include "../../AI/Nullkiller2/Analyzers/ArmyManager.h"
#include "../../lib/IGameSettings.h"

VCMI_LIB_USING_NAMESPACE

namespace test
{
namespace
{
class ScoutArmyCallback final : public CCallback
{
public:
	ScoutArmyCallback() : CCallback(nullptr, std::nullopt, nullptr) {}
	const IGameSettings & getSettings() const override { return *LIBRARY->engineSettings(); }
};

class ScoutCandidate final : public CCreature
{
	int tier;

public:
	ScoutCandidate(int speed, int tier) : tier(tier) { addBonus(speed, BonusType::STACKS_SPEED); }
	int32_t getLevel() const override { return tier; }
};
}

TEST(Nullkiller2ScoutArmyRegression, EqualSpeedKeepsCheaperUnitRegardlessOfOrder)
{
	ScoutArmyCallback callback;
	NK2AI::ArmyManager manager(&callback, nullptr);
	ScoutCandidate expensive(5, 2);
	ScoutCandidate cheap(5, 2);
	std::vector<NK2AI::SlotInfo> army{{&expensive, 10, 10000}, {&cheap, 20, 2000}};

	auto selected = manager.getBestUnitForScout(army, TerrainId::NONE);
	ASSERT_NE(selected, army.end());
	EXPECT_EQ(selected->creature, &cheap);
	std::reverse(army.begin(), army.end());
	selected = manager.getBestUnitForScout(army, TerrainId::NONE);
	ASSERT_NE(selected, army.end());
	EXPECT_EQ(selected->creature, &cheap);
}

TEST(Nullkiller2ScoutArmyRegression, CheapHighTierUnitWinsOverExpensiveFasterUnit)
{
	ScoutArmyCallback callback;
	NK2AI::ArmyManager manager(&callback, nullptr);
	ScoutCandidate expensive(10, 5);
	ScoutCandidate cheap(5, 5);
	// Total power is 101000, hence one scout may keep up to 1010 power.
	std::vector<NK2AI::SlotInfo> army{{&expensive, 10, 100000}, {&cheap, 100, 1000}};

	auto selected = manager.getBestUnitForScout(army, TerrainId::NONE);
	ASSERT_NE(selected, army.end());
	EXPECT_EQ(selected->creature, &cheap);
	std::reverse(army.begin(), army.end());
	selected = manager.getBestUnitForScout(army, TerrainId::NONE);
	ASSERT_NE(selected, army.end());
	EXPECT_EQ(selected->creature, &cheap);
}

TEST(Nullkiller2ScoutArmyRegression, EmptyArmyReturnsEnd)
{
	ScoutArmyCallback callback;
	NK2AI::ArmyManager manager(&callback, nullptr);
	std::vector<NK2AI::SlotInfo> army;
	EXPECT_EQ(manager.getBestUnitForScout(army, TerrainId::NONE), army.end());
}
}
