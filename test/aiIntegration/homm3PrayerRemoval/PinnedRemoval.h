// SPDX-License-Identifier: CC0-1.0
#pragma once
#include "../../../AI/Integration/Ports/HoMM3PrayerRemoval.h"
#include <stdexcept>
namespace PinnedPrayerRemoval
{
using AIIntegration::Ports::HoMM3::PrayerRemovalFacts;
using std::sqrt;
constexpr int SPELL_PRAYER=48,SPELL_POISON=71,kNumSpellsAndCreatureEffects=80,creatureDone=1;
using ESpellId=int;
struct Trait{int32_t m_mastery_bonus[4]={2,2,4,4};int m_karma=1;};inline Trait akSpellTraits[80];
struct type_enchant_data{int m_mastery,m_duration,m_checkResistance=1;type_enchant_data(int,int mastery,int,int duration):m_mastery(mastery),m_duration(duration){}};
struct army
{
 const PrayerRemovalFacts * f=nullptr;const army *target=nullptr;
 int role=0,owner=0,index=0,m_topCreatureDamage=0,m_numTroops=1,m_baseSpeed=0;
 struct{int hitPoints=0,attackSkill=0,defenseSkill=0;}m_monInfo;
 bool praying=false;
 int getSpellTime(int spell)const{return spell==SPELL_PRAYER&&praying?f->duration:0;}
 int getSpellLevel(int)const{return f->mastery;}
 void cancelIndividualSpell(int spell){if(spell!=SPELL_PRAYER)throw std::logic_error("only prayer");praying=false;m_monInfo.attackSkill=m_monInfo.defenseSkill=0;}
 int getOwningSide()const{return owner;}
 void cleaned()const{if(praying)throw std::logic_error("valuation used prayed baseline");}
 const army*getAITarget()const{return target;}
 int getAITargetTime()const{cleaned();return f->oldTime;}
 int getAITargetTime(int)const{cleaned();return f->newTime;}
 int getSpeed()const{return role==2?f->targetSpeed:f->oldSpeed;}
 bool is(int)const{return f->completed;}
 bool canShoot(int)const{return false;}
 int getAIExpectedDamage()const{return f->expectedDamage;}
 int getTotalHitPoints(int)const{return role==2?f->targetHealth:f->health;}
 int getTotalCombatValue(int,int)const{cleaned();return f->total;}
 int getAverageDamage(const army&,bool,int,int,int)const{if(role==1)return f->defenseAverageDamage;cleaned();return f->attackAverageDamage;}
 double getEstimatedDamage(const army*other,int,bool,int)const{if(role==1){other->cleaned();return other->m_monInfo.defenseSkill?f->defenseEstimatedBoosted:f->defenseEstimatedBefore;}cleaned();return m_monInfo.attackSkill?f->attackEstimatedBoosted:f->attackEstimatedBefore;}
};
struct type_AI_spellcaster
{
 using TEnchantValue=int32_t(type_AI_spellcaster::*)(const army*,type_enchant_data)const;
 const PrayerRemovalFacts*f=nullptr;int m_side=0;bool m_winLikely=false;type_AI_spellcaster*m_enemyCaster=nullptr;
 struct{int m_roundsLeft=1,m_lowestAttack=0,m_lowestDefense=0;}m_estimate;
 struct Threat{const army*m_enemy=nullptr;int32_t m_totalDamage=0;};Threat worst_enemies[1],melee_enemies[1],ranged_enemies[1];
 TEnchantValue getEnchantmentFunction(int spell)const{return spell==SPELL_PRAYER?&type_AI_spellcaster::getPrayerValue:nullptr;}
 int32_t getMoveOrderChangeValue(const army*)const{return f->moveOrderValue;}
 int32_t getCancelValue(army*,unsigned char)const;
 int32_t getPrayerValue(const army*,type_enchant_data)const;
 int32_t getDefenseSkillValue(const army*,int32_t,int32_t)const;
 int32_t getDefenseBoostValue(const army*,const army*,int32_t,double)const;
 int32_t getAttackSkillValue(const army*,const army*,int32_t,int32_t)const;
 int32_t getAttackBoostValue(const army*,const army*,int32_t,int32_t,double)const;
 int32_t getAttackBoostValue(const army*,const army*,int32_t,double)const;
 int32_t getSpeedValue(const army*,int32_t,int32_t)const;
 double getDuration(int32_t,unsigned char)const;
};
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define long int32_t
#include "bodies.pinned.inc"
#undef long
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
inline int32_t evaluate(const PrayerRemovalFacts&f,bool cure=false){
 army actor;actor.f=&f;actor.praying=true;actor.owner=f.friendly?0:1;actor.m_topCreatureDamage=f.topDamage;actor.m_monInfo.hitPoints=f.hitPoints;actor.m_baseSpeed=f.newSpeed-(f.mastery>=2?4:2);
 army target;target.f=&f;target.role=2;army worst;worst.f=&f;worst.role=1;if(f.hasTarget)actor.target=&target;
 type_AI_spellcaster main,enemy;main.m_side=0;enemy.m_side=1;main.m_enemyCaster=&enemy;enemy.m_enemyCaster=&main;
 for(auto*c:{&main,&enemy}){c->f=&f;c->m_winLikely=c->m_side==actor.owner?f.ownerWinLikely:!f.ownerWinLikely;c->m_estimate.m_roundsLeft=f.rounds;c->worst_enemies[0].m_enemy=f.hasWorstEnemy?&worst:nullptr;c->melee_enemies[0].m_totalDamage=f.cachedIncoming;}
 return main.getCancelValue(&actor,cure);
}
}
