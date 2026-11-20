/*
 * Remaining-ammunition classification for the FHeroes2 port.
 * GPL-2.0-or-later.
 */
#pragma once

#include "../../../lib/battle/CUnitState.h"

namespace AIIntegration
{
inline bool fheroes2IsArcher(const battle::CUnitState & unit)
{
	return unit.isShooter() && unit.shots.canUse(1);
}

inline bool fheroes2IsArcher(const battle::Unit & unit)
{
	if(!unit.isShooter())
		return false;
	// Native projections derive from CUnitState. Other Unit implementations
	// expose remaining ammunition through an owned, environment-aware copy.
	if(const auto * state = dynamic_cast<const battle::CUnitState *>(&unit))
		return state->shots.canUse(1);
	const auto state = unit.acquireState();
	return state->shots.canUse(1);
}

// Source isImmovable() means disabled by Blind or paralysis, not unable
// to change hexes. Native Bind stops movement but does not stop shooting.
inline bool fheroes2ContributesRangedPressure(const battle::CUnitState & unit)
{
	return unit.alive() && !unit.isCatapult() && fheroes2IsArcher(unit)
		&& !unit.hasBonusOfType(BonusType::NOT_ACTIVE);
}
}
