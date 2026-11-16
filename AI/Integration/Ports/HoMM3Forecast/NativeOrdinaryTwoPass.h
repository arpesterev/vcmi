/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "OrdinaryTwoPass.h"
#include "NativeShooterPrefix.h"
#include "../HoMM3NativeExchangeFacts.h"

namespace HoMM3Forecast
{
struct NativeForecastPolicy
{
	int difficulty = 0;
	std::array<bool, 2> human{};
};

namespace NativeOrdinaryTwoPassDetail
{
inline bool sameSnapshot(const ForecastInput & a, const ForecastInput & b)
{
	if(a.actingSide != b.actingSide || a.secondPhase != b.secondPhase || a.units.size() != b.units.size())
		return false;
	for(size_t i = 0; i < a.units.size(); ++i)
	{
		const auto & x = a.units[i];
		const auto & y = b.units[i];
		if(x.order != y.order || x.controllingSide != y.controllingSide || x.berserk != y.berserk
			|| x.catapult != y.catapult || x.liveHealth != y.liveHealth)
			return false;
	}
	return true;
}
}

/// Shadow reconstruction of currently effective public records, not a native execution certificate.
/// Caller owns the standard battle thread and prevents concurrent mutation/lifetime changes.
/// Does not support background GUI autofight or certify absence of latent proc records.
inline std::optional<OrdinaryTwoPassInput> captureNativeOrdinaryFacts(
	const CPlayerBattleCallback & callback, const NativeLifecycle & lifecycle,
	const NativeForecastPolicy & policy, Side requestedSide, bool checkingSurrender)
{
	using Facts = AIIntegration::Ports::HoMM3::NativeExchangeFacts;
	if(!lifecycle.boundTo(callback) || policy.difficulty < 0 || policy.difficulty > 4
		|| (requestedSide != Side::ATTACKER && requestedSide != Side::DEFENDER))
		return std::nullopt;
	try
	{
		const auto before = lifecycle.snapshot();
		// Constructor validation only. Never call its midpoint damage estimators.
		const NativeShooterModel effectiveRecordValidation(callback);
		(void)effectiveRecordValidation;
		OrdinaryTwoPassInput result;
		result.actingSide = before.actingSide;
		result.secondPhase = before.secondPhase;
		result.requestedSide = requestedSide;
		result.checkingSurrender = checkingSurrender;
		result.difficulty = policy.difficulty;
		result.human = policy.human;
		std::map<UnitId, const battle::Unit *> native;
		std::map<UnitId, int> heads;
		std::optional<std::pair<int32_t, int32_t>> minima;
		if(callback.battleGetDefendedTown() || callback.battleTacticDist()) return std::nullopt;
		for(const auto * unit : callback.battleGetUnitsIf([](const battle::Unit *) { return true; }))
		{
			if(!native.emplace(unit->unitId(), unit).second || !unit->getPosition().isAvailable()
				|| unit->doubleWide() || unit->isSummoned() || unit->isClone()
				|| unit->controllingSide() != unit->unitSide() || unit->defended()
				|| unit->hasBonusOfType(BonusType::UNIT_DEFENDING)) return std::nullopt;
			heads.emplace(unit->unitId(), unit->getPosition().toInt());
			if(!unit->alive()) continue;
			if(!callback.battleCanShoot(unit)) return std::nullopt;
			const auto modifier = Facts::modifiers(callback, *unit);
			if(!modifier) return std::nullopt;
			if(!minima) minima = modifier;
			else
			{
				minima->first = std::min(minima->first, modifier->first);
				minima->second = std::min(minima->second, modifier->second);
			}
		}
		if(native.size() != before.units.size()) return std::nullopt;
		const auto lowest = minima.value_or(std::pair<int32_t, int32_t>{0, 0});
		for(const auto & state : before.units)
		{
			const auto found = native.find(state.order.id);
			if(found == native.end()) return std::nullopt;
			const auto * unit = found->second;
			if(sourceSide(unit->unitSide()) != state.order.owningSide
				|| unit->getAvailableHealth() != state.liveHealth) return std::nullopt;
			const auto facts = Facts::unit(callback, *unit, lowest.first, lowest.second);
			if(!facts) return std::nullopt;
			if(unit->alive())
			{
				const auto speed = Facts::groundSpeed(callback, *unit);
				if(!speed || *speed != state.order.effectiveSpeed) return std::nullopt;
			}
			const auto value = Ordinary::OrdinaryExchangeDetail::integer(
				static_cast<double>(state.liveHealth) * facts->lossUnitValue[facts->canShoot ? 1 : 0] / facts->hitPoints);
			if(!value || *value < 0) return std::nullopt;
			result.units.push_back({state, *facts, heads.at(state.order.id), 0, *value});
		}
		for(const auto & actor : result.units)
		{
			if(actor.state.liveHealth == 0) continue;
			for(const auto & target : result.units)
			{
				if(actor.state.order.owningSide == target.state.order.owningSide) continue;
				const auto pair = Facts::rangedAttack(callback, *native.at(actor.state.order.id), *native.at(target.state.order.id));
				if(!pair) return std::nullopt;
				result.attacks.emplace(std::pair{actor.state.order.id, target.state.order.id}, *pair);
			}
		}
		if(!NativeOrdinaryTwoPassDetail::sameSnapshot(before, lifecycle.snapshot())) return std::nullopt;
		for(const auto & [id, unit] : native)
			if(unit->getPosition().toInt() != heads.at(id)) return std::nullopt;
		return result;
	}
	catch(const UnsupportedSnapshot &)
	{
		return std::nullopt;
	}
}

// Forecast consumers retain full simulation validation; live choice does not
// require a supported future actor or a uniquely ordered future round.
inline std::optional<OrdinaryTwoPassInput> captureNativeOrdinaryTwoPass(
 const CPlayerBattleCallback & callback,const NativeLifecycle & lifecycle,
 const NativeForecastPolicy & policy,Side requestedSide,bool checkingSurrender)
{
 const auto result=captureNativeOrdinaryFacts(callback,lifecycle,policy,requestedSide,checkingSurrender);
 if(!result || !simulateOrdinaryTwoPass(*result))return std::nullopt;
 return result;
}

/// Explicit shadow call only; its result must not drive retreat or spell forecasts.
inline std::optional<OrdinaryTwoPassResult> evaluateNativeOrdinaryTwoPass(
	const CPlayerBattleCallback & callback, const NativeLifecycle & lifecycle,
	const NativeForecastPolicy & policy, Side requestedSide, bool checkingSurrender)
{
	const auto input = captureNativeOrdinaryTwoPass(callback, lifecycle, policy, requestedSide, checkingSurrender);
	if(!input) return std::nullopt;
	return simulateOrdinaryTwoPass(*input);
}
}
