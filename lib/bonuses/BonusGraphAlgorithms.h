/*
 * BonusGraphAlgorithms.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#pragma once
#include "BonusList.h"

namespace BonusGraphAlgorithms
{
template<typename Parents, typename Recurse, typename Update>
void collect(const Parents & parents, const BonusList & local, Recurse recurse, Update update, BonusList & out)
{
	BonusList beforeUpdate;
	for(const auto & parent : parents)
		recurse(parent, beforeUpdate);
	local.getAllBonuses(beforeUpdate);
	for(const auto & bonus : beforeUpdate)
		out.push_back(bonus->updater ? update(bonus, bonus->updater) : bonus);
}

template<typename Decide>
void limit(const BonusList & all, BonusList & accepted, Decide decide)
{
	assert(&all != &accepted);
	BonusList undecided = all;
	while(true)
	{
		int undecidedCount = static_cast<int>(undecided.size());
		for(int i = 0; i < undecided.size(); ++i)
		{
			auto bonus = undecided[i];
			auto decision = decide(*bonus, accepted, undecided);
			if(decision == ILimiter::EDecision::DISCARD || decision == ILimiter::EDecision::NOT_APPLICABLE)
			{
				undecided.erase(i);
				--i;
			}
			else if(decision == ILimiter::EDecision::ACCEPT)
			{
				accepted.push_back(bonus);
				undecided.erase(i);
				--i;
			}
			else
				assert(decision == ILimiter::EDecision::NOT_SURE);
		}
		if(undecided.size() == undecidedCount)
			return;
	}
}
}
