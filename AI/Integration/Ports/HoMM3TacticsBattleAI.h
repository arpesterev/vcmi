/*
 * HoMM3TacticsBattleAI.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later.
 *
 * An experimental, independent tactical projection. HoMM3 target scoring
 * follows sushi-shi/homm3-decomp src/ai_tactical.cpp at
 * e12906accf96c2662b1abadbc49ddc111991bf21 (CC0-1.0).
 * Movement legality and damage estimation remain native VCMI projections.
 * This is not the complete HoMM3 combat AI.
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
class HoMM3TacticsBattleAI final : public CBattleGameInterface
{
public:
	void initBattleInterface(std::shared_ptr<Environment>, std::shared_ptr<CBattleCallback> callback) override
	{
		if(!callback)
			throw std::invalid_argument("HoMM3OriginalTactics requires a battle callback");
		cb = std::move(callback);
		logAi->info("AI_SOURCE_POLICY backend=HoMM3OriginalTactics source=e12906accf96c2662b1abadbc49ddc111991bf21 partial=true projection=VCMI");
	}

	void initBattleInterface(std::shared_ptr<Environment> environment, std::shared_ptr<CBattleCallback> callback,
		AutocombatPreferences) override
	{
		initBattleInterface(std::move(environment), std::move(callback));
	}

	void yourTacticPhase(const BattleID & battleID, int) override
	{
		if(!cb)
			throw std::logic_error("HoMM3OriginalTactics is not initialized");
		cb->battleMakeTacticAction(battleID,
			BattleAction::makeEndOFTacticPhase(cb->getBattle(battleID)->battleGetTacticsSide()));
	}

	void activeStack(const BattleID & battleID, const CStack * stack) override
	{
		if(!cb || !stack)
			throw std::logic_error("HoMM3OriginalTactics received invalid activation");
		const auto battle = cb->getBattle(battleID);
		if(!battle)
			throw std::logic_error("HoMM3OriginalTactics received an unknown battle");
		const auto action = chooseAction(*battle, *stack);
		logAi->debug("AI_PORT_ACTION backend=HoMM3OriginalTactics battle=%d unit=%d action=%s",
			battleID.getNum(), stack->unitId(), action.toString());
		cb->battleMakeUnitAction(battleID, action);
	}

private:
	std::shared_ptr<CBattleCallback> cb;

	static double lossValue(const CStack & target, const DamageEstimation & estimate)
	{
		const double health = static_cast<double>(target.getAvailableHealth());
		if(health <= 0)
			return 0;
		const double damage = std::min(health,
			(static_cast<double>(estimate.damage.min) + static_cast<double>(estimate.damage.max)) / 2);
		return static_cast<double>(target.estimateCombatValue()) * std::max(0.0, damage) / health;
	}

	static double exchangeValue(CPlayerBattleCallback & battle, const CStack & attacker,
		const CStack & target, BattleHex from, bool shooting, int distance)
	{
		const auto oldAttackerHealth = attacker.getAvailableHealth();
		const auto oldTargetHealth = target.getAvailableHealth();
		const auto oldAttackerPosition = attacker.getPosition();
		const auto oldTargetPosition = target.getPosition();
		BattleAttackInfo info(&attacker, &target, distance, shooting);
		info.attackerPos = from;
		DamageEstimation retaliation;
		const auto damage = battle.battleEstimateDamage(info, &retaliation);
		const double score = lossValue(target, damage)
			- (shooting ? 0 : lossValue(attacker, retaliation));
		if(oldAttackerHealth != attacker.getAvailableHealth() || oldTargetHealth != target.getAvailableHealth()
			|| oldAttackerPosition != attacker.getPosition() || oldTargetPosition != target.getPosition())
			throw std::logic_error("HoMM3 tactical preview changed a live battle stack");
		return score;
	}

	static int32_t projectedAttackEffect(double value)
	{
		if(!std::isfinite(value))
			throw std::logic_error("Nonfinite HoMM3 attack projection");
		return static_cast<int32_t>(std::clamp(value, 0.0,
			static_cast<double>(std::numeric_limits<int32_t>::max())));
	}

	static bool isIncapacitated(const CStack & target)
	{
		// HoMM3's army::isIncapacitated includes disables beyond Stone Gaze.
		// VCMI isFrozen() detects Stone Gaze only; inspect actual spell effects
		// so Blind and Paralyze also receive the upstream 1/10 shot weighting.
		const auto hasSpell = [&](SpellID spell)
		{
			return target.hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
		};
		return target.isFrozen() || hasSpell(SpellID(SpellID::BLIND))
			|| hasSpell(SpellID(SpellID::PARALYZE));
	}

	static int32_t rangedScore(CPlayerBattleCallback & battle,
		const CStack & attacker, const CStack & target)
	{
		// Native replacement for getSimpleAttackEffect and enemy target-time lookup.
		// The original uses its own projected enemy goal, not hex distance.
		const int32_t effect = projectedAttackEffect(exchangeValue(battle, attacker, target,
			attacker.getPosition(), true, 0));
		const int speed = std::max(1, static_cast<int>(target.getMovementRange()));
		const int hexDistance = BattleHex::getDistance(attacker.getPosition(), target.getPosition());
		const int time = std::max(1, (hexDistance + speed - 1) / speed);
		const bool canRetaliateLater = battle.battleCanAttackUnit(&target, &attacker);
		return Ports::HoMM3::rangedAttackValue(
			{effect, isIncapacitated(target), !canRetaliateLater, canRetaliateLater, time}, false);
	}

	static BattleAction moveTowardEnemy(CPlayerBattleCallback & battle, const CStack & stack,
		const ReachabilityInfo & reach, const BattleHexArray & available,
		const std::vector<const CStack *> & enemies)
	{
		BattleHex goal;
		int shortest = GameConstants::BFIELD_SIZE;
		for(const auto * enemy : enemies)
		{
			for(const auto & hex : enemy->getAttackableHexes(&stack))
			{
				if(!hex.isValid() || !battle.isMeleeAttackPossible(&stack, enemy, hex))
					continue;
				const int distance = reach.distances[hex.toInt()];
				if(distance < shortest || (distance == shortest && (!goal.isValid() || hex.toInt() < goal.toInt())))
				{
					shortest = distance;
					goal = hex;
				}
			}
		}
		if(!goal.isValid() || shortest >= GameConstants::BFIELD_SIZE)
			return BattleAction::makeDefend(&stack);
		if(stack.hasBonusOfType(BonusType::FLYING))
		{
			BattleHex nearest;
			int bestDistance = GameConstants::BFIELD_SIZE;
			for(const auto & hex : available)
			{
				const int distance = BattleHex::getDistance(goal, hex);
				if(distance < bestDistance || (distance == bestDistance && (!nearest.isValid() || hex.toInt() < nearest.toInt())))
				{
					nearest = hex;
					bestDistance = distance;
				}
			}
			return nearest.isValid() && nearest != stack.getPosition()
				? BattleAction::makeMove(&stack, nearest) : BattleAction::makeDefend(&stack);
		}
		for(int steps = 0; steps < GameConstants::BFIELD_SIZE && goal.isValid(); ++steps)
		{
			if(available.contains(goal))
				return goal == stack.getPosition() ? BattleAction::makeDefend(&stack)
					: BattleAction::makeMove(&stack, goal);
			const BattleHex previous = reach.predecessors[goal.toInt()];
			if(previous == goal)
				break;
			goal = previous;
		}
		return BattleAction::makeDefend(&stack);
	}

	// The TUM mod extends the original HoMM3 tactical policy with documented overrides.
	// Expose the baseline decision for explicit composition, not as a hidden fallback.
public:
	static BattleAction chooseAction(CPlayerBattleCallback & battle, const CStack & stack, bool avoidFastDecoys = false)
	{
		if(stack.isCatapult())
			return battle.getAttackableWallParts().empty() ? BattleAction::makeDefend(&stack)
				: BattleAction::makeCatapultShot(&stack);
		if(stack.isFirstAidTent())
			return BattleAction::makeHeal(&stack);

		auto enemies = battle.battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY);
		vstd::erase_if(enemies, [&](const CStack * enemy)
		{
			return !enemy->alive() || enemy->isInvincible()
				|| (!battle.battleCanShoot(&stack, enemy->getPosition())
					&& !battle.battleCanAttackUnit(&stack, enemy));
		});
		std::sort(enemies.begin(), enemies.end(),
			[](const CStack * lhs, const CStack * rhs) { return lhs->unitId() < rhs->unitId(); });
		if(enemies.empty())
			return BattleAction::makeDefend(&stack);

		const CStack * bestShot = nullptr;
		int32_t bestShotValue = std::numeric_limits<int32_t>::min();
		for(const auto * enemy : enemies)
		{
			if(!battle.battleCanShoot(&stack, enemy->getPosition()))
				continue;
			const int32_t score = rangedScore(battle, stack, *enemy);
			if(!bestShot || score > bestShotValue)
			{
				bestShot = enemy;
				bestShotValue = score;
			}
		}
		if(bestShot)
			return BattleAction::makeShotAttack(&stack, bestShot);
		if(!stack.getPosition().isValid())
			return BattleAction::makeDefend(&stack);

		const auto reach = battle.getReachability(&stack);
		auto available = battle.battleGetAvailableHexes(reach, &stack, false);
		available.sort([](const BattleHex & lhs, const BattleHex & rhs) { return lhs.toInt() < rhs.toInt(); });

		const CStack * target = nullptr;
		BattleHex attackFrom;
		BattleHex attackTo;
		double bestExchange = -std::numeric_limits<double>::infinity();
		for(const auto * enemy : enemies)
		{
			for(const auto & hex : available)
			{
				if(!hex.isAvailable())
					continue;
				const auto attackedHexes = battle.meleeAttackHexes(&stack, enemy, hex);
				if(attackedHexes.empty())
					continue;
				const double exchange = exchangeValue(battle, stack, *enemy, hex, false, reach.distances[hex.toInt()]);
				if(!target || exchange > bestExchange)
				{
					target = enemy;
					attackFrom = hex;
					attackTo = attackedHexes.front();
					bestExchange = exchange;
				}
			}
		}
		if(target)
		{
			if(!available.contains(attackFrom) || battle.battleGetUnitByPos(attackTo) != target)
				throw std::logic_error("HoMM3 tactical port produced an invalid melee candidate");
			return BattleAction::makeMeleeAttack(&stack, attackTo, attackFrom);
		}
		// TUM's public description rejects chasing nearby fast stacks as bait.
		// Apply only to movement: a legal immediate melee strike or ranged shot
		// was already returned above. The unmodified HoMM3 baseline keeps its
		// usual nearest-opponent route selection.
		if(avoidFastDecoys && enemies.size() > 1)
		{
			const int ownSpeed = static_cast<int>(stack.getMovementRange());
			int slowestEnemySpeed = std::numeric_limits<int>::max();
			for(const auto * enemy : enemies)
				slowestEnemySpeed = std::min(slowestEnemySpeed, static_cast<int>(enemy->getMovementRange()));
			auto targets = enemies;
			vstd::erase_if(targets, [&](const CStack * enemy)
			{
				return Ports::TUM::ignoreNearbyFastDecoyForMovement(
					ownSpeed, static_cast<int>(enemy->getMovementRange()), slowestEnemySpeed,
					BattleHex::getDistance(stack.getPosition(), enemy->getPosition()));
			});
			if(!targets.empty() && targets.size() < enemies.size())
			{
				const auto alternate = moveTowardEnemy(battle, stack, reach, available, targets);
				if(alternate.actionType != EActionType::DEFEND)
					return alternate;
			}
		}
		return moveTowardEnemy(battle, stack, reach, available, enemies);
	}
};
}
