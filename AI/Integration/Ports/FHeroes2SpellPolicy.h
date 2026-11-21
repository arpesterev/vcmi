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

#include "FHeroes2NativeStrength.h"

#include "SourceTactics.h"
#include <map>
#include "FHeroes2ArcherClassification.h"
#include "FHeroes2TacticsContext.h"
#include "FHeroes2EarthquakePolicy.h"
#include "FHeroes2TeleportPolicy.h"
#include "FHeroes2DragonSlayer.h"
#include "FHeroes2DispelValuation.h"
#include "FHeroes2ObservedMagic.h"
#include "FHeroes2MagicValue.h"
#include "../../BattleAI/StackWithBonuses.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/CStack.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../lib/spells/ISpellMechanics.h"

namespace AIIntegration
{
/// Source-derived spell priorities with VCMI legality and spell-effect simulation.
/// Native effects preserve host legality; the remaining source gaps are tracked in the acceptance matrix.
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

	static bool completedTurn(const battle::Unit & unit)
	{
		return unit.moved() || unit.defended();
	}

	static bool canValueNewEffect(const battle::Unit & unit, int duration)
	{
		// Source isSpellcastUselessForUnit and spellDurationMultiplier.
		// Native DEFEND spends the turn without setting moved(); WAIT does not.
		return !unit.hasBonusOfType(BonusType::NOT_ACTIVE)
			&& (duration >= 2 || !completedTurn(unit));
	}

	static int actingSpeed(const battle::Unit & unit)
	{
		return unit.hasBonusOfType(BonusType::NOT_ACTIVE) ? 0
			: static_cast<int>(unit.getMovementRange());
	}

	static double removedHasteRatio(const battle::Unit & before, const battle::Unit & after,
		double enemyAverageSpeed, bool defensiveTactics)
	{
		const auto effect = [](SpellID id)
		{
			return Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(id));
		};
		if(before.hasBonusOfType(BonusType::NOT_ACTIVE)
			&& (before.hasBonus(effect(SpellID(SpellID::BLIND)))
				|| before.hasBonus(effect(SpellID(SpellID::PARALYZE)))))
		{
			// Pinned GetSpeed(false,true) is STANDING; Haste ExtraValue is still 2.
			constexpr int sourceHasteBonus = 2;
			return Ports::FHeroes2::hasteSpeedRatio(0, sourceHasteBonus,
				enemyAverageSpeed, false, fheroes2IsArcher(before), defensiveTactics);
		}
		if(before.hasBonusOfType(BonusType::NOT_ACTIVE) || after.hasBonusOfType(BonusType::NOT_ACTIVE))
			return 0;
		return Ports::FHeroes2::hasteSpeedRatio(actingSpeed(after), actingSpeed(before),
			enemyAverageSpeed, false, fheroes2IsArcher(before), defensiveTactics);
	}

	static double removedSlowRatio(const battle::Unit & before, const battle::Unit & after,
		double friendlyAverageSpeed, bool attackingCastle, int distance)
	{
		const auto effect = [](SpellID id)
		{
			return Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(id));
		};
		if(before.hasBonusOfType(BonusType::NOT_ACTIVE)
			&& (before.hasBonus(effect(SpellID(SpellID::BLIND)))
				|| before.hasBonus(effect(SpellID(SpellID::PARALYZE)))))
		{
			// Source Slow checks archers/castle before STANDING speed.
			return fheroes2IsArcher(before) || attackingCastle ? 0.01 : 0;
		}
		if(before.hasBonusOfType(BonusType::NOT_ACTIVE) || after.hasBonusOfType(BonusType::NOT_ACTIVE))
			return 0;
		return Ports::FHeroes2::slowSpeedRatio(actingSpeed(after), actingSpeed(before),
			friendlyAverageSpeed, fheroes2IsArcher(before), attackingCastle, false,
			before.hasBonusOfType(BonusType::FLYING), distance);
	}

	static bool effectiveRangedShield(const CPlayerBattleCallback & real,
		const HypotheticBattle & projected, const CStack & protectedUnit, const battle::Unit & after)
	{
		// Isolate Shield from other effects removed by Dispel. Native factors,
		// integer rounding and the one-damage floor decide whether it helps.
		StackWithBonuses shielded(&projected, static_cast<const battle::Unit *>(&protectedUnit));
		StackWithBonuses unshielded(&projected, static_cast<const battle::Unit *>(&protectedUnit));
		const auto effect = Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(SpellID(SpellID::AIR_SHIELD)));
		unshielded.removeUnitBonus(effect);
		if(!protectedUnit.hasBonus(effect))
		{
			std::vector<Bonus> added;
			const auto effects = after.getAllBonuses(effect);
			for(const auto & bonus : *effects)
				added.push_back(*bonus);
			shielded.addUnitBonus(added);
		}
		for(const auto * unit : real.battleGetAllStacks())
		{
			// Preserve the source's future-volley classification, not current shot legality.
			if(!fheroes2ContributesRangedPressure(*unit)
				|| real.battleGetOwner(unit) == real.battleGetOwner(&protectedUnit))
				continue;
			StackWithBonuses shooter(&projected, static_cast<const battle::Unit *>(unit));
			const auto withShield = real.calculateDmgRange(BattleAttackInfo(&shooter, &shielded, 0, true)).damage;
			const auto withoutShield = real.calculateDmgRange(BattleAttackInfo(&shooter, &unshielded, 0, true)).damage;
			if(withShield.min < withoutShield.min || withShield.max < withoutShield.max)
				return true;
		}
		return false;
	}

	static Army army(const CPlayerBattleCallback & battle, PlayerColor player, bool friendly, bool forceNativeStrength = false)
	{
		Army result;
		const Ports::FHeroes2::NativeStrengthModel strengthModel(battle, forceNativeStrength);
		for(const auto * unit : battle.battleGetAllStacks())
		{
			if(!unit->alive() || (battle.battleGetOwner(unit) == player) != friendly)
				continue;
			const double strength = strengthModel.value(*unit);
			result.strength += strength;
			// Source ai_battle.cpp::analyzeBattleState uses GetSpeed(false, true)
			// for projected army speed and isImmovable() for archer strength.
			// Blind/Paralyze stop both; Bind stops movement but not shooting.
			// Forgetfulness and contact only suppress the current shot.
			const int nextTurnSpeed = unit->hasBonusOfType(BonusType::NOT_ACTIVE)
				? 0 : static_cast<int>(unit->getMovementRange());
			result.weightedSpeed += strength * nextTurnSpeed;
			if(fheroes2ContributesRangedPressure(*unit))
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

	static double damageValue(const Ports::FHeroes2::NativeStrengthModel & strengthModel, const CStack & unit, int64_t damage, const Army & side)
	{
		const double health = static_cast<double>(unit.getAvailableHealth());
		if(damage <= 0 || health <= 0)
			return 0;
		const double strength = strengthModel.value(unit);
		if(static_cast<double>(damage) >= health)
		{
			// Pinned ai_battle_spell.cpp::spellDamageValue uses Unit::GetSpeed(),
			// which is STANDING for Blind/Paralyze, even when the creature's
			// ordinary base speed is still nonzero. Native NOT_ACTIVE is the
			// corresponding combat-disable projection, not VCMI isFrozen().
			// Native moved/defended project the source TR_MOVED check; WAIT does not.
			const int effectiveSpeed = unit.hasBonusOfType(BonusType::NOT_ACTIVE) || completedTurn(unit)
				? 0 : static_cast<int>(unit.getMovementRange());
			return strength + side.strength * (effectiveSpeed > side.averageSpeed() ? 0.07 : 0.035);
		}
		double fraction = static_cast<double>(damage) / health;
		// Pinned ai_battle_spell.cpp::spellDamageValue applies the wake-up
		// penalty to isImmovable() (Blind/Paralyze), not just petrification.
		// VCMI's isFrozen() means STONE_GAZE only; native NOT_ACTIVE is
		// the broader actual cannot-act status used by Blind and Paralyze.
		if(unit.hasBonusOfType(BonusType::NOT_ACTIVE))
			fraction += fraction - 1;
		return strength * fraction;
	}

	// Pinned spellDamageValue folds a Mirror owner's linked copy strength into
	// the owner's lethal score. It does NOT grant another stack-kill bonus for
	// that automatic disappearance. Native state snapshots enumerate both.
	static std::optional<uint32_t> directCascadeClone(const CPlayerBattleCallback & real,
		const HypotheticBattle & projected, const CSpell & spell, const spells::Mechanics & mechanics,
		const battle::Unit * aimed, PlayerColor player, const std::vector<UnitSnapshot> & before)
	{
		const auto * owner = dynamic_cast<const CStack *>(aimed);
		if(!owner || !owner->alive() || owner->isClone() || owner->cloneID < 0
			|| real.battleGetOwner(owner) == player || owner->magicResistance() != 0)
			return std::nullopt;
		const auto & level = spell.getLevelInfo(mechanics.getEffectLevel());
		const auto & effects = level.battleEffects;
		if(!spell.isDamage() || !mechanics.isNegativeSpell() || !mechanics.isMagicalEffect()
			|| mechanics.isMassive() || level.range != std::vector<int>{0}
			|| spell.getTargetType() != spells::AimType::CREATURE
			|| !level.smartTarget || level.clearTarget || level.clearAffected
			|| !(level.effects.isNull() || (level.effects.isStruct() && level.effects.Struct().empty()))
			|| !(level.cumulativeEffects.isNull() || (level.cumulativeEffects.isStruct() && level.cumulativeEffects.Struct().empty()))
			|| !effects.isStruct() || effects.Struct().size() != 1
			|| !effects["directDamage"].isStruct() || effects["directDamage"].Struct().size() != 1
			|| effects["directDamage"]["type"].String() != "damage")
			return std::nullopt;
		// Arbitrary reactions can create a different cause of disappearance.
		for(const auto * unit : real.battleGetAllStacks(true))
			if(unit->hasBonusOfType(BonusType::ON_COMBAT_EVENT)
				|| unit->hasBonusOfType(BonusType::COMBAT_EVENT_TRIGGER))
				return std::nullopt;
		const auto clone = std::find_if(before.begin(), before.end(), [&](const UnitSnapshot & old)
		{
			return old.unit->unitId() == static_cast<uint32_t>(owner->cloneID);
		});
		if(clone == before.end() || clone->health <= 0 || !clone->unit->isClone()
			|| clone->owner != real.battleGetOwner(owner) || clone->unit->unitSide() != owner->unitSide())
			return std::nullopt;
		int owners = 0;
		for(const auto & old : before)
		{
			if(old.unit->cloneID == owner->cloneID) ++owners;
			const auto * after = projected.battleGetUnitByID(old.unit->unitId());
			const auto health = after && after->alive() ? after->getAvailableHealth() : 0;
			if(old.unit->unitId() == owner->unitId() || old.unit->unitId() == clone->unit->unitId())
			{
				if(health != 0) return std::nullopt;
			}
			else if(health != old.health)
				return std::nullopt;
		}
		return owners == 1 ? std::optional<uint32_t>(clone->unit->unitId()) : std::nullopt;
	}

	// Pinned area damage applies the resistance discount before nonlinear
	// kill/wake valuation. This is source policy, not native expected utility:
	// native resistance still rolls all-or-none independently per target.
	// Explicit host shape mapping. Unknown spells keep their existing path;
	// recognized spells with altered shapes cannot use this certificate.
	static std::vector<int> ordinaryAreaRange(SpellID id)
	{
		switch(id.num)
		{
		case SpellID::FIREBALL:
		case SpellID::METEOR_SHOWER: return {0, 1};
		case SpellID::FROST_RING: return {1};
		case SpellID::INFERNO: return {0, 1, 2};
		default: return {};
		}
	}

	struct OrdinaryAreaResistanceProjection
	{
		std::map<uint32_t, int64_t> raw;
		std::map<uint32_t, uint32_t> discounted;
		std::vector<UnitSnapshot> roster;
	};

	static std::optional<OrdinaryAreaResistanceProjection> ordinaryAreaResistanceProjection(
		const CPlayerBattleCallback & real, const CSpell & spell, const spells::Mechanics & mechanics,
		const std::vector<const CStack *> & affected)
	{
		const auto & level = spell.getLevelInfo(mechanics.getEffectLevel());
		const auto & effects = level.battleEffects;
		const auto expectedRange = ordinaryAreaRange(spell.id);
		if(expectedRange.empty() || !spell.isDamage() || !mechanics.isNegativeSpell()
			|| !mechanics.isMagicalEffect() || mechanics.isMassive()
			|| !spell.counteredSpells.empty()
			|| spell.getTargetType() != spells::AimType::LOCATION
			|| level.range != expectedRange || level.smartTarget || mechanics.isSmart()
			|| spell.getLevelInfo(mechanics.getRangeLevel()).range != expectedRange
			|| level.clearTarget || level.clearAffected
			|| !(level.effects.isNull() || (level.effects.isStruct() && level.effects.Struct().empty()))
			|| !(level.cumulativeEffects.isNull() || (level.cumulativeEffects.isStruct() && level.cumulativeEffects.Struct().empty()))
			|| !effects.isStruct() || effects.Struct().size() != 1
			|| !effects["directDamage"].isStruct() || effects["directDamage"].Struct().size() != 1
			|| effects["directDamage"]["type"].String() != "damage")
			return std::nullopt;
		OrdinaryAreaResistanceProjection result;
		// Include corpses/turrets: death/action callbacks can live outside the area.
		for(const auto * unit : real.battleGetAllStacks(true))
		{
			if(unit->hasBonusOfType(BonusType::ON_COMBAT_EVENT)
				|| unit->hasBonusOfType(BonusType::COMBAT_EVENT_TRIGGER))
				return std::nullopt;
			result.roster.push_back({unit, unit->getAvailableHealth(), unit->getCount(),
				unit->getPosition(), real.battleGetOwner(unit)});
		}
		for(const auto * unit : affected)
		{
			if(!unit || !unit->alive() || unit->isClone() || unit->cloneID >= 0 || unit->isTurret()
				|| unit->hasBonusOfType(BonusType::DISINTEGRATE))
				return std::nullopt;
			const int resistance = unit->magicResistance();
			const int64_t raw = mechanics.adjustEffectValue(unit);
			const auto adjusted = Ports::FHeroes2::resistedDamage(raw, resistance);
			if(!adjusted || !result.raw.emplace(unit->unitId(), raw).second)
				return std::nullopt;
			if(resistance > 0 && resistance < 100)
				result.discounted.emplace(unit->unitId(), *adjusted);
		}
		return result;
	}

	static bool ordinaryAreaAftermath(const HypotheticBattle & projected,
		const OrdinaryAreaResistanceProjection & projection)
	{
		const auto afterRoster = projected.battleGetUnitsIf([](const battle::Unit * unit) { return !unit->isGhost(); });
		if(afterRoster.size() != projection.roster.size())
			return false;
		for(const auto & old : projection.roster)
		{
			const auto * after = projected.battleGetUnitByID(old.unit->unitId());
			if(!after || after->getPosition() != old.position || projected.battleGetOwner(after) != old.owner)
				return false;
			const auto raw = projection.raw.find(old.unit->unitId());
			const int64_t expected = raw == projection.raw.end() ? old.health : std::max<int64_t>(0, old.health - raw->second);
			if(after->getAvailableHealth() != expected)
				return false;
		}
		return true;
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

	/// Source nonlethal spell-damage valuation of a live native stack.
	/// Used by native regressions to distinguish Blind's wake-up penalty
	/// from the unrelated VCMI STONE_GAZE-only isFrozen() predicate.
	static double sourceDamageValue(const CPlayerBattleCallback & battle,
		const CStack & target, int64_t damage)
	{
		return damageValue(Ports::FHeroes2::NativeStrengthModel(battle), target, damage, army(battle, battle.battleGetOwner(&target), true));
	}

	static std::optional<BattleAction> choose(const std::shared_ptr<CPlayerBattleCallback> & real,
		const Environment * environment, const CStack * activeStack = nullptr,
		bool farewellDamageOnly = false, const FHeroes2ObservedSpells & observedEnemySpells = {},
		bool forceNativeStrength = false)
	{
		const auto * hero = real->battleGetMyHero();
		if(!hero || real->battleCanCastSpell(hero, spells::Mode::HERO) != ESpellCastProblem::OK)
			return std::nullopt;

		const Ports::FHeroes2::NativeStrengthModel strengthModel(*real, forceNativeStrength);
		const auto player = hero->getOwner();
		const auto ours = army(*real, player, true, forceNativeStrength);
		const auto theirs = army(*real, player, false, forceNativeStrength);
		const FHeroes2ObservedMagic observedMagic(*real, observedEnemySpells, ours.strength);
		if(ours.strength <= 0 || theirs.strength <= 0)
			return std::nullopt;

		// Source uses the current planner stance, including when reversing enemy Haste.
		auto stanceEnemies = real->battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY);
		vstd::erase_if(stanceEnemies, [](const CStack * unit)
		{
			return !unit->alive() || unit->isInvincible();
		});
		std::sort(stanceEnemies.begin(), stanceEnemies.end(),
			[](const CStack * left, const CStack * right) { return left->unitId() < right->unitId(); });
		const bool defensiveTactics = activeStack
			&& FHeroes2TacticsContext::tactics(*real, *activeStack, stanceEnemies, observedEnemySpells, forceNativeStrength).defensive;

		const bool attackingCastle = real->battleGetDefendedTown() != nullptr
			&& real->battleGetMySide() == BattleSide::ATTACKER;

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
				enemyDragonStrength += strengthModel.value(*old.unit);
		std::shared_ptr<HypotheticBattle> previewBaseline;
		std::optional<BattleAction> best;
		double bestValue = 0;

		for(const auto & entry : LIBRARY->spellh->objects)
		{
			const auto * spell = entry.get();
			// Pinned planUnitTurn::farewellSpellcast asks for the most
			// valuable kills, not mere fractional HP damage. Only damaging
			// spells are eligible; VCMI validates their actual legality.
			if(farewellDamageOnly && !spell->isDamage())
				continue;
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
			const bool antiMagicSpell = spell->id == SpellID::ANTI_MAGIC;
			const bool unitEffectSpell = speedSpell || defenseSpell || dragonSlayerSpell
				|| damageRangeSpell || defenseBuff || attackBuff || rangedShield
				|| hypnotizeSpell || disablingSpell || berserkSpell;
			if(!spell->isCombat() || spell->isCreatureAbility()
				|| (!spell->isDamage() && !resurrection && !summonSpell && !teleportSpell && !earthquakeSpell && !speedSpell && !defenseSpell && !dragonSlayerSpell && !damageRangeSpell && !defenseBuff && !attackBuff && !rangedShield && !hypnotizeSpell && !disablingSpell && !berserkSpell && !mirrorImageSpell && !antiMagicSpell && !dispelSpell && spell->id != SpellID::CURE)
				|| !spell->canBeCast(real.get(), spells::Mode::HERO, hero))
				continue;
			if(!environment)
				throw std::logic_error("Spell evaluation requires a native VCMI environment");
			const int cost = real->battleGetSpellCost(spell, hero);
			if(cost < 0 || static_cast<int64_t>(cost) > mana)
				continue;
			spells::BattleCast legality(real.get(), hero, spells::Mode::HERO, spell);
			const auto mechanics = spell->battleMechanics(&legality);
			const int duration = mechanics->getEffectDuration();
			if(rangedShield && duration == 1
				&& std::none_of(before.begin(), before.end(), [&](const UnitSnapshot & old)
				{
					return old.owner != player && old.health > 0 && fheroes2IsArcher(*old.unit)
						&& !old.unit->hasBonusOfType(BonusType::NOT_ACTIVE)
						&& !completedTurn(*old.unit);
				}))
				continue;
			bool areaPartialPossible = false;
			if(!ordinaryAreaRange(spell->id).empty())
				for(const auto * unit : real->battleGetAllStacks(true))
					if(unit->alive() && unit->magicResistance() > 0 && unit->magicResistance() < 100)
						{ areaPartialPossible = true; break; }
			const auto legalTargets = teleportSpell
				? teleportTargets(*mechanics, activeStack) : targets(*mechanics);
			for(const auto & target : legalTargets)
			{
				// Native castEval does not execute beforeCast/reflect. Do not score
				// apparent enemy effects when a hero's direct negative spell may
				// actually reflect onto our army. This is a conservative native
				// availability boundary, not an evaluated reflection distribution.
				const auto * aimed = target.empty() ? nullptr
					: real->battleGetUnitByPos(target.front().hexValue, true);
				if(aimed && mechanics->isNegativeSpell() && !mechanics->isMassive()
					&& spell->getLevelInfo(mechanics->getRangeLevel()).range == std::vector<int>{0}
					&& !aimed->getAllBonuses(Selector::type()(BonusType::MAGIC_MIRROR))->empty()
					&& aimed->valOfBonuses(BonusType::MAGIC_MIRROR) > 0)
					continue;
                // Bounded source-order damage projection. Chain, area and control
                // continue through their existing policies, not this approximation.
                const bool ordinaryDirect = spell->id == SpellID::MAGIC_ARROW
                    || spell->id == SpellID::ICE_BOLT || spell->id == SpellID::LIGHTNING_BOLT
                    || spell->id == SpellID::IMPLOSION;
                if(ordinaryDirect && aimed && aimed->magicResistance() > 0 && aimed->magicResistance() < 100)
                {
                    const auto * aimedStack = dynamic_cast<const CStack *>(aimed);
                    if(!aimedStack)
                        continue;
                    // Only partial resistance enters this new projection. Zero
                    // resistance retains the pre-existing native preview, including
                    // clone/linked and modded-effect behavior.
                    // Combat scripts may resurrect or damage other stacks on death;
                    // their arbitrary aftermath is outside this scalar projection.
                    // Keep corpses: UNIT_DEATH and ACTION_FINISHED can dispatch
                    // to dead units. This native roster excludes removed ghosts.
                    const auto triggerRoster = real->battleGetAllStacks(true);
                    if(std::any_of(triggerRoster.begin(), triggerRoster.end(), [](const CStack * unit)
                        { return unit->hasBonusOfType(BonusType::COMBAT_EVENT_TRIGGER)
                            || unit->hasBonusOfType(BonusType::ON_COMBAT_EVENT); }))
                        continue;
                    const auto & level = spell->getLevelInfo(mechanics->getEffectLevel());
                    const auto & effects = level.battleEffects;
                    // IDs can be modded: admit precisely the standard damage effect,
                    // with no chain/count/percentage/additional scripted semantics.
                    if(!aimed || aimed->isClone() || aimedStack->cloneID >= 0
                        || real->battleGetOwner(aimed) == player || !aimed->alive()
                        || !mechanics->isNegativeSpell() || !mechanics->isMagicalEffect()
                        || mechanics->isMassive() || level.range != std::vector<int>{0}
                        || spell->getTargetType() != spells::AimType::CREATURE
                        || !level.smartTarget || level.clearTarget || level.clearAffected
                        || !(level.effects.isNull() || (level.effects.isStruct() && level.effects.Struct().empty()))
                        || !(level.cumulativeEffects.isNull() || (level.cumulativeEffects.isStruct() && level.cumulativeEffects.Struct().empty()))
                        || !effects.isStruct() || effects.Struct().size() != 1
                        || !effects["directDamage"].isStruct()
                        || effects["directDamage"].Struct().size() != 1
                        || effects["directDamage"]["type"].String() != "damage")
                        continue;
                    const auto damage = Ports::FHeroes2::resistedDamage(
                        mechanics->adjustEffectValue(aimed), aimed->magicResistance());
                    if(!damage || *damage == 0)
                        continue;
                    double value = 0;
                    if(farewellDamageOnly)
                    {
                        const auto remaining = std::max<int64_t>(0, aimed->getAvailableHealth() - *damage);
                        const auto hp = aimed->getMaxHealth();
                        if(hp <= 0 || aimed->getCount() <= 0)
                            continue;
                        const auto survivors = remaining / hp + (remaining % hp != 0);
                        const auto killed = std::clamp<int64_t>(aimed->getCount() - survivors, 0, aimed->getCount());
                        value = strengthModel.value(*aimed) / aimed->getCount() * killed;
                    }
                    else
                        value = damageValue(strengthModel, *aimedStack, *damage, theirs)
                            / std::sqrt(std::max(1, cost) / 3.0);
                    if(std::isfinite(value) && value > bestValue && (farewellDamageOnly || value > threshold))
                    {
                        bestValue = value;
                        best = BattleAction::makeHeroSpellcast(real->battleGetMySide(), target, spell->id);
                    }
                    continue;
                }
				std::optional<OrdinaryAreaResistanceProjection> areaProjection;
				if(areaPartialPossible)
				{
					const auto affected = mechanics->getAffectedStacks(target);
					const bool partial = std::any_of(affected.begin(), affected.end(), [](const CStack * unit)
						{ return unit && unit->magicResistance() > 0 && unit->magicResistance() < 100; });
					if(partial)
					{
						areaProjection = ordinaryAreaResistanceProjection(*real, *spell, *mechanics, affected);
						if(!areaProjection)
							continue;
					}
				}
				if(mirrorImageSpell)
				{
					const auto * source = target.empty() ? nullptr
						: real->battleGetUnitByPos(target.front().hexValue, true);
					if(!source || !canValueNewEffect(*source, duration))
						continue;
				}
				if(!previewBaseline)
					previewBaseline = std::make_shared<HypotheticBattle>(environment, real);
				auto projected = std::make_shared<HypotheticBattle>(environment, previewBaseline);
				// Earthquake is randomized; rank expected wall damage.
				if(!earthquakeSpell)
				{
					spells::BattleCast cast(projected.get(), hero, spells::Mode::HERO, spell);
					cast.castEval(projected->getServerCallback(), target);
					checkUnchanged(*real, *hero, mana, before);
				}
				if(strengthModel.usesSourceScale())
				{
					const auto units = projected->battleGetUnitsIf([](const battle::Unit * unit) { return !unit->isGhost(); });
					if(std::any_of(units.begin(), units.end(), [&](const battle::Unit * unit) { return !strengthModel.supports(*unit); }))
						return choose(real, environment, activeStack, farewellDamageOnly, observedEnemySpells, true);
				}
				if(areaProjection && !ordinaryAreaAftermath(*projected, *areaProjection))
					continue;
				// Heroes II Berserker targets enemies only. Native advanced/expert
				// Berserk is indiscriminate area magic; keep that projection from
				// newly compelling allies. Immune/already-compelled allies are safe.
				if(berserkSpell && std::any_of(before.begin(), before.end(), [&](const UnitSnapshot & old)
				{
					if(old.owner != player || old.health <= 0
						|| old.unit->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE))
						return false;
					const auto * after = projected->battleGetUnitByID(old.unit->unitId());
					return after && after->alive()
						&& after->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE);
				}))
					continue;

				if(farewellDamageOnly)
				{
					// Farewell values kills after the source raw-damage resistance
					// discount, without the ordinary stack-kill or wake-up bonuses.
					// Keep the native full-hit preview for active-stack survival.
					double killedValue = 0;
					bool killsActingStack = false;
					for(const auto & old : before)
					{
						if(old.count <= 0)
							continue;
						const auto * after = projected->battleGetUnitByID(old.unit->unitId());
						const int survivors = after && after->alive() ? after->getCount() : 0;
						int killed = std::clamp(old.count - survivors, 0, old.count);
						if(activeStack && old.unit->unitId() == activeStack->unitId() && killed == old.count)
						{
							killsActingStack = true;
							break;
						}
						if(old.unit->isClone())
							continue;
						if(areaProjection && areaProjection->discounted.contains(old.unit->unitId()))
						{
							const auto damage = areaProjection->discounted.at(old.unit->unitId());
							const auto remaining = std::max<int64_t>(0, old.health - damage);
							const auto hp = old.unit->getMaxHealth();
							if(hp <= 0)
								throw std::logic_error("Farewell spell target has invalid maximum health");
							const auto remainingCount = remaining / hp + (remaining % hp != 0);
							killed = static_cast<int>(std::clamp<int64_t>(old.count - remainingCount, 0, old.count));
						}
						const double perCreatureStrength
							= strengthModel.value(*old.unit) / old.count;
						killedValue += (old.owner == player ? -1.0 : 1.0)
								* perCreatureStrength * killed;
					}
					if(!killsActingStack && std::isfinite(killedValue) && killedValue > bestValue)
					{
						bestValue = killedValue;
						best = BattleAction::makeHeroSpellcast(real->battleGetMySide(), target, spell->id);
					}
					continue;
				}

				const auto cascadeClone = ordinaryDirect
					? directCascadeClone(*real, *projected, *spell, *mechanics, aimed, player, before)
					: std::nullopt;
				double value = 0;
				int64_t friendlyHealth = 0;
				for(const auto & old : before)
				{
					const auto * after = projected->battleGetUnitByID(old.unit->unitId());
					const int64_t health = after && after->alive() ? after->getAvailableHealth() : 0;
					const bool friendly = old.owner == player;
					const bool friendlyAfter = after && after->alive()
						&& projected->battleGetOwner(after) == player;
					if(friendlyAfter)
						friendlyHealth += health;
					if(health < old.health)
					{
						// Keep the actual full-hit preview for friendlyHealth/safety below.
						// Only numerical source damage value receives the discount.
						if(areaProjection && areaProjection->discounted.contains(old.unit->unitId()))
							value += (friendly ? -1 : 1) * damageValue(strengthModel, *old.unit,
								areaProjection->discounted.at(old.unit->unitId()), friendly ? ours : theirs);
						else if(cascadeClone && old.unit->unitId() == *cascadeClone)
							value += strengthModel.value(*old.unit);
						else
							value += (friendly ? -1 : 1) * damageValue(strengthModel, *old.unit, old.health - health, friendly ? ours : theirs);
					}
					else if(health > old.health && after && !after->isClone() && !summonSpell)
					{
						double restored = strengthModel.value(*after)
							* static_cast<double>(health - old.health) / static_cast<double>(health);
						if(resurrection && friendlyAfter)
						{
							// CHealth::heal adds newly raised creatures to getResurrected()
							// only for ONE_BATTLE healing. Do not infer permanence from mastery.
							const bool permanent = after->getCount() > old.count
								&& after->getResurrected() == old.unit->getResurrected();
							restored = Ports::FHeroes2::resurrectionValue(
								restored, ours.strength, theirs.strength, permanent);
						}
						value += friendlyAfter ? restored : -restored;
					}
					int valuationDuration = duration;
					if(unitEffectSpell && after)
					{
						const auto effects = after->getAllBonuses(Selector::source(
							BonusSource::SPELL_EFFECT, BonusSourceID(spell->id)));
						// Native permanent effects, notably Disrupting Ray, cannot
						// expire at the round boundary even at spell power one.
						if(std::any_of(effects->begin(), effects->end(), [](const auto & bonus)
						{
							return Bonus::Permanent(bonus.get()) && !Bonus::NTurns(bonus.get());
						}))
							valuationDuration = std::max(valuationDuration, 2);
					}
					// Source positive effects use trueFriendly, excluding changed sides.
					if(antiMagicSpell && friendly && friendlyAfter && after && health == old.health)
					{
						const double blocked = observedMagic.newlyBlockedStrength(*real, *projected, *old.unit, *after);
						value += strengthModel.value(*old.unit)
							* Ports::FHeroes2::antiMagicRatio(blocked, theirs.strength,
								FHeroes2ObservedMagic::hasPositiveMagic(*old.unit),
								FHeroes2ObservedMagic::removedNegativeMagic(*old.unit, *after),
								FHeroes2ObservedMagic::affordableResurrection(*real, *hero, *old.unit),
								blocked > 0, duration >= 2 || !completedTurn(*old.unit));
					}
					// Dispel/Cure remove existing effects even from disabled units.
					if(unitEffectSpell && !canValueNewEffect(*old.unit, valuationDuration))
						continue;
					// spellDispelValue retains spellDurationMultiplier, but HP healing is immediate.
					if((dispelSpell || spell->id == SpellID::CURE)
						&& duration < 2 && completedTurn(*old.unit))
						continue;
					if((dispelSpell || spell->id == SpellID::CURE) && after && after->alive())
					{
						const auto hypnotize = Selector::source(BonusSource::SPELL_EFFECT,
							BonusSourceID(SpellID(SpellID::HYPNOTIZE)));
						if(old.unit->hasBonus(hypnotize) && !after->hasBonus(hypnotize)
							&& old.unit->isHypnotized() && !after->isHypnotized())
							value += Ports::FHeroes2::dispelledHypnotizeValue(
								strengthModel.value(*old.unit), friendly, friendlyAfter);
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
							strengthModel.value(*old.unit),
							enemyDragonStrength, theirs.strength, nativeEffectGained);
					}
					// Pinned fheroes2 values Disrupting Ray against enemy defense.
					// Use VCMI's post-cast defense to exclude immunity, minimum-
					// defense and no-op effects. No source-equivalence claim is made.
					if(defenseSpell && !friendly && after && after->alive() && health == old.health)
					{
						const double enemyStrength = strengthModel.value(*old.unit);
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
							value += strengthModel.value(*old.unit)
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
					if(dispelSpell && !friendlyAfter && after && after->alive() && health == old.health)
					{
						const auto haste = Selector::source(BonusSource::SPELL_EFFECT,
							BonusSourceID(SpellID(SpellID::HASTE)));
						if(old.unit->hasBonus(haste) && !after->hasBonus(haste))
						{
							value += strengthModel.value(*old.unit)
								* removedHasteRatio(*old.unit, *after, theirs.averageSpeed(), defensiveTactics);
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
							value += strengthModel.value(*old.unit)
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
							value += strengthModel.value(*old.unit)
								* Ports::FHeroes2::stoneSkinDefenseRatio(
									after->getDefense(false), old.unit->getDefense(false));
						}
						const auto bloodlust = Selector::source(BonusSource::SPELL_EFFECT,
							BonusSourceID(SpellID(SpellID::BLOODLUST)));
						if(old.unit->hasBonus(bloodlust) && !after->hasBonus(bloodlust))
						{
							value += strengthModel.value(*old.unit)
								* Ports::FHeroes2::bloodlustAttackRatio(
									after->getAttack(false), old.unit->getAttack(false));
						}
						// Heroes II's positive Shield effect is also counted by
						// spellDispelValue when dispelling a buffed enemy. VCMI's
						// analogous ranged protection is AIR_SHIELD, not SHIELD.
						// Only credit its removal if the native projection actually
						// clears the spell and reduces ranged mitigation. Use
						// OUR archer strength as the threat to the protected enemy.
						const auto airShield = Selector::source(BonusSource::SPELL_EFFECT,
							BonusSourceID(SpellID(SpellID::AIR_SHIELD)));
						if(old.unit->hasBonus(airShield) && !after->hasBonus(airShield)
							&& effectiveRangedShield(*real, *projected, *old.unit, *after))
						{
							value += strengthModel.value(*old.unit)
								* Ports::FHeroes2::shieldRangedRatio(
									ours.shooters, ours.strength, fheroes2IsArcher(*old.unit), true);
						}
					}
					// Pinned fheroes2 spellDispelValue also values CURE as removal
					// of hostile magic, not only recovered hit points. Evaluate only
					// negative effects that the *native* projected Cure really
					// removes. Heroes II ratios are projections onto VCMI statuses.
					if((spell->id == SpellID::CURE || dispelSpell) && friendlyAfter && after && after->alive())
					{
						const auto sourceEffect = [](SpellID id)
						{
							return Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(id));
						};
						const auto curse = sourceEffect(SpellID(SpellID::CURSE));
						if(old.unit->unitType() && old.unit->hasBonus(curse) && !after->hasBonus(curse)
							&& old.unit->hasBonusOfType(BonusType::ALWAYS_MINIMUM_DAMAGE)
							&& !after->hasBonusOfType(BonusType::ALWAYS_MINIMUM_DAMAGE))
						{
							const auto * creature = old.unit->unitType();
							value += strengthModel.value(*old.unit)
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
							const int distance = old.unit->unitSide() == BattleSide::DEFENDER
								? old.position.getX()
								: GameConstants::BFIELD_WIDTH - 1 - old.position.getX();
							value += strengthModel.value(*old.unit)
								* removedSlowRatio(*old.unit, *after, ours.averageSpeed(),
									attackingCastle, std::max(1, distance));
						}
						const auto blind = sourceEffect(SpellID(SpellID::BLIND));
						if(old.unit->hasBonus(blind) && !after->hasBonus(blind)
							&& old.unit->hasBonusOfType(BonusType::NOT_ACTIVE)
							&& !after->hasBonusOfType(BonusType::NOT_ACTIVE))
							value += strengthModel.value(*old.unit)
								* Ports::FHeroes2::blindControlRatio(false, false, true, false, true);
						// spellDispelValue reuses Berserker's distance discount.
						const auto berserk = sourceEffect(SpellID(SpellID::BERSERK));
						if(old.unit->hasBonus(berserk) && !after->hasBonus(berserk)
							&& old.unit->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE)
							&& !after->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE)
							&& old.position.isValid())
						{
							const int distance = old.unit->unitSide() == BattleSide::DEFENDER
								? old.position.getX()
								: GameConstants::BFIELD_WIDTH - 1 - old.position.getX();
							value += strengthModel.value(*old.unit)
								* Ports::FHeroes2::berserkControlRatio(
									false, fheroes2IsArcher(*old.unit), std::max(1, distance), false, true);
						}

						// Native Paralyze is a creature-origin disabling spell;
						// source spellEffectValue gives 0.85 for multi-stack
						// Paralyze, conditioned on actual effect removal.
						const auto paralyze = sourceEffect(SpellID(SpellID::PARALYZE));
						if(old.unit->hasBonus(paralyze) && !after->hasBonus(paralyze)
							&& old.unit->hasBonusOfType(BonusType::NOT_ACTIVE)
							&& !after->hasBonusOfType(BonusType::NOT_ACTIVE))
							value += Ports::FHeroes2::dispelledEffectValue(
								strengthModel.value(*old.unit),
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
						const double strength = strengthModel.value(*old.unit);
						if(friendlyAfter)
						{
							if(old.unit->hasBonus(effect(SpellID(SpellID::HASTE)))
								&& !after->hasBonus(effect(SpellID(SpellID::HASTE))))
								value -= strength * removedHasteRatio(
									*old.unit, *after, theirs.averageSpeed(), defensiveTactics);
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
								&& !after->hasBonus(effect(SpellID(SpellID::AIR_SHIELD)))
								&& effectiveRangedShield(*real, *projected, *old.unit, *after))
								value -= strength * Ports::FHeroes2::shieldRangedRatio(
									theirs.shooters, theirs.strength, fheroes2IsArcher(*old.unit), true);
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
								value -= strength * removedSlowRatio(*old.unit, *after, ours.averageSpeed(),
									attackingCastle, std::max(1, distance));
							}
							// Source retaliation is unavailable while these disables remain active.
							// Blind retaliation is only enabled inside an attack sequence.
							if(enemyCount != 1
								&& old.unit->hasBonus(effect(SpellID(SpellID::BLIND)))
								&& !after->hasBonus(effect(SpellID(SpellID::BLIND)))
								&& old.unit->hasBonusOfType(BonusType::NOT_ACTIVE)
								&& !after->hasBonusOfType(BonusType::NOT_ACTIVE))
								value -= strength * Ports::FHeroes2::blindControlRatio(
									false, false, true, false, true);
							// Removing Berserker from the final enemy has no source penalty.
							if(old.unit->hasBonus(effect(SpellID(SpellID::BERSERK)))
								&& !after->hasBonus(effect(SpellID(SpellID::BERSERK)))
								&& old.unit->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE)
								&& !after->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE)
								&& old.position.isValid())
							{
								const int distance = old.unit->unitSide() == BattleSide::DEFENDER
									? old.position.getX()
									: GameConstants::BFIELD_WIDTH - 1 - old.position.getX();
								value -= strength * Ports::FHeroes2::berserkControlRatio(
									enemyCount == 1, fheroes2IsArcher(*old.unit), std::max(1, distance), false, true);
							}
							if(enemyCount != 1
								&& old.unit->hasBonus(effect(SpellID(SpellID::PARALYZE)))
								&& !after->hasBonus(effect(SpellID(SpellID::PARALYZE)))
								&& old.unit->hasBonusOfType(BonusType::NOT_ACTIVE)
								&& !after->hasBonusOfType(BonusType::NOT_ACTIVE))
								value -= strength * 0.85;
						}
					}
					// Pinned fheroes2 spellEffectValue assigns Stone Skin 10% of
					// allied stack strength. Only accept VCMI's observed defense
					// gain; immunities and already-active buffs must score zero.
					if(defenseBuff && friendly && after && after->alive() && health == old.health)
					{
						value += strengthModel.value(*old.unit)
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
							&& !old.unit->hasBonus(shieldEffect)
							&& effectiveRangedShield(*real, *projected, *old.unit, *after);
						value += strengthModel.value(*old.unit)
							* Ports::FHeroes2::shieldRangedRatio(theirs.shooters,
								theirs.strength, fheroes2IsArcher(*old.unit), newlyProtected);
					}
					// Pinned fheroes2 spellEffectValue gives Bloodlust 10% of
					// friendly stack strength. VCMI applies the attack bonus only
					// to melee: score the actual post-cast melee attack increase.
					// Resisted and already-active effects must not use mana.
					if(attackBuff && friendly && after && after->alive() && health == old.health)
					{
						value += strengthModel.value(*old.unit)
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
						value += strengthModel.value(*old.unit)
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
						value += strengthModel.value(*old.unit)
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
						value += strengthModel.value(*old.unit)
							* Ports::FHeroes2::berserkControlRatio(
								enemyCount == 1, fheroes2IsArcher(*old.unit), std::max(1, distance),
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
						const double strength = strengthModel.value(*old.unit);
						if(friendly && spell->id == SpellID::HASTE)
						{
							const bool slowed = old.unit->hasBonus(Selector::source(
								BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::SLOW))));
							value += strength * Ports::FHeroes2::hasteSpeedRatio(
								beforeSpeed, afterSpeed, theirs.averageSpeed(), slowed,
								fheroes2IsArcher(*old.unit), defensiveTactics);
						}
						else if(!friendly && spell->id == SpellID::SLOW && old.position.isValid())
						{
							const bool hasted = old.unit->hasBonus(Selector::source(
								BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::HASTE))));
							const int distance = old.unit->unitSide() == BattleSide::DEFENDER
								? old.position.getX()
								: GameConstants::BFIELD_WIDTH - 1 - old.position.getX();
							value += strength * Ports::FHeroes2::slowSpeedRatio(
								beforeSpeed, afterSpeed, ours.averageSpeed(), fheroes2IsArcher(*old.unit),
								attackingCastle, hasted,
								old.unit->hasBonusOfType(BonusType::FLYING), std::max(1, distance));
						}
					}
				}
				// Pinned fheroes2 spellTeleportValue considers offensive
				// teleport ONLY for a ground melee stack lacking an immediate
				// melee engagement. Native Lua validates wall/moat, two-step
				// targeting and actual landing; no opponent-target fallback.
				if(teleportSpell && !defensiveTactics && activeStack && !fheroes2IsArcher(*activeStack)
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
							strengthModel.value(*activeStack),
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
							&& !fheroes2IsArcher(*old.unit)
							&& !old.unit->hasBonusOfType(BonusType::FLYING)
							&& !old.unit->isTurret() && !old.unit->isCatapult())
						{
							++meleeStacks;
							meleeStrength += strengthModel.value(*old.unit);
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
				if(mirrorImageSpell && aimed)
				{
					const auto * projectedOriginal = dynamic_cast<const battle::CUnitState *>(projected->battleGetUnitByID(aimed->unitId()));
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
						if(!newlyCreated || !projectedOriginal
							|| projectedOriginal->cloneID != static_cast<int32_t>(clone->unitId()))
							continue;
						// Source Mirror Image values the pre-cast target, including remaining shots.
						// Native Clone still receives the engine-defined ammunition stock.
						value += strengthModel.value(*aimed)
							* Ports::FHeroes2::mirrorImageCloneRatio(
								fheroes2IsArcher(*aimed), aimed->hasBonusOfType(BonusType::FLYING),
								static_cast<int>(aimed->getMovementRange()), theirs.averageSpeed(), true);
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
							summonedValue += strengthModel.value(*unit);
						}
						else if(prior->unit->unitSlot() == SlotID::SUMMONED_SLOT_PLACEHOLDER
							&& unit->getAvailableHealth() > prior->health)
						{
							// Some VCMI summon configurations reinforce the original
							// elemental stack instead of creating a second unit ID.
							const double newHealth = static_cast<double>(unit->getAvailableHealth());
							summonedValue += strengthModel.value(*unit)
								* (newHealth - static_cast<double>(prior->health)) / newHealth;
						}
					}
					value += summonedValue * (ours.strength > theirs.strength * 2 ? 0.5 : 1.0);
				}
				if(friendlyHealth <= 0)
					continue;
				// Only ordinary turns reach this branch; farewell ranks
				// whole kills above, without future mana conservation.
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
