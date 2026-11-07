/*
 * HoMM3SpellPolicy.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later.
 * Source: sushi-shi/homm3-decomp, e12906accf96c2662b1abadbc49ddc111991bf21,
 * src/ai_tactical.cpp (CC0-1.0). Native spell effects and combat values are
 * projections; this is not the complete original hero policy.
 */
#pragma once

#include "HoMM3SpellValue.h"
#include "../../BattleAI/StackWithBonuses.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/CStack.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../lib/spells/ISpellMechanics.h"

namespace AIIntegration
{
class HoMM3SpellPolicy
{
	struct Attack
	{
		const CStack * target = nullptr;
		BattleHex from;
		BattleHex aim;
		bool shooting = false;
		double damage = 0;
	};

	struct Snapshot
	{
		const CStack * unit;
		int64_t health;
		int count;
		BattleHex position;
		PlayerColor owner;
		double value;
		Attack attack;
	};

	static bool inactive(const battle::Unit & unit)
	{
		return unit.hasBonusOfType(BonusType::NOT_ACTIVE);
	}

	static bool completedTurn(const battle::Unit & unit)
	{
		return unit.moved() || unit.defended();
	}

	static bool hasSpell(const battle::Unit & unit, SpellID spell)
	{
		return unit.hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
	}

	static bool attackBuff(SpellID id)
	{
		return id == SpellID::BLESS || id == SpellID::BLOODLUST || id == SpellID::PRECISION;
	}

	static int remainingDuration(const battle::Unit & unit, SpellID spell)
	{
		int duration = 0;
		for(const auto & bonus : *unit.getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))))
			duration = std::max<int>(duration, bonus->turnsRemain);
		return duration;
	}

	static bool removalSupported(SpellID id)
	{
		return attackBuff(id) || id == SpellID::CURSE || id == SpellID::BLIND;
	}

	static double estimateAttack(const CBattleInfoCallback & battle, const battle::Unit & unit, const Attack & attack)
	{
		if(!attack.target)
			return 0;
		BattleAttackInfo info(&unit, attack.target, BattleHex::getDistance(unit.getPosition(), attack.from), attack.shooting);
		info.attackerPos = attack.from;
		info.defenderPos = attack.aim;
		const auto estimate = battle.battleEstimateDamage(info);
		return (static_cast<double>(estimate.damage.min) + static_cast<double>(estimate.damage.max)) / 2;
	}

	// Native immediate attack projection replaces the unported source AI target census.
	static Attack immediateAttack(const CPlayerBattleCallback & battle, const CStack & unit)
	{
		Attack best;
		if(!unit.alive() || inactive(unit) || unit.isTurret() || !unit.isValidTarget())
			return best;
		const auto available = battle.battleGetAvailableHexes(&unit, false);
		double bestValue = 0;
		for(const auto * target : battle.battleGetAllStacks())
		{
			if(!target->alive() || battle.battleGetOwner(target) == battle.battleGetOwner(&unit))
				continue;
			const bool shooting = battle.battleCanShoot(&unit, target->getPosition());
			if(!shooting && !battle.battleCanAttackUnit(&unit, target))
				continue;
			for(const auto from : available)
			{
				if(shooting && from != unit.getPosition())
					continue;
				for(const auto aim : target->getHexes())
				{
					if(!shooting && !battle.battleCanAttackFromHex(available, &unit, aim, from))
						continue;
					Attack candidate{target, from, aim, shooting, 0};
					candidate.damage = estimateAttack(battle, unit, candidate);
					const double value = Ports::HoMM3::spellDamageValue(
						static_cast<double>(target->estimateCombatValue()), target->getAvailableHealth(),
						static_cast<int64_t>(candidate.damage), false);
					if(value > bestValue)
					{
						bestValue = value;
						best = candidate;
					}
				}
			}
		}
		return best;
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

	static void checkUnchanged(const CPlayerBattleCallback & battle, const CGHeroInstance & hero,
		int64_t mana, const std::vector<Snapshot> & before)
	{
		if(static_cast<int64_t>(hero.mana) != mana || battle.battleGetAllStacks(false).size() != before.size())
			throw std::logic_error("HoMM3 spell projection changed live mana or stack membership");
		for(const auto & old : before)
			if(old.unit->getAvailableHealth() != old.health || old.unit->getCount() != old.count
				|| old.unit->getPosition() != old.position || battle.battleGetOwner(old.unit) != old.owner)
				throw std::logic_error("HoMM3 spell projection changed a live unit");
	}

public:
	static std::optional<BattleAction> choose(const std::shared_ptr<CPlayerBattleCallback> & real,
		const Environment * environment, const CStack & active)
	{
		const auto * hero = real->battleGetMyHero();
		if(!hero || real->battleCanCastSpell(hero, spells::Mode::HERO) != ESpellCastProblem::OK)
			return std::nullopt;
		const auto player = hero->getOwner();
		const int64_t mana = hero->mana;
		std::vector<Snapshot> before;
		double friendlyTotal = 0, enemyTotal = 0, friendlyAwake = 0, enemyAwake = 0;
		bool lastAction = true;
		for(const auto * unit : real->battleGetAllStacks(false))
		{
			const bool friendly = real->battleGetOwner(unit) == player;
			const double value = unit->alive() && !unit->isTurret()
				? static_cast<double>(unit->estimateCombatValue()) : 0;
			(friendly ? friendlyTotal : enemyTotal) += value;
			if(!inactive(*unit))
				(friendly ? friendlyAwake : enemyAwake) += value;
			if(friendly && unit != &active && unit->alive() && !inactive(*unit)
				&& !unit->isTurret() && !unit->isCatapult() && !unit->isBallista()
				&& !unit->isFirstAidTent() && !unit->isAmmoCart() && !completedTurn(*unit))
				lastAction = false;
			before.push_back({unit, unit->getAvailableHealth(), unit->getCount(), unit->getPosition(),
				real->battleGetOwner(unit), value, {}});
		}
		if(friendlyTotal <= 0 || enemyTotal <= 0)
			return std::nullopt;
		const int rounds = Ports::HoMM3::spellPlanningRounds(friendlyAwake, enemyAwake);
		bool attacksReady = false;
		std::optional<BattleAction> best;
		double bestValue = 0;
		bool bestNow = false;
		for(const auto & entry : LIBRARY->spellh->objects)
		{
			const auto * spell = entry.get();
			const bool damage = spell->isDamage();
			const bool resurrection = spell->id == SpellID::RESURRECTION || spell->id == SpellID::ANIMATE_DEAD;
			const bool removal = spell->id == SpellID::CURE || spell->id == SpellID::DISPEL;
			const bool buff = attackBuff(spell->id);
			const bool blind = spell->id == SpellID::BLIND;
			if(!spell->isCombat() || spell->isCreatureAbility()
				|| (!damage && !resurrection && !removal && !buff && !blind)
				|| !spell->canBeCast(real.get(), spells::Mode::HERO, hero))
				continue;
			const int cost = real->battleGetSpellCost(spell, hero);
			if(cost < 0 || cost > mana)
				continue;
			if(!environment)
				throw std::logic_error("HoMM3 spell projection requires a native VCMI environment");
			if((buff || blind || removal) && !attacksReady)
			{
				for(auto & old : before)
					old.attack = immediateAttack(*real, *old.unit);
				attacksReady = true;
			}
			spells::BattleCast legality(real.get(), hero, spells::Mode::HERO, spell);
			const auto mechanics = spell->battleMechanics(&legality);
			const CSpell::TargetInfo targetInfo(spell, mechanics->getEffectLevel(), spells::Mode::HERO);
			for(const auto & target : targets(*mechanics))
			{
				// castEval omits beforeCast, including Magic Mirror. Avoid direct
				// reflection until its friendly outcomes can be valued explicitly.
				const auto * aimed = target.empty() ? nullptr
					: real->battleGetUnitByPos(target.front().hexValue, true);
				if(aimed && Ports::HoMM3::nativeHeroSpellMayReflect(mechanics->isMassive(),
					spell->getLevelInfo(mechanics->getRangeLevel()).range == std::vector<int>{0},
					mechanics->isNegativeSpell(), aimed->valOfBonuses(BonusType::MAGIC_MIRROR)))
					continue;
				auto projected = std::make_shared<HypotheticBattle>(environment, real);
				spells::BattleCast cast(projected.get(), hero, spells::Mode::HERO, spell);
				cast.castEval(projected->getServerCallback(), target);
				checkUnchanged(*real, *hero, mana, before);
				double value = 0, friendlyLoss = 0, enemyLoss = 0;
				int64_t friendlyHealth = 0;
				bool supported = true;
				bool castNow = damage ? targetInfo.massive || spell->getTargetType() == spells::AimType::LOCATION
					|| spell->id == SpellID::CHAIN_LIGHTNING || lastAction
					: targetInfo.massive || lastAction;
				for(const auto & old : before)
				{
					const auto * after = projected->battleGetUnitByID(old.unit->unitId());
					const int64_t health = after && after->alive() ? after->getAvailableHealth() : 0;
					const bool friendly = old.owner == player;
					if(friendly)
						friendlyHealth += health;
					if(damage && health < old.health && !old.unit->isTurret())
						(friendly ? friendlyLoss : enemyLoss) += Ports::HoMM3::spellDamageValue(
							old.value, old.health, old.health - health, inactive(*old.unit));
					if(!after || !after->alive())
						continue;
					const double duration = Ports::HoMM3::spellDurationValue(mechanics->getEffectDuration(), rounds, completedTurn(*old.unit));
					if(resurrection && friendly && !after->isClone() && after->getCount() > old.count)
					{
						// Conservative source over-healing gate; the unported combat forecast
						// cannot authorize its decided-battle exception.
						if(old.unit->getKilled() < old.unit->unitBaseAmount() * 3 / 4)
							continue;
						value += static_cast<double>(after->estimateCombatValue())
							* (after->getCount() - old.count) / after->getCount();
						castNow |= old.unit == &active;
					}
					if(buff && friendly && !hasSpell(*old.unit, spell->id) && hasSpell(*after, spell->id)
						&& old.attack.target && !inactive(*old.unit))
					{
						if((spell->id == SpellID::BLOODLUST && old.attack.shooting)
							|| (spell->id == SpellID::PRECISION && !old.attack.shooting))
							continue;
						value += Ports::HoMM3::attackSpellValue(old.value, old.attack.damage,
							estimateAttack(*projected, *after, old.attack), old.attack.target->getAvailableHealth(), duration);
						castNow |= old.unit == &active;
					}
					if(blind && !friendly && !inactive(*old.unit) && inactive(*after) && old.attack.target)
						value += Ports::HoMM3::blindSpellValue(old.value, enemyAwake,
							spell->getLevelPower(mechanics->getEffectLevel()), duration);
					if(removal)
					{
						if(spell->id == SpellID::CURE && friendly && health > old.health && old.health > 0)
						{
							value += old.value * static_cast<double>(health - old.health) / old.health;
							castNow |= old.unit == &active;
						}
						bool removedAttackEffect = false, removedCurse = false, removedBlind = false;
						double removedDuration = 0;
						int attackEffectTurns = -1;
						for(const auto & effect : LIBRARY->spellh->objects)
						{
							if(!hasSpell(*old.unit, effect->id) || hasSpell(*after, effect->id))
								continue;
							if(!removalSupported(effect->id))
								supported = false;
							removedAttackEffect |= attackBuff(effect->id);
							removedCurse |= effect->id == SpellID::CURSE;
							removedBlind |= effect->id == SpellID::BLIND;
							const int effectTurns = remainingDuration(*old.unit, effect->id);
							if(attackBuff(effect->id) || effect->id == SpellID::CURSE)
							{
								if(attackEffectTurns >= 0 && attackEffectTurns != effectTurns)
									supported = false;
								attackEffectTurns = effectTurns;
							}
							const double effectDuration = Ports::HoMM3::spellDurationValue(effectTurns, rounds, completedTurn(*old.unit));
							removedDuration = std::max(removedDuration, effectDuration);
							if(effect->id == SpellID::BLIND && inactive(*old.unit) && !inactive(*after))
								value += (friendly ? 1 : -1) * old.value * effectDuration;
							if(friendly)
								castNow |= old.unit == &active;
						}
						if(removedBlind && (removedAttackEffect || removedCurse))
							supported = false;
						if(old.attack.target && (removedAttackEffect || removedCurse))
						{
							const double newDamage = estimateAttack(*projected, *after, old.attack);
							value += (friendly ? 1 : -1) * Ports::HoMM3::removedAttackSpellValue(old.value,
								old.attack.damage, newDamage, old.attack.target->getAvailableHealth(), removedDuration);
						}
					}
				}
				if(damage)
					value = Ports::HoMM3::massSpellDamageValue(enemyLoss, friendlyLoss, enemyTotal, friendlyTotal);
				const double weighted = Ports::HoMM3::manaWeightedSpellValue(value, mana, cost);
				if(supported && friendlyHealth > 0 && std::isfinite(weighted) && weighted > bestValue)
				{
					bestValue = weighted;
					bestNow = castNow;
					best = BattleAction::makeHeroSpellcast(real->battleGetMySide(), target, spell->id);
				}
			}
		}
		return bestNow ? best : std::nullopt;
	}
};
}
