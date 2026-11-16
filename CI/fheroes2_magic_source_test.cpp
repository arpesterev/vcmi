/* GPL-2.0-or-later. Scalar source reference checks, not native battle parity. */
#include "../AI/Integration/Ports/FHeroes2MagicValue.h"
#include <array>
#include "oracles/fheroes2_damage_resistance.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace AIIntegration::Ports::FHeroes2;

static void equal(double actual, double expected)
{
	if(std::abs(actual - expected) > 1e-9)
		throw std::runtime_error("FHeroes2 strategic/Anti-Magic scalar reference mismatch");
}

int main()
{
	int cases = 0;
 for(uint32_t raw : {0u, 1u, 10u, 20u, 100u, 1000u, 42949672u})
  for(uint32_t resistance = 0; resistance <= 100; ++resistance)
  {
   const auto damage = resistedDamage(raw, resistance);
   if(!damage || *damage != raw * (100 - resistance) / 100)
    throw std::runtime_error("Pinned uint damage discount mismatch");
   for(bool farewell : {false, true})
    for(bool asleep : {false, true})
    {
     double actual = 0;
     if(*damage)
     {
      if(farewell) actual = 10 * (10 - (*damage >= 100 ? 0 : (100 - *damage + 9) / 10));
      else if(*damage >= 100) actual = 107;
      else { double f = double(*damage) / 100; if(asleep) f += f - 1; actual = f * 100; }
     }
     equal(actual, FHeroes2PinnedOracle::value(raw, resistance, 100, 10, 10, 100, 200, false, asleep, farewell));
     ++cases;
    }
  }
 if(resistedDamage(-1, 0) || resistedDamage(4294967296LL, 50)
  || resistedDamage(4294967295LL, 0) || resistedDamage(1, -1) || resistedDamage(1, 101))
  throw std::runtime_error("Unsupported arithmetic was admitted");
 equal(FHeroes2PinnedOracle::value(1000, 50, 100, 10, 10, 100, 200, false, false, false), 107);
 equal(FHeroes2PinnedOracle::value(10, 50, 10, 1, 10, 100, 200, false, false, true), 0);
 equal(FHeroes2PinnedOracle::value(20, 50, 100, 10, 10, 100, 200, false, true, false), -80);
 equal(FHeroes2PinnedOracle::value(1, 50, 100, 10, 10, 100, 200, false, true, false), 0);

	for(int mana = 0; mana <= 400; ++mana)
		for(int cost : {0, 1, 7, 20, 50})
			for(auto family : {StrategicSpellFamily::DAMAGE, StrategicSpellFamily::HIGH_IMPACT,
				StrategicSpellFamily::SUMMON, StrategicSpellFamily::GENERIC})
			{
				const int casts = cost ? std::min(10, mana / cost) : 0;
				const double multiplier = casts == 1 ? 1 : casts - 0.05 * casts * casts;
				double expected = 0;
				if(casts)
				{
					if(family == StrategicSpellFamily::DAMAGE) expected = 230 * multiplier;
					if(family == StrategicSpellFamily::HIGH_IMPACT) expected = 170 * multiplier;
					if(family == StrategicSpellFamily::SUMMON) expected = 230;
					if(family == StrategicSpellFamily::GENERIC) expected = 68 * multiplier;
				}
				equal(strategicSpellValue(family, 1700, mana, cost, 230), expected);
				++cases;
			}
	for(double threat : {0., 199., 200., 201., 2000., 5000., 10000.})
		for(double army : {100., 2000., 20000.})
			for(int flags = 0; flags < 32; ++flags)
			{
				const bool positive = flags & 1, negative = flags & 2, resurrect = flags & 4;
				const bool gained = flags & 8, duration = flags & 16;
				double expected = 0;
				if(!positive && threat > 200 && gained && duration)
				{
					expected = threat / 200 * 0.036;
					if(expected > (resurrect ? 0.35 : 0.9)) expected = resurrect ? 0.35 : 0.9;
					if(threat > army) expected *= 1.5;
					if(negative) expected *= 2;
				}
				equal(antiMagicRatio(threat, army, positive, negative, resurrect, gained, duration), expected);
				++cases;
			}
	// Pinned spellResurrectValue: equal armies are not a winning battle;
	// temporary resurrection never receives the permanent recovery bonus.
	for(const auto & example : {std::array<double, 5>{100, 1000, 900, 1, 200},
		std::array<double, 5>{100, 900, 900, 1, 100},
		std::array<double, 5>{100, 800, 900, 1, 100},
		std::array<double, 5>{100, 1000, 900, 0, 100},
		std::array<double, 5>{0, 1000, 900, 1, 0}})
	{
		equal(resurrectionValue(example[0], example[1], example[2], example[3] != 0), example[4]);
		++cases;
	}
	if(resurrectionValue(100, 1000, 900, true) <= 150
		|| resurrectionValue(100, 1000, 900, false) >= 150)
		throw std::runtime_error("Permanent recovery must change the winning-battle spell ranking");
	++cases;
	std::cout << "F2_MAGIC_SOURCE_CHECKS=" << cases << " scope=scalar_strategic_families,antimagic_ratio,resurrection_value\n";
}
