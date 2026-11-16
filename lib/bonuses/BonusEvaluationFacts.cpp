/*
 * BonusEvaluationFacts.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "BonusEvaluationFacts.h"
#include "lib/CCreatureHandler.h"
#include "lib/CStack.h"
#include "lib/GameLibrary.h"
#include "lib/entities/faction/CTownHandler.h"
#include "lib/battle/BattleInfo.h"

BonusEvaluationFactCapture BonusEvaluationFactCapture::captureDefinitions()
{
	BonusEvaluationFactCapture result;
	std::map<CreatureID, FactionID> creatureFactions;
	std::map<CreatureID, std::set<CreatureID>> directUpgrades;
	LIBRARY->creatures()->forEach([&](const Creature * creature, bool &)
	{
		const auto id = creature->getId();
		creatureFactions.emplace(id, creature->getFactionID());
		result.upgradeAncestors.emplace(id, std::set<CreatureID>{});
		directUpgrades.emplace(id, id.toCreature()->upgrades);
	});
	// Match isMyDirectOrIndirectUpgrade, including self-membership only when a
	// cycle reaches the starting creature. Traversal uses owned edges only.
	for(const auto & [ancestor, edges] : directUpgrades)
	{
		std::vector<CreatureID> pending(edges.begin(), edges.end());
		std::set<CreatureID> visited;
		while(!pending.empty())
		{
			const auto id = pending.back();
			pending.pop_back();
			if(!visited.insert(id).second)
				continue;
			result.upgradeAncestors.at(id).insert(ancestor);
			const auto & next = directUpgrades.at(id);
			pending.insert(pending.end(), next.begin(), next.end());
		}
	}
	LIBRARY->factions()->forEach([&](const Faction * faction, bool &)
	{
		// CFaction::isNativeTerrain is membership in this vector, including
		// any sentinel terrain IDs present in the loaded definition.
		const auto & terrains = dynamic_cast<const CFaction &>(*faction).nativeTerrains;
		result.factionTerrains.emplace(faction->getId(), std::set<ETerrainId>(terrains.begin(), terrains.end()));
	});
	result.creatureFactions = CapturedCreatureSourceFactionTable::capture(creatureFactions);
	return result;
}

BonusLimiterFacts BonusEvaluationFactCapture::captureNode(const CBonusSystemNode & node) const
{
	Bonus subject;
	BonusList empty;
	BonusLimitationContext raw{subject, node, empty, empty};
	NativeBonusLimiterContext context(raw);
	BonusLimiterFacts result;
	result.type = node.getNodeType();
	result.originalOwner = node.getOwner();
	result.capturedCreatureSourceFactions = creatureFactions;
	result.creature = context.hasCreature();
	if(result.creature)
	{
		result.creatureID = context.creatureID();
		const auto ancestors = upgradeAncestors.find(*result.creatureID);
		if(ancestors == upgradeAncestors.end())
			throw std::invalid_argument("Bonus evaluation creature is missing from captured definitions");
		result.creatureLevel = context.creatureLevel();
		result.creatureAlignment = context.creatureAlignment();
		result.upgradeAncestors = ancestors->second;
	}
	result.stackInstance = context.hasStackInstance();
	if(result.stackInstance)
	{
		result.commander = context.isCommander();
		if(!*result.commander)
			result.rank = context.rank();
	}
	result.battleStack = context.hasBattleStack();
	if(result.battleStack)
	{
		result.hexes = context.hexes();
		result.validPosition = context.validPosition();
		result.adjacentCreatures.emplace();
		if(*result.validPosition)
		{
			const auto & stack = dynamic_cast<const CStack &>(node);
			for(const auto * unit : stack.getBattle()->battleAdjacentUnits(&stack))
				result.adjacentCreatures->push_back(unit->creatureId());
		}
	}
	result.currentTerrain = context.currentTerrain();
	result.factionProvider = context.hasFaction();
	if(result.factionProvider)
	{
		result.faction = context.faction();
		result.nativeTerrains = factionTerrains.at(*result.faction);
	}
	return result;
}
