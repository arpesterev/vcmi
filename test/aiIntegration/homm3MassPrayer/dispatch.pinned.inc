case SPELL_DISPEL: {
        considerEnchantment(choice, m_side);
        if (choice.m_mastery == eMasteryAdvanced)
            considerEnchantment(choice, m_enemySide);
        if (choice.m_mastery == eMasteryExpert) {
            type_spell_choice mirror = choice;
            considerEnchantment(mirror, m_enemySide);
            choice.m_value += mirror.m_value;
        }
        break;
    }
