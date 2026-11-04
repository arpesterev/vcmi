/*
 * Differential checks against independently encoded decisions in
 * ihhub/fheroes2@0dbfe9f386f33c2500842f4a59cd284d7f03d541
 * src/fheroes2/ai/ai_battle.cpp, not an upstream binary or full-battle oracle.
 * GPL-2.0-or-later.
 */
#include "../AI/Integration/Ports/SourceTactics.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <stdexcept>
#include <vector>

namespace
{
using namespace AIIntegration::Ports::FHeroes2;

int upstreamDistantTarget(const std::vector<DistantTarget> & candidates)
{
	// Source meleeUnitOffense scans the non-evasive group first; on every
	// scan a tie replaces the earlier target. Only then does it scan all.
	for(int pass = 0; pass != 2; ++pass)
	{
		int winner = -1;
		double top = std::numeric_limits<double>::lowest();
		for(const auto & unit : candidates)
		{
			if(pass == 0 && !unit.cannotEvade)
				continue;
			const double priority = unit.threat / unit.pathDistance;
			if(priority < top)
				continue;
			top = priority;
			winner = unit.unitId;
		}
		if(winner >= 0)
			return winner;
	}
	return -1;
}

int vcmiDistantTarget(const std::vector<DistantTarget> & candidates)
{
	std::optional<DistantTarget> prior;
	for(const auto & candidate : candidates)
		if(!prior || preferDistantTarget(candidate, *prior))
			prior = candidate;
	return prior ? prior->unitId : -1;
}

bool upstreamOutcome(const MeleeOutcome & next, const MeleeOutcome & previous)
{
	// Independent control-flow expression of source IsOutcomeImproved:
	// immediate eligibility, position score, then attack value within 0.001.
	if(next.canAttackImmediately && !previous.canAttackImmediately)
		return true;
	if(next.canAttackImmediately != previous.canAttackImmediately)
		return false;
	if(next.positionValue > previous.positionValue)
		return true;
	return std::fabs(next.positionValue - previous.positionValue) < 0.001
		&& next.attackValue > previous.attackValue;
}

void require(bool result, const char * reason)
{
	if(!result)
		throw std::runtime_error(reason);
}
}

int main()
{
	std::mt19937_64 random(0xF2F2026);
	int64_t cases = 0;
	for(int n = 1; n <= 10; ++n)
		for(int trial = 0; trial < 2000; ++trial)
		{
			std::vector<DistantTarget> candidates;
			for(int i = 0; i < n; ++i)
			{
				candidates.push_back({static_cast<double>(random() % 41),
					static_cast<int>(random() % 12 + 1),
					(random() & 1) != 0, i});
			}
			require(upstreamDistantTarget(candidates) == vcmiDistantTarget(candidates),
				"Source distant target disagrees with VCMI predicate");
			++cases;
		}

	for(bool nextImmediate : {false, true})
		for(bool oldImmediate : {false, true})
			for(int nextPosition = -3; nextPosition <= 3; ++nextPosition)
				for(int oldPosition = -3; oldPosition <= 3; ++oldPosition)
					for(int nextAttack = -3; nextAttack <= 3; ++nextAttack)
						for(int oldAttack = -3; oldAttack <= 3; ++oldAttack)
						{
							const MeleeOutcome next{1, double(nextAttack), double(nextPosition), nextImmediate};
							const MeleeOutcome previous{2, double(oldAttack), double(oldPosition), oldImmediate};
							require(upstreamOutcome(next, previous) == isOutcomeImproved(next, previous),
								"Source melee outcome disagrees with VCMI comparator");
							++cases;
						}

	for(int friendly = 0; friendly <= 100; ++friendly)
		for(int hostile = 0; hostile <= 100; ++hostile)
		{
			const bool sourceRejects = friendly >= 3 * hostile;
			require(catastrophicFriendlyFire(friendly, hostile) == sourceRejects,
				"Source 3:1 area-shot friendly fire differs");
			++cases;
		}
	std::cout << "F2_SOURCE_PARITY_CHECKS=" << cases
		<< " source=0dbfe9f386f33c2500842f4a59cd284d7f03d541"
		<< " scope=distant_target,melee_outcome,area_shot_cutoff\n";
}
