long army::getAdjustedAttack(const army* enemy,
                               bool rangedAttack) const
{
    long attack = m_monInfo.attackSkill;
    if (rangedAttack) {
        if (m_spellInfluence[44])
            attack += m_precisionAmount;
    } else {
        if (m_spellInfluence[43])
            attack += m_bloodlustAmount;
    }
    if (m_spellInfluence[55] && enemy) {
        if ((enemy->is(creatureKing1) && m_slayerLevel >= 0)
            || (enemy->is(creatureKing2) && m_slayerLevel >= 2)
            || (enemy->is(creatureKing3) && m_slayerLevel >= 3)) {
            attack += 8;
            if (getController()) {
                hero* castingHero = getController();
                attack += castingHero->getHeroSpellBonus(55, m_monInfo.level, 8);
            }
        }
    }
    if (m_spellInfluence[56])
        return static_cast<long>(
            getAdjustedDefense(enemy, 0) * m_frenzyFactor + attack);
    return attack;
}

long army::getAdjustedDefense(const army* enemy,
                                bool frenzyIncluded) const
{
    if (frenzyIncluded && m_spellInfluence[56])
        return 0;
    long defense = m_monInfo.defenseSkill;
    if (enemy) {
        if (enemy->m_creatureType == ARMY_CREATURE_BEHEMOTH)
            defense = static_cast<long>(defense - defense * 0.4f);
        else if (enemy->m_creatureType == ARMY_CREATURE_ANCIENT_BEHEMOTH)
            defense = static_cast<long>(defense - defense * 0.8f);
    }
    if (g_combatManager->m_moatOn) {
        int secondHex;
        if (is(creatureDoubleWide))
            secondHex = m_gridIndex + offsetToFront(-1);
        else
            secondHex = -1;
        if (g_combatManager->isInMoat(m_gridIndex, 0)
            || g_combatManager->isInMoat(secondHex, 0))
            defense -= 3;
    }
    return defense;
}

long army::getAttackModifier(const army* enemy,
                               bool rangedAttack) const
{
    long adjusted = getAdjustedAttack(enemy, rangedAttack);
    return adjusted - akCreatureTypeTraits[m_creatureType].attackSkill;
}

long army::getDefenseModifier() const
{
    long adjusted = getAdjustedDefense(0, 1);
    return adjusted - akCreatureTypeTraits[m_creatureType].defenseSkill;
}

inline double army::getDefenseDamageModifier(
    unsigned char rangedAttack) const
{
    return 1.0;
}

double army::getAverageDamage() const
{
    if (m_spellInfluence[41])
        return m_blessAmount + m_monInfo.damageHighBound;
    if (m_spellInfluence[42])
        return cppMax(m_monInfo.damageLowBound - m_curseAmount, 1);
    return (m_monInfo.damageHighBound + m_monInfo.damageLowBound) / 2.0;
}

long army::getAverageDamage(const army& enemy, bool rangedAttack, long amount, bool limitDamage, long distance) const
{
    double average = getAverageDamage();
    long damage = adjustDamage(const_cast<army*>(&enemy),
                                static_cast<long>(amount * average),
                                rangedAttack, 1, distance, 0);
    long totalLife = enemy.getTotalHitPoints(0);
    if (damage < 1)
        damage = 1;
    if (rangedAttack && is(creatureTwoAttacks))
        damage += damage;
    if (limitDamage && damage > totalLife)
        damage = totalLife;
    return damage;
}

int army::computeAttackerBonus(int baseDamage, bool isShooting,
                                 army* defender, bool announce,
                                 long distance) const
{
    int bonus = 0;
    if ((m_creatureType == CREATURE_CAVALIER
         || m_creatureType == CREATURE_CHAMPION)
        && defender->m_creatureType != 0 && defender->m_creatureType != 1) {
        bonus = static_cast<long>(static_cast<double>(baseDamage)
                                  * distance * 0.05);
    }
    if (m_luckStatus > 0)
        bonus += baseDamage;

    long attack = getAdjustedAttack(defender, isShooting);
    long defense = defender->getAdjustedDefense(this, 1);
    if (attack >= defense) {
        double factor = (attack - defense) * 0.05;
        if (factor > 3.0)
            factor = 3.0;
        bonus = static_cast<long>(baseDamage * factor + bonus);
    }

    if (defender != 0) {
        switch (m_creatureType) {
        case CREATURE_EARTH_ELEMENTAL:
        case CREATURE_MAGMA_ELEMENTAL:
            if (defender->m_creatureType == CREATURE_AIR_ELEMENTAL
                || defender->m_creatureType == CREATURE_STORM_ELEMENTAL)
                bonus += baseDamage;
            break;
        case CREATURE_AIR_ELEMENTAL:
        case CREATURE_STORM_ELEMENTAL:
            if (defender->m_creatureType == CREATURE_EARTH_ELEMENTAL
                || defender->m_creatureType == CREATURE_MAGMA_ELEMENTAL)
                bonus += baseDamage;
            break;
        case CREATURE_WATER_ELEMENTAL:
        case CREATURE_ICE_ELEMENTAL:
            if (defender->m_creatureType == CREATURE_FIRE_ELEMENTAL
                || defender->m_creatureType == CREATURE_ENERGY_ELEMENTAL)
                bonus += baseDamage;
            break;
        case CREATURE_FIRE_ELEMENTAL:
        case CREATURE_ENERGY_ELEMENTAL:
            if (defender->m_creatureType == CREATURE_WATER_ELEMENTAL
                || defender->m_creatureType == CREATURE_ICE_ELEMENTAL)
                bonus += baseDamage;
            break;
        }

        unsigned char hates =
            isNaturalEnemy(m_creatureType, defender->m_creatureType);
        if (hates) {
            bonus += baseDamage / 2;
            if (announce) {
                if (!g_combatManager->isQuickCombat()) {
                    std::string text;
                    if (m_numTroops == 1)
                        text = formatString(
                            g_generalText->GetText(GENERAL_TEXT_HATRED_DAMAGE_ONE_FORMAT),
                            getName(),
                            defender->getName());
                    else
                        text = formatString(
                            g_generalText->GetText(GENERAL_TEXT_HATRED_DAMAGE_MANY_FORMAT),
                            getName(),
                            defender->getName());
                    g_combatManager->m_combatWindow->combatMessage(
                        text.c_str(), 1, 0);
                }
            }
        }
    }

    long total = bonus;
    hero* controller = getController();
    if (controller != 0) {
        if (isShooting)
            total = static_cast<long>(
                controller->getArcheryFactor()
                    * static_cast<float>(baseDamage)
                + static_cast<float>(bonus));
        else
            total = static_cast<long>(
                controller->getOffenseFactor()
                    * static_cast<float>(baseDamage)
                + static_cast<float>(bonus));
        if (m_spellInfluence[SPELL_BLESS])
            total += controller->getHeroSpellBonus(SPELL_BLESS,
                                                   m_monInfo.level,
                                                   baseDamage);
    }
    return total;
}

int army::computeAttackerDamageBonuses(int baseDamage,
                                       bool isShooting,
                                       army* defender,
                                       bool simulateOnly,
                                       long distance) const
{
    int result = computeAttackerBonus(baseDamage, isShooting, defender,
                                        simulateOnly == 0, distance);
    switch (m_creatureType) {
    case ARMY_CREATURE_BALLISTA: {
        hero* controlling = getController();
        long mastery =
            controlling->getSecondarySkill(eSecSkillBattlefieldBallistics);
        if (!simulateOnly
            && sRandom(1, 100) <= g_artilleryDoubleChances[mastery]) {
            result += baseDamage;
            if (!g_combatManager->isQuickCombat()) {
                std::string text;
                const char* creatureName;
                creatureName = getName();
                text = formatString((*g_generalText)[GENERAL_TEXT_DOUBLE_DAMAGE_ONE_FORMAT],
                                     creatureName);
                g_combatManager->m_combatWindow->combatMessage(
                    text.c_str(), 1, 0);
            }
        }
        break;
    }

    case CREATURE_DREAD_KNIGHT:
        if (!simulateOnly && sRandom(1, 100) <= 20) {
            result += baseDamage;
            if (!g_combatManager->isQuickCombat()) {
                std::string text;
                if (m_numTroops == 1)
                    text = formatString((*g_generalText)[GENERAL_TEXT_DOUBLE_DAMAGE_ONE_FORMAT],
                                         getName());
                else
                    text = formatString((*g_generalText)[GENERAL_TEXT_DOUBLE_DAMAGE_MANY_FORMAT],
                                         getName());
                g_combatManager->m_combatWindow->combatMessage(
                    text.c_str(), 1, 0);
                SAMPLE2 sample = loadPlaySample(
                    DATA_COMPGEN(0x00660a50, deathBlowSampleName,
                                 "Deathblo.wav"));
                g_combatManager->spellEffect(
                    eSpellEffectDeathBlow, defender, 100,
                    0);
                waitEndSample(sample, -1);
            }
        }
        break;
    }
    return result;
}

inline int army::computeDefenderDamageBonuses(int baseDamage) const
{
    return 0;
}

double army::computeAttackerDamageReduction(const army* defender,
                                            bool isShooting) const
{
    double reduction = 1.0;
    long attack = getAdjustedAttack(defender, isShooting);
    long defense = defender->getAdjustedDefense(this, 1);
    if (defense > attack) {
        double factor = 1.0 - (defense - attack) * 0.025;
        if (factor < 0.3)
            factor = 0.3;
        reduction = factor;
    }
    if (m_creatureType == ARMY_CREATURE_PSYCHIC_ELEMENTAL
            && defender->is(creatureImmuneToMindSpells))
        reduction *= 0.5;
    if (m_creatureType == ARMY_CREATURE_MAGIC_ELEMENTAL
            && (defender->m_creatureType == ARMY_CREATURE_MAGIC_ELEMENTAL
                || defender->m_creatureType == ARMY_CREATURE_BLACK_DRAGON))
        reduction *= 0.5;
    if (isShooting) {
        int hex = m_gridIndex;
        if (is(creatureDoubleWide))
            hex = getSecondGridIndex();
        if (g_combatManager->shotIsThroughWall(this, hex, defender->m_gridIndex))
            reduction *= 0.5;
        if (g_combatManager->shotIsNotOptimal(this, defender))
            reduction *= 0.5;
    }
    if (is(creatureShootingArmy) && !isShooting && !is(creatureNoMeleePenalty))
        reduction *= 0.5;
    if (m_residualBlindness && m_residualParalyze) {
        double penalty = min(
            m_blindFactor, akSpellTraits[SPELL_BLIND].m_mastery_bonus[2] / 100.0);
        reduction = penalty * reduction;
    } else if (m_residualBlindness)
        reduction = m_blindFactor * reduction;
    else if (m_residualParalyze)
        reduction = akSpellTraits[SPELL_BLIND].m_mastery_bonus[2] / 100.0
                    * reduction;
    return reduction;
}

double army::computeDefenderDamageReduction(bool isShooting) const
{
    double reduction = getDefenseDamageModifier(isShooting);
    if (isShooting) {
        if (m_spellInfluence[28])
            reduction *= m_airShieldFactor;
    } else {
        if (m_spellInfluence[27])
            reduction *= m_shieldFactor;
    }
    if (m_spellInfluence[70])
        reduction *= 0.5;
    hero* castingHero = getController();
    if (castingHero)
        reduction *= castingHero->getDefenseFactor();
    return reduction;
}

long army::adjustDamage(army* enemy, long baseDamage, bool isShot,
                         bool simulated, long distance,
                         long* fireDamage) const
{
    if (fireDamage)
        *fireDamage = 0;
    if (!enemy)
        return 0;
    int totalDamage = baseDamage
        + computeAttackerDamageBonuses(
            baseDamage, isShot, enemy, simulated, distance)
        + enemy->computeDefenderDamageBonuses(baseDamage);
    if (fireDamage) {
        *fireDamage = g_combatManager->computeFireShieldDamage(
            totalDamage, this, enemy, enemy->getTotalHitPoints(0));
    }
    totalDamage = static_cast<long>(
        totalDamage * computeAttackerDamageReduction(enemy, isShot));
    totalDamage = static_cast<long>(
        totalDamage * enemy->computeDefenderDamageReduction(isShot));
    if (totalDamage <= 0)
        totalDamage = 1;
    return totalDamage;
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

long aiGetAttackDamage(const army& currentArmy, long ourHits, const army& enemy, bool ranged, long distance)
{
    long troops = (currentArmy.m_monInfo.hitPoints + ourHits - 1)
                  / currentArmy.m_monInfo.hitPoints;
    return currentArmy.getAverageDamage(enemy, ranged, troops, 1, distance);
}

void type_AI_combat_parameters::simulateSingleAttack(const army& currentArmy, long& ourHits, const army& enemy, long& enemyHits, bool ranged, long distance) const
{
    // Mac 0x3d054 retains this call; VC6 expands the same source helper.
    long damage = aiGetAttackDamage(currentArmy, ourHits, enemy, ranged,
                                   distance);
    if (!ranged) {
        long fire = g_combatManager->computeFireShieldDamage(
            damage, &currentArmy, &enemy, enemyHits);
        if (fire > 0) {
            ourHits -= fire;
            if (ourHits < 0)
                ourHits = 0;
        }
    }
    enemyHits -= damage;
    if (enemyHits < 0)
        enemyHits = 0;
}

void type_AI_combat_parameters::simulateAttack(const army& currentArmy, long& ourHits, const army& enemy, long& enemyHits, bool ranged, long distance) const
{
    if (ranged)
        ranged = currentArmy.canShoot(0);
    simulateSingleAttack(currentArmy, ourHits, enemy, enemyHits, ranged, distance);
    if (ourHits == 0 || enemyHits <= 0 || ranged)
        return;
    if (enemy.canRetaliate(currentArmy)
            && (g_game->m_setup.m_difficulty > 0 || g_combatManager->IsHuman[m_ourGroup])) {
        simulateSingleAttack(enemy, enemyHits, currentArmy, ourHits, 0, 0);
        if (ourHits == 0 || enemyHits == 0)
            return;
    }
    if (currentArmy.is(creatureTwoAttacks))
        simulateSingleAttack(currentArmy, ourHits, enemy, enemyHits, 0, 0);
}

long type_AI_combat_parameters::getSimpleAttackEffect(const army& currentArmy, long ourTotal, const army& enemy, long enemyTotal, bool ranged, long distance) const
{
    long ourHits;
    long enemyHits;

    if (ranged)
        ranged = currentArmy.canShoot(0);
    if (m_simulated) {
        ourTotal -= currentArmy.getAIExpectedDamage();
        if (ourTotal <= 0)
            return 0;
        enemyTotal -= enemy.getAIExpectedDamage();
        if (enemyTotal <= 0)
            return 0;
    }
    ourHits = ourTotal;
    enemyHits = enemyTotal;
    simulateAttack(currentArmy, ourHits, enemy, enemyHits, ranged, distance);
    long value = enemy.getLossCombatValue(m_lowestAttack, m_lowestDefense, ranged,
                                              enemyTotal - enemyHits, m_killsOnly);
    if (ourTotal > ourHits)
        value -= currentArmy.getLossCombatValue(m_lowestAttack, m_lowestDefense,
                                                     ranged, ourTotal - ourHits, 0);
    return value;
}

long type_AI_combat_parameters::getSimpleAttackEffect(const army& currentArmy, const army& enemy, bool ranged, long distance) const
{
    long ourTotal = currentArmy.getTotalHitPoints(0);
    long enemyTotal = enemy.getTotalHitPoints(0);
    return getSimpleAttackEffect(currentArmy, ourTotal, enemy, enemyTotal, ranged, distance);
}
