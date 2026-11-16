#include "../AI/Integration/Ports/HoMM3Forecast/OrdinaryLiveMixedChoice.h"
#include "../test/aiIntegration/homm3MixedLive/PinnedMixedLive.h"
#include <iostream>
#include <numeric>
#include <random>
using namespace HoMM3Forecast;
namespace R=PinnedMixedLive;
namespace O=AIIntegration::Ports::HoMM3;
static R::hero heroes[2];
static size_t checks=0,scenarios=0;
static std::array<size_t,9> actionKinds{};static size_t defenseCalls=0,defenseAccepted=0;
static void check(bool ok,const char *why){++checks;if(!ok)throw std::runtime_error(why);}
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

static OrdinaryGroundTwoPassInput groundInput(int side,bool surrender){
 OrdinaryGroundTwoPassInput in;in.ordinaryDomainKnown=true;in.common=mapInput(side,surrender);in.currentSide=static_cast<Side>(R::manager.m_currentSide);in.heroRangedValue=R::manager.heroRangedValue;in.randomFactors=R::draws;
 for(int h=0;h<187;++h){const auto &c=R::manager.m_cells[h];in.map[h].blocked=c.blocked;if(c.value)in.map[h].occupant=O::SourceGroundIdentity{c.value->group,c.value->index};}
 for(int s=0;s<2;++s)for(int i=0;i<R::manager.m_numArmies[s];++i){const auto&a=R::manager.m_armies[s][i];in.ground[a.id]={bool(a.m_spellInfluence[72]),bool(a.is(R::creatureSummoned)),a.m_retaliationCount,a.areaLoss,bool(a.is(R::creatureNoMeleePenalty)),a.m_facing,a.m_expectedMoveOrder};
  for(int bs=0;bs<2;++bs)for(int j=0;j<R::manager.m_numArmies[bs];++j){const auto&b=R::manager.m_armies[bs][j];O::SourceOrdinaryAttack p;p.lucky=a.m_luckStatus>0;p.hasController=a.controller!=nullptr;p.meleeShooterPenalty=a.is(R::creatureShootingArmy)&&!a.is(R::creatureNoMeleePenalty);p.attack=a.getAdjustedAttack(&b,false);p.defense=b.getAdjustedDefense(&a,false);p.controllerSkillFactor=0;p.wallPenalty=false;p.distancePenalty=false;p.defenderReduction=b.computeDefenderDamageReduction(false);in.melee[{a.id,b.id}]=p;in.canRetaliate[{a.id,b.id}]=a.canRetaliate(b);}
 }return in;
}

static void compareLive(R::army &actor){
 OrdinaryLiveMixedInput in{groundInput(R::manager.m_currentSide,false),actor.id,true};
 const auto out=chooseOrdinaryLiveMixed(in);check(bool(out),"mixed unavailable");
 R::manager.currentArmy=&actor;R::drawCursor=0;R::manager.doCompAI(1-R::manager.m_currentSide);
 const auto&a=out->choice;const auto&b=R::manager.trace.back();
 if(int(a.order)!=b.order)std::cerr<<"order "<<int(a.order)<<" vs "<<b.order<<" case "<<scenarios<<"\n";
 check(a.actor==actor.id&&a.actor==b.actor,"actual actor");check(int(a.order)==b.order,"mixed action");check(a.target==b.target,"mixed target");
 if(!actor.shooting){check(a.value==b.value,"mixed value");check(a.danger==b.danger,"mixed danger");check(a.dangerousEnemies==b.marked,"mixed marked");}
 if(b.order==2||b.order==6||b.order==7)check(a.destination==b.destination,"mixed destination");
 if(b.order==6)check(a.attackHex==b.hex,"mixed attack hex");
 check(out->drawsConsumed==R::drawCursor,"mixed draw count");++actionKinds.at(b.order);
 for(int side=0;side<2;++side)for(int i=0;i<R::manager.m_numArmies[side];++i){const auto&u=R::manager.m_armies[side][i];check(a.expectedDamageAfter.at(u.id)==u.AI_expected_damage,"retained ledger");}
 defenseCalls+=R::manager.defenseCalls;defenseAccepted+=R::manager.defenseAccepted;
 ++scenarios;
}
int main(){try{
 R::akCreatureTypeTraits[0].baseFightValue=37;R::akCreatureTypeTraits[1].baseFightValue=103;
 for(int n=1;n<=7;++n)for(int m=1;m<=7;++m)for(int k=1;k<=100;++k){
 setup(n,m,k*317+n*17+m);PinnedForecastPath::manager.setupAdjacencyArray();for(int h=0;h<187;++h){R::manager.m_cells[h].m_refX=(h%17)*44+((h/17)%2?22:44)+14;for(int d=0;d<6;++d)R::manager.m_adjacentCells[h][d]=PinnedForecastPath::manager.m_adjacentCells[h][d];}
 R::manager.m_currentSide=k%2;R::manager.m_inSecondPhase=R::manager.secondPhase;
 R::draws.assign(10000,75+k%26);
 for(int side=0;side<2;++side)for(int i=0;i<R::manager.m_numArmies[side];++i){auto&u=R::manager.m_armies[side][i];u.shooting=(k+i+side)%3==0;if(!u.shooting)u.flags&=~R::creatureShootingArmy;u.m_spellInfluence[59]=u.m_spellInfluence[74]=0;u.m_facing=(k+i)%2;u.AI_expected_damage=2*u.getTotalHitPoints(false)+i;u.AI_target=&R::manager.m_armies[side][0];u.AI_target_value=12345;u.AI_target_distance=99;u.AI_possible_targets=0x7fffffff;}
 auto&actor=R::manager.m_armies[k%2][k%R::manager.m_numArmies[k%2]];
 if(k%10==0){R::drawCursor=0;R::manager.simulateCombat(R::manager.m_currentSide,1);R::manager.trace.clear();R::manager.stops.clear();}
 if(k%11==0&&m>1){auto&dead=R::manager.m_armies[1][m-1];if(&dead!=&actor){R::manager.m_cells[dead.m_gridIndex].value=nullptr;dead.m_numTroops=dead.m_topCreatureDamage=0;dead.flags|=R::creatureImmobilized;dead.m_expectedMoveOrder=k%43;}}
 compareLive(actor);
 if(!actor.shooting){auto absent=OrdinaryLiveMixedInput{groundInput(R::manager.m_currentSide,false),actor.id,true};bool removed=false;for(const auto&u:absent.state.common.units)if(u.state.order.immobilized){absent.state.ground.at(u.state.order.id).priorMoveRank.reset();removed=true;}if(removed)check(!chooseOrdinaryLiveMixed(absent),"unknown retained rank rejects");}
 }
 // A solid public wall isolates enemies; live melee must protect friendly
 // shooters, with both explicit facings and last-client order variation.
 for(int k=0;k<200;++k){
 setup(3,2,10000+k);R::manager.m_currentSide=0;R::manager.secondPhase=false;R::manager.m_inSecondPhase=false;R::draws.assign(1000,90);
 PinnedForecastPath::manager.setupAdjacencyArray();
 for(int h=0;h<187;++h){R::manager.m_cells[h].value=nullptr;R::manager.m_cells[h].m_refX=(h%17)*44+((h/17)%2?22:44)+14;R::manager.m_cells[h].blocked=h/17==5;for(int d=0;d<6;++d)R::manager.m_adjacentCells[h][d]=PinnedForecastPath::manager.m_adjacentCells[h][d];}
 for(int side=0;side<2;++side)for(int i=0;i<R::manager.m_numArmies[side];++i){auto&u=R::manager.m_armies[side][i];u.m_spellInfluence[59]=u.m_spellInfluence[74]=0;u.m_gridIndex=side?137+i*3:36+i*3;u.m_facing=(k+i)%2;u.shooting=side==0&&i>0;if(!u.shooting)u.flags&=~R::creatureShootingArmy;if(k%4==1&&i==2)u.flags|=R::creatureNoMeleePenalty;u.AI_expected_damage=3*u.getTotalHitPoints(false);R::manager.m_cells[u.m_gridIndex].value=&u;}
 auto&actor=R::manager.m_armies[0][0];actor.areaLoss=k%5==0?1:0;compareLive(actor);
 check(R::manager.defenseAccepted==(actor.areaLoss?0:1),"directed shooter defense branch");
 }
 // Retained corpse rank is a real chooser dependency, not just telemetry.
 // The same live army attacks a different enemy if a prior rank is guessed0.
 for(int rank:{6,0}){
 setup(3,3,91);R::manager.m_currentSide=R::manager.m_actingSide=0;R::game.m_setup.m_difficulty=4;R::manager.secondPhase=R::manager.m_inSecondPhase=false;R::draws.assign(1000,90);
 PinnedForecastPath::manager.setupAdjacencyArray();
 for(int h=0;h<187;++h){R::manager.m_cells[h].value=nullptr;R::manager.m_cells[h].m_refX=(h%17)*44+((h/17)%2?22:44)+14;for(int d=0;d<6;++d)R::manager.m_adjacentCells[h][d]=PinnedForecastPath::manager.m_adjacentCells[h][d];}
 const int positions[2][3]={{38,108,59},{57,37,76}};
 for(int side=0;side<2;++side)for(int i=0;i<3;++i){auto&u=R::manager.m_armies[side][i];u.m_gridIndex=positions[side][i];u.shooting=false;u.flags&=~(R::creatureShootingArmy|R::creatureDone|R::creatureWaiting);u.m_spellInfluence[59]=u.m_spellInfluence[74]=0;u.AI_expected_damage=0;R::manager.m_cells[u.m_gridIndex].value=&u;}
 auto&dead=R::manager.m_armies[1][2];R::manager.m_cells[dead.m_gridIndex].value=nullptr;dead.flags|=R::creatureImmobilized;dead.m_numTroops=dead.m_topCreatureDamage=0;dead.m_expectedMoveOrder=rank;
 compareLive(R::manager.m_armies[0][0]);const auto&actual=R::manager.trace.back();check(actual.target==std::optional<uint32_t>(rank?102:101),"corpse rank changes actual chosen enemy");check(actual.value==(rank?1726:426),"corpse rank changes exact source value");
 }
 std::cout<<"defense "<<defenseCalls<<" calls / "<<defenseAccepted<<" accepted\n";
 std::cout<<"mixed live: "<<scenarios<<" scenarios, "<<checks<<" checks; actions";for(size_t i=0;i<actionKinds.size();++i)std::cout<<" "<<i<<":"<<actionKinds[i];std::cout<<"\n";
return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<scenarios<<": "<<e.what()<<"\n";return 1;}}
