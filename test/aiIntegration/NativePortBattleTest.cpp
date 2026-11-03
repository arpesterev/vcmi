/*
 * NativePortBattleTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later.
 */
#include "StdInc.h"
#include <cstdlib>

#include "server/battles/BattleTestFixture.h"
#include "../../AI/Integration/Ports/HoMM3TacticsBattleAI.h"
#include "../../AI/Integration/Ports/FHeroes2SpellPolicy.h"
#ifdef ENABLE_MMAI
#include "../../AI/MMAI/BAI/model_api.h"
#include "../../AI/MMAI/BAI/router.h"
#endif
#include <vcmi/Environment.h>
#include "../../lib/GameLibrary.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/json/JsonUtils.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/battle/BattleLayout.h"
#include "../../lib/callback/AIFactory.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/CBattleGameInterface.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/entities/building/TownFortifications.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/StackLocation.h"
#include "../../server/CGameHandler.h"
#include "../../server/battles/BattleProcessor.h"

namespace
{
class DecisionCallback final : public CBattleCallback
{
public:
	explicit DecisionCallback(PlayerColor player) : CBattleCallback(player, nullptr) {}
	std::optional<BattleAction> action;
	bool spellAction = false;

	// Headless native fixture has no IClient. Returning no surrender/retreat
	// decision preserves the engine's legal combat path instead of calling
	// CBattleCallback's UI-dependent default through a null client pointer.
	std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID &,
		const BattleStateInfoForRetreat &) override
	{
		return std::nullopt;
	}

	void battleMakeUnitAction(const BattleID &, const BattleAction & value) override
	{
		if(action)
			throw std::logic_error("Backend submitted two commands for one activation");
		spellAction = false;
		action = value;
	}
	void battleMakeTacticAction(const BattleID &, const BattleAction & value) override { action = value; }
	void battleMakeSpellAction(const BattleID &, const BattleAction & value) override
	{
		if(action)
			throw std::logic_error("Backend submitted a unit command before spell acknowledgement");
		spellAction = true;
		action = value;
	}
};

class NativePortEnvironment final : public Environment
{
	std::shared_ptr<CCallback> gameCallback;
	std::shared_ptr<DecisionCallback> battleCallback;
public:
	NativePortEnvironment(std::shared_ptr<CCallback> game, std::shared_ptr<DecisionCallback> battle)
		: gameCallback(std::move(game)), battleCallback(std::move(battle)) {}
	const Services * services() const override { return LIBRARY; }
	const GameCb * game() const override { return gameCallback.get(); }
	const BattleCb * battle(const BattleID & battleID) const override { return battleCallback->getBattle(battleID).get(); }
};

class NativeBattleRecorder final : public RecordingGameServer
{
public:
	int64_t damage = 0;
	int shots = 0;
	int meleeAttacks = 0;
	int acceptedResults = 0;
	int injuryPackets = 0;
	int spellPackets = 0;
	int movementPackets = 0;
	int roundPackets = 0;
	std::function<void(const BattleResult &)> onResultBeforeApply;
	std::function<void(const BattleStackMoved &)> onMovementBeforeApply;
	std::function<void(const BattleNextRound &)> onRoundBeforeApply;
	std::function<void(const BattleNextRound &)> onRoundAfterApply;
	std::function<void(const BattleAttack &)> onAttackBeforeApply;
	std::function<void(const StacksInjured &)> onInjuriesBeforeApply;
	std::function<void(const BattleSpellCast &)> onSpellAfterApply;

	void applyPack(CPackForClient & pack) override
	{
		// In the live client battleEnd is delivered while the battle state
		// is still valid. The test server removes that state when it applies
		// BattleResult. Deliver before removal, not after the winning action.
		if(const auto * result = dynamic_cast<const BattleResult *>(&pack))
		{
			if(onResultBeforeApply)
				onResultBeforeApply(*result);
		}
		// The client delivers movements and the first round notification before
		// the server packet changes its authoritative battle state.
		if(const auto * movement = dynamic_cast<const BattleStackMoved *>(&pack))
		{
			if(onMovementBeforeApply)
				onMovementBeforeApply(*movement);
		}
		if(const auto * round = dynamic_cast<const BattleNextRound *>(&pack))
		{
			if(onRoundBeforeApply)
				onRoundBeforeApply(*round);
		}
		// Mirror client/NetPacksClient.cpp ApplyFirstClientNetPackVisitor:
		// both attack notifications precede BattleAttack.applyGs(), allowing
		// neural policies to observe the still-live pre-damage state.
		if(const auto * attack = dynamic_cast<const BattleAttack *>(&pack))
		{
			if(onAttackBeforeApply)
				onAttackBeforeApply(*attack);
		}
		// ApplyFirstClientNetPackVisitor::visitStacksInjured delivers the
		// battleStacksAttacked event before applying spell/ability injuries.
		// This is distinct from the BattleAttack event pair above.
		if(const auto * injuries = dynamic_cast<const StacksInjured *>(&pack))
		{
			if(onInjuriesBeforeApply)
				onInjuriesBeforeApply(*injuries);
		}
		RecordingGameServer::applyPack(pack);
		// ApplyClientNetPackVisitor::visitBattleNextRound fires after the
		// round packet is applied, paired with its pre-apply callback above.
		if(const auto * round = dynamic_cast<const BattleNextRound *>(&pack))
		{
			++roundPackets;
			if(onRoundAfterApply)
				onRoundAfterApply(*round);
		}
		if(dynamic_cast<const BattleStackMoved *>(&pack))
			++movementPackets;
		// ApplyClientNetPackVisitor::visitBattleSpellCast notifies both sides
		// *after* the authoritative spell pack is applied, unlike injuries.
		if(const auto * cast = dynamic_cast<const BattleSpellCast *>(&pack))
		{
			++spellPackets;
			if(onSpellAfterApply)
				onSpellAfterApply(*cast);
		}
		if(const auto * attack = dynamic_cast<const BattleAttack *>(&pack))
		{
			if(attack->shot())
				++shots;
			else
				++meleeAttacks;
			for(const auto & injury : attack->bsa)
				damage += injury.damageAmount;
		}
		if(const auto * injuries = dynamic_cast<const StacksInjured *>(&pack))
		{
			++injuryPackets;
			for(const auto & injury : injuries->stacks)
				damage += injury.damageAmount;
		}
		if(dynamic_cast<const BattleResultAccepted *>(&pack))
			++acceptedResults;
	}
};
}

class AIIntegrationNativeBattle : public BattleTestFixture
{
protected:
	NativeBattleRecorder recorder;
	int ballistaDecisions = 0;
	int ballistaShots = 0;
	std::optional<int> firstBallistaShotTarget;
	int ballistaDefendsWhileAllEnemiesBlind = 0;
	int heroSpellDecisions = 0;
	int mmaiUnitDecisions = 0;
	int mmaiNonpassiveDecisions = 0;
	int replayedMovementEvents = 0;
	int replayedRoundEvents = 0;

#ifdef ENABLE_MMAI
	void requireInstalledNeuralRole(const std::string & role) const
	{
		const auto modelConfig = JsonUtils::assembleFromFiles("MMAI/CONFIG/mmai-settings.json");
		ASSERT_TRUE(modelConfig.isStruct()) << "MMAI neural config is not mounted";
		ASSERT_TRUE(modelConfig["models"][role].isString()) << "Missing neural role: " << role;
		const auto path = "MMAI/models/" + modelConfig["models"][role].String();
		ASSERT_TRUE(CResourceHandler::get()->existsResource(ResourcePath(path, EResType::AI_MODEL)))
			<< "MMAI neural model is not mounted: " << path;
	}
#endif

	void startRecordedGame()
	{
		startGame();
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		gameHandler.reset();
		recorder.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(recorder, gameState());
		gameHandler->randomizer->setSeed(seed);
	}

	void setArmy(const CArmedInstance * army, CreatureID type, int count)
	{
		const StackLocation slot(army->id, SlotID(0));
		ASSERT_TRUE(gameHandler->changeStackType(slot, type.toCreature()));
		ASSERT_TRUE(gameHandler->changeStackCount(slot, count, ChangeValueMode::ABSOLUTE));
	}

	void prepareBattle(CreatureID attackerType, int attackerCount, CreatureID defenderType, int defenderCount)
	{
		startRecordedGame();
		ASSERT_FALSE(HasFatalFailure());
		setArmy(attackerSideHero, attackerType, attackerCount);
		setArmy(defenderSideHero, defenderType, defenderCount);
		ASSERT_FALSE(HasFatalFailure());
		gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
		ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	}

	void teachBattleSpell(CGHeroInstance * hero, SpellID spell, int power = 6)
	{
		teachSpell(hero, spell);
		hero->setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
		hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 10, ChangeValueMode::ABSOLUTE);
		hero->mana = 100;
	}

	void prepareSiege(bool includeDefenderShooter = false)
	{
		startRecordedGame();
		ASSERT_FALSE(HasFatalFailure());
		setArmy(attackerSideHero, CreatureID(10), 30);
		ASSERT_FALSE(HasFatalFailure());
		const auto * town = findFirst<CGTownInstance>();
		ASSERT_NE(town, nullptr);
		if(town->fortificationsLevel().wallsHealth <= 0)
		{
			ASSERT_TRUE(gameHandler->buildStructure(town->id, BuildingID::FORT, true));
		}
		ASSERT_GT(town->fortificationsLevel().wallsHealth, 0);
		ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(town->id, SlotID(0)), CreatureID(0).toCreature(), 30));
		if(includeDefenderShooter)
		{
			ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(town->id, SlotID(1)), CreatureID(2).toCreature(), 1));
		}
		ASSERT_NE(attackerSideHero->getArt(ArtifactPosition::MACH4), nullptr);
		const auto layout = BattleLayout::createDefaultLayout(*gameState(), attackerSideHero, town);
		gameHandler->battles->startBattle(attackerSideHero, town, town->visitablePos(),
			attackerSideHero, nullptr, layout, town);
		ASSERT_EQ(gameState()->currentBattles.size(), 1u);
		ASSERT_TRUE(std::any_of(battle()->stacks.begin(), battle()->stacks.end(),
			[](const auto & unit) { return unit->isCatapult(); })) << "Siege setup did not create the attacking catapult";
	}

	void runToCompletion(std::optional<BattleSide> expectedWinner = std::nullopt, bool spellsEnabled = true,
		const std::string & backendName = "FHeroes2Tactics", const std::string & defenderBackend = "",
		bool requireBothSidesAct = false)
	{
		const std::string & attackerAI = backendName;
		const std::string & defenderAI = defenderBackend.empty() ? backendName : defenderBackend;
		// Some fixtures issue real server attacks to construct their starting
		// positions before the adapter subscription is installed.
		const int attacksBeforeEventSubscription = recorder.shots + recorder.meleeAttacks;
		const int injuriesBeforeEventSubscription = recorder.injuryPackets;
		const int spellsBeforeEventSubscription = recorder.spellPackets;
		const int movesBeforeEventSubscription = recorder.movementPackets;
		const int roundsBeforeEventSubscription = recorder.roundPackets;
		const int64_t damageBeforeEventSubscription = recorder.damage;
		ASSERT_EQ(gameState()->currentBattles.size(), 1u);
		const auto battleID = battle()->getBattleID();
		const auto attackerPlayer = battle()->sideToPlayer(BattleSide::ATTACKER);
		const auto defenderPlayer = battle()->sideToPlayer(BattleSide::DEFENDER);
		std::map<PlayerColor, std::shared_ptr<DecisionCallback>> callbacks;
		std::map<PlayerColor, std::shared_ptr<CBattleGameInterface>> policies;
		for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			const auto player = battle()->sideToPlayer(side);
			auto callback = std::make_shared<DecisionCallback>(player);
			callback->onBattleStarted(battle());
			const auto & name = side == BattleSide::ATTACKER ? attackerAI : defenderAI;
			auto policy = AIFactory::createBattleAI(name);
			ASSERT_NE(policy, nullptr);
			EXPECT_EQ(policy->dllName, name);
			policy->playerID = player;
			auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
			AutocombatPreferences prefs;
			prefs.enableSpellsUsage = spellsEnabled;
			policy->initBattleInterface(environment, callback, prefs);
			policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
				battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
				battle()->getSideHero(BattleSide::ATTACKER), battle()->getSideHero(BattleSide::DEFENDER), side, false);
			callbacks.emplace(player, callback);
			policies.emplace(player, policy);
		}

		int deliveredAttackEvents = 0;
		int deliveredInjuryEvents = 0;
		int deliveredSpellEvents = 0;
		int deliveredMovementEvents = 0;
		int deliveredRoundFirstEvents = 0;
		int deliveredRoundEvents = 0;
		// Record damage received by each side from actual engine injury packets.
		// This is deliberately NOT labelled "damage dealt": friendly fire,
		// spell effects and retaliation can make that attribution misleading.
		std::map<BattleSide, int64_t> damageTakenBySide;
		const auto accountActualInjuries = [&](const std::vector<BattleStackAttacked> & injuries)
		{
			for(const auto & injury : injuries)
			{
				const auto * victim = battle()->battleGetStackByID(injury.stackAttacked);
				ASSERT_NE(victim, nullptr) << "Injury packet must identify a live pre-apply victim";
				if(victim)
					damageTakenBySide[victim->unitSide()] += injury.damageAmount;
			}
		};
		recorder.onMovementBeforeApply = [&](const BattleStackMoved & movement)
		{
			const auto * movedStack = battle()->battleGetStackByID(movement.stack);
			ASSERT_NE(movedStack, nullptr) << "Movement callback must see the pre-move live stack";
			for(auto & entry : policies)
				entry.second->battleStackMoved(movement.battleID, movedStack,
					movement.tilesToMove, movement.distance, movement.teleporting);
			++deliveredMovementEvents;
		};
		recorder.onRoundBeforeApply = [&](const BattleNextRound & round)
		{
			for(auto & entry : policies)
				entry.second->battleNewRoundFirst(round.battleID);
			++deliveredRoundFirstEvents;
		};
		recorder.onRoundAfterApply = [&](const BattleNextRound & round)
		{
			for(auto & entry : policies)
				entry.second->battleNewRound(round.battleID);
			++deliveredRoundEvents;
		};
		recorder.onAttackBeforeApply = [&](const BattleAttack & attack)
		{
			accountActualInjuries(attack.bsa);
			for(auto & entry : policies)
			{
				entry.second->battleAttack(attack.battleID, &attack);
				entry.second->battleStacksAttacked(attack.battleID, attack.bsa, attack.shot());
			}
			++deliveredAttackEvents;
		};
		recorder.onInjuriesBeforeApply = [&](const StacksInjured & injuries)
		{
			accountActualInjuries(injuries.stacks);
			for(auto & entry : policies)
				entry.second->battleStacksAttacked(injuries.battleID, injuries.stacks, false);
			++deliveredInjuryEvents;
		};
		recorder.onSpellAfterApply = [&](const BattleSpellCast & cast)
		{
			for(auto & entry : policies)
				entry.second->battleSpellCast(cast.battleID, &cast);
			++deliveredSpellEvents;
		};

		bool battleEndDelivered = false;
		recorder.onResultBeforeApply = [&](const BattleResult & result)
		{
			ASSERT_FALSE(gameState()->currentBattles.empty()) << "Battle was released before its terminal event";
			ASSERT_FALSE(battleEndDelivered) << "A battle produced multiple terminal events";
			for(auto & entry : policies)
				entry.second->battleEnd(battleID, &result, result.questionID);
			battleEndDelivered = true;
		};

		if(battle()->tacticDistance > 0)
		{
			const auto player = battle()->sideToPlayer(battle()->tacticsSide);
			auto & callback = callbacks.at(player);
			policies.at(player)->yourTacticPhase(battleID, battle()->tacticDistance);
			ASSERT_TRUE(callback->action.has_value());
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action));
		}

		int decisions = 0;
		std::map<PlayerColor, int> decisionsByPlayer;
		std::map<PlayerColor, std::map<EActionType, int>> acceptedActionTypesByPlayer;
		std::vector<std::string> recentDecisions;
		while(recorder.battleResults.empty() && decisions < 512)
		{
			ASSERT_FALSE(gameState()->currentBattles.empty());
			const auto * active = dynamic_cast<const CStack *>(battle()->battleActiveUnit());
			ASSERT_NE(active, nullptr) << "Battle stopped without a result or an active stack";
			const auto player = battle()->battleGetOwner(active);
			auto & callback = callbacks.at(player);
			callback->action.reset();
			callback->spellAction = false;
			policies.at(player)->activeStack(battleID, active);
			ASSERT_TRUE(callback->action.has_value()) << "Backend did not respond to the actual active-stack event";
			const auto action = *callback->action;
			// A completed battle alone does not prove the neural side made
			// meaningful decisions; the scripted opponent could win unaided.
			const auto & activePolicy = player == attackerPlayer ? attackerAI : defenderAI;
			if(activePolicy == "MMAI" && !callback->spellAction)
			{
				++mmaiUnitDecisions;
				if(action.actionType != EActionType::DEFEND && action.actionType != EActionType::WAIT)
					++mmaiNonpassiveDecisions;
			}
			if(callback->spellAction)
				++heroSpellDecisions;
			else if(active->creatureId() == CreatureID::BALLISTA)
			{
				++ballistaDecisions;
				if(action.actionType == EActionType::SHOOT)
				{
					++ballistaShots;
					if(!firstBallistaShotTarget && !action.target.empty())
						firstBallistaShotTarget = action.target.front().unitValue;
				}
				if(action.actionType == EActionType::DEFEND)
				{
					bool sawEnemy = false;
					bool allEnemiesBlind = true;
					for(const auto & unit : battle()->stacks)
					{
						if(unit->alive() && unit->unitSide() != active->unitSide())
						{
							sawEnemy = true;
							allEnemiesBlind &= unit->hasBonus(Selector::source(
								BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLIND))));
						}
					}
					if(sawEnemy && allEnemiesBlind)
						++ballistaDefendsWhileAllEnemiesBlind;
				}
			}
			std::ostringstream trace;
			trace << "activation=" << decisions << " active=" << active->unitId() << " " << action.toString();
			for(const auto & unit : battle()->stacks)
			{
				if(unit->alive())
					trace << " [unit=" << unit->unitId() << " creature=" << unit->creatureIndex()
						<< " side=" << static_cast<int>(unit->unitSide()) << " hex=" << unit->getPosition().toInt()
						<< " hp=" << unit->getAvailableHealth() << "]";
			}
			recentDecisions.push_back(trace.str());
			if(recentDecisions.size() > 20)
				recentDecisions.erase(recentDecisions.begin());
			for(auto & entry : policies)
				entry.second->actionStarted(battleID, action);
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action)) << "The engine rejected the selected action";
			++decisions;
			++decisionsByPlayer[player];
			++acceptedActionTypesByPlayer[player][action.actionType];
			if(recorder.battleResults.empty())
			{
				for(auto & entry : policies)
					entry.second->actionFinished(battleID, action);
			}
		}
		std::ostringstream diagnostics;
		diagnostics << "damage=" << recorder.damage << " shots=" << recorder.shots
			<< " melee=" << recorder.meleeAttacks << " catapult=" << recorder.catapultAttacks.size()
			<< " spells=" << heroSpellDecisions;
		for(const auto & decision : recentDecisions)
			diagnostics << '\n' << decision;
		ASSERT_EQ(recorder.battleResults.size(), 1u) << "No unique result after 512 actual activations\n" << diagnostics.str();
		const auto result = recorder.battleResults.back();
		EXPECT_GT(decisions, 0);
		if(attackerAI != defenderAI || requireBothSidesAct)
		{
			// A completed mixed-policy battle is not evidence for both ports if
			// one army dies before its backend is ever asked for a decision.
			EXPECT_GT(decisionsByPlayer[attackerPlayer], 0) << "Mixed-policy attacker never acted";
			EXPECT_GT(decisionsByPlayer[defenderPlayer], 0) << "Mixed-policy defender never acted";
		}
		EXPECT_GT(recorder.damage, 0) << "No damage was applied by the engine";
		EXPECT_EQ(damageTakenBySide[BattleSide::ATTACKER] + damageTakenBySide[BattleSide::DEFENDER],
			recorder.damage - damageBeforeEventSubscription)
			<< "Per-side pre-apply injury evidence must reconcile with engine-applied damage";
		EXPECT_EQ(deliveredAttackEvents,
			recorder.shots + recorder.meleeAttacks - attacksBeforeEventSubscription)
			<< "Every attack after policy subscription must traverse both source-policy event adapters";
		EXPECT_EQ(deliveredInjuryEvents, recorder.injuryPackets - injuriesBeforeEventSubscription)
			<< "Every spell or ability injury must notify both policies before native damage application";
		EXPECT_EQ(deliveredSpellEvents, recorder.spellPackets - spellsBeforeEventSubscription)
			<< "Every spell cast must notify both policies after its pack is applied";
		EXPECT_EQ(deliveredMovementEvents, recorder.movementPackets - movesBeforeEventSubscription)
			<< "Every movement must notify both policies with pre-move stack state";
		EXPECT_EQ(deliveredRoundFirstEvents, recorder.roundPackets - roundsBeforeEventSubscription)
			<< "Every round must notify both policies before applying the new round";
		EXPECT_EQ(deliveredRoundEvents, recorder.roundPackets - roundsBeforeEventSubscription)
			<< "Every round must notify both policies after applying the new round";
		replayedMovementEvents = deliveredMovementEvents;
		replayedRoundEvents = deliveredRoundEvents;
		EXPECT_TRUE(result.winner == BattleSide::ATTACKER || result.winner == BattleSide::DEFENDER);
		if(expectedWinner)
		{
			EXPECT_EQ(result.winner, *expectedWinner);
		}
		ASSERT_TRUE(battleEndDelivered) << "The terminal event was not delivered with live battle state";
		recorder.onResultBeforeApply = {};
		recorder.onAttackBeforeApply = {};
		recorder.onInjuriesBeforeApply = {};
		recorder.onSpellAfterApply = {};
		recorder.onMovementBeforeApply = {};
		recorder.onRoundBeforeApply = {};
		recorder.onRoundAfterApply = {};
		if(result.questionID != QuestionID::NONE)
		{
			ASSERT_TRUE(gameHandler->answerQuestion(result.questionID, 0, attackerPlayer));
		}
		EXPECT_TRUE(gameState()->currentBattles.empty()) << "The battle result was not applied to the adventure state";
		EXPECT_EQ(recorder.acceptedResults, 1);
		// Compact per-side action-type frequencies allow actual policy behavior
		// to be compared, not just wins and total activations. Counts include
		// only actions accepted by the engine, not attempted commands.
		const auto actionCounts = [&](PlayerColor player)
		{
			std::ostringstream out;
			bool needsSeparator = false;
			for(const auto & entry : acceptedActionTypesByPlayer[player])
			{
				if(needsSeparator)
					out << ',';
				out << static_cast<int>(entry.first) << ':' << entry.second;
				needsSeparator = true;
			}
			return out.str();
		};
		std::cout << "AI_NATIVE_BATTLE_RESULT backend=" << backendName
			<< " defender_backend=" << defenderAI
			<< " attacker_decisions=" << decisionsByPlayer[attackerPlayer]
			<< " defender_decisions=" << decisionsByPlayer[defenderPlayer]
			<< " activations=" << decisions << " damage=" << recorder.damage
			<< " attacker_damage_taken=" << damageTakenBySide[BattleSide::ATTACKER]
			<< " defender_damage_taken=" << damageTakenBySide[BattleSide::DEFENDER]
			<< " native_attack_events=" << deliveredAttackEvents
			<< " native_injury_events=" << deliveredInjuryEvents
			<< " native_spell_events=" << deliveredSpellEvents
			<< " native_movement_events=" << deliveredMovementEvents
			<< " native_round_events=" << deliveredRoundEvents
			<< " ranged_attacks=" << recorder.shots << " melee_attacks=" << recorder.meleeAttacks
			<< " spells=" << heroSpellDecisions
			<< " attacker_action_types=" << actionCounts(attackerPlayer)
			<< " defender_action_types=" << actionCounts(defenderPlayer)
			<< " winner=" << (result.winner == BattleSide::ATTACKER ? "attacker" : "defender")
			<< '\n';
		for(auto & entry : callbacks)
			entry.second->onBattleEnded(battleID);
	}
};

TEST_F(AIIntegrationNativeBattle, completesMeleeBattleThroughFactory)
{
	prepareBattle(CreatureID(0), 30, CreatureID(0), 20);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion();
	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_GT(replayedMovementEvents, 0) << "Melee fixture must exercise pre-apply movement callbacks";
	EXPECT_GT(replayedRoundEvents, 0) << "Melee fixture must exercise both phases of round callbacks";
}

TEST_F(AIIntegrationNativeBattle, completesRangedBattleThroughFactory)
{
	prepareBattle(CreatureID(2), 20, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion();
	EXPECT_GT(recorder.shots, 0);
}

TEST_F(AIIntegrationNativeBattle, appliesDefenderVictoryToAdventureState)
{
	prepareBattle(CreatureID(0), 2, CreatureID(0), 100);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(BattleSide::DEFENDER);
}

TEST_F(AIIntegrationNativeBattle, countsDamageFromTheLethalFirstAttack)
{
	prepareBattle(CreatureID(2), 100, CreatureID(0), 1);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(BattleSide::ATTACKER);
	EXPECT_EQ(recorder.shots, 1);
}

TEST_F(AIIntegrationNativeBattle, completesDoubleWideGroundBattle)
{
	prepareBattle(CreatureID(10), 12, CreatureID(0), 40);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion();
}

TEST_F(AIIntegrationNativeBattle, completesFlyingBattle)
{
	prepareBattle(CreatureID(4), 20, CreatureID(2), 30);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion();
}

TEST_F(AIIntegrationNativeBattle, completesRangedDuelWithoutDefenceDeadlock)
{
	prepareBattle(CreatureID(2), 30, CreatureID(2), 30);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion();
	EXPECT_GT(recorder.shots, 0);
}

TEST_F(AIIntegrationNativeBattle, completesFortifiedTownBattle)
{
	prepareSiege();
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(BattleSide::ATTACKER);
	EXPECT_FALSE(recorder.catapultAttacks.empty());
}

TEST_F(AIIntegrationNativeBattle, fheroes2CastleGarrisonHoldsItsMeleeLineForShooters)
{
	// The original fheroes2 analyzeBattleState treats a defended castle as
	// valuable shooting cover. The ratio here is deliberately tiny so the
	// ranged-advantage branch alone would NOT justify defending.
	prepareSiege(true);
	ASSERT_FALSE(HasFatalFailure());
	const CStack * infantry = nullptr;
	int shooters = 0;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			infantry = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(2))
			++shooters;
	}
	ASSERT_NE(infantry, nullptr);
	ASSERT_EQ(shooters, 1);
	ASSERT_NE(battle()->battleGetDefendedTown(), nullptr);
	ASSERT_NE(battle()->battleGetWallState(EWallPart::GATE), EWallState::NONE);
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::DEFENDER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::DEFENDER, false);
	policy->activeStack(battleID, infantry);
	ASSERT_TRUE(callback->action.has_value());
	EXPECT_EQ(callback->action->actionType, EActionType::DEFEND)
		<< "Defender must use intact fortifications as cover for its shooter";
	runToCompletion(std::nullopt, false);
}

TEST_F(AIIntegrationNativeBattle, controlsBallistaAndCompletesBattle)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 40);
	setArmy(defenderSideHero, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	giveArtifact(attackerSideHero, ArtifactID(ArtifactID::BALLISTA), ArtifactPosition::MACH1);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::ARTILLERY), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	runToCompletion();
	EXPECT_GT(ballistaDecisions, 0);
	EXPECT_GT(ballistaShots, 0) << "Automatic server fire cannot substitute for an AI ballista decision";
}

TEST_F(AIIntegrationNativeBattle, completePortNameCannotSilentlySelectThePartialPolicy)
{
	EXPECT_THROW(AIFactory::createBattleAI("FHeroes2"), std::runtime_error);
	EXPECT_THROW(AIFactory::createBattleAI("HoMM3Original"), std::runtime_error);
	EXPECT_THROW(AIFactory::createBattleAI("TUMEnchanted"), std::runtime_error);
}

TEST_F(AIIntegrationNativeBattle, choosesOffensiveMagicAndCompletesTheBattle)
{
	prepareBattle(CreatureID(0), 30, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::MAGIC_ARROW);
	runToCompletion(BattleSide::ATTACKER);
	EXPECT_GT(heroSpellDecisions, 0);
	EXPECT_FALSE(recorder.castsOf(SpellID::MAGIC_ARROW).empty());
	EXPECT_GT(recorder.spellPackets, 0) << "Native spell packs must reach both AI event adapters";
}

TEST_F(AIIntegrationNativeBattle, disabledMagicKeepsAutocombatPreferences)
{
	prepareBattle(CreatureID(0), 30, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::MAGIC_ARROW);
	runToCompletion(std::nullopt, false);
	EXPECT_EQ(heroSpellDecisions, 0);
	EXPECT_TRUE(recorder.casts.empty());
}

TEST_F(AIIntegrationNativeBattle, lethalSpellFinishesBeforeAnyStaleUnitCommand)
{
	prepareBattle(CreatureID(2), 1, CreatureID(0), 1);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::MAGIC_ARROW, 20);
	runToCompletion(BattleSide::ATTACKER);
	EXPECT_EQ(heroSpellDecisions, 1);
	EXPECT_EQ(recorder.castsOf(SpellID::MAGIC_ARROW).size(), 1u);
	EXPECT_EQ(recorder.shots, 0);
	EXPECT_EQ(recorder.meleeAttacks, 0);
	EXPECT_GT(recorder.injuryPackets, 0) << "A lethal damage spell must emit native StacksInjured events";
	EXPECT_GT(recorder.spellPackets, 0) << "A lethal spell must also emit the native cast notification";
}

TEST_F(AIIntegrationNativeBattle, noManaStillCompletesUsingUnitActions)
{
	prepareBattle(CreatureID(2), 20, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::MAGIC_ARROW);
	attackerSideHero->mana = 0;
	runToCompletion();
	EXPECT_EQ(heroSpellDecisions, 0);
	EXPECT_GT(recorder.shots, 0);
}

TEST_F(AIIntegrationNativeBattle, choosesAreaDamageOnLegalBattlefieldTargets)
{
	prepareBattle(CreatureID(2), 20, CreatureID(0), 50);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::FIREBALL);
	runToCompletion();
	EXPECT_FALSE(recorder.castsOf(SpellID::FIREBALL).empty());
}

TEST_F(AIIntegrationNativeBattle, rejectsGlobalDamageThatWouldDestroyItsOwnArmy)
{
	prepareBattle(CreatureID(2), 1, CreatureID(0), 1);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::ARMAGEDDON, 20);
	runToCompletion();
	EXPECT_TRUE(recorder.castsOf(SpellID::ARMAGEDDON).empty());
	EXPECT_EQ(heroSpellDecisions, 0);
}

TEST_F(AIIntegrationNativeBattle, bothSidesCastWithoutRepeatedCommandsInTheSameRound)
{
	prepareBattle(CreatureID(0), 150, CreatureID(0), 150);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::MAGIC_ARROW, 2);
	teachBattleSpell(defenderSideHero, SpellID::LIGHTNING_BOLT, 2);
	runToCompletion();
	EXPECT_FALSE(recorder.castsOf(SpellID::MAGIC_ARROW).empty());
	EXPECT_FALSE(recorder.castsOf(SpellID::LIGHTNING_BOLT).empty());
	EXPECT_GT(heroSpellDecisions, 1);
}

TEST_F(AIIntegrationNativeBattle, overwhelmingArmyConservesMana)
{
	prepareBattle(CreatureID(0), 1000, CreatureID(0), 1);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::MAGIC_ARROW, 1);
	runToCompletion(BattleSide::ATTACKER);
	EXPECT_EQ(heroSpellDecisions, 0);
}

TEST_F(AIIntegrationNativeBattle, resurrectionUsesRealCasualtiesAndCompletesBattle)
{
	prepareBattle(CreatureID(0), 60, CreatureID(0), 60);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::RESURRECTION, 4);
	setMagicMastery(attackerSideHero, MasteryLevel::EXPERT);
	const CStack * injured = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			injured = unit.get();
	ASSERT_NE(injured, nullptr);
	injure(injured, 300);
	runToCompletion();
	EXPECT_FALSE(recorder.castsOf(SpellID::RESURRECTION).empty());
}

TEST_F(AIIntegrationNativeBattle, fheroes2SpellThreatPreservesForgottenArcherStrength)
{
	// fheroes2's spell-conservation threshold uses strategic archer strength,
	// not immediate shooting legality. Validate that contract against a
	// real VCMI spell effect and then play the native battle to its result.
	prepareBattle(CreatureID(0), 60, CreatureID(2), 30);
	ASSERT_FALSE(HasFatalFailure());
	const CStack * defenderArcher = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(2))
			defenderArcher = unit.get();
	ASSERT_NE(defenderArcher, nullptr);
	ASSERT_TRUE(defenderArcher->isShooter());
	ASSERT_TRUE(battle()->battleCanShoot(defenderArcher));

	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto observer = std::make_shared<DecisionCallback>(player);
	observer->onBattleStarted(battle());
	const auto playerView = observer->getBattle(battle()->getBattleID());
	ASSERT_NE(playerView, nullptr);
	const double rangedBefore = AIIntegration::FHeroes2SpellPolicy::sourceArcherStrength(*playerView, player, false);
	ASSERT_GT(rangedBefore, 0);

	teachBattleSpell(attackerSideHero, SpellID::FORGETFULNESS);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::WATER_MAGIC),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::FORGETFULNESS, defenderArcher));
	ASSERT_TRUE(defenderArcher->isShooter());
	ASSERT_FALSE(defenderArcher->canShoot())
		<< "Forgetfulness must prevent the enemy archer's immediate shot";
	// Forgetfulness also changes the unit's dynamic combat value. The source
	// preserves its ranged classification, not a frozen pre-spell weight.
	ASSERT_GT(defenderArcher->getMovementRange(), 0);
	const double rangedAfter = AIIntegration::FHeroes2SpellPolicy::sourceArcherStrength(*playerView, player, false);
	EXPECT_GT(rangedAfter, 0);
	EXPECT_DOUBLE_EQ(rangedAfter, static_cast<double>(defenderArcher->estimateCombatValue()))
		<< "The disabled archer must contribute its current strength to spell-threat accounting";

	runToCompletion(std::nullopt, false, "FHeroes2Tactics");
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2BlockedArcherStillCountsForDefensiveStance)
{
	// Pinned fheroes2 analyzeBattleState counts an archer as ranged strength
	// even when adjacent enemy melee currently prevents a legal shot.
	// This state used to make our projected stance incorrectly turn offensive.
	prepareBattle(CreatureID(0), 40, CreatureID(0), 20);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * infantry = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			infantry = unit.get();
	ASSERT_NE(infantry, nullptr);
	const BattleHex shooterHex(infantry->getPosition().toInt() + 7);
	const BattleHex blockerHex(shooterHex.toInt() + 1);
	ASSERT_TRUE(shooterHex.isAvailable());
	ASSERT_TRUE(blockerHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(shooterHex), nullptr);
	ASSERT_EQ(battle()->battleGetUnitByPos(blockerHex), nullptr);
	auto * shooter = addStack(BattleSide::ATTACKER, CreatureID(2), shooterHex, 10);
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(addStack(BattleSide::DEFENDER, CreatureID(0), blockerHex, 20), nullptr);
	ASSERT_TRUE(shooter->isShooter());
	ASSERT_GT(shooter->getMovementRange(), 0u);
	ASSERT_FALSE(battle()->battleCanShoot(shooter))
		<< "The enemy must really block the archer's current shot";
	ASSERT_GT(BattleHex::getDistance(infantry->getPosition(), blockerHex),
		static_cast<int>(infantry->getMovementRange()) + 1);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER), battle()->getSideHero(BattleSide::DEFENDER),
		BattleSide::ATTACKER, false);
	policy->activeStack(battleID, infantry);
	ASSERT_TRUE(callback->action.has_value());
	EXPECT_EQ(callback->action->actionType, EActionType::DEFEND)
		<< "Blocked friendly archer still supports defensive fheroes2 stance: "
		<< callback->action->toString();
	policy.reset();
	// The engine, not the test, chooses all subsequent actions and the winner.
	runToCompletion(std::nullopt, false);
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2DefensiveMeleeCoversThreatenedFriendlyShooter)
{
	prepareBattle(CreatureID(0), 40, CreatureID(0), 15);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * infantry = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			infantry = unit.get();
	ASSERT_NE(infantry, nullptr);
	const auto originalHex = infantry->getPosition();
	const BattleHex shooterHex(originalHex.toInt() + 3 * GameConstants::BFIELD_WIDTH);
	const BattleHex enemyHex(shooterHex.toInt() + 5);
	ASSERT_TRUE(shooterHex.isAvailable());
	ASSERT_TRUE(enemyHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(shooterHex), nullptr);
	ASSERT_EQ(battle()->battleGetUnitByPos(enemyHex), nullptr);
	ASSERT_NE(addStack(BattleSide::ATTACKER, CreatureID(2), shooterHex, 120), nullptr);
	ASSERT_NE(addStack(BattleSide::DEFENDER, CreatureID(0), enemyHex, 15), nullptr);
	ASSERT_GT(BattleHex::getDistance(originalHex, enemyHex),
		static_cast<int>(infantry->getMovementRange()) + 1)
		<< "Enemy must be beyond the guard's immediate melee range";
	ASSERT_LE(BattleHex::getDistance(shooterHex, enemyHex),
		static_cast<int>(battle()->battleGetUnitByPos(enemyHex)->getMovementRange()) + 1);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	policy->activeStack(battleID, infantry);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK)
		<< "Defensive fheroes2 melee should cover its threatened shooter: " << callback->action->toString();
	ASSERT_FALSE(callback->action->target.empty());
	const BattleHex destination = callback->action->target.front().hexValue;
	EXPECT_EQ(BattleHex::getDistance(destination, shooterHex), 1)
		<< "The melee screen must stop adjacent to the threatened archer";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Engine rejected the legal cover movement";
	policy.reset();
	runToCompletion(std::nullopt, false);
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2DefensiveMeleeCoversThreatenedWideShooter)
{
	// Pinned fheroes2 meleeUnitDefense visits both cells of every archer,
	// including wide ones; the former VCMI port skipped them entirely.
	prepareBattle(CreatureID(0), 40, CreatureID(0), 15);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * infantry = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			infantry = unit.get();
	ASSERT_NE(infantry, nullptr);
	const auto originalHex = infantry->getPosition();
	const BattleHex shooterHex(originalHex.toInt() + 3 * GameConstants::BFIELD_WIDTH);
	const BattleHex enemyHex(shooterHex.toInt() + 5);
	ASSERT_TRUE(shooterHex.isAvailable());
	ASSERT_TRUE(enemyHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(shooterHex), nullptr);
	ASSERT_EQ(battle()->battleGetUnitByPos(enemyHex), nullptr);
	const CStack * wideShooter = addStack(BattleSide::ATTACKER, CreatureID(77), shooterHex, 12);
	ASSERT_NE(wideShooter, nullptr);
	ASSERT_TRUE(wideShooter->doubleWide());
	ASSERT_TRUE(wideShooter->isShooter());
	ASSERT_EQ(wideShooter->getHexes().size(), 2u);
	for(const auto & occupied : wideShooter->getHexes())
		ASSERT_EQ(battle()->battleGetUnitByPos(occupied), wideShooter);
	ASSERT_NE(addStack(BattleSide::DEFENDER, CreatureID(0), enemyHex, 15), nullptr);
	ASSERT_GT(BattleHex::getDistance(originalHex, enemyHex),
		static_cast<int>(infantry->getMovementRange()) + 1)
		<< "Enemy must be beyond the guard's immediate melee range";
	ASSERT_LE(BattleHex::getDistance(shooterHex, enemyHex),
		static_cast<int>(battle()->battleGetUnitByPos(enemyHex)->getMovementRange()) + 1);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	policy->activeStack(battleID, infantry);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK)
		<< "Defensive fheroes2 melee should cover its threatened shooter: " << callback->action->toString();
	ASSERT_FALSE(callback->action->target.empty());
	const BattleHex destination = callback->action->target.front().hexValue;
	int distanceToWideShooter = GameConstants::BFIELD_SIZE;
	for(const auto & occupied : wideShooter->getHexes())
		distanceToWideShooter = std::min(distanceToWideShooter,
			static_cast<int>(BattleHex::getDistance(destination, occupied)));
	EXPECT_EQ(distanceToWideShooter, 1)
		<< "The screen must cover at least one occupied hex of the wide archer";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Engine rejected the legal cover movement";
	policy.reset();
	runToCompletion(std::nullopt, false);
	EXPECT_GT(recorder.damage, 0);
}


TEST_F(AIIntegrationNativeBattle, fheroes2WideMeleeGuardCoversWideShooterWithBothOccupiedHexes)
{
	// Pinned fheroes2 meleeUnitDefense considers wide guarding units. The
	// earlier port skipped them entirely, despite the native VCMI movement
	// engine supporting legal double-wide positions. Both footprints matter.
	prepareBattle(CreatureID(110), 1, CreatureID(0), 10);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * guard = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(110))
			guard = unit.get();
	ASSERT_NE(guard, nullptr);
	ASSERT_TRUE(guard->doubleWide());
	ASSERT_FALSE(guard->isShooter());
	ASSERT_EQ(battle()->battleActiveUnit(), guard);
	const BattleHex originalHead = guard->getPosition();
	const BattleHex shooterHex(originalHead.toInt() + 4 * GameConstants::BFIELD_WIDTH);
	const BattleHex enemyHex(shooterHex.toInt() + 5);
	ASSERT_TRUE(shooterHex.isAvailable());
	ASSERT_TRUE(enemyHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(shooterHex), nullptr);
	ASSERT_EQ(battle()->battleGetUnitByPos(enemyHex), nullptr);
	const CStack * shooter = addStack(BattleSide::ATTACKER, CreatureID(77), shooterHex, 6);
	ASSERT_NE(shooter, nullptr);
	ASSERT_TRUE(shooter->doubleWide());
	ASSERT_TRUE(shooter->isShooter());
	const CStack * threat = addStack(BattleSide::DEFENDER, CreatureID(0), enemyHex, 15);
	ASSERT_NE(threat, nullptr);
	ASSERT_GT(BattleHex::getDistance(originalHead, enemyHex),
		static_cast<int>(guard->getMovementRange()) + 1)
		<< "Guard must not be able to attack the threat immediately";
	ASSERT_LE(BattleHex::getDistance(shooterHex, enemyHex),
		static_cast<int>(threat->getMovementRange()) + 1)
		<< "Threat must be close enough to justify covering the shooter";

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	policy->activeStack(battleID, guard);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK)
		<< "A wide melee guard must cover a threatened wide shooter: "
		<< callback->action->toString();
	ASSERT_FALSE(callback->action->target.empty());
	const BattleHex destination = callback->action->target.front().hexValue;
	int footprintDistance = GameConstants::BFIELD_SIZE;
	for(const auto & guardHex : guard->getHexes(destination))
		for(const auto & shooterOccupied : shooter->getHexes())
			footprintDistance = std::min(footprintDistance,
				static_cast<int>(BattleHex::getDistance(guardHex, shooterOccupied)));
	EXPECT_EQ(footprintDistance, 1)
		<< "At least one guard footprint cell must screen the wide shooter";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Native battle handler rejected wide melee guard movement";
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2WideGuardDoesNotInventNarrowEnemyTailThreat)
{
	// Regression for the native footprint projection in meleeUnitDefense:
	// a wide friendly *guard* must never contribute a phantom tail to a
	// narrow *enemy* when measuring its ability to threaten our archer.
	prepareBattle(CreatureID(110), 1, CreatureID(0), 10);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * guard = nullptr;
	const CStack * originalEnemy = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(110))
			guard = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			originalEnemy = unit.get();
	}
	ASSERT_NE(guard, nullptr);
	ASSERT_NE(originalEnemy, nullptr);
	ASSERT_TRUE(guard->doubleWide());
	ASSERT_EQ(battle()->battleActiveUnit(), guard);
	const BattleHex originalHead = guard->getPosition();
	const BattleHex shooterHex(originalHead.toInt() + 4 * GameConstants::BFIELD_WIDTH);
	ASSERT_TRUE(shooterHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(shooterHex), nullptr);
	const CStack * shooter = addStack(BattleSide::ATTACKER, CreatureID(77), shooterHex, 6);
	ASSERT_NE(shooter, nullptr);
	ASSERT_TRUE(shooter->doubleWide() && shooter->isShooter());
	const int enemyReach = static_cast<int>(originalEnemy->getMovementRange()) + 1;
	const auto nearestToShooter = [&](const auto & hexes)
	{
		int nearest = GameConstants::BFIELD_SIZE;
		for(const auto & occupied : hexes)
		{
			for(const auto & shooterOccupied : shooter->getHexes())
			{
				nearest = std::min(nearest,
					static_cast<int>(BattleHex::getDistance(occupied, shooterOccupied)));
			}
		}
		return nearest;
	};

	BattleHex enemyHex;
	for(int offset = 1; offset <= 9; ++offset)
	{
		const BattleHex candidate(shooterHex.toInt() + offset);
		if(!candidate.isAvailable() || battle()->battleGetUnitByPos(candidate))
			continue;
		// A wide shooter may itself have a tail, so compare the actual
		// occupied footprint (not merely the shooter's head).
		const int realFootprintDistance = nearestToShooter(std::vector<BattleHex>{candidate});
		const int inventedDistance = nearestToShooter(guard->getHexes(candidate));
		if(realFootprintDistance == enemyReach + 1
			&& inventedDistance <= enemyReach
			&& BattleHex::getDistance(originalHead, candidate)
				> guard->getMovementRange() + 1)
		{
			enemyHex = candidate;
			break;
		}
	}
	ASSERT_TRUE(enemyHex.isValid()) << "Expected a real-footprint versus phantom-tail threat boundary";
	const CStack * threat = addStack(BattleSide::DEFENDER, CreatureID(0), enemyHex, 15);
	ASSERT_NE(threat, nullptr);
	ASSERT_FALSE(threat->doubleWide());
	ASSERT_GT(nearestToShooter(threat->getHexes()),
		static_cast<int>(threat->getMovementRange()) + 1);
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::DEFENDER && unit->alive())
		{
			ASSERT_GT(nearestToShooter(unit->getHexes()),
				static_cast<int>(unit->getMovementRange()) + 1)
				<< "The test must have no real enemy within one-turn melee reach of the archer";
		}
	}

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	policy->activeStack(battleID, guard);
	ASSERT_TRUE(callback->action.has_value());
	EXPECT_EQ(callback->action->actionType, EActionType::DEFEND)
		<< "A phantom tail must not trigger a defensive cover move: " << callback->action->toString();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Engine rejected the guard's actual defensive action";
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2GuardRepositionsFromCoveredWeakShooterToThreatenedStrongShooter)
{
	// Pinned fheroes2 meleeUnitDefense compares *all* friendly shooters
	// using strength minus distance penalty. Already standing beside one
	// weak archer is a zero-travel candidate, not a reason to stop evaluating
	// a more valuable threatened archer.
	prepareBattle(CreatureID(0), 40, CreatureID(0), 15);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * guard = nullptr;
	const CStack * originalEnemy = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			guard = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			originalEnemy = unit.get();
	}
	ASSERT_NE(guard, nullptr);
	ASSERT_NE(originalEnemy, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), guard);
	const BattleHex guardHex = guard->getPosition();
	const BattleHex weakHex(guardHex.toInt() + GameConstants::BFIELD_WIDTH);
	const BattleHex strongHex(guardHex.toInt() + 3 * GameConstants::BFIELD_WIDTH);
	ASSERT_TRUE(weakHex.isAvailable());
	ASSERT_TRUE(strongHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(weakHex), nullptr);
	ASSERT_EQ(battle()->battleGetUnitByPos(strongHex), nullptr);
	const CStack * weak = addStack(BattleSide::ATTACKER, CreatureID(2), weakHex, 1);
	const CStack * strong = addStack(BattleSide::ATTACKER, CreatureID(2), strongHex, 80);
	ASSERT_NE(weak, nullptr);
	ASSERT_NE(strong, nullptr);
	ASSERT_TRUE(weak->isShooter());
	ASSERT_TRUE(strong->isShooter());
	ASSERT_EQ(BattleHex::getDistance(guardHex, weakHex), 1);
	ASSERT_GT(strong->estimateCombatValue(), weak->estimateCombatValue());
	const int enemyReach = static_cast<int>(originalEnemy->getMovementRange()) + 1;
	const int guardReach = static_cast<int>(guard->getMovementRange()) + 1;
	const auto findThreatPosition = [&](const BattleHex & shooterHex)
	{
		BattleHex best;
		int longestToGuard = -1;
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			const BattleHex hex(index);
			if(!hex.isAvailable() || hex.getX() <= shooterHex.getX()
				|| battle()->battleGetUnitByPos(hex) != nullptr)
				continue;
			const int toShooter = BattleHex::getDistance(hex, shooterHex);
			const int toGuard = BattleHex::getDistance(hex, guardHex);
			if(toShooter < 2 || toShooter > enemyReach || toGuard <= guardReach)
				continue;
			if(toGuard > longestToGuard)
			{
				best = hex;
				longestToGuard = toGuard;
			}
		}
		return best;
	};
	const BattleHex weakEnemyHex = findThreatPosition(weakHex);
	ASSERT_TRUE(weakEnemyHex.isValid()) << "Need a real enemy threatening the covered weak shooter";
	const CStack * weakEnemy = addStack(BattleSide::DEFENDER, CreatureID(0), weakEnemyHex, 15);
	ASSERT_NE(weakEnemy, nullptr);
	const BattleHex strongEnemyHex = findThreatPosition(strongHex);
	ASSERT_TRUE(strongEnemyHex.isValid()) << "Need a real enemy threatening the stronger shooter";
	const CStack * strongEnemy = addStack(BattleSide::DEFENDER, CreatureID(0), strongEnemyHex, 15);
	ASSERT_NE(strongEnemy, nullptr);
	ASSERT_LE(BattleHex::getDistance(weakEnemy->getPosition(), weakHex), enemyReach);
	ASSERT_LE(BattleHex::getDistance(strongEnemy->getPosition(), strongHex), enemyReach);
	ASSERT_GT(BattleHex::getDistance(guardHex, weakEnemyHex), guardReach);
	ASSERT_GT(BattleHex::getDistance(guardHex, strongEnemyHex), guardReach);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	const auto reachability = view->getReachability(guard);
	const auto available = view->battleGetAvailableHexes(reachability, guard, false);
	bool canReachStrongCover = false;
	for(const auto & hex : available)
	{
		if(hex.isAvailable() && hex != guardHex
			&& BattleHex::getDistance(hex, strongHex) == 1
			&& reachability.distances[hex.toInt()] <= guard->getMovementRange())
			canReachStrongCover = true;
	}
	ASSERT_TRUE(canReachStrongCover) << "Guard must be able to screen the strong shooter this turn";

	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	policy->activeStack(battleID, guard);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK)
		<< "Already guarding a weak archer must not mask a more valuable threatened archer: "
		<< callback->action->toString();
	ASSERT_FALSE(callback->action->target.empty());
	const BattleHex destination = callback->action->target.front().hexValue;
	EXPECT_EQ(BattleHex::getDistance(destination, strongHex), 1)
		<< "The chosen cover must protect the more valuable archer";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The native engine rejected the selected new cover position";
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2DistantTargetPriorityPursuesEnemyShooter)
{
	prepareBattle(CreatureID(0), 40, CreatureID(0), 1);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * infantry = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			infantry = unit.get();
	}
	ASSERT_NE(infantry, nullptr);
	const auto originalHex = infantry->getPosition();
	// A weak enemy is closer in the upper lane, while a ranged threat is
	// further away in the lower lane. Both are beyond one-turn melee reach.
	const BattleHex weakerHex(originalHex.toInt() + 7 - 2 * GameConstants::BFIELD_WIDTH);
	const BattleHex archerHex(originalHex.toInt() + 8 + 2 * GameConstants::BFIELD_WIDTH);
	ASSERT_TRUE(weakerHex.isAvailable());
	ASSERT_TRUE(archerHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(weakerHex), nullptr);
	ASSERT_EQ(battle()->battleGetUnitByPos(archerHex), nullptr);
	ASSERT_NE(addStack(BattleSide::DEFENDER, CreatureID(0), weakerHex, 1), nullptr);
	ASSERT_NE(addStack(BattleSide::DEFENDER, CreatureID(2), archerHex, 65), nullptr);
	ASSERT_GT(BattleHex::getDistance(originalHex, weakerHex), infantry->getMovementRange() + 1);
	ASSERT_GT(BattleHex::getDistance(originalHex, archerHex), infantry->getMovementRange() + 1);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	policy->activeStack(battleID, infantry);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK)
		<< "Unexpected immediate action: " << callback->action->toString();
	ASSERT_FALSE(callback->action->target.empty());
	const auto destination = callback->action->target.front().hexValue;
	EXPECT_NE(destination, originalHex);
	EXPECT_LT(BattleHex::getDistance(destination, archerHex),
		BattleHex::getDistance(originalHex, archerHex))
		<< "The initial move must close distance to the hostile archer";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The engine rejected the planned distant-target movement";
	policy.reset();
	runToCompletion(std::nullopt, false);
	EXPECT_GT(recorder.damage, 0);
}


TEST_F(AIIntegrationNativeBattle, fheroes2AreaShotValuesActualEnemySplash)
{
	// Original fheroes2 archerDecision accounts for multiple area victims.
	// The stripped CI creature data does not grant Magog a splash bonus. A
	// fixture-local native SHOOTS_ALL_ADJACENT bonus exercises real VCMI area
	// mechanics without changing creature definitions or engine rules.
	prepareBattle(CreatureID(45), 30, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * shooter = nullptr;
	const CStack * isolatedEnemy = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(45))
			shooter = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			isolatedEnemy = unit.get();
	}
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(isolatedEnemy, nullptr);
	ASSERT_TRUE(shooter->isShooter());
	// Use the existing engine bonus only in this synthetic battle fixture.
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SHOOTS_ALL_ADJACENT, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(shooter->hasBonusOfType(BonusType::SHOOTS_ALL_ADJACENT));
	ASSERT_FALSE(shooter->doubleWide());

	BattleHex clusterHex;
	std::vector<BattleHex> neighbours;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex candidate(index);
		if(!candidate.isAvailable() || candidate.getX() < 8 || candidate.getX() > 12
			|| battle()->battleGetUnitByPos(candidate) != nullptr
			|| BattleHex::getDistance(candidate, isolatedEnemy->getPosition()) < 5)
			continue;
		std::vector<BattleHex> freeHexes;
		for(const auto & hex : candidate.getNeighbouringTiles())
			if(hex.isAvailable() && battle()->battleGetUnitByPos(hex) == nullptr
				&& BattleHex::getDistance(hex, isolatedEnemy->getPosition()) >= 4)
				freeHexes.push_back(hex);
		if(freeHexes.size() >= 3)
		{
			clusterHex = candidate;
			neighbours.assign(freeHexes.begin(), freeHexes.begin() + 3);
			break;
		}
	}
	ASSERT_TRUE(clusterHex.isValid());
	const CStack * center = addStack(BattleSide::DEFENDER, CreatureID(0), clusterHex, 1);
	ASSERT_NE(center, nullptr);
	std::vector<const CStack *> clustered{center};
	for(const auto & hex : neighbours)
	{
		const CStack * extra = addStack(BattleSide::DEFENDER, CreatureID(0), hex, 20);
		ASSERT_NE(extra, nullptr);
		clustered.push_back(extra);
	}
	ASSERT_TRUE(battle()->battleCanShoot(shooter, center->getPosition()));
	ASSERT_TRUE(battle()->battleCanShoot(shooter, isolatedEnemy->getPosition()));

	const auto splash = battle()->getAttackedBattleUnits(
		shooter, center, center->getPosition(), true);
	ASSERT_GE(splash.size(), 4u) << "Must use actual Magog splash victims";
	for(const auto * victim : splash)
		EXPECT_EQ(victim->unitSide(), BattleSide::DEFENDER)
			<< "Do not assume fheroes2 friendly-fire mechanics inside VCMI";
	const auto single = battle()->getAttackedBattleUnits(
		shooter, isolatedEnemy, isolatedEnemy->getPosition(), true);
	ASSERT_LE(single.size(), 1u);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	ASSERT_FALSE(callback->action->target.empty());
	const int targetID = callback->action->target.front().unitValue;
	EXPECT_TRUE(std::any_of(clustered.begin(), clustered.end(), [&](const CStack * unit)
	{
		return unit->unitId() == targetID;
	})) << "The source-derived area scorer must prefer the real multi-target splash";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The real battle engine rejected the Magog area shot";
	policy.reset();
	runToCompletion(std::nullopt, false);
	EXPECT_GT(recorder.shots, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2AreaShotEvaluatesBothHexesOfWideTarget)
{
	// Pinned fheroes2 archerDecision scores the HEAD and TAIL aim points of
	// a two-hex AREA_SHOT target separately. Place additional genuine enemy
	// stacks only within the tail footprint. A head-only scorer misses them.
	// Gorgon is a slower, double-wide defender; the Magog must
	// receive the genuine first activation before evaluating its shot.
	prepareBattle(CreatureID(45), 30, CreatureID(102), 1);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * shooter = nullptr;
	const CStack * wideEnemy = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(45))
			shooter = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(102))
			wideEnemy = unit.get();
	}
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(wideEnemy, nullptr);
	ASSERT_TRUE(shooter->isShooter());
	ASSERT_TRUE(wideEnemy->doubleWide());
	const auto & occupied = wideEnemy->getHexes();
	ASSERT_EQ(occupied.size(), 2u);
	const BattleHex head = occupied[0];
	const BattleHex tail = occupied[1];
	ASSERT_NE(head, tail);
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SHOOTS_ALL_ADJACENT, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(shooter->hasBonusOfType(BonusType::SHOOTS_ALL_ADJACENT));

	std::vector<BattleHex> tailOnly;
	for(const auto & hex : tail.getNeighbouringTiles())
	{
		if(hex.isAvailable() && battle()->battleGetUnitByPos(hex) == nullptr
			&& BattleHex::getDistance(hex, head) > 1)
			tailOnly.push_back(hex);
	}
	ASSERT_GE(tailOnly.size(), 2u) << "The real wide stack needs free tail-only neighboring cells";
	const auto * firstExtra = addStack(BattleSide::DEFENDER, CreatureID(0), tailOnly[0], 60);
	const auto * secondExtra = addStack(BattleSide::DEFENDER, CreatureID(0), tailOnly[1], 60);
	ASSERT_NE(firstExtra, nullptr);
	ASSERT_NE(secondExtra, nullptr);
	ASSERT_TRUE(battle()->battleCanShoot(shooter, head));
	ASSERT_TRUE(battle()->battleCanShoot(shooter, tail));

	const auto headVictims = battle()->getAttackedBattleUnits(shooter, wideEnemy, head, true);
	const auto tailVictims = battle()->getAttackedBattleUnits(shooter, wideEnemy, tail, true);
	const auto includes = [](const auto & victims, const CStack * unit)
	{
		return std::any_of(victims.begin(), victims.end(), [&](const auto * victim)
		{
			return victim->unitId() == unit->unitId();
		});
	};
	for(const auto * extra : {firstExtra, secondExtra})
	{
		ASSERT_FALSE(includes(headVictims, extra)) << "Head shot must not reach tail-only enemies";
		ASSERT_TRUE(includes(tailVictims, extra)) << "Tail shot must reach the real extra enemies";
	}

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	ASSERT_EQ(callback->action->target.size(), 1u);
	EXPECT_EQ(callback->action->target.front().hexValue, tail)
		<< "Wide-area target must be aimed at its better tail footprint";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The real engine rejected the source-derived wide-target area shot";
	policy.reset();
	runToCompletion(std::nullopt, false);
	EXPECT_GT(recorder.shots, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2BlockedArcherAttacksWideEnemyTailInRealBattle)
{
	// Pinned fheroes2 archerDecision checks adjacency of full occupied
	// positions, not merely the wide enemy's head. The VCMI projection
	// must allow an actual adjacent tail to enter its blocked-archer melee
	// branch, and the game handler must accept the resulting attack.
	prepareBattle(CreatureID(19), 30, CreatureID(4), 4);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * shooter = nullptr;
	const CStack * originalWide = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(19))
			shooter = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(4))
			originalWide = unit.get();
	}
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(originalWide, nullptr);
	ASSERT_TRUE(shooter->isShooter());
	ASSERT_TRUE(originalWide->doubleWide());
	ASSERT_FALSE(shooter->doubleWide());
	ASSERT_EQ(originalWide->getHexes().size(), 2u);
	int tailOffset = 0;
	for(const auto & occupied : originalWide->getHexes())
		if(occupied != originalWide->getPosition())
			tailOffset = occupied.toInt() - originalWide->getPosition().toInt();
	ASSERT_NE(tailOffset, 0);

	// The normal attacker spawn is at the left edge. A defender's wide
	// tail points outwards, so head-distance > 1 with tail contact cannot
	// be arranged there. Place the real active unit on an empty central
	// hex for this fixture; all subsequent decisions and the outcome remain
	// native engine actions, not prescribed scripted commands.
	const BattleHex shooterPosition(GameConstants::BFIELD_SIZE / 2);
	ASSERT_TRUE(shooterPosition.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(shooterPosition), nullptr);
	const_cast<CStack *>(shooter)->setPosition(shooterPosition);
	ASSERT_EQ(battle()->battleGetUnitByPos(shooterPosition), shooter);

	BattleHex chosenHead;
	BattleHex chosenTail;
	for(const auto & adjacent : shooterPosition.getNeighbouringTiles())
	{
		const BattleHex head(adjacent.toInt() - tailOffset);
		if(!adjacent.isAvailable() || !head.isAvailable()
			|| battle()->battleGetUnitByPos(adjacent) != nullptr
			|| battle()->battleGetUnitByPos(head) != nullptr
			|| BattleHex::getDistance(shooterPosition, head) <= 1)
			continue;
		chosenHead = head;
		chosenTail = adjacent;
		break;
	}
	ASSERT_TRUE(chosenHead.isValid())
		<< "Fixture requires an empty wide head two hexes away and its adjacent tail";
	const auto * wideEnemy = addStack(BattleSide::DEFENDER, CreatureID(4), chosenHead, 3);
	ASSERT_NE(wideEnemy, nullptr);
	ASSERT_TRUE(wideEnemy->doubleWide());
	ASSERT_EQ(battle()->battleGetUnitByPos(chosenHead), wideEnemy);
	ASSERT_EQ(battle()->battleGetUnitByPos(chosenTail), wideEnemy);
	ASSERT_GT(BattleHex::getDistance(shooterPosition, wideEnemy->getPosition()), 1);
	ASSERT_EQ(BattleHex::getDistance(shooterPosition, chosenTail), 1);
	ASSERT_FALSE(battle()->battleCanShoot(shooter));
	ASSERT_GE(static_cast<int>(wideEnemy->getMovementRange()) + 2,
		static_cast<int>(shooter->getMovementRange()))
		<< "The nearby griffin must prevent the pinned safe-retreat branch";
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto inspected = callback->getBattle(battleID);
	ASSERT_NE(inspected, nullptr);
	ASSERT_FALSE(inspected->meleeAttackHexes(shooter, wideEnemy, shooterPosition).empty())
		<< "The real VCMI rules must allow a melee hit on the wide tail";
	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK)
		<< "An adjacent wide tail must be eligible for blocked-archer melee";
	ASSERT_GE(callback->action->target.size(), 2u);
	EXPECT_EQ(inspected->battleGetUnitByPos(callback->action->target[1].hexValue), wideEnemy);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Real game handler rejected the source-derived wide-tail melee attack";
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
}


TEST_F(AIIntegrationNativeBattle, fheroes2BlockedWideArcherMakesNativeMeleeAttack)
{
	// fheroes2 archerDecision handles a blocked ranged creature even when
	// its two-hex footprint is wide. The partial VCMI port previously
	// skipped this source-derived melee branch for all such creatures.
	// A genuine Medusa Queen is a wide shooter. Adjacent enemy infantry blocks
	// its ranged attack, while the native engine must accept its melee hit.
	prepareBattle(CreatureID(77), 20, CreatureID(0), 8);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * shooter = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(77))
			shooter = unit.get();
	ASSERT_NE(shooter, nullptr);
	ASSERT_TRUE(shooter->doubleWide());
	ASSERT_TRUE(shooter->isShooter());
	ASSERT_EQ(battle()->battleActiveUnit(), shooter)
		<< "The source policy must act for the genuinely active wide archer";

	const BattleHex shooterHead = shooter->getPosition();
	const auto candidateHexes = shooterHead.getNeighbouringTiles();
	BattleHex adjacent;
	for(const auto & hex : candidateHexes)
	{
		if(hex.isAvailable() && battle()->battleGetUnitByPos(hex) == nullptr)
		{
			adjacent = hex;
			break;
		}
	}
	ASSERT_TRUE(adjacent.isValid());
	const CStack * blocker = addStack(BattleSide::DEFENDER, CreatureID(0), adjacent, 10);
	ASSERT_NE(blocker, nullptr);
	ASSERT_EQ(battle()->battleGetUnitByPos(adjacent), blocker);
	ASSERT_FALSE(battle()->battleCanShoot(shooter))
		<< "The live Medusa Queen must actually be blocked from shooting";

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_FALSE(view->meleeAttackHexes(shooter, blocker, shooter->getPosition()).empty())
		<< "This is a genuinely legal wide-shooter melee contact";
	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK)
		<< "Blocked wide shooters must use the fheroes2 melee decision";
	ASSERT_GE(callback->action->target.size(), 2u);
	EXPECT_EQ(view->battleGetUnitByPos(callback->action->target[1].hexValue), blocker);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Native server rejected the wide archer's source-derived melee action";
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2WideArcherRetreatChecksItsTailFootprint)
{
	// The pinned fheroes2 archerDecision measures danger against the entire
	// Battle::Position. A Medusa Queen's tail is threatened by adjacent slow
	// melee, although the defender is not adjacent to her head. The selected
	// safe wide-unit movement must be accepted by the real VCMI server.
	prepareBattle(CreatureID(77), 20, CreatureID(16), 8);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * shooter = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(77))
			shooter = unit.get();
	ASSERT_NE(shooter, nullptr);
	ASSERT_TRUE(shooter->doubleWide());
	ASSERT_TRUE(shooter->isShooter());
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);
	// The stock deployment puts the attacker's tail against the field edge;
	// there is no available hex adjacent only to that tail. Reposition the
	// live active stack to a free central double-wide footprint first.
	const BattleHex central(GameConstants::BFIELD_SIZE / 2);
	ASSERT_TRUE(central.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(central), nullptr);
	const BattleHex centralTail = shooter->occupiedHex(central);
	ASSERT_TRUE(centralTail.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(centralTail), nullptr);
	const_cast<CStack *>(shooter)->setPosition(central);
	// The installed creature data gives Medusa Queen only six movement
	// against this Dwarf's four. Give the synthetic test stack an existing
	// +2 speed bonus so the upstream strictly-faster retreat gate can fire.
	// No production creature stats or game mechanics are changed.
	const_cast<CStack *>(shooter)->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::STACKS_SPEED, BonusSource::OTHER, 2, BonusSourceID()));
	ASSERT_EQ(battle()->battleGetUnitByPos(central), shooter);
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);
	const BattleHex shooterHead = shooter->getPosition();
	const BattleHex shooterTail = shooter->occupiedHex();
	ASSERT_TRUE(shooterTail.isAvailable());

	BattleHex blockerHex;
	for(const auto & hex : shooterTail.getNeighbouringTiles())
	{
		if(hex.isAvailable() && battle()->battleGetUnitByPos(hex) == nullptr
			&& BattleHex::getDistance(hex, shooterHead) > 1)
		{
			blockerHex = hex;
			break;
		}
	}
	ASSERT_TRUE(blockerHex.isAvailable()) << "Fixture needs a free tail-only adjacent hex";
	const CStack * blocker = addStack(BattleSide::DEFENDER, CreatureID(16), blockerHex, 10);
	ASSERT_NE(blocker, nullptr);
	ASSERT_EQ(battle()->battleGetUnitByPos(blockerHex), blocker);
	ASSERT_EQ(BattleHex::getDistance(shooterTail, blockerHex), 1);
	ASSERT_GT(BattleHex::getDistance(shooterHead, blockerHex), 1);
	ASSERT_GT(static_cast<int>(shooter->getMovementRange()),
		static_cast<int>(blocker->getMovementRange()) + 2)
		<< "fheroes2 only retreats from sufficiently slower adjacent melee";
	ASSERT_FALSE(battle()->battleCanShoot(shooter))
		<< "A defender next to the tail must block this wide shooter's shot";

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK)
		<< "Wide shooter's tail must count as threatened in fheroes2 retreat: "
		<< callback->action->toString();
	ASSERT_FALSE(callback->action->target.empty());
	const BattleHex destination = callback->action->target.front().hexValue;
	ASSERT_NE(destination, shooterHead);
	int nearestAfter = GameConstants::BFIELD_SIZE;
	for(const auto & occupied : shooter->getHexes(destination))
		for(const auto & enemyHex : blocker->getHexes())
			nearestAfter = std::min(nearestAfter,
				static_cast<int>(BattleHex::getDistance(occupied, enemyHex)));
	EXPECT_GT(nearestAfter, static_cast<int>(blocker->getMovementRange()) + 1)
		<< "Both cells of the retreated wide shooter must be safe";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Engine rejected a wide archer's source-derived retreat";
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2BlockedArcherUsesUpstreamMeleeDamageDifference)
{
	// Pinned fheroes2 archerDecision uses raw outgoing HP damage minus
	// retaliation when a shooter is in melee and has no safe escape.
	// Previously the port used the general combat-value melee evaluator.
	prepareBattle(CreatureID(19), 25, CreatureID(16), 10);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * shooter = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(19))
			shooter = unit.get();
	ASSERT_NE(shooter, nullptr);
	ASSERT_TRUE(shooter->isShooter());
	ASSERT_FALSE(shooter->doubleWide());

	const BattleHex shooterPosition = shooter->getPosition();
	std::vector<BattleHex> adjacent;
	for(const auto & hex : shooterPosition.getNeighbouringTiles())
		if(hex.isAvailable() && battle()->battleGetUnitByPos(hex) == nullptr)
			adjacent.push_back(hex);
	ASSERT_GE(adjacent.size(), 2u);
	const auto * slowBlocker = addStack(BattleSide::DEFENDER, CreatureID(16), adjacent[0], 15);
	const auto * fastBlocker = addStack(BattleSide::DEFENDER, CreatureID(19), adjacent[1], 15);
	ASSERT_NE(slowBlocker, nullptr);
	ASSERT_NE(fastBlocker, nullptr);
	ASSERT_FALSE(battle()->battleCanShoot(shooter));
	ASSERT_GE(static_cast<int>(fastBlocker->getMovementRange()) + 2,
		static_cast<int>(shooter->getMovementRange()))
		<< "A fast adjacent blocker must prevent the upstream escape branch";

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto viewedBattle = callback->getBattle(battleID);
	ASSERT_NE(viewedBattle, nullptr);
	const CStack * expected = nullptr;
	double bestDifference = -std::numeric_limits<double>::infinity();
	for(const auto * enemy : {slowBlocker, fastBlocker})
	{
		const auto attackHexes = viewedBattle->meleeAttackHexes(shooter, enemy, shooter->getPosition());
		ASSERT_FALSE(attackHexes.empty());
		BattleAttackInfo info(shooter, enemy, 0, false);
		info.attackerPos = shooter->getPosition();
		DamageEstimation retaliation;
		const auto damage = viewedBattle->battleEstimateDamage(info, &retaliation);
		const double difference = (static_cast<double>(damage.damage.min) + damage.damage.max
			- static_cast<double>(retaliation.damage.min) - retaliation.damage.max) / 2.0;
		if(!expected || difference > bestDifference)
		{
			expected = enemy;
			bestDifference = difference;
		}
	}
	ASSERT_NE(expected, nullptr);
	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(callback->action->target.size(), 2u);
	const auto targetHex = callback->action->target[1].hexValue;
	const auto * chosen = viewedBattle->battleGetUnitByPos(targetHex);
	ASSERT_NE(chosen, nullptr);
	EXPECT_EQ(chosen->unitId(), expected->unitId())
		<< "Blocked fheroes2 archers rank adjacent melee enemies by damage minus retaliation";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The engine rejected the source-derived blocked-archer melee attack";
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2BlockedFastArcherEscapesAdjacentSlowMelee)
{
	// In pinned fheroes2 archerDecision, retreat is evaluated even when a
	// nearby enemy prevents shooting. The previous VCMI port returned early
	// on battleCanShoot(false), incorrectly forcing a melee attack.
	// Use the naturally active attacker for the first decision. A newly
	// inserted shooter can propose a legal escape, but the game handler
	// correctly rejects its command when another stack owns the turn.
	prepareBattle(CreatureID(19), 25, CreatureID(16), 10);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * shooter = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(19))
			shooter = unit.get();
	ASSERT_NE(shooter, nullptr);
	const BattleHex shooterHex = shooter->getPosition();
	const BattleHex blockerHex(shooterHex.toInt() + 1);
	ASSERT_TRUE(blockerHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(blockerHex), nullptr);
	const auto * blocker = addStack(BattleSide::DEFENDER, CreatureID(16), blockerHex, 10);
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(blocker, nullptr);
	ASSERT_TRUE(shooter->isShooter());
	ASSERT_FALSE(shooter->doubleWide());
	ASSERT_FALSE(battle()->battleCanShoot(shooter)) << "Adjacent enemy must block the shot";
	ASSERT_GT(static_cast<int>(shooter->getMovementRange()),
		static_cast<int>(blocker->getMovementRange()) + 2)
		<< "Pinned fheroes2 only escapes enemies slower by more than two";

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	const auto homm3Decision = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*callback->getBattle(battleID), *shooter);
	ASSERT_EQ(callback->action->actionType, EActionType::WALK)
		<< "Blocked fast fheroes2 archer must escape slower adjacent melee: "
		<< callback->action->toString();
	EXPECT_NE(callback->action->actionType, homm3Decision.actionType)
		<< "FHeroes2 retreat must differ from the independent HoMM3 melee policy";
	ASSERT_FALSE(callback->action->target.empty());
	const BattleHex destination = callback->action->target.front().hexValue;
	EXPECT_GT(BattleHex::getDistance(destination, blockerHex),
		BattleHex::getDistance(shooterHex, blockerHex));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Real engine rejected the fast archer's blocked-shot retreat";
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2ForgottenRemoteShooterDoesNotBlockSafeRetreat)
{
	// Pinned fheroes2 archerDecision treats a distant, unengaged archer as
	// a ranged unit even when Forgetfulness makes its current shot illegal.
	// A temporary speed bonus makes the old battleCanShoot-based port
	// incorrectly classify that archer as an unavoidable fast melee threat.
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(19), 30); // Grand Elf
	setArmy(defenderSideHero, CreatureID(2), 15); // Archer
	teachBattleSpell(attackerSideHero, SpellID::FORGETFULNESS);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::WATER_MAGIC),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	battle()->obstacles.clear();

	const CStack * shooter = nullptr;
	const CStack * forgottenArcher = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(19))
			shooter = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(2))
			forgottenArcher = unit.get();
	}
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(forgottenArcher, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);
	ASSERT_TRUE(forgottenArcher->isShooter());
	ASSERT_GT(BattleHex::getDistance(shooter->getPosition(), forgottenArcher->getPosition()), 1);
	const BattleHex blockerHex(shooter->getPosition().toInt() + 1);
	ASSERT_TRUE(blockerHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(blockerHex), nullptr);
	const CStack * blocker = addStack(BattleSide::DEFENDER, CreatureID(16), blockerHex, 10);
	ASSERT_NE(blocker, nullptr);
	ASSERT_FALSE(battle()->battleCanShoot(shooter));
	ASSERT_GT(static_cast<int>(shooter->getMovementRange()),
		static_cast<int>(blocker->getMovementRange()) + 2);

	ASSERT_TRUE(castOn(attackerSideHero, SpellID::FORGETFULNESS, forgottenArcher));
	ASSERT_FALSE(battle()->battleCanShoot(forgottenArcher));
	ASSERT_TRUE(forgottenArcher->isShooter());
	// Deliberately make this remote archer faster than the acting Grand Elf.
	// Its speed must not affect fheroes2's unengaged-ranged threat category.
	const_cast<CStack *>(forgottenArcher)->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 25, BonusSourceID()));
	ASSERT_GT(static_cast<int>(forgottenArcher->getMovementRange()),
		static_cast<int>(shooter->getMovementRange()));
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK)
		<< "Remote unengaged shooter under Forgetfulness must not prevent Grand Elf retreat: "
		<< callback->action->toString();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Real VCMI engine rejected source-derived archer retreat";
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2FastArcherEscapesAnImminentSlowMeleeThreat)
{
	prepareBattle(CreatureID(19), 25, CreatureID(16), 25);
	ASSERT_FALSE(HasFatalFailure());
	// Place an additional real enemy stack within one turn's melee reach.
	// The enemy starts four hexes away: still a next-turn melee threat, but
	// the archer can reach a safe edge hex. The existing defender remains,
	// so retreat must be safe
	// against *every* enemy rather than just the closest one.
	battle()->obstacles.clear();
	const CStack * shooter = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(19))
			shooter = unit.get();
	ASSERT_NE(shooter, nullptr);
	ASSERT_TRUE(shooter->hasBonusOfType(BonusType::SHOOTER));
	const BattleHex attackerPosition = shooter->getPosition();
	const BattleHex approachingHex(attackerPosition.toInt() + 4);
	ASSERT_TRUE(approachingHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(approachingHex), nullptr);
	ASSERT_NE(addStack(BattleSide::DEFENDER, CreatureID(16), approachingHex, 25), nullptr);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	std::ostringstream retreatDiagnostics;
	const auto inspected = callback->getBattle(battleID);
	// Compare both source-derived backends on the *same live battlefield*,
	// before either command is applied. HoMM3 shoots a valid target while
	// fheroes2 chooses to preserve the faster archer by retreating.
	const auto homm3Decision = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*inspected, *shooter);
	EXPECT_EQ(homm3Decision.actionType, EActionType::SHOOT)
		<< "HoMM3 must retain a legal ranged shot in this differential fixture: " << homm3Decision.toString();
	EXPECT_NE(callback->action->actionType, homm3Decision.actionType)
		<< "Two independently selected policies unexpectedly made the same first decision";
	retreatDiagnostics << "archer pos=" << shooter->getPosition().toInt()
		<< " speed=" << shooter->getMovementRange()
		<< " canShoot=" << inspected->battleCanShoot(shooter)
		<< " doubleWide=" << shooter->doubleWide();
	for(const auto * enemy : inspected->battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY))
	{
		retreatDiagnostics << " enemy=" << enemy->unitId() << " pos=" << enemy->getPosition().toInt()
			<< " speed=" << enemy->getMovementRange()
			<< " distance=" << static_cast<int>(BattleHex::getDistance(shooter->getPosition(), enemy->getPosition()))
			<< " flying=" << enemy->hasBonusOfType(BonusType::FLYING);
	}
	const auto inspectedReach = inspected->getReachability(shooter);
	const auto inspectedAvailable = inspected->battleGetAvailableHexes(inspectedReach, shooter, false);
	retreatDiagnostics << " available=" << inspectedAvailable.size()
		<< " decision=" << callback->action->toString();
	ASSERT_EQ(callback->action->actionType, EActionType::WALK)
		<< "The faster fheroes2 archer should first escape reachable slower melee stacks. "
		<< retreatDiagnostics.str();
	ASSERT_FALSE(callback->action->target.empty());
	EXPECT_NE(callback->action->target.front().hexValue, attackerPosition);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The native game handler rejected the requested archer escape";
	policy.reset();

	// The escape must not merely be a legal move: both distinct backends
	// must play the remaining real battle until the engine declares a winner.
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2SlowArcherDoesNotAttemptUnwinnableEscape)
{
	prepareBattle(CreatureID(2), 25, CreatureID(16), 25);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * shooter = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(2))
			shooter = unit.get();
	ASSERT_NE(shooter, nullptr);
	const BattleHex approachingHex(shooter->getPosition().toInt() + 3);
	ASSERT_TRUE(approachingHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(approachingHex), nullptr);
	ASSERT_NE(addStack(BattleSide::DEFENDER, CreatureID(16), approachingHex, 25), nullptr);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	EXPECT_EQ(callback->action->actionType, EActionType::SHOOT)
		<< "A shooter too slow to escape should retain its legal ranged attack";
	runToCompletion(std::nullopt, false);
}

TEST_F(AIIntegrationNativeBattle, homm3TacticsCompletesRealMeleeBattle)
{
	prepareBattle(CreatureID(0), 30, CreatureID(0), 20);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
}

TEST_F(AIIntegrationNativeBattle, homm3TacticsCompletesRealRangedBattle)
{
	prepareBattle(CreatureID(2), 20, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.shots, 0);
}

TEST_F(AIIntegrationNativeBattle, homm3RangedPrefersActiveTargetOverBlindAndCompletesBattle)
{
	// The decompiled HoMM3 scorer reduces shots into incapacitated targets.
	// A strong but blinded stack must not distract from a weaker active enemy.
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(2), 200);
	setArmy(defenderSideHero, CreatureID(0), 35);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(defenderSideHero->id, SlotID(1)), CreatureID(0).toCreature(), 14));
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::BLIND);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	const CStack * shooter = nullptr;
	const CStack * stronger = nullptr;
	const CStack * active = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(2))
			shooter = unit.get();
		else if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
		{
			if(unit->getCount() == 35)
				stronger = unit.get();
			else if(unit->getCount() == 14)
				active = unit.get();
		}
	}
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(stronger, nullptr);
	ASSERT_NE(active, nullptr);
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto view = std::make_shared<DecisionCallback>(player);
	view->onBattleStarted(battle());
	const auto playerBattle = view->getBattle(battle()->getBattleID());
	ASSERT_NE(playerBattle, nullptr);
	const auto before = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*playerBattle, *shooter);
	ASSERT_EQ(before.actionType, EActionType::SHOOT);
	ASSERT_FALSE(before.target.empty());
	EXPECT_EQ(before.target.front().unitValue, stronger->unitId())
		<< "The strong target must attract the ordinary HoMM3 ranged scorer";
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::BLIND, stronger));
	ASSERT_TRUE(stronger->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::BLIND)))));
	const auto after = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*playerBattle, *shooter);
	ASSERT_EQ(after.actionType, EActionType::SHOOT);
	ASSERT_FALSE(after.target.empty());
	EXPECT_EQ(after.target.front().unitValue, active->unitId())
		<< "The incapacitated stack should receive the original HoMM3 shot-value penalty";
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.shots, 0);
}

TEST_F(AIIntegrationNativeBattle, homm3TacticsAppliesDefenderVictory)
{
	prepareBattle(CreatureID(0), 2, CreatureID(0), 100);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(BattleSide::DEFENDER, false, "HoMM3OriginalTactics");
}

TEST_F(AIIntegrationNativeBattle, homm3TacticsHandlesFlyingAndDoubleWideUnits)
{
	prepareBattle(CreatureID(10), 12, CreatureID(4), 20);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
}

TEST_F(AIIntegrationNativeBattle, tumTacticsCompletesRealMeleeBattle)
{
	prepareBattle(CreatureID(0), 30, CreatureID(0), 20);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
}

TEST_F(AIIntegrationNativeBattle, tumTacticsCompletesRealRangedBattle)
{
	prepareBattle(CreatureID(2), 20, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.shots, 0);
}

TEST_F(AIIntegrationNativeBattle, tumSiegeDefenderHoldsInsideIntactCastleAndCompletesBattle)
{
	prepareSiege();
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_EQ(battle()->battleGetGateState(), EGateState::CLOSED);
	const CStack * defender = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->alive() && unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
		{
			defender = unit.get();
			break;
		}
	}
	ASSERT_NE(defender, nullptr);
	ASSERT_TRUE(battle()->battleIsInsideWalls(defender->getPosition()));
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::DEFENDER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto policy = AIFactory::createBattleAI("TUMEnchantedTactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::DEFENDER, false);
	policy->activeStack(battleID, defender);
	ASSERT_TRUE(callback->action.has_value());
	EXPECT_EQ(callback->action->actionType, EActionType::DEFEND)
		<< "The castle defender must hold while the gate is closed and nobody has breached";
	// Resolve through the engine as well: holding must not trap the siege
	// in a battle without a winner.
	runToCompletion(BattleSide::ATTACKER, false, "TUMEnchantedTactics");
	EXPECT_FALSE(recorder.catapultAttacks.empty());
}

TEST_F(AIIntegrationNativeBattle, tumTacticsFinishesMixedFastAndSlowEnemyBattle)
{
	// Mix one faster flyer with slower infantry so the movement-only
	// anti-distraction policy has the information it needs. The source
	// predicate is tested independently in CI/source_tactics_test.cpp.
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 80);
	setArmy(defenderSideHero, CreatureID(4), 15);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(defenderSideHero->id, SlotID(1)), CreatureID(0).toCreature(), 25));
	ASSERT_FALSE(HasFatalFailure());
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	const CStack * fast = nullptr;
	const CStack * slow = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() != BattleSide::DEFENDER)
			continue;
		if(unit->creatureId() == CreatureID(4))
			fast = unit.get();
		if(unit->creatureId() == CreatureID(0))
			slow = unit.get();
	}
	ASSERT_NE(fast, nullptr);
	ASSERT_NE(slow, nullptr);
	EXPECT_GT(fast->getMovementRange(), slow->getMovementRange());
	// A genuine server-controlled finish is required, not just an
	// invocation of an isolated route-selection predicate.
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
}

TEST_F(AIIntegrationNativeBattle, tumIgnoresNearbyFastDecoyForMovement)
{
	// The HoMM3 baseline chases the nearby fast decoy. The TUM variant
	// should advance toward the slower enemy instead, without preventing
	// a legal attack when one is available. This is a native battle state.
	prepareBattle(CreatureID(0), 80, CreatureID(0), 35);
	ASSERT_FALSE(HasFatalFailure());
	const CStack * own = nullptr;
	const CStack * slow = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->creatureId() != CreatureID(0))
			continue;
		if(unit->unitSide() == BattleSide::ATTACKER)
			own = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER)
			slow = unit.get();
	}
	ASSERT_NE(own, nullptr);
	ASSERT_NE(slow, nullptr);
	const auto origin = own->getPosition();
	const int baitY = origin.getY() >= 4 ? origin.getY() - 3 : origin.getY() + 3;
	const BattleHex baitHex(static_cast<si16>(origin.getX() + 5), static_cast<si16>(baitY));
	ASSERT_TRUE(baitHex.isAvailable());
	ASSERT_LT(BattleHex::getDistance(origin, baitHex), BattleHex::getDistance(origin, slow->getPosition()));
	const CStack * fast = addStack(BattleSide::DEFENDER, CreatureID(4), baitHex, 12);
	ASSERT_NE(fast, nullptr);
	const int speed = static_cast<int>(own->getMovementRange());
	ASSERT_GE(static_cast<int>(fast->getMovementRange()), speed + 2);
	ASSERT_LE(static_cast<int>(slow->getMovementRange()), speed);
	const int distance = BattleHex::getDistance(origin, fast->getPosition());
	ASSERT_GT(distance, speed + 1) << "The decoy must not be immediately attackable";
	ASSERT_LE(distance, speed * 2 + 1) << "The decoy must fall inside the guarded movement range";

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto playerView = callback->getBattle(battleID);
	ASSERT_NE(playerView, nullptr);
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*playerView, *own);
	ASSERT_EQ(baseline.actionType, EActionType::WALK)
		<< "Fixture must make the original HoMM3 baseline pursue rather than attack";

	auto policy = AIFactory::createBattleAI("TUMEnchantedTactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	policy->activeStack(battleID, own);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK);
	ASSERT_FALSE(baseline.target.empty());
	ASSERT_FALSE(callback->action->target.empty());
	EXPECT_NE(callback->action->target.front().hexValue, baseline.target.front().hexValue)
		<< "TUM should advance toward slower infantry rather than the faster decoy";
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
}

TEST_F(AIIntegrationNativeBattle, tumTacticsPropagatesDefenderVictory)
{
	prepareBattle(CreatureID(0), 2, CreatureID(0), 100);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(BattleSide::DEFENDER, false, "TUMEnchantedTactics");
}

TEST_F(AIIntegrationNativeBattle, tumTacticsMakesRealBallistaDecisions)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 40);
	setArmy(defenderSideHero, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	giveArtifact(attackerSideHero, ArtifactID(ArtifactID::BALLISTA), ArtifactPosition::MACH1);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::ARTILLERY),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(ballistaDecisions, 0);
	EXPECT_GT(ballistaShots, 0);
}
TEST_F(AIIntegrationNativeBattle, tumBallistaSelectsUnblindedTargetWhenAnotherIsBlind)
{
	// TUM AI Improvements.txt excludes disabled ballista targets individually,
	// not the whole firing action. A still-attackable enemy must be selected.
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 60);
	setArmy(defenderSideHero, CreatureID(0), 50);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(defenderSideHero->id, SlotID(1)), CreatureID(0).toCreature(), 12));
	ASSERT_FALSE(HasFatalFailure());
	giveArtifact(attackerSideHero, ArtifactID(ArtifactID::BALLISTA), ArtifactPosition::MACH1);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::ARTILLERY),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	teachBattleSpell(attackerSideHero, SpellID::BLIND);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);

	const CStack * blinded = nullptr;
	const CStack * validTarget = nullptr;
	const CStack * ballista = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
		{
			if(unit->getCount() == 50)
				blinded = unit.get();
			else if(unit->getCount() == 12)
				validTarget = unit.get();
		}
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID::BALLISTA)
			ballista = unit.get();
	}
	ASSERT_NE(blinded, nullptr);
	ASSERT_NE(validTarget, nullptr);
	ASSERT_NE(ballista, nullptr);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::BLIND, blinded));
	ASSERT_TRUE(blinded->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::BLIND)))));
	ASSERT_TRUE(battle()->battleCanShoot(ballista, validTarget->getPosition()));

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto policy = AIFactory::createBattleAI("TUMEnchantedTactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	policy->activeStack(battleID, ballista);
	ASSERT_TRUE(callback->action.has_value());
	EXPECT_EQ(callback->action->actionType, EActionType::SHOOT)
		<< "A blind enemy must not suppress shots at another valid enemy";
	ASSERT_FALSE(callback->action->target.empty());
	EXPECT_EQ(callback->action->target.front().unitValue, validTarget->unitId())
		<< "The TUM ballista must select the unblinded stack, never the disabled one";

	// All subsequent commands still come from actual active-unit events,
	// and the VCMI engine must accept them and finish this battle.
	const int validTargetId = validTarget->unitId();
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(ballistaShots, 0);
	ASSERT_TRUE(firstBallistaShotTarget.has_value()) << "The native battle never accepted a ballista shot";
	EXPECT_EQ(*firstBallistaShotTarget, validTargetId)
		<< "The first engine-accepted ballista shot must avoid the blinded enemy";
}

TEST_F(AIIntegrationNativeBattle, tumBallistaDefendsRatherThanBreakingBlind)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 55);
	setArmy(defenderSideHero, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	giveArtifact(attackerSideHero, ArtifactID(ArtifactID::BALLISTA), ArtifactPosition::MACH1);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::ARTILLERY),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	teachBattleSpell(attackerSideHero, SpellID::BLIND);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	const CStack * blinded = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			blinded = unit.get();
	ASSERT_NE(blinded, nullptr);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::BLIND, blinded));
	ASSERT_TRUE(blinded->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::BLIND)))));
	const CStack * attackingBallista = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID::BALLISTA)
			attackingBallista = unit.get();
	ASSERT_NE(attackingBallista, nullptr);
	const auto view = std::make_shared<DecisionCallback>(battle()->sideToPlayer(BattleSide::ATTACKER));
	view->onBattleStarted(battle());
	const auto viewedBattle = view->getBattle(battle()->getBattleID());
	ASSERT_NE(viewedBattle, nullptr);
	const auto baselineDecision = AIIntegration::HoMM3TacticsBattleAI::chooseAction(
		*viewedBattle, *attackingBallista);
	EXPECT_EQ(baselineDecision.actionType, EActionType::SHOOT)
		<< "The baseline must shoot so that the TUM target protection is a real behavioral override";
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(ballistaDecisions, 0);
	EXPECT_GT(ballistaDefendsWhileAllEnemiesBlind, 0)
		<< "TUM ballista must not shoot the only remaining blinded target";
}

TEST_F(AIIntegrationNativeBattle, tumMeleeHoldsPositionToProtectShootersAgainstRealNeutralStack)
{
	// Construct an actual neutral map object, not a player-owned hero whose
	// color is mocked. The TUM protection rule explicitly requires neutrals.
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 60);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(attackerSideHero->id, SlotID(1)), CreatureID(2).toCreature(), 25));
	ASSERT_FALSE(HasFatalFailure());
	gameHandler->createWanderingMonster({9, 9, 0}, CreatureID(0), 40);
	const auto * neutral = findFirst<CGCreature>();
	ASSERT_NE(neutral, nullptr);
	// A wandering monster is UNFLAGGABLE on the adventure map;
	// SideInBattle normalizes that ownership to NEUTRAL for combat.
	ASSERT_EQ(neutral->getOwner(), PlayerColor::UNFLAGGABLE);
	gameHandler->battles->startBattle(attackerSideHero, neutral);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);

	const CStack * melee = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			melee = unit.get();
	}
	ASSERT_NE(melee, nullptr);
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto playerView = callback->getBattle(battleID);
	ASSERT_NE(playerView, nullptr);
	EXPECT_EQ(battle()->sideToPlayer(BattleSide::DEFENDER), PlayerColor::NEUTRAL);
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*playerView, *melee);
	EXPECT_NE(baseline.actionType, EActionType::DEFEND)
		<< "The HoMM3 baseline must advance for a meaningful TUM differential";
	auto policy = AIFactory::createBattleAI("TUMEnchantedTactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	policy->activeStack(battleID, melee);
	ASSERT_TRUE(callback->action.has_value());
	EXPECT_EQ(callback->action->actionType, EActionType::DEFEND)
		<< "TUM melee must protect friendly shooters before neutral contact";
	// In addition to the differential decision above, complete the battle
	// through the real server. A fabricated winner is never accepted.
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.shots, 0);
}


TEST_F(AIIntegrationNativeBattle, tumDoesNotMistakeForgottenNeutralArcherForMeleeOnly)
{
	// TUM's published hold-the-line condition requires that the enemy has
	// no shooters. Forgetfulness blocks an archer's current shots, but it
	// does not turn the enemy army into a melee-only neutral encounter.
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 65);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(attackerSideHero->id, SlotID(1)), CreatureID(2).toCreature(), 25));
	gameHandler->createWanderingMonster({9, 9, 0}, CreatureID(2), 55);
	const auto * neutral = findFirst<CGCreature>();
	ASSERT_NE(neutral, nullptr);
	ASSERT_EQ(neutral->getOwner(), PlayerColor::UNFLAGGABLE);
	teachBattleSpell(attackerSideHero, SpellID::FORGETFULNESS);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::WATER_MAGIC),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	gameHandler->battles->startBattle(attackerSideHero, neutral);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);

	const CStack * melee = nullptr;
	const CStack * enemyArcher = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			melee = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(2))
			enemyArcher = unit.get();
	}
	ASSERT_NE(melee, nullptr);
	ASSERT_NE(enemyArcher, nullptr);
	ASSERT_TRUE(enemyArcher->isShooter());
	ASSERT_TRUE(battle()->battleCanShoot(enemyArcher));
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::FORGETFULNESS, enemyArcher));
	ASSERT_FALSE(enemyArcher->canShoot())
		<< "Expert Forgetfulness must actually prevent this ranged unit from firing";
	ASSERT_TRUE(enemyArcher->isShooter())
		<< "Shot illegality must not erase the enemy's ranged unit classification";

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto playerView = callback->getBattle(battleID);
	ASSERT_NE(playerView, nullptr);
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*playerView, *melee);
	ASSERT_NE(baseline.actionType, EActionType::DEFEND)
		<< "The original tactical baseline must advance in the neutral fixture";
	auto policy = AIFactory::createBattleAI("TUMEnchantedTactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	policy->activeStack(battleID, melee);
	ASSERT_TRUE(callback->action.has_value());
	EXPECT_EQ(callback->action->actionType, baseline.actionType)
		<< "TUM must not guard shooters against an army still containing ranged units";

	// Play out the encounter through authoritative native battle actions.
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.damage, 0);
}

#ifdef ENABLE_MMAI
TEST_F(AIIntegrationNativeBattle, mmaiSelectedNeuralPolicyCannotFallBackToScriptedAI)
{
	// This CI fixture deliberately installs only public Heroes III battle
	// data, not the optional MMAI neural model mod.
	prepareBattle(CreatureID(0), 30, CreatureID(0), 20);
	ASSERT_FALSE(HasFatalFailure());
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto policy = AIFactory::createBattleAI("MMAI");
	ASSERT_NE(policy, nullptr);
	EXPECT_EQ(policy->dllName, "MMAI");
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	policy->initBattleInterface(environment, callback, AutocombatPreferences{});
	try
	{
		policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
			battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
			battle()->getSideHero(BattleSide::ATTACKER),
			battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
		FAIL() << "MMAI silently accepted a scripted model without the neural model mod";
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_NE(std::string(error.what()).find("refusing scripted fallback"), std::string::npos)
			<< "Missing MMAI model must be diagnosed rather than replaced";
	}
}

TEST_F(AIIntegrationNativeBattle, mmaiOppositeSideMetadataIsRejected)
{
	// Test the production Router side-validation seam with side metadata
	// actually read from the checksum-pinned ONNX attacker model. This is a
	// rejection test, not a fabricated combat outcome or a replacement AI.
	if(!std::getenv("VCMI_AI_MMAI_NEURAL_TEST"))
		GTEST_SKIP() << "Requires installed, checksum-verified MMAI ONNX models";
	requireInstalledNeuralRole("attacker");
	ASSERT_FALSE(HasFatalFailure());
	const auto settings = JsonUtils::assembleFromFiles("MMAI/CONFIG/mmai-settings.json");
	ASSERT_TRUE(settings["models"]["attacker"].isString());
	const auto path = "MMAI/models/" + settings["models"]["attacker"].String();
	auto model = MMAI::BAI::CreateNNModel(path);
	ASSERT_NE(model, nullptr);
	ASSERT_EQ(model->getType(), MMAI::Schema::ModelType::NN);

	const auto attackerSide = static_cast<MMAI::Schema::Side>(static_cast<int>(BattleSide::ATTACKER));
	const auto defenderSide = static_cast<MMAI::Schema::Side>(static_cast<int>(BattleSide::DEFENDER));
	ASSERT_NE(attackerSide, defenderSide);
	ASSERT_EQ(model->getSide(), attackerSide)
		<< "The release's attacker model must declare its training side";
	EXPECT_TRUE(MMAI::BAI::validateModelSide(model->getSide(), attackerSide, true, "attacker"));
	EXPECT_THROW(MMAI::BAI::validateModelSide(model->getSide(), defenderSide, true, "defender"),
		std::runtime_error) << "Strict MMAI must refuse opposite-side weights";
	EXPECT_FALSE(MMAI::BAI::validateModelSide(model->getSide(), defenderSide, false, "defender"))
		<< "Legacy hybrid routing reports a mismatch rather than pretending the roles agree";
	EXPECT_TRUE(MMAI::BAI::validateModelSide(MMAI::Schema::Side::BOTH, defenderSide, true, "defender"));
}

TEST_F(AIIntegrationNativeBattle, mmaiWithRealNeuralModelCompletesRealBattle)
{
	// Separate CI invocation installs the published, checksum-pinned VCMI 1.8 MMAI model package.
	// Its model selection must go through AIFactory and Router(true).
	// Without a neural model Router(true) throws: this must NEVER fall back.
	if(!std::getenv("VCMI_AI_MMAI_NEURAL_TEST"))
		GTEST_SKIP() << "Run separately with a real installed MMAI model mod";

	// Prove the actual model configuration and virtual filesystem resource
	// are present before testing inference. This differentiates a missing mod
	// from ONNX loading/schema failures without accepting a scripted fallback.
	const auto modelConfig = JsonUtils::assembleFromFiles("MMAI/CONFIG/mmai-settings.json");
	ASSERT_TRUE(modelConfig.isStruct()) << "MMAI neural config is not mounted in VCMI";
	ASSERT_TRUE(modelConfig["models"]["attacker"].isString())
		<< "Missing MMAI attacker model mapping: " << modelConfig.toCompactString();
	const auto path = "MMAI/models/" + modelConfig["models"]["attacker"].String();
	ASSERT_TRUE(CResourceHandler::get()->existsResource(ResourcePath(path, EResType::AI_MODEL)))
		<< "MMAI attacker model is not mounted: " << path;
	prepareBattle(CreatureID(0), 45, CreatureID(0), 40);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "MMAI", "FHeroes2Tactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_GT(mmaiUnitDecisions, 0) << "The neural attacker was never activated";
	EXPECT_GT(mmaiNonpassiveDecisions, 0) << "The neural attacker never attempted a non-passive action";
}

TEST_F(AIIntegrationNativeBattle, mmaiStrictNeuralDoesNotCastScriptedHeroSpellRealBattle)
{
	// MMAI's official ONNX action space chooses stack actions; it does not
	// contain hero spell selection. In strict mode a learned Magic Arrow
	// must NOT be cast by the unrelated scripted BattleEvaluator.
	if(!std::getenv("VCMI_AI_MMAI_NEURAL_TEST"))
		GTEST_SKIP() << "Requires installed, checksum-verified MMAI ONNX models";
	requireInstalledNeuralRole("attacker");

	prepareBattle(CreatureID(2), 20, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::MAGIC_ARROW, 20);
	const auto observer = std::make_shared<DecisionCallback>(
		battle()->sideToPlayer(BattleSide::ATTACKER));
	observer->onBattleStarted(battle());
	ASSERT_EQ(observer->getBattle(battle()->getBattleID())->battleCanCastSpell(
		attackerSideHero, spells::Mode::HERO), ESpellCastProblem::OK)
		<< "The fixture must make the scripted hero-spell fallback possible";

	// Enable autocombat spells on purpose. All actual MMAI unit decisions
	// must still go through ONNX and be accepted by VCMI's battle engine.
	runToCompletion(std::nullopt, true, "MMAI", "HoMM3OriginalTactics");
	EXPECT_EQ(heroSpellDecisions, 0) << "Strict MMAI delegated hero spells to a scripted policy";
	EXPECT_TRUE(recorder.castsOf(SpellID::MAGIC_ARROW).empty());
	EXPECT_GT(mmaiUnitDecisions, 0);
	EXPECT_GT(mmaiNonpassiveDecisions, 0);
}

TEST_F(AIIntegrationNativeBattle, mmaiDefenderNeuralModelCompletesRealBattle)
{
	// The independent defender model must also handle a real battle with
	// factory-selected strict-neural MMAI. Never accept scripted fallback.
	if(!std::getenv("VCMI_AI_MMAI_NEURAL_TEST"))
		GTEST_SKIP() << "Run separately with an installed MMAI neural model pack";

	const auto modelConfig = JsonUtils::assembleFromFiles("MMAI/CONFIG/mmai-settings.json");
	ASSERT_TRUE(modelConfig.isStruct()) << "MMAI neural config is not mounted in VCMI";
	ASSERT_TRUE(modelConfig["models"]["defender"].isString())
		<< "Missing MMAI defender model mapping: " << modelConfig.toCompactString();
	const auto path = "MMAI/models/" + modelConfig["models"]["defender"].String();
	ASSERT_TRUE(CResourceHandler::get()->existsResource(ResourcePath(path, EResType::AI_MODEL)))
		<< "MMAI defender neural model is not mounted: " << path;

	prepareBattle(CreatureID(0), 40, CreatureID(0), 45);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "MMAI");
	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_GT(mmaiUnitDecisions, 0) << "The neural defender was never activated";
	EXPECT_GT(mmaiNonpassiveDecisions, 0) << "The neural defender never attempted a non-passive action";
}

TEST_F(AIIntegrationNativeBattle, mmaiSiegeDefenderNeuralModelCompletesRealBattle)
{
	// The official MMAI 1.8 pack has a separate defender.siege neural model.
	// Verify model dispatch by fighting an actual fortified town to its result.
	if(!std::getenv("VCMI_AI_MMAI_NEURAL_TEST"))
		GTEST_SKIP() << "Run separately with an installed MMAI neural model pack";

	const auto modelConfig = JsonUtils::assembleFromFiles("MMAI/CONFIG/mmai-settings.json");
	ASSERT_TRUE(modelConfig.isStruct()) << "MMAI neural config is not mounted in VCMI";
	ASSERT_TRUE(modelConfig["models"]["defender.siege"].isString())
		<< "Missing MMAI defender.siege model mapping: " << modelConfig.toCompactString();
	const auto path = "MMAI/models/" + modelConfig["models"]["defender.siege"].String();
	ASSERT_TRUE(CResourceHandler::get()->existsResource(ResourcePath(path, EResType::AI_MODEL)))
		<< "MMAI siege neural model is not mounted: " << path;

	prepareSiege();
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(BattleSide::ATTACKER, false, "FHeroes2Tactics", "MMAI");
	EXPECT_GT(mmaiUnitDecisions, 0) << "The neural siege defender was never activated";
	EXPECT_GT(mmaiNonpassiveDecisions, 0) << "The neural siege defender never attempted a non-passive action";
	EXPECT_FALSE(recorder.catapultAttacks.empty());
}


TEST_F(AIIntegrationNativeBattle, mmaiBothNeuralSidesFightRealBattle)
{
	// This is an actual ONNX-vs-ONNX fight: a scripted opponent cannot
	// conceal a model-routing defect. Both sides must submit engine-accepted
	// commands before the native battle reaches its genuine result.
	if(!std::getenv("VCMI_AI_MMAI_NEURAL_TEST"))
		GTEST_SKIP() << "Requires installed, checksum-verified MMAI ONNX models";
	requireInstalledNeuralRole("attacker");
	requireInstalledNeuralRole("defender");
	ASSERT_FALSE(HasFatalFailure());

	// A symmetric melee duel can deadlock when both neural sides defend
	// until the strict per-model inference limit. Give one side genuine ranged
	// pressure while preserving two independently model-controlled armies.
	prepareBattle(CreatureID(2), 20, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "MMAI", "MMAI", true);
	EXPECT_GE(mmaiUnitDecisions, 2);
	EXPECT_GT(mmaiNonpassiveDecisions, 0);
}

TEST_F(AIIntegrationNativeBattle, mmaiAttackerAgainstHoMM3DefenderRealBattle)
{
	// Extend neural verification beyond FHeroes2: both different policies
	// must receive turns, submit legal actions and reach the real engine result.
	if(!std::getenv("VCMI_AI_MMAI_NEURAL_TEST"))
		GTEST_SKIP() << "Requires installed, checksum-verified MMAI ONNX models";
	requireInstalledNeuralRole("attacker");
	ASSERT_FALSE(HasFatalFailure());
	prepareBattle(CreatureID(0), 45, CreatureID(0), 40);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "MMAI", "HoMM3OriginalTactics");
	EXPECT_GT(mmaiUnitDecisions, 0);
	EXPECT_GT(mmaiNonpassiveDecisions, 0);
}

TEST_F(AIIntegrationNativeBattle, mmaiDefenderAgainstHoMM3AttackerRealBattle)
{
	if(!std::getenv("VCMI_AI_MMAI_NEURAL_TEST"))
		GTEST_SKIP() << "Requires installed, checksum-verified MMAI ONNX models";
	requireInstalledNeuralRole("defender");
	ASSERT_FALSE(HasFatalFailure());
	prepareBattle(CreatureID(0), 40, CreatureID(0), 45);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics", "MMAI");
	EXPECT_GT(mmaiUnitDecisions, 0);
	EXPECT_GT(mmaiNonpassiveDecisions, 0);
}

TEST_F(AIIntegrationNativeBattle, mmaiAttackerAgainstTUMDefenderRealBattle)
{
	if(!std::getenv("VCMI_AI_MMAI_NEURAL_TEST"))
		GTEST_SKIP() << "Requires installed, checksum-verified MMAI ONNX models";
	requireInstalledNeuralRole("attacker");
	ASSERT_FALSE(HasFatalFailure());
	prepareBattle(CreatureID(0), 45, CreatureID(0), 40);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "MMAI", "TUMEnchantedTactics");
	EXPECT_GT(mmaiUnitDecisions, 0);
	EXPECT_GT(mmaiNonpassiveDecisions, 0);
}

TEST_F(AIIntegrationNativeBattle, mmaiDefenderAgainstTUMAttackerRealBattle)
{
	if(!std::getenv("VCMI_AI_MMAI_NEURAL_TEST"))
		GTEST_SKIP() << "Requires installed, checksum-verified MMAI ONNX models";
	requireInstalledNeuralRole("defender");
	ASSERT_FALSE(HasFatalFailure());
	prepareBattle(CreatureID(0), 40, CreatureID(0), 45);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics", "MMAI");
	EXPECT_GT(mmaiUnitDecisions, 0);
	EXPECT_GT(mmaiNonpassiveDecisions, 0);
}

#endif


TEST_F(AIIntegrationNativeBattle, mixedOriginalHoMM3AttackerAgainstFHeroes2DefenderCompletesBattle)
{
	// Same creature/count fixture as the inverse policy assignment below.
	// Compare policy decisions on opposite sides without changing any
	// game rules, forcing actions, or manufacturing a winner.
	prepareBattle(CreatureID(2), 20, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics", "FHeroes2Tactics");
	EXPECT_GT(recorder.shots, 0);
}

TEST_F(AIIntegrationNativeBattle, mixedFHeroes2AttackerAgainstOriginalHoMM3DefenderCompletesBattle)
{
	prepareBattle(CreatureID(2), 20, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.shots, 0);
}

TEST_F(AIIntegrationNativeBattle, mixedTUMAttackerAgainstFHeroes2DefenderCompletesBattle)
{
	prepareBattle(CreatureID(0), 35, CreatureID(2), 20);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics", "FHeroes2Tactics");
	EXPECT_GT(recorder.shots, 0);
}

// Exercise every directed pairing of the three native source-policy ports.
// Reverse the backend assignment on the same armies to expose side-sensitive
// adapter wiring, missed activations and illegal engine actions. These are
// deterministic engine smoke tests, not a balance or win-rate benchmark.
TEST_F(AIIntegrationNativeBattle, mixedOriginalHoMM3AttackerAgainstTUMDefenderCompletesBattle)
{
	prepareBattle(CreatureID(2), 20, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics", "TUMEnchantedTactics");
	EXPECT_GT(recorder.shots, 0);
}

TEST_F(AIIntegrationNativeBattle, mixedTUMAttackerAgainstOriginalHoMM3DefenderCompletesBattle)
{
	prepareBattle(CreatureID(2), 20, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.shots, 0);
}

TEST_F(AIIntegrationNativeBattle, mixedFHeroes2AttackerAgainstTUMDefenderCompletesBattle)
{
	prepareBattle(CreatureID(0), 35, CreatureID(2), 20);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "TUMEnchantedTactics");
	EXPECT_GT(recorder.shots, 0);
}

#ifdef ENABLE_MMAI
TEST_F(AIIntegrationNativeBattle, integrationFourExperimentalBackendsRemainDistinctFactoryChoices)
{
	// This check is about wiring; the source-behavior and real battle tests
	// above remain separate. Full-port IDs are intentionally not advertised.
	for(const std::string name : {"HoMM3OriginalTactics", "FHeroes2Tactics", "TUMEnchantedTactics", "MMAI"})
	{
		auto policy = AIFactory::createBattleAI(name);
		ASSERT_NE(policy, nullptr) << "Missing experimental policy: " << name;
		EXPECT_EQ(policy->dllName, name);
	}
}
#endif
