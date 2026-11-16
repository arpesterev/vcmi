// SPDX-License-Identifier: CC0-1.0
// Exact pinned Prayer body, with input-only component test harness.
#pragma once
#include <cstdint>
namespace PinnedPrayer
{
constexpr int SPELL_PRAYER = 0;
struct Trait { long m_mastery_bonus[4] = {2,2,4,4}; };
inline Trait akSpellTraits[1];
struct type_enchant_data { int m_mastery; long m_duration; };
struct army
{
 const army * target;
 int time;
 const army * getAITarget() const { return target; }
 int getAITargetTime() const { return time; }
};
struct type_AI_spellcaster
{
 double defense, speed, attack;
 long getDefenseSkillValue(const army *, long, long) const { return static_cast<int32_t>(defense); }
 long getSpeedValue(const army *, long, long) const { return static_cast<int32_t>(speed); }
 long getAttackSkillValue(const army *, const army *, long, long) const { return static_cast<int32_t>(attack); }
 long getPrayerValue(const army *, type_enchant_data) const;
};
long type_AI_spellcaster::getPrayerValue(const army* ourArmy, type_enchant_data caster) const
{
    long bonus = akSpellTraits[SPELL_PRAYER].m_mastery_bonus[caster.m_mastery];
    const army* target = ourArmy->getAITarget();
    long value = getDefenseSkillValue(ourArmy, caster.m_duration, bonus);
    value += getSpeedValue(ourArmy, bonus, caster.m_duration);
    if (target != 0
            && ourArmy->getAITargetTime() == 1)
        value += getAttackSkillValue(ourArmy, target, caster.m_duration, bonus);
    return value;
}
}
