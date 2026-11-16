type_AI_combat_parameters::type_AI_combat_parameters(const combatManager* combat, long side)
{
    unsigned char first = 1;
    this->m_ourGroup = side;
    m_enemyGroup = 1 - side;
    m_lowestAttack = 0;
    m_lowestDefense = 0;
    m_killsOnly = 0;
    m_simulated = 0;
    for (long group = 0; group < 2; group++) {
        for (long i = 0; i < combat->m_numArmies[group]; i++) {
            const army* ourArmy = &combat->m_armies[group][i];
            if (ourArmy->m_numTroops <= 0)
                continue;
            if (ourArmy->m_creatureType == CREATURE_ARROW_TOWER)
                continue;
            bool ranged = ourArmy->canShoot(0);
            long attack = ourArmy->getAttackModifier(0, ranged);
            long defense = ourArmy->getDefenseModifier();
            if (first || m_lowestAttack > attack)
                m_lowestAttack = attack;
            if (first || m_lowestDefense > defense)
                m_lowestDefense = defense;
            first = 0;
        }
    }
    // Direct group fields: DC's four get_total_combat_value calls have no
    // get_group/get_enemy_group calls.
    m_friendlyCombatValue = combat->getTotalCombatValue(this->m_ourGroup, m_lowestAttack,
                                               m_lowestDefense, 1);
    m_awakeFriendlyValue = combat->getTotalCombatValue(this->m_ourGroup, m_lowestAttack,
                                                    m_lowestDefense, 0);
    m_enemyCombatValue = combat->getTotalCombatValue(m_enemyGroup, m_lowestAttack,
                                                 m_lowestDefense, 1);
    m_awakeEnemyValue = combat->getTotalCombatValue(m_enemyGroup, m_lowestAttack,
                                                      m_lowestDefense, 0);
    if (m_friendlyCombatValue * 2 < m_enemyCombatValue
            && (g_game->m_setup.m_difficulty > 0
                || g_combatManager->IsHuman[this->m_ourGroup]))
        m_killsOnly = 1;
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
}

unsigned char type_AI_spellcaster::isLastAction() const
{
    const army* current = g_combatManager->getCurrentArmy();
    for (long j = 0; j < g_combatManager->m_numArmies[m_side]; j++) {
        const army* other = &g_combatManager->m_armies[m_side][j];
        if (other->is(creatureSiegeWeapon | creatureImmobilized) || other->isIncapacitated())
            continue;
        if (other->is(creatureDone))
            continue;
        if (other != current)
            return 0;
    }
    return 1;
}
