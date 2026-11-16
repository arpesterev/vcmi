// Independent reconstructed-body oracle; no production chooser/census in reference.
#include "../AI/Integration/Ports/HoMM3Forecast/OrdinaryTwoPass.h"
#include "../test/aiIntegration/homm3TwoPass/PinnedTwoPass.h"
#include "../AI/Integration/Ports/SourceOrdinaryShooterChoice.h"
#include "../AI/Integration/Ports/HoMM3Forecast/CapturedOrdinaryShooterChoice.h"
#include <iostream>
#include <numeric>
#include <random>
using namespace HoMM3Forecast;
namespace R=PinnedTwoPass;
namespace O=AIIntegration::Ports::HoMM3;
static R::hero heroes[2];
static size_t checks=0,scenarios=0,retries=0;
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
// Reference getters supply facts, not selection. The comparison calls the unchanged
// pinned non-simulated chooseShooterAction and checks final target and retry behavior.
static void compareLive(int side,int slot)
{
 const auto mapped=mapInput(side,false);
 R::type_AI_combat_parameters context(&R::manager,side);
 R::manager.findAITargets(1-side,nullptr,0,&context,nullptr);
 O::SourceShooterChoiceInput in;
 const auto & actor=R::manager.m_armies[side][slot];
 auto facts=[&](uint32_t id)->O::SourceOrdinaryUnit{for(const auto &u:mapped.units)if(u.state.order.id==id)return u.facts;throw std::runtime_error("missing actor");};
 in.actor=facts(actor.id);in.friendlyCombatValue=context.m_friendlyCombatValue;in.enemyCombatValue=context.m_enemyCombatValue;
 in.difficulty=R::game.m_setup.m_difficulty;in.human=R::manager.IsHuman[side];
 for(int i=0;i<R::manager.m_numArmies[1-side];++i){
  const auto & u=R::manager.m_armies[1-side][i];O::SourceShooterCandidate c;
  c.rosterIndex=i;c.excluded=u.is(R::creatureImmobilized)||u.m_creatureType==R::CREATURE_ARROW_TOWER;
  c.incapacitated=u.isIncapacitated();c.cannotAttack=u.cannotAttack();c.unit=facts(u.id);c.attack=mapped.attacks.at({actor.id,u.id});
  c.enemyCensus=O::SourceTargetCensus{};
  if(u.getAITarget())c.enemyCensus->target=O::SourceTargetDecision{u.getAITarget()->index,0,0,u.getAITargetTime()};
  in.enemies.push_back(c);
 }
 const auto captured=chooseCapturedOrdinaryShooter(mapped,actor.id);
 const bool eligible=!actor.cannotAttack()&&!actor.m_spellInfluence[59];check(bool(captured)==eligible,"captured eligibility mismatch");
 const auto out=O::chooseSourceOrdinaryShooter(in);check(bool(out),"live input rejected");
 R::manager.chooseShooterAction(&actor,false,side);const auto &ref=R::manager.trace.back();
 std::optional<uint32_t> target;if(out->targetIndex)target=R::manager.m_armies[1-side][*out->targetIndex].id;
 if(captured)check(captured->target==ref.target,"captured live target mismatch");
 if(captured)check(captured->source.value==out->value,"captured source value mismatch");
 check(target==ref.target,"live target mismatch");check(out->killsOnly==ref.killsOnly,"live killsOnly mismatch");
 check(out->retriedWithoutKillsOnly==ref.retried,"live retry mismatch");
 if(target){bool found=false;for(const auto &c:ref.candidates)if(c.id==*target){check(c.value==out->value,"live selected value mismatch");found=true;}check(found,"selected target absent from reference");}
 ++scenarios;retries+=out->retriedWithoutKillsOnly;
}
int main()
{
 try{
 R::akCreatureTypeTraits[0].baseFightValue=37;R::akCreatureTypeTraits[1].baseFightValue=103;
 for(int n=1;n<=7;++n)for(int m=1;m<=7;++m)for(uint32_t k=1;k<=80;++k){setup(n,m,k*317+n*17+m);compareLive(k%2,0);}
 // Equal values choose the later source slot, not smallest unit ID.
 setup(1,2,19);R::manager.m_armies[1][1]=R::manager.m_armies[1][0];
 for(int i=0;i<2;++i){auto &u=R::manager.m_armies[1][i];u.id=200-i;u.index=i;u.m_gridIndex=86+i*2;u.flags=R::creatureShootingArmy;u.m_spellInfluence.fill(0);R::manager.m_cells[u.m_gridIndex].value=&u;}compareLive(0,0);
 check(R::manager.trace.back().target==199,"later slot tie");
 // A large stale expected-damage ledger must not eliminate a live target.
 setup(1,1,7);for(int s=0;s<2;++s)R::manager.m_armies[s][0].AI_expected_damage=INT32_MAX;compareLive(0,0);check(bool(R::manager.trace.back().target),"stale ledger affected live choice");
 // Dead retained slots are excluded without renumbering following targets.
 setup(1,2,73);auto &dead=R::manager.m_armies[1][0];dead.m_numTroops=0;dead.flags|=R::creatureImmobilized;compareLive(0,0);
 setup(1,0,9);compareLive(0,0);
 setup(1,1,7);auto &a=R::manager.m_armies[0][0];auto &b=R::manager.m_armies[1][0];
 a.flags=b.flags=R::creatureShootingArmy;a.m_spellInfluence.fill(0);b.m_spellInfluence.fill(0);
 a.m_numTroops=1;b.m_numTroops=20;a.m_monInfo.damageLowBound=a.m_monInfo.damageHighBound=1;
 b.m_monInfo.hitPoints=10000;a.m_topCreatureDamage=b.m_topCreatureDamage=0;
 R::game.m_setup.m_difficulty=2;compareLive(0,0);
 check(retries>0,"retry coverage");
 // Unknown target-time is not silently converted to the source no-target divisor.
 O::SourceShooterChoiceInput guarded;guarded.actor=mapInput(0,false).units[0].facts;
 guarded.difficulty=2;O::SourceShooterCandidate candidate;candidate.unit=guarded.actor;
 guarded.enemies.push_back(candidate);check(!O::chooseSourceOrdinaryShooter(guarded),"unknown census admitted");
 guarded.enemies[0].enemyCensus=O::SourceTargetCensus{};
 check(bool(O::chooseSourceOrdinaryShooter(guarded)),"known empty census rejected");
 guarded.enemies[0].rosterIndex=1;check(!O::chooseSourceOrdinaryShooter(guarded),"incomplete roster admitted");
 std::cout<<checks<<" checks, "<<scenarios<<" live source scenarios, "<<retries<<" retries\n";
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
