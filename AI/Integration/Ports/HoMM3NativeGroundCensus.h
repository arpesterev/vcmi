/* GPL-2.0-or-later. Public ordinary ground facts; source path search, not native BFS. */
#pragma once
#include "HoMM3NativeRangedCensus.h"
#include "HoMM3NativeGroundMap.h"
#include "HoMM3GroundCensus.h"
#include "../../../lib/StartInfo.h"
#include "../../../lib/callback/IGameInfoCallback.h"
#include <vcmi/Environment.h>

namespace AIIntegration::Ports::HoMM3
{
inline std::optional<bool> nativeRetaliationPolicy(const CPlayerBattleCallback & battle,
 BattleSide side,const Environment * environment)
{
 const auto * game=environment?environment->game():nullptr;
 const auto * start=game?game->getStartInfo():nullptr;
 if(!start || start->getDifficulty()<EMapDifficulty::EASY || start->getDifficulty()>EMapDifficulty::IMPOSSIBLE)
  return std::nullopt;
 if(start->getDifficulty()!=EMapDifficulty::EASY) return true;
 const auto * player=game->getPlayerSettings(battle.sideToPlayer(side));
 if(!player) return std::nullopt;
 return player->isControlledByHuman();
}
inline std::optional<NativeRangedCensus> captureNativeOrdinaryCensus(const CPlayerBattleCallback & battle,
 BattleSide side,const Environment * environment,bool publicStaticMapCertified,bool killsOnly=false,const CurrentDefendSnapshot * witness=nullptr)
{
 auto result=captureNativeRangedCensus(battle,side,killsOnly,witness);
 if(!result || !publicStaticMapCertified) return result;
 const auto policy=nativeRetaliationPolicy(battle,side,environment);
 const auto geometry=captureNativeVisibleGroundMap(battle);
 if(!policy || !geometry) return result;
 int32_t lowestAttack=INT32_MAX,lowestDefense=INT32_MAX;
 for(const auto & group:geometry->roster)
  for(const auto * u:group)
  {
   if(!u->alive()) continue;
   const auto m=NativeExchangeFacts::modifiers(battle,*u,witness);
   if(!m) return result;
   lowestAttack=std::min(lowestAttack,m->first);lowestDefense=std::min(lowestDefense,m->second);
  }
 const int own=side==BattleSide::ATTACKER?0:1;
 const auto & friendly=geometry->roster[own];const auto & enemy=geometry->roster[1-own];
 if(friendly.size()!=result->friendlyIds.size() || enemy.size()!=result->enemyIds.size()) return result;
 for(size_t i=0;i<friendly.size();++i) if(friendly[i]->unitId()!=result->friendlyIds[i]) return result;
 for(size_t i=0;i<enemy.size();++i) if(enemy[i]->unitId()!=result->enemyIds[i]) return result;
 for(size_t i=0;i<friendly.size();++i)
 {
  const auto & actor=*friendly[i];
  // A failed ground proof cannot erase an existing known ranged/filtered row.
  if(result->rows[i] || !actor.alive() || battle.battleCanShoot(&actor)) continue;
  const auto actorFacts=NativeExchangeFacts::unit(battle,actor,lowestAttack,lowestDefense,witness);
  const auto speed=NativeExchangeFacts::groundSpeed(battle,actor);
  if(!actorFacts || !speed || !NativeExchangeFacts::ordinaryMelee(actor)) continue;
  SourceGroundCensusInput input;input.actor=*actorFacts;input.killsOnly=killsOnly;input.retaliationPolicy=*policy;
  input.geometry=SourceGroundSearchInput{};
  input.geometry->map=geometry->cells;input.geometry->actor={own,static_cast<int>(i)};
  input.geometry->head=actor.getPosition().toInt();input.geometry->speed=*speed;
  input.enemies.emplace();
  for(size_t j=0;j<enemy.size();++j)
  {
   const auto & target=*enemy[j];SourceGroundCandidate peer;
   peer.rosterIndex=static_cast<int>(j);peer.head=target.getPosition().toInt();
   peer.unit=NativeExchangeFacts::unit(battle,target,lowestAttack,lowestDefense,witness);
   peer.attack=NativeExchangeFacts::meleeAttack(battle,actor,target,witness);
   peer.retaliation=target.alive()?NativeExchangeFacts::meleeAttack(battle,target,actor,witness):std::optional<SourceOrdinaryAttack>{};
   peer.canRetaliate=NativeExchangeFacts::retaliationAvailable(actor,target);
   input.enemies->push_back(std::move(peer));
  }
  const auto row=produceSourceGroundCensus(input);
  if(row) result->rows[i]=row->census;
 }
 return result;
}
}
