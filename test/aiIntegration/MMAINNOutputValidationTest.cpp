/*
 * MMAINNOutputValidationTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later.
 */
#include "StdInc.h"

#ifdef ENABLE_MMAI
#include "../../AI/MMAI/BAI/nn_output_validation.h"

#include <limits>

TEST(MMAINNOutputValidation, RejectsInvalidOnnxOutputBeforeGreedyOrSampling)
{
	using MMAI::BAI::validateNNActionDistribution;
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();

	EXPECT_THROW(validateNNActionDistribution({}, 1.0f), std::runtime_error);
	EXPECT_THROW(validateNNActionDistribution({0.0f, 0.0f}, 0.0f), std::runtime_error);
	EXPECT_THROW(validateNNActionDistribution({0.0f, nan}, 0.0f), std::runtime_error);
	EXPECT_THROW(validateNNActionDistribution({1.0f, inf}, 1.0f), std::runtime_error);
	EXPECT_THROW(validateNNActionDistribution({0.3f, -0.1f}, 1.0f), std::runtime_error);
	EXPECT_THROW(validateNNActionDistribution({0.3f, 0.7f}, nan), std::runtime_error);
	EXPECT_THROW(validateNNActionDistribution({0.3f, 0.7f}, inf), std::runtime_error);
	EXPECT_THROW(validateNNActionDistribution({0.3f, 0.7f}, -1.0f), std::runtime_error);
}

TEST(MMAINNOutputValidation, PreservesFiniteOnnxActionDistribution)
{
	using MMAI::BAI::validateNNActionDistribution;
	// The ONNX action masks are already reflected in the model output.
	// Only sanity-check here; do not change model probabilities or actions.
	EXPECT_NO_THROW(validateNNActionDistribution({0.0f, 1.0f}, 0.0f));
	EXPECT_NO_THROW(validateNNActionDistribution({0.2f, 0.8f}, 1.0f));
	EXPECT_NO_THROW(validateNNActionDistribution({0.2f, 0.8f}, 0.4f));
}
#endif
