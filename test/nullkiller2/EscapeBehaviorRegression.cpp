/*
 * EscapeBehaviorRegression.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "../StdInc.h"
#include "../../AI/Nullkiller2/Behaviors/EscapeBehavior.h"
#include "../../AI/Nullkiller2/Engine/PriorityEvaluator.h"

namespace test
{
namespace
{
NK2AI::EscapePathCandidate safeCandidate()
{
	NK2AI::EscapePathCandidate candidate;
	candidate.currentTileThreatened = true;
	candidate.sameDay = true;
	candidate.changedTile = true;
	candidate.singleHero = true;
	candidate.visibleDestination = true;
	candidate.unblocked = true;
	candidate.battleFree = true;
	candidate.safeTurns = 2;
	candidate.movementCost = 0.5f;
	return candidate;
}
}

TEST(Nullkiller2EscapeBehavior, AcceptsVisibleSafeSameDayRetreat)
{
	const auto result = NK2AI::evaluateEscapePathCandidate(safeCandidate());
	EXPECT_TRUE(result.accepted);
	EXPECT_GT(result.score, 0.0f);
}

TEST(Nullkiller2EscapeBehavior, DoesNotRunWhenHeroCanHoldItsCurrentTile)
{
	auto candidate = safeCandidate();
	candidate.currentTileThreatened = false;
	EXPECT_FALSE(NK2AI::evaluateEscapePathCandidate(candidate).accepted);
}

TEST(Nullkiller2EscapeBehavior, RejectsBlindEscapeAndUnsafeDestination)
{
	auto candidate = safeCandidate();
	candidate.visibleDestination = false;
	EXPECT_FALSE(NK2AI::evaluateEscapePathCandidate(candidate).accepted);
	candidate.visibleDestination = true;
	candidate.safeTurns = 0;
	EXPECT_FALSE(NK2AI::evaluateEscapePathCandidate(candidate).accepted);
}

TEST(Nullkiller2EscapeBehavior, RejectsCombatAndCourierEscape)
{
	auto candidate = safeCandidate();
	candidate.battleFree = false;
	EXPECT_FALSE(NK2AI::evaluateEscapePathCandidate(candidate).accepted);
	candidate.battleFree = true;
	candidate.singleHero = false;
	EXPECT_FALSE(NK2AI::evaluateEscapePathCandidate(candidate).accepted);
}

TEST(Nullkiller2EscapeBehavior, RejectsLaterBlockedOrZeroMovement)
{
	auto candidate = safeCandidate();
	candidate.sameDay = false;
	EXPECT_FALSE(NK2AI::evaluateEscapePathCandidate(candidate).accepted);
	candidate.sameDay = true;
	candidate.unblocked = false;
	EXPECT_FALSE(NK2AI::evaluateEscapePathCandidate(candidate).accepted);
	candidate.unblocked = true;
	candidate.movementCost = 0;
	EXPECT_FALSE(NK2AI::evaluateEscapePathCandidate(candidate).accepted);
}

TEST(Nullkiller2EscapeBehavior, PrefersMoreTimeBeforeDangerThenCheaperTravel)
{
	auto shortTerm = safeCandidate();
	shortTerm.safeTurns = 1;
	shortTerm.movementCost = 0.1f;
	auto longTerm = safeCandidate();
	longTerm.safeTurns = 2;
	longTerm.movementCost = 1.0f;
	EXPECT_GT(NK2AI::evaluateEscapePathCandidate(longTerm).score,
		NK2AI::evaluateEscapePathCandidate(shortTerm).score);
	longTerm.safeTurns = 1;
	EXPECT_LT(NK2AI::evaluateEscapePathCandidate(longTerm).score,
		NK2AI::evaluateEscapePathCandidate(shortTerm).score);
}

TEST(Nullkiller2EscapeBehavior, EscapeTierPrecedesOrdinaryOffenceAndGathering)
{
	EXPECT_LT(NK2AI::PriorityEvaluator::INSTADEFEND, NK2AI::PriorityEvaluator::ESCAPE);
	EXPECT_LT(NK2AI::PriorityEvaluator::ESCAPE, NK2AI::PriorityEvaluator::KILL);
	EXPECT_LT(NK2AI::PriorityEvaluator::ESCAPE, NK2AI::PriorityEvaluator::EXPLORE_AND_GATHER);
}
}
