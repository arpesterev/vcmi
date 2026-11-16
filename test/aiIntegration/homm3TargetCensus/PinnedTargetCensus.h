// SPDX-License-Identifier: CC0-1.0
// Exact pinned function bodies; input-only doubles supply original source fields.
#pragma once
#include <cstdint>
namespace PinnedTargetCensus
{
struct army
{
    int index = 0;
    int speed = 0;
    bool shooting = false;
    army * AI_target = nullptr;
    long AI_target_value = 0;
    long AI_target_distance = 0;
    long AI_possible_targets = 0;
    int getSpeed() const { return speed; }
    bool canShoot(int) const { return shooting; }
    long getAITargetTime() const { return getAITargetTime(getSpeed()); }
    long getAITargetTime(long speed) const;
    void considerAttack(const army * enemy, long value, long attackDistance);
};
inline void army::considerAttack(const army* enemy, long value, long attackDistance)
{
    long turns;
    if (!getSpeed()) {
        turns = 1;
    } else {
        turns = (attackDistance + getSpeed() - 1) / getSpeed();
        if (turns < 1)
            turns = 1;
    }
    if (turns == 1)
        AI_possible_targets |= 1 << enemy->index;
    if (AI_target) {
        long current = getAITargetTime();
        if (turns > current)
            return;
        if (turns == current && value <= AI_target_value)
            return;
    }
    AI_target = const_cast<army*>(enemy);
    AI_target_value = value;
    AI_target_distance = attackDistance;
}

inline long army::getAITargetTime(long speed) const
{
    if (canShoot(0))
        return 1;
    if (!speed)
        return 1;
    long turns = (AI_target_distance + speed - 1) / speed;
    if (turns < 1)
        return 1;
    return turns;
}

}
