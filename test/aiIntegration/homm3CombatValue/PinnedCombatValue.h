/* Reconstructed CC0 source fixture. Not a retail executable or damage oracle.
 * Source: sushi-shi/homm3-decomp e12906accf96c2662b1abadbc49ddc111991bf21/src/army.cpp
 * Numeric test domains fit signed 32-bit retail long. Fixture dependencies
 * provide explicit values and do not access VCMI or either hero's state.
 */
#pragma once
#include <array>
#include <cmath>
namespace PinnedCombatValue
{
enum { creatureShootingArmy=1, creatureTwoAttacks=2, creatureSiegeWeapon=4,
       creatureSummoned=8, creatureImmobilized=16, creatureClone=32,
       ARMY_CREATURE_BALLISTA=1, eSecSkillBattlefieldBallistics=20 };
struct hero { int level=0; int getSecondarySkill(int) const { return level; } };
struct Trait { double baseFightValue=100; };
inline std::array<Trait,2> akCreatureTypeTraits;
inline constexpr double g_artilleryFactors[4] = {1.0,1.5,1.5,2.0};
struct army
{
    int flags=0, m_creatureType=0, group=0;
    long m_numTroops=10, m_topCreatureDamage=0, AI_expected_damage=0;
    long attackModifier=0, defenseModifier=0, m_blessAmount=0, m_curseAmount=0;
    double damageReduction=1;
    bool shooting=true;
    hero* controller=nullptr;
    struct { long hitPoints=20, damageLowBound=2, damageHighBound=6; } m_monInfo;
    std::array<long,80> m_spellInfluence{};
    bool is(int mask) const { return (flags & mask) != 0; }
    long getAttackModifier(int,bool) const { return attackModifier; }
    long getDefenseModifier() const { return defenseModifier; }
    double computeDefenderDamageReduction(bool) const { return damageReduction; }
    bool canShoot(int) const { return shooting; }
    hero* getController() const { return controller; }
    double getAverageDamage() const;
    double getUnitCombatValue(long,long,bool,const army*) const;
    long getLossCombatValue(long,long,bool,long,bool) const;
    long getTotalHitPoints(bool) const;
};
struct combatManager { army m_armies[2][6]; long m_numArmies[2]={6,6}; };
inline combatManager* g_combatManager=nullptr;
template<class A,class B> auto cppMax(A a,B b) { return a > b ? a : b; }
inline long max(long a,int b) { return a > b ? a : b; }
using std::max;

double army::getAverageDamage() const
{
    if (m_spellInfluence[41])
        return m_blessAmount + m_monInfo.damageHighBound;
    if (m_spellInfluence[42])
        return cppMax(m_monInfo.damageLowBound - m_curseAmount, 1);
    return (m_monInfo.damageHighBound + m_monInfo.damageLowBound) / 2.0;
}

double army::getUnitCombatValue(long lowestAttack, long lowestDefense,
                                   bool ranged,
                                   const army* excluded) const
{
    long attackDiff = getAttackModifier(0, ranged) - lowestAttack;
    long defenseDiff = getDefenseModifier() - lowestDefense;
    double defense = computeDefenderDamageReduction(ranged);
    defense = (defenseDiff * 0.05 + 1.0) * defense;
    if (ranged && !canShoot(0))
        ranged = 0;
    double attack = attackDiff * 0.05 + 1.0;
    if (!ranged && is(creatureShootingArmy))
        attack = attack * 0.5;
    if (m_creatureType == ARMY_CREATURE_BALLISTA) {
        if (getController()) {
            if (getController()
                    ->getSecondarySkill(eSecSkillBattlefieldBallistics) > 1)
                attack = attack + attack;
            attack =
                attack
                * g_artilleryFactors[getController()->getSecondarySkill(
                    eSecSkillBattlefieldBallistics)];
        }
    }
    if (m_spellInfluence[41] || m_spellInfluence[42]) {
        long damageRange = m_monInfo.damageLowBound + m_monInfo.damageHighBound;
        double baseAverage = damageRange / 2.0;
        double averageDamage = getAverageDamage();
        attack = averageDamage / baseAverage * attack;
    }
    if (ranged && is(creatureTwoAttacks))
        attack = attack + attack;
    double result = sqrt(attack * defense)
                    * akCreatureTypeTraits[m_creatureType].baseFightValue;
    if (is(creatureSiegeWeapon | creatureSummoned)) {
        long total = getTotalHitPoints(0);
        long sum = 0;
        army* stack = g_combatManager->m_armies[group];
        for (long i = 0; i < g_combatManager->m_numArmies[group];
             i++, stack++) {
            if (!stack->is(creatureSiegeWeapon | creatureImmobilized | creatureSummoned)) {
                sum += stack->getTotalHitPoints(0);
            }
        }
        if (total == 0)
            result = 0.1;
        else
            result = sum * result / (total + sum);
    }
    return result;
}

long army::getLossCombatValue(long lowestAttack, long lowestDefense,
                                 bool ranged, long damage,
                                 bool killsOnly) const
{
    double value = getUnitCombatValue(lowestAttack, lowestDefense,
                                         ranged, 0);
    if (is(creatureClone))
        return static_cast<long>(m_numTroops * value / 5.0);
    if (killsOnly)
        value = 1000.0;
    long lost = 0;
    if (damage % m_monInfo.hitPoints + m_topCreatureDamage >= m_monInfo.hitPoints)
        lost = m_topCreatureDamage;
    return static_cast<long>((lost + damage) * value / m_monInfo.hitPoints);
}

long army::getTotalHitPoints(bool simulated) const
{
    long total;
    if (is(creatureClone))
        total = 1;
    else
        total = m_monInfo.hitPoints * m_numTroops - m_topCreatureDamage;
    if (simulated)
        total = max(total - AI_expected_damage, 0);
    return total;
}
}
