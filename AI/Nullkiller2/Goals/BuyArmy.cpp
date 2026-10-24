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
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../AIGateway.h"
#include "../Engine/Nullkiller.h"


namespace NK2AI
{

using namespace Goals;

bool BuyArmy::operator==(const BuyArmy & other) const
{
	return town == other.town && objid == other.objid;
}

std::string BuyArmy::toString() const
{
	return "Buy army at " + town->getNameTranslated();
}

void BuyArmy::accept(AIGateway * aiGw)
{
	ui64 valueBought = 0;
	//buy the stacks with largest AI value

	auto upgradeSuccessful = !spendingLimit && aiGw->makePossibleUpgrades(town);

	auto budget = aiGw->nullkiller->getFreeResources();
	if(spendingLimit)
		for(size_t resource = 0; resource < budget.size(); ++resource)
			budget[resource] = std::min(budget[resource], (*spendingLimit)[resource]);
	auto armyToBuy = plannedCreatures.empty()
		? aiGw->nullkiller->armyManager->getArmyAvailableToBuy(town->getUpperArmy(), town, budget)
		: plannedCreatures;

	if(armyToBuy.empty())
	{
		if(upgradeSuccessful)
			return;

		throw cannotFulfillGoalException("No creatures to buy.");
	}

	for(int i = 0; valueBought < value && i < armyToBuy.size(); i++)
	{
		auto res = aiGw->nullkiller->getFreeResources();
		for(size_t resource = 0; resource < res.size(); ++resource)
			res[resource] = std::min(res[resource], budget[resource]);
		auto & ci = armyToBuy[i];

		if(objid != CreatureID::NONE && ci.creID.getNum() != objid)
			continue;

		if(!plannedCreatures.empty())
		{
			// Revalidate the exact stock, not a different greedy purchase with
			// the same price. The state may have changed since planning.
			if(ci.level < 0 || ci.level >= town->creatures.size()
				|| !vstd::contains(town->creatures[ci.level].second, ci.creID))
				continue;
			vstd::amin(ci.count, town->creatures[ci.level].first);
		}
		vstd::amin(ci.count, res / ci.creID.toCreature()->getFullRecruitCost());

		if(ci.count)
		{
			// A defensive budget was evaluated without destroying existing troops.
			if(spendingLimit)
			{
				std::set<CreatureID> types;
				for(const auto & slot : town->getUpperArmy()->Slots())
					types.insert(slot.second->getCreatureID());
				if(town->getVisitingHero() && !town->getGarrisonHero())
					for(const auto & slot : town->getVisitingHero()->Slots())
						types.insert(slot.second->getCreatureID());
				types.insert(ci.creID);
				if(types.size() > GameConstants::ARMY_SIZE)
					continue;
			}
			if (town->getUpperArmy()->stacksCount() == GameConstants::ARMY_SIZE
				&& !town->getUpperArmy()->getSlotFor(ci.creID).validSlot())
			{
				SlotID lowestValueSlot;
				int lowestValue = std::numeric_limits<int>::max();
				for (const auto & slot : town->getUpperArmy()->Slots())
				{
					if (slot.second->getCreatureID() != CreatureID::NONE)
					{
						int currentStackMarketValue =
							slot.second->getCreatureID().toCreature()->getFullRecruitCost().marketValue() * slot.second->getCount();

						if (slot.second->getCreatureID().toCreature()->getFactionID() == town->getFactionID())
							continue;

						if (currentStackMarketValue < lowestValue)
						{
							lowestValue = currentStackMarketValue;
							lowestValueSlot = slot.first;
						}
					}
				}
				if (lowestValueSlot.validSlot())
				{
					aiGw->cc->dismissCreature(town->getUpperArmy(), lowestValueSlot);
				}
			}
			if (town->getUpperArmy()->stacksCount() < GameConstants::ARMY_SIZE || town->getUpperArmy()->getSlotFor(ci.creID).validSlot()) //It is possible we don't scrap despite we wanted to due to not scrapping stacks that fit our faction
			{
				const auto * targetArmy = town->getUpperArmy();
				auto countCreatures = [&]()
				{
					int count = 0;
					for(const auto & slot : targetArmy->Slots())
						if(slot.second->getCreatureID() == ci.creID)
							count += slot.second->getCount();
					return count;
				};
				const auto before = countCreatures();
				aiGw->cc->recruitCreatures(town, targetArmy, ci.creID, ci.count, ci.level);
				// AI callbacks wait for realization. A void request is not proof
				// of success; do not regenerate this same purchase after a no-op.
				const auto recruited = countCreatures() - before;
				if(recruited <= 0)
					throw cannotFulfillGoalException("Creature recruitment was not applied.");
				valueBought += recruited * ci.creID.toCreature()->getAIValue();
				budget -= ci.creID.toCreature()->getFullRecruitCost() * recruited;
			}
		}
	}

	if(!valueBought)
	{
		throw cannotFulfillGoalException("No creatures to buy.");
	}

	if(town->getVisitingHero() && !town->getGarrisonHero())
	{
		aiGw->moveHeroToTile(town->visitablePos(), HeroPtr(town->getVisitingHero(), aiGw->cc.get()));
	}
}

}
