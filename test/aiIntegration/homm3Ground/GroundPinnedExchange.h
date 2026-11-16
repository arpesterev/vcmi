// SPDX-License-Identifier: CC0-1.0
// Scalar reconstructed-source oracle, not a retail executable.
#pragma once
#include "PinnedGround.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <stdexcept>
namespace PinnedGroundExchange
{
using std::min; using std::max; using std::sqrt;
template<class A,class B> auto cppMax(A a,B b) { return a>b?a:b; }
inline long max(long a,int b) { return a>b?a:b; }
constexpr int kNumMasteries=4;
enum { eSecSkillArchery=0,eSecSkillOffense=1,eSecSkillDefense=2,eSecSkillBattlefieldBallistics=3 };
enum { eHeroAbilitySecondarySkill=1, ARTIFACT_BOW_OF_ELVEN_CHERRYWOOD=0,
 ARTIFACT_BOWSTRING_OF_THE_UNICORNS_MANE=1, ARTIFACT_ANGEL_FEATHER_ARROWS=2 };
struct THeroSpecificAbility { int m_type=0,m_skill=-1; };
inline THeroSpecificAbility g_heroSpecificAbilities[2];
#define DATA(address)
#include "../homm3Exchange/hero-factor-tables.pinned.inc"
#undef DATA
struct hero
{
 int m_id=0,m_level=1;
 std::array<int,4> m_skillLevel{};
 std::array<bool,3> artifacts{};
 bool isWieldingArtifact(int id) const { return artifacts.at(id); }
 int getHeroSpellBonus(int,int,int) const { return 0; }
 int getSecondarySkill(int id) const { return m_skillLevel.at(id); }
 float getArcheryFactor() const; float getOffenseFactor() const; float getDefenseFactor() const;
};
#include "../homm3Exchange/hero-factors.pinned.inc"
enum {
 creatureTwoAttacks=1,creatureShootingArmy=2,creatureNoMeleePenalty=4,
 creatureImmuneToMindSpells=8,creatureDoubleWide=16,creatureKing1=32,creatureKing2=64,
 creatureKing3=128,creatureSiegeWeapon=256,creatureSummoned=512,creatureImmobilized=1024,creatureClone=2048
};
enum { CREATURE_CAVALIER=10,CREATURE_CHAMPION,CREATURE_EARTH_ELEMENTAL,CREATURE_MAGMA_ELEMENTAL,
 CREATURE_AIR_ELEMENTAL,CREATURE_STORM_ELEMENTAL,CREATURE_WATER_ELEMENTAL,CREATURE_ICE_ELEMENTAL,
 CREATURE_FIRE_ELEMENTAL,CREATURE_ENERGY_ELEMENTAL,ARMY_CREATURE_BALLISTA,CREATURE_DREAD_KNIGHT,
 ARMY_CREATURE_PSYCHIC_ELEMENTAL,ARMY_CREATURE_MAGIC_ELEMENTAL,ARMY_CREATURE_BLACK_DRAGON,
 ARMY_CREATURE_BEHEMOTH,ARMY_CREATURE_ANCIENT_BEHEMOTH, CREATURE_ARROW_TOWER,
 CREATURE_BALLISTA=ARMY_CREATURE_BALLISTA };
enum { SPELL_BLESS=41,SPELL_CURSE=42,SPELL_BLIND=62,eSpellEffectDeathBlow=0,
 GENERAL_TEXT_HATRED_DAMAGE_ONE_FORMAT=0,GENERAL_TEXT_HATRED_DAMAGE_MANY_FORMAT,
 GENERAL_TEXT_DOUBLE_DAMAGE_ONE_FORMAT,GENERAL_TEXT_DOUBLE_DAMAGE_MANY_FORMAT };
struct CreatureTrait { double baseFightValue=100; long attackSkill=0,defenseSkill=0; };
inline std::array<CreatureTrait,64> akCreatureTypeTraits;
struct SpellTrait { std::array<int,4> m_mastery_bonus{0,0,25,0}; };
inline std::array<SpellTrait,80> akSpellTraits;
inline constexpr double g_artilleryFactors[4]={1.0,1.5,1.5,2.0};
inline constexpr int g_artilleryDoubleChances[4]={0,50,75,100};
struct army
{
 int flags=0,m_creatureType=0,group=0,m_luckStatus=0,m_gridIndex=0;
 int index=0,speed=1,m_expectedMoveOrder=0;
 army * AI_target=nullptr;
 long AI_target_value=0,AI_target_distance=0,AI_possible_targets=0;
 void clearAIValues() { AI_target=nullptr; AI_target_value=0; AI_possible_targets=0; AI_target_distance=0; }
 long getSpeed() const { return speed; }
 long getSpellTime(int id) const { return m_spellInfluence.at(id); }
 long getAITargetTime() const { return getAITargetTime(speed); }
 long getAITargetTime(long) const;
 void considerAttack(const army*,long,long);
 long m_numTroops=1,m_topCreatureDamage=0,AI_expected_damage=0;
 struct { long hitPoints=10,damageLowBound=1,damageHighBound=2,attackSkill=0,defenseSkill=0; int level=1; } m_monInfo;
 std::array<long,80> m_spellInfluence{};
 long m_blessAmount=0,m_curseAmount=0,m_slayerLevel=0,m_precisionAmount=0,m_bloodlustAmount=0;
 double m_frenzyFactor=1,m_blindFactor=1,m_airShieldFactor=1,m_shieldFactor=1;
 bool m_residualBlindness=false,m_residualParalyze=false,shooting=false,retaliates=true;
 hero * controller=nullptr;
 bool is(int mask) const { return (flags&mask)!=0; }
 bool canShoot(int) const { return shooting; }
 bool canRetaliate(const army&) const { return retaliates; }
 hero * getController() const { return controller; }
 long getAIExpectedDamage() const { return AI_expected_damage; }
 int offsetToFront(int) const { return 1; }
 int getSecondGridIndex() const { return m_gridIndex+1; }
 const char * getName() const { return "oracle"; }
 long getAdjustedAttack(const army*,bool) const;
 long getAdjustedDefense(const army*,bool) const;
 long getAttackModifier(const army*,bool) const;
 long getDefenseModifier() const;
 double getDefenseDamageModifier(unsigned char) const;
 double getAverageDamage() const;
 long getAverageDamage(const army&,bool,long,bool,long) const;
 int computeAttackerBonus(int,bool,army*,bool,long) const;
 int computeAttackerDamageBonuses(int,bool,army*,bool,long) const;
 int computeDefenderDamageBonuses(int) const;
 double computeAttackerDamageReduction(const army*,bool) const;
 double computeDefenderDamageReduction(bool) const;
 long adjustDamage(army*,long,bool,bool,long,long*) const;
 double getUnitCombatValue(long,long,bool,const army*) const;
 long getLossCombatValue(long,long,bool,long,bool) const;
 long getTotalHitPoints(bool) const;
};
struct Window { void combatMessage(const char*,int,int) const {} void combatMessage(const std::string&,int,int) const {} };
struct type_AI_combat_parameters;
struct searchArray
{
 PinnedGround::searchArray original;
 void seedCombatPosition(const army*,long,long,int,int);
 const PinnedGround::pathCell * getHex(int h) const { return original.getHex(h); }
};
inline searchArray search;
inline searchArray * g_searchArray=&search;
struct combatManager
{
 bool m_moatOn=false,wallPenalty=false,distancePenalty=false;
 std::array<bool,2> IsHuman{true,true};
 Window window; Window * m_combatWindow=&window;
 army m_armies[2][7]; long m_numArmies[2]={0,0};
 int m_creaturePlacement=0;
 struct Cell { bool m_validMove=false; }; std::array<Cell,200> m_cells;
 void findAITargets(long,const army*,unsigned char,const type_AI_combat_parameters*,searchArray*);
 bool isInMoat(int,int) const { return false; }
 bool isQuickCombat() const { return true; }
 bool shotIsThroughWall(const army*,int,int) const { return wallPenalty; }
 bool shotIsNotOptimal(const army*,const army*) const { return distancePenalty; }
 long computeFireShieldDamage(long,const army*,const army*,long) const { return 0; }
 void spellEffect(int,army*,int,int) const {}
};
inline combatManager manager;
inline combatManager * g_combatManager=&manager;

inline void searchArray::seedCombatPosition(const army *actor,long side,long limit,int placement,int baseSpeed)
{
 auto & reference= PinnedGround::manager.m_armies[side][actor->index];
 reference.group=side; reference.index=actor->index; reference.m_gridIndex=actor->m_gridIndex;
 reference.speed=actor->speed; reference.restricted=actor->getSpellTime(72)!=0;
 original.seedCombatPosition(&reference,side,limit,placement!=0,baseSpeed);
 for(int h=0;h<187;++h) manager.m_cells[h].m_validMove=PinnedGround::manager.m_cells[h].m_validMove;
}
struct Game { struct { int m_difficulty=1; } m_setup; };
inline Game game; inline Game * g_game=&game;
struct Text { const char * GetText(int) const { return ""; } const char * operator[](int) const { return ""; } };
inline Text text; inline Text * g_generalText=&text;
template<class... T> std::string formatString(const char*,T...) { return {}; }
inline bool isNaturalEnemy(int,int) { return false; }
using SAMPLE2=int;
inline int sRandom(int,int) { throw std::logic_error("unsupported random oracle branch"); }
inline int loadPlaySample(const char*) { return 0; }
inline void waitEndSample(int,int) {}
#define DATA_COMPGEN(a,b,c) c
struct type_AI_combat_parameters
{
 bool m_simulated=false,m_killsOnly=false;
 int m_ourGroup=0;
 long m_lowestAttack=0,m_lowestDefense=0;
 void simulateSingleAttack(const army&,long&,const army&,long&,bool,long) const;
 void simulateAttack(const army&,long&,const army&,long&,bool,long) const;
 long getSimpleAttackEffect(const army&,long,const army&,long,bool,long) const;
 long getSimpleAttackEffect(const army&,const army&,bool,long) const;
};
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "../homm3Exchange/exchange-bodies.pinned.inc"
#include "../homm3Exchange/census-bodies.pinned.inc"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#undef DATA_COMPGEN
}
