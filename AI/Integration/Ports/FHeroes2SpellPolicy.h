/*
 * FHeroes2SpellPolicy.h, part of VCMI engine
 *
 * Mana thresholds and damage valuation adapted from fheroes2
 * src/fheroes2/ai/ai_battle_spell.cpp at
 * 0dbfe9f386f33c2500842f4a59cd284d7f03d541.
 * Copyright (C) 2024 - 2026 fheroes2 contributors.
 * License: GNU General Public License v2.0 or later.
 */
#pragma once

#include "SourceTactics.h"
#include "../../BattleAI/StackWithBonuses.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/CStack.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../lib/spells/ISpellMechanics.h"

namespace AIIntegration
{
/// Source-derived spell priorities with VCMI legality and spell-effect simulation.
/// Covers damage, restoration, Haste/Slow, Bless/Curse, Stone Skin, Bloodlust, ranged Shield, Hypnotize, Blind, Berserk and Disrupting Ray using native effects; other buffs, dispels and retreat are not yet ported.
class FHeroes2SpellPolicy
{
	struct Army
	{
		double strength = 0;
		double shooters = 0;
		double weightedSpeed = 0;

		double averageSpeed() const { return strength > 0 ? weightedSpeed / strength : 0; }
	};

	struct UnitSnapshot
	{
		const CStack * unit;
		int64_t health;
		int count;
		BattleHex position;
		PlayerColor owner;
	};

	static Army army(const CPlayerBattleCallback & battle, PlayerColor player, bool friendly)
	{
		Army result;
		for(const auto * unit : battle.battleGetAllStacks())
		{
			if(!unit->alive() || (battle.battleGetOwner(unit) == player) != friendly)
				continue;
			const double strength = static_cast<double>(unit->estimateCombatValue());
			result.strength += strength;
			result.weightedSpeed += strength * unit->getMovementRange();
			// Pinned fheroes2 analyzeBattleState counts movable archers even
			// when a temporary spell or adjacent enemy prevents this turn's shot.
			if(unit->isShooter() && unit->getMovementRange() > 0)
				result.shooters += strength;
		}
		return result;
	}

	static std::vector<spells::Target> targets(const spells::Mechanics & mechanics)
	{
		std::vector<spells::Target> result;
		const auto types = mechanics.getTargetTypes();
		if(types.size() != 1)
			return result;
		if(types.front() == spells::AimType::NOTHING)
		{
			spells::Target target;
			if(mechanics.canBeCastAt(target))
				result.push_back(target);
		}
		else if(types.front() == spells::AimType::CREATURE || types.front() == spells::AimType::LOCATION)
		{
			// Enumerate legal destinations without BattleAI target pruning or random sampling.
			for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
			{
				const BattleHex hex(index);
				if(!hex.isAvailable())
					continue;
				spells::Target target{battle::Destination(hex)};
				if(mechanics.canBeCastAt(target))
					result.push_back(std::move(target));
			}
		}
		return result;
	}

	static double damageValue(const CStack & unit, int64_t damage, const Army & side)
	{
		const double health = static_cast<double>(unit.getAvailableHealth());
		if(damage <= 0 || health <= 0)
			return 0;
		const double strength = static_cast<double>(unit.estimateCombatValue());
		if(static_cast<double>(damage) >= health)
			return strength + side.strength * (unit.getMovementRange() > side.averageSpeed() ? 0.07 : 0.035);
		double fraction = static_cast<double>(damage) / health;
		if(unit.isFrozen())
			fraction += fraction - 1;
		return strength * fraction;
	}

	static void checkUnchanged(const CPlayerBattleCallback & battle, const CGHeroInstance & hero,
		int64_t mana, const std::vector<UnitSnapshot> & before)
	{
		if(static_cast<int64_t>(hero.mana) != mana || battle.battleGetAllStacks(false).size() != before.size())
			throw std::logic_error("Spell evaluation modified live mana or stack membership");
		for(const auto & old : before)
		{
			if(old.unit->getAvailableHealth() != old.health || old.unit->getCount() != old.count
				|| old.unit->getPosition() != old.position || battle.battleGetOwner(old.unit) != old.owner)
				throw std::logic_error("Spell evaluation modified a live battle unit");
		}
	}

public:
	/// Report the source-defined ranged strength used in spell conservation.
	/// Native battle regressions can observe it without changing AI decisions.
	static double sourceArcherStrength(const CPlayerBattleCallback & battle,
		PlayerColor player, bool friendly)
	{
		return army(battle, player, friendly).shooters;
	}

	static std::optional<BattleAction> choose(const std::shared_ptr<CPlayerBattleCallback> & real,
		const Environment * environment)
	{
		const auto * hero = real->battleGetMyHero();
		if(!hero || real->battleCanCastSpell(hero, spells::Mode::HERO) != ESpellCastProblem::OK)
			return std::nullopt;

		const auto player = hero->getOwner();
		const auto ours = army(*real, player, true);
		const auto theirs = army(*real, player, false);
		if(ours.strength <= 0 || theirs.strength <= 0)
			return std::nullopt;

		double threshold = ours.strength * ours.strength / theirs.strength * 0.04;
		if(theirs.shooters / theirs.strength > 0.5)
			threshold *= 0.5;
		if(static_cast<int64_t>(hero->mana) * 2 < hero->manaLimit())
			threshold *= 2;

		std::vector<UnitSnapshot> before;
		for(const auto * unit : real->battleGetAllStacks(false))
			before.push_back({unit, unit->getAvailableHealth(), unit->getCount(), unit->getPosition(), real->battleGetOwner(unit)});
		const int64_t mana = hero->mana;
		const auto enemyCount = std::count_if(before.begin(), before.end(), [&](const UnitSnapshot & old)
		{
			return old.owner != player && old.health > 0;
		});
		std::optional<BattleAction> best;
		double bestValue = 0;

		for(const auto & entry : LIBRARY->spellh->objects)
		{
			const auto * spell = entry.get();
			const bool resurrection = spell->id == SpellID::RESURRECTION || spell->id == SpellID::ANIMATE_DEAD;
			const bool speedSpell = spell->id == SpellID::HASTE || spell->id == SpellID::SLOW;
			const bool defenseSpell = spell->id == SpellID::DISRUPTING_RAY;
			const bool damageRangeSpell = spell->id == SpellID::BLESS || spell->id == SpellID::CURSE;
			const bool defenseBuff = spell->id == SpellID::STONE_SKIN;
			const bool attackBuff = spell->id == SpellID::BLOODLUST;
			const bool rangedShield = spell->id == SpellID::AIR_SHIELD;
			const bool hypnotizeSpell = spell->id == SpellID::HYPNOTIZE;
			const bool disablingSpell = spell->id == SpellID::BLIND;
			const bool berserkSpell = spell->id == SpellID::BERSERK;
			if(!spell->isCombat() || spell->isCreatureAbility()
				|| (!spell->isDamage() && !resurrection && !speedSpell && !defenseSpell && !damageRangeSpell && !defenseBuff && !attackBuff && !rangedShield && !hypnotizeSpell && !disablingSpell && !berserkSpell && spell->id != SpellID::CURE)
				|| !spell->canBeCast(real.get(), spells::Mode::HERO, hero))
				continue;
			if(!environment)
				throw std::logic_error("Spell evaluation requires a native VCMI environment");
			const int cost = real->battleGetSpellCost(spell, hero);
			if(cost < 0 || static_cast<int64_t>(cost) > mana)
				continue;
			spells::BattleCast legality(real.get(), hero, spells::Mode::HERO, spell);
			const auto mechanics = spell->battleMechanics(&legality);
			for(const auto & target : targets(*mechanics))
			{
				auto projected = std::make_shared<HypotheticBattle>(environment, real);
				spells::BattleCast cast(projected.get(), hero, spells::Mode::HERO, spell);
				cast.castEval(projected->getServerCallback(), target);
				checkUnchanged(*real, *hero, mana, before);

				double value = 0;
				int64_t friendlyHealth = 0;
				for(const auto & old : before)
				{
					const auto * after = projected->battleGetUnitByID(old.unit->unitId());
					const int64_t health = after && after->alive() ? after->getAvailableHealth() : 0;
					const bool friendly = old.owner == player;
					if(friendly)
						friendlyHealth += health;
					if(health < old.health)
						value += (friendly ? -1 : 1) * damageValue(*old.unit, old.health - health, friendly ? ours : theirs);
					else if(health > old.health && after && !after->isClone())
					{
						const double restored = static_cast<double>(after->estimateCombatValue())
							* static_cast<double>(health - old.health) / static_cast<double>(health);
						value += friendly ? restored : -restored;
					}
					// Pinned fheroes2 values Disrupting Ray against enemy defense.
					// Use VCMI's post-cast defense to exclude immunity, minimum-
					// defense and no-op effects. No source-equivalence claim is made.
					if(defenseSpell && !friendly && after && after->alive() && health == old.health)
					{
						const double enemyStrength = static_cast<double>(old.unit->estimateCombatValue());
						value += enemyStrength * Ports::FHeroes2::disruptingRayDefenseRatio(
							old.unit->getDefense(false), after->getDefense(false),
							ours.strength, enemyStrength);
					}
					// fheroes2 spellEffectValue gives Bless/Curse 15% of target
					// strength, doubled when removing the opposite effect.
					// Native VCMI keeps getMinDamage/getMaxDamage unchanged:
					// Bless and Curse control the damage *roll* through
					// ALWAYS_MAXIMUM_DAMAGE / ALWAYS_MINIMUM_DAMAGE bonuses.
					// Only value a newly applied projected roll-mode bonus,
					// not fixed damage or a resisted/already-active no-op.
					if(damageRangeSpell && after && after->alive() && health == old.health && old.unit->unitType())
					{
						const bool blessing = spell->id == SpellID::BLESS;
						if((blessing && friendly) || (!blessing && !friendly))
						{
							const auto bonus = blessing ? BonusType::ALWAYS_MAXIMUM_DAMAGE : BonusType::ALWAYS_MINIMUM_DAMAGE;
							const bool changed = after->hasBonusOfType(bonus) && !old.unit->hasBonusOfType(bonus);
							const SpellID opposite(blessing ? SpellID::CURSE : SpellID::BLESS);
							const bool counteracting = old.unit->hasBonus(Selector::source(
								BonusSource::SPELL_EFFECT, BonusSourceID(opposite)));
							const auto * creature = old.unit->unitType();
							value += static_cast<double>(old.unit->estimateCombatValue())
								* Ports::FHeroes2::blessCurseDamageRatio(
									creature->getBaseDamageMin(), creature->getBaseDamageMax(),
									counteracting, changed);
						}
					}
					// Pinned fheroes2 spellEffectValue assigns Stone Skin 10% of
					// allied stack strength. Only accept VCMI's observed defense
					// gain; immunities and already-active buffs must score zero.
					if(defenseBuff && friendly && after && after->alive() && health == old.health)
					{
						value += static_cast<double>(old.unit->estimateCombatValue())
							* Ports::FHeroes2::stoneSkinDefenseRatio(
								old.unit->getDefense(false), after->getDefense(false));
					}
					// Heroes II Shield's original source ratio protects against
					// ranged damage. Heroes III SHIELD is a melee reduction; VCMI
					// AIR_SHIELD is the analogous ranged-protection effect.
					// A hypothetical cast must actually add that native effect.
					if(rangedShield && friendly && after && after->alive() && health == old.health)
					{
						const auto shieldEffect = Selector::source(BonusSource::SPELL_EFFECT,
							BonusSourceID(SpellID(SpellID::AIR_SHIELD)));
						const bool newlyProtected = after->hasBonus(shieldEffect)
							&& !old.unit->hasBonus(shieldEffect);
						value += static_cast<double>(old.unit->estimateCombatValue())
							* Ports::FHeroes2::shieldRangedRatio(theirs.shooters,
								theirs.strength, old.unit->isShooter(), newlyProtected);
					}
					// Pinned fheroes2 spellEffectValue gives Bloodlust 10% of
					// friendly stack strength. VCMI applies the attack bonus only
					// to melee: score the actual post-cast melee attack increase.
					// Resisted and already-active effects must not use mana.
					if(attackBuff && friendly && after && after->alive() && health == old.health)
					{
						value += static_cast<double>(old.unit->estimateCombatValue())
							* Ports::FHeroes2::bloodlustAttackRatio(
								old.unit->getAttack(false), after->getAttack(false));
					}
					// Pinned fheroes2 spellEffectValue gives Hypnotize 1.5
					// times enemy strength. Require actual VCMI control transfer
					// after a hypothetical cast: mere spell eligibility or a
					// resisted/ineffectual cast never justifies spending mana.
					if(hypnotizeSpell && !friendly && after && after->alive() && health == old.health)
					{
						const auto effect = [](SpellID id)
						{
							return Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(id));
						};
						const bool alreadyDisabled = old.unit->isFrozen()
							|| old.unit->hasBonus(effect(SpellID(SpellID::BLIND)))
							|| old.unit->hasBonus(effect(SpellID(SpellID::PARALYZE)));
						const bool newlyControlled = after->isHypnotized()
							&& !old.unit->isHypnotized()
							&& projected->battleGetOwner(after) == player;
						value += static_cast<double>(old.unit->estimateCombatValue())
							* Ports::FHeroes2::hypnotizeControlRatio(alreadyDisabled, newlyControlled);
					}
					// Pinned fheroes2 spellEffectValue values Blind at 0.8 of
					// enemy strength (0.4 for a retaliating last enemy).
					// Require a new, real VCMI Blind bonus after simulation;
					// never value a resisted cast or another active disable.
					if(disablingSpell && !friendly && after && after->alive() && health == old.health)
					{
						const auto hasEffect = [&](const auto * unit, SpellID effect)
						{
							return unit->hasBonus(Selector::source(
								BonusSource::SPELL_EFFECT, BonusSourceID(effect)));
						};
						const bool alreadyDisabled = old.unit->isFrozen()
							|| hasEffect(old.unit, SpellID(SpellID::BLIND))
							|| hasEffect(old.unit, SpellID(SpellID::PARALYZE));
						const bool newBlind = hasEffect(after, SpellID(SpellID::BLIND))
							&& !hasEffect(old.unit, SpellID(SpellID::BLIND));
						value += static_cast<double>(old.unit->estimateCombatValue())
							* Ports::FHeroes2::blindControlRatio(
								enemyCount == 1,
								old.unit->hasBonusOfType(BonusType::UNLIMITED_RETALIATIONS),
								old.unit->ableToRetaliate(), alreadyDisabled, newBlind);
					}
					// Pinned fheroes2 spellEffectValue values Berserker at 85%
					// of enemy strength, excluding the final enemy and reducing
					// value for distant non-archers. The VCMI spell is a location
					// effect, so only score actual newly affected enemy stacks.
					// Native ATTACKS_NEAREST_CREATURE must be newly applied; a
					// resisted spell or an existing disable earns zero.
					if(berserkSpell && !friendly && after && after->alive()
						&& health == old.health && old.position.isValid())
					{
						const auto effect = [](SpellID id)
						{
							return Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(id));
						};
						const bool alreadyDisabled = old.unit->isFrozen()
							|| old.unit->hasBonus(effect(SpellID(SpellID::BLIND)))
							|| old.unit->hasBonus(effect(SpellID(SpellID::PARALYZE)))
							|| old.unit->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE);
						const bool gainedBerserk = after->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE)
							&& after->hasBonus(effect(SpellID(SpellID::BERSERK)))
							&& !old.unit->hasBonus(effect(SpellID(SpellID::BERSERK)));
						const int distance = old.unit->unitSide() == BattleSide::DEFENDER
							? old.position.getX()
							: GameConstants::BFIELD_WIDTH - 1 - old.position.getX();
						value += static_cast<double>(old.unit->estimateCombatValue())
							* Ports::FHeroes2::berserkControlRatio(
								enemyCount == 1, old.unit->isShooter(), std::max(1, distance),
								alreadyDisabled, gainedBerserk);
					}
					// fheroes2 spellEffectValue values Haste/Slow based on the
					// speed actually gained/lost, not an unrelated damage
					// spell's health delta. HypotheticBattle applies native
					// VCMI resistance, stacking, legality and duration.
					if(speedSpell && after && after->alive() && health == old.health)
					{
						const int beforeSpeed = static_cast<int>(old.unit->getMovementRange());
						const int afterSpeed = static_cast<int>(after->getMovementRange());
						const double strength = static_cast<double>(old.unit->estimateCombatValue());
						if(friendly && spell->id == SpellID::HASTE)
						{
							const bool slowed = old.unit->hasBonus(Selector::source(
								BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::SLOW))));
							value += strength * Ports::FHeroes2::hasteSpeedRatio(
								beforeSpeed, afterSpeed, theirs.averageSpeed(), slowed,
								old.unit->isShooter(), false);
						}
						else if(!friendly && spell->id == SpellID::SLOW && old.position.isValid())
						{
							const bool hasted = old.unit->hasBonus(Selector::source(
								BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::HASTE))));
							const bool attackingCastle = real->battleGetDefendedTown() != nullptr
								&& real->battleGetMySide() == BattleSide::ATTACKER;
							const int distance = old.unit->unitSide() == BattleSide::DEFENDER
								? old.position.getX()
								: GameConstants::BFIELD_WIDTH - 1 - old.position.getX();
							value += strength * Ports::FHeroes2::slowSpeedRatio(
								beforeSpeed, afterSpeed, ours.averageSpeed(), old.unit->isShooter(),
								attackingCastle, hasted,
								old.unit->hasBonusOfType(BonusType::FLYING), std::max(1, distance));
						}
					}
				}
				if(friendlyHealth <= 0)
					continue;
				const double scaled = value / std::sqrt(std::max(1, cost) / 3.0);
				if(std::isfinite(scaled) && scaled > bestValue && (resurrection || scaled > threshold))
				{
					bestValue = scaled;
					best = BattleAction::makeHeroSpellcast(real->battleGetMySide(), target, spell->id);
				}
			}
		}
		return best;
	}
};
}
