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
		const CStack & target, BattleHex from, bool shooting, int distance,
		bool projectSecondMeleeStrike = false)
	{
		const auto oldAttackerHealth = attacker.getAvailableHealth();
		const auto oldTargetHealth = target.getAvailableHealth();
		const auto oldAttackerPosition = attacker.getPosition();
		const auto oldTargetPosition = target.getPosition();
		BattleAttackInfo info(&attacker, &target, distance, shooting);
		info.attackerPos = from;
		DamageEstimation retaliation;
		const auto damage = battle.battleEstimateDamage(info, &retaliation);
		double score = lossValue(target, damage)
			- (shooting ? 0 : lossValue(attacker, retaliation));

		// Pinned type_AI_combat_parameters::simulateAttack performs a second
		// creatureTwoAttacks melee strike AFTER the defender's first retaliation,
		// provided both stacks survive. Native battleEstimateDamage estimates
		// only one strike. Replay its representative first-hit/retaliation
		// losses on detached unit states before pricing the second VCMI hit.
		// The preview is approximate: random rolls, on-hit effects, first
		// strike and any further modded attacks are not reconstructed here.
		// TUM's explicitly composed partial HoMM3 baseline is unchanged.
		if(projectSecondMeleeStrike && !shooting && attacker.getTotalAttacks(false) > 1)
		{
			const auto expectedDamage = [](const DamageEstimation & estimate, int64_t available)
			{
				const int64_t middle = estimate.damage.min
					+ (estimate.damage.max - estimate.damage.min) / 2;
				return std::clamp<int64_t>(middle, 0, available);
			};
			auto afterAttacker = attacker.acquireState();
			auto afterTarget = target.acquireState();
			int64_t firstHitLoss = expectedDamage(damage, afterTarget->getAvailableHealth());
			int64_t retaliationLoss = expectedDamage(retaliation, afterAttacker->getAvailableHealth());
			afterTarget->damage(firstHitLoss);
			afterAttacker->damage(retaliationLoss);
			if(afterAttacker->alive() && afterTarget->alive())
			{
				BattleAttackInfo second(afterAttacker.get(), afterTarget.get(), 0, false);
				second.attackerPos = from;
				const auto estimate = battle.battleEstimateDamage(second);
				const auto additional = expectedDamage(estimate, afterTarget->getAvailableHealth());
				// Price incremental casualties against the ORIGINAL combat value
				// and HP. Do not count the first-hit losses a second time.
				if(oldTargetHealth > 0)
					score += static_cast<double>(target.estimateCombatValue())
						* static_cast<double>(additional) / static_cast<double>(oldTargetHealth);
			}
		}
		if(oldAttackerHealth != attacker.getAvailableHealth() || oldTargetHealth != target.getAvailableHealth()
			|| oldAttackerPosition != attacker.getPosition() || oldTargetPosition != target.getPosition())
			throw std::logic_error("HoMM3 tactical preview changed a live battle stack");
		if(!std::isfinite(score))
			throw std::logic_error("Nonfinite HoMM3 sequential attack projection");
		return score;
	}

	static int32_t projectedAttackEffect(double value)
	{
		if(!std::isfinite(value))
			throw std::logic_error("Nonfinite HoMM3 attack projection");
		return static_cast<int32_t>(std::clamp(value, 0.0,
			static_cast<double>(std::numeric_limits<int32_t>::max())));
	}

	static bool isIncapacitated(const CStack & target, bool includeNativeInactive = false)
	{
		// HoMM3's army::isIncapacitated includes disables beyond Stone Gaze.
		// VCMI isFrozen() detects Stone Gaze only; inspect actual spell effects
		// so Blind and Paralyze also receive the upstream 1/10 shot weighting.
		const auto hasSpell = [&](SpellID spell)
		{
			return target.hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
		};
		// Pinned army::isIncapacitated checks exactly spell influences
		// 62 (Blind), 70 (Stone Gaze) and 74 (Paralyze). VCMI NOT_ACTIVE
		// carries that spell ID as its subtype, including when a creature
		// ability rather than a hero spell produced the effect. Never treat
		// unrelated NOT_ACTIVE subtypes as HoMM3 incapacitation.
		const auto hasNativeDisable = [&](SpellID spell)
		{
			return target.hasBonus(Selector::typeSubtype(BonusType::NOT_ACTIVE, BonusSubtypeID(spell)));
		};
		const bool sourceDisablingStatus = includeNativeInactive
			&& (hasNativeDisable(SpellID(SpellID::BLIND))
				|| hasNativeDisable(SpellID(SpellID::STONE_GAZE))
				|| hasNativeDisable(SpellID(SpellID::PARALYZE)));
		// The shared TUM baseline retains its preexisting ranged weighting.
		return sourceDisablingStatus || target.isFrozen()
			|| hasSpell(SpellID(SpellID::BLIND)) || hasSpell(SpellID(SpellID::PARALYZE));
	}

	// Source getHexAttackValue rewards an attack hex that pins opposing
	// shooters. Cache the native ranged-minus-melee proxy ONCE per active
	// stack: evaluating damage for every attack-hex/target combination is
	// unnecessarily expensive, especially with many reachable positions.
	// A complete source threat field and original per-unit valuation remain
	// unported. Use both native occupied cells for wide moving stacks.
	struct ShooterPin
	{
		BattleHexArray occupied;
		int32_t value = 0;
	};

	static bool engagesFootprints(const BattleHexArray & moving, const BattleHexArray & shooter)
	{
		return Ports::HoMM3::attackFootprintEngagesShooter(moving, shooter,
			[](BattleHex lhs, BattleHex rhs) { return BattleHex::getDistance(lhs, rhs); });
	}

	static std::vector<ShooterPin> calculateShooterPins(CPlayerBattleCallback & battle,
		const CStack & attacker, const std::vector<const CStack *> & enemies)
	{
		std::vector<ShooterPin> pins;
		for(const auto * enemy : enemies)
		{
			if(!enemy->isShooter() || !battle.battleCanShoot(enemy))
				continue;
			const auto occupied = enemy->getHexes();
			const auto original = attacker.getHexes();
			if(engagesFootprints(original, occupied))
				continue; // The shooter is already blocked by our stack.
			const BattleAttackInfo ranged(enemy, &attacker, 0, true);
			const BattleAttackInfo melee(enemy, &attacker, 0, false);
			const double rangedValue = lossValue(attacker, battle.battleEstimateDamage(ranged));
			const double meleeValue = lossValue(attacker, battle.battleEstimateDamage(melee));
			// These damage-derived values cover the surviving stack as a whole,
			// unlike HoMM3's per-unit value. One effective native stack unit.
			pins.push_back({occupied, Ports::HoMM3::pinnedShooterValue(
				rangedValue, meleeValue, 1, 1)});
		}
		return pins;
	}

	// Pinned getMultiHeadBonus / getBreathBonus score damage to OTHER
	// affected stacks after the primary hit. The exact Heroes III damage
	// formula cannot be used in VCMI; use its authoritative multi-target
	// geometry and independent single-target damage previews instead.
	// This is HoMM3-only: TUM's explicitly composed baseline must not
	// silently inherit this additional valuation.
	static double projectedCollateral(CPlayerBattleCallback & battle, const CStack & attacker,
		const CStack & target, BattleHex from, BattleHex attackTo, int pathCost)
	{
		if(!attacker.hasBonusOfType(BonusType::ATTACKS_ALL_ADJACENT)
			&& !attacker.hasBonusOfType(BonusType::THREE_HEADED_ATTACK)
			&& !attacker.hasBonusOfType(BonusType::TWO_HEX_ATTACK_BREATH)
			&& !attacker.hasBonusOfType(BonusType::WIDE_BREATH)
			&& !attacker.hasBonusOfType(BonusType::PRISM_HEX_ATTACK_BREATH)
			&& !attacker.hasBonusOfType(BonusType::MULTIHEX_UNIT_ATTACK)
			&& !attacker.hasBonusOfType(BonusType::MULTIHEX_ENEMY_ATTACK))
			return 0.0;

		double value = 0.0;
		// The engine de-duplicates double-wide targets. A direct hit is
		// already in exchangeValue; never score it as collateral.
		for(const auto * affected : battle.getAttackedCreatures(&attacker, attackTo, false, from).first)
		{
			const auto * secondary = dynamic_cast<const CStack *>(affected);
			if(!secondary || secondary == &target || secondary == &attacker || !secondary->alive())
				continue;
			BattleAttackInfo info(&attacker, secondary, pathCost, false);
			info.attackerPos = from;
			const double loss = lossValue(*secondary, battle.battleEstimateDamage(info));
			// Ownership, not immutable deployment side: Hypnotize can make
			// a stack friendly or hostile for the current action.
			value += Ports::HoMM3::collateralStrikeValue(
				battle.battleGetOwner(secondary) != battle.battleGetOwner(&attacker), loss);
		}
		if(!std::isfinite(value))
			throw std::logic_error("Nonfinite HoMM3 collateral attack projection");
		return value;
	}

	// The pinned checkAdjacentHexes subtracts getMultiHeadBonus and
	// getBreathBonus for a defender which can retaliate. A native VCMI
	// retaliation may also strike OTHER stacks next to the landing hex.
	// The ordinary exchangeValue already prices retaliation against the
	// primary attacker, so only additional victims belong here.
	// This is a conditional, post-hit damage projection, not a HoMM3 formula.
	static double projectedRetaliationCollateral(CPlayerBattleCallback & battle,
		const CStack & attacker, const CStack & target, BattleHex from, int pathCost)
	{
		if(!target.hasBonusOfType(BonusType::ATTACKS_ALL_ADJACENT)
			&& !target.hasBonusOfType(BonusType::THREE_HEADED_ATTACK)
			&& !target.hasBonusOfType(BonusType::TWO_HEX_ATTACK_BREATH)
			&& !target.hasBonusOfType(BonusType::WIDE_BREATH)
			&& !target.hasBonusOfType(BonusType::PRISM_HEX_ATTACK_BREATH)
			&& !target.hasBonusOfType(BonusType::MULTIHEX_UNIT_ATTACK)
			&& !target.hasBonusOfType(BonusType::MULTIHEX_ENEMY_ATTACK))
			return 0.0;

		BattleAttackInfo primary(&attacker, &target, pathCost, false);
		primary.attackerPos = from;
		DamageEstimation retaliation;
		const auto initialHit = battle.battleEstimateDamage(primary, &retaliation);
		// This also checks the engine's retaliation availability, no-retaliation
		// attacks and death of the primary target. Do not invent a return hit.
		if(retaliation.damage.max <= 0)
			return 0.0;

		// The defender has already taken the first hit before counterattacking.
		// Model a representative surviving state without modifying the live stack.
		// VCMI's exact random damage and effects are not reconstructed here.
		auto afterHit = target.acquireState();
		int64_t received = initialHit.damage.min
			+ (initialHit.damage.max - initialHit.damage.min) / 2;
		received = std::clamp<int64_t>(received, 0, afterHit->getAvailableHealth());
		afterHit->damage(received);
		if(!afterHit->alive())
			return 0.0;

		double score = 0.0;
		// The attacker is NOT yet physically at its prospective landing hex.
		// getAttackedCreatures looks up a live unit at that hex and would
		// silently return nothing. Use the explicit hypothetical defender
		// overload, which preserves native splash geometry without moving it.
		for(const auto * affected : battle.getAttackedBattleUnits(
			&target, &attacker, from, false, target.getPosition(), from))
		{
			const auto * secondary = dynamic_cast<const CStack *>(affected);
			if(!secondary || secondary == &attacker || secondary == &target || !secondary->alive())
				continue;
			BattleAttackInfo counter(afterHit.get(), secondary, 0, false);
			counter.attackerPos = target.getPosition();
			const double loss = lossValue(*secondary, battle.battleEstimateDamage(counter));
			// A collateral ally of OUR attacker is bad; a stack still loyal
			// to the enemy (including under Hypnotize) benefits our exchange.
			score += Ports::HoMM3::collateralStrikeValue(
				battle.battleGetOwner(secondary) != battle.battleGetOwner(&attacker), loss);
		}
		if(!std::isfinite(score))
			throw std::logic_error("Nonfinite HoMM3 retaliation collateral projection");
		return score;
	}

	static double shooterPinBonus(const CStack & attacker, BattleHex from,
		const std::vector<ShooterPin> & pins)
	{
		const auto footprint = attacker.getHexes(from);
		double value = 0;
		// Exactly one entry per enemy shooter even when both our head and tail
		// contact both cells of a wide shooter (source shared checked mask).
		for(const auto & pin : pins)
			if(engagesFootprints(footprint, pin.occupied))
				value += pin.value;
		return value;
	}


	// Source checkAdjacentHexes adds the precomputed enemy_attacks[hex].
	// VCMI projection: strongest POSSIBLE next action by any other enemy.
	// Distances bound reach but do not reconstruct the original threat field.
	// Primary target's immediate retaliation is in exchangeValue, so skip it.
	static double projectedCellThreat(CPlayerBattleCallback & battle,
		const CStack & attacker, const CStack & primaryTarget, BattleHex destination,
		BattleHex cell, const std::vector<const CStack *> & enemies)
	{
		double worstLoss = 0;
		for(const auto * enemy : enemies)
		{
			// The pinned findEnemyAttacks source excludes stacks which cannot
			// act (including the source's incapacitating spell guard). A Blind
			// or Paralyze target is still physically present and may be attacked,
			// but must not project imaginary next-turn melee or ranged damage.
			// This threat projection is HoMM3-only; TUM calls chooseAction
			// with avoidFastDecoys and retains its existing baseline choices.
			if(enemy == &primaryTarget || !enemy->alive()
				|| enemy->isCatapult() || enemy->isFirstAidTent()
				|| isIncapacitated(*enemy) || enemy->hasBonusOfType(BonusType::NOT_ACTIVE))
				continue;
			int distance = std::numeric_limits<int>::max();
			for(const auto & enemyHex : enemy->getHexes())
				distance = std::min(distance, static_cast<int>(BattleHex::getDistance(cell, enemyHex)));
			if(distance == std::numeric_limits<int>::max())
				continue;
			// If our future footprint pins a shooter it cannot shoot us.
			const bool blocked = engagesFootprints(attacker.getHexes(destination), enemy->getHexes());
			const bool canShoot = enemy->isShooter() && !blocked && battle.battleCanShoot(enemy);
			const int movement = std::max(0, static_cast<int>(enemy->getMovementRange()));
			const bool canMelee = distance > 0 && distance <= movement + 1;
			for(const bool shooting : {false, true})
			{
				if((shooting && !canShoot) || (!shooting && !canMelee))
					continue;
				BattleAttackInfo info(enemy, &attacker, 0, shooting);
				info.defenderPos = destination;
				worstLoss = std::max(worstLoss, lossValue(attacker, battle.battleEstimateDamage(info)));
			}
		}
		return -worstLoss;
	}

	static double projectedLandingThreat(CPlayerBattleCallback & battle,
		const CStack & attacker, const CStack & primaryTarget, BattleHex destination,
		const std::vector<const CStack *> & enemies)
	{
		const auto occupied = attacker.getHexes(destination);
		if(occupied.empty())
			return 0;
		const double head = projectedCellThreat(battle, attacker, primaryTarget,
			destination, occupied.front(), enemies);
		if(occupied.size() == 1)
			return head;
		const double tail = projectedCellThreat(battle, attacker, primaryTarget,
			destination, occupied.back(), enemies);
		return Ports::HoMM3::moreExposedFootprintThreat(head, tail);
	}

	static int32_t rangedScore(CPlayerBattleCallback & battle,
		const CStack & attacker, const CStack & target, bool includeNativeInactive)
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
			{effect, isIncapacitated(target, includeNativeInactive), !canRetaliateLater, canRetaliateLater, time}, false);
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
			const int32_t score = rangedScore(battle, stack, *enemy, !avoidFastDecoys);
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

		// TUM explicitly invokes this baseline with avoidFastDecoys=true.
		// Keep that independent experimental backend's existing decisions
		// unchanged while validating this HoMM3-only attack-position slice.
		const auto shooterPins = avoidFastDecoys ? std::vector<ShooterPin>{}
			: calculateShooterPins(battle, stack, enemies);

		const CStack * target = nullptr;
		BattleHex attackFrom;
		BattleHex attackTo;
		double bestExchange = -std::numeric_limits<double>::infinity();
		int bestPathCost = -1;
		// Source ai_tactical.cpp breaks equal-value, same-turn melee ties
		// toward the longer path only for Cavalier/Champion. All positions
		// in 'available' are legal in the current turn (VCMI projection).
		const bool jouster = stack.creatureId() == CreatureID(10) || stack.creatureId() == CreatureID(11);
		for(const auto * enemy : enemies)
		{
			for(const auto & hex : available)
			{
				if(!hex.isAvailable())
					continue;
				const auto attackedHexes = battle.meleeAttackHexes(&stack, enemy, hex);
				if(attackedHexes.empty())
					continue;
				const int pathCost = reach.distances[hex.toInt()];
				if(pathCost < 0)
					continue;
				// Source-inspired signed threat field. The TUM backend
				// composes this baseline with avoidFastDecoys=true; keep its
				// tactical choices unchanged while this HoMM3 slice evolves.
				const double landingThreat = avoidFastDecoys ? 0.0
					: projectedLandingThreat(battle, stack, *enemy, hex, enemies);
				// A wide target can expose several distinct legal hit hexes from
				// this landing position. Breath/splash depends on WHICH hex is
				// struck. Pinned checkAdjacentHexes explores each enemy hex;
				// do not silently score only meleeAttackHexes().front().
				// TUM intentionally keeps the historical first-aim baseline.
				for(const auto & aimedHex : attackedHexes)
				{
					const double exchange = exchangeValue(battle, stack, *enemy, hex, false, pathCost, !avoidFastDecoys)
						+ shooterPinBonus(stack, hex, shooterPins) + landingThreat
						+ (avoidFastDecoys ? 0.0
							: projectedCollateral(battle, stack, *enemy, hex, aimedHex, pathCost)
							+ projectedRetaliationCollateral(battle, stack, *enemy, hex, pathCost));
					if(!target || exchange > bestExchange
						|| (exchange == bestExchange
							&& Ports::HoMM3::preferEqualValueMeleeHex(pathCost, bestPathCost, jouster)))
					{
						target = enemy;
						attackFrom = hex;
						attackTo = aimedHex;
						bestExchange = exchange;
						bestPathCost = pathCost;
					}
					if(avoidFastDecoys)
						break;
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
				// A wide target can be within the decoy radius by its tail
				// even when its head lies outside. Use native occupied cells.
				const int distance = Ports::TUM::nearestFootprintDistance(
					stack.getHexes(), enemy->getHexes(), [](const BattleHex & lhs, const BattleHex & rhs)
					{
						return BattleHex::getDistance(lhs, rhs);
					});
				return Ports::TUM::ignoreNearbyFastDecoyForMovement(
					ownSpeed, static_cast<int>(enemy->getMovementRange()), slowestEnemySpeed, distance);
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
