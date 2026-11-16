// SPDX-License-Identifier: CC0-1.0
// Exact pinned function with an input-only source API harness.
#pragma once
#include <cmath>
#include <cstdint>
namespace PinnedDisruptingRay
{
constexpr int SPELL_DISRUPTING_RAY = 0;
struct Trait { int m_mastery_bonus[4] = {3, 3, 4, 5}; };
inline Trait akSpellTraits[1];
struct type_enchant_data { int m_mastery; bool m_checkResistance; };
struct army
{
    const army * target = nullptr;
    int32_t total = 0;
    const army * getAITarget() const { return target; }
    long getTotalCombatValue(int, int) const { return total; }
};
struct combatManager
{
    army m_armies[2][7];
    long m_numArmies[2] = {0, 0};
    double chance = 1;
    double spellCastWorkChance(int, int, const army *, int, int, long) const { return chance; }
};
inline combatManager manager;
inline combatManager * g_combatManager = &manager;
struct type_AI_spellcaster
{
    struct { bool m_killsOnly = false; int m_lowestAttack = 0; int m_lowestDefense = 0; } m_estimate;
    int m_side = 0;
    int m_isCreatureSpell = 0;
    long getDisruptiveRayValue(const army *, type_enchant_data) const;
};
inline long type_AI_spellcaster::getDisruptiveRayValue(const army* enemy, type_enchant_data caster) const
{
    const combatManager* combat = g_combatManager;
    const army* stacks = combat->m_armies[m_side];
    if (m_estimate.m_killsOnly)
        return 0;
    long count = combat->m_numArmies[m_side];
    long i;
    for (i = 0; i < count; i++)
        if (stacks[i].getAITarget() == enemy)
            break;
    if (i == count)
        return 0;
    long bonus = akSpellTraits[SPELL_DISRUPTING_RAY].m_mastery_bonus[caster.m_mastery];
    double total = static_cast<double>(enemy->getTotalCombatValue(m_estimate.m_lowestAttack,
                                                                     m_estimate.m_lowestDefense));
    long value = static_cast<long>(
        total - total * sqrt(1.0 - static_cast<double>(bonus) * 0.05));
    if (caster.m_checkResistance) {
        long creatureCast = m_isCreatureSpell != 0;
        value = static_cast<long>(
            g_combatManager->spellCastWorkChance(SPELL_DISRUPTING_RAY, m_side, enemy, 0, 1,
                                                 creatureCast) * value);
    }
    return value;
}
}
