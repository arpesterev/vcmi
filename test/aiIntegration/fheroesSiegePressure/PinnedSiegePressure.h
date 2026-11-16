// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2024 - 2026 fheroes2 contributors.
// Exact pinned analyzeBattleState siege block; input-only engine substitutes.
#pragma once
#include <cassert>
#define DEBUG_LOG(...)
namespace PinnedFHeroesSiegePressure
{
namespace fheroes2 { enum class ArtifactBonusType { NO_SHOOTING_PENALTY }; }
namespace Skill { namespace Secondary { constexpr int ARCHERY=1; } namespace Level { constexpr int NONE=0; } }
struct Artifacts { bool bypass=false; bool isArtifactBonusPresent(fheroes2::ArtifactBonusType) const { return bypass; } };
struct HeroBase
{
    Artifacts artifacts;
    int archery=0;
    const Artifacts & GetBagArtifacts() const { return artifacts; }
    int GetLevelSkill(int) const { return archery; }
};
struct Castle { int GetColor() const { return 1; } };
namespace GameStatic { inline unsigned getCastleWallRangedPenalty() { return 50; } }
namespace Battle
{
enum class TowerType { TWR_CENTER, TWR_LEFT, TWR_RIGHT };
struct Tower { bool valid=false; double strength=0; bool isValid() const { return valid; } double GetStrength() const { return strength; } };
struct Arena
{
    inline static Castle castle;
    inline static bool hasCastle=false;
    inline static Tower towers[3];
    struct Force { HeroBase hero; const HeroBase * GetCommander() const { return &hero; } } attacker;
    const Force & getAttackingForce() const { return attacker; }
    static const Castle * GetCastle() { return hasCastle ? &castle : nullptr; }
    static bool isAnyTowerPresent() { return towers[0].valid || towers[1].valid || towers[2].valid; }
    static const Tower * GetTower(TowerType t) { return &towers[static_cast<int>(t)]; }
};
}
struct Planner
{
    int _myColor=0;
    bool _attackingCastle=false, _defendingCastle=false;
    double _myShootersStrength=0, _enemyShootersStrength=0;
    void apply(const Battle::Arena & arena)
    {
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
    }
};
}
#undef DEBUG_LOG
