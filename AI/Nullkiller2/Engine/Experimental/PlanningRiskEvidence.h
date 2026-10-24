/* GPL-2.0-or-later; see license.txt. Experimental policy, not calibrated battle odds. */
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace NK2AI::experimental
{
enum class RiskRecommendation { UNKNOWN, FAVORABLE, LIKELY_DEFEAT };
struct RiskAssessment
{
	RiskRecommendation recommendation = RiskRecommendation::UNKNOWN;
	double adverseUpper = 1.0;
	double defeatLower = 0.0;
	unsigned samples = 0;
};

// Caller must freeze its loss tolerance and variant count before drawing,
// require favorable evidence separately for EVERY eligible variant, and enforce
// deadline/batch admission. nextBatchSize is advisory, not a worker scheduler.
// This describes independent draws of ONE supported simulator policy/context,
// not probability of winning an arbitrary real battle. Model bias must be
// checked independently against held-out server battles before enabling use.
class PlanningRiskEvidence
{
	struct Outcome { bool won; double loss; };
	std::vector<Outcome> outcomes;
	bool unsupported = false;

	static double upperBinomial(unsigned failures, unsigned trials, double alpha)
	{
		if(failures == trials) return 1.0;
		if(failures == 0) return -std::expm1(std::log(alpha) / trials);
		if(failures + 1 == trials) return std::exp(std::log1p(-alpha) / trials);
		std::vector<double> logCombination(failures + 1, 0.0);
		for(unsigned k = 1; k <= failures; ++k)
			logCombination[k] = logCombination[k - 1] + std::log(double(trials - k + 1)) - std::log(double(k));
		double low = 0.0, high = 1.0;
		const double logAlpha = std::log(alpha);
		for(unsigned iteration = 0; iteration < 64; ++iteration)
		{
			const double p = (low + high) / 2.0;
			const double logP = std::log(p), logQ = std::log1p(-p);
			const unsigned mode = std::min(failures, static_cast<unsigned>((trials + 1) * p));
			const double peak = logCombination[mode] + mode * logP + (trials - mode) * logQ;
			double scaledSum = 0.0;
			for(unsigned k = 0; k <= failures; ++k)
				scaledSum += std::exp(logCombination[k] + k * logP + (trials - k) * logQ - peak);
			const double logCdf = peak + std::log(scaledSum);
			if(logCdf > logAlpha) low = p; else high = p;
		}
		return high;
	}
public:
	static constexpr std::array<unsigned, 4> CHECKPOINTS{16, 32, 64, 128};
	// Explicit conservative gameplay choice, NOT derived from creature stats
	// or a claim of calibrated real-world odds. Strategic loss tolerance is
	// supplied by existing planner policy, never replaced with a flat 35%.
	static constexpr double MAX_ADVERSE_FREQUENCY = 0.05;
	static constexpr double FAMILY_ERROR = 0.05;

	bool append(bool attackerWon, double lostArmyFraction)
	{
		if(outcomes.size() >= CHECKPOINTS.back()) return false;
		if(!std::isfinite(lostArmyFraction) || lostArmyFraction < 0.0 || lostArmyFraction > 1.0)
		{
			unsupported = true;
			return false;
		}
		outcomes.push_back({attackerWon, attackerWon ? lostArmyFraction : 1.0});
		return true;
	}
	void markUnsupported() { unsupported = true; }
	unsigned nextBatchSize(bool deadlineExpired, unsigned remainingDrawBudget) const
	{
		if(deadlineExpired || unsupported) return 0;
		for(const auto checkpoint : CHECKPOINTS)
			if(outcomes.size() < checkpoint)
			{
				const auto required = checkpoint - static_cast<unsigned>(outcomes.size());
				return required <= remainingDrawBudget ? required : 0;
			}
		return 0;
	}
	RiskAssessment assess(double contextualLossTolerance, unsigned visibleVariants, bool permitDefeatRecommendation, double maxAdverseFrequency = MAX_ADVERSE_FREQUENCY) const
	{
		RiskAssessment result;
		result.samples = static_cast<unsigned>(outcomes.size());
		if(unsupported || !std::isfinite(contextualLossTolerance) || contextualLossTolerance < 0.0
			|| contextualLossTolerance > 1.0 || visibleVariants < 1 || visibleVariants > 3
			|| !std::isfinite(maxAdverseFrequency) || maxAdverseFrequency < 0.0 || maxAdverseFrequency > 1.0
			|| std::find(CHECKPOINTS.begin(), CHECKPOINTS.end(), result.samples) == CHECKPOINTS.end())
			return result;
		unsigned adverse = 0, defeats = 0;
		for(const auto & outcome : outcomes)
		{
			defeats += !outcome.won;
			adverse += !outcome.won || outcome.loss > contextualLossTolerance;
		}
		// Union-bound spending across every permitted checkpoint, variant and
		// the two reported bounds avoids stopping on a lucky repeated peek.
		const double alpha = FAMILY_ERROR / (CHECKPOINTS.size() * visibleVariants * 2);
		result.adverseUpper = upperBinomial(adverse, result.samples, alpha);
		result.defeatLower = 1.0 - upperBinomial(result.samples - defeats, result.samples, alpha);
		if(result.adverseUpper <= maxAdverseFrequency)
			result.recommendation = RiskRecommendation::FAVORABLE;
		else if(permitDefeatRecommendation && result.defeatLower > 0.5)
			result.recommendation = RiskRecommendation::LIKELY_DEFEAT;
		return result;
	}
};
}
