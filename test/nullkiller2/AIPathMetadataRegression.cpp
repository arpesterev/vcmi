/*
 * AIPathMetadataRegression.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "../StdInc.h"
#include "../../AI/Nullkiller2/Pathfinding/AINodeStorage.h"
#include "../../AI/Nullkiller2/Pathfinding/Actors.h"

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
	EXPECT_EQ(path.destinationObjectDanger, 0);
	EXPECT_EQ(path.destinationHeroManaCost, 0);
	EXPECT_FALSE(path.hasDestinationRiskProvenance);
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
TEST(Nullkiller2AIPathMetadataRegression, ProducerSeparatesEarlierGuardFromDestination)
{
	CGHeroInstance hero(nullptr);
	NK2AI::ChainActor carrier;
	carrier.hero = &hero;
	NK2AI::AIPathNode source, destination;
	source.armyLoss = 10;
	source.manaCost = 7;
	source.danger = 80;
	destination.actor = &carrier;
	destination.inheritCosts(source); // Same operation as AINodeStorage::commit.
	destination.armyLoss += 5; // Destination battle, after movement commit.
	NK2AI::AIPath path;
	path.targetHero = &hero;
	path.setRiskProvenance(destination, 20, 80);
	EXPECT_TRUE(path.hasDestinationRiskProvenance);
	EXPECT_EQ(path.destinationObjectDanger, 20);
	EXPECT_EQ(path.targetObjectDanger, 80);
	EXPECT_EQ(path.armyLoss, 10);
	EXPECT_EQ(path.destinationHeroManaCost, 7);
}

TEST(Nullkiller2AIPathMetadataRegression, ProducerDoesNotUseCompressedPredecessorLoss)
{
	NK2AI::AIPathNode earlier, destination;
	earlier.armyLoss = 0;
	destination.theNodeBefore = &earlier;
	// Two distinct equal-strength battles: first loss is retained, final excluded.
	NK2AI::AIPathNode source;
	source.armyLoss = 12;
	source.manaCost = 0;
	source.danger = 40;
	destination.inheritCosts(source);
	destination.armyLoss += 12;
	NK2AI::AIPath path;
	path.setRiskProvenance(destination, 40, 40);
	EXPECT_EQ(path.armyLoss, 12);
	EXPECT_EQ(path.destinationObjectDanger, 40);
	// Repeated route nodes for the same guard never multiply its recorded loss.
	path.nodes.resize(3);
	for(auto & node : path.nodes)
		node.danger = 40;
	path.setRiskProvenance(destination, 40, 40);
	EXPECT_EQ(path.armyLoss, 12);
}

TEST(Nullkiller2AIPathMetadataRegression, ProducerIgnoresOtherChainHeroMana)
{
	CGHeroInstance carrierHero(nullptr), donorHero(nullptr);
	NK2AI::ChainActor carrier, donor;
	carrier.hero = &carrierHero;
	donor.hero = &donorHero;
	NK2AI::AIPathNode destination, donorNode;
	destination.actor = &carrier;
	destination.manaCost = 5;
	destination.armyLoss = 0;
	donorNode.actor = &donor;
	donorNode.manaCost = 90;
	destination.chainOther = &donorNode;
	NK2AI::AIPath path;
	path.targetHero = &carrierHero;
	path.setRiskProvenance(destination, 0, 0);
	EXPECT_EQ(path.destinationHeroManaCost, 5);
	path.setRiskProvenance(donorNode, 1, 1);
	EXPECT_EQ(path.destinationHeroManaCost, 0);
}

TEST(Nullkiller2AIPathMetadataRegression, RemainingArmySaturatesInsteadOfWrapping)
{
	EXPECT_EQ(NK2AI::AIPath::remainingArmy(100, 10), 90);
	EXPECT_EQ(NK2AI::AIPath::remainingArmy(100, 100), 0);
	EXPECT_EQ(NK2AI::AIPath::remainingArmy(100, std::numeric_limits<uint64_t>::max()), 0);
}

}
