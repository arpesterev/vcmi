/*
* BuyArmy.cpp, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/
#include "../StdInc.h"
#include "BuyArmy.h"
#include "../../../lib/battle/CombatValue.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../AIGateway.h"
#include "../Engine/Nullkiller.h"

namespace NK2AI
{

using namespace Goals;

namespace
{
int64_t creatureCount(const CCreatureSet * army, CreatureID creature)
{
	int64_t count = 0;
	for(const auto & slot : army->Slots())
		if(slot.second->getCreatureID() == creature)
			count += slot.second->getCount();
	return count;
}
}

bool BuyArmy::operator==(const BuyArmy & other) const
{
	return town == other.town && objid == other.objid;
}

std::string BuyArmy::toString() const
{
	return "Buy army at " + town->getNameTextID();
}

void BuyArmy::accept(AIGateway * aiGw)
{
	if(!town || town->getOwner() != aiGw->playerID)
		throw cannotFulfillGoalException("The recruitment town is no longer ours.");

	ui64 valueBought = 0;
	const auto * upperArmy = town->getUpperArmy();
	bool upgradeSuccessful = aiGw->makePossibleUpgrades(upperArmy);
	const auto * visitor = town->getVisitingHero();
	if(visitor && visitor->getOwner() == aiGw->playerID && visitor != upperArmy)
		upgradeSuccessful = aiGw->makePossibleUpgrades(visitor) || upgradeSuccessful;

	auto armyToBuy = aiGw->nullkiller->armyManager->getArmyAvailableToBuy(
		upperArmy, town, aiGw->nullkiller->getFreeResources());

	for(size_t i = 0; valueBought < value && i < armyToBuy.size(); ++i)
	{
		const auto resources = aiGw->nullkiller->getFreeResources();
		auto & creature = armyToBuy[i];
		if(objid != CreatureID::NONE && creature.creID.getNum() != objid)
			continue;

		vstd::amin(creature.count, resources.affordableCount(creature.creID.toCreature()->getFullRecruitCost()));
		if(creature.count <= 0)
			continue;

		if(needsFreeSlotToRecruit(upperArmy, creature.creID))
		{
			SlotID lowestValueSlot;
			int64_t lowestValue = std::numeric_limits<int64_t>::max();
			for(const auto & slot : upperArmy->Slots())
			{
				const auto * type = slot.second->getCreatureID().toCreature();
				if(type->getFactionID() == town->getFactionID())
					continue;

				const int64_t stackValue = static_cast<int64_t>(type->getFullRecruitCost().marketValue()) * slot.second->getCount();
				if(stackValue < lowestValue)
				{
					lowestValue = stackValue;
					lowestValueSlot = slot.first;
				}
			}

			const int64_t replacementValue = static_cast<int64_t>(creature.creID.toCreature()->getFullRecruitCost().marketValue()) * creature.count;
			if(lowestValueSlot.validSlot() && replacementValue > lowestValue)
				aiGw->cc->dismissCreature(upperArmy, lowestValueSlot);
		}

		if(needsFreeSlotToRecruit(upperArmy, creature.creID))
			continue;

		const auto countBefore = creatureCount(upperArmy, creature.creID);
		aiGw->cc->recruitCreatures(town, upperArmy, creature.creID, creature.count, creature.level);
		const auto countAfter = creatureCount(upperArmy, creature.creID);
		if(countAfter <= countBefore)
		{
			logAi->warn("Recruitment in %s did not add creatures; not counting it as progress", town->getNameTextID());
			continue;
		}

		valueBought += static_cast<ui64>(countAfter - countBefore)
			* LIBRARY->creh->getCombatValue().getAIValue(creature.creID.toCreature());
	}

	if(!valueBought)
	{
		if(upgradeSuccessful)
			return;
		throw cannotFulfillGoalException("No creatures were bought or upgraded.");
	}

	// A visiting hero may belong to an ally. Upgrades alone need no movement.
	if(town->getVisitingHero() && town->getVisitingHero()->tempOwner == aiGw->playerID && !town->getGarrisonHero())
	{
		if(aiGw->moveHeroToTile(town->visitablePos(), HeroPtr(town->getVisitingHero(), aiGw->cc.get()))
			!= HeroMovementResult::COMPLETE)
			throw cannotFulfillGoalException("Unable to revisit town after buying army.");
	}
}

}
