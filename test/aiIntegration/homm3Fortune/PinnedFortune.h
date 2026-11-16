// SPDX-License-Identifier: CC0-1.0
// Exact pinned source bodies, scalar input-only fixture.
#pragma once
#include <algorithm>
#include <cmath>
namespace PinnedFortune {
constexpr int SPELL_FORTUNE=0,creatureDone=0;
struct Trait{int m_mastery_bonus[4]={1,1,2,2};};inline Trait akSpellTraits[1];
struct type_enchant_data{int m_mastery=0,m_duration=1;};
struct army{int total=0,luck=0,damage=0,health=1,m_numTroops=1;bool done=false;const army *target=nullptr;int targetTime=1;
 const army* getAITarget()const{return target;}int getAITargetTime()const{return targetTime;}
 int getLuck(int)const{return luck;}int getAverageDamage(const army&,bool,int,int,int)const{return damage;}
 bool canShoot(int)const{return true;}int getTotalHitPoints(int)const{return health;}
 bool is(int)const{return done;}int getTotalCombatValue(int,int)const{return total;}};
struct type_AI_spellcaster{struct{int m_lowestAttack=0,m_lowestDefense=0;}m_estimate;int rounds=1;
 double getDuration(long turns,bool done)const{double r=turns>=rounds?1.0:double(turns)/rounds;if(done)r-=1.0/rounds;return std::max(0.0,r);}
 long getAttackBoostValue(const army*,const army*,long,long,double)const;
 long getFortuneValue(const army*,type_enchant_data)const;};
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

long type_AI_spellcaster::getFortuneValue(const army* ourArmy, type_enchant_data caster) const
{
    const army* target = ourArmy->getAITarget();
    if (target == 0 || ourArmy->getAITargetTime() > 1)
        return 0;
    long bonus = akSpellTraits[SPELL_FORTUNE].m_mastery_bonus[caster.m_mastery];
    long luck = ourArmy->getLuck(0);
    long damage = ourArmy->getAverageDamage(*target, ourArmy->canShoot(0),
                                               ourArmy->m_numTroops, 0, 0);
    long strikes;
    long value = 0;
    if (luck >= 3)
        return 0;
    if (bonus + luck <= -3)
        return 0;
    if (bonus + luck > 3)
        bonus = 3 - luck;
    if (luck < 0) {
        if (bonus + luck > 0)
            strikes = -luck;
        else
            strikes = bonus;
        value = getAttackBoostValue(ourArmy, target,
            damage / 2, caster.m_duration, 2.0) * strikes / 24;
    }
    if (bonus + luck > 0) {
        if (luck < 0)
            strikes = bonus + luck;
        else
            strikes = bonus;
        value += getAttackBoostValue(ourArmy, target,
            damage, caster.m_duration, 2.0) * strikes / 24;
    }
    return value;
}

}
