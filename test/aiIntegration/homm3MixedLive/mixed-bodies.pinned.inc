unsigned char combatManager::chooseDefenseHex(const army* currentArmy, const army* client, long* bestHex, long* openHexes, searchArray* currentSearchArray)
{
    long bestTravelTime;
    long bestHexesCovered;

    *openHexes = 0;
    *bestHex = -1;
    for (long direction = 0; direction < COMBAT_DIRECTION_WIDE_LOWER + 1; direction++) {
        if (direction >= COMBAT_DIRECTION_COUNT && !client->is(creatureDoubleWide))
            continue;
        long hex = client->getAdjacentHex(direction);
        if (!combatManager::validHex(hex))
            continue;
        hexcell* cell = &m_cells[hex];
        army* occupant = cell->getArmy();
        if (occupant != 0 && occupant != currentArmy)
            continue;
        (*openHexes)++;
        const pathCell* path = currentSearchArray->getHex(hex);
        if (!path->m_visited)
            continue;
        long travelTime = m_creaturePlacement
                ? 1
                : currentSearchArray->getTravelTime(currentArmy, hex);
        long hexesCovered;
        if (currentArmy->is(creatureDoubleWide)
                && client->isAdjacent(
                    hex + currentArmy->offsetToFront(-1)))
            hexesCovered = 2;
        else
            hexesCovered = 1;
        if (*bestHex >= 0) {
            if (travelTime > bestTravelTime)
                continue;
            if (travelTime == bestTravelTime) {
                if (hexesCovered < bestHexesCovered)
                    continue;
                if (hexesCovered == bestHexesCovered) {
                    if (client->m_facing == 1) {
                        if (cell->m_refX < m_cells[*bestHex].m_refX)
                            continue;
                    } else if (cell->m_refX > m_cells[*bestHex].m_refX) {
                        continue;
                    }
                }
            }
        }
        *bestHex = hex;
        bestTravelTime = travelTime;
        bestHexesCovered = hexesCovered;
    }
    return static_cast<unsigned char>(*bestHex >= 0);
}

unsigned char combatManager::attemptShooterDefense(const army* currentArmy, searchArray* currentSearchArray, const type_AI_combat_parameters* estimate)
{
    long hex;

    long bestHex = -1;
    const army* client = m_armies[m_currentSide];
    long openHexes = 0;
    const army* bestClient = 0;
    long bestTime = 0;
    long bestValue = 0;
    for (long i = 0; i < m_numArmies[m_currentSide]; i++, client++) {
        if (client->is(creatureImmobilized))
            continue;
        if (client->m_creatureType == CREATURE_ARROW_TOWER
                || client->m_creatureType == CREATURE_CATAPULT)
            continue;
        if (!client->canShoot(0))
            continue;
        if (client->is(creatureNoMeleePenalty))
            continue;
        if (!chooseDefenseHex(currentArmy, client, &hex, &openHexes,
                                currentSearchArray))
            continue;
        long time = m_creaturePlacement
                ? 1
                : currentSearchArray->getTravelTime(currentArmy, hex);
        long value = client->getTotalCombatValue(estimate->m_lowestAttack,
                                                    estimate->m_lowestDefense);
        if (openHexes > 1)
            value /= openHexes;
        if (bestClient != 0) {
            if (time > bestTime)
                continue;
            if (time == bestTime && bestValue > value)
                continue;
        }
        bestValue = value;
        bestClient = client;
        bestTime = time;
        bestHex = hex;
    }
    if (bestClient == 0)
        return 0;
    if (hex == currentArmy->m_gridIndex) {
        m_nextAction = 3;
        return 1;
    }
    moveToward(currentArmy, bestHex, 0, 0);
    return 1;
}

void combatManager::doCompAI(int whichGroup)
{
    m_lastMovedArmy = 0;
    turnOffHighlighter(1);
    army* currentArmy = getCurrentArmy();
    currentArmy->groupToAttack = -1;
    currentArmy->indexToAttack = -1;
    long action;
    if (currentArmy->canShoot(0)
            || currentArmy->m_creatureType == CREATURE_BALLISTA)
        action = 1;
    else if (currentArmy->is(creatureFlyingArmy))
        action = 2;
    else
        action = 3;
    m_nextAction = 3;
    if (action == 1) {
        if (m_creaturePlacement)
            placeShooter(currentArmy);
        else
            chooseShooterAction(currentArmy, 0, m_currentSide);
    } else {
        chooseMeleeAction(currentArmy, 0, 0, m_currentSide);
    }
}

long searchArray::getTravelTime(const army* currentArmy, long hex) const
{
    const pathCell* cell = getHex(hex);
    long speed = currentArmy->getSpeed();
    long turns = (cell->m_cost + speed - 1) / speed;

    if (turns < 1)
        turns = 1;
    return turns;
}
