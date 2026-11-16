/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "NativeOrdinaryGround.h"
#include "../../../../lib/battle/BattleAction.h"
#include <random>

namespace HoMM3Forecast
{
struct NativeLiveMeleeDecision
{
 BattleAction action;
 OrdinaryGroundAction sourceChoice;
 size_t drawsConsumed=0;
};
namespace NativeLiveMeleeDetail
{
inline std::optional<BattleAction> legalAction(const CPlayerBattleCallback & callback,
 const battle::Unit & active,const OrdinaryGroundAction & choice)
{
 if(callback.battleActiveUnit()!=&active || choice.actor!=active.unitId()
  || !active.alive() || !active.canMove() || active.controllingSide()!=callback.battleGetMySide())
  return std::nullopt;
 switch(choice.order)
 {
 case OrdinaryGroundOrder::DEFEND: return BattleAction::makeDefend(&active);
 case OrdinaryGroundOrder::WAIT:
  if(active.waited()) return std::nullopt;
  return BattleAction::makeWait(&active);
 case OrdinaryGroundOrder::MOVE:
 case OrdinaryGroundOrder::MELEE:
  break;
 default: return std::nullopt;
 }
 const auto reach=callback.getReachability(&active);
 const auto available=callback.battleGetAvailableHexes(reach,&active,false);
 if(!Ordinary::GroundPathDetail::available(choice.destination)) return std::nullopt;
 const BattleHex destination(choice.destination);
 if(choice.order==OrdinaryGroundOrder::MOVE)
 {
  if(choice.target || destination==active.getPosition() || !available.contains(destination)
   || !reach.isReachable(destination) || callback.battleGetUnitByPos(destination,false)) return std::nullopt;
  return BattleAction::makeMove(&active,destination);
 }
 if(!choice.target || !Ordinary::GroundPathDetail::available(choice.attackHex)) return std::nullopt;
 const auto * target=callback.battleGetUnitByID(*choice.target);
 const BattleHex from(choice.attackHex);
 if(!target || !target->alive() || target->controllingSide()==active.controllingSide()
  || target->getPosition()!=destination || callback.battleGetUnitByPos(destination,false)!=target
  || !callback.battleCanAttackUnit(&active,target) || !available.contains(from)
  || !reach.isReachable(from) || BattleHex::mutualPosition(from,destination)==BattleHex::NONE
  || !callback.battleCanAttackFromHex(available,&active,destination,from)) return std::nullopt;
 return BattleAction::makeMeleeAttack(&active,target,from);
}
}

/// Source choice for this actual activation; never consumes a forecast prefix.
/// Trial RNG state commits only after snapshot and final native legality checks.
inline std::optional<NativeLiveMeleeDecision> chooseNativeOrdinaryLiveMelee(
 const CPlayerBattleCallback & callback,const NativeLifecycle & lifecycle,
 const NativeForecastPolicy & policy,const battle::Unit & active,
 bool publicStaticMapKnown,std::minstd_rand & random)
{
 if(!publicStaticMapKnown || !lifecycle.boundTo(callback) || callback.battleActiveUnit()!=&active)
  return std::nullopt;
 try
 {
  const auto before=lifecycle.snapshot();
  std::vector<std::minstd_rand> states{random};
  std::vector<int> factors;
  auto trial=random;
  std::uniform_int_distribution<int> distribution(75,100);
  // One source draw per enemy candidate; the ordinary source roster has 21 slots.
  for(int i=0;i<21;++i){factors.push_back(distribution(trial));states.push_back(trial);}
  const auto captured=captureNativeOrdinaryGround(callback,lifecycle,policy,active,factors,publicStaticMapKnown);
  if(!captured) return std::nullopt;
  const auto selected=chooseOrdinaryLiveMelee(*captured);
  if(!selected) return std::nullopt;
  const auto action=NativeLiveMeleeDetail::legalAction(callback,active,selected->choice);
  if(!action || !NativeOrdinaryTwoPassDetail::sameSnapshot(before,lifecycle.snapshot())
   || callback.battleActiveUnit()!=&active) return std::nullopt;
  if(selected->drawsConsumed>=states.size()) throw std::logic_error("Ordinary melee exceeded its source draw budget");
  random=states.at(selected->drawsConsumed);
  return NativeLiveMeleeDecision{*action,selected->choice,selected->drawsConsumed};
 }
 catch(const UnsupportedSnapshot &) {return std::nullopt;}
}
}
