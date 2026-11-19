// SPDX-License-Identifier: CC0-1.0
#pragma once
#include "NativeLifecycle.h"
#include "ShooterPrefix.h"
#include "../HoMM3CombatValue.h"
#include "../HoMM3GroundPath.h"
#include "../HoMM3CurrentDefendWitness.h"
#include "../../../../lib/CCreatureHandler.h"
#include "../../../../lib/battle/BattleAttackInfo.h"
#include "../../../../lib/battle/CUnitState.h"
#include "../../../../lib/battle/ReachabilityInfo.h"

namespace HoMM3Forecast
{
/// Native midpoint damage is a host projection, not source adjustDamage parity.
class NativeShooterModel
{
	const CPlayerBattleCallback & callback;
	std::map<UnitId, const battle::Unit *> units;
	bool nonEasyDifficulty;
	static std::optional<double> defenseCombatValue(const battle::Unit * unit, bool ranged)
	{
		bool armorSeen = false;
		bool shieldSeen = false;
		bool airShieldSeen = false;
		double factor = 1.0;
		const auto bonuses = unit->getAllBonuses(Selector::type()(BonusType::GENERAL_DAMAGE_REDUCTION));
		for(const auto & bonus : *bonuses)
		{
			if((bonus->duration != BonusDuration::PERMANENT && bonus->duration != BonusDuration::ONE_BATTLE
				&& (bonus->duration != BonusDuration::N_TURNS || bonus->turnsRemain <= 0))
				|| (bonus->valType != BonusValueType::ADDITIVE_VALUE
					&& !(bonus->valType == BonusValueType::BASE_NUMBER
						&& bonus->subtype == BonusCustomSubtype::damageTypeAll && bonus->source != BonusSource::SPELL_EFFECT))
				|| bonus->val < 0 || bonus->val > 100
				|| bonus->effectRange != BonusLimitEffect::NO_LIMIT || bonus->limiter || bonus->propagator
				|| bonus->updater || bonus->propagationUpdater || bonus->parameters || !bonus->stacking.empty())
				return std::nullopt;
			if(bonus->subtype == BonusCustomSubtype::damageTypeAll && bonus->source != BonusSource::SPELL_EFFECT)
			{
				if(armorSeen) return std::nullopt;
				armorSeen = true;
			}
			else if(bonus->subtype == BonusCustomSubtype::damageTypeMelee && bonus->source == BonusSource::SPELL_EFFECT
				&& bonus->sid == BonusSourceID(SpellID(SpellID::SHIELD)))
			{
				if(shieldSeen) return std::nullopt;
				shieldSeen = true;
				if(ranged) continue;
			}
			else if(bonus->subtype == BonusCustomSubtype::damageTypeRanged && bonus->source == BonusSource::SPELL_EFFECT
				&& bonus->sid == BonusSourceID(SpellID(SpellID::AIR_SHIELD)))
			{
				if(airShieldSeen) return std::nullopt;
				airShieldSeen = true;
				if(!ranged) continue;
			}
			else return std::nullopt;
			factor *= 1.0 - bonus->val / 100.0;
		}
		AIIntegration::Ports::HoMM3::UnitCombatValueFacts facts;
		facts.baseFightValue = unit->unitType()->getFightValue();
		facts.defenderDamageFactor = factor;
		facts.ranged = ranged;
		return AIIntegration::Ports::HoMM3::unitCombatValue(facts);
	}
public:
	explicit NativeShooterModel(const CPlayerBattleCallback & value, bool nonEasy = false,
		const AIIntegration::Ports::HoMM3::CurrentDefendSnapshot * witness = nullptr)
		: callback(value), nonEasyDifficulty(nonEasy)
	{
		for(const auto * unit : callback.battleGetUnitsIf([](const battle::Unit *) { return true; }))
		{
			if(unit->getMaxHealth() == 0 || unit->getMaxHealth() > static_cast<uint32_t>(std::numeric_limits<std::int32_t>::max())
				|| unit->isInvincible() || unit->isSummoned() || unit->isClone()
				|| unit->getTotalAttacks(true) != 1)
				throw UnsupportedSnapshot("unsupported attack count or created/invincible unit");
			// Unknown mechanics must not silently enter the bounded damage domain.
			const auto bonuses = unit->getAllBonuses(Selector::all);
			for(const auto & bonus : *bonuses)
			{
				switch(bonus->type)
				{
					case BonusType::UNIT_DEFENDING:
						if(!witness || !witness->bonus(callback,*unit))
							throw UnsupportedSnapshot("unobserved current Defend stance");
						break;
					case BonusType::GENERAL_ATTACK_REDUCTION:
					case BonusType::NO_RETALIATION:
						if(bonus->source != BonusSource::SPELL_EFFECT
							|| (bonus->sid != BonusSourceID(SpellID(SpellID::BLIND))
								&& bonus->sid != BonusSourceID(SpellID(SpellID::PARALYZE))
								&& bonus->sid != BonusSourceID(SpellID(SpellID::STONE_GAZE)))
							|| !unit->hasBonusOfType(BonusType::NOT_ACTIVE))
							throw UnsupportedSnapshot("unsupported retaliation-only status bonus");
						break;
					// Creature nature and hero/adventure metadata do not execute attacks or spells.
					case BonusType::LIVING:
					case BonusType::MANA_REGENERATION:
					case BonusType::HERO_EXPERIENCE_GAIN_PERCENT:
					case BonusType::MANA_PER_KNOWLEDGE_PERCENTAGE:
					case BonusType::MAX_LEARNABLE_SPELL_LEVEL:
					case BonusType::HERO_SPELL_CASTS_PER_COMBAT_TURN:
					case BonusType::THIEVES_GUILD_ACCESS:
					case BonusType::WANDERING_CREATURES_JOIN_BONUS:
					case BonusType::PRIMARY_SKILL:
					case BonusType::STACK_HEALTH:
					case BonusType::STACKS_SPEED:
					case BonusType::CREATURE_DAMAGE:
					case BonusType::SHOOTER:
					case BonusType::SHOTS:
					case BonusType::SPELL:
					case BonusType::SPELLS_OF_LEVEL:
					case BonusType::SPELLS_OF_SCHOOL:
					case BonusType::MAGIC_SCHOOL_SKILL:
					case BonusType::SPELL_DURATION:
					case BonusType::SPELL_DAMAGE:
					case BonusType::SPECIFIC_SPELL_DAMAGE:
					case BonusType::MAGIC_RESISTANCE:
					case BonusType::SPELL_IMMUNITY:
					case BonusType::LEVEL_SPELL_IMMUNITY:
					case BonusType::CHARGE_IMMUNITY:
					case BonusType::NO_MELEE_PENALTY:
					case BonusType::MORALE:
					case BonusType::LUCK:
					case BonusType::NOT_ACTIVE:
					case BonusType::ATTACKS_NEAREST_CREATURE:
					case BonusType::GENERAL_DAMAGE_REDUCTION:
					case BonusType::GENERAL_DAMAGE_PREMY:
					case BonusType::PERCENTAGE_DAMAGE_BOOST:
					case BonusType::ALWAYS_MINIMUM_DAMAGE:
					case BonusType::ALWAYS_MAXIMUM_DAMAGE:
						break;
					default: throw UnsupportedSnapshot("bonus outside bounded shooter projection");
				}
			}
			units.emplace(unit->unitId(), unit);
		}
	}
	bool canShoot(UnitId actor, UnitId target) const
	{
		return callback.battleCanShoot(units.at(actor), units.at(target)->getPosition());
	}
	std::optional<std::int64_t> shotDamage(UnitId actor, UnitId target,
		std::int64_t actorHealth, std::int64_t targetHealth) const
	{
		auto attacker = units.at(actor)->acquireState();
		auto defender = units.at(target)->acquireState();
		std::int64_t lost = attacker->getAvailableHealth() - actorHealth;
		attacker->damage(lost);
		lost = defender->getAvailableHealth() - targetHealth;
		defender->damage(lost);
		const auto estimate = callback.battleEstimateDamage(BattleAttackInfo(attacker.get(), defender.get(), 0, true));
		if(estimate.damage.min < 0 || estimate.damage.max < estimate.damage.min
			|| estimate.damage.max > std::numeric_limits<std::int32_t>::max()) return std::nullopt;
		return estimate.damage.min + (estimate.damage.max - estimate.damage.min) / 2;
	}
	std::optional<std::array<std::int32_t, 2>> peerLosses(UnitId actor, UnitId target,
		std::int64_t actorHealth, std::int64_t targetHealth) const
	{
		if(callback.battleGetDefendedTown()
			|| actorHealth > std::numeric_limits<std::int32_t>::max() - units.at(actor)->getMaxHealth())
			return std::nullopt;
		// Zero modifier baseline avoids projecting the constructor's minima.
		for(const auto & [id, unit] : units)
		{
			(void)id;
			if(!unit->alive()) continue;
			if(unit->getAttack(true) != unit->unitType()->getAttack(true)
				|| unit->getAttack(false) != unit->unitType()->getAttack(false)
				|| unit->getDefense(true) != unit->unitType()->getDefense(true)
				|| unit->getDefense(false) != unit->unitType()->getDefense(false)) return std::nullopt;
			if(!defenseCombatValue(unit, true)) return std::nullopt;
			for(const auto type : {BonusType::GENERAL_DAMAGE_PREMY,
				BonusType::PERCENTAGE_DAMAGE_BOOST, BonusType::ALWAYS_MINIMUM_DAMAGE, BonusType::ALWAYS_MAXIMUM_DAMAGE})
				if(unit->hasBonusOfType(type)) return std::nullopt;
		}
		const auto * victim = units.at(target);
		// No shooter penalty or host/source canShoot discrepancy for non-shooter victims.
		if(victim->isShooter()) return std::nullopt;
		const auto damage = shotDamage(actor, target, actorHealth, targetHealth);
		if(!damage || *damage < 0 || targetHealth <= 0) return std::nullopt;
		const auto loss = std::min(*damage, targetHealth);
		const auto hp = victim->getMaxHealth();
		const auto wound = hp - victim->getFirstHPleft();
		if(loss % hp + wound > std::numeric_limits<std::int32_t>::max()) return std::nullopt;
		const auto adjusted = loss + (loss % hp + wound >= hp ? wound : 0);
		if(adjusted > std::numeric_limits<std::int32_t>::max()) return std::nullopt;
		const auto combatValue = defenseCombatValue(victim, true);
		if(!combatValue) return std::nullopt;
		const auto value = *combatValue;
		std::array<std::int32_t, 2> result;
		for(std::size_t mode = 0; mode < result.size(); ++mode)
		{
			const double raw = static_cast<double>(adjusted) * (mode ? 1000.0 : value) / hp;
			if(!std::isfinite(raw) || raw < 0 || raw > std::numeric_limits<std::int32_t>::max()) return std::nullopt;
			result[mode] = AIIntegration::Ports::HoMM3::lossCombatValue(value, victim->getCount(), hp,
				wound, false, loss, mode != 0);
		}
		return result;
	}

	std::optional<MeleeExchange> meleeExchange(const ForecastInput & input, UnitId actor, UnitId target,
		std::int64_t actorHealth, std::int64_t targetHealth) const
	{
		const auto * first = units.at(actor);
		const auto * second = units.at(target);
		if(!nonEasyDifficulty || callback.battleGetDefendedTown() || callback.battleTacticDist() != 0
			|| !callback.battleGetAllObstacles().empty() || callback.battleHasHero(second->unitSide())
			|| first->getMovementRange() == 0 || second->getMovementRange() == 0
			|| actorHealth <= 0 || targetHealth <= 0
			|| !callback.battleCanAttackUnit(first, second))
			return std::nullopt;
		std::size_t enemies = 0;
		for(const auto & unit : input.units)
		{
			if(unit.liveHealth == 0) continue;
			const auto * native = units.at(unit.order.id);
			const auto sourceLimit = std::numeric_limits<std::int32_t>::max();
			const auto combatValue = defenseCombatValue(native, false);
			if(!combatValue) return std::nullopt;
			// Two full-stack loss values per contribution, times randomFactor <= 100.
			const double scoringBound = static_cast<double>(native->getCount())
				* std::max(1000.0, *combatValue);
			if(unit.liveHealth > sourceLimit - native->getMaxHealth()
				|| scoringBound > sourceLimit / (200.0 * (input.units.size() + 1))) return std::nullopt;
			if(unit.order.owningSide != sourceSide(first->unitSide())) ++enemies;
			if(unit.order.role != OrderRole::ORDINARY || unit.order.immobilized || unit.berserk
				|| unit.controllingSide != unit.order.owningSide || native->isShooter()
				|| native->getHexes().size() != 1 || native->getTotalAttacks(false) != 1
				|| unit.order.sourceSpell62Turns || unit.order.sourceSpell70Turns || unit.order.sourceSpell74Turns
				|| native->getAttack(false) != native->unitType()->getAttack(false)
				|| native->getDefense(false) != native->unitType()->getDefense(false)) return std::nullopt;
			for(const auto type : {BonusType::GENERAL_ATTACK_REDUCTION, BonusType::NO_RETALIATION,
				BonusType::NOT_ACTIVE, BonusType::GENERAL_DAMAGE_PREMY,
				BonusType::PERCENTAGE_DAMAGE_BOOST, BonusType::ALWAYS_MINIMUM_DAMAGE, BonusType::ALWAYS_MAXIMUM_DAMAGE})
				if(native->hasBonusOfType(type)) return std::nullopt;
		}
		if(enemies != 1) return std::nullopt;
		BattleHex attackPosition = first->getPosition();
		if(BattleHex::mutualPosition(attackPosition, second->getPosition()) == BattleHex::NONE)
		{
			if(units.size() != 2 || !attackPosition.isAvailable() || !second->getPosition().isAvailable()) return std::nullopt;
			const auto separation = BattleHex::getDistance(attackPosition, second->getPosition());
			// markEnemyAttacks must mark the actor dangerous without adding threat.
			if(separation > second->getMovementRange() || separation - 1 > first->getMovementRange()) return std::nullopt;
			const auto reach = callback.getReachability(first);
			std::uint32_t cheapest = ReachabilityInfo::INFINITE_DIST;
			// checkAdjacentHexes visits source directions 0..5 and retains the first cost tie.
			for(const int index : AIIntegration::Ports::HoMM3::GroundPathDetail::adjacent(second->getPosition().toInt()))
			{
				if(index < 0) continue;
				const BattleHex hex(index);
				const auto distance = reach.distances[hex.toInt()];
				if(distance < cheapest)
				{
					cheapest = distance;
					attackPosition = hex;
				}
			}
			// The clear 1v1 domain has zero landing threat and no blocking-shooter value.
			if(cheapest != separation - 1) return std::nullopt;
		}
		if(callback.meleeAttackHexes(first, second, attackPosition).empty()) return std::nullopt;
		auto attacker = first->acquireState();
		attacker->setPosition(attackPosition);
		auto defender = second->acquireState();
		std::int64_t lost = attacker->getAvailableHealth() - actorHealth;
		attacker->damage(lost);
		lost = defender->getAvailableHealth() - targetHealth;
		defender->damage(lost);
		const auto strike = [&](auto & from, auto & to) -> bool
		{
			const auto estimate = callback.battleEstimateDamage(BattleAttackInfo(from.get(), to.get(), 0, false));
			if(estimate.damage.min < 0 || estimate.damage.max < estimate.damage.min
				|| estimate.damage.max > std::numeric_limits<std::int32_t>::max()) return false;
			std::int64_t damage = estimate.damage.min + (estimate.damage.max - estimate.damage.min) / 2;
			to->damage(damage);
			return true;
		};
		if(!strike(attacker, defender)) return std::nullopt;
		// simulateMeleeAttack reads retaliation availability without spending it.
		if(defender->alive() && second->ableToRetaliate() && !strike(defender, attacker)) return std::nullopt;
		MeleeExchange exchange{targetHealth - defender->getAvailableHealth(), actorHealth - attacker->getAvailableHealth()};
		const auto lossValue = [](const battle::Unit * unit, std::int64_t damage, bool killsOnly) -> std::optional<std::int32_t>
		{
			const auto hp = unit->getMaxHealth();
			const auto wound = hp - unit->getFirstHPleft();
			if(damage % hp + wound > std::numeric_limits<std::int32_t>::max()) return std::nullopt;
			const auto adjusted = damage + (damage % hp + wound >= hp ? wound : 0);
			if(adjusted > std::numeric_limits<std::int32_t>::max()) return std::nullopt;
			const auto combatValue = defenseCombatValue(unit, false);
			if(!combatValue) return std::nullopt;
			const double value = killsOnly ? 1000.0 : *combatValue;
			const double raw = adjusted * value / hp;
			if(!std::isfinite(raw) || raw < 0 || raw > std::numeric_limits<std::int32_t>::max()) return std::nullopt;
			return AIIntegration::Ports::HoMM3::lossCombatValue(value, unit->getCount(), hp, wound, false, damage, killsOnly);
		};
		const auto ownLoss = lossValue(first, exchange.actorDamage, false);
		if(!ownLoss) return std::nullopt;
		for(bool killsOnly : {false, true})
		{
			const auto enemyLoss = lossValue(second, exchange.targetDamage, killsOnly);
			if(!enemyLoss || *enemyLoss < *ownLoss) return std::nullopt;
		}
		return exchange;
	}

	SurvivorFacts survivorFacts(UnitId id) const
	{
		const auto * unit = units.at(id);
		return {unit->getMaxHealth(), unit->alive() ? unit->getMaxHealth() - unit->getFirstHPleft() : 0,
			unit->hasBonusOfType(BonusType::SIEGE_WEAPON)};
	}
};

inline PrefixProjection nativeShooterPrefix(const CPlayerBattleCallback & callback, const NativeLifecycle & lifecycle, bool nonEasyDifficulty = false)
{
	try
	{
		const auto input = lifecycle.snapshot();
		NativeShooterModel model(callback, nonEasyDifficulty);
		return projectShooterPrefix(input, sourceSide(callback.battleGetMySide()), model);
	}
	catch(const UnsupportedSnapshot & error) { return {std::nullopt, error.what()}; }
}
}
