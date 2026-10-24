/*
 * DetachedDuelRollout.cpp, part of VCMI engine
 * GPL-2.0-or-later; see license.txt.
 */
#include "../../StdInc.h"
#include "DetachedDuelRollout.h"
#include "../../../../lib/battle/BattleInfo.h"
#include "../../../../lib/battle/BattleLayout.h"
#include "../../../../lib/battle/CUnitState.h"
#include "../../../../lib/mapObjects/CGHeroInstance.h"
#include <vcmi/spells/Spell.h>

namespace NK2AI
{
namespace experimental
{
namespace
{
// Limits are computational budgets, not combat-strength multipliers.
constexpr unsigned MAX_ROUNDS = 64;

bool supportedBonuses(const battle::Unit & unit, DuelOutcome & result)
{
	if(unit.doubleWide() || !unit.alive() || !unit.getPosition().isAvailable()
		|| unit.moraleVal() != 0 || unit.luckVal() != 0
		|| unit.getTotalAttacks(false) != 1 || unit.getTotalAttacks(true) != 1)
		return false;
	for(const auto & bonus : *unit.getAllBonuses(Selector::all))
	{
		// Moving conditional bonuses require a re-evaluated bonus graph. This
		// initial subset has none except native-terrain bonuses, whose terrain
		// remains constant throughout this fixed-field duel.
		if((bonus->limiter && bonus->source != BonusSource::TERRAIN_NATIVE)
			|| bonus->updater || bonus->propagationUpdater)
			return false;
		switch(bonus->type)
		{
		case BonusType::PRIMARY_SKILL:
		case BonusType::STACK_HEALTH:
		case BonusType::CREATURE_DAMAGE:
		case BonusType::STACKS_SPEED:
		case BonusType::SHOOTER:
		case BonusType::SHOTS:
		case BonusType::FLYING:
		case BonusType::NO_DISTANCE_PENALTY:
		case BonusType::NO_MELEE_PENALTY:
		case BonusType::MORALE: // checked zero above
		case BonusType::LUCK:
		case BonusType::NO_MORALE:
		case BonusType::SPELL_IMMUNITY:
		case BonusType::LEVEL_SPELL_IMMUNITY:
		case BonusType::MAGIC_RESISTANCE:
		case BonusType::MANA_CHANNELING:
		case BonusType::LIVING:
		case BonusType::UNDEAD:
		case BonusType::NON_LIVING:
		case BonusType::NO_LUCK:
		// Inert in a no-magic battle; retained, never granted or rewritten.
		case BonusType::MOVEMENT:
		case BonusType::SIGHT_RADIUS:
		case BonusType::MANA_REGENERATION:
		case BonusType::MANA_PER_KNOWLEDGE_PERCENTAGE:
		case BonusType::HERO_EXPERIENCE_GAIN_PERCENT:
		case BonusType::MAX_LEARNABLE_SPELL_LEVEL:
		case BonusType::HERO_SPELL_CASTS_PER_COMBAT_TURN:
		case BonusType::SPELL_DAMAGE:
			break;
		default:
			result.unsupportedBonusType = static_cast<int>(bonus->type);
			return false;
		}
	}
	return true;
}

bool hasAffordableMagic(const CGHeroInstance * hero)
{
	if(!hero || !hero->hasSpellbook())
		return false;
	for(const auto spell : hero->getSpellsInSpellbook())
		if(spell.toSpell()->isCombat() && hero->getSpellCost(spell.toSpell()) <= hero->mana)
			return true;
	return false;
}

class DetachedDuel final : public BattleInfo, public battle::IUnitEnvironment
{
public:
	std::array<std::unique_ptr<battle::CUnitStateDetached>, 2> units;
	int roundNumber = 0;

	explicit DetachedDuel(const CBattleInfoCallback & initial, const battle::Units & source)
		: BattleInfo(nullptr, initial.getBattle()->getLayout())
	{
		activeStack = initial.battleActiveUnit() ? initial.battleActiveUnit()->unitId() : -1;
		terrainType = initial.battleTerrainType();
		battlefieldType = initial.battleGetBattlefieldType();
		for(const auto * original : source)
		{
			const size_t index = original->unitSide() == BattleSide::ATTACKER ? 0 : 1;
			units[index] = std::make_unique<battle::CUnitStateDetached>(original, original);
			units[index]->localInit(this);
			units[index]->position = original->getPosition();
		}
		// No sides[].armyObjectID/heroID is set. Consequently the BattleInfo
		// destructor cannot clear a live army's battle pointer. No localInit()
		// on BattleInfo, no bonus graph attachment, and no real state writes.
	}

	battle::Units getUnitsIf(const battle::UnitFilter & predicate) const override
	{
		battle::Units result;
		for(const auto & unit : units)
			if(unit && predicate(unit.get()))
				result.push_back(unit.get());
		return result;
	}
	int32_t getRound() const override { return roundNumber; }
	PlayerColor getSidePlayer(BattleSide side) const override
	{
		return units[side == BattleSide::ATTACKER ? 0 : 1]->unitOwner();
	}
	bool unitHasAmmoCart(const battle::Unit *) const override { return false; }
	PlayerColor unitEffectiveOwner(const battle::Unit * unit) const override { return unit->unitOwner(); }

	void hit(battle::CUnitStateDetached & attacker, battle::CUnitStateDetached & defender,
		bool ranged, bool counter, bool favourable)
	{
		const BattleAttackInfo info(&attacker, &defender, 0, ranged);
		const auto range = calculateDmgRange(info).damage;
		const bool useMaximum = (attacker.unitSide() == BattleSide::ATTACKER) == favourable;
		int64_t damage = useMaximum ? range.max : range.min;
		defender.damage(damage);
		attacker.afterAttack(ranged, counter);
	}

	bool act(battle::CUnitStateDetached & actor, battle::CUnitStateDetached & enemy, bool favourable)
	{
		if(battleCanShoot(&actor, enemy.position))
		{
			hit(actor, enemy, true, false, favourable);
			return true;
		}
		const auto available = battleGetAvailableHexes(&actor, true);
		for(const auto hex : enemy.getAttackableHexes(&actor))
		{
			if(available.contains(hex) && battleCanAttackUnit(&actor, &enemy))
			{
				actor.position = hex;
				hit(actor, enemy, false, false, favourable);
				if(enemy.ableToRetaliate() && actor.alive())
					hit(enemy, actor, false, true, favourable);
				return true;
			}
		}
		// Same deterministic approach policy as the independent server driver.
		// Reachability is engine-owned, including flying and occupied hexes.
		BattleHex best = actor.position;
		for(const auto hex : available)
		{
			const auto distance = BattleHex::getDistance(hex, enemy.position);
			const auto bestDistance = BattleHex::getDistance(best, enemy.position);
			if(distance < bestDistance || (distance == bestDistance && hex.toInt() < best.toInt()))
				best = hex;
		}
		if(best == actor.position)
			return false;
		actor.position = best;
		return true;
	}
};
}

DuelOutcome runDetachedDuel(const CBattleInfoCallback & initial, bool favourableToAttacker)
{
	DuelOutcome result;
	if(!initial.getBattle() || initial.battleGetDefendedTown() || initial.battleTacticDist()
		|| !initial.battleGetAllObstacles().empty())
	{
		result.unsupportedReason = "battlefield, siege or tactics";
		return result;
	}
	if(initial.battleGetFightingHero(BattleSide::DEFENDER)
		|| hasAffordableMagic(initial.battleGetFightingHero(BattleSide::ATTACKER)))
	{
		result.unsupportedReason = "enemy hero or available magic";
		return result;
	}
	const auto source = initial.battleAliveUnits();
	if(source.size() != 2 || source[0]->unitSide() == source[1]->unitSide())
	{
		result.unsupportedReason = "not a one-stack duel";
		return result;
	}
	for(const auto * unit : source)
		if(!supportedBonuses(*unit, result) || unit->getAvailableHealth() != unit->getTotalHealth())
		{
			result.unsupportedReason = "unit ability, morale, luck or initial state";
			return result;
		}

	DetachedDuel duel(initial, source);
	for(unsigned round = 0; round < MAX_ROUNDS; ++round)
	{
		duel.roundNumber = round + 1;
		for(auto & unit : duel.units)
			unit->afterNewRound();
		std::vector<battle::Units> queue;
		duel.battleGetTurnOrder(queue, 2, 1, round == 0 ? 0 : -1);
		if(queue.empty())
			break;
		bool progressed = false;
		for(const auto * next : queue.front())
		{
			auto & actor = *duel.units[next->unitSide() == BattleSide::ATTACKER ? 0 : 1];
			auto & enemy = *duel.units[next->unitSide() == BattleSide::ATTACKER ? 1 : 0];
			if(!actor.alive() || !enemy.alive())
				break;
			progressed |= duel.act(actor, enemy, favourableToAttacker);
			actor.movedThisRound = true;
			duel.activeStack = actor.unitId();
			++result.actions;
		}
		if(!duel.units[0]->alive() || !duel.units[1]->alive())
		{
			result.status = DuelStatus::FINISHED;
			result.winner = duel.units[0]->alive() ? BattleSide::ATTACKER : BattleSide::DEFENDER;
			result.attackerSurvivors = duel.units[0]->getCount();
			result.defenderSurvivors = duel.units[1]->getCount();
			return result;
		}
		if(!progressed)
			break;
	}
	result.status = DuelStatus::BUDGET_EXHAUSTED;
	return result;
}
}
}
