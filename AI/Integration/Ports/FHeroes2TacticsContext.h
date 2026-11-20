/*
 * Shared native stance projection for the FHeroes2 tactical port.
 * GPL-2.0-or-later.
 */
#pragma once

#include "SourceTactics.h"
#include "FHeroes2ArcherClassification.h"
#include "FHeroes2NativeStrength.h"
#include "FHeroes2SiegePressure.h"
#include "FHeroes2ObservedMagic.h"
#include "../../../lib/CStack.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"

namespace AIIntegration
{
/// Census-wide source strength or native fallback; enemy magic remains observed-only.
class FHeroes2TacticsContext
{
	static double strength(const CPlayerBattleCallback & battle, const battle::Unit & stack)
	{
		return Ports::FHeroes2::NativeStrengthModel(battle).value(stack);
	}

	static double maximumOwnDamage(const CPlayerBattleCallback & battle)
	{
		const auto * hero = battle.battleGetMyHero();
		if(!hero)
			return 0;
		Ports::FHeroes2::MaximumSpellDamage maximum(hero->mana);
		for(const auto & entry : LIBRARY->spellh->objects)
		{
			const auto * spell = entry.get();
			if(!spell->isCombat() || !spell->isDamage() || spell->isCreatureAbility()
				|| !hero->canCastThisSpell(spell))
				continue;
			maximum.consider(true, true, battle.battleGetSpellCost(spell, hero),
				static_cast<double>(spell->calculateDamage(hero)));
		}
		return maximum.value();
	}

	static std::optional<bool> attackerCoverBypass(const CPlayerBattleCallback & battle)
	{
		if(battle.battleGetMySide() == BattleSide::ATTACKER)
			if(const auto * hero = battle.battleGetMyHero())
				if(hero->getSecSkillLevel(SecondarySkill::ARCHERY) > 0)
					return true;
		std::optional<bool> consensus;
		for(const auto * unit : battle.battleGetAllStacks(false))
		{
			if(!unit->alive() || unit->controllingSide() != BattleSide::ATTACKER
				|| !unit->isShooter() || unit->isCatapult() || !unit->shots.canUse(1)
				|| unit->hasBonusOfType(BonusType::NOT_ACTIVE))
				continue;
			if(unit->controllingSide() != unit->unitSide())
				return std::nullopt;
			bool bypass = false;
			// Inspect evaluated public stack bonuses, not the hostile hero object.
			for(const auto selectedBonuses = unit->getAllBonuses(Selector::source(BonusSource::SECONDARY_SKILL,
				BonusSourceID(SecondarySkill(SecondarySkill::ARCHERY)))); const auto & bonus : *selectedBonuses)
			{
				if(bonus->type != BonusType::PERCENTAGE_DAMAGE_BOOST
					|| bonus->subtype != BonusCustomSubtype::damageTypeRanged
					|| bonus->valType != BonusValueType::BASE_NUMBER
					|| (bonus->val != 10 && bonus->val != 25 && bonus->val != 50)
					|| bonus->effectRange != BonusLimitEffect::NO_LIMIT || bonus->limiter
					|| bonus->updater || bonus->propagator || bonus->propagationUpdater)
					return std::nullopt;
				bypass = true;
			}
			// Golden Bow's SHOOTER_ONLY limiter has already been evaluated on this
			// shooter. Require every ranged stack to agree before treating it globally.
			for(const auto selectedBonuses = unit->getAllBonuses(Selector::type()(BonusType::NO_WALL_PENALTY)
				.And(Selector::sourceTypeSel(BonusSource::ARTIFACT))); const auto & bonus : *selectedBonuses)
			{
				if(bonus->effectRange != BonusLimitEffect::NO_LIMIT || bonus->updater
					|| bonus->propagator || bonus->propagationUpdater)
					return std::nullopt;
				bypass = true;
			}
			if(consensus && *consensus != bypass)
				return std::nullopt;
			consensus = bypass;
		}
		return consensus.value_or(false);
	}

public:
	static Ports::FHeroes2::SiegePressure siegePressure(const CPlayerBattleCallback & battle, BattleSide side)
	{
		Ports::FHeroes2::SiegePressure result;
		result.ownDefender = side == BattleSide::DEFENDER;
		if(!battle.battleGetDefendedTown())
			return result;
		for(const auto * unit : battle.battleGetAllStacks(true))
			if(unit->isTurret() && unit->alive() && unit->unitSide() == BattleSide::DEFENDER)
			{
				result.active = true;
				result.towers += strength(battle, *unit);
			}
		if(result.active)
			result.attackerIgnoresCover = attackerCoverBypass(battle);
		return result;
	}

	static bool isPositionInDefendedArea(CPlayerBattleCallback & battle,
		const CStack & stack, const BattleHex & head)
	{
		// Hypnotize changes the defended side, not the native head/tail facing.
		const auto side = battle.playerToSide(battle.battleGetOwner(&stack));
		const bool defendingCastle = side == BattleSide::DEFENDER && battle.battleGetDefendedTown()
			&& !battle.battleGetStacksIf([](const CStack * unit)
			{ return unit->isTurret() && unit->alive() && !unit->isGhost() && unit->unitSide() == BattleSide::DEFENDER; }).empty();
		return Ports::FHeroes2::isPositionInDefendedArea(stack.getHexes(head),
			[&](const BattleHex & hex)
			{
				if(!hex.isAvailable())
					return false;
				if(defendingCastle)
					return battle.battleIsInsideWalls(hex);
				return side == BattleSide::ATTACKER
					? hex.getX() < GameConstants::BFIELD_WIDTH / 2
					: hex.getX() >= GameConstants::BFIELD_WIDTH / 2;
			});
	}


	static Ports::FHeroes2::Tactics tactics(CPlayerBattleCallback & battle, const CStack & stack,
		const std::vector<const CStack *> & enemies, const FHeroes2ObservedSpells & observed = {},
		bool forceNativeStrength = false)
	{
		Ports::FHeroes2::TacticsInput input;
		const Ports::FHeroes2::NativeStrengthModel strengthModel(battle, forceNativeStrength);
		for(const auto * friendly : battle.battleGetStacks(CBattleInfoEssentials::ONLY_MINE))
		{
			input.armyStrength += strengthModel.value(*friendly);
			// Like the source, this census does not exclude archers because
			// an adjacent enemy blocks a shot at the current moment.
			if(fheroes2ContributesRangedPressure(*friendly))
				input.shootersStrength += strengthModel.value(*friendly);
		}
		for(const auto * enemy : enemies)
		{
			input.enemyArmyStrength += strengthModel.value(*enemy);
			if(fheroes2ContributesRangedPressure(*enemy))
				input.enemyShootersStrength += strengthModel.value(*enemy);
		}
		if(input.armyStrength <= 0 || input.enemyArmyStrength <= 0)
			return {};
		input.flying = stack.hasBonusOfType(BonusType::FLYING);
		const auto side = battle.playerToSide(battle.battleGetOwner(&stack));
		Ports::FHeroes2::applySiegePressure(input, siegePressure(battle, side));
		input.insideDefendedArea = isPositionInDefendedArea(battle, stack, stack.getPosition());
		// Source order: tower/cover pressure, hero pressure, then stance ratios.
		Ports::FHeroes2::addHeroRangedPressure(input.shootersStrength, input.enemyShootersStrength,
			input.shootersStrength > 1 ? maximumOwnDamage(battle) : 0,
			FHeroes2ObservedMagic::maximumObservedDamage(battle, observed));
		return Ports::FHeroes2::selectTactics(input);
	}

};
}
