#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include "../../../AI/Integration/Ports/FHeroes2OrdinaryThreat.h"
namespace { namespace FHeroesThreatFixtureOracle {
namespace fheroes2 {
enum class MonsterAbilityType {NO_MELEE_PENALTY,DOUBLE_DAMAGE_TO_UNDEAD,UNDEAD,EARTH_CREATURE,AIR_CREATURE,FIRE_CREATURE,WATER_CREATURE,UNLIMITED_RETALIATION};
enum class MonsterWeaknessType {DOUBLE_DAMAGE_FROM_EARTH_CREATURES,DOUBLE_DAMAGE_FROM_AIR_CREATURES,DOUBLE_DAMAGE_FROM_FIRE_CREATURES,DOUBLE_DAMAGE_FROM_WATER_CREATURES};
template<class T,class U> std::optional<T> checkedCast(U x){return static_cast<T>(x);}
}
namespace Skill {struct Secondary {enum{ARCHERY};};}
struct Commander {uint32_t archeryPercent=0;uint32_t GetSecondarySkillValue(int)const{return archeryPercent;}};
struct Spell {enum {SHIELD,BLIND,DRAGONSLAYER}; int id; explicit Spell(int value):id(value){} uint32_t ExtraValue()const{return id==SHIELD?2:0;}};
struct GameStatic {static int getCastleWallRangedPenalty(){return 0;}};
namespace Battle {
enum {SP_CURSE=1,SP_BLESS=2,CAP_TOWER=4,SP_SHIELD=8,SP_STONE=16,SP_DRAGONSLAYER=32,TR_RETALIATED=64,CAP_MIRRORIMAGE=128,SP_HYPNOTIZE=256,IS_PARALYZE_MAGIC=512,SP_BLIND=1024};
struct Unit;
struct Arena {bool IsShootingPenalty(const Unit&,const Unit&)const{return false;}};
struct Monster {uint32_t minDamage,maxDamage;uint32_t GetDamageMin()const{return minDamage;}uint32_t GetDamageMax()const{return maxDamage;}};
struct ArmyTroop:Monster {uint32_t count;uint32_t GetDamageMin()const{return count*minDamage;}uint32_t GetDamageMax()const{return count*maxDamage;}};
struct Unit:ArmyTroop {
 uint32_t _hitPoints,hp;int attack,defense;bool retaliated=false;bool _blindRetaliation=false;bool archer=false;bool blocked=false;bool noMeleePenalty=false;int damageMode=0;bool shield=false;Commander commander;
 explicit Unit(const AIIntegration::Ports::FHeroes2::OrdinaryThreatUnit& x) {minDamage=x.minimumDamage;maxDamage=x.maximumDamage;count=x.count;_hitPoints=x.health;hp=x.hitPoints;attack=x.attack;defense=x.defense;retaliated=!x.canRetaliate;commander.archeryPercent=x.archeryPercent;noMeleePenalty=x.noMeleePenalty;damageMode=x.damageMode;shield=x.shield;}
 bool Modes(int flags)const{return (retaliated&&(flags&TR_RETALIATED)) || (damageMode<0&&(flags&SP_CURSE)) || (damageMode>0&&(flags&SP_BLESS)) || (shield&&(flags&SP_SHIELD));}
 bool isArchers()const{return archer;}bool isHandFighting()const{return blocked;}
 static bool isHandFighting(const Unit&,const Unit&){return false;}
 bool isAbilityPresent(fheroes2::MonsterAbilityType type)const{return type==fheroes2::MonsterAbilityType::NO_MELEE_PENALTY&&noMeleePenalty;}
 bool isWeaknessPresent(fheroes2::MonsterWeaknessType)const{return false;}
 bool isDragons()const{return false;}
 const Commander* GetCommander()const{return commander.archeryPercent?&commander:nullptr;}
 const Arena* GetArena()const {static Arena a;return &a;}
 int GetAttack()const{return attack;}int GetDefense()const{return defense;}
 uint32_t EstimateRetaliatoryDamage(uint32_t)const;
 uint32_t CalculateMinDamage(const Unit&)const;uint32_t CalculateMaxDamage(const Unit&)const;
 uint32_t getPotentialDamage(const Unit&)const;uint32_t CalculateDamageUnit(const Unit&,double)const;
};
uint32_t GetCountFromHitPoints(const Unit& u,uint32_t hp){return hp/u.hp+(hp%u.hp!=0);}
}
uint32_t Battle::Unit::EstimateRetaliatoryDamage( const uint32_t damageTaken ) const
{
    // The entire unit is destroyed, no retaliation
    if ( damageTaken >= _hitPoints ) {
        return 0;
    }

    // Mirror images are destroyed anyway and hypnotized units never respond to an attack
    if ( Modes( TR_RETALIATED | CAP_MIRRORIMAGE | SP_HYPNOTIZE ) ) {
        return 0;
    }

    // Units with this ability retaliate even when under the influence of paralyzing spells
    if ( Modes( IS_PARALYZE_MAGIC ) && !isAbilityPresent( fheroes2::MonsterAbilityType::UNLIMITED_RETALIATION ) ) {
        return 0;
    }

    const uint32_t unitsLeft = GetCountFromHitPoints( *this, _hitPoints - damageTaken );
    assert( unitsLeft > 0 );

    const uint32_t damagePerUnit = [this]() {
        if ( Modes( SP_CURSE ) ) {
            return Monster::GetDamageMin();
        }

        if ( Modes( SP_BLESS ) ) {
            return Monster::GetDamageMax();
        }

        return ( Monster::GetDamageMin() + Monster::GetDamageMax() ) / 2;
    }();

    const uint32_t retaliatoryDamage = unitsLeft * damagePerUnit;

    if ( !Modes( SP_BLIND ) ) {
        return retaliatoryDamage;
    }

    // The retaliatory damage of a blinded unit is reduced
    const uint32_t reductionPercent = Spell( Spell::BLIND ).ExtraValue();
    assert( reductionPercent <= 100 );

    return retaliatoryDamage * ( 100 - reductionPercent ) / 100;
}

uint32_t Battle::Unit::CalculateMinDamage( const Unit & enemy ) const
{
    return CalculateDamageUnit( enemy, ArmyTroop::GetDamageMin() );
}

uint32_t Battle::Unit::CalculateMaxDamage( const Unit & enemy ) const
{
    return CalculateDamageUnit( enemy, ArmyTroop::GetDamageMax() );
}

uint32_t Battle::Unit::getPotentialDamage( const Unit & enemy ) const
{
    if ( Modes( Battle::SP_CURSE ) ) {
        return CalculateMinDamage( enemy );
    }

    if ( Modes( Battle::SP_BLESS ) ) {
        return CalculateMaxDamage( enemy );
    }

    return ( CalculateMinDamage( enemy ) + CalculateMaxDamage( enemy ) ) / 2;
}

uint32_t Battle::Unit::CalculateDamageUnit( const Unit & enemy, double dmg ) const
{
    if ( isArchers() ) {
        // Melee penalty can be applied either if the archer is blocked by enemy units and cannot shoot,
        // or if he is not blocked, but was attacked by a friendly unit (for example, in the case of using
        // Berserk or Hypnotize spells) and deals retaliatory damage
        if ( !isHandFighting() && !isHandFighting( *this, enemy ) ) {
            // Hero's Archery skill may increase damage
            if ( GetCommander() ) {
                dmg += ( dmg * GetCommander()->GetSecondarySkillValue( Skill::Secondary::ARCHERY ) / 100 );
            }

            const Arena * arena = GetArena();
            assert( arena != nullptr );

            // Penalty for damage to castle defenders behind the castle walls
            if ( arena->IsShootingPenalty( *this, enemy ) ) {
                dmg *= 1 - ( GameStatic::getCastleWallRangedPenalty() / 100.0 );
            }

            // The Shield spell does not affect the damage of the castle towers
            if ( !Modes( CAP_TOWER ) && enemy.Modes( SP_SHIELD ) ) {
                dmg /= Spell( Spell::SHIELD ).ExtraValue();
            }
        }
        else if ( !isAbilityPresent( fheroes2::MonsterAbilityType::NO_MELEE_PENALTY ) ) {
            dmg /= 2;
        }
    }

    // The retaliatory damage of a blinded unit is reduced
    if ( _blindRetaliation ) {
        // Petrified units cannot attack, respectively, there should be no retaliation
        assert( !enemy.Modes( SP_STONE ) );

        const uint32_t reductionPercent = Spell( Spell::BLIND ).ExtraValue();
        assert( reductionPercent <= 100 );

        dmg = dmg * ( 100 - reductionPercent ) / 100;
    }

    // A petrified unit takes only half of the damage
    if ( enemy.Modes( SP_STONE ) ) {
        // Petrified units cannot attack, respectively, there should be no retaliation
        assert( !_blindRetaliation );

        dmg /= 2;
    }

    // If multiple options are suitable at the same time, the damage should be doubled only once
    if ( ( isAbilityPresent( fheroes2::MonsterAbilityType::DOUBLE_DAMAGE_TO_UNDEAD ) && enemy.isAbilityPresent( fheroes2::MonsterAbilityType::UNDEAD ) )
         || ( isAbilityPresent( fheroes2::MonsterAbilityType::EARTH_CREATURE )
              && enemy.isWeaknessPresent( fheroes2::MonsterWeaknessType::DOUBLE_DAMAGE_FROM_EARTH_CREATURES ) )
         || ( isAbilityPresent( fheroes2::MonsterAbilityType::AIR_CREATURE )
              && enemy.isWeaknessPresent( fheroes2::MonsterWeaknessType::DOUBLE_DAMAGE_FROM_AIR_CREATURES ) )
         || ( isAbilityPresent( fheroes2::MonsterAbilityType::FIRE_CREATURE )
              && enemy.isWeaknessPresent( fheroes2::MonsterWeaknessType::DOUBLE_DAMAGE_FROM_FIRE_CREATURES ) )
         || ( isAbilityPresent( fheroes2::MonsterAbilityType::WATER_CREATURE )
              && enemy.isWeaknessPresent( fheroes2::MonsterWeaknessType::DOUBLE_DAMAGE_FROM_WATER_CREATURES ) ) ) {
        dmg *= 2;
    }

    int r = GetAttack() - enemy.GetDefense();
    if ( enemy.isDragons() && Modes( SP_DRAGONSLAYER ) ) {
        r += Spell( Spell::DRAGONSLAYER ).ExtraValue();
    }

    // Attack bonus is 20% to 300%
    dmg *= 1 + ( 0 < r ? 0.1 * std::min( r, 20 ) : 0.05 * std::max( r, -16 ) );

    return std::max<uint32_t>( fheroes2::checkedCast<uint32_t>( dmg ).value(), 1U );
}
static inline AIIntegration::Ports::FHeroes2::OrdinaryThreatDamage value(const AIIntegration::Ports::FHeroes2::OrdinaryThreatUnit& a,const AIIntegration::Ports::FHeroes2::OrdinaryThreatUnit& d) { Battle::Unit sa(a),sd(d);const auto damage=sa.getPotentialDamage(sd);return {damage,sd.EstimateRetaliatoryDamage(damage)};}
// Same unmodified pinned CalculateDamageUnit body, exercising its ranged branch.
// No commander, walls, or spell effects in this explicitly bounded oracle.
static inline AIIntegration::Ports::FHeroes2::OrdinaryThreatDamage rangedValue(const AIIntegration::Ports::FHeroes2::OrdinaryThreatUnit& a,const AIIntegration::Ports::FHeroes2::OrdinaryThreatUnit& d) { Battle::Unit sa(a),sd(d);sa.archer=true;return {sa.getPotentialDamage(sd),0};}
static inline AIIntegration::Ports::FHeroes2::OrdinaryThreatDamage archerMeleeValue(const AIIntegration::Ports::FHeroes2::OrdinaryThreatUnit& a,const AIIntegration::Ports::FHeroes2::OrdinaryThreatUnit& d,bool hasAmmo) { Battle::Unit sa(a),sd(d);sa.archer=hasAmmo;sa.blocked=true;const auto damage=sa.getPotentialDamage(sd);return {damage,sd.EstimateRetaliatoryDamage(damage)};}
// Defender archer state is explicit, while the pinned retaliation method
// intentionally uses innate damage midpoint without an archer /2 penalty.
static inline AIIntegration::Ports::FHeroes2::OrdinaryThreatDamage archerDefenderValue(const AIIntegration::Ports::FHeroes2::OrdinaryThreatUnit& a,const AIIntegration::Ports::FHeroes2::OrdinaryThreatUnit& d,bool defenderHasAmmo,bool rangedAttack=false) { Battle::Unit sa(a),sd(d);sa.archer=rangedAttack;sd.archer=defenderHasAmmo;const auto damage=sa.getPotentialDamage(sd);return {damage,rangedAttack?0:sd.EstimateRetaliatoryDamage(damage)};}
}}



