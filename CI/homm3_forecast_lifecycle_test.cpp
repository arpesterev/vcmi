// SPDX-License-Identifier: CC0-1.0
#include "../test/aiIntegration/homm3Forecast/Lifecycle.h"
#include <iostream>
#include <stdexcept>

using namespace HoMM3Forecast;
int checks = 0;
void check(bool value) { ++checks; if(!value) throw std::runtime_error("lifecycle assertion failed"); }
template<typename F> void rejected(F operation)
{
	bool failed = false;
	try { operation(); } catch(const std::exception &) { failed = true; }
	check(failed);
}
int main()
{
	RoundPhase phase;
	check(!phase.secondPhase(1));
	phase.attach(0);
	check(!phase.secondPhase(0));
	phase.newRound(1);
	check(phase.secondPhase(1) == false);
	rejected([&] { phase.newRound(1); });
	rejected([&] { phase.newRound(0); });
	for(int round = 2; round < 102; ++round)
	{
		phase.newRound(round);
		check(phase.secondPhase(round) == false);
		for(int normal = 0; normal < 7; ++normal)
		{
			phase.beforeUnitActionOrOwnActivation(round, false);
			check(phase.secondPhase(round) == false);
		}
		phase.beforeUnitActionOrOwnActivation(round, true);
		check(phase.secondPhase(round) == true);
		for(int moraleOrNormal = 0; moraleOrNormal < 7; ++moraleOrNormal)
		{
			phase.beforeUnitActionOrOwnActivation(round, false);
			check(phase.secondPhase(round) == true);
		}
		phase.attach(round);
		check(!phase.secondPhase(round));
		phase.beforeUnitActionOrOwnActivation(round, false);
		check(!phase.secondPhase(round));
		phase.beforeUnitActionOrOwnActivation(round, true);
		check(phase.secondPhase(round) == true);
	}
	phase.beforeUnitActionOrOwnActivation(105, false);
	check(!phase.secondPhase(105));
	check(!phase.secondPhase(101));
	phase.beforeUnitActionOrOwnActivation(105, true);
	check(phase.secondPhase(105) == true);
	phase.end();
	check(!phase.secondPhase(105));
	InitialRoster roster;
	std::vector<InitialIdentity> units{{9, Side::DEFENDER, 6}, {4, Side::ATTACKER, 5}, {2, Side::ATTACKER, 1}};
	roster.bind(units);
	check(roster.sourceIndex(2) == 0);
	check(roster.sourceIndex(4) == 1);
	check(roster.sourceIndex(9) == 0);
	std::reverse(units.begin(), units.end());
	roster.validate(units);
	rejected([&] { auto missing = units; missing.erase(missing.begin()); roster.validate(missing); });
	rejected([&] { auto replacement = units; replacement[0].id = 99; roster.validate(replacement); });
	rejected([&] { auto changedOwner = units; changedOwner[0].owningSide = Side::DEFENDER; roster.validate(changedOwner); });
	rejected([&] { roster.bind({{1, Side::ATTACKER, 8}}); });
	rejected([&] { roster.bind({{1, Side::ATTACKER, 0}, {1, Side::DEFENDER, 1}}); });
	rejected([&] { roster.bind({{1, Side::ATTACKER, 0}, {2, Side::ATTACKER, 0}}); });
	check(roster.sourceIndex(4) == 1);
	std::cout << checks << " lifecycle checks passed\n";
}
