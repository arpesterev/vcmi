/*
 * BattleAINumericRegression.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "../mock/BattleFake.h"
#include "AI/BattleAI/BattleAI.h"
#include "lib/CStack.h"
#include "lib/CCreatureHandler.h"
#include "lib/battle/BattleAction.h"
#include "lib/callback/CBattleCallback.h"
#include <limits>

namespace test
{
using namespace ::testing;

class BattleAINumericStackMock : public CStack
{
public:
	MOCK_CONST_METHOD0(creatureId, CreatureID());
	MOCK_CONST_METHOD0(creatureIndex, int32_t());
	MOCK_CONST_METHOD0(unitSide, BattleSide());
	MOCK_CONST_METHOD0(getCount, int32_t());
	MOCK_CONST_METHOD0(alive, bool());
	MOCK_CONST_METHOD0(isGhost, bool());
};

class BattleAINumericRegression : public ::testing::Test
{
protected:
	std::shared_ptr<NiceMock<battle::BattleFake>> battleInfo;
	std::vector<std::unique_ptr<NiceMock<BattleAINumericStackMock>>> stacks;

	void SetUp() override
	{
#if SCRIPTING_ENABLED
		battleInfo = std::make_shared<NiceMock<battle::BattleFake>>(std::shared_ptr<scripting::PoolMock>());
#else
		battleInfo = std::make_shared<NiceMock<battle::BattleFake>>();
#endif
		ON_CALL(*battleInfo, getStacksIf(_)).WillByDefault(Invoke(
			[this](const TStackFilter & filter)
			{
				TStacks result;
				for(const auto & stack : stacks)
					if(filter(stack.get()))
						result.push_back(stack.get());
				return result;
			}));
		ON_CALL(*battleInfo, getSidePlayer(_)).WillByDefault(Invoke(
			[](BattleSide side) { return PlayerColor(side == BattleSide::ATTACKER ? 0 : 1); }));
		ON_CALL(*battleInfo, getSideHero(_)).WillByDefault(Return(nullptr));
		ON_CALL(*battleInfo, getBattleID()).WillByDefault(Return(BattleID(1)));
	}

	void addStack(BattleSide side, int32_t count, bool living = true)
	{
		auto stack = std::make_unique<NiceMock<BattleAINumericStackMock>>();
		ON_CALL(*stack, creatureId()).WillByDefault(Return(CreatureID(0)));
		ON_CALL(*stack, creatureIndex()).WillByDefault(Return(0));
		ON_CALL(*stack, unitSide()).WillByDefault(Return(side));
		ON_CALL(*stack, getCount()).WillByDefault(Return(count));
		ON_CALL(*stack, alive()).WillByDefault(Return(living));
		ON_CALL(*stack, isGhost()).WillByDefault(Return(false));
		stacks.push_back(std::move(stack));
	}

	std::optional<BattleAction> requestRetreatDecision()
	{
		auto callback = std::make_shared<CBattleCallback>(PlayerColor(0), nullptr);
		callback->waitTillRealize = true;
		callback->onBattleStarted(battleInfo.get());
		CBattleAI ai;
		ai.initBattleInterface(nullptr, callback);
		ai.battleStart(BattleID(1), nullptr, nullptr, int3(0), nullptr, nullptr, BattleSide::ATTACKER, false);
		return ai.considerFleeingOrSurrendering(BattleID(1));
	}
};

TEST_F(BattleAINumericRegression, WidensEachStackStrengthBeforeMultiplication)
{
	ASSERT_GT(CreatureID(0).toCreature()->getAIValue(), 1);
	constexpr int32_t count = std::numeric_limits<int32_t>::max();
	addStack(BattleSide::ATTACKER, count);
	addStack(BattleSide::DEFENDER, count / 2);
	const float expected = static_cast<float>(static_cast<double>(count) / (count / 2));
	EXPECT_FLOAT_EQ(CBattleAI::getStrengthRatio(battleInfo, BattleSide::ATTACKER), expected);
}

TEST_F(BattleAINumericRegression, AccumulatesLargeArmyWithoutIntOverflow)
{
	const int32_t value = CreatureID(0).toCreature()->getAIValue();
	ASSERT_GT(value, 0);
	// Each individual product fits int32_t, but two friendly products do not.
	const int32_t count = static_cast<int32_t>((static_cast<int64_t>(std::numeric_limits<int32_t>::max()) * 3 / 4) / value);
	ASSERT_GT(count, 0);
	addStack(BattleSide::ATTACKER, count);
	addStack(BattleSide::ATTACKER, count);
	addStack(BattleSide::DEFENDER, count);
	EXPECT_FLOAT_EQ(CBattleAI::getStrengthRatio(battleInfo, BattleSide::ATTACKER), 2.0f);
}

TEST_F(BattleAINumericRegression, PreservesZeroEnemyStrengthFallback)
{
	addStack(BattleSide::ATTACKER, 10);
	EXPECT_FLOAT_EQ(CBattleAI::getStrengthRatio(battleInfo, BattleSide::ATTACKER), 1.0f);
}

TEST_F(BattleAINumericRegression, ZeroOwnStrengthRemainsZeroAgainstEnemy)
{
	addStack(BattleSide::DEFENDER, 10);
	EXPECT_FLOAT_EQ(CBattleAI::getStrengthRatio(battleInfo, BattleSide::ATTACKER), 0.0f);
}

TEST_F(BattleAINumericRegression, EmptyBattleHasNoRetreatDecision)
{
	EXPECT_FALSE(requestRetreatDecision().has_value());
}

TEST_F(BattleAINumericRegression, DeadLastOwnStackHasNoRetreatDecision)
{
	addStack(BattleSide::ATTACKER, 0, false);
	EXPECT_FALSE(requestRetreatDecision().has_value());
}

TEST(BattleAICallbackLifecycleRegression, RestoresWaitingAdventureCallbackAfterBattleAI)
{
	auto callback = std::make_shared<CBattleCallback>(PlayerColor(0), nullptr);
	callback->waitTillRealize = true;
	{
		CBattleAI ai;
		ai.initBattleInterface(nullptr, callback);
		EXPECT_FALSE(callback->waitTillRealize);
	}
	EXPECT_TRUE(callback->waitTillRealize);
}

TEST(BattleAICallbackLifecycleRegression, PreservesAlreadyAsynchronousCallbackAfterBattleAI)
{
	auto callback = std::make_shared<CBattleCallback>(PlayerColor(0), nullptr);
	callback->waitTillRealize = false;
	{
		CBattleAI ai;
		ai.initBattleInterface(nullptr, callback);
		EXPECT_FALSE(callback->waitTillRealize);
	}
	EXPECT_FALSE(callback->waitTillRealize);
}

}
