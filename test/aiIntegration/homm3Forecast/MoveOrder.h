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
