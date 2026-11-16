/*
 * FHeroes2Strength.h, part of VCMI engine
 * GPL-2.0-or-later.
 * Derived from ihhub/fheroes2@0dbfe9f386f33c2500842f4a59cd284d7f03d541
 * monster_info.cpp::getMonsterBaseStrength, Monster::GetMonsterStrength.
 */
#pragma once

#include <cmath>
#include <stdexcept>

namespace AIIntegration::Ports::FHeroes2
{
enum class StrengthSpell { NONE, DISABLE, WEAK };

struct StrengthFacts
{
	int minimumDamage = 0;
	int maximumDamage = 0;
	int hitPoints = 0;
	int speed = 4;
	bool archer = false;
	bool noRetaliation = false;
	bool doubleShooting = false;
	bool doubleMelee = false;
	bool doubleDamageUndead = false;
	bool twoCellAttack = false;
	bool unlimitedRetaliation = false;
	bool allAdjacentAttack = false;
	bool areaShot = false;
	bool noMeleePenalty = false;
	bool flying = false;
	bool enemyHalving = false;
	bool soulEater = false;
	bool healthDrain = false;
	StrengthSpell spell = StrengthSpell::NONE;
	int spellChance = 0;
};

inline double monsterBaseStrength(const StrengthFacts & facts)
{
	if(facts.minimumDamage < 0
		|| facts.maximumDamage < facts.minimumDamage || facts.hitPoints <= 0
		|| facts.speed < 0 || facts.spellChance < 0 || facts.spellChance > 100
		|| (facts.doubleShooting && !facts.archer)
		|| (facts.spell != StrengthSpell::NONE && facts.spell != StrengthSpell::DISABLE && facts.spell != StrengthSpell::WEAK))
		throw std::invalid_argument("Invalid FHeroes2 strength facts");
	const double effectiveHealth = facts.hitPoints * (facts.noRetaliation ? 1.4 : 1.0);
	double damage = (static_cast<double>(facts.minimumDamage) + facts.maximumDamage) / 2.0;
	if(facts.doubleShooting)
		damage *= 2;
	else if(facts.doubleMelee)
		damage *= facts.noRetaliation ? 2 : 1.75;
	if(facts.doubleDamageUndead) damage *= 1.15;
	if(facts.twoCellAttack) damage *= 1.2;
	if(facts.unlimitedRetaliation) damage *= 1.25;
	if(facts.allAdjacentAttack || facts.areaShot) damage *= 1.3;
	double special = 1;
	if(facts.archer) special += facts.noMeleePenalty ? 0.5 : 0.4;
	if(facts.flying) special += 0.3;
	if(facts.enemyHalving) special += 1;
	if(facts.soulEater) special += 2;
	if(facts.healthDrain) special += 0.3;
	if(facts.spell == StrengthSpell::DISABLE) special += facts.spellChance / 100.0;
	else if(facts.spell == StrengthSpell::WEAK) special += facts.spellChance / 100.0 / 10.0;
	const int speedDifference = facts.speed - 4; // Speed::AVERAGE in the pinned source.
	special += speedDifference < 0 ? speedDifference * 0.1 : speedDifference * 0.05;
	return std::sqrt(damage * effectiveHealth) * special;
}

inline double stackStrength(const StrengthFacts & facts, int attack, int defense, int count)
{
	if(attack < 0 || defense < 0 || count < 0)
		throw std::invalid_argument("Invalid FHeroes2 stack strength facts");
	return (1.0 + attack * 0.1 + defense * 0.05) * monsterBaseStrength(facts) * count;
}
}
