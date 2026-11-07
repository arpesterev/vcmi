/* GPL-2.0-or-later. Scalar source reference checks, not native battle parity. */
#include "../AI/Integration/Ports/FHeroes2MagicValue.h"
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
	std::cout << "F2_MAGIC_SOURCE_CHECKS=" << cases << " scope=scalar_strategic_families,antimagic_ratio\n";
}
