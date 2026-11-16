/*
 * FHeroes2NativeThreat.h, part of VCMI engine
 * GPL-2.0-or-later.
 */
#pragma once

#include "FHeroes2OrdinaryThreat.h"
#include "FHeroes2NativeStrength.h"
#include <map>

namespace AIIntegration::Ports::FHeroes2
{
// Source distance ignores engagement while ammunition still classifies an
// archer. The native fallback retains its original ability-to-fire projection.
inline bool nativeThreatRangedDistance(const CStack & unit, bool shooting, bool sourceDamage)
{
	return sourceDamage ? unit.isShooter() && unit.shots.canUse(1) : shooting;
}

// Pinned Unit::isDoubleAttack only counts the second ranged strike when
// ammunition remains for it. This is live threat, not innate strength.
inline int nativeThreatAttackCount(const CStack & unit, bool shooting)
{
	const int attacks = std::max(1, static_cast<int>(unit.getTotalAttacks(shooting)));
	// Native ammo carts can supply shots even with an empty displayed counter.
	// Only canonical double attack is adjusted; modded multi-strikes retain
	// the pre-existing native projection rather than inventing a new rule.
	return shooting && attacks == 2 && !unit.shots.canUse(2) ? 1 : attacks;
}

inline std::optional<OrdinaryThreatUnit> ordinaryNativeThreatUnit(const CStack & unit, bool allowRanged = false)
{
	const auto innate = nativeStrengthFacts(unit);
	if(!innate || !unit.alive() || unit.isClone()
		|| (innate->archer && !allowRanged) || innate->twoCellAttack || innate->allAdjacentAttack
		|| innate->unlimitedRetaliation || unit.getMaxHealth() != innate->hitPoints
		|| unit.getMinDamage(false) != innate->minimumDamage || unit.getMaxDamage(false) != innate->maximumDamage
		|| unit.getTotalAttacks(false) != (innate->doubleMelee ? 2 : 1)
		|| unit.getAvailableHealth() > std::numeric_limits<uint32_t>::max())
		return std::nullopt;
	if(innate->archer && (!unit.isShooter() || unit.getAttack(true) != unit.getAttack(false)
		|| unit.getMinDamage(true) != innate->minimumDamage || unit.getMaxDamage(true) != innate->maximumDamage
		|| unit.getTotalAttacks(true) != (innate->doubleShooting ? 2 : 1)))
		return std::nullopt;
	// Width has no effect on source damage arithmetic. Reachability/footprints
	// remain native. Flight is admitted only when the live ability is exactly
	// the canonical creature record, never a dynamic grant or duplicate.
	const auto liveFlight = unit.getAllBonuses(Selector::type()(BonusType::FLYING));
	if(innate->flying)
	{
		const auto baseFlight = unit.unitType()->getBonusBearer()->getAllBonuses(Selector::type()(BonusType::FLYING));
		if(baseFlight->size() != 1 || liveFlight->size() != 1
			|| (*baseFlight)[0].get() != (*liveFlight)[0].get())
			return std::nullopt;
	}
	else if(!liveFlight->empty())
		return std::nullopt;
	// This exemption must be the unchanged canonical creature ability, not a
	// granted spell/artifact or duplicate whose semantics are uncertified.
	const auto liveMelee = unit.getAllBonuses(Selector::type()(BonusType::NO_MELEE_PENALTY));
	if(innate->noMeleePenalty)
	{
		const auto baseMelee = unit.unitType()->getBonusBearer()->getAllBonuses(Selector::type()(BonusType::NO_MELEE_PENALTY));
		if(!innate->archer || baseMelee->size() != 1 || liveMelee->size() != 1
			|| (*baseMelee)[0].get() != (*liveMelee)[0].get())
			return std::nullopt;
	}
	else if(!liveMelee->empty())
		return std::nullopt;
	uint32_t archeryPercent = 0;
	int damageMode = 0;
	unsigned magicSchools = 0;
	bool shield = false;
	const auto bonuses = unit.getAllBonuses(Selector::all);
	for(const auto & bonus : *bonuses)
	{
		switch(bonus->type)
		{
		case BonusType::MANA_REGENERATION: case BonusType::SPELL_DAMAGE:
		case BonusType::THIEVES_GUILD_ACCESS: case BonusType::WANDERING_CREATURES_JOIN_BONUS:
		case BonusType::HERO_EXPERIENCE_GAIN_PERCENT: case BonusType::MANA_PER_KNOWLEDGE_PERCENTAGE:
		case BonusType::MAX_LEARNABLE_SPELL_LEVEL: case BonusType::HERO_SPELL_CASTS_PER_COMBAT_TURN:
			continue;
		default: break;
		}
		if(bonus->limiter || bonus->updater || bonus->propagator || bonus->propagationUpdater)
			return std::nullopt;
		switch(bonus->type)
		{
		case BonusType::PRIMARY_SKILL: case BonusType::CREATURE_DAMAGE:
		case BonusType::STACK_HEALTH: case BonusType::STACKS_SPEED:
		case BonusType::MORALE: case BonusType::LUCK:
		case BonusType::UNDEAD: case BonusType::NON_LIVING: case BonusType::LIVING:
		case BonusType::MIND_IMMUNITY: case BonusType::SPELL_IMMUNITY:
		case BonusType::MAGIC_RESISTANCE: case BonusType::DRAGON_NATURE:
		case BonusType::CHARGE_IMMUNITY: case BonusType::KING:
			break;
		case BonusType::MAGIC_SCHOOL_SKILL:
		{
			// Hero school mastery changes spell casting, not ordinary unit
			// damage. Creature casting abilities remain outside this domain.
			unsigned school = 0;
			if(bonus->sid == BonusSourceID(SecondarySkill(SecondarySkill::FIRE_MAGIC))
				&& bonus->subtype == BonusSubtypeID(SpellSchool::FIRE)) school = 1;
			if(bonus->sid == BonusSourceID(SecondarySkill(SecondarySkill::AIR_MAGIC))
				&& bonus->subtype == BonusSubtypeID(SpellSchool::AIR)) school = 2;
			if(bonus->sid == BonusSourceID(SecondarySkill(SecondarySkill::WATER_MAGIC))
				&& bonus->subtype == BonusSubtypeID(SpellSchool::WATER)) school = 4;
			if(bonus->sid == BonusSourceID(SecondarySkill(SecondarySkill::EARTH_MAGIC))
				&& bonus->subtype == BonusSubtypeID(SpellSchool::EARTH)) school = 8;
			if(!school || (magicSchools & school) || bonus->source != BonusSource::SECONDARY_SKILL
				|| bonus->duration != BonusDuration::PERMANENT || bonus->effectRange != BonusLimitEffect::NO_LIMIT
				|| bonus->valType != BonusValueType::BASE_NUMBER || bonus->val < 1 || bonus->val > 3
				|| bonus->parameters || !bonus->stacking.empty())
				return std::nullopt;
			magicSchools |= school;
			break;
		}
		case BonusType::GENERAL_DAMAGE_REDUCTION:
			// Air Shield is the H3 analogue of pinned H2 Shield. H2 always
			// halves ranged damage; native H3 still applies its own 25/50%.
			if(shield || bonus->source != BonusSource::SPELL_EFFECT
				|| bonus->sid != BonusSourceID(SpellID(SpellID::AIR_SHIELD))
				|| bonus->subtype != BonusSubtypeID(BonusCustomSubtype::damageTypeRanged)
				|| bonus->duration != BonusDuration::N_TURNS || bonus->turnsRemain <= 0
				|| bonus->effectRange != BonusLimitEffect::NO_LIMIT
				|| bonus->valType != BonusValueType::ADDITIVE_VALUE
				|| (bonus->val != 25 && bonus->val != 50) || bonus->parameters || !bonus->stacking.empty())
				return std::nullopt;
			shield = true;
			break;
		case BonusType::ALWAYS_MAXIMUM_DAMAGE:
		case BonusType::ALWAYS_MINIMUM_DAMAGE:
		{
			const bool bless = bonus->type == BonusType::ALWAYS_MAXIMUM_DAMAGE;
			// H2 selects the innate damage endpoint at every skill level.
			// H3's advanced/expert +/-1 remains native authoritative damage.
			if(damageMode || bonus->subtype != BonusSubtypeID() || bonus->source != BonusSource::SPELL_EFFECT
				|| bonus->sid != BonusSourceID(SpellID(bless ? SpellID::BLESS : SpellID::CURSE))
				|| bonus->duration != BonusDuration::N_TURNS || bonus->turnsRemain <= 0
				|| bonus->effectRange != BonusLimitEffect::NO_LIMIT
				|| bonus->valType != BonusValueType::INDEPENDENT_MAX
				|| (bonus->val != 0 && bonus->val != 1) || bonus->parameters || !bonus->stacking.empty())
				return std::nullopt;
			damageMode = bless ? 1 : -1;
			break;
		}
		case BonusType::PERCENTAGE_DAMAGE_BOOST:
			// Public live certificate for unmodified basic/advanced/expert
			// Archery. No opposing hero skill lookup, artifact amplification,
			// specialty, duplicate record or synthetic effective percentage.
			if(archeryPercent || bonus->subtype != BonusSubtypeID(BonusCustomSubtype::damageTypeRanged)
				|| bonus->source != BonusSource::SECONDARY_SKILL
				|| bonus->sid != BonusSourceID(SecondarySkill(SecondarySkill::ARCHERY))
				|| bonus->duration != BonusDuration::PERMANENT || bonus->effectRange != BonusLimitEffect::NO_LIMIT
				|| bonus->valType != BonusValueType::BASE_NUMBER || bonus->parameters || !bonus->stacking.empty()
				|| (bonus->val != 10 && bonus->val != 25 && bonus->val != 50))
				return std::nullopt;
			archeryPercent = bonus->val;
			break;
		case BonusType::NO_MELEE_PENALTY:
			if(!innate->noMeleePenalty || bonus->source != BonusSource::CREATURE_ABILITY
				|| bonus->sid != BonusSourceID(unit.creatureId()) || bonus->duration != BonusDuration::PERMANENT
				|| bonus->effectRange != BonusLimitEffect::NO_LIMIT || bonus->parameters || !bonus->stacking.empty()
				|| bonus->valType != BonusValueType::ADDITIVE_VALUE)
				return std::nullopt;
			break;
		case BonusType::FLYING:
			if(!innate->flying || bonus->source != BonusSource::CREATURE_ABILITY
				|| bonus->sid != BonusSourceID(unit.creatureId()) || bonus->duration != BonusDuration::PERMANENT
				|| bonus->effectRange != BonusLimitEffect::NO_LIMIT || bonus->parameters || !bonus->stacking.empty()
				|| bonus->valType != BonusValueType::ADDITIVE_VALUE)
				return std::nullopt;
			break;
		case BonusType::SHOTS: case BonusType::SHOOTER:
			if(!innate->archer || !allowRanged)
				return std::nullopt;
			break;
		case BonusType::ADDITIONAL_ATTACK:
		case BonusType::BLOCKS_RETALIATION:
			if(bonus->source != BonusSource::CREATURE_ABILITY)
				return std::nullopt;
			break;
		default: return std::nullopt;
		}
	}
	if(unit.valOfBonuses(Selector::type()(BonusType::PERCENTAGE_DAMAGE_BOOST)
		.And(Selector::subtype()(BonusSubtypeID(BonusCustomSubtype::damageTypeRanged)))) != archeryPercent)
		return std::nullopt;
	return OrdinaryThreatUnit{static_cast<uint32_t>(unit.getCount()),
		static_cast<uint32_t>(unit.getAvailableHealth()), static_cast<uint32_t>(innate->hitPoints),
		static_cast<uint32_t>(innate->minimumDamage), static_cast<uint32_t>(innate->maximumDamage),
		unit.getAttack(false), unit.getDefense(false), unit.ableToRetaliate(), archeryPercent, innate->noMeleePenalty, damageMode, shield};
}

// Pinned archerDecision compares only adjacent enemies, with one damage
// projection for the entire comparison. Unsupported H3 exchanges stay native.
inline std::optional<std::map<uint32_t, OrdinaryThreatDamage>> ordinaryBlockedArcherDamage(
	const CPlayerBattleCallback & battle, const CStack & active, const std::vector<const CStack *> & enemies)
{
	const auto attacker = ordinaryNativeThreatUnit(active, true);
	if(!attacker || !active.isShooter() || !active.shots.canUse(1)
		|| !active.getPosition().isValid() || !battle.battleIsUnitBlocked(&active)
		|| battle.battleGetDefendedTown() || active.unitOwner() != battle.battleGetOwner(&active))
		return std::nullopt;
	std::map<uint32_t, OrdinaryThreatDamage> result;
	for(const auto * enemy : enemies)
	{
		if(!enemy->getPosition().isValid())
			continue;
		const auto contacts = battle.meleeAttackHexes(&active, enemy, active.getPosition());
		if(contacts.empty())
			continue;
		const auto defender = ordinaryNativeThreatUnit(*enemy, enemy->isShooter());
		if(!defender || enemy->unitOwner() != battle.battleGetOwner(enemy))
			return std::nullopt;
		for(const auto & contact : contacts)
			for(const auto * victim : battle.getAttackedBattleUnits(&active, enemy, contact, false, active.getPosition()))
				if(victim != enemy && victim != &active)
					return std::nullopt;
		const auto damage = ordinaryThreatDamage(*attacker, *defender, true, false);
		// Source converts both uint32 values to int32 before subtracting.
		if(!damage || damage->potential > std::numeric_limits<int32_t>::max()
			|| damage->retaliation > std::numeric_limits<int32_t>::max())
			return std::nullopt;
		result.emplace(enemy->unitId(), *damage);
	}
	return result;
}

inline std::optional<std::map<uint32_t, OrdinaryThreatDamage>> ordinaryPursuitThreats(
	const CPlayerBattleCallback & battle, const CStack & active, const std::vector<const CStack *> & enemies)
{
	// Pinned EstimateRetaliatoryDamage uses innate midpoint damage for all
	// ordinary defenders, including archers; it does not apply melee /2.
	// Keep the same strict fact/effect admission for an active ranged unit.
	const auto defender = ordinaryNativeThreatUnit(active, active.isShooter());
	if(!defender || active.unitOwner() != battle.battleGetOwner(&active)
		|| (active.isShooter() && battle.battleGetDefendedTown()))
		return std::nullopt;
	std::map<uint32_t, OrdinaryThreatDamage> result;
	for(const auto * enemy : enemies)
	{
		const bool innateArcher = enemy->isShooter();
		const bool sourceArcher = innateArcher && enemy->shots.canUse(1);
		const bool blocked = innateArcher && battle.battleIsUnitBlocked(enemy);
		const bool shooting = sourceArcher && !blocked;
		// Source valuation, not native H3 damage: an empty-ammo archer is no
		// longer isArchers() in fheroes2 and therefore has no source /2 penalty.
		// Sieges, special shooting permissions and ranged effects stay native.
		if(innateArcher && battle.battleGetDefendedTown())
			return std::nullopt;
		if(shooting && (active.getDefense(true) != active.getDefense(false)
			|| !battle.battleCanShoot(enemy)
			// Pinned CalculateDamageUnit has no H3 distance penalty. Keep
			// source valuation at long range; native shots still apply their
			// own distance reduction. Castle cover remains uncertified.
			|| battle.battleHasWallPenalty(enemy, enemy->getPosition(), active.getPosition())))
			return std::nullopt;
		const auto attacker = ordinaryNativeThreatUnit(*enemy, innateArcher);
		if(!attacker || enemy->unitOwner() != battle.battleGetOwner(enemy))
			return std::nullopt;
		auto damage = ordinaryThreatDamage(*attacker, *defender, sourceArcher && blocked, shooting);
		if(!damage)
			return std::nullopt;
		if(shooting || enemy->hasBonusOfType(BonusType::BLOCKS_RETALIATION))
			damage->retaliation = 0;
		result.emplace(enemy->unitId(), *damage);
	}
	return result;
}
// Ordinary archerDecision ranks the enemy's threat to the acting archer,
// not outgoing casualty value. One uncertified legal shot rejects the comparison.
inline std::optional<std::map<uint32_t, OrdinaryThreatDamage>> ordinaryRangedTargetThreats(
 const CPlayerBattleCallback & battle, const CStack & active, const std::vector<const CStack *> & enemies)
{
 if(!active.isShooter() || !active.shots.canUse(1) || battle.battleIsUnitBlocked(&active)
  || battle.battleGetDefendedTown() || active.hasBonusOfType(BonusType::SHOOTS_ALL_ADJACENT))
  return std::nullopt;
 std::vector<const CStack *> candidates;
 for(const auto * enemy : enemies)
 {
  if(!enemy->alive() || enemy->isInvincible() || !enemy->getPosition().isValid()
   || !battle.battleCanShoot(&active, enemy->getPosition()))
   continue;
  if(!battle.getAttackedBattleUnits(&active, enemy, enemy->getPosition(), true).empty())
   return std::nullopt;
  candidates.push_back(enemy);
 }
 if(candidates.empty())return std::nullopt;
 return ordinaryPursuitThreats(battle, active, candidates);
}

// optimalAttackValue keeps both units at their current positions for threat
// evaluation; hypothetical contact geometry only controls legal candidates.
inline std::optional<std::map<uint32_t, OrdinaryThreatDamage>> ordinaryMeleeTargetThreats(
 const CPlayerBattleCallback & battle, const CStack & active, const ReachabilityInfo & reachability,
 const BattleHexArray & positions, const std::vector<const CStack *> & enemies)
{
 if(active.isShooter() || battle.battleGetDefendedTown())return std::nullopt;
 std::vector<const CStack *> candidates;
 for(const auto * enemy : enemies)
 {
  if(!enemy->alive() || enemy->isInvincible() || !battle.battleCanAttackUnit(&active,enemy))continue;
  bool included=false;
  for(const auto & from:positions)
  {
   if(!from.isAvailable() || !reachability.isReachable(from))continue;
   for(const auto & aim:battle.meleeAttackHexes(&active,enemy,from))
   {
    included=true;
    for(const auto * victim:battle.getAttackedBattleUnits(&active,enemy,aim,false,from))
     if(victim!=enemy && victim!=&active)return std::nullopt;
   }
  }
  if(included)candidates.push_back(enemy);
 }
 if(candidates.empty())return std::nullopt;
 return ordinaryPursuitThreats(battle,active,candidates);
}

}
