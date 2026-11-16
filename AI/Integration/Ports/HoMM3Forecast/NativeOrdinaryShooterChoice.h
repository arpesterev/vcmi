/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "../../../../lib/CStack.h"
#include "NativeOrdinaryTwoPass.h"
#include "CapturedOrdinaryShooterChoice.h"
#include "../../../../lib/StartInfo.h"
#include "../../../../lib/callback/IGameInfoCallback.h"
#include "../../../../lib/battle/BattleAction.h"
#include <vcmi/Environment.h>

namespace HoMM3Forecast
{
// Actual participant ownership, not the AI module's presence or spell preference.
inline std::optional<NativeForecastPolicy> captureNativeShooterPolicy(
 const CPlayerBattleCallback & battle, const Environment * environment)
{
 const auto * game=environment?environment->game():nullptr;
 const auto * start=game?game->getStartInfo():nullptr;
 if(!start || start->getDifficulty()<EMapDifficulty::EASY || start->getDifficulty()>EMapDifficulty::IMPOSSIBLE)
  return std::nullopt;
 NativeForecastPolicy result;result.difficulty=static_cast<int>(start->getDifficulty());
 for(int side=0;side<2;++side)
 {
  const auto color=battle.sideToPlayer(side==0?BattleSide::ATTACKER:BattleSide::DEFENDER);
  if(color==PlayerColor::NEUTRAL){result.human[side]=false;continue;}
  const auto * player=game->getPlayerSettings(color);
  if(!player)return std::nullopt;
  result.human[side]=player->isControlledByHuman();
 }
 return result;
}

inline std::optional<BattleAction> chooseNativeOrdinaryShooter(
 const CPlayerBattleCallback & battle,const NativeLifecycle & lifecycle,
 const Environment * environment,const CStack & active)
{
 const auto policy=captureNativeShooterPolicy(battle,environment);if(!policy)return std::nullopt;
 const auto facts=captureNativeOrdinaryFacts(battle,lifecycle,*policy,sourceSide(active.unitSide()),false);
 if(!facts)return std::nullopt;
 const auto choice=chooseCapturedOrdinaryShooter(*facts,active.unitId());if(!choice)return std::nullopt;
 if(!choice->target)return BattleAction::makeDefend(&active);
 const auto *target=battle.battleGetStackByID(*choice->target);
 if(!target||!target->alive()||target->isInvincible()||target->unitSide()==active.unitSide()
  ||!battle.battleCanShoot(&active,target->getPosition()))return std::nullopt;
 return BattleAction::makeShotAttack(&active,target);
}
}
