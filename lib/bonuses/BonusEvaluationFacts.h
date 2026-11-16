/*
 * BonusEvaluationFacts.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include "BonusLimiterContext.h"

/// Value-only definition closure. Both capture calls require the same caller-owned
/// consistent definition, node, battle and session lifetime/read epoch.
/// No lock is acquired here; owned previews must reuse the captured facts.
class DLL_LINKAGE BonusEvaluationFactCapture
{
	std::shared_ptr<const CapturedCreatureSourceFactionTable> creatureFactions;
	std::map<CreatureID, std::set<CreatureID>> upgradeAncestors;
	std::map<FactionID, std::set<ETerrainId>> factionTerrains;

	BonusEvaluationFactCapture() = default;
public:
	static BonusEvaluationFactCapture captureDefinitions();
	BonusLimiterFacts captureNode(const CBonusSystemNode & node) const;
};
