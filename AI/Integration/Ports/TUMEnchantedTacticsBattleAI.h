/*
 * TUMEnchantedTacticsBattleAI.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later.
 *
 * Experimental projection of the documented TUM Enchanted combat improvements:
 * https://github.com/SadnessPower/TUM-Enchanted/blob/475d80bbf3e935ac854bcb6e68ef02019efb8de3/AI%20Improvements.txt
 * The TUM mod patches the original HoMM3 battle AI, so unchanged decisions
 * use our explicitly named partial HoMM3 tactical baseline. The original
 * compiled ERA/DLL implementation is not claimed to have been ported.
 */
#pragma once

#include "HoMM3TacticsBattleAI.h"
#include "SourceTactics.h"

namespace AIIntegration
{
class TUMEnchantedTacticsBattleAI final : public CBattleGameInterface
{
public:
	void initBattleInterface(std::shared_ptr<Environment>, std::shared_ptr<CBattleCallback> callback) override
	{
		if(!callback)
			throw std::invalid_argument("TUMEnchantedTactics requires a battle callback");
		cb = std::move(callback);
		logAi->info("AI_SOURCE_POLICY backend=TUMEnchantedTactics source=475d80bbf3e935ac854bcb6e68ef02019efb8de3 partial=true base=HoMM3OriginalTactics");
	}

	void initBattleInterface(std::shared_ptr<Environment> environment, std::shared_ptr<CBattleCallback> callback,
		AutocombatPreferences) override
	{
		initBattleInterface(std::move(environment), std::move(callback));
	}

	void yourTacticPhase(const BattleID & battleID, int) override
	{
		if(!cb)
			throw std::logic_error("TUMEnchantedTactics is not initialized");
		cb->battleMakeTacticAction(battleID,
			BattleAction::makeEndOFTacticPhase(cb->getBattle(battleID)->battleGetTacticsSide()));
	}

	void activeStack(const BattleID & battleID, const CStack * stack) override
	{
		if(!cb || !stack)
			throw std::logic_error("TUMEnchantedTactics received invalid activation");
		const auto battle = cb->getBattle(battleID);
		if(!battle)
			throw std::logic_error("TUMEnchantedTactics received an unknown battle");

		BattleAction action;
		if(stack->creatureId() == CreatureID::BALLISTA)
			action = chooseBallistaShot(*battle, *stack);
		// Published TUM Hypnotize override: attacking former allies is scored
		// by damage inflicted + retaliation received, not net exchange.
		else if(stack->isHypnotized() && stack->unitOwner() != battle->battleGetOwner(stack))
		{
			if(const auto attack = chooseHypnotizedAttack(*battle, *stack))
				action = *attack;
			else
				action = HoMM3TacticsBattleAI::chooseAction(*battle, *stack, true);
		}
		else if(!stack->isCatapult() && !stack->isFirstAidTent()
			// A ranged creature with no available shot is still a shooter, not
			// a melee screen. The published hold/fortify overrides govern melee.
			&& !stack->isShooter() && shouldProtectShooters(*battle, *stack))
		{
			// AI Improvements.txt explicitly permits waiting for neutral melee
			// while friendly shooters work. Preserve the unit's later-turn
			// option instead of spending its first activation on Defend.
			// A unit that has already waited must not issue a second WAIT.
			action = stack->waited() ? BattleAction::makeDefend(stack)
				: BattleAction::makeWait(stack);
		}
		else if(!stack->isCatapult() && !stack->isFirstAidTent()
			&& !stack->isShooter() && shouldHoldFortifiedSiege(*battle, *stack))
			action = BattleAction::makeDefend(stack);
		else
			action = HoMM3TacticsBattleAI::chooseAction(*battle, *stack, true);

		logAi->debug("AI_PORT_ACTION backend=TUMEnchantedTactics battle=%d unit=%d action=%s",
			battleID.getNum(), stack->unitId(), action.toString());
		cb->battleMakeUnitAction(battleID, action);
	}

private:
	std::shared_ptr<CBattleCallback> cb;

	static bool hasDisabledEffect(const CStack & unit)
	{
		// The generic isFrozen() method checks only STONE_GAZE in VCMI.
		const auto disabledBy = [&](SpellID spell)
		{
			// NOT_ACTIVE subtype identifies Blind, Paralyze or Petrification,
			// even when a creature ability (not a hero spell) applied it.
			// Retain spell-source detection for native spell-effect bonuses.
			return unit.hasBonus(Selector::typeSubtype(BonusType::NOT_ACTIVE, BonusSubtypeID(spell)))
				|| unit.hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
		};
		return !Ports::TUM::ballistaMayTarget(
			disabledBy(SpellID(SpellID::BLIND)),
			disabledBy(SpellID(SpellID::PARALYZE)),
			disabledBy(SpellID(SpellID::STONE_GAZE)));
	}

	static BattleAction chooseBallistaShot(CPlayerBattleCallback & battle, const CStack & stack)
	{
		auto targets = battle.battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY);
		std::sort(targets.begin(), targets.end(), [](const CStack * left, const CStack * right)
		{
			return left->unitId() < right->unitId();
		});
		const CStack * selected = nullptr;
		double bestValue = -1;
		for(const auto * enemy : targets)
		{
			if(!enemy->alive() || enemy->isInvincible() || hasDisabledEffect(*enemy)
				|| !battle.battleCanShoot(&stack, enemy->getPosition()))
				continue;
			// The unpublished TUM targeting formula is unavailable. Score the
			// expected combat value actually removed by a native VCMI shot,
			// not the untouched stack's entire strength. This is a bounded
			// engine-specific projection, not an ERA/DLL-equivalent formula.
			BattleAttackInfo info(&stack, enemy, 0, true);
			info.attackerPos = stack.getPosition();
			DamageEstimation retaliation;
			const auto damage = battle.battleEstimateDamage(info, &retaliation);
			const double health = static_cast<double>(enemy->getAvailableHealth());
			if(health <= 0)
				continue;
			const double expectedDamage = (static_cast<double>(damage.damage.min)
				+ static_cast<double>(damage.damage.max)) / 2.0;
			const double value = static_cast<double>(enemy->estimateCombatValue())
				* std::clamp(expectedDamage, 0.0, health) / health;
			if(!selected || value > bestValue)
			{
				selected = enemy;
				bestValue = value;
			}
		}
		return selected ? BattleAction::makeShotAttack(&stack, selected)
			: BattleAction::makeDefend(&stack);
	}

	// AI Improvements.txt / SPELLS / HYPNOTIZE: attack the original ally
	// with maximum projected outgoing + incoming damage. VCMI computes
	// actual legal positions and combat estimates. The closed-source TUM
	// spell internals are not claimed to have been reproduced.
	static std::optional<BattleAction> chooseHypnotizedAttack(CPlayerBattleCallback & battle, const CStack & stack)
	{
		auto targets = battle.battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY);
		vstd::erase_if(targets, [](const CStack * target)
		{
			return !target->alive() || target->isInvincible() || !target->getPosition().isValid();
		});
		std::sort(targets.begin(), targets.end(), [](const CStack * lhs, const CStack * rhs)
		{
			return lhs->unitId() < rhs->unitId();
		});
		const auto score = [&](const CStack & victim, BattleHex origin, int distance, bool shooting)
		{
			BattleAttackInfo info(&stack, &victim, distance, shooting);
			info.attackerPos = origin;
			DamageEstimation retaliation;
			const auto damage = battle.battleEstimateDamage(info, &retaliation);
			const double targetHealth = static_cast<double>(victim.getAvailableHealth());
			const double dealt = std::min(targetHealth,
				(static_cast<double>(damage.damage.min) + damage.damage.max) / 2.0);
			// A target guaranteed to die from the first hit cannot retaliate.
			// Damage estimation may still return a hypothetical counterstrike;
			// do not reward that physically impossible damage under TUM's
			// published dealt + received Hypnotize preference.
			const bool guaranteedKill = static_cast<double>(damage.damage.min) >= targetHealth;
			const double received = (shooting || guaranteedKill) ? 0.0
				: std::min(static_cast<double>(stack.getAvailableHealth()),
					(static_cast<double>(retaliation.damage.min) + retaliation.damage.max) / 2.0);
			return Ports::TUM::hypnotizedAttackDamageSum(dealt, received);
		};

		// TUM's published Hypnotize priority applies across all legal attack
		// types. A legal shot does not automatically outrank a reachable melee
		// strike whose retaliation makes the combined damage larger.
		std::optional<BattleAction> bestAction;
		double bestScore = -std::numeric_limits<double>::infinity();
		for(const auto * target : targets)
		{
			if(!battle.battleCanShoot(&stack, target->getPosition()))
				continue;
			const double value = score(*target, stack.getPosition(), 0, true);
			if(!bestAction || value > bestScore)
			{
				bestAction = BattleAction::makeShotAttack(&stack, target);
				bestScore = value;
			}
		}
		if(!stack.getPosition().isValid())
			return bestAction;

		const auto reach = battle.getReachability(&stack);
		auto available = battle.battleGetAvailableHexes(reach, &stack, false);
		available.sort([](const BattleHex & lhs, const BattleHex & rhs)
		{
			return lhs.toInt() < rhs.toInt();
		});
		for(const auto * target : targets)
		{
			for(const auto & hex : available)
			{
				if(!hex.isAvailable())
					continue;
				const auto attackedHexes = battle.meleeAttackHexes(&stack, target, hex);
				if(attackedHexes.empty())
					continue;
				const double value = score(*target, hex, reach.distances[hex.toInt()], false);
				if(!bestAction || value > bestScore)
				{
					bestAction = BattleAction::makeMeleeAttack(&stack, attackedHexes.front(), hex);
					bestScore = value;
				}
			}
		}
		return bestAction;
	}

	static bool canReachFriendlyNextTurn(CPlayerBattleCallback & battle,
		const CStack & enemy, const std::vector<const CStack *> & friends)
	{
		// TUM's next-turn threat gate is about stacks able to move, not
		// merely their geometric reach. Native NOT_ACTIVE covers Blind,
		// Paralyze and Petrification even when a creature ability applied it.
		// A disabled neutral must not provoke a pointless melee charge while
		// friendly shooters can exploit the immobilization.
		if(enemy.hasBonusOfType(BonusType::NOT_ACTIVE) || enemy.isFrozen())
			return false;
		const auto reach = battle.getReachability(&enemy);
		for(const auto * friendly : friends)
		{
			if(!friendly->alive())
				continue;
			for(const auto & hex : friendly->getAttackableHexes(&enemy))
			{
				if(!hex.isValid() || !battle.isMeleeAttackPossible(&enemy, friendly, hex))
					continue;
				if(reach.distances[hex.toInt()] <= static_cast<int>(enemy.getMovementRange()))
					return true;
			}
		}
		return false;
	}

	static bool shouldHoldFortifiedSiege(CPlayerBattleCallback & battle, const CStack & active)
	{
		// TUM's published siege adjustment favors remaining behind intact
		// fortifications. With no disclosed plugin source, model only the
		// conservative case: a closed/blocked gate and no enemy inside the walls.
		// A wide defender is sheltered only if both its occupied cells are
		// inside: a head-only check can hold a unit already straddling a wall.
		// Never prevent the defender from responding once the gate opens, an
		// enemy breaches the walls, or a melee enemy is already adjacent.
		if(active.unitSide() != BattleSide::DEFENDER
			|| battle.battleGetDefendedTown() == nullptr
			|| !Ports::TUM::footprintFullyInsideCastle(active.getHexes(), [&](const BattleHex & hex)
			{
				return hex.isValid() && battle.battleIsInsideWalls(hex);
			}))
			return false;
		const auto gate = battle.battleGetGateState();
		if(gate != EGateState::CLOSED && gate != EGateState::BLOCKED)
			return false;
		const auto enemies = battle.battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY);
		if(enemies.empty())
			return false;
		return std::none_of(enemies.begin(), enemies.end(), [&](const CStack * enemy)
		{
			return enemy->alive() && enemy->getPosition().isValid()
				&& (Ports::TUM::footprintBreachesCastle(enemy->getHexes(), [&](const BattleHex & hex)
				{
					return hex.isValid() && battle.battleIsInsideWalls(hex);
				}) || battle.isMeleeAttackPossible(&active, enemy));
		});
	}

	static bool shouldProtectShooters(CPlayerBattleCallback & battle, const CStack & active)
	{
		const auto friends = battle.battleGetStacks(CBattleInfoEssentials::ONLY_MINE);
		const auto enemies = battle.battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY);
		if(enemies.empty() || friends.empty())
			return false;

		Ports::TUM::ShooterProtection state;
		state.controlledByNeutral = battle.battleGetOwner(&active) == PlayerColor::NEUTRAL;
		state.defendingSiege = battle.battleGetDefendedTown() != nullptr
			&& active.unitSide() == BattleSide::DEFENDER;
		state.opponentIsNeutral = std::all_of(enemies.begin(), enemies.end(), [&](const CStack * enemy)
		{
			return battle.battleGetOwner(enemy) == PlayerColor::NEUTRAL;
		});
		// The published TUM condition is about armies having shooters, not
		// the ability to fire in this exact activation. A shooter blocked by
		// adjacent melee is still ranged; our own line must also have ammo
		// and not be completely disabled by Forgetfulness to benefit from
		// waiting. A blinded/paralyzed/petrified shooter has ammunition but
		// cannot take the turn needed to provide ranged support. Do not hold
		// melee for a completely incapacitated firing line. Enemy shooter
		// classification remains intentionally conservative.
		state.hasShooters = std::any_of(friends.begin(), friends.end(), [&](const CStack * friendly)
		{
			if(!friendly->alive() || !friendly->isShooter() || !friendly->canShoot()
				|| friendly->hasBonusOfType(BonusType::NOT_ACTIVE) || friendly->isFrozen())
				return false;
			// In VCMI an adjacent opponent can block every shot despite
			// ammunition and canShoot() remaining true. TUM screening is
			// useful only while at least one ranged ally can actually fire.
			// Engine-specific safeguard, not a claimed closed-DLL formula.
			return std::any_of(enemies.begin(), enemies.end(), [&](const CStack * enemy)
			{
				return enemy->alive() && enemy->getPosition().isValid()
					&& battle.battleCanShoot(friendly, enemy->getPosition());
			});
		});
		state.opponentHasShooters = std::any_of(enemies.begin(), enemies.end(), [&](const CStack * enemy)
		{
			return enemy->alive() && enemy->isShooter();
		});
		if(!state.opponentIsNeutral || !state.hasShooters || state.opponentHasShooters)
			return false;
		state.opponentCanReachAnyFriendlyStack = std::any_of(enemies.begin(), enemies.end(),
			[&](const CStack * enemy)
			{
				return enemy->alive() && canReachFriendlyNextTurn(battle, *enemy, friends);
			});
		return Ports::TUM::holdMeleeForShooters(state);
	}
};
}
