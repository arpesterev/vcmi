static long getAttackValue(const army* currentArmy, const army* enemy,
                           long enemyHitPoints, type_AI_combat_parameters& data)
{
    if (enemyHitPoints <= 0)
        return 0;
    long currentHits = currentArmy->getTotalHitPoints(data.m_simulated);
    long damage = aiGetAttackDamage(*currentArmy, currentHits, *enemy, 0, 0);
    if (damage > enemyHitPoints)
        damage = enemyHitPoints;
    double combatValue = enemy->getUnitCombatValue(data.m_lowestAttack,
                                                   data.m_lowestDefense, 0, 0);
    if (data.m_killsOnly) {
        return ((enemyHitPoints % enemy->m_monInfo.hitPoints + damage)
                / enemy->m_monInfo.hitPoints) * combatValue;
    }
    return damage * combatValue / enemy->m_monInfo.hitPoints;
}

long combatManager::getAttackChange(const army* currentArmy, const army* enemy, type_AI_combat_parameters& data)
{
    if (enemy->getSpellTime(70) || enemy->m_retaliationCount == 0)
        return 0;
    if (enemy->m_retaliationCount > 1)
        return 0;
    long enemyHits = enemy->getTotalHitPoints(data.m_simulated);
    long ourHits = currentArmy->getTotalHitPoints(data.m_simulated);
    long committed = 0;
    long bestOther = 0;
    enemyHits -= aiGetAttackDamage(*currentArmy, ourHits, *enemy, 0, 0);
    if (enemyHits <= 0)
        return 0;
    const army* friendly = m_armies[m_currentSide];
    for (long i = 0; i < m_numArmies[m_currentSide]; i++, friendly++) {
        if (friendly->m_creatureType == CREATURE_ARROW_TOWER)
            continue;
        if ((friendly->getAIPossibleTargets() & (1 << enemy->index)) == 0)
            continue;
        long change = getAttackValue(friendly, enemy, enemyHits, data);
        change -= friendly->getAITargetValue();
        if (friendly->getAITarget() == enemy && change > 0)
            committed += change;
        else if (change > bestOther)
            bestOther = change;
    }
    return committed + bestOther;
}

static long getEnemyAttackLimit(const army* ourArmy,
                                const type_AI_combat_parameters& estimate)
{
    long hitPoints = ourArmy->getTotalHitPoints(estimate.m_simulated);
    return -ourArmy->getLossCombatValue(
        estimate.m_lowestAttack, estimate.m_lowestDefense,
        ourArmy->canShoot(0), hitPoints, 0);
}

void combatManager::markFriendlyArmies(const army* ourArmy, long* enemyAttacks, long markedEnemies, const type_AI_combat_parameters& estimate) const
{
    long enemySide = estimate.getEnemyGroup();
    long areaEffect = getAreaEffect(enemySide, ourArmy, markedEnemies,
                                       estimate);
    unsigned char checked[COMBAT_GRID_CELLS];
    memset(checked, 0, COMBAT_GRID_CELLS);
    long floorValue = getEnemyAttackLimit(ourArmy, estimate);
    const army* friendly = m_armies[estimate.getGroup()];
    for (long i = 0; i < m_numArmies[estimate.getGroup()]; i++, friendly++) {
        if (friendly->is(creatureImmobilized))
            continue;
        if (friendly->m_creatureType == CREATURE_ARROW_TOWER)
            continue;
        if (friendly == ourArmy)
            continue;
        long meleeValue;
        if (friendly->is(creatureHasExtendedAttack)) {
            long damage = friendly->getAverageDamage(*ourArmy, 0,
                                                       friendly->m_numTroops,
                                                       1, 0);
            meleeValue = ourArmy->getLossCombatValue(
                    estimate.m_lowestAttack, estimate.m_lowestDefense,
                    ourArmy->canShoot(0), damage, 0);
        } else {
            meleeValue = 0;
        }
        if (areaEffect == 0 && meleeValue == 0)
            continue;
        long count = (friendly->is(creatureDoubleWide)) ? 8 : 6;
        for (long direction = count; direction-- > 0; ) {
            long hex = friendly->getAdjacentHex(direction);
            if (!combatManager::validHex(hex))
                continue;
            if (areaEffect != 0) {
                if (checked[hex]) {
                    enemyAttacks[hex]--;
                } else {
                    checked[hex] = 1;
                    enemyAttacks[hex] -= areaEffect;
                }
                if (enemyAttacks[hex] < floorValue)
                    enemyAttacks[hex] = floorValue;
            }
            if (friendly->is(creatureHasExtendedAttack)) {
                long farHex = friendly->getAdjacentCellIndex(hex, direction);
                if (!combatManager::validHex(farHex))
                    continue;
                enemyAttacks[farHex] -= meleeValue;
                if (enemyAttacks[farHex] < floorValue)
                    enemyAttacks[farHex] = floorValue;
            }
        }
    }
}

void combatManager::markEnemyAttacks(const army* ourArmy, long* enemyAttacks, long& dangerousEnemies, const type_AI_combat_parameters& estimate) const
{
    long side = estimate.getGroup();
    long enemySide = estimate.getEnemyGroup();
    long floorValue = getEnemyAttackLimit(ourArmy, estimate);
    const army* enemy = m_armies[enemySide];
    dangerousEnemies = 0;
    for (long i = 0; i < m_numArmies[enemySide]; i++, enemy++) {
        if (enemy->cannotAttack()
                || enemy->m_creatureType == CREATURE_ARROW_TOWER)
            continue;
        if (enemy->canShoot(0)) {
            dangerousEnemies |= 1 << enemy->index;
            continue;
        }
        g_searchArray->seedCombatPosition(enemy, enemySide,
                                          enemy->getSpeed() + 1, 0,
                                          enemy->getSpeed() + 1);
        long j;
        const army* friendly = m_armies[side];
        for (j = 0; j < m_numArmies[side]; j++, friendly++) {
            if (friendly->is(creatureImmobilized))
                continue;
            if (friendly->m_creatureType == CREATURE_ARROW_TOWER)
                continue;
            const pathCell* cell = g_searchArray->getHex(friendly->m_gridIndex);
            if (m_cells[friendly->m_gridIndex].m_validMove
                    && cell->m_cost <= enemy->getSpeed())
                break;
        }
        if (j < m_numArmies[side]) {
            dangerousEnemies |= 1 << enemy->index;
            if (enemy->is(creatureMultiHeaded) && g_game->m_setup.m_difficulty >= 2
                    && !IsHuman[side])
                markMultiheadedEnemy(ourArmy, enemy, enemyAttacks,
                                       floorValue, g_searchArray, estimate);
            continue;
        }
        long value = -estimate.getSimpleAttackEffect(*(enemy), *(ourArmy), 0, 0);
        if (value >= 0)
            continue;
        for (long hex = 0; hex < COMBAT_GRID_CELLS; hex++) {
            const pathCell* cell = g_searchArray->getHex(hex);
            if (!cell->m_visited)
                continue;
            enemyAttacks[hex] += value;
            if (enemyAttacks[hex] < floorValue)
                enemyAttacks[hex] = floorValue;
        }
        if (enemy->is(creatureDoubleWide)) {
            long direction = enemy->m_facing ? 1 : 4;
            for (long hex = 0; hex < COMBAT_GRID_CELLS; hex++) {
                const pathCell* cell = g_searchArray->getHex(hex);
                if (!cell->m_visited)
                    continue;
                if (g_searchArray->isMoat(static_cast<short>(hex)))
                    continue;
                long adjacent = m_adjacentCells[hex][direction];
                if (!validHex(adjacent))
                    continue;
                const pathCell* other = g_searchArray->getHex(adjacent);
                if (other->m_visited)
                    continue;
                enemyAttacks[adjacent] += value;
                if (enemyAttacks[adjacent] < floorValue)
                    enemyAttacks[adjacent] = floorValue;
            }
        }
    }
}

type_AI_attack_hex_chooser::type_AI_attack_hex_chooser(const army* attack_army, const army* enemy_army, const long* enemy_attack_array, searchArray* search_data, const type_AI_combat_parameters* data)
{
    estimate = data;
    attacker = attack_army;
    speed = attack_army->getSpeed();
    enemy = enemy_army;
    best_value = 0;
    best_attack_time = 0;
    enemy_attacks = enemy_attack_array;
    search_array = search_data;
    best_hex = -1;
    long enemyHits = enemy_army->getTotalHitPoints(data->m_simulated);
    long ourHits = attack_army->getTotalHitPoints(data->m_simulated);
    enemyHits -= aiGetAttackDamage(*attack_army, ourHits, *enemy_army, 0, 0);
    if (enemyHits < 0)
        enemyHits = 0;
    retaliation_strength = (enemy_army->m_monInfo.hitPoints + enemyHits - 1) / enemy_army->m_monInfo.hitPoints;
    our_strength = (attack_army->m_monInfo.hitPoints + ourHits - 1) / attack_army->m_monInfo.hitPoints;
}

long type_AI_attack_hex_chooser::getHexAttackValue(long hex, long& checked)
{
    double combatValue;
    long value = 0;
    if (!g_game->m_setup.m_difficulty
        && !g_combatManager->IsHuman[estimate->getGroup()])
        return 0;
    for (long direction = 0; direction < COMBAT_DIRECTION_COUNT; direction++) {
        long index = g_combatManager->m_adjacentCells[hex][direction];
        if (!g_combatManager->validHex(index))
            continue;
        army* neighbor = g_combatManager->m_cells[index].getArmy();
        if (!neighbor)
            continue;
        if (neighbor->getOwningSide() == g_combatManager->m_currentSide)
            continue;
        if (neighbor == attacker)
            continue;
        if (checked & (1 << neighbor->index))
            continue;
        checked |= 1 << neighbor->index;
        if (!neighbor->canShoot(attacker))
            continue;
        long hits = neighbor->getTotalHitPoints(estimate->m_simulated);
        if (hits == 0)
            continue;
        combatValue = neighbor->getUnitCombatValue(
                           estimate->m_lowestAttack, estimate->m_lowestDefense, 1,
                           attacker);
        combatValue -= neighbor->getUnitCombatValue(
                           estimate->m_lowestAttack, estimate->m_lowestDefense, 0, 0);
        long share = static_cast<long>(combatValue * static_cast<double>(hits)
                                       / static_cast<double>(neighbor->m_monInfo.hitPoints));
        if (share < 1)
            share = 1;
        value += share;
    }
    return value;
}

long type_AI_attack_hex_chooser::getAttackTime(const pathCell* cell) const
{
    if (speed == 0)
        return 0 < cell->m_cost ? 100 : 1;
    long turns = (cell->m_cost + speed - 1) / speed;
    if (search_array->isMoat(cell->m_point.m_x))
        turns++;
    if (turns < 1)
        turns = 1;
    return turns;
}

void type_AI_attack_hex_chooser::checkAdjacentHexes(long enemyHex, long startDirection, long stopDirection)
{
    // DC records enemy_value, direction, checked and attack_time at
    // procedure scope. Both bonus helpers take the enemy's stored combat
    // side, not the getOwningSide accessor.
    long threat;
    long direction;
    long checked;
    long turns;
    for (direction = startDirection; direction < stopDirection; direction++) {
        long hex = g_combatManager->m_adjacentCells[enemyHex][direction];
        if (!g_combatManager->validHex(hex))
            continue;
        const pathCell* cell = search_array->getHex(hex);
        if (!cell->m_visited)
            continue;
        if (cell->m_flightCost > 0)
            continue;
        turns = getAttackTime(cell);
        if (getBestHex() >= 0) {
            if (getAttackTime() < turns)
                continue;
        }
        checked = 0;
        long value = getHexAttackValue(hex, checked);
        if (g_game->m_setup.m_difficulty > 0
                || g_combatManager->IsHuman[estimate->getGroup()]) {
            if (attacker->is(creatureMultiHeaded))
                value += getMultiHeadBonus(g_combatManager->m_currentSide,
                                              attacker, hex, our_strength,
                                              enemy, enemy->m_gridIndex,
                                              estimate);
            if (attacker->is(creatureHasExtendedAttack))
                value += getBreathBonus(g_combatManager->m_currentSide,
                                          attacker, hex, our_strength,
                                          enemy, enemy->m_gridIndex,
                                          estimate);
            if (enemy->canRetaliate(*attacker)) {
                if (enemy->is(creatureMultiHeaded))
                    value -= getMultiHeadBonus(enemy->group,
                                                  enemy,
                                                  enemy->m_gridIndex,
                                                  retaliation_strength,
                                                  attacker, hex, estimate);
                if (enemy->is(creatureHasExtendedAttack))
                    value -= getBreathBonus(enemy->group,
                                              enemy,
                                              enemy->m_gridIndex,
                                              retaliation_strength,
                                              attacker, hex, estimate);
            }
        }
        threat = enemy_attacks[hex];
        if (attacker->is(creatureDoubleWide)) {
            long otherHex = hex + attacker->offsetToFront(-1);
            value += getHexAttackValue(otherHex, checked);
            threat = min(enemy_attacks[otherHex], threat);
        }
        value += threat;
        if (getBestHex() >= 0 && turns == getAttackTime()) {
            if (value < getHexValue())
                continue;
            if (value == getHexValue()) {
                const pathCell* bestCell = search_array->getHex(getBestHex());
                long difference = cell->m_cost - bestCell->m_cost;
                if ((attacker->m_creatureType == CREATURE_CAVALIER
                            || attacker->m_creatureType == CREATURE_CHAMPION)
                        && (g_game->m_setup.m_difficulty > 0
                            || g_combatManager->IsHuman[estimate->getGroup()])) {
                    if (difference <= 0)
                        continue;
                } else {
                    if (difference >= 0)
                        continue;
                }
            }
        }
        best_value = value;
        best_hex = hex;
        best_attack_time = turns;
    }
}

unsigned char type_AI_attack_hex_chooser::findAttackHex()
{
    best_value = 0;
    best_hex = -1;
    checkAdjacentHexes(enemy->m_gridIndex, 0, 6);
    if (enemy->is(creatureDoubleWide))
        checkAdjacentHexes(enemy->getSecondGridIndex(), 0, 6);
    if (attacker->is(creatureDoubleWide)) {
        long hex = enemy->m_gridIndex;
        long offset = -attacker->offsetToFront(-1);
        if (enemy->is(creatureDoubleWide)
                && offset == enemy->offsetToFront(-1))
            hex = enemy->getSecondGridIndex();
        if (offset < 0) {
            long second = g_combatManager->m_adjacentCells[hex][4];
            if (g_combatManager->validHex(second))
                checkAdjacentHexes(second, 3, 6);
        } else {
            long second = g_combatManager->m_adjacentCells[hex][1];
            if (g_combatManager->validHex(second))
                checkAdjacentHexes(second, 0, 3);
        }
    }
    return combatManager::validHex(getBestHex());
}

unsigned char combatManager::moveToward(const army* currentArmy, long targetHex, const long* enemyAttacks, unsigned char considerWaiting)
{
    if (!currentArmy->getSpellTime(72) && currentArmy->getSpeed()) {
        g_searchArray->findCombatPath(currentArmy, m_currentSide, targetHex,
                                      m_creaturePlacement, 0x7f, -1);
        if (static_cast<long>(g_searchArray->getPathSteps()) > 0) {
            m_nextAction = 2;
            long moveLeft = currentArmy->getSpeed();
            long pathIndex = g_searchArray->getPathSteps() - 1;
            long step;
            long startDanger;
            long limit;
            long bestDanger;
            long hex = currentArmy->m_gridIndex;
            unsigned char committed;

            bestDanger = 0;
            startDanger = 0;
            m_nextActionGridIndex = hex;
            if (m_creaturePlacement)
                moveLeft = pathIndex + 1;
            if (m_creaturePlacement || m_inSecondPhase)
                considerWaiting = 0;
            if (g_game->m_setup.m_difficulty < 2 && !IsHuman[currentArmy->group])
                considerWaiting = 0;
            if (enemyAttacks == 0) {
                considerWaiting = 0;
            } else {
                bestDanger = enemyAttacks[hex];
                if (currentArmy->is(creatureDoubleWide))
                    bestDanger = min(bestDanger,
                            enemyAttacks[hex
                                    + currentArmy->offsetToFront(-1)]);
                startDanger = bestDanger;
            }

            committed = 0;
            limit = (pathIndex / moveLeft) * moveLeft;
            if (pathIndex >= 0) {
                for (step = pathIndex + 1; pathIndex >= 0;
                        pathIndex--, step--, moveLeft--) {
                    if (moveLeft <= 0)
                        break;
                    hex = const_cast<army*>(currentArmy)->getAdjacentCellIndex(
                            hex, g_searchArray->getStep(pathIndex));
                    if (!validHex(hex))
                        break;
                    const pathCell* cell = g_searchArray->getHex(hex);
                    if (cell->m_flightCost == 0) {
                        long secondHex = (currentArmy->is(creatureDoubleWide))
                                ? hex + currentArmy->offsetToFront(-1) : hex;
                        if (!(((step <= limit && committed) || considerWaiting)
                                    && enemyAttacks != 0)
                                || (enemyAttacks[hex] >= bestDanger
                                    && enemyAttacks[secondHex] >= bestDanger)) {
                            if (!m_creaturePlacement
                                    || !isOutsidePlacementBoundry(
                                            currentArmy->group, hex)) {
                                m_nextActionGridIndex = hex;
                                committed = 1;
                                if (enemyAttacks != 0) {
                                    bestDanger = enemyAttacks[hex];
                                    if (currentArmy->is(creatureDoubleWide))
                                        bestDanger = min(bestDanger,
                                                enemyAttacks[secondHex]);
                                }
                            }
                        }
                        if (!currentArmy->is(creatureFlyingArmy)) {
                            if (g_searchArray->isMoat(
                                        static_cast<short>(hex))
                                    || g_searchArray->isMoat(
                                            static_cast<short>(secondHex)))
                                moveLeft = 0;
                        }
                    }
                }
            }

            if (considerWaiting && m_nextActionGridIndex != hex
                    && bestDanger >= startDanger)
                m_nextAction = 8;
            if (m_nextActionGridIndex == currentArmy->m_gridIndex)
                m_nextAction = considerWaiting ? 8 : 3;
            return 1;
        }
    }
    return 0;
}

bool combatManager::chooseMeleeTarget(const army* currentArmy, bool teleport, long& actionValue, type_AI_combat_parameters& estimate)
{
    long enemyAttacks[COMBAT_GRID_CELLS];

    long ourGroup = estimate.getGroup();
    long enemyGroup = estimate.getEnemyGroup();
    army* bestTarget = 0;
    long bestValue = 0;
    long bestTime = 0;
    long bestHexValue = 0;
    unsigned char bestIsBlockingAction = 0;
    long markedEnemies = 0;
    long bestHex = -1;
    long budget = 127;
    memset(enemyAttacks, 0, sizeof(enemyAttacks));

    markFirewalls(currentArmy, enemyAttacks, &estimate);
    if (g_game->m_gameVersion >= 2)
        markMoat(currentArmy, enemyAttacks, &estimate);
    if (g_game->m_setup.m_difficulty > 0 || IsHuman[ourGroup])
        markEnemyAttacks(currentArmy, enemyAttacks, markedEnemies,
                           estimate);
    if (g_game->m_setup.m_difficulty >= 2 || IsHuman[ourGroup])
        markFriendlyArmies(currentArmy, enemyAttacks, markedEnemies,
                             estimate);

    if (currentArmy->isIncapacitated())
        budget = 0;
    if (teleport)
        g_searchArray->markTeleport(currentArmy, ourGroup);
    else
        g_searchArray->seedCombatPosition(currentArmy, ourGroup, budget,
                                          m_creaturePlacement, -1);
    unsigned char stayingInCastle = shouldStayInCastle(&estimate);
    std::copy(enemyAttacks,enemyAttacks+187,trace.back().danger.begin());trace.back().marked=markedEnemies;

    for (long i = 0; i < m_numArmies[enemyGroup]; i++) {
        army* enemy = &m_armies[enemyGroup][i];
        if (enemy->is(creatureImmobilized))
            continue;
        if (enemy->m_creatureType == CREATURE_ARROW_TOWER)
            continue;
        if (estimate.m_simulated && enemy->getTotalHitPoints(1) == 0)
            continue;
        if (currentArmy->getSpeed() == 0 || currentArmy->getSpellTime(72)) {
            const pathCell* stand = g_searchArray->getHex(enemy->m_gridIndex);
            if (stand->m_cost > 0)
                continue;
        }
        if (stayingInCastle && !combatManager::inCastle(enemy->m_gridIndex)
                && (!enemy->is(creatureDoubleWide)
                    || !combatManager::inCastle(enemy->getSecondGridIndex())))
            continue;

        long change = 0;
        unsigned char isBlockingAction = 0;
        if (m_cells[enemy->m_gridIndex].m_validMove
                && (g_game->m_setup.m_difficulty >= 2 || IsHuman[ourGroup])
                && !currentArmy->cannotAttack()
                && !enemy->is(creatureMultiHeaded)) {
            const pathCell* reach = g_searchArray->getHex(enemy->m_gridIndex);
            if (reach->m_cost <= currentArmy->getSpeed())
                change = getAttackChange(currentArmy, enemy, estimate);
        }

        type_AI_attack_hex_chooser chooser(currentArmy, enemy, enemyAttacks,
                                           g_searchArray, &estimate);
        if (!chooser.findAttackHex())
            continue;
        if (!currentArmy->cannotAttack()) {
            long distance = 0;
            if (chooser.getAttackTime() == 1) {
                const pathCell* attackCell = g_searchArray->getHex(chooser.getBestHex());
                distance = attackCell->m_cost;
            }
            change += estimate.getSimpleAttackEffect(*(currentArmy), *(enemy), 0, distance);
        }

        if (chooser.getHexValue() <= 0 || currentArmy->is(creatureSummoned)
                || (g_game->m_setup.m_difficulty == 0 && !IsHuman[ourGroup])) {
            change += chooser.getHexValue();
        } else if (currentArmy->m_creatureType == CREATURE_HARPY
                   || currentArmy->m_creatureType == CREATURE_HARPY_HAG) {
            if (change < chooser.getHexValue()) {
                isBlockingAction = 1;
                change = chooser.getHexValue();
            }
        } else if (change < 0 && hasRangedAdvantage(estimate)) {
            change = chooser.getHexValue();
            isBlockingAction = 1;
        } else {
            change += chooser.getHexValue();
        }
        if (change < 0
                && (g_game->m_setup.m_difficulty > 0 || IsHuman[ourGroup])
                && chooser.getHexValue() < enemyAttacks[chooser.getBestHex()])
            continue;

        long randomFactor = ::random(75, 100);
        long score = randomFactor * change / 100;
        if (bestTarget != 0) {
            if (enemy->isIncapacitated() && !bestTarget->isIncapacitated())
                continue;
            if (!(bestTarget->isIncapacitated() && !enemy->isIncapacitated())) {
                if (bestTime < chooser.getAttackTime())
                    continue;
                if (bestTime == chooser.getAttackTime()) {
                    if (bestValue > score)
                        continue;
                    if (bestValue == score) {
                        if (bestTarget->m_topCreatureDamage
                                > enemy->m_topCreatureDamage)
                            continue;
                        if (bestTarget->m_topCreatureDamage
                                == enemy->m_topCreatureDamage) {
                            const pathCell* held = g_searchArray->getHex(bestHex);
                            const pathCell* offered
                                    = g_searchArray->getHex(chooser.getBestHex());
                            if (held->m_cost < offered->m_cost)
                                continue;
                        }
                    }
                }
            }
        }
        bestTarget = enemy;
        bestIsBlockingAction = isBlockingAction;
        bestHex = chooser.getBestHex();
        bestTime = chooser.getAttackTime();
        bestHexValue = chooser.getHexValue() * randomFactor / (bestTime * 100);
        bestValue = score / bestTime;
    }

    actionValue = max(bestValue, bestHexValue);
    if (teleport) {
        if (bestTarget != 0 && actionValue >= 0) {
            m_nextAction = 6;
            m_nextActionExtra = bestHex;
            m_nextActionGridIndex = bestTarget->m_gridIndex;
            return 1;
        }
        m_nextAction = 3;
        return 1;
    }
    if (!estimate.m_simulated && !m_creaturePlacement
            && chooseSpellAction(currentArmy, &actionValue, &estimate))
        return 1;
    if (bestIsBlockingAction) {
        actionValue = bestHexValue;
        if (bestHex == currentArmy->m_gridIndex)
            return 0;
        moveToward(currentArmy, bestHex, enemyAttacks,
                    static_cast<unsigned char>(!estimate.m_simulated
                                               && bestTime > 1));
        return 1;
    }

    if (bestTarget == 0) {
        if (m_fortificationLevel > 0 && m_currentSide == 0) {
            long hex = g_castleWallColumns[combatManager::gridY(currentArmy->m_gridIndex)];
            while (hex > currentArmy->m_gridIndex) {
                const pathCell* cell = g_searchArray->getHex(hex);
                if (cell->m_visited) {
                    if (!isInMoat(hex, 0)) {
                        if (!currentArmy->is(creatureDoubleWide))
                            break;
                        if (!isInMoat(hex + currentArmy->offsetToFront(-1), 0))
                            break;
                    }
                }
                hex--;
            }
            if (hex > currentArmy->m_gridIndex) {
                moveToward(currentArmy, hex, enemyAttacks, 0);
                return 1;
            }
            m_nextAction = 3;
            return 1;
        }
    }
    if (bestTarget == 0
            || (bestValue < 0 && bestValue < bestHexValue
                && !currentArmy->is(creatureSummoned)
                && (g_game->m_setup.m_difficulty > 0 || IsHuman[ourGroup])
                && hasRangedAdvantage(estimate))) {
        actionValue = 0;
        if (!estimate.m_simulated
                && getAreaEffect(enemyGroup, currentArmy,
                                   markedEnemies,
                                   estimate) == 0
                && attemptShooterDefense(currentArmy, g_searchArray, &estimate))
            return 1;
        if (bestTarget == 0) {
            if (!estimate.m_simulated
                    && chooseToRun(currentArmy, enemyAttacks, g_searchArray))
                return 1;
            return 0;
        }
    }

    actionValue = bestValue;
    if (bestTime <= 1 && !m_creaturePlacement) {
        m_nextActionExtra = bestHex;
        m_nextAction = 6;
        m_nextActionGridIndex = bestTarget->m_gridIndex;
        return 1;
    }
    if (bestHex == currentArmy->m_gridIndex)
        return 0;
    // An ordinary int 0/1 argument.
    int shouldMoveToward = !estimate.m_simulated && bestTime > 1;
    moveToward(currentArmy, bestHex, enemyAttacks,
               shouldMoveToward);
    return 1;
}

long combatManager::sourceChooseMeleeAction(const army* currentArmy, bool teleport, bool simulated, long side)
{
    type_AI_combat_parameters data(this, side);
    data.m_simulated = simulated;
    findMoveOrder(0);
    findAITargets(side, currentArmy, 1, &data, 0);
    long actionValue = 0;
    if (chooseMeleeTarget(currentArmy, teleport, actionValue, data))
        return actionValue;
    if (!simulated && chooseSpellAction(currentArmy, &actionValue, &data))
        return actionValue;
    if (!m_inSecondPhase && (g_game->m_setup.m_difficulty >= 2 || IsHuman[side])) {
        m_nextAction = 8;
        return 0;
    }
    m_nextAction = 3;
    return 0;
}

void combatManager::simulateMeleeAttack(army* currentArmy, long hex,
                                          army* target, long enemyHex,
                                          long ourGroup)
{
    if (currentArmy->is(creatureMultiHeaded)) {
        long directions = currentArmy->getMultiHeadDirections(hex, target,
                                                                  enemyHex);
        long hit = 0;
        long direction = 7;
        do {
            if (!(directions & (1 << direction)))
                continue;
            long adjacent = currentArmy->getAdjacentHex(hex, direction);
            if (!validHex(adjacent))
                continue;
            army* victim = m_cells[adjacent].getArmy();
            if (!victim)
                continue;
            long bit = 1 << victim->index;
            if (hit & bit)
                continue;
            if (victim->group == ourGroup)
                continue;
            hit |= bit;
            simulateSimpleAttack(currentArmy, victim, 0, 0, 0);
        } while (direction-- > 0);
        return;
    }

    simulateSimpleAttack(currentArmy, target,
                           g_searchArray->getHex(hex)->m_cost, 0, 0);

    if (currentArmy->is(creatureHasExtendedAttack)) {
        long direction = currentArmy->getAttackDirection(hex, target,
                                                            enemyHex);
        long behindHex = currentArmy->getAdjacentHex(hex, direction);
        behindHex = currentArmy->getAdjacentCellIndex(behindHex, direction);
        if (!validHex(behindHex))
            return;
        army* behind = m_cells[behindHex].getArmy();
        if (!behind || behind == target)
            return;
        simulateSimpleAttack(currentArmy, behind, 0, 0, 1);
    }
}

void combatManager::simulateMeleeAttack(army* currentArmy, army* target,
                                          long ourGroup)
{
    long hex = m_nextActionExtra;
    if (!validHex(hex))
        return;

    long hitPoints = target->getTotalHitPoints(0);
    simulateMeleeAttack(currentArmy, hex, target, target->m_gridIndex,
                          ourGroup);

    if (target->canRetaliate(*currentArmy)
        && (g_game->m_setup.m_difficulty > 0 || IsHuman[ourGroup]))
        simulateMeleeAttack(target, target->m_gridIndex, currentArmy, hex,
                              1 - ourGroup);

    if (currentArmy->is(creatureTwoAttacks) && target->getAIExpectedDamage() < hitPoints)
        simulateMeleeAttack(currentArmy, hex, target, target->m_gridIndex,
                              ourGroup);
}

unsigned char combatManager::chooseToRun(const army* ourArmy, const long* enemyAttacks, const searchArray* currentSearchArray)
{
    if (g_game->m_setup.m_difficulty < 2
        && !IsHuman[ourArmy->group])
        return 0;

    long worstDanger = enemyAttacks[ourArmy->m_gridIndex];
    if (ourArmy->is(creatureDoubleWide)) {
        long secondHex = ourArmy->getSecondGridIndex();
        worstDanger = min(worstDanger, enemyAttacks[secondHex]);
    }

    if (worstDanger >= 0 || ourArmy->isIncapacitated()
        || ourArmy->getSpellTime(72))
        return 0;

    long bestDistance = 0;
    long bestHex = -1;
    for (long hex = 0; hex < COMBAT_GRID_CELLS; ++hex) {
        const pathCell* cell = currentSearchArray->getHex(hex);
        if (!cell->m_visited || cell->m_flightCost > 0)
            continue;

        long distance = cell->m_cost;
        if (distance > ourArmy->getSpeed())
            continue;

        long danger = enemyAttacks[hex];
        if (ourArmy->is(creatureDoubleWide)) {
            long secondHex = hex + ourArmy->offsetToFront(-1);
            danger = min(danger, enemyAttacks[secondHex]);
        }
        if (danger < worstDanger)
            continue;
        if (danger == worstDanger && bestDistance < distance)
            continue;
        worstDanger = danger;
        bestDistance = distance;
        bestHex = hex;
    }

    if (bestHex < 0 || bestHex == ourArmy->m_gridIndex)
        return 0;
    return moveToward(ourArmy, bestHex, enemyAttacks, 0);
}

unsigned char combatManager::hasRangedAdvantage(type_AI_combat_parameters& data)
{
    long totalValue[2];
    long shooterValue[2];

    for (long side = 0; side < 2; side++) {
        shooterValue[side] = 0;
        totalValue[side] = 0;
        const army* stack = m_armies[side];
        for (long i = 0; i < m_numArmies[side]; i++, stack++) {
            if (!stack->is(creatureImmobilized)
                    && !stack->isIncapacitated()
                    && stack->m_creatureType != CREATURE_ARROW_TOWER) {
                long value = stack->getTotalCombatValue(
                    data.m_lowestAttack, data.m_lowestDefense);
                totalValue[side] += value;
                if (stack->canShoot(0))
                    shooterValue[side] += value;
            }
        }
        shooterValue[side] += heroRangedValue[side];
    }

    return shooterValue[data.getGroup()] > shooterValue[data.getEnemyGroup()];
}
