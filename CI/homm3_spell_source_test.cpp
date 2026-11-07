#include "../AI/Integration/Ports/HoMM3SpellValue.h"

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
	require(nativeHeroSpellMayReflect(false, true, true, 100), "Guaranteed mirror rejects a hostile direct hero cast");
	require(nativeHeroSpellMayReflect(false, true, true, 1), "Unmodeled partial reflection is conservatively excluded");
	require(!nativeHeroSpellMayReflect(false, true, true, 0), "Zero-chance mirror does not reject a direct spell");
	require(!nativeHeroSpellMayReflect(false, false, true, 100), "Area spells remain eligible against Magic Mirror");
	require(!nativeHeroSpellMayReflect(true, true, true, 100), "Mass spells remain eligible against Magic Mirror");
	require(!nativeHeroSpellMayReflect(false, true, false, 100), "Positive and neutral spells are not reflected");
	require(hostileSpellShouldAttackNow(true, false, false, false, true), "Source kill-only branch attacks immediately");
	require(hostileSpellShouldAttackNow(false, true, false, false, true), "Last friendly opportunity attacks immediately");
	require(hostileSpellShouldAttackNow(false, false, true, false, true), "Active melee target permits casting before retaliation");
	require(!hostileSpellShouldAttackNow(false, false, false, false, false), "Nonthreatening target cannot force an early cast");
	require(!hostileSpellShouldAttackNow(false, false, false, true, true), "Earlier friendly action preserves a later casting opportunity");
	require(hostileSpellShouldAttackNow(false, false, false, true, false), "Threat acting before every other ally permits an early cast");
	require(spellPlanningRounds(100, 100) == 7, "Equal armies retain the seven-round source horizon");
	require(spellPlanningRounds(260, 100) == 1, "Source first odds rung shortens the horizon");
	require(spellPlanningRounds(190, 100) == 2, "Source second odds rung is inclusive");
	require(spellPlanningRounds(150, 100) == 3, "Source third odds rung is inclusive");
	require(spellPlanningRounds(131, 100) == 4, "Source fourth odds rung is inclusive");
	require(spellPlanningRounds(120, 100) == 5, "Source fifth odds rung is inclusive");
	require(spellPlanningRounds(113, 100) == 6, "Source sixth odds rung is inclusive");
	require(spellPlanningRounds(0, 100) == 1, "No awake opposition has a one-round horizon");
	require(spellPlanningRounds(100, 260) == 1, "Horizon does not depend on attacker orientation");
	require(spellDurationValue(2, 4, false) == 0.5, "Short spell duration is proportional to the horizon");
	require(spellDurationValue(2, 4, true) == 0.25, "Already-moved stack loses one useful spell turn");
	require(spellDurationValue(8, 4, true) == 0.75, "Source moved penalty follows duration cap");
	require(spellDurationValue(1, 1, true) == 0, "One-turn effect cannot benefit an already-moved stack");
	require(spellDamageValue(100, 100, 20, false) == 20, "Native damage is valued against original stack value");
	require(spellDamageValue(100, 100, 20, true) == -60, "Waking a disabled enemy can have negative value");
	require(spellDamageValue(100, 100, 100, true) == 100, "Lethal damage has no wake-up penalty");
	require(spellDamageValue(100, 100, 150, false) == 100, "Overkill does not inflate spell value");
	require(spellDamageValue(100, 100, 0, true) == 0, "Unaffected disabled stack contributes nothing");
	require(nativeSpellSuccessPercent(true, true, 100) == 0, "Complete native resistance prevents hostile magical effects");
	require(nativeSpellSuccessPercent(true, true, 40) == 60, "Partial native resistance preserves its landing probability");
	require(nativeSpellSuccessPercent(true, true, 150) == 0, "Native resistance above 100 cannot create negative probability");
	require(nativeSpellSuccessPercent(true, true, -20) == 100, "Negative native resistance cannot exceed certain success");
	require(nativeSpellSuccessPercent(false, true, 100) == 100, "Positive and neutral spells bypass native resistance");
	require(nativeSpellSuccessPercent(true, false, 100) == 100, "Nonmagical effects bypass native resistance");
	require(resistanceWeightedProjectedLoss(100, 100, 40, false, 50) == 20, "Native capped loss is discounted by landing probability");
	require(resistanceWeightedProjectedLoss(100, 100, 400, false, 50) == 50, "Host preview caps overkill before resistance, unlike original source ordering");
	require(resistanceWeightedProjectedLoss(100, 100, 40, true, 50) == -60, "Wake-up penalty follows the discounted native loss");
	require(resistanceWeightedProjectedLoss(100, 100, 100, true, 0) == 0, "A fully resisted spell cannot wake a disabled enemy");
	require(resistanceWeightedProjectedLoss(100, 100, 3, false, 50) == 1, "Projected loss truncates fractional hit points");
	require(resistanceWeightedProjectedLoss(100, 100, INT64_MAX, false, 100) == 100, "Large native damage discounts without signed overflow");
	require(massSpellDamageValue(100, 40, 1000, 200) == 0,
		"Area spell with positive raw exchange still fails the proportional friendly-loss gate");
	require(massSpellDamageValue(100, 20, 1000, 200) == 0,
		"Equal proportional army loss is rejected");
	require(massSpellDamageValue(100, 10, 1000, 200) == 90,
		"Profitable proportional damage keeps its signed absolute exchange");
	require(massSpellDamageValue(100, -20, 1000, 200) == 100,
		"Waking an ally does not invent negative friendly casualties");
	require(massSpellDamageValue(300, 200, 1000, 200) == 0,
		"Destroying the complete friendly army is rejected");
	require(manaWeightedSpellValue(100, 14, 2) == 250, "Seven affordable casts use source 5/2 multiplier");
	require(manaWeightedSpellValue(100, 13, 2) == 100 * std::sqrt(6.0),
		"Source uses integer affordable-cast count before square root");
	require(manaWeightedSpellValue(100, 3, 2) == 100, "One affordable cast gets no mana premium");
	require(manaWeightedSpellValue(100, 1, 2) == 0, "Unaffordable spells are excluded");
	require(manaWeightedSpellValue(100, 0, 0) == 250, "Native free spells do not divide by zero");
	require(attackSpellValue(100, 10, 40, 100, 0.5) == 50,
		"Attack boost uses source square-root increase and duration");
	require(attackSpellValue(100, 10, 40, 10, 1) == 0,
		"Attack buff gets no benefit when the unbuffed hit already kills");
	require(curseSpellValue(100, 40, 10, 1) == 50, "Curse removal retains the source square-root decrease");
	require(removedAttackSpellValue(100, 10, 40, 100, 1) == 50,
		"Removing Curse with weaker attack buffs keeps the net improvement");
	require(removedAttackSpellValue(100, 40, 10, 100, 1) == -100,
		"Removing Curse with stronger attack buffs charges the net loss");
	require(removedAttackSpellValue(100, 10, 0, 100, 1) == -100,
		"Losing all attack damage is a cost, not a free dispel");
	require(removedAttackSpellValue(100, 10, 10, 100, 1) == 0,
		"Removal with no net attack change has zero value");
	require(removedAttackSpellValue(10000, 130, 125, 5000, 1)
		+ removedAttackSpellValue(500, 5, 6, 5000, 1) < 0,
		"Mass Dispel must charge a large mixed-effect loss before adding a small Curse-removal gain");
	require(resurrectionFitsCasualties(10, 20, 100), "Small resurrection may restore casualties below the three-quarter threshold");
	require(resurrectionFitsCasualties(20, 20, 100), "Exact-capacity restoration does not waste healing");
	require(!resurrectionFitsCasualties(21, 20, 100), "Overhealing small casualties waits without a predicted win");
	require(resurrectionFitsCasualties(100, 75, 100), "Heavy casualties permit a partially wasted resurrection");
	require(!resurrectionFitsCasualties(100, 74, 100), "Heavy-loss exception begins at the source integer threshold");
	require(resurrectionFitsCasualties(5, 2, 3), "Three-quarter source threshold retains integer truncation");
	require(!resurrectionFitsCasualties(0, 10, 100), "No full resurrected creature has no source restoration value");
	require(!resurrectionFitsCasualties(10, 0, 100), "Healing a wound without resurrecting creatures is not resurrection value");
	require(blindSpellValue(100, 200, 50, 0.5) == 50, "Blind on one of several awake enemies uses duration");
	require(blindSpellValue(100, 100, 0, 1) == 50, "Expert Blind on the last awake enemy is capped at half value");
	require(blindSpellValue(100, 100, 50, 1) < 15, "Basic Blind on the last enemy prices its retaliation");
	std::cout << checks << " HoMM3 spell source projection checks passed\n";
}
