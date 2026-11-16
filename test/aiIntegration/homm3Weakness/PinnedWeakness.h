// SPDX-License-Identifier: CC0-1.0
#pragma once
#include <cmath>
#include <algorithm>
namespace PinnedWeakness {
using std::min;
constexpr int SPELL_WEAKNESS=0,creatureDone=0;
struct Trait{int m_mastery_bonus[4]={3,3,6,6};};inline Trait akSpellTraits[1];
struct type_enchant_data{int m_mastery=0,m_duration=1;};
struct army{int total=0,damage=0,health=1,m_numTroops=1,index=0,originalAttack=6;bool done=false;const army *target=nullptr;int targetTime=1;
 struct {int attackSkill=6;}m_monInfo;double oldEstimate=1,newEstimate=1;
 const army*getAITarget()const{return target;}int getAITargetTime()const{return targetTime;}
 bool canShoot(int)const{return true;}long getAverageDamage(const army&,bool,int,int,int)const{return damage;}
 double getEstimatedDamage(const army*,int,bool,int)const{return m_monInfo.attackSkill==originalAttack?oldEstimate:newEstimate;}
 long getTotalHitPoints(int)const{return health;}bool is(int)const{return done;}long getTotalCombatValue(int,int)const{return total;}};
struct type_AI_spellcaster{struct{int m_lowestAttack=0,m_lowestDefense=0;bool m_killsOnly=false;}m_estimate;bool m_winLikely=false;int m_enemyCanAttack=1,rounds=1;
 double getDuration(long turns,bool done)const{double r=turns>=rounds?1.0:double(turns)/rounds;if(done)r-=1.0/rounds;return std::max(0.0,r);}
 long getAttackBoostValue(const army*,const army*,long,long,double)const;long getAttackBoostValue(const army*,const army*,long,double)const;
 long getAttackSkillValue(const army*,const army*,long,long)const;long getWeaknessValue(const army*,type_enchant_data)const;};
long type_AI_spellcaster::getAttackBoostValue(const army* ourArmy,
    const army* enemy, long oldDamage, long duration, double increase) const
{
    long newDamage = static_cast<long>(oldDamage * increase);
    long enemyHits = enemy->getTotalHitPoints(0);
    if (newDamage > enemyHits) {
        newDamage = enemyHits;
        increase = static_cast<double>(enemyHits) / oldDamage;
    }
    if (newDamage <= oldDamage)
        return 0;
    double modifier = getDuration(duration, ourArmy->is(creatureDone));
    double total = static_cast<double>(ourArmy->getTotalCombatValue(
        m_estimate.m_lowestAttack, m_estimate.m_lowestDefense));
    return static_cast<long>((sqrt(increase) - 1.0) * total * modifier);
}

long type_AI_spellcaster::getAttackBoostValue(const army* ourArmy,
    const army* enemy, long duration, double increase) const
{
    long oldDamage = ourArmy->getAverageDamage(*enemy, ourArmy->canShoot(0),
        ourArmy->m_numTroops, 1, 0);
    return getAttackBoostValue(ourArmy, enemy, oldDamage, duration, increase);
}

long type_AI_spellcaster::getAttackSkillValue(const army* ourArmy, const army* enemy, long duration, long bonus) const
{
    if (m_winLikely)
        return 0;
    army testArmy = *ourArmy;
    testArmy.m_monInfo.attackSkill += bonus;
    bool ranged = ourArmy->canShoot(0);
    double oldDamage = ourArmy->getEstimatedDamage(enemy, 100, ranged, 0);
    double newDamage = testArmy.getEstimatedDamage(enemy, 100, ranged, 0);
    double increase = newDamage / oldDamage;
    return getAttackBoostValue(ourArmy, enemy, duration, increase);
}

long type_AI_spellcaster::getWeaknessValue(const army* enemy, type_enchant_data caster) const
{
    if ((m_enemyCanAttack & (1 << enemy->index)) != 0 && !m_estimate.m_killsOnly) {
        const army* target = enemy->getAITarget();
        if (target != 0
                && enemy->getAITargetTime() <= 1) {
            long capped = min(
                static_cast<int>(akSpellTraits[SPELL_WEAKNESS]
                                     .m_mastery_bonus[caster.m_mastery]),
                static_cast<int>(enemy->m_monInfo.attackSkill));
            return getAttackSkillValue(enemy, target, caster.m_duration, capped);
        }
    }
    return 0;
}

}
