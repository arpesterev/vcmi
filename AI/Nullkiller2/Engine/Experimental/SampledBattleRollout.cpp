/* GPL-2.0-or-later; see license.txt. Experimental, not production-wired. */
#include "../../StdInc.h"
#include "SampledBattleRollout.h"
#include "DetachedDefendBonuses.h"
#include "../../../../lib/battle/BattleInfo.h"
#include "../../../../lib/battle/BattleLayout.h"
#include "../../../../lib/battle/CUnitState.h"
#include "../../../../lib/callback/GameRandomizer.h"
#include "../../../../lib/battle/CObstacleInstance.h"
#include "../../../../lib/CCreatureHandler.h"
namespace NK2AI::experimental
{
namespace
{
constexpr unsigned MAX_ACTIONS = 256;
constexpr unsigned MAX_ROUNDS = 64;
bool supportedBonuses(const battle::Unit & unit, SampledBattleOutcome & result)
{
	if(!unit.alive() || !unit.getPosition().isAvailable()
		|| unit.getTotalAttacks(false) < 1 || unit.getTotalAttacks(false) > 2
		|| unit.getTotalAttacks(true) < 1 || unit.getTotalAttacks(true) > 2)
		return false;
	const auto effectiveBonuses = unit.getAllBonuses(Selector::all);
	for(const auto & bonus : *effectiveBonuses)
	{
		constexpr auto fixedDuringBattle = BonusDuration::PERMANENT | BonusDuration::ONE_BATTLE
			| BonusDuration::ONE_DAY | BonusDuration::ONE_WEEK | BonusDuration::N_DAYS;
		if(!bonus->duration || (bonus->duration & ~fixedDuringBattle))
			return false; // No combat-turn/attack/commander-trigger expiration model.
		// Moving conditional bonuses require a re-evaluated bonus graph. This
		// initial subset has none except native-terrain bonuses, whose terrain
		// remains constant throughout this fixed-field scenario.
		if((bonus->limiter && bonus->source != BonusSource::TERRAIN_NATIVE)
			|| bonus->updater || bonus->propagationUpdater)
			return false;
		switch(bonus->type)
		{
		// Jousting itself is unsupported; its defender-only immunity is inert.
		case BonusType::CHARGE_IMMUNITY:
		case BonusType::PRIMARY_SKILL:
		case BonusType::STACK_HEALTH:
		case BonusType::CREATURE_DAMAGE:
		case BonusType::STACKS_SPEED:
		case BonusType::PERCENTAGE_DAMAGE_BOOST:
		case BonusType::GENERAL_DAMAGE_REDUCTION:
		case BonusType::GENERAL_DAMAGE_PREMY:
		case BonusType::SHOOTER:
		case BonusType::SHOTS:
		case BonusType::FLYING:
		case BonusType::ADDITIONAL_ATTACK:
		case BonusType::ADDITIONAL_RETALIATION:
		case BonusType::UNLIMITED_RETALIATIONS:
		case BonusType::NO_RETALIATION:
		case BonusType::BLOCKS_RETALIATION:
		case BonusType::NO_DISTANCE_PENALTY:
		case BonusType::NO_MELEE_PENALTY:
		case BonusType::MORALE:
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
		case BonusType::ROUGH_TERRAIN_DISCOUNT:
		case BonusType::GENERATE_RESOURCE:
		case BonusType::MAGIC_SCHOOL_SKILL:
		case BonusType::SIGHT_RADIUS:
		case BonusType::MANA_REGENERATION:
		case BonusType::MANA_PER_KNOWLEDGE_PERCENTAGE:
		case BonusType::HERO_EXPERIENCE_GAIN_PERCENT:
		case BonusType::MAX_LEARNABLE_SPELL_LEVEL:
		case BonusType::HERO_SPELL_CASTS_PER_COMBAT_TURN:
		case BonusType::SPELL_DAMAGE:
		// Used only by CGHeroInstance::getEffectDuration when casting. No
		// spells or spell-trigger abilities are admitted by this rollout.
		case BonusType::SPELL_DURATION:
			break;
		default:
			result.unsupportedBonusType = static_cast<int>(bonus->type);
			return false;
		}
	}
	return true;
}


size_t sideIndex(BattleSide side) { return side == BattleSide::ATTACKER ? 0 : 1; }

class SampledBattle final : public BattleInfo, public battle::IUnitEnvironment
{
public:
	std::map<uint32_t, std::unique_ptr<DetachedDefendBonuses>> ownedBonuses;
	std::vector<std::unique_ptr<battle::CUnitStateDetached>> units;
	GameRandomizer randomizer;
	int roundNumber = 0;
	std::vector<std::string> * trace;
	void record(const std::string & message) { if(trace) trace->push_back(message); }
	SampledBattle(const CBattleInfoCallback & initial, const IGameInfoCallback & settings,
		const battle::Units & source, int seed, std::vector<std::string> * trace)
		: BattleInfo(nullptr, initial.getBattle()->getLayout()), randomizer(settings), trace(trace)
	{
		randomizer.setSeed(seed);
		activeStack = initial.battleActiveUnit() ? initial.battleActiveUnit()->unitId() : -1;
		terrainType = initial.battleTerrainType();
		battlefieldType = initial.battleGetBattlefieldType();
		for(const auto * original : source)
		{
			auto bonus = std::make_unique<DetachedDefendBonuses>(*original);
			auto unit = std::make_unique<battle::CUnitStateDetached>(original, bonus.get());
			ownedBonuses.emplace(original->unitId(), std::move(bonus));
			unit->localInit(this);
			unit->position = original->getPosition();
			units.push_back(std::move(unit));
		}
		std::sort(units.begin(), units.end(), [](const auto & a, const auto & b) { return a->unitId() < b->unitId(); });
		// Only ordinary immutable obstacles pass eligibility. Copy their values;
		// never use the live BattleInfo side/army links or initialize its graph.
		for(const auto & obstacle : initial.battleGetAllObstacles())
			obstacles.push_back(std::make_shared<CObstacleInstance>(*obstacle));
	}
	battle::Units getUnitsIf(const battle::UnitFilter & predicate) const override
	{
		battle::Units result;
		for(const auto & unit : units)
			if(predicate(unit.get())) result.push_back(unit.get());
		return result;
	}
	int32_t getRound() const override { return roundNumber; }
	PlayerColor getSidePlayer(BattleSide side) const override
	{
		for(const auto & unit : units)
			if(unit->unitSide() == side) return unit->unitOwner();
		return PlayerColor::NEUTRAL;
	}
	bool unitHasAmmoCart(const battle::Unit *) const override { return false; }
	PlayerColor unitEffectiveOwner(const battle::Unit * unit) const override { return unit->unitOwner(); }
	battle::CUnitStateDetached * find(uint32_t id)
	{
		for(auto & unit : units) if(unit->unitId() == id) return unit.get();
		return nullptr;
	}
	ObjectInstanceID armyID(const battle::Unit & unit) const { return ObjectInstanceID(sideIndex(unit.unitSide())); }
	bool finished() const
	{
		std::array<bool, 2> alive{};
		for(const auto & unit : units) alive[sideIndex(unit->unitSide())] |= unit->alive();
		return !alive[0] || !alive[1];
	}
	void hit(battle::CUnitStateDetached & attacker, battle::CUnitStateDetached & defender, bool ranged, bool counter)
	{
		BattleAttackInfo info(&attacker, &defender, 0, ranged);
		const int luck = attacker.luckVal();
		info.luckyStrike = luck > 0 && randomizer.rollGoodLuck(armyID(attacker), luck);
		info.unluckyStrike = luck < 0 && randomizer.rollBadLuck(armyID(attacker), -luck);
		// The server initializes this army's ability RNG even at zero chance.
		// Mirror that draw on our independent stream for fixed-seed references;
		// actual nonzero death-blow abilities remain outside the whitelist.
		(void)randomizer.rollCombatAbility(armyID(attacker), 0);
		const auto range = calculateDmgRange(info).damage;
		int64_t damage = getActualDamage(range, attacker.getCount(), randomizer.getDefault());
		if(trace) record("hit actor=" + std::to_string(attacker.unitId()) + " target=" + std::to_string(defender.unitId())
			+ " ranged=" + std::to_string(ranged) + " counter=" + std::to_string(counter)
			+ " damage=" + std::to_string(damage) + " before_health=" + std::to_string(defender.getAvailableHealth()));
		defender.damage(damage);
		attacker.afterAttack(ranged, counter);
	}
	void act(battle::CUnitStateDetached & actor)
	{
		// Fixed transparent policy shared with the reference harness, not a
		// claim that this imitates optimal or full tactical-AI play.
		for(auto & enemy : units)
			if(enemy->alive() && enemy->unitSide() != actor.unitSide() && battleCanShoot(&actor, enemy->position))
			{
				if(trace) record("shot actor=" + std::to_string(actor.unitId()) + " target=" + std::to_string(enemy->unitId()));
				const int attacks = actor.getTotalAttacks(true);
				for(int strike = 0; strike < attacks && actor.alive() && enemy->alive() && actor.shots.canUse(); ++strike)
					hit(actor, *enemy, true, false);
				return;
			}
		const auto available = battleGetAvailableHexes(&actor, true);
		for(auto & enemy : units)
			if(enemy->alive() && enemy->unitSide() != actor.unitSide())
				for(const auto hex : enemy->getAttackableHexes(&actor))
					if(available.contains(hex) && battleCanAttackUnit(&actor, enemy.get()))
					{
						if(trace) record("melee actor=" + std::to_string(actor.unitId()) + " target=" + std::to_string(enemy->unitId()) + " hex=" + std::to_string(hex.toInt()));
						actor.position = hex;
						const int attacks = actor.getTotalAttacks(false);
						for(int strike = 0; strike < attacks && actor.alive() && enemy->alive(); ++strike)
						{
							hit(actor, *enemy, false, false);
							// Server retaliation belongs only to the first strike of
							// this sequence, even for unlimited retaliators.
							if(strike == 0 && enemy->ableToRetaliate() && actor.alive()
								&& !actor.hasBonusOfType(BonusType::BLOCKS_RETALIATION))
								hit(*enemy, actor, false, true);
						}
						return;
					}
		auto distance = [&](BattleHex hex)
		{
			int best = 1000;
			for(const auto & enemy : units)
				if(enemy->alive() && enemy->unitSide() != actor.unitSide())
					best = std::min(best, static_cast<int>(BattleHex::getDistance(hex, enemy->position)));
			return best;
		};
		BattleHex best = actor.position;
		for(const auto hex : available)
			if(distance(hex) < distance(best) || (distance(hex) == distance(best) && hex.toInt() < best.toInt())) best = hex;
		if(trace) record(std::string(best == actor.position ? "defend actor=" : "move actor=") + std::to_string(actor.unitId()) + " hex=" + std::to_string(best.toInt()));
		if(best == actor.position)
		{
			ownedBonuses.at(actor.unitId())->defend();
			actor.defending = true;
		}
		actor.position = best;
	}
};
}

SampledBattleOutcome runSampledBattle(const CBattleInfoCallback & initial,
	const IGameInfoCallback & frozenSettings, int seed, std::vector<std::string> * trace, const std::function<bool()> * shouldStop)
{
	SampledBattleOutcome result;
	if(!initial.getBattle() || initial.battleGetDefendedTown() || initial.battleTacticDist() || initial.battleGetRound() > 1
		|| initial.battleGetFightingHero(BattleSide::ATTACKER) || initial.battleGetFightingHero(BattleSide::DEFENDER))
	{
		result.unsupportedReason = "requires detached no-hero snapshot without siege/tactics";
		return result;
	}
	for(const auto & obstacle : initial.battleGetAllObstacles())
		if(!obstacle || typeid(*obstacle) != typeid(CObstacleInstance)
			|| (obstacle->obstacleType != CObstacleInstance::USUAL && obstacle->obstacleType != CObstacleInstance::ABSOLUTE_OBSTACLE))
		{
			result.unsupportedReason = "dynamic or subclassed obstacle";
			return result;
		}
	const auto source = initial.battleAliveUnits();
	std::array<unsigned, 2> counts{};
	for(const auto * unit : source)
	{
		++counts[sideIndex(unit->unitSide())];
		if(!supportedBonuses(*unit, result) || unit->getAvailableHealth() != unit->getTotalHealth()
			|| unit->moved() || unit->waited() || unit->defended())
		{
			result.unsupportedReason = "unsupported ability or already-acted/wounded initial stack";
			return result;
		}
	}
	if(counts[0] == 0 || counts[1] == 0 || counts[0] > 7 || counts[1] > 7)
	{
		result.unsupportedReason = "army size";
		return result;
	}
	SampledBattle battle(initial, frozenSettings, source, seed, trace);
	for(unsigned round = 0; round < MAX_ROUNDS && result.actions < MAX_ACTIONS && !battle.finished(); ++round)
	{
		battle.roundNumber = round + 1;
		for(auto & unit : battle.units) unit->afterNewRound();
		while(!battle.finished() && result.actions < MAX_ACTIONS)
		{
			if(shouldStop && (*shouldStop)())
			{
				result.status = SampledBattleStatus::BUDGET_EXHAUSTED;
				result.unsupportedReason = "decision cancelled or deadline reached";
				return result;
			}
			// Recompute after every action: a killed equal-speed stack must not
			// influence later side-alternation ties in a stale round queue.
			std::vector<battle::Units> queue;
			battle.battleGetTurnOrder(queue, 1, 0, result.actions == 0 ? 0 : -1);
			if(queue.empty() || queue.front().empty()) break;
			auto * actor = battle.find(queue.front().front()->unitId());
			if(!actor || !actor->alive()) break;
			if(trace) battle.record("activation round=" + std::to_string(round + 1) + " actor=" + std::to_string(actor->unitId()) + " health=" + std::to_string(actor->getAvailableHealth()) + " hex=" + std::to_string(actor->position.toInt()));
			battle.ownedBonuses.at(actor->unitId())->beforeActivation();
			const int morale = actor->moraleVal();
			const bool badMorale = morale < 0 && battle.randomizer.rollBadMorale(battle.armyID(*actor), -morale);
			if(badMorale && trace) battle.record("bad_morale actor=" + std::to_string(actor->unitId()));
			if(!badMorale) battle.act(*actor);
			actor->movedThisRound = true;
			battle.activeStack = actor->unitId();
			++result.actions;
			if(!badMorale && !actor->defending && actor->alive() && actor->canMove() && morale > 0 && !battle.finished()
				&& result.actions < MAX_ACTIONS && battle.randomizer.rollGoodMorale(battle.armyID(*actor), morale))
			{
				if(trace) battle.record("good_morale actor=" + std::to_string(actor->unitId()));
				actor->hadMorale = true;
				battle.act(*actor);
				++result.actions;
			}
		}
	}
	result.status = battle.finished() ? SampledBattleStatus::FINISHED : SampledBattleStatus::ROLLOUT_LIMIT;
	if(result.status == SampledBattleStatus::ROLLOUT_LIMIT)
		result.unsupportedReason = result.actions >= MAX_ACTIONS ? "rollout activation limit" : "rollout round limit";
	for(const auto & unit : battle.units)
	{
		result.remainingHealth[sideIndex(unit->unitSide())] += unit->getAvailableHealth();
		result.remainingCreatures[sideIndex(unit->unitSide())] += unit->getCount();
		const auto original = std::find_if(source.begin(), source.end(), [&unit](const auto * candidate) { return candidate->unitId() == unit->unitId(); });
		const double value = unit->unitType()->getAIValue();
		result.initialArmyValue[sideIndex(unit->unitSide())] += value * (*original)->getCount();
		result.remainingArmyValue[sideIndex(unit->unitSide())] += value * unit->getCount();
		result.stacks.push_back({unit->unitId(), unit->creatureId(), unit->unitSide(), (*original)->getCount(), unit->getCount()});
	}
	if(result.status == SampledBattleStatus::FINISHED)
		result.winner = result.remainingHealth[0] ? BattleSide::ATTACKER : BattleSide::DEFENDER;
	return result;
}
}
