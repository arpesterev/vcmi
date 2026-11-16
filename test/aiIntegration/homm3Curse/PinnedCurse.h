// SPDX-License-Identifier: CC0-1.0
// Exact pinned getCurseValue body with a minimal input-only source API harness.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace PinnedCurse
{
constexpr int SPELL_CURSE = 42;
constexpr int creatureDone = 1;
struct Trait { int m_mastery_bonus[4] = {0, 0, 1, 1}; };
inline Trait akSpellTraits[43];
struct type_enchant_data { int m_mastery; int m_duration; bool m_checkResistance; };
struct army
{
    int index = 0;
    struct { int damageLowBound; } m_monInfo;
    int32_t total;
    double average;
    bool done;
    long getTotalCombatValue(int, int) const { return total; }
    double getAverageDamage() const { return average; }
    bool is(int) const { return done; }
};
struct Manager
{
    double chance = 1;
    double spellCastWorkChance(int, int, const army *, int, int, long) const { return chance; }
};
inline Manager manager;
inline Manager * g_combatManager = &manager;
struct type_AI_spellcaster
{
    int m_enemyCanAttack = 1;
    struct { int m_lowestAttack = 0; int m_lowestDefense = 0; bool m_killsOnly = false; } m_estimate;
    bool m_winLikely = false;
    int m_isCreatureSpell = 0;
    int m_side = 0;
    int rounds = 1;
    double getDuration(long turns, bool done) const
    {
        double result = turns >= rounds ? 1.0 : static_cast<double>(turns) / rounds;
        if(done) result -= 1.0 / rounds;
        return std::max(0.0, result);
    }
    long getCurseValue(const army *, type_enchant_data) const;
};
long type_AI_spellcaster::getCurseValue(const army* enemy, type_enchant_data caster) const
{
    if ((m_enemyCanAttack & (1 << enemy->index)) != 0 && !m_estimate.m_killsOnly && !m_winLikely) {
        long value = enemy->getTotalCombatValue(m_estimate.m_lowestAttack,
                                                   m_estimate.m_lowestDefense);
        double oldAverage = enemy->getAverageDamage();
        double newAverage = enemy->m_monInfo.damageLowBound - akSpellTraits[SPELL_CURSE].m_mastery_bonus[caster.m_mastery];
        if (newAverage < 1.0)
            newAverage = 1.0;
        double decrease = newAverage / oldAverage;
        value = static_cast<long>(value - sqrt(decrease) * value);
        double scale = getDuration(caster.m_duration, enemy->is(creatureDone));
        value = static_cast<long>(value * scale);
        if (caster.m_checkResistance) {
            long creatureCast = m_isCreatureSpell != 0;
            value = static_cast<long>(
                g_combatManager->spellCastWorkChance(SPELL_CURSE, m_side, enemy, 0, 1,
                                                     creatureCast) * value);
        }
        return value;
    }
    return 0;
}
inline long planningRounds(long m_awakeFriendlyValue, long m_awakeEnemyValue)
{
    long m_roundsLeft;
    const double g_aiOddsLadder[] = {2.6, 1.9, 1.5, 1.31, 1.2, 1.13};
    long high = m_awakeFriendlyValue;
    long low = m_awakeEnemyValue;
    if (high < low) {
        high = m_awakeEnemyValue;
        low = m_awakeFriendlyValue;
    }
    if (low * 5 > high && high != 0) {
        double ratio = static_cast<double>(high) / static_cast<double>(low);
        for (m_roundsLeft = 0; m_roundsLeft < 6; m_roundsLeft++) {
            if (ratio >= g_aiOddsLadder[m_roundsLeft])
                break;
        }
        m_roundsLeft++;
    } else {
        m_roundsLeft = 1;
    }
    return m_roundsLeft;
}

}
