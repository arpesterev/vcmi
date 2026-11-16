// SPDX-License-Identifier: CC0-1.0
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace PinnedInvariantOrder
{
enum {creatureDone=1,creatureWaiting=2,creatureImmobilized=4};
enum {CREATURE_ARROW_TOWER=1,CREATURE_FIRST_AID_TENT=2,CREATURE_AMMO_CART=3};
struct army {
 uint32_t id=0;int index=0,group=0,speed=0,m_creatureType=0,flags=0,m_expectedMoveOrder=0;
 std::array<int,80> spells{};
 bool is(int flag)const{return flags&flag;}int getSpeed()const{return speed;}
 int getSpellTime(int spell)const{return spells.at(spell);}
 bool isIncapacitated()const{return spells[62]||spells[70]||spells[74];}
};
struct func_moves_before {unsigned char operator()(const army*,const army*);};
struct combatManager {
 army m_armies[2][21];int m_numArmies[2]{};int m_actingSide=0;bool secondPhase=false;
 bool isInSecondPhase()const{return secondPhase;}
 void findMoveOrder(std::vector<army*>*);
};
inline combatManager manager;inline combatManager *g_combatManager=&manager;
inline std::vector<uint32_t> permutation;
#define long int32_t
#include "bodies.instrumented.inc"
#undef long
}
