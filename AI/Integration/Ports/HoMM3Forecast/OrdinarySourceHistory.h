/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "InvariantMoveOrder.h"
#include <map>
#include <optional>
#include <span>

namespace HoMM3Forecast
{
// Source scratch and facing, not native turn-queue or GUI animation state.
class OrdinarySourceHistory
{
public:
 enum class CompletedAction { MOVE, MELEE, SHOOT, WAIT, DEFEND };
 struct Entry
 {
  Side side=Side::ATTACKER;
  int32_t sourceIndex=0;
  bool immobilized=false;
  std::optional<int32_t> facing;
  std::optional<int32_t> rank;
 };
private:
 std::map<UnitId,Entry> entries;
 bool initialized=false;
 bool matches(std::span<const OrderUnit> units,bool allowDeaths)const
 {
  if(!initialized||units.size()!=entries.size())return false;
  std::map<UnitId,bool> seen;
  for(const auto&u:units){auto it=entries.find(u.id);
   if(it==entries.end()||!seen.emplace(u.id,true).second||u.owningSide!=it->second.side||u.sourceIndex!=it->second.sourceIndex||u.role!=OrderRole::ORDINARY)return false;
   if(it->second.immobilized&&!u.immobilized)return false;
   if(!allowDeaths&&it->second.immobilized!=u.immobilized)return false;
  }
  return true;
 }
 void forgetLivingRanks(){for(auto&[id,e]:entries){(void)id;if(!e.immobilized)e.rank.reset();}}
public:
 void invalidate(){entries.clear();initialized=false;}
 // Only a verified new battle may certify source initial facing. No rank default.
 bool beginFresh(std::span<const OrderUnit> units)
 {
  invalidate();
  std::map<std::pair<Side,int32_t>,bool> slots;
  for(const auto&u:units){
   if(u.immobilized||u.role!=OrderRole::ORDINARY||(u.owningSide!=Side::ATTACKER&&u.owningSide!=Side::DEFENDER)||u.sourceIndex<0||u.sourceIndex>=21||!slots.emplace(std::make_pair(u.owningSide,u.sourceIndex),true).second||!entries.emplace(u.id,Entry{u.owningSide,u.sourceIndex,false,1-static_cast<int32_t>(u.owningSide),std::nullopt}).second){invalidate();return false;}
  }
  initialized=!entries.empty();return initialized;
 }
 // Invoke only when the port executes a source-equivalent findMoveOrder call.
 // It writes all living ranks, including the other side, and leaves corpses alone.
 bool recordSourceOrder(const MoveOrderInput&input)
 {
  if(!matches(input.units,false)){invalidate();return false;}
  std::optional<ProjectedMoveOrder> projected;
  try{projected=projectInvariantMoveOrder(input);}catch(const std::invalid_argument&){forgetLivingRanks();return false;}
  if(!projected){forgetLivingRanks();return false;}
  for(const auto&u:projected->units)entries.at(u.id).rank=u.expectedMoveOrder;
  return true;
 }
 // Join an unknown no-write/write caller branch. Preserve only equal outcomes;
 // this never manufactures a rank for an initially unknown entry.
 bool possibleSourceOrder(const MoveOrderInput&input)
 {
  if(!matches(input.units,false)){invalidate();return false;}
  std::optional<ProjectedMoveOrder> projected;
  try{projected=projectInvariantMoveOrder(input);}catch(const std::invalid_argument&){forgetLivingRanks();return false;}
  if(!projected){forgetLivingRanks();return false;}
  for(const auto&u:projected->units){auto&e=entries.at(u.id);if(e.rank!=u.expectedMoveOrder)e.rank.reset();}
  return true;
 }
 // Called after a complete supported ordinary action, including its retaliation.
 // Surviving source facings restore; newly dead units retain rank but not facing.
 bool completeOrdinary(CompletedAction action,std::span<const OrderUnit> after)
 {
  switch(action){case CompletedAction::MOVE:case CompletedAction::MELEE:case CompletedAction::SHOOT:case CompletedAction::WAIT:case CompletedAction::DEFEND:break;default:invalidate();return false;}
  if(!matches(after,true)){invalidate();return false;}
  for(const auto&u:after){auto&e=entries.at(u.id);if(u.immobilized)e.facing.reset();e.immobilized=u.immobilized;}
  return true;
 }
 // Unknown intervening source-order calls may rewrite live ranks, not corpses.
 void unknownOrderEvent(){forgetLivingRanks();}
 const Entry * find(UnitId id)const{auto it=entries.find(id);return it==entries.end()?nullptr:&it->second;}
 bool known()const{return initialized;}
};
}
