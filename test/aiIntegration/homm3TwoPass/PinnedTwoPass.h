// SPDX-License-Identifier: CC0-1.0
// Scalar reconstructed-source oracle, not a retail executable.
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
#include <map>
#include <optional>
#include <array>
#include <cmath>
#include <string>
#include <stdexcept>
namespace PinnedTwoPass
{
using std::min; using std::max; using std::sqrt;
template<class A,class B> auto cppMax(A a,B b) { return a>b?a:b; }
inline int32_t max(int32_t a,int b) { return a>b?a:b; }
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
 creatureKing3=128,creatureSiegeWeapon=256,creatureSummoned=512,creatureImmobilized=1024,creatureClone=2048,creatureDone=4096,creatureWaiting=8192,creatureCatapult=16384
};
enum { CREATURE_CAVALIER=10,CREATURE_CHAMPION,CREATURE_EARTH_ELEMENTAL,CREATURE_MAGMA_ELEMENTAL,
 CREATURE_AIR_ELEMENTAL,CREATURE_STORM_ELEMENTAL,CREATURE_WATER_ELEMENTAL,CREATURE_ICE_ELEMENTAL,
 CREATURE_FIRE_ELEMENTAL,CREATURE_ENERGY_ELEMENTAL,ARMY_CREATURE_BALLISTA,CREATURE_DREAD_KNIGHT,
 ARMY_CREATURE_PSYCHIC_ELEMENTAL,ARMY_CREATURE_MAGIC_ELEMENTAL,ARMY_CREATURE_BLACK_DRAGON,
 ARMY_CREATURE_BEHEMOTH,ARMY_CREATURE_ANCIENT_BEHEMOTH, CREATURE_ARROW_TOWER,
 CREATURE_BALLISTA=ARMY_CREATURE_BALLISTA, CREATURE_MAGOG=50,CREATURE_LICH,CREATURE_POWER_LICH,CREATURE_CATAPULT,CREATURE_FIRST_AID_TENT,CREATURE_AMMO_CART, ARMY_CREATURE_FIRST_AID_TENT=CREATURE_FIRST_AID_TENT,ARMY_CREATURE_AMMO_CART=CREATURE_AMMO_CART,AI_ORDER_SHOOT=7,AI_ORDER_MOVE_AND_ATTACK=6 };
enum { SPELL_BLESS=41,SPELL_CURSE=42,SPELL_BLIND=62,eSpellEffectDeathBlow=0,
 GENERAL_TEXT_HATRED_DAMAGE_ONE_FORMAT=0,GENERAL_TEXT_HATRED_DAMAGE_MANY_FORMAT,
 GENERAL_TEXT_DOUBLE_DAMAGE_ONE_FORMAT,GENERAL_TEXT_DOUBLE_DAMAGE_MANY_FORMAT };
struct CreatureTrait { double baseFightValue=100; int32_t attackSkill=0,defenseSkill=0; };
inline std::array<CreatureTrait,64> akCreatureTypeTraits;
struct SpellTrait { std::array<int,4> m_mastery_bonus{0,0,25,0}; };
inline std::array<SpellTrait,80> akSpellTraits;
inline constexpr double g_artilleryFactors[4]={1.0,1.5,1.5,2.0};
inline constexpr int g_artilleryDoubleChances[4]={0,50,75,100};
struct army
{
 uint32_t id=0;
 int flags=0,m_creatureType=0,group=0,m_luckStatus=0,m_gridIndex=0;
 int index=0,speed=1,m_expectedMoveOrder=0;
 army * AI_target=nullptr;
 int32_t AI_target_value=0,AI_target_distance=0,AI_possible_targets=0;
 void clearAIValues() { AI_target=nullptr; AI_target_value=0; AI_possible_targets=0; AI_target_distance=0; }
 int32_t getSpeed() const { return speed; }
 int32_t getSpellTime(int id) const { return m_spellInfluence.at(id); }
 int32_t getAITargetTime() const { return getAITargetTime(speed); }
 int32_t getAITargetTime(int32_t) const;
 void considerAttack(const army*,int32_t,int32_t);
 int32_t m_numTroops=1,m_topCreatureDamage=0,AI_expected_damage=0;
 struct { int32_t hitPoints=10,damageLowBound=1,damageHighBound=2,attackSkill=0,defenseSkill=0; int level=1; } m_monInfo;
 std::array<int32_t,80> m_spellInfluence{};
 int32_t m_blessAmount=0,m_curseAmount=0,m_slayerLevel=0,m_precisionAmount=0,m_bloodlustAmount=0;
 double m_frenzyFactor=1,m_blindFactor=1,m_airShieldFactor=1,m_shieldFactor=1;
 bool m_residualBlindness=false,m_residualParalyze=false,shooting=false,retaliates=true;
 hero * controller=nullptr;
 bool is(int mask) const { return (flags&mask)!=0; }
 bool canShoot(int) const { return shooting; }
 bool canRetaliate(const army&) const { return retaliates; }
 hero * getController() const { return controller; }
 bool isIncapacitated() const { return m_spellInfluence[62] || m_spellInfluence[70] || m_spellInfluence[74]; }
 bool cannotAttack() const { return isIncapacitated() || is(creatureImmobilized) || m_creatureType==ARMY_CREATURE_FIRST_AID_TENT || m_creatureType==ARMY_CREATURE_AMMO_CART; }
 int getControllingSide() const { return group; }
 const army * getAITarget() const { return AI_target; }
 int32_t getTotalCombatValue(int32_t,int32_t) const;
 void setAIExpectedDamage(int32_t);
 int32_t getAIExpectedDamage() const { return AI_expected_damage; }
 int offsetToFront(int) const { return 1; }
 int getSecondGridIndex() const { return m_gridIndex+1; }
 const char * getName() const { return "oracle"; }
 int32_t getAdjustedAttack(const army*,bool) const;
 int32_t getAdjustedDefense(const army*,bool) const;
 int32_t getAttackModifier(const army*,bool) const;
 int32_t getDefenseModifier() const;
 double getDefenseDamageModifier(unsigned char) const;
 double getAverageDamage() const;
 int32_t getAverageDamage(const army&,bool,int32_t,bool,int32_t) const;
 int computeAttackerBonus(int,bool,army*,bool,int32_t) const;
 int computeAttackerDamageBonuses(int,bool,army*,bool,int32_t) const;
 int computeDefenderDamageBonuses(int) const;
 double computeAttackerDamageReduction(const army*,bool) const;
 double computeDefenderDamageReduction(bool) const;
 int32_t adjustDamage(army*,int32_t,bool,bool,int32_t,int32_t*) const;
 double getUnitCombatValue(int32_t,int32_t,bool,const army*) const;
 int32_t getLossCombatValue(int32_t,int32_t,bool,int32_t,bool) const;
 int32_t getTotalHitPoints(bool) const;
};
struct Window { void combatMessage(const char*,int,int) const {} void combatMessage(const std::string&,int,int) const {} };
struct type_AI_combat_parameters;
struct searchArray
{
 struct Cell { int32_t m_cost=0; };
 void seedCombatPosition(const army*,int32_t,int32_t,int,int) { throw std::logic_error("ground source path not supplied"); }
 const Cell * getHex(int) const { throw std::logic_error("ground source path not supplied"); }
};
inline searchArray search;
inline searchArray * g_searchArray=&search;
struct Ranked {uint32_t id; int32_t value;};
struct Action {uint32_t actor;std::optional<uint32_t> target;bool killsOnly=false,retried=false;std::vector<Ranked> initialCandidates,candidates;int32_t damage=0;std::map<uint32_t,int32_t> ledger;};
struct func_moves_before {unsigned char operator()(const army*,const army*);};
struct combatManager
{
 int m_actingSide=0;bool secondPhase=false;
 int32_t m_nextAction=0,m_nextActionExtra=0,m_nextActionGridIndex=0,m_nextActionGridIndex2=0;
 std::vector<Action> trace;std::vector<size_t> stops;std::vector<uint32_t> fixedOrder;
 bool isInSecondPhase() const {return secondPhase;}
 int32_t getTotalCombatValue(int32_t,int32_t,int32_t,bool) const;
 void findMoveOrder(std::vector<army*>*);
 int32_t chooseShooterTarget(const army*,type_AI_combat_parameters&,int32_t&) const;
 int32_t sourceChooseShooterTarget(const army*,type_AI_combat_parameters&,int32_t&) const;
 void chooseShooterAction(const army*,bool,int32_t);
 void sourceChooseShooterAction(const army*,bool,int32_t);
 int32_t simulateActions(std::vector<army*>&,int32_t,int32_t);
 int32_t sourceSimulateActions(std::vector<army*>&,int32_t,int32_t);
 void simulateCombat(int32_t,unsigned char);
 int32_t getAreaAttackValue(const army&,int32_t,int32_t,const type_AI_combat_parameters&) const {throw std::logic_error("area excluded");}
 bool chooseCyclopsAction(int32_t,int32_t,type_AI_combat_parameters&) {throw std::logic_error("cyclops excluded");}
 void chooseMeleeAction(const army*,int,int,int32_t) {throw std::logic_error("ground actor excluded");}
 void simulateMeleeAttack(army*,army*,int32_t) {throw std::logic_error("ground actor excluded");}
 bool m_moatOn=false,wallPenalty=false,distancePenalty=false;
 std::array<bool,2> IsHuman{true,true};
 Window window; Window * m_combatWindow=&window;
 army m_armies[2][21]; int32_t m_numArmies[2]={0,0};
 int m_creaturePlacement=0;
 struct Cell { bool m_validMove=false; army * value=nullptr; army * getArmy() const {return value;} }; std::array<Cell,200> m_cells;
 void findAITargets(int32_t,const army*,unsigned char,const type_AI_combat_parameters*,searchArray*);
 bool isInMoat(int,int) const { return false; }
 bool isQuickCombat() const { return true; }
 bool shotIsThroughWall(const army*,int,int) const { return wallPenalty; }
 bool shotIsNotOptimal(const army*,const army*) const { return distancePenalty; }
 int32_t computeFireShieldDamage(int32_t,const army*,const army*,int32_t) const { return 0; }
 void spellEffect(int,army*,int,int) const {}
};
inline combatManager manager;
inline combatManager * g_combatManager=&manager;
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
 type_AI_combat_parameters(const combatManager*,int32_t);
 int32_t m_enemyGroup=0,m_friendlyCombatValue=0,m_enemyCombatValue=0,m_awakeFriendlyValue=0,m_awakeEnemyValue=0,m_roundsLeft=0;
 int32_t getGroup() const {return m_ourGroup;}int32_t getEnemyGroup() const {return m_enemyGroup;}
 int32_t getRangedAttackValue(const army&,const army&) const;
 int32_t sourceRangedAttackValue(const army&,const army&) const;
 bool m_simulated=false,m_killsOnly=false;
 int m_ourGroup=0;
 int32_t m_lowestAttack=0,m_lowestDefense=0;
 void simulateSingleAttack(const army&,int32_t&,const army&,int32_t&,bool,int32_t) const;
 void simulateAttack(const army&,int32_t&,const army&,int32_t&,bool,int32_t) const;
 int32_t getSimpleAttackEffect(const army&,int32_t,const army&,int32_t,bool,int32_t) const;
 int32_t getSimpleAttackEffect(const army&,const army&,bool,int32_t) const;
};
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
inline constexpr double g_aiOddsLadder[6]={2.6,1.9,1.5,1.31,1.2,1.13};
inline bool validHex(int32_t value){return value>=0 && value<187;}
#define long int32_t
#include "../homm3Exchange/exchange-bodies.pinned.inc"
#include "../homm3Exchange/census-bodies.pinned.inc"
#include "two-pass-bodies.instrumented.inc"
#undef long
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#undef DATA_COMPGEN
inline int32_t type_AI_combat_parameters::getRangedAttackValue(const army & a,const army & b) const {
 const auto value=sourceRangedAttackValue(a,b);g_combatManager->trace.back().candidates.push_back({b.id,value});return value;
}
inline int32_t combatManager::chooseShooterTarget(const army *a,type_AI_combat_parameters & data,int32_t & best) const {
 auto & tr=g_combatManager->trace.back();
 if(!tr.candidates.empty()){tr.initialCandidates=tr.candidates;tr.candidates.clear();tr.retried=true;}
 tr.killsOnly=tr.killsOnly||data.m_killsOnly;
 const auto hex=sourceChooseShooterTarget(a,data,best);
 if(hex>=0 && m_cells.at(hex).value)tr.target=m_cells.at(hex).value->id;else tr.target.reset();
 return hex;
}
inline void combatManager::chooseShooterAction(const army *a,bool simulated,int32_t side) {
 if(!trace.empty()){for(int s=0;s<2;++s)for(int i=0;i<m_numArmies[s];++i)trace.back().ledger[m_armies[s][i].id]=m_armies[s][i].AI_expected_damage;}
 trace.push_back(Action{});trace.back().actor=a->id;sourceChooseShooterAction(a,simulated,side);
 if(!trace.back().retried)trace.back().initialCandidates=trace.back().candidates;
}
inline int32_t combatManager::simulateActions(std::vector<army*> & order,int32_t cursor,int32_t side) {
 if(stops.empty()){fixedOrder.clear();for(auto*u:order)fixedOrder.push_back(u->id);}
 // Observe each action ledger through setAIExpectedDamage? A no-action has no write.
 const auto result=sourceSimulateActions(order,cursor,side);
 if(!trace.empty()){for(int s=0;s<2;++s)for(int i=0;i<m_numArmies[s];++i)trace.back().ledger[m_armies[s][i].id]=m_armies[s][i].AI_expected_damage;}
 stops.push_back(result);return result;
}
}

