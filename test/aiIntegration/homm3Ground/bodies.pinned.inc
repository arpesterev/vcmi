void searchArray::seedCombatPosition(const army* thisArmy, long currentGroup, long limit, bool inPlacementPhase, long baseSpeed)
{
    if (m_cellData == 0)
        init();
    if (inPlacementPhase) {
        baseSpeed = 1000;
        limit = 1000;
    } else {
        if (baseSpeed < 0)
            baseSpeed = thisArmy->getSpeed();
        if (thisArmy->getSpellTime(72))
            limit = 0;
    }
    findCombatPath(thisArmy, currentGroup, -1, inPlacementPhase, limit,
                   baseSpeed);

    for (long i = 0; i < COMBAT_GRID_CELLS; i++) {
        const pathCell* cell = getHex(i);
        if (cell->m_visited && static_cast<long>(cell->m_cost) <= baseSpeed
                && cell->m_flightCost == 0
                && (!inPlacementPhase
                    || !g_combatManager->isOutsidePlacementBoundry(
                            currentGroup, i))) {
            g_combatManager->m_cells[i].m_validMove = 1;
            if (thisArmy->is(creatureDoubleWide) && !thisArmy->is(creatureSiegeWeapon)) {
                long second = i + thisArmy->offsetToFront(-1);
                if (!g_combatManager->inInvisibleColumn(second)) {
                    if (inPlacementPhase
                            && g_combatManager->isOutsidePlacementBoundry(
                                    currentGroup, second))
                        g_combatManager->m_cells[i].m_validMove = 0;
                    else
                        g_combatManager->m_cells[second].m_frontMove = 1;
                }
            }
        }
    }

    if (thisArmy->canShoot(0) && !inPlacementPhase
            && thisArmy->m_creatureType != CREATURE_CATAPULT) {
        long other = 1 - currentGroup;
        const army* enemy = g_combatManager->m_armies[other];
        for (long j = 0; j < g_combatManager->m_numArmies[other]; j++, enemy++) {
            if (!enemy->is(creatureImmobilized)
                    && enemy->m_creatureType != CREATURE_ARROW_TOWER) {
                g_combatManager->m_cells[enemy->m_gridIndex].m_validMove = 1;
                if (enemy->is(creatureDoubleWide))
                    g_combatManager->m_cells[enemy->getSecondGridIndex()]
                            .m_validMove = 1;
            }
        }
    }
}

void searchArray::markEnemy(long hex, long cost)
{
    hexcell* combatCell = &g_combatManager->m_cells[hex];
    pathCell* cell = getHex(hex);
    if (combatCell->m_validMove && cell->m_cost <= cost)
        return;
    combatCell->m_validMove = 1;
    cell->m_cost = static_cast<unsigned short>(cost);
}

bool searchArray::checkEnemyArmies(long hex, long cost,
                                  long currentGroup, long destination)
{
    if (!combatManager::validHex(hex))
        return 0;
    const army* enemy = g_combatManager->m_cells[hex].getArmy();
    if (enemy == 0)
        return 0;
    if (enemy->getOwningSide() == currentGroup)
        return 0;

    markEnemy(enemy->m_gridIndex, cost);
    if (enemy->is(creatureDoubleWide))
        markEnemy(enemy->getSecondGridIndex(), cost);
    return hex == destination;
}

bool searchArray::findCombatPath(const army* currentArmy,
                                          long currentGroup, long destination,
                                          bool inPlacementPhase,
                                          long limit, long baseSpeed)
{
    if (currentArmy == 0)
        return 0;

    unsigned char siegePressure = 0;
    if (g_combatManager->m_defendingTown != 0
            && g_combatManager->isComputerAction(currentArmy)) {
        if (currentGroup == 1
                || g_combatManager->m_drawbridgeState != DRAWBRIDGE_UP)
            siegePressure = 1;
        if (currentArmy->getTotalHitPoints(0)
                <= g_moatDamage[
                        g_combatManager->m_defendingTown->m_type] * 4)
            siegePressure = 1;
        if (siegePressure
                && currentArmy->getTotalHitPoints(0)
                    > g_moatDamage[
                            g_combatManager->m_defendingTown->m_type] * 40)
            siegePressure = 0;
    }

    for (long clearHex = 0; clearHex < COMBAT_GRID_CELLS; clearHex++) {
        g_combatManager->m_cells[clearHex].m_validMove = 0;
        g_combatManager->m_cells[clearHex].m_frontMove = 0;
    }

    if (inPlacementPhase) {
        baseSpeed = limit = 1000;
    } else {
        if (baseSpeed < 0)
            baseSpeed = currentArmy->getSpeed();
        if (baseSpeed == 0 || currentArmy->getSpellTime(72))
            limit = 0;
    }

    long startHex = currentArmy->m_gridIndex;
    if (m_cellData == 0)
        init();
    setMoat(currentArmy);

    int bestDistance = 800;
    int bestHex = -1;
    // Dreamcast CodeView names function-scope `pathCell pc` and emits its
    // empty constructor before both vector clears; that function-scope
    // lifetime is kept.
    pathCell pc;
    m_result.clear();
    m_queue.clear();
    memset(m_cellData, 0, COMBAT_GRID_CELLS * sizeof(pathCell));

    pushCombatPoint(startHex, currentArmy->m_facing ? 1 : 4, 0, 0, limit);

    while (m_queue.size() > 0) {
        pc = m_queue.back();
        m_queue.pop_back();

        long cost = pc.m_cost;
        if (cost > limit)
            continue;

        if (combatManager::validHex(destination)
                && pc.m_flightCost == 0) {
            long distance = combatManager::getDistance(pc.m_point.m_x, destination);
            if (distance < bestDistance) {
                bestHex = pc.m_point.m_x;
                bestDistance = distance;
                if (distance == 0)
                    break;
            }
        }

        long hex = pc.m_point.m_x;
        long adjacent;
        long direction;
        for (direction = 0; direction < COMBAT_DIRECTION_COUNT; direction++) {
            adjacent = currentArmy->getAdjacentCellIndex(hex, direction);
            if (!combatManager::validHex(adjacent))
                continue;

            int flightCost = 0;
            unsigned char moat = 0;
            if (!currentArmy->is(creatureDoubleWide)) {
                moat = isMoat(adjacent);
            } else {
                long front = pc.m_point.m_x + currentArmy->offsetToFront(-1);
                long tail = adjacent + currentArmy->offsetToFront(-1);
                if (isMoat(adjacent) && adjacent != front)
                    moat = 1;
                if (isMoat(tail) && tail != hex)
                    moat = 1;
            }

            int step = 1;
            if (moat && baseSpeed > 0)
                step = baseSpeed - cost % baseSpeed;

            if (!currentArmy->canFit(adjacent, 0, 0)
                    || (siegePressure && moat)) {
                long enemyCost = pc.m_cost;
                unsigned char blocked = 0;
                if (isMoat(pc.m_point.m_x))
                    enemyCost += baseSpeed;
                if (limit <= baseSpeed) {
                    if (isMoat(pc.m_point.m_x))
                        blocked = 1;
                    if (currentArmy->is(creatureDoubleWide)
                            && isMoat(static_cast<short>(
                                    pc.m_point.m_x
                                    + (currentArmy->offsetToFront(-1)))))
                        blocked = 1;
                }
                if (enemyCost <= limit && !blocked) {
                    if (checkEnemyArmies(adjacent, enemyCost, currentGroup,
                                         destination)
                            || (currentArmy->is(creatureDoubleWide)
                                && checkEnemyArmies(adjacent
                                    + currentArmy->offsetToFront(-1),
                                    enemyCost, currentGroup, destination))) {
                        pathCell* reached = getHex(adjacent);
                        reached->m_point.m_x = static_cast<short>(adjacent);
                        reached->m_direction = direction;
                        reached->m_lastPoint = pc.m_point;
                        m_result.push_back(reached);
                        break;
                    }
                }
                if (!(currentArmy->is(creatureFlyingArmy)
                        || currentArmy->m_creatureType == CREATURE_DEVIL
                        || currentArmy->m_creatureType == CREATURE_ARCH_DEVIL))
                    continue;
                flightCost = pc.m_flightCost + 1;
                if (flightCost >= currentArmy->getSpeed())
                    continue;
            }
            pushCombatPoint(adjacent, direction, pc.m_cost + step, flightCost,
                            limit);
        }

        // DC 1358/1360 read the result vector directly here as well.
        if (m_result.size() > 0) {
            bestHex = m_result[0]->m_lastPoint.m_x;
            break;
        }
    }

    return buildCombatPath(currentArmy, startHex, bestHex,
                             destination);
}

void searchArray::pushCombatPoint(int index, int direction, int cost, int flightCost, int limit)
{
    if (!combatManager::validHex(index))
        return;
    if (cost > limit)
        return;

    pathCell* cell = getHex(index);
    if (cell->m_visited && cell->m_cost <= cost)
        return;
    if (m_queue.size() >= 500)
        return;

    int last = m_queue.size();
    int first = 0;
    int middle;
    for (;;) {
        middle = (first + last) / 2;
        if (last <= first)
            break;
        if (cost < m_queue[middle].m_cost)
            first = middle + 1;
        else
            last = middle;
    }

    pathCell currentPathCell;
    currentPathCell.m_point.m_x = index;
    currentPathCell.m_visited = 1;
    currentPathCell.m_point.m_y = 0;
    currentPathCell.m_direction = direction;
    currentPathCell.m_cost = cost;
    currentPathCell.m_flightCost = flightCost;

    if (middle < m_queue.size()) {
        m_queue.insert(m_queue.begin() + middle, currentPathCell);
    } else {
        m_queue.push_back(currentPathCell);
    }
    *cell = currentPathCell;
}

int army::canFit(int destIndex, int allowShifting, int* newDestIndex) const
{
    if (newDestIndex)
        *newDestIndex = destIndex;

    if (!combatManager::validHex(destIndex))
        return 0;
    if (combatManager::inInvisibleColumn(destIndex))
        return 0;

    const hexcell* cell = &g_combatManager->m_cells[destIndex];
    if (g_combatManager->hexIsBlocked(destIndex))
        return 0;
    if (cell->hasArmy()) {
        if (cell->m_armySide != group)
            return 0;
        if (cell->m_armySlot != index)
            return 0;
    }

    if (!is(creatureDoubleWide))
        return 1;

    int otherIndex = getAdjacentCellIndex(destIndex, m_facing ? 1 : 4);
    if (!combatManager::validHex(otherIndex))
        return 0;
    if (combatManager::inInvisibleColumn(otherIndex))
        return 0;
    hexcell* otherCell = &g_combatManager->m_cells[otherIndex];
    if (!g_combatManager->hexIsBlocked(otherIndex)) {
        if (!otherCell->hasArmy()
                || (otherCell->m_armySide == group
                    && otherCell->m_armySlot == index))
            return 1;
    }

    if (allowShifting) {
        int shiftedIndex = getAdjacentCellIndex(destIndex, m_facing ? 4 : 1);
        if (!combatManager::validHex(shiftedIndex))
            return 0;
        if (combatManager::inInvisibleColumn(shiftedIndex))
            return 0;
        hexcell* shiftedCell = &g_combatManager->m_cells[shiftedIndex];
        if (g_combatManager->hexIsBlocked(shiftedIndex))
            return 0;
        if (shiftedCell->hasArmy()) {
            if (shiftedCell->m_armySide != g_combatManager->m_actingSide)
                return 0;
            if (shiftedCell->m_armySlot != g_combatManager->m_actingSlot)
                return 0;
        }
        if (newDestIndex)
            *newDestIndex = shiftedIndex;
        return 1;
    }
    return 0;
}

int army::getAdjacentCellIndex(int currIndex, int direction) const
{
    if (!combatManager::validHex(currIndex))
        return -1;
    if (direction == COMBAT_DIRECTION_WIDE_UPPER)
        direction = (m_facing == 1) ? 5 : 0;
    else if (direction == COMBAT_DIRECTION_WIDE_LOWER)
        direction = (m_facing == 1) ? 3 : 2;
    return g_combatManager->m_adjacentCells[currIndex][direction];
}

void combatManager::setupAdjacencyArray()
{
    int adjacent;
    for (int hex = 0; hex < COMBAT_GRID_CELLS; hex++) {
        int row = gridY(hex);
        int column = gridX(hex);

        for (int direction = 0; direction < COMBAT_DIRECTION_COUNT;
                direction++) {
            // DC cmbtmgr.cpp:1541 names InInvisibleColumn here.
            if (inInvisibleColumn(hex)) {
                m_adjacentCells[hex][direction] = -1;
                if (column == 0) {
                    if (direction >= COMBAT_DIRECTION_3)
                        continue;
                } else if (direction <= COMBAT_DIRECTION_2) {
                    continue;
                }
            }

            switch (direction) {
            case COMBAT_DIRECTION_0:
                adjacent = rowIsOdd(row) ? hex - 17 : hex - 16;
                break;
            case COMBAT_DIRECTION_1:
                adjacent = hex + 1;
                break;
            case COMBAT_DIRECTION_2:
                adjacent = rowIsOdd(row) ? hex + 17 : hex + 18;
                break;
            case COMBAT_DIRECTION_3:
                adjacent = rowIsOdd(row) ? hex + 16 : hex + 17;
                break;
            case COMBAT_DIRECTION_4:
                adjacent = hex - 1;
                break;
            case COMBAT_DIRECTION_5:
                adjacent = rowIsOdd(row) ? hex - 18 : hex - 17;
                break;
            }

            if (validHex(adjacent)) {
                int adjacentColumn = gridX(adjacent);
                if (adjacentColumn != 0
                        && adjacentColumn != COMBAT_GRID_LAST_COLUMN) {
                    m_adjacentCells[hex][direction] =
                        static_cast<short>(adjacent);
                    continue;
                }
            }
            m_adjacentCells[hex][direction] = -1;
        }
    }
}

bool combatManager::hexIsBlocked(int index) const
{
    if (m_fortificationLevel > 0
            && (index == COMBAT_HEX_GATE || index == COMBAT_HEX_GATE_MOAT)) {
        if (m_drawbridgeState == DRAWBRIDGE_UP && !doorCanBeLowered())
            return 1;
    } else if (m_cells[index].m_attributes & hexcell::blocked)
        return 1;
    return 0;
}

army* hexcell::getArmy() const
{
    // Dreamcast hexcell.cpp:39 calls the canonical HexCell.h helper.
    if (hasArmy())
        return &g_combatManager->m_armies[m_armySide][m_armySlot];
    return 0;
}
