// SPDX-License-Identifier: CC0-1.0
#include "OwnershipPrefix.h"
#include <algorithm>
#include <stdexcept>
#include <limits>

namespace HoMM3Forecast
{
OwnershipPrefix::OwnershipPrefix(const ForecastInput & input, Side requestedSide)
	: units(input.units), expectedDamage(input.units.size(), 0), side(requestedSide)
{
	if(side != Side::ATTACKER && side != Side::DEFENDER)
		throw std::invalid_argument("HoMM3 prefix: invalid requested side");
	MoveOrderInput ordering;
	ordering.actingSide = input.actingSide;
	ordering.secondPhase = input.secondPhase;
	for(const auto & unit : units)
	{
		if(unit.controllingSide != Side::ATTACKER && unit.controllingSide != Side::DEFENDER)
			throw std::invalid_argument("HoMM3 prefix: invalid controller side");
		if(unit.liveHealth < 0 || unit.liveHealth > std::numeric_limits<std::int32_t>::max())
			throw std::invalid_argument("HoMM3 prefix: live health outside source int32 domain");
		ordering.units.push_back(unit.order);
	}
	order = projectMoveOrder(ordering);
}

std::size_t OwnershipPrefix::indexOf(UnitId id) const
{
	const auto found = std::find_if(units.begin(), units.end(), [id](const ForecastUnit & unit)
	{
		return unit.order.id == id;
	});
	if(found == units.end())
		throw std::invalid_argument("HoMM3 prefix: unknown unit ID");
	return static_cast<std::size_t>(found - units.begin());
}

std::int64_t OwnershipPrefix::projectedHealth(UnitId id) const
{
	const auto index = indexOf(id);
	return units[index].liveHealth - expectedDamage[index];
}

void OwnershipPrefix::addExpectedDamage(UnitId id, std::int64_t damage)
{
	if(damage < 0)
		throw std::invalid_argument("HoMM3 prefix: negative projected damage");
	const auto index = indexOf(id);
	if(damage > std::numeric_limits<std::int32_t>::max() - expectedDamage[index])
		throw std::invalid_argument("HoMM3 prefix: source expected-damage addition would overflow");
	expectedDamage[index] = std::min(expectedDamage[index] + damage, units[index].liveHealth);
}

std::optional<UnitId> OwnershipPrefix::next()
{
	if(finished)
		return std::nullopt;
	while(cursor < order.units.size())
	{
		const UnitId id = order.units[cursor].id;
		const auto & unit = units[indexOf(id)];
		const auto & state = unit.order;
		if(state.immobilized || state.sourceSpell62Turns != 0 || state.sourceSpell70Turns != 0
			|| state.sourceSpell74Turns != 0 || state.role == OrderRole::FIRST_AID_TENT
			|| state.role == OrderRole::AMMUNITION_CART || unit.berserk || unit.catapult
			|| projectedHealth(id) == 0)
		{
			++cursor;
			continue;
		}
		if(unit.controllingSide != side)
		{
			boundary = id;
			finished = true;
			return std::nullopt;
		}
		boundary.reset();
		++cursor;
		return id;
	}
	boundary.reset();
	finished = true;
	return std::nullopt;
}
}
