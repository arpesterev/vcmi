// SPDX-License-Identifier: CC0-1.0
// Scalar reconstructed-source oracle, not a retail executable.
#pragma once
#include "../homm3GroundTwoPass/PinnedForecastPath.h"
#include <algorithm>
#include <functional>
#include <cstdint>
#include <vector>
#include <map>
#include <optional>
#include <array>
#include <cmath>
#include <string>
#include <stdexcept>
namespace PinnedMixedLive
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
 creatureKing3=128,creatureSiegeWeapon=256,creatureSummoned=512,creatureImmobilized=1024,creatureClone=2048,creatureDone=4096,creatureWaiting=8192,creatureCatapult=16384,creatureMultiHeaded=32768,creatureHasExtendedAttack=65536,creatureFlyingArmy=131072
};
enum { CREATURE_CAVALIER=10,CREATURE_CHAMPION,CREATURE_EARTH_ELEMENTAL,CREATURE_MAGMA_ELEMENTAL,
 CREATURE_AIR_ELEMENTAL,CREATURE_STORM_ELEMENTAL,CREATURE_WATER_ELEMENTAL,CREATURE_ICE_ELEMENTAL,
 CREATURE_FIRE_ELEMENTAL,CREATURE_ENERGY_ELEMENTAL,ARMY_CREATURE_BALLISTA,CREATURE_DREAD_KNIGHT,
 ARMY_CREATURE_PSYCHIC_ELEMENTAL,ARMY_CREATURE_MAGIC_ELEMENTAL,ARMY_CREATURE_BLACK_DRAGON,
 ARMY_CREATURE_BEHEMOTH,ARMY_CREATURE_ANCIENT_BEHEMOTH, CREATURE_ARROW_TOWER,
 CREATURE_BALLISTA=ARMY_CREATURE_BALLISTA, CREATURE_HARPY=40,CREATURE_HARPY_HAG,CREATURE_MAGOG=50,CREATURE_LICH,CREATURE_POWER_LICH,CREATURE_CATAPULT,CREATURE_FIRST_AID_TENT,CREATURE_AMMO_CART, ARMY_CREATURE_FIRST_AID_TENT=CREATURE_FIRST_AID_TENT,ARMY_CREATURE_AMMO_CART=CREATURE_AMMO_CART,AI_ORDER_SHOOT=7,AI_ORDER_MOVE_AND_ATTACK=6 };
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
 int groupToAttack=-1,indexToAttack=-1;
 int index=0,speed=1,m_expectedMoveOrder=0,m_retaliationCount=1,m_facing=0; int32_t areaLoss=0;
 army * AI_target=nullptr;
 int32_t AI_target_value=0,AI_target_distance=0,AI_possible_targets=0;
 void clearAIValues() { AI_target=nullptr; AI_target_value=0; AI_possible_targets=0; AI_target_distance=0; }
 int32_t getSpeed() const { return speed; }
 int32_t getSpellTime(int id) const { return m_spellInfluence.at(id); }
 int32_t getAIPossibleTargets() const {return AI_possible_targets;}
 int32_t getAITargetValue() const {return AI_target_value;}
 int getOwningSide() const {return group;}
 int getAdjacentCellIndex(int hex,int direction) const;
 int getAdjacentHex(int hex,int direction) const {return getAdjacentCellIndex(hex,direction);}
 int getAdjacentHex(int direction) const {return getAdjacentCellIndex(m_gridIndex,direction);}
 int getMultiHeadDirections(int,const army*,int)const {throw std::logic_error("multihead excluded");}
 int getAttackDirection(int,const army*,int)const {throw std::logic_error("breath excluded");}
 int32_t getAITargetTime() const { return getAITargetTime(speed); }
 int32_t getAITargetTime(int32_t) const;
 void considerAttack(const army*,int32_t,int32_t);
 int32_t m_numTroops=1,m_topCreatureDamage=0,AI_expected_damage=0;
 struct { int32_t hitPoints=10,damageLowBound=1,damageHighBound=2,attackSkill=0,defenseSkill=0; int level=1,m_hasSpell=0; } m_monInfo;
 std::array<int32_t,80> m_spellInfluence{};
 int32_t m_blessAmount=0,m_curseAmount=0,m_slayerLevel=0,m_precisionAmount=0,m_bloodlustAmount=0;
 double m_frenzyFactor=1,m_blindFactor=1,m_airShieldFactor=1,m_shieldFactor=1;
 bool m_residualBlindness=false,m_residualParalyze=false,shooting=false,retaliates=true;
 hero * controller=nullptr;
 bool is(int mask) const { return (flags&mask)!=0; }
 bool isAdjacent(int)const{throw std::logic_error("wide excluded");}
 bool canShoot(int) const { return shooting; }
 bool canShoot(const army*) const {return shooting;}
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
using pathCell=PinnedForecastPath::pathCell;
inline constexpr int COMBAT_GRID_CELLS=187,COMBAT_DIRECTION_COUNT=6,COMBAT_DIRECTION_WIDE_LOWER=7;
struct searchArray
{
 PinnedForecastPath::searchArray engine;
 void sync(const army*);
 void publish();
 void seedCombatPosition(const army*,int32_t,int32_t,int,int);
 bool findCombatPath(const army*,int32_t,int32_t,int,int,int);
 const pathCell *getHex(int hex)const{return engine.getHex(hex);}
 int32_t getTravelTime(const army*,int32_t)const;
 bool isMoat(int)const{return false;}
 size_t getPathSteps()const{return engine.m_result.size();}
 int getStep(int index)const{return engine.m_result.at(index)->m_direction;}
 void markTeleport(const army*,int){throw std::logic_error("teleport excluded");}
};
inline searchArray search;
inline searchArray * g_searchArray=&search;
struct Ranked {uint32_t id; int32_t value;};
struct Action {uint32_t actor;std::optional<uint32_t> target;bool killsOnly=false,retried=false;std::vector<Ranked> initialCandidates,candidates;int32_t damage=0;std::map<uint32_t,int32_t> ledger;int order=3,hex=-1,destination=-1;int32_t value=0;std::array<int32_t,187> danger{};uint32_t marked=0;};
struct func_moves_before {unsigned char operator()(const army*,const army*);};
struct combatManager
{
 int m_actingSide=0,m_currentSide=0,m_fortificationLevel=0;bool secondPhase=false,m_inSecondPhase=false; std::array<int32_t,2> heroRangedValue{};
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
 int32_t chooseMeleeAction(const army*,bool,bool,int32_t);
 int32_t sourceChooseMeleeAction(const army*,bool,bool,int32_t);
 bool chooseMeleeTarget(const army*,bool,int32_t&,type_AI_combat_parameters&);
 void simulateMeleeAttack(army*,army*,int32_t);
 void simulateMeleeAttack(army*,int32_t,army*,int32_t,int32_t);
 int32_t getAttackChange(const army*,const army*,type_AI_combat_parameters&);
 void markEnemyAttacks(const army*,int32_t*,int32_t&,const type_AI_combat_parameters&)const;
 void markFriendlyArmies(const army*,int32_t*,int32_t,const type_AI_combat_parameters&)const;
 unsigned char hasRangedAdvantage(type_AI_combat_parameters&);
 unsigned char moveToward(const army*,int32_t,const int32_t*,unsigned char);
 int32_t getAreaEffect(int32_t,const army* u,int32_t,const type_AI_combat_parameters&)const{return u->areaLoss;}
 void markFirewalls(const army*,int32_t*,type_AI_combat_parameters*)const{}
 void markMoat(const army*,int32_t*,type_AI_combat_parameters*)const{}
 unsigned char shouldStayInCastle(type_AI_combat_parameters*)const{return 0;}
 bool chooseSpellAction(const army*a,int32_t*,type_AI_combat_parameters*)const{if(a->m_monInfo.m_hasSpell==0)return false;throw std::logic_error("live spell excluded");}
 int defenseCalls=0,defenseAccepted=0;
 unsigned char sourceAttemptShooterDefense(const army*,searchArray*,const type_AI_combat_parameters*);
 unsigned char attemptShooterDefense(const army*a,searchArray*b,const type_AI_combat_parameters*c){++defenseCalls;const auto result=sourceAttemptShooterDefense(a,b,c);defenseAccepted+=result;return result;}
 unsigned char chooseDefenseHex(const army*,const army*,int32_t*,int32_t*,searchArray*);
 army * currentArmy=nullptr,*m_lastMovedArmy=nullptr;
 army *getCurrentArmy()const{return currentArmy;}
 void turnOffHighlighter(int){}
 void placeShooter(const army*){throw std::logic_error("placement excluded");}
 void doCompAI(int32_t);

 unsigned char chooseToRun(const army*,const int32_t*,const searchArray*);
 bool isOutsidePlacementBoundry(int,int)const{throw std::logic_error("placement excluded");}
 void markMultiheadedEnemy(const army*,const army*,int32_t*,int32_t,searchArray*,const type_AI_combat_parameters&)const{throw std::logic_error("multihead excluded");}
 static bool validHex(int h){return h>=0&&h<187;}
 static int gridY(int h){return h/17;}
 static bool inCastle(int){throw std::logic_error("siege excluded");}
 short m_adjacentCells[187][6]{};
 bool m_moatOn=false,wallPenalty=false,distancePenalty=false;
 std::array<bool,2> IsHuman{true,true};
 Window window; Window * m_combatWindow=&window;
 army m_armies[2][21]; int32_t m_numArmies[2]={0,0};
 int m_creaturePlacement=0;
 struct Cell { int m_refX=0; bool blocked=false;bool m_validMove=false; army * value=nullptr; army * getArmy() const {return value;} }; std::array<Cell,200> m_cells;
 void findAITargets(int32_t,const army*,unsigned char,const type_AI_combat_parameters*,searchArray*);
 bool isInMoat(int,int) const { return false; }
 bool isQuickCombat() const { return true; }
 std::function<bool(const army*,int,int)> nativeWallPenalty;
 std::function<bool(const army*,const army*)> nativeDistancePenalty;
 bool shotIsThroughWall(const army* a,int x,int y) const { return nativeWallPenalty?nativeWallPenalty(a,x,y):wallPenalty; }
 bool shotIsNotOptimal(const army* a,const army* b) const { return nativeDistancePenalty?nativeDistancePenalty(a,b):distancePenalty; }
 int32_t computeFireShieldDamage(int32_t,const army*,const army*,int32_t) const { return 0; }
 void spellEffect(int,army*,int,int) const {}
};
using hexcell=combatManager::Cell;
inline combatManager manager;
inline combatManager * g_combatManager=&manager;
struct Game { int m_gameVersion=0;struct { int m_difficulty=1; } m_setup; };
inline Game game; inline Game * g_game=&game;
struct Text { const char * GetText(int) const { return ""; } const char * operator[](int) const { return ""; } };
inline Text text; inline Text * g_generalText=&text;
template<class... T> std::string formatString(const char*,T...) { return {}; }
inline bool isNaturalEnemy(int,int) { return false; }
using SAMPLE2=int;
inline int sRandom(int,int) { throw std::logic_error("unsupported random oracle branch"); }
inline int loadPlaySample(const char*) { return 0; }
inline void waitEndSample(int,int) {}
inline std::vector<int> draws;inline size_t drawCursor=0;
inline int32_t drawRandom(int32_t low,int32_t high){if(low!=75||high!=100||drawCursor>=draws.size())throw std::logic_error("draw stream exhausted");return draws.at(drawCursor++);}
inline int32_t g_castleWallColumns[11]{};
inline int32_t getMultiHeadBonus(int32_t,const army*,int32_t,int32_t,const army*,int32_t,const type_AI_combat_parameters*){throw std::logic_error("multihead excluded");}
inline int32_t getBreathBonus(int32_t,const army*,int32_t,int32_t,const army*,int32_t,const type_AI_combat_parameters*){throw std::logic_error("breath excluded");}
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
struct type_AI_attack_hex_chooser{
 const type_AI_combat_parameters *estimate;const army *attacker,*enemy;const int32_t*enemy_attacks;searchArray*search_array;
 int32_t speed,best_value,best_attack_time,best_hex,retaliation_strength,our_strength;
 type_AI_attack_hex_chooser(const army*,const army*,const int32_t*,searchArray*,const type_AI_combat_parameters*);
 int32_t getHexAttackValue(int32_t,int32_t&);int32_t getAttackTime(const pathCell*)const;
 void checkAdjacentHexes(int32_t,int32_t,int32_t);unsigned char findAttackHex();
 int32_t getBestHex()const{return best_hex;}int32_t getAttackTime()const{return best_attack_time;}int32_t getHexValue()const{return best_value;}
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
#include "../homm3TwoPass/two-pass-bodies.instrumented.inc"
#define random PinnedMixedLive::drawRandom
#include "../homm3GroundTwoPass/ground-bodies.instrumented.inc"
#pragma GCC diagnostic push
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#define attemptShooterDefense sourceAttemptShooterDefense
#include "mixed-bodies.pinned.inc"
#undef attemptShooterDefense
#pragma GCC diagnostic pop
#undef random
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
 trace.push_back(Action{});trace.back().actor=a->id;sourceChooseShooterAction(a,simulated,side);trace.back().order=m_nextAction;trace.back().destination=m_nextActionGridIndex;
 if(!trace.back().retried)trace.back().initialCandidates=trace.back().candidates;
}
inline int32_t combatManager::simulateActions(std::vector<army*> & order,int32_t cursor,int32_t side) {
 if(stops.empty()){fixedOrder.clear();for(auto*u:order)fixedOrder.push_back(u->id);}
 // Observe each action ledger through setAIExpectedDamage? A no-action has no write.
 const auto result=sourceSimulateActions(order,cursor,side);
 if(!trace.empty()){for(int s=0;s<2;++s)for(int i=0;i<m_numArmies[s];++i)trace.back().ledger[m_armies[s][i].id]=m_armies[s][i].AI_expected_damage;}
 stops.push_back(result);return result;
}

inline void searchArray::sync(const army*){
 auto & target=PinnedForecastPath::manager;target= PinnedForecastPath::combatManager{};target.setupAdjacencyArray();
 for(int s=0;s<2;++s){target.m_numArmies[s]=manager.m_numArmies[s];for(int i=0;i<manager.m_numArmies[s];++i){auto&a=target.m_armies[s][i];const auto&b=manager.m_armies[s][i];a.group=s;a.index=i;a.m_gridIndex=b.m_gridIndex;a.speed=b.speed;a.restricted=b.m_spellInfluence[72];}}
 for(int h=0;h<187;++h){auto&a=target.m_cells[h];const auto&b=manager.m_cells[h];a.m_attributes=b.blocked?1:0;if(b.value){a.m_armySide=b.value->group;a.m_armySlot=b.value->index;}}
}
inline void searchArray::publish(){for(int h=0;h<187;++h)manager.m_cells[h].m_validMove=PinnedForecastPath::manager.m_cells[h].m_validMove;}
inline void searchArray::seedCombatPosition(const army*a,int32_t side,int32_t limit,int placement,int base){sync(a);engine.seedCombatPosition(&PinnedForecastPath::manager.m_armies[a->group][a->index],side,limit,placement,base);publish();}
inline bool searchArray::findCombatPath(const army*a,int32_t side,int32_t destination,int placement,int limit,int base){sync(a);const bool r=engine.findCombatPath(&PinnedForecastPath::manager.m_armies[a->group][a->index],side,destination,placement,limit,base);publish();return r;}
inline int army::getAdjacentCellIndex(int hex,int direction)const{return hex>=0&&hex<187?manager.m_adjacentCells[hex][direction]:-1;}
inline int32_t combatManager::chooseMeleeAction(const army*a,bool teleport,bool simulated,int32_t side){
 if(!trace.empty())for(int s=0;s<2;++s)for(int i=0;i<m_numArmies[s];++i)trace.back().ledger[m_armies[s][i].id]=m_armies[s][i].AI_expected_damage;
 trace.push_back(Action{});trace.back().actor=a->id;const auto value=sourceChooseMeleeAction(a,teleport,simulated,side);
 auto&tr=trace.back();tr.value=value;tr.order=m_nextAction;tr.destination=m_nextActionGridIndex;tr.hex=m_nextActionExtra;
 if(m_nextAction==6&&validHex(m_nextActionGridIndex)&&m_cells[m_nextActionGridIndex].value)tr.target=m_cells[m_nextActionGridIndex].value->id;
 return value;
}

}
