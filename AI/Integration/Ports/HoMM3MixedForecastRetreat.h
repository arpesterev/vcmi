/* GPL-2.0-or-later. Bounded ordinary ground source forecast consumer. */
#pragma once
#include "HoMM3ForecastRetreat.h"
#include "HoMM3Forecast/NativeMixedHistory.h"
#include <random>
namespace AIIntegration::Ports::HoMM3
{
inline std::optional<BattleAction> forecastMixedAnnihilationRetreat(
 const CPlayerBattleCallback & battle,const Environment * environment,const CStack & active,
 const HoMM3Forecast::NativeLifecycle & lifecycle,bool human,bool autocombat,
 bool publicStaticMap,HoMM3Forecast::NativeMixedHistory & history,std::minstd_rand & random)
{
 namespace F=HoMM3Forecast;
 const auto policy=forecastRetreatPolicy(battle,environment,active,human,autocombat);
 if(!policy || !publicStaticMap || !history.boundTo(battle) || !history.ready()
  || !lifecycle.boundTo(battle))return std::nullopt;
 try
 {
  const auto before=lifecycle.snapshot();
  // A conservative bounded draw budget. The source model fails closed if it
  // needs more; speculative draws never touch the live generator.
  constexpr size_t budget=1024;
  std::vector<int> factors;factors.reserve(budget);
  std::vector<std::minstd_rand> states{random};states.reserve(budget+1);
  auto trial=random;std::uniform_int_distribution<int> distribution(75,100);
  for(size_t i=0;i<budget;++i){factors.push_back(distribution(trial));states.push_back(trial);}
  const auto capture=F::captureNativeOrdinaryMixed(battle,lifecycle,*policy,active,
   factors,history.source(),publicStaticMap);
  if(!capture)return std::nullopt;
  auto input=capture->state;
  bool melee=false,own=false,enemy=false;
  for(const auto & unit:input.common.units)
  {
   const auto & order=unit.state.order;
   // Future corpse-facing/rank and incapacitation provenance remain excluded.
   if(unit.state.liveHealth<=0 || order.immobilized || F::OrdinaryTwoPassDetail::cannotAttack(order)
    || unit.state.berserk || order.role!=F::OrderRole::ORDINARY)return std::nullopt;
   melee|=!unit.facts.canShoot;
   if(order.owningSide==F::Side::ATTACKER)own=true;else enemy=true;
  }
  if(!melee || !own || !enemy)return std::nullopt;
  input.common.requestedSide=F::Side::ATTACKER;input.common.checkingSurrender=true;
  const auto prediction=F::simulateOrdinaryGroundTwoPass(input);
  if(!prediction || prediction->passes!=2 || prediction->drawsConsumed>=states.size()
   || !F::NativeOrdinaryTwoPassDetail::sameSnapshot(before,lifecycle.snapshot())
   || battle.battleActiveUnit()!=&active)return std::nullopt;
  // Original pre-AI simulation consumes its random draws even when it elects
  // to keep fighting. Commit only the exact successful model draw count.
  random=states[prediction->drawsConsumed];
  for(const auto & unit:input.common.units)
   if(unit.state.order.owningSide==F::Side::ATTACKER)
   {
    const auto damage=prediction->expectedDamage.find(unit.state.order.id);
    if(damage==prediction->expectedDamage.end())return std::nullopt;
    if(damage->second<unit.state.liveHealth)return boundedEconomicRetreat(battle,environment->game());
   }
  return BattleAction::makeRetreat(BattleSide::ATTACKER);
 }
 catch(const F::UnsupportedSnapshot &){return std::nullopt;}
}
}
