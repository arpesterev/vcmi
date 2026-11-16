float hero::getArcheryFactor() const
{
    float factor = g_archeryFactors[m_skillLevel[eSecSkillArchery]];
    if (m_skillLevel[eSecSkillArchery] > 0) {
        const THeroSpecificAbility& ability = g_heroSpecificAbilities[m_id];
        if (ability.m_type == eHeroAbilitySecondarySkill && ability.m_skill == eSecSkillArchery)
            factor = (m_level * 0.05f + 1.0f) * factor;
        if (isWieldingArtifact(ARTIFACT_BOW_OF_ELVEN_CHERRYWOOD))
            factor = factor + 0.05f;
        if (isWieldingArtifact(ARTIFACT_BOWSTRING_OF_THE_UNICORNS_MANE))
            factor = factor + 0.1f;
        if (isWieldingArtifact(ARTIFACT_ANGEL_FEATHER_ARROWS))
            factor = factor + 0.15f;
    }
    return factor;
}

float hero::getOffenseFactor() const
{
    float factor = g_offenseFactors[m_skillLevel[eSecSkillOffense]];
    if (m_skillLevel[eSecSkillOffense] > 0) {
        const THeroSpecificAbility& ability = g_heroSpecificAbilities[m_id];
        if (ability.m_type == eHeroAbilitySecondarySkill && ability.m_skill == eSecSkillOffense)
            factor = (m_level * 0.05f + 1.0f) * factor;
    }
    return factor;
}

float hero::getDefenseFactor() const
{
    float factor = g_defenseFactors[m_skillLevel[eSecSkillDefense]];
    if (m_skillLevel[eSecSkillDefense] > 0) {
        const THeroSpecificAbility& ability = g_heroSpecificAbilities[m_id];
        if (ability.m_type == eHeroAbilitySecondarySkill && ability.m_skill == eSecSkillDefense)
            factor = (m_level * 0.05f + 1.0f) * factor;
    }
    if (factor > 1.0f)
        factor = 1.0f;
    return 1.0f - factor;
}
