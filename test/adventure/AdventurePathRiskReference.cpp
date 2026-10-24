/* GPL-2.0-or-later. Injected-evidence protocol test, NOT battle calibration. */
#include "AdventurePathRiskBridge.h"
#include "../nullkiller2/PlanningRiskConsumerAccess.h"
#include "../../AI/Nullkiller2/Engine/Experimental/PlanningRiskDecision.h"
#include "../../AI/Nullkiller2/Behaviors/CaptureObjectsBehavior.h"
#include "../../AI/Nullkiller2/Goals/ExecuteHeroChain.h"
#include <cmath>

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
		PlanningRiskRoute cleanRoute;
		cleanRoute.knownEarlierDanger = true;
		const auto targetDecision = decideNeutralRisk(ai, *hero, hero, monster->visitablePos(), cleanRoute, false);
		std::cout << "PATH_RISK_DIAGNOSTIC probe=monster epoch=" << key.epoch
			<< " hero=" << hero->id.getNum() << " target=" << monster->id.getNum()
			<< " hero_start=" << hero->visitablePos().toString() << " target_pos=" << monster->visitablePos().toString()
			<< " current_army=1 earlier_danger=0 earlier_loss=0 spent_mana=0 recommendation="
			<< static_cast<int>(targetDecision.recommendation) << " sampled_loss=" << targetDecision.sampledLoss << std::endl;
		ASSERT_EQ(targetDecision.recommendation, RiskRecommendation::FAVORABLE)
			<< "Publication must be valid at exact monster tile before interpreting a path rejection";
		const auto halo = monster->visitablePos() + int3(-1, 0, 0);
		ASSERT_TRUE(bridge.callback->isVisible(halo));
		const auto haloObjects = bridge.callback->getVisitableObjs(halo, false);
		const auto haloGuards = bridge.callback->getGuardingCreatures(halo);
		std::cout << "PATH_RISK_DIAGNOSTIC probe=halo pos=" << halo.toString() << " objects=" << haloObjects.size()
			<< " guards=" << haloGuards.size();
		for(const auto * guard : haloGuards) std::cout << " guard_id=" << guard->id.getNum();
		const auto haloDecision = decideNeutralRisk(ai, *hero, hero, halo, cleanRoute, false);
		std::cout << " recommendation=" << static_cast<int>(haloDecision.recommendation) << std::endl;
		ASSERT_TRUE(haloObjects.empty());
		ASSERT_EQ(haloGuards.size(), 1u);
		ASSERT_EQ(haloGuards.front(), monster);
		ASSERT_EQ(haloDecision.recommendation, RiskRecommendation::FAVORABLE)
			<< "Qualified unique visible guard approach must resolve the same encounter";
		const auto favorable = rebuild();
		ASSERT_FALSE(favorable.empty()) << "Eligible publication must reach actual pathfinder before scalar pruning";
		for(const auto & path : favorable)
		{
			EXPECT_EQ(path.targetHero, hero);
			EXPECT_EQ(path.heroArmy, hero);
			ASSERT_TRUE(path.hasSingleForecastEncounter);
			EXPECT_EQ(path.singleForecastEncounter, monster->id);
			EXPECT_LE(path.armyLoss, static_cast<uint64_t>(std::ceil(.1 * hero->getArmyStrength())));
		}
		const auto visitGoals = Goals::CaptureObjectsBehavior::getVisitGoals(favorable, &ai, monster, true);
		ASSERT_FALSE(visitGoals.empty());
		for(const auto & goal : visitGoals)
		{
			const auto * chain = dynamic_cast<const Goals::ExecuteHeroChain *>(goal.get());
			ASSERT_NE(chain, nullptr) << "Require executable production capture goal, not helper arithmetic";
			EXPECT_EQ(chain->getPath().getTotalArmyLoss(),
				static_cast<uint64_t>(std::ceil(.1 * hero->getArmyStrength())))
				<< "Same guard encounter may be charged exactly once across halo and monster tile";
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

TEST_F(AdventurePathRiskReference, ZeroRoundedLossEarlierCombatStillRejectsDestinationOnlyPublication)
{
	ASSERT_NO_FATAL_FAILURE(initialize("risk_zero_loss_earlier_guard"));
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
	ASSERT_EQ(earlier->CArmedInstance::getCreature(SlotID(0))->getJsonKey(), "core:peasant");
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
		{
			ASSERT_GT(path.getTotalDanger(), 0u) << "No combat-free bypass may satisfy this control";
			ASSERT_EQ(path.getTotalArmyLoss(), 0u) << "Must exercise real rounded-zero loss, not assume from unit count";
		}
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
