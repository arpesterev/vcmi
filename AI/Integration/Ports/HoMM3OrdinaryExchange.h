/*
 * Ordinary exchange branch from sushi-shi/homm3-decomp e12906accf96c2662b1abadbc49ddc111991bf21,
 * src/army.cpp damage helpers and src/ai_tactical.cpp simulateAttack/getSimpleAttackEffect.
 * Source reconstruction: CC0-1.0. VCMI adaptation: GPL-2.0-or-later.
 */
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace AIIntegration::Ports::HoMM3
{
// Values are source per-creature combat values for each INCOMING channel.
// A producer must validate ordinary source semantics, not fill these with native estimates.
struct SourceOrdinaryUnit
{
	int32_t count = 1, hitPoints = 1, topWound = 0;
	int32_t minimumDamage = 1, maximumDamage = 1;
	bool blessed = false, cursed = false;
	int32_t blessAmount = 0, curseAmount = 0;
	bool canShoot = false, doubleAttack = false;
	std::array<double, 2> lossUnitValue{1, 1}; // melee, ranged
};

// Pair-specific source-adjusted stats and factors. No jousting, hatred, elemental
// opposition, Psychic/Magic elemental reductions, residual Blind/Paralyze,
// Slayer/Frenzy/Behemoth special adjustment, specialty or fire shield.
struct SourceOrdinaryAttack
{
	int32_t attack = 0, defense = 0;
	bool lucky = false, hasController = false;
	float controllerSkillFactor = 0;
	bool wallPenalty = false, distancePenalty = false, meleeShooterPenalty = false;
	double defenderReduction = 1;
};

struct SourceOrdinaryExchangeInput
{
	SourceOrdinaryUnit attacker, defender;
	std::array<SourceOrdinaryAttack, 2> attack; // melee, ranged
	SourceOrdinaryAttack retaliation;
	bool ranged = false, canRetaliate = false, retaliationPolicy = true, killsOnly = false;
	bool simulated = false;
	int32_t attackerExpectedDamage = 0, defenderExpectedDamage = 0;
};

struct SourceOrdinaryExchangeResult
{
	int32_t value = 0;
	int32_t attackerHealth = 0, defenderHealth = 0;
};

namespace OrdinaryExchangeDetail
{
inline std::optional<int32_t> integer(double value)
{
	if(!std::isfinite(value) || value < 0 || value > INT32_MAX)
		return std::nullopt;
	return static_cast<int32_t>(value);
}

inline std::optional<int32_t> health(const SourceOrdinaryUnit & unit)
{
	if(unit.count < 0 || unit.hitPoints < 1 || unit.topWound < 0 || unit.topWound >= unit.hitPoints
		|| (unit.count == 0 && unit.topWound != 0) || int64_t{unit.count} * unit.hitPoints > INT32_MAX
		|| unit.minimumDamage < 1 || unit.maximumDamage < unit.minimumDamage
		|| int64_t{unit.minimumDamage} + unit.maximumDamage > INT32_MAX
		|| unit.blessAmount < 0 || unit.curseAmount < 0
		|| int64_t{unit.blessAmount} + unit.maximumDamage > INT32_MAX
		|| (unit.blessed && unit.cursed))
		return std::nullopt;
	for(double value : unit.lossUnitValue)
		if(!std::isfinite(value) || value < 0 || value > INT32_MAX)
			return std::nullopt;
	return static_cast<int32_t>(int64_t{unit.count} * unit.hitPoints - unit.topWound);
}

inline bool valid(const SourceOrdinaryAttack & facts)
{
	return facts.attack >= 0 && facts.defense >= 0
		&& std::isfinite(facts.controllerSkillFactor) && facts.controllerSkillFactor >= 0
		&& facts.controllerSkillFactor <= 1 && std::isfinite(facts.defenderReduction)
		&& facts.defenderReduction >= 0 && facts.defenderReduction <= 1;
}

inline std::optional<int32_t> damage(const SourceOrdinaryUnit & unit, int32_t survivingHealth,
	int32_t targetLiveHealth, const SourceOrdinaryAttack & facts, bool ranged)
{
	// aiGetAttackDamage's ceil is a signed-long addition in the source.
	const int64_t numerator = int64_t{unit.hitPoints} + survivingHealth - 1;
	if(int64_t{unit.hitPoints} + survivingHealth > INT32_MAX || numerator < 0 || !valid(facts))
		return std::nullopt;
	const int32_t troops = static_cast<int32_t>(numerator / unit.hitPoints);
	const double average = unit.blessed ? double(unit.blessAmount) + unit.maximumDamage
		: unit.cursed ? std::max(unit.minimumDamage - unit.curseAmount, 1)
		: (unit.minimumDamage + unit.maximumDamage) / 2.0;
	const auto base = integer(troops * average);
	if(!base) return std::nullopt;
	int32_t bonus = facts.lucky ? *base : 0;
	if(facts.attack >= facts.defense)
	{
		const double factor = std::min((facts.attack - facts.defense) * 0.05, 3.0);
		const auto increased = integer(*base * factor + bonus);
		if(!increased) return std::nullopt;
		bonus = *increased;
	}
	if(facts.hasController)
	{
		// Source controller factors and both operands are float, before long conversion.
		const auto adjusted = integer(facts.controllerSkillFactor * static_cast<float>(*base) + static_cast<float>(bonus));
		if(!adjusted) return std::nullopt;
		bonus = *adjusted;
	}
	const int64_t augmented = int64_t{*base} + bonus;
	if(augmented > INT32_MAX) return std::nullopt;
	double reduction = facts.defense > facts.attack
		? std::max(1.0 - (facts.defense - facts.attack) * 0.025, 0.3) : 1.0;
	if(ranged)
	{
		if(facts.wallPenalty) reduction *= 0.5;
		if(facts.distancePenalty) reduction *= 0.5;
	}
	else if(facts.meleeShooterPenalty) reduction *= 0.5;
	const auto reduced = integer(static_cast<int32_t>(augmented) * reduction);
	if(!reduced) return std::nullopt;
	const auto defended = integer(*reduced * facts.defenderReduction);
	if(!defended) return std::nullopt;
	int32_t result = std::max(*defended, 1);
	if(ranged && unit.doubleAttack)
	{
		if(result > INT32_MAX / 2) return std::nullopt;
		result += result;
	}
	return std::min(result, targetLiveHealth);
}

inline std::optional<int32_t> loss(const SourceOrdinaryUnit & unit, bool ranged, int32_t damage, bool killsOnly)
{
	const int32_t wound = damage % unit.hitPoints >= unit.hitPoints - unit.topWound ? unit.topWound : 0;
	if(int64_t{wound} + damage > INT32_MAX) return std::nullopt;
	const double value = killsOnly ? 1000.0 : unit.lossUnitValue[ranged ? 1 : 0];
	return integer((wound + damage) * value / unit.hitPoints);
}
}

inline std::optional<SourceOrdinaryExchangeResult> evaluateSourceOrdinaryExchange(const SourceOrdinaryExchangeInput & input)
{
	using namespace OrdinaryExchangeDetail;
	const auto ourLive = health(input.attacker), enemyLive = health(input.defender);
	if(!ourLive || !enemyLive || *ourLive == 0 || input.attackerExpectedDamage < 0 || input.defenderExpectedDamage < 0)
		return std::nullopt;
	int32_t ourInitial = *ourLive, enemyInitial = *enemyLive;
	if(input.simulated)
	{
		ourInitial = std::max(0, ourInitial - input.attackerExpectedDamage);
		enemyInitial = std::max(0, enemyInitial - input.defenderExpectedDamage);
		if(ourInitial == 0 || enemyInitial == 0)
			return SourceOrdinaryExchangeResult{0, ourInitial, enemyInitial};
	}
	int32_t ours = ourInitial, theirs = enemyInitial;
	const bool ranged = input.ranged && input.attacker.canShoot;
	const auto strike = [&](const SourceOrdinaryUnit & attacker, int32_t hp, int32_t targetLive,
		int32_t & victim, const SourceOrdinaryAttack & profile, bool shot)
	{
		const auto dealt = damage(attacker, hp, targetLive, profile, shot);
		if(!dealt) return false;
		victim = std::max(0, victim - *dealt);
		return true;
	};
	if(!strike(input.attacker, ours, *enemyLive, theirs, input.attack[ranged ? 1 : 0], ranged))
		return std::nullopt;
	if(ours != 0 && theirs > 0 && !ranged)
	{
		if(input.canRetaliate && input.retaliationPolicy)
			if(!strike(input.defender, theirs, *ourLive, ours, input.retaliation, false))
				return std::nullopt;
		if(ours > 0 && theirs > 0 && input.attacker.doubleAttack)
			if(!strike(input.attacker, ours, *enemyLive, theirs, input.attack[0], false))
				return std::nullopt;
	}
	const auto enemyValue = loss(input.defender, ranged, enemyInitial - theirs, input.killsOnly);
	const auto ourValue = loss(input.attacker, ranged, ourInitial - ours, false);
	if(!enemyValue || !ourValue) return std::nullopt;
	return SourceOrdinaryExchangeResult{*enemyValue - *ourValue, ours, theirs};
}
}
