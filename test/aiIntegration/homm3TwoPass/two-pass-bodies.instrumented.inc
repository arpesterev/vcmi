// Exact reconstructed-source bodies; see source-manifest.json

long combatManager::getTotalCombatValue(long side, long lowestAttack, long lowestDefense, bool includeCripples) const
{
    long total = 0;
    const army* currentArmy = m_armies[side];
    for (long i = 0; i < m_numArmies[side]; i++, currentArmy++) {
        if (currentArmy->is(creatureImmobilized) || currentArmy->m_creatureType == CREATURE_ARROW_TOWER)
            continue;
        if (!includeCripples && currentArmy->cannotAttack())
            continue;
        total += currentArmy->getTotalCombatValue(lowestAttack, lowestDefense);
    }
    return total;
}

long combatManager::sourceChooseShooterTarget(const army* currentArmy, type_AI_combat_parameters& data, long& bestValue) const
{
    long bestTarget = -1;
    long hex;
    long ourGroup = data.getGroup();
    long enemyGroup = data.getEnemyGroup();
    unsigned char isAreaEffect = 0;
    const army* bestArmy = 0;
    std::vector<army*> targets;

    if (currentArmy->m_creatureType == CREATURE_MAGOG
            || currentArmy->m_creatureType == CREATURE_LICH
            || currentArmy->m_creatureType == CREATURE_POWER_LICH)
        isAreaEffect = 1;

    for (long i = 0; i < m_numArmies[enemyGroup]; i++) {
        const army* target = &m_armies[enemyGroup][i];
        if (target->is(creatureImmobilized) || target->m_creatureType == CREATURE_ARROW_TOWER)
            continue;
        if (data.m_simulated && target->getTotalHitPoints(1) == 0)
            continue;

        hex = target->m_gridIndex;
        long value;
        if (isAreaEffect) {
            value = getAreaAttackValue(*currentArmy, hex, ourGroup, data);
            if (target->is(creatureDoubleWide)) {
                long secondValue = getAreaAttackValue(
                    *currentArmy, target->getSecondGridIndex(), ourGroup,
                    data);
                if (secondValue > value) {
                    hex = target->getSecondGridIndex();
                    value = secondValue;
                }
            }
            if (value < 0)
                continue;
        } else {
            value = data.getRangedAttackValue(*(currentArmy), *(target));
        }

        if (bestArmy) {
            if (target->isIncapacitated()
                    && !bestArmy->isIncapacitated())
                continue;
            if ((target->isIncapacitated() || !bestArmy->isIncapacitated())
                    && value < bestValue)
                continue;
        }
        bestArmy = target;
        bestValue = value;
        bestTarget = hex;
    }
    return bestTarget;
}

void combatManager::sourceChooseShooterAction(const army* currentArmy, bool simulated, long side)
{
    long bestValue = 0;
    type_AI_combat_parameters data(this, side);
    data.m_simulated = simulated;
    findAITargets(1 - side, 0, 0, &data, 0);
    long actionValue = chooseShooterTarget(currentArmy, data, bestValue);
    if (!simulated && currentArmy->is(creatureCatapult)
            && chooseCyclopsAction(bestValue, side, data))
        return;
    if (actionValue < 0) {
        m_nextAction = 3;
        return;
    }
    if (data.m_killsOnly && !bestValue) {
        data.m_killsOnly = 0;
        actionValue = chooseShooterTarget(currentArmy, data, bestValue);
    }
    m_nextAction = 7;
    m_nextActionGridIndex = actionValue;
}

static long getMoveOrder(const army* currentArmy)
{
    if (currentArmy->m_creatureType == CREATURE_FIRST_AID_TENT
            || currentArmy->m_creatureType == CREATURE_AMMO_CART)
        return -100000;
    if (currentArmy->getSpellTime(62) > 1 || currentArmy->getSpellTime(70) > 1)
        return -10000;
    if (currentArmy->is(creatureDone)
            || const_cast<army*>(currentArmy)->isIncapacitated())
        return currentArmy->getSpeed() - 1000;
    if (currentArmy->is(creatureWaiting) || g_combatManager->isInSecondPhase())
        return -currentArmy->getSpeed();
    return currentArmy->getSpeed();
}

unsigned char func_moves_before::operator()(const army* a, const army* b)
{
    if (a->m_expectedMoveOrder > b->m_expectedMoveOrder)
        return true;
    if (a->m_expectedMoveOrder < b->m_expectedMoveOrder)
        return false;
    return a->index < b->index;
}

void combatManager::findMoveOrder(std::vector<army*>* result)
{
    std::vector<army*> order;
    for (long side = 0; side < 2; side++) {
        for (long i = 0; i < m_numArmies[side]; i++) {
            army* currentArmy = &m_armies[side][i];
            if (currentArmy->m_creatureType == CREATURE_ARROW_TOWER)
                continue;
            if (currentArmy->is(creatureImmobilized))
                continue;
            currentArmy->m_expectedMoveOrder = getMoveOrder(currentArmy);
            order.push_back(currentArmy);
        }
    }
    std::sort(order.begin(), order.end(), func_moves_before());
    long wantSide = m_actingSide;
    for (unsigned i = 0; i < order.size(); i++) {
        if (order[i]->group != wantSide) {
            long key = order[i]->m_expectedMoveOrder;
            for (unsigned j = i + 1; j < order.size(); j++) {
                if (order[j]->m_expectedMoveOrder != key)
                    break;
                if (order[j]->group == wantSide) {
                    std::swap(order[i], order[j]);
                    break;
                }
            }
        }
        wantSide = 1 - order[i]->group;
    }
    for (unsigned k = 0; k < order.size(); k++) {
        order[k]->m_expectedMoveOrder = order.size() - k;
        if (result)
            result->push_back(order[k]);
    }
}

static void simulateSimpleAttack(army* currentArmy, army* target,
                                   long distance, unsigned char ranged,
                                   unsigned char breathAttack)
{
    long hits = currentArmy->getTotalHitPoints(1);
    if (hits <= 0)
        return;
    long damage = aiGetAttackDamage(*(currentArmy), hits, *(target), ranged, distance);
    g_combatManager->trace.back().damage = damage;
    if (!ranged && !breathAttack) {
        long targetHits = target->getTotalHitPoints(1);
        long burn = g_combatManager->computeFireShieldDamage(
            damage, currentArmy, target, targetHits);
        if (burn > 0)
            currentArmy->setAIExpectedDamage(
                currentArmy->getAIExpectedDamage() + burn);
    }
    target->setAIExpectedDamage(target->getAIExpectedDamage() + damage);
}

long combatManager::sourceSimulateActions(std::vector<army*>& list, long i,
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

void combatManager::simulateCombat(long ourGroup, unsigned char checkingSurrender)
{
    std::vector<army*> order;
    long saved3c = m_nextAction;
    long saved40 = m_nextActionExtra;
    long saved44 = m_nextActionGridIndex;
    long saved48 = m_nextActionGridIndex2;

    findMoveOrder(&order);
    for (unsigned i = 0; i < order.size(); i++)
        order[i]->setAIExpectedDamage(0);
    long stoppedAt = simulateActions(order, 0, ourGroup);
    if (checkingSurrender)
        simulateActions(order, stoppedAt, 1 - ourGroup);

    m_nextAction = saved3c;
    m_nextActionExtra = saved40;
    m_nextActionGridIndex = saved44;
    m_nextActionGridIndex2 = saved48;
}

long type_AI_combat_parameters::sourceRangedAttackValue(const army& currentArmy, const army& enemy) const
{
    long value = getSimpleAttackEffect(currentArmy, enemy, 1, 0);
    if (!g_game->m_setup.m_difficulty && !g_combatManager->IsHuman[m_ourGroup])
        return value;
    if (enemy.isIncapacitated())
        return value / 10;
    return (!enemy.cannotAttack()
                    && enemy.getAITarget() != 0
                    && enemy.getAITargetTime() <= 5)
            ? value / enemy.getAITargetTime()
            : value / 5;
}

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

long army::getTotalCombatValue(long lowestAttack, long lowestDefense) const
{
    if (m_numTroops <= 0)
        return 0;
    bool ranged = canShoot(0);
    double value = getUnitCombatValue(lowestAttack, lowestDefense,
                                         ranged, 0);
    if (is(creatureClone))
        return static_cast<long>(m_numTroops * value / 5.0);
    return static_cast<long>(getTotalHitPoints(0)
                             * value / m_monInfo.hitPoints);
}

void army::setAIExpectedDamage(long arg)
{
    AI_expected_damage = min(arg, getTotalHitPoints(0));
}
