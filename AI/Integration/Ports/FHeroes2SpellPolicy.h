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
#include "FHeroes2EarthquakePolicy.h"
#include "FHeroes2TeleportPolicy.h"
#include "FHeroes2DragonSlayer.h"
#include "FHeroes2DispelValuation.h"
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
/// Covers damage, restoration, native elemental summoning, limited Cure dispelling (Curse/Blind/Slow), enemy Haste/Bless dispelling, Haste/Slow, Bless/Curse, Stone Skin, Bloodlust, ranged Shield, Clone/Mirror Image, Hypnotize, Blind, Berserk and Disrupting Ray using native effects; other buffs, dispels and retreat are not yet ported.
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

	// fheroes2 Unit::GetSpeed(false, true) yields STANDING for Blind/Paralyze.
	// Native VCMI may retain nonzero raw movement while NOT_ACTIVE; that
	// speed cannot confer Haste/Slow value until the control effect ends.
	static int actingSpeed(const battle::Unit & unit)
	{
		return unit.hasBonusOfType(BonusType::NOT_ACTIVE) ? 0
			: static_cast<int>(unit.getMovementRange());
	}

	static Army army(const CPlayerBattleCallback & battle, PlayerColor player, bool friendly)
	{
		Army result;
		for(const auto * unit : battle.battleGetAllStacks())
		{
			if(!unit->alive() || (battle.battleGetOwner(unit) == player) != friendly)
				continue;
			const double strength = static_cast<double>(unit->estimateCombatValue());
			result.strength += strength;
			// Pinned fheroes2 analyzeBattleState weights GetSpeed(false, true),
			// which is STANDING for Blind/Paralyze. VCMI can retain a positive
			// raw movement range under Blind, so use its NOT_ACTIVE status for
			// this current-state projection. This cannot forecast dispels.
			const bool immovable = unit->hasBonusOfType(BonusType::NOT_ACTIVE);
			result.weightedSpeed += strength * actingSpeed(*unit);
			// The source retains blocked/Forgetful archers but excludes
			// immovable ones from strategic ranged strength.
			if(unit->isShooter() && unit->getMovementRange() > 0 && !immovable)
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

	// Lua VCMI teleport.lua adds LOCATION to the CREATURE first target.
	// The generic target enumerator intentionally only supports one aim.
	// Enumerate native-legal two-step aims for the currently active troop,
	// rather than trying an unbounded Cartesian product of all creatures.
	static std::vector<spells::Target> teleportTargets(const spells::Mechanics & mechanics,
		const CStack * active)
	{
		std::vector<spells::Target> result;
		if(!active || !active->alive() || !active->getPosition().isValid()
			|| active->isInvincible())
			return result;
		const auto types = mechanics.getTargetTypes();
		if(types.size() != 2 || types[0] != spells::AimType::CREATURE
			|| types[1] != spells::AimType::LOCATION)
			return result;
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			const BattleHex hex(index);
			if(!hex.isAvailable() || hex == active->getPosition())
				continue;
			spells::Target target{battle::Destination(active), battle::Destination(hex)};
			if(mechanics.canBeCastAt(target))
				result.push_back(std::move(target));
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
		const Environment * environment, const CStack * activeStack = nullptr)
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
		double enemyDragonStrength = 0;
		for(const auto & old : before)
			if(old.owner != player && old.health > 0
				&& old.unit->hasBonusOfType(BonusType::DRAGON_NATURE))
				enemyDragonStrength += static_cast<double>(old.unit->estimateCombatValue());
		std::optional<BattleAction> best;
		double bestValue = 0;

		for(const auto & entry : LIBRARY->spellh->objects)
		{
			const auto * spell = entry.get();
			const bool resurrection = spell->id == SpellID::RESURRECTION || spell->id == SpellID::ANIMATE_DEAD;
			const bool earthquakeSpell = spell->id == SpellID::EARTHQUAKE;
			const bool teleportSpell = spell->id == SpellID::TELEPORT;
			const bool summonSpell = spell->id == SpellID::SUMMON_AIR_ELEMENTAL
				|| spell->id == SpellID::SUMMON_EARTH_ELEMENTAL
				|| spell->id == SpellID::SUMMON_FIRE_ELEMENTAL
				|| spell->id == SpellID::SUMMON_WATER_ELEMENTAL;
			const bool speedSpell = spell->id == SpellID::HASTE || spell->id == SpellID::SLOW;
			const bool dispelSpell = spell->id == SpellID::DISPEL;
			const bool defenseSpell = spell->id == SpellID::DISRUPTING_RAY;
			const bool dragonSlayerSpell = spell->id == SpellID::SLAYER;
			const bool damageRangeSpell = spell->id == SpellID::BLESS || spell->id == SpellID::CURSE;
			const bool defenseBuff = spell->id == SpellID::STONE_SKIN;
			const bool attackBuff = spell->id == SpellID::BLOODLUST;
			const bool rangedShield = spell->id == SpellID::AIR_SHIELD;
			const bool hypnotizeSpell = spell->id == SpellID::HYPNOTIZE;
			const bool disablingSpell = spell->id == SpellID::BLIND;
			const bool berserkSpell = spell->id == SpellID::BERSERK;
			const bool mirrorImageSpell = spell->id == SpellID::CLONE;
			if(!spell->isCombat() || spell->isCreatureAbility()
				|| (!spell->isDamage() && !resurrection && !summonSpell && !teleportSpell && !earthquakeSpell && !speedSpell && !defenseSpell && !dragonSlayerSpell && !damageRangeSpell && !defenseBuff && !attackBuff && !rangedShield && !hypnotizeSpell && !disablingSpell && !berserkSpell && !mirrorImageSpell && !dispelSpell && spell->id != SpellID::CURE)
				|| !spell->canBeCast(real.get(), spells::Mode::HERO, hero))
				continue;
			if(!environment)
				throw std::logic_error("Spell evaluation requires a native VCMI environment");
			const int cost = real->battleGetSpellCost(spell, hero);
			if(cost < 0 || static_cast<int64_t>(cost) > mana)
				continue;
			spells::BattleCast legality(real.get(), hero, spells::Mode::HERO, spell);
			const auto mechanics = spell->battleMechanics(&legality);
			const auto legalTargets = teleportSpell
				? teleportTargets(*mechanics, activeStack) : targets(*mechanics);
			for(const auto & target : legalTargets)
			{
				auto projected = std::make_shared<HypotheticBattle>(environment, real);
				// Earthquake is randomized; rank expected wall damage.
				if(!earthquakeSpell)
				{
					spells::BattleCast cast(projected.get(), hero, spells::Mode::HERO, spell);
					cast.castEval(projected->getServerCallback(), target);
					checkUnchanged(*real, *hero, mana, before);
				}

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
					else if(health > old.health && after && !after->isClone() && !summonSpell)
					{
						const double restored = static_cast<double>(after->estimateCombatValue())
							* static_cast<double>(health - old.health) / static_cast<double>(health);
						value += friendly ? restored : -restored;
					}
					// Pinned spellDragonSlayerValue only values this buff when
					// enemy dragons exist. VCMI's SLAYER is a target-dependent
					// creature bonus, so require a newly applied real native
					// hypothetical effect, not a guessed damage increase.
					if(dragonSlayerSpell && friendly && after && after->alive()
						&& health == old.health)
					{
						const bool nativeEffectGained = after->hasBonusOfType(BonusType::SLAYER)
							&& !old.unit->hasBonusOfType(BonusType::SLAYER);
						value += Ports::FHeroes2::dragonSlayerValue(
							static_cast<double>(old.unit->estimateCombatValue()),
							enemyDragonStrength, theirs.strength, nativeEffectGained);
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
					// The pinned fheroes2 spellDispelValue also ranks DISPEL
					// against beneficial enemy effects. Start with enemy Haste:
					// reverse its getSpellHasteRatio using *actual* VCMI
					// pre/post-cast movement. This is deliberately not a
					// blanket dispel heuristic: other effect combinations
					// are not yet valued, and no-op casts earn zero.
					if(dispelSpell && !friendly && after && after->alive() && health == old.health)
					{
						const auto haste = Selector::source(BonusSource::SPELL_EFFECT,
							BonusSourceID(SpellID(SpellID::HASTE)));
						if(old.unit->hasBonus(haste) && !after->hasBonus(haste))
						{
							const int hasted = actingSpeed(*old.unit);
							const int restored = actingSpeed(*after);
							value += static_cast<double>(old.unit->estimateCombatValue())
								* Ports::FHeroes2::hasteSpeedRatio(restored, hasted,
									ours.averageSpeed(), false, old.unit->isShooter(), false);
						}
						// In the pinned fheroes2 spellDispelValue an enemy's
						// positive Bless contributes the same spellEffectValue
						// used for applying Bless. Require the native VCMI
						// Dispel preview to remove both the spell effect and its
						// maximum-damage roll. Fixed-damage creatures and no-op
						// dispels receive no value.
						const auto bless = Selector::source(BonusSource::SPELL_EFFECT,
							BonusSourceID(SpellID(SpellID::BLESS)));
						if(old.unit->unitType() && old.unit->hasBonus(bless) && !after->hasBonus(bless)
							&& old.unit->hasBonusOfType(BonusType::ALWAYS_MAXIMUM_DAMAGE)
							&& !after->hasBonusOfType(BonusType::ALWAYS_MAXIMUM_DAMAGE))
						{
							const auto * creature = old.unit->unitType();
							value += static_cast<double>(old.unit->estimateCombatValue())
								* Ports::FHeroes2::blessCurseDamageRatio(
									creature->getBaseDamageMin(), creature->getBaseDamageMax(), false, true);
						}
						// Pinned fheroes2 spellDispelValue also evaluates friendly
						// Stone Skin and Bloodlust on enemy units. Reuse the
						// original spellEffectValue ratios in reverse, but only if
						// the native Dispel really removes the source bonus and
						// its corresponding defense / melee-attack increase.
						// A resisted cast or no-op never earns priority.
						const auto stoneSkin = Selector::source(BonusSource::SPELL_EFFECT,
							BonusSourceID(SpellID(SpellID::STONE_SKIN)));
						if(old.unit->hasBonus(stoneSkin) && !after->hasBonus(stoneSkin))
						{
							value += static_cast<double>(old.unit->estimateCombatValue())
								* Ports::FHeroes2::stoneSkinDefenseRatio(
									after->getDefense(false), old.unit->getDefense(false));
						}
						const auto bloodlust = Selector::source(BonusSource::SPELL_EFFECT,
							BonusSourceID(SpellID(SpellID::BLOODLUST)));
						if(old.unit->hasBonus(bloodlust) && !after->hasBonus(bloodlust))
						{
							value += static_cast<double>(old.unit->estimateCombatValue())
								* Ports::FHeroes2::bloodlustAttackRatio(
									after->getAttack(false), old.unit->getAttack(false));
						}
						// Heroes II's positive Shield effect is also counted by
						// spellDispelValue when dispelling a buffed enemy. VCMI's
						// analogous ranged protection is AIR_SHIELD, not SHIELD.
						// Only credit its removal if the native projection actually
						// clears the spell and the ranged mitigation bonus. Use
						// OUR archer strength as the threat to the protected enemy.
						const auto airShield = Selector::source(BonusSource::SPELL_EFFECT,
							BonusSourceID(SpellID(SpellID::AIR_SHIELD)));
						if(old.unit->hasBonus(airShield) && !after->hasBonus(airShield)
							&& old.unit->hasBonusOfType(BonusType::GENERAL_DAMAGE_REDUCTION)
							&& !after->hasBonusOfType(BonusType::GENERAL_DAMAGE_REDUCTION))
						{
							value += static_cast<double>(old.unit->estimateCombatValue())
								* Ports::FHeroes2::shieldRangedRatio(
									ours.shooters, ours.strength, old.unit->isShooter(), true);
						}
					}
					// Pinned fheroes2 spellDispelValue also values CURE as removal
					// of hostile magic, not only recovered hit points. Evaluate only
					// negative effects that the *native* projected Cure really
					// removes. Heroes II ratios are projections onto VCMI statuses.
					if((spell->id == SpellID::CURE || dispelSpell) && friendly && after && after->alive())
					{
						const auto sourceEffect = [](SpellID id)
						{
							return Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(id));
						};
						const auto curse = sourceEffect(SpellID(SpellID::CURSE));
						if(old.unit->unitType() && old.unit->hasBonus(curse) && !after->hasBonus(curse)
							&& !after->hasBonusOfType(BonusType::ALWAYS_MINIMUM_DAMAGE))
						{
							const auto * creature = old.unit->unitType();
							value += static_cast<double>(old.unit->estimateCombatValue())
								* Ports::FHeroes2::blessCurseDamageRatio(
									creature->getBaseDamageMin(), creature->getBaseDamageMax(), false, true);
						}
						// fheroes2 spellDispelValue values removing hostile Slow even
						// from an unwounded ally. Compute the reversed Slow mobility
						// loss using the *observed* restored VCMI movement range.
						// No change from a resisted or already-cleared effect scores zero.
						const auto slow = sourceEffect(SpellID(SpellID::SLOW));
						if(old.unit->hasBonus(slow) && !after->hasBonus(slow) && old.position.isValid())
						{
							const int slowedSpeed = actingSpeed(*old.unit);
							const int restoredSpeed = actingSpeed(*after);
							const int distance = old.unit->unitSide() == BattleSide::DEFENDER
								? old.position.getX()
								: GameConstants::BFIELD_WIDTH - 1 - old.position.getX();
							value += static_cast<double>(old.unit->estimateCombatValue())
								* Ports::FHeroes2::slowSpeedRatio(
									restoredSpeed, slowedSpeed, ours.averageSpeed(),
									old.unit->isShooter(), false, false,
									old.unit->hasBonusOfType(BonusType::FLYING), std::max(1, distance));
						}
						const auto blind = sourceEffect(SpellID(SpellID::BLIND));
						if(old.unit->hasBonus(blind) && !after->hasBonus(blind))
							value += static_cast<double>(old.unit->estimateCombatValue())
								* Ports::FHeroes2::blindControlRatio(false, false, true, false, true);
						// Pinned fheroes2 spellDispelValue also restores allies
						// affected by hostile Berserker. Its spellEffectValue
						// assigns 85% of stack strength to this loss of control.
						// Cure must actually remove both the spell and VCMI's
						// ATTACKS_NEAREST_CREATURE behavior; merely permitting a
						// cast or having an unrelated Berserk-like bonus scores zero.
						// Heroes II Berserker and Heroes III Berserk do not have
						// identical targeting or movement rules.
						const auto berserk = sourceEffect(SpellID(SpellID::BERSERK));
						if(old.unit->hasBonus(berserk) && !after->hasBonus(berserk)
							&& old.unit->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE)
							&& !after->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE))
							value += static_cast<double>(old.unit->estimateCombatValue()) * 0.85;

						// Native Paralyze is a creature-origin disabling spell;
						// source spellEffectValue gives 0.85 for multi-stack
						// Paralyze, conditioned on actual effect removal.
						const auto paralyze = sourceEffect(SpellID(SpellID::PARALYZE));
						if(old.unit->hasBonus(paralyze) && !after->hasBonus(paralyze))
							value += Ports::FHeroes2::dispelledEffectValue(
								static_cast<double>(old.unit->estimateCombatValue()),
								0.85, true, true, true);
					}
					// Pinned spellDispelValue also SUBTRACTS value when a
					// broad native Dispel would remove beneficial magic
					// from our troops, or harmful magic from the enemy.
					// The existing branches above score removal of enemy
					// buffs and allied debuffs, not these harmful cases.
					if(dispelSpell && after && after->alive() && health == old.health)
					{
						const auto effect = [](SpellID id)
						{
							return Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(id));
						};
						const double strength = static_cast<double>(old.unit->estimateCombatValue());
						if(friendly)
						{
							if(old.unit->hasBonus(effect(SpellID(SpellID::HASTE)))
								&& !after->hasBonus(effect(SpellID(SpellID::HASTE))))
							{
								const double haste = Ports::FHeroes2::hasteSpeedRatio(
									static_cast<int>(after->getMovementRange()),
									static_cast<int>(old.unit->getMovementRange()),
									theirs.averageSpeed(), false, old.unit->isShooter(), false);
								value -= strength * haste;
							}
							if(old.unit->unitType() && old.unit->hasBonus(effect(SpellID(SpellID::BLESS)))
								&& !after->hasBonus(effect(SpellID(SpellID::BLESS)))
								&& old.unit->hasBonusOfType(BonusType::ALWAYS_MAXIMUM_DAMAGE)
								&& !after->hasBonusOfType(BonusType::ALWAYS_MAXIMUM_DAMAGE))
							{
								const auto * creature = old.unit->unitType();
								value -= strength * Ports::FHeroes2::blessCurseDamageRatio(
									creature->getBaseDamageMin(), creature->getBaseDamageMax(), false, true);
							}
							if(old.unit->hasBonus(effect(SpellID(SpellID::STONE_SKIN)))
								&& !after->hasBonus(effect(SpellID(SpellID::STONE_SKIN))))
								value -= strength * Ports::FHeroes2::stoneSkinDefenseRatio(
									after->getDefense(false), old.unit->getDefense(false));
							if(old.unit->hasBonus(effect(SpellID(SpellID::BLOODLUST)))
								&& !after->hasBonus(effect(SpellID(SpellID::BLOODLUST))))
								value -= strength * Ports::FHeroes2::bloodlustAttackRatio(
									after->getAttack(false), old.unit->getAttack(false));
							if(old.unit->hasBonus(effect(SpellID(SpellID::AIR_SHIELD)))
								&& !after->hasBonus(effect(SpellID(SpellID::AIR_SHIELD))))
								value -= strength * Ports::FHeroes2::shieldRangedRatio(
									theirs.shooters, theirs.strength, old.unit->isShooter(), true);
						}
						else
						{
							if(old.unit->unitType()
								&& old.unit->hasBonus(effect(SpellID(SpellID::CURSE)))
								&& !after->hasBonus(effect(SpellID(SpellID::CURSE)))
								&& old.unit->hasBonusOfType(BonusType::ALWAYS_MINIMUM_DAMAGE)
								&& !after->hasBonusOfType(BonusType::ALWAYS_MINIMUM_DAMAGE))
							{
								const auto * creature = old.unit->unitType();
								value -= strength * Ports::FHeroes2::blessCurseDamageRatio(
									creature->getBaseDamageMin(), creature->getBaseDamageMax(), false, true);
							}
							if(old.unit->hasBonus(effect(SpellID(SpellID::SLOW)))
								&& !after->hasBonus(effect(SpellID(SpellID::SLOW)))
								&& old.position.isValid())
							{
								const int distance = old.unit->unitSide() == BattleSide::DEFENDER
									? old.position.getX()
									: GameConstants::BFIELD_WIDTH - 1 - old.position.getX();
								value -= strength * Ports::FHeroes2::slowSpeedRatio(
									static_cast<int>(after->getMovementRange()),
									static_cast<int>(old.unit->getMovementRange()),
									ours.averageSpeed(), old.unit->isShooter(), false, false,
									old.unit->hasBonusOfType(BonusType::FLYING), std::max(1, distance));
							}
							if(old.unit->hasBonus(effect(SpellID(SpellID::BLIND)))
								&& !after->hasBonus(effect(SpellID(SpellID::BLIND))))
								value -= strength * Ports::FHeroes2::blindControlRatio(
									false, false, true, false, true);
							if(old.unit->hasBonus(effect(SpellID(SpellID::BERSERK)))
								&& !after->hasBonus(effect(SpellID(SpellID::BERSERK))))
								value -= strength * 0.85;
							if(old.unit->hasBonus(effect(SpellID(SpellID::PARALYZE)))
								&& !after->hasBonus(effect(SpellID(SpellID::PARALYZE))))
								value -= strength * 0.85;
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
						const int beforeSpeed = actingSpeed(*old.unit);
						const int afterSpeed = actingSpeed(*after);
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
				// Pinned fheroes2 spellTeleportValue considers offensive
				// teleport ONLY for a ground melee stack lacking an immediate
				// melee engagement. Native Lua validates wall/moat, two-step
				// targeting and actual landing; no opponent-target fallback.
				if(teleportSpell && activeStack && !activeStack->isShooter()
					&& !activeStack->hasBonusOfType(BonusType::FLYING)
					&& !activeStack->isHypnotized()
					&& real->battleGetOwner(activeStack) == player)
				{
					bool canMeleeNow = false;
					const auto reachable = real->getReachability(activeStack);
					const auto steps = real->battleGetAvailableHexes(reachable, activeStack, false);
					for(const auto & hex : steps)
					{
						if(canMeleeNow || !hex.isAvailable())
							continue;
						for(const auto & old : before)
						{
							if(old.owner == player || old.health <= 0)
								continue;
							if(!real->meleeAttackHexes(activeStack, old.unit, hex).empty())
							{
								canMeleeNow = true;
								break;
							}
						}
					}
					const auto * simulated = projected->battleGetUnitByID(activeStack->unitId());
					if(simulated && simulated->alive())
					{
						const auto distanceToEnemy = [&](const battle::Unit & unit)
						{
							int distance = GameConstants::BFIELD_SIZE;
							for(const auto & myHex : unit.getHexes())
								for(const auto & old : before)
								{
									if(old.owner == player || old.health <= 0)
										continue;
									for(const auto & enemyHex : old.unit->getHexes())
										distance = std::min(distance,
											static_cast<int>(BattleHex::getDistance(myHex, enemyHex)));
								}
							return distance;
						};
						value += Ports::FHeroes2::offensiveTeleportValue(
							static_cast<double>(activeStack->estimateCombatValue()),
							true, canMeleeNow, simulated->getPosition() != activeStack->getPosition(),
							distanceToEnemy(*activeStack), distanceToEnemy(*simulated));
					}
				}
				if(earthquakeSpell)
				{
					const bool attackingCastle = real->battleGetDefendedTown() != nullptr
						&& real->battleGetMySide() == BattleSide::ATTACKER
						&& real->hasFortifications();
					int meleeStacks = 0;
					double meleeStrength = 0;
					for(const auto & old : before)
						if(old.owner == player && old.health > 0
							&& !old.unit->isShooter()
							&& !old.unit->hasBonusOfType(BonusType::FLYING)
							&& !old.unit->isTurret() && !old.unit->isCatapult())
						{
							++meleeStacks;
							meleeStrength += static_cast<double>(old.unit->estimateCombatValue());
						}
					int standing = 0;
					int total = 0;
					if(attackingCastle)
						for(const auto part : {EWallPart::KEEP, EWallPart::BOTTOM_WALL,
							EWallPart::BELOW_GATE, EWallPart::OVER_GATE, EWallPart::UPPER_WALL,
							EWallPart::GATE})
						{
							if(real->battleGetWallState(part) == EWallState::NONE)
								continue;
							++total;
							if(real->battleGetWallState(part) != EWallState::DESTROYED)
								++standing;
						}
					// VCMI catapult.lua rolls normal hit or critical per effect
					// point; a single expected hit is a conservative proxy.
					// No chance is assigned if the siege has no standing walls.
					value += Ports::FHeroes2::earthquakeValue(attackingCastle,
						meleeStacks, meleeStrength, ours.strength,
						standing, total, 1.0, theirs.shooters, theirs.strength);
				}
				// Heroes II MIRRORIMAGE becomes native VCMI CLONE. Apply the
				// pinned 1.0/0.55/0.33 archer/flying/ground source ratio
				// only when the *projected* battle really creates a new
				// friendly clone. Never infer success from cast legality,
				// an existing clone, or a spell effect on the original.
				if(mirrorImageSpell)
				{
					// HypotheticBattle creates StackWithBonuses units, not CStack objects.
					// The legacy CStack-only view is delegated to the live battle and
					// silently omits newly summoned clones. Query simulated Units.
					for(const auto * clone : projected->battleGetUnitsIf([](const battle::Unit * unit) { return !unit->isGhost(); }))
					{
						if(!clone->alive() || !clone->isClone()
							|| projected->battleGetOwner(clone) != player)
							continue;
						const bool newlyCreated = std::none_of(before.begin(), before.end(),
							[&](const UnitSnapshot & old) { return old.unit->unitId() == clone->unitId(); });
						if(!newlyCreated)
							continue;
						value += static_cast<double>(clone->estimateCombatValue())
							* Ports::FHeroes2::mirrorImageCloneRatio(
								clone->isShooter(), clone->hasBonusOfType(BonusType::FLYING),
								static_cast<int>(clone->getMovementRange()), theirs.averageSpeed(), true);
					}
				}
				// Pinned fheroes2 spellSummonValue counts the combat strength of
				// newly summoned troops and halves it when already overwhelming
				// the enemy. Use the *native simulated* new/augmented stacks:
				// a legal spell with no actual spawn must score zero. Heroes III
				// elemental placement, combat strength and reinforcement rules
				// belong to VCMI, not to the Heroes II battle simulator.
				if(summonSpell)
				{
					double summonedValue = 0;
					for(const auto * unit : projected->battleGetUnitsIf([](const battle::Unit * u) { return !u->isGhost(); }))
					{
						if(!unit->alive() || unit->isClone() || projected->battleGetOwner(unit) != player)
							continue;
						const auto prior = std::find_if(before.begin(), before.end(), [&](const UnitSnapshot & old)
						{
							return old.unit->unitId() == unit->unitId();
						});
						if(prior == before.end())
						{
							summonedValue += static_cast<double>(unit->estimateCombatValue());
						}
						else if(prior->unit->unitSlot() == SlotID::SUMMONED_SLOT_PLACEHOLDER
							&& unit->getAvailableHealth() > prior->health)
						{
							// Some VCMI summon configurations reinforce the original
							// elemental stack instead of creating a second unit ID.
							const double newHealth = static_cast<double>(unit->getAvailableHealth());
							summonedValue += static_cast<double>(unit->estimateCombatValue())
								* (newHealth - static_cast<double>(prior->health)) / newHealth;
						}
					}
					value += summonedValue * (ours.strength > theirs.strength * 2 ? 0.5 : 1.0);
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
