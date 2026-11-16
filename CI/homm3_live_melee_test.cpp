#include "../AI/Integration/Ports/HoMM3Forecast/OrdinaryLiveMeleeChoice.h"
#include "../test/aiIntegration/homm3GroundTwoPass/PinnedGroundTwoPass.h"
#include <iostream>
#include <numeric>
#include <random>
using namespace HoMM3Forecast;
namespace R=PinnedGroundTwoPass;
namespace O=AIIntegration::Ports::HoMM3;
static R::hero heroes[2];
static size_t checks=0,scenarios=0;
static std::array<size_t,9> actionKinds{};static size_t notForecastFirst=0,liveDifferent=0;
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
static void compareLive(uint32_t active){
 OrdinaryLiveMeleeInput in;in.state=groundInput(R::manager.m_currentSide,false);in.activeId=active;in.noCreatureSpellKnown=true;
 auto out=chooseOrdinaryLiveMelee(in);check(bool(out),"live unavailable");
 R::army * actor=nullptr;for(int s=0;s<2;++s)for(int i=0;i<R::manager.m_numArmies[s];++i)if(R::manager.m_armies[s][i].id==active)actor=&R::manager.m_armies[s][i];
 check(actor!=nullptr,"missing active");R::drawCursor=0;R::manager.chooseMeleeAction(actor,false,false,R::manager.m_currentSide);
 const auto&a=out->choice;const auto&b=R::manager.trace.back();
 check(a.actor==active&&a.actor==b.actor,"actual actor");check(int(a.order)==b.order,"live action");check(a.target==b.target,"live target");
 if(a.value!=b.value)std::cerr<<"value "<<a.value<<" vs "<<b.value<<"\n";
 check(a.value==b.value,"live value");check(a.danger==b.danger,"live danger");check(a.dangerousEnemies==b.marked,"live marked");
 if(b.order==2||b.order==6)check(a.destination==b.destination,"live destination");
 if(b.order==6)check(a.attackHex==b.hex,"live attack hex");
 check(out->drawsConsumed==R::drawCursor,"live draw count");++actionKinds.at(b.order);
 std::vector<R::army*> rawOrder;R::manager.findMoveOrder(&rawOrder);if(!rawOrder.empty()&&rawOrder.front()->id!=active)++notForecastFirst;
 for(const auto&[id,damage]:a.expectedDamageAfter){(void)id;check(damage==0,"live must not simulate damage");}
 const auto savedLive=R::manager.trace.back();R::manager.trace.clear();R::drawCursor=0;
 R::manager.chooseMeleeAction(actor,false,true,R::manager.m_currentSide);
 liveDifferent += savedLive.order!=R::manager.trace.back().order||savedLive.destination!=R::manager.trace.back().destination;
 R::manager.trace.clear();R::manager.trace.push_back(savedLive);
 ++scenarios;
}
int main(){try{
 R::akCreatureTypeTraits[0].baseFightValue=37;R::akCreatureTypeTraits[1].baseFightValue=103;
 for(int n=1;n<=7;++n)for(int m=1;m<=7;++m)for(int k=1;k<=100;++k){
 setup(n,m,k*317+n*17+m);PinnedForecastPath::manager.setupAdjacencyArray();for(int h=0;h<187;++h)for(int d=0;d<6;++d)R::manager.m_adjacentCells[h][d]=PinnedForecastPath::manager.m_adjacentCells[h][d];
 R::manager.m_currentSide=k%2;R::manager.m_inSecondPhase=R::manager.secondPhase;
 R::draws.clear();for(int j=0;j<1000;++j)R::draws.push_back(75+(j*17+k)%26);
 for(int s=0;s<2;++s)for(int i=0;i<R::manager.m_numArmies[s];++i){auto&a=R::manager.m_armies[s][i];a.shooting=s!=R::manager.m_currentSide&&(k+i)%3==0;if(!a.shooting)a.flags &= ~R::creatureShootingArmy;
 a.AI_expected_damage=0;a.m_spellInfluence[59]=a.m_spellInfluence[74]=0;
 if(k%9==0&&i==0)a.m_spellInfluence[72]=1;
 if(k%11==0&&i==0&&s==0)a.speed=0;
 if(k%13==0)a.retaliates=false;
 if(k%17==0)a.m_retaliationCount=2;
 a.areaLoss=k%4?0:20+i*7;if(k%7==0)a.flags|=R::creatureSummoned;
 }
 R::manager.heroRangedValue={k*13,k*17};
 if(k%5==0)for(int row=0;row<11;++row)for(int col:{7,8}){int h=row*17+col;if(!R::manager.m_cells[h].value&&(k%10==0||row!=4))R::manager.m_cells[h].blocked=true;}
 auto&active=R::manager.m_armies[R::manager.m_currentSide][k%R::manager.m_numArmies[R::manager.m_currentSide]];active.flags &= ~(R::creatureDone|R::creatureWaiting);compareLive(active.id);
 }
 // No attack path across a complete barrier; source live chooseToRun escapes
 // the supplied enemy area-spell clustering danger around an adjacent friend.
 setup(2,1,81);R::manager.m_currentSide=0;R::manager.secondPhase=R::manager.m_inSecondPhase=false;
 PinnedForecastPath::manager.setupAdjacencyArray();for(int h=0;h<187;++h)for(int d=0;d<6;++d)R::manager.m_adjacentCells[h][d]=PinnedForecastPath::manager.m_adjacentCells[h][d];
 for(auto&c:R::manager.m_cells)c=R::combatManager::Cell{};
 for(int side=0;side<2;++side)for(int i=0;i<R::manager.m_numArmies[side];++i){auto&a=R::manager.m_armies[side][i];a.shooting=false;a.flags=0;a.AI_expected_damage=0;a.m_spellInfluence.fill(0);a.speed=8-side*2-i;a.areaLoss=40;a.m_gridIndex=side?154:18+i;R::manager.m_cells[a.m_gridIndex].value=&a;}
 for(int col=1;col<=15;++col)R::manager.m_cells[5*17+col].blocked=true;
 R::game.m_setup.m_difficulty=2;compareLive(1);check(R::manager.trace.back().order==2&&!R::manager.trace.back().target,"source run fallback must move without target");
 // Original physical roster limit: every one of 21 enemy candidates reaches
 // the source random call exactly once; 22 peers must be rejected up front.
 setup(1,21,81);R::manager.m_currentSide=0;R::manager.secondPhase=R::manager.m_inSecondPhase=false;
 PinnedForecastPath::manager.setupAdjacencyArray();for(int h=0;h<187;++h)for(int d=0;d<6;++d)R::manager.m_adjacentCells[h][d]=PinnedForecastPath::manager.m_adjacentCells[h][d];
 for(auto&c:R::manager.m_cells)c=R::combatManager::Cell{};
 for(int side=0;side<2;++side)for(int i=0;i<R::manager.m_numArmies[side];++i){auto&a=R::manager.m_armies[side][i];a.shooting=false;a.flags=0;a.AI_expected_damage=0;a.m_spellInfluence.fill(0);a.speed=side?i+1:23;a.areaLoss=0;a.m_gridIndex=side?(4+2*(i/7))*17+1+2*(i%7):18;R::manager.m_cells[a.m_gridIndex].value=&a;}
 R::draws.assign(21,87);compareLive(1);check(R::drawCursor==21,"exact maximum 21 source draws");
 OrdinaryLiveMeleeInput bound;bound.state=groundInput(0,false);bound.activeId=1;bound.noCreatureSpellKnown=true;
 const auto atBound=chooseOrdinaryLiveMelee(bound);check(atBound&&atBound->drawsConsumed==21,"exact maximum 21 production draws");
 auto shortStream=bound;shortStream.state.randomFactors.pop_back();check(!chooseOrdinaryLiveMelee(shortStream),"20 draws insufficient for 21 candidates");
 auto oversized=bound;auto extra=oversized.state.common.units.back();extra.state.order.id=999;extra.state.order.sourceIndex=21;extra.head=145;oversized.state.common.units.push_back(extra);oversized.state.ground[999]={};oversized.state.map[145].occupant=O::SourceGroundIdentity{1,21};check(!chooseOrdinaryLiveMelee(oversized),"22 enemy peers rejected");
 setup(2,2,81);for(int side=0;side<2;++side)for(int i=0;i<2;++i){auto&a=R::manager.m_armies[side][i];a.shooting=false;a.flags&=~R::creatureShootingArmy;a.AI_expected_damage=0;a.m_spellInfluence[59]=a.m_spellInfluence[74]=0;}
 OrdinaryLiveMeleeInput valid;valid.state=groundInput(0,false);valid.activeId=1;valid.noCreatureSpellKnown=true;
 check(bool(chooseOrdinaryLiveMelee(valid)),"live negative-test baseline");
 auto reject=[&](auto mutation,const char*why){auto bad=valid;mutation(bad);check(!chooseOrdinaryLiveMelee(bad),why);};
 reject([](auto&x){x.activeId=9999;},"unknown active");
 reject([](auto&x){x.noCreatureSpellKnown=false;},"unproven creature spell");
 reject([](auto&x){x.state.ordinaryDomainKnown=false;},"unproven ordinary domain");
 reject([](auto&x){x.state.currentSide=Side::DEFENDER;},"active side mismatch");
 reject([](auto&x){x.state.common.units[0].state.berserk=true;},"berserk active");
 reject([](auto&x){x.state.common.units[0].initialExpectedDamage=1;},"live retained simulated damage");
 reject([](auto&x){auto &u=x.state.common.units[1];u.facts.canShoot=true;u.liveCombatValue=int32_t(double(u.state.liveHealth)*u.facts.lossUnitValue[1]/u.facts.hitPoints);},"friendly shooter defense unknown");
 reject([](auto&x){x.state.randomFactors.clear();},"live missing draw stream");
 for(int action:{2,3,6,8}){check(actionKinds[action]>0,"live branch absent");std::cout<<"order "<<action<<": "<<actionKinds[action]<<"; ";}
 check(liveDifferent>0,"live-specific waiting not exercised");std::cout<<"live differs from simulated "<<liveDifferent<<"; ";
 check(notForecastFirst>0,"actual active independence absent");std::cout<<"not forecast first "<<notForecastFirst<<"\n";
 std::cout<<scenarios<<" live melee scenarios, "<<checks<<" checks\n";
 }catch(const std::exception&e){std::cerr<<"scenario "<<scenarios<<" check "<<checks<<": "<<e.what()<<'\n';return 1;}}
