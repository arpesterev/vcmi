#include "../../../AI/Integration/Ports/HoMM3DefendProvenance.h"
#include <stdexcept>
#define CHECK(condition) do { if(!(condition)) throw std::runtime_error("check failed: " #condition); } while(false)
#include <iostream>
using namespace AIIntegration::Ports::HoMM3;
using P = DefendProvenance;
using F = DefendUnitFacts;
const DefendOrdinaryDomain safe{true,true,true,true,true,true,true};
F facts(uint32_t id, int d, bool defended = false)
{
 return {id, 100+id, true,true,d,d+(defended ? std::max(d/5,1) : 0),{6,d,12},
  defended ? (d<5 ? F::Stance::CanonicalAddOneZeroExtra : F::Stance::CanonicalPercent20ZeroExtra) : F::Stance::Absent};
}
void establish(P & p, int d = 9)
{
 p.attach(7,1);
 CHECK(p.start(7,1,1,P::Action::Defend,facts(1,d),safe));
 CHECK(!p.query(7,1,facts(1,d,true),safe));
 CHECK(p.finish(7,1,1,{facts(1,d,true),facts(2,12)},safe));
 CHECK(p.query(7,1,facts(1,d,true),safe));
}
int main()
{
 unsigned checks = 0;
 for(int d : {0,1,4,5,9,10,12,107374182})
 {
  P p; establish(p,d); auto b=p.query(7,1,facts(1,d,true),safe);
  CHECK(b && b->originalDefense==d && b->fixedBonus==std::max(d/5,1)); ++checks;
 }
 for(auto action : {P::Action::Wait,P::Action::NoAction,P::Action::Walk,P::Action::Shoot,P::Action::Melee})
 {
  P p; establish(p); // next actor activation is a query, not an invalidation
  CHECK(p.query(7,1,facts(1,9,true),safe));
  CHECK(p.start(7,1,2,action,facts(2,12),safe));
  CHECK(!p.query(7,1,facts(1,9,true),safe));
  CHECK(p.finish(7,1,2,{facts(1,9,true),facts(2,12)},safe));
  CHECK(p.query(7,1,facts(1,9,true),safe)); ++checks;
 }
 { P p; establish(p); CHECK(p.start(7,1,2,P::Action::Defend,facts(2,12),safe));
   CHECK(p.finish(7,1,2,{facts(1,9,true),facts(2,12,true)},safe));
   CHECK(p.query(7,1,facts(1,9,true),safe)); CHECK(p.query(7,1,facts(2,12,true),safe));
   CHECK(!p.query(7,1,facts(1,9),safe)); CHECK(p.query(7,1,facts(2,12,true),safe)); ++checks; }
 { P p; p.attach(7,1); CHECK(!p.query(7,1,facts(1,9,true),safe)); ++checks; }
 { P p; establish(p); p.nextRound(2); CHECK(!p.query(7,2,facts(1,9,true),safe)); ++checks; }
 { P p; establish(p); p.detach(); CHECK(!p.query(7,1,facts(1,9,true),safe)); ++checks; }
 for(int kind=0;kind<7;++kind)
 {
  P p; establish(p); auto f=facts(1,9,true);
  if(kind==0) ++f.identity;
  if(kind==1) f.defenseProvenance.push_back(1);
  if(kind==2) ++f.underlyingDefense;
  if(kind==3) ++f.displayedDefense;
  if(kind==4) f.alive=false;
  if(kind==5) f.canonical=false;
  if(kind==6) f.stance=F::Stance::Unknown;
  CHECK(!p.query(7,1,f,safe)); CHECK(!p.query(7,1,facts(1,9,true),safe)); ++checks;
 }
 { P p; establish(p); CHECK(p.start(7,1,2,P::Action::Walk,facts(2,12),safe));
   p.invalidate(); CHECK(!p.finish(7,1,2,{facts(1,9,true)},safe)); ++checks; }
 { P p; establish(p); CHECK(!p.start(7,1,3,P::Action::Walk,facts(2,12),safe)); CHECK(!p.query(7,1,facts(1,9,true),safe)); ++checks; }
 { P p; establish(p); CHECK(!p.start(7,1,2,P::Action::Unsupported,facts(2,12),safe)); ++checks; }
 { P p; establish(p); CHECK(!p.query(8,1,facts(1,9,true),safe)); ++checks; }
 { P p; establish(p); CHECK(!p.query(7,1,facts(1,9,true),{})); ++checks; }
 { P p; establish(p); CHECK(p.start(7,1,2,P::Action::Walk,facts(2,12),safe));
   CHECK(!p.finish(7,1,2,{facts(1,9,true),facts(1,9,true)},safe)); ++checks; }
 { P p; p.attach(7,1); CHECK(!p.start(7,1,1,P::Action::Defend,facts(1,107374183),safe)); ++checks; }
 // Direct regressions requested by independent review; original31 retained above.
 { P p; establish(p); CHECK(p.start(7,1,2,P::Action::Walk,facts(2,12),safe));
   CHECK(!p.start(7,1,3,P::Action::Wait,facts(2,12),safe));
   CHECK(!p.finish(7,1,2,{facts(1,9,true),facts(2,12)},safe));
   CHECK(!p.query(7,1,facts(1,9,true),safe)); ++checks; }
 { P p; establish(p); CHECK(!p.finish(7,1,1,{facts(1,9,true)},safe));
   CHECK(!p.query(7,1,facts(1,9,true),safe)); ++checks; }
 for(int kind=0;kind<3;++kind)
 {
  P p; establish(p); CHECK(p.start(7,1,2,P::Action::Walk,facts(2,12),safe));
  CHECK(!p.finish(kind==0?8:7,kind==1?2:1,kind==2?3:2,{facts(1,9,true),facts(2,12)},safe));
  CHECK(!p.query(7,1,facts(1,9,true),safe)); ++checks;
 }
 { P p; p.attach(7,1); const auto last=std::numeric_limits<uint64_t>::max();
   CHECK(p.start(7,1,last,P::Action::Defend,facts(1,9),safe));
   CHECK(p.finish(7,1,last,{facts(1,9,true)},safe));
   CHECK(!p.start(7,1,0,P::Action::Walk,facts(2,12),safe));
   CHECK(!p.query(7,1,facts(1,9,true),safe)); ++checks; }
 { P p; establish(p); CHECK(!p.start(7,1,1,P::Action::Walk,facts(2,12),safe));
   CHECK(!p.query(7,1,facts(1,9,true),safe)); ++checks; }
 { P p; establish(p); CHECK(!p.start(8,1,2,P::Action::Walk,facts(2,12),safe)); ++checks; }
 { P p; establish(p); CHECK(!p.start(7,2,2,P::Action::Walk,facts(2,12),safe)); ++checks; }
 std::cout << checks << " deterministic Defend scenarios passed\n";
}
