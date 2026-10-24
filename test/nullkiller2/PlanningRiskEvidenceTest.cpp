/* GPL-2.0-or-later; see license.txt. Evidence policy tests, not calibration. */
#include "../StdInc.h"
#include "../../AI/Nullkiller2/Engine/Experimental/PlanningRiskEvidence.h"
using namespace NK2AI::experimental;

TEST(PlanningRiskEvidence, LuckyThreeCannotApproveAndCheckpointSpendingIsConservative)
{
	PlanningRiskEvidence evidence;
	for(int i = 0; i < 3; ++i) ASSERT_TRUE(evidence.append(true, 0.0));
	EXPECT_EQ(evidence.assess(0.35, 2, true).recommendation, RiskRecommendation::UNKNOWN);
	for(int i = 3; i < 64; ++i) ASSERT_TRUE(evidence.append(true, 0.0));
	EXPECT_EQ(evidence.assess(0.35, 2, true).recommendation, RiskRecommendation::UNKNOWN);
	for(int i = 64; i < 128; ++i) ASSERT_TRUE(evidence.append(true, 0.0));
	const auto result = evidence.assess(0.35, 2, true);
	const double alpha = 0.05 / 16.0;
	EXPECT_NEAR(result.adverseUpper, 1.0 - std::pow(alpha, 1.0 / 128.0), 1e-12);
	EXPECT_EQ(result.recommendation, RiskRecommendation::FAVORABLE);
	EXPECT_FALSE(evidence.append(true, 0.0));
}

TEST(PlanningRiskEvidence, MixedOutcomesAndUnsupportedCannotBecomeFavorable)
{
	PlanningRiskEvidence evidence;
	for(int i = 0; i < 128; ++i) ASSERT_TRUE(evidence.append(i % 2 == 0, 0.1));
	const auto result = evidence.assess(0.35, 1, true);
	EXPECT_GT(result.adverseUpper, 0.5);
	EXPECT_LT(result.defeatLower, 0.5);
	EXPECT_EQ(result.recommendation, RiskRecommendation::UNKNOWN);
	evidence.markUnsupported();
	EXPECT_EQ(evidence.assess(1.0, 1, true).recommendation, RiskRecommendation::UNKNOWN);
}

TEST(PlanningRiskEvidence, ContextToleranceAndOwnMagicRemainDirectional)
{
	PlanningRiskEvidence expensiveWin, defeats;
	for(int i = 0; i < 128; ++i)
	{
		ASSERT_TRUE(expensiveWin.append(true, 0.6));
		ASSERT_TRUE(defeats.append(false, 1.0));
	}
	EXPECT_EQ(expensiveWin.assess(0.35, 1, true).recommendation, RiskRecommendation::UNKNOWN);
	EXPECT_EQ(expensiveWin.assess(0.8, 1, true).recommendation, RiskRecommendation::FAVORABLE);
	EXPECT_EQ(defeats.assess(1.0, 1, true).recommendation, RiskRecommendation::LIKELY_DEFEAT);
	EXPECT_EQ(defeats.assess(1.0, 1, false).recommendation, RiskRecommendation::UNKNOWN);
}

TEST(PlanningRiskEvidence, DeadlineAndBudgetNeverProducePartialCheckpointDecision)
{
	PlanningRiskEvidence evidence;
	EXPECT_EQ(evidence.nextBatchSize(true, 128), 0u);
	EXPECT_EQ(evidence.nextBatchSize(false, 15), 0u);
	EXPECT_EQ(evidence.nextBatchSize(false, 16), 16u);
	for(int i = 0; i < 15; ++i) ASSERT_TRUE(evidence.append(true, 0.0));
	EXPECT_EQ(evidence.assess(0.35, 1, true).recommendation, RiskRecommendation::UNKNOWN);
	EXPECT_EQ(evidence.nextBatchSize(true, 128), 0u);
	EXPECT_EQ(evidence.nextBatchSize(false, 1), 1u);
}

TEST(PlanningRiskEvidence, BinomialTailMatchesIndependentHighPrecisionControls)
{
	// Controls computed by directly summing binomial coefficients with
	// 70-digit Decimal arithmetic, independent of the recurrence used above.
	for(const auto & [trials, failures, expected] : std::vector<std::tuple<unsigned, unsigned, double>>{
		{16, 1, 0.4022631042634067}, {32, 16, 0.7397607202991023}, {128, 127, 0.9999755480098226}})
	{
		PlanningRiskEvidence evidence;
		for(unsigned i = 0; i < trials; ++i) ASSERT_TRUE(evidence.append(i >= failures, 0.0));
		EXPECT_NEAR(evidence.assess(0.35, 2, true).adverseUpper, expected, 1e-12);
	}
}

TEST(PlanningRiskEvidence, ExtremeComplementaryBoundsRemainNumericallyValid)
{
	PlanningRiskEvidence oneDefeat, allButOneDefeat;
	for(unsigned i = 0; i < 128; ++i)
	{
		ASSERT_TRUE(oneDefeat.append(i != 0, 0.0));
		ASSERT_TRUE(allButOneDefeat.append(i == 0, 0.0));
	}
	const double exactUpper = std::exp(std::log1p(-0.05 / 16.0) / 128.0);
	EXPECT_NEAR(allButOneDefeat.assess(0.35, 2, true).adverseUpper, exactUpper, 1e-12);
	EXPECT_NEAR(oneDefeat.assess(0.35, 2, true).defeatLower, 1.0 - exactUpper, 1e-12);
}
