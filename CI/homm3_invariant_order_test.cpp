#include "../AI/Integration/Ports/HoMM3Forecast/InvariantMoveOrder.h"
#include "../test/aiIntegration/homm3InvariantOrder/PinnedInvariantOrder.h"
#include <iostream>
#include <map>
#include <random>
using namespace HoMM3Forecast;
namespace R=PinnedInvariantOrder;
static size_t checks=0,cases=0,orientations=0;
static void check(bool b,const char*m){++checks;if(!b)throw std::runtime_error(m);}
static void load(const MoveOrderInput & in)
{
 R::manager={};R::manager.m_actingSide=int(in.actingSide);R::manager.secondPhase=in.secondPhase;
 for(const auto &u:in.units){const int s=int(u.owningSide);auto&a=R::manager.m_armies[s][u.sourceIndex];
  a.id=u.id;a.group=s;a.index=u.sourceIndex;a.speed=u.effectiveSpeed;
  a.flags=(u.done?R::creatureDone:0)|(u.waiting?R::creatureWaiting:0)|(u.immobilized?R::creatureImmobilized:0);
  a.spells[62]=u.sourceSpell62Turns;a.spells[70]=u.sourceSpell70Turns;a.spells[74]=u.sourceSpell74Turns;
  a.m_creatureType=u.role==OrderRole::ARROW_TOWER?R::CREATURE_ARROW_TOWER:u.role==OrderRole::FIRST_AID_TENT?R::CREATURE_FIRST_AID_TENT:u.role==OrderRole::AMMUNITION_CART?R::CREATURE_AMMO_CART:0;
  R::manager.m_numArmies[s]=std::max(R::manager.m_numArmies[s],u.sourceIndex+1);
 }
}
static std::vector<OrderedUnit> reference(const MoveOrderInput & in,const std::vector<UnitId>&permutation)
{
 load(in);R::permutation=permutation;std::vector<R::army*> order;R::manager.findMoveOrder(&order);
 std::vector<OrderedUnit> out;for(const auto *u:order)out.push_back({u->id,u->m_expectedMoveOrder});return out;
}
static void exhaustive(const MoveOrderInput & in)
{
 load(in);std::vector<R::army*> sorted;
 for(int s=0;s<2;++s)for(int i=0;i<R::manager.m_numArmies[s];++i){auto&a=R::manager.m_armies[s][i];if(a.is(R::creatureImmobilized)||a.m_creatureType==R::CREATURE_ARROW_TOWER)continue;a.m_expectedMoveOrder=R::getMoveOrder(&a);sorted.push_back(&a);}
 std::sort(sorted.begin(),sorted.end(),R::func_moves_before());
 std::vector<UnitId> ids;std::vector<size_t> ties;
 for(size_t i=0;i<sorted.size();++i){ids.push_back(sorted[i]->id);if(i&&sorted[i]->index==sorted[i-1]->index&&sorted[i]->m_expectedMoveOrder==sorted[i-1]->m_expectedMoveOrder)ties.push_back(i-1);}
 check(ties.size()<=7,"test enumeration bound");
 const auto expected=reference(in,ids);bool invariant=true;
 for(size_t mask=0;mask<(size_t{1}<<ties.size());++mask){auto order=ids;for(size_t b=0;b<ties.size();++b)if(mask&(size_t{1}<<b))std::swap(order[ties[b]],order[ties[b]+1]);invariant&=reference(in,order)==expected;++orientations;}
 const auto actual=projectInvariantMoveOrder(in,128);check(bool(actual)==invariant,"invariance availability mismatch");
 if(actual){check(actual->units==expected,"rank mismatch");check(actual->containsComparatorTies==!ties.empty(),"tie provenance lost");}
 ++cases;
}
static MoveOrderInput mirrored(int n)
{
 MoveOrderInput in;for(int s=0;s<2;++s)for(int i=0;i<n;++i){OrderUnit u;u.id=s*100+i;u.owningSide=static_cast<Side>(s);u.sourceIndex=i;u.effectiveSpeed=5;in.units.push_back(u);}return in;
}
int main(){try{
 for(int n=1;n<=7;++n)for(int side=0;side<2;++side)for(bool phase:{false,true}){auto in=mirrored(n);in.actingSide=static_cast<Side>(side);in.secondPhase=phase;exhaustive(in);check(bool(projectInvariantMoveOrder(in,0)),"paired block needs no enumeration budget");}
 // Every subset of the two native seven-slot rosters within one equal-key
 // block, including all mixed singleton/pair layouts, both entering sides/phases.
 for(int mask=0;mask<(1<<14);++mask)for(int side=0;side<2;++side)for(bool phase:{false,true}){
  auto in=mirrored(7);in.actingSide=static_cast<Side>(side);in.secondPhase=phase;
  for(int i=0;i<14;++i)in.units[i].immobilized=(mask&(1<<i))==0;
  exhaustive(in);
 }
 std::mt19937 rng(0x32abc);
 for(int k=0;k<6000;++k){MoveOrderInput in;in.actingSide=static_cast<Side>(rng()%2);in.secondPhase=rng()%2;
  for(int s=0;s<2;++s){int n=rng()%8;for(int i=0;i<n;++i){OrderUnit u;u.id=s*100+i;u.owningSide=static_cast<Side>(s);u.sourceIndex=i;u.effectiveSpeed=rng()%8;u.done=rng()%9==0;u.waiting=rng()%4==0;u.immobilized=rng()%11==0;u.sourceSpell62Turns=rng()%13==0?2:0;u.sourceSpell70Turns=rng()%17==0?1:0;u.sourceSpell74Turns=rng()%19==0?1:0;if(rng()%31==0)u.role=OrderRole::FIRST_AID_TENT;in.units.push_back(u);}}
  exhaustive(in);
 }
 // Counterexample: swapping A2/B2 before original side alternation changes A1/A2 ranks.
 auto mixed=mirrored(3);mixed.units[3].immobilized=mixed.units[4].immobilized=true;exhaustive(mixed);check(!projectInvariantMoveOrder(mixed),"mixed non-invariant tie must reject");
 auto budget=mirrored(2);budget.units[3].immobilized=true;exhaustive(budget);check(!projectInvariantMoveOrder(budget,0),"unproven mixed block beyond budget must reject");
 // Structural induction covers full paired physical rosters without 2^21 enumeration.
 for(int side=0;side<2;++side){auto full=mirrored(21);full.actingSide=static_cast<Side>(side);const auto actual=projectInvariantMoveOrder(full,0);check(bool(actual),"paired 21-slot roster");
  for(int trial=0;trial<1000;++trial){std::vector<UnitId> ids;for(int i=0;i<21;++i){bool swap=rng()%2;ids.push_back((swap?100:0)+i);ids.push_back((swap?0:100)+i);}check(reference(full,ids)==actual->units,"paired induction sampled oracle");++orientations;}
 }
 std::cout<<cases<<" exhaustive source cases, "<<orientations<<" original tie orientations, "<<checks<<" checks\n";
 }catch(const std::exception&e){std::cerr<<"case "<<cases<<": "<<e.what()<<'\n';return 1;}}
