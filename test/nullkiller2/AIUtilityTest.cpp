/*
 * AIUtilityTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "AI/Nullkiller2/AIGateway.h"
#include "AI/Nullkiller2/AIUtility.h"

#include "mock/GameHandlerTestClient.h"
#include "nullkiller2/NullkillerTest.h"

#include "lib/CPlayerState.h"
#include "lib/gameState/CGameState.h"
#include "lib/gameState/QuestInfo.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/CGResource.h"
#include "lib/mapObjects/Quest.h"
#include "lib/networkPacks/PacksForClient.h"

namespace
{
const PlayerColor PLAYER = PlayerColor(0);
const int3 HERO_POS(5, 5, 0);
const int3 SEER_POS(7, 5, 0);

TinyH3M::TinyH3MBuilder makeQuestlessSeerMap()
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::HOTA);
	builder
		.hotaVersion(3)
		.size(36, false)
		.name("NK2QuestlessSeer")
		.playerActive(PLAYER)
		.randomTown({2, 2, 0}, PLAYER)
		.hero(HERO_POS, HeroTypeID(0), PLAYER)
		.seerHutMulti(SEER_POS, {}, {});

	return builder;
}

TinyH3M::TinyH3MBuilder makeQuestGuardMap()
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::HOTA);
	builder
		.hotaVersion(3)
		.size(36, false)
		.name("NK2KnownQuestGuard")
		.playerActive(PLAYER)
		.randomTown({2, 2, 0}, PLAYER)
		.hero(HERO_POS, HeroTypeID(0), PLAYER)
		.questGuard(SEER_POS, TinyH3M::TinyH3MBuilder::missionLevel(30));

	return builder;
}

class Nullkiller2_AIUtility : public NullkillerTest {};

class Nullkiller2_LocalPlanning : public NullkillerTest
{
protected:
	void startResourceMap()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NK2LocalResourcePlanning")
			.playerActive(PLAYER).playerActive(PlayerColor(1))
			.hero(HERO_POS, HeroTypeID(0), PLAYER)
			.resource({7, 5, 0}, GameResID(GameResID::WOOD), 20)
			.town({30, 30, 0}, FactionID::CASTLE, PlayerColor(1));
		startWithMap(std::move(builder));
		revealMap(PLAYER);
	}

	void placeVisitorOnResource(CGHeroInstance & hero, const CGResource & resource)
	{
		ChangeObjPos move;
		move.objid = hero.id;
		// ChangeObjPos applies the object's visitable offset itself.
		move.nPos = resource.visitablePos();
		move.initiator = PLAYER;
		gameState()->apply(move);
		ASSERT_EQ(hero.visitablePos(), resource.visitablePos());
	}
};
}

TEST_F(Nullkiller2_AIUtility, trackedSeerWithoutActiveQuestIsNotVisitable)
{
	ASSERT_NO_FATAL_FAILURE(startWithMap(makeQuestlessSeerMap()));

	const auto * hero = findHeroAt(HERO_POS);
	const auto * seer = expectAt<SeerHut>(SEER_POS);
	ASSERT_NE(hero, nullptr);
	ASSERT_EQ(seer->getActiveQuest(), nullptr);

	gameState()->players.at(PLAYER).quests.push_back(seer->getQuestIdentity());

	const auto callback = makeCallback(PLAYER);
	auto gateway = std::make_unique<NK2AI::AIGateway>();
	gateway->initGameInterface(std::shared_ptr<Environment>(), callback);

	EXPECT_FALSE(NK2AI::shouldVisit(gateway->nullkiller.get(), hero, seer));
}

// quest objects are never remembered as visited, so a known but unmet guard would be revisited every pass
TEST_F(Nullkiller2_AIUtility, knownUnmetQuestGuardIsNotVisitable)
{
	ASSERT_NO_FATAL_FAILURE(startWithMap(makeQuestGuardMap()));

	const auto * hero = findHeroAt(HERO_POS);
	const auto * guard = expectAt<QuestGuard>(SEER_POS);
	ASSERT_NE(hero, nullptr);

	const auto callback = makeCallback(PLAYER);
	auto gateway = std::make_unique<NK2AI::AIGateway>();
	gateway->initGameInterface(std::shared_ptr<Environment>(), callback);

	EXPECT_TRUE(NK2AI::shouldVisit(gateway->nullkiller.get(), hero, guard));

	gameState()->players.at(PLAYER).quests.push_back(guard->getQuestIdentity());

	EXPECT_FALSE(NK2AI::shouldVisit(gateway->nullkiller.get(), hero, guard));
}

TEST_F(Nullkiller2_LocalPlanning, ownVisitorDoesNotHideKnownResourceGuards)
{
	ASSERT_NO_FATAL_FAILURE(startResourceMap());
	auto * resource = findFirst<CGResource>();
	auto * hero = findHeroByOwner(PLAYER);
	ASSERT_NE(resource, nullptr);
	ASSERT_NE(hero, nullptr);
	ASSERT_TRUE(resource->setCreature(SlotID(0), CreatureID(CreatureID::decode("pikeman")), 5));
	const auto gateway = makeGateway(PLAYER);
	gateway->nullkiller->memory->markObjectVisited(resource);
	const auto expected = resource->estimateCombatValue();
	ASSERT_GT(expected, 0u);
	ASSERT_EQ(gateway->nullkiller->dangerEvaluator->evaluateDanger(resource), expected);
	ASSERT_NO_FATAL_FAILURE(placeVisitorOnResource(*hero, *resource));

	EXPECT_EQ(gateway->nullkiller->dangerEvaluator->evaluateDanger(resource->visitablePos(), hero, false), expected);
}

TEST_F(Nullkiller2_LocalPlanning, unknownResourceGuardsAreNotRevealedByVisitorFiltering)
{
	ASSERT_NO_FATAL_FAILURE(startResourceMap());
	auto * resource = findFirst<CGResource>();
	auto * hero = findHeroByOwner(PLAYER);
	ASSERT_NE(resource, nullptr);
	ASSERT_NE(hero, nullptr);
	ASSERT_TRUE(resource->setCreature(SlotID(0), CreatureID(CreatureID::decode("pikeman")), 5));
	const auto gateway = makeGateway(PLAYER);
	ASSERT_EQ(gateway->nullkiller->dangerEvaluator->evaluateDanger(resource), 0u);
	ASSERT_NO_FATAL_FAILURE(placeVisitorOnResource(*hero, *resource));

	EXPECT_EQ(gateway->nullkiller->dangerEvaluator->evaluateDanger(resource->visitablePos(), hero, false), 0u);
}

TEST_F(Nullkiller2_LocalPlanning, completeTurnChoosesAndCollectsReachableResource)
{
	ASSERT_NO_FATAL_FAILURE(startResourceMap());
	const auto * resource = findFirst<CGResource>();
	const auto * hero = findHeroByOwner(PLAYER);
	ASSERT_NE(resource, nullptr);
	ASSERT_NE(hero, nullptr);
	const auto resourceID = resource->id;
	// TinyMapGameTest initializes day zero, before the first movement refill.
	SetMovePoints movement(hero->id, 2000);
	gameState()->apply(movement);
	GameHandlerTestClient client(gameState(), PLAYER);
	const auto gateway = makeGateway(PLAYER, &client);
	const auto before = gateway->cc->getResourceAmount(GameResID(GameResID::WOOD));

	// Do not feed a pickup goal to the AI: it must discover and choose it.
	gateway->nullkiller->makeTurn();

	EXPECT_EQ(gateway->cc->tryGetObj(resourceID), nullptr);
	EXPECT_GT(gateway->cc->getResourceAmount(GameResID(GameResID::WOOD)), before);
}
