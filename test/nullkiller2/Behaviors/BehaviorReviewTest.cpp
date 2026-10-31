/*
 * BehaviorReviewTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "AI/Nullkiller2/AIGateway.h"
#include "AI/Nullkiller2/Behaviors/BuyArmyBehavior.h"
#include "AI/Nullkiller2/Behaviors/CaptureObjectsBehavior.h"
#include "AI/Nullkiller2/Goals/BuyArmy.h"
#include "AI/Nullkiller2/Goals/ExecuteHeroChain.h"

#include "mock/GameHandlerTestClient.h"
#include "nullkiller2/NullkillerTest.h"

#include "lib/CPlayerState.h"
#include "lib/entities/artifact/CArtifactInstance.h"
#include "lib/gameState/UpgradeInfo.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/CGTownInstance.h"
#include "lib/networkPacks/PacksForClient.h"
#include "lib/networkPacks/PacksForServer.h"

namespace
{
const PlayerColor PLAYER(0);
const PlayerColor ENEMY(1);

/// After ten rejected requests allow the real server to proceed. A regression
/// must fail its assertions instead of hanging the complete test process.
class BoundedRejectingClient : public GameHandlerTestClient
{
public:
	using GameHandlerTestClient::GameHandlerTestClient;
	bool rejectUpgrades = false;
	bool rejectRecruitment = false;
	bool rejectArtifacts = false;
	int upgradeRequests = 0;
	int recruitmentRequests = 0;
	int artifactRequests = 0;
	int movementRequests = 0;

	int sendRequest(const CPackForServer & request, PlayerColor player, bool waitTillRealize) override
	{
		if(dynamic_cast<const UpgradeCreature *>(&request))
		{
			++upgradeRequests;
			if(rejectUpgrades && upgradeRequests <= 10)
				return 100000 + upgradeRequests;
		}
		if(dynamic_cast<const RecruitCreatures *>(&request))
		{
			++recruitmentRequests;
			if(rejectRecruitment && recruitmentRequests <= 10)
				return 200000 + recruitmentRequests;
		}
		if(dynamic_cast<const ExchangeArtifacts *>(&request))
		{
			++artifactRequests;
			if(rejectArtifacts && artifactRequests <= 10)
				return 300000 + artifactRequests;
		}
		if(dynamic_cast<const MoveHero *>(&request))
			++movementRequests;
		return GameHandlerTestClient::sendRequest(request, player, waitTillRealize);
	}
};

class ForcedGoldPressure : public NK2AI::BuildAnalyzer
{
public:
	explicit ForcedGoldPressure(NK2AI::Nullkiller * ai) : BuildAnalyzer(ai) {}
	bool isGoldPressureOverMax() const override { return true; }
};

class Nullkiller2_BehaviorReview : public NullkillerTest
{
protected:
	void startEconomyMap(bool secondTown = false)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NK2BehaviorReview")
			.playerActive(PLAYER).playerActive(ENEMY)
			.town({9, 5, 0}, FactionID::CASTLE, PLAYER)
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("orrin")), PLAYER)
			.town({30, 30, 0}, FactionID::CASTLE, ENEMY);
		if(secondTown)
			builder.town({19, 5, 0}, FactionID::CASTLE, PLAYER);
		startWithMap(std::move(builder));
		grantResources(PLAYER, GameResID(GameResID::GOLD), 1000000);
		revealMap(PLAYER);
	}

	CGTownInstance * ourTown() const
	{
		for(auto * town : findAll<CGTownInstance>())
			if(town->getOwner() == PLAYER)
				return town;
		return nullptr;
	}

	CGTownInstance * mutableTown(ObjectInstanceID id) const
	{
		for(auto * town : findAll<CGTownInstance>())
			if(town->id == id)
				return town;
		return nullptr;
	}

	void prepareUpgrades(CGTownInstance & town)
	{
		const CreatureID pikeman(CreatureID::decode("pikeman"));
		const CreatureID halberdier(CreatureID::decode("halberdier"));
		town.addBuilding(BuildingID::DWELL_LVL_1);
		town.addBuilding(BuildingID::DWELL_LVL_1_UP);
		town.creatures.at(0).second = {pikeman, halberdier};
		for(auto & level : town.creatures)
			level.first = 0;
		town.clearSlots();
	}

	void placeVisitor(CGHeroInstance & hero, CGTownInstance & town)
	{
		ChangeObjPos move;
		move.objid = hero.id;
		move.nPos = town.visitablePos();
		move.initiator = PLAYER;
		gameState()->apply(move);
		SetHeroesInTown occupants;
		occupants.tid = town.id;
		occupants.visiting = hero.id;
		occupants.garrison = ObjectInstanceID::NONE;
		gameState()->apply(occupants);
		SetMovePoints exhausted(hero.id, 0);
		gameState()->apply(exhausted);
	}

	NK2AI::AIPath stationaryPath(const CGHeroInstance & hero)
	{
		NK2AI::AIPath path;
		path.targetHero = &hero;
		path.heroArmy = &hero;
		path.exchangeCount = 1;
		path.nodes.push_back(pathNode(hero, hero.visitablePos()));
		return path;
	}
};
}

TEST_F(Nullkiller2_BehaviorReview, rejectedUpgradeStopsAfterOneRequest)
{
	startEconomyMap();
	auto * town = ourTown();
	ASSERT_NE(town, nullptr);
	prepareUpgrades(*town);
	const CreatureID pikeman(CreatureID::decode("pikeman"));
	ASSERT_TRUE(town->setCreature(SlotID(0), pikeman, 100));
	BoundedRejectingClient client(gameState(), PLAYER);
	client.rejectUpgrades = true;
	auto gateway = makeGateway(PLAYER, &client);

	UpgradeInfo offer(town->getStackPtr(SlotID(0))->getId());
	gateway->cc->fillUpgradeInfo(town, SlotID(0), offer);
	ASSERT_TRUE(offer.hasUpgrades()) << "the fixture must offer a real upgrade";
	EXPECT_FALSE(gateway->makePossibleUpgrades(town));
	EXPECT_EQ(client.upgradeRequests, 1);
	EXPECT_EQ(town->getStackPtr(SlotID(0))->getCreatureID(), pikeman);
}

TEST_F(Nullkiller2_BehaviorReview, acceptedUpgradeChangesArmyAndReturnsSuccess)
{
	startEconomyMap();
	auto * town = ourTown();
	ASSERT_NE(town, nullptr);
	prepareUpgrades(*town);
	ASSERT_TRUE(town->setCreature(SlotID(0), CreatureID(CreatureID::decode("pikeman")), 100));
	BoundedRejectingClient client(gameState(), PLAYER);
	auto gateway = makeGateway(PLAYER, &client);

	EXPECT_TRUE(gateway->makePossibleUpgrades(town));
	EXPECT_EQ(client.upgradeRequests, 1);
	ASSERT_NE(town->getStackPtr(SlotID(0)), nullptr);
	EXPECT_EQ(town->getStackPtr(SlotID(0))->getCreatureID(), CreatureID(CreatureID::decode("halberdier")));
	EXPECT_EQ(town->getStackPtr(SlotID(0))->getCount(), 100);
}

TEST_F(Nullkiller2_BehaviorReview, visitingHeroUpgradesWithoutStockOrMovement)
{
	startEconomyMap();
	auto * town = ourTown();
	auto * hero = findHeroByOwner(PLAYER);
	ASSERT_NE(town, nullptr);
	ASSERT_NE(hero, nullptr);
	prepareUpgrades(*town);
	hero->clearSlots();
	ASSERT_TRUE(hero->setCreature(SlotID(0), CreatureID(CreatureID::decode("pikeman")), 100));
	placeVisitor(*hero, *town);
	BoundedRejectingClient client(gameState(), PLAYER);
	auto gateway = makeGateway(PLAYER, &client);
	gateway->nullkiller->heroManager->update();

	const auto goals = NK2AI::Goals::BuyArmyBehavior().decompose(gateway->nullkiller.get());
	ASSERT_EQ(goals.size(), 1u);
	ASSERT_EQ(goals.front()->town, town);
	EXPECT_NO_THROW(goals.front()->asTask()->accept(gateway.get()));
	EXPECT_EQ(hero->getStackPtr(SlotID(0))->getCreatureID(), CreatureID(CreatureID::decode("halberdier")));
	EXPECT_EQ(client.movementRequests, 0);
	EXPECT_EQ(client.recruitmentRequests, 0);
}

TEST_F(Nullkiller2_BehaviorReview, rejectedRecruitmentIsNotSuccessfulProgress)
{
	startEconomyMap();
	auto * town = ourTown();
	ASSERT_NE(town, nullptr);
	prepareUpgrades(*town);
	town->creatures.at(0).first = 10;
	BoundedRejectingClient client(gameState(), PLAYER);
	client.rejectRecruitment = true;
	auto gateway = makeGateway(PLAYER, &client);
	const auto resources = gateway->cc->getResourceAmount();
	NK2AI::Goals::BuyArmy goal(town, 100000);

	EXPECT_THROW(goal.accept(gateway.get()), NK2AI::cannotFulfillGoalException);
	EXPECT_GT(client.recruitmentRequests, 0);
	EXPECT_EQ(town->getUpperArmy()->stacksCount(), 0);
	EXPECT_EQ(gateway->cc->getResourceAmount(), resources);
}

TEST_F(Nullkiller2_BehaviorReview, recruitmentDoesNotSpendReservedResources)
{
	startEconomyMap();
	auto * town = ourTown();
	ASSERT_NE(town, nullptr);
	prepareUpgrades(*town);
	town->creatures.at(0).first = 10;
	BoundedRejectingClient client(gameState(), PLAYER);
	auto gateway = makeGateway(PLAYER, &client);
	const auto resources = gateway->cc->getResourceAmount();
	gateway->nullkiller->lockResources(resources);
	NK2AI::Goals::BuyArmy goal(town, 100000);

	EXPECT_THROW(goal.accept(gateway.get()), NK2AI::cannotFulfillGoalException);
	EXPECT_EQ(client.recruitmentRequests, 0);
	EXPECT_EQ(gateway->cc->getResourceAmount(), resources);
}

TEST_F(Nullkiller2_BehaviorReview, oneTownSavingForIncomeDoesNotSuppressAnotherTown)
{
	startEconomyMap(true);
	auto gateway = makeGateway(PLAYER);
	auto towns = gateway->cc->getTownsInfo();
	ASSERT_EQ(towns.size(), 2u);
	auto * first = mutableTown(towns.front()->id);
	auto * second = mutableTown(towns.back()->id);
	ASSERT_NE(first, nullptr);
	ASSERT_NE(second, nullptr);
	ASSERT_FALSE(first->hasBuilt(BuildingID::CITY_HALL));
	ASSERT_NE(gateway->cc->canBuildStructure(first, BuildingID::CITY_HALL), EBuildingState::FORBIDDEN);
	second->addBuilding(BuildingID::CITY_HALL);
	prepareUpgrades(*second);
	second->creatures.at(0).first = 100;
	gateway->nullkiller->heroManager->update();
	gateway->nullkiller->dangerHitMap->updateHitMap();
	gateway->nullkiller->buildAnalyzer = std::make_unique<ForcedGoldPressure>(gateway->nullkiller.get());

	const auto goals = NK2AI::Goals::BuyArmyBehavior().decompose(gateway->nullkiller.get());
	ASSERT_EQ(goals.size(), 1u);
	EXPECT_EQ(goals.front()->town, second);
}

TEST_F(Nullkiller2_BehaviorReview, threatenedTownCanRecruitWithoutAnyHero)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name("NK2UnmannedDefence")
		.playerActive(PLAYER).playerActive(ENEMY)
		.town({9, 5, 0}, FactionID::CASTLE, PLAYER)
		.hero({5, 7, 0}, HeroTypeID(HeroTypeID::decode("orrin")), ENEMY)
		.town({30, 30, 0}, FactionID::CASTLE, ENEMY);
	startWithMap(std::move(builder));
	auto * town = ourTown();
	ASSERT_NE(town, nullptr);
	prepareUpgrades(*town);
	town->creatures.at(0).first = 100;
	grantResources(PLAYER, GameResID(GameResID::GOLD), 100000);
	revealMap(PLAYER);
	auto gateway = makeGateway(PLAYER);
	ASSERT_TRUE(gateway->cc->getHeroesInfo().empty());
	gateway->nullkiller->dangerHitMap->updateHitMap();
	const auto threat = gateway->nullkiller->dangerHitMap->getObjectThreat(town);
	ASSERT_GT(threat.fastestDanger.danger, 0u);
	ASSERT_LE(threat.fastestDanger.turn, 1);

	const auto goals = NK2AI::Goals::BuyArmyBehavior().decompose(gateway->nullkiller.get());
	ASSERT_EQ(goals.size(), 1u);
	EXPECT_EQ(goals.front()->town, town);
	EXPECT_GE(goals.front()->asTask()->priority, 1000000.0f);
}

TEST_F(Nullkiller2_BehaviorReview, rejectedArtifactPlacementDoesNotLoop)
{
	startEconomyMap();
	auto * hero = findHeroByOwner(PLAYER);
	ASSERT_NE(hero, nullptr);
	const ArtifactID sword(ArtifactID::decode("centaurAxe"));
	auto * artifact = map()->createArtifact(sword);
	ASSERT_NE(artifact, nullptr);
	map()->putArtifactInstance(*hero, artifact->getId(), ArtifactPosition::BACKPACK_START);
	BoundedRejectingClient client(gameState(), PLAYER);
	client.rejectArtifacts = true;
	auto gateway = makeGateway(PLAYER, &client);

	NK2AI::AIGateway::pickBestArtifacts(gateway->cc, hero);
	EXPECT_EQ(client.artifactRequests, 1);
	EXPECT_EQ(hero->getArtPos(artifact), ArtifactPosition::BACKPACK_START);
}

TEST_F(Nullkiller2_BehaviorReview, acceptedArtifactPlacementStillEquipsTheArtifact)
{
	startEconomyMap();
	auto * hero = findHeroByOwner(PLAYER);
	ASSERT_NE(hero, nullptr);
	auto * artifact = map()->createArtifact(ArtifactID(ArtifactID::decode("centaurAxe")));
	ASSERT_NE(artifact, nullptr);
	map()->putArtifactInstance(*hero, artifact->getId(), ArtifactPosition::BACKPACK_START);
	BoundedRejectingClient client(gameState(), PLAYER);
	auto gateway = makeGateway(PLAYER, &client);

	NK2AI::AIGateway::pickBestArtifacts(gateway->cc, hero);
	EXPECT_EQ(client.artifactRequests, 1);
	EXPECT_NE(hero->getArtPos(artifact), ArtifactPosition::BACKPACK_START);
	EXPECT_EQ(map()->getArtifactInstance(artifact->getId()), artifact);
}

TEST_F(Nullkiller2_BehaviorReview, dailyConstructionCapKeepsTownAndUpgradeKnowledge)
{
	startEconomyMap();
	auto * town = ourTown();
	ASSERT_NE(town, nullptr);
	prepareUpgrades(*town);
	auto gateway = makeGateway(PLAYER);
	town->built = gateway->cc->getSettings().getInteger(EGameSettings::TOWNS_BUILDINGS_PER_TURN_CAP);
	gateway->nullkiller->buildAnalyzer->update();

	const auto & development = gateway->nullkiller->buildAnalyzer->getDevelopmentInfo();
	ASSERT_EQ(development.size(), 1u);
	EXPECT_EQ(development.front().town, town);
	EXPECT_FALSE(development.front().built.empty());
	EXPECT_TRUE(gateway->nullkiller->buildAnalyzer->isBuilt(town->getFactionID(), BuildingID::DWELL_LVL_1_UP));
	EXPECT_TRUE(development.front().toBuild.empty()) << "the construction cap still applies";
}

TEST(Nullkiller2_CaptureIdentity, emptyGoalsAreEqual)
{
	EXPECT_TRUE(NK2AI::Goals::CaptureObjectsBehavior() == NK2AI::Goals::CaptureObjectsBehavior());
}

TEST(Nullkiller2_CaptureIdentity, overlappingButDifferentFiltersAreNotEqual)
{
	NK2AI::Goals::CaptureObjectsBehavior first;
	NK2AI::Goals::CaptureObjectsBehavior second;
	first.ofType(Obj::RESOURCE).ofType(Obj::ARTIFACT);
	second.ofType(Obj::RESOURCE).ofType(Obj::MINE);
	EXPECT_FALSE(first == second);
	EXPECT_FALSE(second == first);
}

TEST(Nullkiller2_CaptureIdentity, equivalentFilterOrderDoesNotMatter)
{
	NK2AI::Goals::CaptureObjectsBehavior first;
	NK2AI::Goals::CaptureObjectsBehavior second;
	first.ofType(Obj::RESOURCE).ofType(Obj::ARTIFACT);
	second.ofType(Obj::ARTIFACT).ofType(Obj::RESOURCE);
	EXPECT_TRUE(first == second);
}

TEST_F(Nullkiller2_BehaviorReview, stationaryVisitKeepsFiniteClosestWayRatio)
{
	startEconomyMap();
	const auto * hero = findHeroByOwner(PLAYER);
	ASSERT_NE(hero, nullptr);
	auto gateway = makeGateway(PLAYER);
	gateway->nullkiller->heroManager->update();
	const auto goals = NK2AI::Goals::CaptureObjectsBehavior::getVisitGoals(
		{stationaryPath(*hero)}, gateway->nullkiller.get());
	ASSERT_EQ(goals.size(), 1u);
	const auto * visit = dynamic_cast<const NK2AI::Goals::ExecuteHeroChain *>(goals.front().get());
	ASSERT_NE(visit, nullptr);
	EXPECT_TRUE(std::isfinite(visit->closestWayRatio));
	EXPECT_FLOAT_EQ(visit->closestWayRatio, 1.0f);
}

TEST_F(Nullkiller2_BehaviorReview, invalidCostKeepsRouteIndexWithoutGeneratingATask)
{
	startEconomyMap();
	const auto * hero = findHeroByOwner(PLAYER);
	ASSERT_NE(hero, nullptr);
	auto gateway = makeGateway(PLAYER);
	gateway->nullkiller->heroManager->update();
	auto invalid = stationaryPath(*hero);
	invalid.nodes.front().cost = std::numeric_limits<float>::quiet_NaN();
	const auto goals = NK2AI::Goals::CaptureObjectsBehavior::getVisitGoals(
		{invalid, stationaryPath(*hero)}, gateway->nullkiller.get());
	ASSERT_EQ(goals.size(), 2u);
	EXPECT_TRUE(goals[0]->invalid());
	EXPECT_FALSE(goals[1]->invalid());
}

TEST_F(Nullkiller2_BehaviorReview, unsafeFightRemainsRejected)
{
	startEconomyMap();
	const auto * hero = findHeroByOwner(PLAYER);
	ASSERT_NE(hero, nullptr);
	auto gateway = makeGateway(PLAYER);
	gateway->nullkiller->heroManager->update();
	auto dangerous = stationaryPath(*hero);
	dangerous.targetObjectDanger = std::numeric_limits<uint64_t>::max() / 4;
	const auto goals = NK2AI::Goals::CaptureObjectsBehavior::getVisitGoals({dangerous}, gateway->nullkiller.get());
	ASSERT_EQ(goals.size(), 1u);
	EXPECT_TRUE(goals.front()->invalid());
}

TEST_F(Nullkiller2_BehaviorReview, reachableResourceProducesExecutablePickup)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name("NK2ResourcePickup")
		.playerActive(PLAYER).playerActive(ENEMY)
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("orrin")), PLAYER)
		.resource({7, 5, 0}, GameResID(GameResID::WOOD), 20)
		.town({30, 30, 0}, FactionID::CASTLE, ENEMY);
	startWithMap(std::move(builder));
	revealMap(PLAYER);
	const auto * resource = findObjectAt({7, 5, 0});
	ASSERT_NE(resource, nullptr);
	const auto resourceID = resource->id;
	BoundedRejectingClient client(gameState(), PLAYER);
	auto gateway = makeGateway(PLAYER, &client);
	preparePlanning(*gateway);
	const auto paths = gateway->nullkiller->pathfinder->getPathInfo(resource->visitablePos(), false);
	const auto goals = NK2AI::Goals::CaptureObjectsBehavior::getVisitGoals(paths, gateway->nullkiller.get(), resource);
	const auto visit = std::find_if(goals.begin(), goals.end(), [](const auto & goal)
	{
		return !goal->invalid() && goal->isElementar();
	});
	ASSERT_NE(visit, goals.end());
	const auto woodBefore = gateway->cc->getResourceAmount(GameResID(GameResID::WOOD));
	EXPECT_NO_THROW((*visit)->asTask()->accept(gateway.get()));
	EXPECT_EQ(gateway->cc->tryGetObj(resourceID), nullptr);
	EXPECT_GT(gateway->cc->getResourceAmount(GameResID(GameResID::WOOD)), woodBefore);
}
