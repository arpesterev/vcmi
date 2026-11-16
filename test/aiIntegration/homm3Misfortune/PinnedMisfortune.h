// SPDX-License-Identifier: CC0-1.0
// Pinned e12906accf96c2662b1abadbc49ddc111991bf21 ai_tactical.cpp body.
#pragma once
#include "../homm3Mirth/PinnedMirth.h"
namespace PinnedMisfortune
{
inline double aiValueOfLuck(long luck,long change){return PinnedMirth::valueOfLuckAndMorale(luck,change,0.0173f,-0.0122f);}
constexpr int SPELL_MISFORTUNE=0,creatureNoMorale=0,creatureDone=1;
struct Trait{int m_mastery_bonus[4]={1,1,2,2};};inline Trait akSpellTraits[1];
struct army {int total=0,luck=0,index=0;bool immune=false,done=false;
 bool is(int flag)const{return flag==creatureNoMorale?immune:done;}
 int getLuck(int)const{return luck;}long getTotalCombatValue(int,int)const{return total;}};
struct type_enchant_data{int m_mastery=0,m_duration=1;bool m_checkResistance=true;};
struct Manager{double chance=1;double spellCastWorkChance(int,int,const army*,int,int,int)const{return chance;}};
inline Manager manager;inline Manager *g_combatManager=&manager;
struct type_AI_spellcaster{
 struct {int m_lowestAttack=0,m_lowestDefense=0;bool m_killsOnly=false;}m_estimate;
 int m_enemyCanAttack=1,m_isCreatureSpell=0,m_side=0,rounds=1;bool m_winLikely=false;
 double getDuration(long turns,bool done)const{double r=turns>=rounds?1.0:double(turns)/rounds;if(done)r-=1.0/rounds;return std::max(0.0,r);}
 long getMisfortuneValue(const army*,type_enchant_data)const;
};
long type_AI_spellcaster::getMisfortuneValue(const army* enemy, type_enchant_data caster) const
{
    if ((m_enemyCanAttack & (1 << enemy->index)) == 0)
        return 0;
    if (m_estimate.m_killsOnly)
        return 0;
    long change = akSpellTraits[SPELL_MISFORTUNE].m_mastery_bonus[caster.m_mastery];
    double effect = -aiValueOfLuck(enemy->getLuck(1), -change);
    if (effect == 0.0)
        return 0;
    if (caster.m_checkResistance) {
        long creatureCast = m_isCreatureSpell != 0;
        effect = effect * g_combatManager->spellCastWorkChance(SPELL_MISFORTUNE, m_side, enemy,
                                                               0, 1, creatureCast);
    }
    double scale = getDuration(caster.m_duration, enemy->is(creatureDone));
    double total = static_cast<double>(enemy->getTotalCombatValue(m_estimate.m_lowestAttack,
                                                                     m_estimate.m_lowestDefense));
    return static_cast<long>(total * scale * effect);
}

}
