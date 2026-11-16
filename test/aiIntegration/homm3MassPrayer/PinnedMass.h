// SPDX-License-Identifier: CC0-1.0
#pragma once
#include "../homm3PrayerRemoval/PinnedRemoval.h"
#include <vector>
namespace PinnedMassPrayer
{
constexpr int SPELL_DISPEL=35,eMasteryAdvanced=2,eMasteryExpert=3;
struct type_enchant_data{int m_spell=SPELL_DISPEL,m_mastery=eMasteryExpert;};
struct type_spell_choice:type_enchant_data{int32_t m_value=0;bool m_castNow=false;};
struct army{AIIntegration::Ports::HoMM3::PrayerRemovalFacts facts;bool praying=true;bool eligible=true;bool cannotAttack()const{return !eligible;}int getSpellTime(int)const{return 0;}};
struct Manager{int m_numArmies[2]{};std::vector<army>m_armies[2];bool validSpellTargetArmy(int,int,const army*,int,int)const{return true;}};
inline Manager*g_combatManager=nullptr;
struct type_AI_spellcaster{
 using TEnchantValue=int32_t(type_AI_spellcaster::*)(const army*,type_enchant_data)const;
 int m_side=0,m_enemySide=1,m_isCreatureSpell=0;
 bool spellTargetsASingleArmy(int,int)const{return false;}
 void considerSingleEnchantment(type_spell_choice&,int)const{throw std::logic_error("mass only");}
 TEnchantValue getEnchantmentFunction(int)const{return &type_AI_spellcaster::getDispelValue;}
 int32_t getCancelValue(army*a,int bad)const{
  if(bad||!a->praying)throw std::logic_error("invalid copy");
  a->praying=false;
  // The source valuation receives a per-target copy, never a board with every
  // Prayer removed. Check other original stacks remain untouched at each call.
  for(const auto&side:g_combatManager->m_armies)for(const auto&peer:side)if(!peer.praying)throw std::logic_error("shared clean contamination");
  return PinnedPrayerRemoval::evaluate(a->facts);
 }
 int32_t getDispelValue(const army*,type_enchant_data)const;
 void considerEnchantment(type_spell_choice&,int32_t)const;
 void dispel(type_spell_choice&choice)const{switch(choice.m_spell){
#include "dispatch.pinned.inc"
 default:throw std::logic_error("not Dispel");}}
};
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define long int32_t
#include "bodies.pinned.inc"
#undef long
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
inline int32_t evaluate(Manager&manager){g_combatManager=&manager;for(int i=0;i<2;++i)manager.m_numArmies[i]=manager.m_armies[i].size();type_spell_choice choice;type_AI_spellcaster caster;caster.dispel(choice);if(!choice.m_castNow)throw std::logic_error("mass timing");g_combatManager=nullptr;return choice.m_value;}
}
