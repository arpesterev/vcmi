/*
 * FHeroes2RoundLimiter.h, part of VCMI engine
 *
 * Pinned fheroes2 src/fheroes2/ai/ai_battle.cpp:
 * BattlePlanner::isLimitOfTurnsExceeded.
 * Copyright (C) fheroes2 contributors. GNU GPL v2.0 or later.
 * VCMI uses this state machine only for an AI attacker's legal retreat.
 */
#pragma once

#include <cstdint>

namespace AIIntegration::Ports::FHeroes2
{
/// Counts consecutive *completed* transitions between combat rounds with
/// unchanged casualties on BOTH sides. The first observed round establishes
/// the baseline; the 50th deathless transition exhausts the limit.
class DeathlessRoundLimiter
{
public:
	static constexpr uint32_t MAX_ROUNDS_WITHOUT_DEATHS = 50;

	bool observe(int32_t round, uint64_t attackerDeaths, uint64_t defenderDeaths)
	{
		if(round < 0)
			return false; // tactics phase is not a combat round
		if(round == lastRound)
			return remaining == 0; // a later stack activation cannot consume another round

		if(lastRound < 0 || round != lastRound + 1
			|| attackerDeaths != previousAttackerDeaths
			|| defenderDeaths != previousDefenderDeaths)
			remaining = MAX_ROUNDS_WITHOUT_DEATHS;
		else if(remaining > 0)
			--remaining;

		lastRound = round;
		previousAttackerDeaths = attackerDeaths;
		previousDefenderDeaths = defenderDeaths;
		return remaining == 0;
	}

	void reset()
	{
		lastRound = -1;
		remaining = MAX_ROUNDS_WITHOUT_DEATHS;
		previousAttackerDeaths = 0;
		previousDefenderDeaths = 0;
	}

	uint32_t remainingRounds() const { return remaining; }

private:
	int32_t lastRound = -1;
	uint32_t remaining = MAX_ROUNDS_WITHOUT_DEATHS;
	uint64_t previousAttackerDeaths = 0;
	uint64_t previousDefenderDeaths = 0;
};
}
