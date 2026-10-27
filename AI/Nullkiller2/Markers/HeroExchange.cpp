/*
* HeroExchange.cpp, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/
#include "StdInc.h"
#include "HeroExchange.h"
#include "../AIGateway.h"
#include "../Engine/Nullkiller.h"
#include "../AIUtility.h"
#include "../Analyzers/ArmyManager.h"
#include "../../../lib/mapping/TerrainTile.h"
#include "../../../lib/entities/artifact/CArtifact.h"

namespace NK2AI
{

using namespace Goals;

bool HeroExchange::operator==(const HeroExchange & other) const
{
	return false;
}

std::string HeroExchange::toString() const
{
	return "Hero exchange for " +hero->getNameTextID() + " by " + exchangePath.toString();
}

uint64_t HeroExchange::getReinforcementArmyStrength(const Nullkiller * aiNk) const
{
	assert(exchangePath.targetHero && exchangePath.heroArmy);

	return aiNk->armyManager->howManyReinforcementsCanGet(
		hero,
		hero,
		exchangePath.heroArmy,
		aiNk->cc->getTile(exchangePath.targetTile())->getTerrainID());
}

int64_t HeroExchange::getReinforcementArtifactValue(const Nullkiller * aiNk) const
{
	const auto * courier = exchangePath.targetHero;
	if(!hero || !courier || courier == hero
		|| exchangePath.turn() > 1 || exchangePath.exchangeCount != 1
		|| aiNk->heroManager->getHeroRoleOrDefaultInefficient(hero) != MAIN
		|| aiNk->heroManager->getHeroRoleOrDefaultInefficient(courier) != SCOUT)
		return 0;

	int64_t bestUpgrade = 0;
	const auto consider = [&](const ArtSlotInfo & info)
	{
		if(info.locked)
			return;
		const auto * art = info.getArt();
		if(!art || !art->getType()->isTradable())
			return;
		const auto & choices = art->getType()->getPossibleSlots();
		const auto slots = choices.find(hero->bearerType());
		if(slots == choices.end())
			return;

		const int64_t score = std::max<int64_t>(0, getArtifactScoreForHero(hero, art));
		for(const auto slot : slots->second)
		{
			const auto * equippedInfo = hero->getSlot(slot);
			if(equippedInfo && equippedInfo->locked)
				continue;
			if(!art->canBePutAt(hero, slot, true))
				continue;
			const auto * equipped = hero->getArt(slot);
			const int64_t currentScore = equipped ? std::max<int64_t>(0, getArtifactScoreForHero(hero, equipped)) : 0;
			bestUpgrade = std::max(bestUpgrade, score - currentScore);
		}
	};

	for(const auto & [slot, art] : courier->artifactsWorn)
		if(slot != ArtifactPosition::MACH4)
			consider(art);
	for(const auto & art : courier->artifactsInBackpack)
		consider(art);

	// Only the most valuable replacement is counted, so multiple incompatible
	// artifacts never create an inflated transport incentive.
	return std::min<int64_t>(bestUpgrade, 20000);
}

}
