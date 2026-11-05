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

TEST(MMAINNOutputValidation, V13V15WarMachineTargetsOnlyLivingShootableEnemies)
{
	struct Candidate
	{
		int value;
		bool living;
		bool shootable;
		bool alive() const { return living; }
	};
	const Candidate dead{1000, false, true};
	const Candidate blocked{900, true, false};
	const Candidate legalLow{10, true, true};
	const Candidate legalHigh{20, true, true};
	const auto canShoot = [](const Candidate * target) { return target->shootable; };
	const auto score = [](const Candidate * target) { return target->value; };
	using MMAI::BAI::selectLegalNNWarMachineTarget;

	const std::vector<const Candidate *> mixed{nullptr, &dead, &blocked, &legalLow, &legalHigh};
	EXPECT_EQ(selectLegalNNWarMachineTarget(mixed, canShoot, score), &legalHigh);
	const std::vector<const Candidate *> noLegal{nullptr, &dead, &blocked};
	EXPECT_EQ(selectLegalNNWarMachineTarget(noLegal, canShoot, score), nullptr);
	const std::vector<const Candidate *> empty;
	EXPECT_EQ(selectLegalNNWarMachineTarget(empty, canShoot, score), nullptr);
}

TEST(MMAINNOutputValidation, V13V15RejectNonFiniteObservationBeforeInference)
{
	using MMAI::BAI::validateNNObservationValues;
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();

	// Sparse graphs legitimately contain no edges; finite model inputs
	// must pass through unchanged without normalizing or clipping.
	EXPECT_NO_THROW(validateNNObservationValues({}, "edge_attr_flat"));
	EXPECT_NO_THROW(validateNNObservationValues({-1.0f, 0.0f, 1.0f}, "obs"));
	EXPECT_THROW(validateNNObservationValues({nan}, "obs"), std::runtime_error);
	EXPECT_THROW(validateNNObservationValues({0.0f, inf}, "x_Unit"), std::runtime_error);
	EXPECT_THROW(validateNNObservationValues({-inf, 0.0f}, "edge_attr_flat"), std::runtime_error);

	try
	{
		validateNNObservationValues({0.0f, 1.0f, nan}, "x_Action");
		FAIL() << "Non-finite ONNX observation was accepted";
	}
	catch(const std::runtime_error & e)
	{
		const std::string diagnostic = e.what();
		EXPECT_NE(diagnostic.find("x_Action"), std::string::npos);
		EXPECT_NE(diagnostic.find("index 2"), std::string::npos);
	}
}

TEST(MMAINNOutputValidation, V15RejectsUnrepresentableNeuralActionIdBeforeDispatch)
{
	using MMAI::BAI::checkedNNV15ActionId;
	const auto maxAction = std::numeric_limits<int>::max();

	// Normal V15 action nodes, including zero, are dispatched unchanged.
	EXPECT_EQ(checkedNNV15ActionId(0), 0);
	EXPECT_EQ(checkedNNV15ActionId(1), 1);
	EXPECT_EQ(checkedNNV15ActionId(maxAction), maxAction);

	// The ONNX head and action graph use int64, but Router returns int.
	// Negative or oversized IDs must not wrap into a different valid node.
	EXPECT_THROW(checkedNNV15ActionId(-1), std::runtime_error);
	EXPECT_THROW(checkedNNV15ActionId(static_cast<int64_t>(maxAction) + 1), std::runtime_error);
	EXPECT_THROW(checkedNNV15ActionId(std::numeric_limits<int64_t>::max()), std::runtime_error);

	try
	{
		checkedNNV15ActionId(std::numeric_limits<int64_t>::max());
		FAIL() << "An oversized neural graph ID reached VCMI dispatch";
	}
	catch(const std::runtime_error & e)
	{
		EXPECT_NE(std::string(e.what()).find("outside the VCMI action range"), std::string::npos);
	}
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

TEST(MMAINNOutputValidation, V13RejectsDecodedActionOutsideLiveSchema)
{
	using MMAI::BAI::checkedV13DecodedAction;
	// Valid live action IDs remain unchanged; the model's policy is not
	// normalized, clamped, resampled or rerouted.
	EXPECT_EQ(checkedV13DecodedAction(0, 100), 0);
	EXPECT_EQ(checkedV13DecodedAction(1, 100), 1);
	EXPECT_EQ(checkedV13DecodedAction(99, 100), 99);

	// These are legitimate masked metadata sentinels, but never live
	// commands. Prevent a negative mask index or invalid VCMI action.
	EXPECT_THROW(checkedV13DecodedAction(-1, 100), std::runtime_error);
	EXPECT_THROW(checkedV13DecodedAction(std::numeric_limits<int32_t>::min(), 100), std::runtime_error);
	EXPECT_THROW(checkedV13DecodedAction(100, 100), std::runtime_error);
	EXPECT_THROW(checkedV13DecodedAction(std::numeric_limits<int32_t>::max(), 100), std::runtime_error);
	EXPECT_THROW(checkedV13DecodedAction(0, 0), std::runtime_error);

	try
	{
		checkedV13DecodedAction(-1, 100);
		FAIL() << "Masked V13 action_table sentinel reached live action conversion";
	}
	catch(const std::runtime_error & e)
	{
		EXPECT_NE(std::string(e.what()).find("outside the VCMI action space"), std::string::npos);
	}
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


TEST(MMAINNOutputValidation, V15RejectsDuplicateNodeAndEdgeMetadataTypes)
{
	using MMAI::BAI::requireUniqueNNMetadataType;
	// The upstream exporter derives node_order and edge_order from unique
	// dictionary keys. The model must reject repeated schema types before
	// ONNX inference instead of silently duplicating a graph relation.
	std::set<int> nodeTypes;
	EXPECT_NO_THROW(requireUniqueNNMetadataType(nodeTypes, 0, "node_order"));
	EXPECT_NO_THROW(requireUniqueNNMetadataType(nodeTypes, 1, "node_order"));
	EXPECT_THROW(requireUniqueNNMetadataType(nodeTypes, 0, "node_order"), std::runtime_error);

	std::set<int> edgeTypes;
	EXPECT_NO_THROW(requireUniqueNNMetadataType(edgeTypes, 0, "edge_order"));
	EXPECT_NO_THROW(requireUniqueNNMetadataType(edgeTypes, 1, "edge_order"));
	EXPECT_THROW(requireUniqueNNMetadataType(edgeTypes, 1, "edge_order"), std::runtime_error);

	// A repeated numeric value in a different category is not a duplicate.
	EXPECT_NO_THROW(requireUniqueNNMetadataType(edgeTypes, 2, "edge_order"));
	EXPECT_EQ(nodeTypes.size(), 2u);
	EXPECT_EQ(edgeTypes.size(), 3u);
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

TEST(MMAINNOutputValidation, V13StrictRejectsInvalidModelActionWithoutResampling)
{
	using MMAI::BAI::enforceNNV13SingleLiveAction;
	// A valid decision, including V13 WAIT, remains unchanged.
	EXPECT_NO_THROW(enforceNNV13SingleLiveAction(true, true, 1));
	EXPECT_NO_THROW(enforceNNV13SingleLiveAction(true, true, 42));

	// A rejected V13 masked action or disallowed hero retreat must fail on
	// the first decoded output, before the legacy retry loop samples again.
	try
	{
		enforceNNV13SingleLiveAction(true, false, 42);
		FAIL() << "Strict V13 accepted an invalid neural action";
	}
	catch(const std::runtime_error & e)
	{
		const std::string diagnostic(e.what());
		EXPECT_NE(diagnostic.find("42"), std::string::npos);
		EXPECT_NE(diagnostic.find("refusing resampling"), std::string::npos);
	}
	EXPECT_THROW(enforceNNV13SingleLiveAction(true, false, 0), std::runtime_error);

	// Explicit gym/hybrid callers retain the original invalid-action retry.
	EXPECT_NO_THROW(enforceNNV13SingleLiveAction(false, false, 42));
}

TEST(MMAINNOutputValidation, RejectsScriptedFallbackForStrictOrInvalidActiveActor)
{
	using MMAI::BAI::canNNUseLegacyBattleFallback;
	struct Creature
	{
		bool living;
		bool alive() const { return living; }
	};
	const Creature living{true};
	const Creature dead{false};

	// The neural identity never delegates to BattleEvaluator.
	EXPECT_FALSE(canNNUseLegacyBattleFallback(true, &living));
	EXPECT_FALSE(canNNUseLegacyBattleFallback(true, &dead));
	EXPECT_FALSE(canNNUseLegacyBattleFallback(true, static_cast<const Creature *>(nullptr)));

	// Legacy gym/hybrid fallback is still allowed for a live creature only.
	EXPECT_TRUE(canNNUseLegacyBattleFallback(false, &living));
	EXPECT_FALSE(canNNUseLegacyBattleFallback(false, &dead));
	EXPECT_FALSE(canNNUseLegacyBattleFallback(false, static_cast<const Creature *>(nullptr)));
}

TEST(MMAINNOutputValidation, V13RejectsDeadOrMissingEngineOwnedMachineActor)
{
	using MMAI::BAI::requireNNV13LivingActiveStack;
	struct Creature
	{
		bool living;
		bool alive() const { return living; }
	};
	const Creature active{true};
	const Creature dead{false};
	int dispatched = 0;
	const auto dispatchEngineOwnedAction = [&dispatched](const Creature * stack)
	{
		requireNNV13LivingActiveStack(stack);
		++dispatched;
	};

	EXPECT_NO_THROW(dispatchEngineOwnedAction(&active));
	EXPECT_EQ(dispatched, 1);
	EXPECT_THROW(dispatchEngineOwnedAction(&dead), std::runtime_error);
	EXPECT_THROW(dispatchEngineOwnedAction(static_cast<const Creature *>(nullptr)), std::runtime_error);
	EXPECT_EQ(dispatched, 1);

	try
	{
		requireNNV13LivingActiveStack(&dead);
		FAIL() << "V13 accepted a dead engine-owned war-machine actor";
	}
	catch(const std::runtime_error & e)
	{
		EXPECT_NE(std::string(e.what()).find("active creature is missing or no longer alive"), std::string::npos);
	}
}

TEST(MMAINNOutputValidation, V13RejectsStaleOrDeadActiveCreatureBeforeModelDispatch)
{
	using MMAI::BAI::requireNNV13ActionActor;
	struct Creature
	{
		bool living = true;
		bool alive() const { return living; }
	};
	struct Observed
	{
		const Creature * cstack = nullptr;
	};

	Creature active;
	Creature another;
	const Observed matching{&active};
	const Observed stale{&another};
	const Observed missing{};
	EXPECT_NO_THROW(requireNNV13ActionActor(&matching, &active));

	// The observation may still contain a different stack after an event.
	EXPECT_THROW(requireNNV13ActionActor(&stale, &active), std::runtime_error);
	EXPECT_THROW(requireNNV13ActionActor(&missing, &active), std::runtime_error);
	EXPECT_THROW(requireNNV13ActionActor(static_cast<const Observed *>(nullptr), &active), std::runtime_error);
	EXPECT_THROW(requireNNV13ActionActor(&matching, static_cast<const Creature *>(nullptr)), std::runtime_error);

	// A matching but dead stack must not be sent to the server either.
	active.living = false;
	try
	{
		requireNNV13ActionActor(&matching, &active);
		FAIL() << "V13 accepted an already destroyed active creature";
	}
	catch(const std::runtime_error & e)
	{
		EXPECT_NE(std::string(e.what()).find("matching living active creature"), std::string::npos);
	}
}

TEST(MMAINNOutputValidation, V13StrictNeuralRetreatUsesLiveEnginePermission)
{
	using MMAI::BAI::canNNV13DispatchRetreat;

	// A V13 model-selected RETREAT is dispatched as selected when VCMI
	// permits fleeing; the legal ONNX decision is never silently discarded.
	EXPECT_TRUE(canNNV13DispatchRetreat(true, false, true));
	// A neural model cannot force an illegal live retreat or substitute one.
	EXPECT_FALSE(canNNV13DispatchRetreat(true, false, false));
	// Legacy gym/hybrid behavior must not change: explicit model RETREAT
	// remains invalid, whereas ACTION_RESET still resets its training episode.
	EXPECT_FALSE(canNNV13DispatchRetreat(false, false, true));
	EXPECT_TRUE(canNNV13DispatchRetreat(false, true, false));
	EXPECT_TRUE(canNNV13DispatchRetreat(true, true, false));
}

TEST(MMAINNOutputValidation, V13RejectsIllegalTargetBeforeEngineDispatch)
{
	using MMAI::BAI::requireNNV13LegalTarget;
	struct Stack
	{
		bool living;
		bool alive() const { return living; }
	};
	const Stack living{true};
	const Stack dead{false};
	int callbackCalls = 0;
	const auto legal = [&callbackCalls](const Stack *)
	{
		++callbackCalls;
		return true;
	};

	// The selected V13 SHOOT or AMOVE target is preserved when it is
	// still alive and legal according to the live engine callback.
	EXPECT_NO_THROW(requireNNV13LegalTarget(&living, legal, "SHOOT"));
	EXPECT_NO_THROW(requireNNV13LegalTarget(&living, legal, "AMOVE"));
	EXPECT_EQ(callbackCalls, 2);

	// A stale observation may retain a pointer to an already dead stack.
	// In strict neural combat, refuse both action types without querying
	// the callback or resampling a different action.
	EXPECT_THROW(requireNNV13LegalTarget(&dead, legal, "SHOOT"), std::runtime_error);
	EXPECT_THROW(requireNNV13LegalTarget(&dead, legal, "AMOVE"), std::runtime_error);
	EXPECT_EQ(callbackCalls, 2);
	EXPECT_THROW(requireNNV13LegalTarget(&living,
		[](const Stack *) { return false; }, "SHOOT"), std::runtime_error);
	EXPECT_THROW(requireNNV13LegalTarget(&living,
		[](const Stack *) { return false; }, "AMOVE"), std::runtime_error);

	bool queriedMissingTarget = false;
	EXPECT_THROW(requireNNV13LegalTarget(static_cast<const Stack *>(nullptr),
		[&queriedMissingTarget](const Stack *) {
			queriedMissingTarget = true;
			return true;
		}, "AMOVE"), std::runtime_error);
	EXPECT_FALSE(queriedMissingTarget);

	try
	{
		requireNNV13LegalTarget(&dead, legal, "SHOOT");
		FAIL() << "MMAI V13 accepted a dead model-selected target";
	}
	catch(const std::runtime_error & e)
	{
		EXPECT_NE(std::string(e.what()).find("V13: model SHOOT"), std::string::npos);
	}
}

TEST(MMAINNOutputValidation, V13RejectsReplacedShootHexTargetBeforeDispatch)
{
	using MMAI::BAI::requireNNV13SelectedShootHexTarget;
	struct Stack { int id; };
	const Stack original{1};
	const Stack replacement{2};

	// Both pointers may identify still-living stacks, and the original
	// stack may remain shootable *elsewhere*. V13's model chose the old
	// hex, not a permission to follow that stack to a new position.
	EXPECT_NO_THROW(requireNNV13SelectedShootHexTarget(&original, &original));
	EXPECT_THROW(requireNNV13SelectedShootHexTarget(&original, &replacement), std::runtime_error);
	EXPECT_THROW(requireNNV13SelectedShootHexTarget(&original, static_cast<const Stack *>(nullptr)), std::runtime_error);
	EXPECT_THROW(requireNNV13SelectedShootHexTarget(static_cast<const Stack *>(nullptr), &replacement), std::runtime_error);
	EXPECT_THROW(requireNNV13SelectedShootHexTarget(static_cast<const Stack *>(nullptr), static_cast<const Stack *>(nullptr)), std::runtime_error);

	try
	{
		requireNNV13SelectedShootHexTarget(&original, &replacement);
		FAIL() << "V13 silently redirected an ONNX-selected SHOOT hex";
	}
	catch(const std::runtime_error & e)
	{
		EXPECT_NE(std::string(e.what()).find("V13: model SHOOT selected hex"), std::string::npos);
	}
}

TEST(MMAINNOutputValidation, V13RejectsReplacedMeleeHexTargetBeforeDispatch)
{
	using MMAI::BAI::requireNNV13SelectedMeleeHexTarget;
	struct Stack { int id; };
	const Stack original{1};
	const Stack replacement{2};

	// Even if both occupants could be attacked, an ONNX-selected melee
	// direction must never silently switch to the replacement creature.
	EXPECT_NO_THROW(requireNNV13SelectedMeleeHexTarget(&original, &original));
	EXPECT_THROW(requireNNV13SelectedMeleeHexTarget(&original, &replacement), std::runtime_error);
	EXPECT_THROW(requireNNV13SelectedMeleeHexTarget(&original, static_cast<const Stack *>(nullptr)), std::runtime_error);
	EXPECT_THROW(requireNNV13SelectedMeleeHexTarget(static_cast<const Stack *>(nullptr), &replacement), std::runtime_error);
	EXPECT_THROW(requireNNV13SelectedMeleeHexTarget(static_cast<const Stack *>(nullptr), static_cast<const Stack *>(nullptr)), std::runtime_error);

	try
	{
		requireNNV13SelectedMeleeHexTarget(&original, &replacement);
		FAIL() << "V13 silently redirected an ONNX-selected melee attack";
	}
	catch(const std::runtime_error & e)
	{
		EXPECT_NE(std::string(e.what()).find("V13: model AMOVE target hex"), std::string::npos);
	}
}

TEST(MMAINNOutputValidation, V13RejectsMoveOutsideLiveEngineReachability)
{
	using MMAI::BAI::requireNNV13LegalMove;
	struct Hex
	{
		int index;
		bool available;
		bool isAvailable() const { return available; }
		int toInt() const { return index; }
	};
	struct Reachability
	{
		std::array<unsigned, 3> distances{0, 1, 4};
		bool isReachable(const Hex & hex) const { return distances.at(hex.index) < 1000000; }
	};

	Reachability live;
	// An ONNX destination at the movement limit is still legal. The
	// same-hex DEFEND path does not call this MOVE-only guard.
	EXPECT_NO_THROW(requireNNV13LegalMove(Hex{2, true}, live, 4u));
	EXPECT_NO_THROW(requireNNV13LegalMove(Hex{1, true}, live, 1u));

	// The originally valid V13 observation can be stale at dispatch time.
	EXPECT_THROW(requireNNV13LegalMove(Hex{2, true}, live, 3u), std::runtime_error);
	live.distances[1] = 1000000;
	EXPECT_THROW(requireNNV13LegalMove(Hex{1, true}, live, 9u), std::runtime_error);
	EXPECT_THROW(requireNNV13LegalMove(Hex{0, false}, live, 9u), std::runtime_error);
	// Invalid hex is rejected before indexing the reachability table.
	EXPECT_THROW(requireNNV13LegalMove(Hex{-1, false}, live, 9u), std::runtime_error);
}

TEST(MMAINNOutputValidation, V15RejectsStaleActiveCreatureBeforeActionConversion)
{
	using MMAI::BAI::requireNNV15ActionActor;
	using MMAI::BAI::requireNNV15LivingActiveStack;
	struct Creature
	{
		bool living = true;
		bool alive() const { return living; }
	};
	struct Unit
	{
		Creature cstack;
	};
	struct Action
	{
		std::shared_ptr<Unit> by;
	};

	const auto unit = std::make_shared<Unit>();
	const auto action = std::make_shared<Action>();
	action->by = unit;
	Creature otherStack;

	EXPECT_NO_THROW(requireNNV15LivingActiveStack(&unit->cstack));
	EXPECT_NO_THROW(requireNNV15ActionActor(action, &unit->cstack));
	EXPECT_THROW(requireNNV15ActionActor(action, &otherStack), std::runtime_error);

	auto missingActor = std::make_shared<Action>();
	EXPECT_THROW(requireNNV15ActionActor(missingActor, &unit->cstack), std::runtime_error);
	EXPECT_THROW(requireNNV15LivingActiveStack(static_cast<const Creature *>(nullptr)), std::runtime_error);
	EXPECT_THROW(requireNNV15ActionActor(action, static_cast<const Creature *>(nullptr)), std::runtime_error);

	std::shared_ptr<Action> missingAction;
	EXPECT_THROW(requireNNV15ActionActor(missingAction, &unit->cstack), std::runtime_error);

	// An event adapter can still hold the same stack pointer after its death.
	// Reject it both before ONNX inference and immediately before dispatch.
	unit->cstack.living = false;
	EXPECT_THROW(requireNNV15LivingActiveStack(&unit->cstack), std::runtime_error);
	EXPECT_THROW(requireNNV15ActionActor(action, &unit->cstack), std::runtime_error);
}


TEST(MMAINNOutputValidation, RejectsUnexpectedTerminalOnnxAction)
{
	using MMAI::BAI::requireNNModelTerminalReset;
	using MMAI::Schema::ACTION_RESET;
	using MMAI::Schema::ACTION_RETREAT;

	// V15 graph action 0 can be valid during a live battle, but not
	// after the ONNX model has received the terminal observation.
	EXPECT_NO_THROW(requireNNModelTerminalReset(ACTION_RESET));
	EXPECT_THROW(requireNNModelTerminalReset(ACTION_RETREAT), std::runtime_error);
	EXPECT_THROW(requireNNModelTerminalReset(0), std::runtime_error);
	EXPECT_THROW(requireNNModelTerminalReset(42), std::runtime_error);
}

TEST(MMAINNOutputValidation, V13V15RejectMovedOrReplacedObservedActorBeforeDispatch)
{
	using MMAI::BAI::requireNNObservedActorAtHex;
	struct Stack
	{
		int hex;
		bool living = true;
		bool alive() const { return living; }
		int getPosition() const { return hex; }
	};
	Stack actor{12};
	Stack replacement{12};
	const Stack * occupant = &actor;
	int lookups = 0;
	const auto liveAtHex = [&occupant, &lookups](int hex) -> const Stack *
	{
		++lookups;
		return hex == 12 ? occupant : nullptr;
	};
	for(const char * version : {"V13", "V15"})
		EXPECT_NO_THROW(requireNNObservedActorAtHex(&actor, 12, liveAtHex, version));

	actor.hex = 13; // same living actor moved after observation
	for(const char * version : {"V13", "V15"})
		EXPECT_THROW(requireNNObservedActorAtHex(&actor, 12, liveAtHex, version), std::runtime_error);
	actor.hex = 12;

	occupant = &replacement; // another living creature replaced actor
	for(const char * version : {"V13", "V15"})
		EXPECT_THROW(requireNNObservedActorAtHex(&actor, 12, liveAtHex, version), std::runtime_error);
	occupant = nullptr;
	EXPECT_THROW(requireNNObservedActorAtHex(&actor, 12, liveAtHex, "V15"), std::runtime_error);

	occupant = &actor;
	actor.living = false;
	const int previousLookups = lookups;
	EXPECT_THROW(requireNNObservedActorAtHex(&actor, 12, liveAtHex, "V13"), std::runtime_error);
	EXPECT_THROW(requireNNObservedActorAtHex(static_cast<const Stack *>(nullptr), 12, liveAtHex, "V15"), std::runtime_error);
	EXPECT_EQ(lookups, previousLookups); // null/dead actors never query VCMI

	actor.living = true;
	EXPECT_NO_THROW(requireNNObservedActorAtHex(&actor, 12, liveAtHex, "V15"));
}

TEST(MMAINNOutputValidation, V15RejectsMovedOrReplacedObservedTargetBeforeDispatch)
{
	using MMAI::BAI::requireNNV15ObservedTargetAtHex;
	struct Stack
	{
		int hex;
		bool living = true;
		bool alive() const { return living; }
		int getPosition() const { return hex; }
	};
	Stack selected{12};
	Stack replacement{12};
	const Stack * occupant = &selected;
	const auto lookup = [&occupant](int hex) -> const Stack *
	{
		return hex == 12 ? occupant : nullptr;
	};

	EXPECT_NO_THROW(requireNNV15ObservedTargetAtHex(&selected, 12, lookup, "SHOOT"));
	EXPECT_NO_THROW(requireNNV15ObservedTargetAtHex(&selected, 12, lookup, "AMOVE"));

	// The target moved. The same still-living stack is not at the observed hex.
	selected.hex = 13;
	EXPECT_THROW(requireNNV15ObservedTargetAtHex(&selected, 12, lookup, "SHOOT"), std::runtime_error);
	EXPECT_THROW(requireNNV15ObservedTargetAtHex(&selected, 12, lookup, "AMOVE"), std::runtime_error);

	// Replacing the target with another live unit is not the ONNX decision.
	selected.hex = 12;
	occupant = &replacement;
	EXPECT_THROW(requireNNV15ObservedTargetAtHex(&selected, 12, lookup, "SHOOT"), std::runtime_error);
	occupant = nullptr;
	EXPECT_THROW(requireNNV15ObservedTargetAtHex(&selected, 12, lookup, "AMOVE"), std::runtime_error);

	occupant = &selected;
	selected.living = false;
	EXPECT_THROW(requireNNV15ObservedTargetAtHex(&selected, 12, lookup, "SHOOT"), std::runtime_error);
	EXPECT_THROW(requireNNV15ObservedTargetAtHex(static_cast<const Stack *>(nullptr), 12, lookup, "AMOVE"), std::runtime_error);
}

TEST(MMAINNOutputValidation, V15RejectsDeadOrMissingModelAttackTarget)
{
	using MMAI::BAI::requireNNV15LegalTarget;
	struct Stack
	{
		bool living;
		bool alive() const { return living; }
	};
	const Stack living{true};
	const Stack dead{false};
	int liveCallbackCalls = 0;
	const auto legal = [&liveCallbackCalls](const Stack *)
	{
		++liveCallbackCalls;
		return true;
	};

	// Both attack variants use this production guard immediately before
	// constructing their engine actions. A valid ONNX-selected target is
	// preserved if the actual VCMI callback still permits the action.
	EXPECT_NO_THROW(requireNNV15LegalTarget(&living, legal, "SHOOT"));
	EXPECT_NO_THROW(requireNNV15LegalTarget(&living, legal, "AMOVE"));
	EXPECT_EQ(liveCallbackCalls, 2);

	// A formerly legal graph target may be killed by an intervening event.
	// Never shoot at its old hex, reinterpret the attack, or sample again.
	EXPECT_THROW(requireNNV15LegalTarget(&dead, legal, "SHOOT"), std::runtime_error);
	EXPECT_THROW(requireNNV15LegalTarget(&dead, legal, "AMOVE"), std::runtime_error);
	EXPECT_THROW(requireNNV15LegalTarget(static_cast<const Stack *>(nullptr), legal, "SHOOT"), std::runtime_error);
	EXPECT_EQ(liveCallbackCalls, 2); // null/dead do not query the callback

	EXPECT_THROW(requireNNV15LegalTarget(&living,
		[](const Stack *) { return false; }, "AMOVE"), std::runtime_error);
	try
	{
		requireNNV15LegalTarget(&dead, legal, "SHOOT");
		FAIL() << "MMAI V15 accepted a dead model-selected target";
	}
	catch(const std::runtime_error & e)
	{
		EXPECT_NE(std::string(e.what()).find("V15: model SHOOT"), std::string::npos);
	}
}

TEST(MMAINNOutputValidation, V15RejectsMoveOutsideLiveEngineReachability)
{
	using MMAI::BAI::requireNNV15LegalMove;
	struct Hex
	{
		int index;
		bool available;
		bool isAvailable() const { return available; }
		int toInt() const { return index; }
	};
	struct Reachability
	{
		std::array<unsigned, 3> distances{0, 1, 4};
		bool isReachable(const Hex & hex) const { return distances.at(hex.index) < 1000000; }
	};

	Reachability live;
	// Reachable destination at the exact movement limit remains legal.
	EXPECT_NO_THROW(requireNNV15LegalMove(Hex{2, true}, live, 4u));
	EXPECT_NO_THROW(requireNNV15LegalMove(Hex{1, true}, live, 1u));

	// The graph's older action mask is insufficient if speed, occupancy,
	// wall/gate state or obstacles changed before server dispatch.
	EXPECT_THROW(requireNNV15LegalMove(Hex{2, true}, live, 3u), std::runtime_error);
	live.distances[1] = 1000000;
	EXPECT_THROW(requireNNV15LegalMove(Hex{1, true}, live, 9u), std::runtime_error);
	EXPECT_THROW(requireNNV15LegalMove(Hex{0, false}, live, 9u), std::runtime_error);

	// An invalid battlefield hex must be rejected before indexing distances.
	EXPECT_THROW(requireNNV15LegalMove(Hex{-1, false}, live, 9u), std::runtime_error);
}

TEST(MMAINNOutputValidation, V15RejectsMeleeApproachOutsideLiveEngineReachability)
{
	using MMAI::BAI::requireNNV15LegalMeleeApproach;
	struct Hex
	{
		int index;
		bool available;
		bool isAvailable() const { return available; }
		int toInt() const { return index; }
	};
	struct Reachability
	{
		std::array<unsigned, 3> distances{0, 2, 5};
		bool isReachable(const Hex & hex) const { return distances.at(hex.index) < 1000000; }
	};

	Reachability live;
	// The current hex and a destination at the movement limit remain legal.
	EXPECT_NO_THROW(requireNNV15LegalMeleeApproach(Hex{0, true}, live, 0u));
	EXPECT_NO_THROW(requireNNV15LegalMeleeApproach(Hex{2, true}, live, 5u));
	// A stale graph mask cannot authorize a changed live movement range.
	EXPECT_THROW(requireNNV15LegalMeleeApproach(Hex{2, true}, live, 4u), std::runtime_error);
	live.distances[1] = 1000000;
	EXPECT_THROW(requireNNV15LegalMeleeApproach(Hex{1, true}, live, 9u), std::runtime_error);
	EXPECT_THROW(requireNNV15LegalMeleeApproach(Hex{0, false}, live, 9u), std::runtime_error);
	EXPECT_THROW(requireNNV15LegalMeleeApproach(Hex{-1, false}, live, 9u), std::runtime_error);
	try
	{
		requireNNV15LegalMeleeApproach(Hex{2, true}, live, 4u);
		FAIL() << "MMAI V15 accepted a stale melee approach";
	}
	catch(const std::runtime_error & e)
	{
		EXPECT_NE(std::string(e.what()).find("AMOVE approach"), std::string::npos);
	}
}

TEST(MMAINNOutputValidation, V15RejectsEmptyActionGraphBeforeOnnxInference)
{
	using MMAI::BAI::requireNNV15ActionCandidates;
	EXPECT_NO_THROW(requireNNV15ActionCandidates(1u));
	EXPECT_NO_THROW(requireNNV15ActionCandidates(165u));
	try
	{
		requireNNV15ActionCandidates(0u);
		FAIL() << "MMAI V15 attempted inference on an empty action graph";
	}
	catch(const std::runtime_error & e)
	{
		EXPECT_NE(std::string(e.what()).find("before ONNX inference"), std::string::npos);
	}
}

TEST(MMAINNOutputValidation, V15RejectsInactiveGraphActionBeforeServerDispatch)
{
	using MMAI::BAI::requireNNV15ActiveAction;
	struct Action
	{
		bool isActive = true;
	};

	auto active = std::make_shared<Action>();
	EXPECT_NO_THROW(requireNNV15ActiveAction(active));

	active->isActive = false; // stale graph after a battle event
	EXPECT_THROW(requireNNV15ActiveAction(active), std::runtime_error);

	std::shared_ptr<Action> missing;
	EXPECT_THROW(requireNNV15ActiveAction(missing), std::runtime_error);
}

TEST(MMAINNOutputValidation, StrictNeuralRejectsRepeatedAnsiRenderRequests)
{
	using MMAI::BAI::requireStrictNNRenderProgress;
	using MMAI::BAI::STRICT_NN_RENDER_REQUEST_LIMIT;

	// A renderer request is an observation protocol step, not a VCMI
	// battle action. A neural model stuck in it must fail closed.
	EXPECT_NO_THROW(requireStrictNNRenderProgress(true, 0));
	for(unsigned n = 1; n <= STRICT_NN_RENDER_REQUEST_LIMIT; ++n)
		EXPECT_NO_THROW(requireStrictNNRenderProgress(true, n));
	try
	{
		requireStrictNNRenderProgress(true, STRICT_NN_RENDER_REQUEST_LIMIT + 1);
		FAIL() << "Strict neural policy accepted unbounded render requests";
	}
	catch(const std::runtime_error & e)
	{
		EXPECT_NE(std::string(e.what()).find("ANSI render"), std::string::npos);
	}
	// Keep the source training/hybrid renderer's unrestricted semantics.
	EXPECT_NO_THROW(requireStrictNNRenderProgress(false, STRICT_NN_RENDER_REQUEST_LIMIT + 1));
}

#endif
