/*
 * nn_output_validation.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace MMAI::BAI
{
// Shared by V13 and V15 before either greedy argmax or stochastic sampling.
// A malformed ONNX output must never become an apparently legal combat action
// because comparisons with NaN fail, nor reach a random distribution as Inf.
// Fail at NN model construction rather than after the first server action
// when the config supplies a malformed sampling temperature. Greedy zero and
// all finite nonnegative temperatures are valid for both exported schemas.
inline void validateNNModelSamplingTemperature(float temperature)
{
	if(!std::isfinite(temperature) || temperature < 0.0f)
		throw std::runtime_error("MMAI: non-finite or negative neural sampling temperature");
}

inline void validateNNActionDistribution(const std::vector<float> & probabilities, float temperature)
{
	validateNNModelSamplingTemperature(temperature);

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

// V13/V15 tensors derive dimensions from live battle observations. Never
// multiply signed dimensions before validating them: overflow is undefined
// behavior, and sparse zero-edge graphs legitimately have a zero dimension.
// Preserve all valid ONNX tensor sizes and values without normalization.
inline void requireNNTensorElementCount(const std::string & name,
                                        const std::vector<std::int64_t> & shape,
                                        std::size_t elements)
{
	bool hasZeroDimension = false;
	for(const auto dimension : shape)
	{
		if(dimension < 0)
			throw std::runtime_error("MMAI: neural tensor '" + name + "' has a negative dimension");
		hasZeroDimension |= dimension == 0;
	}

	std::size_t expected = hasZeroDimension ? 0 : 1;
	if(!hasZeroDimension)
	{
		for(const auto dimension : shape)
		{
			const auto value = static_cast<std::uint64_t>(dimension);
			if(value > std::numeric_limits<std::size_t>::max() / expected)
				throw std::runtime_error("MMAI: neural tensor '" + name + "' element count overflows");
			expected *= static_cast<std::size_t>(value);
		}
	}

	if(expected != elements)
		throw std::runtime_error("MMAI: neural tensor '" + name + "' element count mismatch: shape has "
			+ std::to_string(expected) + " elements, data has " + std::to_string(elements));
}

// V15 graph nodes and edges must be flattened *before* ONNX allocation.
// Validate arithmetic before vector resize/reserve, span offsets, or
// narrowing to ONNX int32/int64 dimensions. Sparse zero-edge graphs are valid.
inline std::size_t checkedNNObservationProduct(const std::string & name, std::size_t count, std::size_t stride)
{
	if(stride != 0 && count > std::numeric_limits<std::size_t>::max() / stride)
		throw std::runtime_error("MMAI: neural observation '" + name + "' size product overflows");
	return count * stride;
}

inline std::size_t checkedNNObservationSum(const std::string & name, std::size_t count, std::size_t increment)
{
	if(count > std::numeric_limits<std::size_t>::max() - increment)
		throw std::runtime_error("MMAI: neural observation '" + name + "' size sum overflows");
	return count + increment;
}

inline int checkedNNObservationIntSize(const std::string & name, std::size_t count)
{
	if(count > static_cast<std::size_t>(std::numeric_limits<int>::max()))
		throw std::runtime_error("MMAI: neural observation '" + name + "' exceeds ONNX int32 length");
	return static_cast<int>(count);
}

inline std::int64_t checkedNNObservationAxis(const std::string & name, std::size_t count)
{
	if(count > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
		throw std::runtime_error("MMAI: neural observation '" + name + "' exceeds ONNX int64 axis");
	return static_cast<std::int64_t>(count);
}

// V13 flattens each link type into a shared edge-index tensor while
// storing each type's length in int32. Reject an oversized individual link
// or aggregate BEFORE insert/reserve, not after a narrowing cast. This is
// only validation: valid edge order and ONNX observations are unchanged.
inline int checkedNNV13LinkLength(std::size_t existingEdges, std::size_t newEdges)
{
	const auto length = checkedNNObservationIntSize("V13 link length", newEdges);
	const auto totalEdges = checkedNNObservationSum("V13 edges", existingEdges, newEdges);
	(void)checkedNNObservationAxis("V13 edges", totalEdges);
	(void)checkedNNObservationProduct("V13 edge indices", totalEdges, 2u);
	return length;
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

