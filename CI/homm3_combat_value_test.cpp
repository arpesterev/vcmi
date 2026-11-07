#include "../AI/Integration/Ports/HoMM3CombatValue.h"
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "../test/aiIntegration/homm3CombatValue/PinnedCombatValue.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <iostream>
#include <stdexcept>

namespace
{
int checks = 0;
void require(bool condition, const char * message)
{
	++checks;
	if(!condition)
		throw std::runtime_error(message);
}
}

int main()
{
	using namespace AIIntegration::Ports::HoMM3;
	using namespace PinnedCombatValue;
	combatManager combat;
	g_combatManager = &combat;
	// Each exclusion appears separately. Clone contributes source HP=1.
	combat.m_armies[0][1].flags = creatureSiegeWeapon;
	combat.m_armies[0][2].flags = creatureSummoned;
	combat.m_armies[0][3].flags = creatureImmobilized;
	combat.m_armies[0][4].flags = creatureClone;
	combat.m_armies[0][5].m_numTroops = 3;
	auto & unit = combat.m_armies[0][0];
	hero controller;
	unit.controller = &controller;
	for(int flags = 0; flags < 64; ++flags)
	for(int artillery = 0; artillery < 4; ++artillery)
	for(int ranged = 0; ranged < 2; ++ranged)
	for(int shoots = 0; shoots < 2; ++shoots)
	for(int alteration = 0; alteration < 3; ++alteration)
	{
		unit.flags = flags;
		unit.m_creatureType = artillery == 0 ? 0 : ARMY_CREATURE_BALLISTA;
		controller.level = artillery;
		unit.shooting = shoots;
		unit.attackModifier = 5;
		unit.defenseModifier = 10;
		unit.damageReduction = 0.75;
		unit.m_spellInfluence[41] = alteration == 1;
		unit.m_spellInfluence[42] = alteration == 2;
		unit.m_blessAmount = 1;
		unit.m_curseAmount = 1;
		unit.m_topCreatureDamage = 7;
		UnitCombatValueFacts facts;
		facts.attackDifference = 7;
		facts.defenseDifference = 9;
		facts.defenderDamageFactor = 0.75;
		facts.baseFightValue = 100;
		facts.ranged = ranged;
		facts.canShoot = shoots;
		facts.shooter = unit.is(creatureShootingArmy);
		facts.doubleRangedAttack = unit.is(creatureTwoAttacks);
		facts.ballista = unit.m_creatureType == ARMY_CREATURE_BALLISTA;
		facts.artilleryLevel = artillery;
		facts.alteredDamage = alteration != 0;
		facts.baseAverageDamage = 4;
		facts.averageDamage = unit.getAverageDamage();
		facts.siegeOrSummoned = unit.is(creatureSiegeWeapon | creatureSummoned);
		facts.totalHitPoints = unit.getTotalHitPoints(false);
		facts.ordinarySideHitPoints = 61;
		if(!unit.is(creatureSiegeWeapon | creatureSummoned | creatureImmobilized))
			facts.ordinarySideHitPoints += unit.getTotalHitPoints(false);
		const double expected = unit.getUnitCombatValue(-2, 1, ranged, nullptr);
		const double actual = unitCombatValue(facts);
		require(actual == expected, "Unit value differs from pinned reconstructed body");
		for(int damage : {0, 1, 12, 13, 19, 20, 32, 33, 193})
		for(bool killsOnly : {false, true})
			require(lossCombatValue(actual, 10, 20, 7, unit.is(creatureClone), damage, killsOnly)
				== unit.getLossCombatValue(-2, 1, ranged, damage, killsOnly),
				"Loss value differs from pinned reconstructed body");
	}
	require(lossCombatValue(100, 10, 20, 7, false, 12, false) == 60, "Below casualty threshold");
	require(lossCombatValue(100, 10, 20, 7, false, 13, false) == 100, "Threshold restores old wound");
	require(lossCombatValue(100, 10, 20, 7, false, 20, false) == 100, "Modulo does not restore old wound for a whole HP multiple");
	require(lossCombatValue(100, 10, 20, 7, false, 12, true) == 600, "killsOnly retains fractional unit weighting");
	require(lossCombatValue(100, 10, 20, 7, true, 0, true) == 200, "Clone precedes damage and killsOnly branches");
	require(lossCombatValue(100, 10, 20, 7, false, 26, false) == 130, "Complete exchange restores wound at most once");
	require(lossCombatValue(100, 10, 20, 7, false, 13, false) * 2 == 200, "Per-hit rating differs from complete exchange");
	UnitCombatValueFacts zero;
	zero.siegeOrSummoned = true;
	require(unitCombatValue(zero) == 0.1, "Source zero-health siege fallback");
	zero.totalHitPoints = 20;
	zero.baseFightValue = 100;
	require(unitCombatValue(zero) == 0, "Summons without ordinary friendly mass have no source value");
	require(lossCombatValue(1e20, INT64_MAX, 20, 7, true, 0, false) == INT32_MAX, "Native overflow saturates");
	std::cout << checks << " reconstructed combat-value checks passed\n";
}
