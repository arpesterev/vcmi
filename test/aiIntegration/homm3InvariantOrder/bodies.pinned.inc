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
