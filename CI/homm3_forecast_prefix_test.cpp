// SPDX-License-Identifier: CC0-1.0
#include "../test/aiIntegration/homm3Forecast/SourcePrefixReference.h"
#include <iostream>
#include <limits>
#include <random>
using namespace HoMM3Forecast;
using namespace HoMM3PrefixReference;
namespace
{
std::uint64_t checks = 0;
void require(bool condition, const char * message)
{
	++checks;
	if(!condition) throw std::runtime_error(message);
}
ForecastUnit unit(UnitId id, Side side, int slot, int speed)
{
	ForecastUnit unit;
	unit.order.id = id; unit.order.owningSide = side; unit.order.sourceIndex = slot;
	unit.order.effectiveSpeed = speed; unit.controllingSide = side; unit.liveHealth = 100;
	return unit;
}
void compare(const ForecastInput & input, Side side, const std::vector<ScriptedDamage> & script)
{
	require(projectedTrace(input, side, script) == sourceTrace(input, side, script), "source prefix trace mismatch");
}
}
int main()
{
	ForecastInput input;
	input.units = {unit(0, Side::ATTACKER, 0, 9), unit(100, Side::DEFENDER, 0, 8),
		unit(1, Side::ATTACKER, 1, 7), unit(101, Side::DEFENDER, 1, 6)};
	compare(input, Side::ATTACKER, {});
	require(projectedTrace(input, Side::ATTACKER, {}).opportunities == std::vector<UnitId>({0}), "normal prefix must stop at enemy");
	compare(input, Side::ATTACKER, {{0, 100, 100}});
	require(projectedTrace(input, Side::ATTACKER, {{0, 100, 100}}).opportunities == std::vector<UnitId>({0, 1}), "projected death must remove boundary before side test");
	input.units[1].berserk = true;
	compare(input, Side::ATTACKER, {});
	require(projectedTrace(input, Side::ATTACKER, {}).opportunities == std::vector<UnitId>({0, 1}), "Berserk opponent must skip before boundary");
	input.units[1].berserk = false; input.units[1].catapult = true; compare(input, Side::ATTACKER, {});
	input.units[1].catapult = false; input.units[1].controllingSide = Side::ATTACKER;
	compare(input, Side::ATTACKER, {});
	require(projectedTrace(input, Side::ATTACKER, {}).opportunities == std::vector<UnitId>({0, 100, 1}), "controller affects prefix but does not change owning-side order");
	input.units[2].order.done = true; compare(input, Side::ATTACKER, {});
	input.units[3].order.sourceSpell62Turns = 1;
	compare(input, Side::ATTACKER, {});
	require(projectedTrace(input, Side::ATTACKER, {}).opportunities.back() == 1, "done unit is eligible when earlier opponent cannot act");
	input.units[2].order.done = false; input.units[3].order.sourceSpell62Turns = 0;
	OwnershipPrefix immutable(input, Side::DEFENDER);
	input.units[0].controllingSide = Side::DEFENDER;
	require(!immutable.next(), "caller mutation must not change captured metadata");
	immutable.addExpectedDamage(0, 100);
	require(!immutable.next(), "returned ownership boundary terminates this prefix invocation");
	require(immutable.opposingBoundary() == std::optional<UnitId>(0), "boundary identity remains available");
	std::mt19937 random(0x13f00d);
	for(int iteration = 0; iteration < 20000; ++iteration)
	{
		ForecastInput sample; sample.actingSide = static_cast<Side>(random() % 2); sample.secondPhase = (random() % 2) != 0;
		std::vector<ScriptedDamage> script;
		for(int side = 0; side < 2; ++side) for(int slot = 0; slot < 4; ++slot)
		{
			auto current = unit(side * 100 + slot, static_cast<Side>(side), slot, random() % 12);
			current.order.role = static_cast<OrderRole>(random() % 4);
			current.order.immobilized = (random() % 8) == 0; current.order.done = (random() % 2) != 0;
			current.order.waiting = (random() % 2) != 0;
			current.order.sourceSpell62Turns = ((random() % 5) == 0 ? static_cast<int>(random() % 4) - 1 : 0);
			current.order.sourceSpell70Turns = ((random() % 5) == 0 ? static_cast<int>(random() % 4) - 1 : 0);
			current.order.sourceSpell74Turns = ((random() % 5) == 0 ? static_cast<int>(random() % 4) - 1 : 0);
			current.liveHealth = random() % 150; current.berserk = (random() % 7) == 0;
			current.catapult = current.order.role == OrderRole::ORDINARY && (random() % 7) == 0;
			current.controllingSide = static_cast<Side>(random() % 2);
			sample.units.push_back(current);
			if(random() % 2) script.push_back({current.order.id, static_cast<UnitId>((1 - side) * 100 + random() % 4), static_cast<int>(random() % 250)});
		}
		compare(sample, Side::ATTACKER, script); compare(sample, Side::DEFENDER, script);
	}
	ForecastInput bounds; bounds.units = {unit(0, Side::ATTACKER, 0, 1)};
	OwnershipPrefix capped(bounds, Side::ATTACKER);
	capped.addExpectedDamage(0, std::numeric_limits<std::int32_t>::max());
	require(capped.projectedHealth(0) == 0, "source-bounded overkill must cap health");
	bool rejected = false;
	try { capped.addExpectedDamage(0, std::numeric_limits<std::int32_t>::max()); }
	catch(const std::invalid_argument &) { rejected = true; }
	require(rejected, "undefined source arithmetic must not be silently widened");
	std::cout << checks << " prefix checks passed (40000 generated source differential traces)\n";
}
