/* GPL-2.0-or-later. Extracted scalar ordering from ihhub/fheroes2
 * 0dbfe9f386f33c2500842f4a59cd284d7f03d541
 * src/fheroes2/ai/ai_battle_spell.cpp::spellDamageValue, lines 168-222.
 * Test oracle, not the production implementation. No clone/link domain.
 */
#pragma once
#include <cstdint>
#include <algorithm>
namespace FHeroes2PinnedOracle
{
inline double value(uint32_t spellDamage, uint32_t magicResist, uint32_t hitPoints,
 uint32_t count, uint32_t creatureHP, double strength, double armyStrength,
 bool faster, bool immovable, bool retreating)
{
 const uint32_t damage = spellDamage * (100 - magicResist) / 100;
 if(damage == 0) return 0;
 if(retreating)
 {
  const uint32_t survivors = damage >= hitPoints ? 0
   : (hitPoints - damage) / creatureHP + ((hitPoints - damage) % creatureHP != 0);
  return strength / count * (count - survivors);
 }
 if(damage >= hitPoints) return strength + armyStrength * (faster ? .07 : .035);
 double fraction = std::min(double(damage) / hitPoints, 1.0);
 if(immovable) fraction += fraction - 1;
 return fraction * strength;
}
}
