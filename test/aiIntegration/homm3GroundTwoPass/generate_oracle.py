from pathlib import Path
import hashlib,json,sys
root=Path(__file__).resolve().parent.parent
out=root/'homm3GroundTwoPass';out.mkdir(exist_ok=True)
up=Path(sys.argv[1])
manifest=[]
def extract(file,sig):
 text=(up/file).read_text(); start=text.index(sig); brace=text.index('{',start); depth=1;i=brace+1
 while depth:
  if text[i]=='{':depth+=1
  elif text[i]=='}':depth-=1
  i+=1
 body=text[start:i];manifest.append({'file':file,'signature':sig,'sha256':hashlib.sha256(body.encode()).hexdigest()});return body
p=(root/'homm3Ground/PinnedGround.h').read_text().replace('namespace PinnedGround','namespace PinnedForecastPath')
p=p.replace('int group=0,index=0,m_gridIndex=0,m_facing=0,m_creatureType=0,speed=1;','int group=0,index=0,m_gridIndex=0,m_facing=0,m_creatureType=0,speed=1,groupToAttack=-1;')
p=p.replace('static int getDistance(int,int) { throw std::logic_error("destination excluded"); }','static long getDistance(long,long);')
p=p.replace('bool buildCombatPath(const army*,int,int,int dest) { if(dest!=-1)throw std::logic_error("destination excluded");return false; }','bool buildCombatPath(const army*,int,int,int);')
p=p.replace('#include "bodies.pinned.inc"','#include "../homm3Ground/bodies.pinned.inc"\ninline int oppositeDirection(int direction){return (direction+3)%6;}\n'+extract('findpath.cpp','bool searchArray::buildCombatPath(')+'\n'+extract('cmbtmgr.cpp','long combatManager::getDistance('))
p=p.replace('#include <array>','#include <array>\n#include <algorithm>\n#include <cstdlib>\nusing std::max;using std::abs;')
(out/'PinnedForecastPath.h').write_text(p)
p=(root/'homm3TwoPass/PinnedTwoPass.h').read_text().replace('namespace PinnedTwoPass','namespace PinnedGroundTwoPass').replace('#include <algorithm>','#include "PinnedForecastPath.h"\n#include <algorithm>')
p=p.replace('creatureCatapult=16384','creatureCatapult=16384,creatureMultiHeaded=32768,creatureHasExtendedAttack=65536,creatureFlyingArmy=131072')
p=p.replace('CREATURE_MAGOG=50','CREATURE_HARPY=40,CREATURE_HARPY_HAG,CREATURE_MAGOG=50')
p=p.replace('int index=0,speed=1,m_expectedMoveOrder=0;','int index=0,speed=1,m_expectedMoveOrder=0,m_retaliationCount=1,m_facing=0; int32_t areaLoss=0;')
p=p.replace('int32_t getAITargetTime() const','int32_t getAIPossibleTargets() const {return AI_possible_targets;}\n int32_t getAITargetValue() const {return AI_target_value;}\n int getOwningSide() const {return group;}\n int getAdjacentCellIndex(int hex,int direction) const;\n int getAdjacentHex(int hex,int direction) const {return getAdjacentCellIndex(hex,direction);}\n int getAdjacentHex(int direction) const {return getAdjacentCellIndex(m_gridIndex,direction);}\n int getMultiHeadDirections(int,const army*,int)const {throw std::logic_error("multihead excluded");}\n int getAttackDirection(int,const army*,int)const {throw std::logic_error("breath excluded");}\n int32_t getAITargetTime() const')
p=p.replace('bool canShoot(int) const { return shooting; }','bool canShoot(int) const { return shooting; }\n bool canShoot(const army*) const {return shooting;}')
p=p.replace('struct searchArray\n{\n struct Cell { int32_t m_cost=0; };\n void seedCombatPosition(const army*,int32_t,int32_t,int,int) { throw std::logic_error("ground source path not supplied"); }\n const Cell * getHex(int) const { throw std::logic_error("ground source path not supplied"); }\n};','''using pathCell=PinnedForecastPath::pathCell;
inline constexpr int COMBAT_GRID_CELLS=187,COMBAT_DIRECTION_COUNT=6;
struct searchArray
{
 PinnedForecastPath::searchArray engine;
 void sync(const army*);
 void publish();
 void seedCombatPosition(const army*,int32_t,int32_t,int,int);
 bool findCombatPath(const army*,int32_t,int32_t,int,int,int);
 const pathCell *getHex(int hex)const{return engine.getHex(hex);}
 bool isMoat(int)const{return false;}
 size_t getPathSteps()const{return engine.m_result.size();}
 int getStep(int index)const{return engine.m_result.at(index)->m_direction;}
 void markTeleport(const army*,int){throw std::logic_error("teleport excluded");}
};''')
p=p.replace('std::map<uint32_t,int32_t> ledger;','std::map<uint32_t,int32_t> ledger;int order=3,hex=-1,destination=-1;int32_t value=0;std::array<int32_t,187> danger{};uint32_t marked=0;')
p=p.replace('int m_actingSide=0;bool secondPhase=false;','int m_actingSide=0,m_currentSide=0,m_fortificationLevel=0;bool secondPhase=false,m_inSecondPhase=false; std::array<int32_t,2> heroRangedValue{};')
p=p.replace('void chooseMeleeAction(const army*,int,int,int32_t) {throw std::logic_error("ground actor excluded");}\n void simulateMeleeAttack(army*,army*,int32_t) {throw std::logic_error("ground actor excluded");}','''int32_t chooseMeleeAction(const army*,bool,bool,int32_t);
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
 bool chooseSpellAction(const army*,int32_t*,type_AI_combat_parameters*)const{throw std::logic_error("live spell excluded");}
 bool attemptShooterDefense(const army*,searchArray*,type_AI_combat_parameters*)const{throw std::logic_error("live shooter defense excluded");}
 bool chooseToRun(const army*,int32_t*,searchArray*)const{throw std::logic_error("live run excluded");}
 bool isOutsidePlacementBoundry(int,int)const{throw std::logic_error("placement excluded");}
 void markMultiheadedEnemy(const army*,const army*,int32_t*,int32_t,searchArray*,const type_AI_combat_parameters&)const{throw std::logic_error("multihead excluded");}
 static bool validHex(int h){return h>=0&&h<187;}
 static int gridY(int h){return h/17;}
 static bool inCastle(int){throw std::logic_error("siege excluded");}
 short m_adjacentCells[187][6]{};''')
p=p.replace('struct Cell { bool m_validMove=false;','struct Cell { bool blocked=false;bool m_validMove=false;')
p=p.replace('struct Game { struct { int m_difficulty=1; } m_setup; };','struct Game { int m_gameVersion=0;struct { int m_difficulty=1; } m_setup; };')
p=p.replace('#define DATA_COMPGEN(a,b,c) c','''inline std::vector<int> draws;inline size_t drawCursor=0;
inline int32_t drawRandom(int32_t low,int32_t high){if(low!=75||high!=100||drawCursor>=draws.size())throw std::logic_error("draw stream exhausted");return draws.at(drawCursor++);}
inline int32_t g_castleWallColumns[11]{};
inline int32_t getMultiHeadBonus(int32_t,const army*,int32_t,int32_t,const army*,int32_t,const type_AI_combat_parameters*){throw std::logic_error("multihead excluded");}
inline int32_t getBreathBonus(int32_t,const army*,int32_t,int32_t,const army*,int32_t,const type_AI_combat_parameters*){throw std::logic_error("breath excluded");}
#define DATA_COMPGEN(a,b,c) c''')
# Need chooser declaration from header exact.
# handcrafted layout to avoid include dependencies, exact method bodies follow.
decl='''struct type_AI_attack_hex_chooser{
 const type_AI_combat_parameters *estimate;const army *attacker,*enemy;const int32_t*enemy_attacks;searchArray*search_array;
 int32_t speed,best_value,best_attack_time,best_hex,retaliation_strength,our_strength;
 type_AI_attack_hex_chooser(const army*,const army*,const int32_t*,searchArray*,const type_AI_combat_parameters*);
 int32_t getHexAttackValue(int32_t,int32_t&);int32_t getAttackTime(const pathCell*)const;
 void checkAdjacentHexes(int32_t,int32_t,int32_t);unsigned char findAttackHex();
 int32_t getBestHex()const{return best_hex;}int32_t getAttackTime()const{return best_attack_time;}int32_t getHexValue()const{return best_value;}
};
'''
p=p.replace('#if defined(__GNUC__)\n#pragma GCC diagnostic push',decl+'\n#if defined(__GNUC__)\n#pragma GCC diagnostic push',1)
# retain adjacent independent exact bodies, old two-pass body plus new melee bodies.
p=p.replace('#include "two-pass-bodies.instrumented.inc"','#include "../homm3TwoPass/two-pass-bodies.instrumented.inc"\n#define random PinnedGroundTwoPass::drawRandom\n#include "ground-bodies.instrumented.inc"\n#undef random')
# Oracle has source-defined canCastSpells hero calls; restrict by replacing hasRangedAdvantage original with traced values helper; record abstraction.
end='''
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
'''
p=p.rstrip();p=p[:-1]+end+'\n}\n'
p=p.replace('sourceChooseShooterAction(a,simulated,side);','sourceChooseShooterAction(a,simulated,side);trace.back().order=m_nextAction;trace.back().destination=m_nextActionGridIndex;')
p=p.replace(' int level=1;', ' int level=1,m_hasSpell=0;')
p=p.replace(' bool chooseSpellAction(const army*,int32_t*,type_AI_combat_parameters*)const{throw std::logic_error("live spell excluded");}', ' bool chooseSpellAction(const army*a,int32_t*,type_AI_combat_parameters*)const{if(a->m_monInfo.m_hasSpell==0)return false;throw std::logic_error("live spell excluded");}')
p=p.replace(' bool attemptShooterDefense(const army*,searchArray*,type_AI_combat_parameters*)const{throw std::logic_error("live shooter defense excluded");}', ' bool attemptShooterDefense(const army*,searchArray*,type_AI_combat_parameters*)const{for(int i=0;i<m_numArmies[m_currentSide];++i)if(m_armies[m_currentSide][i].canShoot(0))throw std::logic_error("live shooter defense excluded");return false;}')
p=p.replace(' bool chooseToRun(const army*,int32_t*,searchArray*)const{throw std::logic_error("live run excluded");}', ' unsigned char chooseToRun(const army*,const int32_t*,const searchArray*);')
(out/'PinnedGroundTwoPass.h').write_text(p)
sigs=[('ai.cpp','static long getAttackValue('),('ai.cpp','long combatManager::getAttackChange('),('ai.cpp','static long getEnemyAttackLimit('),('ai.cpp','void combatManager::markFriendlyArmies('),('ai.cpp','void combatManager::markEnemyAttacks('),('ai_tactical.cpp','type_AI_attack_hex_chooser::type_AI_attack_hex_chooser('),('ai_tactical.cpp','long type_AI_attack_hex_chooser::getHexAttackValue('),('ai_tactical.cpp','long type_AI_attack_hex_chooser::getAttackTime('),('ai_tactical.cpp','void type_AI_attack_hex_chooser::checkAdjacentHexes('),('ai_tactical.cpp','unsigned char type_AI_attack_hex_chooser::findAttackHex('),('ai.cpp','unsigned char combatManager::moveToward('),('ai.cpp','bool combatManager::chooseMeleeTarget('),('ai.cpp','long combatManager::chooseMeleeAction('),('ai.cpp','void combatManager::simulateMeleeAttack(army* currentArmy, long hex,'),('ai.cpp','void combatManager::simulateMeleeAttack(army* currentArmy, army* target,')]
sigs.append(('ai.cpp','unsigned char combatManager::chooseToRun('))
bodies='\n\n'.join(extract(*s) for s in sigs)
adv=extract('ai.cpp','unsigned char combatManager::hasRangedAdvantage(')
exact_adv=adv
# Narrow host shim substitutes only already-valued hero contribution; no siege in model.
start=adv.index('        if (m_heroes[side]')
stop=adv.index('\n    }\n',start)
adv=adv[:start]+'        shooterValue[side] += heroRangedValue[side];'+adv[stop:]
start=adv.index('    if (m_fortificationLevel')
stop=adv.index('    return shooterValue',start)
adv=adv[:start]+adv[stop:]
bodies += '\n\n'+adv
(out/'ground-bodies.pinned.inc').write_text(bodies[:bodies.rfind(adv)]+exact_adv+'\n')
bodies=bodies.replace('long combatManager::chooseMeleeAction(','long combatManager::sourceChooseMeleeAction(')
bodies=bodies.replace('    unsigned char stayingInCastle = shouldStayInCastle(&estimate);','    unsigned char stayingInCastle = shouldStayInCastle(&estimate);\n    std::copy(enemyAttacks,enemyAttacks+187,trace.back().danger.begin());trace.back().marked=markedEnemies;')
(out/'ground-bodies.instrumented.inc').write_text(bodies+'\n')
manifest.append({'abstraction':'hasRangedAdvantage uses supplied exact heroRangedValue, no hero spellbook/siege; getAreaEffect returns supplied exact ordinary no-special source areaLoss; path methods delegated to separately pinned original path bodies, actor/map copied without algorithm substitution.'})
(out/'source-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
