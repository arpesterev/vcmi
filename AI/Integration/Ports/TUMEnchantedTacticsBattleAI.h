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
		else if(!stack->isCatapult() && !stack->isFirstAidTent()
			&& !battle->battleCanShoot(stack)
			&& (shouldProtectShooters(*battle, *stack) || shouldHoldFortifiedSiege(*battle, *stack)))
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
		const auto affectedBy = [&](SpellID spell)
		{
			return unit.hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
		};
		return !Ports::TUM::ballistaMayTarget(
			affectedBy(SpellID(SpellID::BLIND)),
			affectedBy(SpellID(SpellID::PARALYZE)),
			affectedBy(SpellID(SpellID::STONE_GAZE)));
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
			// TUM's unpublished tower/ballista target weighting cannot be
			// reconstructed from a compiled plugin; this is a VCMI projection.
			const double value = static_cast<double>(enemy->estimateCombatValue());
			if(!selected || value > bestValue)
			{
				selected = enemy;
				bestValue = value;
			}
		}
		return selected ? BattleAction::makeShotAttack(&stack, selected)
			: BattleAction::makeDefend(&stack);
	}

	static bool canReachFriendlyNextTurn(CPlayerBattleCallback & battle,
		const CStack & enemy, const std::vector<const CStack *> & friends)
	{
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
		// waiting. Enemy archer classification is intentionally conservative.
		state.hasShooters = std::any_of(friends.begin(), friends.end(), [&](const CStack * friendly)
		{
			return friendly->alive() && friendly->isShooter() && friendly->canShoot();
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
