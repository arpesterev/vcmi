/*
 * EscapeBehavior.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include "../Goals/CGoal.h"

namespace NK2AI
{
// A pure scoring seam: the planner supplies real path and threat-map evidence.
struct EscapePathCandidate
{
	bool currentTileThreatened = false;
	bool sameDay = false;
	bool changedTile = false;
	bool singleHero = false;
	bool visibleDestination = false;
	bool unblocked = false;
	bool battleFree = false;
	int safeTurns = 0; // Earliest known fatal enemy arrival, capped at 3.
	float movementCost = 0.0f;
};

struct EscapePathEvaluation
{
	bool accepted = false;
	float score = 0.0f;
};

EscapePathEvaluation evaluateEscapePathCandidate(const EscapePathCandidate & candidate);

namespace Goals
{
class EscapeBehavior : public CGoal<EscapeBehavior>
{
public:
	EscapeBehavior() : CGoal(ESCAPE_BEHAVIOR) {}
	TGoalVec decompose(const Nullkiller * aiNk) const override;
	std::string toString() const override;
	bool operator==(const EscapeBehavior &) const override { return true; }
};
}
}
