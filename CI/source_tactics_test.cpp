#include "../AI/Integration/Ports/SourceTactics.h"

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

	const FHeroes2::MeleeOutcome immediate{0, 1, 1, true};
	const FHeroes2::MeleeOutcome distant{1, 1000, 1000, false};
	require(FHeroes2::isOutcomeImproved(immediate, distant), "Immediate attacks must outrank distant opportunities");
	require(!FHeroes2::isOutcomeImproved(distant, immediate), "Distant high score must not replace immediate attack");

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

	try { selectTactics({}); return 1; } catch(const std::invalid_argument &) { ++checks; }
	try { rangedAttackValue({100, false, false, true, 0}, false); return 1; } catch(const std::invalid_argument &) { ++checks; }
	std::cout << "Source tactical predicate checks: " << checks << '\n';
}
