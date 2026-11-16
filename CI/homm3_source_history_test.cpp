#include "../AI/Integration/Ports/HoMM3Forecast/OrdinarySourceHistory.h"
#include <iostream>
#include <stdexcept>
using namespace HoMM3Forecast;
static size_t checks=0;
static void check(bool b,const char*m){++checks;if(!b)throw std::runtime_error(m);}
int main(){try{
 MoveOrderInput state;
 for(int side=0;side<2;++side)for(int i=0;i<3;++i){OrderUnit u;u.id=side*100+i+1;u.owningSide=static_cast<Side>(side);u.sourceIndex=i;u.effectiveSpeed=side*3+i+1;state.units.push_back(u);}
 OrdinarySourceHistory h;check(h.beginFresh(state.units),"fresh");
 for(const auto&u:state.units){check(h.find(u.id)->facing==1-static_cast<int>(u.owningSide),"source facing");check(!h.find(u.id)->rank,"rank starts unknown");}
 check(h.possibleSourceOrder(state),"unknown caller branch");check(!h.find(103)->rank,"possible writer cannot initialize unknown rank");
 check(h.recordSourceOrder(state),"source writer");check(h.find(103)->rank==6,"highest enemy rank");
 check(h.possibleSourceOrder(state)&&h.find(103)->rank==6,"equal write or preserve outcomes retain known rank");
 auto changed=state;changed.units.back().effectiveSpeed=0;check(h.possibleSourceOrder(changed)&&!h.find(103)->rank,"differing outcomes become unknown");
 check(h.recordSourceOrder(state),"certain writer restores ranks");
 auto dead=state;dead.units.back().immobilized=true;
 check(h.completeOrdinary(OrdinarySourceHistory::CompletedAction::MELEE,dead.units),"supported kill including retaliation");
 check(h.find(103)->rank==6&&!h.find(103)->facing,"corpse retains rank not facing");
 check(h.recordSourceOrder(dead),"next live source event");check(h.find(103)->rank==6,"later projection leaves corpse alone");
 for(auto action:{OrdinarySourceHistory::CompletedAction::MOVE,OrdinarySourceHistory::CompletedAction::SHOOT,OrdinarySourceHistory::CompletedAction::WAIT,OrdinarySourceHistory::CompletedAction::DEFEND}){check(h.completeOrdinary(action,dead.units),"ordinary completion");check(h.find(1)->facing==1&&h.find(101)->facing==0&&h.find(103)->rank==6,"continued facing and rank");}
 h.unknownOrderEvent();check(!h.find(1)->rank&&h.find(103)->rank==6,"unknown writer cannot rewrite corpse");
 check(h.recordSourceOrder(dead),"known writer recovers live ranks");
 check(!h.completeOrdinary(OrdinarySourceHistory::CompletedAction::MELEE,state.units)&&!h.known(),"resurrection invalidates");
 check(h.beginFresh(state.units),"new battle");check(h.completeOrdinary(OrdinarySourceHistory::CompletedAction::SHOOT,dead.units),"first shot kill");check(!h.find(103)->rank,"no invented rank before first writer");
 check(h.recordSourceOrder(dead)&&!h.find(103)->rank,"later writer cannot recover unknown corpse");
 auto replaced=dead;replaced.units[0].id=999;check(!h.completeOrdinary(OrdinarySourceHistory::CompletedAction::MOVE,replaced.units),"replacement invalidates");
 check(!h.beginFresh(dead.units),"late corpse snapshot not fresh");
 check(h.beginFresh(state.units),"fresh again");auto control=state;control.units[0].owningSide=Side::DEFENDER;check(!h.recordSourceOrder(control),"control change invalidates");
 auto tied=state;for(auto&u:tied.units)u.effectiveSpeed=5;
 check(h.beginFresh(tied.units)&&h.recordSourceOrder(tied),"paired initial order known");
 const auto rankA=h.find(101)->rank,rankB=h.find(102)->rank;
 tied.units[3].immobilized=tied.units[4].immobilized=true;
 check(h.completeOrdinary(OrdinarySourceHistory::CompletedAction::MELEE,tied.units),"two ordinary deaths");
 check(!h.recordSourceOrder(tied),"noninvariant writer not certified");
 check(!h.find(1)->rank&&!h.find(103)->rank&&h.find(101)->rank==rankA&&h.find(102)->rank==rankB,"failed writer invalidates only eligible ranks");
 auto duplicate=state;duplicate.units[1].sourceIndex=0;check(!h.beginFresh(duplicate.units),"duplicate source slot rejected");
 std::cout<<checks<<" source history checks\n";
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
