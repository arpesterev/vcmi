/*
 * AIPathMetadataRegression.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "../StdInc.h"
#include "../../AI/Nullkiller2/Pathfinding/AINodeStorage.h"

VCMI_LIB_USING_NAMESPACE

namespace test
{
TEST(Nullkiller2AIPathMetadataRegression, EmptyPathHasDeterministicMetadata)
{
	NK2AI::AIPath path;
	EXPECT_TRUE(path.nodes.empty());
	EXPECT_EQ(path.targetHero, nullptr);
	EXPECT_EQ(path.heroArmy, nullptr);
	EXPECT_EQ(path.chainMask, 0);
	EXPECT_EQ(path.exchangeCount, 0);
	EXPECT_EQ(path.getTotalDanger(), 0);
	EXPECT_EQ(path.getTotalArmyLoss(), 0);
	EXPECT_EQ(path.movementCost(), 0);
}

TEST(Nullkiller2AIPathMetadataRegression, DefaultNodeCannotPretendToBeAValidChain)
{
	NK2AI::AIPathNodeInfo node;
	EXPECT_EQ(node.targetHero, nullptr);
	EXPECT_EQ(node.chainMask, 0);
	EXPECT_EQ(node.parentIndex, -1);
	EXPECT_EQ(node.layer, EPathfindingLayer::WRONG);
	EXPECT_FALSE(node.coord.isValid());
	EXPECT_FALSE(node.specialAction);
	EXPECT_FALSE(node.actionIsBlocked);
}

TEST(Nullkiller2AIPathMetadataRegression, SynthesizedGraphNodePreservesCarrierAndFullChainMask)
{
	CGHeroInstance hero(nullptr);
	NK2AI::AIPath path;
	path.targetHero = &hero;
	path.chainMask = (uint64_t(1) << 40) | 3;
	for(const auto layer : {EPathfindingLayer::LAND, EPathfindingLayer::SAIL})
	{
		const auto node = path.createNodeInfo(int3(4, 5, 0), layer);
		EXPECT_EQ(node.targetHero, &hero);
		EXPECT_EQ(node.chainMask, path.chainMask);
		EXPECT_EQ(node.coord, int3(4, 5, 0));
		EXPECT_EQ(node.layer, layer);
		EXPECT_EQ(node.parentIndex, -1);
		EXPECT_FALSE(node.specialAction);
		EXPECT_FALSE(node.actionIsBlocked);
	}
}
}
