/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "NativeOrdinaryTwoPass.h"
#include "OrdinaryLiveMeleeChoice.h"
#include "../HoMM3NativeGroundMap.h"
#include "../../../../lib/gameState/InfoAboutArmy.h"
#include "../../../../lib/mapObjects/CGHeroInstance.h"

namespace HoMM3Forecast
{
/// Caller holds the battle thread and certifies uninterrupted public static-map history.
/// Hero spell pressure is known zero only for absent heroes or our own non-casting no-book hero.
inline std::optional<OrdinaryLiveMeleeInput> captureNativeOrdinaryGround(
 const CPlayerBattleCallback & callback, const NativeLifecycle & lifecycle,
 const NativeForecastPolicy & policy, const battle::Unit & active,
 const std::vector<int> & randomFactors, bool publicStaticMapKnown = false)
{
 using Facts = AIIntegration::Ports::HoMM3::NativeExchangeFacts;
 if(!publicStaticMapKnown || !lifecycle.boundTo(callback) || callback.battleActiveUnit()!=&active
  || active.controllingSide()!=callback.battleGetMySide() || policy.difficulty<0 || policy.difficulty>4)
  return std::nullopt;
 try
 {
  const auto before=lifecycle.snapshot();
  const NativeShooterModel effectiveRecords(callback);
  (void)effectiveRecords;
  // Source hero casting requires a spellbook. Host artifact grants may bypass it.
  // Inspect full hero facts only for our own side; an opposing hero stays unknown.
  for(const auto side:{BattleSide::ATTACKER,BattleSide::DEFENDER})
  {
   if(!callback.battleGetHeroInfo(side).details) continue;
   if(side!=callback.battleGetMySide()) return std::nullopt;
   const auto * hero=callback.battleGetFightingHero(side);
   if(!hero || hero->getOwner()!=callback.sideToPlayer(side) || hero->hasSpellbook() || hero->canCastSpells())
    return std::nullopt;
  }
  const auto geometry=AIIntegration::Ports::HoMM3::captureNativeVisibleGroundMap(callback);
  if(!geometry) return std::nullopt;
  OrdinaryLiveMeleeInput input;
  input.activeId=active.unitId();
  auto & result=input.state;
  result.currentSide=sourceSide(active.controllingSide());
  result.common.actingSide=before.actingSide;
  result.common.requestedSide=result.currentSide;
  result.common.secondPhase=before.secondPhase;
  result.common.difficulty=policy.difficulty;
  result.common.human=policy.human;
  result.map=geometry->cells;
  result.randomFactors=randomFactors;
  std::map<UnitId,const battle::Unit *> native;
  std::optional<std::pair<int32_t,int32_t>> minima;
  for(const auto & group:geometry->roster) for(const auto * unit:group)
  {
   if(!unit->alive() || unit->defended() || unit->hasBonusOfType(BonusType::UNIT_DEFENDING)
    || unit->hasBonusOfType(BonusType::SHOOTER) || !Facts::ordinaryMelee(*unit)
    || unit->hasBonusOfType(BonusType::CASTS) || unit->hasBonusOfType(BonusType::SPELLCASTER)
    || unit->hasBonusOfType(BonusType::RANDOM_SPELLCASTER) || unit->hasBonusOfType(BonusType::ADJACENT_SPELLCASTER)
    || !native.emplace(unit->unitId(),unit).second) return std::nullopt;
   const auto modifier=Facts::modifiers(callback,*unit);
   if(!modifier) return std::nullopt;
   if(!minima) minima=modifier;
   else {minima->first=std::min(minima->first,modifier->first);minima->second=std::min(minima->second,modifier->second);}
  }
  if(!minima || native.size()!=before.units.size()) return std::nullopt;
  for(const auto & state:before.units)
  {
   const auto it=native.find(state.order.id);
   if(it==native.end()) return std::nullopt;
   const auto & unit=*it->second;
   const auto facts=Facts::unit(callback,unit,minima->first,minima->second);
   const auto speed=Facts::groundSpeed(callback,unit);
   const auto retaliation=Facts::retaliationAvailable(unit,unit);
   if(!facts || facts->canShoot || !speed || *speed>125 || *speed!=state.order.effectiveSpeed || !retaliation
    || sourceSide(unit.unitSide())!=state.order.owningSide || unit.getAvailableHealth()!=state.liveHealth)
    return std::nullopt;
   const auto value=Ordinary::OrdinaryExchangeDetail::integer(double(state.liveHealth)*facts->lossUnitValue[0]/facts->hitPoints);
   if(!value || *value<0) return std::nullopt;
   result.common.units.push_back({state,*facts,unit.getPosition().toInt(),0,*value});
   result.ground.emplace(state.order.id,OrdinaryGroundUnit{false,false,*retaliation?1:0,0});
   Ordinary::SourceGroundSearchInput search;
   search.map=result.map;search.actor={static_cast<int>(state.order.owningSide),state.order.sourceIndex};
   search.head=unit.getPosition().toInt();search.speed=*speed;
   const auto sourcePath=ordinaryGroundPath(search,127,*speed);
   if(!sourcePath) return std::nullopt;
   const auto reach=callback.getReachability(&unit);
   for(int hex=0;hex<187;++hex)
   {
    if(result.map[hex].occupant || !Ordinary::GroundPathDetail::available(hex)) continue;
    const BattleHex destination(hex);
    if(sourcePath->visited[hex]!=reach.isReachable(destination)
     || (sourcePath->visited[hex] && sourcePath->cost[hex]!=reach.distances.at(hex))) return std::nullopt;
   }
  }
  for(const auto & [actorId,actor]:native) for(const auto & [targetId,target]:native)
  {
   // Source attackChange can query self and same-side rows as well as opponent pairs.
   const auto attack=Facts::meleeAttack(callback,*actor,*target);
   const auto retaliation=Facts::retaliationAvailable(*target,*actor);
   if(!attack || !retaliation) return std::nullopt;
   result.melee.emplace(std::pair{actorId,targetId},*attack);
   result.canRetaliate.emplace(std::pair{actorId,targetId},*retaliation);
  }
  if(!NativeOrdinaryTwoPassDetail::sameSnapshot(before,lifecycle.snapshot()) || callback.battleActiveUnit()!=&active)
   return std::nullopt;
  const auto afterGeometry=AIIntegration::Ports::HoMM3::captureNativeVisibleGroundMap(callback);
  if(!afterGeometry) return std::nullopt;
  for(int hex=0;hex<187;++hex)
   if(afterGeometry->cells[hex].blocked!=result.map[hex].blocked || afterGeometry->cells[hex].occupant!=result.map[hex].occupant)
    return std::nullopt;
  result.ordinaryDomainKnown=true;
  input.noCreatureSpellKnown=true;
  if(!chooseOrdinaryLiveMelee(input)) return std::nullopt;
  return input;
 }
 catch(const UnsupportedSnapshot &) {return std::nullopt;}
}
}
