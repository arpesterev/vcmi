/* GPL-2.0-or-later; see license.txt. Experimental detached battle state. */
#pragma once
#include "../../../../lib/bonuses/IBonusBearer.h"
#include "../../../../lib/bonuses/BonusList.h"
#include "../../../../lib/constants/Enumerations.h"

namespace NK2AI::experimental
{
// No bonus-system graph, no live army links. Owns the effective initial list
// and only the native defensive-stance effects introduced by this rollout.
class DetachedDefendBonuses final : public IBonusBearer
{
	TConstBonusListPtr initial;
	BonusList stance;
	int version = 1; // Engine caches reserve zero for an uninitialized value.
public:
	explicit DetachedDefendBonuses(const IBonusBearer & source)
		: initial(source.getAllBonuses(Selector::all)) {}

	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & = {}) const override
	{
		auto result = std::make_shared<BonusList>();
		initial->getBonuses(*result, selector);
		stance.getBonuses(*result, selector);
		return result;
	}
	int32_t getTreeVersion() const override { return version; }

	void beforeActivation()
	{
		if(!stance.empty())
		{
			stance.clear();
			++version;
		}
	}

	void defend()
	{
		beforeActivation();
		const auto selector = Selector::typeSubtype(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::DEFENSE));
		const auto existing = getAllBonuses(selector);
		BonusList defence = *existing;
		const auto oldValue = defence.totalValue();
		auto percentage = std::make_shared<Bonus>(BonusDuration::STACK_GETS_TURN, BonusType::PRIMARY_SKILL,
			BonusSource::OTHER, 20, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE), BonusValueType::PERCENT_TO_ALL);
		auto extra = std::make_shared<Bonus>(BonusDuration::STACK_GETS_TURN, BonusType::PRIMARY_SKILL,
			BonusSource::OTHER, valOfBonuses(BonusType::DEFENSIVE_STANCE), BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE), BonusValueType::ADDITIVE_VALUE);
		defence.push_back(percentage);
		defence.push_back(extra);
		if(defence.totalValue() == oldValue)
			stance.push_back(std::make_shared<Bonus>(BonusDuration::STACK_GETS_TURN, BonusType::PRIMARY_SKILL,
				BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE), BonusValueType::ADDITIVE_VALUE));
		else
			stance.push_back(percentage);
		stance.push_back(extra);
		stance.push_back(std::make_shared<Bonus>(BonusDuration::STACK_GETS_TURN, BonusType::UNIT_DEFENDING, BonusSource::OTHER, 0, BonusSourceID()));
		++version;
	}
};
}
