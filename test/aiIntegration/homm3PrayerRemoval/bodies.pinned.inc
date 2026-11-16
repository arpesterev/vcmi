long type_AI_spellcaster::getCancelValue(army* currentArmy, unsigned char badSpellsOnly) const
{
    long value = 0;
    for (long spell = 10; spell < kNumSpellsAndCreatureEffects; spell++) {
        long duration = currentArmy->getSpellTime(spell);
        if (duration == 0)
            continue;
        if (badSpellsOnly) {
            if (akSpellTraits[spell].m_karma >= 0)
                continue;
        } else {
            if (spell == SPELL_POISON)
                continue;
        }
        TEnchantValue valueOf = getEnchantmentFunction(spell);
        if (valueOf == 0)
            continue;
        type_enchant_data caster(static_cast<ESpellId>(spell), /* HOMM3_ENUM_CAST_REVISION_BOUNDARY */
                                 currentArmy->getSpellLevel(spell),
                                 duration, duration);
        caster.m_checkResistance = 0;
        currentArmy->cancelIndividualSpell(spell);
        long ours = currentArmy->getOwningSide() == m_side;
        long bad = akSpellTraits[spell].m_karma < 0;
        if (bad == ours)
            value += (m_enemyCaster->*valueOf)(currentArmy, caster);
        else
            value -= (this->*valueOf)(currentArmy, caster);
    }
    return value;
}

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

long type_AI_spellcaster::getDefenseSkillValue(const army* ourArmy, long duration, long bonus) const
{
    const army* enemy = worst_enemies[ourArmy->index].m_enemy;
    if (!enemy)
        return 0;
    army testArmy = *ourArmy;
    testArmy.m_monInfo.defenseSkill += bonus;
    bool ranged = enemy->canShoot(0);
    double oldDamage = enemy->getEstimatedDamage(ourArmy, 100, ranged, 0);
    double newDamage = enemy->getEstimatedDamage(&testArmy, 100, ranged, 0);
    double increase = oldDamage / newDamage;
    return getDefenseBoostValue(ourArmy, enemy, duration, increase);
}

long type_AI_spellcaster::getDefenseBoostValue(const army* ourArmy, const army* enemy, long duration, double increase) const
{
    long damage = enemy->getAverageDamage(*ourArmy, enemy->canShoot(0),
                                            enemy->m_numTroops, 0, 0);
    long reduced = static_cast<long>(static_cast<double>(damage) / increase);
    long hits = ourArmy->getTotalHitPoints(0);
    if (damage > hits) {
        increase = static_cast<double>(hits) / static_cast<double>(reduced);
        damage = hits;
    }
    if (reduced >= damage)
        return 0;
    if (m_winLikely) {
        if (ourArmy->getAIExpectedDamage() + ourArmy->m_topCreatureDamage
                < ourArmy->m_monInfo.hitPoints)
            return 0;
    }
    if ((melee_enemies[ourArmy->index].m_totalDamage
                + ranged_enemies[ourArmy->index].m_totalDamage) * m_estimate.m_roundsLeft
            + ourArmy->m_topCreatureDamage < ourArmy->m_monInfo.hitPoints)
        return 0;
    double scale = getDuration(duration, 0);
    return static_cast<long>(scale * ourArmy->getTotalCombatValue(m_estimate.m_lowestAttack,
        m_estimate.m_lowestDefense) * (sqrt(increase) - 1.0));
}

long type_AI_spellcaster::getAttackSkillValue(const army* ourArmy, const army* enemy, long duration, long bonus) const
{
    if (m_winLikely)
        return 0;
    army testArmy = *ourArmy;
    testArmy.m_monInfo.attackSkill += bonus;
    bool ranged = ourArmy->canShoot(0);
    double oldDamage = ourArmy->getEstimatedDamage(enemy, 100, ranged, 0);
    double newDamage = testArmy.getEstimatedDamage(enemy, 100, ranged, 0);
    double increase = newDamage / oldDamage;
    return getAttackBoostValue(ourArmy, enemy, duration, increase);
}

long type_AI_spellcaster::getAttackBoostValue(const army* ourArmy,
    const army* enemy, long oldDamage, long duration, double increase) const
{
    long newDamage = static_cast<long>(oldDamage * increase);
    long enemyHits = enemy->getTotalHitPoints(0);
    if (newDamage > enemyHits) {
        newDamage = enemyHits;
        increase = static_cast<double>(enemyHits) / oldDamage;
    }
    if (newDamage <= oldDamage)
        return 0;
    double modifier = getDuration(duration, ourArmy->is(creatureDone));
    double total = static_cast<double>(ourArmy->getTotalCombatValue(
        m_estimate.m_lowestAttack, m_estimate.m_lowestDefense));
    return static_cast<long>((sqrt(increase) - 1.0) * total * modifier);
}

long type_AI_spellcaster::getAttackBoostValue(const army* ourArmy,
    const army* enemy, long duration, double increase) const
{
    long oldDamage = ourArmy->getAverageDamage(*enemy, ourArmy->canShoot(0),
        ourArmy->m_numTroops, 1, 0);
    return getAttackBoostValue(ourArmy, enemy, oldDamage, duration, increase);
}

long type_AI_spellcaster::getSpeedValue(const army* ourArmy, long increase, long duration) const
{
    if (ourArmy->getAITarget() == 0)
        return 0;
    if (m_winLikely)
        return 0;
    long turns = duration;
    if (ourArmy->is(creatureDone))
        turns = duration - 1;
    if (turns == 0)
        return 0;
    long value = 0;
    long oldSpeed = ourArmy->getSpeed();
    long newSpeed = ourArmy->m_baseSpeed + increase;
    long oldTime = ourArmy->getAITargetTime();
    long newTime = ourArmy->getAITargetTime(newSpeed);
    if (newTime > m_estimate.m_roundsLeft)
        return 0;
    if (newTime == 1) {
        const army* target = ourArmy->getAITarget();
        if (target->getSpeed() >= oldSpeed && target->getSpeed() < newSpeed) {
            value = getMoveOrderChangeValue(ourArmy);
            if (value < 0)
                value = 0;
        }
    }
    if (newTime < oldTime) {
        if (oldTime > m_estimate.m_roundsLeft + 1)
            oldTime = m_estimate.m_roundsLeft + 1;
        long total = ourArmy->getTotalCombatValue(m_estimate.m_lowestAttack,
                                                      m_estimate.m_lowestDefense);
        value += (m_estimate.m_roundsLeft - newTime + 1) * total / m_estimate.m_roundsLeft
                 - (m_estimate.m_roundsLeft - oldTime + 1) * total / m_estimate.m_roundsLeft;
    }
    return value;
}

double type_AI_spellcaster::getDuration(long turns, unsigned char movedThisTurn) const
{
    double result;
    if (turns >= m_estimate.m_roundsLeft)
        result = 1.0;
    else
        result = static_cast<double>(turns) / m_estimate.m_roundsLeft;
    if (movedThisTurn) {
        result -= 1.0 / m_estimate.m_roundsLeft;
        if (result < 0.0)
            return 0.0;
    }
    return result;
}
