#include "../AI/Integration/Ports/HoMM3Forecast/OrdinaryTwoPass.h"
#include <iostream>
#include <stdexcept>
using namespace HoMM3Forecast;
int checks=0;
void check(bool good,const char * why){++checks;if(!good)throw std::runtime_error(why);}
OrdinaryForecastUnit troop(UnitId id,Side side,int slot,int speed,int count,int damage,int head)
{
 OrdinaryForecastUnit u;u.state.order.id=id;u.state.order.owningSide=side;u.state.controllingSide=side;
 u.state.order.sourceIndex=slot;u.state.order.effectiveSpeed=speed;
 u.facts.count=count;u.facts.hitPoints=10;u.facts.minimumDamage=u.facts.maximumDamage=damage;
 u.facts.canShoot=true;u.facts.lossUnitValue={1,1};u.state.liveHealth=count*10;u.liveCombatValue=count;u.head=head;return u;
}
OrdinaryTwoPassInput input()
{
 OrdinaryTwoPassInput i;i.difficulty=2;i.checkingSurrender=true;
 i.units={troop(1,Side::ATTACKER,0,12,1,1,18),troop(2,Side::DEFENDER,0,10,10,10,30)};
 i.attacks[{1,2}]={};i.attacks[{2,1}]={};i.actionScratch={3,4,5,6};return i;
}
int main()
{
 auto i=input();auto r=simulateOrdinaryTwoPass(i);
 check(bool(r),"ordinary two passes admitted");check(r->passes==2&&r->actions.size()==2,"both ownership blocks executed");
 check(r->expectedDamage.at(1)==10&&r->expectedDamage.at(2)==1,"second pass retains wound and kills first side");
 check(r->actionScratch==i.actionScratch,"four action fields unchanged");
 i.checkingSurrender=false;r=simulateOrdinaryTwoPass(i);
 check(r&&r->expectedDamage.at(1)==0&&r->expectedDamage.at(2)==1&&r->passes==1,"one pass is not defeat evidence");
 i=input();i.units.push_back(troop(3,Side::ATTACKER,1,8,100,1,35));i.attacks[{3,2}]={};i.attacks[{2,3}]={};
 r=simulateOrdinaryTwoPass(i);check(r&&r->stoppedAt[1]<r->order.size(),"two passes stop at next ownership boundary");
 for(int mode=0;mode<8;++mode)
 {
  i=input();
  if(mode==0)i.units[0].initialExpectedDamage=-1;
  if(mode==1)i.units[0].initialExpectedDamage=11;
  if(mode==2)i.units[1].head=i.units[0].head;
  if(mode==3)i.units[1].head=-1;
  if(mode==4)i.attacks.erase({1,2});
  if(mode==5)i.units[0].facts.canShoot=false;
  if(mode==6)i.units[1].liveCombatValue=999;
  if(mode==7)i.units[1].state.order.sourceIndex=2;
  check(!simulateOrdinaryTwoPass(i),"unknown/malformed inputs cannot prove outcome");
 }
 i=input();i.units[0].state.order.immobilized=true;i.units[0].initialExpectedDamage=4;
 r=simulateOrdinaryTwoPass(i);check(r&&r->order.size()==1&&r->expectedDamage.at(1)>=4,"off-order initial ledger retained");
 i={};i.checkingSurrender=true;r=simulateOrdinaryTwoPass(i);check(r&&r->passes==2&&r->actions.empty(),"empty two-pass construction");
 i=input();for(auto &u:i.units){u.state.order.sourceSpell70Turns=1;u.facts.lossUnitValue={double(INT32_MAX),double(INT32_MAX)};u.facts.count=1;u.state.liveHealth=10;u.liveCombatValue=INT32_MAX;}
 check(!simulateOrdinaryTwoPass(i),"empty prefix still validates signed constructor multiplication");
 i=input();i.units[0].state.order.effectiveSpeed=i.units[1].state.order.effectiveSpeed;
 check(!simulateOrdinaryTwoPass(i),"source comparator ambiguity unavailable");
 std::cout<<"two-pass contract checks "<<checks<<'\n';
}
