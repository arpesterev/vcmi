/*
 * nn_output_validation.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#pragma once

#include <cmath>
#include <stdexcept>
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

// V15 emits a critic value in addition to the action head. Although the
// value does not determine the selected action, non-finite ONNX output is
// evidence of a broken model and must not be accepted as a valid prediction.
inline void validateNNValueEstimate(float estimate)
{
	if(!std::isfinite(estimate))
		throw std::runtime_error("MMAI: non-finite neural value estimate");
}
}
