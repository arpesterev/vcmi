/*
* RecruitHero.h, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/
#pragma once

#include "CGoal.h"

namespace NK2AI
{

struct HeroPtr;
class AIGateway;
class FuzzyHelper;

namespace Goals
{
	class DLL_EXPORT RecruitHero : public ElementarGoal<RecruitHero>
	{
	private:
		const CGHeroInstance * heroToBuy;
		bool prepareDefence = false;
		TResources defenceBudget;
		std::vector<creInfo> defenceCreatures;
		uint64_t requiredDefenceStrength = 0;

	public:
		RecruitHero(const CGTownInstance * townWithTavern, const CGHeroInstance * heroToBuy)
			: ElementarGoal(Goals::RECRUIT_HERO), heroToBuy(heroToBuy)
		{
			town = townWithTavern;
			priority = 1;
		}

		RecruitHero(const CGTownInstance * townWithTavern)
			: RecruitHero(townWithTavern, nullptr)
		{
		}

		RecruitHero(const CGTownInstance * town, const CGHeroInstance * hero,
			const TResources & budget, const std::vector<creInfo> & creatures, uint64_t requiredStrength)
			: RecruitHero(town, hero)
		{
			prepareDefence = true;
			defenceBudget = budget;
			defenceCreatures = creatures;
			requiredDefenceStrength = requiredStrength;
		}

		bool operator==(const RecruitHero & other) const override
		{
			return true;
		}

		std::string toString() const override;
		const CGHeroInstance* getHero() const override { return heroToBuy; }
		void accept(AIGateway * aiGw) override;
	};
}

}
