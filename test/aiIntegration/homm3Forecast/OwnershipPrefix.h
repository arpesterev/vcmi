// SPDX-License-Identifier: CC0-1.0
#pragma once
#include "MoveOrder.h"
#include <optional>

namespace HoMM3Forecast
{
struct ForecastUnit
{
	OrderUnit order;
	Side controllingSide = Side::ATTACKER;
	bool berserk = false;
	bool catapult = false;
	std::int64_t liveHealth = 0;
};

struct ForecastInput
{
	std::vector<ForecastUnit> units;
	Side actingSide = Side::ATTACKER;
	bool secondPhase = false;
};

/// Source simulateActions control flow; attack choice/effects are supplied by the caller.
class OwnershipPrefix
{
	std::vector<ForecastUnit> units;
	ProjectedMoveOrder order;
	std::vector<std::int64_t> expectedDamage;
	Side side;
	std::size_t cursor = 0;
	bool finished = false;
	std::optional<UnitId> boundary;
	std::size_t indexOf(UnitId id) const;
public:
	explicit OwnershipPrefix(const ForecastInput & input, Side requestedSide);
	std::optional<UnitId> next();
	void addExpectedDamage(UnitId id, std::int64_t damage);
	std::int64_t projectedHealth(UnitId id) const;
	std::size_t stoppedAt() const { return cursor; }
	std::optional<UnitId> opposingBoundary() const { return boundary; }
	const ProjectedMoveOrder & moveOrder() const { return order; }
};
}
