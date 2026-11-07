// SPDX-License-Identifier: CC0-1.0
#pragma once
#include "Lifecycle.h"
#include "NativeSnapshot.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/callback/IBattleEventsReceiver.h"

namespace HoMM3Forecast
{
/// Test-only consumer of the public battle-AI event contract; never reads a hero or game state.
class NativeLifecycle : public IBattleEventsReceiver
{
	const CPlayerBattleCallback & callback;
	std::optional<BattleID> battle;
	RoundPhase phase;
	std::optional<UnitId> planningUnit;
	InitialRoster roster;
	bool rosterSupported = false;

	std::vector<InitialIdentity> identities() const
	{
		std::vector<InitialIdentity> result;
		for(const auto * unit : callback.battleGetUnitsIf([](const battle::Unit *) { return true; }))
		{
			if(!unit->unitSlot().validSlot() || unit->isClone() || unit->isTurret() || unit->isCatapult()
				|| unit->isBallista() || unit->isFirstAidTent() || unit->isAmmoCart())
				throw UnsupportedSnapshot("HoMM3 lifecycle: created/clone/machine identity is not mapped");
			result.push_back({unit->unitId(), sourceSide(unit->unitSide()), unit->unitSlot().getNum()});
		}
		return result;
	}
	void observeActive()
	{
		const auto * active = callback.battleActiveUnit();
		if(!active) return;
		const auto currentPhase = active->battleQueuePhase(0);
		phase.beforeUnitActionOrOwnActivation(callback.battleGetRound(),
			currentPhase == battle::BattlePhases::WAIT || currentPhase == battle::BattlePhases::WAIT_MORALE);
	}
public:
	explicit NativeLifecycle(const CPlayerBattleCallback & value) : callback(value) {}
	void attach(const BattleID & id)
	{
		battle = id;
		planningUnit.reset();
		phase.attach(callback.battleGetRound());
		rosterSupported = false;
		try { roster.bind(identities()); rosterSupported = true; }
		catch(const UnsupportedSnapshot &) { /* Unsupported identity is a normal native state. */ }
	}
	void battleStart(const BattleID & id, const CCreatureSet *, const CCreatureSet *, int3,
		const CGHeroInstance *, const CGHeroInstance *, BattleSide, bool) override { attach(id); }
	void battleNewRound(const BattleID & id) override
	{
		if(battle == id) { planningUnit.reset(); phase.newRound(callback.battleGetRound()); }
	}
	void actionStarted(const BattleID & id, const BattleAction & action) override
	{
		if(battle != id) return;
		planningUnit.reset();
		if(!action.isUnitAction()) return;
		const auto * active = callback.battleActiveUnit();
		if(!active || active->unitId() != action.stackNumber)
			throw std::logic_error("HoMM3 lifecycle: action does not match the active unit");
		observeActive();
	}
	void ownActivation(const BattleID & id)
	{
		if(battle != id) return;
		const auto * active = callback.battleActiveUnit();
		if(!active || active->controllingSide() != callback.battleGetMySide())
			throw std::logic_error("HoMM3 lifecycle: activation is not controlled by this player");
		observeActive();
		planningUnit = active->unitId();
	}
	void battleEnd(const BattleID & id, const BattleResult *, QuestionID) override
	{
		if(battle == id) { battle.reset(); planningUnit.reset(); phase.end(); rosterSupported = false; }
	}
	std::optional<bool> secondPhase() const
	{
		if(!battle) return std::nullopt;
		return phase.secondPhase(callback.battleGetRound());
	}
	ForecastInput snapshot() const
	{
		if(!battle)
			throw UnsupportedSnapshot("HoMM3 lifecycle: battle is not attached");
		const auto * active = callback.battleActiveUnit();
		if(!active || active->controllingSide() != callback.battleGetMySide()
			|| planningUnit != active->unitId())
			throw UnsupportedSnapshot("HoMM3 lifecycle: snapshot requires an observed own planning activation");
		const auto second = secondPhase();
		if(!second || !rosterSupported)
			throw UnsupportedSnapshot("HoMM3 lifecycle: phase or initial identity is not known");
		roster.validate(identities());
		auto result = captureInitialTroops(callback, *second);
		for(const auto & unit : result.units)
			if(unit.order.sourceIndex != roster.sourceIndex(unit.order.id))
				throw std::logic_error("HoMM3 lifecycle: compact slot changed");
		return result;
	}
};
}
