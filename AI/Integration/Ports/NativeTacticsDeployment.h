/*
 * Native tactics-phase deployment for the partial combat ports.
 * License: GNU General Public License v2.0 or later.
 */
#pragma once

#include "../../../lib/CStack.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/callback/CBattleCallback.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include <vector>

namespace AIIntegration
{
/// Host adaptation, not a reconstruction of either upstream prebattle AI.
class NativeTacticsDeployment
{
	struct State
	{
		std::vector<uint32_t> order;
		std::set<uint32_t> visited;
		bool ended = false;
		uint32_t pendingUnit = 0;
		bool pending = false;
	};
	std::map<int, State> battles;

	static bool shooter(const CStack & unit)
	{
		return !unit.isCatapult() && unit.isShooter() && unit.shots.canUse(1)
			&& !unit.hasBonusOfType(BonusType::NOT_ACTIVE);
	}

	void advance(CBattleCallback & callback, const BattleID & id)
	{
		auto battle = callback.getBattle(id);
		auto it = battles.find(id.getNum());
		if(it == battles.end() || it->second.ended)
			return;
		if(battle->battleGetTacticDist() <= 0 || battle->battleGetTacticsSide() != battle->battleGetMySide())
		{
			it->second.ended = true;
			return;
		}
		auto & state = it->second;
		auto units = battle->battleGetStacks(CBattleInfoEssentials::ONLY_MINE);
		for(const auto unitId : state.order)
		{
			if(!state.visited.insert(unitId).second)
				continue;
			const auto * unit = battle->battleGetStackByID(unitId);
			if(!unit || battle->battleGetOwner(unit) != callback.getPlayerID().value())
				continue;
			if(!unit->alive() || unit->unitSide() != battle->battleGetTacticsSide()
				|| !unit->getPosition().isAvailable() || unit->getMovementRange(0) <= 0
				|| unit->hasBonusOfType(BonusType::NOT_ACTIVE))
				continue;
			const auto origin = unit->getPosition();
			const bool ranged = shooter(*unit);
			const bool attacker = unit->unitSide() == BattleSide::ATTACKER;
			const auto accessibility = battle->getAccessibility(unit);
			// Shooters go to the rear before guards are considered. With no
			// shooter to cover, melee advances within the native tactics zone.
			auto score = [&](BattleHex head)
			{
				int front = attacker ? head.getX() : GameConstants::BFIELD_WIDTH - 1 - head.getX();
				int cover = GameConstants::BFIELD_SIZE;
				if(!ranged)
					for(const auto * friendly : units)
						if(friendly != unit && shooter(*friendly) && friendly->getPosition().isAvailable())
						{
							const auto vip = friendly->getPosition();
							const int vipFront = attacker ? vip.getX() : GameConstants::BFIELD_WIDTH - 1 - vip.getX();
							if(front <= vipFront)
								continue;
							for(const auto & cell : unit->getHexes(head))
								for(const auto & vipCell : friendly->getHexes())
									cover = std::min(cover, static_cast<int>(BattleHex::getDistance(cell, vipCell)));
						}
				return std::tuple(ranged ? front : cover, ranged ? 0 : -front,
					static_cast<int>(BattleHex::getDistance(origin, head)), head.toInt());
			};
			BattleHex best = origin;
			for(const auto & head : battle->battleGetAvailableHexes(unit, false))
			{
				// Reject native tail-click aliases: select the exact head that
				// the server will occupy, including the complete wide footprint.
				if(!accessibility.accessible(head, unit))
					continue;
				if(score(head) < score(best))
					best = head;
			}
			if(best == origin)
				continue;
			state.pendingUnit = unit->unitId();
			state.pending = true;
			callback.battleMakeTacticAction(id, BattleAction::makeMove(unit, best));
			return;
		}
		state.ended = true;
		state.pending = false;
		callback.battleMakeTacticAction(id, BattleAction::makeEndOFTacticPhase(battle->battleGetTacticsSide()));
	}

public:
	void clear() { battles.clear(); }
	void end(const BattleID & id) { battles.erase(id.getNum()); }
	void start(CBattleCallback & callback, const BattleID & id)
	{
		if(battles.count(id.getNum()))
			return;
		auto units = callback.getBattle(id)->battleGetStacks(CBattleInfoEssentials::ONLY_MINE);
		std::sort(units.begin(), units.end(), [](const CStack * a, const CStack * b)
		{
			return std::tuple(!shooter(*a), a->unitId()) < std::tuple(!shooter(*b), b->unitId());
		});
		State state;
		for(const auto * unit : units)
			state.order.push_back(unit->unitId());
		battles.emplace(id.getNum(), std::move(state));
		advance(callback, id);
	}
	void finished(CBattleCallback & callback, const BattleID & id, const BattleAction & action)
	{
		auto it = battles.find(id.getNum());
		if(it == battles.end() || !it->second.pending || action.actionType != EActionType::WALK
			|| action.stackNumber != it->second.pendingUnit)
			return;
		it->second.pending = false;
		advance(callback, id);
	}
};
}
