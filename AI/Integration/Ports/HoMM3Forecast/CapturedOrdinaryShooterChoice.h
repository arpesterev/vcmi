/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "OrdinaryTwoPass.h"
#include "../SourceOrdinaryShooterChoice.h"
namespace HoMM3Forecast
{
// A supported no-target decision is distinct from unsupported capture.
struct NativeOrdinaryShooterDecision
{
 std::optional<UnitId> target;
 Ordinary::SourceShooterChoice source;
};
inline std::optional<NativeOrdinaryShooterDecision> chooseCapturedOrdinaryShooter(
 const OrdinaryTwoPassInput & facts,UnitId active)
{
 using namespace OrdinaryTwoPassDetail;
 const OrdinaryForecastUnit * actor=nullptr;
 std::array<std::vector<const OrdinaryForecastUnit *>,2> roster;
 std::array<int64_t,2> total{};
 for(const auto &u:facts.units)
 {
  const auto &o=u.state.order;
  if(!validSide(o.owningSide)||u.state.controllingSide!=o.owningSide
   ||o.role!=OrderRole::ORDINARY||u.state.catapult||u.liveCombatValue<0)return std::nullopt;
  const auto health=Ordinary::OrdinaryExchangeDetail::health(u.facts);
  if(!health||*health!=u.state.liveHealth||(*health==0&&!o.immobilized))return std::nullopt;
  roster[index(o.owningSide)].push_back(&u);
  if(!o.immobilized)total[index(o.owningSide)]+=u.liveCombatValue;
  if(o.id==active){if(actor)return std::nullopt;actor=&u;}
 }
 if(!actor||actor->state.liveHealth<=0||actor->state.berserk||cannotAttack(actor->state.order)
  ||!actor->facts.canShoot)return std::nullopt;
 for(auto &side:roster){
  std::sort(side.begin(),side.end(),[](const auto *a,const auto *b){return a->state.order.sourceIndex<b->state.order.sourceIndex;});
  for(size_t i=0;i<side.size();++i)if(side[i]->state.order.sourceIndex!=static_cast<int32_t>(i))return std::nullopt;
 }
 const int side=index(actor->state.order.owningSide);
 if(total[side]>INT32_MAX/2||total[1-side]>INT32_MAX)return std::nullopt;
 Ordinary::SourceShooterChoiceInput in;in.actor=actor->facts;
 in.friendlyCombatValue=static_cast<int32_t>(total[side]);in.enemyCombatValue=static_cast<int32_t>(total[1-side]);
 in.difficulty=facts.difficulty;in.human=facts.human[side];
 const bool killsOnly=total[side]*2<total[1-side]&&(in.difficulty>0||in.human);
 for(const auto *enemy:roster[1-side])
 {
  const auto &state=enemy->state;const auto &order=state.order;
  Ordinary::SourceShooterCandidate c;c.rosterIndex=order.sourceIndex;c.unit=enemy->facts;
  // Source army::processDeath sets creatureImmobilized before retaining the slot.
  c.excluded=order.immobilized;c.incapacitated=incapacitated(order);c.cannotAttack=cannotAttack(order);
  if(!c.excluded)
  {
   const auto pair=facts.attacks.find({active,order.id});if(pair==facts.attacks.end())return std::nullopt;c.attack=pair->second;
   Ordinary::SourceRangedCensusInput row;row.actor=enemy->facts;row.speed=order.effectiveSpeed;
   row.immobilized=order.immobilized;row.stoneGazeTurns=order.sourceSpell70Turns;
   row.blindTurns=order.sourceSpell62Turns;row.berserkTurns=state.berserk?1:0;
   row.killsOnly=killsOnly;row.enemies.emplace(); // simulated remains false
   for(const auto *target:roster[side])
   {
    Ordinary::SourceRangedCandidate t;t.rosterIndex=target->state.order.sourceIndex;t.unit=target->facts;
    const auto attack=facts.attacks.find({order.id,target->state.order.id});
    if(attack!=facts.attacks.end())t.attack=attack->second;
    row.enemies->push_back(t);
   }
   c.enemyCensus=Ordinary::produceSourceRangedCensus(row);
  }
  in.enemies.push_back(c);
 }
 const auto result=Ordinary::chooseSourceOrdinaryShooter(in);if(!result)return std::nullopt;
 NativeOrdinaryShooterDecision out;out.source=*result;
 if(result->targetIndex)out.target=roster[1-side][*result->targetIndex]->state.order.id;
 return out;
}

}
