#include "../AI/Integration/Ports/SourceTactics.h"

#include <array>
#include <iostream>
#include <string>

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
	using namespace AIIntegration::Ports;
	using HoMM3::rangedAttackValue;
	using HoMM3::preferEqualValueMeleeHex;
	using FHeroes2::selectTactics;

	for(int difficulty = 0; difficulty < 2; ++difficulty)
		for(int incapacitated = 0; incapacitated < 2; ++incapacitated)
			for(int cannotAttack = 0; cannotAttack < 2; ++cannotAttack)
				for(int hasTarget = 0; hasTarget < 2; ++hasTarget)
					for(int time = 1; time <= 7; ++time)
					{
						const HoMM3::RangedTarget target{1001, bool(incapacitated), bool(cannotAttack), bool(hasTarget), time};
						const int divisor = difficulty ? 1 : incapacitated ? 10 : !cannotAttack && hasTarget && time <= 5 ? time : 5;
						require(rangedAttackValue(target, difficulty) == 1001 / divisor, "HoMM3 target weighting changed");
					}

	// HoMM3 check_adjacent_hexes: on equal melee value and one-turn
	// reachability, Cavalier/Champion prefer the greater path cost.
	require(preferEqualValueMeleeHex(6, 4, true), "Jouster prefers a longer equal-value charge");
	require(!preferEqualValueMeleeHex(4, 6, true), "Jouster rejects a shorter equal-value charge");
	require(preferEqualValueMeleeHex(4, 6, false), "Regular troop prefers the cheaper equal-value position");
	require(!preferEqualValueMeleeHex(6, 4, false), "Regular troop rejects an expensive equal-value position");
	require(!preferEqualValueMeleeHex(4, 4, true), "Equal cost must keep a stable choice");
	require(!preferEqualValueMeleeHex(4, 4, false), "Equal cost must keep a stable non-jouster choice");
	try { preferEqualValueMeleeHex(-1, 4, true); return 1; }
	catch(const std::invalid_argument &) { ++checks; }
	try { preferEqualValueMeleeHex(4, -1, false); return 1; }
	catch(const std::invalid_argument &) { ++checks; }

	// Source getHexAttackValue scales the ranged-versus-melee combat-value
	// difference by surviving unit-equivalents and never gives less than 1
	// for a live eligible shooter. Native damage projection is separate.
	require(HoMM3::pinnedShooterValue(10, 4, 35, 10) == 21,
		"HoMM3 pinned shooter value must scale by surviving health");
	require(HoMM3::pinnedShooterValue(10, 4, 37, 10) == 22,
		"HoMM3 pinned shooter value truncates toward zero like source long cast");
	require(HoMM3::pinnedShooterValue(4, 10, 35, 10) == 1,
		"HoMM3 scorer floors nonpositive live shooter contribution at one");
	require(HoMM3::pinnedShooterValue(10, 4, 0, 10) == 0,
		"Dead shooter cannot receive suppression credit");
	require(HoMM3::pinnedShooterValue(1e20, 0, 10, 1) == std::numeric_limits<int32_t>::max(),
		"Native source projection must not overflow its signed score");
	try { HoMM3::pinnedShooterValue(10, 4, 10, 0); return 1; }
	catch(const std::invalid_argument &) { ++checks; }
	try { HoMM3::pinnedShooterValue(std::numeric_limits<double>::infinity(), 4, 10, 1); return 1; }
	catch(const std::invalid_argument &) { ++checks; }

	// Original getMultiHeadBonus counts additional enemies; getBreathBonus
	// also subtracts damage to allies. The host engine resolves which
	// stacks are actually struck from a given native attack hex.
	require(HoMM3::collateralStrikeValue(true, 21.5) == 21.5,
		"HoMM3 multi-head collateral must reward enemy damage");
	require(HoMM3::collateralStrikeValue(false, 21.5) == -21.5,
		"HoMM3 breath collateral must penalize friendly fire");
	require(HoMM3::collateralStrikeValue(true, 0) == 0,
		"Zero secondary damage must not create bonus");
	try { HoMM3::collateralStrikeValue(true, -1); return 1; }
	catch(const std::invalid_argument &) { ++checks; }
	try { HoMM3::collateralStrikeValue(false, std::numeric_limits<double>::infinity()); return 1; }
	catch(const std::invalid_argument &) { ++checks; }

	// HoMM3 checkAdjacentHexes evaluates BOTH attacker's occupied hexes and
	// VCMI free shooting (e.g. Sharpshooter's Bow) can survive
	// adjacent contact. The original source valuation is not reused when
	// native rules prove the intended suppression does not occur.
	require(HoMM3::nativeShooterSuppressionPossible(true, false, true),
		"Ordinary shooter loses its shot when contacted");
	require(!HoMM3::nativeShooterSuppressionPossible(true, true, true),
		"Unrestricted free shooter does not lose its shot to a pin");
	require(HoMM3::nativeShooterSuppressionPossible(true, true, false),
		"Restricted free shooter cannot shoot adjacent targets");
	require(!HoMM3::nativeShooterSuppressionPossible(false, false, false),
		"Unavailable shot cannot receive a fictional pin bonus");

	// shares its visited-shooter mask. Physical contact on a wide tail must
	// count, while a nonadjacent head alone does not. A wide shooter may also
	// be engaged at its tail; its two cells never imply two separate shooters.
	const auto linearDistance = [](int lhs, int rhs) { return std::abs(lhs - rhs); };
	require(HoMM3::attackFootprintEngagesShooter(std::array<int, 2>{2, 5},
		std::array<int, 1>{6}, linearDistance),
		"HoMM3 wide attacker must count shooter blocked by its trailing hex");
	require(!HoMM3::attackFootprintEngagesShooter(std::array<int, 1>{2},
		std::array<int, 1>{6}, linearDistance),
		"HoMM3 head-only contact cannot invent shooter engagement");
	require(HoMM3::attackFootprintEngagesShooter(std::array<int, 1>{5},
		std::array<int, 2>{6, 7}, linearDistance),
		"HoMM3 shooter tail contact must count");
	require(!HoMM3::attackFootprintEngagesShooter(std::array<int, 2>{2, 3},
		std::array<int, 2>{6, 7}, linearDistance),
		"HoMM3 distant footprints do not pin shooters");
	require(!HoMM3::attackFootprintEngagesShooter(std::array<int, 0>{},
		std::array<int, 1>{6}, linearDistance),
		"HoMM3 empty attack footprint cannot pin a shooter");
	require(!HoMM3::attackFootprintEngagesShooter(std::array<int, 2>{2, 5},
		std::array<int, 0>{}, linearDistance),
		"HoMM3 empty enemy footprint cannot generate score");

	// Pinned checkAdjacentHexes uses precomputed enemy_attacks[hex]; the
	// VCMI approximation must not invent a shot at an empty future cell.
	require(!HoMM3::projectedNativeRangedThreat(true, true, false, true, false, 0, 1),
		"Ordinary shooter cannot fire through new melee contact");
	require(HoMM3::projectedNativeRangedThreat(true, true, true, true, false, 0, 1),
		"Unrestricted free shooting remains dangerous when engaged");
	require(!HoMM3::projectedNativeRangedThreat(true, true, true, false, false, 0, 1),
		"Restricted free shooter cannot shoot an adjacent target");
	require(!HoMM3::projectedNativeRangedThreat(false, false, true, true, false, 0, 2),
		"A currently disabled shooter must not invent a shot");
	require(HoMM3::projectedNativeRangedThreat(true, false, false, false, true, 3, 3),
		"Native limited shooting range includes its exact boundary");
	require(!HoMM3::projectedNativeRangedThreat(true, false, false, false, true, 3, 4),
		"Native limited-range shooter cannot threaten beyond its range");
	require(HoMM3::projectedNativeRangedThreat(true, false, false, false, false, 0, 4),
		"Unlimited normal shooting may threaten a distant cell");
	try { HoMM3::projectedNativeRangedThreat(true, false, false, false, true, -1, 2); return 1; }
	catch(const std::invalid_argument &) { ++checks; }
	try { HoMM3::projectedNativeRangedThreat(true, false, false, false, false, 0, -1); return 1; }
	catch(const std::invalid_argument &) { ++checks; }

	// Source checkAdjacentHexes combines a wide stack's per-cell threats
	// using min, so the more exposed cell dominates, not their average.
	require(HoMM3::moreExposedFootprintThreat(-50, -10) == -50,
		"HoMM3 wide attacker must retain head-cell risk");
	require(HoMM3::moreExposedFootprintThreat(-5, -70) == -70,
		"HoMM3 wide attacker must retain tail-cell risk");
	require(HoMM3::moreExposedFootprintThreat(0, 0) == 0,
		"HoMM3 empty threat must be neutral");
	try { HoMM3::moreExposedFootprintThreat(std::numeric_limits<double>::quiet_NaN(), -1); return 1; }
	catch(const std::invalid_argument &) { ++checks; }

	FHeroes2::TacticsInput input{100, 100, 30, 0, true, false, false};
	require(selectTactics(input).defensive, "Protect the ranged advantage");
	input.shootersStrength = 0;
	require(!selectTactics(input).defensive, "Infantry without shooters should advance");
	input.defendingCastle = true;
	require(selectTactics(input).defensive, "Siege towers justify defence");
	input.insideDefendedArea = false;
	require(!selectTactics(input).defensive, "A unit across the line should continue attacking");
	input = {100, 100, 80, 70, true, false, false};
	require(!selectTactics(input).defensive, "Attack an exposed mostly-ranged enemy army");
	input = {1000, 100, 300, 0, true, false, false};
	require(selectTactics(input).defensive, "The overpower boundary is strict");
	input.armyStrength = 1001;
	require(!selectTactics(input).defensive, "Overwhelming army should advance");
	input = {600, 100, 300, 0, true, true, false};
	require(selectTactics(input).defensive, "Flying overpower boundary is strict");
	input.armyStrength = 601;
	require(!selectTactics(input).defensive, "Overwhelming flying army should advance");


	// fheroes2 BattlePlanner's Haste/Slow valuations use real projected
	// speed changes, target type, relative army speed and distance. An
	// already-active buff with unchanged speed is worth zero.
	require(FHeroes2::hasteSpeedRatio(4, 6, 7, false, false, false) == 0.2,
		"Haste doubles value for slow allied melee relative to enemy speed");
	require(FHeroes2::hasteSpeedRatio(4, 6, 4, false, true, false) == 0.05,
		"Haste is worth less when a shooter does not need movement");
	require(FHeroes2::hasteSpeedRatio(4, 6, 4, true, true, false) == 0.2,
		"Haste counteracts an existing Slow before shooter discount");
	require(FHeroes2::hasteSpeedRatio(4, 4, 7, true, false, false) == 0,
		"Repeated Haste with no actual speed gain has no value");
	require(FHeroes2::slowSpeedRatio(8, 4, 5, false, false, false, false, 2) == 0.2,
		"Slow values loss of mobility reduced by distance");
	require(FHeroes2::slowSpeedRatio(8, 4, 5, true, false, false, false, 2) == 0.01,
		"Source Slow special case for archers uses a small fixed value");
	require(FHeroes2::slowSpeedRatio(8, 4, 5, false, false, true, false, 2) == 0.8,
		"Slow that cancels Haste restores twice the tactical value");
	require(FHeroes2::slowSpeedRatio(8, 8, 5, false, false, false, false, 2) == 0,
		"Repeated Slow with no actual change has no value");
	try { FHeroes2::hasteSpeedRatio(-1, 4, 5, false, false, false); return 1; }
	catch(const std::invalid_argument &) { ++checks; }
	try { FHeroes2::slowSpeedRatio(4, 2, 5, false, false, false, false, 0); return 1; }
	catch(const std::invalid_argument &) { ++checks; }

	// fheroes2 spellEffectValue: Bless/Curse are 15% of strength,
	// doubled when cancelling the opposite effect. A fixed-damage unit,
	// resisted cast or an unchanged damage range must not score value.
	require(FHeroes2::blessCurseDamageRatio(1, 3, false, true) == 0.15,
		"Bless and Curse assign fifteen percent to a varying damage range");
	require(FHeroes2::blessCurseDamageRatio(1, 3, true, true) == 0.3,
		"Counteracting opposite damage magic doubles the spell value");
	require(FHeroes2::blessCurseDamageRatio(2, 2, true, true) == 0,
		"A fixed-damage creature gets no Bless or Curse source value");
	require(FHeroes2::blessCurseDamageRatio(1, 3, true, false) == 0,
		"VCMI immunity, already-active effect or native no-op has no value");
	try { FHeroes2::blessCurseDamageRatio(-1, 3, false, true); return 1; }
	catch(const std::invalid_argument &) { ++checks; }
	try { FHeroes2::blessCurseDamageRatio(3, 2, false, true); return 1; }
	catch(const std::invalid_argument &) { ++checks; }

	// Pinned fheroes2 spellEffectValue values Stone Skin at 10% only
	// when the VCMI simulation actually increases the target's defense.
	require(FHeroes2::stoneSkinDefenseRatio(4, 7) == 0.1,
		"Stone Skin values an observed native defense increase");
	require(FHeroes2::stoneSkinDefenseRatio(4, 4) == 0,
		"Already-active or resisted Stone Skin must not consume mana for no benefit");
	require(FHeroes2::stoneSkinDefenseRatio(7, 4) == 0,
		"Lower defense cannot count as a successful Stone Skin buff");
	try { FHeroes2::stoneSkinDefenseRatio(-1, 4); return 1; }
	catch(const std::invalid_argument &) { ++checks; }

	// Pinned fheroes2 spellEffectValue values Bloodlust at 10% of
	// stack strength only if a real cast increases native melee attack.
	require(FHeroes2::bloodlustAttackRatio(4, 7) == 0.1,
		"Bloodlust rewards a native melee attack increase");
	require(FHeroes2::bloodlustAttackRatio(4, 4) == 0,
		"Already-active or resisted Bloodlust must have no value");
	require(FHeroes2::bloodlustAttackRatio(7, 4) == 0,
		"Decreased attack does not count as a Bloodlust buff");
	try { FHeroes2::bloodlustAttackRatio(-1, 4); return 1; }
	catch(const std::invalid_argument &) { ++checks; }

	// Heroes II Shield weights 30% by enemy ranged strength, with a 25%
	// bonus to friendly archers. VCMI uses AIR_SHIELD for ranged protection.
	require(FHeroes2::shieldRangedRatio(100, 100, false, true) == 0.3,
		"Ranged Shield base ratio against a wholly ranged army");
	require(FHeroes2::shieldRangedRatio(50, 100, true, true) == 0.1875,
		"Friendly shooters receive the original Shield priority increase");
	require(FHeroes2::shieldRangedRatio(0, 100, true, true) == 0,
		"No ranged opponents means no ranged Shield benefit");
	require(FHeroes2::shieldRangedRatio(100, 100, true, false) == 0,
		"Already-active or resisted Shield cannot be valued");
	try { FHeroes2::shieldRangedRatio(110, 100, false, true); return 1; }
	catch(const std::invalid_argument &) { ++checks; }
	try { FHeroes2::shieldRangedRatio(-1, 100, false, true); return 1; }
	catch(const std::invalid_argument &) { ++checks; }

	// Pinned fheroes2 Hypnotize is 150% of target strength, contingent
	// on an actual new control transfer rather than spell eligibility.
	require(FHeroes2::hypnotizeControlRatio(false, true) == 1.5,
		"Hypnotize values a successfully controlled enemy");
	require(FHeroes2::hypnotizeControlRatio(false, false) == 0,
		"Resisted or repeated Hypnotize cannot be scored");
	require(FHeroes2::hypnotizeControlRatio(true, true) == 0,
		"An already disabled enemy cannot earn a fresh control valuation");

	// Pinned fheroes2 Blind ratios: 80% except the last enemy,
	// which needs an available, limited retaliation for 40%.
	// A resisted Blind or an enemy disabled by another spell scores zero.
	require(FHeroes2::blindControlRatio(false, false, false, false, true) == 0.8,
		"Blind values control of an enemy when other enemies remain");
	require(FHeroes2::blindControlRatio(true, false, true, false, true) == 0.4,
		"Blind values the last enemy at the source's reduced ratio");
	require(FHeroes2::blindControlRatio(true, true, true, false, true) == 0,
		"Blind must not value last-enemy unlimited retaliation");
	require(FHeroes2::blindControlRatio(true, false, false, false, true) == 0,
		"Blind must not value last-enemy spent retaliation");
	require(FHeroes2::blindControlRatio(false, false, true, true, true) == 0,
		"Do not value Blind over a pre-existing disabling effect");
	require(FHeroes2::blindControlRatio(false, false, true, false, false) == 0,
		"Blind must not value resisted or already-active native effect");

	// Pinned getSpellDisruptingRayRatio: full value for three points,
	// partial value at the minimum defense boundary, reduced valuation
	// against a larger enemy and no value without an actual native change.
	require(FHeroes2::disruptingRayDefenseRatio(6, 3, 200, 100) == 0.2,
		"Disrupting Ray values the full three-point defense reduction");
	require(std::fabs(FHeroes2::disruptingRayDefenseRatio(2, 1, 100, 100) - 0.2 / 3) < 1e-9,
		"Disrupting Ray cannot reduce defense below one");
	require(FHeroes2::disruptingRayDefenseRatio(6, 3, 50, 100) == 0.1,
		"Weaker army discounts a spell against an oversized target");
	require(FHeroes2::disruptingRayDefenseRatio(1, 1, 100, 100) == 0,
		"Minimum defense has no further Disrupting Ray value");
	require(FHeroes2::disruptingRayDefenseRatio(6, 6, 100, 100) == 0,
		"Resisted or repeated no-op Disrupting Ray has no value");
	try { FHeroes2::disruptingRayDefenseRatio(-1, 0, 100, 100); return 1; }
	catch(const std::invalid_argument &) { ++checks; }

	// Pinned fheroes2 archerDecision friendly-fire cutoff is inclusive 3:1.
	require(!FHeroes2::catastrophicFriendlyFire(29, 10), "Area shot below 3:1 is eligible");
	require(FHeroes2::catastrophicFriendlyFire(30, 10), "Area shot at 3:1 is rejected");
	require(FHeroes2::catastrophicFriendlyFire(31, 10), "Area shot above 3:1 is rejected");
	require(!FHeroes2::catastrophicFriendlyFire(0, 10), "Direct shot with no allies remains eligible");
	try { FHeroes2::catastrophicFriendlyFire(-1, 10); return 1; }
	catch(const std::invalid_argument &) { ++checks; }
	try { FHeroes2::catastrophicFriendlyFire(std::numeric_limits<double>::infinity(), 10); return 1; }
	catch(const std::invalid_argument &) { ++checks; }

	// Pinned Mirror Image valuation from fheroes2 spellEffectValue:
	// 100% for shooters, 55% for flyers, 33% for other creatures,
	// and fivefold discount below the opposing average speed.
	require(FHeroes2::mirrorImageCloneRatio(true, false, 6, 5, true) == 1.0,
		"Ranged Mirror Image keeps full source strength");
	require(FHeroes2::mirrorImageCloneRatio(false, true, 6, 5, true) == 0.55,
		"Flying Mirror Image uses source's 55% ratio");
	require(FHeroes2::mirrorImageCloneRatio(false, false, 6, 5, true) == 0.33,
		"Ground Mirror Image uses source's 33% ratio");
	require(FHeroes2::mirrorImageCloneRatio(true, false, 4, 5, true) == 0.2,
		"Slow Mirror Image shooter is discounted fivefold");
	require(FHeroes2::mirrorImageCloneRatio(false, true, 4, 5, false) == 0,
		"No simulated friendly clone must never earn source value");
	try { FHeroes2::mirrorImageCloneRatio(false, false, -1, 2, true); return 1; }
	catch(const std::invalid_argument &) { ++checks; }
	try { FHeroes2::mirrorImageCloneRatio(false, false, 2,
		std::numeric_limits<double>::infinity(), true); return 1; }
	catch(const std::invalid_argument &) { ++checks; }

	// fheroes2's defended-area test rejects a wide unit straddling the
	// border, even when its head remains on the defended side. Also reject
	// an empty/invalid footprint; the actual castle geometry is supplied
	// by VCMI and tested in the native siege below.
	const auto isDefendedCell = [](bool defended) { return defended; };
	require(FHeroes2::isPositionInDefendedArea(std::array<bool, 1>{true}, isDefendedCell),
		"Single sheltered fheroes2 unit can defend");
	require(!FHeroes2::isPositionInDefendedArea(std::array<bool, 1>{false}, isDefendedCell),
		"Single exposed fheroes2 unit should attack");
	require(FHeroes2::isPositionInDefendedArea(std::array<bool, 2>{true, true}, isDefendedCell),
		"Both occupied cells of wide fheroes2 defender are sheltered");
	require(!FHeroes2::isPositionInDefendedArea(std::array<bool, 2>{true, false}, isDefendedCell),
		"Exposed wide tail invalidates fheroes2 defensive stance");
	require(!FHeroes2::isPositionInDefendedArea(std::array<bool, 2>{false, true}, isDefendedCell),
		"Exposed wide head invalidates fheroes2 defensive stance");
	require(!FHeroes2::isPositionInDefendedArea(std::array<bool, 0>{}, isDefendedCell),
		"Empty footprint cannot justify fheroes2 defense");

	const FHeroes2::MeleeOutcome immediate{0, 1, 1, true};
	const FHeroes2::MeleeOutcome distant{1, 1000, 1000, false};
	require(FHeroes2::isOutcomeImproved(immediate, distant), "Immediate attacks must outrank distant opportunities");
	require(!FHeroes2::isOutcomeImproved(distant, immediate), "Distant high score must not replace immediate attack");

	// Source meleeUnitOffense: non-evasive targets get the first pass,
	// then the threat/path-distance ratio decides among that tier.
	const FHeroes2::DistantTarget none{};
	const FHeroes2::DistantTarget farArcher{100, 10, true, 2};
	const FHeroes2::DistantTarget nearFastDecoy{1000, 2, false, 1};
	const FHeroes2::DistantTarget nearArcher{50, 2, true, 3};
	const FHeroes2::DistantTarget tiedArcher{100, 4, true, 4};
	require(FHeroes2::preferDistantTarget(farArcher, none), "First distant target must be accepted");
	require(FHeroes2::preferDistantTarget(farArcher, nearFastDecoy), "Prefer engagement over high-value evasive bait");
	require(!FHeroes2::preferDistantTarget(nearFastDecoy, farArcher), "Fast evasive bait must not override an archer");
	require(FHeroes2::preferDistantTarget(nearArcher, farArcher), "Threat / path distance must select better target");
	require(!FHeroes2::preferDistantTarget(farArcher, nearArcher), "Longer route loses at lower normalized threat");
	// Pinned meleeUnitOffense uses "if (priority < maxPriority) continue":
	// equal-ranked later candidates replace the earlier choice. The test
	// checks both visitation orders, not an invented low-unit-ID preference.
	require(FHeroes2::preferDistantTarget(nearArcher, tiedArcher),
		"Equal-scoring later candidate replaces an earlier higher-ID target");
	require(FHeroes2::preferDistantTarget(tiedArcher, nearArcher),
		"Equal-scoring later candidate replaces an earlier lower-ID target");
	require(!FHeroes2::preferDistantTarget(farArcher, tiedArcher),
		"Strictly weaker distant candidate must not override the winner");
	try { FHeroes2::preferDistantTarget({1, 0, true, 1}, farArcher); return 1; }
	catch(const std::invalid_argument &) { ++checks; }
	try { FHeroes2::preferDistantTarget({std::numeric_limits<double>::infinity(), 1, true, 1}, farArcher); return 1; }
	catch(const std::invalid_argument &) { ++checks; }

	TUM::ShooterProtection cover{false, false, true, true, false, false};
	require(TUM::holdMeleeForShooters(cover), "Hold melee while shooters attack neutral melee");
	cover.opponentCanReachAnyFriendlyStack = true;
	require(!TUM::holdMeleeForShooters(cover), "Counterattack once neutrals can reach the line");
	cover.opponentCanReachAnyFriendlyStack = false;
	cover.opponentHasShooters = true;
	require(!TUM::holdMeleeForShooters(cover), "Do not wait against neutral shooters");
	cover.opponentHasShooters = false;
	cover.opponentIsNeutral = false;
	require(!TUM::holdMeleeForShooters(cover), "Do not apply neutral-only rule to an enemy hero");
	for(int effects = 0; effects < 8; ++effects)
		require(TUM::ballistaMayTarget(effects & 1, effects & 2, effects & 4) == (effects == 0), "Do not wake disabled targets with a ballista");
	require(!TUM::creatureMayApplyEffect(true), "Do not recast existing effect");
	require(TUM::creatureMayApplyEffect(false), "Allow a new effect");
	// Siege hold is invalidated by a breach in either occupied cell of a
	// double-wide enemy. A head-only check would miss the second case.
	const auto insideWalls = [](bool inside) { return inside; };
	require(!TUM::footprintBreachesCastle(std::array<bool, 1>{false}, insideWalls),
		"Outside single-cell enemy has not breached");
	require(TUM::footprintBreachesCastle(std::array<bool, 1>{true}, insideWalls),
		"Inside single-cell enemy has breached");
	require(!TUM::footprintBreachesCastle(std::array<bool, 2>{false, false}, insideWalls),
		"Both cells of wide enemy remain outside");
	require(TUM::footprintBreachesCastle(std::array<bool, 2>{true, false}, insideWalls),
		"Wide enemy head inside castle invalidates hold");
	require(TUM::footprintBreachesCastle(std::array<bool, 2>{false, true}, insideWalls),
		"Wide enemy tail inside castle invalidates hold");

	// The mirrored condition applies to wide *defenders*: one exposed
	// cell invalidates the fortified hold even if the head is sheltered.
	require(TUM::footprintFullyInsideCastle(std::array<bool, 1>{true}, insideWalls),
		"Sheltered single-cell defender can hold");
	require(!TUM::footprintFullyInsideCastle(std::array<bool, 1>{false}, insideWalls),
		"Exposed single-cell defender cannot hold");
	require(TUM::footprintFullyInsideCastle(std::array<bool, 2>{true, true}, insideWalls),
		"Wide defender wholly behind walls can hold");
	require(!TUM::footprintFullyInsideCastle(std::array<bool, 2>{false, true}, insideWalls),
		"Wide defender outside head invalidates hold");
	require(!TUM::footprintFullyInsideCastle(std::array<bool, 2>{true, false}, insideWalls),
		"Wide defender exposed tail invalidates hold");
	require(!TUM::footprintFullyInsideCastle(std::array<bool, 0>{}, insideWalls),
		"Empty or invalid footprint cannot justify a fortified hold");

	// TUM Hypnotize uses damage dealt PLUS retaliation received, unlike
	// the net-exchange score of an ordinary combatant.
	require(TUM::hypnotizedAttackDamageSum(10, 25) == 35,
		"Hypnotized attacker values both directions of melee damage");
	require(TUM::hypnotizedAttackDamageSum(12, 0) == 12,
		"Hypnotized ranged shot does not invent retaliation");
	require(TUM::hypnotizedAttackDamageSum(10, 30) > TUM::hypnotizedAttackDamageSum(20, 5),
		"Retaliation can reverse the preferred TUM Hypnotize target");
	try { TUM::hypnotizedAttackDamageSum(-1, 2); return 1; }
	catch(const std::invalid_argument &) { ++checks; }
	try { TUM::hypnotizedAttackDamageSum(std::numeric_limits<double>::infinity(), 2); return 1; }
	catch(const std::invalid_argument &) { ++checks; }

	// In the live VCMI projection, a fast double-wide decoy may be near
	// by its tail, even if its head is just outside the two-turn radius.
	const auto decoyFootprintDistance = [](int first, int second) { return std::abs(first - second); };
	const int wideDistance = TUM::nearestFootprintDistance(
		std::array<int, 1>{0}, std::array<int, 2>{10, 9}, decoyFootprintDistance);
	require(wideDistance == 9, "Decoy distance must use the nearest occupied cell");
	require(!TUM::ignoreNearbyFastDecoyForMovement(4, 7, 3, 10),
		"Wide enemy head alone would miss the guarded radius");
	require(TUM::ignoreNearbyFastDecoyForMovement(4, 7, 3, wideDistance),
		"Wide enemy tail inside the radius must count as nearby");
	require(TUM::nearestFootprintDistance(std::array<int, 2>{0, 1},
		std::array<int, 2>{10, 11}, decoyFootprintDistance) == 9,
		"Wide acting stack must also use both occupied cells");
	require(TUM::nearestFootprintDistance(std::array<int, 0>{},
		std::array<int, 1>{5}, decoyFootprintDistance) == std::numeric_limits<int>::max(),
		"Invalid empty footprint must not create a nearby target");

	// The published fast-decoy fix has no recoverable scoring code.
	// Pin the conservative movement-only predicate and its boundary cases.
	require(TUM::ignoreNearbyFastDecoyForMovement(4, 7, 3, 6),
		"Ignore a fast nearby bait when a slower opponent is available");
	require(TUM::ignoreNearbyFastDecoyForMovement(4, 6, 4, 9),
		"Ignore an eligible decoy at the inclusive distance boundary");
	require(!TUM::ignoreNearbyFastDecoyForMovement(4, 7, 3, 10),
		"Distant fast foes are not treated as nearby bait");
	require(!TUM::ignoreNearbyFastDecoyForMovement(4, 5, 3, 4),
		"A one-speed advantage alone is not enough to ignore a target");
	require(!TUM::ignoreNearbyFastDecoyForMovement(4, 7, 5, 4),
		"Do not ignore the only enemy that can be caught by a slow force");
	require(!TUM::ignoreNearbyFastDecoyForMovement(4, 7, 7, 4),
		"Do not ignore equally fast targets without a slower alternative");
	require(!TUM::ignoreNearbyFastDecoyForMovement(4, 7, 3, -1),
		"Reject invalid distance rather than ignoring a target");
	require(!TUM::ignoreNearbyFastDecoyForMovement(0, 7, 3, 1),
		"No mobility implies no fast-decoy movement policy");

	try { selectTactics({}); return 1; } catch(const std::invalid_argument &) { ++checks; }
	try { rangedAttackValue({100, false, false, true, 0}, false); return 1; } catch(const std::invalid_argument &) { ++checks; }
	std::cout << "Source tactical predicate checks: " << checks << '\n';
}
