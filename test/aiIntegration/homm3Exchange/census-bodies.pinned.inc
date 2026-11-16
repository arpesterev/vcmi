void army::considerAttack(const army* enemy, long value, long attackDistance)
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

long army::getAITargetTime(long speed) const
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

void combatManager::findAITargets(long ourGroup, const army* currentArmy,
                                    unsigned char meleeOnly,
                                    const type_AI_combat_parameters* data,
                                    searchArray* currentSearchArray)
{
    long enemyGroup = 1 - ourGroup;
    if (currentSearchArray == 0)
        currentSearchArray = g_searchArray;

    long i;
    long j;
    army* ours = m_armies[ourGroup];
    army* theirs;
    bool shooter;
    for (i = 0; i < m_numArmies[ourGroup]; i++, ours++) {
        ours->clearAIValues();
        if (ours->is(creatureImmobilized))
            continue;
        if (ours->is(creatureSiegeWeapon)
            && ours->m_creatureType != CREATURE_BALLISTA)
            continue;
        if (data->m_simulated && ours->getTotalHitPoints(1) == 0)
            continue;
        if (ours->getSpellTime(70) > 1)
            continue;
        if (ours->getSpellTime(62) > 1)
            continue;
        if (ours->getSpellTime(60))
            continue;
        if (ours->getSpellTime(59))
            continue;
        if (ours == currentArmy)
            continue;

        shooter = ours->canShoot(0);
        if (shooter && meleeOnly)
            continue;
        if (!shooter) {
            if (meleeOnly)
                currentSearchArray->seedCombatPosition(
                    ours, ourGroup, ours->getSpeed(), m_creaturePlacement, -1);
            else
                currentSearchArray->seedCombatPosition(
                    ours, ourGroup, 0x7f, m_creaturePlacement, -1);
        }

        theirs = m_armies[enemyGroup];
        for (j = 0; j < m_numArmies[enemyGroup]; j++, theirs++) {
            if (theirs->m_creatureType == CREATURE_ARROW_TOWER)
                continue;
            if (meleeOnly && theirs->m_expectedMoveOrder >= ours->m_expectedMoveOrder)
                continue;
            if (!shooter && !m_cells[theirs->m_gridIndex].m_validMove)
                continue;
            if (data->m_simulated && theirs->getTotalHitPoints(1) == 0)
                continue;

            long time;
            if (shooter) {
                time = 1;
            } else {
                time = currentSearchArray->getHex(theirs->m_gridIndex)->m_cost;
                if (ours->getSpeed() == 0 && time > 0)
                    continue;
            }
            long effect;
            if (time > ours->getSpeed())
                effect = data->getSimpleAttackEffect(*(ours), *(theirs), shooter, 0);
            else
                effect = data->getSimpleAttackEffect(*(ours), *(theirs), shooter, time);
            ours->considerAttack(theirs, effect, time);
        }
    }
}
