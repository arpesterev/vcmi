void army::initialize(TCreatureType type, long number, const hero* owner,
                      long newGroup, long newIndex, long newGridIndex)
{
    initClean();
    m_creatureType = type;
    m_numTroops = number;
    m_drawPriority = 4;
    TCreatureTypeTraits* traits = &m_monInfo;
    *traits = akCreatureTypeTraits[type];
    traits->townType = g_game->getAlignment(type);
    if (owner != 0)
        owner->applyCreatureStatBonuses(type, traits);
    if (g_combatManager->m_magicTerrain
            != COMBAT_SPELL_RESTRICTION_NO_CREATURE_SPELLS
        && townManager::getNativeTerrain(m_monInfo.townType)
               == g_combatManager->m_terrainType)
        m_onNativeTerrain = 1;
    else
        m_onNativeTerrain = 0;
    if (m_onNativeTerrain) {
        if (!is(creatureSiegeWeapon))
            m_monInfo.speed++;
        m_monInfo.attackSkill++;
        m_monInfo.defenseSkill++;
    }
    m_facing = 1 - newGroup;
    m_currFrameType = cs_wait;
    groupToAttack = -1;
    group = newGroup;
    indexToAttack = -1;
    index = newIndex;
    m_gridIndex = newGridIndex;
    m_numTroopsBattleResurrected = 0;
    m_currFrameIndex = 0;
    m_luckStatus = 0;
    m_topCreatureDamage = 0;
    m_showAttackFrames = 0;
    m_residualBlindness = 0;
    m_residualParalyze = 0;
    m_allUnitsKilled = 0;
    m_someUnitsDamaged = 0;
    m_showFireShield = 0;
    m_origNumTroops = m_numTroops;
    m_baseSpeed = m_monInfo.speed;
    m_origHitPoints = m_monInfo.hitPoints;
    m_poisonPenalty = 1.0f;
}
