/* GPL-2.0-or-later; see license.txt. Experimental, not production-wired. */
#pragma once
#include "../../../../lib/battle/BattleSide.h"
#include "../../../../lib/constants/EntityIdentifiers.h"
VCMI_LIB_NAMESPACE_BEGIN
class CBattleInfoCallback;
class IGameInfoCallback;
VCMI_LIB_NAMESPACE_END
namespace NK2AI::experimental
{
enum class SampledBattleStatus { UNSUPPORTED, BUDGET_EXHAUSTED, FINISHED };
struct SampledStackOutcome
{
	uint32_t unit;
	CreatureID creature;
	BattleSide side;
	int initialCount;
	int survivors;
};
struct SampledBattleOutcome
{
	SampledBattleStatus status = SampledBattleStatus::UNSUPPORTED;
	BattleSide winner = BattleSide::NONE;
	std::array<int64_t, 2> remainingHealth{};
	std::array<int64_t, 2> remainingCreatures{};
	std::array<double, 2> initialArmyValue{};
	std::array<double, 2> remainingArmyValue{};
	std::vector<SampledStackOutcome> stacks;
	unsigned actions = 0;
	const char * unsupportedReason = "none";
	int unsupportedBonusType = -1;
};
// Both inputs must be owned immutable snapshot data for the entire call.
// Samples use fresh independent bias history, not authoritative game RNG state.
// A small collection of policy outcomes is NOT a calibrated win probability.
SampledBattleOutcome runSampledBattle(const CBattleInfoCallback & initial,
	const IGameInfoCallback & frozenSettings, int seed, std::vector<std::string> * trace = nullptr);
}
