// SPDX-License-Identifier: CC0-1.0
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace HoMM3Forecast
{
using UnitId = std::uint32_t;

enum class Side : std::int32_t { ATTACKER, DEFENDER };
enum class OrderRole { ORDINARY, ARROW_TOWER, FIRST_AID_TENT, AMMUNITION_CART };

struct OrderUnit
{
	UnitId id = 0;
	Side owningSide = Side::ATTACKER;
	std::int32_t sourceIndex = 0;
	std::int32_t effectiveSpeed = 0;
	OrderRole role = OrderRole::ORDINARY;
	bool immobilized = false;
	bool done = false;
	bool waiting = false;
	std::int32_t sourceSpell62Turns = 0;
	std::int32_t sourceSpell70Turns = 0;
	std::int32_t sourceSpell74Turns = 0;
	bool operator==(const OrderUnit &) const = default;
};

struct MoveOrderInput
{
	std::vector<OrderUnit> units;
	Side actingSide = Side::ATTACKER;
	bool secondPhase = false;
	bool operator==(const MoveOrderInput &) const = default;
};

struct Candidate
{
	UnitId id;
	Side owningSide;
	std::int32_t sourceIndex;
	std::int32_t key;
	bool operator==(const Candidate &) const = default;
};

struct OrderedUnit
{
	UnitId id;
	std::int32_t expectedMoveOrder;
	bool operator==(const OrderedUnit &) const = default;
};

struct ProjectedMoveOrder
{
	std::vector<OrderedUnit> units;
	// True identifies a source comparator tie, even when all tie orders converge.
	bool containsComparatorTies;
};

/// Returns eligible units and exact source keys; leaves input unchanged.
std::vector<Candidate> makeCandidates(const MoveOrderInput & input);

/// The source comparator intentionally has no side/ID tie-break.
bool sourceMovesBefore(const Candidate & lhs, const Candidate & rhs);

/// Applies original side swaps to a validated, comparator-sorted permutation.
std::vector<OrderedUnit> alternateSides(std::span<const Candidate> sorted, Side actingSide);

/// Uses owning side ascending only within source-equivalent ties.
ProjectedMoveOrder projectMoveOrder(const MoveOrderInput & input);

/// Evaluates one caller-supplied source-comparator-valid permutation of eligible IDs.
std::vector<OrderedUnit> orderWithComparatorPermutation(const MoveOrderInput & input,
	std::span<const UnitId> comparatorOrder);
}

// SPDX-License-Identifier: CC0-1.0

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace HoMM3Forecast
{
namespace Detail
{
inline void checkSide(Side side)
{
	if(side != Side::ATTACKER && side != Side::DEFENDER)
		throw std::invalid_argument("HoMM3 move order: invalid side");
}

inline Side opposite(Side side)
{
	return side == Side::ATTACKER ? Side::DEFENDER : Side::ATTACKER;
}

inline void validateIdentity(std::span<const Candidate> candidates)
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

inline std::int32_t moveKey(const OrderUnit & unit, bool secondPhase)
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

inline std::vector<Candidate> makeCandidates(const MoveOrderInput & input)
{
	Detail::checkSide(input.actingSide);
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
		identities.push_back({unit.id, unit.owningSide, unit.sourceIndex, Detail::moveKey(unit, input.secondPhase)});
	}
	Detail::validateIdentity(identities);
	std::vector<Candidate> result;
	result.reserve(identities.size());
	for(std::size_t i = 0; i < input.units.size(); ++i)
		if(!input.units[i].immobilized && input.units[i].role != OrderRole::ARROW_TOWER)
			result.push_back(identities[i]);
	return result;
}

inline bool sourceMovesBefore(const Candidate & lhs, const Candidate & rhs)
{
	if(lhs.key != rhs.key)
		return lhs.key > rhs.key;
	return lhs.sourceIndex < rhs.sourceIndex;
}

inline std::vector<OrderedUnit> alternateSides(std::span<const Candidate> sorted, Side actingSide)
{
	Detail::checkSide(actingSide);
	Detail::validateIdentity(sorted);
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
		desired = Detail::opposite(order[i].owningSide);
	}
	std::vector<OrderedUnit> result;
	result.reserve(order.size());
	for(std::size_t i = 0; i < order.size(); ++i)
		result.push_back({order[i].id, static_cast<std::int32_t>(order.size() - i)});
	return result;
}

inline ProjectedMoveOrder projectMoveOrder(const MoveOrderInput & input)
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

inline std::vector<OrderedUnit> orderWithComparatorPermutation(const MoveOrderInput & input,
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
