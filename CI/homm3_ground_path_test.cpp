#include "../AI/Integration/Ports/HoMM3GroundPath.h"
#include "../test/aiIntegration/homm3Ground/PinnedGround.h"
#include <iostream>
#include <stdexcept>
#include <climits>
namespace
{
using namespace AIIntegration::Ports::HoMM3;
namespace Ref = PinnedGround;
uint64_t checks=0, maps=0;
void require(bool value,const char *message) { ++checks;if(!value)throw std::runtime_error(message); }
bool available(int h) { return h>=0&&h<187&&h%17!=0&&h%17!=16; }
void compare(const SourceGroundSearchInput &in)
{
 const auto actual=searchSourceGround(in);require(actual.has_value(),"admissible map rejected");++maps;
 Ref::manager={};Ref::manager.setupAdjacencyArray();
 for(int h=0;h<187;++h)
 {
  Ref::manager.m_cells[h].m_attributes=in.map[h].blocked?Ref::hexcell::blocked:0;
  if(const auto id=in.map[h].occupant)
  {
   auto &cell=Ref::manager.m_cells[h];cell.m_armySide=id->side;cell.m_armySlot=id->index;
   auto &a=Ref::manager.m_armies[id->side][id->index];a.group=id->side;a.index=id->index;a.m_gridIndex=h;
  }
 }
 auto &actor=Ref::manager.m_armies[in.actor.side][in.actor.index];
 actor.speed=in.speed;actor.restricted=in.movementRestricted;
 Ref::searchArray search;search.seedCombatPosition(&actor,in.actor.side,127,false,-1);
 for(int h=0;h<187;++h)
 {
  const auto &cell=*search.getHex(h);const auto &published=Ref::manager.m_cells[h];
  const bool marked=published.m_validMove&&in.map[h].occupant&&in.map[h].occupant->side!=in.actor.side;
  require(actual->visited[h]==cell.m_visited,"visited mismatch");
  require(actual->validMove[h]==published.m_validMove,"validMove mismatch");
  require(actual->enemyMarked[h]==marked,"enemyMarked mismatch");
  require(actual->cost[h]==(cell.m_visited||marked?cell.m_cost:UINT16_MAX),"cost mismatch");
 }
}
SourceGroundSearchInput base(int head=86)
{
 SourceGroundSearchInput in;in.head=head;in.speed=6;in.map[head].occupant=in.actor;return in;
}
}
int main()
{
 for(int head=0;head<187;++head)if(available(head))
 for(int target=0;target<187;++target)if(available(target)&&target!=head)
 for(int speed:{0,1,6,127})
 {
  auto in=base(head);in.speed=speed;in.map[target].occupant=SourceGroundIdentity{1,3};compare(in);
 }
 // Corpse coordinates are metadata, not occupants: compare every published free cell.
 for(int head=0;head<187;++head)if(available(head))
 for(int speed:{0,1,4,127,INT_MAX})
 for(bool restricted:{false,true})
 { auto in=base(head);in.speed=speed;in.movementRestricted=restricted;compare(in); }
 uint32_t random=0x9c38143u;
 auto next=[&](){random=random*1664525u+1013904223u;return random;};
 for(int sample=0;sample<2000;++sample)
 {
  int head;do{head=next()%187;}while(!available(head));
  auto in=base(head);in.actor.side=sample%2;in.map[head].occupant=in.actor;
  in.speed=sample%18;in.movementRestricted=sample%11==0;
  int ids[2]={1,1};
  for(int h=0;h<187;++h)if(available(h)&&h!=head)
  {
   auto roll=next()%10;
   if(roll<3)in.map[h].blocked=true;
   else if(roll==3)
   {int side=next()%2;if(ids[side]<31)in.map[h].occupant=SourceGroundIdentity{side,ids[side]++};}
  }
  compare(in);
 }
 auto invalid=[&](SourceGroundSearchInput in){require(!searchSourceGround(in),"invalid domain accepted");};
 {auto in=base();in.domainKnown=false;invalid(in);}
 {auto in=base();in.speed=-1;invalid(in);}
 {auto in=base();in.map[in.head].occupant.reset();invalid(in);}
 {auto in=base();in.map[in.head].blocked=true;invalid(in);}
 {auto in=base();in.map[87].occupant=in.actor;invalid(in);}
 {auto in=base();in.map[87].occupant=SourceGroundIdentity{2,0};invalid(in);}
 {auto in=base();in.map[87].occupant=SourceGroundIdentity{1,31};invalid(in);}
 {auto in=base();in.map[0].occupant=SourceGroundIdentity{1,0};invalid(in);}
 {auto in=base();in.map[87].occupant=SourceGroundIdentity{1,0};in.map[87].blocked=true;invalid(in);}
 for(int head:{-1,0,16,187,INT_MAX}){auto in=base();in.head=head;invalid(in);}
 std::cout<<maps<<" ground maps, "<<checks<<" pinned publication/contract checks passed\n";
}
