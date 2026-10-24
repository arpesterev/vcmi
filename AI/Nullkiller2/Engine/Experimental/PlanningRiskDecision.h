/* GPL-2.0-or-later; see license.txt. Qualification-gated consumer. */
#pragma once
#include "PlanningRiskTable.h"

VCMI_LIB_NAMESPACE_BEGIN
class CCreatureSet;
VCMI_LIB_NAMESPACE_END

namespace NK2AI
{
class Nullkiller;
namespace experimental
{
// Called only inside the held planning read scope. Returned values are copies,
// not authority to reuse a publication after a game-state change.
struct PlanningRiskDecision
{
	RiskRecommendation recommendation = RiskRecommendation::UNKNOWN;
	double sampledLoss = 1;
};
// Capture and recapture under the same actual game-state reader lock. Appends
// policy inputs to source identity, not the reusable combat-physics memo key.
bool captureNeutralLossPolicy(const Nullkiller & ai, const CGHeroInstance & hero, CapturedRiskRequest & request);
PlanningRiskDecision decideNeutralRisk(const Nullkiller & ai, const CGHeroInstance & hero,
	const CCreatureSet * army, const int3 & destination, PlanningRiskRoute route,
	bool permitDefeatRecommendation);
}
}
