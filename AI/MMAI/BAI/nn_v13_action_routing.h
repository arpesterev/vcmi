/*
 * nn_v13_action_routing.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#pragma once

#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <vector>

namespace MMAI::BAI
{
// V13's action table has three levels: primary action, destination/target
// hex, and (only for AMOVE) attack hex. WAIT uses neither hex head.
// Unused ONNX rows may legitimately contain only zero probabilities.
// Sampling them would reject a legal action and advance the model RNG.
//
// SampleFn is the production categorical sampler (or its deterministic
// greedy mode). This helper neither invents actions nor repairs bad logits:
// relevant branches are still validated by categorical and the V13 masks.
template<typename SampleFn>
auto sampleV13ActionPath(
	const std::vector<float> & mainProbabilities,
	const std::vector<std::vector<float>> & hex1Probabilities,
	const std::vector<std::vector<float>> & hex2Probabilities,
	SampleFn sample)
{
	const auto action = sample(mainProbabilities);
	using Sample = std::remove_cv_t<decltype(action)>;
	const Sample unused{0, 1.0, 1.0}; // neutral probability/confidence for absent heads
	auto firstHex = unused;
	auto secondHex = unused;

	if(action.index < 0 || static_cast<size_t>(action.index) >= hex1Probabilities.size())
		throw std::out_of_range("MMAI V13: primary action index outside hex1 action table");

	if(action.index != 0) // MOVE, AMOVE, SHOOT need their first hex
	{
		firstHex = sample(hex1Probabilities.at(action.index));
		if(action.index == 2) // AMOVE additionally needs its second hex
			secondHex = sample(hex2Probabilities.at(firstHex.index));
	}

	return std::tuple<Sample, Sample, Sample>{action, firstHex, secondHex};
}
}
