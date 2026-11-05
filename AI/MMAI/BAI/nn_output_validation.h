/*
 * nn_output_validation.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace MMAI::BAI
{
// Shared by V13 and V15 before either greedy argmax or stochastic sampling.
// A malformed ONNX output must never become an apparently legal combat action
// because comparisons with NaN fail, nor reach a random distribution as Inf.
inline void validateNNActionDistribution(const std::vector<float> & probabilities, float temperature)
{
	if(!std::isfinite(temperature) || temperature < 0.0f)
		throw std::runtime_error("MMAI: non-finite or negative neural sampling temperature");

	if(probabilities.empty())
		throw std::runtime_error("MMAI: neural action distribution is empty");

	bool hasPositiveProbability = false;
	for(const float probability : probabilities)
	{
		if(!std::isfinite(probability) || probability < 0.0f)
			throw std::runtime_error("MMAI: non-finite or negative neural action probability");
		hasPositiveProbability |= probability > 0.0f;
	}

	if(!hasPositiveProbability)
		throw std::runtime_error("MMAI: neural action distribution contains no positive probability");
}

// V13 and V15 observations are numerical ONNX inputs. An invalid battle
// attribute must not be silently promoted to a plausible model decision.
// Empty edge-attribute tensors are legitimate on sparse battle graphs.
inline void validateNNObservationValues(const std::vector<float> & values, const std::string & name)
{
	for(std::size_t i = 0; i < values.size(); ++i)
		if(!std::isfinite(values[i]))
			throw std::runtime_error("MMAI: non-finite neural observation in " + name
				+ " at index " + std::to_string(i));
}

// V15's dynamic graph may have no active creature-action nodes after
// battle-event invalidation. Reject the snapshot before invoking ONNX:
// zero-length action tensors are not a policy decision and must not be
// interpreted as a retreat, resampled, or routed into scripted BattleAI.
// Terminal observations are handled separately by ACTION_RESET.
inline void requireNNV15ActionCandidates(std::size_t activeActionCount)
{
	if(activeActionCount == 0)
		throw std::runtime_error("MMAI V15: observation has no active creature actions before ONNX inference");
}

// V15 action nodes are identified by int64 values in ONNX, but the battle
// interface returns a signed int action ID. Reject a malformed or oversized
// graph ID before narrowing it: truncation could otherwise dispatch a
// different, apparently legal creature action instead of the model's choice.
inline int checkedNNV15ActionId(std::int64_t actionId)
{
	if(actionId < 0 || actionId > std::numeric_limits<int>::max())
		throw std::runtime_error("MMAI V15: ONNX-selected action ID is outside the VCMI action range");
	return static_cast<int>(actionId);
}

// V15's "action" and "value" heads are scalar values (ONNX shape [] or
// [1]), not arbitrary tensors whose dimensions happen to multiply to one.
// The previous dimension-product check also allowed [-1, -1] and could
// overflow on malformed shapes before reading element zero.
inline void requireNNV15ScalarOutputShape(const std::vector<std::int64_t> & shape, const std::string & name)
{
	if(!shape.empty() && !(shape.size() == 1 && shape.front() == 1))
		throw std::runtime_error("MMAI V15: ONNX output " + name + " must have scalar or [1] shape");
}

// V15 emits a critic value in addition to the action head. Although the
// value does not determine the selected action, non-finite ONNX output is
// evidence of a broken model and must not be accepted as a valid prediction.
inline void validateNNValueEstimate(float estimate)
{
	if(!std::isfinite(estimate))
		throw std::runtime_error("MMAI: non-finite neural value estimate");
}
}
