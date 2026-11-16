from pathlib import Path
p=Path(__file__).parent
source=(p/'pinned-battle_troop.cpp').read_text()
def body(signature):
 start=source.index(signature); brace=source.index('{',start); depth=1;end=brace+1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
functions=['uint32_t Battle::Unit::EstimateRetaliatoryDamage','uint32_t Battle::Unit::CalculateMinDamage','uint32_t Battle::Unit::CalculateMaxDamage','uint32_t Battle::Unit::getPotentialDamage','uint32_t Battle::Unit::CalculateDamageUnit']
stubs=r'''
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
'''
main=r'''
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
'''
(p/'ThreatOracle.cpp').write_text(stubs+'\n\n'.join(body(s) for s in functions)+main)

header = stubs[stubs.index('namespace fheroes2'):]+ '\n\n'.join(body(s) for s in functions)
(p/'PinnedThreatFixtureOracle.h').write_text('#pragma once\n#include <algorithm>\n#include <cassert>\n#include <cstdint>\n#include <optional>\n#include "../../../AI/Integration/Ports/FHeroes2OrdinaryThreat.h"\nnamespace { namespace FHeroesThreatFixtureOracle {\n'+header+'\nstatic inline AIIntegration::Ports::FHeroes2::OrdinaryThreatDamage value(const AIIntegration::Ports::FHeroes2::OrdinaryThreatUnit& a,const AIIntegration::Ports::FHeroes2::OrdinaryThreatUnit& d) { Battle::Unit sa(a),sd(d);const auto damage=sa.getPotentialDamage(sd);return {damage,sd.EstimateRetaliatoryDamage(damage)};}\n}}\n')
