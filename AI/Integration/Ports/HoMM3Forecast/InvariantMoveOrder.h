/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "MoveOrder.h"
#include <optional>

namespace HoMM3Forecast
{
// Accept only when every source-comparator tie permutation yields the same
// post-alternation identity/rank sequence. The budget counts orientations per
// source key block; ordinary native seven-slot rosters need at most 128.
inline std::optional<ProjectedMoveOrder> projectInvariantMoveOrder(
 const MoveOrderInput & input,size_t orientationBudget=4096)
{
 auto sorted=makeCandidates(input);
 std::stable_sort(sorted.begin(),sorted.end(),sourceMovesBefore);
 ProjectedMoveOrder result{{},false};
 Side desired=input.actingSide;
 for(size_t first=0;first<sorted.size();)
 {
  size_t end=first+1;while(end<sorted.size()&&sorted[end].key==sorted[first].key)++end;
  std::vector<Candidate> block(sorted.begin()+first,sorted.begin()+end);
  std::vector<size_t> ties;bool allPaired=true;
  for(size_t i=0;i<block.size();)
  {
   size_t j=i+1;while(j<block.size()&&!sourceMovesBefore(block[i],block[j])&&!sourceMovesBefore(block[j],block[i]))++j;
   if(j-i==2){ties.push_back(i);result.containsComparatorTies=true;}
   else if(j-i==1)allPaired=false;
   else throw std::invalid_argument("Source comparator group exceeds the two owning sides");
   i=j;
  }
  const auto canonical=alternateSides(block,desired);
  // If the entire block consists of A_i/B_i pairs, the desired member is
  // always available inside the current pair. Each swap is local; after two
  // emitted units the desired side is unchanged. Induction proves all 2^n
  // orientations have this exact canonical sequence, without enumeration.
  if(!allPaired&&!ties.empty())
  {
   if(ties.size()>=std::numeric_limits<size_t>::digits)return std::nullopt;
   const size_t permutations=size_t{1}<<ties.size();
   if(permutations>orientationBudget)return std::nullopt;
   for(size_t mask=1;mask<permutations;++mask)
   {
    auto candidate=block;
    for(size_t bit=0;bit<ties.size();++bit)if(mask&(size_t{1}<<bit))std::swap(candidate[ties[bit]],candidate[ties[bit]+1]);
    if(alternateSides(candidate,desired)!=canonical)return std::nullopt;
   }
  }
  for(const auto & unit:canonical)result.units.push_back({unit.id,static_cast<int32_t>(sorted.size()-result.units.size())});
  if(!canonical.empty())
  {
   const auto last=std::find_if(block.begin(),block.end(),[&](const auto&unit){return unit.id==canonical.back().id;});
   desired=last->owningSide==Side::ATTACKER?Side::DEFENDER:Side::ATTACKER;
  }
  first=end;
 }
 return result;
}
}
