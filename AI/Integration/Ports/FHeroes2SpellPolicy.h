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
/// This covers damage, restoration, Haste/Slow, and a projected Disrupting Ray defense reduction; other buffs, dispels and retreat are not yet ported.
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
		std::optional<BattleAction> best;
		double bestValue = 0;

		for(const auto & entry : LIBRARY->spellh->objects)
		{
			const auto * spell = entry.get();
			const bool resurrection = spell->id == SpellID::RESURRECTION || spell->id == SpellID::ANIMATE_DEAD;
			const bool speedSpell = spell->id == SpellID::HASTE || spell->id == SpellID::SLOW;
			const bool defenseSpell = spell->id == SpellID::DISRUPTING_RAY;
			if(!spell->isCombat() || spell->isCreatureAbility()
				|| (!spell->isDamage() && !resurrection && !speedSpell && !defenseSpell && spell->id != SpellID::CURE)
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
