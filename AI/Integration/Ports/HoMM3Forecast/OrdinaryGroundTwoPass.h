/* Ordinary ground/shooter source model with explicit simulated and live modes.
 * sushi-shi/homm3-decomp e12906accf96c2662b1abadbc49ddc111991bf21, CC0-1.0. */
#pragma once
#include "OrdinaryTwoPass.h"
#include "InvariantMoveOrder.h"
#include "OrdinaryGroundPath.h"
#include <stdexcept>

namespace HoMM3Forecast
{
struct OrdinaryGroundUnit
{
 bool bound=false,summoned=false;
 int retaliationCount=1;
 // Source loss value of the enemy's best affordable area spell. Supplied by
 // a separate source-valued producer; no hidden enemy spellbook lookup here.
 int32_t enemyAreaLoss=0;
 bool noMeleePenalty=false;
 int facing=0;
 std::optional<int32_t> priorMoveRank=std::nullopt;
};
struct OrdinaryGroundTwoPassInput
{
 // Caller certifies the ordinary, single-hex, no-special domain described in
 // test/aiIntegration/homm3GroundTwoPass/README.md; this is not a native adapter.
 bool ordinaryDomainKnown=false;
 OrdinaryTwoPassInput common;
 Side currentSide=Side::ATTACKER;
 Ordinary::SourceGroundMap map{};
 std::map<UnitId,OrdinaryGroundUnit> ground;
 std::map<std::pair<UnitId,UnitId>,Ordinary::SourceOrdinaryAttack> melee;
 std::map<std::pair<UnitId,UnitId>,bool> canRetaliate;
 std::array<int32_t,2> heroRangedValue{};
 // Draws in source invocation order, one per accepted melee target candidate.
 std::vector<int> randomFactors;
};
enum class OrdinaryGroundOrder { MOVE=2,DEFEND=3,MELEE=6,SHOOT=7,WAIT=8 };
struct OrdinaryGroundAction
{
 UnitId actor=0;
 OrdinaryGroundOrder order=OrdinaryGroundOrder::DEFEND;
 std::optional<UnitId> target;
 int attackHex=-1,destination=-1;
 int32_t value=0;
 std::array<int32_t,187> danger{};
 uint32_t dangerousEnemies=0;
 std::map<UnitId,int32_t> expectedDamageAfter;
};
struct OrdinaryGroundTwoPassResult
{
 std::vector<UnitId> order;
 std::array<size_t,2> stoppedAt{};
 size_t passes=0,drawsConsumed=0;
 std::map<UnitId,int32_t> expectedDamage;
 std::vector<OrdinaryGroundAction> actions;
 std::array<int32_t,4> actionScratch{};
};
namespace OrdinaryGroundDetail
{
struct Unavailable{};
inline void require(bool value){if(!value)throw Unavailable{};}
inline int32_t integer(int64_t value){require(value>=INT32_MIN&&value<=INT32_MAX);return static_cast<int32_t>(value);}
inline int32_t cast(double value){require(std::isfinite(value)&&value>=INT32_MIN&&value<=INT32_MAX);return static_cast<int32_t>(value);}
template<class T> const T & take(const std::optional<T> & v){require(bool(v));return *v;}
class Model
{
 const OrdinaryGroundTwoPassInput & in;
 const OrdinaryTwoPassInput & common;
 std::optional<UnitId> liveActor;
 std::map<UnitId,const OrdinaryForecastUnit*> units;
 std::array<std::vector<UnitId>,2> roster;
 std::map<UnitId,int> moveRank;
 std::map<UnitId,Ordinary::SourceTargetCensus> storedRows;
 std::array<int32_t,2> total{},awake{},shooters{};
 OrdinaryGroundTwoPassResult result;
 int side(UnitId id)const{return static_cast<int>(unit(id).state.order.owningSide);}
 const OrdinaryForecastUnit & unit(UnitId id)const{return *units.at(id);}
 const OrdinaryGroundUnit & meta(UnitId id)const{const auto it=in.ground.find(id);require(it!=in.ground.end());return it->second;}
 int32_t hp(UnitId id)const{return integer(unit(id).state.liveHealth-(liveActor?0:result.expectedDamage.at(id)));}
 bool incap(UnitId id)const{return OrdinaryTwoPassDetail::incapacitated(unit(id).state.order);}
 bool cannot(UnitId id)const{return OrdinaryTwoPassDetail::cannotAttack(unit(id).state.order);}
 bool policy(int side)const{return common.difficulty>0||common.human.at(side);}
 bool advanced(int side)const{return common.difficulty>=2||common.human.at(side);}
 bool kills(int side)const{return integer(int64_t(total[side])*2)<total[1-side]&&policy(side);}
 bool rangedAdvantage(int side)const{return integer(int64_t(shooters[side])+in.heroRangedValue[side])>integer(int64_t(shooters[1-side])+in.heroRangedValue[1-side]);}
 const Ordinary::SourceOrdinaryAttack & pair(UnitId a,UnitId b,bool ranged)const{
  const auto & pairs=ranged?common.attacks:in.melee;const auto it=pairs.find({a,b});require(it!=pairs.end());return it->second;
 }
 bool retaliates(UnitId target,UnitId actor)const{
  const auto it=in.canRetaliate.find({target,actor});require(it!=in.canRetaliate.end());return it->second;
 }
 int32_t effect(UnitId a,UnitId b,bool ranged,bool killsOnly,int controllingSide)const{
  if(hp(a)<=0||hp(b)<=0)return 0;
  Ordinary::SourceOrdinaryExchangeInput e;e.attacker=unit(a).facts;e.defender=unit(b).facts;
  e.ranged=ranged;e.simulated=!liveActor;e.killsOnly=killsOnly;e.attackerExpectedDamage=result.expectedDamage.at(a);e.defenderExpectedDamage=result.expectedDamage.at(b);
  e.attack[ranged?1:0]=pair(a,b,ranged);e.canRetaliate=!ranged&&retaliates(b,a);e.retaliationPolicy=policy(controllingSide);
  if(e.canRetaliate&&e.retaliationPolicy)e.retaliation=pair(b,a,false);
  return take(Ordinary::evaluateSourceOrdinaryExchange(e)).value;
 }
 OrdinaryGroundPath path(UnitId id,int limit=127,int base=-1,int destination=-1)const{
  Ordinary::SourceGroundSearchInput g;g.map=in.map;g.actor={side(id),unit(id).state.order.sourceIndex};g.head=unit(id).head;g.speed=unit(id).state.order.effectiveSpeed;
  g.movementRestricted=meta(id).bound;
  return take(ordinaryGroundPath(g,limit,base<0?g.speed:base,destination));
 }
 std::map<UnitId,Ordinary::SourceTargetCensus> census(int own,std::optional<UnitId> excluded,bool meleeOnly,bool killsOnly,int contextSide){
  auto & rows=storedRows;
  for(const auto id:roster[own]){
   rows[id]={};const auto & u=unit(id);const auto & o=u.state.order;
   if(o.immobilized||hp(id)==0||o.sourceSpell70Turns>1||o.sourceSpell62Turns>1||u.state.berserk||excluded==id||(u.facts.canShoot&&meleeOnly))continue;
   std::optional<OrdinaryGroundPath> p;if(!u.facts.canShoot)p=path(id,meleeOnly?o.effectiveSpeed:127);
   std::vector<Ordinary::SourceTargetCandidate> candidates;
   for(const auto enemy:roster[1-own]){
    if(meleeOnly&&moveRank.at(enemy)>=moveRank.at(id))continue;
    if(p&&!p->validMove[unit(enemy).head])continue;
    if(!liveActor&&!hp(enemy))continue;
    const int distance=p?p->cost[unit(enemy).head]:1;if(p&&!o.effectiveSpeed&&distance>0)continue;
    candidates.push_back({unit(enemy).state.order.sourceIndex,effect(id,enemy,u.facts.canShoot,killsOnly,contextSide),distance});
   }
   rows[id]=take(Ordinary::reduceSourceTargetCensus(o.effectiveSpeed,u.facts.canShoot,candidates));
  }
  return rows;
 }
 int32_t raw(UnitId a,UnitId b,bool ranged)const{
  // aiGetAttackDamage keeps its minimum-one result even at zero simulated HP.
  // strike() guards dead attackers separately; attackChange() does not.
  return take(Ordinary::OrdinaryExchangeDetail::damage(unit(a).facts,hp(a),integer(unit(b).state.liveHealth),pair(a,b,ranged),ranged));
 }
 int32_t loss(UnitId id,bool ranged,int32_t damage,bool killsOnly=false)const{return take(Ordinary::OrdinaryExchangeDetail::loss(unit(id).facts,ranged,damage,killsOnly));}
 void strike(UnitId a,UnitId b,bool ranged){if(hp(a)<=0)return;const int32_t sum=integer(int64_t(result.expectedDamage.at(b))+raw(a,b,ranged));result.expectedDamage.at(b)=std::min(integer(unit(b).state.liveHealth),sum);}
 int32_t attackChange(UnitId actor,UnitId enemy,const std::map<UnitId,Ordinary::SourceTargetCensus> & rows,bool killsOnly)const{
  if(unit(enemy).state.order.sourceSpell70Turns||meta(enemy).retaliationCount!=1)return 0;
  // The source deliberately reads currentSide, not the simulated prefix side.
  const int32_t enemyHits=integer(int64_t(hp(enemy))-raw(actor,enemy,false));if(enemyHits<=0)return 0;
  int32_t committed=0,best=0;
  for(const auto f:roster[static_cast<int>(in.currentSide)]){
   const auto it=rows.find(f);require(it!=rows.end());const auto & row=it->second;
   if(!(row.possibleTargets&(uint32_t(1)<<unit(enemy).state.order.sourceIndex)))continue;
   const int32_t damage=std::min(raw(f,enemy,false),enemyHits);const auto & e=unit(enemy).facts;
   int32_t value=killsOnly?cast((integer(int64_t(enemyHits%e.hitPoints)+damage)/e.hitPoints)*e.lossUnitValue[0]):cast(damage*e.lossUnitValue[0]/e.hitPoints);
   value=integer(int64_t(value)-(row.target?row.target->exchangeValue:0));
   if(row.target&&roster[1-side(f)].at(row.target->rosterIndex)==enemy&&value>0)committed=integer(int64_t(committed)+value);else best=std::max(best,value);
  }
  return integer(int64_t(committed)+best);
 }
 void danger(UnitId actor,int own,bool killsOnly,OrdinaryGroundAction & action)const{
  const int32_t floor=-loss(actor,unit(actor).facts.canShoot,hp(actor));
  if(policy(own))for(const auto enemy:roster[1-own]){
   if(cannot(enemy))continue;
   if(unit(enemy).facts.canShoot){action.dangerousEnemies|=uint32_t(1)<<unit(enemy).state.order.sourceIndex;continue;}
   const int speed=unit(enemy).state.order.effectiveSpeed;const auto reach=path(enemy,speed+1,speed+1);
   bool committed=false;for(const auto friendId:roster[own]){if(unit(friendId).state.order.immobilized)continue;const int head=unit(friendId).head;if(reach.validMove[head]&&reach.cost[head]<=speed){committed=true;break;}}
   if(committed){action.dangerousEnemies|=uint32_t(1)<<unit(enemy).state.order.sourceIndex;continue;}
   const int32_t value=integer(-int64_t(effect(enemy,actor,false,killsOnly,own)));if(value>=0)continue;
   for(int hex=0;hex<187;++hex)if(reach.visited[hex])action.danger[hex]=std::max(floor,integer(int64_t(action.danger[hex])+value));
  }
  if(advanced(own)&&meta(actor).enemyAreaLoss){
   std::array<bool,187> checked{};
   for(const auto friendId:roster[own]){if(friendId==actor||unit(friendId).state.order.immobilized)continue;
    for(const int hex:Ordinary::GroundPathDetail::adjacent(unit(friendId).head))if(hex>=0){
     action.danger[hex]=std::max(floor,integer(int64_t(action.danger[hex])-(checked[hex]?1:meta(actor).enemyAreaLoss)));checked[hex]=true;
    }
   }
  }
 }
 int32_t blockingValue(UnitId actor,int hex,int own)const{
  if(!policy(own))return 0;
  int32_t value=0;
  for(int adjacent:Ordinary::GroundPathDetail::adjacent(hex))if(adjacent>=0&&in.map[adjacent].occupant){
   const auto identity=*in.map[adjacent].occupant;const auto neighbor=roster[identity.side].at(identity.index);
   if(identity.side==static_cast<int>(in.currentSide)||neighbor==actor||!unit(neighbor).facts.canShoot||!hp(neighbor))continue;
   const auto & u=unit(neighbor).facts;const int32_t share=std::max(1,cast((u.lossUnitValue[1]-u.lossUnitValue[0])*hp(neighbor)/u.hitPoints));value=integer(int64_t(value)+share);
  }
  return value;
 }
 void move(UnitId actor,int target,OrdinaryGroundAction & action,bool considerWaiting=false,bool withDanger=true)const{
  const int speed=unit(actor).state.order.effectiveSpeed;if(!speed||meta(actor).bound)return;
  const auto route=path(actor,127,speed,target);if(route.steps.empty())return;
  action.order=OrdinaryGroundOrder::MOVE;action.destination=unit(actor).head;
  int remaining=speed,index=static_cast<int>(route.steps.size())-1,bestDanger=withDanger?action.danger[action.destination]:0;
  const int startDanger=bestDanger;int lastHex=action.destination;
  considerWaiting=considerWaiting&&withDanger&&!common.secondPhase&&advanced(side(actor));
  const int limit=(index/remaining)*remaining;bool committed=false;
  for(int step=index+1;index>=0;--index,--step,--remaining){if(remaining<=0)break;const int hex=route.steps[index];lastHex=hex;
   if(!withDanger||!((step<=limit&&committed)||considerWaiting)||action.danger[hex]>=bestDanger){action.destination=hex;committed=true;bestDanger=action.danger[hex];}
  }
  if(considerWaiting&&action.destination!=lastHex&&bestDanger>=startDanger)action.order=OrdinaryGroundOrder::WAIT;
  if(action.destination==unit(actor).head)action.order=considerWaiting?OrdinaryGroundOrder::WAIT:OrdinaryGroundOrder::DEFEND;
 }
 struct DefenseHex {int hex=-1,open=0,time=0;};
 int travelTime(UnitId actor,int hex,const OrdinaryGroundPath & reach)const{
  const int speed=unit(actor).state.order.effectiveSpeed;
  // The original getTravelTime divides by speed without a zero guard.
  require(speed>0);return std::max(1,integer(int64_t(reach.cost[hex])+speed-1)/speed);
 }
 static int screenX(int hex){return (hex%17)*44+((hex/17)%2?22:44)+14;}
 DefenseHex defenseHex(UnitId actor,UnitId client,const OrdinaryGroundPath & reach)const{
  DefenseHex result;
  for(int hex:Ordinary::GroundPathDetail::adjacent(unit(client).head)){
   if(hex<0)continue;
   if(in.map[hex].occupant&&in.map[hex].occupant!=Ordinary::SourceGroundIdentity{side(actor),unit(actor).state.order.sourceIndex})continue;
   ++result.open;if(!reach.visited[hex])continue;
   const int time=travelTime(actor,hex,reach);
   if(result.hex>=0){if(time>result.time)continue;if(time==result.time){
    if(meta(client).facing==1?screenX(hex)<screenX(result.hex):screenX(hex)>screenX(result.hex))continue;
   }}
   result.hex=hex;result.time=time;
  }
  return result;
 }
 bool shooterDefense(UnitId actor,const OrdinaryGroundPath & reach,OrdinaryGroundAction & action)const{
  std::optional<UnitId> best;int bestHex=-1,bestTime=0,lastHex=-1;int32_t bestValue=0;
  for(const auto client:roster[static_cast<int>(in.currentSide)]){
   if(unit(client).state.order.immobilized||!unit(client).facts.canShoot||meta(client).noMeleePenalty)continue;
   const auto offered=defenseHex(actor,client,reach);lastHex=offered.hex;if(lastHex<0)continue;
   int32_t value=unit(client).liveCombatValue;if(offered.open>1)value/=offered.open;
   if(best&&(offered.time>bestTime||(offered.time==bestTime&&bestValue>value)))continue;
   best=client;bestHex=offered.hex;bestTime=offered.time;bestValue=value;
  }
  if(!best)return false;
  // Original checks the LAST examined hex here, not bestHex. Keep that quirk.
  if(lastHex==unit(actor).head){action.order=OrdinaryGroundOrder::DEFEND;return true;}
  move(actor,bestHex,action,false,false);return true;
 }
 bool runAway(UnitId actor,const OrdinaryGroundPath & reach,OrdinaryGroundAction & action)const{
  if(!advanced(side(actor))||incap(actor)||meta(actor).bound)return false;
  int32_t worst=action.danger[unit(actor).head];if(worst>=0)return false;
  int best=-1,distance=0;
  for(int hex=0;hex<187;++hex){if(!reach.visited[hex]||reach.cost[hex]>unit(actor).state.order.effectiveSpeed)continue;
   const int32_t danger=action.danger[hex];if(danger<worst||(danger==worst&&distance<reach.cost[hex]))continue;
   worst=danger;distance=reach.cost[hex];best=hex;
  }
  if(best<0||best==unit(actor).head)return false;
  move(actor,best,action);return action.order==OrdinaryGroundOrder::MOVE;
 }
 OrdinaryGroundAction meleeAction(UnitId actor,int own,bool live=false){
  OrdinaryGroundAction action;action.actor=actor;const auto & a=unit(actor);const bool killsOnly=kills(own);
  auto rows=census(own,actor,true,killsOnly,own);danger(actor,own,killsOnly,action);const auto reach=path(actor);
  std::optional<UnitId> best;int bestHex=-1,bestTime=0;int32_t bestValue=0,bestHexValue=0;bool bestBlocking=false;
  for(const auto enemy:roster[1-own]){
   const auto & e=unit(enemy);if(e.state.order.immobilized||!hp(enemy))continue;
   if((!a.state.order.effectiveSpeed||meta(actor).bound)&&reach.cost[e.head]>0)continue;
   int32_t change=0;if(reach.validMove[e.head]&&advanced(own)&&!cannot(actor)&&reach.cost[e.head]<=a.state.order.effectiveSpeed)
    change=attackChange(actor,enemy,rows,killsOnly);
   int chosen=-1,turns=0;int32_t hexValue=0;
   for(const int hex:Ordinary::GroundPathDetail::adjacent(e.head)){
    if(hex<0||!reach.visited[hex])continue;
    const int speed=a.state.order.effectiveSpeed;const int time=speed?std::max(1,integer(int64_t(reach.cost[hex])+speed-1)/speed):(reach.cost[hex]>0?100:1);
    if(chosen>=0&&turns<time)continue;
    const int32_t value=integer(int64_t(blockingValue(actor,hex,own))+action.danger[hex]);
    if(chosen>=0&&turns==time&&(value<hexValue||(value==hexValue&&reach.cost[hex]>=reach.cost[chosen])))continue;
    chosen=hex;turns=time;hexValue=value;
   }
   if(chosen<0)continue;
   if(!cannot(actor))change=integer(int64_t(change)+effect(actor,enemy,false,killsOnly,own));
   bool blocking=false;
   if(hexValue<=0||meta(actor).summoned||!policy(own))change=integer(int64_t(change)+hexValue);
   else if(change<0&&rangedAdvantage(own)){change=hexValue;blocking=true;}
   else change=integer(int64_t(change)+hexValue);
   if(change<0&&policy(own)&&hexValue<action.danger[chosen])continue;
   require(result.drawsConsumed<in.randomFactors.size());const int factor=in.randomFactors[result.drawsConsumed++];require(factor>=75&&factor<=100);
   const int32_t score=integer(int64_t(factor)*change)/100;
   if(best){
    if(incap(enemy)&&!incap(*best))continue;
    if(!(incap(*best)&&!incap(enemy))){
     if(bestTime<turns)continue;
     if(bestTime==turns){if(bestValue>score)continue;if(bestValue==score){if(unit(*best).facts.topWound>e.facts.topWound)continue;if(unit(*best).facts.topWound==e.facts.topWound&&reach.cost[bestHex]<reach.cost[chosen])continue;}}
    }
   }
   best=enemy;bestBlocking=blocking;bestHex=chosen;bestTime=turns;bestHexValue=integer(int64_t(hexValue)*factor)/integer(int64_t(turns)*100);bestValue=score/turns;
  }
  action.value=std::max(bestValue,bestHexValue);
  if(bestBlocking){action.value=bestHexValue;if(bestHex!=a.head){move(actor,bestHex,action,live&&bestTime>1);return action;} }
  else if(best){
   if(live&&bestValue<0&&bestValue<bestHexValue&&!meta(actor).summoned&&policy(own)&&rangedAdvantage(own)
    &&!meta(actor).enemyAreaLoss&&shooterDefense(actor,reach,action)){action.value=0;return action;}
   if(bestValue<0&&bestValue<bestHexValue&&!meta(actor).summoned&&policy(own)&&rangedAdvantage(own))action.value=0;
   action.value=bestValue;
   if(bestTime<=1){action.order=OrdinaryGroundOrder::MELEE;action.target=best;action.attackHex=bestHex;action.destination=unit(*best).head;return action;}
   if(bestHex!=a.head){move(actor,bestHex,action,live&&bestTime>1);return action;}
  }
  if(live&&!best&&!meta(actor).enemyAreaLoss&&shooterDefense(actor,reach,action)){action.value=0;return action;}
  if(live&&!best&&runAway(actor,reach,action)){action.value=0;return action;}
  action.value=0;action.order=!common.secondPhase&&advanced(own)?OrdinaryGroundOrder::WAIT:OrdinaryGroundOrder::DEFEND;return action;
 }
 OrdinaryGroundAction shooterAction(UnitId actor,int own){
  OrdinaryGroundAction action;action.actor=actor;const bool initialKills=kills(own);const auto rows=census(1-own,std::nullopt,false,initialKills,own);
  int32_t bestValue=0;
  const auto choose=[&](bool killsOnly){action.target.reset();for(const auto enemy:roster[1-own]){
   if(unit(enemy).state.order.immobilized||!hp(enemy))continue;
   int32_t value=effect(actor,enemy,true,killsOnly,own);
   if(policy(own)){const auto & row=rows.at(enemy);const int divisor=incap(enemy)?10:(!cannot(enemy)&&row.target&&row.target->attackTime<=5?row.target->attackTime:5);require(divisor>0);value/=divisor;}
   if(action.target&&((incap(enemy)&&!incap(*action.target))||((incap(enemy)||!incap(*action.target))&&value<bestValue)))continue;
   action.target=enemy;bestValue=value;
  }};
  choose(initialKills);if(action.target&&initialKills&&!bestValue)choose(false);
  if(action.target){action.order=OrdinaryGroundOrder::SHOOT;action.destination=unit(*action.target).head;action.value=bestValue;}
  return action;
 }
public:
 explicit Model(const OrdinaryGroundTwoPassInput & input,std::optional<UnitId> active=std::nullopt):in(input),common(input.common),liveActor(active){
  require(in.ordinaryDomainKnown);
  require(OrdinaryTwoPassDetail::validSide(common.actingSide)&&OrdinaryTwoPassDetail::validSide(common.requestedSide)&&OrdinaryTwoPassDetail::validSide(in.currentSide));
  require(common.difficulty>=0&&common.difficulty<=4);for(auto v:in.heroRangedValue)require(v>=0);
  MoveOrderInput order;order.actingSide=common.actingSide;order.secondPhase=common.secondPhase;
  for(const auto & u:common.units){const auto & o=u.state.order;require(OrdinaryTwoPassDetail::validSide(o.owningSide)&&o.owningSide==u.state.controllingSide);
   const auto live=take(Ordinary::OrdinaryExchangeDetail::health(u.facts));require(live==u.state.liveHealth&&u.initialExpectedDamage>=0&&(liveActor||u.initialExpectedDamage<=live)&&u.liveCombatValue>=0);
   require(o.role==OrderRole::ORDINARY&&!u.state.catapult&&o.effectiveSpeed>=0&&o.effectiveSpeed<=125&&o.sourceSpell62Turns>=0&&o.sourceSpell70Turns>=0&&o.sourceSpell74Turns>=0);
   require(Ordinary::GroundPathDetail::available(u.head)&&units.emplace(o.id,&u).second);roster[static_cast<int>(o.owningSide)].push_back(o.id);result.expectedDamage[o.id]=u.initialExpectedDamage;storedRows[o.id]={};order.units.push_back(o);
   require(u.liveCombatValue==take(Ordinary::OrdinaryExchangeDetail::integer(double(live)*u.facts.lossUnitValue[u.facts.canShoot?1:0]/u.facts.hitPoints)));
   require(meta(o.id).retaliationCount>=0&&meta(o.id).enemyAreaLoss>=0);
   require(meta(o.id).facing==0||meta(o.id).facing==1);
   if(!o.immobilized){auto s=side(o.id);total[s]=integer(int64_t(total[s])+u.liveCombatValue);if(!cannot(o.id))awake[s]=integer(int64_t(awake[s])+u.liveCombatValue);if(!incap(o.id)&&u.facts.canShoot)shooters[s]=integer(int64_t(shooters[s])+u.liveCombatValue);}
  }
  for(int s=0;s<2;++s){auto & r=roster[s];std::sort(r.begin(),r.end(),[&](auto a,auto b){return unit(a).state.order.sourceIndex<unit(b).state.order.sourceIndex;});require(r.size()<=21);for(size_t i=0;i<r.size();++i)require(unit(r[i]).state.order.sourceIndex==static_cast<int>(i));(void)kills(s);}
  (void)integer(int64_t(std::min(awake[0],awake[1]))*5);
  for(int h=0;h<187;++h)if(in.map[h].occupant){const auto id=*in.map[h].occupant;require(Ordinary::GroundPathDetail::available(h)&&Ordinary::GroundPathDetail::identity(id)&&!in.map[h].blocked&&id.index<static_cast<int>(roster[id.side].size()));const auto u=roster[id.side][id.index];require(unit(u).head==h&&unit(u).state.liveHealth>0);}
  for(const auto & [id,u]:units)if(u->state.liveHealth>0)require(in.map[u->head].occupant==Ordinary::SourceGroundIdentity{side(id),u->state.order.sourceIndex});
  if(liveActor)require(units.contains(*liveActor));
  ProjectedMoveOrder projected;
  // Live shooter census never filters by move rank, so it needs no tie-order proof.
  if(!liveActor||!unit(*liveActor).facts.canShoot){try{projected=take(projectInvariantMoveOrder(order));}catch(const std::invalid_argument &){throw Unavailable{};}}
  for(size_t i=0;i<projected.units.size();++i){const auto id=projected.units[i].id;result.order.push_back(id);moveRank[id]=static_cast<int>(projected.units.size()-i);if(!liveActor)result.expectedDamage[id]=0;}
  for(const auto & [id,u]:units)if(!moveRank.contains(id)){
   if(liveActor&&!unit(*liveActor).facts.canShoot&&u->state.order.immobilized){require(meta(id).priorMoveRank.has_value());moveRank[id]=*meta(id).priorMoveRank;}
   else moveRank[id]=0;
  }
  result.actionScratch=common.actionScratch;
 }
 OrdinaryGroundAction liveMeleeChoice(UnitId active){
  require(units.contains(active));const auto & actor=unit(active);
  require(!actor.facts.canShoot&&!cannot(active)&&!actor.state.berserk&&hp(active)>0);
  require(side(active)==static_cast<int>(in.currentSide));
  // This initial live leaf proves attemptShooterDefense has no eligible client.
  for(const auto id:roster[side(active)])require(!unit(id).facts.canShoot);
  for(const auto &[id,u]:units){require(u->state.liveHealth>0&&u->initialExpectedDamage==0);(void)id;}
  auto choice=meleeAction(active,side(active),true);choice.expectedDamageAfter=result.expectedDamage;return choice;
 }
 OrdinaryGroundAction liveMixedChoice(UnitId active){
  require(liveActor==active&&units.contains(active));const auto & actor=unit(active);
  require(!cannot(active)&&!actor.state.berserk&&hp(active)>0&&side(active)==static_cast<int>(in.currentSide));
  // A processed source corpse remains in the original roster but is immobilized
  // and has no physical map occupancy. Do not erase its original index.
  for(const auto &[id,u]:units){(void)id;require(u->state.liveHealth>0||u->state.order.immobilized);}
  auto choice=actor.facts.canShoot?shooterAction(active,side(active)):meleeAction(active,side(active),true);
  choice.expectedDamageAfter=result.expectedDamage;return choice;
 }
 size_t drawsConsumed()const{return result.drawsConsumed;}
 OrdinaryGroundTwoPassResult run(){
  size_t cursor=0;for(size_t pass=0;pass<(common.checkingSurrender?2u:1u);++pass){const int own=pass?1-static_cast<int>(common.requestedSide):static_cast<int>(common.requestedSide);++result.passes;
   while(cursor<result.order.size()){
    const auto actor=result.order[cursor];const auto & a=unit(actor);
    if(cannot(actor)||a.state.berserk||!hp(actor)){++cursor;continue;}if(side(actor)!=own)break;
    auto action=a.facts.canShoot?shooterAction(actor,own):meleeAction(actor,own);
    if(action.order==OrdinaryGroundOrder::SHOOT)strike(actor,*action.target,true);
    else if(action.order==OrdinaryGroundOrder::MELEE){const auto enemy=*action.target;strike(actor,enemy,false);if(retaliates(enemy,actor)&&policy(own))strike(enemy,actor,false);if(a.facts.doubleAttack&&hp(enemy)>0)strike(actor,enemy,false);}
    action.expectedDamageAfter=result.expectedDamage;result.actions.push_back(std::move(action));++cursor;
   }result.stoppedAt[pass]=cursor;
  }return result;
 }
};
}
inline std::optional<OrdinaryGroundTwoPassResult> simulateOrdinaryGroundTwoPass(const OrdinaryGroundTwoPassInput & in)
{
 try{return OrdinaryGroundDetail::Model(in).run();}catch(const OrdinaryGroundDetail::Unavailable &){return std::nullopt;}
}
}
