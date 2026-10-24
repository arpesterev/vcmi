/*
* BuyArmy.h, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/
#pragma once

#include "CGoal.h"
#include <optional>

namespace NK2AI
{

struct HeroPtr;
class AIGateway;
class FuzzyHelper;

namespace Goals
{
	class DLL_EXPORT BuyArmy : public ElementarGoal<BuyArmy>
	{
	private:
		std::optional<TResources> spendingLimit;
		std::vector<creInfo> plannedCreatures;
		const CGHeroInstance * defenceCommander = nullptr;
		uint64_t requiredDefenceStrength = 0;
		BuyArmy()
			: ElementarGoal(Goals::BUY_ARMY)
		{
		}
	public:
		BuyArmy(const CGTownInstance * Town, int val)
			: ElementarGoal(Goals::BUY_ARMY)
		{
			town = Town; //where to buy this army
			value = val; //expressed in AI unit strength
			priority = 3;//TODO: evaluate?
		}

		BuyArmy(const CGTownInstance * town, int value, const TResources & budget, const std::vector<creInfo> & creatures = {},
			const CGHeroInstance * commander = nullptr, uint64_t requiredStrength = 0)
			: BuyArmy(town, value)
		{
			spendingLimit = budget;
			plannedCreatures = creatures;
			defenceCommander = commander;
			requiredDefenceStrength = requiredStrength;
			hero = commander;
		}

		static bool isDefencePlanAvailable(const CGTownInstance * town, const CGHeroInstance * defender,
			const TResources & available, const TResources & budget, const std::vector<creInfo> & creatures);

		bool operator==(const BuyArmy & other) const override;

		std::string toString() const override;

		void accept(AIGateway * aiGw) override;
	};
}

}
