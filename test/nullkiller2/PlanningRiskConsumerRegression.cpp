/* GPL-2.0-or-later; see license.txt. Real consumer protocol, not outcome calibration. */
#include "../StdInc.h"
#include "../../AI/Nullkiller2/Engine/Nullkiller.h"
#include "../../AI/Nullkiller2/Engine/Experimental/PlanningRiskDecision.h"
#include "../../AI/Nullkiller2/Behaviors/CaptureObjectsBehavior.h"
#include "../../AI/Nullkiller2/Goals/ExecuteHeroChain.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/mapObjects/ObjectTemplate.h"
#include "../../lib/mapping/TerrainTile.h"
#include "../../lib/json/JsonNode.h"

VCMI_LIB_USING_NAMESPACE
#include "PlanningRiskConsumerAccess.h"

namespace
{
using namespace NK2AI;
using namespace NK2AI::experimental;
class ConsumerCallback final : public CCallback
{
public:
	TerrainTile tile;
	const CGHeroInstance * hero = nullptr;
	const CGCreature * target = nullptr;
	PlayerState player{this};
	ConsumerCallback() : CCallback(nullptr, PlayerColor(0), nullptr) {}
	const TerrainTile * getTile(int3, bool = true) const override { return &tile; }
	const CGObjectInstance * getObj(ObjectInstanceID id, bool = true) const override
	{
		if(hero && id == hero->id) return hero;
		if(target && id == target->id) return target;
		return nullptr;
	}
	std::vector<const CGObjectInstance *> getGuardingCreatures(int3) const override { return {}; }
	PlayerRelations getPlayerRelations(PlayerColor, PlayerColor) const override { return PlayerRelations::ENEMIES; }
	const PlayerState * getPlayerState(PlayerColor, bool = true) const override { return &player; }
	std::vector<const CGHeroInstance *> getHeroesInfo() const override { return hero ? std::vector<const CGHeroInstance *>{hero} : std::vector<const CGHeroInstance *>{}; }
	std::vector<const CGTownInstance *> getTownsInfo(bool = true) const override { return {}; }
};
class PlanningRiskConsumerRegression : public ::testing::Test
{
protected:
	ConsumerCallback callback;
	CGHeroInstance hero{&callback};
	CGCreature target{&callback};
	Nullkiller ai;
	AIPath path;
	PlanningRiskKey key{1, ObjectInstanceID(1), ObjectInstanceID(2), "published-protocol-state"};
	void SetUp() override
	{
		ai.cc = std::shared_ptr<ConsumerCallback>(&callback, [](auto *) {});
		ai.playerID = PlayerColor(0);
		ai.settings = std::make_unique<Settings>(0);
		ai.heroManager = std::make_unique<HeroManager>(&callback, &ai);
		ai.buildAnalyzer = std::make_unique<BuildAnalyzer>(&ai);
		ai.armyManager = std::make_unique<ArmyManager>(&callback, &ai);
		hero.id = key.hero;
		hero.tempOwner = ai.playerID;
		hero.setHeroType(HeroTypeID(0));
		hero.pushPrimSkill(PrimarySkill::ATTACK, 0);
		hero.pushPrimSkill(PrimarySkill::DEFENSE, 0);
		ASSERT_TRUE(hero.setCreature(SlotID(0), CreatureID(0), 100));
		target.id = key.target;
		target.ID = Obj::MONSTER;
		target.subID = MapObjectSubID(0);
		ASSERT_TRUE(target.setCreature(SlotID(0), CreatureID(0), 10));
		target.tempOwner = PlayerColor::UNFLAGGABLE;
		auto appearance = std::make_shared<ObjectTemplate>();
		JsonNode geometry;
		geometry["mask"].Vector().push_back(JsonNode("A"));
		appearance->readJson(geometry, false);
		target.appearance = appearance;
		target.setAnchorPos(int3(0, 0, 0));
		callback.hero = &hero;
		callback.target = &target;
		callback.tile.visitableObjects.push_back(target.id);
		path.targetHero = &hero;
		path.heroArmy = &hero;
		path.chainMask = 1;
		path.exchangeCount = 1;
		AIPathNodeInfo node{};
		node.coord = int3(0, 0, 0);
		node.layer = EPathfindingLayer::LAND;
		node.targetHero = &hero;
		node.parentIndex = -1;
		node.chainMask = 1;
		path.nodes.push_back(node);
	}
	void provenance(uint64_t danger, uint64_t earlierDanger = 0)
	{
		ChainActor actor;
		actor.hero = &hero;
		AIPathNode destination{};
		destination.actor = &actor;
		destination.arrivalDanger = earlierDanger;
		destination.danger = std::max(danger, earlierDanger);
		path.setRiskProvenance(destination, danger, destination.danger);
	}
	void publish(bool win, double loss, double tolerance = .35, bool magic = false)
	{
		PlanningRiskRecord record;
		record.decisionEvidence = record.allScenariosFinished = true;
		record.contextualLossTolerance = tolerance;
		record.unmodelledOwnMagic = magic;
		SampledBattleOutcome sample;
		sample.status = SampledBattleStatus::FINISHED;
		sample.winner = win ? BattleSide::ATTACKER : BattleSide::DEFENDER;
		sample.initialArmyValue[0] = 1000;
		sample.remainingArmyValue[0] = 1000 * (1 - loss);
		record.variantScenarios = {std::vector<SampledBattleOutcome>(128, sample)};
		PlanningRiskConsumerRegressionAccess::publish(ai, key, std::move(record));
	}
	bool captured(bool force = false)
	{
		const auto goals = Goals::CaptureObjectsBehavior::getVisitGoals({path}, &ai, &target, force);
		return !goals.empty() && !goals.front()->invalid();
	}
};
TEST_F(PlanningRiskConsumerRegression, FavorablePublicationRescuesScalarRejection)
{
	provenance(hero.getArmyStrength() * 2);
	EXPECT_FALSE(captured(true));
	publish(true, .1);
	EXPECT_TRUE(captured(true));
	provenance(hero.getArmyStrength() * 2, 1);
	EXPECT_FALSE(captured(true));
}
TEST_F(PlanningRiskConsumerRegression, DefeatPublicationRejectsNormalCaptureButNotForcedContext)
{
	provenance(1);
	publish(false, 1);
	EXPECT_FALSE(captured());
	EXPECT_TRUE(captured(true));
	publish(false, 1, .35, true);
	EXPECT_TRUE(captured());
}
TEST_F(PlanningRiskConsumerRegression, FreshPolicyDoesNotDependOnEmptyOrRefreshedArmyManager)
{
	CapturedRiskRequest first, second;
	ASSERT_TRUE(captureNeutralLossPolicy(ai, hero, first));
	ai.armyManager->update();
	ASSERT_TRUE(captureNeutralLossPolicy(ai, hero, second));
	EXPECT_EQ(first.contextualLossTolerance, second.contextualLossTolerance);
	EXPECT_EQ(first.key.capturedState, second.key.capturedState);
}
}
