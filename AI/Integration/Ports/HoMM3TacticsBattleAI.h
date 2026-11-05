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
			if(const auto * hero = battle.battleGetFightingHero(attacker.unitSide()))
				totalMeleeAttacks += hero->valOfBonuses(BonusType::HERO_GRANTS_ATTACKS,
					BonusSubtypeID(attacker.creatureId()));
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
				// All losses are priced against ORIGINAL combat value and health;
				// never recount the first strike or repeat the retaliation.
				if(oldTargetHealth > 0)
					score += static_cast<double>(target.estimateCombatValue())
						* static_cast<double>(additional) / static_cast<double>(oldTargetHealth);
				afterTarget->damage(additional);
			}
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
			if(const auto * hero = battle.battleGetFightingHero(attacker.unitSide()))
				totalRangedAttacks += hero->valOfBonuses(BonusType::HERO_GRANTS_ATTACKS,
					BonusSubtypeID(attacker.creatureId()));
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
					if(oldTargetHealth > 0)
						score += static_cast<double>(target.estimateCombatValue())
							* static_cast<double>(extraHit) / static_cast<double>(oldTargetHealth);
					remainingTarget->damage(extraHit);
				}
			}
		}
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

		// Native FIRST_STRIKE may kill the moving attacker BEFORE its own
		// multi-head or breath attack is executed. The pinned Heroes III
		// checkAdjacentHexes has no preemptive first-strike rule, so this is
		// strictly a VCMI legality correction to the partial port. A certain
		// lethal counter must never earn damage credit against bystanders.
		const auto firstStrikeMelee = target.hasBonus(Selector::typeSubtype(
			BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeAll)
			.Or(Selector::typeSubtype(BonusType::FIRST_STRIKE,
				BonusCustomSubtype::damageTypeMelee)));
		// The counterattack happens before every collateral hit. A nonlethal
		// FIRST_STRIKE can still remove creatures from the attacker, so pricing
		// secondary damage with its untouched strength is also fictitious.
		// Simulate the representative preemptive loss on a DETACHED stack.
		// This is a VCMI host-rule adjustment; pinned getMultiHeadBonus and
		// getBreathBonus do not perform such a preemptive counterattack.
		auto survivingAttacker = attacker.acquireState();
		if(firstStrikeMelee && !target.hasBonusOfType(BonusType::NOT_ACTIVE)
			&& !attacker.hasBonusOfType(BonusType::BLOCKS_RETALIATION)
			&& !attacker.isInvincible() && target.ableToRetaliate()
			&& !battle.isLongWeaponAttack(&attacker, &target, from))
		{
			BattleAttackInfo firstCounter(&target, &attacker, 0, false);
			firstCounter.attackerPos = target.getPosition();
			firstCounter.defenderPos = from;
			const auto counter = battle.battleEstimateDamage(firstCounter);
			int64_t counterHit = counter.damage.min
				+ (counter.damage.max - counter.damage.min) / 2;
			counterHit = std::clamp<int64_t>(counterHit, 0, survivingAttacker->getAvailableHealth());
			survivingAttacker->damage(counterHit);
			if(!survivingAttacker->alive())
				return 0.0;
		}

		double value = 0.0;
		// Use the original stack for native footprint/aim geometry: removing
		// creatures does not move a surviving stack. Use survivingAttacker
		// ONLY for secondary damage, after the preemptive counter.
		// The engine de-duplicates double-wide targets. A direct hit is
		// already in exchangeValue; never score it as collateral.
		for(const auto * affected : battle.getAttackedCreatures(&attacker, attackTo, false, from).first)
		{
			const auto * secondary = dynamic_cast<const CStack *>(affected);
			if(!secondary || secondary == &target || secondary == &attacker || !secondary->alive())
				continue;
			BattleAttackInfo info(survivingAttacker.get(), secondary, pathCost, false);
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

		BattleAttackInfo primary(&attacker, &target, pathCost, false);
		primary.attackerPos = from;
		primary.defenderPos = aimedHex;
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
			// Project the shot at the future occupied cell. A normal archer
			// loses its shot when engaged, but unrestricted FREE_SHOOTING
			// (e.g. Sharpshooter's Bow) still permits adjacent shots.
			// Respect LIMITED_SHOOTING_RANGE, too: querying
			// battleCanShoot(enemy, cell) would reject this currently empty
			// landing hex, not answer the hypothetical attack question.
			const bool blocked = engagesFootprints(attacker.getHexes(destination), enemy->getHexes());
			const auto restricted = Selector::typeSubtype(BonusType::FREE_SHOOTING,
				BonusCustomSubtype::freeShootingExceptAdjacent);
			const bool canShootAdjacent = !enemy->hasBonus(restricted)
				|| enemy->hasBonus(Selector::type()(BonusType::FREE_SHOOTING).And(restricted.Not()));
			const auto limitedRange = enemy->getBonus(Selector::type()(BonusType::LIMITED_SHOOTING_RANGE));
			const bool canShootNow = battle.battleCanShoot(enemy);
			// Moving our active stack can unpin an archer which cannot shoot
			// from the CURRENT board position. Recheck its other blockers
			// rather than treating today's disabled shot as a permanent
			// future state. Berserk units are blocked by either owner.
			const bool berserk = enemy->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE);
			const bool moverPinsNow = engagesFootprints(attacker.getHexes(), enemy->getHexes())
				&& (berserk || battle.battleMatchOwner(enemy, &attacker));
			bool anotherBlockerRemains = false;
			if(!canShootNow && enemy->canShoot() && moverPinsNow)
			{
				for(const auto * unit : battle.battleGetUnitsIf([](const battle::Unit * unit) { return unit->alive(); }))
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
			const bool canShootAfterMove = Ports::HoMM3::projectedShooterReadyAfterMoverLeaves(
				canShootNow, enemy->canShoot(), moverPinsNow, anotherBlockerRemains);
			const bool canShoot = enemy->isShooter()
				&& Ports::HoMM3::projectedNativeRangedThreat(
					canShootAfterMove, blocked, enemy->canShootBlocked(),
					canShootAdjacent, limitedRange != nullptr,
					limitedRange ? limitedRange->val : 0,
					BattleHex::getDistance(enemy->getPosition(), cell));
			const int movement = std::max(0, static_cast<int>(enemy->getMovementRange()));
			// A shooter-only war machine or arrow tower is not a melee attacker,
			// even when its hex borders our prospective landing position. The
			// source's enemy threat census excludes enemies that cannot attack;
			// ask VCMI about the actual unit/owner/melee capability before
			// estimating a hypothetical counterattack. Shooting remains separate.
			const bool canMelee = distance > 0 && distance <= movement + 1
				&& battle.battleCanAttackUnit(enemy, &attacker);
			for(const bool shooting : {false, true})
			{
				if((shooting && !canShoot) || (!shooting && !canMelee))
					continue;
				BattleAttackInfo info(enemy, &attacker, 0, shooting);
				// checkAdjacentHexes evaluates each occupied hex separately.
				// The projected defender still occupies its old position in
				// the live battle, so explicitly aim at the future HEAD or TAIL
				// being scored. A head-only preview would silently discard the
				// second cell's native attack geometry.
				info.defenderPos = cell;
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
		double value = 0.0;
		for(const auto * affected : battle.getAttackedBattleUnits(
			&attacker, &primaryTarget, primaryTarget.getPosition(), true))
		{
			const auto * secondary = dynamic_cast<const CStack *>(affected);
			if(!secondary || secondary == &primaryTarget || secondary == &attacker || !secondary->alive())
				continue;
			BattleAttackInfo info(&attacker, secondary, 0, true);
			const double loss = lossValue(*secondary, battle.battleEstimateDamage(info));
			value += Ports::HoMM3::collateralStrikeValue(
				battle.battleGetOwner(secondary) != battle.battleGetOwner(&attacker), loss);
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
		int distance = std::numeric_limits<int>::max();
		for(const auto & enemyHex : target.getHexes())
			for(const auto & friendlyHex : attacker.getHexes())
				distance = std::min(distance,
					static_cast<int>(BattleHex::getDistance(enemyHex, friendlyHex)));
		if(distance == std::numeric_limits<int>::max())
			throw std::logic_error("HoMM3 ranged target has no valid occupied footprint");
		// Retail getRangedAttackValue uses the enemy's global AI target time;
		// this partial port has no such goal map. Only the independent HoMM3
		// policy projects time to adjacent melee contact, using BOTH occupied
		// footprints and the pinned stationary-unit rule. TUM's explicitly
		// composed partial baseline keeps its prior head-to-head estimate.
		const int oldSpeed = std::max(1, nativeMovement);
		const int oldHeadDistance = BattleHex::getDistance(attacker.getPosition(), target.getPosition());
		const int oldTime = std::max(1, (oldHeadDistance + oldSpeed - 1) / oldSpeed);
		const int time = includeNativeInactive
			? Ports::HoMM3::projectedRangedTargetTime(distance, nativeMovement,
				battle.battleCanShoot(&target, attacker.getPosition())) : oldTime;
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
					const double exchange = exchangeValue(battle, stack, *enemy, hex, false, pathCost, !avoidFastDecoys,
						false, avoidFastDecoys ? BattleHex() : aimedHex)
						+ shooterPinBonus(stack, hex, shooterPins) + landingThreat
						+ (avoidFastDecoys ? 0.0
							: projectedCollateral(battle, stack, *enemy, hex, aimedHex, pathCost)
							+ projectedRetaliationCollateral(battle, stack, *enemy, hex, pathCost, aimedHex));
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
