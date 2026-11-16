/* Reconstructed source e12906accf96c2662b1abadbc49ddc111991bf21, CC0-1.0.
 * Ordinary shooter simulateCombat; no native admission or withdrawal consumer. */
#pragma once
#include "OwnershipPrefix.h"
#include "../HoMM3RangedCensus.h"
#include <map>

namespace HoMM3Forecast
{
namespace Ordinary = AIIntegration::Ports::HoMM3;
struct OrdinaryForecastUnit
{
 ForecastUnit state;
 Ordinary::SourceOrdinaryUnit facts;
 int head = -1;
 int32_t initialExpectedDamage = 0;
 // Exact source getTotalCombatValue at the common live-field minima.
 int32_t liveCombatValue = 0;
};
struct OrdinaryTwoPassInput
{
 std::vector<OrdinaryForecastUnit> units;
 Side actingSide=Side::ATTACKER, requestedSide=Side::ATTACKER;
 bool secondPhase=false, checkingSurrender=false;
 int difficulty=0;
 std::array<bool,2> human{};
 std::map<std::pair<UnitId,UnitId>,Ordinary::SourceOrdinaryAttack> attacks;
 std::array<int32_t,4> actionScratch{};
};
struct OrdinaryRankedTarget
{
 UnitId id;
 int32_t value;
};
struct OrdinaryForecastAction
{
 UnitId actor;
 std::optional<UnitId> target;
 bool killsOnly=false, retriedWithoutKillsOnly=false;
 std::vector<OrdinaryRankedTarget> initialCandidates, candidates;
 std::map<UnitId,int32_t> expectedDamageAfter;
 int32_t damage=0;
};
struct OrdinaryTwoPassResult
{
 std::vector<UnitId> order;
 std::array<size_t,2> stoppedAt{};
 size_t passes=0;
 std::map<UnitId,int32_t> expectedDamage;
 std::vector<OrdinaryForecastAction> actions;
 std::array<int32_t,4> actionScratch{};
};
namespace OrdinaryTwoPassDetail
{
inline bool incapacitated(const OrderUnit & u)
{ return u.sourceSpell62Turns || u.sourceSpell70Turns || u.sourceSpell74Turns; }
inline bool cannotAttack(const OrderUnit & u)
{ return incapacitated(u) || u.immobilized || u.role==OrderRole::FIRST_AID_TENT || u.role==OrderRole::AMMUNITION_CART; }
inline int index(Side s) { return s==Side::ATTACKER?0:1; }
inline bool validSide(Side s) { return s==Side::ATTACKER || s==Side::DEFENDER; }
}
inline std::optional<OrdinaryTwoPassResult> simulateOrdinaryTwoPass(const OrdinaryTwoPassInput & in)
{
 using namespace OrdinaryTwoPassDetail;
 if(!validSide(in.actingSide)||!validSide(in.requestedSide)||in.difficulty<0||in.difficulty>4) return std::nullopt;
 std::map<UnitId,const OrdinaryForecastUnit *> units;
 std::array<std::vector<const OrdinaryForecastUnit *>,2> roster;
 std::array<std::optional<UnitId>,187> occupancy{};
 std::array<int64_t,2> total{},awake{};
 OrdinaryTwoPassResult out;out.actionScratch=in.actionScratch;
 MoveOrderInput move;move.actingSide=in.actingSide;move.secondPhase=in.secondPhase;
 for(const auto & u:in.units)
 {
  const auto & s=u.state;const auto & o=s.order;
  const auto hp=Ordinary::OrdinaryExchangeDetail::health(u.facts);
  if(!validSide(o.owningSide)||s.controllingSide!=o.owningSide||!hp||s.liveHealth!=*hp
   ||u.initialExpectedDamage<0||u.initialExpectedDamage>*hp||u.liveCombatValue<0||(*hp==0&&u.liveCombatValue!=0)
   ||o.sourceSpell62Turns<0||o.sourceSpell70Turns<0||o.sourceSpell74Turns<0
   ||u.head<0||u.head>=187||u.head%17==0||u.head%17==16||!units.emplace(o.id,&u).second) return std::nullopt;
  const auto totalValue=Ordinary::OrdinaryExchangeDetail::integer(
   static_cast<double>(*hp)*u.facts.lossUnitValue[u.facts.canShoot?1:0]/u.facts.hitPoints);
  if(!totalValue||*totalValue!=u.liveCombatValue) return std::nullopt;
  // Machines/special footprint roles have no ordinary pair/occupancy producer.
  if(o.role!=OrderRole::ORDINARY||s.catapult) return std::nullopt;
  auto & group=roster[index(o.owningSide)];group.push_back(&u);
  if(*hp>0)
  {
   if(occupancy[u.head]) return std::nullopt;
   occupancy[u.head]=o.id;
  }
  if(!o.immobilized)
  {
   total[index(o.owningSide)]+=u.liveCombatValue;
   if(!cannotAttack(o)) awake[index(o.owningSide)]+=u.liveCombatValue;
  }
  out.expectedDamage.emplace(o.id,u.initialExpectedDamage);move.units.push_back(o);
 }
 for(auto & group:roster)
 {
  std::sort(group.begin(),group.end(),[](auto a,auto b){return a->state.order.sourceIndex<b->state.order.sourceIndex;});
  if(group.size()>21) return std::nullopt;
  for(size_t i=0;i<group.size();++i) if(group[i]->state.order.sourceIndex!=static_cast<int>(i)) return std::nullopt;
 }
 for(int s=0;s<2;++s) if(total[s]>INT32_MAX||awake[s]>INT32_MAX) return std::nullopt;
 const auto construct=[&](int side)->std::optional<bool>
 {
  // Original fields are signed32 longs; multiplication precedes comparison.
  if(total[side]>INT32_MAX/2||std::min(awake[0],awake[1])>INT32_MAX/5) return std::nullopt;
  return total[side]*2<total[1-side]&&(in.difficulty>0||in.human[side]);
 };
 ProjectedMoveOrder order;
 try { order=projectMoveOrder(move); } catch(const std::invalid_argument &) { return std::nullopt; }
 if(order.containsComparatorTies) return std::nullopt;
 for(const auto & o:order.units){out.order.push_back(o.id);out.expectedDamage.at(o.id)=0;}
 const auto hp=[&](UnitId id){return units.at(id)->state.liveHealth-out.expectedDamage.at(id);};
 size_t cursor=0;
 for(size_t pass=0;pass<(in.checkingSurrender?2u:1u);++pass)
 {
  const int side=pass?1-index(in.requestedSide):index(in.requestedSide);
  const auto kills=construct(side);if(!kills) return std::nullopt;
  ++out.passes;
  while(cursor<out.order.size())
  {
   const UnitId id=out.order[cursor];const auto & actor=*units.at(id);const auto & a=actor.state;
   if(cannotAttack(a.order)||a.berserk||a.catapult||hp(id)==0){++cursor;continue;}
   if(index(a.controllingSide)!=side) break;
   if(!actor.facts.canShoot) return std::nullopt;
   std::map<UnitId,Ordinary::SourceTargetCensus> targets;
   for(const auto * peer:roster[1-side])
   {
    const auto & p=peer->state;Ordinary::SourceRangedCensusInput row;
    row.actor=peer->facts;row.speed=p.order.effectiveSpeed;row.immobilized=p.order.immobilized;
    row.stoneGazeTurns=p.order.sourceSpell70Turns;row.blindTurns=p.order.sourceSpell62Turns;
    row.berserkTurns=p.berserk?1:0;row.simulated=true;row.killsOnly=*kills;
    row.expectedDamage=out.expectedDamage.at(p.order.id);row.enemies.emplace();
    for(const auto * target:roster[side])
    {
     Ordinary::SourceRangedCandidate c;c.rosterIndex=target->state.order.sourceIndex;c.unit=target->facts;
     c.expectedDamage=out.expectedDamage.at(target->state.order.id);
     const auto attack=in.attacks.find({p.order.id,target->state.order.id});if(attack!=in.attacks.end()) c.attack=attack->second;
     row.enemies->push_back(c);
    }
    const auto result=Ordinary::produceSourceRangedCensus(row);if(!result) return std::nullopt;
    targets.emplace(p.order.id,*result);
   }
   OrdinaryForecastAction trace;trace.actor=id;trace.killsOnly=*kills;
   const auto choose=[&](bool killsOnly)->bool
   {
    trace.candidates.clear();trace.target.reset();int32_t best=0;bool bestIncap=false;
    for(const auto * target:roster[1-side])
    {
     const auto & t=target->state;const UnitId tid=t.order.id;
     if(t.order.immobilized||hp(tid)==0) continue;
     const auto attack=in.attacks.find({id,tid});if(attack==in.attacks.end()) return false;
     Ordinary::SourceOrdinaryExchangeInput exchange;exchange.attacker=actor.facts;exchange.defender=target->facts;
     exchange.attack[1]=attack->second;exchange.ranged=true;exchange.simulated=true;exchange.killsOnly=killsOnly;
     exchange.attackerExpectedDamage=out.expectedDamage.at(id);exchange.defenderExpectedDamage=out.expectedDamage.at(tid);
     const auto score=Ordinary::evaluateSourceOrdinaryExchange(exchange);if(!score) return false;
     int32_t value=score->value;const bool incap=incapacitated(t.order);
     if(in.difficulty||in.human[side])
     {
      const auto & census=targets.at(tid);
      const int divisor=incap?10:(!cannotAttack(t.order)&&census.target&&census.target->attackTime<=5?census.target->attackTime:5);
      if(divisor<=0) return false;
      value/=divisor;
     }
     trace.candidates.push_back({tid,value});
     if(trace.target&&((incap&&!bestIncap)||((incap||!bestIncap)&&value<best))) continue;
     trace.target=tid;best=value;bestIncap=incap;
    }
    if(trace.target&&killsOnly&&best==0)trace.retriedWithoutKillsOnly=true;
    return true;
   };
   if(!choose(*kills)) return std::nullopt;
   trace.initialCandidates=trace.candidates;
   if(trace.retriedWithoutKillsOnly&&!choose(false)) return std::nullopt;
   if(trace.target)
   {
    const auto & target=*units.at(*trace.target);
    if(occupancy[target.head]!=trace.target) return std::nullopt;
    const auto damage=Ordinary::OrdinaryExchangeDetail::damage(actor.facts,static_cast<int32_t>(hp(id)),
     static_cast<int32_t>(target.state.liveHealth),in.attacks.at({id,*trace.target}),true);
    if(!damage||*damage>INT32_MAX-out.expectedDamage.at(*trace.target)) return std::nullopt;
    trace.damage=*damage;
    out.expectedDamage.at(*trace.target)=static_cast<int32_t>(std::min<int64_t>(target.state.liveHealth,
     int64_t(out.expectedDamage.at(*trace.target))+*damage));
   }
   trace.expectedDamageAfter=out.expectedDamage;
   out.actions.push_back(std::move(trace));++cursor;
  }
  out.stoppedAt[pass]=cursor;
 }
 return out;
}
}
