/* GPL-2.0-or-later. Injected-evidence protocol test, NOT battle calibration. */
#include "AdventurePathRiskBridge.h"
#include "../nullkiller2/PlanningRiskConsumerAccess.h"

namespace
{
using namespace NK2AI;
using namespace NK2AI::experimental;
class AdventurePathRiskReference : public AdventureScenarioBridge
{
protected:
	static PlanningRiskRecord evidence(bool favorable)
	{
		PlanningRiskRecord record;
		record.decisionEvidence = record.allScenariosFinished = true;
		record.contextualLossTolerance = .35;
		SampledBattleOutcome sample;
		sample.status = SampledBattleStatus::FINISHED;
		sample.winner = favorable ? BattleSide::ATTACKER : BattleSide::DEFENDER;
		sample.initialArmyValue[0] = 1000;
		sample.remainingArmyValue[0] = favorable ? 900 : 0;
		record.variantScenarios = {std::vector<SampledBattleOutcome>(128, sample)};
		return record;
	}
};

TEST_F(AdventurePathRiskReference, ActualPathfinderRequiresEligibleFavorablePublication)
{
	ASSERT_NO_FATAL_FAILURE(initialize("blocked_goal_alternative"));
	ASSERT_NO_FATAL_FAILURE(initializePlanner());
	const auto * hero = dynamic_cast<const CGHeroInstance *>(object("hero_0"));
	const auto * monster = dynamic_cast<const CGCreature *>(object("guarded_reward"));
	ASSERT_NE(hero, nullptr);
	ASSERT_NE(monster, nullptr);
	ASSERT_EQ(hero->getOwner(), PlayerColor(0));
	const auto ownedHeroes = bridge.callback->getHeroesInfo();
	ASSERT_FALSE(ownedHeroes.empty());
	ASSERT_NE(std::find(ownedHeroes.begin(), ownedHeroes.end(), hero), ownedHeroes.end());
	ASSERT_EQ(hero->getStackCount(SlotID(0)), 20);
	ASSERT_EQ(monster->getStackCount(SlotID(0)), 999);
	ASSERT_TRUE(bridge.callback->isVisible(monster));
	ASSERT_EQ(bridge.callback->getVisitableObjs(monster->visitablePos(), false).size(), 1u);
	for(const auto * guard : bridge.callback->getGuardingCreatures(monster->visitablePos()))
		ASSERT_EQ(guard, monster) << "Eligible rescue must not contain another guarding creature";
	ASSERT_GT(gateway->nullkiller->dangerEvaluator->evaluateDanger(monster), hero->getArmyStrength());
	const int requestsBefore = bridge.requests;
	ASSERT_NO_FATAL_FAILURE(PlanningRiskSchedulerRegressionAccess::plannerTurn(*gateway, false, [&](Nullkiller & ai)
	{
		PathfinderSettings config;
		config.useHeroChain = false;
		config.mainTurnDistanceLimit = config.scoutTurnDistanceLimit = 1;
		const auto rebuild = [&]()
		{
			// Production storage clear/setHeroes, engine calculatePaths and
			// AIMovementAfterDestinationRule::bypassBattle. No fabricated AIPath.
			ai.pathfinder->updatePaths({{hero, HeroRole::MAIN}}, config);
			return ai.pathfinder->getPathInfo(monster->visitablePos());
		};
		ASSERT_TRUE(rebuild().empty()) << "Fixture must genuinely hit scalar pre-pruning";
		PlanningRiskKey key{17, hero->id, monster->id, "injected-path-protocol-only"};
		PlanningRiskConsumerRegressionAccess::publish(ai, key, evidence(false));
		ASSERT_TRUE(rebuild().empty()) << "Unsafe evidence cannot rescue a scalar-pruned encounter";
		PlanningRiskConsumerRegressionAccess::publish(ai, key, evidence(true));
		const auto favorable = rebuild();
		ASSERT_FALSE(favorable.empty()) << "Eligible publication must reach actual pathfinder before scalar pruning";
		for(const auto & path : favorable)
		{
			EXPECT_EQ(path.targetHero, hero);
			EXPECT_EQ(path.heroArmy, hero);
		}
		gateway->invalidatePaths(); // production epoch invalidation, not a forged stale record
		ASSERT_TRUE(rebuild().empty()) << "Invalidated publication must not rescue stale route";
	}));
	EXPECT_EQ(bridge.requests, requestsBefore) << "Path qualification must not execute a fight or move";
	EXPECT_EQ(monster->getStackCount(SlotID(0)), 999);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 20);
}

TEST_F(AdventurePathRiskReference, ActualEarlierCombatCannotUseDestinationOnlyPublication)
{
	ASSERT_NO_FATAL_FAILURE(initialize("risk_earlier_guard"));
	ASSERT_NO_FATAL_FAILURE(initializePlanner());
	const auto * hero = dynamic_cast<const CGHeroInstance *>(object("hero_0"));
	const auto * monster = dynamic_cast<const CGCreature *>(object("guarded_reward"));
	const auto * earlier = dynamic_cast<const CGCreature *>(object("earlier_guard"));
	ASSERT_NE(hero, nullptr); ASSERT_NE(monster, nullptr); ASSERT_NE(earlier, nullptr);
	ASSERT_EQ(hero->getOwner(), PlayerColor(0));
	const auto ownedHeroes = bridge.callback->getHeroesInfo();
	ASSERT_FALSE(ownedHeroes.empty());
	ASSERT_NE(std::find(ownedHeroes.begin(), ownedHeroes.end(), hero), ownedHeroes.end());
	ASSERT_TRUE(bridge.callback->isVisible(monster));
	ASSERT_TRUE(bridge.callback->isVisible(earlier));
	ASSERT_EQ(earlier->getStackCount(SlotID(0)), 1);
	ASSERT_EQ(monster->getStackCount(SlotID(0)), 999);
	ASSERT_EQ(bridge.callback->getVisitableObjs(monster->visitablePos(), false).size(), 1u);
	for(const auto * guard : bridge.callback->getGuardingCreatures(monster->visitablePos()))
		ASSERT_EQ(guard, monster) << "Control must isolate earlier combat, not overlapping destination guards";
	const int requestsBefore = bridge.requests;
	ASSERT_NO_FATAL_FAILURE(PlanningRiskSchedulerRegressionAccess::plannerTurn(*gateway, false, [&](Nullkiller & ai)
	{
		PathfinderSettings config;
		config.useHeroChain = false;
		config.mainTurnDistanceLimit = config.scoutTurnDistanceLimit = 1;
		const auto rebuild = [&]() { ai.pathfinder->updatePaths({{hero, HeroRole::MAIN}}, config); };
		rebuild();
		const auto intermediate = ai.pathfinder->getPathInfo(int3(8, 5, 0));
		ASSERT_FALSE(intermediate.empty()) << "Real path must traverse the weak guard before strong destination";
		for(const auto & path : intermediate)
			ASSERT_GT(path.getTotalDanger(), 0u) << "No combat-free bypass may satisfy this control";
		ASSERT_TRUE(ai.pathfinder->getPathInfo(monster->visitablePos()).empty());
		PlanningRiskConsumerRegressionAccess::publish(ai,
			PlanningRiskKey{17, hero->id, monster->id, "injected-destination-only"}, evidence(true));
		rebuild();
		ASSERT_TRUE(ai.pathfinder->getPathInfo(monster->visitablePos()).empty())
			<< "Destination-only evidence must not rescue a route with actual earlier combat";
	}));
	EXPECT_EQ(bridge.requests, requestsBefore);
	EXPECT_EQ(earlier->getStackCount(SlotID(0)), 1);
	EXPECT_EQ(monster->getStackCount(SlotID(0)), 999);
}
}
