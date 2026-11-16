from pathlib import Path
import json,hashlib,sys
root=Path(__file__).resolve().parent;up=Path(sys.argv[1]).read_text();manifest=[]
def extract(sig):
 start=up.index(sig);i=up.index('{',start)+1;depth=1
 while depth:depth+=(up[i]=='{')-(up[i]=='}');i+=1
 text=up[start:i];manifest.append({'signature':sig,'sha256':hashlib.sha256(text.encode()).hexdigest()});return text
bodies='\n\n'.join(extract(s) for s in ['static long getMoveOrder(', 'unsigned char func_moves_before::operator()(', 'void combatManager::findMoveOrder('])
(root/'bodies.pinned.inc').write_text(bodies+'\n')
injection='''std::sort(order.begin(), order.end(), func_moves_before());
    if(!permutation.empty()) {
        std::vector<army*> injected;
        for(auto id : permutation) {
            auto it = std::find_if(order.begin(), order.end(), [=](auto* unit){return unit->id == id;});
            if(it == order.end()) throw std::logic_error("unknown source permutation ID");
            injected.push_back(*it);
        }
        if(injected.size()!=order.size() || !std::is_sorted(injected.begin(),injected.end(),func_moves_before()))
            throw std::logic_error("invalid source comparator permutation");
        order=std::move(injected);
    }'''
(root/'bodies.instrumented.inc').write_text(bodies.replace('std::sort(order.begin(), order.end(), func_moves_before());',injection)+'\n')
(root/'source-manifest.json').write_text(json.dumps({'repository':'sushi-shi/homm3-decomp','commit':'e12906accf96c2662b1abadbc49ddc111991bf21','file':'ai.cpp','methods':manifest,'instrumentation':'After original std::sort, inject a caller-supplied comparator-valid tie permutation. Original key/comparator/alternating-side/rank statements unchanged.'},indent=2)+'\n')
(root/'PinnedInvariantOrder.h').write_text('''// SPDX-License-Identifier: CC0-1.0
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
''')

base=root.parent
s=(base/'homm3GroundTwoPass/PinnedGroundTwoPass.h').read_text().replace('PinnedGroundTwoPass','PinnedGroundPermutation').replace('#include "PinnedForecastPath.h"','#include "../homm3GroundTwoPass/PinnedForecastPath.h"').replace('#include "ground-bodies.instrumented.inc"','#include "../homm3GroundTwoPass/ground-bodies.instrumented.inc"').replace('#include "../homm3TwoPass/two-pass-bodies.instrumented.inc"','#include "two-pass-permuted.inc"').replace('inline std::vector<int> draws;','inline std::vector<uint32_t> sourceTiePermutation;\ninline std::vector<int> draws;')
(root/'PinnedGroundPermutation.h').write_text(s)
s=(base/'homm3TwoPass/two-pass-bodies.instrumented.inc').read_text()
s=s.replace('std::sort(order.begin(), order.end(), func_moves_before());',injection.replace('permutation','sourceTiePermutation'))
(root/'two-pass-permuted.inc').write_text(s)
