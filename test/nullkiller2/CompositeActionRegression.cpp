/*
 * CompositeActionRegression.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "../StdInc.h"
#include "../../AI/Nullkiller2/Pathfinding/AINodeStorage.h"
#include "../../AI/Nullkiller2/Pathfinding/Actions/SpecialAction.h"

namespace test
{
namespace
{
class GraphActionGate final : public NK2AI::SpecialAction
{
	bool allowed;

public:
	mutable int checks = 0;
	explicit GraphActionGate(bool allowed) : allowed(allowed) {}
	bool canAct(const NK2AI::Nullkiller *, const NK2AI::AIPathNodeInfo &) const override
	{
		++checks;
		return allowed;
	}
	std::string toString() const override { return "Graph action gate"; }
};
}

TEST(Nullkiller2CompositeActionRegression, GraphNodeChecksEveryRequiredAction)
{
	auto allowed = std::make_shared<GraphActionGate>(true);
	auto blocked = std::make_shared<GraphActionGate>(false);
	NK2AI::CompositeAction composite({allowed, blocked});
	const NK2AI::SpecialAction & action = composite;
	NK2AI::AIPathNodeInfo node;
	EXPECT_FALSE(action.canAct(nullptr, node));
	EXPECT_EQ(allowed->checks, 1);
	EXPECT_EQ(blocked->checks, 1);
}

TEST(Nullkiller2CompositeActionRegression, NestedGraphActionCannotHideBlockedPart)
{
	auto blocked = std::make_shared<GraphActionGate>(false);
	auto inner = std::make_shared<NK2AI::CompositeAction>(
		std::vector<std::shared_ptr<const NK2AI::SpecialAction>>{blocked});
	NK2AI::CompositeAction outer({inner});
	const NK2AI::SpecialAction & action = outer;
	NK2AI::AIPathNodeInfo node;
	EXPECT_FALSE(action.canAct(nullptr, node));
	EXPECT_EQ(blocked->checks, 1);
}

TEST(Nullkiller2CompositeActionRegression, AvailableGraphActionStillPasses)
{
	auto allowed = std::make_shared<GraphActionGate>(true);
	NK2AI::CompositeAction composite({allowed});
	const NK2AI::SpecialAction & action = composite;
	NK2AI::AIPathNodeInfo node;
	EXPECT_TRUE(action.canAct(nullptr, node));
	EXPECT_EQ(allowed->checks, 1);
	const NK2AI::CompositeAction empty(std::vector<std::shared_ptr<const NK2AI::SpecialAction>>{});
	EXPECT_TRUE(empty.canAct(nullptr, node));
}
}
