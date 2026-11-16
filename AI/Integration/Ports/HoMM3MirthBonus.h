/* HoMM3 Mirth preview boundary. GPL-2.0-or-later. */
#pragma once
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/bonuses/Propagators.h"
#include <tuple>

namespace AIIntegration::Ports::HoMM3
{
inline bool sameMirthLimiter(const TLimiterPtr & a,const TLimiterPtr & b)
{
 if(!a || !b)return !a && !b;
 if(typeid(*a)!=typeid(*b))return false;
 if(const auto * terrain=dynamic_cast<const TerrainLimiter *>(a.get()))
  return terrain->terrainType==static_cast<const TerrainLimiter &>(*b).terrainType;
 if(const auto * aggregate=dynamic_cast<const AggregateLimiter *>(a.get()))
 {
  const auto & other=static_cast<const AggregateLimiter &>(*b);
  if(aggregate->limiters.size()!=other.limiters.size())return false;
  for(size_t i=0;i<aggregate->limiters.size();++i)
   if(!sameMirthLimiter(aggregate->limiters[i],other.limiters[i]))return false;
  return true;
 }
 return a->toJsonNode()==b->toJsonNode();
}
inline bool sameMirthBaselineBonus(const Bonus & a,const Bonus & b)
{
 // Native subtype IDs include sentinels which are not serializable config IDs.
 const auto scalar=[](const Bonus & v)
 {
  return std::tie(v.duration,v.val,v.turnsRemain,v.valType,v.source,v.targetSourceType,
   v.targetSourceID,v.effectRange,v.type,v.subtype,v.sid,v.stacking,v.bonusOwner);
 };
 if(scalar(a)!=scalar(b))return false;
 const auto component=[](const auto & x,const auto & y)
 {
  if(!x || !y)return !x && !y;
  return typeid(*x)==typeid(*y) && x->toJsonNode()==y->toJsonNode();
 };
 try
 {
  if(!sameMirthLimiter(a.limiter,b.limiter)
   || !component(a.updater,b.updater) || !component(a.propagationUpdater,b.propagationUpdater)
   || !component(a.parameters,b.parameters))return false;
  if(!a.propagator || !b.propagator)return !a.propagator && !b.propagator;
  return typeid(*a.propagator)==typeid(*b.propagator)
   && a.propagator->getPropagatorType()==b.propagator->getPropagatorType();
 }
 catch(const std::exception &)
 {
  // An unmapped component must not turn a declined preview into a game crash.
  return false;
 }
}
}
