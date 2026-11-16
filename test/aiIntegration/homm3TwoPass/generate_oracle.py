from pathlib import Path
import re,json,hashlib
import argparse

parser = argparse.ArgumentParser(description="Regenerate the two-pass fixture using the source pin in SOURCE_NOTICE.md")
parser.add_argument("upstream", type=Path, help="Path to a checkout of the pinned homm3-decomp source")
upstream = parser.parse_args().upstream
out = Path(__file__).resolve().parent
base = out.parent / "homm3Exchange"
ai, tactical, army = (upstream / "src" / name for name in ("ai.cpp", "ai_tactical.cpp", "army.cpp"))
def body(path,sig):
 s=path.read_text();start=s.index(sig);brace=s.index('{',start);depth=1;i=brace+1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[start:i]+'\n'
parts=[]
for path,sigs in [(ai,['long combatManager::getTotalCombatValue(','long combatManager::chooseShooterTarget(','void combatManager::chooseShooterAction(','static long getMoveOrder(','unsigned char func_moves_before::operator()(','void combatManager::findMoveOrder(','static void simulateSimpleAttack(','long combatManager::simulateActions(','void combatManager::simulateCombat(']),(tactical,['long type_AI_combat_parameters::getRangedAttackValue(','type_AI_combat_parameters::type_AI_combat_parameters(']),(army,['long army::getTotalCombatValue(','void army::setAIExpectedDamage('])]:
 for sig in sigs:parts.append(("src/" + path.name,sig,body(path,sig)))
pinned='// Exact reconstructed-source bodies; see source-manifest.json\n\n'+'\n'.join(x[2] for x in parts)
(out/'two-pass-bodies.pinned.inc').write_text(pinned)
# Derived instrumentation: original method names renamed only where wrappers need traces;
# two observer insertions capture damage and prefix return cursor, without changing decisions.
instrumented=pinned.replace('long combatManager::chooseShooterTarget(', 'long combatManager::sourceChooseShooterTarget(').replace('void combatManager::chooseShooterAction(', 'void combatManager::sourceChooseShooterAction(').replace('long type_AI_combat_parameters::getRangedAttackValue(', 'long type_AI_combat_parameters::sourceRangedAttackValue(').replace('long combatManager::simulateActions(', 'long combatManager::sourceSimulateActions(')
instrumented=instrumented.replace('long damage = aiGetAttackDamage(*(currentArmy), hits, *(target), ranged, distance);','long damage = aiGetAttackDamage(*(currentArmy), hits, *(target), ranged, distance);\n    g_combatManager->trace.back().damage = damage;')
(out/'two-pass-bodies.instrumented.inc').write_text(instrumented)
h=(base/'PinnedExchange.h').read_text().replace('namespace PinnedExchange','namespace PinnedTwoPass').replace('#include <algorithm>','#include <algorithm>\n#include <cstdint>\n#include <vector>\n#include <map>\n#include <optional>')
h=h.replace('creatureClone=2048','creatureClone=2048,creatureDone=4096,creatureWaiting=8192,creatureCatapult=16384')
h=h.replace('CREATURE_BALLISTA=ARMY_CREATURE_BALLISTA','CREATURE_BALLISTA=ARMY_CREATURE_BALLISTA, CREATURE_MAGOG=50,CREATURE_LICH,CREATURE_POWER_LICH,CREATURE_CATAPULT,CREATURE_FIRST_AID_TENT,CREATURE_AMMO_CART, ARMY_CREATURE_FIRST_AID_TENT=CREATURE_FIRST_AID_TENT,ARMY_CREATURE_AMMO_CART=CREATURE_AMMO_CART,AI_ORDER_SHOOT=7,AI_ORDER_MOVE_AND_ATTACK=6')
h=h.replace('int flags=0,m_creatureType', 'uint32_t id=0;\n int flags=0,m_creatureType')
h=h.replace('long getAIExpectedDamage() const', '''bool isIncapacitated() const { return m_spellInfluence[62] || m_spellInfluence[70] || m_spellInfluence[74]; }
 bool cannotAttack() const { return isIncapacitated() || is(creatureImmobilized) || m_creatureType==ARMY_CREATURE_FIRST_AID_TENT || m_creatureType==ARMY_CREATURE_AMMO_CART; }
 int getControllingSide() const { return group; }
 const army * getAITarget() const { return AI_target; }
 long getTotalCombatValue(long,long) const;
 void setAIExpectedDamage(long);
 long getAIExpectedDamage() const''')
h=h.replace('army m_armies[2][7]', 'army m_armies[2][21]')
h=h.replace('struct Cell { bool m_validMove=false; };', 'struct Cell { bool m_validMove=false; army * value=nullptr; army * getArmy() const {return value;} };')
h=h.replace('struct combatManager\n{','''struct Ranked {uint32_t id; int32_t value;};
struct Action {uint32_t actor;std::optional<uint32_t> target;bool killsOnly=false,retried=false;std::vector<Ranked> initialCandidates,candidates;int32_t damage=0;std::map<uint32_t,int32_t> ledger;};
struct func_moves_before {unsigned char operator()(const army*,const army*);};
struct combatManager
{
 int m_actingSide=0;bool secondPhase=false;
 long m_nextAction=0,m_nextActionExtra=0,m_nextActionGridIndex=0,m_nextActionGridIndex2=0;
 std::vector<Action> trace;std::vector<size_t> stops;std::vector<uint32_t> fixedOrder;
 bool isInSecondPhase() const {return secondPhase;}
 long getTotalCombatValue(long,long,long,bool) const;
 void findMoveOrder(std::vector<army*>*);
 long chooseShooterTarget(const army*,type_AI_combat_parameters&,long&) const;
 long sourceChooseShooterTarget(const army*,type_AI_combat_parameters&,long&) const;
 void chooseShooterAction(const army*,bool,long);
 void sourceChooseShooterAction(const army*,bool,long);
 long simulateActions(std::vector<army*>&,long,long);
 long sourceSimulateActions(std::vector<army*>&,long,long);
 void simulateCombat(long,unsigned char);
 long getAreaAttackValue(const army&,long,long,const type_AI_combat_parameters&) const {throw std::logic_error("area excluded");}
 bool chooseCyclopsAction(long,long,type_AI_combat_parameters&) {throw std::logic_error("cyclops excluded");}
 void chooseMeleeAction(const army*,int,int,long) {throw std::logic_error("ground actor excluded");}
 void simulateMeleeAttack(army*,army*,long) {throw std::logic_error("ground actor excluded");}''')
h=h.replace('struct type_AI_combat_parameters\n{','''struct type_AI_combat_parameters
{
 type_AI_combat_parameters(const combatManager*,long);
 long m_enemyGroup=0,m_friendlyCombatValue=0,m_enemyCombatValue=0,m_awakeFriendlyValue=0,m_awakeEnemyValue=0,m_roundsLeft=0;
 long getGroup() const {return m_ourGroup;}long getEnemyGroup() const {return m_enemyGroup;}
 long getRangedAttackValue(const army&,const army&) const;
 long sourceRangedAttackValue(const army&,const army&) const;''')
# Reuse exact scalar pin through relative paths, not production helpers.
h=re.sub(r'#include "([^"/]+\.inc)"',r'#include "../homm3Exchange/\1"',h)
h=h.replace('#include "../homm3Exchange/exchange-bodies.pinned.inc"','''inline constexpr double g_aiOddsLadder[6]={2.6,1.9,1.5,1.31,1.2,1.13};
inline bool validHex(long value){return value>=0 && value<187;}
#define long int32_t
#include "../homm3Exchange/exchange-bodies.pinned.inc"''')
h=h.replace('#include "../homm3Exchange/census-bodies.pinned.inc"','#include "../homm3Exchange/census-bodies.pinned.inc"\n#include "two-pass-bodies.instrumented.inc"\n#undef long')
h=h.replace('#pragma GCC diagnostic ignored "-Wunused-parameter"','#pragma GCC diagnostic ignored "-Wunused-parameter"\n#pragma GCC diagnostic ignored "-Wsign-compare"')
# Harness declaration widths match source 32-bit long; exact body files use temporary macro above.
h=re.sub(r'\blong\b','int32_t',h).replace('#define int32_t int32_t','#define long int32_t').replace('#undef int32_t','#undef long')
h=h.replace('\n#undef DATA_COMPGEN\n}', '''
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
''')
(out/'PinnedTwoPass.h').write_text(h)
manifest={'pin':'e12906accf96c2662b1abadbc49ddc111991bf21','width':'Exact long tokens compiled as int32_t for source ABI, harness declarations matched; no LP64 arithmetic oracle','bodies':[{'source':p,'signature':sig,'sha256':hashlib.sha256(s.encode()).hexdigest()} for p,sig,s in parts],'instrumentation':'Renamed four methods to source-prefixed names for trace wrappers; one damage trace assignment. Scalar/census bodies untouched, no production algorithm used.'}
manifest['supplemental_and_inherited_sha256']={str(p.relative_to(out.parent)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [out/'ai_tactical.h.snapshot',out/'cmbtmgr.h.snapshot',*(base.glob('*.inc'))]}
(out/'source-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
