from pathlib import Path
w=Path(__file__).resolve().parent
s=(w/'pinned-monster_info.cpp').read_text();start=s.index('    double getMonsterBaseStrength(');end=s.index('\n    void populateMonsterData()',start)
body=s[start:end]
monster=(w/'pinned-monster.cpp').read_text();a=monster.index('double Monster::GetMonsterStrength(');b=monster.index('\nuint32_t Monster::GetRNDSize()',a);monsterbody=monster[a:b]
troop=(w/'pinned-army_troop.cpp').read_text();a=troop.index('double Troop::GetStrength()');b=troop.index('\ndouble Troop::GetStrengthWithBonus',a);troopbody=troop[a:b]
head=r'''#include <algorithm>
#include <cstdint>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#include <random>
#include "../../../AI/Integration/Ports/FHeroes2Strength.h"
#include "pinned-speed.h"
namespace Spell { enum {BLIND,PARALYZE,PETRIFY,DISPEL,CURSE}; }
namespace fheroes2 {
enum class MonsterAbilityType { NO_ENEMY_RETALIATION, DOUBLE_SHOOTING, DOUBLE_MELEE_ATTACK, DOUBLE_DAMAGE_TO_UNDEAD, TWO_CELL_MELEE_ATTACK, UNLIMITED_RETALIATION, ALL_ADJACENT_CELL_MELEE_ATTACK, AREA_SHOT, NO_MELEE_PENALTY, FLYING, ENEMY_HALVING, SOUL_EATER, HP_DRAIN, SPELL_CASTER };
struct MonsterAbility { MonsterAbilityType type; int value=0; int percentage=0; bool operator==(MonsterAbilityType t) const {return type==t;} };
struct MonsterBattleStats { uint32_t hp,damageMin,damageMax,shots,speed; double monsterBaseStrength; std::vector<MonsterAbility> abilities; };
struct MonsterData { MonsterBattleStats battleStats; };
bool isAbilityPresent(const std::vector<MonsterAbility>& a,MonsterAbilityType t) {return std::find(a.begin(),a.end(),t)!=a.end();}
MonsterData currentData; const MonsterData& getMonsterData(int) {return currentData;}
}
struct Monster { int id=0,attack=0,defense=0; int GetAttack() const {return attack;} int GetDefense() const {return defense;} double GetMonsterStrength(int attack=-1,int defense=-1) const; };
struct Troop : Monster { int _count=0; double GetStrength() const; };
'''
tail=r'''
int main() {
 using namespace AIIntegration::Ports::FHeroes2;
 using A=fheroes2::MonsterAbilityType;
 std::mt19937 rng(0xF2A31);
 int checked=0;
 for(int i=0;i<4096;++i) {
  StrengthFacts f; f.minimumDamage=1+rng()%80;f.maximumDamage=f.minimumDamage+rng()%80;f.hitPoints=1+rng()%1000;f.speed=rng()%26;f.archer=rng()%2;
  fheroes2::MonsterData d; auto& b=d.battleStats;b.hp=f.hitPoints;b.damageMin=f.minimumDamage;b.damageMax=f.maximumDamage;b.shots=f.archer?12:0;b.speed=f.speed;
  auto add=[&](bool& flag,A a) {flag=rng()%2;if(flag)b.abilities.push_back({a});};
  add(f.noRetaliation,A::NO_ENEMY_RETALIATION);
  f.doubleShooting=f.archer && rng()%2;if(f.doubleShooting)b.abilities.push_back({A::DOUBLE_SHOOTING});
  add(f.doubleMelee,A::DOUBLE_MELEE_ATTACK);add(f.doubleDamageUndead,A::DOUBLE_DAMAGE_TO_UNDEAD);
  add(f.twoCellAttack,A::TWO_CELL_MELEE_ATTACK);add(f.unlimitedRetaliation,A::UNLIMITED_RETALIATION);
  add(f.allAdjacentAttack,A::ALL_ADJACENT_CELL_MELEE_ATTACK);add(f.areaShot,A::AREA_SHOT);
  add(f.noMeleePenalty,A::NO_MELEE_PENALTY);add(f.flying,A::FLYING);add(f.enemyHalving,A::ENEMY_HALVING);add(f.soulEater,A::SOUL_EATER);add(f.healthDrain,A::HP_DRAIN);
  const int spell=int(rng()%6);if(spell<5){ f.spell=spell<3?StrengthSpell::DISABLE:StrengthSpell::WEAK;f.spellChance=rng()%101;b.abilities.push_back({A::SPELL_CASTER,spell,f.spellChance});}
  const int attack=rng()%100,defense=rng()%100,count=rng()%10000;
  fheroes2::currentData=d;fheroes2::currentData.battleStats.monsterBaseStrength=getMonsterBaseStrength(d);
  Troop troop;troop.attack=attack;troop.defense=defense;troop._count=count;const double expected=troop.GetStrength();
  const double actual=stackStrength(f,attack,defense,count);
  if(expected!=actual) {std::cerr<<i<<" "<<expected<<" "<<actual<<"\n";return 1;}++checked;
 }
 StrengthFacts f;f.minimumDamage=2;f.maximumDamage=4;f.hitPoints=10;
 if(stackStrength(f,2,3,5)!=(1.0+2*0.1+3*0.05)*std::sqrt(30.0)*5)return 2;
 for(int bad=0;bad<5;++bad) {auto q=f;if(bad==0)q.hitPoints=0;if(bad==1)q.minimumDamage=-1;if(bad==2)q.spellChance=101;if(bad==3)q.doubleShooting=true;if(bad==4)q.spell=static_cast<StrengthSpell>(255);bool threw=false;try{monsterBaseStrength(q);}catch(const std::invalid_argument&){threw=true;}if(!threw)return 3;}
 std::cout<<checked<<" exact pinned-body differential samples; 1 analytic; 5 invalid domains passed\n";
}
'''
(w/'StrengthOracle.cpp').write_text(head+body+monsterbody+troopbody+tail)
(w/'pinned-strength-body.cpp').write_text(body)
