/* Staged asset-free interaction draft; no production or frozen-target edits. */
#include "AdventureInteractionBridge.h"
#include "../../lib/mapObjects/MiscObjects.h"

namespace
{
class AdventureInteractionScenarios : public AdventureScenarioBridge
{
protected:
	void finishActiveTurn()
	{
		CCallback actual(bridge.handler->gs, bridge.active, &bridge);
		actual.endTurn();
	}

	void observeThreats()
	{
		// Real owned storage/state locks, no forged danger table or state edits.
		NK2AI::PlanningRiskSchedulerRegressionAccess::plannerTurn(*gateway, false);
	}

	void diagnoseThreat(const char * phase, const CGHeroInstance * enemy, const CGTownInstance * town)
	{
		// Diagnostic only. Same real callback/fog, no revealing or executing actions.
		NK2AI::PlanningRiskSchedulerRegressionAccess::plannerTurn(*gateway, false, [&](NK2AI::Nullkiller & ai)
		{
			NK2AI::PathfinderSettings config;
			config.useHeroChain = false;
			config.mainTurnDistanceLimit = config.scoutTurnDistanceLimit = ai.settings->getThreatTurnDistanceLimit();
			ai.pathfinder->updatePaths({{enemy, NK2AI::HeroRole::MAIN}}, config);
			const auto actualArmy = NK2AI::getHeroArmyStrengthWithCommander(enemy, enemy);
			const auto townDanger = ai.dangerEvaluator->evaluateDanger(town->visitablePos(), enemy, true);
			const auto scalarLoss = ai.pathfinder->getStorage()->evaluateArmyLoss(enemy, actualArmy, townDanger);
			std::cout << "INTERACTION_DIAGNOSTIC phase=" << phase
				<< " enemy_x=" << enemy->visitablePos().x << " enemy_y=" << enemy->visitablePos().y
				<< " enemy_army=" << enemy->getArmyStrength() << " local_army=" << uniqueLocalStrength(town)
				<< " actual_enemy_army=" << actualArmy << " town_danger_for_enemy=" << townDanger << " scalar_loss=" << scalarLoss
				<< " enemy_visible=" << bridge.callback->isVisible(enemy) << " unseen_corridor_y=";
			for(int y = 7; y <= 40; ++y)
				if(!bridge.callback->isVisible(int3(44, y, 0))) std::cout << y << ',';
			std::cout << std::endl;
			for(const auto & target : {town->visitablePos(), town->visitablePos() + int3(0, 1, 0)})
			{
				const auto paths = ai.pathfinder->getPathInfo(target);
				std::cout << "INTERACTION_DIAGNOSTIC phase=" << phase << " target=" << target.toString()
					<< " paths=" << paths.size();
				for(const auto & path : paths)
					std::cout << " turn=" << static_cast<int>(path.turn()) << " danger=" << path.getTotalDanger()
						<< " loss=" << path.getTotalArmyLoss();
				std::cout << std::endl;
			}
			gateway->invalidatePaths(); // never leave enemy-only cached paths for planner
		});
	}

	static uint64_t uniqueLocalStrength(const CGTownInstance * town)
	{
		const auto * upper = town->getUpperArmy();
		uint64_t strength = upper->getArmyStrength();
		if(town->getVisitingHero() && town->getVisitingHero() != upper)
			strength += town->getVisitingHero()->getArmyStrength();
		return strength;
	}
};

TEST_F(AdventureInteractionScenarios, UrgentTownPurchaseThenThreeTurnRecovery)
{
	ASSERT_NO_FATAL_FAILURE(initialize("defence_economy_three_turns"));
	ASSERT_NO_FATAL_FAILURE(initializePlanner());
	const auto * threatened = dynamic_cast<const CGTownInstance *>(object("town_threatened"));
	const auto * safeTown = dynamic_cast<const CGTownInstance *>(object("town_safe"));
	const auto * objective = dynamic_cast<const CGMine *>(object("safe_objective"));
	const auto * observer = object("retreat_observer");
	const auto * observerFar = object("retreat_observer_far");
	const auto * enemy = dynamic_cast<const CGHeroInstance *>(object("hero_1"));
	ASSERT_NE(threatened, nullptr); ASSERT_NE(safeTown, nullptr);
	ASSERT_NE(objective, nullptr); ASSERT_NE(observer, nullptr); ASSERT_NE(observerFar, nullptr); ASSERT_NE(enemy, nullptr);
	ASSERT_EQ(objective->ID, Obj::MINE);
	ASSERT_EQ(objective->producedResource, GameResID::GOLD);
	ASSERT_EQ(objective->stacksCount(), 0);
	ASSERT_EQ(objective->getOwner(), PlayerColor::NEUTRAL);
	ASSERT_EQ(threatened->getOwner(), PlayerColor(0));
	ASSERT_EQ(safeTown->getOwner(), PlayerColor(0));
	ASSERT_EQ(observer->getOwner(), PlayerColor(0));
	ASSERT_EQ(observerFar->getOwner(), PlayerColor(0));
	ASSERT_TRUE(bridge.callback->isVisible(enemy));
	ASSERT_TRUE(bridge.callback->isVisible(objective));
	ASSERT_EQ(bridge.callback->getResourceAmount(EGameResID::GOLD), 2200)
		<< "Reject changed fixture income/event timing instead of tuning acceptance";
	const auto initialResources = bridge.callback->getResourceAmount();
	const auto initialStrength = uniqueLocalStrength(threatened);
	diagnoseThreat("before_recruitment", enemy, threatened);
	observeThreats();
	const auto initialThreat = gateway->nullkiller->dangerHitMap->getObjectThreat(threatened).fastestDanger;
	ASSERT_LE(initialThreat.turn, 1);
	ASSERT_GT(initialThreat.danger, initialStrength);
	for(const auto & threat : gateway->nullkiller->dangerHitMap->getTownThreats(safeTown))
		ASSERT_GT(threat.turn, 1);

	NK2AI::PlanningRiskSchedulerRegressionAccess::plannerTurn(*gateway);
	ASSERT_EQ(bridge.callback->getPlayerStatus(PlayerColor(0)), EPlayerStatus::INGAME);
	ASSERT_GT(uniqueLocalStrength(threatened), initialStrength);
	const auto firstPurchase = std::find_if(bridge.purchases.begin(), bridge.purchases.end(),
		[](const auto & purchase) { return purchase.bought > 0; });
	ASSERT_NE(firstPurchase, bridge.purchases.end());
	EXPECT_EQ(firstPurchase->town, threatened->id);
	ASSERT_FALSE(bridge.spending.empty());
	EXPECT_EQ(bridge.spending.front().town, threatened->id)
		<< "Nonurgent construction/recruitment must not consume the urgent budget first";
	TResources actualSpent;
	for(const auto & spend : bridge.spending) actualSpent += spend.resources;
	EXPECT_EQ(initialResources - bridge.callback->getResourceAmount(), actualSpent);
	EXPECT_EQ(threatened->getOwner(), PlayerColor(0));
	EXPECT_EQ(safeTown->getOwner(), PlayerColor(0));
	ASSERT_EQ(objective->getOwner(), PlayerColor::NEUTRAL)
		<< "Recovery objective must not already be captured before urgency removal";
	diagnoseThreat("after_recruitment_before_withdrawal", enemy, threatened);
	finishActiveTurn();
	ASSERT_EQ(bridge.active, PlayerColor(1));

	// Script only legal own-player requests. No teleport, army edit or fog grant.
	CCallback blue(bridge.handler->gs, PlayerColor(1), &bridge);
	for(int y = 11; y <= 25; ++y)
	{
		const int3 next(45, y, 0);
		const auto beforeMovement = enemy->movementPointsRemaining();
		blue.moveHero(enemy, next, false);
		ASSERT_EQ(enemy->anchorPos(), next);
		ASSERT_LT(enemy->movementPointsRemaining(), beforeMovement);
	}
	ASSERT_TRUE(bridge.callback->isVisible(enemy)) << "Owned ore-mine sight must expose retreat endpoint";
	ASSERT_EQ(observer->getOwner(), PlayerColor(0));
	finishActiveTurn();
	ASSERT_EQ(bridge.active, PlayerColor(0));
	ASSERT_EQ(bridge.handler->gameState().getDate(Date::DAY), 2);
	diagnoseThreat("after_first_withdrawal", enemy, threatened);
	observeThreats();
	ASSERT_LE(gateway->nullkiller->dangerHitMap->getObjectThreat(threatened).fastestDanger.turn, 1)
		<< "Day2 is deliberately still inside the two-turn threat horizon";
	NK2AI::PlanningRiskSchedulerRegressionAccess::plannerTurn(*gateway);
	ASSERT_EQ(objective->getOwner(), PlayerColor::NEUTRAL)
		<< "Do not count earlier mine capture as post-withdrawal recovery";
	finishActiveTurn();
	ASSERT_EQ(bridge.active, PlayerColor(1));
	for(int y = 26; y <= 40; ++y)
	{
		const int3 next(45, y, 0);
		const auto beforeMovement = enemy->movementPointsRemaining();
		blue.moveHero(enemy, next, false);
		ASSERT_EQ(enemy->anchorPos(), next);
		ASSERT_LT(enemy->movementPointsRemaining(), beforeMovement);
	}
	ASSERT_TRUE(bridge.callback->isVisible(enemy));
	ASSERT_EQ(observerFar->getOwner(), PlayerColor(0));
	finishActiveTurn();
	ASSERT_EQ(bridge.active, PlayerColor(0));
	ASSERT_EQ(bridge.handler->gameState().getDate(Date::DAY), 3);
	observeThreats();
	for(const auto & threat : gateway->nullkiller->dangerHitMap->getTownThreats(threatened))
		ASSERT_GT(threat.turn, 1) << "Second legal retreat did not clear the predeclared two-turn urgency horizon";
	const int movesBefore = bridge.productiveMoves;
	NK2AI::PlanningRiskSchedulerRegressionAccess::plannerTurn(*gateway);
	EXPECT_EQ(objective->getOwner(), PlayerColor(0));
	EXPECT_GT(bridge.productiveMoves, movesBefore);
	EXPECT_EQ(threatened->getOwner(), PlayerColor(0));
	EXPECT_EQ(safeTown->getOwner(), PlayerColor(0));
	EXPECT_EQ(bridge.callback->getHeroesInfo().size(), 1u);
}
}
