// GPL-2.0-or-later. Observed individual spell record, not caster provenance.
#pragma once
#include "HoMM3NativeTerrainFacts.h"
namespace AIIntegration::Ports::HoMM3
{
struct NativeSpellDefenseFacts
{
 static bool stoneSkin(const Bonus & b)
 {
  return !b.limiter && NativeTerrainFacts::neutralMetadata(b)
   && b.type==BonusType::PRIMARY_SKILL && b.subtype==BonusSubtypeID(PrimarySkill::DEFENSE)
   && b.source==BonusSource::SPELL_EFFECT && b.sid==BonusSourceID(SpellID(SpellID::STONE_SKIN))
   && b.valType==BonusValueType::ADDITIVE_VALUE && (b.val==3 || b.val==6)
   && b.duration==BonusDuration::N_TURNS && b.turnsRemain>0
   && b.effectRange==BonusLimitEffect::NO_LIMIT;
 }
};
}
