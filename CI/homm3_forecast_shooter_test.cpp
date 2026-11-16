// SPDX-License-Identifier: CC0-1.0
#include "../AI/Integration/Ports/HoMM3Forecast/ShooterPrefix.h"
#include <cassert>
#include <iostream>
using namespace HoMM3Forecast;
struct ShooterModel
{
	bool shooting = true;
	bool canShoot(UnitId, UnitId) const { return shooting; }
	std::optional<int64_t> shotDamage(UnitId, UnitId, int64_t health, int64_t) const { return ((health + 9) / 10) * 2; }
	SurvivorFacts survivorFacts(UnitId) const { return {10, 0, false}; }
};
int main()
{
	ForecastInput input;
	ForecastUnit friendUnit, enemy;
	friendUnit.order.id = 1; friendUnit.order.effectiveSpeed = 10; friendUnit.liveHealth = 100;
	enemy.order.id = 2; enemy.order.owningSide = Side::DEFENDER; enemy.controllingSide = Side::DEFENDER;
	enemy.order.effectiveSpeed = 5; enemy.liveHealth = 20;
	input.units = {friendUnit, enemy};
	ShooterModel model;
	auto result = projectShooterPrefix(input, Side::ATTACKER, model);
	assert(result.outcome && result.outcome->winLikely && result.outcome->spellsNotRequired);
	assert(input.units[1].liveHealth == 20);
	input.units[1].liveHealth = 21;
	result = projectShooterPrefix(input, Side::ATTACKER, model);
	assert(result.outcome && !result.outcome->winLikely && result.outcome->opposingBoundary == 2);
	input.units[1].order.effectiveSpeed = 15;
	result = projectShooterPrefix(input, Side::ATTACKER, model);
	assert(result.outcome && result.outcome->shots.empty());
	input.units[1].berserk = true;
	result = projectShooterPrefix(input, Side::ATTACKER, model);
	assert(result.outcome && result.outcome->shots.size() == 1);
	model.shooting = false;
	assert(!projectShooterPrefix(input, Side::ATTACKER, model).outcome);
	model.shooting = true;
	enemy.order.id = 3; enemy.order.sourceIndex = 1; input.units.push_back(enemy);
	assert(!projectShooterPrefix(input, Side::ATTACKER, model).outcome);
	std::cout << "PASS: bounded production shooter elimination, boundary, skip, unavailable, and input immutability\n";
}
