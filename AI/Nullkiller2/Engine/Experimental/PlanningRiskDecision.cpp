/* GPL-2.0-or-later; see license.txt. Qualification-gated consumer. */
#include "../../StdInc.h"
#include "PlanningRiskDecision.h"
#include "../Nullkiller.h"
#include "../ArmyLossPolicy.h"
#include "../../../../lib/mapObjects/CGTownInstance.h"
#include "../../../../lib/CPlayerState.h"
#include "../../../../lib/mapObjects/CGCreature.h"
#include "../../../../lib/mapObjects/CGHeroInstance.h"

namespace NK2AI::experimental
{
bool captureNeutralLossPolicy(const Nullkiller & ai, const CGHeroInstance & hero, CapturedRiskRequest & request)
{
	const auto * player = ai.cc->getPlayerState(ai.playerID);
	if(!player || !ai.settings || hero.getOwner() != ai.playerID) return false;
	std::map<CreatureID, uint64_t> ownedCounts;
	const auto addArmy = [&](const CCreatureSet & army)
	{
		for(const auto & slot : army.Slots())
			ownedCounts[slot.second->getCreatureID()] += slot.second->getCount();
	};
	for(const auto * owned : ai.cc->getHeroesInfo()) addArmy(*owned);
	for(const auto * owned : ai.cc->getTownsInfo()) addArmy(*owned);
	float heroPower = 0;
	float totalPower = 0;
	std::set<CreatureID> types;
	for(const auto & slot : hero.Slots())
	{
		heroPower += slot.second->getPower();
		const auto id = slot.second->getCreatureID();
		if(types.insert(id).second)
			totalPower += id.toCreature()->getAIValue() * ownedCounts[id];
	}
	const float ratio = totalPower > 0 ? heroPower / totalPower : 0;
	request.contextualLossTolerance = std::clamp(static_cast<double>(maxWillingArmyLoss(
		ai.settings->getMaxArmyLossTarget(), ratio, player->daysWithoutCastle.has_value())), 0.0, 1.0);
	// Hexfloat is lossless and independent of decimal precision defaults.
	std::ostringstream descriptor;
	descriptor << ":loss-policy:" << std::hexfloat << request.contextualLossTolerance;
	request.key.capturedState += descriptor.str();
	return true;
}

PlanningRiskDecision decideNeutralRisk(const Nullkiller & ai, const CGHeroInstance & hero,
	const CCreatureSet * army, const int3 & destination, PlanningRiskRoute route,
	bool permitDefeatRecommendation)
{
	PlanningRiskDecision result;
	if(!ai.hasPlanningRisk()) return result;
	route.usesCurrentHeroArmy = army == &hero;
	if(!route.usesCurrentHeroArmy || !route.knownEarlierDanger || route.earlierDanger
		|| route.earlierArmyLoss || route.spentMana || hero.getOwner() != ai.playerID)
		return result;
	const auto objects = ai.cc->getVisitableObjs(destination, false);
	// A guard of another object, or overlapping encounter, is not the captured
	// single neutral fight. No guessed split of aggregate danger is permitted.
	if(objects.size() != 1) return result;
	const auto * target = dynamic_cast<const CGCreature *>(objects.front());
	if(!target || typeid(*target) != typeid(CGCreature) || target->ID != Obj::MONSTER
		|| !target->appearance || target->visitablePos() != destination
		|| (target->getOwner() != PlayerColor::NEUTRAL && target->getOwner() != PlayerColor::UNFLAGGABLE))
		return result;
	for(const auto * guard : ai.cc->getGuardingCreatures(destination))
		if(guard != target) return result;
	const auto record = ai.findPlanningRisk(hero.id, target->id, route);
	if(!record || !record->decisionEvidence || !record->contextualSummaryReady) return result;

	result.recommendation = record->contextualAssessment.recommendation;
	if(!permitDefeatRecommendation && result.recommendation == RiskRecommendation::LIKELY_DEFEAT)
		result.recommendation = RiskRecommendation::UNKNOWN;
	result.sampledLoss = record->contextualWorstLoss;
	return result;
}
}
