#include "../AI/Integration/Ports/HoMM3Forecast/OrdinaryGroundTwoPass.h"
#include "../test/aiIntegration/homm3GroundTwoPass/PinnedGroundTwoPass.h"
#include <iostream>
#include <numeric>
#include <random>
using namespace HoMM3Forecast;
namespace R=PinnedGroundTwoPass;
namespace O=AIIntegration::Ports::HoMM3;
static R::hero heroes[2];
static size_t checks=0,scenarios=0;
static std::array<size_t,9> actionKinds{};
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
 for(int s=0;s<2;++s)for(int i=0;i<R::manager.m_numArmies[s];++i){const auto&a=R::manager.m_armies[s][i];in.ground[a.id]={bool(a.m_spellInfluence[72]),bool(a.is(R::creatureSummoned)),a.m_retaliationCount,a.areaLoss};
  for(int bs=0;bs<2;++bs)for(int j=0;j<R::manager.m_numArmies[bs];++j){const auto&b=R::manager.m_armies[bs][j];O::SourceOrdinaryAttack p;p.lucky=a.m_luckStatus>0;p.hasController=a.controller!=nullptr;p.meleeShooterPenalty=a.is(R::creatureShootingArmy)&&!a.is(R::creatureNoMeleePenalty);p.attack=a.getAdjustedAttack(&b,false);p.defense=b.getAdjustedDefense(&a,false);p.controllerSkillFactor=0;p.wallPenalty=false;p.distancePenalty=false;p.defenderReduction=b.computeDefenderDamageReduction(false);in.melee[{a.id,b.id}]=p;in.canRetaliate[{a.id,b.id}]=a.canRetaliate(b);}
 }return in;
}
static void compare(int side,bool surrender){
 auto input=groundInput(side,surrender);auto out=simulateOrdinaryGroundTwoPass(input);check(bool(out),"unavailable");
 R::drawCursor=0;R::manager.simulateCombat(side,surrender);
 check(out->actionScratch==input.common.actionScratch,"production scratch preserved");
 check((std::array<int32_t,4>{R::manager.m_nextAction,R::manager.m_nextActionExtra,R::manager.m_nextActionGridIndex,R::manager.m_nextActionGridIndex2})==input.common.actionScratch,"source scratch restored");
 check(out->order==R::manager.fixedOrder,"order");check(out->passes==R::manager.stops.size(),"passes");for(size_t p=0;p<out->passes;++p)check(out->stoppedAt[p]==R::manager.stops[p],"cursor");
 check(out->actions.size()==R::manager.trace.size(),"actions count");
 for(size_t i=0;i<out->actions.size();++i){const auto&a=out->actions[i];const auto&b=R::manager.trace[i];
  if(int(a.order)!=b.order||a.target!=b.target)std::cerr<<"actor "<<a.actor<<" order "<<int(a.order)<<" vs "<<b.order<<" target "<<a.target.value_or(0)<<" vs "<<b.target.value_or(0)<<"\n";
  ++actionKinds.at(int(a.order));check(a.actor==b.actor,"actor");check(int(a.order)==b.order,"action order");check(a.target==b.target,"target");check(a.expectedDamageAfter==b.ledger,"action ledger");
  const auto actor=std::find_if(input.common.units.begin(),input.common.units.end(),[&](const auto&u){return u.state.order.id==a.actor;});if(!actor->facts.canShoot){if(a.value!=b.value)std::cerr<<"actor "<<a.actor<<" value "<<a.value<<" vs "<<b.value<<" order "<<int(a.order)<<"\n";check(a.value==b.value,"action value");check(a.danger==b.danger,"danger map");check(a.dangerousEnemies==b.marked,"marked enemies");if(int(a.order)==6)check(a.attackHex==b.hex,"attack hex");}
  if(int(a.order)==2||int(a.order)==6||int(a.order)==7)check(a.destination==b.destination,"destination");

 }
 check(out->drawsConsumed==R::drawCursor,"draw cursor");
 for(int s=0;s<2;++s)for(int i=0;i<R::manager.m_numArmies[s];++i){const auto&a=R::manager.m_armies[s][i];check(out->expectedDamage.at(a.id)==a.AI_expected_damage,"final ledger");}
 ++scenarios;
}
int main(){try{
 R::akCreatureTypeTraits[0].baseFightValue=37;R::akCreatureTypeTraits[1].baseFightValue=103;
 for(int n=1;n<=7;++n)for(int m=1;m<=7;++m)for(int k=1;k<=100;++k){
 setup(n,m,k*317+n*17+m);PinnedForecastPath::manager.setupAdjacencyArray();for(int h=0;h<187;++h)for(int d=0;d<6;++d)R::manager.m_adjacentCells[h][d]=PinnedForecastPath::manager.m_adjacentCells[h][d];R::manager.m_currentSide=k%2;R::manager.m_inSecondPhase=R::manager.secondPhase;
 R::draws.clear();for(int j=0;j<1000;++j)R::draws.push_back(75+(j*17+k)%26);
 for(int s=0;s<2;++s)for(int i=0;i<R::manager.m_numArmies[s];++i){auto&a=R::manager.m_armies[s][i];a.shooting=(k+i+s)%3==0;if(!a.shooting)a.flags &= ~R::creatureShootingArmy;
 if(k%9==0&&i==0)a.m_spellInfluence[72]=1;
 if(k%11==0&&i==0&&s==0)a.speed=0;
 if(k%13==0)a.retaliates=false;
 if(k%17==0)a.m_retaliationCount=2;
 a.areaLoss=k%4?0:20+i*7;if(k%7==0)a.flags|=R::creatureSummoned;
 }
 R::manager.heroRangedValue={k*13,k*17};
 if(k%5==0)for(int row=0;row<11;++row)for(int col:{7,8}){int h=row*17+col;if(!R::manager.m_cells[h].value&&(k%10==0||row!=4))R::manager.m_cells[h].blocked=true;}
 compare(k%2,k%3!=0);
 }
 setup(0,0,9);compare(0,true);
 setup(2,2,81);const auto valid=groundInput(0,true);
 auto reject=[&](auto mutation,const char*why){auto bad=valid;mutation(bad);check(!simulateOrdinaryGroundTwoPass(bad),why);};
 reject([](auto&x){x.ordinaryDomainKnown=false;},"unproven domain");
 reject([](auto&x){x.common.units[0].initialExpectedDamage=-1;},"negative ledger");
 reject([](auto&x){x.common.units[0].facts.count=INT32_MAX;},"health overflow");
 reject([](auto&x){x.common.units[0].facts.lossUnitValue[0]=std::numeric_limits<double>::quiet_NaN();},"NaN value");
 reject([](auto&x){x.common.units[0].state.catapult=true;},"machine domain");
 reject([](auto&x){x.common.units[0].state.controllingSide=Side::DEFENDER;},"control mismatch");
 reject([](auto&x){x.common.units[0].state.order.effectiveSpeed=126;},"path source budget overflow");
 reject([](auto&x){x.common.units[0].state.order.sourceIndex=4;},"incomplete roster");
 reject([](auto&x){x.map[x.common.units[0].head].occupant.reset();},"missing occupant");
 reject([](auto&x){x.heroRangedValue[0]=-1;},"invalid hero input");
 reject([](auto&x){x.ground.begin()->second.enemyAreaLoss=-1;},"invalid area input");
 for(int action:{2,3,6,7,8}){check(actionKinds[action]>0,"action branch not covered");std::cout<<"order "<<action<<": "<<actionKinds[action]<<"; ";}
 std::cout<<'\n';
 std::cout<<scenarios<<" scenarios, "<<checks<<" checks\n";
 }catch(const std::exception&e){std::cerr<<"scenario "<<scenarios<<" check "<<checks<<": "<<e.what()<<'\n';return 1;}}
