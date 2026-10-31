/*
 * FHeroes2BattleAI.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later.
 * Full text of license available in license.txt file, in main folder.
 */
#pragma once

#include "SourceTactics.h"
#include "../../../lib/CStack.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/callback/CBattleCallback.h"
#include "../../../lib/callback/CBattleGameInterface.h"

namespace AIIntegration
{
/// Partial tactical port. VCMI supplies strength and damage projections; upstream spell,
/// retreat, archer-kiting and siege-position policies are not implemented here.
class FHeroes2BattleAI final : public CBattleGameInterface
{
public:
	void initBattleInterface(std::shared_ptr<Environment>, std::shared_ptr<CBattleCallback> callback) override
	{
		if(!callback)
			throw std::invalid_argument("FHeroes2Tactics requires a battle callback");
		cb = std::move(callback);
		logAi->info("AI_SOURCE_POLICY backend=FHeroes2Tactics source=0dbfe9f386f33c2500842f4a59cd284d7f03d541 partial=true projection=VCMI");
	}

	void initBattleInterface(std::shared_ptr<Environment> env, std::shared_ptr<CBattleCallback> callback, AutocombatPreferences) override
	{
		initBattleInterface(std::move(env), std::move(callback));
	}

	void yourTacticPhase(const BattleID & battleID, int) override
	{
		if(!cb)
			throw std::logic_error("FHeroes2Tactics is not initialized");
		cb->battleMakeTacticAction(battleID, BattleAction::makeEndOFTacticPhase(cb->getBattle(battleID)->battleGetTacticsSide()));
	}

	void activeStack(const BattleID & battleID, const CStack * stack) override
	{
		if(!cb || !stack)
			throw std::logic_error("FHeroes2Tactics received an invalid decision event");
		const auto battle = cb->getBattle(battleID);
		if(!battle)
			throw std::logic_error("FHeroes2Tactics received an unknown battle");
		const auto action = chooseAction(*battle, *stack);
		logAi->debug("AI_PORT_ACTION backend=FHeroes2Tactics battle=%d unit=%d action=%s",
			battleID.getNum(), stack->unitId(), action.toString());
		cb->battleMakeUnitAction(battleID, action);
	}

private:
	std::shared_ptr<CBattleCallback> cb;

	static double strength(const CStack & stack)
	{
		return static_cast<double>(stack.estimateCombatValue());
	}

	static double lossValue(const CStack & stack, const DamageEstimation & estimate)
	{
		const double health = static_cast<double>(stack.getAvailableHealth());
		if(health <= 0)
			return 0;
		const double damage = std::min(health,
			(static_cast<double>(estimate.damage.min) + static_cast<double>(estimate.damage.max)) / 2);
		return strength(stack) * damage / health;
	}

	static double attackValue(CPlayerBattleCallback & battle, const CStack & attacker,
		const CStack & defender, const BattleHex & from, bool shooting, int distance)
	{
		const int attackerPosition = attacker.getPosition().toInt();
		const int defenderPosition = defender.getPosition().toInt();
		const int64_t attackerHealth = attacker.getAvailableHealth();
		const int64_t defenderHealth = defender.getAvailableHealth();
		BattleAttackInfo info(&attacker, &defender, distance, shooting);
		info.attackerPos = from;
		DamageEstimation retaliation;
		const auto damage = battle.battleEstimateDamage(info, &retaliation);
		const double value = lossValue(defender, damage) - (shooting ? 0 : lossValue(attacker, retaliation));
		if(attackerPosition != attacker.getPosition().toInt()
			|| defenderPosition != defender.getPosition().toInt()
			|| attackerHealth != attacker.getAvailableHealth()
			|| defenderHealth != defender.getAvailableHealth())
			throw std::logic_error("Damage preview modified a live battle unit");
		return value;
	}

	static Ports::FHeroes2::Tactics tactics(CPlayerBattleCallback & battle, const CStack & stack,
		const std::vector<const CStack *> & enemies)
	{
		Ports::FHeroes2::TacticsInput input;
		for(const auto * friendly : battle.battleGetStacks(CBattleInfoEssentials::ONLY_MINE))
		{
			input.armyStrength += strength(*friendly);
			if(battle.battleCanShoot(friendly))
				input.shootersStrength += strength(*friendly);
		}
		for(const auto * enemy : enemies)
		{
			input.enemyArmyStrength += strength(*enemy);
			if(battle.battleCanShoot(enemy))
				input.enemyShootersStrength += strength(*enemy);
		}
		if(input.armyStrength <= 0 || input.enemyArmyStrength <= 0)
			return {};
		input.flying = stack.hasBonusOfType(BonusType::FLYING);
		input.insideDefendedArea = stack.unitSide() == BattleSide::ATTACKER
			? stack.getPosition().getX() < GameConstants::BFIELD_WIDTH / 2
			: stack.getPosition().getX() >= GameConstants::BFIELD_WIDTH / 2;
		return Ports::FHeroes2::selectTactics(input);
	}

	static BattleAction approach(CPlayerBattleCallback & battle, const CStack & stack,
		const ReachabilityInfo & reachability, const BattleHexArray & available,
		const std::vector<const CStack *> & enemies)
	{
		BattleHex destination;
		int shortest = GameConstants::BFIELD_SIZE;
		for(const auto * enemy : enemies)
		{
			for(const auto & hex : enemy->getAttackableHexes(&stack))
			{
				if(!hex.isValid() || !battle.isMeleeAttackPossible(&stack, enemy, hex))
					continue;
				const auto distance = reachability.distances[hex.toInt()];
				if(distance < shortest || (distance == shortest && destination.isValid() && hex.toInt() < destination.toInt()))
				{
					shortest = distance;
					destination = hex;
				}
			}
		}
		if(!destination.isValid() || shortest >= GameConstants::BFIELD_SIZE)
			return BattleAction::makeDefend(&stack);

		if(stack.hasBonusOfType(BonusType::FLYING))
		{
			BattleHex nearest;
			int bestDistance = GameConstants::BFIELD_SIZE;
			for(const auto & hex : available)
			{
				const int distance = BattleHex::getDistance(destination, hex);
				if(distance < bestDistance || (distance == bestDistance && nearest.isValid() && hex.toInt() < nearest.toInt()))
				{
					nearest = hex;
					bestDistance = distance;
				}
			}
			if(nearest.isValid() && nearest != stack.getPosition())
				return BattleAction::makeMove(&stack, nearest);
			return BattleAction::makeDefend(&stack);
		}

		for(int steps = 0; steps < GameConstants::BFIELD_SIZE && destination.isValid(); ++steps)
		{
			if(available.contains(destination))
				return destination == stack.getPosition() ? BattleAction::makeDefend(&stack) : BattleAction::makeMove(&stack, destination);
			const auto previous = reachability.predecessors[destination.toInt()];
			if(previous == destination)
				break;
			destination = previous;
		}
		return BattleAction::makeDefend(&stack);
	}

	static BattleAction chooseAction(CPlayerBattleCallback & battle, const CStack & stack)
	{
		if(stack.isCatapult())
			return battle.getAttackableWallParts().empty() ? BattleAction::makeDefend(&stack) : BattleAction::makeCatapultShot(&stack);
		if(stack.isFirstAidTent())
			return BattleAction::makeHeal(&stack);

		auto enemies = battle.battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY);
		vstd::erase_if(enemies, [&](const CStack * enemy)
		{
			return !enemy->alive() || enemy->isInvincible() || !battle.battleCanAttackUnit(&stack, enemy);
		});
		std::sort(enemies.begin(), enemies.end(), [](const CStack * left, const CStack * right) { return left->unitId() < right->unitId(); });
		if(enemies.empty())
			return BattleAction::makeDefend(&stack);

		const CStack * shootingTarget = nullptr;
		double bestShot = std::numeric_limits<double>::lowest();
		for(const auto * enemy : enemies)
		{
			if(!battle.battleCanShoot(&stack, enemy->getPosition()))
				continue;
			const double value = attackValue(battle, stack, *enemy, stack.getPosition(), true, 0);
			if(value > bestShot)
			{
				bestShot = value;
				shootingTarget = enemy;
			}
		}
		if(shootingTarget)
			return BattleAction::makeShotAttack(&stack, shootingTarget);
		if(!stack.getPosition().isValid())
			return BattleAction::makeDefend(&stack);

		const auto reachability = battle.getReachability(&stack);
		auto available = battle.battleGetAvailableHexes(reachability, &stack, false);
		available.sort([](const BattleHex & left, const BattleHex & right) { return left.toInt() < right.toInt(); });
		const CStack * target = nullptr;
		BattleHex targetHex;
		Ports::FHeroes2::MeleeOutcome best;
		for(const auto * enemy : enemies)
		{
			for(const auto & hex : available)
			{
				if(!hex.isAvailable())
					continue;
				const auto attackedHexes = battle.meleeAttackHexes(&stack, enemy, hex);
				if(attackedHexes.empty())
					continue;
				const double value = attackValue(battle, stack, *enemy, hex, false, reachability.distances[hex.toInt()]);
				const Ports::FHeroes2::MeleeOutcome candidate{hex.toInt(), strength(*enemy), value, true};
				if(!target || Ports::FHeroes2::isOutcomeImproved(candidate, best))
				{
					target = enemy;
					targetHex = attackedHexes.front();
					best = candidate;
				}
			}
		}
		if(target)
		{
			const BattleHex from(best.fromIndex);
			const auto * occupant = battle.battleGetUnitByPos(targetHex);
			if(!available.contains(from) || occupant != target)
				throw std::logic_error("FHeroes2Tactics invalid melee candidate: unit=" + std::to_string(stack.unitId())
					+ " target=" + std::to_string(target->unitId()) + " targetPosition=" + std::to_string(target->getPosition().toInt())
					+ " selectedHex=" + std::to_string(targetHex.toInt()) + " from=" + std::to_string(from.toInt())
					+ " reachable=" + std::to_string(available.contains(from))
					+ " occupant=" + (occupant ? std::to_string(occupant->unitId()) : "none"));
			return BattleAction::makeMeleeAttack(&stack, targetHex, from);
		}
		if(tactics(battle, stack, enemies).defensive)
			return BattleAction::makeDefend(&stack);
		return approach(battle, stack, reachability, available, enemies);
	}
};
}
