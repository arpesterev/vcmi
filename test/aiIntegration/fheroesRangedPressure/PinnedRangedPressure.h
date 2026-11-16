// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2024 - 2026 fheroes2 contributors.
// Pinned function and hero branch with input-only engine API substitutes.
#pragma once
#include <algorithm>
#include <vector>
namespace PinnedFHeroesRangedPressure
{
struct Spell
{
    bool combat, damage;
    int cost;
    double magnitude;
    bool isCombat() const { return combat; }
    bool isDamage() const { return damage; }
    int spellPoints() const { return cost; }
};
struct HeroBase
{
    int mana;
    std::vector<Spell> spells;
    const std::vector<Spell> & getAllSpells() const { return spells; }
    int GetSpellPoints() const { return mana; }
    int GetPower() const { return 0; }
    double GetMagicStrategicValue(double) const { return 0; }
};
namespace fheroes2
{
inline double getSpellDamage(const Spell & spell, int, const HeroBase *) { return spell.magnitude; }
}
inline double commanderMaximumSpellDamageValue( const HeroBase & commander )
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
struct Planner
{
    const HeroBase * _commander = nullptr;
    double _myShootersStrength = 0, _enemyShootersStrength = 0;
    double _myArmyStrength = 1, _enemySpellStrength = 0;
    int _myColor = 0;
    struct Arena
    {
        const HeroBase * enemy;
        const HeroBase * getEnemyCommander(int) const { return enemy; }
    };
    void apply(const Arena & arena)
    {
    // Calculate each hero spell strength and add it to shooter values after castle modifiers were applied
    if ( _commander && _myShootersStrength > 1 ) {
        _myShootersStrength += commanderMaximumSpellDamageValue( *_commander );
    }

    const HeroBase * enemyCommander = arena.getEnemyCommander( _myColor );
    if ( enemyCommander ) {
        _enemySpellStrength = enemyCommander->GetMagicStrategicValue( _myArmyStrength );
        _enemyShootersStrength += commanderMaximumSpellDamageValue( *enemyCommander );
    }
    }
};
}
