
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <iostream>
#include <stdexcept>
#include "../../../AI/Integration/Ports/FHeroes2OrdinaryThreat.h"
namespace fheroes2 {
enum class MonsterAbilityType {NO_MELEE_PENALTY,DOUBLE_DAMAGE_TO_UNDEAD,UNDEAD,EARTH_CREATURE,AIR_CREATURE,FIRE_CREATURE,WATER_CREATURE,UNLIMITED_RETALIATION};
enum class MonsterWeaknessType {DOUBLE_DAMAGE_FROM_EARTH_CREATURES,DOUBLE_DAMAGE_FROM_AIR_CREATURES,DOUBLE_DAMAGE_FROM_FIRE_CREATURES,DOUBLE_DAMAGE_FROM_WATER_CREATURES};
template<class T,class U> std::optional<T> checkedCast(U x){return static_cast<T>(x);}
}
namespace Skill {struct Secondary {enum{ARCHERY};};}
struct Commander {uint32_t GetSecondarySkillValue(int)const{return 0;}};
struct Spell {enum {SHIELD,BLIND,DRAGONSLAYER}; explicit Spell(int){} uint32_t ExtraValue()const{return 0;}};
struct GameStatic {static int getCastleWallRangedPenalty(){return 0;}};
namespace Battle {
enum {SP_CURSE=1,SP_BLESS=2,CAP_TOWER=4,SP_SHIELD=8,SP_STONE=16,SP_DRAGONSLAYER=32,TR_RETALIATED=64,CAP_MIRRORIMAGE=128,SP_HYPNOTIZE=256,IS_PARALYZE_MAGIC=512,SP_BLIND=1024};
struct Unit;
struct Arena {bool IsShootingPenalty(const Unit&,const Unit&)const{return false;}};
struct Monster {uint32_t minDamage,maxDamage;uint32_t GetDamageMin()const{return minDamage;}uint32_t GetDamageMax()const{return maxDamage;}};
struct ArmyTroop:Monster {uint32_t count;uint32_t GetDamageMin()const{return count*minDamage;}uint32_t GetDamageMax()const{return count*maxDamage;}};
struct Unit:ArmyTroop {
 uint32_t _hitPoints,hp;int attack,defense;bool retaliated=false;bool _blindRetaliation=false;
 explicit Unit(const AIIntegration::Ports::FHeroes2::OrdinaryThreatUnit& x) {minDamage=x.minimumDamage;maxDamage=x.maximumDamage;count=x.count;_hitPoints=x.health;hp=x.hitPoints;attack=x.attack;defense=x.defense;retaliated=!x.canRetaliate;}
 bool Modes(int flags)const{return retaliated&&(flags&TR_RETALIATED);}
 bool isArchers()const{return false;}bool isHandFighting()const{return false;}
 static bool isHandFighting(const Unit&,const Unit&){return false;}
 bool isAbilityPresent(fheroes2::MonsterAbilityType)const{return false;}
 bool isWeaknessPresent(fheroes2::MonsterWeaknessType)const{return false;}
 bool isDragons()const{return false;}
 const Commander* GetCommander()const{return nullptr;}
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
int main(){
 using namespace AIIntegration::Ports::FHeroes2;
 uint32_t seed=19371;auto next=[&](){seed=seed*1664525u+1013904223u;return seed;};
 for(int i=0;i<8192;++i){
  auto facts=[&](){OrdinaryThreatUnit u;u.count=1+next()%500;u.hitPoints=1+next()%300;u.health=u.count*u.hitPoints-next()%u.hitPoints;u.minimumDamage=next()%40;u.maximumDamage=u.minimumDamage+next()%40;u.attack=next()%60;u.defense=next()%60;u.canRetaliate=next()%3;return u;};
  const auto a=facts(),d=facts();Battle::Unit sa(a),sd(d);const auto damage=sa.getPotentialDamage(sd);const auto retaliation=sd.EstimateRetaliatoryDamage(damage);
  const auto actual=ordinaryThreatDamage(a,d);
  if(!actual||actual->potential!=damage||actual->retaliation!=retaliation)throw std::runtime_error("source differential mismatch");
 }
 // Per-creature integer midpoint and post-hit wounded count matter independently.
 OrdinaryThreatUnit a{1,10,10,1,1,0,0,true},d{3,21,10,1,2,0,0,true};
 const auto result=ordinaryThreatDamage(a,d);if(!result||result->potential!=1||result->retaliation!=2)throw std::runtime_error("wounded retaliation");
 a.count=0;if(ordinaryThreatDamage(a,d))throw std::runtime_error("invalid count accepted");
 std::cout<<"8192 pinned-body differential + wounded integer oracle + invalid count PASS\n";
}
