/*
 * PlanningBattleSnapshot.h, part of VCMI engine
 * GPL-2.0-or-later; see license.txt.
 */
#pragma once
#include "../../../../lib/battle/BattleLayout.h"
#include "../../../../lib/constants/EntityIdentifiers.h"
#include "../../../../lib/int3.h"

VCMI_LIB_NAMESPACE_BEGIN
class IGameInfoCallback;
class CArmedInstance;
class CGHeroInstance;
class CGCreature;
class CBattleInfoCallback;
VCMI_LIB_NAMESPACE_END

namespace NK2AI::experimental
{
// Explicit context, not a guess of the authoritative RNG's future choice.
// The caller must enumerate compatible visible variants or return unknown.
struct PlanningBattleContext
{
	int3 tile;
	TerrainId terrain;
	BattleField battlefield;
	BattleLayout layout;
};

// Owns copied armies, bonus definitions and callback lifetime. Engine battle
// initialization/destruction only touches these copies. No serialization of the
// live game, no server RNG, no attachment to a live army or hero bonus graph.
class PlanningBattleSnapshot
{
	struct Impl;
	std::unique_ptr<Impl> impl;
	explicit PlanningBattleSnapshot(std::unique_ptr<Impl> implementation);
	static std::unique_ptr<PlanningBattleSnapshot> createOwned(
		const IGameInfoCallback & visibleInfo, const CArmedInstance & attacker,
		const CArmedInstance & defender, const PlanningBattleContext & context, bool initializeBattle, std::string * unknownReason);
public:
	~PlanningBattleSnapshot();
	PlanningBattleSnapshot(const PlanningBattleSnapshot &) = delete;
	PlanningBattleSnapshot & operator=(const PlanningBattleSnapshot &) = delete;

	static std::unique_ptr<PlanningBattleSnapshot> create(
		const IGameInfoCallback & visibleInfo, const CArmedInstance & attacker,
		const CArmedInstance & defender, const PlanningBattleContext & context, std::string * unknownReason = nullptr);
	// Ordinary neutral encounter adapter. Applies engine stack splitting on
	// detached copies, including deterministic same-faction single upgrades.
	static std::unique_ptr<PlanningBattleSnapshot> createNeutralEncounter(
		const IGameInfoCallback & visibleInfo, const CGHeroInstance & attacker,
		const CGCreature & defender, const PlanningBattleContext & context, std::string * unknownReason = nullptr);
	const CBattleInfoCallback & battle() const;
	const IGameInfoCallback & frozenGameInfo() const;
	const std::string & capturedSourceState() const;
	std::string physicsState() const;
	bool hasUnmodelledOwnMagic() const;
};
}
