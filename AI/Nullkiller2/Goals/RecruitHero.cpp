/*
* RecruitHero.cpp, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/
#include "StdInc.h"
#include "Goals.h"
#include "../AIGateway.h"
#include "../AIUtility.h"
#include "BuyArmy.h"
#include "ExchangeSwapTownHeroes.h"
#include "../Behaviors/DefenceBehavior.h"
#include "../../../lib/constants/StringConstants.h"


namespace NK2AI
{

using namespace Goals;

std::string RecruitHero::toString() const
{
	if(heroToBuy)
		return "Recruit " + heroToBuy->getNameTranslated() + " at " + town->getNameTranslated();
	else
		return "Recruit hero at " + town->getNameTranslated();
}

void RecruitHero::accept(AIGateway * aiGw)
{
	auto t = town;

	if(!t)
	{
		throw cannotFulfillGoalException("No town to recruit hero!");
	}

	logAi->debug("Trying to recruit a hero in %s at %s", t->getNameTranslated(), t->visitablePos().toString());

	const auto heroes = aiGw->cc->getAvailableHeroes(t);
	if(!heroes.size())
	{
		throw cannotFulfillGoalException("No available heroes in tavern in " + t->nodeName());
	}

	auto heroToHire = heroToBuy;
	if(!heroToHire)
	{
		for(auto hero : heroes)
		{
			if(!heroToHire || hero->getTotalStrength() > heroToHire->getTotalStrength())
				heroToHire = hero;
		}
	}

	if(!heroToHire)
		throw cannotFulfillGoalException("No hero to hire!");

	if(prepareDefence)
	{
		auto totalCost = defenceBudget;
		totalCost[EGameResID::GOLD] += GameConstants::HERO_GOLD_COST;
		auto resourcesAfterHire = aiGw->nullkiller->getFreeResources();
		if(t->getVisitingHero() || t->getGarrisonHero() || !t->hasBuilt(BuildingID::TAVERN)
			|| !vstd::contains(heroes, heroToHire) || aiGw->nullkiller->heroManager->heroCapReached()
			|| !resourcesAfterHire.canAfford(totalCost))
			throw cannotFulfillGoalException("Town rescue hire or total budget is no longer available.");
		resourcesAfterHire[EGameResID::GOLD] -= GameConstants::HERO_GOLD_COST;
		if(!BuyArmy::isDefencePlanAvailable(t, heroToHire, resourcesAfterHire, defenceBudget, defenceCreatures))
			throw cannotFulfillGoalException("Town rescue stock or army slots are no longer available.");
		double projectedStrength = DefenceBehavior::combinedDefenceStrength(t, heroToHire);
		for(const auto & creature : defenceCreatures)
			if(creature.creID != CreatureID::NONE)
				projectedStrength += creature.count * static_cast<double>(creature.creID.toCreature()->getAIValue()) * heroToHire->getFightingStrength();
		if(projectedStrength < requiredDefenceStrength)
			throw cannotFulfillGoalException("Town rescue would no longer provide the required defence strength.");
	}

	if(t->getVisitingHero())
	{
		aiGw->cc->swapGarrisonHero(t);
	}

	if(t->getVisitingHero())
		throw cannotFulfillGoalException("Town " + t->nodeName() + " is occupied. Cannot recruit hero!");

	const auto hiredType = heroToHire->getHeroTypeID();
	aiGw->cc->recruitHero(t, heroToHire);
	if(!t->getVisitingHero() || t->getVisitingHero()->getHeroTypeID() != hiredType)
		throw cannotFulfillGoalException("Hero recruitment was not applied.");

	{
		// TODO: Mircea: Consider same behavior when a hero is lost? Relevant?
		std::unique_lock lockGuard(aiGw->nullkiller->aiStateMutex);
		aiGw->nullkiller->heroManager->update();
		aiGw->nullkiller->objectClusterizer->reset();
	}

	if(prepareDefence)
	{
		const auto * defender = t->getVisitingHero();
		if(!BuyArmy::isDefencePlanAvailable(t, defender, aiGw->nullkiller->getFreeResources(), defenceBudget, defenceCreatures))
			throw cannotFulfillGoalException("Hero hired, but the remaining town rescue plan is no longer available.");
		if(!defenceCreatures.empty())
		{
			BuyArmy purchase(t, std::numeric_limits<int>::max(), defenceBudget, defenceCreatures);
			try
			{
				purchase.accept(aiGw);
			}
			catch(const cannotFulfillGoalException & error)
			{
				throw cannotFulfillGoalException(std::string("Hero hired; town troop purchase failed without rollback: ") + error.what());
			}
		}
		if(DefenceBehavior::combinedDefenceStrength(t, defender) < requiredDefenceStrength)
			throw cannotFulfillGoalException("Town rescue recruitment was incomplete; hero and any bought troops remain in town.");
		ExchangeSwapTownHeroes(t, defender, HeroLockedReason::DEFENCE, true).accept(aiGw);
	}
}

}
