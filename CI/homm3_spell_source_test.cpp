#include "../AI/Integration/Ports/HoMM3SpellValue.h"

#include "../test/aiIntegration/homm3Curse/PinnedCurse.h"

#include "../test/aiIntegration/homm3DisruptingRay/PinnedDisruptingRay.h"

#include <limits>
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
	require(resistanceWeightedProjectedLoss(100, 100, 40, true, 50) == -10, "Wake-up penalty applies only when the spell lands");
	require(resistanceWeightedProjectedLoss(100, 100, 100, true, 0) == 0, "A fully resisted spell cannot wake a disabled enemy");
	require(resistanceWeightedProjectedLoss(100, 100, 3, false, 50) == 1.5, "Expected value preserves fractional damage contribution");
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
	require(resurrectionFitsCasualties(21, 20, 100, true), "Decided battle permits overcapacity resurrection below the casualty threshold");
	require(!resurrectionFitsCasualties(0, 20, 100, true), "Decided battle still needs a full healable creature");
	require(!resurrectionFitsCasualties(20, 0, 100, true), "Decided battle still needs dead creatures");
	require(!resurrectionFitsCasualties(21, 20, 100, false), "Undecided battle preserves overcapacity refusal");
	require(blindSpellValue(100, 200, 50, 0.5) == 50, "Blind on one of several awake enemies uses duration");
	require(blindSpellValue(100, 100, 0, 1) == 50, "Expert Blind on the last awake enemy is capped at half value");
	require(blindSpellValue(100, 100, 50, 1) < 15, "Basic Blind on the last enemy prices its retaliation");
	require(directCurseSpellValue(103, 4, 1, 0.5, 50) == 12,
		"Direct Curse truncates before duration and again before resistance");
	require(directCurseSpellValue(100, 2, 1, 1, 100) == 29,
		"Ordinary per-creature Curse ratio retains the source long result");
	require(directCurseSpellValue(100, 3, 1, 1, 100) == 42,
		"Existing Bless enters the old per-creature average");
	require(directCurseSpellValue(100, 2, 1, 0, 100) == 0,
		"Already completed one-turn Curse has no duration value");
	require(directCurseSpellValue(100, 2, 1, 1, 0) == 0,
		"Guaranteed resistance prevents direct Curse value");
	require(directCurseSpellValue(100, 1, 1, 1, 100) == 0,
		"Minimum-one Curse floor cannot reduce fixed-one damage");
	require(directCurseSpellValue(100, 2, 0, 1, 100) == 0,
		"Zero after-average is outside pinned Curse domain");
	require(directCurseSpellValue(static_cast<double>(INT32_MAX) + 1, 2, 1, 1, 100) == 0,
		"Native totals outside signed source long domain are excluded");
	require(directCurseContextSupported(50, 100), "Equality proves projected non-killsOnly at every difficulty");
	require(!directCurseContextSupported(49, 100), "Underdog exception remains unsupported");
	require(directCurseContextSupported(INT32_MAX / 2, INT32_MAX - 1), "Last safe signed-long doubling is admitted");
	require(!directCurseContextSupported(INT32_MAX / 2 + 1, 1), "Source signed doubling overflow is excluded");
	require(!directCurseContextSupported(1, static_cast<int64_t>(INT32_MAX) + 1), "Enemy source-total overflow is excluded");
	require(!directCurseContextSupported(0, 1) && !directCurseContextSupported(1, 0), "Empty sides do not justify Curse");
	for(int total : {1, 7, 103, 10000})
		for(int mastery : {0, 1, 2, 3})
			for(int rounds : {1, 3, 7})
				for(int turns : {1, 2, 6})
					for(bool done : {false, true})
						for(int chance : {0, 33, 50, 100})
						{
							PinnedCurse::type_AI_spellcaster oracle;
							oracle.rounds = rounds;
							PinnedCurse::manager.chance = chance / 100.0;
							PinnedCurse::army enemy{0, {2}, total, 3.5, done};
							const auto expected = oracle.getCurseValue(&enemy, {mastery, turns, true});
							require(directCurseSpellValue(total, 3.5, mastery >= 2 ? 1 : 2,
								spellDurationValue(turns, rounds, done), chance) == expected,
								"Direct Curse matches independent pinned function across integer/duration/resistance boundaries");
						}
	require(!directCursePlanningRounds(500000000, 500000000), "Awake low times five overflow is outside source domain");
	require(directCursePlanningRounds(INT32_MAX / 5, INT32_MAX / 5) == 7, "Last safe equal awake totals retain seven rounds");
	require(!directCursePlanningRounds(INT32_MAX / 5 + 1, INT32_MAX / 5 + 1), "First overflowing equal awake totals are rejected");
	require(directCursePlanningRounds(0, INT32_MAX) == 1, "A sleeping side has zero awake low without overflow");
	require(!directCursePlanningRounds(-1, 1) && !directCursePlanningRounds(1, static_cast<int64_t>(INT32_MAX) + 1), "Awake inputs fit nonnegative source longs");
	require(directCurseDamageRangeSupported(1, INT32_MAX - 1), "Last safe damage sum also permits one-point Bless");
	require(!directCurseDamageRangeSupported(1, INT32_MAX), "Damage sum overflow is rejected before averaging");
	require(!directCurseDamageRangeSupported(0, 1) && !directCurseDamageRangeSupported(2, 1), "Damage bounds retain positive ordered source domain");
	require(directCurseModifierDifference(INT32_MAX, 0) == INT32_MAX, "Last signed modifier difference is admitted");
	require(!directCurseModifierDifference(INT32_MAX, -1), "Signed modifier difference overflow is excluded");
	require(directCurseModifierDifference(-1, INT32_MIN) == INT32_MAX, "Negative source modifiers retain exact safe difference");
	require(!directCurseModifierDifference(0, INT32_MIN), "Cross-sign modifier overflow is excluded");
	require(!directCurseSignedValue(static_cast<int64_t>(INT32_MIN) - 1) && !directCurseSignedValue(static_cast<int64_t>(INT32_MAX) + 1), "Raw modifier projection fits source long");
	require(directCurseHealthSupported(INT32_MAX, 1, INT32_MAX), "Last safe raw hitpoint product is admitted");
	require(!directCurseHealthSupported(INT32_MAX / 2 + 1, 2, INT32_MAX), "Raw health multiplication cannot overflow before wound subtraction");
	require(!directCurseHealthSupported(2, 5, 11) && !directCurseHealthSupported(2, 5, 0), "Available health must belong to the positive source raw-health domain");
	for(int64_t low : {int64_t(0), int64_t(1), int64_t(INT32_MAX / 5)})
		for(int64_t high : {int64_t(1), int64_t(INT32_MAX / 5), int64_t(INT32_MAX)})
			if(low <= high)
			{
				require(directCursePlanningRounds(low, high) == PinnedCurse::planningRounds(low, high),
					"Accepted source horizon boundaries match exact pinned constructor tail");
				require(directCursePlanningRounds(high, low) == PinnedCurse::planningRounds(high, low),
					"Pinned constructor side exchange preserves bounded horizon");
			}
	for(int commonModifier : {-10, 0, 1, 20})
		for(int difference : {10, 40})
		{
			const auto mapped = directCurseModifierDifference(commonModifier + difference, commonModifier);
			require(mapped == difference, "Shared native stat modifiers cancel at the source minimum");
			require(directCurseContextSupported(100, static_cast<int>(100 * (1 + *mapped * 0.05)))
				== (difference == 10), "Shared native terrain modifiers preserve asymmetric Curse context admission");
		}

	// Differential checks preserve the original function's two integer casts.
	{
		namespace Ref = PinnedDisruptingRay;
		Ref::army enemy;
		Ref::type_AI_spellcaster caster;
		for(int side : {0, 1})
		for(int count : {0, 1, 7})
		for(int slot : {-1, 0, 6})
		for(bool killsOnly : {false, true})
		for(bool resistance : {false, true})
		for(double chance : {0.0, 0.01, 0.33, 0.5, 0.99, 1.0})
		for(int bonus : {0, 3, 4, 5, 20})
		for(int32_t total : {0, 1, 7, 13, 100, 1001, 1000000, INT32_MAX})
		{
			Ref::manager = {};
			Ref::manager.m_numArmies[side] = count;
			if(slot >= 0) Ref::manager.m_armies[side][slot].target = &enemy;
			Ref::manager.chance = chance;
			Ref::akSpellTraits[0].m_mastery_bonus[0] = bonus;
			enemy.total = total;
			caster.m_side = side;
			caster.m_estimate.m_killsOnly = killsOnly;
			const auto actual = directDisruptingRaySpellValue(total, bonus,
				slot >= 0 && slot < count, killsOnly, resistance, chance);
			require(actual && *actual == caster.getDisruptiveRayValue(&enemy, {0, resistance}),
				"Disrupting Ray matches pinned source gates, side census and truncation");
		}
		for(int64_t total : {int64_t(-1), int64_t(INT32_MAX) + 1, INT64_MAX})
			require(!directDisruptingRaySpellValue(total, 3, true, false, true, 1),
				"Unsupported Ray combat totals are unavailable, not overflowed");
		for(int bonus : {-1, 21, INT32_MAX})
			require(!directDisruptingRaySpellValue(100, bonus, true, false, true, 1),
				"Ray rejects nonreal or unsupported square-root domains");
		for(double chance : {-0.01, 1.01, std::numeric_limits<double>::infinity(),
			-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
		{
			require(!directDisruptingRaySpellValue(100, 3, true, false, true, chance),
				"Ray rejects invalid checked probabilities");
			require(directDisruptingRaySpellValue(100, 3, true, false, false, chance) == 7,
				"Unchecked resistance is not read by source valuation");
		}
		require(directDisruptingRaySpellValue(13, 3, true, false, true, 0.99) == 0,
			"Ray truncates raw value before multiplying by landing probability");
		require(directDisruptingRaySpellValue(INT64_MAX, 21, true, true, true, 2) == 0,
			"Kill-only short circuit does not evaluate valuation inputs");
		require(directDisruptingRaySpellValue(INT64_MAX, 21, false, false, true, 2) == 0,
			"Missing commitment short circuit does not evaluate valuation inputs");
	}
	std::cout << checks << " HoMM3 spell source projection checks passed\n";
}
