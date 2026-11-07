// SPDX-License-Identifier: CC0-1.0
#include "MoveOrder.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace HoMM3Forecast
{
namespace
{
void checkSide(Side side)
{
	if(side != Side::ATTACKER && side != Side::DEFENDER)
		throw std::invalid_argument("HoMM3 move order: invalid side");
}

Side opposite(Side side)
{
	return side == Side::ATTACKER ? Side::DEFENDER : Side::ATTACKER;
}

void validateIdentity(std::span<const Candidate> candidates)
{
	if(candidates.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
		throw std::invalid_argument("HoMM3 move order: rank exceeds source integer range");
	for(std::size_t i = 0; i < candidates.size(); ++i)
	{
		const auto & current = candidates[i];
		checkSide(current.owningSide);
		if(current.sourceIndex < 0)
			throw std::invalid_argument("HoMM3 move order: negative source index");
		for(std::size_t j = 0; j < i; ++j)
		{
			const auto & earlier = candidates[j];
			if(current.id == earlier.id)
				throw std::invalid_argument("HoMM3 move order: duplicate unit ID");
			if(current.owningSide == earlier.owningSide && current.sourceIndex == earlier.sourceIndex)
				throw std::invalid_argument("HoMM3 move order: duplicate owning-side source index");
		}
	}
}

std::int32_t moveKey(const OrderUnit & unit, bool secondPhase)
{
	if(unit.role == OrderRole::FIRST_AID_TENT || unit.role == OrderRole::AMMUNITION_CART)
		return -100000;
	if(unit.sourceSpell62Turns > 1 || unit.sourceSpell70Turns > 1)
		return -10000;
	if(unit.done || unit.sourceSpell62Turns != 0 || unit.sourceSpell70Turns != 0 || unit.sourceSpell74Turns != 0)
		return unit.effectiveSpeed - 1000;
	if(unit.waiting || secondPhase)
		return -unit.effectiveSpeed;
	return unit.effectiveSpeed;
}
}

std::vector<Candidate> makeCandidates(const MoveOrderInput & input)
{
	checkSide(input.actingSide);
	std::vector<Candidate> identities;
	identities.reserve(input.units.size());
	for(const auto & unit : input.units)
	{
		if(unit.effectiveSpeed < 0)
			throw std::invalid_argument("HoMM3 move order: negative effective speed is unsupported");
		switch(unit.role)
		{
			case OrderRole::ORDINARY:
			case OrderRole::ARROW_TOWER:
			case OrderRole::FIRST_AID_TENT:
			case OrderRole::AMMUNITION_CART:
				break;
			default:
				throw std::invalid_argument("HoMM3 move order: invalid order role");
		}
		identities.push_back({unit.id, unit.owningSide, unit.sourceIndex, moveKey(unit, input.secondPhase)});
	}
	validateIdentity(identities);
	std::vector<Candidate> result;
	result.reserve(identities.size());
	for(std::size_t i = 0; i < input.units.size(); ++i)
		if(!input.units[i].immobilized && input.units[i].role != OrderRole::ARROW_TOWER)
			result.push_back(identities[i]);
	return result;
}

bool sourceMovesBefore(const Candidate & lhs, const Candidate & rhs)
{
	if(lhs.key != rhs.key)
		return lhs.key > rhs.key;
	return lhs.sourceIndex < rhs.sourceIndex;
}

std::vector<OrderedUnit> alternateSides(std::span<const Candidate> sorted, Side actingSide)
{
	checkSide(actingSide);
	validateIdentity(sorted);
	if(!std::is_sorted(sorted.begin(), sorted.end(), sourceMovesBefore))
		throw std::invalid_argument("HoMM3 move order: permutation violates source comparator");
	std::vector<Candidate> order(sorted.begin(), sorted.end());
	Side desired = actingSide;
	for(std::size_t i = 0; i < order.size(); ++i)
	{
		if(order[i].owningSide != desired)
		{
			for(std::size_t j = i + 1; j < order.size() && order[j].key == order[i].key; ++j)
			{
				if(order[j].owningSide == desired)
				{
					std::swap(order[i], order[j]);
					break;
				}
			}
		}
		desired = opposite(order[i].owningSide);
	}
	std::vector<OrderedUnit> result;
	result.reserve(order.size());
	for(std::size_t i = 0; i < order.size(); ++i)
		result.push_back({order[i].id, static_cast<std::int32_t>(order.size() - i)});
	return result;
}

ProjectedMoveOrder projectMoveOrder(const MoveOrderInput & input)
{
	auto order = makeCandidates(input);
	std::sort(order.begin(), order.end(), [](const Candidate & lhs, const Candidate & rhs)
	{
		if(sourceMovesBefore(lhs, rhs))
			return true;
		if(sourceMovesBefore(rhs, lhs))
			return false;
		return lhs.owningSide < rhs.owningSide;
	});
	bool hasTies = false;
	for(std::size_t i = 1; i < order.size(); ++i)
		if(!sourceMovesBefore(order[i - 1], order[i]) && !sourceMovesBefore(order[i], order[i - 1]))
			hasTies = true;
	return {alternateSides(order, input.actingSide), hasTies};
}

std::vector<OrderedUnit> orderWithComparatorPermutation(const MoveOrderInput & input,
	std::span<const UnitId> comparatorOrder)
{
	const auto candidates = makeCandidates(input);
	if(comparatorOrder.size() != candidates.size())
		throw std::invalid_argument("HoMM3 move order: incomplete eligible-unit permutation");
	std::vector<Candidate> sorted;
	sorted.reserve(candidates.size());
	for(const UnitId id : comparatorOrder)
	{
		const auto found = std::find_if(candidates.begin(), candidates.end(), [id](const Candidate & candidate)
		{
			return candidate.id == id;
		});
		if(found == candidates.end())
			throw std::invalid_argument("HoMM3 move order: unknown or excluded unit in permutation");
		sorted.push_back(*found);
	}
	return alternateSides(sorted, input.actingSide);
}
}
