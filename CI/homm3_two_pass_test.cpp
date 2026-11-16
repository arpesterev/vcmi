// Independent reconstructed-body oracle; no production chooser/census in reference.
#include "../AI/Integration/Ports/HoMM3Forecast/OrdinaryTwoPass.h"
#include "../test/aiIntegration/homm3TwoPass/PinnedTwoPass.h"
#include <iostream>
#include <numeric>
#include <random>
using namespace HoMM3Forecast;
namespace R=PinnedTwoPass;
namespace O=AIIntegration::Ports::HoMM3;
static R::hero heroes[2];
static size_t checks=0,scenarios=0,retries=0,secondPassKills=0;
static void check(bool ok,const char * why){++checks;if(!ok)throw std::runtime_error(why);}
static OrdinaryTwoPassInput mapInput(int requested,bool surrender)
{
 OrdinaryTwoPassInput in;in.actingSide=static_cast<Side>(R::manager.m_actingSide);in.requestedSide=static_cast<Side>(requested);
 in.secondPhase=R::manager.secondPhase;in.checkingSurrender=surrender;in.difficulty=R::game.m_setup.m_difficulty;in.human=R::manager.IsHuman;
 in.actionScratch={R::manager.m_nextAction,R::manager.m_nextActionExtra,R::manager.m_nextActionGridIndex,R::manager.m_nextActionGridIndex2};
 R::type_AI_combat_parameters context(&R::manager,requested);
 for(int side=0;side<2;++side)for(int i=0;i<R::manager.m_numArmies[side];++i){
  const auto & a=R::manager.m_armies[side][i];OrdinaryForecastUnit u;auto & o=u.state.order;
  o.id=a.id;o.owningSide=static_cast<Side>(side);o.sourceIndex=i;o.effectiveSpeed=a.speed;o.immobilized=a.is(R::creatureImmobilized);
  o.done=a.is(R::creatureDone);o.waiting=a.is(R::creatureWaiting);o.sourceSpell62Turns=a.m_spellInfluence[62];o.sourceSpell70Turns=a.m_spellInfluence[70];o.sourceSpell74Turns=a.m_spellInfluence[74];
  u.state.controllingSide=static_cast<Side>(side);u.state.berserk=a.m_spellInfluence[59];u.state.liveHealth=a.getTotalHitPoints(false);u.head=a.m_gridIndex;u.initialExpectedDamage=a.AI_expected_damage;
  auto & f=u.facts;f.count=a.m_numTroops;f.hitPoints=a.m_monInfo.hitPoints;f.topWound=a.m_topCreatureDamage;f.minimumDamage=a.m_monInfo.damageLowBound;f.maximumDamage=a.m_monInfo.damageHighBound;
  f.canShoot=a.shooting;f.doubleAttack=a.is(R::creatureTwoAttacks);f.blessed=a.m_spellInfluence[41];f.cursed=a.m_spellInfluence[42];f.blessAmount=a.m_blessAmount;f.curseAmount=a.m_curseAmount;
  for(int channel=0;channel<2;++channel)f.lossUnitValue[channel]=a.getUnitCombatValue(context.m_lowestAttack,context.m_lowestDefense,channel,nullptr);
  u.liveCombatValue=a.getTotalCombatValue(context.m_lowestAttack,context.m_lowestDefense);in.units.push_back(u);
  for(int j=0;j<R::manager.m_numArmies[1-side];++j){const auto & b=R::manager.m_armies[1-side][j];O::SourceOrdinaryAttack p;
   p.attack=a.getAdjustedAttack(&b,true);p.defense=b.getAdjustedDefense(&a,true);p.lucky=a.m_luckStatus>0;p.hasController=a.controller!=nullptr;
   p.controllerSkillFactor=a.controller?a.controller->getArcheryFactor():0;p.wallPenalty=R::manager.wallPenalty;p.distancePenalty=R::manager.distancePenalty;
   p.meleeShooterPenalty=a.is(R::creatureShootingArmy)&&!a.is(R::creatureNoMeleePenalty);p.defenderReduction=b.computeDefenderDamageReduction(true);in.attacks[{a.id,b.id}]=p;
  }
 }
 return in;
}
static void compare(int requested,bool surrender)
{
 const auto in=mapInput(requested,surrender);const auto out=simulateOrdinaryTwoPass(in);check(bool(out),"bounded reference input unexpectedly unavailable");
 R::manager.simulateCombat(requested,surrender);++scenarios;
 check(out->order==R::manager.fixedOrder,"order mismatch");check(out->passes==R::manager.stops.size(),"pass count mismatch");
 for(size_t p=0;p<out->passes;++p)check(out->stoppedAt[p]==R::manager.stops[p],"cursor mismatch");
 check(out->actionScratch==in.actionScratch,"production scratch altered");
 check((std::array<int32_t,4>{R::manager.m_nextAction,R::manager.m_nextActionExtra,R::manager.m_nextActionGridIndex,R::manager.m_nextActionGridIndex2})==in.actionScratch,"reference scratch not restored");
 check(out->actions.size()==R::manager.trace.size(),"action count mismatch");
 for(size_t i=0;i<out->actions.size();++i){const auto & a=out->actions[i];const auto & b=R::manager.trace[i];
  check(a.actor==b.actor&&a.target==b.target,"selected actor/target mismatch");check(a.killsOnly==b.killsOnly&&a.retriedWithoutKillsOnly==b.retried,"killsOnly retry mismatch");retries+=b.retried;
  check(a.damage==b.damage,"raw applied damage mismatch");check(a.expectedDamageAfter==b.ledger,"per-action ledger mismatch");
  auto rank=[&](const auto & x,const auto & y){check(x.size()==y.size(),"candidate size mismatch");for(size_t j=0;j<x.size();++j)check(x[j].id==y[j].id&&x[j].value==y[j].value,"candidate score mismatch");};
  rank(a.initialCandidates,b.initialCandidates);rank(a.candidates,b.candidates);
 }
 for(int side=0;side<2;++side)for(int i=0;i<R::manager.m_numArmies[side];++i){const auto & u=R::manager.m_armies[side][i];check(out->expectedDamage.at(u.id)==u.AI_expected_damage,"final ledger mismatch");}
 if(surrender){bool dead=true;for(int i=0;i<R::manager.m_numArmies[requested];++i)dead &= R::manager.m_armies[requested][i].getTotalHitPoints(true)==0;secondPassKills+=dead;}
}
static void setup(int n,int m,uint32_t seed)
{
 R::manager=R::combatManager{};R::game.m_setup.m_difficulty=seed%5;R::manager.IsHuman={bool(seed&1),bool(seed&2)};R::manager.m_actingSide=(seed>>2)&1;R::manager.secondPhase=(seed%7)==0;
 R::manager.m_nextAction=17;R::manager.m_nextActionExtra=29;R::manager.m_nextActionGridIndex=42;R::manager.m_nextActionGridIndex2=55;
 for(int side=0;side<2;++side){heroes[side]=R::hero{};heroes[side].m_id=side;heroes[side].m_skillLevel[R::eSecSkillArchery]=(seed+side)%4;heroes[side].m_skillLevel[R::eSecSkillDefense]=(seed+side+1)%4;}
 R::manager.m_numArmies[0]=n;R::manager.m_numArmies[1]=m;R::manager.wallPenalty=seed%11==0;R::manager.distancePenalty=seed%13==0;
 auto pick=[&](uint32_t v){seed=seed*1664525u+1013904223u;return(seed>>8)%v;};
 std::vector<int> speeds(n+m);std::iota(speeds.begin(),speeds.end(),1);std::mt19937 generator(seed);std::shuffle(speeds.begin(),speeds.end(),generator);int pos=0;
 for(int s=0;s<2;++s)for(int i=0;i<R::manager.m_numArmies[s];++i){auto & u=R::manager.m_armies[s][i];u.id=100*s+i+1;u.group=s;u.index=i;u.speed=speeds[pos++];u.m_gridIndex=18+s*68+i*2;u.shooting=true;u.flags=R::creatureShootingArmy;
  u.m_numTroops=1+pick(20);u.m_monInfo.hitPoints=5+pick(30);u.m_topCreatureDamage=pick(u.m_monInfo.hitPoints);u.m_monInfo.damageLowBound=1+pick(8);u.m_monInfo.damageHighBound=u.m_monInfo.damageLowBound+pick(9);u.m_monInfo.attackSkill=pick(15);u.m_monInfo.defenseSkill=pick(15);u.m_creatureType=pick(2);
  if(pick(3)==0)u.flags|=R::creatureTwoAttacks;
  if(pick(2))u.controller=&heroes[s];
  const auto spell=pick(3);u.m_spellInfluence[41]=spell==1;u.m_spellInfluence[42]=spell==2;u.m_blessAmount=pick(2);u.m_curseAmount=pick(2);
  u.m_luckStatus=pick(2);u.m_airShieldFactor=pick(2)?1:.75;u.AI_expected_damage=pick(u.getTotalHitPoints(false)+1);
  if(pick(5)==0)u.flags|=R::creatureDone;
  if(pick(5)==0)u.flags|=R::creatureWaiting;
  // Distinct speed keys avoid comparator ambiguity even across same roster index.
  if(pick(7)==0)u.m_spellInfluence[74]=1;
  if(pick(11)==0)u.m_spellInfluence[59]=1;
  R::manager.m_cells[u.m_gridIndex].value=&u;
 }
}
int main()
{
 try{
 R::akCreatureTypeTraits[0].baseFightValue=37;R::akCreatureTypeTraits[1].baseFightValue=103;
 for(int n=2;n<=7;++n)for(int m=2;m<=7;++m)for(uint32_t k=1;k<=60;++k){setup(n,m,k*317+n*17+m);compare(k%2,k%3!=0);}
 // Retained immobilized unit's nonzero ledger is not cleared with move order.
 setup(2,2,73);R::manager.m_armies[0][0].flags|=R::creatureImmobilized;compare(0,true);
 // Empty order still constructs both side-local contexts and restores scratch.
 setup(0,0,9);compare(0,true);
 // A source zero-best killsOnly retry: weak ranged damage against high-HP enemies.
 setup(1,1,7);auto & a=R::manager.m_armies[0][0];auto & b=R::manager.m_armies[1][0];
 a.flags=R::creatureShootingArmy;b.flags=R::creatureShootingArmy;a.m_spellInfluence.fill(0);b.m_spellInfluence.fill(0);a.m_numTroops=1;b.m_numTroops=20;a.m_monInfo.damageLowBound=a.m_monInfo.damageHighBound=1;b.m_monInfo.hitPoints=10000;a.m_topCreatureDamage=b.m_topCreatureDamage=0;a.speed=20;b.speed=10;a.AI_expected_damage=b.AI_expected_damage=0;R::game.m_setup.m_difficulty=2;R::manager.secondPhase=false;compare(0,true);

 // A,B,A must stop at the second ownership boundary, never run a third prefix.
 setup(2,1,29);for(int side=0;side<2;++side)for(int i=0;i<R::manager.m_numArmies[side];++i){auto &u=R::manager.m_armies[side][i];u.flags=R::creatureShootingArmy;u.m_spellInfluence.fill(0);u.m_numTroops=20;u.m_monInfo.hitPoints=100;u.m_topCreatureDamage=0;u.m_monInfo.damageLowBound=u.m_monInfo.damageHighBound=1;u.AI_expected_damage=0;}
 R::manager.secondPhase=false;R::manager.m_armies[0][0].speed=30;R::manager.m_armies[1][0].speed=20;R::manager.m_armies[0][1].speed=10;
 {const auto out=simulateOrdinaryTwoPass(mapInput(0,true));check(out&&out->stoppedAt[0]==1&&out->stoppedAt[1]==2&&out->actions.size()==2,"A B A boundary contract");}compare(0,true);
 // First requested side absent from the first eligible action: pass1 cursor0.
 setup(1,1,29);for(int side=0;side<2;++side){auto &u=R::manager.m_armies[side][0];u.flags=R::creatureShootingArmy;u.m_spellInfluence.fill(0);u.speed=side?20:10;}R::manager.secondPhase=false;
 {const auto out=simulateOrdinaryTwoPass(mapInput(0,true));check(out&&out->stoppedAt[0]==0,"empty first ownership prefix");}compare(0,true);
 // Later original roster slot wins equal target value, independently of stable IDs.
 setup(1,2,19);R::manager.secondPhase=false;R::manager.m_armies[0][0].flags=R::creatureShootingArmy;R::manager.m_armies[0][0].m_spellInfluence.fill(0);R::manager.m_armies[0][0].speed=30;
 R::manager.m_armies[1][1]=R::manager.m_armies[1][0];
 for(int i=0;i<2;++i){auto &u=R::manager.m_armies[1][i];u.id=200-i;u.index=i;u.m_gridIndex=86+i*2;u.speed=10-i;u.flags=R::creatureShootingArmy;u.m_spellInfluence.fill(0);R::manager.m_cells[u.m_gridIndex].value=&u;}
 {const auto out=simulateOrdinaryTwoPass(mapInput(0,false));check(out&&out->actions.front().target==199,"later roster tie not stable ID");}compare(0,false);
 // Retained corpse remains in raw order while skip-before-boundary ignores it.
 setup(2,2,139);auto &corpse=R::manager.m_armies[1][0];corpse.m_numTroops=0;corpse.m_topCreatureDamage=0;corpse.AI_expected_damage=0;R::manager.m_cells[corpse.m_gridIndex].value=nullptr;compare(0,true);
 // Zero-speed ordinary shooter, with the same source time1 census semantics.
 setup(1,1,51);R::manager.m_armies[1][0].speed=0;compare(0,true);

 // Known no-target opportunities are traced rather than unavailable.
 setup(2,0,29);for(int i=0;i<2;++i){auto &u=R::manager.m_armies[0][i];u.flags=R::creatureShootingArmy;u.m_spellInfluence.fill(0);}compare(0,true);
 // Source retained corpse may share a live unit head; choose resolves live occupancy.
 setup(2,2,75);auto & dead=R::manager.m_armies[1][0];auto & live=R::manager.m_armies[1][1];R::manager.m_cells[dead.m_gridIndex].value=nullptr;dead.m_numTroops=0;dead.m_topCreatureDamage=0;dead.AI_expected_damage=0;dead.m_gridIndex=live.m_gridIndex;compare(0,true);
 for(int spell:{62,70})for(int turns:{1,2}){setup(2,2,107);R::manager.m_armies[1][0].m_spellInfluence[spell]=turns;compare(0,true);}
 // Negative/overflow/missing-context vectors must be rejected before a source-UB oracle run.
 setup(2,2,81);const auto valid=mapInput(0,true);
 auto reject=[&](auto mutation,const char *why){auto bad=valid;mutation(bad);check(!simulateOrdinaryTwoPass(bad),why);};
 reject([](auto &x){x.units[0].initialExpectedDamage=-1;},"negative retained ledger rejected");
 reject([](auto &x){x.units[0].initialExpectedDamage=INT32_MAX;},"over-live ledger rejected");
 reject([](auto &x){x.units[0].facts.count=INT32_MAX;},"HP source multiplication overflow rejected");
 reject([](auto &x){x.units[0].liveCombatValue=INT32_MAX;},"inconsistent constructor totals rejected");
 reject([](auto &x){x.units[0].head=x.units[1].head;},"live hex alias rejected");
 reject([](auto &x){x.units[0].state.catapult=true;},"machine outside ordinary domain rejected");
 reject([](auto &x){x.units[0].state.controllingSide=Side::DEFENDER;},"control ownership mismatch rejected");
 reject([](auto &x){x.units[0].state.order.id=x.units[1].state.order.id;},"duplicate identity rejected");
 reject([](auto &x){x.units[0].state.order.sourceIndex=29;},"incomplete original roster rejected");

 reject([](auto &x){x.units[0].facts.lossUnitValue[1]=std::numeric_limits<double>::quiet_NaN();},"NaN unit loss unavailable");
 reject([](auto &x){x.units[0].facts.lossUnitValue[0]=std::numeric_limits<double>::infinity();},"infinite unit loss unavailable");
 reject([](auto &x){for(auto &u:x.units){u.state.order.sourceSpell74Turns=1;u.facts.count=1;u.facts.hitPoints=10;u.facts.topWound=0;u.state.liveHealth=10;u.initialExpectedDamage=0;u.facts.lossUnitValue={double(INT32_MAX),double(INT32_MAX)};u.liveCombatValue=INT32_MAX;}},"constructor overflow before all-skipped prefix");
 reject([](auto &x){for(auto &u:x.units){u.state.order.sourceSpell62Turns=u.state.order.sourceSpell70Turns=u.state.order.sourceSpell74Turns=0;u.state.berserk=false;}auto &u=x.units[0];u.facts.count=1;u.facts.hitPoints=1500000000;u.facts.topWound=0;u.state.liveHealth=1500000000;u.initialExpectedDamage=0;u.facts.lossUnitValue={1,1};u.liveCombatValue=1;},"source ceil addition overflow before division");
 reject([](auto &x){auto unit=x.units[0];x.units.clear();x.attacks.clear();for(int i=0;i<22;++i){auto u=unit;u.state.order.id=1000+i;u.state.order.sourceIndex=i;u.head=18+i%15+17*(i/15);x.units.push_back(u);}},"22-peer roster exceeds pinned physical21 capacity");
 check(retries>0,"zero-best retry not exercised");
 std::cout<<scenarios<<" source two-pass scenarios, "<<checks<<" checks, "<<retries<<" retries, "<<secondPassKills<<" no-survivor outcomes\n";
 }catch(const std::exception&e){std::cerr<<"scenario "<<scenarios<<" check "<<checks<<": "<<e.what()<<'\n';return 1;}
}
