/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "../../BattleAI/StackWithBonuses.h"
namespace AIIntegration::Ports::HoMM3
{
// getDispelValue creates a target army copy, not a globally dispelled board.
// The caller owns a synchronous immutable pre-cast baseline read epoch.
inline std::shared_ptr<HypotheticBattle> singlePrayerRemovedCopy(const Environment *environment,
 const std::shared_ptr<HypotheticBattle>&baseline,uint32_t target)
{
 auto copy=std::make_shared<HypotheticBattle>(environment,baseline);
 copy->getForUpdate(target)->removeUnitBonus(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::PRAYER))));
 return copy;
}
inline bool samePrayerRemovalState(const battle::Unit & expected,const battle::Unit & actual)
{
 if(expected.unitId()!=actual.unitId() || expected.unitSide()!=actual.unitSide()
  || expected.unitSlot()!=actual.unitSlot() || expected.unitOwner()!=actual.unitOwner()
  || expected.controllingSide()!=actual.controllingSide() || expected.getAvailableHealth()!=actual.getAvailableHealth()
  || expected.alive()!=actual.alive() || expected.getCount()!=actual.getCount()
  || expected.getPosition()!=actual.getPosition() || expected.getMovementRange()!=actual.getMovementRange()
  || expected.acquireState()->save()!=actual.acquireState()->save())return false;
 const auto left=expected.getAllBonuses(Selector::all),right=actual.getAllBonuses(Selector::all);
 if(left->size()!=right->size())return false;
 std::vector<JsonNode> remaining;for(const auto&bonus:*right)remaining.push_back(bonus->toJsonNode());
 for(const auto&bonus:*left){const auto it=std::find(remaining.begin(),remaining.end(),bonus->toJsonNode());if(it==remaining.end())return false;remaining.erase(it);}
 return remaining.empty();
}
// Certify the WHOLE unit roster, including units which a later scoring loop
// would skip as absent/dead/inactive. No changed participant may be omitted.
inline bool onlyPrayerRemoved(const Environment *environment,const std::shared_ptr<HypotheticBattle>&baseline,
 const HypotheticBattle&projected)
{
 const auto all=[](const battle::Unit*){return true;};
 const auto before=baseline->battleGetUnitsIf(all),after=projected.battleGetUnitsIf(all);
 if(before.size()!=after.size())return false;
 const auto prayer=Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::PRAYER)));
 for(const auto*unit:before){
  const auto*changed=projected.battleGetUnitByID(unit->unitId());if(!changed)return false;
  if(baseline->battleGetOwner(unit)!=projected.battleGetOwner(changed))return false;
  if(unit->hasBonus(prayer)&&!changed->hasBonus(prayer)){
   if(!unit->alive() || unit->isGhost())return false;
   auto copy=singlePrayerRemovedCopy(environment,baseline,unit->unitId());
   if(!samePrayerRemovalState(*copy->battleGetUnitByID(unit->unitId()),*changed))return false;
  }else if(!samePrayerRemovalState(*unit,*changed))return false;
 }
 return true;
}
}
