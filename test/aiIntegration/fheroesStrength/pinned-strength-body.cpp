    double getMonsterBaseStrength( const fheroes2::MonsterData & data )
    {
        const fheroes2::MonsterBattleStats & battleStats = data.battleStats;
        const std::vector<fheroes2::MonsterAbility> & abilities = battleStats.abilities;

        const double effectiveHP = battleStats.hp * ( fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::NO_ENEMY_RETALIATION ) ? 1.4 : 1 );
        const bool isArchers = ( battleStats.shots > 0 );

        double damagePotential = ( battleStats.damageMin + battleStats.damageMax ) / 2.0;

        if ( fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::DOUBLE_SHOOTING ) ) {
            // How can it be that this ability is assigned not to a shooter?
            assert( isArchers );

            damagePotential *= 2;
        }
        else if ( fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::DOUBLE_MELEE_ATTACK ) ) {
            // Melee attacker will lose potential on second attack after retaliation
            damagePotential *= fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::NO_ENEMY_RETALIATION ) ? 2 : 1.75;
        }

        if ( fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::DOUBLE_DAMAGE_TO_UNDEAD ) ) {
            damagePotential *= 1.15; // 15% of all Monsters are Undead, deals double damage.
        }

        if ( fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::TWO_CELL_MELEE_ATTACK ) ) {
            damagePotential *= 1.2;
        }

        if ( fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::UNLIMITED_RETALIATION ) ) {
            damagePotential *= 1.25;
        }

        if ( fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::ALL_ADJACENT_CELL_MELEE_ATTACK )
             || fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::AREA_SHOT ) ) {
            damagePotential *= 1.3;
        }

        double monsterSpecial = 1.0;

        if ( isArchers ) {
            monsterSpecial += fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::NO_MELEE_PENALTY ) ? 0.5 : 0.4;
        }

        if ( fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::FLYING ) ) {
            monsterSpecial += 0.3;
        }

        if ( fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::ENEMY_HALVING ) ) {
            monsterSpecial += 1;
        }

        if ( fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::SOUL_EATER ) ) {
            monsterSpecial += 2;
        }

        if ( fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::HP_DRAIN ) ) {
            monsterSpecial += 0.3;
        }

        if ( const auto abilityIter = std::find( abilities.begin(), abilities.end(), fheroes2::MonsterAbilityType::SPELL_CASTER ); abilityIter != abilities.end() ) {
            // This is a tricky evaluation. Spell casting ability depends on a type of spell and chance to inflict the spell.
            switch ( abilityIter->value ) {
            case Spell::PARALYZE:
            case Spell::BLIND:
            case Spell::PETRIFY:
                monsterSpecial += abilityIter->percentage / 100.0;
                break;
            case Spell::DISPEL:
            case Spell::CURSE:
                // These spell are very weak and do not impact much during battle.
                monsterSpecial += abilityIter->percentage / 100.0 / 10.0;
                break;
            default:
                // Did you add a new spell casting ability? Add the logic above!
                assert( 0 );
                break;
            }
        }

        // Higher speed gives initiative advantage/first attack. Remap speed value to -0.2...+0.15, AVERAGE is 0
        // Punish slow speeds more as unit won't participate in first rounds and slows down strategic army
        const int speedDiff = battleStats.speed - Speed::AVERAGE;
        monsterSpecial += ( speedDiff < 0 ) ? speedDiff * 0.1 : speedDiff * 0.05;

        // Additional HP and Damage effectiveness diminishes with every combat round; strictly x4 HP == x2 unit count
        return sqrt( damagePotential * effectiveHP ) * monsterSpecial;
    }
