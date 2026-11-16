/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "OrdinaryLiveMeleeChoice.h"
namespace HoMM3Forecast
{
struct OrdinaryLiveMixedInput
{
 OrdinaryGroundTwoPassInput state;
 UnitId activeId=0;
 bool noCreatureSpellKnown=false;
};
// Ordinary doCompAI arm: actual current army, non-simulated physical HP,
// rebuilt relevant census, ordinary shooter or melee dispatch. No forecast run.
inline std::optional<OrdinaryLiveMeleeResult> chooseOrdinaryLiveMixed(const OrdinaryLiveMixedInput & input)
{
 try{
  OrdinaryGroundDetail::Model model(input.state,input.activeId);
  const auto active=std::find_if(input.state.common.units.begin(),input.state.common.units.end(),[&](const auto&u){return u.state.order.id==input.activeId;});
  if(active==input.state.common.units.end()||(!active->facts.canShoot&&!input.noCreatureSpellKnown))return std::nullopt;
  auto choice=model.liveMixedChoice(input.activeId);return OrdinaryLiveMeleeResult{std::move(choice),model.drawsConsumed()};
 }catch(const OrdinaryGroundDetail::Unavailable &){return std::nullopt;}
}
}
