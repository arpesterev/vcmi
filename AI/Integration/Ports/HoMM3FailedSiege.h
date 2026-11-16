/* HoMM3 failed-siege withdrawal, bounded native bridge. GPL-2.0-or-later.
 * Source: homm3-decomp e12906accf96c2662b1abadbc49ddc111991bf21 ai.cpp,
 * failedSiege and the pre-simulation gates of aiCheckRetreat (CC0-1.0).
 */
#pragma once
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/CStack.h"
#include "../../../lib/callback/IGameInfoCallback.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapping/CMapHeader.h"
#include "../../../lib/StartInfo.h"
#include "../../../lib/CPlayerState.h"

namespace AIIntegration::Ports::HoMM3
{
inline std::optional<BattleAction> failedSiegeRetreat(const CPlayerBattleCallback & battle,
 const IGameInfoCallback * game,const CStack & active,bool human,bool autocombat)
{
 if(human || autocombat || !game || battle.battleTacticDist()
  || battle.battleGetMySide()!=BattleSide::ATTACKER || battle.battleActiveUnit()!=&active
  || !active.alive() || active.unitSide()!=BattleSide::ATTACKER
  || active.controllingSide()!=BattleSide::ATTACKER)return std::nullopt;
 const auto * hero=battle.battleGetMyHero();
 const auto * start=game->getStartInfo();
 const auto * map=game->getMapHeader();
 if(!hero || !start || !map || hero->getOwner()!=battle.sideToPlayer(BattleSide::ATTACKER)
  || active.unitOwner()!=hero->getOwner() || start->getDifficulty()<EMapDifficulty::HARD
  || start->getDifficulty()>EMapDifficulty::IMPOSSIBLE || !battle.battleCanFlee())return std::nullopt;
 // Decline custom victory objectives, including the protected defeat-hero target.
 bool standard=false;
 for(const auto & event:map->triggeredEvents)if(event.effect.type==EventEffect::VICTORY)
 {
  const auto expression=event.trigger.get();
  const auto * condition=std::get_if<EventCondition>(&expression);
  if(!condition || condition->condition!=EventCondition::STANDARD_WIN)return std::nullopt;
  standard=true;
 }
 // Native flee legality ignores negative permissions against a hero-less army;
 // the source explicitly forbids our own equipped Shackles (canonical H3 ID125).
 if(hero->hasArt(ArtifactID(125),true))return std::nullopt;
 if(!standard)return std::nullopt;
 const auto * player=game->tryGetPlayerState(hero->getOwner());
 if(!player || player->isHuman())return std::nullopt;
 bool tavern=false;
 for(const auto * town:player->getTowns())
  if(town && town->getOwner()==hero->getOwner() && town->hasBuilt(BuildingID::TAVERN))tavern=true;
 if(!tavern || !battle.battleGetDefendedTown() || battle.battleGetGateState()!=EGateState::CLOSED)
  return std::nullopt;
 for(const auto part:{EWallPart::BOTTOM_WALL,EWallPart::BELOW_GATE,EWallPart::OVER_GATE,EWallPart::UPPER_WALL})
  if(battle.battleGetWallState(part)<=EWallState::DESTROYED)return std::nullopt;
 bool attacker=false,defender=false;
 for(const auto * unit:battle.battleGetAllStacks(false))
 {
  if(!unit->alive())continue;
  if(unit->unitSide()!=unit->controllingSide() || unit->isClone() || unit->isSummoned())return std::nullopt;
  if(unit->unitSide()==BattleSide::ATTACKER)
  {
   attacker=true;
   // This first bridge admits ordinary grounded non-shooters only. It does
   // not reinterpret blocked/depleted archers or special movement as source canShoot(0).
   if(unit->hasBonusOfType(BonusType::FLYING) || unit->isCatapult() || unit->isShooter()
    || unit->isTurret() || !unit->getPosition().isAvailable()
    || battle.battleIsInsideWalls(unit->getPosition()))return std::nullopt;
  }
  else
  {
   if(unit->isTurret())continue;
   defender=true;
   if(!unit->getPosition().isAvailable() || !battle.battleIsInsideWalls(unit->getPosition()))
    return std::nullopt;
  }
 }
 if(!attacker || !defender)return std::nullopt;
 return BattleAction::makeRetreat(BattleSide::ATTACKER);
}
}
