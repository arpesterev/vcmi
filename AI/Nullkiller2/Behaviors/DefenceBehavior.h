/*
* DefenceBehavior.h, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/
#pragma once

#include "lib/GameLibrary.h"
#include "../Goals/CGoal.h"
#include "../AIUtility.h"

namespace NK2AI
{

struct HitMapInfo;

namespace Goals
{
	class DefenceBehavior : public CGoal<DefenceBehavior>
	{
	public:
		DefenceBehavior()
			:CGoal(DEFENCE)
		{
		}

		Goals::TGoalVec decompose(const Nullkiller * aiNk) const override;
		std::string toString() const override;

		// Recomputed before optional spending; only immediately affordable,
		// useful purchases are returned, with a shared budget across towns.
		static Goals::TGoalVec decomposeUrgentPurchases(const Nullkiller * aiNk);
		static const CGHeroInstance * selectDefender(const CGTownInstance * town, const HitMapInfo & threat,
			const std::vector<const CGHeroInstance *> & heroes, const TResources & resources);
		static uint64_t combinedDefenceStrength(const CGTownInstance * town, const CGHeroInstance * hero);

		bool operator==(const DefenceBehavior & other) const override
		{
			return true;
		}

	private:
		void evaluateDefence(Goals::TGoalVec & tasks, const CGTownInstance * town, const Nullkiller * aiNk) const;
	};
}


}
