/*
 * FHeroes2BattleAI.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later.
 * Full text of license available in license.txt file, in main folder.
 */
#pragma once

#include "SourceTactics.h"
#include "FHeroes2SpellPolicy.h"
#include "../../../lib/CStack.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/callback/CBattleCallback.h"
#include "../../../lib/callback/CBattleGameInterface.h"

namespace AIIntegration
{
/// Partial tactical port with damage/restoration magic. VCMI supplies rule projections;
/// upstream buff, retreat, archer-kiting and siege-position policies remain incomplete.
class FHeroes2BattleAI final : public CBattleGameInterface
{
public:
	void initBattleInterface(std::shared_ptr<Environment> environment, std::shared_ptr<CBattleCallback> callback) override
	{
		if(!callback)
			throw std::invalid_argument("FHeroes2Tactics requires a battle callback");
		cb = std::move(callback);
		env = std::move(environment);
		preferences = {};
		logAi->info("AI_SOURCE_POLICY backend=FHeroes2Tactics source=0dbfe9f386f33c2500842f4a59cd284d7f03d541 partial=true projection=VCMI");
	}

	void initBattleInterface(std::shared_ptr<Environment> environment, std::shared_ptr<CBattleCallback> callback, AutocombatPreferences prefs) override
	{
		initBattleInterface(std::move(environment), std::move(callback));
		preferences = prefs;
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
		if(preferences.enableSpellsUsage && !stack->isCatapult() && !stack->isFirstAidTent())
		{
			if(const auto spell = FHeroes2SpellPolicy::choose(battle, env.get()))
			{
				logAi->debug("AI_PORT_SPELL backend=FHeroes2Tactics battle=%d action=%s", battleID.getNum(), spell->toString());
				cb->battleMakeSpellAction(battleID, *spell);
				// The engine applies the spell and reactivates a surviving stack.
				return;
			}
		}
		const auto action = chooseAction(*battle, *stack);
		logAi->debug("AI_PORT_ACTION backend=FHeroes2Tactics battle=%d unit=%d action=%s",
			battleID.getNum(), stack->unitId(), action.toString());
		cb->battleMakeUnitAction(battleID, action);
	}

private:
	std::shared_ptr<CBattleCallback> cb;
	std::shared_ptr<Environment> env;
	AutocombatPreferences preferences;

	static double strength(const battle::Unit & stack)
	{
		return static_cast<double>(stack.estimateCombatValue());
	}

	static double lossValue(const battle::Unit & stack, const DamageEstimation & estimate)
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
			// fheroes2 analyzeBattleState counts ranged units even when an
			// adjacent enemy currently blocks shooting. BattleCanShoot is a
			// per-turn legality check, not the source unit classification.
			if(friendly->isShooter() && friendly->getMovementRange() > 0)
				input.shootersStrength += strength(*friendly);
		}
		for(const auto * enemy : enemies)
		{
			input.enemyArmyStrength += strength(*enemy);
			if(enemy->isShooter() && enemy->getMovementRange() > 0)
				input.enemyShootersStrength += strength(*enemy);
		}
		if(input.armyStrength <= 0 || input.enemyArmyStrength <= 0)
			return {};
		input.flying = stack.hasBonusOfType(BonusType::FLYING);
		// fheroes2 includes defensive castle/tower advantage when deciding
		// whether to hold the line. The native projection requires actual
		// surviving fortifications; an undefended open-field battle is not a siege.
		input.defendingCastle = stack.unitSide() == BattleSide::DEFENDER
			&& battle.battleGetDefendedTown() != nullptr
			&& battle.battleGetWallState(EWallPart::GATE) != EWallState::NONE;
		input.insideDefendedArea = stack.unitSide() == BattleSide::ATTACKER
			? stack.getPosition().getX() < GameConstants::BFIELD_WIDTH / 2
			: stack.getPosition().getX() >= GameConstants::BFIELD_WIDTH / 2;
		return Ports::FHeroes2::selectTactics(input);
	}

	// fheroes2 meleeUnitOffense first pursues targets that cannot evade,
	// then maximizes threat divided by the path distance. Unlike the former
	// nearest-stack policy, this can pursue a remote archer over nearby bait.
	// The threat and legal routes are native VCMI projections.
	static BattleAction approach(CPlayerBattleCallback & battle, const CStack & stack,
		const ReachabilityInfo & reachability, const BattleHexArray & available,
		const std::vector<const CStack *> & enemies)
	{
		BattleHex destination;
		std::optional<Ports::FHeroes2::DistantTarget> best;
		const int ownSpeed = static_cast<int>(stack.getMovementRange());
		for(const auto * enemy : enemies)
		{
			BattleHex goal;
			int shortest = GameConstants::BFIELD_SIZE;
			for(const auto & hex : enemy->getAttackableHexes(&stack))
			{
				if(!hex.isValid() || !battle.isMeleeAttackPossible(&stack, enemy, hex))
					continue;
				const int distance = reachability.distances[hex.toInt()];
				if(distance < shortest || (distance == shortest && (!goal.isValid() || hex.toInt() < goal.toInt())))
				{
					shortest = distance;
					goal = hex;
				}
			}
			if(!goal.isValid() || shortest >= GameConstants::BFIELD_SIZE)
				continue;

			const int enemySpeed = static_cast<int>(enemy->getMovementRange());
			const bool cannotEvade = enemy->hasBonusOfType(BonusType::SHOOTER)
				|| enemySpeed <= 0 || (!enemy->hasBonusOfType(BonusType::FLYING) && enemySpeed < ownSpeed);
			const Ports::FHeroes2::DistantTarget candidate{
				std::max(0.0, strength(*enemy)), std::max(1, shortest), cannotEvade,
				static_cast<int>(enemy->unitId())};
			if(!best || Ports::FHeroes2::preferDistantTarget(candidate, *best))
			{
				best = candidate;
				destination = goal;
			}
		}
		if(!destination.isValid())
			return BattleAction::makeDefend(&stack);

		if(stack.hasBonusOfType(BonusType::FLYING))
		{
			BattleHex nearest;
			int bestDistance = GameConstants::BFIELD_SIZE;
			for(const auto & hex : available)
			{
				const int distance = BattleHex::getDistance(destination, hex);
				if(distance < bestDistance || (distance == bestDistance && (!nearest.isValid() || hex.toInt() < nearest.toInt())))
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


	// fheroes2 meleeUnitDefense prefers covering valuable friendly archers.
	// This conservative VCMI projection moves a single-width melee stack into
	// a reachable neighboring hex only when ground melee can threaten a shooter
	// next turn. Never disrupt an available immediate attack; wide-unit cover,
	// long-range deployment and spell/area-threat spacing remain unsupported.
	static std::optional<BattleAction> coverThreatenedShooter(CPlayerBattleCallback & battle,
		const CStack & stack, const ReachabilityInfo & reachability,
		const BattleHexArray & available, const std::vector<const CStack *> & enemies)
	{
		if(stack.doubleWide() || stack.hasBonusOfType(BonusType::SHOOTER))
			return std::nullopt;
		const auto friends = battle.battleGetStacks(CBattleInfoEssentials::ONLY_MINE);
		double totalShooterStrength = 0;
		for(const auto * friendly : friends)
		{
			if(friendly != &stack && friendly->alive() && friendly->getPosition().isValid()
				&& !friendly->doubleWide() && friendly->hasBonusOfType(BonusType::SHOOTER))
				totalShooterStrength += strength(*friendly);
		}
		if(totalShooterStrength <= 0)
			return std::nullopt;

		BattleHex bestCoverHex;
		double bestCoverValue = -std::numeric_limits<double>::infinity();
		int bestTravel = GameConstants::BFIELD_SIZE;
		for(const auto * friendly : friends)
		{
			if(friendly == &stack || !friendly->alive() || !friendly->getPosition().isValid()
				|| friendly->doubleWide() || !friendly->hasBonusOfType(BonusType::SHOOTER))
				continue;
			const bool threatened = std::any_of(enemies.begin(), enemies.end(), [&](const CStack * enemy)
			{
				return enemy->getPosition().isValid() && !enemy->hasBonusOfType(BonusType::SHOOTER)
					&& BattleHex::getDistance(friendly->getPosition(), enemy->getPosition())
						<= static_cast<int>(enemy->getMovementRange()) + 1;
			});
			if(!threatened)
				continue;
			if(BattleHex::getDistance(stack.getPosition(), friendly->getPosition()) == 1)
				return BattleAction::makeDefend(&stack);

			for(const auto & hex : available)
			{
				if(!hex.isAvailable() || hex == stack.getPosition()
					|| BattleHex::getDistance(hex, friendly->getPosition()) != 1)
					continue;
				const int travel = reachability.distances[hex.toInt()];
				if(travel <= 0 || travel > static_cast<int>(stack.getMovementRange()))
					continue;
				// Source: archer strength minus the path-distance penalty equal
				// to all friendly shooter strength / 15 per traveled hex.
				const double value = strength(*friendly) - totalShooterStrength * travel / 15.0;
				if(value > bestCoverValue || (value == bestCoverValue
					&& (travel < bestTravel || (travel == bestTravel
						&& (!bestCoverHex.isValid() || hex.toInt() < bestCoverHex.toInt())))))
				{
					bestCoverHex = hex;
					bestCoverValue = value;
					bestTravel = travel;
				}
			}
		}
		return bestCoverHex.isValid() ? std::optional<BattleAction>(BattleAction::makeMove(&stack, bestCoverHex))
			: std::nullopt;
	}

	// fheroes2 ai_battle.cpp archerDecision, pinned at 0dbfe9f386f33c2500842f4a59cd284d7f03d541:
	// only a sufficiently faster shooter attempts to escape an imminent melee
	// threat, and only to a position safe from every visible enemy stack.
	// Native VCMI movement hexes supply legality. Geometric enemy reach is
	// deliberately conservative because the currently occupied archer hex
	// could otherwise block the enemy path used to assess a future position.
	static std::optional<BattleAction> archerRetreat(CPlayerBattleCallback & battle,
		const CStack & stack, const std::vector<const CStack *> & enemies)
	{
		// Upstream evaluates escape before its isHandFighting() melee branch.
		// An adjacent slow melee enemy may block a shot without preventing
		// movement to a safe hex; shot legality must not suppress retreat.
		if(stack.doubleWide() || !stack.hasBonusOfType(BonusType::SHOOTER)
			|| !stack.getPosition().isValid())
			return std::nullopt;

		const int speed = static_cast<int>(stack.getMovementRange());
		if(speed <= 0)
			return std::nullopt;
		if(std::any_of(enemies.begin(), enemies.end(), [](const CStack * enemy)
		{
			return enemy->hasBonusOfType(BonusType::FLYING);
		}))
			return std::nullopt;

		const auto isThreatened = [&](const BattleHex & position, const CStack & enemy)
		{
			const int distance = BattleHex::getDistance(position, enemy.getPosition());
			// fheroes2 considers an unblocked shooter dangerous here only
			// when adjacent, because retreat cannot avoid a ranged attack.
			const int reach = enemy.hasBonusOfType(BonusType::SHOOTER)
				&& battle.battleCanShoot(&enemy) ? 1
				: static_cast<int>(enemy.getMovementRange()) + 1;
			return distance <= reach;
		};

		bool threatened = false;
		for(const auto * enemy : enemies)
		{
			if(!isThreatened(stack.getPosition(), *enemy))
				continue;
			threatened = true;
			const int enemySpeed = static_cast<int>(enemy->getMovementRange());
			if(enemySpeed > 0 && enemySpeed + 2 >= speed)
				return std::nullopt;
		}
		if(!threatened)
			return std::nullopt;

		const auto reachability = battle.getReachability(&stack);
		const auto available = battle.battleGetAvailableHexes(reachability, &stack, false);
		BattleHex safest;
		int safestDistance = -1;
		int smallestCenterDistance = GameConstants::BFIELD_SIZE;
		const BattleHex center(GameConstants::BFIELD_SIZE / 2);
		for(const auto & hex : available)
		{
			if(!hex.isAvailable() || hex == stack.getPosition())
				continue;
			int nearest = GameConstants::BFIELD_SIZE;
			bool safe = true;
			for(const auto * enemy : enemies)
			{
				if(isThreatened(hex, *enemy))
				{
					safe = false;
					break;
				}
				nearest = std::min(nearest, static_cast<int>(BattleHex::getDistance(hex, enemy->getPosition())));
			}
			if(!safe)
				continue;
			const int centerDistance = BattleHex::getDistance(hex, center);
			if(nearest > safestDistance || (nearest == safestDistance
				&& (centerDistance < smallestCenterDistance || (centerDistance == smallestCenterDistance
					&& (!safest.isValid() || hex.toInt() < safest.toInt())))))
			{
				safest = hex;
				safestDistance = nearest;
				smallestCenterDistance = centerDistance;
			}
		}
		return safest.isValid() ? std::optional<BattleAction>(BattleAction::makeMove(&stack, safest))
			: std::nullopt;
	}

	// Pinned fheroes2 archerDecision: if escape failed and melee has blocked
	// the shot, choose among *adjacent* enemies by raw hit-point damage minus
	// expected retaliation. The ordinary melee evaluator instead compares
	// combat-value losses, which is a different decision rule. Native VCMI
	// damage estimates are used because fheroes2's combat simulator differs.
	static std::optional<BattleAction> blockedArcherMelee(CPlayerBattleCallback & battle,
		const CStack & stack, const std::vector<const CStack *> & enemies)
	{
		if(stack.doubleWide() || !stack.hasBonusOfType(BonusType::SHOOTER)
			|| battle.battleCanShoot(&stack) || !stack.getPosition().isValid())
			return std::nullopt;

		const CStack * bestTarget = nullptr;
		BattleHex bestTargetHex;
		double bestDamageDifference = -std::numeric_limits<double>::infinity();
		for(const auto * enemy : enemies)
		{
			if(!enemy->getPosition().isValid()
				|| BattleHex::getDistance(stack.getPosition(), enemy->getPosition()) != 1)
				continue;
			const auto attackHexes = battle.meleeAttackHexes(&stack, enemy, stack.getPosition());
			if(attackHexes.empty())
				continue;
			BattleAttackInfo attack(&stack, enemy, 0, false);
			attack.attackerPos = stack.getPosition();
			DamageEstimation retaliation;
			const auto damage = battle.battleEstimateDamage(attack, &retaliation);
			const double outgoing = (static_cast<double>(damage.damage.min) + damage.damage.max) / 2.0;
			const double incoming = (static_cast<double>(retaliation.damage.min) + retaliation.damage.max) / 2.0;
			const double difference = outgoing - incoming;
			if(!bestTarget || difference > bestDamageDifference)
			{
				bestTarget = enemy;
				bestTargetHex = attackHexes.front();
				bestDamageDifference = difference;
			}
		}
		return bestTarget
			? std::optional<BattleAction>(BattleAction::makeMeleeAttack(&stack, bestTargetHex, stack.getPosition()))
			: std::nullopt;
	}


	// fheroes2 archerDecision values the complete AREA_SHOT footprint.
	// VCMI returns additional targets only: ordinary ranged attacks have
	// no splash, and Magog SHOOTS_ALL_ADJACENT attacks hostiles only.
	// Include the primary target explicitly, without inventing friendly hits
	// or modifying VCMI combat rules. Values are VCMI damage projections.
	struct RangedAreaValue
	{
		bool dangerous = false;
		double priority = 0;
	};

	static RangedAreaValue rangedAreaValue(CPlayerBattleCallback & battle,
		const CStack & shooter, const CStack & target)
	{
		const auto additionalVictims = battle.getAttackedBattleUnits(
			&shooter, &target, target.getPosition(), true);
		double friendlyDamage = 0;
		double enemyDamage = 0;
		double priority = 0;
		const auto evaluate = [&](const battle::Unit & victim)
		{
			BattleAttackInfo info(&shooter, &victim, 0, true);
			info.attackerPos = shooter.getPosition();
			const auto estimate = battle.battleEstimateDamage(info);
			const double health = std::max(0.0, static_cast<double>(victim.getAvailableHealth()));
			const double hitPoints = std::min(health, std::max(0.0,
				(static_cast<double>(estimate.damage.min) + estimate.damage.max) / 2.0));
			const double value = lossValue(victim, estimate);
			if(battle.battleGetOwner(&victim) == battle.battleGetOwner(&shooter))
			{
				friendlyDamage += hitPoints;
				priority -= value;
			}
			else
			{
				enemyDamage += hitPoints;
				priority += value;
			}
		};
		evaluate(target);
		for(const auto * victim : additionalVictims)
			if(victim != &target)
				evaluate(*victim);
		return {Ports::FHeroes2::catastrophicFriendlyFire(friendlyDamage, enemyDamage), priority};
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
			// Ranged-only war machines cannot pass the melee eligibility check.
			return !enemy->alive() || enemy->isInvincible()
				|| (!battle.battleCanShoot(&stack, enemy->getPosition())
					&& !battle.battleCanAttackUnit(&stack, enemy));
		});
		std::sort(enemies.begin(), enemies.end(), [](const CStack * left, const CStack * right) { return left->unitId() < right->unitId(); });
		if(enemies.empty())
			return BattleAction::makeDefend(&stack);
		if(const auto retreat = archerRetreat(battle, stack, enemies))
			return *retreat;
		if(const auto melee = blockedArcherMelee(battle, stack, enemies))
			return *melee;

		const CStack * shootingTarget = nullptr;
		double bestShot = std::numeric_limits<double>::lowest();
		bool hasLegalShot = false;
		for(const auto * enemy : enemies)
		{
			if(!battle.battleCanShoot(&stack, enemy->getPosition()))
				continue;
			hasLegalShot = true;
			const auto area = rangedAreaValue(battle, stack, *enemy);
			if(area.dangerous)
				continue;
			if(area.priority > bestShot)
			{
				bestShot = area.priority;
				shootingTarget = enemy;
			}
		}
		if(shootingTarget)
			return BattleAction::makeShotAttack(&stack, shootingTarget);
		// Do not replace rejected friendly-fire shots with a different target
		// chosen by the generic, target-only melee scorer.
		if(hasLegalShot)
			return BattleAction::makeDefend(&stack);
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
		{
			if(const auto cover = coverThreatenedShooter(battle, stack, reachability, available, enemies))
				return *cover;
			return BattleAction::makeDefend(&stack);
		}
		return approach(battle, stack, reachability, available, enemies);
	}
};
}
