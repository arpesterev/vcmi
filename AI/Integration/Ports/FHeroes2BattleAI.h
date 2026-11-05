/*
 * FHeroes2BattleAI.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later.
 * Full text of license available in license.txt file, in main folder.
 */
#pragma once

#include "SourceTactics.h"
#include "FHeroes2SpellPolicy.h"
#include "FHeroes2BerserkOrder.h"
#include "FHeroes2SiegeGeometry.h"

#include <array>
#include "FHeroes2RoundLimiter.h"

#include <map>
#include "FHeroes2RetreatDecision.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/entities/artifact/ArtifactUtils.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/CStack.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/callback/CBattleCallback.h"
#include "../../../lib/callback/CBattleGameInterface.h"

namespace AIIntegration
{
/// Partial tactical port with damage/restoration magic. VCMI supplies rule projections;
/// upstream buff, retreat, archer-kiting and siege-position policies remain incomplete.
class FHeroes2BattleAI final : public CBattleGameInterface
{
public:
	void initBattleInterface(std::shared_ptr<Environment> environment, std::shared_ptr<CBattleCallback> callback) override
	{
		if(!callback)
			throw std::invalid_argument("FHeroes2Tactics requires a battle callback");
		cb = std::move(callback);
		env = std::move(environment);
		preferences = {};
		humanAutocombat = false;
		logAi->info("AI_SOURCE_POLICY backend=FHeroes2Tactics source=0dbfe9f386f33c2500842f4a59cd284d7f03d541 partial=true projection=VCMI");
	}

	void initBattleInterface(std::shared_ptr<Environment> environment, std::shared_ptr<CBattleCallback> callback, AutocombatPreferences prefs) override
	{
		initBattleInterface(std::move(environment), std::move(callback));
		preferences = prefs;
		humanAutocombat = true;
	}

	void yourTacticPhase(const BattleID & battleID, int) override
	{
		if(!cb)
			throw std::logic_error("FHeroes2Tactics is not initialized");
		cb->battleMakeTacticAction(battleID, BattleAction::makeEndOFTacticPhase(cb->getBattle(battleID)->battleGetTacticsSide()));
	}

	void activeStack(const BattleID & battleID, const CStack * stack) override
	{
		if(!cb || !stack)
			throw std::logic_error("FHeroes2Tactics received an invalid decision event");
		const auto battle = cb->getBattle(battleID);
		if(!battle)
			throw std::logic_error("FHeroes2Tactics received an unknown battle");
		// Pinned fheroes2 ai_battle.cpp::BattlePlanner::planUnitTurn
		// returns berserkTurn before army analysis, retreat or hero magic.
		// A genuinely compelled native stack must execute its creature turn
		// first, even when the controlling hero could cast an attractive spell.
		// Invalidate a farewell withdrawal queued on a preceding callback:
		// a compelled turn is not the source spell -> withdrawal sequence.
		if(stack->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE))
		{
			if(pendingWithdrawals.erase(battleID.getNum()) != 0)
				logAi->warn("AI_PORT_FAREWELL_CANCELLED backend=FHeroes2Tactics battle=%d reason=compelled_berserk",
					battleID.getNum());
			const auto action = chooseBerserkAction(*battle, *stack);
			logAi->debug("AI_PORT_BERSERK backend=FHeroes2Tactics battle=%d unit=%d action=%s",
				battleID.getNum(), stack->unitId(), action.toString());
			cb->battleMakeUnitAction(battleID, action);
			return;
		}
		// fheroes2 planUnitTurn queues SPELLCAST followed by RETREAT or
		// SURRENDER. VCMI delivers only one action per callback, so resolve
		// the queued withdrawal when the spell has been applied and the
		// battle's active-stack callback resumes. The engine must reauthorize
		// the final action; a changed battle state is never force-retreated.
		if(const auto pending = pendingWithdrawals.find(battleID.getNum());
			pending != pendingWithdrawals.end())
		{
			const auto requested = pending->second;
			pendingWithdrawals.erase(pending);
			if(requested == Ports::FHeroes2::Withdrawal::RETREAT && battle->battleCanFlee())
			{
				cb->battleMakeUnitAction(battleID, BattleAction::makeRetreat(battle->battleGetMySide()));
				return;
			}
			if(requested == Ports::FHeroes2::Withdrawal::SURRENDER)
			{
				const auto * hero = battle->battleGetMyHero();
				const auto * game = env ? env->game() : nullptr;
				if(hero && game && battle->battleCanSurrender(hero->getOwner()))
				{
					const int cost = battle->battleGetSurrenderCost();
					if(cost >= 0 && game->getResource(hero->getOwner(), GameResID::GOLD) >= cost)
					{
						cb->battleMakeUnitAction(battleID, BattleAction::makeSurrender(battle->battleGetMySide()));
						return;
					}
				}
			}
			logAi->warn("AI_PORT_FAREWELL_CANCELLED backend=FHeroes2Tactics battle=%d reason=withdrawal_no_longer_legal",
				battleID.getNum());
		}
		// fheroes2 exits an AI attacker's battle after 50 consecutive
		// rounds with no deaths. VCMI must authorize retreat, and a
		// human-controlled autobattle must never lose its hero this way.
		// This is evaluated before another spell or unit action.
		if(!human && !humanAutocombat && battle->battleGetMySide() == BattleSide::ATTACKER)
		{
			uint64_t attackerDeaths = 0;
			uint64_t defenderDeaths = 0;
			for(const CStack * unit : battle->battleGetAllStacks())
			{
				const auto killed = static_cast<uint64_t>(std::max(0, unit->getKilled()));
				if(unit->unitSide() == BattleSide::ATTACKER)
					attackerDeaths += killed;
				else if(unit->unitSide() == BattleSide::DEFENDER)
					defenderDeaths += killed;
			}
			auto & limiter = stalemateByBattle[battleID.getNum()];
			if(limiter.observe(battle->battleGetRound(), attackerDeaths, defenderDeaths)
				&& battle->battleGetMyHero() && battle->battleCanFlee())
			{
				logAi->info("AI_PORT_STALEMATE backend=FHeroes2Tactics battle=%d rounds=%u action=RETREAT",
					battleID.getNum(), Ports::FHeroes2::DeathlessRoundLimiter::MAX_ROUNDS_WITHOUT_DEATHS);
				cb->battleMakeUnitAction(battleID, BattleAction::makeRetreat(BattleSide::ATTACKER));
				return;
			}
		}
		if(!human && !humanAutocombat && env && env->game())
		{
			const auto * hero = battle->battleGetMyHero();
			const auto * player = hero ? env->game()->tryGetPlayerState(hero->getOwner()) : nullptr;
			if(hero && player && !player->isHuman())
			{
				double ourStrength = 0;
				double enemyStrength = 0;
				int initialFriendlySlots = 0;
				bool fullyLostStack = false;
				for(const auto * unit : battle->battleGetAllStacks())
				{
					if(unit->unitSide() == battle->battleGetMySide()
						&& (unit->getCount() > 0 || unit->getKilled() > 0))
					{
						++initialFriendlySlots;
						fullyLostStack |= unit->getCount() == 0 && unit->getKilled() > 0;
					}
					if(!unit->alive() || unit->isInvincible())
						continue;
					if(battle->battleGetOwner(unit) == hero->getOwner())
						ourStrength += strength(*unit);
					else
						enemyStrength += strength(*unit);
				}
				const auto heroes = player->getHeroes();
				const auto towns = player->getTowns();
				const bool canRehire = heroes.size() > 1 || (!towns.empty()
					&& !(towns.size() == 1 && hero->getVisitedTown() == towns.front()));
				const bool valuableArtifacts = std::any_of(
					ArtifactUtils::commonWornSlots().begin(), ArtifactUtils::commonWornSlots().end(),
					[&](const ArtifactPosition & slot) { return hero->getArt(slot) != nullptr; });
				const int primarySkillTotal = hero->getPrimSkillLevel(PrimarySkill::ATTACK)
					+ hero->getPrimSkillLevel(PrimarySkill::DEFENSE)
					+ hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER)
					+ hero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE);
				const bool canFlee = battle->battleCanFlee();
				const bool canSurrender = battle->battleCanSurrender(hero->getOwner());
				const int surrenderCost = canSurrender ? battle->battleGetSurrenderCost() : -1;
				const bool affordable = surrenderCost >= 0
					&& env->game()->getResource(hero->getOwner(), GameResID::GOLD) >= surrenderCost;
				// Source NORMAL difficulty ratio. No hidden-map intelligence:
				// only the current battle, owned hero and owned player economy.
				const Ports::FHeroes2::WithdrawalContext context{ourStrength, enemyStrength, 100.0 / 7.5,
					false, initialFriendlySlots < 4 || fullyLostStack, canFlee, canSurrender,
					affordable, valuableArtifacts, canRehire, primarySkillTotal};
				const auto outcome = Ports::FHeroes2::decideWithdrawal(context);
				if(outcome != Ports::FHeroes2::Withdrawal::FIGHT && preferences.enableSpellsUsage)
				{
					if(const auto farewell = FHeroes2SpellPolicy::choose(battle, env.get(), stack, true))
					{
						pendingWithdrawals[battleID.getNum()] = outcome;
						logAi->debug("AI_PORT_FAREWELL backend=FHeroes2Tactics battle=%d action=%s",
							battleID.getNum(), farewell->toString());
						cb->battleMakeSpellAction(battleID, *farewell);
						return;
					}
				}
				switch(outcome)
				{
					case Ports::FHeroes2::Withdrawal::RETREAT:
						cb->battleMakeUnitAction(battleID, BattleAction::makeRetreat(battle->battleGetMySide()));
						return;
					case Ports::FHeroes2::Withdrawal::SURRENDER:
						cb->battleMakeUnitAction(battleID, BattleAction::makeSurrender(battle->battleGetMySide()));
						return;
					case Ports::FHeroes2::Withdrawal::FIGHT:
						break;
				}
			}
		}
		if(preferences.enableSpellsUsage && !stack->isCatapult() && !stack->isFirstAidTent())
		{
			if(const auto spell = FHeroes2SpellPolicy::choose(battle, env.get(), stack))
			{
				logAi->debug("AI_PORT_SPELL backend=FHeroes2Tactics battle=%d action=%s", battleID.getNum(), spell->toString());
				cb->battleMakeSpellAction(battleID, *spell);
				// The engine applies the spell and reactivates a surviving stack.
				return;
			}
		}
		const auto action = chooseAction(*battle, *stack);
		logAi->debug("AI_PORT_ACTION backend=FHeroes2Tactics battle=%d unit=%d action=%s",
			battleID.getNum(), stack->unitId(), action.toString());
		cb->battleMakeUnitAction(battleID, action);
	}

void battleStart(const BattleID & battleID, const CCreatureSet *, const CCreatureSet *,
		int3, const CGHeroInstance *, const CGHeroInstance *, BattleSide, bool) override
	{
		stalemateByBattle[battleID.getNum()].reset();
		pendingWithdrawals.erase(battleID.getNum());
	}

	void battleEnd(const BattleID & battleID, const BattleResult *, QuestionID) override
	{
		stalemateByBattle.erase(battleID.getNum());
		pendingWithdrawals.erase(battleID.getNum());
	}

private:
	std::map<int32_t, Ports::FHeroes2::Withdrawal> pendingWithdrawals;
	std::map<int32_t, Ports::FHeroes2::DeathlessRoundLimiter> stalemateByBattle;
	std::shared_ptr<CBattleCallback> cb;
	std::shared_ptr<Environment> env;
	AutocombatPreferences preferences;
	bool humanAutocombat = false;

	static double strength(const battle::Unit & stack)
	{
		return static_cast<double>(stack.estimateCombatValue());
	}

	static double lossValue(const battle::Unit & stack, const DamageEstimation & estimate)
	{
		const double health = static_cast<double>(stack.getAvailableHealth());
		if(health <= 0)
			return 0;
		const double damage = std::min(health,
			(static_cast<double>(estimate.damage.min) + static_cast<double>(estimate.damage.max)) / 2);
		return strength(stack) * damage / health;
	}

	static double attackValue(CPlayerBattleCallback & battle, const CStack & attacker,
		const CStack & defender, const BattleHex & from, bool shooting, int distance)
	{
		const int attackerPosition = attacker.getPosition().toInt();
		const int defenderPosition = defender.getPosition().toInt();
		const int64_t attackerHealth = attacker.getAvailableHealth();
		const int64_t defenderHealth = defender.getAvailableHealth();
		BattleAttackInfo info(&attacker, &defender, distance, shooting);
		info.attackerPos = from;
		DamageEstimation retaliation;
		const auto damage = battle.battleEstimateDamage(info, &retaliation);
		const double value = lossValue(defender, damage) - (shooting ? 0 : lossValue(attacker, retaliation));
		if(attackerPosition != attacker.getPosition().toInt()
			|| defenderPosition != defender.getPosition().toInt()
			|| attackerHealth != attacker.getAvailableHealth()
			|| defenderHealth != defender.getAvailableHealth())
			throw std::logic_error("Damage preview modified a live battle unit");
		return value;
	}

	// Pinned ai_battle.cpp::optimalAttackValue includes every enemy struck
	// by Hydra-style adjacent melee and the second creature in a dragon's
	// breath line. The old port valued only the directly selected defender,
	// and could therefore select a single-target attack instead of a legal
	// multi-target strike. Ask the native VCMI attack geometry for the
	// *hypothetical attack-from hex* and the exact aimed target cell; VCMI
	// determines which units actually receive collateral damage, including
	// friendly fire. Only the primary defender retaliates in this estimate.
	// Heroes II/III secondary-hit geometry and damage are not identical.
	static double meleeAreaAttackValue(CPlayerBattleCallback & battle, const CStack & attacker,
		const CStack & defender, const BattleHex & from, const BattleHex & aimedHex, int distance)
	{
		double value = attackValue(battle, attacker, defender, from, false, distance);
		for(const auto * victim : battle.getAttackedBattleUnits(
			&attacker, &defender, aimedHex, false, from, defender.getPosition()))
		{
			if(victim == &defender)
				continue;
			BattleAttackInfo hit(&attacker, victim, distance, false);
			hit.attackerPos = from;
			const auto damage = battle.battleEstimateDamage(hit);
			const bool friendly = battle.battleGetOwner(victim) == battle.battleGetOwner(&attacker);
			value += (friendly ? -1.0 : 1.0) * lossValue(*victim, damage);
		}
		return value;
	}

	static Ports::FHeroes2::Tactics tactics(CPlayerBattleCallback & battle, const CStack & stack,
		const std::vector<const CStack *> & enemies)
	{
		Ports::FHeroes2::TacticsInput input;
		for(const auto * friendly : battle.battleGetStacks(CBattleInfoEssentials::ONLY_MINE))
		{
			input.armyStrength += strength(*friendly);
			// fheroes2 analyzeBattleState counts ranged units even when an
			// adjacent enemy currently blocks shooting. BattleCanShoot is a
			// per-turn legality check, not the source unit classification.
			if(friendly->isShooter() && friendly->getMovementRange() > 0)
				input.shootersStrength += strength(*friendly);
		}
		for(const auto * enemy : enemies)
		{
			input.enemyArmyStrength += strength(*enemy);
			if(enemy->isShooter() && enemy->getMovementRange() > 0)
				input.enemyShootersStrength += strength(*enemy);
		}
		if(input.armyStrength <= 0 || input.enemyArmyStrength <= 0)
			return {};
		input.flying = stack.hasBonusOfType(BonusType::FLYING);
		// fheroes2 includes defensive castle/tower advantage when deciding
		// whether to hold the line. The native projection requires actual
		// surviving fortifications; an undefended open-field battle is not a siege.
		input.defendingCastle = stack.unitSide() == BattleSide::DEFENDER
			&& battle.battleGetDefendedTown() != nullptr
			&& battle.battleGetWallState(EWallPart::GATE) != EWallState::NONE;
		// Pinned fheroes2 isPositionLocatedInDefendedArea checks the entire
		// Battle::Position, not only a wide stack's head. During castle defense
		// it uses castle cells instead of the open-field midpoint. VCMI owns
		// the occupied-cell geometry and siege wall classification.
		const auto isDefended = [&](const BattleHex & hex)
		{
			if(!hex.isValid())
				return false;
			if(input.defendingCastle)
				return battle.battleIsInsideWalls(hex);
			return stack.unitSide() == BattleSide::ATTACKER
				? hex.getX() < GameConstants::BFIELD_WIDTH / 2
				: hex.getX() >= GameConstants::BFIELD_WIDTH / 2;
		};
		input.insideDefendedArea = Ports::FHeroes2::isPositionInDefendedArea(
			stack.getHexes(), isDefended);
		return Ports::FHeroes2::selectTactics(input);
	}

	// Pinned fheroes2 meleeUnitOffense advances towards cells under siege
	// walls when no enemy has a reachable attack position. Heroes II has a
	// cellsUnderWallsIndexes path policy; VCMI does not supply that list.
	// Use living garrison footprints as an approximate destination and submit
	// only a native-reachable head hex. This is NOT equivalent wall pathfinding.
	static std::optional<BattleAction> approachSiegeWall(CPlayerBattleCallback & battle,
		const CStack & stack, const BattleHexArray & available,
		const std::vector<const CStack *> & enemies)
	{
		if(stack.unitSide() != BattleSide::ATTACKER || battle.battleGetDefendedTown() == nullptr
			|| stack.getMovementRange() == 0 || stack.isTurret()
			|| enemies.empty() || !stack.getPosition().isValid())
			return std::nullopt;

		const auto distanceToDefenders = [&](const BattleHex & head)
		{
			int nearest = GameConstants::BFIELD_SIZE;
			for(const auto * defender : enemies)
			{
				if(!defender->alive() || !defender->getPosition().isValid())
					continue;
				for(const auto & from : stack.getHexes(head))
					for(const auto & to : defender->getHexes())
						nearest = std::min(nearest, static_cast<int>(BattleHex::getDistance(from, to)));
			}
			return nearest;
		};
		int nearest = distanceToDefenders(stack.getPosition());
		BattleHex best;
		for(const auto & hex : available)
		{
			if(!hex.isAvailable() || hex == stack.getPosition())
				continue;
			const int distance = distanceToDefenders(hex);
			if(distance < nearest || (distance == nearest && best.isValid() && hex.toInt() < best.toInt()))
			{
				best = hex;
				nearest = distance;
			}
		}
		if(best.isValid())
			return BattleAction::makeMove(&stack, best);

		// A closed, fortified garrison may be beyond direct reach. The
		// source then searches cells under castle walls; VCMI has different
		// wall geometry, so use its *actual* wall hexes and legal movement.
		// This is only a fallback after no step toward a living defender.
		std::vector<BattleHex> wallTargets;
		for(EWallPart part : {EWallPart::BOTTOM_WALL, EWallPart::BELOW_GATE,
			EWallPart::GATE, EWallPart::OVER_GATE, EWallPart::UPPER_WALL})
		{
			const auto state = battle.battleGetWallState(part);
			if(state == EWallState::NONE || state == EWallState::DESTROYED)
				continue;
			const BattleHex wallHex = battle.wallPartToBattleHex(part);
			if(wallHex.isAvailable())
				wallTargets.push_back(wallHex);
		}
		if(wallTargets.empty())
			return std::nullopt;

		const auto wallDistance = [&](const BattleHex & head)
		{
			int distance = GameConstants::BFIELD_SIZE;
			for(const auto & occupied : stack.getHexes(head))
				for(const auto & wall : wallTargets)
					distance = std::min(distance, static_cast<int>(BattleHex::getDistance(occupied, wall)));
			return distance;
		};
		const int startingWallDistance = wallDistance(stack.getPosition());
		const auto reachability = battle.getReachability(&stack);
		Ports::FHeroes2::SiegeWallApproach bestWall{};
		bestWall.headHex = -1;
		for(const auto & hex : available)
		{
			if(!hex.isAvailable() || hex == stack.getPosition())
				continue;
			const Ports::FHeroes2::SiegeWallApproach candidate{
				wallDistance(hex), static_cast<int>(reachability.distances[hex.toInt()]), hex.toInt()};
			if(Ports::FHeroes2::preferSiegeWallStep(candidate, bestWall, startingWallDistance))
				bestWall = candidate;
		}
		if(bestWall.headHex >= 0)
			return BattleAction::makeMove(&stack, BattleHex(bestWall.headHex));
		return std::nullopt;
	}

	// fheroes2 meleeUnitOffense first pursues targets that cannot evade,
	// then maximizes threat divided by the path distance. Unlike the former
	// nearest-stack policy, this can pursue a remote archer over nearby bait.
	// The threat and legal routes are native VCMI projections.
	static BattleAction approach(CPlayerBattleCallback & battle, const CStack & stack,
		const ReachabilityInfo & reachability, const BattleHexArray & available,
		const std::vector<const CStack *> & enemies, bool cautious)
	{
		BattleHex destination;
		std::optional<Ports::FHeroes2::DistantTarget> best;
		const int ownSpeed = static_cast<int>(stack.getMovementRange());
		for(const auto * enemy : enemies)
		{
			BattleHex goal;
			int shortest = GameConstants::BFIELD_SIZE;
			for(const auto & hex : enemy->getAttackableHexes(&stack))
			{
				if(!hex.isValid() || !battle.isMeleeAttackPossible(&stack, enemy, hex))
					continue;
				const int distance = reachability.distances[hex.toInt()];
				if(distance < shortest || (distance == shortest && (!goal.isValid() || hex.toInt() < goal.toInt())))
				{
					shortest = distance;
					goal = hex;
				}
			}
			if(!goal.isValid() || shortest >= GameConstants::BFIELD_SIZE)
				continue;

			// Pinned fheroes2 meleeUnitOffense calls GetSpeed(false, true).
			// Blind/Paralyze opponents cannot evade on their next turn even
			// when VCMI exposes a positive raw movement range under NOT_ACTIVE.
			// This affects pursuit priority, not the legality of attacking them.
			const int enemySpeed = enemy->hasBonusOfType(BonusType::NOT_ACTIVE)
				? 0 : static_cast<int>(enemy->getMovementRange());
			const bool cannotEvade = enemy->hasBonusOfType(BonusType::SHOOTER)
				|| enemySpeed <= 0 || (!enemy->hasBonusOfType(BonusType::FLYING) && enemySpeed < ownSpeed);
			const Ports::FHeroes2::DistantTarget candidate{
				std::max(0.0, strength(*enemy)), std::max(1, shortest), cannotEvade,
				static_cast<int>(enemy->unitId())};
			if(!best || Ports::FHeroes2::preferDistantTarget(candidate, *best))
			{
				best = candidate;
				destination = goal;
			}
		}
		if(!destination.isValid())
		{
			if(const auto wall = approachSiegeWall(battle, stack, available, enemies))
				return *wall;
			return BattleAction::makeDefend(&stack);
		}

		if(stack.hasBonusOfType(BonusType::FLYING))
		{
			BattleHex nearest;
			int bestDistance = GameConstants::BFIELD_SIZE;
			for(const auto & hex : available)
			{
				const int distance = BattleHex::getDistance(destination, hex);
				if(distance < bestDistance || (distance == bestDistance && (!nearest.isValid() || hex.toInt() < nearest.toInt())))
				{
					nearest = hex;
					bestDistance = distance;
				}
			}
			if(nearest.isValid() && nearest != stack.getPosition())
				return BattleAction::makeMove(&stack, nearest);
			return BattleAction::makeDefend(&stack);
		}

		// Pinned fheroes2 findOptimalPositionForSubsequentAttack evaluates
		// each route step, preferring the least exposed step, with farther
		// progress on a tie. Only submit native-reachable VCMI hexes.
		std::vector<BattleHex> pathSteps;
		for(int steps = 0; steps < GameConstants::BFIELD_SIZE && destination.isValid(); ++steps)
		{
			if(available.contains(destination) && destination != stack.getPosition())
			{
				if(!cautious)
					return BattleAction::makeMove(&stack, destination);
				pathSteps.push_back(destination);
			}
			const auto previous = reachability.predecessors[destination.toInt()];
			if(previous == destination)
				break;
			destination = previous;
		}
		if(pathSteps.empty())
		{
			if(const auto wall = approachSiegeWall(battle, stack, available, enemies))
				return *wall;
			return BattleAction::makeDefend(&stack);
		}
		// Predecessors arrive from far to near; source visits near to far.
		std::reverse(pathSteps.begin(), pathSteps.end());
		const auto friends = battle.battleGetStacks(CBattleInfoEssentials::ONLY_MINE);
		BattleHex safest;
		double lowestThreat = 0;
		for(const auto & step : pathSteps)
		{
			double threat = 0;
			for(const auto * enemy : enemies)
			{
				// Source skips flyers and unengaged archers in the path-risk
				// sum because changing the melee destination cannot avoid them.
				if(enemy->hasBonusOfType(BonusType::FLYING))
					continue;
				if(enemy->isShooter())
				{
					const bool engaged = std::any_of(friends.begin(), friends.end(), [&](const CStack * friendly)
					{
						for(const auto & allyHex : friendly->getHexes())
							for(const auto & enemyHex : enemy->getHexes())
								if(BattleHex::getDistance(allyHex, enemyHex) == 1)
									return true;
						return false;
					});
					if(!engaged)
						continue;
				}
				// Pinned fheroes2 isUnitAbleToApproachPosition uses
				// GetSpeed(false, true): Blind/Paralyze means STANDING even
				// on the following turn. Native VCMI getMovementRange()
				// may still report the unmodified speed under NOT_ACTIVE;
				// it must not create imaginary next-turn melee reach.
				// Do not use canMove(): it also reflects whether a stack
				// has already acted, which the source explicitly ignores.
				if(enemy->hasBonusOfType(BonusType::NOT_ACTIVE))
					continue;
				const int speed = static_cast<int>(enemy->getMovementRange());
				if(speed <= 0)
					continue;
				int distance = GameConstants::BFIELD_SIZE;
				for(const auto & ownHex : stack.getHexes(step))
					for(const auto & enemyHex : enemy->getHexes())
						distance = std::min(distance, static_cast<int>(BattleHex::getDistance(ownHex, enemyHex)));
				// Conservative geometric bound for source
				// isUnitAbleToApproachPosition. VCMI walls and obstacles may
				// reduce reach: this is a risk estimate, not an action rule.
				if(distance <= speed + 1)
					threat += strength(*enemy);
			}
			// Source ties within 0.001 and favors farther path progress.
			if(!safest.isValid() || threat < lowestThreat || std::fabs(threat - lowestThreat) < 0.001)
			{
				lowestThreat = threat;
				safest = step;
			}
		}
		return safest.isValid() ? BattleAction::makeMove(&stack, safest) : BattleAction::makeDefend(&stack);
	}


	// fheroes2 meleeUnitDefense prefers covering valuable friendly archers.
	// This conservative VCMI projection moves a melee stack into a reachable
	// neighboring position only when ground melee can threaten a shooter
	// next turn. Both occupied cells of wide archers *and wide covering melee
	// units* count in the distance check. The VCMI movement projection supplies
	// legal wide-unit head positions; never disrupt an immediate attack.
	// Upstream side-cover priorities and spell/area-threat spacing remain partial.
	static std::optional<BattleAction> coverThreatenedShooter(CPlayerBattleCallback & battle,
		const CStack & stack, const ReachabilityInfo & reachability,
		const BattleHexArray & available, const std::vector<const CStack *> & enemies)
	{
		// fheroes2 meleeUnitDefense explicitly skips archer cover when the
		// current stack's original army differs from its current controller.
		// In VCMI this happens with Hypnotize: a stolen guard must not screen
		// the controller's archers as though it belonged to their army.
		if(stack.hasBonusOfType(BonusType::SHOOTER)
			|| stack.unitOwner() != battle.battleGetOwner(&stack))
			return std::nullopt;
		const auto friends = battle.battleGetStacks(CBattleInfoEssentials::ONLY_MINE);
		const auto distanceToShooter = [&](const BattleHex & head, const CStack & shooter)
		{
			int nearest = GameConstants::BFIELD_SIZE;
			// A wide guard covers from either occupied cell; using only its
			// head incorrectly rejects legal screens beside the guard's tail.
			for(const auto & guardHex : stack.getHexes(head))
				for(const auto & shooterHex : shooter.getHexes())
					nearest = std::min(nearest,
						static_cast<int>(BattleHex::getDistance(guardHex, shooterHex)));
			return nearest;
		};
		double totalShooterStrength = 0;
		for(const auto * friendly : friends)
		{
			// Pinned ai_battle.cpp::analyzeBattleState adds to _myRangedUnitsOnly
			// only isArchers() && !isImmovable(). The defense-distance
			// modifier in meleeUnitDefense uses precisely that aggregate.
			// VCMI Blind/Paralyze can retain SHOOTER and raw movement speed;
			// their stacks must not inflate the cost of covering *other*
			// friendly archers. Do not remove such stacks as candidate
			// beneficiaries: the source's cover loop still visits them.
			if(friendly != &stack && friendly->alive() && friendly->getPosition().isValid()
				&& friendly->hasBonusOfType(BonusType::SHOOTER)
				&& friendly->getMovementRange() > 0
				&& !friendly->hasBonusOfType(BonusType::NOT_ACTIVE))
				totalShooterStrength += strength(*friendly);
		}
		if(totalShooterStrength <= 0)
			return std::nullopt;

		// Pinned fheroes2 analyzeBattleState sets _avoidStackingUnits when
		// active area-shot archers make up more than 10% of enemy strength.
		// Its meleeUnitDefense then looks one more cell away when screening
		// friendly archers. VCMI SHOOTS_ALL_ADJACENT is only a native splash
		// projection, not an assertion that Heroes II and III area shots agree.
		// In particular, incapacitated or engaged enemy shooters do not
		// project a ranged splash shot for this turn.
		double enemyStrength = 0;
		double areaShotStrength = 0;
		for(const auto * enemy : enemies)
		{
			if(!enemy->alive())
				continue;
			const double value = strength(*enemy);
			enemyStrength += value;
			// Pinned fheroes2 analyzeBattleState excludes immovable archers
			// from AREA_SHOT pressure. Native Blind/Paralyze can leave the
			// creature's ranged bonus intact while forbidding its next action;
			// do not project a future splash threat from such a stack.
			if(enemy->isShooter() && !enemy->hasBonusOfType(BonusType::NOT_ACTIVE)
				&& enemy->hasBonusOfType(BonusType::SHOOTS_ALL_ADJACENT)
				&& battle.battleCanShoot(enemy))
				areaShotStrength += value;
		}
		const bool avoidSplashGrouping = enemyStrength > 0
			&& areaShotStrength / enemyStrength > 0.1;
		const int coverGap = avoidSplashGrouping ? 2 : 1;

		BattleHex bestCoverHex;
		double bestCoverValue = -std::numeric_limits<double>::infinity();
		int bestTravel = GameConstants::BFIELD_SIZE;
		for(const auto * friendly : friends)
		{
			if(friendly == &stack || !friendly->alive() || !friendly->getPosition().isValid()
				|| !friendly->hasBonusOfType(BonusType::SHOOTER))
				continue;
			const bool threatened = std::any_of(enemies.begin(), enemies.end(), [&](const CStack * enemy)
			{
				if(!enemy->getPosition().isValid() || enemy->hasBonusOfType(BonusType::SHOOTER))
					return false;
				// The threat is the *enemy's* real footprint, not a hypothetical
				// footprint of this covering unit placed at an enemy cell. Reusing
				// distanceToShooter here projected a wide guard's tail onto narrow
				// opponents, inventing a one-hex closer melee threat.
				int nearest = GameConstants::BFIELD_SIZE;
				for(const auto & enemyHex : enemy->getHexes())
					for(const auto & shooterHex : friendly->getHexes())
						nearest = std::min(nearest,
							static_cast<int>(BattleHex::getDistance(enemyHex, shooterHex)));
				// The pinned source's future approach check cannot project a
				// Blind/Paralyze-disabled melee unit across the battlefield.
				// Keep its adjacent contact: even an immobile enemy still
				// blocks a shooter's ranged attack from one hex away.
				const int futureReach = enemy->hasBonusOfType(BonusType::NOT_ACTIVE)
					? 1 : static_cast<int>(enemy->getMovementRange()) + 1;
				return nearest <= futureReach;
			});
			if(!threatened)
				continue;
			// A guard already adjacent to one threatened shooter must not
			// immediately defend without considering the other shooters.
			// Treat its current screen as a zero-travel candidate, then
			// compare every reachable cover using the same source-derived
			// archer-strength / travel-penalty ranking.
			if(distanceToShooter(stack.getPosition(), *friendly) == coverGap)
			{
				const double value = strength(*friendly);
				if(value > bestCoverValue)
				{
					bestCoverValue = value;
					bestCoverHex = stack.getPosition();
					bestTravel = 0;
				}
				continue;
			}

			for(const auto & hex : available)
			{
				if(!hex.isAvailable() || hex == stack.getPosition()
					|| distanceToShooter(hex, *friendly) != coverGap)
					continue;
				const int travel = reachability.distances[hex.toInt()];
				if(travel <= 0 || travel > static_cast<int>(stack.getMovementRange()))
					continue;
				// Source: archer strength minus the path-distance penalty equal
				// to all friendly shooter strength / 15 per traveled hex.
				const double value = strength(*friendly) - totalShooterStrength * travel / 15.0;
				if(value > bestCoverValue || (value == bestCoverValue
					&& (travel < bestTravel || (travel == bestTravel
						&& (!bestCoverHex.isValid() || hex.toInt() < bestCoverHex.toInt())))))
				{
					bestCoverHex = hex;
					bestCoverValue = value;
					bestTravel = travel;
				}
			}
		}
		return bestCoverHex.isValid() && bestCoverHex != stack.getPosition()
			? std::optional<BattleAction>(BattleAction::makeMove(&stack, bestCoverHex))
			: std::nullopt;
	}

	// Pinned fheroes2 meleeUnitDefense (ai_battle.cpp, lines 1774 and
	// 2012-2039): before generic melee target selection, attack enemies
	// physically blocking friendly archers. The source scores each blocker
	// with BestAttackOutcome. Here the positions and damage previews are
	// native VCMI projections, and only immediately legal attacks qualify.
	// In particular, a blocked archer must not redirect a guard to an
	// unrelated, more profitable melee target.
	static std::optional<BattleAction> attackShooterBlocker(CPlayerBattleCallback & battle,
		const CStack & guard, const ReachabilityInfo & reachability,
		const BattleHexArray & available, const std::vector<const CStack *> & enemies)
	{
		if(guard.hasBonusOfType(BonusType::SHOOTER)
			|| guard.unitOwner() != battle.battleGetOwner(&guard))
			return std::nullopt;

		const auto friends = battle.battleGetStacks(CBattleInfoEssentials::ONLY_MINE);
		const CStack * chosenBlocker = nullptr;
		BattleHex chosenFrom;
		BattleHex chosenTargetHex;
		double chosenShooterValue = std::numeric_limits<double>::lowest();
		for(const auto * shooter : friends)
		{
			if(shooter == &guard || !shooter->alive() || !shooter->getPosition().isValid()
				|| !shooter->isShooter() || battle.battleCanShoot(shooter))
				continue;

			const auto shooterHexes = shooter->getHexes();
			const CStack * bestBlocker = nullptr;
			BattleHex bestFrom;
			BattleHex bestTargetHex;
			Ports::FHeroes2::MeleeOutcome bestOutcome;
			for(const auto * enemy : enemies)
			{
				// Lack of a shot can also result from a spell or empty ammo.
				// Only real enemy contact can make a unit an archer blocker.
				const auto enemyHexes = enemy->getHexes();
				const bool adjacent = std::any_of(shooterHexes.begin(),
					shooterHexes.end(), [&](const BattleHex & friendlyHex)
					{
						return std::any_of(enemyHexes.begin(), enemyHexes.end(),
							[&](const BattleHex & enemyHex)
							{
								return BattleHex::getDistance(friendlyHex, enemyHex) == 1;
							});
					});
				if(!adjacent)
					continue;
				for(const auto & from : available)
				{
					if(!from.isAvailable())
						continue;
					const auto targets = battle.meleeAttackHexes(&guard, enemy, from);
					// Pinned ai_battle.cpp::meleeUnitDefense delegates blockers to
					// BestAttackOutcome, which uses optimalAttackValue/Vector.
					// Evaluate EVERY native-legal aim, as in the offensive path:
					// a breath or all-adjacent hit can release an archer while
					// injuring other stacks (including friendly collateral).
					// Merely taking targets.front() would discard the vector
					// whose secondary victims make it the better blocker attack.
					for(const auto & aimedHex : targets)
					{
						if(battle.battleGetUnitByPos(aimedHex) != enemy)
							continue;
						const double value = meleeAreaAttackValue(battle, guard, *enemy,
							from, aimedHex, reachability.distances[from.toInt()]);
						const Ports::FHeroes2::MeleeOutcome outcome{
							from.toInt(), strength(*enemy), value, true};
						if(!bestBlocker || Ports::FHeroes2::isOutcomeImproved(outcome, bestOutcome))
						{
							bestBlocker = enemy;
							bestFrom = from;
							bestTargetHex = aimedHex;
							bestOutcome = outcome;
						}
					}
				}
			}
			if(bestBlocker && (!chosenBlocker || strength(*shooter) > chosenShooterValue))
			{
				chosenShooterValue = strength(*shooter);
				chosenBlocker = bestBlocker;
				chosenFrom = bestFrom;
				chosenTargetHex = bestTargetHex;
			}
		}
		return chosenBlocker
			? std::optional<BattleAction>(BattleAction::makeMeleeAttack(&guard, chosenTargetHex, chosenFrom))
			: std::nullopt;
	}

	// Pinned fheroes2 meleeUnitDefense (ai_battle.cpp): when a friendly
	// shooter is engaged, BestAttackOutcome may return a *future* melee
	// position against its blocker, not only an attack this turn. Follow
	// the native VCMI reachability predecessor chain to a legal first step;
	// never manufacture WALK_AND_ATTACK beyond the current movement range.
	// Heroes II and III pathfinding, obstruction and attack geometry differ.
	static std::optional<BattleAction> pursueShooterBlocker(CPlayerBattleCallback & battle,
		const CStack & guard, const ReachabilityInfo & reachability,
		const BattleHexArray & available, const std::vector<const CStack *> & enemies)
	{
		if(guard.hasBonusOfType(BonusType::SHOOTER)
			|| guard.unitOwner() != battle.battleGetOwner(&guard)
			|| !guard.getPosition().isValid() || guard.getMovementRange() == 0)
			return std::nullopt;
		const auto friends = battle.battleGetStacks(CBattleInfoEssentials::ONLY_MINE);
		double allShooterStrength = 0;
		for(const auto * friendly : friends)
			if(friendly != &guard && friendly->alive() && friendly->isShooter())
				allShooterStrength += strength(*friendly);
		if(allShooterStrength <= 0)
			return std::nullopt;

		// The source declines a remote two-turn screen if another immediate
		// melee attack exists and the guard is too slow to reach the archer.
		const bool immediateMeleeExists = std::any_of(enemies.begin(), enemies.end(),
			[&](const CStack * enemy)
			{
				return std::any_of(available.begin(), available.end(), [&](const BattleHex & from)
				{
					return from.isAvailable() && !battle.meleeAttackHexes(&guard, enemy, from).empty();
				});
			});
		const int speed = static_cast<int>(guard.getMovementRange());
		BattleHex bestStep;
		double bestPriority = -std::numeric_limits<double>::infinity();
		int bestTravel = GameConstants::BFIELD_SIZE;
		for(const auto * shooter : friends)
		{
			if(shooter == &guard || !shooter->alive() || !shooter->isShooter()
				|| !shooter->getPosition().isValid() || battle.battleCanShoot(shooter))
				continue;
			for(const auto * blocker : enemies)
			{
				// Lack of ammunition and magical shot suppression are not
				// evidence of a physically engaged archer.
				bool adjacent = false;
				for(const auto & shooterHex : shooter->getHexes())
					for(const auto & blockerHex : blocker->getHexes())
						adjacent |= BattleHex::getDistance(shooterHex, blockerHex) == 1;
				if(!adjacent)
					continue;

				int shortest = GameConstants::BFIELD_SIZE;
				BattleHex goal;
				for(const auto & from : blocker->getAttackableHexes(&guard))
				{
					if(!from.isValid() || !battle.isMeleeAttackPossible(&guard, blocker, from))
						continue;
					const int distance = static_cast<int>(reachability.distances[from.toInt()]);
					if(distance < shortest || (distance == shortest
						&& (!goal.isValid() || from.toInt() < goal.toInt())))
					{
						shortest = distance;
						goal = from;
					}
				}
				if(!goal.isValid() || shortest <= speed || shortest >= GameConstants::BFIELD_SIZE
					|| (immediateMeleeExists && !guard.hasBonusOfType(BonusType::FLYING)
						&& shortest > speed * 2))
					continue;

				BattleHex step;
				for(int turns = 0; turns < GameConstants::BFIELD_SIZE; ++turns)
				{
					if(available.contains(goal) && goal != guard.getPosition())
					{
						step = goal;
						break;
					}
					const auto previous = reachability.predecessors[goal.toInt()];
					if(!previous.isValid() || previous == goal)
						break;
					goal = previous;
				}
				if(!step.isValid())
					continue;
				// Source defenseDistanceModifier = total shooter strength / 15.
				const double priority = strength(*shooter)
					- allShooterStrength * shortest / 15.0;
				if(!bestStep.isValid() || priority > bestPriority
					|| (priority == bestPriority && (shortest < bestTravel
						|| (shortest == bestTravel && step.toInt() < bestStep.toInt()))))
				{
					bestPriority = priority;
					bestTravel = shortest;
					bestStep = step;
				}
			}
		}
		return bestStep.isValid() ? std::optional<BattleAction>(BattleAction::makeMove(&guard, bestStep))
			: std::nullopt;
	}

	// fheroes2 ai_battle.cpp archerDecision, pinned at 0dbfe9f386f33c2500842f4a59cd284d7f03d541:
	// only a sufficiently faster shooter attempts to escape an imminent melee
	// threat, and only to a position safe from every visible enemy stack.
	// Native VCMI movement hexes supply legality. Geometric enemy reach is
	// deliberately conservative because the currently occupied archer hex
	// could otherwise block the enemy path used to assess a future position.
	static std::optional<BattleAction> archerRetreat(CPlayerBattleCallback & battle,
		const CStack & stack, const std::vector<const CStack *> & enemies)
	{
		// Upstream evaluates escape before its isHandFighting() melee branch.
		// An adjacent slow melee enemy may block a shot without preventing
		// movement to a safe hex; shot legality must not suppress retreat.
		if(!stack.hasBonusOfType(BonusType::SHOOTER) || !stack.getPosition().isValid())
			return std::nullopt;

		const int speed = static_cast<int>(stack.getMovementRange());
		if(speed <= 0)
			return std::nullopt;
		if(std::any_of(enemies.begin(), enemies.end(), [](const CStack * enemy)
		{
			return enemy->hasBonusOfType(BonusType::FLYING);
		}))
			return std::nullopt;

		// fheroes2 archerDecision evaluates the occupied *position*, not
		// only its head cell. A wide shooter can be threatened beside its tail,
		// and a wide enemy may reach either occupied cell. VCMI provides the
		// authoritative footprint for hypothetical legal head positions.
		const auto footprintDistance = [&](const BattleHex & position, const CStack & enemy)
		{
			int nearest = GameConstants::BFIELD_SIZE;
			for(const auto & ownHex : stack.getHexes(position))
				for(const auto & enemyHex : enemy.getHexes())
					nearest = std::min(nearest, static_cast<int>(BattleHex::getDistance(ownHex, enemyHex)));
			return nearest;
		};

		// In pinned fheroes2 archerDecision, ranged enemies are melee
		// movement threats only while actually engaged in hand-to-hand combat.
		// VCMI battleCanShoot(false) also covers Forgetfulness, exhausted ammo
		// and other shot restrictions: these must not turn a distant archer
		// into an advancing melee threat for the retreat decision.
		const auto friendlies = battle.battleGetStacks(CBattleInfoEssentials::ONLY_MINE);
		const auto handFighting = [&](const CStack & enemy)
		{
			for(const auto * friendly : friendlies)
			{
				if(!friendly->alive() || battle.battleGetOwner(friendly) == battle.battleGetOwner(&enemy))
					continue;
				for(const auto & friendlyHex : friendly->getHexes())
					for(const auto & enemyHex : enemy.getHexes())
						if(BattleHex::getDistance(friendlyHex, enemyHex) == 1)
							return true;
			}
			return false;
		};

		const auto isThreatened = [&](const BattleHex & position, const CStack & enemy)
		{
			const int distance = footprintDistance(position, enemy);
			const bool unengagedArcher = enemy.isShooter() && !handFighting(enemy);
			// Pinned fheroes2 Unit::GetSpeed(false, true) reports STANDING
			// for Blind and Paralyze even with nonzero base speed. Native
			// NOT_ACTIVE is the current can't-act projection: contact still
			// blocks a shot, but the unit cannot chase next turn.
			const bool disabled = enemy.hasBonusOfType(BonusType::NOT_ACTIVE);
			const int reach = unengagedArcher || disabled ? 1
				: static_cast<int>(enemy.getMovementRange()) + 1;
			return distance <= reach;
		};

		bool threatened = false;
		for(const auto * enemy : enemies)
		{
			if(!isThreatened(stack.getPosition(), *enemy))
				continue;
			threatened = true;
			// The upstream next-turn speed check excludes Blind/Paralyze.
			// Do not let VCMI's raw movement range veto a legal retreat.
			const int enemySpeed = enemy->hasBonusOfType(BonusType::NOT_ACTIVE)
				? 0 : static_cast<int>(enemy->getMovementRange());
			if(enemySpeed > 0 && enemySpeed + 2 >= speed)
				return std::nullopt;
		}
		if(!threatened)
			return std::nullopt;

		const auto reachability = battle.getReachability(&stack);
		const auto available = battle.battleGetAvailableHexes(reachability, &stack, false);
		BattleHex safest;
		int safestDistance = -1;
		int smallestCenterDistance = GameConstants::BFIELD_SIZE;
		const BattleHex center(GameConstants::BFIELD_SIZE / 2);
		for(const auto & hex : available)
		{
			if(!hex.isAvailable() || hex == stack.getPosition())
				continue;
			int nearest = GameConstants::BFIELD_SIZE;
			bool safe = true;
			for(const auto * enemy : enemies)
			{
				if(isThreatened(hex, *enemy))
				{
					safe = false;
					break;
				}
				nearest = std::min(nearest, footprintDistance(hex, *enemy));
			}
			if(!safe)
				continue;
			const int centerDistance = BattleHex::getDistance(hex, center);
			if(nearest > safestDistance || (nearest == safestDistance
				&& (centerDistance < smallestCenterDistance || (centerDistance == smallestCenterDistance
					&& (!safest.isValid() || hex.toInt() < safest.toInt())))))
			{
				safest = hex;
				safestDistance = nearest;
				smallestCenterDistance = centerDistance;
			}
		}
		return safest.isValid() ? std::optional<BattleAction>(BattleAction::makeMove(&stack, safest))
			: std::nullopt;
	}

	// Pinned fheroes2 archerDecision: if escape failed and melee has blocked
	// the shot, choose among *adjacent* enemies by raw hit-point damage minus
	// expected retaliation. The ordinary melee evaluator instead compares
	// combat-value losses, which is a different decision rule. Native VCMI
	// damage estimates are used because fheroes2's combat simulator differs.
	static std::optional<BattleAction> blockedArcherMelee(CPlayerBattleCallback & battle,
		const CStack & stack, const std::vector<const CStack *> & enemies)
	{
		// Both occupied hexes of a wide archer participate in native VCMI
		// melee-contact checks; fheroes2 does not exclude wide shooters here.
		// Unlike the still single-width retreat projection, this branch
		// evaluates VCMI-legal contact from the stack's actual position.
		if(!stack.hasBonusOfType(BonusType::SHOOTER)
			|| battle.battleCanShoot(&stack) || !stack.getPosition().isValid())
			return std::nullopt;

		const CStack * bestTarget = nullptr;
		BattleHex bestTargetHex;
		double bestDamageDifference = -std::numeric_limits<double>::infinity();
		for(const auto * enemy : enemies)
		{
			if(!enemy->getPosition().isValid())
				continue;
			// fheroes2 measures contact between whole occupied positions. A wide
			// enemy's tail can be adjacent even when its head is two hexes away.
			// Trust VCMI's actual melee-contact check instead of head distance.
			const auto attackHexes = battle.meleeAttackHexes(&stack, enemy, stack.getPosition());
			if(attackHexes.empty())
				continue;
			BattleAttackInfo attack(&stack, enemy, 0, false);
			attack.attackerPos = stack.getPosition();
			DamageEstimation retaliation;
			const auto damage = battle.battleEstimateDamage(attack, &retaliation);
			const double outgoing = (static_cast<double>(damage.damage.min) + damage.damage.max) / 2.0;
			const double incoming = (static_cast<double>(retaliation.damage.min) + retaliation.damage.max) / 2.0;
			const double difference = outgoing - incoming;
			if(!bestTarget || difference > bestDamageDifference)
			{
				bestTarget = enemy;
				bestTargetHex = attackHexes.front();
				bestDamageDifference = difference;
			}
		}
		return bestTarget
			? std::optional<BattleAction>(BattleAction::makeMeleeAttack(&stack, bestTargetHex, stack.getPosition()))
			: std::nullopt;
	}


	// fheroes2 archerDecision values the complete AREA_SHOT footprint.
	// VCMI returns additional targets only: ordinary ranged attacks have
	// no splash, and Magog SHOOTS_ALL_ADJACENT attacks hostiles only.
	// Include the primary target explicitly, without inventing friendly hits
	// or modifying VCMI combat rules. Values are VCMI damage projections.
	struct RangedAreaValue
	{
		bool dangerous = false;
		double priority = 0;
	};

	static RangedAreaValue rangedAreaValue(CPlayerBattleCallback & battle,
		const CStack & shooter, const CStack & target, const BattleHex & aimedHex)
	{
		const auto additionalVictims = battle.getAttackedBattleUnits(
			&shooter, &target, aimedHex, true);
		double friendlyDamage = 0;
		double enemyDamage = 0;
		double priority = 0;
		const auto evaluate = [&](const battle::Unit & victim)
		{
			BattleAttackInfo info(&shooter, &victim, 0, true);
			info.attackerPos = shooter.getPosition();
			const auto estimate = battle.battleEstimateDamage(info);
			const double health = std::max(0.0, static_cast<double>(victim.getAvailableHealth()));
			const double hitPoints = std::min(health, std::max(0.0,
				(static_cast<double>(estimate.damage.min) + estimate.damage.max) / 2.0));
			const double value = lossValue(victim, estimate);
			if(battle.battleGetOwner(&victim) == battle.battleGetOwner(&shooter))
			{
				friendlyDamage += hitPoints;
				priority -= value;
			}
			else
			{
				enemyDamage += hitPoints;
				priority += value;
			}
		};
		// Pinned fheroes2 archerDecision first collects AREA_SHOT victims in a
		// set of head-cell indexes. A wide unit may occupy several affected
		// hexes, but must contribute its damage and friendly-fire risk ONCE.
		// The sorted head indexes also make the floating-point sum stable.
		// Keep VCMI's native victim list and damage estimates; only the
		// source's unique-target accounting is projected here.
		std::map<int, const battle::Unit *> affected;
		affected.emplace(target.getPosition().toInt(), &target);
		for(const auto * victim : additionalVictims)
		{
			if(victim && victim->alive())
				affected.emplace(victim->getPosition().toInt(), victim);
		}
		for(const auto & [head, victim] : affected)
			evaluate(*victim);
		return {Ports::FHeroes2::catastrophicFriendlyFire(friendlyDamage, enemyDamage), priority};
	}

	// Pinned fheroes2 berserkTurn does not evaluate the strategic target
	// list: a Berserk-controlled creature attacks the nearest accessible
	// unit, including an ally, or approaches that unit on a future turn.
	// Native VCMI action legality is always checked by the authoritative
	// server, and our planner uses only the observed battlefield.
	static BattleAction chooseBerserkAction(CPlayerBattleCallback & battle, const CStack & stack)
	{
		struct Candidate
		{
			const CStack * unit;
			Ports::FHeroes2::BerserkCandidate priority;
		};
		std::vector<Candidate> sorted;
		for(const auto * other : battle.battleGetAllStacks())
		{
			if(other == &stack || !other->alive() || other->isInvincible()
				|| !other->getPosition().isValid())
				continue;
			int nearest = GameConstants::BFIELD_SIZE;
			for(const auto & a : stack.getHexes())
				for(const auto & b : other->getHexes())
					nearest = std::min(nearest, static_cast<int>(BattleHex::getDistance(a, b)));
			sorted.push_back({other, {static_cast<int>(other->unitId()), nearest}});
		}
		std::sort(sorted.begin(), sorted.end(), [](const Candidate & a, const Candidate & b)
		{
			return Ports::FHeroes2::preferBerserkCandidate(a.priority, b.priority);
		});
		if(sorted.empty())
			return BattleAction::makeDefend(&stack);

		if(stack.isShooter() && battle.battleCanShoot(&stack))
			for(const auto & candidate : sorted)
				if(battle.battleCanShoot(&stack, candidate.unit->getPosition()))
					return BattleAction::makeShotAttack(&stack, candidate.unit);

		const auto routes = battle.getReachability(&stack);
		auto available = battle.battleGetAvailableHexes(routes, &stack, false);
		available.sort([](const BattleHex & a, const BattleHex & b) { return a.toInt() < b.toInt(); });
		for(const auto & candidate : sorted)
		{
			int shortest = GameConstants::BFIELD_SIZE;
			BattleHex from;
			BattleHex targetHex;
			for(const auto & hex : available)
			{
				if(!hex.isAvailable())
					continue;
				const auto attacked = battle.meleeAttackHexes(&stack, candidate.unit, hex);
				const int cost = static_cast<int>(routes.distances[hex.toInt()]);
				if(!attacked.empty() && cost < shortest)
				{
					shortest = cost;
					from = hex;
					targetHex = attacked.front();
				}
			}
			if(from.isValid() && targetHex.isValid())
				return BattleAction::makeMeleeAttack(&stack, targetHex, from);
		}

		const auto * nearestUnit = sorted.front().unit;
		int startingDistance = GameConstants::BFIELD_SIZE;
		for(const auto & a : stack.getHexes())
			for(const auto & b : nearestUnit->getHexes())
				startingDistance = std::min(startingDistance, static_cast<int>(BattleHex::getDistance(a, b)));
		BattleHex best;
		int bestDistance = startingDistance;
		int bestCost = GameConstants::BFIELD_SIZE;
		for(const auto & hex : available)
		{
			if(!hex.isAvailable() || hex == stack.getPosition())
				continue;
			int remaining = GameConstants::BFIELD_SIZE;
			for(const auto & a : stack.getHexes(hex))
				for(const auto & b : nearestUnit->getHexes())
					remaining = std::min(remaining, static_cast<int>(BattleHex::getDistance(a, b)));
			const int cost = static_cast<int>(routes.distances[hex.toInt()]);
			if(remaining < bestDistance || (remaining == bestDistance && cost < bestCost))
			{
				best = hex;
				bestDistance = remaining;
				bestCost = cost;
			}
		}
		return best.isValid() ? BattleAction::makeMove(&stack, best)
			: BattleAction::makeDefend(&stack);
	}

	static BattleAction chooseAction(CPlayerBattleCallback & battle, const CStack & stack)
	{
		// Berserk is a special creature turn, preceding normal tactical AI.
		if(stack.hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE))
			return chooseBerserkAction(battle, stack);
		if(stack.isCatapult())
			return battle.getAttackableWallParts().empty() ? BattleAction::makeDefend(&stack) : BattleAction::makeCatapultShot(&stack);
		if(stack.isFirstAidTent())
			return BattleAction::makeHeal(&stack);

		// Pinned fheroes2 analyzeBattleState and archerDecision examine the
		// *whole living enemy force*, regardless of whether this particular
		// creature can target every enemy. Source force evaluation and
		// source attack-target eligibility are independent decisions.
		auto allEnemies = battle.battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY);
		vstd::erase_if(allEnemies, [](const CStack * enemy)
		{
			return !enemy->alive() || enemy->isInvincible();
		});
		std::sort(allEnemies.begin(), allEnemies.end(),
			[](const CStack * left, const CStack * right) { return left->unitId() < right->unitId(); });
		// Keep a separate VCMI targetable set. battleCanAttackUnit checks
		// melee capability, NOT current-turn reachability; only the native
		// route/attack queries and the server prove a chosen action legal.
		auto enemies = allEnemies;
		vstd::erase_if(enemies, [&](const CStack * enemy)
		{
			return !battle.battleCanShoot(&stack, enemy->getPosition())
				&& !battle.battleCanAttackUnit(&stack, enemy);
		});
		// Source archerDecision assesses retreat before testing whether a
		// legal shot or melee target exists. A blocked shooter must still
		// consider the movement threats of otherwise inaccessible enemies.
		if(const auto retreat = archerRetreat(battle, stack, allEnemies))
			return *retreat;
		if(enemies.empty())
		{
			// A unit with no targetable enemy may still be able to approach
			// the living garrison. Use native WALK, never invent an attack.
			if(stack.unitSide() == BattleSide::ATTACKER && battle.battleGetDefendedTown() != nullptr)
			{
				const auto defenders = battle.battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY);
				const auto routes = battle.getReachability(&stack);
				const auto steps = battle.battleGetAvailableHexes(routes, &stack, false);
				if(const auto wall = approachSiegeWall(battle, stack, steps, defenders))
					return *wall;
			}
			return BattleAction::makeDefend(&stack);
		}
		if(const auto melee = blockedArcherMelee(battle, stack, enemies))
			return *melee;

		const CStack * shootingTarget = nullptr;
		BattleHex shootingHex;
		double bestShot = std::numeric_limits<double>::lowest();
		bool hasLegalShot = false;
		for(const auto * enemy : enemies)
		{
			// Pinned fheroes2 archerDecision evaluates both head and tail
			// footprints of a wide area-shot target. Ordinary ranged shots
			// retain unit-targeted actions, which have no separate footprint.
			for(const auto & aimedHex : enemy->getHexes())
			{
				if(aimedHex != enemy->getPosition()
					&& !stack.hasBonusOfType(BonusType::SHOOTS_ALL_ADJACENT))
					continue;
				if(!battle.battleCanShoot(&stack, aimedHex))
					continue;
				hasLegalShot = true;
				const auto area = rangedAreaValue(battle, stack, *enemy, aimedHex);
				if(area.dangerous)
					continue;
				if(area.priority > bestShot)
				{
					bestShot = area.priority;
					shootingTarget = enemy;
					shootingHex = aimedHex;
				}
			}
		}
		if(shootingTarget)
		{
			if(shootingHex != shootingTarget->getPosition())
				return BattleAction::makeShotAttack(&stack, shootingHex);
			return BattleAction::makeShotAttack(&stack, shootingTarget);
		}
		// Do not replace rejected friendly-fire shots with a different target
		// chosen by the generic, target-only melee scorer.
		if(hasLegalShot)
			return BattleAction::makeDefend(&stack);
		if(!stack.getPosition().isValid())
			return BattleAction::makeDefend(&stack);

		const auto reachability = battle.getReachability(&stack);
		auto available = battle.battleGetAvailableHexes(reachability, &stack, false);
		available.sort([](const BattleHex & left, const BattleHex & right) { return left.toInt() < right.toInt(); });
		const auto stance = tactics(battle, stack, allEnemies);
		if(stance.defensive)
		{
			// Pinned meleeUnitDefense covers a threatened friendly archer
			// *before* considering unrelated melee opportunities. Previously
			// any legal immediate attack bypassed this screen entirely.
			// An archer's blocked shot gives its actual adjacent enemy a
			// higher priority than unrelated melee targets. A legal native
			// attack is required; otherwise retain the generic melee policy.
			if(const auto unblock = attackShooterBlocker(battle, stack, reachability, available, allEnemies))
				return *unblock;
			if(const auto pursue = pursueShooterBlocker(battle, stack, reachability, available, allEnemies))
				return *pursue;
			const auto friendlyUnits = battle.battleGetStacks(CBattleInfoEssentials::ONLY_MINE);
			const bool shootersCanFire = std::all_of(friendlyUnits.begin(), friendlyUnits.end(),
				[&](const CStack * friendly)
				{
					return !friendly->alive() || !friendly->isShooter()
						|| battle.battleCanShoot(friendly);
				});
			if(shootersCanFire)
					if(const auto cover = coverThreatenedShooter(battle, stack, reachability, available, allEnemies))
						return *cover;
		}
		const CStack * target = nullptr;
		BattleHex targetHex;
		Ports::FHeroes2::MeleeOutcome best;
		for(const auto * enemy : enemies)
		{
			for(const auto & hex : available)
			{
				if(!hex.isAvailable())
					continue;
				const auto attackedHexes = battle.meleeAttackHexes(&stack, enemy, hex);
				if(attackedHexes.empty())
					continue;
				// An area-melee creature can inflict different secondary hits
				// from different legal attack vectors, even against the same wide
				// primary target. Evaluate every cell the native engine permits.
				for(const auto & aimedHex : attackedHexes)
				{
					const double value = meleeAreaAttackValue(battle, stack, *enemy,
						hex, aimedHex, reachability.distances[hex.toInt()]);
					const Ports::FHeroes2::MeleeOutcome candidate{hex.toInt(), strength(*enemy), value, true};
					if(!target || Ports::FHeroes2::isOutcomeImproved(candidate, best))
					{
						target = enemy;
						targetHex = aimedHex;
						best = candidate;
					}
				}
			}
		}
		if(target)
		{
			const BattleHex from(best.fromIndex);
			const auto * occupant = battle.battleGetUnitByPos(targetHex);
			if(!available.contains(from) || occupant != target)
				throw std::logic_error("FHeroes2Tactics invalid melee candidate: unit=" + std::to_string(stack.unitId())
					+ " target=" + std::to_string(target->unitId()) + " targetPosition=" + std::to_string(target->getPosition().toInt())
					+ " selectedHex=" + std::to_string(targetHex.toInt()) + " from=" + std::to_string(from.toInt())
					+ " reachable=" + std::to_string(available.contains(from))
					+ " occupant=" + (occupant ? std::to_string(occupant->unitId()) : "none"));
			return BattleAction::makeMeleeAttack(&stack, targetHex, from);
		}
		if(stance.defensive)
		{
			if(const auto cover = coverThreatenedShooter(battle, stack, reachability, available, allEnemies))
				return *cover;
			return BattleAction::makeDefend(&stack);
		}
		return approach(battle, stack, reachability, available, enemies,
			stance.cautiousOffensive);
	}
};
}
