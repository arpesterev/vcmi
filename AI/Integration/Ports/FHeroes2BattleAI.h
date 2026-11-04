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
				switch(Ports::FHeroes2::decideWithdrawal(context))
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
			if(const auto spell = FHeroes2SpellPolicy::choose(battle, env.get()))
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
	}

	void battleEnd(const BattleID & battleID, const BattleResult *, QuestionID) override
	{
		stalemateByBattle.erase(battleID.getNum());
	}

private:
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

			const int enemySpeed = static_cast<int>(enemy->getMovementRange());
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
			if(friendly != &stack && friendly->alive() && friendly->getPosition().isValid()
				&& friendly->hasBonusOfType(BonusType::SHOOTER))
				totalShooterStrength += strength(*friendly);
		}
		if(totalShooterStrength <= 0)
			return std::nullopt;

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
				return nearest <= static_cast<int>(enemy->getMovementRange()) + 1;
			});
			if(!threatened)
				continue;
			// A guard already adjacent to one threatened shooter must not
			// immediately defend without considering the other shooters.
			// Treat its current screen as a zero-travel candidate, then
			// compare every reachable cover using the same source-derived
			// archer-strength / travel-penalty ranking.
			if(distanceToShooter(stack.getPosition(), *friendly) == 1)
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
					|| distanceToShooter(hex, *friendly) != 1)
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
			const int reach = unengagedArcher ? 1 : static_cast<int>(enemy.getMovementRange()) + 1;
			return distance <= reach;
		};

		bool threatened = false;
		for(const auto * enemy : enemies)
		{
			if(!isThreatened(stack.getPosition(), *enemy))
				continue;
			threatened = true;
			const int enemySpeed = static_cast<int>(enemy->getMovementRange());
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
		evaluate(target);
		for(const auto * victim : additionalVictims)
			if(victim != &target)
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
				const double value = attackValue(battle, stack, *enemy, hex, false, reachability.distances[hex.toInt()]);
				const Ports::FHeroes2::MeleeOutcome candidate{hex.toInt(), strength(*enemy), value, true};
				if(!target || Ports::FHeroes2::isOutcomeImproved(candidate, best))
				{
					target = enemy;
					targetHex = attackedHexes.front();
					best = candidate;
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
		const auto stance = tactics(battle, stack, allEnemies);
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
