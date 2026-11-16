    // Add castle siege (and battle arena) modifiers
    const Castle * castle = Battle::Arena::GetCastle();

    // Mark as castle siege only if any tower is present. If no towers present then nothing to defend and most likely all walls are destroyed as well.
    if ( castle && Battle::Arena::isAnyTowerPresent() ) {
        const bool attackerIgnoresCover = [&arena]() {
            const HeroBase * commander = arena.getAttackingForce().GetCommander();
            assert( commander != nullptr );

            if ( commander->GetBagArtifacts().isArtifactBonusPresent( fheroes2::ArtifactBonusType::NO_SHOOTING_PENALTY ) ) {
                return true;
            }

            if ( commander->GetLevelSkill( Skill::Secondary::ARCHERY ) != Skill::Level::NONE ) {
                return true;
            }

            return false;
        }();

        const auto getTowerStrength = []( const Battle::Tower * tower ) { return ( tower && tower->isValid() ) ? tower->GetStrength() : 0; };

        double towerStr = getTowerStrength( Battle::Arena::GetTower( Battle::TowerType::TWR_CENTER ) );
        towerStr += getTowerStrength( Battle::Arena::GetTower( Battle::TowerType::TWR_LEFT ) );
        towerStr += getTowerStrength( Battle::Arena::GetTower( Battle::TowerType::TWR_RIGHT ) );

        DEBUG_LOG( DBG_BATTLE, DBG_TRACE, "- Castle strength: " << towerStr )

        // Tower strength can't be negative. If this assertion triggers something is wrong with the logic above.
        assert( towerStr >= 0 );

        if ( _myColor == castle->GetColor() ) {
            _defendingCastle = true;
            _myShootersStrength += towerStr;

            if ( !attackerIgnoresCover ) {
                _enemyShootersStrength /= 1 + ( GameStatic::getCastleWallRangedPenalty() / 100.0 );
            }
        }
        else {
            _attackingCastle = true;
            _enemyShootersStrength += towerStr;

            if ( !attackerIgnoresCover ) {
                _myShootersStrength /= 1 + ( GameStatic::getCastleWallRangedPenalty() / 100.0 );
            }
        }
    }
