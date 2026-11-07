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
#include "HoMM3SpellPolicy.h"
#include "HoMM3CombatValue.h"
#include "../../../lib/CStack.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/callback/CBattleCallback.h"
#include "../../../lib/callback/CBattleGameInterface.h"

namespace AIIntegration
{
class HoMM3TacticsBattleAI final : public CBattleGameInterface
{
public:
	void initBattleInterface(std::shared_ptr<Environment> environment, std::shared_ptr<CBattleCallback> callback) override
	{
		if(!callback)
			throw std::invalid_argument("HoMM3OriginalTactics requires a battle callback");
		cb = std::move(callback);
		env = std::move(environment);
		preferences = {};
		logAi->info("AI_SOURCE_POLICY backend=HoMM3OriginalTactics source=e12906accf96c2662b1abadbc49ddc111991bf21 partial=true projection=VCMI");
	}

	void initBattleInterface(std::shared_ptr<Environment> environment, std::shared_ptr<CBattleCallback> callback,
		AutocombatPreferences prefs) override
	{
		initBattleInterface(std::move(environment), std::move(callback));
		preferences = prefs;
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
		if(preferences.enableSpellsUsage && !stack->isCatapult() && !stack->isFirstAidTent()
			&& !stack->isBallista() && !stack->isAmmoCart() && !stack->isTurret()
			&& !stack->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE))
		{
			if(const auto spell = HoMM3SpellPolicy::choose(battle, env.get(), *stack))
			{
				logAi->debug("AI_PORT_SPELL backend=HoMM3OriginalTactics battle=%d action=%s",
					battleID.getNum(), spell->toString());
				cb->battleMakeSpellAction(battleID, *spell);
				return;
			}
		}
		const auto action = chooseAction(*battle, *stack);
		logAi->debug("AI_PORT_ACTION backend=HoMM3OriginalTactics battle=%d unit=%d action=%s",
			battleID.getNum(), stack->unitId(), action.toString());
		cb->battleMakeUnitAction(battleID, action);
	}

private:
	std::shared_ptr<CBattleCallback> cb;
	std::shared_ptr<Environment> env;
	AutocombatPreferences preferences;

	static int visibleHeroGrantedAttacks(CPlayerBattleCallback & battle, const CStack & attacker)
	{
		// Hypnotize changes control, not unitSide(). The native server grants
		// attacks from the ORIGINAL side's hero, which a player-scoped
		// callback may not inspect. Do not substitute the controller's hero
		// or call battleGetFightingHero on a hidden side (it throws).
		// Keep intrinsic stack attacks when that extra information is hidden.
		const auto perspective = battle.battleGetMySide();
		if(perspective != BattleSide::ALL_KNOWING && perspective != attacker.unitSide())
			return 0;
		if(const auto * hero = battle.battleGetFightingHero(attacker.unitSide()))
			return hero->valOfBonuses(BonusType::HERO_GRANTS_ATTACKS,
				BonusSubtypeID(attacker.creatureId()));
		return 0;
	}

	static double lossValue(const CStack & target, const DamageEstimation & estimate)
	{
		const double health = static_cast<double>(target.getAvailableHealth());
		if(health <= 0)
			return 0;
		const double damage = std::min(health,
			(static_cast<double>(estimate.damage.min) + static_cast<double>(estimate.damage.max)) / 2);
		return static_cast<double>(target.estimateCombatValue()) * std::max(0.0, damage) / health;
	}

	// Source getUnitCombatValue over public native unit facts. VCMI modifiers,
	// creature fight values and damage-reduction bonuses are host projections.
	// Hidden controller Artillery is unknown and retains the neutral factor.
	static double projectedUnitCombatValue(CPlayerBattleCallback & battle, const CStack & unit, bool ranged)
	{
		const auto attackModifier = [](const CStack & stack, bool shooting)
		{
			return static_cast<int64_t>(stack.getAttack(shooting)) - stack.unitType()->getAttack(shooting);
		};
		const auto defenseModifier = [](const CStack & stack)
		{
			return static_cast<int64_t>(stack.getDefense(false)) - stack.unitType()->getDefense(false);
		};
		int64_t lowestAttack = 0;
		int64_t lowestDefense = 0;
		bool first = true;
		int64_t ordinarySideHealth = 0;
		for(const auto * stack : battle.battleGetAllStacks())
		{
			if(!stack->alive() || stack->isTurret())
				continue;
			const auto attack = attackModifier(*stack, battle.battleCanShoot(stack));
			const auto defense = defenseModifier(*stack);
			if(first || attack < lowestAttack)
				lowestAttack = attack;
			if(first || defense < lowestDefense)
				lowestDefense = defense;
			first = false;
			// Original group, not Hypnotize control. Native stationary support
			// machines map to source immobilized; ordinary disabled troops count.
			if(stack->unitSide() == unit.unitSide() && !stack->hasBonusOfType(BonusType::SIEGE_WEAPON)
				&& !stack->isSummoned() && !stack->isAmmoCart() && !stack->isFirstAidTent())
				ordinarySideHealth += stack->isClone() ? 1 : stack->getAvailableHealth();
		}
		Ports::HoMM3::UnitCombatValueFacts facts;
		facts.attackDifference = attackModifier(unit, ranged) - lowestAttack;
		facts.defenseDifference = defenseModifier(unit) - lowestDefense;
		facts.baseFightValue = unit.unitType()->getFightValue();
		facts.ranged = ranged;
		facts.canShoot = battle.battleCanShoot(&unit);
		facts.shooter = unit.isShooter();
		facts.doubleRangedAttack = unit.getTotalAttacks(true) > 1;
		facts.ballista = unit.isBallista();
		if(facts.ballista && (battle.battleGetMySide() == BattleSide::ALL_KNOWING
			|| battle.battleGetMySide() == unit.controllingSide()))
		{
			if(const auto * hero = battle.battleGetFightingHero(unit.controllingSide()))
				facts.artilleryLevel = hero->getSecSkillLevel(SecondarySkill::ARTILLERY);
		}
		const auto allDamage = Selector::typeSubtype(BonusType::GENERAL_DAMAGE_REDUCTION,
			BonusCustomSubtype::damageTypeAll);
		const auto spell = Selector::sourceType()(BonusSource::SPELL_EFFECT);
		const auto factor = [&](const CSelector & selector)
		{
			return std::max(0.0, 1.0 - unit.valOfBonuses(selector) / 100.0);
		};
		facts.defenderDamageFactor = factor(allDamage.And(spell.Not())) * factor(allDamage.And(spell))
			* factor(Selector::typeSubtype(BonusType::GENERAL_DAMAGE_REDUCTION,
				ranged ? BonusCustomSubtype::damageTypeRanged : BonusCustomSubtype::damageTypeMelee));
		const bool blessed = unit.hasBonusOfType(BonusType::ALWAYS_MAXIMUM_DAMAGE);
		const bool cursed = unit.hasBonusOfType(BonusType::ALWAYS_MINIMUM_DAMAGE);
		facts.alteredDamage = blessed || cursed;
		facts.baseAverageDamage = std::max(1.0,
			(static_cast<double>(unit.getMinDamage(ranged)) + unit.getMaxDamage(ranged)) / 2.0);
		const int shift = unit.valOfBonuses(BonusType::ALWAYS_MAXIMUM_DAMAGE)
			- unit.valOfBonuses(BonusType::ALWAYS_MINIMUM_DAMAGE);
		facts.averageDamage = facts.baseAverageDamage;
		if(blessed != cursed)
			facts.averageDamage = std::max(1.0,
				static_cast<double>(blessed ? unit.getMaxDamage(ranged) : unit.getMinDamage(ranged)) + shift);
		facts.siegeOrSummoned = unit.hasBonusOfType(BonusType::SIEGE_WEAPON) || unit.isSummoned();
		facts.totalHitPoints = unit.isClone() ? 1 : unit.getAvailableHealth();
		facts.ordinarySideHitPoints = ordinarySideHealth;
		return Ports::HoMM3::unitCombatValue(facts);
	}

	static int64_t projectedDamage(const DamageEstimation & damage, int64_t available)
	{
		return std::clamp<int64_t>(damage.damage.min
			+ (damage.damage.max - damage.damage.min) / 2, 0, available);
	}

	static double projectedSourceLoss(CPlayerBattleCallback & battle, const CStack & unit,
		bool ranged, int64_t damage)
	{
		// Native zero-damage hits do not destroy clones. The pure source leaf
		// deliberately retains its unconditional Clone branch, including zero.
		if(damage <= 0 || !unit.alive())
			return 0;
		return Ports::HoMM3::lossCombatValue(projectedUnitCombatValue(battle, unit, ranged),
			unit.getCount(), unit.getMaxHealth(), unit.getMaxHealth() - unit.getFirstHPleft(),
			unit.isClone(), std::min(damage, unit.getAvailableHealth()), false);
	}

	static double exchangeValue(CPlayerBattleCallback & battle, const CStack & attacker,
		const CStack & target, BattleHex from, bool shooting, int distance,
		bool projectSecondMeleeStrike = false, bool projectNativeRangedRetaliation = false,
		BattleHex aimedHex = BattleHex())
	{
		const auto oldAttackerHealth = attacker.getAvailableHealth();
		const auto oldTargetHealth = target.getAvailableHealth();
		const auto oldAttackerPosition = attacker.getPosition();
		const auto oldTargetPosition = target.getPosition();
		BattleAttackInfo info(&attacker, &target, distance, shooting);
		info.attackerPos = from;
		// Pinned checkAdjacentHexes considers each occupied enemy hex. The
		// native damage script uses defenderPos when pricing the actual hit,
		// and reverse() uses it to locate the post-hit retaliation. Passing
		// the selected aim avoids evaluating a different wide-target hex.
		// Do not alter the TUM-composed partial baseline's attack preview.
		if(projectSecondMeleeStrike && !shooting)
		{
			if(!aimedHex.isValid() || !target.getHexes().contains(aimedHex))
				throw std::logic_error("HoMM3 selected an invalid native melee aim");
			info.defenderPos = aimedHex;
		}
		DamageEstimation retaliation;
		DamageEstimation nativeRangedCounter;
		auto damage = battle.battleEstimateDamage(info, &retaliation);
		// VCMI performs every native melee strike, including attacks granted by
		// the hero. Pinned HoMM3 simulateAttack models at most the creature's
		// original second strike; later hits are an explicit VCMI extension.
		// Keep this calculation inside the HoMM3-only projection: the TUM
		// composed policy deliberately retains its original partial baseline.
		int totalMeleeAttacks = attacker.getTotalAttacks(false);
		if(projectSecondMeleeStrike && !shooting)
		{
			totalMeleeAttacks += visibleHeroGrantedAttacks(battle, attacker);
		}
		// Retail HoMM3 simulateAttack does not model FIRST_STRIKE. VCMI
		// resolves a preemptive melee retaliation before the first hit, and
		// supports preemptive ranged retaliation with a separate subtype.
		// Match the real server's subtype, disabled-unit and attack gates.
		// This sequential VCMI-only preview is confined to HoMM3: TUM keeps
		// the explicitly composed original partial baseline unchanged.
		const auto firstStrikeFor = [&](BonusCustomSubtype subtype)
		{
			return target.hasBonus(Selector::typeSubtype(BonusType::FIRST_STRIKE,
					BonusCustomSubtype::damageTypeAll)
				.Or(Selector::typeSubtype(BonusType::FIRST_STRIKE, subtype)))
				&& !target.hasBonusOfType(BonusType::NOT_ACTIVE);
		};
		const bool firstStrikeMeleeCounter = projectSecondMeleeStrike && !shooting
			&& firstStrikeFor(BonusCustomSubtype::damageTypeMelee)
			&& !attacker.hasBonusOfType(BonusType::BLOCKS_RETALIATION)
			&& !attacker.isInvincible()
			&& !battle.isLongWeaponAttack(&attacker, &target, from)
			&& target.ableToRetaliate();
		const bool firstStrikeRangedCounter = projectNativeRangedRetaliation && shooting
			&& firstStrikeFor(BonusCustomSubtype::damageTypeRanged)
			&& target.hasBonusOfType(BonusType::RANGED_RETALIATION)
			&& !attacker.hasBonusOfType(BonusType::BLOCKS_RANGED_RETALIATION)
			&& target.ableToRetaliate()
			&& battle.battleCanShoot(&target, from);
		const bool preemptiveCounter = firstStrikeMeleeCounter || firstStrikeRangedCounter;
		DamageEstimation preemptiveDamage;
		if(preemptiveCounter)
		{
			BattleAttackInfo counter(&target, &attacker, 0, shooting);
			counter.attackerPos = target.getPosition();
			counter.defenderPos = from;
			preemptiveDamage = battle.battleEstimateDamage(counter);
			auto survivingAttacker = attacker.acquireState();
			int64_t counterHit = preemptiveDamage.damage.min
				+ (preemptiveDamage.damage.max - preemptiveDamage.damage.min) / 2;
			counterHit = std::clamp<int64_t>(counterHit, 0, survivingAttacker->getAvailableHealth());
			survivingAttacker->damage(counterHit);
			if(survivingAttacker->alive())
			{
				BattleAttackInfo remainingHit(survivingAttacker.get(), &target, distance, shooting);
				remainingHit.attackerPos = from;
				if(!shooting)
					remainingHit.defenderPos = aimedHex;
				damage = battle.battleEstimateDamage(remainingHit);
			}
			else
			{
				damage.damage.min = 0;
				damage.damage.max = 0;
			}
		}
		// Pinned simulateAttack returns immediately when the first hit kills
		// its victim. Some native retaliation previews are independent of that
		// simulated first hit, so never charge an impossible counterattack for
		// a CERTAIN kill. Keep the TUM-composed baseline unchanged until the
		// HoMM3-only sequential exchange is independently validated.
		const bool certainFirstHitKill = ((projectSecondMeleeStrike && !shooting)
				|| (projectNativeRangedRetaliation && shooting))
			&& oldTargetHealth > 0 && damage.damage.min >= oldTargetHealth;
		double retaliationLoss = preemptiveCounter ? lossValue(attacker, preemptiveDamage)
			: (shooting || certainFirstHitKill ? 0.0 : lossValue(attacker, retaliation));
		// Pinned simulateAttack resolves first strike -> retaliation ->
		// optional second strike. Native battleEstimateDamage(..., &retaliation)
		// ALREADY previews post-hit casualties over both damage endpoints
		// for ordinary single-strike melee. Do not replace its native
		// interval by a less reliable representative midpoint. Re-price only
		// when projecting a SECOND attack from detached intermediate states.
		// This is approximate VCMI damage, not retail HoMM3 parity.
		// Leave TUM's explicitly separate baseline untouched.
		if(projectSecondMeleeStrike && !shooting && !firstStrikeMeleeCounter
			&& totalMeleeAttacks > 1 && !certainFirstHitKill
			&& retaliation.damage.max > 0)
		{
			auto afterFirst = target.acquireState();
			int64_t firstHit = damage.damage.min
				+ (damage.damage.max - damage.damage.min) / 2;
			firstHit = std::clamp<int64_t>(firstHit, 0, afterFirst->getAvailableHealth());
			afterFirst->damage(firstHit);
			if(afterFirst->alive())
			{
				BattleAttackInfo counter(afterFirst.get(), &attacker, 0, false);
				counter.attackerPos = target.getPosition();
				counter.defenderPos = from;
				retaliation = battle.battleEstimateDamage(counter);
				retaliationLoss = lossValue(attacker, retaliation);
			}
			else
			{
				retaliationLoss = 0.0;
			}
		}
		// Pinned simulateAttack/getRangedAttackValue has no ranged retaliation:
		// vanilla Heroes III cannot counter a shot. VCMI can. Its generic
		// battleEstimateDamage retaliation output does not include this
		// native ability, so explicitly price the REVERSE legal shot instead.
		// This is a HoMM3-only host-engine extension, not retail source parity;
		// TUM keeps its established source-inspired baseline.
		if(projectNativeRangedRetaliation && shooting && !firstStrikeRangedCounter && !certainFirstHitKill
			&& target.hasBonusOfType(BonusType::RANGED_RETALIATION)
			&& !attacker.hasBonusOfType(BonusType::BLOCKS_RANGED_RETALIATION)
			&& target.ableToRetaliate()
			&& battle.battleCanShoot(&target, attacker.getPosition()))
		{
			// The native return shot occurs AFTER our first hit. Estimating it
			// from the untouched target overprices casualties when the hit
			// removes some of its shooters. Use a detached representative
			// post-hit state, as for the pinned source's sequential melee.
			// This is a VCMI RANGED_RETALIATION extension, not retail parity.
			auto afterShot = target.acquireState();
			int64_t firstHit = damage.damage.min
				+ (damage.damage.max - damage.damage.min) / 2;
			firstHit = std::clamp<int64_t>(firstHit, 0, afterShot->getAvailableHealth());
			afterShot->damage(firstHit);
			if(afterShot->alive())
			{
				BattleAttackInfo counter(afterShot.get(), &attacker, 0, true);
				counter.attackerPos = target.getPosition();
				nativeRangedCounter = battle.battleEstimateDamage(counter);
				retaliationLoss = lossValue(attacker, nativeRangedCounter);
			}
		}
		double score = lossValue(target, damage) - retaliationLoss;
		int64_t totalTargetDamage = projectedDamage(damage, oldTargetHealth);
		int64_t totalAttackerDamage = preemptiveCounter ? projectedDamage(preemptiveDamage, oldAttackerHealth)
			: (certainFirstHitKill ? 0 : projectedDamage(shooting ? nativeRangedCounter : retaliation, oldAttackerHealth));

		// Pinned simulateAttack performs first hit, retaliation, then a second
		// hit if both survive. The host also supports extra creature/hero melee
		// attacks. Replay surviving stacks and price every ordinary follow-up
		// strike, with at most ONE retaliation, matching performAttackSequence.
		// Damage remains a representative native estimate: random rolls,
		// FEROCITY-triggered extra attacks and on-hit effects are not simulated.
		// Do not alter TUM's separate composed HoMM3 baseline.
		if(projectSecondMeleeStrike && !shooting && totalMeleeAttacks > 1)
		{
			const auto expectedDamage = [](const DamageEstimation & estimate, int64_t available)
			{
				const int64_t middle = estimate.damage.min
					+ (estimate.damage.max - estimate.damage.min) / 2;
				return std::clamp<int64_t>(middle, 0, available);
			};
			auto afterAttacker = attacker.acquireState();
			auto afterTarget = target.acquireState();
			if(firstStrikeMeleeCounter)
			{
				int64_t preemptiveLoss = expectedDamage(preemptiveDamage, afterAttacker->getAvailableHealth());
				afterAttacker->damage(preemptiveLoss);
			}
			if(afterAttacker->alive())
			{
				int64_t firstHitLoss = expectedDamage(damage, afterTarget->getAvailableHealth());
				afterTarget->damage(firstHitLoss);
			}
			if(!firstStrikeMeleeCounter && afterTarget->alive())
			{
				int64_t counterLoss = certainFirstHitKill ? 0
					: expectedDamage(retaliation, afterAttacker->getAvailableHealth());
				afterAttacker->damage(counterLoss);
			}
			for(int attackIndex = 1; attackIndex < totalMeleeAttacks
				&& afterAttacker->alive() && afterTarget->alive(); ++attackIndex)
			{
				BattleAttackInfo followup(afterAttacker.get(), afterTarget.get(), 0, false);
				followup.attackerPos = from;
				followup.defenderPos = aimedHex;
				const auto estimate = battle.battleEstimateDamage(followup);
				int64_t additional = expectedDamage(estimate, afterTarget->getAvailableHealth());
				afterTarget->damage(additional);
			}
			totalTargetDamage = oldTargetHealth - afterTarget->getAvailableHealth();
			totalAttackerDamage = oldAttackerHealth - afterAttacker->getAvailableHealth();
		}

		// Retail HoMM3 simulateAttack is single-shot for ranged attacks.
		// The real VCMI server fires additional ranged volleys (including
		// HERO_GRANTS_ATTACKS) only while the attacker, target and ammunition
		// survive. Price these ordinary host-engine shots on detached states.
		// This extension is HoMM3-only: the TUM composed policy keeps its
		// historical partial baseline and no retail parity is claimed.
		if(projectNativeRangedRetaliation && shooting)
		{
			int totalRangedAttacks = attacker.getTotalAttacks(true);
			totalRangedAttacks += visibleHeroGrantedAttacks(battle, attacker);
			totalRangedAttacks = std::min(totalRangedAttacks, attacker.shots.available());
			if(totalRangedAttacks > 1)
			{
				const auto expectedDamage = [](const DamageEstimation & estimate, int64_t available)
				{
					const int64_t middle = estimate.damage.min
						+ (estimate.damage.max - estimate.damage.min) / 2;
					return std::clamp<int64_t>(middle, 0, available);
				};
				auto remainingAttacker = attacker.acquireState();
				auto remainingTarget = target.acquireState();
				if(firstStrikeRangedCounter)
				{
					int64_t counterHit = expectedDamage(preemptiveDamage, remainingAttacker->getAvailableHealth());
					remainingAttacker->damage(counterHit);
				}
				if(remainingAttacker->alive())
				{
					int64_t firstHit = expectedDamage(damage, remainingTarget->getAvailableHealth());
					remainingTarget->damage(firstHit);
				}
				if(!firstStrikeRangedCounter && remainingTarget->alive()
					&& nativeRangedCounter.damage.max > 0)
				{
					int64_t counterHit = expectedDamage(nativeRangedCounter, remainingAttacker->getAvailableHealth());
					remainingAttacker->damage(counterHit);
				}
				for(int attackIndex = 1; attackIndex < totalRangedAttacks
					&& remainingAttacker->alive() && remainingTarget->alive(); ++attackIndex)
				{
					BattleAttackInfo followup(remainingAttacker.get(), remainingTarget.get(), 0, true);
					followup.attackerPos = from;
					const auto estimate = battle.battleEstimateDamage(followup);
					int64_t extraHit = expectedDamage(estimate, remainingTarget->getAvailableHealth());
					remainingTarget->damage(extraHit);
				}
				totalTargetDamage = oldTargetHealth - remainingTarget->getAvailableHealth();
				totalAttackerDamage = oldAttackerHealth - remainingAttacker->getAvailableHealth();
			}
		}
		// Wound credit is nonlinear. Apply it once to each complete exchange,
		// against the original top wound, after all surviving native strikes.
		// No killsOnly gate is inferred from the incomplete tactical forecast.
		if(projectSecondMeleeStrike || projectNativeRangedRetaliation)
			score = projectedSourceLoss(battle, target, shooting, totalTargetDamage)
				- projectedSourceLoss(battle, attacker, shooting, totalAttackerDamage);
		if(oldAttackerHealth != attacker.getAvailableHealth() || oldTargetHealth != target.getAvailableHealth()
			|| oldAttackerPosition != attacker.getPosition() || oldTargetPosition != target.getPosition())
			throw std::logic_error("HoMM3 tactical preview changed a live battle stack");
		if(!std::isfinite(score))
			throw std::logic_error("Nonfinite HoMM3 sequential attack projection");
		return score;
	}

	static int32_t projectedAttackEffect(double value, bool hoMM3SourceSigned)
	{
		if(!std::isfinite(value))
			throw std::logic_error("Nonfinite HoMM3 attack projection");
		// Source getSimpleAttackEffect is signed: a risky return shot may
		// outweigh inflicted damage. The native HoMM3-only projection keeps
		// that negative result for target ranking instead of turning every
		// losing exchange into an identical zero-valued shot. TUM's
		// explicitly composed partial baseline remains unchanged.
		if(hoMM3SourceSigned)
			return Ports::HoMM3::signedProjectedAttackEffect(value);
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
			// Pinned getHexAttackValue requires neighbor->canShoot(attacker),
			// not merely that the shooter can fire at SOME other stack.
			// An unrelated legal shot must not reward pinning a shooter
			// that cannot currently shoot our active attacker.
			if(!enemy->isShooter())
				continue;
			const bool canShootAttacker = battle.battleCanShoot(enemy, attacker.getPosition());
			// getHexAttackValue rewards contact with a shooter only when
			// contact actually removes its shot. Under native VCMI rules,
			// unrestricted FREE_SHOOTING can retain adjacent fire, so a
			// damage-derived pin bonus would otherwise reward a phantom
			// suppression. Restricted free shooting (Steel Elves) still
			// loses its adjacent shot. This is an explicit VCMI projection,
			// not a claim about the retail HoMM3 score.
			const auto restricted = Selector::typeSubtype(BonusType::FREE_SHOOTING,
				BonusCustomSubtype::freeShootingExceptAdjacent);
			const bool canShootAdjacent = !enemy->hasBonus(restricted)
				|| enemy->hasBonus(Selector::type()(BonusType::FREE_SHOOTING).And(restricted.Not()));
			if(!Ports::HoMM3::nativeShooterSuppressionPossible(
				canShootAttacker, enemy->canShootBlocked(), canShootAdjacent))
				continue;
			const auto occupied = enemy->getHexes();
			const auto original = attacker.getHexes();
			if(engagesFootprints(original, occupied))
				continue; // The shooter is already blocked by our stack.
			pins.push_back({occupied, Ports::HoMM3::pinnedShooterValue(
				projectedUnitCombatValue(battle, *enemy, true), projectedUnitCombatValue(battle, *enemy, false),
				enemy->isClone() ? 1 : enemy->getAvailableHealth(), enemy->getMaxHealth())});
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

		// Pinned checkAdjacentHexes scores one multi-head/breath swing. VCMI
		// performAttackSequence may execute another creature or hero-granted
		// swing, provided BOTH primary combatants survive. Replay the native
		// sequence on detached states so later collateral cannot be credited
		// against a dead bystander or after the primary target is destroyed.
		// This is an explicit host extension, not retail HoMM3 equivalence.
		const auto expectedDamage = [](const DamageEstimation & estimate, int64_t available)
		{
			const int64_t midpoint = estimate.damage.min
				+ (estimate.damage.max - estimate.damage.min) / 2;
			return std::clamp<int64_t>(midpoint, 0, available);
		};
		int totalMeleeAttacks = attacker.getTotalAttacks(false);
		totalMeleeAttacks += visibleHeroGrantedAttacks(battle, attacker);
		if(totalMeleeAttacks <= 0)
			return 0.0;

		const auto firstStrikeSelector = Selector::typeSubtype(BonusType::FIRST_STRIKE,
			BonusCustomSubtype::damageTypeAll).Or(Selector::typeSubtype(BonusType::FIRST_STRIKE,
				BonusCustomSubtype::damageTypeMelee));
		const bool firstStrike = target.hasBonus(firstStrikeSelector)
			&& !target.hasBonusOfType(BonusType::NOT_ACTIVE);
		const bool canCounter = !attacker.hasBonusOfType(BonusType::BLOCKS_RETALIATION)
			&& !attacker.isInvincible() && target.ableToRetaliate()
			&& !battle.isLongWeaponAttack(&attacker, &target, from);

		auto remainingAttacker = attacker.acquireState();
		auto remainingPrimary = target.acquireState();
		if(firstStrike && canCounter)
		{
			BattleAttackInfo preemptive(&target, &attacker, 0, false);
			preemptive.attackerPos = target.getPosition();
			preemptive.defenderPos = from;
			const auto damage = battle.battleEstimateDamage(preemptive);
			int64_t counterHit = expectedDamage(damage, remainingAttacker->getAvailableHealth());
			remainingAttacker->damage(counterHit);
			if(!remainingAttacker->alive())
				return 0.0;
		}

		// getAttackedCreatures uses the *real* legal attack-from/aim geometry
		// and de-duplicates a wide stack. Keep each original unit for its
		// original top wound, but use a different detached state for
		// successive hits and cap every victim's cumulative lost health.
		std::vector<std::pair<const CStack *, decltype(target.acquireState())>> secondaryStates;
		for(const auto * affected : battle.getAttackedCreatures(&attacker, attackTo, false, from).first)
		{
			const auto * secondary = dynamic_cast<const CStack *>(affected);
			if(secondary && secondary != &target && secondary != &attacker && secondary->alive())
				secondaryStates.emplace_back(secondary, secondary->acquireState());
		}
		if(secondaryStates.empty())
			return 0.0;

		double value = 0.0;
		for(int attackIndex = 0; attackIndex < totalMeleeAttacks
			&& remainingAttacker->alive() && remainingPrimary->alive(); ++attackIndex)
		{
			const int attackDistance = attackIndex == 0 ? pathCost : 0;
			for(auto & entry : secondaryStates)
			{
				auto & remaining = entry.second;
				if(!remaining->alive())
					continue;
				BattleAttackInfo info(remainingAttacker.get(), remaining.get(), attackDistance, false);
				info.attackerPos = from;
				const auto estimate = battle.battleEstimateDamage(info);
				int64_t lostHealth = expectedDamage(estimate, remaining->getAvailableHealth());
				remaining->damage(lostHealth);
			}

			BattleAttackInfo primary(remainingAttacker.get(), remainingPrimary.get(), attackDistance, false);
			primary.attackerPos = from;
			primary.defenderPos = attackTo;
			DamageEstimation retaliation;
			const auto primaryHit = battle.battleEstimateDamage(primary, &retaliation);
			int64_t firstLoss = expectedDamage(primaryHit, remainingPrimary->getAvailableHealth());
			remainingPrimary->damage(firstLoss);

			// The native server permits at most one ordinary retaliation for a
			// multi-strike activation. FIRST_STRIKE already happened before
			// the opening swing; otherwise a surviving target counters now.
			// Its casualties affect the following collateral previews, but
			// direct counter damage is charged only in exchangeValue.
			if(attackIndex == 0 && !firstStrike && canCounter && remainingPrimary->alive()
				&& retaliation.damage.max > 0)
			{
				BattleAttackInfo counter(remainingPrimary.get(), remainingAttacker.get(), 0, false);
				counter.attackerPos = target.getPosition();
				counter.defenderPos = from;
				const auto counterEstimate = battle.battleEstimateDamage(counter);
				int64_t counterHit = expectedDamage(counterEstimate, remainingAttacker->getAvailableHealth());
				remainingAttacker->damage(counterHit);
			}
		}
		// Aggregate each victim's loss before restoring its original top wound.
		for(const auto & entry : secondaryStates)
		{
			const auto * original = entry.first;
			const auto damage = original->getAvailableHealth() - entry.second->getAvailableHealth();
			value += Ports::HoMM3::collateralStrikeValue(
				battle.battleGetOwner(original) != battle.battleGetOwner(&attacker),
				projectedSourceLoss(battle, *original, false, damage));
		}
		if(!std::isfinite(value))
			throw std::logic_error("Nonfinite HoMM3 sequential collateral projection");
		return value;
	}

	// The pinned checkAdjacentHexes subtracts getMultiHeadBonus and
	// getBreathBonus for a defender which can retaliate. A native VCMI
	// retaliation may also strike OTHER stacks next to the landing hex.
	// The ordinary exchangeValue already prices retaliation against the
	// primary attacker, so only additional victims belong here.
	// This is a conditional, post-hit damage projection, not a HoMM3 formula.
	static double projectedRetaliationCollateral(CPlayerBattleCallback & battle,
		const CStack & attacker, const CStack & target, BattleHex from, int pathCost, BattleHex aimedHex)
	{
		// Pinned checkAdjacentHexes evaluates every legal occupied target hex.
		// In VCMI the same aim must be forwarded to the hypothetical primary
		// strike before estimating whether its surviving defender retaliates.
		if(!aimedHex.isValid() || !target.getHexes().contains(aimedHex))
			throw std::logic_error("HoMM3 retaliation projection requires a native legal aim");
		if(!target.hasBonusOfType(BonusType::ATTACKS_ALL_ADJACENT)
			&& !target.hasBonusOfType(BonusType::THREE_HEADED_ATTACK)
			&& !target.hasBonusOfType(BonusType::TWO_HEX_ATTACK_BREATH)
			&& !target.hasBonusOfType(BonusType::WIDE_BREATH)
			&& !target.hasBonusOfType(BonusType::PRISM_HEX_ATTACK_BREATH)
			&& !target.hasBonusOfType(BonusType::MULTIHEX_UNIT_ATTACK)
			&& !target.hasBonusOfType(BonusType::MULTIHEX_ENEMY_ATTACK))
			return 0.0;

		// Retail checkAdjacentHexes assumes hit -> retaliation. VCMI melee
		// FIRST_STRIKE instead retaliates before the opening hit, at the
		// defender's FULL strength. That preemptive area counter can strike
		// allied bystanders even if the defender would die to our first hit.
		// This is a host-rule correction, not a retail HoMM3 ability.
		const bool preemptive = target.hasBonus(
			Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeAll)
			.Or(Selector::typeSubtype(BonusType::FIRST_STRIKE,
				BonusCustomSubtype::damageTypeMelee)))
			&& !target.hasBonusOfType(BonusType::NOT_ACTIVE)
			&& !attacker.hasBonusOfType(BonusType::BLOCKS_RETALIATION)
			&& !attacker.isInvincible() && target.ableToRetaliate()
			&& !battle.isLongWeaponAttack(&attacker, &target, from);
		auto afterHit = target.acquireState();
		if(!preemptive)
		{
			BattleAttackInfo primary(&attacker, &target, pathCost, false);
			primary.attackerPos = from;
			primary.defenderPos = aimedHex;
			DamageEstimation retaliation;
			const auto initialHit = battle.battleEstimateDamage(primary, &retaliation);
			// For an ordinary counter, the native preview checks whether
			// retaliation is blocked or the first hit certainly kills.
			if(retaliation.damage.max <= 0)
				return 0.0;
			// Estimate the surviving defender at the actual counterattack
			// point. Never mutate the live unit during tactical scoring.
			int64_t received = initialHit.damage.min
				+ (initialHit.damage.max - initialHit.damage.min) / 2;
			received = std::clamp<int64_t>(received, 0, afterHit->getAvailableHealth());
			afterHit->damage(received);
			if(!afterHit->alive())
				return 0.0;
		}

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
			const double loss = projectedSourceLoss(battle, *secondary, false,
				projectedDamage(battle.battleEstimateDamage(counter), secondary->getAvailableHealth()));
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


	// Pinned checkAdjacentHexes uses enemy_attacks[hex], computed from
	// actual target/path information. Cache each hostile unit's native
	// one-activation landing hexes once, before ranking our candidate
	// attacks. A geometric speed radius alone incorrectly projects melee
	// attacks THROUGH closed fortress walls and blocking obstacles.
	// This is still not the original HoMM3 precomputed target/threat map.
	// Each hostile origin has a different native path cost (including charge
	// distance). The source threat map is path-dependent; evaluating damage
	// from a threat's CURRENT hex makes a mobile Cavalier appear stationary.
	// This is still a VCMI estimate, not the retail HoMM3 threat field.
	struct MeleeThreatOrigin
	{
		BattleHex hex;
		int pathCost;
	};

	struct MeleeThreatSource
	{
		const CStack * stack;
		std::vector<MeleeThreatOrigin> reachableOrigins;
		// The board does not change while choosing one HoMM3 action.
		// Cache this shooter's present blockers and intrinsic capability
		// once, not for every possible landing cell and melee target.
		bool shooterReadyAfterMoverLeaves = false;
		bool shooterCanShootBlocked = false;
		bool shooterCanShootAdjacent = false;
		bool shooterHasRangeLimit = false;
		int shooterRange = 0;
	};

	static std::vector<MeleeThreatSource> collectThreatSources(CPlayerBattleCallback & battle,
		const CStack & attacker, const std::vector<const CStack *> & enemies)
	{
		std::vector<MeleeThreatSource> sources;
		sources.reserve(enemies.size());
		for(const auto * enemy : enemies)
		{
			std::vector<MeleeThreatOrigin> reachableOrigins;
			if(enemy->alive() && enemy->getPosition().isValid()
				&& !enemy->isCatapult() && !enemy->isFirstAidTent()
				&& !isIncapacitated(*enemy) && !enemy->hasBonusOfType(BonusType::NOT_ACTIVE)
				&& battle.battleCanAttackUnit(enemy, &attacker))
			{
				const auto reach = battle.getReachability(enemy);
				const auto available = battle.battleGetAvailableHexes(reach, enemy, true);
				reachableOrigins.reserve(available.size());
				for(const auto origin : available)
				{
					if(!origin.isAvailable())
						continue;
					const auto cost = reach.distances[origin.toInt()];
					if(cost >= ReachabilityInfo::INFINITE_DIST)
						continue;
					reachableOrigins.push_back({origin, static_cast<int>(cost)});
				}
			}
			MeleeThreatSource source{enemy, std::move(reachableOrigins)};
			if(enemy->alive() && enemy->isShooter()
				&& !isIncapacitated(*enemy) && !enemy->hasBonusOfType(BonusType::NOT_ACTIVE))
			{
				const auto restricted = Selector::typeSubtype(BonusType::FREE_SHOOTING,
					BonusCustomSubtype::freeShootingExceptAdjacent);
				source.shooterCanShootBlocked = enemy->canShootBlocked();
				source.shooterCanShootAdjacent = !enemy->hasBonus(restricted)
					|| enemy->hasBonus(Selector::type()(BonusType::FREE_SHOOTING).And(restricted.Not()));
				const auto limitedRange = enemy->getBonus(Selector::type()(BonusType::LIMITED_SHOOTING_RANGE));
				source.shooterHasRangeLimit = limitedRange != nullptr;
				source.shooterRange = limitedRange ? limitedRange->val : 0;
				const bool canShootNow = battle.battleCanShoot(enemy);
				// Moving the active stack can release a currently pinned shooter.
				// Count OTHER remaining blockers using actual ownership, also
				// retaining the native Berserk exception.
				const bool berserk = enemy->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE);
				const bool moverPinsNow = engagesFootprints(attacker.getHexes(), enemy->getHexes())
					&& (berserk || battle.battleMatchOwner(enemy, &attacker));
				bool anotherBlockerRemains = false;
				if(!canShootNow && enemy->canShoot() && moverPinsNow)
				{
					for(const auto * unit : battle.battleGetUnitsIf(
						[](const battle::Unit * unit) { return unit->alive(); }))
					{
						if(unit == enemy || unit == &attacker)
							continue;
						if((berserk || battle.battleMatchOwner(enemy, unit))
							&& engagesFootprints(unit->getHexes(), enemy->getHexes()))
						{
							anotherBlockerRemains = true;
							break;
						}
					}
				}
				source.shooterReadyAfterMoverLeaves =
					Ports::HoMM3::projectedShooterReadyAfterMoverLeaves(
						canShootNow, enemy->canShoot(), moverPinsNow, anotherBlockerRemains);
			}
			sources.push_back(std::move(source));
		}
		return sources;
	}

	// Source checkAdjacentHexes adds the precomputed enemy_attacks[hex].
	// VCMI projection: strongest POSSIBLE next action by any other enemy,
	// subject to host pathfinding and hypothetical head/tail contact.
	// Primary target's immediate retaliation is in exchangeValue, so skip it.
	static double projectedCellThreat(CPlayerBattleCallback & battle,
		const CStack & attacker, const CStack & primaryTarget, BattleHex destination,
		BattleHex cell, const std::vector<MeleeThreatSource> & threats)
	{
		// Every enemy may act after the selected melee strike. Count at most
		// one attack per enemy, yet do not discard other enemies entirely.
		// Saturating at the full stack's value avoids fictitious overkill.
		double accumulatedLoss = 0;
		const double stackValue = std::max(
			projectedSourceLoss(battle, attacker, false, attacker.getAvailableHealth()),
			projectedSourceLoss(battle, attacker, true, attacker.getAvailableHealth()));
		for(const auto & threat : threats)
		{
			const auto * enemy = threat.stack;
			double worstEnemyLoss = 0;
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
			// Future shooting depends on the landing footprint; shooter
			// ability, other blockers and range are cached once per decision.
			// Native enemy reachability/attack geometry remains unchanged.
			const bool blocked = engagesFootprints(attacker.getHexes(destination), enemy->getHexes());
			const bool canShoot = Ports::HoMM3::projectedNativeRangedThreat(
				threat.shooterReadyAfterMoverLeaves, blocked,
				threat.shooterCanShootBlocked, threat.shooterCanShootAdjacent,
				threat.shooterHasRangeLimit, threat.shooterRange,
				BattleHex::getDistance(enemy->getPosition(), cell));
			// Project a hostile shot from its actual stationary position,
			// but the VCMI melee strike from EACH legal origin and path cost.
			// JOUSTING/charge modifiers may depend on this cost. Using the current
			// position and zero distance for every legal attack gives the
			// landing threat map a false damage value even when its geometry
			// is correct. Only the strongest possible native attack is priced.
			// checkAdjacentHexes evaluates the future HEAD and TAIL separately.
			if(canShoot)
			{
				BattleAttackInfo info(enemy, &attacker, 0, true);
				info.attackerPos = enemy->getPosition();
				info.defenderPos = cell;
				worstEnemyLoss = std::max(worstEnemyLoss,
					projectedSourceLoss(battle, attacker, true,
						projectedDamage(battle.battleEstimateDamage(info), attacker.getAvailableHealth())));
				// Pinned simulateAttack returns after a single ranged hit,
				// whereas the VCMI host fires ordinary extra volleys.
				// Re-evaluate each against a detached surviving victim; the
				// total is capped by actual available HP. This is a conservative
				// threat bound, not the retail enemy_attacks field: intervening
				// counterfire and random on-hit abilities remain unmodeled.
				// The opponent hero is intentionally hidden from a player-scoped
				// CBattleInfo callback. battleGetFightingHero(enemy->unitSide())
				// would THROW_INVALID_CALL, not return a public bonus value.
				// Therefore count intrinsic/stack attacks only; additional
				// enemy-hero grants remain an explicitly unported information gap.
				const int volleys = std::min(enemy->getTotalAttacks(true), enemy->shots.available());
				if(volleys > 1)
				{
					auto remainingShooter = enemy->acquireState();
					auto remainingVictim = attacker.acquireState();
					for(int shot = 0; shot < volleys && remainingVictim->alive(); ++shot)
					{
						BattleAttackInfo volley(remainingShooter.get(), remainingVictim.get(), 0, true);
						volley.attackerPos = enemy->getPosition();
						volley.defenderPos = cell;
						const auto estimate = battle.battleEstimateDamage(volley);
						int64_t hit = estimate.damage.min
							+ (estimate.damage.max - estimate.damage.min) / 2;
						hit = std::clamp<int64_t>(hit, 0, remainingVictim->getAvailableHealth());
						remainingVictim->damage(hit);
					}
					const auto lost = attacker.getAvailableHealth() - remainingVictim->getAvailableHealth();
					worstEnemyLoss = std::max(worstEnemyLoss,
						projectedSourceLoss(battle, attacker, true, lost));
				}
			}
			for(const auto & origin : threat.reachableOrigins)
			{
				if(!battle.meleeAttackHexes(enemy, &attacker, origin.hex, destination).contains(cell))
					continue;
				BattleAttackInfo info(enemy, &attacker, origin.pathCost, false);
				info.attackerPos = origin.hex;
				info.defenderPos = cell;
				worstEnemyLoss = std::max(worstEnemyLoss,
					projectedSourceLoss(battle, attacker, false,
						projectedDamage(battle.battleEstimateDamage(info), attacker.getAvailableHealth())));
			}
			accumulatedLoss = Ports::HoMM3::addIndependentEnemyThreat(
				accumulatedLoss, worstEnemyLoss, stackValue);
		}
		return -accumulatedLoss;
	}

	static double projectedLandingThreat(CPlayerBattleCallback & battle,
		const CStack & attacker, const CStack & primaryTarget, BattleHex destination,
		const std::vector<MeleeThreatSource> & threats)
	{
		const auto occupied = attacker.getHexes(destination);
		if(occupied.empty())
			return 0;
		const double head = projectedCellThreat(battle, attacker, primaryTarget,
			destination, occupied.front(), threats);
		if(occupied.size() == 1)
			return head;
		const double tail = projectedCellThreat(battle, attacker, primaryTarget,
			destination, occupied.back(), threats);
		return Ports::HoMM3::moreExposedFootprintThreat(head, tail);
	}

	// Retail getRangedAttackValue scores a single victim. VCMI shooters
	// may have SHOOTS_ALL_ADJACENT: the real server can injure other stacks,
	// including friendlies. Price only victims of the ACTUAL native head-aim
	// command (makeShotAttack below); never invent a second aim hex here.
	// This explicitly host-specific first-volley correction is HoMM3-only,
	// not a claim about the original Heroes III projectile policy or
	// additional shots, and leaves TUM's separate partial baseline unchanged.
	static double projectedRangedCollateral(CPlayerBattleCallback & battle,
		const CStack & attacker, const CStack & primaryTarget)
	{
		if(!attacker.hasBonusOfType(BonusType::SHOOTS_ALL_ADJACENT))
			return 0.0;
		// Retail getRangedAttackValue considers one shot; VCMI can fire more,
		// including hero-granted volleys. Mirror the same native ammunition
		// and survival gates as the HoMM3-only primary exchange projection.
		// This remains a host-specific approximation, not source equivalence.
		int totalShots = attacker.getTotalAttacks(true);
		totalShots += visibleHeroGrantedAttacks(battle, attacker);
		totalShots = std::min(totalShots, attacker.shots.available());
		if(totalShots <= 0)
			return 0.0;
		const auto expectedDamage = [](const DamageEstimation & estimate, int64_t available)
		{
			const int64_t midpoint = estimate.damage.min
				+ (estimate.damage.max - estimate.damage.min) / 2;
			return std::clamp<int64_t>(midpoint, 0, available);
		};
		auto remainingAttacker = attacker.acquireState();
		auto remainingPrimary = primaryTarget.acquireState();
		// Retain each original victim for its initial wound and unit value, but
		// estimate later volleys against independent post-hit unit states.
		// Never mutate live battle units while choosing the opening shot.
		std::vector<std::pair<const CStack *, decltype(primaryTarget.acquireState())>> secondaryStates;
		for(const auto * affected : battle.getAttackedBattleUnits(
			&attacker, &primaryTarget, primaryTarget.getPosition(), true))
		{
			const auto * secondary = dynamic_cast<const CStack *>(affected);
			if(secondary && secondary != &primaryTarget && secondary != &attacker && secondary->alive())
				secondaryStates.emplace_back(secondary, secondary->acquireState());
		}
		const bool firstStrike = primaryTarget.hasBonus(
			Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeAll)
			.Or(Selector::typeSubtype(BonusType::FIRST_STRIKE,
				BonusCustomSubtype::damageTypeRanged)))
			&& !primaryTarget.hasBonusOfType(BonusType::NOT_ACTIVE);
		const bool canReturnFire = primaryTarget.hasBonusOfType(BonusType::RANGED_RETALIATION)
			&& !attacker.hasBonusOfType(BonusType::BLOCKS_RANGED_RETALIATION)
			&& primaryTarget.ableToRetaliate()
			&& battle.battleCanShoot(&primaryTarget, attacker.getPosition());
		if(firstStrike && canReturnFire)
		{
			BattleAttackInfo counter(&primaryTarget, &attacker, 0, true);
			counter.attackerPos = primaryTarget.getPosition();
			counter.defenderPos = attacker.getPosition();
			const auto estimate = battle.battleEstimateDamage(counter);
			int64_t preemptiveLoss = expectedDamage(estimate, remainingAttacker->getAvailableHealth());
			remainingAttacker->damage(preemptiveLoss);
		}
		double value = 0.0;
		for(int shot = 0; shot < totalShots && remainingAttacker->alive() && remainingPrimary->alive(); ++shot)
		{
			for(auto & entry : secondaryStates)
			{
				auto & remaining = entry.second;
				if(!remaining->alive())
					continue;
				BattleAttackInfo info(remainingAttacker.get(), remaining.get(), 0, true);
				info.attackerPos = attacker.getPosition();
				const auto estimate = battle.battleEstimateDamage(info);
				int64_t lostHealth = expectedDamage(estimate, remaining->getAvailableHealth());
				remaining->damage(lostHealth);
			}
			BattleAttackInfo primary(remainingAttacker.get(), remainingPrimary.get(), 0, true);
			primary.attackerPos = attacker.getPosition();
			const auto estimate = battle.battleEstimateDamage(primary);
			int64_t primaryLoss = expectedDamage(estimate, remainingPrimary->getAvailableHealth());
			remainingPrimary->damage(primaryLoss);
			// Native ranged retaliation follows the opening volley, and can
			// cancel further splash attacks by killing the shooter. It is not
			// an original HoMM3 ability. One ordinary retaliation per sequence.
			if(shot == 0 && !firstStrike && canReturnFire && remainingPrimary->alive())
			{
				BattleAttackInfo counter(remainingPrimary.get(), remainingAttacker.get(), 0, true);
				counter.attackerPos = primaryTarget.getPosition();
				counter.defenderPos = attacker.getPosition();
				const auto counterEstimate = battle.battleEstimateDamage(counter);
				int64_t counterLoss = expectedDamage(counterEstimate, remainingAttacker->getAvailableHealth());
				remainingAttacker->damage(counterLoss);
			}
		}
		// Aggregate each victim's loss before restoring its original top wound.
		for(const auto & entry : secondaryStates)
		{
			const auto * original = entry.first;
			const auto damage = original->getAvailableHealth() - entry.second->getAvailableHealth();
			value += Ports::HoMM3::collateralStrikeValue(
				battle.battleGetOwner(original) != battle.battleGetOwner(&attacker),
				projectedSourceLoss(battle, *original, true, damage));
		}
		if(!std::isfinite(value))
			throw std::logic_error("Nonfinite HoMM3 ranged collateral projection");
		return value;
	}

	static int32_t rangedScore(CPlayerBattleCallback & battle,
		const CStack & attacker, const CStack & target, bool includeNativeInactive)
	{
		// Native replacement for getSimpleAttackEffect and enemy target-time lookup.
		// The original uses its own projected enemy goal, not hex distance.
		const double direct = exchangeValue(battle, attacker, target,
			attacker.getPosition(), true, 0, false, includeNativeInactive);
		const int32_t effect = projectedAttackEffect(direct + (includeNativeInactive
			? projectedRangedCollateral(battle, attacker, target) : 0.0), includeNativeInactive);
		const int nativeMovement = std::max(0, static_cast<int>(target.getMovementRange()));
		// Retail getRangedAttackValue uses the enemy's global AI goal time,
		// which is not yet ported. In VCMI the target's *actual* reachability
		// is a safer projection than raw hex distance: closed siege walls,
		// obstacles and double-wide movement can make a nearby enemy unable
		// to approach this shooter. TUM retains its old geometric estimate.
		const int oldSpeed = std::max(1, nativeMovement);
		const int oldHeadDistance = BattleHex::getDistance(attacker.getPosition(), target.getPosition());
		const int oldTime = std::max(1, (oldHeadDistance + oldSpeed - 1) / oldSpeed);
		const bool canShootNow = battle.battleCanShoot(&target, attacker.getPosition());
		// Pinned getRangedAttackValue asks whether the ENEMY has an AI target,
		// not whether it can shoot our currently active stack. In a real VCMI
		// battle a limited-range shooter may threaten another friendly stack
		// without being able to fire back at this distant attacker. Preserve
		// that global danger instead of giving it the source's 1/5 discount.
		// Use only legal shots accepted by the native battle callback. TUM's
		// separate source-inspired baseline remains explicitly unchanged.
		bool hasNativeShot = canShootNow;
		if(includeNativeInactive && !hasNativeShot)
		{
			for(const auto * friendly : battle.battleGetStacks(CBattleInfoEssentials::ONLY_MINE))
			{
				if(friendly->alive() && battle.battleCanShoot(&target, friendly->getPosition()))
				{
					hasNativeShot = true;
					break;
				}
			}
		}
		int time = oldTime;
		if(includeNativeInactive)
		{
			const auto nativeReach = battle.getReachability(&target);
			const uint32_t steps = nativeReach.distToNearestNeighbour(&target, &attacker);
			// INFINITE_DIST means there is no native melee path at all. Keep
			// that distinction rather than treating a wall as one hex of travel.
			// projectedRangedTargetTime expects distance = steps-to-contact + 1.
			const int pathDistance = steps >= ReachabilityInfo::INFINITE_DIST
				? ReachabilityInfo::INFINITE_DIST : static_cast<int>(steps) + 1;
			time = Ports::HoMM3::projectedRangedTargetTime(
				pathDistance, nativeMovement, hasNativeShot);
		}
		// Source enemy.cannotAttack includes available ranged attacks. VCMI's
		// battleCanAttackUnit is melee-only: Ballista and other ranged-only
		// units would otherwise be discounted even when they can shoot an ally.
		const bool meleeCapable = battle.battleCanAttackUnit(&target, &attacker);
		const bool canAttack = includeNativeInactive
			? Ports::HoMM3::nativeTargetCanAttack(meleeCapable, hasNativeShot) : meleeCapable;
		return Ports::HoMM3::rangedAttackValue(
			{effect, isIncapacitated(target, includeNativeInactive), !canAttack, canAttack, time}, false);
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
		// Pinned getHexAttackValue checks all hostile neighboring shooters,
		// regardless of whether our current stack can legally attack them.
		// An INVINCIBLE VCMI shooter remains dangerous and may be pinned.
		// Keep the shooter census separate from the attack-target filter.
		// TUM's independent composition retains its old baseline.
		const auto allOpponents = avoidFastDecoys ? std::vector<const CStack *>{}
			: battle.battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY);
		const auto shooterPins = avoidFastDecoys ? std::vector<ShooterPin>{}
			: calculateShooterPins(battle, stack, allOpponents);
		// Reuse native enemy reachability across every candidate hex/aim:
		// this must not run a fresh BFS for each projected threat cell.
		// TUM deliberately receives no HoMM3-only landing threat.
		// Pinned findEnemyAttacks likewise scans *every* live opposing army,
		// including opponents that cannot be selected as our attack target.
		const auto threatSources = avoidFastDecoys ? std::vector<MeleeThreatSource>{}
			: collectThreatSources(battle, stack, allOpponents);

		const CStack * target = nullptr;
		BattleHex attackFrom;
		BattleHex attackTo;
		double bestExchange = -std::numeric_limits<double>::infinity();
		int32_t bestDiscreteExchange = std::numeric_limits<int32_t>::min();
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
					: projectedLandingThreat(battle, stack, *enemy, hex, threatSources);
				// A wide target can expose several distinct legal hit hexes from
				// this landing position. Breath/splash depends on WHICH hex is
				// struck. Pinned checkAdjacentHexes explores each enemy hex;
				// do not silently score only meleeAttackHexes().front().
				// TUM intentionally keeps the historical first-aim baseline.
				for(const auto & aimedHex : attackedHexes)
				{
					const double exchange = exchangeValue(battle, stack, *enemy, hex, false, pathCost, !avoidFastDecoys,
						false, avoidFastDecoys ? BattleHex() : aimedHex)
						+ shooterPinBonus(stack, hex, shooterPins) + landingThreat
						+ (avoidFastDecoys ? 0.0
							: projectedCollateral(battle, stack, *enemy, hex, aimedHex, pathCost)
							+ projectedRetaliationCollateral(battle, stack, *enemy, hex, pathCost, aimedHex));
					// Retail checkAdjacentHexes compares signed long attack-hex
					// scores. Only HoMM3 uses that discrete ranking: the TUM
					// composition retains its historical fractional baseline.
					const int32_t discreteExchange = avoidFastDecoys ? 0
						: Ports::HoMM3::discreteMeleeHexValue(exchange);
					const bool improvesValue = avoidFastDecoys ? exchange > bestExchange
						: discreteExchange > bestDiscreteExchange;
					const bool equalValue = avoidFastDecoys ? exchange == bestExchange
						: discreteExchange == bestDiscreteExchange;
					if(!target || improvesValue || (equalValue
						&& Ports::HoMM3::preferEqualValueMeleeHex(pathCost, bestPathCost, jouster)))
					{
						target = enemy;
						attackFrom = hex;
						attackTo = aimedHex;
						bestExchange = exchange;
						bestDiscreteExchange = discreteExchange;
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
			{
				// A siege weapon is not the slower mobile stack described in
				// TUM's anti-distraction rule. Otherwise its near-zero speed
				// can misclassify every nearby fast creature as bait.
				// An incapacitated enemy likewise cannot currently move.
				if(enemy->hasBonusOfType(BonusType::SIEGE_WEAPON)
					|| enemy->hasBonusOfType(BonusType::NOT_ACTIVE) || enemy->isFrozen())
					continue;
				slowestEnemySpeed = std::min(slowestEnemySpeed, static_cast<int>(enemy->getMovementRange()));
			}
			auto targets = enemies;
			vstd::erase_if(targets, [&](const CStack * enemy)
			{
				// TUM-only: a siege machine or an incapacitated high-speed stack
				// cannot flee as a mobile decoy, irrespective of its speed stat.
				// HoMM3 retains the baseline policy when this flag is false.
				if(enemy->hasBonusOfType(BonusType::SIEGE_WEAPON)
					|| enemy->hasBonusOfType(BonusType::NOT_ACTIVE) || enemy->isFrozen())
					return false;
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
			if(slowestEnemySpeed != std::numeric_limits<int>::max()
				&& !targets.empty() && targets.size() < enemies.size())
			{
				// Only after an actual fast mobile decoy has been excluded,
				// route toward an active, slower mobile unit. A siege machine or
				// incapacitated stack is not a mobile alternative, even though it
				// may still be legally attacked (handled above) or be a legitimate
				// target of the unchanged HoMM3 fallback. In particular, a nearby
				// paralyzed stack must not replace the slow mobile stack that
				// justified TUM's anti-decoy movement rule.
				// This guard runs only with avoidFastDecoys=true (TUM).
				auto mobileAlternatives = targets;
				vstd::erase_if(mobileAlternatives, [](const CStack * enemy)
				{
					return enemy->hasBonusOfType(BonusType::SIEGE_WEAPON)
						|| enemy->hasBonusOfType(BonusType::NOT_ACTIVE) || enemy->isFrozen();
				});
				if(!mobileAlternatives.empty())
					targets = std::move(mobileAlternatives);
				const auto alternate = moveTowardEnemy(battle, stack, reach, available, targets);
				if(alternate.actionType != EActionType::DEFEND)
					return alternate;
			}
		}
		return moveTowardEnemy(battle, stack, reach, available, enemies);
	}
};
}
