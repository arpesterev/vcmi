/*
 * FHeroes2NativeStrength.h, part of VCMI engine
 * GPL-2.0-or-later.
 */
#pragma once

#include "FHeroes2Strength.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/CStack.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../lib/bonuses/BonusSelector.h"

#include <optional>

namespace AIIntegration::Ports::FHeroes2
{
inline std::optional<StrengthFacts> nativeStrengthFacts(const battle::Unit & unit)
{
	const auto * type = unit.unitType();
	if(!type || unit.hasBonusOfType(BonusType::ON_COMBAT_EVENT)
		|| unit.hasBonusOfType(BonusType::COMBAT_EVENT_TRIGGER)
		|| unit.isTurret() || unit.isCatapult() || unit.isBallista()
		|| unit.isFirstAidTent() || unit.isAmmoCart() || unit.isCommander())
		return std::nullopt;
	StrengthFacts facts;
	facts.minimumDamage = type->getBaseDamageMin();
	facts.maximumDamage = type->getBaseDamageMax();
	facts.hitPoints = type->getBaseHitPoints();
	facts.speed = type->getBaseSpeed();
	facts.archer = type->getBaseShots() > 0;
	if(facts.minimumDamage < 0 || facts.maximumDamage < facts.minimumDamage
		|| facts.hitPoints <= 0 || facts.speed < 0)
		return std::nullopt;
	const auto bonuses = type->getBonusBearer()->getAllBonuses(Selector::all);
	for(const auto & bonus : *bonuses)
	{
		if(bonus->limiter || bonus->updater || bonus->propagator || bonus->propagationUpdater)
			return std::nullopt;
		switch(bonus->type)
		{
		case BonusType::PRIMARY_SKILL: case BonusType::CREATURE_DAMAGE:
		case BonusType::STACK_HEALTH: case BonusType::STACKS_SPEED:
		case BonusType::SHOTS: case BonusType::SHOOTER:
		case BonusType::UNDEAD: case BonusType::NON_LIVING: case BonusType::LIVING:
		case BonusType::MIND_IMMUNITY: case BonusType::SPELL_IMMUNITY:
		case BonusType::MAGIC_RESISTANCE: case BonusType::DRAGON_NATURE:
		case BonusType::CHARGE_IMMUNITY: case BonusType::KING:
			break;
		case BonusType::FLYING: facts.flying = true; break;
		case BonusType::BLOCKS_RETALIATION: facts.noRetaliation = true; break;
		case BonusType::NO_MELEE_PENALTY: facts.noMeleePenalty = true; break;
		case BonusType::UNLIMITED_RETALIATIONS: facts.unlimitedRetaliation = true; break;
		case BonusType::TWO_HEX_ATTACK_BREATH: facts.twoCellAttack = true; break;
		case BonusType::ATTACKS_ALL_ADJACENT: facts.allAdjacentAttack = true; break;
		case BonusType::ADDITIONAL_ATTACK:
			if(bonus->val != 1)
				return std::nullopt;
			if(bonus->valType == BonusValueType::ADDITIVE_VALUE)
			{
				if(bonus->source != BonusSource::CREATURE_ABILITY
					|| bonus->sid != BonusSourceID(unit.creatureId()) || bonus->duration != BonusDuration::PERMANENT
					|| bonus->parameters || !bonus->stacking.empty()
					)
					return std::nullopt;
				const auto liveAttacks = unit.getAllBonuses(Selector::type()(BonusType::ADDITIONAL_ATTACK));
				if(liveAttacks->size() != 1 || (*liveAttacks)[0].get() != bonus.get())
					return std::nullopt;
				if(facts.archer)
				{
					if(!unit.isShooter() || bonus->effectRange != BonusLimitEffect::ONLY_DISTANCE_FIGHT
						|| unit.getTotalAttacks(true) != 2 || unit.getTotalAttacks(false) != 1)
						return std::nullopt;
				}
				else if(unit.isShooter() || unit.getTotalAttacks(false) != 2
					|| (bonus->effectRange != BonusLimitEffect::NO_LIMIT && bonus->effectRange != BonusLimitEffect::ONLY_MELEE_FIGHT))
					return std::nullopt;
			}
			else if(bonus->valType != BonusValueType::BASE_NUMBER)
				return std::nullopt;
			if(bonus->effectRange == BonusLimitEffect::ONLY_DISTANCE_FIGHT && facts.archer)
				{ if(facts.doubleShooting) return std::nullopt; facts.doubleShooting = true; }
			else if(bonus->effectRange == BonusLimitEffect::ONLY_MELEE_FIGHT)
				{ if(facts.doubleMelee) return std::nullopt; facts.doubleMelee = true; }
			else if(bonus->effectRange == BonusLimitEffect::NO_LIMIT)
			{
				if(facts.doubleShooting || facts.doubleMelee) return std::nullopt;
				facts.doubleShooting = facts.archer;
				{ if(facts.doubleMelee) return std::nullopt; facts.doubleMelee = true; }
			}
			else return std::nullopt;
			break;
		default: return std::nullopt;
		}
	}
	return facts;
}

class NativeStrengthModel
{
	bool sourceScale = true;
public:
	explicit NativeStrengthModel(const CPlayerBattleCallback & battle, bool forceNative = false)
	{
		if(forceNative) { sourceScale = false; return; }
		// A summon may introduce a mod-defined creature absent from the census.
		if(const auto * hero = battle.battleGetMyHero())
			for(const auto id : {SpellID::SUMMON_AIR_ELEMENTAL, SpellID::SUMMON_EARTH_ELEMENTAL,
				SpellID::SUMMON_FIRE_ELEMENTAL, SpellID::SUMMON_WATER_ELEMENTAL})
				if(hero->canCastThisSpell(SpellID(id).toSpell())) sourceScale = false;
		// One unsupported roster member switches every comparison to native units.
		for(const auto * unit : battle.battleGetAllStacks(true))
			if(!nativeStrengthFacts(*unit))
			{
				sourceScale = false;
				break;
			}
	}
	bool usesSourceScale() const { return sourceScale; }
	bool supports(const battle::Unit & unit) const { return !sourceScale || nativeStrengthFacts(unit).has_value(); }
	double value(const battle::Unit & unit) const
	{
		if(!sourceScale)
			return static_cast<double>(unit.estimateCombatValue());
		const auto facts = nativeStrengthFacts(unit);
		if(!facts)
			throw std::logic_error("Unsupported projected unit in source strength census");
		return stackStrength(*facts, unit.getAttack(facts->archer), unit.getDefense(false), unit.getCount());
	}
};
}
