/* Bounded live source forecast withdrawal. GPL-2.0-or-later.
 * homm3-decomp e12906accf96c2662b1abadbc49ddc111991bf21:
 * ai.cpp::aiCheckRetreat and command.cpp::checkGetAIMove (CC0-1.0).
 */
#pragma once
#include "HoMM3FailedSiege.h"
#include "HoMM3RetreatEconomics.h"
#include "HoMM3Forecast/NativeOrdinaryShooterChoice.h"

namespace AIIntegration::Ports::HoMM3
{
// This consumer uses the source two-pass expected-damage ledger, NOT host
// strength ratios, midpoint estimates, or a combat-to-completion prediction.
inline std::optional<HoMM3Forecast::NativeForecastPolicy> forecastRetreatPolicy(
 const CPlayerBattleCallback & battle,const Environment * environment,const CStack & active,
 bool human,bool autocombat)
{
 const auto * game=environment?environment->game():nullptr;
 if(human || autocombat || !game || battle.battleTacticDist() || battle.battleGetDefendedTown()
  || battle.battleGetMySide()!=BattleSide::ATTACKER || battle.battleActiveUnit()!=&active
  || !active.alive() || active.unitSide()!=BattleSide::ATTACKER
  || active.controllingSide()!=BattleSide::ATTACKER || !battle.battleCanFlee())return std::nullopt;
 const auto * hero=battle.battleGetMyHero();
 const auto * map=game->getMapHeader();
 const auto policy=HoMM3Forecast::captureNativeShooterPolicy(battle,environment);
 if(!hero || !map || !policy || policy->difficulty<2 || policy->difficulty>4
  || hero->getOwner()!=battle.sideToPlayer(BattleSide::ATTACKER) || active.unitOwner()!=hero->getOwner()
  || hero->exp<2000 || hero->hasSpellbook() || hero->canCastSpells()
  || hero->hasArt(ArtifactID(125),true) || battle.battleGetHeroInfo(BattleSide::DEFENDER).details)
  return std::nullopt;
 const auto * player=game->tryGetPlayerState(hero->getOwner());
 if(!player || player->isHuman() || policy->human[0])return std::nullopt;
 bool tavern=false,standard=false;
 for(const auto * town:player->getTowns())
  if(town && town->getOwner()==hero->getOwner() && town->hasBuilt(BuildingID::TAVERN))tavern=true;
 if(!tavern)return std::nullopt;
 for(const auto & event:map->triggeredEvents)if(event.effect.type==EventEffect::VICTORY)
 {
  const auto expression=event.trigger.get();
  const auto * condition=std::get_if<EventCondition>(&expression);
  if(!condition || condition->condition!=EventCondition::STANDARD_WIN)return std::nullopt;
  standard=true;
 }
 if(!standard)return std::nullopt;
 return policy;
}
inline std::optional<BattleAction> forecastAnnihilationRetreat(
 const CPlayerBattleCallback & battle,const Environment * environment,const CStack & active,
 const HoMM3Forecast::NativeLifecycle & lifecycle,bool human,bool autocombat)
{
 const auto policy=forecastRetreatPolicy(battle,environment,active,human,autocombat);
 if(!policy)return std::nullopt;
 const auto facts=HoMM3Forecast::captureNativeOrdinaryFacts(battle,lifecycle,*policy,
  HoMM3Forecast::Side::ATTACKER,true);
 if(!facts)return std::nullopt;
 // Preserve the existing bounded native ordinary facts validation. Do not
 // reinterpret incapacitated troop survivorship or spell-expiry ordering here.
 bool own=false,enemy=false;
 for(const auto & unit:facts->units)
 {
  if(unit.state.liveHealth<=0)continue;
  const auto & order=unit.state.order;
  if(order.immobilized || HoMM3Forecast::OrdinaryTwoPassDetail::cannotAttack(order)
   || unit.state.berserk || order.role!=HoMM3Forecast::OrderRole::ORDINARY)return std::nullopt;
  if(order.owningSide==HoMM3Forecast::Side::ATTACKER)own=true;else enemy=true;
 }
 if(!own || !enemy)return std::nullopt;
 const auto prediction=HoMM3Forecast::simulateOrdinaryTwoPass(*facts);
 if(!prediction || prediction->passes!=2)return std::nullopt;
 for(const auto & unit:facts->units)
  if(unit.state.order.owningSide==HoMM3Forecast::Side::ATTACKER && unit.state.liveHealth>0)
  {
   const auto damage=prediction->expectedDamage.find(unit.state.order.id);
   if(damage==prediction->expectedDamage.end())return std::nullopt;
   if(damage->second<unit.state.liveHealth)return boundedEconomicRetreat(battle,environment->game());
  }
 return BattleAction::makeRetreat(BattleSide::ATTACKER);
}
}
