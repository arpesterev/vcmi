/*
 * ArmyPlanningRegression.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "../StdInc.h"
#include "StrategyCallbackStub.h"
#include "../../AI/Nullkiller2/Analyzers/ArmyManager.h"
#include "../../lib/mapObjects/CGDwelling.h"

namespace test
{
TEST(Nullkiller2ArmyPlanningRegression, UnaffordableFirstEntryDoesNotReserveLastFreeSlot)
{
	StrategyCallbackStub callback;
	callback.dayOfWeek = 1;
	NK2AI::ArmyManager manager(&callback, nullptr);
	CCreatureSet army;
	for(int slot = 0; slot < 6; ++slot)
		army.setCreature(SlotID(slot), CreatureID(2 + 2 * slot), 100);
	CGDwelling dwelling(&callback);
	dwelling.ID = Obj::CREATURE_GENERATOR4;
	dwelling.creatures = {{1, {CreatureID(0)}}, {1, {CreatureID(13)}}};
	TResources available;
	available[EGameResID::GOLD] = 1000;
	ASSERT_TRUE(available.canAfford(CreatureID(0).toCreature()->getFullRecruitCost()));
	ASSERT_FALSE(available.canAfford(CreatureID(13).toCreature()->getFullRecruitCost()));

	const auto planned = manager.getArmyAvailableToBuy(&army, &dwelling, available);
	ASSERT_EQ(planned.size(), 1);
	EXPECT_EQ(planned.front().creID, CreatureID(0));
	EXPECT_EQ(planned.front().count, 1);
}

TEST(Nullkiller2ArmyPlanningRegression, FullArmyReplacementUsesAffordableQuantityNotDwellingStock)
{
	StrategyCallbackStub callback;
	callback.dayOfWeek = 1;
	NK2AI::ArmyManager manager(&callback, nullptr);
	CCreatureSet army;
	for(int slot = 0; slot < 7; ++slot)
		army.setCreature(SlotID(slot), CreatureID(2 * slot), 100);
	CGDwelling dwelling(&callback);
	dwelling.ID = Obj::CREATURE_GENERATOR4;
	// A large stock is not an upgrade when only one recruit can be paid for.
	dwelling.creatures = {{1000, {CreatureID(1)}}};
	const auto available = CreatureID(1).toCreature()->getFullRecruitCost();
	EXPECT_TRUE(manager.getArmyAvailableToBuy(&army, &dwelling, available).empty());
}

TEST(Nullkiller2ArmyPlanningRegression, UnaffordableEntryDoesNotChangeAffordablePlan)
{
	StrategyCallbackStub callback;
	callback.dayOfWeek = 1;
	NK2AI::ArmyManager manager(&callback, nullptr);
	CCreatureSet army;
	for(int slot = 0; slot < 6; ++slot)
		army.setCreature(SlotID(slot), CreatureID(2 + 2 * slot), 100);
	CGDwelling dwelling(&callback);
	dwelling.ID = Obj::CREATURE_GENERATOR4;
	dwelling.creatures = {{4, {CreatureID(0)}}};
	TResources available;
	available[EGameResID::GOLD] = 1000;
	const auto baseline = manager.getArmyAvailableToBuy(&army, &dwelling, available);
	dwelling.creatures.push_back({1, {CreatureID(13)}});
	const auto withUnaffordable = manager.getArmyAvailableToBuy(&army, &dwelling, available);
	ASSERT_EQ(baseline.size(), 1);
	ASSERT_EQ(withUnaffordable.size(), baseline.size());
	EXPECT_EQ(withUnaffordable.front().creID, baseline.front().creID);
	EXPECT_EQ(withUnaffordable.front().count, baseline.front().count);
}
}
