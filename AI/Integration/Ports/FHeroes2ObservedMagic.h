/*
 * Observed hostile magic for the FHeroes2 policy.
 * GPL-2.0-or-later.
 */
#pragma once

#include "FHeroes2MagicValue.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/gameState/InfoAboutArmy.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/ProxyCaster.h"

#include <map>

namespace AIIntegration
{
/// Only public hero-cast events populate this set; it is not an enemy spellbook.
using FHeroes2ObservedSpells = std::map<SpellID, bool>;

class FHeroes2ObservedMagic
{
	class PublicCaster final : public spells::ProxyCaster
	{
		PlayerColor owner;
		int power;

	public:
		explicit PublicCaster(const InfoAboutHero & hero)
			: spells::ProxyCaster(nullptr), owner(hero.owner),
			  power(hero.details->primskills.at(static_cast<size_t>(PrimarySkill::SPELL_POWER))) {}

		PlayerColor getCasterOwner() const override { return owner; }
		int32_t getEffectPower(const spells::Spell *) const override { return power; }
		int32_t getEnchantPower(const spells::Spell *) const override { return power; }
	};

	struct Threat
	{
		const CSpell * spell;
		double strength;
	};

	InfoAboutHero enemy;
	std::vector<Threat> threats;

public:
	FHeroes2ObservedMagic(const CPlayerBattleCallback & battle,
		const FHeroes2ObservedSpells & observed, double ourArmyStrength)
		: enemy(battle.battleGetEnemyHero())
	{
		if(!enemy.details || enemy.details->mana <= 0)
			return;
		const PublicCaster caster(enemy);
		for(const auto & [id, observedMultipleTargets] : observed)
		{
			const auto * spell = id.toSpell();
			if(!spell->isCombat() || spell->isCreatureAbility()
				|| (!spell->isDamage() && !spell->isNegative()))
				continue;

			// Mastery and artifacts are not public. Use native untrained
			// cost/damage, not hidden hero fields or a fabricated full book.
			const int cost = spell->getCost(MasteryLevel::NONE);
			using Family = Ports::FHeroes2::StrategicSpellFamily;
			const auto family = spell->isDamage() ? Family::DAMAGE
				: observedMultipleTargets || id == SpellID::BLIND || id == SpellID::PARALYZE
				? Family::HIGH_IMPACT : Family::GENERIC;
			const double magnitude = spell->isDamage()
				? static_cast<double>(spell->calculateDamage(&caster)) : 0;
			const double strength = Ports::FHeroes2::strategicSpellValue(
				family, ourArmyStrength, enemy.details->mana, cost, magnitude);
			if(strength > 0)
				threats.push_back({spell, strength});
		}
	}

	double newlyBlockedStrength(const CBattleInfoCallback & beforeBattle,
		const CBattleInfoCallback & afterBattle, const battle::Unit & before,
		const battle::Unit & after) const
	{
		if(threats.empty() || after.valOfBonuses(BonusType::LEVEL_SPELL_IMMUNITY)
			<= before.valOfBonuses(BonusType::LEVEL_SPELL_IMMUNITY))
			return 0;
		const PublicCaster caster(enemy);
		double result = 0;
		for(const auto & threat : threats)
		{
			spells::BattleCast oldCast(&beforeBattle, &caster, spells::Mode::HERO, threat.spell);
			spells::BattleCast newCast(&afterBattle, &caster, spells::Mode::HERO, threat.spell);
			if(threat.spell->battleMechanics(&oldCast)->isReceptive(&before)
				&& !threat.spell->battleMechanics(&newCast)->isReceptive(&after))
				result = std::max(result, threat.strength);
		}
		return result;
	}

	static bool affordableResurrection(const CPlayerBattleCallback & battle,
		const CGHeroInstance & hero, const battle::Unit & target)
	{
		for(const auto id : {SpellID::RESURRECTION, SpellID::ANIMATE_DEAD})
		{
			const auto * spell = SpellID(id).toSpell();
			if(!hero.canCastThisSpell(spell))
				continue;
			const int cost = battle.battleGetSpellCost(spell, &hero);
			if(cost < 0 || cost > hero.mana)
				continue;
			spells::BattleCast cast(&battle, &hero, spells::Mode::HERO, spell);
			// Future casualties need not exist yet; retain native nature/immunity.
			if(spell->battleMechanics(&cast)->isReceptive(&target))
				return true;
		}
		return false;
	}

	static bool hasPositiveMagic(const battle::Unit & unit)
	{
		for(const auto & entry : LIBRARY->spellh->objects)
			if(entry->isCombat() && entry->isPositive()
				&& unit.hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(entry->id))))
				return true;
		return false;
	}

	static bool removedNegativeMagic(const battle::Unit & before, const battle::Unit & after)
	{
		for(const auto id : {SpellID::CURSE, SpellID::SLOW, SpellID::BLIND,
			SpellID::PARALYZE, SpellID::STONE_GAZE, SpellID::BERSERK, SpellID::HYPNOTIZE})
		{
			const auto effect = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(id)));
			if(!before.hasBonus(effect) || after.hasBonus(effect))
				continue;
			// A removed source tag does not prove that another native disable
			// or stat modifier stopped affecting the unit.
			switch(id)
			{
				case SpellID::CURSE:
					if(before.hasBonusOfType(BonusType::ALWAYS_MINIMUM_DAMAGE)
						&& !after.hasBonusOfType(BonusType::ALWAYS_MINIMUM_DAMAGE)) return true;
					break;
				case SpellID::SLOW:
					if(after.getMovementRange() > before.getMovementRange()) return true;
					break;
				case SpellID::BERSERK:
					if(before.hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE)
						&& !after.hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE)) return true;
					break;
				case SpellID::HYPNOTIZE:
					if(before.isHypnotized() && !after.isHypnotized()) return true;
					break;
				default:
					if(before.hasBonusOfType(BonusType::NOT_ACTIVE)
						&& !after.hasBonusOfType(BonusType::NOT_ACTIVE)) return true;
					break;
			}
		}
		return false;
	}
};
}
