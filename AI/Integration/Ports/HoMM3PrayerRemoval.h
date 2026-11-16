/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace AIIntegration::Ports::HoMM3
{
// Facts for the buff OWNER's cached caster, with Prayer removed from the copy.
// Damage ratios are reevaluated on that clean copy; cached incoming totals and
// target identity/distance remain those of the pre-cancellation caster.
struct PrayerRemovalFacts
{
 bool singlePrayerRemoved=false,cleanedBaselineKnown=false,friendly=false;
 bool ownerContextKnown=false,ownerWinLikely=false,completed=false;
 int mastery=0,duration=0,rounds=0;
 int32_t total=0,health=0,hitPoints=0,topDamage=0,expectedDamage=0;
 bool hasWorstEnemy=false,hasTarget=false;
 int32_t cachedIncoming=0,defenseAverageDamage=0;
 double defenseEstimatedBefore=0,defenseEstimatedBoosted=0;
 int32_t targetHealth=0,attackAverageDamage=0;
 double attackEstimatedBefore=0,attackEstimatedBoosted=0;
 int32_t oldSpeed=0,newSpeed=0,targetSpeed=0,oldTime=0,newTime=0,moveOrderValue=0;
};
inline std::optional<int32_t> singlePrayerRemovalValue(const PrayerRemovalFacts&f)
{
 if(!f.singlePrayerRemoved||!f.cleanedBaselineKnown||!f.ownerContextKnown||f.mastery<0||f.mastery>3||f.duration<=0||f.rounds<1||f.rounds>7||f.total<0||f.health<=0||f.hitPoints<=0||f.topDamage<0||f.topDamage>=f.hitPoints||f.expectedDamage<0||f.cachedIncoming<0)return std::nullopt;
 if(int64_t(f.expectedDamage)+f.topDamage>INT32_MAX||int64_t(f.cachedIncoming)*f.rounds+f.topDamage>INT32_MAX)return std::nullopt;
 const auto integer=[](double n)->std::optional<int32_t>{if(!std::isfinite(n)||n<INT32_MIN||n>INT32_MAX)return std::nullopt;return static_cast<int32_t>(n);};
 int64_t defense=0,speed=0,attack=0;
 if(f.hasWorstEnemy){
  if(f.defenseAverageDamage<0||!std::isfinite(f.defenseEstimatedBefore)||!std::isfinite(f.defenseEstimatedBoosted)||f.defenseEstimatedBefore<=0||f.defenseEstimatedBoosted<=0)return std::nullopt;
  double increase=f.defenseEstimatedBefore/f.defenseEstimatedBoosted;
  const auto reduced=integer(f.defenseAverageDamage/increase);if(!reduced||*reduced<0)return std::nullopt;
  int32_t damage=f.defenseAverageDamage;
  if(damage>f.health){if(*reduced==0)return std::nullopt;increase=double(f.health)/ *reduced;damage=f.health;}
  if(*reduced<damage&&(!f.ownerWinLikely||int64_t(f.expectedDamage)+f.topDamage>=f.hitPoints)&&int64_t(f.cachedIncoming)*f.rounds+f.topDamage>=f.hitPoints){
   const auto value=integer(std::min(1.0,double(f.duration)/f.rounds)*f.total*(std::sqrt(increase)-1.0));if(!value)return std::nullopt;defense=*value;
  }
 }
 if(f.hasTarget&&!f.ownerWinLikely){
  if(f.oldSpeed<0||f.newSpeed<0||f.targetSpeed<0||f.oldTime<1||f.newTime<1)return std::nullopt;
  if(f.duration-(f.completed?1:0)!=0&&f.newTime<=f.rounds){
   if(f.newTime==1&&f.targetSpeed>=f.oldSpeed&&f.targetSpeed<f.newSpeed)speed=std::max(0,f.moveOrderValue);
   if(f.newTime<f.oldTime){const auto oldTime=std::min(f.oldTime,f.rounds+1);const int64_t first=int64_t(f.rounds-f.newTime+1)*f.total,second=int64_t(f.rounds-oldTime+1)*f.total;
    if(first>INT32_MAX||second>INT32_MAX)return std::nullopt;
    speed+=first/f.rounds-second/f.rounds;
   }
  }
  if(f.oldTime==1){
   if(f.targetHealth<0||f.attackAverageDamage<=0||!std::isfinite(f.attackEstimatedBefore)||!std::isfinite(f.attackEstimatedBoosted)||f.attackEstimatedBefore<=0||f.attackEstimatedBoosted<0)return std::nullopt;
   double increase=f.attackEstimatedBoosted/f.attackEstimatedBefore;
   const auto offered=integer(f.attackAverageDamage*increase);if(!offered)return std::nullopt;
   int32_t damage=*offered;if(damage>f.targetHealth){damage=f.targetHealth;increase=double(f.targetHealth)/f.attackAverageDamage;}
   if(damage>f.attackAverageDamage){const double duration=std::max(0.0,std::min(1.0,double(f.duration)/f.rounds)-(f.completed?1.0/f.rounds:0));const auto value=integer((std::sqrt(increase)-1.0)*f.total*duration);if(!value)return std::nullopt;attack=*value;}
  }
 }
 const int64_t total=defense+speed+attack;if(total<0||total>INT32_MAX)return std::nullopt;
 return static_cast<int32_t>(f.friendly?-total:total);
}
}
