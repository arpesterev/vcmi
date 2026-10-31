/*
* BuyArmyBehavior.cpp, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/
#include "StdInc.h"
#include "BuyArmyBehavior.h"
#include "../AIGateway.h"
#include "../AIUtility.h"
#include "../Goals/BuyArmy.h"
#include "../Engine/Nullkiller.h"

namespace NK2AI
{

using namespace Goals;

std::string BuyArmyBehavior::toString() const
{
	return "Buy army";
}

Goals::TGoalVec BuyArmyBehavior::decompose(const Nullkiller * aiNk) const
{
	Goals::TGoalVec tasks;
	const auto heroes = aiNk->cc->getHeroesInfo();
	const auto availableResources = aiNk->getFreeResources();

	for(const auto * town : aiNk->cc->getTownsInfo())
	{
		bool immediateThreat = false;
		if(aiNk->dangerHitMap->isHitMapUpToDate())
		{
			const auto & threat = aiNk->dangerHitMap->getObjectThreat(town);
			immediateThreat = (threat.fastestDanger.danger > 0 && threat.fastestDanger.turn <= 1)
				|| (threat.maximumDanger.danger > 0 && threat.maximumDanger.turn <= 1);
		}

		if(!immediateThreat && aiNk->buildAnalyzer->isGoldPressureOverMax()
			&& !town->hasBuilt(BuildingID::CITY_HALL)
			&& aiNk->cc->canBuildStructure(town, BuildingID::CITY_HALL) != EBuildingState::FORBIDDEN)
		{
			// Saving for income in this town must not suppress all later towns.
			continue;
		}

		const auto affordableReinforcement = aiNk->armyManager->howManyReinforcementsCanBuy(
			town->getUpperArmy(), town, availableResources);
		uint64_t reinforcement = immediateThreat ? affordableReinforcement : 0;

		if(!immediateThreat && affordableReinforcement > 0)
		{
			const auto townArmy = aiNk->armyManager->getArmyAvailableToBuyAsCCreatureSet(town, availableResources);
			for(const auto * hero : heroes)
			{
				if(aiNk->heroManager->getHeroRoleOrDefaultInefficient(hero) != HeroRole::MAIN)
					continue;

				const auto useful = aiNk->armyManager->howManyReinforcementsCanGet(
					hero, hero, townArmy.get(), TerrainId::NONE);
				reinforcement = std::max<uint64_t>(reinforcement, std::min(useful, affordableReinforcement));
			}
		}

		// Existing troops can be upgraded even when every dwelling is sold out.
		// A visiting hero must not be forced to leave and re-enter its own town.
		auto upgrade = aiNk->armyManager->calculateCreaturesUpgrade(town->getUpperArmy(), town, availableResources);
		int64_t upgradeValue = std::max<int64_t>(0, upgrade.upgradeValue);
		const auto * visitor = town->getVisitingHero();
		if(visitor && visitor->getOwner() == aiNk->playerID && visitor != town->getUpperArmy())
		{
			auto remainingResources = availableResources - upgrade.upgradeCost;
			remainingResources.removeNegative();
			upgradeValue += std::max<int64_t>(0,
				aiNk->armyManager->calculateCreaturesUpgrade(visitor, town, remainingResources).upgradeValue);
		}

		if(reinforcement == 0 && upgradeValue == 0)
			continue;

		// One goal per town; urgent defence is independent of MAIN hero assignment.
		const float value = static_cast<float>(reinforcement) + static_cast<float>(upgradeValue);
		const float priority = immediateThreat ? 1000000.0f + std::min(value, 99999.0f) : value;
		tasks.push_back(sptr(BuyArmy(town, reinforcement).setpriority(priority)));
	}

	return tasks;
}

}
