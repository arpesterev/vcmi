#define main existing_mixed_main
#include "homm3_live_mixed_test.cpp"
#undef main
#include "../AI/Integration/Ports/HoMM3Forecast/OrdinarySourceHistory.h"
int main(){try{
 R::akCreatureTypeTraits[0].baseFightValue=37;R::akCreatureTypeTraits[1].baseFightValue=103;
 setup(3,3,91);R::manager.m_currentSide=R::manager.m_actingSide=0;R::game.m_setup.m_difficulty=4;R::manager.secondPhase=R::manager.m_inSecondPhase=false;R::draws.assign(1000,90);
 PinnedForecastPath::manager.setupAdjacencyArray();
 for(int h=0;h<187;++h){R::manager.m_cells[h].value=nullptr;R::manager.m_cells[h].m_refX=(h%17)*44+((h/17)%2?22:44)+14;for(int d=0;d<6;++d)R::manager.m_adjacentCells[h][d]=PinnedForecastPath::manager.m_adjacentCells[h][d];}
 const int positions[2][3]={{38,108,59},{57,37,76}};
 for(int side=0;side<2;++side)for(int i=0;i<3;++i){auto&u=R::manager.m_armies[side][i];u.m_gridIndex=positions[side][i];u.shooting=false;u.flags&=~(R::creatureShootingArmy|R::creatureDone|R::creatureWaiting);u.m_spellInfluence[59]=u.m_spellInfluence[74]=0;u.AI_expected_damage=0;R::manager.m_cells[u.m_gridIndex].value=&u;}
 auto&dead=R::manager.m_armies[1][2];dead.speed=125;
 auto orderFrom=[&](){MoveOrderInput o;o.actingSide=Side::ATTACKER;for(const auto&u:groundInput(0,false).common.units)o.units.push_back(u.state.order);return o;};
 OrdinarySourceHistory history;auto before=orderFrom();check(history.beginFresh(before.units),"true start");check(!history.find(dead.id)->rank,"initial unknown");
 // A certified source order event occurs while the future corpse is alive.
 R::manager.findMoveOrder(nullptr);check(history.recordSourceOrder(before),"source writer");check(history.find(dead.id)->rank==dead.m_expectedMoveOrder&&dead.m_expectedMoveOrder==6,"rank matches original pre-death writer");
 R::manager.m_cells[dead.m_gridIndex].value=nullptr;dead.flags|=R::creatureImmobilized;dead.m_numTroops=dead.m_topCreatureDamage=0;
 auto after=orderFrom();check(history.completeOrdinary(OrdinarySourceHistory::CompletedAction::MELEE,after.units),"observed supported death");
 // A later ordinary action and another source-order event must not erase it.
 check(history.completeOrdinary(OrdinarySourceHistory::CompletedAction::SHOOT,after.units),"other ordinary action");
 check(history.recordSourceOrder(after),"next melee source boundary");
 OrdinaryLiveMixedInput input{groundInput(0,false),1,true};
 for(auto&[id,meta]:input.state.ground){meta.priorMoveRank=history.find(id)->rank;if(history.find(id)->facing)meta.facing=*history.find(id)->facing;}
 const auto accepted=chooseOrdinaryLiveMixed(input);check(bool(accepted),"post-death continued live choice");
 R::manager.currentArmy=&R::manager.m_armies[0][0];R::drawCursor=0;R::manager.doCompAI(0);const auto&pinned=R::manager.trace.back();
 check(accepted->choice.target==pinned.target&&accepted->choice.target==std::optional<UnitId>(102),"retained history source target");check(accepted->choice.value==1726&&pinned.value==1726,"retained history exact value");
 input.state.ground.at(dead.id).priorMoveRank=0;const auto wrong=chooseOrdinaryLiveMixed(input);check(wrong&&wrong->choice.target==std::optional<UnitId>(101)&&wrong->choice.value==426,"zero guessing changes target");
 input.state.ground.at(dead.id).priorMoveRank.reset();check(!chooseOrdinaryLiveMixed(input),"unknown history rejects");
 std::cout<<checks<<" source history corpse differential checks\n";
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
