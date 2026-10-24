/*
* DefenceBehavior.cpp, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/
#include "StdInc.h"
#include "DefenceBehavior.h"

#include "../AIGateway.h"
#include "../AIUtility.h"
#include "../Behaviors/CaptureObjectsBehavior.h"
#include "../Engine/Nullkiller.h"
#include "../Goals/BuyArmy.h"
#include "../Goals/Composition.h"
#include "../Goals/DismissHero.h"
#include "../Goals/ExchangeSwapTownHeroes.h"
#include "../Goals/ExecuteHeroChain.h"
#include "../Goals/RecruitHero.h"
#include "../Markers/DefendTown.h"
#include "../../lib/IGameSettings.h"

namespace NK2AI
{

const float THREAT_IGNORE_RATIO = 2;

using namespace Goals;

std::string DefenceBehavior::toString() const
{
	return "Defend towns";
}

Goals::TGoalVec DefenceBehavior::decompose(const Nullkiller * aiNk) const
{
	Goals::TGoalVec tasks;

	for(const auto town : aiNk->cc->getTownsInfo())
	{
		evaluateDefence(tasks, town, aiNk);
	}

	return tasks;
}

uint64_t DefenceBehavior::combinedDefenceStrength(const CGTownInstance * town, const CGHeroInstance * hero)
{
	const auto * army = town->getUpperArmy();
	if(!hero)
		return army->getArmyStrength();
	if(army == hero)
		return static_cast<uint64_t>(hero->getArmyStrength() * hero->getFightingStrength());
	// No dismissal or imaginary eighth slot is allowed to make this plan work.
	if(!hero->canBeMergedWith(*army))
		return static_cast<uint64_t>(hero->getArmyStrength() * hero->getFightingStrength());
	return static_cast<uint64_t>((army->getArmyStrength() + hero->getArmyStrength()) * hero->getFightingStrength());
}

const CGHeroInstance * DefenceBehavior::selectDefender(const CGTownInstance * town, const HitMapInfo & threat,
	const std::vector<const CGHeroInstance *> & heroes, const TResources & resources)
{
	if(threat.turn > 1 || !threat.danger || town->getGarrisonHero() || town->getVisitingHero()
		|| !town->hasBuilt(BuildingID::TAVERN) || resources[EGameResID::GOLD] < GameConstants::HERO_GOLD_COST)
		return nullptr;
	const CGHeroInstance * best = nullptr;
	for(const auto * hero : heroes)
	{
		if(hero->canBeMergedWith(*town->getUpperArmy()) && combinedDefenceStrength(town, hero) >= threat.danger
			&& (!best || combinedDefenceStrength(town, hero) > combinedDefenceStrength(town, best)))
			best = hero;
	}
	return best;
}

namespace
{
HitMapInfo urgentTownThreat(const CGTownInstance * town, const Nullkiller * aiNk)
{
	HitMapInfo result;
	auto consider = [&](const HitMapInfo & threat)
	{
		if(threat.turn <= 1 && threat.heroPtr.isVerified() && threat.danger > result.danger)
			result = threat;
	};
	const auto & node = aiNk->dangerHitMap->getObjectThreat(town);
	consider(node.fastestDanger);
	consider(node.maximumDanger);
	for(const auto & threat : aiNk->dangerHitMap->getTownThreats(town))
		consider(threat);
	return result;
}
}

Goals::TGoalVec DefenceBehavior::decomposeUrgentPurchases(const Nullkiller * aiNk)
{
	Goals::TGoalVec tasks;
	auto available = aiNk->getFreeResources();
	auto towns = aiNk->cc->getTownsInfo();
	std::stable_sort(towns.begin(), towns.end(), [aiNk](const auto * left, const auto * right)
	{
		const auto a = urgentTownThreat(left, aiNk);
		const auto b = urgentTownThreat(right, aiNk);
		return a.turn != b.turn ? a.turn < b.turn : left->id < right->id;
	});
	std::set<const CGHeroInstance *> reservedHeroes;
	for(const auto * town : towns)
	{
		const auto threat = urgentTownThreat(town, aiNk);
		// One intervening AI turn is the preparation horizon. Distant danger
		// must not permanently consume the economy or immobilize a garrison.
		if(threat.turn > 1 || !threat.heroPtr.isVerified() || !threat.danger)
			continue;
		const auto * defender = town->getGarrisonHero() ? town->getGarrisonHero() : town->getVisitingHero();
		const auto strength = combinedDefenceStrength(town, defender);
		if(strength >= threat.danger)
			continue;

		if(!defender && town->hasBuilt(BuildingID::TAVERN)
			&& available[EGameResID::GOLD] >= GameConstants::HERO_GOLD_COST
			&& !aiNk->heroManager->heroCapReached())
		{
			auto candidates = aiNk->cc->getAvailableHeroes(town);
			vstd::erase_if(candidates, [&](const auto * hero) { return reservedHeroes.count(hero) != 0; });
			const auto * best = selectDefender(town, threat, candidates, available);
			if(best)
			{
				available[EGameResID::GOLD] -= GameConstants::HERO_GOLD_COST;
				reservedHeroes.insert(best);
				tasks.push_back(sptr(RecruitHero(town, best).setpriority(1.0f / (1 + threat.turn))));
				continue;
			}
		}

		// Buy only enough currently available troops to make holding plausible.
		// Infeasible towns reserve nothing, leaving funds for other towns.
		auto army = aiNk->armyManager->getArmyAvailableToBuy(town->getUpperArmy(), town, available);
		std::set<CreatureID> types;
		for(const auto & slot : town->getUpperArmy()->Slots())
			types.insert(slot.second->getCreatureID());
		if(defender && defender != town->getUpperArmy())
			for(const auto & slot : defender->Slots())
				types.insert(slot.second->getCreatureID());
		if(types.size() > GameConstants::ARMY_SIZE)
			continue;
		TResources cost;
		std::vector<creInfo> plannedCreatures;
		uint64_t value = 0;
		const double multiplier = defender ? defender->getFightingStrength() : 1.0;
		for(const auto & creature : army)
		{
			if(!types.count(creature.creID) && types.size() == GameConstants::ARMY_SIZE)
				continue;
			const auto power = creature.creID.toCreature()->getAIValue();
			if(!power || multiplier <= 0)
				continue;
			const auto missing = static_cast<double>(threat.danger) - strength - value * multiplier;
			const int count = static_cast<int>(std::min<double>(creature.count, std::ceil(missing / (power * multiplier))));
			if(count <= 0)
				break;
			types.insert(creature.creID);
			auto planned = creature;
			planned.count = count;
			plannedCreatures.push_back(planned);
			value += static_cast<uint64_t>(count) * power;
			cost += creature.creID.toCreature()->getFullRecruitCost() * count;
			if(strength + value * multiplier >= threat.danger)
				break;
		}
		if(value && strength + value * multiplier >= threat.danger && available.canAfford(cost))
		{
			available -= cost;
			tasks.push_back(sptr(BuyArmy(town, value, cost, plannedCreatures).setpriority(1.0f / (1 + threat.turn))));
		}
	}
	return tasks;
}

bool isThreatUnderControl(const CGTownInstance * town, const HitMapInfo & threat, const Nullkiller * aiNk, const std::vector<AIPath> & paths)
{
	int dayOfWeek = aiNk->cc->getDate(Date::DAY_OF_WEEK);

	for(const AIPath & path : paths)
	{
		// A strong hero cannot cover this threat through an unavailable route.
		if(aiNk->arePathHeroesLocked(path) || path.getFirstBlockedAction())
			continue;

		bool threatIsWeak = path.getHeroStrength() / (float)threat.danger > THREAT_IGNORE_RATIO;
		bool needToSaveGrowth = threat.turn == 0 && dayOfWeek == LIBRARY->engineSettings()->getInteger(EGameSettings::GENERAL_DAYS_PER_WEEK);

		if(threatIsWeak && !needToSaveGrowth)
		{
			if((path.exchangeCount == 1 && path.turn() < threat.turn) || path.turn() < threat.turn - 1 || (path.turn() < threat.turn && threat.turn >= 2))
			{
#if NK2AI_TRACE_LEVEL >= 1
				logAi->trace(
					"Hero %s can eliminate danger for town %s using path %s.", path.targetHero->getObjectName(), town->getObjectName(), path.toString()
				);
#endif

				return true;
			}
		}
	}

	return false;
}

void handleCounterAttack(
	const CGTownInstance * town,
	const HitMapInfo & threat,
	const HitMapInfo & maximumDanger,
	const Nullkiller * aiNk,
	Goals::TGoalVec & tasks
)
{
	if(threat.heroPtr.isVerified() && threat.turn <= 1 && (threat.danger == maximumDanger.danger || threat.turn < maximumDanger.turn))
	{
		auto heroCapturingPaths = aiNk->pathfinder->getPathInfo(threat.heroPtr->visitablePos());
		auto goals = CaptureObjectsBehavior::getVisitGoals(heroCapturingPaths, aiNk, threat.heroPtr.get());

		for(int i = 0; i < heroCapturingPaths.size(); i++)
		{
			AIPath & path = heroCapturingPaths[i];
			TSubgoal goal = goals[i];

			if(!goal || goal->invalid() || !goal->isElementar())
				continue;

			Composition composition;
			composition.addNext(DefendTown(town, threat, path, true)).addNext(goal);
			tasks.push_back(Goals::sptr(composition));
		}
	}
}

bool handleGarrisonHeroFromPreviousTurn(const CGTownInstance * town, Goals::TGoalVec & tasks, const Nullkiller * aiNk)
{
	if(aiNk->isHeroLocked(town->getGarrisonHero()))
	{
		logAi->trace("Hero %s in garrison of town %s is supposed to defend the town", town->getGarrisonHero()->getNameTranslated(), town->getNameTranslated());
		return true;
	}

	if(!town->getVisitingHero())
	{
		if(aiNk->cc->getHeroCount(aiNk->playerID, false) < GameConstants::MAX_HEROES_PER_PLAYER)
		{
			logAi->trace("Extracting hero %s from garrison of town %s", town->getGarrisonHero()->getNameTranslated(), town->getNameTranslated());
			tasks.push_back(Goals::sptr(Goals::ExchangeSwapTownHeroes(town, nullptr).setpriority(5)));
			return false;
		}

		if(aiNk->heroManager->getHeroRoleOrDefaultInefficient(town->getGarrisonHero()) == HeroRole::MAIN)
		{
			auto armyDismissLimit = 1000;
			auto heroToDismiss = aiNk->heroManager->findWeakHeroToDismiss(armyDismissLimit);
			if(heroToDismiss)
			{
				tasks.push_back(Goals::sptr(Goals::DismissHero(heroToDismiss).setpriority(5)));
				return false;
			}
		}
	}

	return false;
}

void DefenceBehavior::evaluateDefence(Goals::TGoalVec & tasks, const CGTownInstance * town, const Nullkiller * aiNk) const
{
#if NK2AI_TRACE_LEVEL >= 1
	logAi->trace("Evaluating defence for %s", town->getNameTranslated());
#endif

	auto threatNode = aiNk->dangerHitMap->getObjectThreat(town);
	std::vector<HitMapInfo> threats = aiNk->dangerHitMap->getTownThreats(town);
	// TODO: Mircea: Why don't we check if there's any danger in threadNode? Maybe map is still unexplored and no danger
	// or simply no one is around
	threats.push_back(threatNode.fastestDanger); // no guarantee that fastest danger will be there

	if(town->getGarrisonHero() && (aiNk->isHeroLocked(town->getGarrisonHero())
		|| ((threatNode.fastestDanger.turn > 1 || !threatNode.fastestDanger.heroPtr.isVerified())
			&& handleGarrisonHeroFromPreviousTurn(town, tasks, aiNk))))
		return;

	if(!threatNode.fastestDanger.heroPtr.isVerified())
	{
#if NK2AI_TRACE_LEVEL >= 1
		logAi->trace("No threat found for town %s", town->getNameTranslated());
#endif
		return;
	}

	auto paths = aiNk->pathfinder->getPathInfo(town->visitablePos());

	for(auto & threat : threats)
	{
#if NK2AI_TRACE_LEVEL >= 1
		logAi->trace(
			"Town %s has threat %lld in %s turns, hero: %s",
			town->getNameTranslated(),
			threat.danger,
			std::to_string(threat.turn),
			threat.heroPtr.nameOrDefault()
		);
#endif
		handleCounterAttack(town, threat, threatNode.maximumDanger, aiNk, tasks);

		if(isThreatUnderControl(town, threat, aiNk, paths))
			continue;


		if(paths.empty())
		{
#if NK2AI_TRACE_LEVEL >= 1
			logAi->trace("No ways to defend town %s", town->getNameTranslated());
#endif
			continue;
		}

		std::vector<int> pathsToDefend;
		std::map<const CGHeroInstance *, std::vector<int>> defferedPaths;
		AIPath * closestWay = nullptr;

		for(int i = 0; i < paths.size(); i++)
		{
			auto & path = paths[i];
			if(path.turn() > threat.turn || aiNk->arePathHeroesLocked(path))
				continue;
			if(!closestWay || path.movementCost() < closestWay->movementCost())
				closestWay = &path;

#if NK2AI_TRACE_LEVEL >= 1
			logAi->trace(
				"Hero %s can defend town with force %lld in %s turns, cost: %f, path: %s",
				path.targetHero->getObjectName(),
				path.getHeroStrength(),
				std::to_string(path.turn()),
				path.movementCost(),
				path.toString()
			);
#endif

			auto townDefenseStrength = town->getGarrisonHero()
										 ? town->getGarrisonHero()->getTotalStrength()
										 : (town->getVisitingHero() ? town->getVisitingHero()->getTotalStrength() : town->getUpperArmy()->getArmyStrength());

			if(town->getVisitingHero() && path.targetHero == town->getVisitingHero())
			{
				if(path.getHeroStrength() < townDefenseStrength)
					continue;
			}
			else if(town->getGarrisonHero() && path.targetHero == town->getGarrisonHero())
			{
				if(path.getHeroStrength() < townDefenseStrength)
					continue;
			}

			if(path.turn() <= threat.turn - 2)
			{
#if NK2AI_TRACE_LEVEL >= 1
				logAi->trace(
					"Defer defence of %s by %s because he has enough time to reach the town next turn", town->getObjectName(), path.targetHero->getObjectName()
				);
#endif

				defferedPaths[path.targetHero].push_back(i);
				continue;
			}

			if(!path.targetHero->canBeMergedWith(*town))
			{
#if NK2AI_TRACE_LEVEL >= 1
				logAi->trace("Can't merge armies of hero %s and town %s", path.targetHero->getObjectName(), town->getObjectName());
#endif
				continue;
			}

			if(path.targetHero == town->getVisitingHero() && path.exchangeCount == 1)
			{
#if NK2AI_TRACE_LEVEL >= 1
				logAi->trace("Put %s to garrison of town %s", path.targetHero->getObjectName(), town->getObjectName());
#endif

				// dismiss creatures we are not able to pick to be able to hide in garrison
				if(town->getGarrisonHero() || town->getUpperArmy()->stacksCount() == 0 || path.targetHero->canBeMergedWith(*town)
				   || (town->getUpperArmy()->getArmyStrength() < 500 && town->fortLevel() >= CGTownInstance::CITADEL))
				{
					tasks.push_back(
						Goals::sptr(
							Composition()
								.addNext(DefendTown(town, threat, path.targetHero, combinedDefenceStrength(town, path.targetHero)))
								.addNext(ExchangeSwapTownHeroes(town, town->getVisitingHero(), HeroLockedReason::DEFENCE))
						)
					);
				}

				continue;
			}

			// main without army and visiting scout with army, very specific case
			if(town->getVisitingHero() && town->getUpperArmy()->stacksCount() == 0 && path.targetHero != town->getVisitingHero() && path.exchangeCount == 1
			   && path.turn() == 0 && aiNk->heroManager->evaluateHero(path.targetHero) > aiNk->heroManager->evaluateHero(town->getVisitingHero())
			   && 10 * path.targetHero->getTotalStrength() < town->getVisitingHero()->getTotalStrength())
			{
				path.heroArmy = town->getVisitingHero();

				tasks.push_back(
					Goals::sptr(
						Composition()
							.addNext(DefendTown(town, threat, path))
							.addNextSequence(
								{sptr(ExchangeSwapTownHeroes(town, town->getVisitingHero())),
								 sptr(ExecuteHeroChain(path, town)),
								 sptr(ExchangeSwapTownHeroes(town, path.targetHero, HeroLockedReason::DEFENCE))}
							)
					)
				);

				continue;
			}

			if(threat.turn == 0 || (path.turn() <= threat.turn && path.getHeroStrength() >= threat.danger * aiNk->settings->getSafeAttackRatio()))
			{
				if(aiNk->arePathHeroesLocked(path))
				{
#if NK2AI_TRACE_LEVEL >= 1
					logAi->trace("Can not move %s to defend town %s. Path is locked.", path.targetHero->getObjectName(), town->getObjectName());

#endif
					continue;
				}

				pathsToDefend.push_back(i);
			}
		}

		for(int i : pathsToDefend)
		{
			AIPath & path = paths[i];
			for(int j : defferedPaths[path.targetHero])
			{
				AIPath & defferedPath = paths[j];
				if(defferedPath.getHeroStrength() >= path.getHeroStrength() && defferedPath.turn() <= path.turn())
				{
					continue; // TODO: Mircea: Should it be break instead? Or continue for the outside loop?
				}
			}

			Composition composition;
			composition.addNext(DefendTown(town, threat, path));
			TGoalVec sequence;

			if(town->getGarrisonHero() && path.targetHero == town->getGarrisonHero() && path.exchangeCount == 1)
			{
				composition.addNext(ExchangeSwapTownHeroes(town, town->getGarrisonHero(), HeroLockedReason::DEFENCE));
				tasks.push_back(Goals::sptr(composition));

#if NK2AI_TRACE_LEVEL >= 1
				logAi->trace("Locking hero %s in garrison of %s", town->getGarrisonHero()->getObjectName(), town->getObjectName());
#endif
				continue;
			}

			if(town->getVisitingHero() && path.targetHero != town->getVisitingHero() && !path.containsHero(town->getVisitingHero()))
			{
				if(town->getGarrisonHero() && town->getGarrisonHero() != path.targetHero)
				{
#if NK2AI_TRACE_LEVEL >= 1
					logAi->trace("Cancel moving %s to defend town %s as the town has garrison hero", path.targetHero->getObjectName(), town->getObjectName());
#endif
					continue;
				}

				if(path.turn() == 0)
				{
					sequence.push_back(sptr(ExchangeSwapTownHeroes(town, town->getVisitingHero())));
				}
			}

#if NK2AI_TRACE_LEVEL >= 1
			logAi->trace("Move %s to defend town %s", path.targetHero->getObjectName(), town->getObjectName());
#endif

			ExecuteHeroChain heroChain(path, town);

			if(closestWay)
			{
				heroChain.setClosestWayRatio(*closestWay);
			}

			sequence.push_back(sptr(heroChain));
			composition.addNextSequence(sequence);

			const auto firstBlockedAction = path.getFirstBlockedAction();
			if(firstBlockedAction)
			{
				auto subGoal = firstBlockedAction->decompose(aiNk, path.targetHero);

#if NK2AI_TRACE_LEVEL >= 2
				logAi->trace("Decomposing special action %s returns %s", firstBlockedAction->toString(), subGoal->toString());
#endif

				if(subGoal->invalid())
				{
#if NK2AI_TRACE_LEVEL >= 1
					logAi->trace("Path is invalid. Skipping");
#endif
					continue;
				}

				composition.addNext(subGoal);
			}

			tasks.push_back(Goals::sptr(composition));
		}
	}

	logAi->debug("Found %d tasks", tasks.size());
}

}
