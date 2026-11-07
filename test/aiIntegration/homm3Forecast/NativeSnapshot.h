// SPDX-License-Identifier: CC0-1.0
#pragma once
#include "OwnershipPrefix.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/Unit.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/bonuses/BonusList.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace HoMM3Forecast
{
class UnsupportedSnapshot : public std::runtime_error
{
public:
	using std::runtime_error::runtime_error;
};

/// Requires every authoritative new-round and activation event, starting at battle setup.
class PhaseHistory
{
	std::optional<std::int32_t> round;
	std::optional<UnitId> activeId;
	bool second = false;
public:
	void onNewRound(std::int32_t value)
	{
		if(round && value <= *round)
			throw UnsupportedSnapshot("HoMM3 phase: duplicate or reversed round event");
		round = value;
		activeId.reset();
		second = false;
	}
	void observeActivation(const CPlayerBattleCallback & callback)
	{
		if(!round || *round != callback.battleGetRound() || !callback.battleActiveUnit())
			throw UnsupportedSnapshot("HoMM3 phase: missing round or activation event");
		const auto * active = callback.battleActiveUnit();
		const auto phase = active->battleQueuePhase(0);
		if(phase == battle::BattlePhases::WAIT || phase == battle::BattlePhases::WAIT_MORALE)
			second = true;
		activeId = active->unitId();
	}
	bool secondPhase(const CPlayerBattleCallback & callback) const
	{
		if(!round || *round != callback.battleGetRound() || !activeId || !callback.battleActiveUnit()
			|| *activeId != callback.battleActiveUnit()->unitId())
			throw UnsupportedSnapshot("HoMM3 phase: incomplete activation history");
		return second;
	}
};

inline Side sourceSide(BattleSide side)
{
	if(side == BattleSide::ATTACKER) return Side::ATTACKER;
	if(side == BattleSide::DEFENDER) return Side::DEFENDER;
	throw UnsupportedSnapshot("HoMM3 snapshot: unit has no combat side");
}

inline std::int32_t statusTurns(const battle::Unit & unit, SpellID spell, BonusType type)
{
	std::optional<std::int32_t> duration;
	const auto selector = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)).And(Selector::type()(type));
	for(const auto & bonus : *unit.getAllBonuses(selector))
	{
		if(!Bonus::NTurns(bonus.get()) || bonus->turnsRemain <= 0)
			throw UnsupportedSnapshot("HoMM3 snapshot: unsupported active-status duration");
		if(duration && *duration != bonus->turnsRemain)
			throw UnsupportedSnapshot("HoMM3 snapshot: inconsistent active-status durations");
		duration = bonus->turnsRemain;
	}
	return duration.value_or(0);
}

/// Test-only adapter for retained initial ordinary troop slots. It never reads heroes.
inline ForecastInput captureInitialTroops(const CPlayerBattleCallback & callback, const PhaseHistory & history)
{
	ForecastInput result;
	result.secondPhase = history.secondPhase(callback);
	result.actingSide = sourceSide(callback.battleActiveUnit()->unitSide());
	auto visible = callback.battleGetUnitsIf([](const battle::Unit *) { return true; });
	std::sort(visible.begin(), visible.end(), [](const auto * lhs, const auto * rhs)
	{
		if(lhs->unitSide() != rhs->unitSide()) return lhs->unitSide() < rhs->unitSide();
		return lhs->unitSlot() < rhs->unitSlot();
	});
	std::array<std::int32_t, 2> nextIndex{0, 0};
	std::array<int, 2> previousSlot{-1, -1};
	for(const auto * unit : visible)
	{
		if(!unit->unitSlot().validSlot() || unit->isClone() || unit->isGhost() || unit->isTurret()
			|| unit->isCatapult() || unit->isBallista() || unit->isFirstAidTent() || unit->isAmmoCart())
			throw UnsupportedSnapshot("HoMM3 snapshot: created/clone/ghost/machine lifecycle is not mapped");
		ForecastUnit projected;
		projected.order.id = unit->unitId();
		projected.order.owningSide = sourceSide(unit->unitSide());
		const auto side = static_cast<std::size_t>(projected.order.owningSide);
		if(previousSlot[side] == unit->unitSlot().getNum())
			throw UnsupportedSnapshot("HoMM3 snapshot: duplicate initial army slot");
		previousSlot[side] = unit->unitSlot().getNum();
		projected.order.sourceIndex = nextIndex[side]++;
		projected.order.effectiveSpeed = unit->getInitiative(0);
		projected.order.immobilized = !unit->alive();
		projected.order.done = unit->moved() || unit->defended();
		projected.order.waiting = !result.secondPhase && unit->waited();
		projected.liveHealth = unit->getAvailableHealth();
		projected.controllingSide = projected.order.owningSide;
		if(unit->alive())
		{
			projected.order.sourceSpell62Turns = statusTurns(*unit, SpellID::BLIND, BonusType::NOT_ACTIVE);
			projected.order.sourceSpell70Turns = statusTurns(*unit, SpellID::STONE_GAZE, BonusType::NOT_ACTIVE);
			projected.order.sourceSpell74Turns = statusTurns(*unit, SpellID::PARALYZE, BonusType::NOT_ACTIVE);
			for(const auto & bonus : *unit->getAllBonuses(Selector::type()(BonusType::NOT_ACTIVE)))
				if(bonus->source != BonusSource::SPELL_EFFECT
					|| (bonus->sid != BonusSourceID(SpellID(SpellID::BLIND))
						&& bonus->sid != BonusSourceID(SpellID(SpellID::STONE_GAZE))
						&& bonus->sid != BonusSourceID(SpellID(SpellID::PARALYZE))))
					throw UnsupportedSnapshot("HoMM3 snapshot: unsupported incapacitation source");
			const auto berserk = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BERSERK)))
				.And(Selector::type()(BonusType::ATTACKS_NEAREST_CREATURE));
			for(const auto & bonus : *unit->getAllBonuses(berserk))
			{
				if(bonus->duration != BonusDuration::UNTIL_OWN_ATTACK)
					throw UnsupportedSnapshot("HoMM3 snapshot: unsupported Berserk lifetime");
				projected.berserk = true;
			}
			if(unit->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE) != projected.berserk)
				throw UnsupportedSnapshot("HoMM3 snapshot: unsupported Berserk source");
			const bool hypnotized = statusTurns(*unit, SpellID::HYPNOTIZE, BonusType::HYPNOTIZED) != 0;
			if(unit->isHypnotized() != hypnotized)
				throw UnsupportedSnapshot("HoMM3 snapshot: unsupported controller source");
			projected.controllingSide = sourceSide(unit->controllingSide());
		}
		result.units.push_back(projected);
	}
	return result;
}
}
