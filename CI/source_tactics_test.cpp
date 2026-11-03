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


	// Pinned fheroes2 archerDecision friendly-fire cutoff is inclusive 3:1.
	require(!FHeroes2::catastrophicFriendlyFire(29, 10), "Area shot below 3:1 is eligible");
	require(FHeroes2::catastrophicFriendlyFire(30, 10), "Area shot at 3:1 is rejected");
	require(FHeroes2::catastrophicFriendlyFire(31, 10), "Area shot above 3:1 is rejected");
	require(!FHeroes2::catastrophicFriendlyFire(0, 10), "Direct shot with no allies remains eligible");
	try { FHeroes2::catastrophicFriendlyFire(-1, 10); return 1; }
	catch(const std::invalid_argument &) { ++checks; }
	try { FHeroes2::catastrophicFriendlyFire(std::numeric_limits<double>::infinity(), 10); return 1; }
	catch(const std::invalid_argument &) { ++checks; }

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
	require(FHeroes2::preferDistantTarget(nearArcher, tiedArcher), "Equal priority must use stable stack identity");
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
