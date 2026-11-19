/*
 * HoMM3SpellPolicy.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later.
 * Source: sushi-shi/homm3-decomp, e12906accf96c2662b1abadbc49ddc111991bf21,
 * src/ai_tactical.cpp (CC0-1.0). Native spell effects and combat values are
 * projections; this is not the complete original hero policy.
 */
#pragma once
#include "HoMM3Forecast/ShooterPrefix.h"

#include "HoMM3SpellValue.h"
#include "HoMM3PrayerRemoval.h"
#include "HoMM3MassPrayerRemoval.h"
#include "HoMM3PrayerTargetCopy.h"
#include "HoMM3FreshPrayerContext.h"
#include "HoMM3CombatValue.h"
#include "HoMM3NativeGroundCensus.h"
#include <map>
#include <array>
#include "HoMM3MirthBonus.h"
#include "../../BattleAI/StackWithBonuses.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/CStack.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../lib/spells/ISpellMechanics.h"

namespace AIIntegration
{
class HoMM3SpellPolicy
{
	struct Attack
	{
		const CStack * target = nullptr;
		BattleHex from;
		BattleHex aim;
		bool shooting = false;
		double damage = 0;
	};

	struct Snapshot
	{
		const CStack * unit;
		int64_t health;
		int count;
		BattleHex position;
		PlayerColor owner;
		double value;
		Attack attack;
	};

	static bool inactive(const battle::Unit & unit)
	{
		return unit.hasBonusOfType(BonusType::NOT_ACTIVE);
	}

	static bool completedTurn(const battle::Unit & unit)
	{
		return unit.moved() || unit.defended();
	}

	static bool hasSpell(const battle::Unit & unit, SpellID spell)
	{
		return unit.hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
	}

	static bool attackBuff(SpellID id)
	{
		return id == SpellID::BLESS || id == SpellID::BLOODLUST || id == SpellID::PRECISION;
	}

	static auto ordinaryRoll(const battle::Unit & unit, BonusType type, SpellID spell)
		-> std::optional<int>
	{
		const auto bonuses = unit.getAllBonuses(Selector::type()(type));
		if(bonuses->size() != 1)
			return std::nullopt;
		const auto & bonus = bonuses->front();
		if(bonus->source != BonusSource::SPELL_EFFECT || bonus->sid != BonusSourceID(spell)
			|| bonus->duration != BonusDuration::N_TURNS || bonus->turnsRemain <= 0
			|| bonus->valType != BonusValueType::INDEPENDENT_MAX
			|| bonus->effectRange != BonusLimitEffect::NO_LIMIT || bonus->limiter || bonus->propagator
			|| bonus->updater || bonus->propagationUpdater || bonus->parameters || !bonus->stacking.empty()
			|| (bonus->val != 0 && bonus->val != 1))
			return std::nullopt;
		return bonus->val;
	}

	static std::optional<std::pair<double, double>> curseAverages(
		const battle::Unit & before, const battle::Unit & after, bool ranged, int mastery)
	{
		if(before.isClone() || before.isSummoned() || before.isTurret()
			|| before.hasBonusOfType(BonusType::ALWAYS_MINIMUM_DAMAGE)
			|| after.hasBonusOfType(BonusType::ALWAYS_MAXIMUM_DAMAGE))
			return std::nullopt;
		const int low = before.unitType()->getMinDamage(ranged);
		const int high = before.unitType()->getMaxDamage(ranged);
		if(!Ports::HoMM3::directCurseDamageRangeSupported(low, high) || before.getMinDamage(ranged) != low
			|| before.getMaxDamage(ranged) != high || after.getMinDamage(ranged) != low
			|| after.getMaxDamage(ranged) != high)
			return std::nullopt;

		const auto curse = ordinaryRoll(after, BonusType::ALWAYS_MINIMUM_DAMAGE, SpellID(SpellID::CURSE));
		if(!curse || *curse != (mastery >= MasteryLevel::ADVANCED ? 1 : 0))
			return std::nullopt;
		double average = (static_cast<double>(low) + high) / 2;
		if(before.hasBonusOfType(BonusType::ALWAYS_MAXIMUM_DAMAGE))
		{
			const auto bless = ordinaryRoll(before, BonusType::ALWAYS_MAXIMUM_DAMAGE, SpellID(SpellID::BLESS));
			if(!bless || !hasSpell(before, SpellID(SpellID::BLESS)) || hasSpell(after, SpellID(SpellID::BLESS)))
				return std::nullopt;
			average = static_cast<double>(high) + *bless;
		}
		// Source army::getAverageDamage is per creature, before target factors
		// and stack rounding. Native attack estimates only validate the effect.
		return std::make_pair(average, static_cast<double>(std::max(1, low - *curse)));
	}

	struct CurseContext
	{
		std::map<uint32_t, int32_t> values;
		int rounds = 1;
	};

	struct SourceCombatContext
	{
		std::map<uint32_t, int32_t> values;
		int64_t friendly = 0, enemy = 0, friendlyAwake = 0, enemyAwake = 0;
	};

	static std::optional<SourceCombatContext> sourceCombatContext(const CPlayerBattleCallback & battle,
		const std::vector<Snapshot> & before, PlayerColor player)
	{
		int64_t lowestAttack = INT32_MAX, lowestDefense = INT32_MAX;
		for(const auto & old : before)
		{
			const auto & unit = *old.unit;
			if(!unit.alive()) continue;
			if(unit.isTurret() || unit.isClone() || unit.isSummoned()
				|| unit.hasBonusOfType(BonusType::SIEGE_WEAPON)
				|| unit.controllingSide() != unit.unitSide()
				|| unit.hasBonusOfType(BonusType::GENERAL_DAMAGE_REDUCTION)
				|| unit.getTotalAttacks(true) != 1 || unit.getTotalAttacks(false) != 1)
				return std::nullopt;
			const bool ranged = battle.battleCanShoot(&unit);
			const int64_t attack = static_cast<int64_t>(unit.getAttack(ranged)) - unit.unitType()->getAttack(ranged);
			const int64_t defense = static_cast<int64_t>(unit.getDefense(false)) - unit.unitType()->getDefense(false);
			if(!Ports::HoMM3::directCurseSignedValue(attack) || !Ports::HoMM3::directCurseSignedValue(defense)
				|| !Ports::HoMM3::directCurseHealthSupported(unit.getCount(), unit.getMaxHealth(), unit.getAvailableHealth())
				|| unit.unitType()->getFightValue() < 0)
				return std::nullopt;
			lowestAttack = std::min(lowestAttack, attack);
			lowestDefense = std::min(lowestDefense, defense);
		}
		SourceCombatContext context;
		int64_t friendly = 0, enemy = 0, friendlyAwake = 0, enemyAwake = 0;
		for(const auto & old : before)
		{
			const auto & unit = *old.unit;
			if(!unit.alive()) continue;
			const bool ranged = battle.battleCanShoot(&unit);
			const int low = unit.unitType()->getMinDamage(ranged), high = unit.unitType()->getMaxDamage(ranged);
			if(!Ports::HoMM3::directCurseDamageRangeSupported(low, high) || unit.getMinDamage(ranged) != low || unit.getMaxDamage(ranged) != high)
				return std::nullopt;
			Ports::HoMM3::UnitCombatValueFacts facts;
			const auto attackDifference = Ports::HoMM3::directCurseModifierDifference(
				static_cast<int64_t>(unit.getAttack(ranged)) - unit.unitType()->getAttack(ranged), lowestAttack);
			const auto defenseDifference = Ports::HoMM3::directCurseModifierDifference(
				static_cast<int64_t>(unit.getDefense(false)) - unit.unitType()->getDefense(false), lowestDefense);
			if(!attackDifference || !defenseDifference) return std::nullopt;
			facts.attackDifference = *attackDifference;
			facts.defenseDifference = *defenseDifference;
			facts.baseFightValue = unit.unitType()->getFightValue();
			facts.ranged = facts.canShoot = ranged;
			facts.shooter = unit.isShooter();
			facts.baseAverageDamage = facts.averageDamage = (static_cast<double>(low) + high) / 2;
			const bool blessed = unit.hasBonusOfType(BonusType::ALWAYS_MAXIMUM_DAMAGE);
			const bool cursed = unit.hasBonusOfType(BonusType::ALWAYS_MINIMUM_DAMAGE);
			if(blessed && cursed) return std::nullopt;
			if(blessed || cursed)
			{
				const auto amount = ordinaryRoll(unit, blessed ? BonusType::ALWAYS_MAXIMUM_DAMAGE : BonusType::ALWAYS_MINIMUM_DAMAGE,
					SpellID(blessed ? SpellID::BLESS : SpellID::CURSE));
				if(!amount) return std::nullopt;
				facts.alteredDamage = true;
				facts.averageDamage = blessed ? static_cast<double>(high) + *amount : std::max(1, low - *amount);
			}
			const double value = unit.getAvailableHealth() * Ports::HoMM3::unitCombatValue(facts) / unit.getMaxHealth();
			if(!std::isfinite(value) || value < 0 || value > INT32_MAX) return std::nullopt;
			const auto total = static_cast<int32_t>(value);
			context.values.emplace(unit.unitId(), total);
			auto & sideTotal = old.owner == player ? friendly : enemy;
			if(total > INT32_MAX - sideTotal) return std::nullopt;
			sideTotal += total;
			if(!inactive(unit)) (old.owner == player ? friendlyAwake : enemyAwake) += total;
		}
		context.friendly = friendly;
		context.enemy = enemy;
		context.friendlyAwake = friendlyAwake;
		context.enemyAwake = enemyAwake;
		return context;
	}

	static std::optional<CurseContext> curseContext(const CPlayerBattleCallback & battle,
		const std::vector<Snapshot> & before, PlayerColor player)
	{
		auto source = sourceCombatContext(battle, before, player);
		if(!source) return std::nullopt;
		// This sufficient !killsOnly condition does not require IsHuman or difficulty.
		// The underdog/easy-AI exception needs source context not mapped here.
		if(!Ports::HoMM3::directCurseContextSupported(source->friendly, source->enemy))
			return std::nullopt;
		const auto planningRounds = Ports::HoMM3::directCursePlanningRounds(source->friendlyAwake, source->enemyAwake);
		if(!planningRounds) return std::nullopt;
		return CurseContext{std::move(source->values), *planningRounds};
	}

	static std::optional<int32_t> rayTargetValue(const CPlayerBattleCallback & battle,
		const std::vector<Snapshot> & before, PlayerColor player, const CStack & active)
	{
		// One eligible shooter and one enemy prove both source commitment and isLastAction.
		if(before.size() != 2 || !active.alive() || !active.isShooter() || !battle.battleCanShoot(&active))
			return std::nullopt;
		const CStack * enemy = nullptr;
		int friendlyCount = 0;
		for(const auto & old : before)
		{
			if(!old.unit->alive() || inactive(*old.unit) || old.unit->doubleWide()
				|| hasSpell(*old.unit, SpellID::BERSERK) || hasSpell(*old.unit, SpellID::HYPNOTIZE)
				|| hasSpell(*old.unit, SpellID::BLIND) || hasSpell(*old.unit, SpellID::STONE_GAZE)
				|| hasSpell(*old.unit, SpellID::PARALYZE))
				return std::nullopt;
			if(old.owner == player)
			{
				if(old.unit != &active) return std::nullopt;
				++friendlyCount;
			}
			else enemy = old.unit;
		}
		if(friendlyCount != 1 || !enemy || !battle.battleCanShoot(&active, enemy->getPosition()))
			return std::nullopt;
		const auto source = sourceCombatContext(battle, before, player);
		if(!source || source->friendly <= 0 || source->enemy <= 0
			|| source->friendly > INT32_MAX / 2 || source->friendly * 2 < source->enemy)
			return std::nullopt;
		return source->values.at(enemy->unitId());
	}

	static std::map<uint32_t, int32_t> rayTargetValues(const CPlayerBattleCallback & battle,
		const std::vector<Snapshot> & before, PlayerColor player, const CStack & active,
		const Environment * environment, bool publicStaticMapCertified, const Ports::HoMM3::CurrentDefendSnapshot * witness)
	{
		std::map<uint32_t, int32_t> result;
		// Preserve the independently proved singleton witness even outside the
		// ordinary exchange adapter's narrower damage-fact domain.
		if(const auto singleton = rayTargetValue(battle, before, player, active))
			for(const auto & old : before)
				if(old.owner != player) result.emplace(old.unit->unitId(), *singleton);
		if(!Ports::HoMM3::nativeSourceLastAction(battle, active)) return result;
		const auto source = sourceCombatContext(battle, before, player);
		if(!source || source->friendly <= 0 || source->enemy <= 0
			|| source->friendly > INT32_MAX / 2 || source->friendly * 2 < source->enemy)
			return result;
		const auto census = Ports::HoMM3::captureNativeOrdinaryCensus(battle, active.unitSide(), environment, publicStaticMapCertified, false, witness);
		if(!census) return result;
		for(const auto & old : before)
		{
			if(old.owner == player || !old.unit->alive()) continue;
			const auto commitment = census->committed(old.unit->unitId());
			if(commitment && *commitment)
				result.emplace(old.unit->unitId(), source->values.at(old.unit->unitId()));
		}
		return result;
	}

	static bool ordinaryRayEffect(const battle::Unit & before, const battle::Unit & after, int bonus)
	{
		const auto effects = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::DISRUPTING_RAY)));
		for(const auto * unit : {&before, &after})
		{
			const auto bonuses = unit->getAllBonuses(effects);
			for(const auto & effect : *bonuses)
				if(effect->type != BonusType::PRIMARY_SKILL
					|| effect->subtype != BonusSubtypeID(PrimarySkill::DEFENSE)
					|| effect->duration != BonusDuration::PERMANENT
					|| effect->val > 0 || effect->valType != BonusValueType::ADDITIVE_VALUE
					|| effect->effectRange != BonusLimitEffect::NO_LIMIT || effect->limiter || effect->propagator
					|| effect->updater || effect->propagationUpdater || effect->parameters || !effect->stacking.empty())
					return false;
		}
		return int64_t{after.valOfBonuses(effects)} == int64_t{before.valOfBonuses(effects)} - bonus
			&& after.getAvailableHealth() == before.getAvailableHealth()
			&& after.getPosition() == before.getPosition();
	}

	static bool defenseBuff(SpellID id)
	{
		return id == SpellID::STONE_SKIN || id == SpellID::SHIELD || id == SpellID::AIR_SHIELD;
	}

	struct IncomingReach
	{
		std::optional<BattleHexArray> available;
		std::optional<ReachabilityInfo::TDistances> distances;
	};

	// One best legal incoming attack per enemy; projected Units must not be
	// replaced by the live CStack view exposed by HypotheticBattle.
	static double incomingDamage(const CBattleInfoCallback & battle,
		const battle::Unit & enemy, const battle::Unit & target, bool & shooting,
		IncomingReach & reach)
	{
		shooting = false;
		if(!enemy.alive() || inactive(enemy) || enemy.isTurret() || !enemy.isValidTarget()
			|| !target.alive() || battle.battleGetOwner(&enemy) == battle.battleGetOwner(&target))
			return 0;
		shooting = battle.battleCanShoot(&enemy, target.getPosition());
		if(!shooting && !battle.battleCanAttackUnit(&enemy, &target))
			return 0;
		if(!reach.available)
			reach.available = battle.battleGetAvailableHexes(&enemy, false);
		const auto & available = *reach.available;
		double best = 0;
		for(const auto from : available)
		{
			if(shooting && from != enemy.getPosition())
				continue;
			for(const auto aim : target.getHexes())
			{
				if(!shooting && (!battle.meleeAttackHexes(&enemy, &target, from).contains(aim)
					|| !battle.battleCanAttackFromHex(available, &enemy, aim, from)))
					continue;
				if(!shooting && !reach.distances)
					reach.distances = battle.battleGetDistances(&enemy, enemy.getPosition());
				const auto distance = shooting ? 0u : reach.distances->at(from.toInt());
				if(distance >= ReachabilityInfo::INFINITE_DIST)
					continue;
				BattleAttackInfo info(&enemy, &target, distance, shooting);
				info.attackerPos = from;
				if(!shooting)
					info.defenderPos = aim;
				const auto estimate = battle.battleEstimateDamage(info);
				best = std::max(best, (static_cast<double>(estimate.damage.min)
					+ static_cast<double>(estimate.damage.max)) / 2);
			}
		}
		return best;
	}

	struct DefenseThreat
	{
		const CStack * enemy;
		const CStack * target;
		double damage;
		bool shooting;
	};

	static std::vector<DefenseThreat> defenseCensus(const CBattleInfoCallback & real,
		const std::vector<Snapshot> & before)
	{
		std::vector<DefenseThreat> result;
		for(const auto & enemy : before)
		{
			IncomingReach reach;
			for(const auto & target : before)
			{
				if(enemy.owner == target.owner)
					continue;
				bool shooting = false;
				const double damage = incomingDamage(real, *enemy.unit, *target.unit, shooting, reach);
				if(damage > 0)
					result.push_back({enemy.unit, target.unit, damage, shooting});
			}
		}
		return result;
	}

	static double projectedDefenseValue(const CBattleInfoCallback & projected, const Snapshot & old,
		const battle::Unit & after, const std::vector<DefenseThreat> & threats,
		SpellID spell, int rounds, int duration, std::map<uint32_t, IncomingReach> & projectedReach,
		const HoMM3Forecast::PrefixProjection & forecast = HoMM3Forecast::PrefixProjection())
	{
		double totalBefore = 0, totalAfter = 0, worst = 0, worstAfter = 0;
		bool worstRanged = false;
		for(const auto & threat : threats)
		{
			if(threat.target != old.unit)
				continue;
			const auto * projectedEnemy = projected.battleGetUnitByID(threat.enemy->unitId());
			if(!projectedEnemy)
				continue;
			bool rangedAfter = false;
			auto & reach = projectedReach[projectedEnemy->unitId()];
			const double damage = threat.damage;
			const double reduced = incomingDamage(projected, *projectedEnemy, after, rangedAfter, reach);
			totalBefore += damage;
			totalAfter += reduced;
			const bool relevant = spell == SpellID::STONE_SKIN
				|| (spell == SpellID::SHIELD && !threat.shooting)
				|| (spell == SpellID::AIR_SHIELD && threat.shooting);
			if(relevant && (damage > worst || (damage == worst && worstRanged && !threat.shooting)))
			{
				worst = damage;
				worstAfter = reduced;
				worstRanged = threat.shooting;
			}
		}
		const double denominator = spell == SpellID::STONE_SKIN ? worstAfter : totalAfter;
		if(denominator <= 0 || worst <= 0)
			return 0;
		const double increase = (spell == SpellID::STONE_SKIN ? worst : totalBefore) / denominator;
		// Native immediate threats replace the source multi-turn target census.
		const auto side = old.unit->unitSide() == BattleSide::ATTACKER
			? HoMM3Forecast::Side::ATTACKER : HoMM3Forecast::Side::DEFENDER;
		const auto expected = (old.unit->unitSide() == BattleSide::ATTACKER || old.unit->unitSide() == BattleSide::DEFENDER)
			? HoMM3Forecast::winningExpectedDamage(forecast, side, old.unit->unitId(),
				old.health, old.unit->getMaxHealth(), old.unit->getMaxHealth() - old.unit->getFirstHPleft())
			: std::nullopt;
		return Ports::HoMM3::defenseSpellValue(old.value, old.health, old.unit->getMaxHealth(),
			old.unit->getMaxHealth() - old.unit->getFirstHPleft(), worst, increase,
			totalBefore, rounds, Ports::HoMM3::spellDurationValue(duration, rounds, false), expected);
	}

	static int remainingDuration(const battle::Unit & unit, SpellID spell)
	{
		int duration = 0;
		const auto bonuses = unit.getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
		for(const auto & bonus : *bonuses)
			duration = std::max<int>(duration, bonus->turnsRemain);
		return duration;
	}

	static bool removalSupported(SpellID id)
	{
		return attackBuff(id) || id == SpellID::CURSE || id == SpellID::BLIND || id == SpellID::PARALYZE
			|| id == SpellID::MIRTH || id == SpellID::PRAYER;
	}

	static std::optional<double> removedMirthValue(const Snapshot & old, const battle::Unit & after,
		double duration)
	{
		const auto bonuses = old.unit->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(SpellID(SpellID::MIRTH))));
		if(bonuses->size() != 1 || inactive(*old.unit) || inactive(after)
			|| old.unit->isClone() || after.getAvailableHealth() != old.health
			|| after.getCount() != old.count || after.getPosition() != old.position)
			return std::nullopt;
		const auto other = Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(SpellID(SpellID::MIRTH))).Not();
		const auto beforeOther = old.unit->getAllBonuses(other), afterOther = after.getAllBonuses(other);
		if(beforeOther->size() != afterOther->size()
			|| !std::is_permutation(beforeOther->begin(), beforeOther->end(), afterOther->begin(),
				[](const auto & a, const auto & b) { return Ports::HoMM3::sameMirthBaselineBonus(*a, *b); }))
			return std::nullopt;
		const auto & bonus = bonuses->front();
		if(bonus->type != BonusType::MORALE || bonus->valType != BonusValueType::ADDITIVE_VALUE
			|| (bonus->val != 1 && bonus->val != 2) || bonus->duration != BonusDuration::N_TURNS
			|| bonus->turnsRemain <= 0 || bonus->effectRange != BonusLimitEffect::NO_LIMIT
			|| bonus->limiter || bonus->propagator || bonus->updater || bonus->propagationUpdater
			|| bonus->parameters || !bonus->stacking.empty())
			return std::nullopt;
		if(old.unit->unaffectedByMorale() || after.unaffectedByMorale()
			|| old.unit->moraleVal() == after.moraleVal())
			return 0;
		const int morale = after.moraleVal();
		if(morale < -3 || morale > 3 || old.unit->moraleVal() != std::min(3, morale + bonus->val))
			return std::nullopt;
		const double total = static_cast<double>(after.estimateCombatValue());
		if(total > INT32_MAX)
			return std::nullopt;
		return Ports::HoMM3::mirthSpellValue(total, morale, bonus->val, duration);
	}

 static std::optional<double> addedMirthValue(const Snapshot & old, const battle::Unit & after,
  int mastery, double duration)
 {
  if(hasSpell(*old.unit, SpellID(SpellID::MIRTH)) || hasSpell(*old.unit, SpellID(SpellID::SORROW))
   || inactive(*old.unit) || inactive(after) || old.unit->isClone()
   || after.getAvailableHealth()!=old.health || after.getCount()!=old.count || after.getPosition()!=old.position)
   return std::nullopt;
  const auto bonuses=after.getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::MIRTH))));
  if(bonuses->size()!=1)return std::nullopt;
  const auto other=Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::MIRTH))).Not();
  const auto beforeOther=old.unit->getAllBonuses(other),afterOther=after.getAllBonuses(other);
  if(beforeOther->size()!=afterOther->size()
   || !std::is_permutation(beforeOther->begin(),beforeOther->end(),afterOther->begin(),
    [](const auto & a,const auto & b)
    {
     return Ports::HoMM3::sameMirthBaselineBonus(*a,*b);
    }))
   return std::nullopt;
  const auto & bonus=bonuses->front();
  const int amount=mastery>=MasteryLevel::ADVANCED ? 2 : 1;
  if(bonus->type!=BonusType::MORALE || bonus->valType!=BonusValueType::ADDITIVE_VALUE
   || bonus->val!=amount || bonus->duration!=BonusDuration::N_TURNS || bonus->turnsRemain<=0
   || bonus->effectRange!=BonusLimitEffect::NO_LIMIT || bonus->limiter || bonus->propagator
   || bonus->updater || bonus->propagationUpdater || bonus->parameters || !bonus->stacking.empty())
   return std::nullopt;
  if(old.unit->unaffectedByMorale() || after.unaffectedByMorale())return 0;
  const int morale=old.unit->moraleVal();
  if(morale < -3 || morale > 3 || after.moraleVal()!=std::min(3,morale+amount))return std::nullopt;
  if(old.value>INT32_MAX)return std::nullopt;
  return Ports::HoMM3::mirthSpellValue(old.value,morale,amount,duration);
 }
 static std::optional<double> addedSorrowValue(const Snapshot & old, const battle::Unit & after,
  int mastery, double duration, int total, int successPercent)
 {
  if(hasSpell(*old.unit, SpellID(SpellID::MIRTH)) || hasSpell(*old.unit, SpellID(SpellID::SORROW))
   || inactive(*old.unit) || inactive(after) || old.unit->isClone()
   || after.getAvailableHealth()!=old.health || after.getCount()!=old.count || after.getPosition()!=old.position)
   return std::nullopt;
  const auto bonuses=after.getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::SORROW))));
  if(bonuses->size()!=1)return std::nullopt;
  const auto other=Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::SORROW))).Not();
  const auto beforeOther=old.unit->getAllBonuses(other),afterOther=after.getAllBonuses(other);
  if(beforeOther->size()!=afterOther->size()
   || !std::is_permutation(beforeOther->begin(),beforeOther->end(),afterOther->begin(),
    [](const auto & a,const auto & b)
    {
     return Ports::HoMM3::sameMirthBaselineBonus(*a,*b);
    }))
   return std::nullopt;
  const auto & bonus=bonuses->front();
  const int amount=mastery>=MasteryLevel::ADVANCED ? 2 : 1;
  if(bonus->type!=BonusType::MORALE || bonus->valType!=BonusValueType::ADDITIVE_VALUE
   || bonus->val!=-amount || bonus->duration!=BonusDuration::N_TURNS || bonus->turnsRemain<=0
   || bonus->effectRange!=BonusLimitEffect::NO_LIMIT || bonus->limiter || bonus->propagator
   || bonus->updater || bonus->propagationUpdater || bonus->parameters || !bonus->stacking.empty())
   return std::nullopt;
  if(old.unit->unaffectedByMorale() || after.unaffectedByMorale())return 0;
  const int morale=old.unit->moraleVal();
  if(morale < -3 || morale > 3 || after.moraleVal()!=std::max(-3,morale-amount))return std::nullopt;
  if(total<0)return std::nullopt;
  return Ports::HoMM3::sorrowSpellValue(total,morale,amount,duration,successPercent);
 }

 static std::optional<double> addedMisfortuneValue(const Snapshot & old, const battle::Unit & after,
  int mastery, double duration, int total, int successPercent)
 {
  if(hasSpell(*old.unit, SpellID(SpellID::FORTUNE)) || hasSpell(*old.unit, SpellID(SpellID::MISFORTUNE))
   || inactive(*old.unit) || inactive(after) || old.unit->isClone()
   || after.getAvailableHealth()!=old.health || after.getCount()!=old.count || after.getPosition()!=old.position)
   return std::nullopt;
  const auto bonuses=after.getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::MISFORTUNE))));
  if(bonuses->size()!=1)return std::nullopt;
  const auto other=Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::MISFORTUNE))).Not();
  const auto beforeOther=old.unit->getAllBonuses(other),afterOther=after.getAllBonuses(other);
  if(beforeOther->size()!=afterOther->size()
   || !std::is_permutation(beforeOther->begin(),beforeOther->end(),afterOther->begin(),
    [](const auto & a,const auto & b)
    {
     return Ports::HoMM3::sameMirthBaselineBonus(*a,*b);
    }))
   return std::nullopt;
  const auto & bonus=bonuses->front();
  const int amount=mastery>=MasteryLevel::ADVANCED ? 2 : 1;
  if(bonus->type!=BonusType::LUCK || bonus->valType!=BonusValueType::ADDITIVE_VALUE
   || bonus->val!=-amount || bonus->duration!=BonusDuration::N_TURNS || bonus->turnsRemain<=0
   || bonus->effectRange!=BonusLimitEffect::NO_LIMIT || bonus->limiter || bonus->propagator
   || bonus->updater || bonus->propagationUpdater || bonus->parameters || !bonus->stacking.empty())
   return std::nullopt;
  if(old.unit->hasBonusOfType(BonusType::NO_LUCK) || after.hasBonusOfType(BonusType::NO_LUCK))return 0;
  const int luck=old.unit->luckVal();
  // Default native rules clamp bad luck to zero. Do not price a source
  // negative-luck benefit that the host cannot execute.
  if(luck<amount || luck>3)return std::nullopt;
  if(luck < -3 || luck > 3 || after.luckVal()!=std::max(-3,luck-amount))return std::nullopt;
  if(total<0)return std::nullopt;
  return Ports::HoMM3::misfortuneSpellValue(total,luck,amount,duration,successPercent);
 }

 // Reconstruct the source findEnemyAttacks/addEnemy membership, including
 // the already-committed melee enemy exclusion (setMeleeEnemies does not set
 // m_enemyCanAttack). Unknown source rows are never replaced by host damage.
 static std::map<uint32_t,bool> sorrowAttackers(const CPlayerBattleCallback & battle,
  const std::vector<Snapshot> & before,PlayerColor player,const Environment * environment,
  bool staticMap,const Ports::HoMM3::CurrentDefendSnapshot * witness)
 {
  const auto side=battle.battleGetMySide();
  const auto opposite=side==BattleSide::ATTACKER?BattleSide::DEFENDER:BattleSide::ATTACKER;
  const auto enemy=Ports::HoMM3::captureNativeOrdinaryCensus(battle,opposite,environment,staticMap,false,witness);
  const auto own=Ports::HoMM3::captureNativeOrdinaryCensus(battle,side,environment,staticMap,false,witness);
  std::map<uint32_t,bool> result;if(!enemy || !own)return result;
  for(const auto & old:before)
  {
   if(old.owner==player || !old.unit->alive())continue;
   if(inactive(*old.unit) || hasSpell(*old.unit,SpellID(SpellID::HYPNOTIZE))){result.emplace(old.unit->unitId(),false);continue;}
   const auto found=std::find(enemy->friendlyIds.begin(),enemy->friendlyIds.end(),old.unit->unitId());
   if(found==enemy->friendlyIds.end())continue;
   const auto & row=enemy->rows.at(found-enemy->friendlyIds.begin());if(!row)continue;
   if(battle.battleCanShoot(old.unit)){result.emplace(old.unit->unitId(),true);continue;}
   bool known=true,can=false;
   for(const auto & friendly:before)
   {
    if(friendly.owner!=player || !friendly.unit->alive() || friendly.unit->isTurret())continue;
    const auto fi=std::find(enemy->enemyIds.begin(),enemy->enemyIds.end(),friendly.unit->unitId());
    if(fi==enemy->enemyIds.end()){known=false;break;}
    if(!(row->possibleTargets & (uint32_t{1}<<(fi-enemy->enemyIds.begin()))))continue;
    if(!inactive(*friendly.unit) && !hasSpell(*friendly.unit,SpellID(SpellID::HYPNOTIZE)) && !battle.battleCanShoot(friendly.unit))
    {
     const auto oi=std::find(own->friendlyIds.begin(),own->friendlyIds.end(),friendly.unit->unitId());
     if(oi==own->friendlyIds.end()){known=false;break;}
     const auto & ownRow=own->rows.at(oi-own->friendlyIds.begin());if(!ownRow){known=false;break;}
     if(ownRow->target && ownRow->target->attackTime<=1
      && own->enemyIds.at(ownRow->target->rosterIndex)==old.unit->unitId())continue;
    }
    can=true;break;
   }
   if(known)result.emplace(old.unit->unitId(),can);
  }
  return result;
 }

 // Source findEnemyAttacks/addEnemy sets the ranged enemy membership bit
 // independently of target ranking or lucky-strike state. This sufficient
 // certificate admits positive-luck shooters without relaxing exchange facts.
 static std::map<uint32_t,bool> misfortuneAttackers(const CPlayerBattleCallback & battle,
  const std::vector<Snapshot> & before,PlayerColor player)
 {
  std::map<uint32_t,bool> result;
  if(battle.battleGetDefendedTown() || battle.battleTacticDist())return result;
  for(const auto & old:before)
  {
   if(old.owner==player || !old.unit->alive())continue;
   const auto & enemy=*old.unit;
   if(inactive(enemy) || enemy.isTurret() || enemy.isClone() || enemy.isSummoned()
    || enemy.controllingSide()!=enemy.unitSide() || hasSpell(enemy,SpellID(SpellID::HYPNOTIZE)))
    {result.emplace(enemy.unitId(),false);continue;}
   // Melee membership depends on source projected target commitments, which
   // our zero-luck census cannot certify for a lucky target. Fail closed.
   if(!battle.battleCanShoot(&enemy))continue;
   bool can=false;
   for(const auto & own:before)
    if(own.owner==player && own.unit->alive() && !inactive(*own.unit) && !own.unit->isTurret()
     && battle.battleCanShoot(&enemy,own.unit->getPosition())){can=true;break;}
   result.emplace(enemy.unitId(),can);
  }
  return result;
 }

 // Certified opposing source target and exact ordinary damage ladder for
 // getWeaknessValue -> getAttackSkillValue -> getAttackBoostValue.
 static std::map<uint32_t,std::array<int32_t,5>> weaknessTargetFacts(const CPlayerBattleCallback & battle,
  const std::vector<Snapshot> & before,PlayerColor player,const Environment * environment,bool staticMap,
  const Ports::HoMM3::CurrentDefendSnapshot * witness,int amount)
 {
  using namespace Ports::HoMM3;
  std::map<uint32_t,std::array<int32_t,5>> result;
  const auto context=sourceCombatContext(battle,before,player);if(!context)return result;
  const auto side=battle.battleGetMySide()==BattleSide::ATTACKER?BattleSide::DEFENDER:BattleSide::ATTACKER;
  const auto census=captureNativeOrdinaryCensus(battle,side,environment,staticMap,false,witness);
  if(!census)return result;
  for(size_t i=0;i<census->friendlyIds.size();++i)
  {
   const auto * actor=battle.battleGetUnitByID(census->friendlyIds[i]);const auto & row=census->rows[i];
   if(!actor || !actor->alive() || !row || hasSpell(*actor,SpellID(SpellID::BLOODLUST))
    || hasSpell(*actor,SpellID(SpellID::PRECISION)))continue;
   if(!row->target || row->target->attackTime>1)continue;
   const auto * target=battle.battleGetUnitByID(census->enemyIds.at(row->target->rosterIndex));
   if(!target || !target->alive() || target->getAvailableHealth()>INT32_MAX)continue;
   const auto unit=NativeExchangeFacts::unit(battle,*actor,0,0,witness);if(!unit)continue;
   const auto pair=unit->canShoot?NativeExchangeFacts::rangedAttack(battle,*actor,*target,witness)
    :NativeExchangeFacts::meleeAttack(battle,*actor,*target,witness);if(!pair)continue;
   auto increased=*pair;const int capped=std::min(amount,pair->attack);
   if(increased.attack>INT32_MAX-capped)
    continue;
   increased.attack+=capped;
   // getEstimatedDamage(100) is a fixed base, not a 100-creature strike and
   // not a double shot. The certified ordinary pair excludes residual effects.
   SourceOrdinaryUnit fixed;fixed.minimumDamage=fixed.maximumDamage=100;
   const auto oldEstimate=OrdinaryExchangeDetail::damage(fixed,1,INT32_MAX,*pair,unit->canShoot);
   const auto newEstimate=OrdinaryExchangeDetail::damage(fixed,1,INT32_MAX,increased,unit->canShoot);
   const auto average=OrdinaryExchangeDetail::damage(*unit,actor->getAvailableHealth(),target->getAvailableHealth(),*pair,unit->canShoot);
   if(!oldEstimate || !newEstimate || !average)continue;
   result.emplace(actor->unitId(),std::array<int32_t,5>{context->values.at(actor->unitId()),*average,
    static_cast<int32_t>(target->getAvailableHealth()),*oldEstimate,*newEstimate});
  }
  return result;
 }
 static std::optional<double> addedWeaknessValue(const Snapshot & old,const battle::Unit & after,
  int amount,double duration,const std::array<int32_t,5> & facts)
 {
  if(hasSpell(*old.unit,SpellID(SpellID::WEAKNESS)) || inactive(*old.unit) || inactive(after)
   || old.unit->isClone() || after.getAvailableHealth()!=old.health || after.getCount()!=old.count
   || after.getPosition()!=old.position)return std::nullopt;
  const auto selector=Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::WEAKNESS)));
  const auto bonuses=after.getAllBonuses(selector);if(bonuses->size()!=1)return std::nullopt;
  const auto a=old.unit->getAllBonuses(selector.Not()),b=after.getAllBonuses(selector.Not());
  if(a->size()!=b->size() || !std::is_permutation(a->begin(),a->end(),b->begin(),
   [](const auto &x,const auto &y){return Ports::HoMM3::sameMirthBaselineBonus(*x,*y);}))return std::nullopt;
  const auto & bonus=bonuses->front();
  if(bonus->type!=BonusType::PRIMARY_SKILL || bonus->subtype!=BonusSubtypeID(PrimarySkill::ATTACK)
   || bonus->val!=-amount || bonus->valType!=BonusValueType::ADDITIVE_VALUE
   || bonus->duration!=BonusDuration::N_TURNS || bonus->turnsRemain<=0 || bonus->effectRange!=BonusLimitEffect::NO_LIMIT
   || bonus->limiter || bonus->propagator || bonus->updater || bonus->propagationUpdater || bonus->parameters || !bonus->stacking.empty())return std::nullopt;
  for(bool ranged:{false,true})if(after.getAttack(ranged)!=std::max(0,old.unit->getAttack(ranged)-amount))return std::nullopt;
  const auto value=Ports::HoMM3::weaknessSpellValue(facts[0],facts[1],facts[2],facts[3],facts[4],duration);
  if(!value)
   return std::nullopt;
  return *value;
 }

 static std::map<uint32_t,std::array<int32_t,3>> fortuneTargetFacts(const CPlayerBattleCallback & battle,
  const std::vector<Snapshot> & before,PlayerColor player,const Environment * environment,bool staticMap,
  const Ports::HoMM3::CurrentDefendSnapshot * witness)
 {
  using Facts=Ports::HoMM3::NativeExchangeFacts;
  std::map<uint32_t,std::array<int32_t,3>> result;
  const auto context=sourceCombatContext(battle,before,player);
  const auto policy=Ports::HoMM3::nativeRetaliationPolicy(battle,battle.battleGetMySide(),environment);
  if(!context || !policy || context->friendly>INT32_MAX/2)return result;
  const bool killsOnly=context->friendly*2<context->enemy && *policy;
  const auto census=Ports::HoMM3::captureNativeOrdinaryCensus(battle,battle.battleGetMySide(),environment,staticMap,killsOnly,witness);
  if(!census)return result;
  for(size_t i=0;i<census->friendlyIds.size();++i)
  {
   const auto * actor=battle.battleGetUnitByID(census->friendlyIds[i]);const auto & row=census->rows[i];
   if(!actor || !actor->alive() || actor->luckVal()!=0 || !row)continue;
   if(!row->target || row->target->attackTime>1){result.emplace(actor->unitId(),std::array<int32_t,3>{0,0,1});continue;}
   const auto * target=battle.battleGetUnitByID(census->enemyIds.at(row->target->rosterIndex));
   if(!target || !target->alive() || target->getAvailableHealth()>INT32_MAX)continue;
   const auto unit=Facts::unit(battle,*actor,0,0,witness);
   if(!unit)continue;
   const auto pair=unit->canShoot?Facts::rangedAttack(battle,*actor,*target,witness):Facts::meleeAttack(battle,*actor,*target,witness);
   if(!pair)continue;
   // INT32_MAX disables only the exchange helper's final victim-HP cap:
   // getFortuneValue reads the original uncapped average attack first.
   const auto damage=Ports::HoMM3::OrdinaryExchangeDetail::damage(*unit,actor->getAvailableHealth(),INT32_MAX,*pair,unit->canShoot);
   if(!damage)continue;
   result.emplace(actor->unitId(),std::array<int32_t,3>{context->values.at(actor->unitId()),*damage,static_cast<int32_t>(target->getAvailableHealth())});
  }
  return result;
 }
 static std::optional<double> addedFortuneValue(const Snapshot & old,const battle::Unit & after,
  int mastery,double duration,const std::array<int32_t,3> & facts)
 {
  if(hasSpell(*old.unit,SpellID(SpellID::FORTUNE)) || hasSpell(*old.unit,SpellID(SpellID::MISFORTUNE))
   || inactive(*old.unit) || inactive(after) || old.unit->isClone() || old.unit->luckVal()!=0
   || after.getAvailableHealth()!=old.health || after.getCount()!=old.count || after.getPosition()!=old.position)return std::nullopt;
  const auto selector=Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::FORTUNE)));
  const auto bonuses=after.getAllBonuses(selector);
  if(bonuses->size()!=1)return std::nullopt;
  const auto a=old.unit->getAllBonuses(selector.Not()),b=after.getAllBonuses(selector.Not());
  if(a->size()!=b->size() || !std::is_permutation(a->begin(),a->end(),b->begin(),
   [](const auto &x,const auto &y){return Ports::HoMM3::sameMirthBaselineBonus(*x,*y);}))return std::nullopt;
  const int amount=mastery>=MasteryLevel::ADVANCED?2:1;const auto & bonus=bonuses->front();
  if(bonus->type!=BonusType::LUCK || bonus->val!=amount || bonus->valType!=BonusValueType::ADDITIVE_VALUE
   || bonus->duration!=BonusDuration::N_TURNS || bonus->turnsRemain<=0 || bonus->effectRange!=BonusLimitEffect::NO_LIMIT
   || bonus->limiter || bonus->propagator || bonus->updater || bonus->propagationUpdater || bonus->parameters || !bonus->stacking.empty())return std::nullopt;
  if(old.unit->hasBonusOfType(BonusType::NO_LUCK) || after.hasBonusOfType(BonusType::NO_LUCK))return 0;
  if(after.luckVal()!=amount)return std::nullopt;
  const auto value=Ports::HoMM3::fortuneSpellValue(facts[0],0,amount,facts[1],facts[2],duration);
  if(!value)return std::nullopt;
  return *value;
 }

	static double estimateAttackAtDistance(const CBattleInfoCallback & battle, const battle::Unit & unit,
		const Attack & attack, uint32_t distance)
	{
		if(!attack.target || distance >= ReachabilityInfo::INFINITE_DIST)
			return 0;
		BattleAttackInfo info(&unit, attack.target, distance, attack.shooting);
		info.attackerPos = attack.from;
		// Native ranged damage uses the victim's head even for a tail aim.
		if(!attack.shooting)
			info.defenderPos = attack.aim;
		const auto estimate = battle.battleEstimateDamage(info);
		return (static_cast<double>(estimate.damage.min) + static_cast<double>(estimate.damage.max)) / 2;
	}

	static double estimateAttack(const CBattleInfoCallback & battle, const battle::Unit & unit, const Attack & attack)
	{
		if(!attack.target)
			return 0;
		const auto distance = attack.shooting ? 0u
			: battle.battleGetDistances(&unit, unit.getPosition()).at(attack.from.toInt());
		return estimateAttackAtDistance(battle, unit, attack, distance);
	}

	// Native immediate attack projection replaces the unported source AI target census.
	static Attack immediateAttack(const CPlayerBattleCallback & battle, const CStack & unit)
	{
		Attack best;
		if(!unit.alive() || inactive(unit) || unit.isTurret() || !unit.isValidTarget())
			return best;
		const auto available = battle.battleGetAvailableHexes(&unit, false);
		std::optional<ReachabilityInfo::TDistances> distances;
		double bestValue = 0;
		for(const auto * target : battle.battleGetAllStacks())
		{
			if(!target->alive() || battle.battleGetOwner(target) == battle.battleGetOwner(&unit))
				continue;
			const bool shooting = battle.battleCanShoot(&unit, target->getPosition());
			if(!shooting && !battle.battleCanAttackUnit(&unit, target))
				continue;
			for(const auto from : available)
			{
				if(shooting && from != unit.getPosition())
					continue;
				for(const auto aim : target->getHexes())
				{
					if(!shooting && (!battle.meleeAttackHexes(&unit, target, from).contains(aim)
						|| !battle.battleCanAttackFromHex(available, &unit, aim, from)))
						continue;
					Attack candidate{target, from, aim, shooting, 0};
					// Reuse one path census across melee targets, landings and aims.
					if(!shooting && !distances)
						distances = battle.battleGetDistances(&unit, unit.getPosition());
					candidate.damage = estimateAttackAtDistance(battle, unit, candidate,
						shooting ? 0u : distances->at(from.toInt()));
					const double value = Ports::HoMM3::spellDamageValue(
						static_cast<double>(target->estimateCombatValue()), target->getAvailableHealth(),
						static_cast<int64_t>(candidate.damage), false);
					if(value > bestValue)
					{
						bestValue = value;
						best = candidate;
					}
				}
			}
		}
		return best;
	}

	struct ArrivalGoal
	{
		Attack attack;
		uint32_t distance = 0;
		const CStack * unit = nullptr;
	};

	static ArrivalGoal arrivalGoal(const CPlayerBattleCallback & battle, const CStack & unit)
	{
		ArrivalGoal best;
		if(!unit.alive() || inactive(unit) || unit.isTurret() || !unit.isValidTarget()
			|| !unit.getPosition().isValid() || unit.getMovementRange() == 0)
			return best;
		const auto reach = battle.getReachability(&unit);
		BattleHexArray landings;
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			const BattleHex hex(index);
			if(hex.isAvailable() && reach.isReachable(hex))
				landings.insert(hex);
		}
		int bestTime = 0;
		double bestValue = 0;
		for(const auto * target : battle.battleGetAllStacks())
		{
			if(!target->alive() || battle.battleGetOwner(target) == battle.battleGetOwner(&unit))
				continue;
			const bool shooting = battle.battleCanShoot(&unit, target->getPosition());
			if(!shooting && !battle.battleCanAttackUnit(&unit, target))
				continue;
			for(const auto from : landings)
			{
				if(shooting && from != unit.getPosition())
					continue;
				for(const auto aim : target->getHexes())
				{
					if(!shooting && (!battle.meleeAttackHexes(&unit, target, from).contains(aim)
						|| !battle.battleCanAttackFromHex(landings, &unit, aim, from)))
						continue;
					const uint32_t distance = shooting ? 0u : reach.distances.at(from.toInt());
					const int time = Ports::HoMM3::speedArrivalTime(distance, unit.getMovementRange(), shooting);
					Attack attack{target, from, aim, shooting, 0};
					// Charge applies only to this turn's movement, not the whole approach.
					attack.damage = estimateAttackAtDistance(battle, unit, attack,
						std::min<uint32_t>(distance, unit.getMovementRange()));
					const double value = Ports::HoMM3::spellDamageValue(
						static_cast<double>(target->estimateCombatValue()), target->getAvailableHealth(),
						static_cast<int64_t>(attack.damage), false);
					if(!best.attack.target || time < bestTime || (time == bestTime && value > bestValue))
					{
						best = {attack, distance, &unit};
						bestTime = time;
						bestValue = value;
					}
				}
			}
		}
		return best;
	}

	// Source getExchangeEffect: two ordered attack sequences on fresh HP copies.
	// This native projection is limited to ordinary single-hex melee pairs.
	static double orderedMeleeExchange(const HypotheticBattle & battle,
		const battle::Unit & first, const battle::Unit & second, BattleHex firstHex, BattleHex secondHex)
	{
		auto attacker = first.acquireState();
		auto defender = second.acquireState();
		const auto attack = [&](auto & actor, auto & victim, BattleHex from, BattleHex aim)
		{
			BattleAttackInfo info(actor.get(), victim.get(), 0, false);
			info.attackerPos = from;
			info.defenderPos = aim;
			DamageEstimation retaliation;
			const auto hit = battle.battleEstimateDamage(info, &retaliation);
			const auto apply = [](auto & unit, const DamageEstimation & estimate)
			{
				int64_t damage = std::clamp<int64_t>(estimate.damage.min
					+ (estimate.damage.max - estimate.damage.min) / 2, 0, unit->getAvailableHealth());
				unit->damage(damage);
			};
			apply(victim, hit);
			if(!actor->alive() || !victim->alive())
				return;
			if(retaliation.damage.max > 0)
			{
				BattleAttackInfo counter(victim.get(), actor.get(), 0, false);
				counter.attackerPos = aim;
				counter.defenderPos = from;
				apply(actor, battle.battleEstimateDamage(counter));
				if(!actor->alive())
					return;
			}
			if(actor->getTotalAttacks(false) == 2)
				apply(victim, battle.battleEstimateDamage(info));
		};
		attack(attacker, defender, firstHex, secondHex);
		if(defender->alive() && attacker->alive())
			attack(defender, attacker, secondHex, firstHex);
		return Ports::HoMM3::spellDamageValue(static_cast<double>(second.estimateCombatValue()),
			second.getAvailableHealth(), second.getAvailableHealth() - defender->getAvailableHealth(), false)
			- Ports::HoMM3::spellDamageValue(static_cast<double>(first.estimateCombatValue()),
			first.getAvailableHealth(), first.getAvailableHealth() - attacker->getAvailableHealth(), false);
	}

	static double moveOrderValue(const HypotheticBattle & battle, const battle::Unit & unit, const ArrivalGoal & goal)
	{
		if(!goal.attack.target || goal.attack.shooting)
			return 0;
		const auto & enemy = *goal.attack.target;
		for(const auto * participant : std::array<const battle::Unit *, 2>{&unit, &enemy})
		{
			if(!participant->alive() || inactive(*participant) || participant->isClone()
				|| participant->isTurret() || participant->getHexes().size() != 1
				|| participant->getTotalAttacks(false) < 1 || participant->getTotalAttacks(false) > 2
				|| participant->isInvincible())
				return 0;
			for(const auto type : {BonusType::JOUSTING, BonusType::FIRST_STRIKE, BonusType::ON_COMBAT_EVENT, BonusType::COMBAT_EVENT_TRIGGER,
				BonusType::FEROCITY, BonusType::SPELL_BEFORE_ATTACK, BonusType::SPELL_AFTER_ATTACK,
				BonusType::ATTACKS_ALL_ADJACENT, BonusType::THREE_HEADED_ATTACK,
				BonusType::TWO_HEX_ATTACK_BREATH, BonusType::LONG_WEAPON, BonusType::SPELL_LIKE_ATTACK,
				BonusType::WIDE_BREATH, BonusType::PRISM_HEX_ATTACK_BREATH,
				BonusType::MULTIHEX_UNIT_ATTACK, BonusType::MULTIHEX_ENEMY_ATTACK})
				if(participant->hasBonusOfType(type))
					return 0;
		}
		if(battle.battleCanShoot(&enemy, unit.getPosition()))
			return 0;
		return std::max(0.0, orderedMeleeExchange(battle, unit, enemy, goal.attack.from, goal.attack.aim)
			+ orderedMeleeExchange(battle, enemy, unit, goal.attack.aim, goal.attack.from));
	}

	static double projectedSpeedValue(const HypotheticBattle & projected, const Snapshot & old,
		const battle::Unit & after, const ArrivalGoal & goal, SpellID spell, int rounds, int duration,
		const std::map<uint32_t, ArrivalGoal> & goals)
	{
		if(!goal.attack.target || after.getPosition() != old.position || after.getMovementRange() == 0)
			return 0;
		const bool slow = spell == SpellID::SLOW;
		const auto bonuses = after.getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
		if(bonuses->size() != 1)
			return 0;
		const auto & bonus = bonuses->front();
		if(bonus->type != BonusType::STACKS_SPEED || bonus->valType != (slow
			? BonusValueType::PERCENT_TO_ALL : BonusValueType::ADDITIVE_VALUE)
			|| (slow ? bonus->val >= 0 || bonus->val < -100 : bonus->val <= 0))
			return 0;
		StackWithBonuses base(&projected, old.unit);
		base.removeUnitBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::HASTE))));
		base.removeUnitBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::SLOW))));
		const int speed = old.unit->getMovementRange();
		const int newSpeed = Ports::HoMM3::speedAfterSpell(base.getMovementRange(), speed, bonus->val, slow);
		// Prayer uses source baseSpeed + bonus even when Haste/Slow changes actual speed.
		// Other speed spells retain the native counterspell-result check.
		if(spell != SpellID::PRAYER && newSpeed != static_cast<int>(after.getMovementRange()))
			return 0;
		const int oldTime = Ports::HoMM3::speedArrivalTime(goal.distance, speed, goal.attack.shooting);
		const int newTime = Ports::HoMM3::speedArrivalTime(goal.distance, newSpeed, goal.attack.shooting);
		double value = Ports::HoMM3::speedApproachValue(static_cast<int64_t>(old.value), oldTime, newTime,
			rounds, duration, completedTurn(*old.unit), slow, goal.attack.shooting);
		if(duration - (completedTurn(*old.unit) ? 1 : 0) <= 0
			|| (slow ? oldTime : newTime) > rounds)
			return value;
		if(!slow && newTime == 1 && goal.attack.target->getMovementRange() >= static_cast<uint32_t>(speed)
			&& goal.attack.target->getMovementRange() < static_cast<uint32_t>(newSpeed))
			value += moveOrderValue(projected, *old.unit, goal);
		if(slow && oldTime == 1)
		{
			double best = 0;
			for(const auto & [id, friendlyGoal] : goals)
			{
				if(friendlyGoal.attack.target != old.unit)
					continue;
				// Use the baseline stack, not the speed-modified mass preview.
				const auto * friendly = friendlyGoal.unit;
				if(!friendly || friendly->getMovementRange() > static_cast<uint32_t>(speed)
					|| friendly->getMovementRange() <= static_cast<uint32_t>(newSpeed))
					continue;
				best = std::max(best, moveOrderValue(projected, *friendly, friendlyGoal));
			}
			value += best;
		}
		return value;
	}

	static std::optional<std::array<Bonus, 3>> ordinaryPrayer(const battle::Unit & before,
		const battle::Unit & after, int mastery, int duration)
	{
		if(before.isClone() || before.isTurret() || before.isSummoned()
			|| before.getAvailableHealth() != after.getAvailableHealth()
			|| before.getPosition() != after.getPosition() || before.unitOwner() != after.unitOwner())
			return std::nullopt;
		const auto effects = after.getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(SpellID(SpellID::PRAYER))));
		if(effects->size() != 3 || duration <= 0)
			return std::nullopt;
		const int amount = mastery >= MasteryLevel::ADVANCED ? 4 : 2;
		if(before.getMovementRange() > uint32_t(INT32_MAX - amount)) return std::nullopt;
		std::array<Bonus, 3> result;
		std::array<bool, 3> seen{};
		for(const auto & effect : *effects)
		{
			if(effect->val != amount || effect->valType != BonusValueType::ADDITIVE_VALUE
				|| effect->duration != BonusDuration::N_TURNS || effect->turnsRemain != duration
				|| effect->effectRange != BonusLimitEffect::NO_LIMIT || effect->limiter
				|| !Ports::HoMM3::NativeTerrainFacts::neutralMetadata(*effect))
				return std::nullopt;
			int index = -1;
			if(effect->type == BonusType::PRIMARY_SKILL && effect->subtype == BonusSubtypeID(PrimarySkill::ATTACK)) index = 0;
			if(effect->type == BonusType::PRIMARY_SKILL && effect->subtype == BonusSubtypeID(PrimarySkill::DEFENSE)) index = 1;
			if(effect->type == BonusType::STACKS_SPEED && effect->subtype == BonusSubtypeID()) index = 2;
			if(index < 0 || seen.at(index)) return std::nullopt;
			result.at(index) = *effect;
			seen.at(index) = true;
		}
		if(int64_t{after.getAttack(false)} != int64_t{before.getAttack(false)} + amount
			|| int64_t{after.getAttack(true)} != int64_t{before.getAttack(true)} + amount
			|| int64_t{after.getDefense(false)} != int64_t{before.getDefense(false)} + amount)
			return std::nullopt;
		return result;
	}

	// Source component composition; target, threat and exchange values use the existing native projections.
	static std::optional<int32_t> projectedPrayerValue(const HypotheticBattle & baseline,
		const Snapshot & old, const battle::Unit & after, const std::vector<DefenseThreat> & threats,
		const std::map<uint32_t, ArrivalGoal> & goals, int mastery, int rounds, int duration,
		const HoMM3Forecast::PrefixProjection & forecast)
	{
		const auto effects = ordinaryPrayer(*old.unit, after, mastery, duration);
		if(!effects || old.value < 0 || old.value > INT32_MAX) return std::nullopt;
		StackWithBonuses attackOnly(&baseline, old.unit), defenseOnly(&baseline, old.unit), speedOnly(&baseline, old.unit);
		attackOnly.addUnitBonus({effects->at(0)});
		defenseOnly.addUnitBonus({effects->at(1)});
		speedOnly.addUnitBonus({effects->at(2)});
		if(speedOnly.getMovementRange() != after.getMovementRange()) return std::nullopt;
		std::map<uint32_t, IncomingReach> reach;
		const double defense = projectedDefenseValue(baseline, old, defenseOnly, threats,
			SpellID(SpellID::STONE_SKIN), rounds, duration, reach, forecast);
		const bool winLikely = forecast.outcome && forecast.outcome->winLikely;
		const double speed = winLikely ? 0 : projectedSpeedValue(baseline, old, speedOnly,
			goals.at(old.unit->unitId()), SpellID(SpellID::PRAYER), rounds, duration, goals);
		const double attack = winLikely || !old.attack.target ? 0 : Ports::HoMM3::attackSpellValue(old.value,
			old.attack.damage, estimateAttack(baseline, attackOnly, old.attack), old.attack.target->getAvailableHealth(),
			Ports::HoMM3::spellDurationValue(duration, rounds, completedTurn(*old.unit)));
		return Ports::HoMM3::prayerComponentValue(defense, speed, attack);
	}

 // Single Prayer cancellation: preserve cached identities/totals, recalculate
 // damage ratios and total combat value on the Dispel-cleaned native copy.
 static std::optional<int32_t> removedPrayerValue(const Ports::HoMM3::FreshPrayerContext & context,
  const HypotheticBattle & projected,
  const Snapshot & old, const battle::Unit & clean, const std::vector<DefenseThreat> & threats,
  const ArrivalGoal & goal, int rounds, bool friendly)
 {
  using namespace Ports::HoMM3;
  const int turns=remainingDuration(*old.unit,SpellID(SpellID::PRAYER));
  const auto prayer=old.unit->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::PRAYER))));
  if(prayer->size()!=3||hasSpell(clean,SpellID(SpellID::PRAYER))||inactive(clean))return std::nullopt;
  const int mastery=prayer->front()->val==4?MasteryLevel::ADVANCED:MasteryLevel::BASIC;
  const auto effects=ordinaryPrayer(clean,*old.unit,mastery,turns);if(!effects)return std::nullopt;
  const auto integer=[](double n)->std::optional<int32_t>{if(!std::isfinite(n)||n<0||n>INT32_MAX)return std::nullopt;return static_cast<int32_t>(n);};
  if(!context.current())return std::nullopt;
   const auto owner=HoMM3Forecast::sourceSide(old.unit->unitSide());
   const auto residual=context.residual(owner,old.unit->unitId());if(!residual)return std::nullopt;
   PrayerRemovalFacts f;f.singlePrayerRemoved=f.cleanedBaselineKnown=f.ownerContextKnown=true;
   f.friendly=friendly;f.ownerWinLikely=residual->winLikely;f.expectedDamage=residual->expectedDamage;
   f.mastery=mastery;f.duration=turns;f.rounds=rounds;f.completed=completedTurn(clean);
   const auto total=integer(clean.estimateCombatValue()),health=integer(clean.getAvailableHealth()),hp=integer(clean.getMaxHealth());
   if(!total||!health||!hp)return std::nullopt;
   f.total=*total;f.health=*health;f.hitPoints=*hp;f.topDamage=clean.getMaxHealth()-clean.getFirstHPleft();
   StackWithBonuses attackOnly(&projected,&clean),defenseOnly(&projected,&clean);
   attackOnly.addUnitBonus({effects->at(0)});defenseOnly.addUnitBonus({effects->at(1)});
   const DefenseThreat *worst=nullptr;double incoming=0;
   for(const auto&threat:threats)if(threat.target==old.unit){incoming+=threat.damage;if(!worst||threat.damage>worst->damage||(threat.damage==worst->damage&&worst->shooting&&!threat.shooting))worst=&threat;}
   const auto cached=integer(incoming);if(!cached)return std::nullopt;f.cachedIncoming=*cached;
   if(worst){
    const auto*enemy=projected.battleGetUnitByID(worst->enemy->unitId());if(!enemy)return std::nullopt;
    bool ranged=false;IncomingReach reach,boostedReach;
    f.defenseEstimatedBefore=incomingDamage(projected,*enemy,clean,ranged,reach);
    f.defenseEstimatedBoosted=incomingDamage(projected,*enemy,defenseOnly,ranged,boostedReach);
    const auto average=integer(f.defenseEstimatedBefore);if(!average)return std::nullopt;
    f.defenseAverageDamage=*average;f.hasWorstEnemy=true;
   }
   f.hasTarget=goal.attack.target!=nullptr;
   if(f.hasTarget){
    StackWithBonuses base(&projected,&clean);
    base.removeUnitBonus(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::HASTE))));
    base.removeUnitBonus(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::SLOW))));
    const auto speed=integer(clean.getMovementRange()),next=integer(double(base.getMovementRange())+effects->at(2).val),targetHP=integer(goal.attack.target->getAvailableHealth());
    if(!speed||!next||!targetHP)return std::nullopt;
    f.oldSpeed=*speed;f.newSpeed=*next;f.targetSpeed=goal.attack.target->getMovementRange();f.targetHealth=*targetHP;
    f.oldTime=speedArrivalTime(goal.distance,f.oldSpeed,goal.attack.shooting);f.newTime=speedArrivalTime(goal.distance,f.newSpeed,goal.attack.shooting);
    if(f.newTime==1&&f.targetSpeed>=f.oldSpeed&&f.targetSpeed<f.newSpeed){const auto order=integer(moveOrderValue(projected,clean,goal));if(!order)return std::nullopt;f.moveOrderValue=*order;}
    f.attackEstimatedBefore=estimateAttack(projected,clean,goal.attack);f.attackEstimatedBoosted=estimateAttack(projected,attackOnly,goal.attack);
    const auto average=integer(f.attackEstimatedBefore);if(!average)return std::nullopt;f.attackAverageDamage=*average;
   }
   return singlePrayerRemovalValue(f);
 }

	static bool shouldAttackNow(const CStack & enemy, const CStack & active,
		PlayerColor player, bool lastAction, const std::vector<Snapshot> & before,
		const std::vector<battle::Units> & order)
	{
		bool activeMeleeTarget = false;
		bool enemyCanAttack = false;
		for(const auto & old : before)
		{
			if(old.unit == &active)
				activeMeleeTarget = old.owner == player && old.attack.target == &enemy
					&& !old.attack.shooting && !active.hasBonusOfType(BonusType::BLOCKS_RETALIATION);
			if(old.unit == &enemy)
				enemyCanAttack = old.attack.target != nullptr;
		}
		// Native turn order replaces source m_expectedMoveOrder. A missing
		// enemy in this round cannot justify the imminent-threat branch.
		bool friendlyBeforeEnemy = true;
		if(!order.empty())
		{
			bool earlierFriend = false;
			for(const auto * unit : order.front())
			{
				if(unit->unitId() == enemy.unitId())
				{
					friendlyBeforeEnemy = earlierFriend;
					break;
				}
				if(unit->unitId() == active.unitId() || inactive(*unit) || unit->isTurret())
					continue;
				for(const auto & old : before)
					if(old.unit->unitId() == unit->unitId() && old.owner == player)
						earlierFriend = true;
			}
		}
		return Ports::HoMM3::hostileSpellShouldAttackNow(false, lastAction,
			activeMeleeTarget, enemyCanAttack, friendlyBeforeEnemy);
	}

	static std::vector<spells::Target> targets(const spells::Mechanics & mechanics)
	{
		std::vector<spells::Target> result;
		const auto types = mechanics.getTargetTypes();
		if(types.size() != 1)
			return result;
		if(types.front() == spells::AimType::NOTHING)
		{
			spells::Target target;
			if(mechanics.canBeCastAt(target))
				result.push_back(target);
		}
		else if(types.front() == spells::AimType::CREATURE || types.front() == spells::AimType::LOCATION)
		{
			for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
			{
				const BattleHex hex(index);
				if(!hex.isAvailable())
					continue;
				spells::Target target{battle::Destination(hex)};
				if(mechanics.canBeCastAt(target))
					result.push_back(std::move(target));
			}
		}
		return result;
	}

	static void checkUnchanged(const CPlayerBattleCallback & battle, const CGHeroInstance & hero,
		int64_t mana, const std::vector<Snapshot> & before)
	{
		if(static_cast<int64_t>(hero.mana) != mana || battle.battleGetAllStacks(false).size() != before.size())
			throw std::logic_error("HoMM3 spell projection changed live mana or stack membership");
		for(const auto & old : before)
			if(old.unit->getAvailableHealth() != old.health || old.unit->getCount() != old.count
				|| old.unit->getPosition() != old.position || battle.battleGetOwner(old.unit) != old.owner)
				throw std::logic_error("HoMM3 spell projection changed a live unit");
	}

public:
	static std::optional<BattleAction> choose(const std::shared_ptr<CPlayerBattleCallback> & real,
		const Environment * environment, const CStack & active,
		const HoMM3Forecast::PrefixProjection & forecast = {}, bool publicStaticMapCertified = false,
		const Ports::HoMM3::CurrentDefendSnapshot * currentDefend = nullptr,
        const HoMM3Forecast::NativeLifecycle * prayerLifecycle = nullptr, bool prayerNonEasyDifficulty = false)
	{
		if(currentDefend && !currentDefend->matchesContext(*real)) currentDefend=nullptr;
		const auto * hero = real->battleGetMyHero();
		if(!hero || real->battleCanCastSpell(hero, spells::Mode::HERO) != ESpellCastProblem::OK)
			return std::nullopt;
		const auto player = hero->getOwner();
		const int64_t mana = hero->mana;
		std::vector<Snapshot> before;
		double friendlyTotal = 0, enemyTotal = 0, friendlyAwake = 0, enemyAwake = 0;
		bool lastAction = true;
		for(const auto * unit : real->battleGetAllStacks(false))
		{
			const bool friendly = real->battleGetOwner(unit) == player;
			const double value = unit->alive() && !unit->isTurret()
				? static_cast<double>(unit->estimateCombatValue()) : 0;
			(friendly ? friendlyTotal : enemyTotal) += value;
			if(!inactive(*unit))
				(friendly ? friendlyAwake : enemyAwake) += value;
			if(friendly && unit != &active && unit->alive() && !inactive(*unit)
				&& !unit->isTurret() && !unit->isCatapult() && !unit->isBallista()
				&& !unit->isFirstAidTent() && !unit->isAmmoCart() && !completedTurn(*unit))
				lastAction = false;
			before.push_back({unit, unit->getAvailableHealth(), unit->getCount(), unit->getPosition(),
				real->battleGetOwner(unit), value, {}});
		}
		if(friendlyTotal <= 0 || enemyTotal <= 0)
			return std::nullopt;
		const int rounds = Ports::HoMM3::spellPlanningRounds(friendlyAwake, enemyAwake);
		const bool winLikely = forecast.outcome && forecast.outcome->winLikely;
		std::vector<battle::Units> order;
		real->battleGetTurnOrder(order, before.size(), 1);
		bool attacksReady = false;
		std::optional<CurseContext> curseFacts;
		bool curseContextReady = false;
		std::optional<std::vector<DefenseThreat>> defenseThreats;
		std::optional<std::map<uint32_t, ArrivalGoal>> arrivalGoals;
		std::shared_ptr<HypotheticBattle> previewBaseline;
		std::optional<BattleAction> best;
		double bestValue = 0;
		bool bestNow = false;
		for(const auto & entry : LIBRARY->spellh->objects)
		{
			const auto * spell = entry.get();
			const bool damage = spell->isDamage();
			const bool resurrection = spell->id == SpellID::RESURRECTION || spell->id == SpellID::ANIMATE_DEAD;
			if(forecast.outcome && forecast.outcome->spellsNotRequired && !resurrection)
				continue;
			const bool removal = spell->id == SpellID::CURE || spell->id == SpellID::DISPEL;
			const bool buff = attackBuff(spell->id);
			const bool curse = spell->id == SpellID::CURSE;
			const bool ray = spell->id == SpellID::DISRUPTING_RAY;
			const bool defense = defenseBuff(spell->id);
			const bool speed = spell->id == SpellID::HASTE || spell->id == SpellID::SLOW;
			const bool prayer = spell->id == SpellID::PRAYER;
			const bool blind = spell->id == SpellID::BLIND;
			const bool mirth = spell->id == SpellID::MIRTH;
   const bool sorrow = spell->id == SpellID::SORROW;
   const bool fortune = spell->id == SpellID::FORTUNE;
   const bool misfortune = spell->id == SpellID::MISFORTUNE;
   const bool weakness = spell->id == SpellID::WEAKNESS;
			const bool clone = spell->id == SpellID::CLONE;
			const bool summon = spell->id == SpellID::SUMMON_AIR_ELEMENTAL
				|| spell->id == SpellID::SUMMON_EARTH_ELEMENTAL
				|| spell->id == SpellID::SUMMON_FIRE_ELEMENTAL
				|| spell->id == SpellID::SUMMON_WATER_ELEMENTAL;
			if(!spell->isCombat() || spell->isCreatureAbility()
				|| (!damage && !resurrection && !removal && !buff && !curse && !ray && !defense && !speed && !prayer && !blind && !clone && !summon && !mirth && !sorrow && !fortune && !misfortune && !weakness)
				|| !spell->canBeCast(real.get(), spells::Mode::HERO, hero))
				continue;
			std::map<uint32_t, int32_t> rayTotals;
			if(ray)
			{
				rayTotals = rayTargetValues(*real, before, player, active, environment, publicStaticMapCertified, currentDefend);
				if(rayTotals.empty()) continue;
			}
			const int cost = real->battleGetSpellCost(spell, hero);
			if(cost < 0 || cost > mana)
				continue;
			if(!environment)
				throw std::logic_error("HoMM3 spell projection requires a native VCMI environment");
			if((damage || buff || curse || blind || removal || clone || speed || prayer || sorrow || fortune || misfortune || weakness) && !attacksReady)
			{
				for(auto & old : before)
					old.attack = immediateAttack(*real, *old.unit);
				attacksReady = true;
			}
			if(curse || sorrow || misfortune || weakness)
			{
				if(!curseContextReady)
				{
					curseFacts = curseContext(*real, before, player);
					curseContextReady = true;
				}
				if(!curseFacts || (!misfortune && winLikely)) continue;
			}
   std::map<uint32_t,std::array<int32_t,3>> fortuneFacts;
   std::optional<SourceCombatContext> fortuneContext;
   std::optional<int> fortuneRounds;
   if(fortune){fortuneContext=sourceCombatContext(*real,before,player);if(!fortuneContext)continue;
    fortuneRounds=Ports::HoMM3::directCursePlanningRounds(fortuneContext->friendlyAwake,fortuneContext->enemyAwake);if(!fortuneRounds)continue;
    fortuneFacts=fortuneTargetFacts(*real,before,player,environment,publicStaticMapCertified,currentDefend);if(fortuneFacts.empty())continue;}
   std::map<uint32_t,bool> sorrowCensus;
   if(sorrow || misfortune || weakness){sorrowCensus=misfortune ? misfortuneAttackers(*real,before,player)
    : sorrowAttackers(*real,before,player,environment,publicStaticMapCertified,currentDefend);if(sorrowCensus.empty())continue;}
   std::array<std::map<uint32_t,std::array<int32_t,5>>,2> weaknessFacts;
   if(weakness){for(int tier=0;tier<2;++tier)weaknessFacts[tier]=weaknessTargetFacts(*real,before,player,environment,publicStaticMapCertified,currentDefend,tier?6:3);
    if(weaknessFacts[0].empty() && weaknessFacts[1].empty())continue;}

			if((defense || prayer || removal) && !defenseThreats)
				defenseThreats = defenseCensus(*real, before);
			if((speed || prayer || removal) && !arrivalGoals)
			{
				arrivalGoals.emplace();
				for(const auto & old : before)
					arrivalGoals->emplace(old.unit->unitId(), arrivalGoal(*real, *old.unit));
			}
			spells::BattleCast legality(real.get(), hero, spells::Mode::HERO, spell);
			const auto mechanics = spell->battleMechanics(&legality);
			const CSpell::TargetInfo targetInfo(spell, mechanics->getEffectLevel(), spells::Mode::HERO);
			for(const auto & target : targets(*mechanics))
			{
				// castEval omits beforeCast, including Magic Mirror. Avoid direct
				// reflection until its friendly outcomes can be valued explicitly.
				const auto * aimed = target.empty() ? nullptr
					: real->battleGetUnitByPos(target.front().hexValue, true);
				if(aimed && Ports::HoMM3::nativeHeroSpellMayReflect(mechanics->isMassive(),
					spell->getLevelInfo(mechanics->getRangeLevel()).range == std::vector<int>{0},
					mechanics->isNegativeSpell(), aimed->valOfBonuses(BonusType::MAGIC_MIRROR)))
					continue;
				// A current baseline witness does not map native percent defense after Ray
				// to the source fixed historical bonus. Defended Ray targets stay unavailable.
				if(ray && aimed && aimed->hasBonusOfType(BonusType::UNIT_DEFENDING)) continue;
				if(!previewBaseline)
					previewBaseline = std::make_shared<HypotheticBattle>(environment, real);
				auto projected = std::make_shared<HypotheticBattle>(environment, previewBaseline);
				spells::BattleCast cast(projected.get(), hero, spells::Mode::HERO, spell);
				cast.castEval(projected->getServerCallback(), target);
				checkUnchanged(*real, *hero, mana, before);
				double value = 0, friendlyLoss = 0, enemyLoss = 0;
                bool prayerCancellationCandidate=false;size_t cancellationChangedUnits=0;
                std::vector<int32_t> friendlyPrayerValues, enemyPrayerValues;
				std::map<uint32_t, IncomingReach> projectedReach;
				int64_t friendlyHealth = 0;
				bool supported = true;
				bool castNow = damage ? targetInfo.massive || spell->getTargetType() == spells::AimType::LOCATION
					|| spell->id == SpellID::CHAIN_LIGHTNING || lastAction
					: summon || targetInfo.massive || lastAction;
				if(ray) castNow = true; // rayTargetValues already proves source isLastAction.
				if(aimed && real->battleGetOwner(aimed) != player)
					for(const auto & old : before)
						if(old.unit == aimed)
							castNow |= shouldAttackNow(*old.unit, active, player, lastAction, before, order);
                if(removal) for(const auto & old : before)
                {
                    const auto *after=projected->battleGetUnitByID(old.unit->unitId());
                    if(after && hasSpell(*old.unit,SpellID(SpellID::PRAYER)) && !hasSpell(*after,SpellID(SpellID::PRAYER))) prayerCancellationCandidate=true;
                }
                const auto evaluateTargets=[&](const Ports::HoMM3::FreshPrayerContext *prayerContext)
                {
				for(const auto & old : before)
				{
					const auto * after = projected->battleGetUnitByID(old.unit->unitId());
					if(ray && currentDefend && currentDefend->bonus(*real,*old.unit)
					 && (!after || !currentDefend->bonus(*projected,*after))) supported=false;
					const int64_t health = after && after->alive() ? after->getAvailableHealth() : 0;
					const bool friendly = old.owner == player;
					const int successPercent = Ports::HoMM3::nativeSpellSuccessPercent(
						mechanics->isNegativeSpell(), mechanics->isMagicalEffect(), old.unit->magicResistance());
					if(friendly)
						friendlyHealth += health;
					if(ray && !friendly && old.unit == aimed)
					{
						const int level = mechanics->getEffectLevel();
						const int bonus = level >= MasteryLevel::EXPERT ? 5 : level >= MasteryLevel::ADVANCED ? 4 : 3;
						if(!after || !rayTotals.contains(old.unit->unitId()) || !ordinaryRayEffect(*old.unit, *after, bonus))
							supported = false;
						else
						{
							const auto sourceValue = Ports::HoMM3::directDisruptingRaySpellValue(
								rayTotals.at(old.unit->unitId()), bonus, true, false, true, successPercent / 100.0);
							if(!sourceValue) supported = false;
							else value += *sourceValue;
						}
					}
					if(damage && health < old.health && !old.unit->isTurret())
						(friendly ? friendlyLoss : enemyLoss) += Ports::HoMM3::resistanceWeightedProjectedLoss(
							old.value, old.health, old.health - health, inactive(*old.unit), successPercent);
					if(!after || !after->alive())
						continue;
					const double duration = Ports::HoMM3::spellDurationValue(mechanics->getEffectDuration(), rounds, completedTurn(*old.unit));
     if(mirth && friendly && hasSpell(*after,SpellID(SpellID::MIRTH)))
     {
      if(projected->battleGetOwner(after)!=old.owner) supported=false;
      else
      {
       const auto gain=addedMirthValue(old,*after,mechanics->getEffectLevel(),duration);
       if(!gain)supported=false;
       else {value+=*gain;castNow|=old.unit==&active;}
      }
     }
     if(fortune && friendly && hasSpell(*after,SpellID(SpellID::FORTUNE)))
     {
      const auto facts=fortuneFacts.find(old.unit->unitId());
      if(projected->battleGetOwner(after)!=old.owner || facts==fortuneFacts.end())supported=false;
      else
      {
       const int sourceRounds=*fortuneRounds;
       const auto gain=addedFortuneValue(old,*after,mechanics->getEffectLevel(),
        Ports::HoMM3::spellDurationValue(mechanics->getEffectDuration(),sourceRounds,completedTurn(*old.unit)),facts->second);
       if(!gain)supported=false;else{value+=*gain;castNow|=old.unit==&active;}
      }
     }
     if(weakness && !friendly && hasSpell(*after,spell->id))
     {
      const int tier=mechanics->getEffectLevel()>=MasteryLevel::ADVANCED?1:0;
      const auto facts=weaknessFacts[tier].find(old.unit->unitId());const auto threat=sorrowCensus.find(old.unit->unitId());
      if(projected->battleGetOwner(after)!=old.owner || facts==weaknessFacts[tier].end() || threat==sorrowCensus.end())supported=false;
      else
      {
       const auto amount=addedWeaknessValue(old,*after,tier?6:3,
        Ports::HoMM3::spellDurationValue(mechanics->getEffectDuration(),curseFacts->rounds,completedTurn(*old.unit)),facts->second);
       bool forecastDead=false;
       if(forecast.outcome && forecast.outcome->side==HoMM3Forecast::sourceSide(real->battleGetMySide()))
       {
        const auto initial=forecast.outcome->initial.find(old.unit->unitId());const auto remaining=forecast.outcome->health.find(old.unit->unitId());
        forecastDead=initial!=forecast.outcome->initial.end() && remaining!=forecast.outcome->health.end()
         && initial->second.liveHealth==old.health && remaining->second<=0;
       }
       if(!amount)supported=false;else if(threat->second && !forecastDead)value+=*amount;
      }
     }
     if((sorrow || misfortune) && !friendly && hasSpell(*after,spell->id))
     {
      const auto threat=sorrowCensus.find(old.unit->unitId());
      bool forecastDead=false;
      if(forecast.outcome && forecast.outcome->side==HoMM3Forecast::sourceSide(real->battleGetMySide()))
      {
       const auto initial=forecast.outcome->initial.find(old.unit->unitId());
       const auto remaining=forecast.outcome->health.find(old.unit->unitId());
       forecastDead=initial!=forecast.outcome->initial.end() && remaining!=forecast.outcome->health.end()
        && initial->second.liveHealth==old.health && remaining->second<=0;
      }
      if(projected->battleGetOwner(after)!=old.owner || threat==sorrowCensus.end())supported=false;
      else
      {
       const double sourceDuration=Ports::HoMM3::spellDurationValue(mechanics->getEffectDuration(),curseFacts->rounds,completedTurn(*old.unit));
       const auto amount=misfortune
        ? addedMisfortuneValue(old,*after,mechanics->getEffectLevel(),sourceDuration,curseFacts->values.at(old.unit->unitId()),successPercent)
        : addedSorrowValue(old,*after,mechanics->getEffectLevel(),sourceDuration,curseFacts->values.at(old.unit->unitId()),successPercent);
       if(!amount)supported=false;else if(threat->second && !forecastDead)value+=*amount;
      }
     }
					if(resurrection && friendly && !after->isClone() && after->getCount() > old.count)
					{
						// Native heal.lua computes this capacity before capping against
						// wounds; the forecast supplies the source decided-battle exception.
						const int64_t healable = mechanics->applySpellBonus(mechanics->getEffectValue(), old.unit)
							/ std::max<uint32_t>(1, old.unit->getMaxHealth());
						if(!Ports::HoMM3::resurrectionFitsCasualties(healable,
							old.unit->unitBaseAmount() - old.count, old.unit->unitBaseAmount(), winLikely))
							continue;
						if(winLikely && spell->id == SpellID::RESURRECTION
							&& mechanics->getEffectLevel() < MasteryLevel::ADVANCED)
							continue;
						const int urgency = friendlyAwake > enemyAwake && rounds <= 1 ? 2 : 1;
						value += static_cast<double>(after->estimateCombatValue())
							* (after->getCount() - old.count) / after->getCount() * urgency;
						castNow |= old.unit == &active || winLikely;
					}
					if(buff && (spell->id == SpellID::BLESS || !winLikely)
						&& friendly && !hasSpell(*old.unit, spell->id) && hasSpell(*after, spell->id)
						&& old.attack.target && !inactive(*old.unit))
					{
						if((spell->id == SpellID::BLOODLUST && old.attack.shooting)
							|| (spell->id == SpellID::PRECISION && !old.attack.shooting))
							continue;
						value += Ports::HoMM3::attackSpellValue(old.value, old.attack.damage,
							estimateAttack(*projected, *after, old.attack), old.attack.target->getAvailableHealth(), duration);
						castNow |= old.unit == &active;
					}
					// The source integer ladder uses per-creature averages, not rounded native attacks.
					if(curse && !friendly
						&& !inactive(*old.unit) && old.attack.target
						&& !hasSpell(*old.unit, spell->id) && hasSpell(*after, spell->id))
					{
						const auto averages = curseAverages(*old.unit, *after, old.attack.shooting, mechanics->getEffectLevel());
						if(!averages)
							supported = false;
						else if(estimateAttack(*projected, *after, old.attack) < old.attack.damage)
							value += Ports::HoMM3::directCurseSpellValue(curseFacts->values.at(old.unit->unitId()),
								averages->first, averages->second, Ports::HoMM3::spellDurationValue(
									mechanics->getEffectDuration(), curseFacts->rounds, completedTurn(*old.unit)), successPercent);
					}
					if(defense && friendly && !inactive(*old.unit) && !old.unit->isTurret()
						&& !hasSpell(*old.unit, spell->id) && hasSpell(*after, spell->id))
					{
						value += projectedDefenseValue(*projected, old, *after, *defenseThreats,
							spell->id, rounds, mechanics->getEffectDuration(), projectedReach, forecast);
						castNow |= old.unit == &active;
					}
					if(prayer && friendly && !inactive(*old.unit) && !hasSpell(*old.unit, spell->id)
						&& hasSpell(*after, spell->id))
					{
						const auto amount = projectedPrayerValue(*previewBaseline, old, *after, *defenseThreats,
							*arrivalGoals, mechanics->getEffectLevel(), rounds, mechanics->getEffectDuration(), forecast);
						if(!amount) supported = false;
						else value += *amount;
						castNow |= old.unit == &active;
					}
					// Pinned getSpeedValue/getMuckAndMireValue suppress direct speed casts in a forecast win.
					if(speed && !winLikely && (friendly == (spell->id == SpellID::HASTE)) && !inactive(*old.unit)
						&& !hasSpell(*old.unit, spell->id) && hasSpell(*after, spell->id))
					{
						value += projectedSpeedValue(*projected, old, *after,
							arrivalGoals->at(old.unit->unitId()), spell->id, rounds,
							mechanics->getEffectDuration(), *arrivalGoals) * successPercent / 100;
						castNow |= spell->id == SpellID::HASTE;
					}
					// getBlindValue suppresses control in a forecast win, including wins with losses.
					if(blind && !winLikely && !friendly && !inactive(*old.unit) && inactive(*after) && old.attack.target)
						value += Ports::HoMM3::blindSpellValue(old.value, enemyAwake,
							spell->getLevelPower(mechanics->getEffectLevel()), duration) * successPercent / 100;
					if(removal)
					{
						if(spell->id == SpellID::CURE && friendly && health > old.health && old.health > 0)
						{
							value += old.value * static_cast<double>(health - old.health) / old.health;
							castNow |= old.unit == &active;
						}
						bool removedAttackEffect = false, removedCurse = false, removedBlind = false, removedParalyze = false;
						bool removedMirth = false, removedPrayer = false;
						int removedEffects = 0;
						double removedDuration = 0;
						int attackEffectTurns = -1;
						for(const auto & effect : LIBRARY->spellh->objects)
						{
							if(!hasSpell(*old.unit, effect->id) || hasSpell(*after, effect->id))
								continue;
							if(!removalSupported(effect->id))
								supported = false;
							removedAttackEffect |= attackBuff(effect->id);
							removedCurse |= effect->id == SpellID::CURSE;
							removedBlind |= effect->id == SpellID::BLIND;
							removedParalyze |= effect->id == SpellID::PARALYZE;
							removedMirth |= effect->id == SpellID::MIRTH;
                            removedPrayer |= effect->id == SpellID::PRAYER;
							++removedEffects;
							const int effectTurns = remainingDuration(*old.unit, effect->id);
							if(attackBuff(effect->id) || effect->id == SpellID::CURSE)
							{
								if(attackEffectTurns >= 0 && attackEffectTurns != effectTurns)
									supported = false;
								attackEffectTurns = effectTurns;
							}
							const double effectDuration = Ports::HoMM3::spellDurationValue(effectTurns, rounds, completedTurn(*old.unit));
							if(effect->id == SpellID::MIRTH)
							{
								const auto mirth = removedMirthValue(old, *after, effectDuration);
								if(!mirth || projected->battleGetOwner(after) != old.owner)
									supported = false;
								else
									// getCancelValue revalues the buff after cancellation, with ownership sign.
									value += (friendly ? -1 : 1) * *mirth;
							}
							removedDuration = std::max(removedDuration, effectDuration);
							if((effect->id == SpellID::BLIND || effect->id == SpellID::PARALYZE)
								&& inactive(*old.unit) && !inactive(*after))
								value += (friendly ? 1 : -1) * old.value * effectDuration;
							if(friendly)
								castNow |= old.unit == &active;
						}
						if(removedEffects>0 || health!=old.health) ++cancellationChangedUnits;
                        if(removedPrayer)
                        {
                            prayerCancellationCandidate=true;
                            if(removedEffects!=1 || spell->id!=SpellID::DISPEL || !prayerContext || !defenseThreats || !arrivalGoals || !arrivalGoals->contains(old.unit->unitId())) supported=false;
                            else
                            {
                                // Original getDispelValue copies this target alone. Other stacks
                                // retain their pre-cast Prayer even when the real spell is mass.
                                auto isolated=Ports::HoMM3::singlePrayerRemovedCopy(environment,previewBaseline,old.unit->unitId());
                                auto clean=isolated->getForUpdate(old.unit->unitId());
                                const auto prayerValue=removedPrayerValue(*prayerContext,*isolated,old,*clean,*defenseThreats,arrivalGoals->at(old.unit->unitId()),rounds,friendly);
                                if(!prayerValue || !Ports::HoMM3::samePrayerRemovalState(*clean,*after) || health!=old.health || projected->battleGetOwner(after)!=old.owner) supported=false;
                                else (friendly?friendlyPrayerValues:enemyPrayerValues).push_back(*prayerValue);
                            }
                        }
                        // Sequential mixed-effect cancellation is not projected here.
						if((removedParalyze || removedMirth) && removedEffects != 1)
							supported = false;
						if(removedBlind && (removedAttackEffect || removedCurse))
							supported = false;
						if(old.attack.target && (removedAttackEffect || removedCurse))
						{
							const double newDamage = estimateAttack(*projected, *after, old.attack);
							value += (friendly ? 1 : -1) * Ports::HoMM3::removedAttackSpellValue(old.value,
								old.attack.damage, newDamage, old.attack.target->getAvailableHealth(), removedDuration);
						}
					}
				}
                };
                if(prayerCancellationCandidate)
                {
                    if(!prayerLifecycle || !Ports::HoMM3::onlyPrayerRemoved(environment,previewBaseline,*projected)) supported=false;
                    else
                    {
                        const auto evaluated=Ports::HoMM3::FreshPrayerContext::evaluate(*real,*prayerLifecycle,prayerNonEasyDifficulty,
                            [&](const Ports::HoMM3::FreshPrayerContext & context)->std::optional<int32_t>{evaluateTargets(&context);return 0;});
                        if(!evaluated) supported=false;
                    }
                }
                else evaluateTargets(nullptr);
                if(prayerCancellationCandidate)
                {
                    if(cancellationChangedUnits!=friendlyPrayerValues.size()+enemyPrayerValues.size()) supported=false;
                    const auto mass=Ports::HoMM3::sumPrayerRemovalValues(friendlyPrayerValues,enemyPrayerValues);
                    if(!mass) supported=false; else value+=*mass;
                }
				if(summon || clone)
				{
					// HypotheticBattle-created units are not present in its CStack view.
					bool createdClone = false;
					for(const auto * unit : projected->battleGetUnitsIf([](const battle::Unit * unit) { return !unit->isGhost(); }))
					{
						if(!unit->alive() || projected->battleGetOwner(unit) != player)
							continue;
						const auto prior = std::find_if(before.begin(), before.end(),
							[&](const Snapshot & old) { return old.unit->unitId() == unit->unitId(); });
						if(clone)
						{
							createdClone |= unit->isClone() && prior == before.end();
							continue;
						}
						if(unit->isClone() || !unit->isSummoned())
							continue;
						const int added = unit->getCount() - (prior == before.end() ? 0 : prior->count);
						if(added > 0)
							value += static_cast<double>(unit->estimateCombatValue()) * added / unit->getCount();
					}
					if(clone && createdClone)
						for(const auto & old : before)
							if(old.unit == aimed && old.owner == player && !completedTurn(*old.unit) && old.attack.target)
							{
								value += Ports::HoMM3::spellDamageValue(
									static_cast<double>(old.attack.target->estimateCombatValue()),
									old.attack.target->getAvailableHealth(), static_cast<int64_t>(old.attack.damage), false);
								castNow |= old.unit == &active;
							}
				}
				if(damage)
					value = Ports::HoMM3::massSpellDamageValue(enemyLoss, friendlyLoss, enemyTotal, friendlyTotal);
				const double weighted = Ports::HoMM3::manaWeightedSpellValue(value, mana, cost);
				if(supported && friendlyHealth > 0 && std::isfinite(weighted) && weighted > bestValue)
				{
					bestValue = weighted;
					bestNow = castNow;
					best = BattleAction::makeHeroSpellcast(real->battleGetMySide(), target, spell->id);
				}
			}
		}
		return bestNow ? best : std::nullopt;
	}
};
}
