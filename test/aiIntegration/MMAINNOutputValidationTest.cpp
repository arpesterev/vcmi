/*
 * MMAINNOutputValidationTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later.
 */
#include "StdInc.h"

#ifdef ENABLE_MMAI
#include "../../AI/MMAI/BAI/nn_output_validation.h"
#include "../../AI/MMAI/BAI/nn_model_metadata.h"
#include "../../AI/MMAI/BAI/nn_model_resolution.h"
#include "../../AI/MMAI/BAI/nn_v13_action_routing.h"

#include <algorithm>

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

TEST(MMAINNOutputValidation, V15RejectsInvalidValueHead)
{
	using MMAI::BAI::validateNNValueEstimate;
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();
	EXPECT_THROW(validateNNValueEstimate(nan), std::runtime_error);
	EXPECT_THROW(validateNNValueEstimate(inf), std::runtime_error);
	EXPECT_THROW(validateNNValueEstimate(-inf), std::runtime_error);
	EXPECT_NO_THROW(validateNNValueEstimate(-1.0f));
	EXPECT_NO_THROW(validateNNValueEstimate(0.0f));
	EXPECT_NO_THROW(validateNNValueEstimate(1.0f));
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
namespace
{
struct V13TestSample
{
	int index;
	double confidence;
	double prob;
};

V13TestSample v13TestGreedy(const std::vector<float> & probabilities)
{
	MMAI::BAI::validateNNActionDistribution(probabilities, 0.0f);
	const auto best = std::max_element(probabilities.begin(), probabilities.end());
	return {static_cast<int>(std::distance(probabilities.begin(), best)), 1.0, *best};
}
}

TEST(MMAINNOutputValidation, V13SamplesOnlyRequiredHierarchicalHeads)
{
	using MMAI::BAI::sampleV13ActionPath;
	// All unused conditional rows are deliberately invalid. Production V13
	// may not examine them and must not consume a random draw for them.
	std::vector<std::vector<float>> hex1(4, {0.0f, 0.0f, 0.0f});
	hex1[1] = {0.1f, 0.9f, 0.0f};
	hex1[2] = {0.1f, 0.0f, 0.9f};
	hex1[3] = {0.0f, 1.0f, 0.0f};
	std::vector<std::vector<float>> hex2(3, {0.0f, 0.0f, 0.0f});
	hex2[2] = {0.7f, 0.3f, 0.0f};

	const auto [wait, waitHex1, waitHex2] = sampleV13ActionPath(
		{1.0f, 0.0f, 0.0f, 0.0f}, hex1, hex2, v13TestGreedy);
	EXPECT_EQ(wait.index, 0);
	EXPECT_EQ(waitHex1.index, 0);
	EXPECT_EQ(waitHex2.index, 0);
	EXPECT_DOUBLE_EQ(waitHex1.confidence * waitHex2.confidence, 1.0);

	const auto [move, moveHex1, moveHex2] = sampleV13ActionPath(
		{0.0f, 1.0f, 0.0f, 0.0f}, hex1, hex2, v13TestGreedy);
	EXPECT_EQ(move.index, 1);
	EXPECT_EQ(moveHex1.index, 1);
	EXPECT_EQ(moveHex2.index, 0);

	const auto [amove, amoveHex1, amoveHex2] = sampleV13ActionPath(
		{0.0f, 0.0f, 1.0f, 0.0f}, hex1, hex2, v13TestGreedy);
	EXPECT_EQ(amove.index, 2);
	EXPECT_EQ(amoveHex1.index, 2);
	EXPECT_EQ(amoveHex2.index, 0);

	const auto [shoot, shootHex1, shootHex2] = sampleV13ActionPath(
		{0.0f, 0.0f, 0.0f, 1.0f}, hex1, hex2, v13TestGreedy);
	EXPECT_EQ(shoot.index, 3);
	EXPECT_EQ(shootHex1.index, 1);
	EXPECT_EQ(shootHex2.index, 0);
}

TEST(MMAINNOutputValidation, V13RejectsInvalidRequiredHead)
{
	using MMAI::BAI::sampleV13ActionPath;
	std::vector<std::vector<float>> hex1(4, {0.0f, 0.0f});
	std::vector<std::vector<float>> hex2(2, {0.0f, 0.0f});
	hex1[2] = {0.0f, 1.0f};

	// AMOVE cannot proceed with an empty second-hex distribution.
	EXPECT_THROW(sampleV13ActionPath({0.0f, 0.0f, 1.0f, 0.0f},
		hex1, hex2, v13TestGreedy), std::runtime_error);
	// MOVE cannot proceed with an empty first-hex distribution either.
	EXPECT_THROW(sampleV13ActionPath({0.0f, 1.0f, 0.0f, 0.0f},
		hex1, hex2, v13TestGreedy), std::runtime_error);
}

TEST(MMAINNOutputValidation, V13RejectsOutOfRangeActionTableEntries)
{
	using MMAI::BAI::checkedV13ActionTableEntry;
	using std::numeric_limits;
	// Exporter-compatible values, including the masked-action sentinel.
	EXPECT_EQ(checkedV13ActionTableEntry(-1), -1);
	EXPECT_EQ(checkedV13ActionTableEntry(0), 0);
	EXPECT_EQ(checkedV13ActionTableEntry(numeric_limits<int32_t>::min()), numeric_limits<int32_t>::min());
	EXPECT_EQ(checkedV13ActionTableEntry(numeric_limits<int32_t>::max()), numeric_limits<int32_t>::max());
	// These previously wrapped silently during JSON -> int32_t conversion.
	EXPECT_THROW(checkedV13ActionTableEntry(numeric_limits<int64_t>::max()), std::runtime_error);
	EXPECT_THROW(checkedV13ActionTableEntry(numeric_limits<int64_t>::min()), std::runtime_error);
}

TEST(MMAINNOutputValidation, V13RejectsJaggedActionTableMetadata)
{
	using MMAI::BAI::validateV13ActionTableShape;
	std::vector<std::vector<std::vector<int32_t>>> table(
		4, std::vector<std::vector<int32_t>>(165, std::vector<int32_t>(165, 0)));
	EXPECT_NO_THROW(validateV13ActionTableShape(table));

	// Previously only action 0 / hex 0 was checked on model load.
	// A missing row in another primary action or a short last hex2 row
	// survived loading and could break a later model-selected battle action.
	table[3].pop_back();
	EXPECT_THROW(validateV13ActionTableShape(table), std::runtime_error);
	table[3].emplace_back(165, 0);
	table[2][164].pop_back();
	EXPECT_THROW(validateV13ActionTableShape(table), std::runtime_error);
	table[2][164].push_back(0);
	EXPECT_NO_THROW(validateV13ActionTableShape(table));
}


TEST(MMAINNOutputValidation, RejectsInvalidModelSideMetadata)
{
	using MMAI::BAI::parseNNModelSide;
	using MMAI::Schema::Side;
	EXPECT_EQ(parseNNModelSide("0"), Side::LEFT);
	EXPECT_EQ(parseNNModelSide("1"), Side::RIGHT);
	EXPECT_EQ(parseNNModelSide("2"), Side::BOTH);

	// The model role must be exact, unsigned and within the known side enum.
	for(const char * value : {"", "1garbage", "2.0", "-1", "3", " 0", "+1", "0\n", "99999999999999999999"})
		EXPECT_THROW(parseNNModelSide(value), std::runtime_error);
}

TEST(MMAINNOutputValidation, RejectsInvalidModelVersionMetadata)
{
	using MMAI::BAI::parseNNModelVersion;
	EXPECT_EQ(parseNNModelVersion("13"), 13);
	EXPECT_EQ(parseNNModelVersion("15"), 15);

	// The factory used stoi, which accepted a valid prefix ("15bad")
	// and deferred unsupported-version errors until after the metadata parse.
	for(const char * value : {"", "15bad", "13.0", " 15", "+15", "-13",
		"0", "12", "14", "16", "99999999999999999999"})
		EXPECT_THROW(parseNNModelVersion(value), std::runtime_error);
}

TEST(MMAINNOutputValidation, StrictRejectsConfiguredNeuralLoadError)
{
	using MMAI::BAI::enforceStrictNNModelLoad;
	const std::runtime_error missing("ONNX resource not found");
	EXPECT_NO_THROW(enforceStrictNNModelLoad(false, "attacker.siege", "siege.onnx", missing));
	try
	{
		enforceStrictNNModelLoad(true, "attacker.siege", "siege.onnx", missing);
		FAIL() << "strict MMAI accepted broken configured ONNX";
	}
	catch(const std::runtime_error & e)
	{
		const std::string diagnostic(e.what());
		EXPECT_NE(diagnostic.find("attacker.siege"), std::string::npos);
		EXPECT_NE(diagnostic.find("siege.onnx"), std::string::npos);
		EXPECT_NE(diagnostic.find("ONNX resource not found"), std::string::npos);
		EXPECT_NE(diagnostic.find("refusing substitute"), std::string::npos);
	}
}

TEST(MMAINNOutputValidation, LegacyFallbackCannotPoisonStrictModelCache)
{
	using MMAI::BAI::mayCacheResolvedNNModel;
	const std::map<std::string, std::string> configured = {
		{"attacker", "attacker.onnx"},
		{"attacker.siege", "siege.onnx"}
	};
	// A broken explicit siege model may fall back in hybrid mode, but the
	// parent weights cannot be cached under the failed siege-model key.
	EXPECT_FALSE(mayCacheResolvedNNModel(configured, "attacker.siege", "attacker.onnx"));
	EXPECT_TRUE(mayCacheResolvedNNModel(configured, "attacker.siege", "siege.onnx"));
	EXPECT_TRUE(mayCacheResolvedNNModel(configured, "attacker", "attacker.onnx"));
	// Keys with no explicit override can intentionally inherit ancestors.
	EXPECT_TRUE(mayCacheResolvedNNModel(configured, "attacker.siege.optional", "attacker.onnx"));
}

TEST(MMAINNOutputValidation, StrictLiveBattleRejectsGymResetButAllowsGraphActionZero)
{
	using MMAI::BAI::enforceStrictNNLiveBattleAction;
	using MMAI::Schema::ACTION_RESET;
	using MMAI::Schema::ACTION_RETREAT;
	// Do not turn a training reset into an unsolicited hero retreat when
	// the VCMI server is waiting for a strict neural stack action.
	EXPECT_THROW(enforceStrictNNLiveBattleAction(true, ACTION_RESET), std::runtime_error);
	// In V15, action IDs are graph node indexes starting at 0; rejecting
	// numeric ACTION_RETREAT would mistakenly reject a legal learned move.
	EXPECT_EQ(ACTION_RETREAT, 0);
	EXPECT_NO_THROW(enforceStrictNNLiveBattleAction(true, ACTION_RETREAT));
	EXPECT_NO_THROW(enforceStrictNNLiveBattleAction(true, 0));
	EXPECT_NO_THROW(enforceStrictNNLiveBattleAction(true, 42));
	// Preserve upstream training/hybrid behavior when strict mode is off.
	EXPECT_NO_THROW(enforceStrictNNLiveBattleAction(false, ACTION_RESET));
	EXPECT_NO_THROW(enforceStrictNNLiveBattleAction(false, ACTION_RETREAT));
}

#endif
