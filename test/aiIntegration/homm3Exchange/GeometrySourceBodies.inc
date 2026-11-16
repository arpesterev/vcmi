// Exact source bodies from sushi-shi/homm3-decomp e12906accf96c2662b1abadbc49ddc111991bf21/src/cmbtmgr.cpp (CC0-1.0).
long combatManager::getDistance(long start, long stop)
{
    int sx = gridX(start);
    int sy = gridY(start);
    int tx = gridX(stop);
    int ty = gridY(stop);
    long a = (sy + 1) / 2 - (ty + 1) / 2 - sx + tx;
    long b = ty / 2 - sy / 2 - sx + tx;
    if ((a < 0) == (b < 0))
        return max(abs(a), abs(b));  // DC 0x62ed8: includes.h wrapper.
    return abs(a) + abs(b);
}

bool combatManager::shotIsNotOptimal(const army* attacker, const army* defender) const
{
    int side = attacker->getControllingSide();
    if (m_heroes[side]
            && (m_heroes[side]->isWieldingArtifact(ARTIFACT_GOLDEN_BOW)
                || m_heroes[side]->isWieldingArtifact(
                    ARTIFACT_BOW_OF_THE_SHARPSHOOTER)))
        return 0;
    if (attacker->m_creatureType == CREATURE_ARROW_TOWER
            || attacker->m_creatureType == CREATURE_SHARPSHOOTER)
        return 0;

    int source = attacker->m_gridIndex;
    int dest = defender->m_gridIndex;
    if (attacker->is(creatureDoubleWide))
        source = attacker->getSecondGridIndex();
    if (getDistance(source, dest) <= 10)
        return 0;
    if (!defender->is(creatureDoubleWide))
        return 1;
    dest = defender->getSecondGridIndex();
    return getDistance(source, dest) > 10;
}
