/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "OrdinaryGroundTwoPass.h"
namespace HoMM3Forecast
{
struct OrdinaryLiveMeleeInput
{
 OrdinaryGroundTwoPassInput state;
 UnitId activeId=0;
 // Exact source m_hasSpell==0 (or canCastSpells(side,0)==false) certificate.
 bool noCreatureSpellKnown=false;
};
struct OrdinaryLiveMeleeResult
{
 OrdinaryGroundAction choice;
 size_t drawsConsumed=0;
};
inline std::optional<OrdinaryLiveMeleeResult> chooseOrdinaryLiveMelee(const OrdinaryLiveMeleeInput & input)
{
 if(!input.noCreatureSpellKnown)return std::nullopt;
 try{OrdinaryGroundDetail::Model model(input.state);auto choice=model.liveMeleeChoice(input.activeId);return OrdinaryLiveMeleeResult{std::move(choice),model.drawsConsumed()};}
 catch(const OrdinaryGroundDetail::Unavailable &){return std::nullopt;}
}
}
