#include "../AI/Integration/Ports/HoMM3GroundCensus.h"
#include "../AI/Integration/Ports/HoMM3RangedCensus.h"
#include "../test/aiIntegration/homm3Ground/GroundPinnedExchange.h"
#include <iostream>
#include <stdexcept>
using namespace AIIntegration::Ports::HoMM3;
namespace Ref = PinnedGroundExchange;
namespace Path = PinnedGround;
static int checks=0;
static void require(bool value,const char * why) { ++checks; if(!value) throw std::runtime_error(why); }
static SourceOrdinaryUnit facts(const Ref::army & a)
{
 SourceOrdinaryUnit u;u.count=a.m_numTroops;u.hitPoints=a.m_monInfo.hitPoints;u.topWound=a.m_topCreatureDamage;
 u.minimumDamage=a.m_monInfo.damageLowBound;u.maximumDamage=a.m_monInfo.damageHighBound;
 u.doubleAttack=a.is(Ref::creatureTwoAttacks);u.canShoot=a.canShoot(0);
 for(int c=0;c<2;++c)u.lossUnitValue[c]=a.getUnitCombatValue(0,0,c!=0,nullptr);
 return u;
}
static SourceOrdinaryAttack attack(const Ref::army & a,const Ref::army & b)
{
 SourceOrdinaryAttack p;p.attack=a.getAdjustedAttack(&b,false);p.defense=b.getAdjustedDefense(&a,false);
 p.defenderReduction=b.computeDefenderDamageReduction(false);
 p.meleeShooterPenalty=a.is(Ref::creatureShootingArmy)&&!a.is(Ref::creatureNoMeleePenalty);
 return p;
}
static void loadMap(const SourceGroundSearchInput & g)
{
 Path::manager={};Path::manager.setupAdjacencyArray();
 for(int h=0;h<187;++h)
 {
  auto & c=Path::manager.m_cells[h];c.m_attributes=g.map[h].blocked?Path::hexcell::blocked:0;
  if(const auto id=g.map[h].occupant)
  {
   c.m_armySide=id->side;c.m_armySlot=id->index;
   auto & a=Path::manager.m_armies[id->side][id->index];a.group=id->side;a.index=id->index;a.m_gridIndex=h;
  }
 }
}
int main()
{
 for(int side:{0,1})for(int friends=2;friends<=7;++friends)for(int enemies=2;enemies<=7;++enemies)
 for(int speed:{0,1,4,7})for(int layout=0;layout<5;++layout)for(bool retaliates:{false,true})for(bool policy:{false,true})for(bool kills:{false,true})
 {
  Ref::manager={};Ref::game.m_setup.m_difficulty=policy?1:0;Ref::manager.IsHuman={false,false};
  Ref::manager.m_numArmies[side]=friends;Ref::manager.m_numArmies[1-side]=enemies;
  SourceGroundSearchInput g;g.actor={side,0};g.head=18;g.speed=speed;
  for(int i=0;i<friends;++i)
  {
   auto & a=Ref::manager.m_armies[side][i];a={};a.group=side;a.index=i;a.m_gridIndex=18+i*17;a.speed=speed;
   a.m_numTroops=17+i;a.m_monInfo.hitPoints=11;a.m_topCreatureDamage=5;
   a.m_monInfo.damageLowBound=2;a.m_monInfo.damageHighBound=7;a.m_monInfo.attackSkill=11;
   a.flags=i%2?0:Ref::creatureTwoAttacks;g.map[a.m_gridIndex].occupant=SourceGroundIdentity{side,i};
  }
  for(int j=0;j<enemies;++j)
  {
   auto & e=Ref::manager.m_armies[1-side][j];e={};e.group=1-side;e.index=j;e.m_gridIndex=(layout==1?19:30)+j*17;
   e.m_numTroops=2+j;e.m_monInfo.hitPoints=13;e.m_topCreatureDamage=j;e.m_monInfo.attackSkill=j*3;
   e.m_monInfo.defenseSkill=j*5;e.m_monInfo.damageLowBound=2;e.m_monInfo.damageHighBound=5;
   e.retaliates=retaliates;e.flags=j%2?Ref::creatureShootingArmy:0;
   if(layout==3&&j%2==0){e.m_numTroops=0;e.m_topCreatureDamage=0;}
   else g.map[e.m_gridIndex].occupant=SourceGroundIdentity{1-side,j};
  }
  if(layout==2)for(int r=0;r<10;++r)g.map[r*17+8].blocked=true;
  if(layout==4)for(int h:{1,19,36})g.map[h].blocked=true;
  loadMap(g);
  Ref::type_AI_combat_parameters params;params.m_ourGroup=side;params.m_killsOnly=kills;
  Ref::manager.findAITargets(side,nullptr,false,&params,nullptr);
  for(int i=0;i<friends;++i)
  {
   auto & a=Ref::manager.m_armies[side][i];SourceGroundCensusInput in;in.actor=facts(a);in.geometry=g;
   in.geometry->actor.index=i;in.geometry->head=a.m_gridIndex;in.retaliationPolicy=policy;in.killsOnly=kills;
   in.enemies=std::vector<SourceGroundCandidate>{};
   for(int j=0;j<enemies;++j)
   {
    const auto & e=Ref::manager.m_armies[1-side][j];SourceGroundCandidate p;
    p.rosterIndex=j;p.head=e.m_gridIndex;p.unit=facts(e);p.attack=attack(a,e);p.retaliation=attack(e,a);p.canRetaliate=retaliates;
    in.enemies->push_back(p);
   }
   const auto row=produceSourceGroundCensus(in);if(!row) std::cerr<<side<<" "<<friends<<" "<<enemies<<" "<<speed<<" "<<layout<<" "<<i<<" "<<retaliates<<" "<<policy<<" "<<kills<<"\n";
   require(bool(row),"Complete ordinary ground row available");
   require(row->census.possibleTargets==a.AI_possible_targets,"Full source target mask");
   require(bool(row->census.target)==bool(a.AI_target),"Full source target presence");
   if(a.AI_target)
   {
    require(row->census.target->rosterIndex==a.AI_target->index,"Full source target identity");
    require(row->census.target->exchangeValue==a.AI_target_value,"Full source target value");
    require(row->census.target->attackDistance==a.AI_target_distance,"Full source target cost");
    require(row->census.target->attackTime==a.getAITargetTime(),"Full source target time");
   }
   Ref::searchArray pathReference;
   pathReference.seedCombatPosition(&a,side,127,false,-1);
   std::vector<std::pair<int,int>> expectedCandidates;
   for(int j=0;j<enemies;++j)
   {
    const int h=Ref::manager.m_armies[1-side][j].m_gridIndex;
    const int cost=pathReference.getHex(h)->m_cost;
    if(Ref::manager.m_cells[h].m_validMove && (speed!=0 || cost==0)) expectedCandidates.emplace_back(j,cost);
   }
   require(expectedCandidates.size()==row->candidates.size(),"Entire independent eligible candidate stream retained");
   for(size_t n=0;n<row->candidates.size();++n)
   {
    const auto & c=row->candidates[n];const auto & e=Ref::manager.m_armies[1-side][c.rosterIndex];
    require(c.rosterIndex==expectedCandidates[n].first && *c.attackDistance==expectedCandidates[n].second,"Every source candidate identity and raw path cost");
    const int distance=expectedCandidates[n].second>speed?0:expectedCandidates[n].second;
    require(row->damageDistances[n]==distance,"Source damage-distance contract");
    require(*c.exchangeValue==params.getSimpleAttackEffect(a,e,false,distance),"Every candidate full pinned exchange");
   }
  }
 }
 SourceGroundCensusInput base;base.actor.count=10;base.geometry=SourceGroundSearchInput{};
 base.geometry->head=18;base.geometry->actor={0,0};base.geometry->speed=1;base.geometry->map[18].occupant=SourceGroundIdentity{0,0};
 base.geometry->map[19].occupant=SourceGroundIdentity{1,0};base.retaliationPolicy=true;
 SourceGroundCandidate p;p.head=19;p.unit=SourceOrdinaryUnit{};p.attack=SourceOrdinaryAttack{};p.retaliation=SourceOrdinaryAttack{};p.canRetaliate=false;
 base.enemies=std::vector<SourceGroundCandidate>{p};
 require(bool(produceSourceGroundCensus(base)),"Baseline adjacency");
 for(int mode=0;mode<7;++mode)
 {
  auto in=base;
  if(mode==0)in.enemies.reset();
  if(mode==1)in.geometry.reset();
  if(mode==2)in.retaliationPolicy.reset();
  if(mode==3)in.enemies->at(0).attack.reset();
  if(mode==4)in.enemies->at(0).canRetaliate.reset();
  if(mode==5)in.meleeOnly=true;
  if(mode==6)in.simulated=true;
  require(!produceSourceGroundCensus(in),"Unavailable domains stay unknown");
 }
 auto empty=base;empty.geometry->map[19].occupant.reset();empty.enemies->at(0).unit->count=0;empty.enemies->at(0).head=30;
 require(produceSourceGroundCensus(empty)&&!produceSourceGroundCensus(empty)->census.target,"Far corpse known empty");
 empty.enemies->at(0).attack.reset();require(bool(produceSourceGroundCensus(empty)),"Unreachable peer needs no exchange");
 empty.enemies->at(0).head=19;require(!produceSourceGroundCensus(empty),"Reachable corpse needs known zero exchange");
 empty.enemies->at(0).attack=SourceOrdinaryAttack{};
 auto zero=produceSourceGroundCensus(empty);require(zero&&zero->census.target&&zero->census.target->exchangeValue==0,"Reachable corpse first zero target");
 auto invalid=base;invalid.enemies->at(0).rosterIndex=1;require(!produceSourceGroundCensus(invalid),"Missing original identity unknown");
 auto filtered=base;filtered.immobilized=true;filtered.enemies.reset();require(bool(produceSourceGroundCensus(filtered)),"Excluded actor known empty");
 require(sourceTargetCommitted(0,{std::nullopt,zero->census})==true,"Ground existential witness dominates unknown peer");
 require(!sourceTargetCommitted(0,{std::nullopt,SourceTargetCensus{}}),"No witness plus unknown stays unknown");
 for(int gate=0;gate<8;++gate)
 {
  auto in=base;
  if(gate==0)in.stoneGazeTurns=1;
  if(gate==1)in.blindTurns=1;
  if(gate==2)in.stoneGazeTurns=2;
  if(gate==3)in.blindTurns=2;
  if(gate==4)in.hypnotizeTurns=1;
  if(gate==5)in.berserkTurns=1;
  if(gate==6)in.currentArmyExcluded=true;
  if(gate==7)in.siegeWeapon=true;
  auto out=produceSourceGroundCensus(in);
  require(out && bool(out->census.target)==(gate<2),"Exact source exclusion thresholds");
 }
 {auto in=base;in.siegeWeapon=in.ballista=true;require(!produceSourceGroundCensus(in),"Ballista is unavailable, not source-filtered empty");}
 {
  auto in=base;in.enemies->at(0).unit->count=0;
  auto live=p;live.rosterIndex=1;in.enemies->push_back(live);in.geometry->map[19].occupant=SourceGroundIdentity{1,1};
  auto out=produceSourceGroundCensus(in);
  require(out&&out->candidates.size()==2&&out->candidates[0].rosterIndex==0&&*out->candidates[0].exchangeValue==0
   &&out->candidates[0].attackDistance==out->candidates[1].attackDistance,"Corpse and live enemy same head retain both identities");
  in.enemies->pop_back();in.geometry->map[19].occupant=SourceGroundIdentity{0,1};
  out=produceSourceGroundCensus(in);require(out&&out->candidates.empty(),"Friendly occupied corpse head has no enemy publication");
  in.enemies->at(0).head=18;out=produceSourceGroundCensus(in);
  require(out&&out->candidates.size()==1&&*out->candidates[0].attackDistance==0,"Corpse on actor head is valid zero-distance landing");
 }
 {
  auto in=base;in.geometry->movementRestricted=true;
  auto out=produceSourceGroundCensus(in);require(out&&out->candidates.size()==1,"Restricted actor keeps adjacent sourcecost0 enemy");
  in.geometry->map[19].occupant.reset();in.geometry->map[30].occupant=SourceGroundIdentity{1,0};in.enemies->at(0).head=30;
  out=produceSourceGroundCensus(in);require(out&&out->candidates.empty(),"Restricted actor does not publish distant enemy");
 }
 std::cout<<"ground census checks "<<checks<<'\n';
}
