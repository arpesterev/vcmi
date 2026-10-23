/*
 * PlanningCacheRegression.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "../StdInc.h"
#include "../../lib/callback/CCallback.h"

#include "../../AI/Nullkiller2/AIGateway.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/networkPacks/ArtifactLocation.h"
#include "../../lib/networkPacks/PacksForClient.h"

VCMI_LIB_USING_NAMESPACE

namespace test
{

class PlanningCacheCallback final : public CCallback
{
public:
	const CGObjectInstance * changedObject = nullptr;

	PlanningCacheCallback() : CCallback(nullptr, std::nullopt, nullptr) {}
	int3 getMapSize() const override { return int3(2, 1, 1); }
	std::vector<const CGTownInstance *> getTownsInfo(bool = true) const override { return {}; }
	const CGObjectInstance * getObj(ObjectInstanceID, bool = true) const override { return changedObject; }
	PlayerRelations getPlayerRelations(PlayerColor, PlayerColor) const override
	{
		return PlayerRelations::SAME_PLAYER;
	}
};

class Nullkiller2PlanningCacheRegression : public ::testing::Test
{
protected:
	std::shared_ptr<PlanningCacheCallback> callback = std::make_shared<PlanningCacheCallback>();
	NK2AI::AIGateway gateway;
	CGHeroInstance hero{callback.get()};
	CGTownInstance town{callback.get()};

	void SetUp() override
	{
		gateway.cc = callback;
		gateway.playerID = PlayerColor(0);
		gateway.nullkiller->cc = callback;
		gateway.nullkiller->playerID = PlayerColor(0);
		gateway.nullkiller->dangerHitMap =
			std::make_unique<NK2AI::DangerHitMapAnalyzer>(gateway.nullkiller.get());
	}

	void expectInvalidation(const std::function<void()> & event)
	{
		auto & map = *gateway.nullkiller->dangerHitMap;
		// Real update with empty memory needs no pathfinder or CGameState.
		map.updateHitMap();
		ASSERT_TRUE(map.isHitMapUpToDate());
		event();
		// Nullkiller::updateState checks this flag before reusing HeroManager,
		// actor paths and object clusters, so a dirty hitmap forces regeneration.
		EXPECT_FALSE(map.isHitMapUpToDate());
	}
};

TEST_F(Nullkiller2PlanningCacheRegression, ArmyAndGarrisonChangesInvalidateCachedPlanning)
{
	expectInvalidation([&] { gateway.garrisonsChanged(ObjectInstanceID(1), ObjectInstanceID(2)); });
	expectInvalidation([&] { gateway.heroInGarrisonChange(&town); });
}

TEST_F(Nullkiller2PlanningCacheRegression, HeroStatsAndMovementInvalidateCachedPlanning)
{
	expectInvalidation([&] { gateway.heroPrimarySkillChanged(&hero, PrimarySkill::ATTACK, 1); });
	expectInvalidation([&] { gateway.heroSecondarySkillChanged(&hero, 0, 1); });
	expectInvalidation([&] { gateway.heroManaPointsChanged(&hero); });
	expectInvalidation([&] { gateway.heroMovePointsChanged(&hero); });
}

TEST_F(Nullkiller2PlanningCacheRegression, HeroAndPlayerBonusesInvalidateCachedPlanning)
{
	const Bonus bonus;
	expectInvalidation([&] { gateway.heroBonusChanged(&hero, bonus, true); });
	expectInvalidation([&] { gateway.playerBonusChanged(bonus, false); });
}

TEST_F(Nullkiller2PlanningCacheRegression, ArtifactChangesInvalidateCachedPlanning)
{
	const ArtifactLocation location(ObjectInstanceID(1));
	expectInvalidation([&] { gateway.artifactMoved(location, location); });
	expectInvalidation([&] { gateway.artifactPut(location); });
	expectInvalidation([&] { gateway.artifactRemoved(location); });
	expectInvalidation([&] { gateway.artifactAssembled(location); });
	expectInvalidation([&] { gateway.artifactDisassembled(location); });
}

TEST_F(Nullkiller2PlanningCacheRegression, BuildingsInvalidateCachedPlanning)
{
	expectInvalidation([&] { gateway.buildChanged(&town, BuildingID::FORT, 1); });
}

TEST_F(Nullkiller2PlanningCacheRegression, ResourcesAndAvailableCreaturesInvalidateCachedPlanning)
{
	expectInvalidation([&] { gateway.receivedResource(); });
	expectInvalidation([&] { gateway.availableCreaturesChanged(&town); });
}

TEST_F(Nullkiller2PlanningCacheRegression, OwnershipChangesInvalidateCachedPlanning)
{
	SetObjectProperty change;
	change.what = ObjProperty::OWNER;
	change.identifier = PlayerColor(0);
	callback->changedObject = &hero;
	expectInvalidation([&] { gateway.objectPropertyChanged(&change); });

	// A missing object still changes the assumptions of cached planning.
	callback->changedObject = nullptr;
	expectInvalidation([&] { gateway.objectPropertyChanged(&change); });
}

TEST_F(Nullkiller2PlanningCacheRegression, NotificationsBeforeAnalyzerInitializationAreSafe)
{
	gateway.nullkiller->dangerHitMap.reset();
	EXPECT_NO_THROW(gateway.garrisonsChanged(ObjectInstanceID(1), ObjectInstanceID(2)));
	EXPECT_NO_THROW(gateway.heroManaPointsChanged(&hero));
}

TEST_F(Nullkiller2PlanningCacheRegression, ThreatResetPreservesIndependentTerritoryCache)
{
	NK2AI::HitMapNode node;
	node.closestTown = &town;
	node.maximumDanger.danger = 1000;
	node.maximumDanger.turn = 1;
	node.fastestDanger.danger = 500;
	node.fastestDanger.turn = 0;

	// This is the production reset used by updateHitMap for every map tile.
	node.reset();
	EXPECT_EQ(node.maximumDanger.danger, 0u);
	EXPECT_EQ(node.fastestDanger.danger, 0u);
	EXPECT_EQ(node.maximumDanger.turn, NK2AI::PathfinderSettings::MaxTurnDistanceLimit);
	EXPECT_EQ(node.fastestDanger.turn, NK2AI::PathfinderSettings::MaxTurnDistanceLimit);
	EXPECT_EQ(node.closestTown, &town);
}

}
