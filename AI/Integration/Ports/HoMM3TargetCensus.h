/*
 * Source: sushi-shi/homm3-decomp@e12906accf96c2662b1abadbc49ddc111991bf21,
 * src/army.cpp::considerAttack and getAITargetTime (CC0-1.0).
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace AIIntegration::Ports::HoMM3
{
struct SourceTargetCandidate
{
	int32_t rosterIndex;
	std::optional<int32_t> exchangeValue;
	std::optional<int32_t> attackDistance;
};

struct SourceTargetDecision
{
	int32_t rosterIndex;
	int32_t exchangeValue;
	int32_t attackDistance;
	int32_t attackTime;
};

struct SourceTargetCensus
{
	std::optional<SourceTargetDecision> target;
	uint32_t possibleTargets = 0;
};

// Input is the complete, source-filtered candidate stream in original roster order.
// Unknown inputs return nullopt; a known empty stream returns a census without a target.
// Distances and exchange values must come from source-equivalent producers, not native estimates.
inline std::optional<SourceTargetCensus> reduceSourceTargetCensus(int32_t speed, bool shooting,
	const std::optional<std::vector<SourceTargetCandidate>> & candidates)
{
	if(speed < 0 || !candidates)
		return std::nullopt;
	SourceTargetCensus result;
	uint32_t seen = 0;
	for(const auto & candidate : *candidates)
	{
		// Exclude inputs whose original signed 32-bit shift or ceil addition is undefined.
		if(candidate.rosterIndex < 0 || candidate.rosterIndex > 30
			|| !candidate.exchangeValue || !candidate.attackDistance || *candidate.attackDistance < 0)
			return std::nullopt;
		const auto bit = uint32_t{1} << candidate.rosterIndex;
		if(seen & bit)
			return std::nullopt;
		seen |= bit;
		const int64_t numerator = int64_t{*candidate.attackDistance} + speed - 1;
		if(speed != 0 && int64_t{*candidate.attackDistance} + speed > std::numeric_limits<int32_t>::max())
			return std::nullopt;
		const int32_t turns = speed == 0 ? 1 : static_cast<int32_t>(std::max<int64_t>(1, numerator / speed));
		if(turns == 1)
			result.possibleTargets |= bit;
		if(result.target && (turns > result.target->attackTime
			|| (turns == result.target->attackTime && *candidate.exchangeValue <= result.target->exchangeValue)))
			continue;
		result.target = SourceTargetDecision{candidate.rosterIndex, *candidate.exchangeValue,
			*candidate.attackDistance, shooting ? 1 : turns};
	}
	return result;
}
}
