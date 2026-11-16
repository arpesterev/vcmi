// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "HoMM3DefendProvenance.h"
#include "HoMM3NativeTerrainFacts.h"
#include "HoMM3NativeSpellDefenseFacts.h"
#include "../../../lib/battle/Unit.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/CCreatureHandler.h"

namespace AIIntegration::Ports::HoMM3
{
/// Leaf native metadata extractor, NOT a full no-proc domain certificate.
/// Unchanged creature + public nonnegative hero defense; contextual overload
/// also admits exact canonical native terrain. One pre-existing canonical Stone Skin
/// record is admitted; other spell modifiers remain unavailable.
struct NativeDefendFacts
{
 static bool neutral(const Bonus & b)
 {
  return !b.limiter && !b.propagator && !b.updater && !b.propagationUpdater
   && !b.parameters && b.stacking.empty() && b.targetSourceType == BonusSource::OTHER
   && !b.targetSourceID.hasValue() && b.bonusOwner == PlayerColor::CANNOT_DETERMINE
   && b.effectRange == BonusLimitEffect::NO_LIMIT && b.turnsRemain == 0;
 }
 static bool stanceRecord(const Bonus & b)
 {
  return neutral(b) && b.source == BonusSource::OTHER && !b.sid.hasValue()
   && b.duration == BonusDuration::STACK_GETS_TURN;
 }
 static std::optional<DefendUnitFacts> extract(const battle::Unit & u,uint64_t generation)
 { return extractImpl(nullptr,u,generation); }
 static std::optional<DefendUnitFacts> extract(const CBattleInfoCallback & battle,const battle::Unit & u,uint64_t generation)
 { return extractImpl(&battle,u,generation); }
private:
 static std::optional<DefendUnitFacts> extractImpl(const CBattleInfoCallback * context,const battle::Unit & u, uint64_t observedGeneration)
 {
  if(!u.alive() || u.isClone() || u.isSummoned() || u.isTurret()
   || u.hasBonusOfType(BonusType::DEFENSIVE_STANCE)) return std::nullopt;
  DefendUnitFacts f;
  f.id=u.unitId(); f.identity=observedGeneration; f.alive=true;
  int base=0, hero=0, zero=0, percent=0, weak=0, tag=0, stoneSkin=0;
  int64_t defense=0;
  bool terrainAttack=false,terrainDefense=false;
  std::vector<std::vector<int64_t>> dependencies;
  const auto bonuses=u.getAllBonuses(Selector::all);
  for(const auto & ptr : *bonuses)
  {
   const auto & b=*ptr;
   if(b.type == BonusType::UNIT_DEFENDING)
   {
    if(!stanceRecord(b) || b.subtype != BonusSubtypeID()
     || b.val != 0 || b.valType != BonusValueType::ADDITIVE_VALUE || ++tag != 1)
     return std::nullopt;
    continue;
   }
   if(b.type==BonusType::PRIMARY_SKILL && b.source==BonusSource::TERRAIN_NATIVE
    && (b.subtype==BonusSubtypeID(PrimarySkill::ATTACK) || b.subtype==BonusSubtypeID(PrimarySkill::DEFENSE)))
   {
    const bool attack=b.subtype==BonusSubtypeID(PrimarySkill::ATTACK);
    if(!context || !NativeTerrainFacts::canonical(*context,u,b) || (attack?terrainAttack:terrainDefense)) return std::nullopt;
    if(attack) terrainAttack=true;
    else
    {
     terrainDefense=true;defense+=b.val;
     dependencies.push_back({2,context->battleTerrainType().getNum(),b.val});
    }
    continue;
   }
   if(b.type != BonusType::PRIMARY_SKILL || b.subtype != BonusSubtypeID(PrimarySkill::DEFENSE)) continue;
   if(stanceRecord(b))
   {
    if(b.valType==BonusValueType::PERCENT_TO_ALL && b.val==20) ++percent;
    else if(b.valType==BonusValueType::ADDITIVE_VALUE && b.val==0) ++zero;
    else if(b.valType==BonusValueType::ADDITIVE_VALUE && b.val==1) ++weak;
    else return std::nullopt;
    continue;
   }
   if(NativeSpellDefenseFacts::stoneSkin(b))
   {
    if(++stoneSkin!=1) return std::nullopt;
    defense+=b.val;
    // Native source ID identifies the spell, NOT its caster. Fixed-width tags
    // retain duration even when an equal-valued refreshed record looks alike.
    dependencies.push_back({3,b.sid.as<SpellID>().getNum(),b.val});
    dependencies.push_back({4,static_cast<int64_t>(b.duration),b.turnsRemain});
    continue;
   }
   if(!neutral(b) || b.duration != BonusDuration::PERMANENT) return std::nullopt;
   if(b.source==BonusSource::CREATURE_ABILITY && b.sid==BonusSourceID(u.unitType()->getId())
    && b.valType==BonusValueType::BASE_NUMBER && b.val==u.unitType()->getDefense(false))
   {
    if(++base != 1) return std::nullopt;
    dependencies.push_back({0,u.unitType()->getId().getNum(),b.val});
   }
   else if(b.source==BonusSource::HERO_BASE_SKILL && b.sid.as<ObjectInstanceID>().hasValue()
    && b.valType==BonusValueType::ADDITIVE_VALUE && b.val>=0)
    {
    if(++hero != 1) return std::nullopt;
    dependencies.push_back({1,b.sid.as<ObjectInstanceID>().getNum(),b.val});
   }
   else return std::nullopt;
   defense+=b.val;
   if(defense<0 || defense>std::numeric_limits<int32_t>::max()/20) return std::nullopt;
  }
  if(base!=1 || terrainAttack!=terrainDefense || defense<0
   || defense>std::numeric_limits<int32_t>::max()/20) return std::nullopt;
  if(tag==0 && zero==0 && percent==0 && weak==0) f.stance=DefendUnitFacts::Stance::Absent;
  else if(tag==1 && zero==1 && percent==1 && weak==0 && defense>=5)
   f.stance=DefendUnitFacts::Stance::CanonicalPercent20ZeroExtra;
  else if(tag==1 && zero==1 && percent==0 && weak==1 && defense<5)
   f.stance=DefendUnitFacts::Stance::CanonicalAddOneZeroExtra;
  else return std::nullopt;
  const int64_t adjusted=defense+(tag ? std::max<int64_t>(defense/5,1) : 0);
  if(u.getDefense(false)!=adjusted || u.getDefense(true)!=adjusted) return std::nullopt;
  std::sort(dependencies.begin(),dependencies.end());
  f.defenseProvenance={u.unitType()->getId().getNum(),static_cast<int64_t>(u.unitSide()),u.unitSlot().getNum()};
  for(const auto & record:dependencies) f.defenseProvenance.insert(f.defenseProvenance.end(),record.begin(),record.end());
  f.underlyingDefense=defense; f.displayedDefense=adjusted; f.canonical=true;
  return f;
 }
};
}
