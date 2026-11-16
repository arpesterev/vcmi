double commanderMaximumSpellDamageValue( const HeroBase & commander )
    {
        double bestValue = 0;

        for ( const Spell & spell : commander.getAllSpells() ) {
            if ( !spell.isCombat() || !spell.isDamage() ) {
                continue;
            }

            if ( commander.GetSpellPoints() < spell.spellPoints() ) {
                continue;
            }

            bestValue = std::max( bestValue, static_cast<double>( fheroes2::getSpellDamage( spell, commander.GetPower(), &commander ) ) );
        }

        return bestValue;
    }

// Hero branch, extracted verbatim from analyzeBattleState:
    // Calculate each hero spell strength and add it to shooter values after castle modifiers were applied
    if ( _commander && _myShootersStrength > 1 ) {
        _myShootersStrength += commanderMaximumSpellDamageValue( *_commander );
    }

    const HeroBase * enemyCommander = arena.getEnemyCommander( _myColor );
    if ( enemyCommander ) {
        _enemySpellStrength = enemyCommander->GetMagicStrategicValue( _myArmyStrength );
        _enemyShootersStrength += commanderMaximumSpellDamageValue( *enemyCommander );
    }

