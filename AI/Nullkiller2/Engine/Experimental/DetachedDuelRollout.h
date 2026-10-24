/*
 * DetachedDuelRollout.h, part of VCMI engine
 * GPL-2.0-or-later; see license.txt.
 * Experimental: not connected to adventure decisions until reference validation.
 */
#pragma once
#include "../../../../lib/battle/BattleSide.h"

VCMI_LIB_NAMESPACE_BEGIN
class CBattleInfoCallback;
VCMI_LIB_NAMESPACE_END

namespace NK2AI
{
namespace experimental
{
enum class DuelStatus { UNSUPPORTED, BUDGET_EXHAUSTED, FINISHED };

struct DuelOutcome
{
	DuelStatus status = DuelStatus::UNSUPPORTED;
	BattleSide winner = BattleSide::NONE;
	int attackerSurvivors = 0;
	int defenderSurvivors = 0;
	unsigned actions = 0;
	const char * unsupportedReason = "none";
	int unsupportedBonusType = -1;
};

// A fixed-policy forecast, NOT a calibrated win probability or guaranteed bound.
// Reads a fully initialized initial battle snapshot, never initializes a battle
// on live adventure armies. Uses engine damage, health, ammunition, retaliation,
// initiative and reachability; rejects unsupported state rather than inventing it.
// favourableToAttacker selects damage endpoints, not bonuses or changed stats.
DuelOutcome runDetachedDuel(const CBattleInfoCallback & initial, bool favourableToAttacker);
}
}
