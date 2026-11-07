// SPDX-License-Identifier: CC0-1.0
#pragma once
#include "OwnershipPrefix.h"
#include <algorithm>
#include <array>
#include <map>
#include <stdexcept>
#include <vector>

namespace HoMM3PrefixReference
{
using HoMM3Forecast::UnitId;
struct ScriptedDamage { UnitId actor; UnitId target; std::int32_t amount; };
struct Trace
{
	std::vector<UnitId> opportunities;
	std::size_t stoppedAt = 0;
	std::map<UnitId, std::int64_t> projectedHealth;
	bool operator==(const Trace &) const = default;
};
inline long max(long a, int b) { return a > b ? a : b; }
using std::min;
enum { creatureImmobilized = 1, creatureClone = 2, ARMY_CREATURE_FIRST_AID_TENT = 2,
	ARMY_CREATURE_AMMO_CART = 3, CREATURE_CATAPULT = 4, AI_ORDER_SHOOT = 7, AI_ORDER_MOVE_AND_ATTACK = 6 };
struct army
{
	UnitId id = 0;
	int m_creatureType = 0, flags = 0, controller = 0;
	long m_numTroops = 0, m_topCreatureDamage = 0, AI_expected_damage = 0;
	struct { long hitPoints = 1; } m_monInfo;
	std::array<int, 80> m_spellInfluence{};
	bool is(int mask) const { return (flags & mask) != 0; }
	bool isIncapacitated() const;
	bool cannotAttack() const;
	int getSpellTime(int spell) const { return m_spellInfluence.at(spell); }
	long getTotalHitPoints(bool simulated) const;
	void setAIExpectedDamage(long arg);
	int getControllingSide() const { return controller; }
	bool canShoot(int) const { return true; }
	long getAIExpectedDamage() const { return AI_expected_damage; }
};
struct cell { army * value = nullptr; army * getArmy() { return value; } };
struct combatManager
{
	std::vector<cell> m_cells;
	long m_nextAction = 0, m_nextActionGridIndex = -1;
	std::vector<UnitId> opportunities;
	std::vector<ScriptedDamage> script;
	long pendingDamage = 0;
	long simulateActions(std::vector<army *> &, long, long);
	void chooseShooterAction(const army * actor, int, long)
	{
		opportunities.push_back(actor->id);
		m_nextAction = 0;
		for(const auto & effect : script)
			if(effect.actor == actor->id)
				for(std::size_t i = 0; i < m_cells.size(); ++i)
					if(m_cells[i].value->id == effect.target)
					{
						m_nextAction = AI_ORDER_SHOOT;
						m_nextActionGridIndex = static_cast<long>(i);
						pendingDamage = effect.amount;
					}
	}
	void chooseMeleeAction(const army * actor, int, int, long side) { chooseShooterAction(actor, 0, side); }
	void simulateMeleeAttack(army *, army *, long) { throw std::logic_error("fixture has no melee effect model"); }
};
inline combatManager * g_combatManager = nullptr;
inline bool validHex(long value)
{
	return value >= 0 && static_cast<std::size_t>(value) < g_combatManager->m_cells.size();
}
struct type_AI_combat_parameters { type_AI_combat_parameters(combatManager *, long) {} };
inline void simulateSimpleAttack(army *, army * target, long, unsigned char, unsigned char)
{
	target->setAIExpectedDamage(target->getAIExpectedDamage() + g_combatManager->pendingDamage);
}
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "SourcePrefixBodies.inc"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

inline Trace sourceTrace(const HoMM3Forecast::ForecastInput & input, HoMM3Forecast::Side side,
	const std::vector<ScriptedDamage> & script)
{
	HoMM3Forecast::OwnershipPrefix hostOrder(input, side);
	std::vector<army> sourceUnits;
	for(const auto & unit : input.units)
	{
		army source;
		source.id = unit.order.id;
		source.m_creatureType = unit.catapult ? CREATURE_CATAPULT : static_cast<int>(unit.order.role);
		source.flags = unit.order.immobilized ? creatureImmobilized : 0;
		source.controller = static_cast<int>(unit.controllingSide);
		source.m_numTroops = static_cast<long>(unit.liveHealth);
		source.m_spellInfluence[62] = unit.order.sourceSpell62Turns;
		source.m_spellInfluence[70] = unit.order.sourceSpell70Turns;
		source.m_spellInfluence[74] = unit.order.sourceSpell74Turns;
		source.m_spellInfluence[59] = unit.berserk ? 1 : 0;
		sourceUnits.push_back(source);
	}
	combatManager combat;
	combat.script = script;
	for(auto & source : sourceUnits) combat.m_cells.push_back({&source});
	std::vector<army *> ordered;
	for(const auto & next : hostOrder.moveOrder().units)
		for(auto & source : sourceUnits) if(source.id == next.id) ordered.push_back(&source);
	g_combatManager = &combat;
	Trace result;
	result.stoppedAt = static_cast<std::size_t>(combat.simulateActions(ordered, 0, static_cast<int>(side)));
	result.opportunities = combat.opportunities;
	for(const auto & unit : sourceUnits) result.projectedHealth.emplace(unit.id, unit.getTotalHitPoints(true));
	return result;
}

inline Trace projectedTrace(const HoMM3Forecast::ForecastInput & input, HoMM3Forecast::Side side,
	const std::vector<ScriptedDamage> & script)
{
	HoMM3Forecast::OwnershipPrefix prefix(input, side);
	Trace result;
	while(const auto actor = prefix.next())
	{
		result.opportunities.push_back(*actor);
		for(const auto & damage : script) if(damage.actor == *actor) prefix.addExpectedDamage(damage.target, damage.amount);
	}
	result.stoppedAt = prefix.stoppedAt();
	for(const auto & unit : input.units) result.projectedHealth.emplace(unit.order.id, prefix.projectedHealth(unit.order.id));
	return result;
}
}
