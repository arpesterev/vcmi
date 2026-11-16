/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "NativeMixedHistory.h"
#include "NativeLiveMeleeDecision.h"

namespace HoMM3Forecast
{
inline std::optional<NativeLiveMeleeDecision> chooseNativeOrdinaryLiveMixed(
 const CPlayerBattleCallback & callback,const NativeLifecycle & lifecycle,
 const NativeForecastPolicy & policy,const battle::Unit & active,
 bool publicStaticMapKnown,NativeMixedHistory & history,std::minstd_rand & random,
 const AIIntegration::Ports::HoMM3::CurrentDefendSnapshot * witness = nullptr)
{
 if(!publicStaticMapKnown||!history.boundTo(callback)||!history.ready()||!lifecycle.boundTo(callback)
  ||callback.battleActiveUnit()!=&active)return std::nullopt;
 try
 {
  const auto before=lifecycle.snapshot();
  std::vector<std::minstd_rand> states{random};std::vector<int> factors;auto trial=random;
  std::uniform_int_distribution<int> distribution(75,100);
  for(int i=0;i<21;++i){factors.push_back(distribution(trial));states.push_back(trial);}
  const auto captured=captureNativeOrdinaryMixed(callback,lifecycle,policy,active,factors,history.source(),publicStaticMapKnown,witness);
  if(!captured)return std::nullopt;
  const auto selected=chooseOrdinaryLiveMixed(*captured);if(!selected)return std::nullopt;
  std::optional<BattleAction> action;
  if(selected->choice.order==OrdinaryGroundOrder::SHOOT)
  {
   const auto * target=selected->choice.target?callback.battleGetUnitByID(*selected->choice.target):nullptr;
   if(selected->choice.actor!=active.unitId()||!active.alive()||!active.canMove()||active.controllingSide()!=callback.battleGetMySide()
    ||!target||!target->alive()||target->isInvincible()||target->controllingSide()==active.controllingSide()
    ||!callback.battleCanShoot(&active,target->getPosition()))return std::nullopt;
   action=BattleAction::makeShotAttack(&active,target);
  }
  else action=NativeLiveMeleeDetail::legalAction(callback,active,selected->choice);
  if(!action||!NativeOrdinaryTwoPassDetail::sameSnapshot(before,lifecycle.snapshot())||callback.battleActiveUnit()!=&active)return std::nullopt;
  auto candidate=history.source();
  const auto actor=std::find_if(captured->state.common.units.begin(),captured->state.common.units.end(),[&](const auto & unit){return unit.state.order.id==active.unitId();});
  if(actor==captured->state.common.units.end())return std::nullopt;
  if(!actor->facts.canShoot)
  {
   MoveOrderInput order;order.actingSide=captured->state.common.actingSide;order.secondPhase=captured->state.common.secondPhase;
   for(const auto & unit:captured->state.common.units)order.units.push_back(unit.state.order);
   if(!candidate.recordSourceOrder(order))return std::nullopt;
  }
  if(selected->drawsConsumed>=states.size())throw std::logic_error("Ordinary mixed exceeded its source draw budget");
  if(!history.stage(candidate,*action))return std::nullopt;
  random=states.at(selected->drawsConsumed);
  return NativeLiveMeleeDecision{*action,selected->choice,selected->drawsConsumed};
 }
 catch(const UnsupportedSnapshot &){return std::nullopt;}
}
}
