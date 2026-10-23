/*
 * MissingTargetRegression.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "../StdInc.h"

#include "../../AI/Nullkiller2/Engine/Nullkiller.h"
#include "../../AI/Nullkiller2/Goals/CaptureObject.h"
#include "../../lib/mapObjects/CGTownInstance.h"

VCMI_LIB_USING_NAMESPACE

namespace NK2AI
{
bool isEquivalentGoals(Goals::TSubgoal, Goals::TSubgoal, CCallback &);
}

namespace test
{
namespace
{
class MissingTargetCallback final : public CCallback
{
public:
	std::map<ObjectInstanceID, const CGObjectInstance *> objects;
	MissingTargetCallback() : CCallback(nullptr, std::nullopt, nullptr) {}
	const CGObjectInstance * getObj(ObjectInstanceID id, bool = true) const override
	{
		auto found = objects.find(id);
		return found == objects.end() ? nullptr : found->second;
	}
};

class PositionedCaptureTarget final : public CGTownInstance
{
public:
	using CGTownInstance::CGTownInstance;
	int3 visitablePos() const override { return int3(0, 0, 0); }
};
}

class Nullkiller2MissingTargetRegression : public ::testing::Test
{
protected:
	std::shared_ptr<MissingTargetCallback> callback = std::make_shared<MissingTargetCallback>();
	NK2AI::Nullkiller ai;
	PositionedCaptureTarget first{callback.get()};
	PositionedCaptureTarget second{callback.get()};

	void SetUp() override
	{
		ai.cc = callback;
		first.id = ObjectInstanceID(1);
		second.id = ObjectInstanceID(2);
		first.ID = second.ID = Obj::TOWN;
		first.subID = second.subID = 0;
		callback->objects = {{first.id, &first}, {second.id, &second}};
	}
};

TEST_F(Nullkiller2MissingTargetRegression, MissingArmyRewardIsZeroWithoutDereferencingTarget)
{
	const NK2AI::RewardEvaluator evaluator(nullptr);
	EXPECT_EQ(evaluator.getArmyReward(nullptr, nullptr, nullptr, false), 0u);
}

TEST_F(Nullkiller2MissingTargetRegression, RemovedCaptureTargetProducesNoTasks)
{
	const NK2AI::Goals::CaptureObject goal(&first);
	callback->objects.erase(first.id);
	EXPECT_TRUE(goal.decompose(&ai).empty());
}

TEST_F(Nullkiller2MissingTargetRegression, MissingFirstCaptureTargetIsNotEquivalentToShipyard)
{
	auto goal1 = NK2AI::Goals::sptr(NK2AI::Goals::CaptureObject(&first));
	auto goal2 = NK2AI::Goals::sptr(NK2AI::Goals::CaptureObject(&second));
	second.ID = Obj::SHIPYARD;
	callback->objects.erase(first.id);
	EXPECT_FALSE(NK2AI::isEquivalentGoals(goal1, goal2, *callback));
}

TEST_F(Nullkiller2MissingTargetRegression, MissingSecondCaptureTargetIsNotEquivalentToShipyard)
{
	auto goal1 = NK2AI::Goals::sptr(NK2AI::Goals::CaptureObject(&first));
	auto goal2 = NK2AI::Goals::sptr(NK2AI::Goals::CaptureObject(&second));
	first.ID = Obj::SHIPYARD;
	callback->objects.erase(second.id);
	EXPECT_FALSE(NK2AI::isEquivalentGoals(goal1, goal2, *callback));
}

TEST_F(Nullkiller2MissingTargetRegression, ExistingShipyardsRemainEquivalent)
{
	auto goal1 = NK2AI::Goals::sptr(NK2AI::Goals::CaptureObject(&first));
	auto goal2 = NK2AI::Goals::sptr(NK2AI::Goals::CaptureObject(&second));
	first.ID = second.ID = Obj::SHIPYARD;
	EXPECT_TRUE(NK2AI::isEquivalentGoals(goal1, goal2, *callback));
}

TEST_F(Nullkiller2MissingTargetRegression, IdenticalGoalRemainsEquivalentAfterTargetDisappears)
{
	auto goal = NK2AI::Goals::sptr(NK2AI::Goals::CaptureObject(&first));
	callback->objects.clear();
	EXPECT_TRUE(NK2AI::isEquivalentGoals(goal, goal, *callback));
}

TEST_F(Nullkiller2MissingTargetRegression, DistinctOrdinaryTargetsRemainDifferent)
{
	auto goal1 = NK2AI::Goals::sptr(NK2AI::Goals::CaptureObject(&first));
	auto goal2 = NK2AI::Goals::sptr(NK2AI::Goals::CaptureObject(&second));
	EXPECT_FALSE(NK2AI::isEquivalentGoals(goal1, goal2, *callback));
}
}
