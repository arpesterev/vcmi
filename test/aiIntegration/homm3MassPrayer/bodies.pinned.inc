long type_AI_spellcaster::getDispelValue(const army* ourArmy, type_enchant_data caster) const
{
    army testArmy = *ourArmy;
    return getCancelValue(&testArmy, 0);
}

void type_AI_spellcaster::considerEnchantment(type_spell_choice& choice, long group) const
{
    if (spellTargetsASingleArmy(choice.m_spell, choice.m_mastery)) {
        considerSingleEnchantment(choice, group);
        return;
    }
    TEnchantValue valueOf = getEnchantmentFunction(choice.m_spell);
    long value = 0;
    for (long i = 0; i < g_combatManager->m_numArmies[group]; i++) {
        const army* target = &g_combatManager->m_armies[group][i];
        if (target->cannotAttack())
            continue;
        if (target->getSpellTime(choice.m_spell))
            continue;
        long creatureCast = m_isCreatureSpell != 0;
        if (g_combatManager->validSpellTargetArmy(choice.m_spell, m_side, target, 1,
                                            creatureCast))
            value += (this->*valueOf)(target, choice);
    }
    choice.m_castNow = 1;
    choice.m_value = value;
}
