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
#include "../../../lib/mapObjects/CGHeroInstance.h"

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
		// VCMI arrow towers are activated as native shooters with special
		// (non-field) positions. Do not route their turn through the partial
		// HoMM3 movement fallback. The published TUM tower formula is closed;
		// use a bounded engine-damage projection, never a claimed DLL port.
		if(stack->isTurret())
			action = chooseSiegeShooterShot(*battle, *stack, false);
		else if(stack->creatureId() == CreatureID::BALLISTA)
			action = chooseSiegeShooterShot(*battle, *stack, true);
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
			// The published archer screen protects a MOBILE MELEE formation.
			// A modded non-melee stack (e.g. native SIEGE_WEAPON) must not
			// wait for a melee attack it can never make. Ranged creatures
			// remain shooters even when their ammunition is exhausted.
			&& stack->isMeleeAttacker() && !stack->isShooter()
			&& shouldProtectShooters(*battle, *stack))
		{
			// AI Improvements.txt explicitly permits waiting for neutral melee
			// while friendly shooters work. Preserve the unit's later-turn
			// option instead of spending its first activation on Defend.
			// A unit that has already waited must not issue a second WAIT.
			action = stack->waited() ? BattleAction::makeDefend(stack)
				: BattleAction::makeWait(stack);
		}
		else if(!stack->isCatapult() && !stack->isFirstAidTent()
			&& stack->isMeleeAttacker() && !stack->isShooter()
			&& shouldHoldFortifiedSiege(*battle, *stack))
			action = BattleAction::makeDefend(stack);
		else
			action = HoMM3TacticsBattleAI::chooseAction(*battle, *stack, true);

		logAi->debug("AI_PORT_ACTION backend=TUMEnchantedTactics battle=%d unit=%d action=%s",
			battleID.getNum(), stack->unitId(), action.toString());
		cb->battleMakeUnitAction(battleID, action);
	}

private:
	std::shared_ptr<CBattleCallback> cb;

	static bool hasDisabledEffect(const battle::Unit & unit)
	{
		// The generic isFrozen() method checks only STONE_GAZE in VCMI.
		const auto disabledBy = [&](SpellID spell)
		{
			// Only a real native NOT_ACTIVE status disables the target.
			// Match either the status subtype (creature abilities) or its
			// spell source (hero casts). Other bonuses tagged with the same
			// spell source must not falsely make an active target untouchable.
			const auto inactive = Selector::type()(BonusType::NOT_ACTIVE);
			return unit.hasBonus(Selector::typeSubtype(BonusType::NOT_ACTIVE, BonusSubtypeID(spell)))
				|| unit.hasBonus(inactive.And(
					Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))));
		};
		return !Ports::TUM::ballistaMayTarget(
			disabledBy(SpellID(SpellID::BLIND)),
			disabledBy(SpellID(SpellID::PARALYZE)),
			disabledBy(SpellID(SpellID::STONE_GAZE)));
	}

	// Shared legal-target and native damage evaluation for the two distinct
	// siege shooters. Only ballistae have TUM's documented prohibition on
	// breaking Blind, Paralyze or Petrification. Arrow towers do NOT inherit it.
	static BattleAction chooseSiegeShooterShot(CPlayerBattleCallback & battle, const CStack & stack,
		bool preserveDisabledTargets)
	{
		auto targets = battle.battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY);
		std::sort(targets.begin(), targets.end(), [](const CStack * left, const CStack * right)
		{
			return left->unitId() < right->unitId();
		});
		// BattleActionProcessor::performRangedAttack resolves one attack plus
		// native extra shots from the creature and the fighting hero (Artillery).
		// Estimating only the FIRST shot misprices a killable low-health target
		// against a durable target during the same ballista activation.
		// Never project more shots than the war machine has ammunition.
		// With no shots, the native action is DEFEND; do not call clamp()
		// with an upper bound smaller than its lower bound.
		if(!stack.shots.canUse())
			return BattleAction::makeDefend(&stack);
		int64_t attacks = stack.getTotalAttacks(true);
		if(const auto * hero = battle.battleGetFightingHero(stack.unitSide()))
			attacks += hero->valOfBonuses(BonusType::HERO_GRANTS_ATTACKS,
				BonusSubtypeID(stack.creatureId()));
		const int legalShots = static_cast<int>(std::clamp<int64_t>(
			attacks, 1, stack.shots.available()));
		const CStack * selected = nullptr;
		BattleHex selectedAim;
		double bestValue = 0.0;
		for(const auto * enemy : targets)
		{
			if(!enemy->alive() || enemy->isInvincible() || !enemy->getPosition().isValid()
				|| (preserveDisabledTargets && hasDisabledEffect(*enemy)))
				continue;
			// A wide enemy exposes independently legal head and tail shots.
			// VCMI can report different splash victims for each occupied cell.
			for(const auto & aimedHex : enemy->getHexes())
			{
				if(!aimedHex.isValid() || !battle.battleCanShoot(&stack, aimedHex))
					continue;
				// TUM forbids ballista attacks against disabled enemies. VCMI
				// can give a ballista an area shot via mod bonuses: aiming beside
				// Blind/Paralyze/Petrification would still wake that disabled unit.
				// Use the engine's ACTUAL affected-unit list, not distance guesses,
				// before considering this otherwise-legal shot. Arrow towers do
				// not inherit the published ballista-only prohibition.
				// The same native impact list drives both the ballista's
				// disabled-target veto and collateral-aware casualty evaluation.
				// SHOOTS_ALL_ADJACENT currently targets only enemies in VCMI; other
				// native attack effects may report additional or friendly victims.
				const auto impacted = battle.getAttackedBattleUnits(
					&stack, enemy, aimedHex, true);
				if(preserveDisabledTargets && std::any_of(impacted.begin(), impacted.end(),
					[&](const battle::Unit * hit)
					{
						return hit && battle.battleGetOwner(hit) != battle.battleGetOwner(&stack)
							&& hasDisabledEffect(*hit);
					}))
					continue;
				// VCMI resolves ranged FIRST_STRIKE retaliation before the siege
				// machine fires its opening shot. A guaranteed lethal countershot
				// therefore leaves no volley to value. This is a host-engine safety
				// correction, not the undisclosed ERA/DLL targeting formula.
				static const auto rangedFirstStrike = Selector::typeSubtype(
					BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeAll).Or(
					Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeRanged));
				// A nonlethal FIRST_STRIKE shot also removes war-machine HP
				// BEFORE our volley. Carry both native damage bounds into the
				// private survivor previews, rather than scoring a full-strength
				// ballista that the server has already wounded.
				int64_t earlyCounterMin = 0;
				int64_t earlyCounterMax = 0;
				bool preempted = false;
				if(enemy->hasBonus(rangedFirstStrike)
					&& !enemy->hasBonusOfType(BonusType::NOT_ACTIVE)
					&& enemy->hasBonusOfType(BonusType::RANGED_RETALIATION)
					&& enemy->ableToRetaliate()
					&& !stack.hasBonusOfType(BonusType::BLOCKS_RANGED_RETALIATION)
					&& battle.battleCanShoot(enemy, stack.getPosition()))
				{
					BattleAttackInfo preemptive(enemy, &stack, 0, true);
					preemptive.attackerPos = enemy->getPosition();
					preemptive.defenderPos = stack.getPosition();
					const auto counter = battle.battleEstimateDamage(preemptive, nullptr);
					if(counter.damage.min >= stack.getAvailableHealth())
						continue;
					earlyCounterMin = counter.damage.min;
					earlyCounterMax = counter.damage.max;
					preempted = true;
				}
				// The TUM ballista and arrow-tower targeting formulas are not
				// available. Rank legal native shots by projected combat value
				// removed, not the untouched stack's entire strength. This remains
				// an engine-specific partial projection, never ERA/DLL parity.
				const auto projectedLoss = [&](const battle::Unit & hit)
				{
					const double health = static_cast<double>(hit.getAvailableHealth());
					if(health <= 0)
						return 0.0;
					const auto lossFrom = [&](const battle::Unit & attacker)
					{
						BattleAttackInfo info(&attacker, &hit, 0, true);
						info.attackerPos = stack.getPosition();
						info.defenderPos = hit.unitId() == enemy->unitId() ? aimedHex : hit.getPosition();
						const auto damage = battle.battleEstimateDamage(info, nullptr);
						const double expectedDamage = (static_cast<double>(damage.damage.min)
							+ static_cast<double>(damage.damage.max)) / 2.0;
						return Ports::TUM::projectedSiegeVolleyLoss(
							static_cast<double>(hit.estimateCombatValue()), health,
							std::max(0.0, expectedDamage), legalShots);
					};
					if(!preempted)
						return lossFrom(stack);
					const auto survivorLoss = [&](int64_t counterDamage)
					{
						auto survivors = stack.acquireState();
						survivors->damage(counterDamage);
						return survivors->alive() ? lossFrom(*survivors) : 0.0;
					};
					return (survivorLoss(earlyCounterMin) + survivorLoss(earlyCounterMax)) / 2.0;
				};
				// The unpublished TUM targeting formula is not reproduced here.
				// Rank all engine-reported collateral, not just the primary target.
				// Subtract allied injury only when VCMI actually reports a legal
				// friendly victim. No friendly fire is invented for area shots.
				double value = projectedLoss(*enemy);
				for(const auto * hit : impacted)
				{
					if(!hit || !hit->alive() || hit->isInvincible()
						|| hit->unitId() == enemy->unitId())
						continue;
					const double collateral = projectedLoss(*hit);
					value += battle.battleGetOwner(hit) == battle.battleGetOwner(&stack)
						? -collateral : collateral;
				}
				// DEFEND is safer than spending ammunition on net harmful shots.
				// Native VCMI, not a scripted fallback, executes the chosen action.
				if(value > bestValue)
				{
					selected = enemy;
					selectedAim = aimedHex;
					bestValue = value;
				}
			}
		}
		return selected ? (selectedAim == selected->getPosition()
			? BattleAction::makeShotAttack(&stack, selected)
			: BattleAction::makeShotAttack(&stack, selectedAim))
			: BattleAction::makeDefend(&stack);
	}

	// AI Improvements.txt / SPELLS / HYPNOTIZE: attack the original ally
	// with maximum projected outgoing + incoming damage. VCMI computes
	// actual legal positions and combat estimates. The closed-source TUM
	// spell internals are not claimed to have been reproduced.
	static std::optional<BattleAction> chooseHypnotizedAttack(CPlayerBattleCallback & battle, const CStack & stack)
	{
		auto targets = battle.battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY);
		// ONLY_ENEMY reflects the callback's visible side, not the current
		// controller of each unit. Hypnotize can put the active stack itself
		// in that collection: rejecting it explicitly prevents a self-attack.
		// Use native battle ownership for every candidate, since a previously
		// friendly stack may now be controlled by either side.
		vstd::erase_if(targets, [&](const CStack * target)
		{
			return target->unitId() == stack.unitId()
				|| battle.battleGetOwner(target) == battle.battleGetOwner(&stack)
				|| !target->alive() || target->isInvincible()
				|| !target->getPosition().isValid();
		});
		std::sort(targets.begin(), targets.end(), [](const CStack * lhs, const CStack * rhs)
		{
			return lhs->unitId() < rhs->unitId();
		});
		const auto score = [&](const CStack & victim, BattleHex origin, int distance, bool shooting, BattleHex aimedHex)
		{
			// Price the actual occupied cell struck, not always a wide target's head.
			if(!aimedHex.isValid() || !victim.getHexes().contains(aimedHex))
				throw std::logic_error("TUM Hypnotize selected an invalid native aim");
			BattleAttackInfo info(&stack, &victim, distance, shooting);
			info.attackerPos = origin;
			info.defenderPos = aimedHex;
			DamageEstimation retaliation;
			const auto damage = battle.battleEstimateDamage(info, &retaliation);
			const double targetHealth = static_cast<double>(victim.getAvailableHealth());
			double dealt = std::min(targetHealth,
				(static_cast<double>(damage.damage.min) + damage.damage.max) / 2.0);
			// Normally a guaranteed first-hit kill prevents retaliation;
			// the native ranged FIRST_STRIKE exception counters before that
			// first hit, so it must be evaluated separately.
			const bool guaranteedKill = static_cast<double>(damage.damage.min) >= targetHealth;
			// VCMI resolves ranged FIRST_STRIKE before the shooter's opening
			// attack. The victim therefore returns fire with its original
			// strength even if the later shot would be guaranteed lethal.
			static const auto rangedFirstStrike = Selector::typeSubtype(
				BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeAll).Or(
				Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeRanged));
			// FIRST_STRIKE alone does not cause ranged retaliation. The VCMI
			// server counters before the opening shot only when the victim
			// actually has a legal RANGED_RETALIATION action. In particular,
			// do not suppress our splash estimate merely because a non-shooter
			// carries an otherwise inert FIRST_STRIKE bonus.
			const bool counterFirst = shooting && victim.hasBonus(rangedFirstStrike)
				&& !victim.hasBonusOfType(BonusType::NOT_ACTIVE)
				&& victim.hasBonusOfType(BonusType::RANGED_RETALIATION)
				&& victim.ableToRetaliate()
				&& !stack.hasBonusOfType(BonusType::BLOCKS_RANGED_RETALIATION)
				&& battle.battleCanShoot(&victim, stack.getPosition());
			static const auto meleeFirstStrike = Selector::typeSubtype(
				BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeAll).Or(
				Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeMelee));
			// VCMI's melee FIRST_STRIKE retaliates before any ordinary
			// melee attack from a legal native attack hex. meleeAttackHexes()
			// already excludes nonadjacent and long-weapon-only attacks. A
			// double-wide attacker may strike from its tail while the head is
			// more than one hex away, so head-to-aim distance is not a valid
			// retaliation gate (BattleActionProcessor::performAttackSequence).
			const bool meleeCounterFirst = !shooting && victim.hasBonus(meleeFirstStrike)
				&& !victim.hasBonusOfType(BonusType::NOT_ACTIVE)
				&& !stack.hasBonusOfType(BonusType::BLOCKS_RETALIATION)
				&& !stack.isInvincible();
			// A stack that exhausted its counterattacks cannot retaliate, even
			// if a damage preview reports the hypothetical exchange. Retain
			// native VCMI's authoritative retaliation eligibility.
			// The native damage preview omits RANGED_RETALIATION. The VCMI
			// server can counter a shot when the victim can shoot back, is
			// eligible for retaliation and the attacker does not block it.
			// Preserve the first-strike bounds for any native secondary hits.
			int64_t earlyRangedMin = 0;
			int64_t earlyRangedMax = 0;
			int64_t earlyMeleeMin = 0;
			int64_t earlyMeleeMax = 0;
			double received = 0.0;
			if((!guaranteedKill || counterFirst || meleeCounterFirst) && victim.ableToRetaliate())
			{
				if(shooting)
				{
					if(victim.hasBonusOfType(BonusType::RANGED_RETALIATION)
						&& !stack.hasBonusOfType(BonusType::BLOCKS_RANGED_RETALIATION)
						&& battle.battleCanShoot(&victim, stack.getPosition()))
					{
						// Native FIRST_STRIKE resolves the countershot before
						// casualties. For ordinary RANGED_RETALIATION, first reduce
						// the victim on a private state; never modify the live stack.
						if(counterFirst)
						{
							BattleAttackInfo counter(&victim, &stack, 0, true);
							counter.attackerPos = victim.getPosition();
							const auto firstCounter = battle.battleEstimateDamage(counter, nullptr);
							earlyRangedMin = firstCounter.damage.min;
							earlyRangedMax = firstCounter.damage.max;
							received = (static_cast<double>(firstCounter.damage.min)
								+ firstCounter.damage.max) / 2.0;
							// In native VCMI FIRST_STRIKE fires before the hypnotized
							// stack attacks. The counter can kill some or all shooters;
							// its full-strength return shot cannot coexist with our
							// pre-retaliation outgoing-damage estimate. Preview the
							// follow-up from two private post-counter stack states.
							const auto shotAfterCounter = [&](int64_t counterDamage)
							{
								auto survivors = stack.acquireState();
								survivors->damage(counterDamage);
								if(!survivors->alive())
									return 0.0;
								BattleAttackInfo followUp(survivors.get(), &victim, distance, true);
								followUp.attackerPos = origin;
								followUp.defenderPos = aimedHex;
								const auto preview = battle.battleEstimateDamage(followUp, nullptr);
								return std::min(targetHealth,
									(static_cast<double>(preview.damage.min) + preview.damage.max) / 2.0);
							};
							dealt = (shotAfterCounter(firstCounter.damage.min)
								+ shotAfterCounter(firstCounter.damage.max)) / 2.0;
						}
						else
						{
							const auto counterAfterShot = [&](int64_t shotDamage)
							{
								auto remaining = victim.acquireState();
								remaining->damage(shotDamage);
								if(!remaining->alive())
									return DamageEstimation();
								BattleAttackInfo counter(remaining.get(), &stack, 0, true);
								counter.attackerPos = victim.getPosition();
								return battle.battleEstimateDamage(counter, nullptr);
							};
							const auto lowShotCounter = counterAfterShot(damage.damage.min);
							const auto highShotCounter = counterAfterShot(damage.damage.max);
							const double minimum = static_cast<double>(std::min(
								lowShotCounter.damage.min, highShotCounter.damage.min));
							const double maximum = static_cast<double>(std::max(
								lowShotCounter.damage.max, highShotCounter.damage.max));
							received = (minimum + maximum) / 2.0;
						}
					}
				}
				else if(meleeCounterFirst)
				{
					// BattleActionProcessor::performAttackSequence counters before
					// our opening swing. Preview each damage bound on private stack
					// states; a lethal counter prevents outgoing damage entirely.
					BattleAttackInfo counter(&victim, &stack, 0, false);
					counter.attackerPos = victim.getPosition();
					counter.defenderPos = origin;
					const auto firstCounter = battle.battleEstimateDamage(counter, nullptr);
					earlyMeleeMin = firstCounter.damage.min;
					earlyMeleeMax = firstCounter.damage.max;
					received = (static_cast<double>(firstCounter.damage.min)
						+ firstCounter.damage.max) / 2.0;
					const auto dealtAfterCounter = [&](int64_t counterDamage)
					{
						auto survivors = stack.acquireState();
						survivors->damage(counterDamage);
						if(!survivors->alive())
							return 0.0;
						BattleAttackInfo followUp(survivors.get(), &victim, distance, false);
						followUp.attackerPos = origin;
						followUp.defenderPos = aimedHex;
						const auto preview = battle.battleEstimateDamage(followUp, nullptr);
						return std::min(targetHealth,
							(static_cast<double>(preview.damage.min) + preview.damage.max) / 2.0);
					};
					dealt = (dealtAfterCounter(firstCounter.damage.min)
						+ dealtAfterCounter(firstCounter.damage.max)) / 2.0;
				}
				else
					received = (static_cast<double>(retaliation.damage.min)
						+ retaliation.damage.max) / 2.0;
			}
			// Ordinary retaliation includes first-hit casualties; native melee
			// and ranged FIRST_STRIKE occur before those casualties. Multi-hit
			// and other special-attack sequencing remains outside this projection.
			// Native ranged FIRST_STRIKE takes casualties before the opening
			// shot, including every secondary SHOOTS_ALL_ADJACENT victim.
			// Project collateral from the same two PRIVATE survivor states as
			// the primary hit. Never count a splash shot from a dead shooter.
			// This is a VCMI-specific bound, not the unavailable TUM DLL formula.
			double friendlySplashLoss = 0.0;
			if(shooting && stack.hasBonusOfType(BonusType::SHOOTS_ALL_ADJACENT))
			{
				const auto impacted = battle.getAttackedBattleUnits(&stack, &victim, aimedHex, true, origin);
				for(const auto * hit : impacted)
				{
					if(!hit || hit->unitId() == victim.unitId()
						|| !hit->alive() || hit->isInvincible())
						continue;
					const auto projectedSplash = [&](const battle::Unit & attacker)
					{
						BattleAttackInfo splash(&attacker, hit, distance, true);
						splash.attackerPos = origin;
						splash.defenderPos = hit->getPosition();
						const auto preview = battle.battleEstimateDamage(splash, nullptr);
						return std::clamp(
							(static_cast<double>(preview.damage.min) + preview.damage.max) / 2.0,
							0.0, static_cast<double>(hit->getAvailableHealth()));
					};
					const auto afterCounter = [&](int64_t counterDamage)
					{
						auto survivors = stack.acquireState();
						survivors->damage(counterDamage);
						return survivors->alive() ? projectedSplash(*survivors) : 0.0;
					};
					const double loss = counterFirst
						? (afterCounter(earlyRangedMin) + afterCounter(earlyRangedMax)) / 2.0
						: projectedSplash(stack);
					if(battle.battleGetOwner(hit) == battle.battleGetOwner(&stack))
						friendlySplashLoss += loss;
					else
						dealt += loss;
				}
			}
			// A hypnotized attacker that survives melee FIRST_STRIKE can
			// still hit secondary victims with native breath/multihex attacks.
			// Preview these victims from the SAME private post-counter states
			// used for the primary attack, without altering live battle stacks.
			if(!shooting)
			{
				const auto impacted = battle.getAttackedBattleUnits(
					&stack, &victim, aimedHex, false, origin);
				for(const auto * hit : impacted)
				{
					if(!hit || hit->unitId() == victim.unitId()
						|| !hit->alive() || hit->isInvincible())
						continue;
					const auto projectedSplash = [&](const battle::Unit & attacker)
					{
						BattleAttackInfo splash(&attacker, hit, distance, false);
						splash.attackerPos = origin;
						splash.defenderPos = hit->getPosition();
						const auto preview = battle.battleEstimateDamage(splash, nullptr);
						return std::clamp(
							(static_cast<double>(preview.damage.min) + preview.damage.max) / 2.0,
							0.0, static_cast<double>(hit->getAvailableHealth()));
					};
					const auto afterCounter = [&](int64_t counterDamage)
					{
						auto survivors = stack.acquireState();
						survivors->damage(counterDamage);
						return survivors->alive() ? projectedSplash(*survivors) : 0.0;
					};
					const double loss = meleeCounterFirst
						? (afterCounter(earlyMeleeMin) + afterCounter(earlyMeleeMax)) / 2.0
						: projectedSplash(stack);
					if(battle.battleGetOwner(hit) == battle.battleGetOwner(&stack))
						friendlySplashLoss += loss;
					else
						dealt += loss;
				}
			}
			// None of this establishes equivalence with the unavailable ERA/DLL.
			received = std::clamp(received, 0.0, static_cast<double>(stack.getAvailableHealth()));
			return Ports::TUM::hypnotizedAttackDamageSum(dealt, received) - friendlySplashLoss;
		};

		// TUM's published Hypnotize priority applies across all legal attack
		// types. A legal shot does not automatically outrank a reachable melee
		// strike whose retaliation makes the combined damage larger.
		std::optional<BattleAction> bestAction;
		double bestScore = -std::numeric_limits<double>::infinity();
		for(const auto * target : targets)
		{
			// An original ally may occupy two cells. Native VCMI range/damage
			// estimation receives the exact aimed cell, and the two legal shots
			// can have different results. Do not silently assign every wide
			// creature a head-only Hypnotize shot. This is a VCMI projection,
			// not a claim about the unavailable TUM DLL's aiming internals.
			for(const auto & aimedHex : target->getHexes())
			{
				if(!battle.battleCanShoot(&stack, aimedHex))
					continue;
				const double value = score(*target, stack.getPosition(), 0, true, aimedHex);
				if(!bestAction || value > bestScore)
				{
					// Preserve the unit-target native action for a head shot.
					// A tail shot must explicitly aim at that occupied hex.
					bestAction = aimedHex == target->getPosition()
						? BattleAction::makeShotAttack(&stack, target)
						: BattleAction::makeShotAttack(&stack, aimedHex);
					bestScore = value;
				}
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
				const int pathCost = reach.distances[hex.toInt()];
				if(pathCost < 0)
					continue;
				for(const auto & aimedHex : attackedHexes)
				{
					const double value = score(*target, hex, pathCost, false, aimedHex);
					if(!bestAction || value > bestScore)
					{
						bestAction = BattleAction::makeMeleeAttack(&stack, aimedHex, hex);
						bestScore = value;
					}
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
		// Native VCMI battleCanAttackUnit rejects siege weapons as melee
		// attackers. Their geometric footprint/reach can still look dangerous,
		// but a non-melee unit must not break TUM's neutral shooter screen.
		// This is a host-engine legality safeguard, not hidden ERA/DLL logic.
		if(!enemy.isMeleeAttacker()
			|| enemy.hasBonusOfType(BonusType::NOT_ACTIVE) || enemy.isFrozen())
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
				// A legal melee-contact hex can still have no path to it. VCMI
				// records that as a negative distance; it must not count as a
				// next-turn neutral threat that breaks the archer screen.
				if(Ports::TUM::canReachNextTurn(reach.distances[hex.toInt()],
					static_cast<int>(enemy.getMovementRange())))
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
			// Siege weapons (including a ballista) can fire, but they are not
			// the creature archer line protected by TUM's neutral-melee screen.
			// Otherwise a hero with only a ballista can make infantry wait
			// indefinitely for autonomous war-machine fire.
			if(!friendly->alive() || !friendly->isShooter()
				|| friendly->hasBonusOfType(BonusType::SIEGE_WEAPON) || !friendly->canShoot()
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
