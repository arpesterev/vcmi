// SPDX-License-Identifier: CC0-1.0
// Verbatim reconstructed bodies: sushi-shi/homm3-decomp
// pin e12906accf96c2662b1abadbc49ddc111991bf21. See SOURCE_NOTICE.md.
// include/army.h:1324
inline bool army::isIncapacitated() const
    {
        return m_spellInfluence[62] || m_spellInfluence[70]
               || m_spellInfluence[74];
    }

// include/army.h:1343
inline bool army::cannotAttack() const
    {
        return isIncapacitated() || is(creatureImmobilized)
               || m_creatureType == ARMY_CREATURE_FIRST_AID_TENT
               || m_creatureType == ARMY_CREATURE_AMMO_CART;
    }

// src/army.cpp:2384
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

// src/army.cpp:2399
void army::setAIExpectedDamage(long arg)
{
    AI_expected_damage = min(arg, getTotalHitPoints(0));
}

// src/ai.cpp:2196
long combatManager::simulateActions(std::vector<army*>& list, long i,
                                     long ourGroup)
{
    type_AI_combat_parameters data(this, ourGroup);

    for (; i < list.size(); i++) {
        army* currentArmy = list[i];
        if (currentArmy->cannotAttack()
            || currentArmy->getSpellTime(59)
            || currentArmy->m_creatureType == CREATURE_CATAPULT
            || currentArmy->getTotalHitPoints(1) == 0)
            continue;
        if (currentArmy->getControllingSide() != ourGroup)
            return i;

        unsigned char shooting = currentArmy->canShoot(0);
        if (shooting) {
            chooseShooterAction(currentArmy, 1, ourGroup);
            if (m_nextAction != AI_ORDER_SHOOT)
                continue;
        } else {
            chooseMeleeAction(currentArmy, 0, 1, ourGroup);
            if (m_nextAction != AI_ORDER_MOVE_AND_ATTACK)
                continue;
        }

        long hex = m_nextActionGridIndex;
        if (!validHex(hex))
            continue;
        army* target = m_cells[hex].getArmy();
        if (!target)
            continue;
        if (shooting) {
            simulateSimpleAttack(currentArmy, target, 0, 1, 0);
        } else {
            simulateMeleeAttack(currentArmy, target, ourGroup);
        }
    }
    return i;
}
