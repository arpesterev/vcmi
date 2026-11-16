/* GPL-2.0-or-later. Bounded immutable native facts for reconstructed HoMM3
 * e12906accf96c2662b1abadbc49ddc111991bf21 ordinary exchange. No native damage
 * midpoint, enemy hero private state, conditional specialties or proc forecasts.
 */
#pragma once
#include "HoMM3OrdinaryExchange.h"
#include "HoMM3CombatValue.h"
#include "HoMM3NativeTerrainFacts.h"
#include "HoMM3NativeSpellDefenseFacts.h"
#include "HoMM3CurrentDefendWitness.h"
#include "../../../lib/battle/Unit.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/BattleHex.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/gameState/InfoAboutArmy.h"
#include "../../../lib/CCreatureHandler.h"
#include <limits>

namespace AIIntegration::Ports::HoMM3
{
struct NativeExchangeFacts
{
private:
 struct Profile
 {
  int32_t attack[2]{}, defense = 0;
  bool canShoot = false, shooter = false, controller = false;
  float skill[2]{0, 0};
  double reduction[2]{1, 1};
  int bless = 0, curse = 0;
  bool blessed = false, cursed = false;
 };
 static bool plainExceptLimiter(const Bonus & b)
 { return NativeTerrainFacts::neutralMetadata(b); }
 static bool plain(const Bonus & b)
 {
  return !b.limiter && plainExceptLimiter(b);
 }
 static bool canonicalNativeTerrain(const CPlayerBattleCallback & battle,
  const battle::Unit & u, const Bonus & b)
 { return NativeTerrainFacts::canonical(battle,u,b); }
 static bool permanent(const Bonus & b)
 {
  return plain(b) && b.duration == BonusDuration::PERMANENT
   && b.effectRange == BonusLimitEffect::NO_LIMIT;
 }
 static bool creatureBase(const Bonus & b, const battle::Unit & u)
 {
  return permanent(b) && b.source == BonusSource::CREATURE_ABILITY
   && b.sid == BonusSourceID(u.unitType()->getId()) && b.valType == BonusValueType::BASE_NUMBER;
 }
 static bool unsupported(const battle::Unit & u)
 {
  // Explicitly exclude every known extra damage/defense/target/proc mechanism
  // not represented by the scalar ordinary source facts below.
  for(const auto type : {BonusType::IN_FRENZY, BonusType::SLAYER, BonusType::FIRST_STRIKE, BonusType::RANGED_RETALIATION,
   BonusType::ENEMY_DEFENCE_REDUCTION, BonusType::ENEMY_ATTACK_REDUCTION,
   BonusType::HATE, BonusType::HATES_TRAIT, BonusType::JOUSTING,
   BonusType::GENERAL_DAMAGE_PREMY, BonusType::GENERAL_ATTACK_REDUCTION,
   BonusType::DOUBLE_DAMAGE_CHANCE, BonusType::BONUS_DAMAGE_PERCENTAGE,
   BonusType::BONUS_DAMAGE_CHANCE, BonusType::HERO_GRANTS_ATTACKS,
   BonusType::DAMAGE_RECEIVED_CAP, BonusType::ADDITIONAL_ATTACK,
   BonusType::TWO_HEX_ATTACK_BREATH, BonusType::PRISM_HEX_ATTACK_BREATH,
   BonusType::MULTIHEX_UNIT_ATTACK, BonusType::MULTIHEX_ENEMY_ATTACK,
   BonusType::THREE_HEADED_ATTACK, BonusType::ATTACKS_ALL_ADJACENT,
   BonusType::SHOOTS_ALL_ADJACENT, BonusType::SPELL_LIKE_ATTACK,
   BonusType::SPELL_BEFORE_ATTACK, BonusType::SPELL_AFTER_ATTACK,
   BonusType::COMBAT_EVENT_TRIGGER, BonusType::ON_COMBAT_EVENT,
   BonusType::LIMITED_SHOOTING_RANGE, BonusType::FREE_SHOOTING,
   BonusType::NO_DISTANCE_PENALTY, BonusType::NO_WALL_PENALTY,
   BonusType::NO_MELEE_PENALTY, BonusType::FORGETFULL,
   BonusType::NOT_ACTIVE, BonusType::HYPNOTIZED, BonusType::ATTACKS_NEAREST_CREATURE})
   if(u.hasBonusOfType(type)) return true;
  return false;
 }
 static std::optional<Profile> profile(const CPlayerBattleCallback & battle, const battle::Unit & u, const CurrentDefendSnapshot * witness = nullptr)
 {
  if(battle.battleGetDefendedTown() || battle.battleTacticDist() || u.doubleWide()
   || u.isClone() || u.isSummoned() || u.isTurret() || u.isInvincible()
   || u.hasBonusOfType(BonusType::SIEGE_WEAPON)
   || u.controllingSide() != u.unitSide() || unsupported(u)) return std::nullopt;
  const auto defendBonus=witness && witness->matchesContext(battle)?witness->bonus(battle,u):std::nullopt;
  Profile p;
  p.controller = battle.battleGetHeroInfo(u.unitSide()).details.has_value();
  // No guessed lucky attack state: exclude nonzero observable luck and any
  // unmodeled luck record. Source m_luckStatus is not a probability multiplier.
  if(u.luckVal() != 0) return std::nullopt;
  int64_t stats[3]{0,0,0}; // attack melee/ranged, defense
  bool baseAttack = false, baseDefense = false;
  bool terrainAttack = false, terrainDefense = false;
  const auto primaryRecords = u.getAllBonuses(Selector::type()(BonusType::PRIMARY_SKILL));
  for(const auto & ptr : *primaryRecords)
  {
   const auto & b = *ptr;
   const bool attack = b.subtype == BonusSubtypeID(PrimarySkill::ATTACK);
   const bool defense = b.subtype == BonusSubtypeID(PrimarySkill::DEFENSE);
   if(!attack && !defense) continue;
   if(defense && defendBonus && NativeDefendFacts::stanceRecord(b)) continue;
   if(b.source == BonusSource::TERRAIN_NATIVE)
   {
    if(!canonicalNativeTerrain(battle,u,b) || (attack ? terrainAttack : terrainDefense))
     return std::nullopt;
    if(attack) { terrainAttack=true; ++stats[0]; ++stats[1]; }
    else { terrainDefense=true; ++stats[2]; }
    continue;
   }
   if(!plain(b)) return std::nullopt;
   bool melee = true, ranged = true;
   if(creatureBase(b,u))
   {
    if(attack ? baseAttack : baseDefense) return std::nullopt;
    if(attack) baseAttack = true; else baseDefense = true;
   }
   else if(b.source == BonusSource::HERO_BASE_SKILL && permanent(b)
    && b.valType == BonusValueType::ADDITIVE_VALUE && b.val >= 0)
   {
    if(!p.controller) return std::nullopt;
   }
   else if(b.source == BonusSource::SPELL_EFFECT && b.valType == BonusValueType::ADDITIVE_VALUE)
   {
    if(b.sid == BonusSourceID(SpellID(SpellID::DISRUPTING_RAY)) && defense
     && b.duration == BonusDuration::PERMANENT && b.effectRange == BonusLimitEffect::NO_LIMIT
     && (b.val == -3 || b.val == -4 || b.val == -5)) {}
    else if(b.duration == BonusDuration::N_TURNS && b.turnsRemain > 0)
    {
     if(attack && b.sid == BonusSourceID(SpellID(SpellID::BLOODLUST))
      && b.effectRange == BonusLimitEffect::ONLY_MELEE_FIGHT && (b.val == 3 || b.val == 6)) ranged = false;
     else if(attack && b.sid == BonusSourceID(SpellID(SpellID::PRECISION))
      && b.effectRange == BonusLimitEffect::ONLY_DISTANCE_FIGHT && (b.val == 3 || b.val == 6)) melee = false;
     else if(NativeSpellDefenseFacts::stoneSkin(b)) {}
     else return std::nullopt;
    }
    else return std::nullopt;
   }
   else return std::nullopt;
   if(attack) { if(melee) stats[0] += b.val; if(ranged) stats[1] += b.val; }
   else stats[2] += b.val;
  }
  if(defendBonus) stats[2]+=*defendBonus;
  if(!baseAttack || !baseDefense || terrainAttack != terrainDefense) return std::nullopt;
  for(auto value : stats) if(value < 0 || value > INT32_MAX) return std::nullopt;
  if(stats[0] != u.getAttack(false) || stats[1] != u.getAttack(true)
   || stats[2] != u.getDefense(false) || stats[2] != u.getDefense(true)) return std::nullopt;
  p.attack[0]=stats[0]; p.attack[1]=stats[1]; p.defense=stats[2];
  // The native standard base numbers must be unchanged; no hidden percent,
  // specialty, modded damage/HP or extra-ammunition arithmetic is admitted.
  for(const auto type : {BonusType::CREATURE_DAMAGE, BonusType::STACK_HEALTH, BonusType::SHOTS})
  {
   const auto baseRecords = u.getAllBonuses(Selector::type()(type));
   for(const auto & b : *baseRecords)
    if(!creatureBase(*b,u)) return std::nullopt;
  }
  if(u.getMinDamage(false) != u.unitType()->getMinDamage(false)
   || u.getMaxDamage(false) != u.unitType()->getMaxDamage(false)
   || u.getMinDamage(true) != u.unitType()->getMinDamage(true)
   || u.getMaxDamage(true) != u.unitType()->getMaxDamage(true)
   || u.getMinDamage(false) != u.getMinDamage(true) || u.getMaxDamage(false) != u.getMaxDamage(true)
   || u.getMaxHealth() != u.unitType()->getMaxHealth()
   || u.getTotalAttacks(false) != 1 || u.getTotalAttacks(true) != 1) return std::nullopt;
  p.shooter = u.isShooter();
  p.canShoot = u.alive() && battle.battleCanShoot(&u);
  // Native general availability is equivalent only after excluding free
  // shooting, Forgetfulness, custom ranges and tactics above.
  bool seenSkills[2]{false,false};
  const auto skillRecords = u.getAllBonuses(Selector::type()(BonusType::PERCENTAGE_DAMAGE_BOOST));
  for(const auto & ptr : *skillRecords)
  {
   const auto & b = *ptr;
   const int channel = b.subtype == BonusCustomSubtype::damageTypeRanged ? 1
    : b.subtype == BonusCustomSubtype::damageTypeMelee ? 0 : -1;
   if(channel < 0 || seenSkills[channel] || !p.controller || !permanent(b)
    || b.source != BonusSource::SECONDARY_SKILL || b.valType != BonusValueType::BASE_NUMBER
    || b.sid != BonusSourceID(SecondarySkill(channel ? SecondarySkill::ARCHERY : SecondarySkill::OFFENCE))) return std::nullopt;
   const int a = 10, v = channel ? 25 : 20, w = channel ? 50 : 30;
   if(b.val != a && b.val != v && b.val != w) return std::nullopt;
   seenSkills[channel] = true;
   // Literal float tables match the pinned helpers. Do not divide a double
   // native aggregate, which loses source float rounding and provenance.
   p.skill[channel] = b.val == a ? .1f : b.val == v ? (channel ? .25f : .2f) : (channel ? .5f : .3f);
  }
  bool armorer = false;
  const auto reductionRecords = u.getAllBonuses(Selector::type()(BonusType::GENERAL_DAMAGE_REDUCTION));
  for(const auto & ptr : *reductionRecords)
  {
   const auto & b = *ptr;
   if(armorer || !p.controller || !permanent(b) || b.source != BonusSource::SECONDARY_SKILL
    || b.sid != BonusSourceID(SecondarySkill(SecondarySkill::ARMORER))
    || b.subtype != BonusCustomSubtype::damageTypeAll || b.valType != BonusValueType::BASE_NUMBER
    || (b.val != 5 && b.val != 10 && b.val != 15)) return std::nullopt;
   armorer = true;
   const float factor = b.val == 5 ? .05f : b.val == 10 ? .1f : .15f;
   p.reduction[0] = p.reduction[1] = 1.0f - factor;
  }
  for(const auto type : {BonusType::ALWAYS_MINIMUM_DAMAGE, BonusType::ALWAYS_MAXIMUM_DAMAGE})
  {
   const auto records = u.getAllBonuses(Selector::type()(type));
   if(records->empty()) continue;
   if(records->size() != 1) return std::nullopt;
   const auto & b = *records->front();
   const bool bless = type == BonusType::ALWAYS_MAXIMUM_DAMAGE;
   if(!plain(b) || b.source != BonusSource::SPELL_EFFECT
    || b.sid != BonusSourceID(SpellID(bless ? SpellID::BLESS : SpellID::CURSE))
    || b.duration != BonusDuration::N_TURNS || b.turnsRemain <= 0
    || b.valType != BonusValueType::INDEPENDENT_MAX || b.effectRange != BonusLimitEffect::NO_LIMIT
    || b.subtype != BonusCustomSubtype::damageTypeAll || (b.val != 0 && b.val != 1)) return std::nullopt;
   if(bless) {p.blessed=true;p.bless=b.val;} else {p.cursed=true;p.curse=b.val;}
  }
  if(p.blessed && p.cursed) return std::nullopt;
  return p;
 }
public:
 static std::optional<std::pair<int32_t,int32_t>> modifiers(const CPlayerBattleCallback & battle, const battle::Unit & u, const CurrentDefendSnapshot * witness = nullptr)
 {
  const auto p = profile(battle,u,witness);
  if(!p) return std::nullopt;
  const int64_t a = int64_t(p->attack[p->canShoot ? 1 : 0]) - u.unitType()->getAttack(p->canShoot);
  const int64_t d = int64_t(p->defense) - u.unitType()->getDefense(false);
  if(a < INT32_MIN || a > INT32_MAX || d < INT32_MIN || d > INT32_MAX) return std::nullopt;
  return std::pair<int32_t,int32_t>{a,d};
 }
 static std::optional<SourceOrdinaryUnit> unit(const CPlayerBattleCallback & battle, const battle::Unit & u,
  int32_t lowestAttackModifier, int32_t lowestDefenseModifier, const CurrentDefendSnapshot * witness = nullptr)
 {
  // Removed original slots remain zero-loss census identities, never disappear
  // or renumber peers. No live combat value or fictitious shooting is assigned.
  if(!u.alive())
  {
   if(u.getAvailableHealth() != 0 || u.getCount() != 0) return std::nullopt;
   SourceOrdinaryUnit dead;
   dead.count = 0; dead.lossUnitValue = {0,0};
   return dead;
  }
  const auto p = profile(battle,u,witness);
  if(!p) return std::nullopt;
  SourceOrdinaryUnit result;
  if(u.getCount() > INT32_MAX || u.getMaxHealth() > INT32_MAX) return std::nullopt;
  result.count = u.getCount(); result.hitPoints = u.getMaxHealth();
  const int64_t total = int64_t(result.count)*result.hitPoints;
  const int64_t wound = total-u.getAvailableHealth();
  if(total > INT32_MAX || wound < 0 || wound >= result.hitPoints) return std::nullopt;
  result.topWound = wound;
  result.minimumDamage = u.getMinDamage(false); result.maximumDamage = u.getMaxDamage(false);
  result.canShoot = p->canShoot;
  result.blessed=p->blessed; result.cursed=p->cursed; result.blessAmount=p->bless; result.curseAmount=p->curse;
  // Validate signed32 damage sums BEFORE computing Bless/Curse averages.
  if(!OrdinaryExchangeDetail::health(result)) return std::nullopt;
  for(int channel=0;channel<2;++channel)
  {
   const int64_t attack = int64_t(p->attack[channel])-u.unitType()->getAttack(channel)-lowestAttackModifier;
   const int64_t defense = int64_t(p->defense)-u.unitType()->getDefense(false)-lowestDefenseModifier;
   if(attack < INT32_MIN || attack > INT32_MAX || defense < INT32_MIN || defense > INT32_MAX
    || attack*.05+1 < 0 || defense*.05+1 < 0) return std::nullopt;
   UnitCombatValueFacts facts;
   facts.attackDifference=attack; facts.defenseDifference=defense;
   facts.ranged=channel; facts.canShoot=p->canShoot; facts.shooter=p->shooter;
   // Reduction uses INCOMING channel even when the victim cannot shoot.
   facts.defenderDamageFactor=p->reduction[channel];
   facts.baseFightValue=u.unitType()->getFightValue();
   facts.baseAverageDamage=(double(result.minimumDamage)+result.maximumDamage)/2;
   facts.alteredDamage=p->blessed||p->cursed;
   facts.averageDamage=p->blessed ? double(result.maximumDamage)+p->bless
    : p->cursed ? std::max<int64_t>(1,int64_t(result.minimumDamage)-p->curse) : facts.baseAverageDamage;
   if(facts.baseFightValue < 0 || facts.baseAverageDamage <= 0) return std::nullopt;
   const double value=unitCombatValue(facts);
   if(!std::isfinite(value)||value<0||value>INT32_MAX) return std::nullopt;
   result.lossUnitValue[channel]=value;
  }
  if(!OrdinaryExchangeDetail::health(result)) return std::nullopt;
  return result;
 }
 static std::optional<int32_t> groundSpeed(const CPlayerBattleCallback & battle, const battle::Unit & u)
 {
  int64_t speed=0;
  bool base=false,terrain=false;
  const auto records=u.getAllBonuses(Selector::type()(BonusType::STACKS_SPEED));
  for(const auto & ptr:*records)
  {
   const auto & b=*ptr;
   if(creatureBase(b,u) && !base && b.subtype==BonusSubtypeID()) base=true;
   else if(canonicalNativeTerrain(battle,u,b) && !terrain && b.subtype==BonusSubtypeID()) terrain=true;
   else return std::nullopt;
   speed+=b.val;
  }
  if(!base || speed<0 || speed>INT32_MAX || speed!=u.getInitiative(0)
   || speed!=u.getMovementRange()) return std::nullopt;
  return static_cast<int32_t>(speed);
 }
 static bool ordinaryMelee(const battle::Unit & u)
 {
  for(const auto type : {BonusType::FLYING, BonusType::BLOCKS_RETALIATION,
   BonusType::NO_RETALIATION, BonusType::UNLIMITED_RETALIATIONS,
   BonusType::ADDITIONAL_RETALIATION, BonusType::RANGED_RETALIATION,
   BonusType::RETURN_AFTER_STRIKE, BonusType::DISINTEGRATE, BonusType::FIRST_STRIKE,
   BonusType::FEROCITY, BonusType::LONG_WEAPON})
   if(u.hasBonusOfType(type)) return false;
  return !u.isGhost() && !u.doubleWide();
 }
 static std::optional<SourceOrdinaryAttack> meleeAttack(const CPlayerBattleCallback & battle,
  const battle::Unit & attacker, const battle::Unit & defender, const CurrentDefendSnapshot * witness = nullptr)
 {
  if(!ordinaryMelee(attacker) || !ordinaryMelee(defender)) return std::nullopt;
  const auto a=profile(battle,attacker,witness);
  if(!a) return std::nullopt;
  if(!defender.alive()) return SourceOrdinaryAttack{};
  const auto d=profile(battle,defender,witness);
  if(!d) return std::nullopt;
  SourceOrdinaryAttack result;
  result.attack=a->attack[0]; result.defense=d->defense;
  result.hasController=a->controller; result.controllerSkillFactor=a->skill[0];
  result.defenderReduction=d->reduction[0]; result.meleeShooterPenalty=a->shooter;
  return result;
 }
 static std::optional<bool> retaliationAvailable(const battle::Unit & attacker,
  const battle::Unit & defender)
 {
  if(!ordinaryMelee(attacker) || !ordinaryMelee(defender)) return std::nullopt;
  if(!defender.alive()) return false;
  const auto * state=dynamic_cast<const battle::CUnitState *>(&defender);
  if(!state || !state->counterAttacks.isLimited() || state->counterAttacks.total()!=1)
   return std::nullopt;
  const auto remaining=state->counterAttacks.available();
  if(remaining<0 || remaining>1 || defender.ableToRetaliate()!=(remaining>0)) return std::nullopt;
  return remaining>0;
 }
 static std::optional<SourceOrdinaryAttack> rangedAttack(const CPlayerBattleCallback & battle,
  const battle::Unit & attacker, const battle::Unit & defender, const CurrentDefendSnapshot * witness = nullptr)
 {
  const auto a=profile(battle,attacker,witness);
  if(!a || !a->canShoot) return std::nullopt;
  // A retained dead original slot has zero source loss. Supply a neutral finite
  // profile only to preserve its explicit zero candidate, not to target a corpse.
  if(!defender.alive()) return SourceOrdinaryAttack{};
  const auto d=profile(battle,defender,witness);
  if(!d || !attacker.getPosition().isAvailable() || !defender.getPosition().isAvailable()
   || !battle.battleCanShoot(&attacker, defender.getPosition())) return std::nullopt;
  SourceOrdinaryAttack result;
  result.attack=a->attack[1]; result.defense=d->defense;
  result.hasController=a->controller; result.controllerSkillFactor=a->skill[1];
  result.defenderReduction=d->reduction[1];
  // Proven against pinned combatManager::getDistance for all 27,225 available
  // hex pairs. Artifact/creature no-distance exceptions are excluded above.
  result.distancePenalty=BattleHex::getDistance(attacker.getPosition(),defender.getPosition())>10;
  return result;
 }
};
}
