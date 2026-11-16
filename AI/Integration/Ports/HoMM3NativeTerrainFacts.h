// GPL-2.0-or-later. Exact shared standard native-terrain record shape.
#pragma once
#include "../../../lib/battle/CBattleInfoCallback.h"
#include "../../../lib/battle/Unit.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/CCreatureHandler.h"
#include <limits>
namespace AIIntegration::Ports::HoMM3
{
struct NativeTerrainFacts
{
 static bool neutralMetadata(const Bonus & b)
 {
  // This checkout has no live addInfo member. Bonus::convertAddInfo migrates
  // legacy nondefault auxiliary values into parameters, rejected below.
  // Icon/description/hidden are presentation only; owner/target-source fields
  // are conservatively required to have their canonical neutral defaults.
  return !b.propagator && !b.updater && !b.propagationUpdater
   && !b.parameters && b.stacking.empty()
   && b.targetSourceType == BonusSource::OTHER && !b.targetSourceID.hasValue()
   && b.bonusOwner == PlayerColor::CANNOT_DETERMINE;
 }
 static bool canonical(const CBattleInfoCallback & battle,
  const battle::Unit & u, const Bonus & b)
 {
  // Pinned army::initialize applies +1 attack/defense on native terrain.
  // Native BattleInfo builds exactly this two-child limiter. This exception
  // does NOT admit arbitrary conditional bonuses or arbitrary AllOf shapes.
  if((b.type != BonusType::PRIMARY_SKILL && b.type != BonusType::STACKS_SPEED) || b.source != BonusSource::TERRAIN_NATIVE
   || b.sid.hasValue() || b.val != 1 || b.valType != BonusValueType::ADDITIVE_VALUE
   || b.duration != BonusDuration::ONE_BATTLE || b.turnsRemain != 0
   || b.effectRange != BonusLimitEffect::NO_LIMIT || !neutralMetadata(b)
   || !u.unitType()->isNativeTerrain(battle.battleTerrainType())) return false;
  const auto * all = dynamic_cast<const AllOfLimiter *>(b.limiter.get());
  if(!all || all->limiters.size() != 2) return false;
  bool terrain = false, creature = false;
  for(const auto & child : all->limiters)
  {
   if(const auto * t = dynamic_cast<const TerrainLimiter *>(child.get()))
   {
    if(terrain || t->terrainType != TerrainId(ETerrainId::NATIVE_TERRAIN)) return false;
    terrain = true;
   }
   else if(const auto * c = dynamic_cast<const CreatureLevelLimiter *>(child.get()))
   {
    if(creature || c->minLevel != 0 || c->maxLevel != std::numeric_limits<uint32_t>::max()) return false;
    creature = true;
   }
   else return false;
  }
  return terrain && creature;
 }
};
}
