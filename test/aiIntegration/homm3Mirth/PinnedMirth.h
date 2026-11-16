// SPDX-License-Identifier: CC0-1.0
// Exact pinned bodies with an input-only API harness.
#pragma once
#include <algorithm>
#include <cstdint>
namespace PinnedMirth
{
constexpr int SPELL_MIRTH = 0;
constexpr int creatureNoMorale = 0;
constexpr int creatureDone = 1;
struct Trait { int m_mastery_bonus[4] = {1, 1, 2, 2}; };
inline Trait akSpellTraits[1];
struct type_enchant_data { int m_mastery; int m_duration; };
struct army
{
    int32_t total;
    int morale;
    bool immune;
    bool done;
    bool is(int flag) const { return flag == creatureNoMorale ? immune : done; }
    int getMorale(int) const { return morale; }
    long getTotalCombatValue(int, int) const { return total; }
};
double valueOfLuckAndMorale(long value, long change, double goodValueMultiplier, double badValueMultiplier)
{
    if (change > 0) {
        if (value >= 3)
            return 0.0;
        if (value + change > 3)
            change = 3 - value;
        if (value >= 0)
            return static_cast<double>(change) * goodValueMultiplier;
        if (change <= value)
            return -(static_cast<double>(change) * badValueMultiplier);
        return static_cast<double>(value + change) * goodValueMultiplier
               + static_cast<double>(value) * badValueMultiplier;
    }
    if (value <= -3)
        return 0.0;
    if (value + change < -3)
        change = -3 - value;
    if (value <= 0)
        return -(static_cast<double>(change) * badValueMultiplier);
    if (-change <= value)
        return static_cast<double>(change) * goodValueMultiplier;
    return -(static_cast<double>(value) * goodValueMultiplier)
           - static_cast<double>(value + change) * badValueMultiplier;
}
inline double aiValueOfMorale(long morale, long change)
{
    return valueOfLuckAndMorale(morale, change, 0.0173, -0.0833);
}
struct type_AI_spellcaster
{
    struct { int m_lowestAttack = 0; int m_lowestDefense = 0; } m_estimate;
    int rounds = 1;
    double getDuration(long turns, bool done) const
    {
        double result = turns >= rounds ? 1.0 : static_cast<double>(turns) / rounds;
        if(done) result -= 1.0 / rounds;
        return std::max(0.0, result);
    }
    long getMirthValue(const army *, type_enchant_data) const;
};
long type_AI_spellcaster::getMirthValue(const army* ourArmy, type_enchant_data caster) const
{
    if (ourArmy->is(creatureNoMorale))
        return 0;
    long change = akSpellTraits[SPELL_MIRTH].m_mastery_bonus[caster.m_mastery];
    double effect = aiValueOfMorale(ourArmy->getMorale(1), change);
    if (effect == 0.0)
        return 0;
    double scale = getDuration(caster.m_duration, ourArmy->is(creatureDone));
    double total = static_cast<double>(ourArmy->getTotalCombatValue(m_estimate.m_lowestAttack,
                                                                        m_estimate.m_lowestDefense));
    return static_cast<long>(total * scale * effect);
}
}
