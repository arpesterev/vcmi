/* GPL-2.0-or-later; see license.txt. Protocol tests, not combat calibration. */
#include "../StdInc.h"
#include "../../AI/Nullkiller2/Engine/Experimental/PlanningRiskTable.h"
#include "../../AI/Nullkiller2/Pathfinding/AINodeStorage.h"
#include "../../AI/Nullkiller2/Engine/ArmyLossPolicy.h"

VCMI_LIB_USING_NAMESPACE
using namespace NK2AI;
using namespace NK2AI::experimental;
namespace
{
SampledBattleOutcome outcome(bool won, double loss)
{
	SampledBattleOutcome value;
	value.status = SampledBattleStatus::FINISHED;
	value.winner = won ? BattleSide::ATTACKER : BattleSide::DEFENDER;
	value.initialArmyValue[0] = 1000;
	value.remainingArmyValue[0] = 1000 * (1 - loss);
	return value;
}
PlanningRiskRecord record(unsigned samples, bool won, double loss, unsigned variants = 1)
{
	PlanningRiskRecord value;
	value.decisionEvidence = true;
	value.allScenariosFinished = true;
	value.variantScenarios.resize(variants, std::vector<SampledBattleOutcome>(samples, outcome(won, loss)));
	return value;
}
}
TEST(PlanningRiskDecision, ThreeLuckyWinsCannotRescueAnUnsafeScalarFight)
{
	auto value = record(3, true, 0);
	EXPECT_EQ(value.assess(.35, true).recommendation, RiskRecommendation::UNKNOWN);
	value = record(128, true, .1, 2);
	EXPECT_EQ(value.assess(.35, true).recommendation, RiskRecommendation::FAVORABLE);
	value.variantScenarios[1][0] = outcome(false, 1);
	EXPECT_EQ(value.assess(.35, true).recommendation, RiskRecommendation::UNKNOWN);
}
TEST(PlanningRiskDecision, EveryVariantAndCompleteCheckpointAreRequired)
{
	auto value = record(128, true, .1, 2);
	value.variantScenarios[1].resize(127);
	EXPECT_EQ(value.assess(.35, true).recommendation, RiskRecommendation::UNKNOWN);
	value = record(128, true, .1, 2);
	value.variantScenarios[1][0].status = SampledBattleStatus::UNSUPPORTED;
	EXPECT_EQ(value.assess(.35, true).recommendation, RiskRecommendation::UNKNOWN);
}
TEST(PlanningRiskDecision, CostlyVictoryIsContextualAndMagicBlocksNoSpellVeto)
{
	auto value = record(128, true, .6);
	EXPECT_EQ(value.assess(.35, true).recommendation, RiskRecommendation::UNKNOWN);
	EXPECT_EQ(value.assess(1, true).recommendation, RiskRecommendation::FAVORABLE);
	value = record(32, false, 1);
	EXPECT_EQ(value.assess(.35, true).recommendation, RiskRecommendation::LIKELY_DEFEAT);
	EXPECT_EQ(value.assess(.35, false).recommendation, RiskRecommendation::UNKNOWN);
	value.unmodelledOwnMagic = true;
	EXPECT_EQ(value.assess(.35, true).recommendation, RiskRecommendation::UNKNOWN);
}
TEST(PlanningRiskDecision, ProducerRetainsEarlierDangerEvenWithoutEstimatedLoss)
{
	AIPathNode source{};
	source.danger = 200;
	source.armyLoss = 0;
	AIPathNode destination{};
	destination.inheritCosts(source);
	destination.danger = 500;
	AIPath path;
	path.setRiskProvenance(destination, 500, 500);
	EXPECT_TRUE(path.hasEarlierDangerProvenance);
	EXPECT_EQ(path.beforeDestinationDanger, 200);
	EXPECT_EQ(path.armyLoss, 0);
	source.danger = 0;
	destination.inheritCosts(source);
	destination.danger = 500;
	path.setRiskProvenance(destination, 500, 500);
	EXPECT_EQ(path.beforeDestinationDanger, 0);
	EXPECT_TRUE(path.hasEarlierDangerProvenance);
	// Legacy folded danger export cannot claim exact route provenance.
	path.setDangerProvenance(500, 500);
	EXPECT_FALSE(path.hasEarlierDangerProvenance);
}

TEST(PlanningRiskDecision, SharedLossPolicyPreservesEstablishedFallback)
{
	EXPECT_FLOAT_EQ(maxWillingArmyLoss(.35f, 1, false), .35f);
	EXPECT_FLOAT_EQ(maxWillingArmyLoss(.35f, .5f, false), .175f);
	EXPECT_FLOAT_EQ(maxWillingArmyLoss(.35f, 0, false), 1);
	EXPECT_FLOAT_EQ(maxWillingArmyLoss(.35f, 1, true), 1);
	EXPECT_FLOAT_EQ(maxWillingArmyLoss(0, 1, false), 1);
	EXPECT_FLOAT_EQ(maxWillingArmyLoss(.35f, std::numeric_limits<float>::quiet_NaN(), false), 1);
}

TEST(PlanningRiskDecision, ThreeFieldsUseAllVariantUnionBudgetAndFourAreUnknown)
{
	auto value = record(128, true, 0, 3);
	const auto decision = value.assess(.35, true);
	EXPECT_EQ(decision.recommendation, RiskRecommendation::FAVORABLE);
	EXPECT_NEAR(decision.adverseUpper, -std::expm1(std::log(.05 / 24) / 128), 1e-12);
	value.variantScenarios[2].resize(64);
	EXPECT_EQ(value.assess(.35, true).recommendation, RiskRecommendation::UNKNOWN);
	value = record(128, true, 0, 4);
	EXPECT_EQ(value.assess(.35, true).recommendation, RiskRecommendation::UNKNOWN);
}
TEST(PlanningRiskDecision, PublicationSummaryRefreshesPolicyAndMagicWithoutRedrawing)
{
	auto value = record(128, true, .6);
	value.contextualLossTolerance = 1;
	value.finalizeDecisionSummary();
	ASSERT_TRUE(value.contextualSummaryReady);
	EXPECT_EQ(value.contextualAssessment.recommendation, RiskRecommendation::FAVORABLE);
	value.contextualLossTolerance = .35;
	value.finalizeDecisionSummary();
	EXPECT_EQ(value.contextualAssessment.recommendation, RiskRecommendation::UNKNOWN);
	value = record(128, false, 1);
	value.contextualLossTolerance = .35;
	value.finalizeDecisionSummary();
	EXPECT_EQ(value.contextualAssessment.recommendation, RiskRecommendation::LIKELY_DEFEAT);
	value.unmodelledOwnMagic = true;
	value.finalizeDecisionSummary();
	EXPECT_EQ(value.contextualAssessment.recommendation, RiskRecommendation::UNKNOWN);
	EXPECT_EQ(value.variantScenarios[0].size(), 128u);
}
