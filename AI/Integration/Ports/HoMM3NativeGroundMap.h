/* GPL-2.0-or-later. Visible native facts only; this is NOT a completeness certificate. */
#pragma once
#include "HoMM3GroundPath.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/battle/Unit.h"
#include <algorithm>

namespace AIIntegration::Ports::HoMM3
{
struct NativeVisibleGroundMap
{
 SourceGroundMap cells{};
 std::array<std::vector<const battle::Unit *>,2> roster;
};
// Default player perspective only. A caller must separately prove uninterrupted
// public static-map history before supplying these cells to source ground search.
inline std::optional<NativeVisibleGroundMap> captureNativeVisibleGroundMap(const CPlayerBattleCallback & battle)
{
 if(battle.battleGetDefendedTown() || battle.battleTacticDist()) return std::nullopt;
 const auto perspective=battle.battleGetMySide();
 if(perspective!=BattleSide::ATTACKER && perspective!=BattleSide::DEFENDER) return std::nullopt;
 NativeVisibleGroundMap result;
 std::array<bool,187> obstacles{};
 for(const auto & obstacle:battle.battleGetAllObstacles())
 {
  if((obstacle->obstacleType!=CObstacleInstance::USUAL && obstacle->obstacleType!=CObstacleInstance::ABSOLUTE_OBSTACLE)
   || obstacle->stopsMovement() || obstacle->triggersEffects()) return std::nullopt;
  for(const auto hex:obstacle->getBlockedTiles())
  {
   if(!hex.isValid()) return std::nullopt;
   obstacles[hex.toInt()]=true;
  }
 }
 auto all=battle.battleGetUnitsIf([](const battle::Unit *) { return true; });
 std::sort(all.begin(),all.end(),[](const auto *a,const auto *b)
 {
  return a->unitSide()!=b->unitSide()?a->unitSide()<b->unitSide():a->unitSlot()<b->unitSlot();
 });
 for(const auto * u:all)
 {
  if((u->unitSide()!=BattleSide::ATTACKER && u->unitSide()!=BattleSide::DEFENDER)
   || !u->unitSlot().validSlot() || u->controllingSide()!=u->unitSide()
   || u->doubleWide() || u->isGhost() || u->isClone() || u->isSummoned() || u->isTurret()
   || u->hasBonusOfType(BonusType::SIEGE_WEAPON) || !u->getPosition().isAvailable()) return std::nullopt;
  const int side=u->unitSide()==BattleSide::ATTACKER?0:1;
  auto & roster=result.roster[side];
  if(roster.size()>=31 || (!roster.empty() && roster.back()->unitSlot()==u->unitSlot())) return std::nullopt;
  const int index=static_cast<int>(roster.size());roster.push_back(u);
  if(u->alive())
  {
   auto & cell=result.cells[u->getPosition().toInt()];
   if(cell.occupant) return std::nullopt;
   cell.occupant=SourceGroundIdentity{side,index};
  }
 }
 const auto accessibility=battle.getAccessibility();
 for(int h=0;h<187;++h)
 {
  auto & cell=result.cells[h];
  switch(accessibility[h])
  {
  case EAccessibility::SIDE_COLUMN:
   if(GroundPathDetail::available(h) || cell.occupant) return std::nullopt;
   break;
  case EAccessibility::ALIVE_STACK:
   if(!cell.occupant || obstacles[h]) return std::nullopt;
   break;
  case EAccessibility::ACCESSIBLE:
   if(cell.occupant || obstacles[h]) return std::nullopt;
   break;
  case EAccessibility::OBSTACLE:
   if(!obstacles[h] || cell.occupant) return std::nullopt;
   cell.blocked=true;
   break;
  case EAccessibility::UNAVAILABLE:
   if(cell.occupant) return std::nullopt;
   cell.blocked=true;
   break;
  default: return std::nullopt;
  }
 }
 return result;
}
}
