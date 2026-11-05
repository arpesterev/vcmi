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
	int unitChangePackets = 0;
	int obstacleChangePackets = 0;
	int catapultEventPackets = 0;
	int gateChangePackets = 0;
	int triggerEffectPackets = 0;
	std::function<void(const BattleUnitsChanged &)> onUnitsChangedAfterApply;
	std::function<void(const BattleObstaclesChanged &)> onObstaclesChangedAfterApply;
	std::function<void(const CatapultAttack &)> onCatapultAfterApply;
	std::function<void(const BattleUpdateGateState &)> onGateChangedBeforeApply;
	std::function<void(const BattleTriggerEffect &)> onTriggerBeforeApply;
	std::function<void(const BattleResult &)> onResultBeforeApply;
	std::function<void(const BattleStackMoved &)> onMovementBeforeApply;
	std::function<void(const BattleNextRound &)> onRoundBeforeApply;
	std::function<void(const BattleNextRound &)> onRoundAfterApply;
	std::function<void(const BattleAttack &)> onAttackBeforeApply;
	std::function<void(const StacksInjured &)> onInjuriesBeforeApply;
	std::function<void(const BattleSpellCast &)> onSpellAfterApply;
	std::function<void(const StacksInjured &)> onInjuriesAfterApply;

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
		// Native client's ApplyFirst visitor dispatches gate and one-shot
		// creature effects before mutating the authoritative battle state.
		if(const auto * gate = dynamic_cast<const BattleUpdateGateState *>(&pack))
		{
			++gateChangePackets;
			if(onGateChangedBeforeApply)
				onGateChangedBeforeApply(*gate);
		}
		if(const auto * trigger = dynamic_cast<const BattleTriggerEffect *>(&pack))
		{
			++triggerEffectPackets;
			if(onTriggerBeforeApply)
				onTriggerBeforeApply(*trigger);
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
		// Match the real client packet visitor: structural, obstacle and siege
		// notifications run after the authoritative packet applies. This
		// matters for summoned units, clone visibility and siege routing.
		if(const auto * units = dynamic_cast<const BattleUnitsChanged *>(&pack))
		{
			++unitChangePackets;
			if(onUnitsChangedAfterApply)
				onUnitsChangedAfterApply(*units);
		}
		if(const auto * obstacles = dynamic_cast<const BattleObstaclesChanged *>(&pack))
		{
			++obstacleChangePackets;
			if(onObstaclesChangedAfterApply)
				onObstaclesChangedAfterApply(*obstacles);
		}
		if(const auto * catapult = dynamic_cast<const CatapultAttack *>(&pack))
		{
			++catapultEventPackets;
			if(onCatapultAfterApply)
				onCatapultAfterApply(*catapult);
		}
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
			if(onInjuriesAfterApply)
				onInjuriesAfterApply(*injuries);
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
	std::map<PlayerColor, std::map<EActionType, int>> lastAcceptedActionTypesByPlayer;
	std::map<int, std::map<EActionType, int>> lastAcceptedActionTypesByUnit;
	std::map<int, EActionType> lastFirstAcceptedActionTypeByUnit;
	int ballistaDecisions = 0;
	int ballistaShots = 0;
	std::optional<int> firstBallistaShotTarget;
	int ballistaDefendsWhileAllEnemiesBlind = 0;
	int heroSpellDecisions = 0;
	int mmaiUnitDecisions = 0;
	int mmaiNonpassiveDecisions = 0;
	int mmaiAutomaticDecisions = 0;
	int replayedMovementEvents = 0;
	int replayedRoundEvents = 0;
	int replayedUnitChangeEvents = 0;
	int replayedCatapultEvents = 0;
	int encounterSeed = seed; // Fixture-only RNG input for reproducible directed matchups.

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
		gameHandler->randomizer->setSeed(encounterSeed);
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

	void prepareSiege(bool includeDefenderShooter = false, CreatureID defenderType = CreatureID(0),
		int defenderCount = 30)
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
		ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(town->id, SlotID(0)), defenderType.toCreature(), defenderCount));
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
		const int unitsBeforeEventSubscription = recorder.unitChangePackets;
		const int obstaclesBeforeEventSubscription = recorder.obstacleChangePackets;
		const int catapultsBeforeEventSubscription = recorder.catapultEventPackets;
		const int gatesBeforeEventSubscription = recorder.gateChangePackets;
		const int triggersBeforeEventSubscription = recorder.triggerEffectPackets;
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
		int deliveredUnitChangeEvents = 0;
		int deliveredObstacleChangeEvents = 0;
		int deliveredCatapultEvents = 0;
		int deliveredGateChangeEvents = 0;
		int deliveredTriggerEvents = 0;
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
		recorder.onUnitsChangedAfterApply = [&](const BattleUnitsChanged & units)
		{
			for(auto & entry : policies)
				entry.second->battleUnitsChanged(units.battleID, units.changedStacks);
			++deliveredUnitChangeEvents;
		};
		recorder.onObstaclesChangedAfterApply = [&](const BattleObstaclesChanged & obstacles)
		{
			for(auto & entry : policies)
				entry.second->battleObstaclesChanged(obstacles.battleID, obstacles.change);
			++deliveredObstacleChangeEvents;
		};
		recorder.onCatapultAfterApply = [&](const CatapultAttack & catapult)
		{
			for(auto & entry : policies)
				entry.second->battleCatapultAttacked(catapult.battleID, catapult);
			++deliveredCatapultEvents;
		};
		recorder.onGateChangedBeforeApply = [&](const BattleUpdateGateState & gate)
		{
			for(auto & entry : policies)
				entry.second->battleGateStateChanged(gate.battleID, gate.state);
			++deliveredGateChangeEvents;
		};
		recorder.onTriggerBeforeApply = [&](const BattleTriggerEffect & trigger)
		{
			for(auto & entry : policies)
				entry.second->battleTriggerEffect(trigger.battleID, trigger);
			++deliveredTriggerEvents;
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
		std::map<int, std::map<EActionType, int>> acceptedActionTypesByUnit;
		std::map<int, EActionType> firstAcceptedActionTypeByUnit;
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
			const int actingUnitId = active->unitId();
			// A completed battle alone does not prove the neural side made
			// meaningful decisions; the scripted opponent could win unaided.
			const auto & activePolicy = player == attackerPlayer ? attackerAI : defenderAI;
			if(activePolicy == "MMAI" && !callback->spellAction)
			{
				// VCMI MMAI's published v13/v15 backends handle first aid,
				// catapults and arrow towers through deterministic auto-actions.
				// They are genuine MMAI actions, but NOT ONNX-selected creature
				// decisions. Keep them out of neural inference evidence.
				if(active->isFirstAidTent() || active->isCatapult() || active->isTurret())
					++mmaiAutomaticDecisions;
				else
				{
					++mmaiUnitDecisions;
					if(action.actionType != EActionType::DEFEND && action.actionType != EActionType::WAIT)
						++mmaiNonpassiveDecisions;
				}
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
			++acceptedActionTypesByUnit[actingUnitId][action.actionType];
			firstAcceptedActionTypeByUnit.try_emplace(actingUnitId, action.actionType);
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
		EXPECT_EQ(deliveredUnitChangeEvents, recorder.unitChangePackets - unitsBeforeEventSubscription)
			<< "Every native unit change must reach both source-policy adapters after state application";
		EXPECT_EQ(deliveredObstacleChangeEvents, recorder.obstacleChangePackets - obstaclesBeforeEventSubscription)
			<< "Every native obstacle change must reach both source-policy adapters";
		EXPECT_EQ(deliveredCatapultEvents, recorder.catapultEventPackets - catapultsBeforeEventSubscription)
			<< "Every native catapult event must reach both source-policy adapters";
		EXPECT_EQ(deliveredGateChangeEvents, recorder.gateChangePackets - gatesBeforeEventSubscription)
			<< "Every native gate change must reach both source-policy adapters before state application";
		EXPECT_EQ(deliveredTriggerEvents, recorder.triggerEffectPackets - triggersBeforeEventSubscription)
			<< "Every native one-shot creature effect must reach both source-policy adapters before state application";
		replayedUnitChangeEvents = deliveredUnitChangeEvents;
		replayedCatapultEvents = deliveredCatapultEvents;
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
		lastAcceptedActionTypesByPlayer = acceptedActionTypesByPlayer;
		lastAcceptedActionTypesByUnit = acceptedActionTypesByUnit;
		lastFirstAcceptedActionTypeByUnit = firstAcceptedActionTypeByUnit;
		EXPECT_TRUE(result.winner == BattleSide::ATTACKER || result.winner == BattleSide::DEFENDER);
		if(expectedWinner)
		{
			EXPECT_EQ(result.winner, *expectedWinner);
		}
		ASSERT_TRUE(battleEndDelivered) << "The terminal event was not delivered with live battle state";
		recorder.onResultBeforeApply = {};
		recorder.onUnitsChangedAfterApply = {};
		recorder.onObstaclesChangedAfterApply = {};
		recorder.onCatapultAfterApply = {};
		recorder.onGateChangedBeforeApply = {};
		recorder.onTriggerBeforeApply = {};
		recorder.onAttackBeforeApply = {};
		recorder.onInjuriesBeforeApply = {};
		recorder.onSpellAfterApply = {};
		recorder.onInjuriesAfterApply = {};
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
		std::cout << "AI_NATIVE_BATTLE_RESULT seed=" << encounterSeed
			<< " backend=" << backendName
			<< " defender_backend=" << defenderAI
			<< " attacker_decisions=" << decisionsByPlayer[attackerPlayer]
			<< " defender_decisions=" << decisionsByPlayer[defenderPlayer]
			<< " activations=" << decisions << " damage=" << recorder.damage
			<< " attacker_damage_taken=" << damageTakenBySide[BattleSide::ATTACKER]
			<< " defender_damage_taken=" << damageTakenBySide[BattleSide::DEFENDER]
			<< " native_unit_change_events=" << deliveredUnitChangeEvents
			<< " native_obstacle_change_events=" << deliveredObstacleChangeEvents
			<< " native_catapult_events=" << deliveredCatapultEvents
			<< " native_gate_change_events=" << deliveredGateChangeEvents
			<< " native_trigger_events=" << deliveredTriggerEvents
			<< " native_attack_events=" << deliveredAttackEvents
			<< " native_injury_events=" << deliveredInjuryEvents
			<< " native_spell_events=" << deliveredSpellEvents
			<< " native_movement_events=" << deliveredMovementEvents
			<< " native_round_events=" << deliveredRoundEvents
			<< " ranged_attacks=" << recorder.shots << " melee_attacks=" << recorder.meleeAttacks
			<< " spells=" << heroSpellDecisions
			<< " mmai_model_eligible_activations=" << mmaiUnitDecisions
			<< " mmai_automatic_activations=" << mmaiAutomaticDecisions
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
	EXPECT_GT(replayedCatapultEvents, 0)
		<< "Siege-machine packet must reach both experimental adapters after applying to the battle";
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

TEST_F(AIIntegrationNativeBattle, fheroes2UsesNativeHasteAndCompletesBattle)
{
	// The source-derived Haste speed valuation uses VCMI's projected
	// spell effect. Only Haste is taught to this hero, so a genuine cast
	// proves the policy can select a non-damage spell without BattleAI.
	prepareBattle(CreatureID(0), 80, CreatureID(0), 80);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::HASTE, 6);
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.castsOf(SpellID::HASTE).empty())
		<< "FHeroes2 must value native movement buffs, not only damage or resurrection";
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2UsesNativeSlowAndCompletesBattle)
{
	// A slower melee army can benefit from reducing enemy Cavalier speed.
	// This is a real status spell applied through the ordinary game handler.
	prepareBattle(CreatureID(0), 100, CreatureID(10), 12);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::SLOW, 6);
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.castsOf(SpellID::SLOW).empty())
		<< "FHeroes2 must evaluate projected enemy movement loss";
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2UsesNativeDisruptingRayAndCompletesBattle)
{
	// Pinned fheroes2 getSpellDisruptingRayRatio values a real defense loss,
	// not damage inflicted by the spell. Only this spell is learned; the
	// actual engine must accept the cast and return a genuine battle result.
	prepareBattle(CreatureID(0), 80, CreatureID(0), 80);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::DISRUPTING_RAY, 6);
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.castsOf(SpellID::DISRUPTING_RAY).empty())
		<< "FHeroes2 must select projected defense reduction without scripted BattleAI";
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2UsesNativeBlessAndCompletesBattle)
{
	// A real VCMI hypothetical Bless must activate Pikeman's maximum-
	// damage roll before this partial fheroes2 policy assigns any value.
	// No separate scripted BattleAI spell decision is involved.
	prepareBattle(CreatureID(0), 80, CreatureID(0), 80);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::BLESS, 6);
	const CStack * blessed = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			blessed = unit.get();
	ASSERT_NE(blessed, nullptr);
	ASSERT_FALSE(blessed->hasBonusOfType(BonusType::ALWAYS_MAXIMUM_DAMAGE));
	bool observedRealBuff = false;
	// BattleSpellCast is emitted *before* SetStackEffect in VCMI.
	// A post-apply StacksInjured packet (including the spell's terminal
	// empty packet) is emitted after all spell effects have been applied.
	recorder.onInjuriesAfterApply = [&](const StacksInjured &)
	{
		if(!recorder.castsOf(SpellID::BLESS).empty())
			observedRealBuff |= blessed->hasBonusOfType(BonusType::ALWAYS_MAXIMUM_DAMAGE);
	};
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_TRUE(observedRealBuff) << "Bless must really force the native maximum-damage roll";
	EXPECT_FALSE(recorder.castsOf(SpellID::BLESS).empty())
		<< "FHeroes2 must cast a source-valued Bless through the actual engine";
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2UsesNativeCurseAndCompletesBattle)
{
	// The enemy's native minimum-damage roll mode is imposed by Curse;
	// the source ratio is applied only when the projection detects it.
	prepareBattle(CreatureID(0), 80, CreatureID(0), 80);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::CURSE, 6);
	const CStack * cursed = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			cursed = unit.get();
	ASSERT_NE(cursed, nullptr);
	ASSERT_FALSE(cursed->hasBonusOfType(BonusType::ALWAYS_MINIMUM_DAMAGE));
	bool observedRealDebuff = false;
	recorder.onInjuriesAfterApply = [&](const StacksInjured &)
	{
		if(!recorder.castsOf(SpellID::CURSE).empty())
			observedRealDebuff |= cursed->hasBonusOfType(BonusType::ALWAYS_MINIMUM_DAMAGE);
	};
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_TRUE(observedRealDebuff) << "Curse must really force the native minimum-damage roll";
	EXPECT_FALSE(recorder.castsOf(SpellID::CURSE).empty())
		<< "FHeroes2 must cast a source-valued Curse through the actual engine";
	EXPECT_GT(recorder.damage, 0);
}

// A fheroes2 Dispel scores a beneficial enemy Bless through its existing
// 0.15 spellEffectValue, but only when the real VCMI spell effect and forced
// maximum-damage roll are removed. Verify the independently selected Dispel
// in a complete battle. No engine rules or stack outcomes are overridden.
TEST_F(AIIntegrationNativeBattle, fheroes2DispelsEnemyBlessAndCompletesBattle)
{
	prepareBattle(CreatureID(0), 80, CreatureID(0), 80);
	ASSERT_FALSE(HasFatalFailure());
	const CStack * enemy = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			enemy = unit.get();
	ASSERT_NE(enemy, nullptr);
	const auto initialHealth = enemy->getAvailableHealth();
	teachBattleSpell(defenderSideHero, SpellID::BLESS, 6);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::BLESS, enemy))
		<< "The enemy hero must successfully apply Bless via the game server";
	const auto bless = Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLESS)));
	ASSERT_TRUE(enemy->hasBonus(bless));
	ASSERT_TRUE(enemy->hasBonusOfType(BonusType::ALWAYS_MAXIMUM_DAMAGE));
	ASSERT_EQ(enemy->getAvailableHealth(), initialHealth)
		<< "The Dispel decision must value removal of a buff, not damage";
	teachBattleSpell(attackerSideHero, SpellID::DISPEL, 6);
	// VCMI requires Advanced Water Magic to target an enemy with Dispel;
	// avoid bypassing the real spell legality gate.
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::WATER_MAGIC),
		MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	bool removedRealBless = false;
	recorder.onInjuriesAfterApply = [&](const StacksInjured &)
	{
		if(!recorder.castsOf(SpellID::DISPEL).empty())
			removedRealBless |= !enemy->hasBonus(bless)
				&& !enemy->hasBonusOfType(BonusType::ALWAYS_MAXIMUM_DAMAGE);
	};
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.castsOf(SpellID::DISPEL).empty())
		<< "FHeroes2 must choose Dispel against a genuinely blessed enemy";
	EXPECT_TRUE(removedRealBless)
		<< "The authoritative server must remove native Bless and its damage roll";
	EXPECT_GT(recorder.damage, 0);
}

// Dispel must value the enemy's real Stone Skin defense bonus using the
// pinned fheroes2 spellDispelValue, not a synthetic score. Native VCMI
// targeting and effect removal are checked in a complete server battle.
TEST_F(AIIntegrationNativeBattle, fheroes2DispelsEnemyStoneSkinAndCompletesBattle)
{
	prepareBattle(CreatureID(0), 80, CreatureID(0), 80);
	ASSERT_FALSE(HasFatalFailure());
	const CStack * enemy = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			enemy = unit.get();
	ASSERT_NE(enemy, nullptr);
	const int baseDefense = enemy->getDefense(false);
	const auto originalHealth = enemy->getAvailableHealth();
	teachBattleSpell(defenderSideHero, SpellID::STONE_SKIN, 6);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::STONE_SKIN, enemy));
	const auto stoneSkin = Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::STONE_SKIN)));
	ASSERT_TRUE(enemy->hasBonus(stoneSkin));
	ASSERT_GT(enemy->getDefense(false), baseDefense);
	ASSERT_EQ(enemy->getAvailableHealth(), originalHealth);
	teachBattleSpell(attackerSideHero, SpellID::DISPEL, 6);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::WATER_MAGIC),
		MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	bool observedRemoval = false;
	recorder.onInjuriesAfterApply = [&](const StacksInjured &)
	{
		if(!recorder.castsOf(SpellID::DISPEL).empty())
			observedRemoval |= !enemy->hasBonus(stoneSkin) && enemy->getDefense(false) == baseDefense;
	};
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.castsOf(SpellID::DISPEL).empty())
		<< "FHeroes2 must independently choose Dispel against enemy Stone Skin";
	EXPECT_TRUE(observedRemoval)
		<< "The real server must remove Stone Skin and restore enemy defense";
	EXPECT_GT(recorder.damage, 0);
}

// Bloodlust is another positive enemy effect in the pinned fheroes2
// dispel policy. Require actual native melee-attack restoration following
// an engine-accepted Dispel, then a completed non-synthetic battle.
TEST_F(AIIntegrationNativeBattle, fheroes2DispelsEnemyBloodlustAndCompletesBattle)
{
	prepareBattle(CreatureID(0), 80, CreatureID(0), 80);
	ASSERT_FALSE(HasFatalFailure());
	const CStack * enemy = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			enemy = unit.get();
	ASSERT_NE(enemy, nullptr);
	const int baseAttack = enemy->getAttack(false);
	const auto originalHealth = enemy->getAvailableHealth();
	teachBattleSpell(defenderSideHero, SpellID::BLOODLUST, 6);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::BLOODLUST, enemy));
	const auto bloodlust = Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLOODLUST)));
	ASSERT_TRUE(enemy->hasBonus(bloodlust));
	ASSERT_GT(enemy->getAttack(false), baseAttack);
	ASSERT_EQ(enemy->getAvailableHealth(), originalHealth);
	teachBattleSpell(attackerSideHero, SpellID::DISPEL, 6);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::WATER_MAGIC),
		MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	bool observedRemoval = false;
	recorder.onInjuriesAfterApply = [&](const StacksInjured &)
	{
		if(!recorder.castsOf(SpellID::DISPEL).empty())
			observedRemoval |= !enemy->hasBonus(bloodlust) && enemy->getAttack(false) == baseAttack;
	};
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.castsOf(SpellID::DISPEL).empty())
		<< "FHeroes2 must independently choose Dispel against enemy Bloodlust";
	EXPECT_TRUE(observedRemoval)
		<< "The real server must remove Bloodlust and restore enemy melee attack";
	EXPECT_GT(recorder.damage, 0);
}

// The original fheroes2 spellDispelValue counts an enemy's protective
// Shield as a positive effect to remove. Heroes III/VCMI uses AIR_SHIELD
// for ranged protection; require an actual spell and mitigation removal,
// not a synthetic decision or a made-up Heroes II combat rule.
TEST_F(AIIntegrationNativeBattle, fheroes2DispelsEnemyRangedShieldAndCompletesBattle)
{
	prepareBattle(CreatureID(2), 80, CreatureID(2), 80);
	ASSERT_FALSE(HasFatalFailure());
	const CStack * enemy = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(2))
			enemy = unit.get();
	ASSERT_NE(enemy, nullptr);
	const auto originalHealth = enemy->getAvailableHealth();
	teachBattleSpell(defenderSideHero, SpellID::AIR_SHIELD, 6);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::AIR_SHIELD, enemy));
	const auto airShield = Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::AIR_SHIELD)));
	ASSERT_TRUE(enemy->hasBonus(airShield));
	ASSERT_TRUE(enemy->hasBonusOfType(BonusType::GENERAL_DAMAGE_REDUCTION));
	ASSERT_EQ(enemy->getAvailableHealth(), originalHealth)
		<< "Dispel must be selected because of protection, not missing health";
	teachBattleSpell(attackerSideHero, SpellID::DISPEL, 6);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::WATER_MAGIC),
		MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	bool removedRealProtection = false;
	recorder.onInjuriesAfterApply = [&](const StacksInjured &)
	{
		if(!recorder.castsOf(SpellID::DISPEL).empty())
			removedRealProtection |= !enemy->hasBonus(airShield)
				&& !enemy->hasBonusOfType(BonusType::GENERAL_DAMAGE_REDUCTION);
	};
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.castsOf(SpellID::DISPEL).empty())
		<< "FHeroes2 must independently choose Dispel against ranged protection";
	EXPECT_TRUE(removedRealProtection)
		<< "The authoritative server must remove Air Shield's ranged mitigation";
	EXPECT_GT(recorder.damage, 0);
}

// fheroes2 spellDispelValue values removing an enemy's beneficial magic.
// Dispel must be the policy's own hero decision, remove a real enemy Haste
// through authoritative server packets and complete combat with damage.
TEST_F(AIIntegrationNativeBattle, fheroes2DispelsEnemyHasteAndCompletesBattle)
{
	prepareBattle(CreatureID(0), 80, CreatureID(0), 80);
	ASSERT_FALSE(HasFatalFailure());
	const CStack * enemy = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			enemy = unit.get();
	ASSERT_NE(enemy, nullptr);
	const auto baseSpeed = enemy->getMovementRange();
	const auto baseHealth = enemy->getAvailableHealth();
	teachBattleSpell(defenderSideHero, SpellID::HASTE, 6);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::HASTE, enemy))
		<< "The server must actually give the enemy Haste";
	const auto haste = Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::HASTE)));
	ASSERT_TRUE(enemy->hasBonus(haste));
	ASSERT_GT(enemy->getMovementRange(), baseSpeed);
	ASSERT_EQ(enemy->getAvailableHealth(), baseHealth)
		<< "Dispel must be valued as magic removal, not damage";
	teachBattleSpell(attackerSideHero, SpellID::DISPEL, 6);
	// Basic Water Magic marks Dispel as a smart *friendly-only* spell in
	// native VCMI. Advanced Water Magic enables deliberate enemy targeting.
	// The first CI fixture failed because it omitted this real rule gate.
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::WATER_MAGIC),
		MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	bool sawNativeDispel = false;
	recorder.onInjuriesAfterApply = [&](const StacksInjured &)
	{
		if(!recorder.castsOf(SpellID::DISPEL).empty())
			sawNativeDispel |= !enemy->hasBonus(haste) && enemy->getMovementRange() == baseSpeed;
	};
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.castsOf(SpellID::DISPEL).empty())
		<< "FHeroes2 must choose Dispel against the real enemy Haste";
	EXPECT_TRUE(sawNativeDispel)
		<< "The authoritative engine must restore enemy movement, not just log a cast";
	EXPECT_GT(recorder.damage, 0);
}

// This is a real Cure cast, not a manufactured healing score. fheroes2's
// spellDispelValue assigns positive value to dispelling harmful magic.
// Restore a cursed but initially unwounded friendly stack and verify the
// native VCMI status actually disappears after the effect packets.
TEST_F(AIIntegrationNativeBattle, fheroes2CuresNativeCurseAndCompletesBattle)
{
	prepareBattle(CreatureID(0), 80, CreatureID(0), 80);
	ASSERT_FALSE(HasFatalFailure());
	const CStack * friendly = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			friendly = unit.get();
	ASSERT_NE(friendly, nullptr);
	teachBattleSpell(defenderSideHero, SpellID::CURSE, 6);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::CURSE, friendly))
		<< "The server must apply the initial enemy Curse";
	const auto cursed = Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::CURSE)));
	ASSERT_TRUE(friendly->hasBonus(cursed));
	ASSERT_TRUE(friendly->hasBonusOfType(BonusType::ALWAYS_MINIMUM_DAMAGE));
	teachBattleSpell(attackerSideHero, SpellID::CURE, 6);
	bool sawNativeDispel = false;
	recorder.onInjuriesAfterApply = [&](const StacksInjured &)
	{
		if(!recorder.castsOf(SpellID::CURE).empty())
			sawNativeDispel |= !friendly->hasBonus(cursed)
				&& !friendly->hasBonusOfType(BonusType::ALWAYS_MINIMUM_DAMAGE);
	};
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.castsOf(SpellID::CURE).empty())
		<< "Source-derived Cure priority must select a real hero spell";
	EXPECT_TRUE(sawNativeDispel)
		<< "The game engine must remove the hostile spell, not just report a cast";
	EXPECT_GT(recorder.damage, 0);
}

// Heroes II assigns positive spellDispelValue to freeing a blind ally even
// without any missing hit points. Verify the VCMI Cure projection against a
// *real* enemy Blind, followed by an engine-accepted FHeroes2 Cure and a
// complete combat. This exercises the existing Cure/Blind branch separately
// from the already verified Cure/Curse branch.
TEST_F(AIIntegrationNativeBattle, fheroes2CuresNativeBlindAndCompletesBattle)
{
	prepareBattle(CreatureID(0), 80, CreatureID(0), 80);
	ASSERT_FALSE(HasFatalFailure());
	const CStack * friendly = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			friendly = unit.get();
	ASSERT_NE(friendly, nullptr);
	const auto healthBefore = friendly->getAvailableHealth();
	teachBattleSpell(defenderSideHero, SpellID::BLIND, 6);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::BLIND, friendly))
		<< "The server must apply the initial enemy Blind";
	const auto blind = Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLIND)));
	ASSERT_TRUE(friendly->hasBonus(blind));
	// VCMI isFrozen() represents Stone Gaze, not Blind. Blind gives
	// NOT_ACTIVE and must prevent this living stack from taking a turn.
	ASSERT_TRUE(friendly->hasBonusOfType(BonusType::NOT_ACTIVE));
	ASSERT_FALSE(friendly->canMove());
	ASSERT_EQ(friendly->getAvailableHealth(), healthBefore)
		<< "Cure must have value from dispelling Blind, not prior injury";
	teachBattleSpell(attackerSideHero, SpellID::CURE, 6);
	bool sawNativeDispel = false;
	recorder.onInjuriesAfterApply = [&](const StacksInjured &)
	{
		if(!recorder.castsOf(SpellID::CURE).empty())
			sawNativeDispel |= !friendly->hasBonus(blind) && friendly->canMove();
	};
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.castsOf(SpellID::CURE).empty())
		<< "FHeroes2 must choose Cure for Blind, even without wounded units";
	EXPECT_TRUE(sawNativeDispel)
		<< "The real game engine must remove Blind, not just accept Cure";
	EXPECT_GT(recorder.damage, 0);
}

// fheroes2 spellDispelValue recognizes Slow as harmful magic. A real Cure
// must restore mobility, even when an allied flying stack has taken no damage.
TEST_F(AIIntegrationNativeBattle, fheroes2CuresNativeSlowAndCompletesBattle)
{
	prepareBattle(CreatureID(4), 80, CreatureID(4), 80);
	ASSERT_FALSE(HasFatalFailure());
	const CStack * friendly = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(4))
			friendly = unit.get();
	ASSERT_NE(friendly, nullptr);
	ASSERT_TRUE(friendly->hasBonusOfType(BonusType::FLYING));
	const auto originalSpeed = friendly->getMovementRange();
	const auto originalHealth = friendly->getAvailableHealth();
	teachBattleSpell(defenderSideHero, SpellID::SLOW, 6);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::SLOW, friendly))
		<< "The real enemy hero must apply Slow to the flying ally";
	const auto slow = Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::SLOW)));
	ASSERT_TRUE(friendly->hasBonus(slow));
	ASSERT_LT(friendly->getMovementRange(), originalSpeed);
	ASSERT_EQ(friendly->getAvailableHealth(), originalHealth)
		<< "The Cure priority must value a debuff, not pre-existing damage";
	teachBattleSpell(attackerSideHero, SpellID::CURE, 6);
	bool observedNativeSpeedRecovery = false;
	recorder.onInjuriesAfterApply = [&](const StacksInjured &)
	{
		if(!recorder.castsOf(SpellID::CURE).empty())
			observedNativeSpeedRecovery |= !friendly->hasBonus(slow)
				&& friendly->getMovementRange() == originalSpeed;
	};
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.castsOf(SpellID::CURE).empty())
		<< "FHeroes2 must select Cure for a real Slow, not just lost HP";
	EXPECT_TRUE(observedNativeSpeedRecovery)
		<< "The game engine must restore the unit's movement range";
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2UsesNativeStoneSkinAndCompletesBattle)
{
	// The source values Stone Skin as a defensive buff. Observe the real
	// VCMI defense change after its effect packets, not merely a spellcast.
	prepareBattle(CreatureID(0), 80, CreatureID(0), 80);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::STONE_SKIN, 6);
	const CStack * protectedStack = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			protectedStack = unit.get();
	ASSERT_NE(protectedStack, nullptr);
	const int originalDefense = protectedStack->getDefense(false);
	bool sawRealDefenseGain = false;
	recorder.onInjuriesAfterApply = [&](const StacksInjured &)
	{
		if(!recorder.castsOf(SpellID::STONE_SKIN).empty())
			sawRealDefenseGain |= protectedStack->getDefense(false) > originalDefense;
	};
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.castsOf(SpellID::STONE_SKIN).empty())
		<< "FHeroes2 must select Stone Skin without the scripted BattleAI";
	EXPECT_TRUE(sawRealDefenseGain) << "Native Stone Skin must increase actual defense";
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2UsesNativeRangedShieldAndCompletesBattle)
{
	// Heroes II Shield values the enemy ranged contingent. VCMI's distinct
	// Air Shield reduces ranged damage, while Heroes III Shield is melee-only.
	// Exercise the actual analogous native effect and an accepted server cast.
	prepareBattle(CreatureID(0), 80, CreatureID(2), 80);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::AIR_SHIELD, 6);
	const CStack * protectedStack = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			protectedStack = unit.get();
	ASSERT_NE(protectedStack, nullptr);
	const auto effect = Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::AIR_SHIELD)));
	ASSERT_FALSE(protectedStack->hasBonus(effect));
	bool observedRangedProtection = false;
	recorder.onInjuriesAfterApply = [&](const StacksInjured &)
	{
		if(!recorder.castsOf(SpellID::AIR_SHIELD).empty())
			observedRangedProtection |= protectedStack->hasBonus(effect)
				&& protectedStack->hasBonusOfType(BonusType::GENERAL_DAMAGE_REDUCTION);
	};
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.castsOf(SpellID::AIR_SHIELD).empty())
		<< "FHeroes2 must choose ranged protection against actual archers";
	EXPECT_TRUE(observedRangedProtection)
		<< "The real VCMI server must apply ranged damage mitigation";
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2UsesNativeBloodlustAndCompletesBattle)
{
	// Source-derived 10% Bloodlust valuation must be gated on a real
	// VCMI melee attack gain, then accepted by the real server.
	prepareBattle(CreatureID(0), 80, CreatureID(0), 80);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::BLOODLUST, 6);
	const CStack * buffed = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			buffed = unit.get();
	ASSERT_NE(buffed, nullptr);
	const int originalMeleeAttack = buffed->getAttack(false);
	bool observedRealMeleeGain = false;
	recorder.onInjuriesAfterApply = [&](const StacksInjured &)
	{
		if(!recorder.castsOf(SpellID::BLOODLUST).empty())
			observedRealMeleeGain |= buffed->getAttack(false) > originalMeleeAttack;
	};
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.castsOf(SpellID::BLOODLUST).empty())
		<< "FHeroes2 must select Bloodlust without the scripted BattleAI";
	EXPECT_TRUE(observedRealMeleeGain) << "Native Bloodlust must improve melee attack";
	EXPECT_GT(recorder.damage, 0);
}

// Pinned Heroes II MIRRORIMAGE evaluates the newly created duplicate,
// with an archery-specific source ratio. VCMI's CLONE is the native
// projection. No mock spawn or substituted spell decision is allowed.
TEST_F(AIIntegrationNativeBattle, fheroes2UsesNativeCloneAndCompletesBattle)
{
	prepareBattle(CreatureID(2), 20, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::CLONE, 20);
	// Check spell legality and hypothetical native spawn independently
	// before judging FHeroes2's chosen action.
	const auto casterPlayer = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto probeCallback = std::make_shared<DecisionCallback>(casterPlayer);
	probeCallback->onBattleStarted(battle());
	const auto realBattle = probeCallback->getBattle(battle()->getBattleID());
	ASSERT_NE(realBattle, nullptr);
	auto probeEnvironment = std::make_shared<NativePortEnvironment>(makeCallback(casterPlayer), probeCallback);
	const CStack * original = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(2))
			original = unit.get();
	ASSERT_NE(original, nullptr);
	bool foundCloneSpell = false;
	for(const auto & entry : LIBRARY->spellh->objects)
	{
		if(entry->id != SpellID::CLONE)
			continue;
		foundCloneSpell = true;
		EXPECT_TRUE(entry->isCombat()) << "Clone is not recognized as a combat spell";
		EXPECT_FALSE(entry->isCreatureAbility()) << "Clone is unexpectedly a creature-only spell";
		EXPECT_TRUE(entry->canBeCast(realBattle.get(), spells::Mode::HERO, attackerSideHero))
			<< "Clone cannot be cast by the trained fixture hero";
		spells::BattleCast legality(realBattle.get(), attackerSideHero, spells::Mode::HERO, entry.get());
		const auto mechanics = entry->battleMechanics(&legality);
		EXPECT_EQ(mechanics->getTargetTypes().size(), 1u)
			<< "Clone's targeting shape differs from the current spell enumerator";
		spells::Target target{battle::Destination(original->getPosition())};
		EXPECT_TRUE(mechanics->canBeCastAt(target)) << "Native Clone cannot target this Archer";
		auto simulation = std::make_shared<HypotheticBattle>(probeEnvironment.get(), realBattle);
		spells::BattleCast projectedCast(simulation.get(), attackerSideHero, spells::Mode::HERO, entry.get());
		projectedCast.castEval(simulation->getServerCallback(), target);
		const auto projected = simulation->battleGetUnitsIf([](const battle::Unit * unit) { return !unit->isGhost(); });
		std::ostringstream cloneProbe;
		for(const auto & [id, state] : simulation->stackStates)
			cloneProbe << " state[id=" << id << " clone=" << state->isClone()
				<< " alive=" << state->alive() << " ownerIsCaster="
				<< (simulation->battleGetOwner(state.get()) == casterPlayer) << "]";
		for(const auto * unit : projected)
			cloneProbe << " visible[id=" << unit->unitId() << " clone=" << unit->isClone()
				<< " alive=" << unit->alive() << " ownerIsCaster="
				<< (simulation->battleGetOwner(unit) == casterPlayer) << "]";
		const auto cloneCount = std::count_if(projected.begin(), projected.end(),
			[&](const auto * unit) { return unit->alive() && unit->isClone() && simulation->battleGetOwner(unit) == casterPlayer; });
		EXPECT_GT(cloneCount, 0)
			<< "Clone projection produced no friendly clone; mutated unit states: " << simulation->stackStates.size()
			<< ", visible stacks: " << projected.size() << "; " << cloneProbe.str();
	}
	EXPECT_TRUE(foundCloneSpell) << "No native Clone spell registered";
	bool sawNativeClone = false;
	recorder.onInjuriesAfterApply = [&](const StacksInjured &)
	{
		if(recorder.castsOf(SpellID::CLONE).empty())
			return;
		for(const auto & unit : battle()->stacks)
			if(unit->alive() && unit->unitSide() == BattleSide::ATTACKER && unit->isClone())
				sawNativeClone = true;
	};
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.castsOf(SpellID::CLONE).empty())
		<< "FHeroes2 must select a native Clone from an observed projection";
	EXPECT_TRUE(sawNativeClone)
		<< "The VCMI server must create an actual friendly clone";
	EXPECT_GT(replayedUnitChangeEvents, 0)
		<< "Clone creation must notify both experimental adapters through the native unit-change event";
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2UsesNativeHypnotizeAndCompletesBattle)
{
	// Keep a second defending stack alive: an effective control transfer
	// must be observed after native packets, and the remaining enemies
	// must still be defeated by legal engine actions.
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 80);
	setArmy(defenderSideHero, CreatureID(0), 20);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(defenderSideHero->id, SlotID(1)), CreatureID(0).toCreature(), 30));
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	teachBattleSpell(attackerSideHero, SpellID::HYPNOTIZE, 30);
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	bool sawControlTransfer = false;
	recorder.onInjuriesAfterApply = [&](const StacksInjured &)
	{
		if(recorder.castsOf(SpellID::HYPNOTIZE).empty())
			return;
		for(const auto & unit : battle()->stacks)
			if(unit->unitSide() == BattleSide::DEFENDER && unit->isHypnotized()
				&& battle()->battleGetOwner(unit.get()) == player)
				sawControlTransfer = true;
	};
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.castsOf(SpellID::HYPNOTIZE).empty())
		<< "FHeroes2 must select Hypnotize from a real control projection";
	EXPECT_TRUE(sawControlTransfer)
		<< "The native game engine must transfer control of a defending stack";
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2UsesNativeBlindAndCompletesBattle)
{
	// The pinned fheroes2 spell policy values Blind only when a real
	// hypothetical cast adds the effect. Observe the actual status
	// after engine packets and complete combat with real server actions.
	prepareBattle(CreatureID(0), 80, CreatureID(0), 80);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::BLIND, 6);
	const CStack * blinded = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			blinded = unit.get();
	ASSERT_NE(blinded, nullptr);
	const auto blindSelector = Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLIND)));
	ASSERT_FALSE(blinded->hasBonus(blindSelector));
	bool observedRealBlind = false;
	recorder.onInjuriesAfterApply = [&](const StacksInjured &)
	{
		if(!recorder.castsOf(SpellID::BLIND).empty())
			observedRealBlind |= blinded->hasBonus(blindSelector);
	};
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.castsOf(SpellID::BLIND).empty())
		<< "FHeroes2 must select Blind without scripted BattleAI";
	EXPECT_TRUE(observedRealBlind) << "Engine must apply the real native Blind effect";
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2UsesNativeBerserkAndCompletesBattle)
{
	// fheroes2 only values Berserker when at least two enemy stacks remain.
	// A real VCMI location spell must newly impose ATTACKS_NEAREST_CREATURE
	// on an enemy before the independent policy assigns its control value.
	EXPECT_DOUBLE_EQ(AIIntegration::Ports::FHeroes2::berserkControlRatio(
		false, true, 1, false, true), 0.85);
	EXPECT_DOUBLE_EQ(AIIntegration::Ports::FHeroes2::berserkControlRatio(
		false, false, 5, false, true), 0.17);
	EXPECT_DOUBLE_EQ(AIIntegration::Ports::FHeroes2::berserkControlRatio(
		true, true, 1, false, true), 0.0);
	EXPECT_DOUBLE_EQ(AIIntegration::Ports::FHeroes2::berserkControlRatio(
		false, true, 1, true, true), 0.0);
	EXPECT_DOUBLE_EQ(AIIntegration::Ports::FHeroes2::berserkControlRatio(
		false, true, 1, false, false), 0.0);

	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 80);
	setArmy(defenderSideHero, CreatureID(0), 70);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(defenderSideHero->id, SlotID(1)), CreatureID(2).toCreature(), 20));
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::BERSERK, 6);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	std::vector<const CStack *> enemies;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::DEFENDER && unit->alive())
			enemies.push_back(unit.get());
	ASSERT_EQ(enemies.size(), 2u);
	const auto effect = Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BERSERK)));
	for(const auto * enemy : enemies)
		EXPECT_FALSE(enemy->hasBonus(effect));
	bool observedRealBerserk = false;
	recorder.onInjuriesAfterApply = [&](const StacksInjured &)
	{
		if(recorder.castsOf(SpellID::BERSERK).empty())
			return;
		for(const auto * enemy : enemies)
			if(enemy->alive())
				observedRealBerserk |= enemy->hasBonus(effect)
					&& enemy->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE);
	};
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.castsOf(SpellID::BERSERK).empty())
		<< "FHeroes2 must select a real enemy-control spell rather than a scripted fallback";
	EXPECT_TRUE(observedRealBerserk)
		<< "The actual VCMI server must apply Berserk before the next creature action";
	EXPECT_GT(recorder.damage, 0);
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

TEST_F(AIIntegrationNativeBattle, fheroes2HypnotizedMeleeDoesNotCoverControllerArchers)
{
	// Source meleeUnitDefense protects archers only when the melee unit
	// still belongs to its original army. Hypnotize changes the controller
	// without changing the source army. Test a real VCMI spell, not a mock
	// owner flag, and require a legal resulting unit action.
	prepareBattle(CreatureID(0), 10, CreatureID(0), 8);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * guard = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			guard = unit.get();
	ASSERT_NE(guard, nullptr);
	const BattleHex initial = guard->getPosition();
	const BattleHex shooterHex(initial.toInt() + 3 * GameConstants::BFIELD_WIDTH);
	const BattleHex enemyHex(shooterHex.toInt() + 5);
	ASSERT_TRUE(shooterHex.isAvailable());
	ASSERT_TRUE(enemyHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(shooterHex), nullptr);
	ASSERT_EQ(battle()->battleGetUnitByPos(enemyHex), nullptr);
	const CStack * shooter = addStack(BattleSide::DEFENDER, CreatureID(2), shooterHex, 120);
	const CStack * enemy = addStack(BattleSide::ATTACKER, CreatureID(0), enemyHex, 45);
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(enemy, nullptr);
	ASSERT_GT(BattleHex::getDistance(initial, enemyHex),
		static_cast<int>(guard->getMovementRange()) + 1)
		<< "Guard must not have a competing immediate melee attack";
	ASSERT_LE(BattleHex::getDistance(shooterHex, enemyHex),
		static_cast<int>(enemy->getMovementRange()) + 1)
		<< "The foreign archer must be threatened for this regression";

	teachBattleSpell(defenderSideHero, SpellID::HYPNOTIZE, 20);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::HYPNOTIZE, guard));
	ASSERT_TRUE(guard->isHypnotized());
	const auto player = battle()->sideToPlayer(BattleSide::DEFENDER);
	ASSERT_NE(guard->unitOwner(), player);
	ASSERT_EQ(battle()->battleGetOwner(guard), player)
		<< "The actual spell must transfer tactical control";
	ASSERT_TRUE(shooter->isShooter());
	// Select this live stack's activation. As with the fixture's act(),
	// the test changes turn scheduling only, never combat/game rules.
	battle()->activeStack = guard->unitId();
	const auto battleID = battle()->getBattleID();
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
	policy->activeStack(battleID, guard);
	ASSERT_TRUE(callback->action.has_value());
	EXPECT_EQ(callback->action->actionType, EActionType::DEFEND)
		<< "A hypnotized enemy may not reposition to screen its controller's archers: "
		<< callback->action->toString();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Engine rejected the hypnotized unit's defensive command";
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics", "FHeroes2Tactics");
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

TEST_F(AIIntegrationNativeBattle, homm3JousterPrefersLongerEqualValueMeleeRoute)
{
	// The pinned HoMM3 check_adjacent_hexes prefers a *longer* same-turn
	// approach for Cavalier/Champion when attack value is otherwise tied.
	// A single nearby Peasant is killed outright from any legal attack hex,
	// keeping exchange value equal while native path costs differ. The far
	// defender ensures the real game continues after the first accepted hit.
	prepareBattle(CreatureID(10), 2, CreatureID(0), 1);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * cavalier = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(10))
			cavalier = unit.get();
	ASSERT_NE(cavalier, nullptr);
	ASSERT_TRUE(cavalier->hasBonusOfType(BonusType::JOUSTING));
	const BattleHex nearbyHex(cavalier->getPosition().toInt() + 5);
	ASSERT_TRUE(nearbyHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(nearbyHex), nullptr);
	const CStack * nearbyPeasant = addStack(BattleSide::DEFENDER, CreatureID(0), nearbyHex, 1);
	ASSERT_NE(nearbyPeasant, nullptr);

	const auto reach = battle()->getReachability(cavalier);
	const auto available = battle()->battleGetAvailableHexes(reach, cavalier, false);
	int shortest = std::numeric_limits<int>::max();
	int longest = -1;
	for(const auto & hex : available)
	{
		if(!hex.isAvailable() || battle()->meleeAttackHexes(cavalier, nearbyPeasant, hex).empty())
			continue;
		const int cost = reach.distances[hex.toInt()];
		if(cost < 0)
			continue;
		shortest = std::min(shortest, cost);
		longest = std::max(longest, cost);
	}
	ASSERT_LT(shortest, longest) << "The native fixture needs two legal equal-kill routes with distinct costs";

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto policy = AIFactory::createBattleAI("HoMM3OriginalTactics");
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
	ASSERT_EQ(battle()->battleActiveUnit(), cavalier);
	policy->activeStack(battleID, cavalier);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(callback->action->target.size(), 2u);
	const BattleHex from = callback->action->target.front().hexValue;
	const BattleHex destination = callback->action->target[1].hexValue;
	ASSERT_TRUE(from.isValid());
	EXPECT_EQ(reach.distances[from.toInt()], longest)
		<< "The first native Cavalier strike must use the longer equal-value approach";
	EXPECT_EQ(battle()->battleGetUnitByPos(destination), nearbyPeasant);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "VCMI rejected the source-derived Cavalier charge";
	policy.reset();
	// The first Cavalier decision was already submitted to the real engine
	// before the continuation harness begins counting policy activations.
	// This is a source-policy regression, not a mixed-AI matchup: continue
	// with HoMM3 on both sides rather than claiming an unobserved first turn.
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

TEST_F(AIIntegrationNativeBattle, fheroes2WideCastleDefenderKeepsShelteredStanceInRealSiege)
{
	// The pinned fheroes2 defended-area predicate uses BOTH wide-unit
	// occupied cells and castle geometry. Confirm the VCMI projection uses
	// a genuine castle garrison and that ordinary AI/engine actions finish.
	prepareSiege(true, CreatureID(10), 6);
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_EQ(battle()->battleGetGateState(), EGateState::CLOSED);
	const CStack * guard = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->alive() && unit->unitSide() == BattleSide::DEFENDER
			&& unit->creatureId() == CreatureID(10))
			guard = unit.get();
	ASSERT_NE(guard, nullptr);
	const auto occupied = guard->getHexes();
	ASSERT_EQ(occupied.size(), 2u);
	ASSERT_TRUE(std::all_of(occupied.begin(), occupied.end(),
		[&](const BattleHex & hex) { return hex.isValid() && battle()->battleIsInsideWalls(hex); }))
		<< "This siege fixture must have a genuinely sheltered wide garrison";
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
	policy->activeStack(battleID, guard);
	ASSERT_TRUE(callback->action.has_value());
	EXPECT_EQ(callback->action->actionType, EActionType::DEFEND)
		<< "A fully sheltered wide castle guard should favor the ranged castle defense";
	runToCompletion(std::nullopt, false, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.catapultAttacks.empty());
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

TEST_F(AIIntegrationNativeBattle, tumWideSiegeDefenderHoldsOnlyWhenBothOccupiedCellsAreSheltered)
{
	// Exercise the actual TUM adapter with a double-wide castle garrison.
	// The isolated predicate separately covers asymmetric head/tail cases;
	// this live fixture proves the all-inside hold and subsequent battle.
	prepareSiege(false, CreatureID(10), 6);
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_EQ(battle()->battleGetGateState(), EGateState::CLOSED);
	const CStack * defender = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->alive() && unit->unitSide() == BattleSide::DEFENDER
			&& unit->creatureId() == CreatureID(10))
			defender = unit.get();
	ASSERT_NE(defender, nullptr);
	const auto occupied = defender->getHexes();
	ASSERT_EQ(occupied.size(), 2u) << "The siege defender must be a wide creature";
	ASSERT_TRUE(std::all_of(occupied.begin(), occupied.end(), [&](const BattleHex & hex)
	{
		return hex.isValid() && battle()->battleIsInsideWalls(hex);
	})) << "The real garrison must occupy two sheltered cells for this hold test";

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
		<< "A wide defender fully behind closed fortifications should hold";
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
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
	// The live Griffin decoy occupies two native VCMI hexes. Verify that
	// the tactical predicate receives their true minimum distance rather
	// than the head-only approximation before playing the complete battle.
	const auto fastCells = fast->getHexes();
	const auto ownCells = own->getHexes();
	ASSERT_EQ(fastCells.size(), 2u);
	int expectedFootprintDistance = GameConstants::BFIELD_SIZE;
	for(const auto & friendlyCell : ownCells)
		for(const auto & enemyCell : fastCells)
			expectedFootprintDistance = std::min(expectedFootprintDistance,
				static_cast<int>(BattleHex::getDistance(friendlyCell, enemyCell)));
	const int footprintDistance = AIIntegration::Ports::TUM::nearestFootprintDistance(
		ownCells, fastCells, [](const BattleHex & lhs, const BattleHex & rhs)
		{
			return BattleHex::getDistance(lhs, rhs);
		});
	ASSERT_EQ(footprintDistance, expectedFootprintDistance);
	ASSERT_LE(footprintDistance, distance);
	ASSERT_LE(footprintDistance, speed * 2 + 1);
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

	// Add a closer but immobile enemy war machine AFTER observing the normal
	// slow-infantry route. The fast Griffin is still genuine mobile bait.
	// This deliberate SIEGE_WEAPON fixture must not become the movement
	// destination after TUM rejects that bait. The native engine still
	// controls all actions and the machine is not attributed to TUM.
	const BattleHex intendedAdvance = callback->action->target.front().hexValue;
	BattleHex machineHex;
	for(const int dx : {6, 7, 5})
	{
		if(machineHex.isValid())
			break;
		for(const int dy : {3, -3, 4, -4})
		{
			const BattleHex candidate(static_cast<si16>(origin.getX() + dx),
				static_cast<si16>(origin.getY() + dy));
			if(candidate.isAvailable() && battle()->battleGetUnitByPos(candidate) == nullptr
				&& BattleHex::getDistance(origin, candidate) > speed + 1
				&& BattleHex::getDistance(origin, candidate)
					< BattleHex::getDistance(origin, slow->getPosition()))
			{
				machineHex = candidate;
				break;
			}
		}
	}
	ASSERT_TRUE(machineHex.isValid())
		<< "Need a nearby nonattacking machine outside the infantry path";
	auto * machine = addStack(BattleSide::DEFENDER, CreatureID(0), machineHex, 12);
	ASSERT_NE(machine, nullptr);
	machine->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::SIEGE_WEAPON, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(machine->hasBonusOfType(BonusType::SIEGE_WEAPON));
	ASSERT_EQ(static_cast<int>(machine->getMovementRange()), 0);
	const auto withMachineBaseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*playerView, *own);
	ASSERT_EQ(withMachineBaseline.actionType, EActionType::WALK);
	callback->action.reset();
	policy->activeStack(battleID, own);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK);
	ASSERT_FALSE(callback->action->target.empty());
	EXPECT_EQ(callback->action->target.front().hexValue, intendedAdvance)
		<< "TUM must keep advancing toward mobile slower infantry, not a stationary war machine";
	EXPECT_NE(callback->action->target.front().hexValue, withMachineBaseline.target.front().hexValue)
		<< "Published anti-decoy movement must remain distinct from partial HoMM3";
	battle()->activeStack = own->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Authoritative VCMI server rejected the independent TUM anti-decoy move";
	EXPECT_GT(recorder.movementPackets, 0);
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
}


// A disabled enemy is still an attackable target, but it is not the slower
// MOBILE alternative that justifies ignoring a nearby fast decoy. The old
// TUM-only filter removed siege machines while retaining paralyzed stacks
// in its movement candidates, redirecting the advance to an immobile unit.
// Keep the immediate-attack and non-TUM HoMM3 policies unchanged.
TEST_F(AIIntegrationNativeBattle, tumIncapacitatedAlternativeCannotRedirectFastDecoyAvoidance)
{
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
		else if(unit->unitSide() == BattleSide::DEFENDER)
			slow = unit.get();
	}
	ASSERT_NE(own, nullptr);
	ASSERT_NE(slow, nullptr);
	const BattleHex origin = own->getPosition();
	const int baitY = origin.getY() >= 4 ? origin.getY() - 3 : origin.getY() + 3;
	const BattleHex baitHex(static_cast<si16>(origin.getX() + 5), static_cast<si16>(baitY));
	ASSERT_TRUE(baitHex.isAvailable());
	const auto * fast = addStack(BattleSide::DEFENDER, CreatureID(4), baitHex, 12);
	ASSERT_NE(fast, nullptr);
	const int speed = static_cast<int>(own->getMovementRange());
	ASSERT_GE(static_cast<int>(fast->getMovementRange()), speed + 2);
	ASSERT_LE(static_cast<int>(slow->getMovementRange()), speed);
	ASSERT_GT(BattleHex::getDistance(origin, baitHex), speed + 1);
	ASSERT_LE(AIIntegration::Ports::TUM::nearestFootprintDistance(
		own->getHexes(), fast->getHexes(), [](const BattleHex & a, const BattleHex & b)
		{
			return BattleHex::getDistance(a, b);
		}), speed * 2 + 1);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *own);
	ASSERT_EQ(baseline.actionType, EActionType::WALK);
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
	ASSERT_FALSE(callback->action->target.empty());
	const BattleHex mobileAdvance = callback->action->target.front().hexValue;
	ASSERT_FALSE(baseline.target.empty());
	ASSERT_NE(mobileAdvance, baseline.target.front().hexValue)
		<< "The original fast decoy must already redirect the partial HoMM3 movement";
	callback->action.reset();

	// Inject a closer, non-attacking Peasant with a native NOT_ACTIVE status.
	// This is a fixture-only creature effect, not a claimed TUM spell proc.
	// Place it off the selected move so it cannot block the initial route.
	BattleHex disabledHex;
	for(const int dx : {6, 7, 5})
	{
		if(disabledHex.isValid())
			break;
		for(const int dy : {3, -3, 4, -4})
		{
			const BattleHex candidate(static_cast<si16>(origin.getX() + dx),
				static_cast<si16>(origin.getY() + dy));
			if(candidate.isAvailable() && battle()->battleGetUnitByPos(candidate) == nullptr
				&& BattleHex::getDistance(origin, candidate) > speed + 1
				&& BattleHex::getDistance(origin, candidate)
					< BattleHex::getDistance(origin, slow->getPosition())
				&& BattleHex::getDistance(candidate, mobileAdvance) > 1)
			{
				disabledHex = candidate;
				break;
			}
		}
	}
	ASSERT_TRUE(disabledHex.isValid()) << "Need an off-route disabled target nearer than mobile infantry";
	auto * disabled = addStack(BattleSide::DEFENDER, CreatureID(0), disabledHex, 12);
	ASSERT_NE(disabled, nullptr);
	disabled->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::NOT_ACTIVE, BonusSource::CREATURE_ABILITY,
		1, BonusSourceID(CreatureID(0)), BonusSubtypeID(SpellID(SpellID::PARALYZE))));
	ASSERT_TRUE(disabled->hasBonusOfType(BonusType::NOT_ACTIVE));
	ASSERT_TRUE(view->battleCanAttackUnit(own, disabled))
		<< "The disabled unit must remain a legal future melee target";
	const auto withDisabledHoMM3 = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *own);
	ASSERT_EQ(withDisabledHoMM3.actionType, EActionType::WALK);
	ASSERT_FALSE(withDisabledHoMM3.target.empty());
	policy->activeStack(battleID, own);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK);
	ASSERT_FALSE(callback->action->target.empty());
	EXPECT_EQ(callback->action->target.front().hexValue, mobileAdvance)
		<< "TUM must continue toward active slower infantry, not the paralyzed closer stack";
	EXPECT_NE(callback->action->target.front().hexValue,
		withDisabledHoMM3.target.front().hexValue)
		<< "The TUM movement override must remain independent of the HoMM3 baseline";
	battle()->activeStack = own->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Authoritative VCMI server rejected TUM's independently chosen movement";
	EXPECT_GT(recorder.movementPackets, 0);
	policy.reset();
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
}

TEST_F(AIIntegrationNativeBattle, tumSiegeWeaponLosesMobileDecoyStatus)
{
	// Pinned TUM avoids pursuing fast mobile bait. First prove the native
	// Griffin triggers a distinct TUM action; then mark it a siege weapon
	// and require the engine-legitimate non-decoy decision.
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
	auto * fast = addStack(BattleSide::DEFENDER, CreatureID(4), baitHex, 12);
	ASSERT_NE(fast, nullptr);
	const int speed = static_cast<int>(own->getMovementRange());
	ASSERT_GE(static_cast<int>(fast->getMovementRange()), speed + 2);
	ASSERT_LE(static_cast<int>(slow->getMovementRange()), speed);
	const int distance = BattleHex::getDistance(origin, fast->getPosition());
	// The live Griffin decoy occupies two native VCMI hexes. Verify that
	// the tactical predicate receives their true minimum distance rather
	// than the head-only approximation before playing the complete battle.
	const auto fastCells = fast->getHexes();
	const auto ownCells = own->getHexes();
	ASSERT_EQ(fastCells.size(), 2u);
	int expectedFootprintDistance = GameConstants::BFIELD_SIZE;
	for(const auto & friendlyCell : ownCells)
		for(const auto & enemyCell : fastCells)
			expectedFootprintDistance = std::min(expectedFootprintDistance,
				static_cast<int>(BattleHex::getDistance(friendlyCell, enemyCell)));
	const int footprintDistance = AIIntegration::Ports::TUM::nearestFootprintDistance(
		ownCells, fastCells, [](const BattleHex & lhs, const BattleHex & rhs)
		{
			return BattleHex::getDistance(lhs, rhs);
		});
	ASSERT_EQ(footprintDistance, expectedFootprintDistance);
	ASSERT_LE(footprintDistance, distance);
	ASSERT_LE(footprintDistance, speed * 2 + 1);
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
	// The siege-weapon flag is explicit fixture setup, not a natural
	// Griffin ability or an assertion about TUM's unavailable DLL.
	fast->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::SIEGE_WEAPON, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(fast->hasBonusOfType(BonusType::SIEGE_WEAPON));
	ASSERT_FALSE(fast->isMeleeAttacker());
	// Unlike a visual unit label, native SIEGE_WEAPON changes the engine's
	// movement capability: getMovementRange() now returns zero. Checking
	// the previous fast speed here was invalid. The regression concerns
	// the legal decision after that real rule takes effect, not a fictitious
	// fast-moving siege machine.
	ASSERT_EQ(static_cast<int>(fast->getMovementRange()), 0)
		<< "VCMI must classify the siege-weapon-marked stack as immobile";
	const auto machineBaseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*playerView, *own);
	ASSERT_EQ(machineBaseline.actionType, EActionType::WALK);
	callback->action.reset();
	policy->activeStack(battleID, own);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK);
	EXPECT_EQ(callback->action->toString(), machineBaseline.toString())
		<< "A siege machine cannot lure melee away from the original route";
	battle()->activeStack = own->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The authoritative VCMI server rejected TUM's legal machine-safe move";
	EXPECT_GT(recorder.movementPackets, 0);
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
}

TEST_F(AIIntegrationNativeBattle, tumWarMachineCannotBeSlowAnchorForFastDecoyAvoidance)
{
	// Pinned TUM only discourages pursuing fast mobile bait near slower stacks.
	// A VCMI siege weapon must not become the speed-zero "slow army" that
	// makes an otherwise legitimate fast opponent look like a distraction.
	// The SIEGE_WEAPON bonus is explicit fixture setup, not a creature proc.
	prepareBattle(CreatureID(0), 80, CreatureID(0), 35);
	ASSERT_FALSE(HasFatalFailure());
	const CStack * own = nullptr;
	CStack * machineLike = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->creatureId() != CreatureID(0))
			continue;
		if(unit->unitSide() == BattleSide::ATTACKER)
			own = unit.get();
		else if(unit->unitSide() == BattleSide::DEFENDER)
			machineLike = unit.get();
	}
	ASSERT_NE(own, nullptr);
	ASSERT_NE(machineLike, nullptr);
	const auto origin = own->getPosition();
	const int baitY = origin.getY() >= 4 ? origin.getY() - 3 : origin.getY() + 3;
	const BattleHex baitHex(static_cast<si16>(origin.getX() + 5), static_cast<si16>(baitY));
	ASSERT_TRUE(baitHex.isAvailable());
	ASSERT_LT(BattleHex::getDistance(origin, baitHex),
		BattleHex::getDistance(origin, machineLike->getPosition()));
	const auto * fast = addStack(BattleSide::DEFENDER, CreatureID(4), baitHex, 12);
	ASSERT_NE(fast, nullptr);
	const int ownSpeed = static_cast<int>(own->getMovementRange());
	ASSERT_GE(static_cast<int>(fast->getMovementRange()), ownSpeed + 2);
	ASSERT_GT(BattleHex::getDistance(origin, baitHex), ownSpeed + 1);
	ASSERT_LE(BattleHex::getDistance(origin, baitHex), ownSpeed * 2 + 1);
	ASSERT_LE(static_cast<int>(machineLike->getMovementRange()), ownSpeed);
	machineLike->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::SIEGE_WEAPON, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(machineLike->hasBonusOfType(BonusType::SIEGE_WEAPON));
	ASSERT_FALSE(machineLike->isMeleeAttacker());

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *own);
	ASSERT_EQ(baseline.actionType, EActionType::WALK);
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
	EXPECT_EQ(callback->action->toString(), baseline.toString())
		<< "No slower mobile stack exists, so a siege weapon must not redirect TUM's movement";
	battle()->activeStack = own->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "VCMI server rejected the independently selected TUM movement";
	EXPECT_GT(recorder.movementPackets, 0);
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
}

TEST_F(AIIntegrationNativeBattle, tumDoesNotAvoidIncapacitatedFastStackAsMobileDecoy)
{
	// A paralyzed fast stack cannot be a mobile distraction. TUM must
	// preserve the legitimate pursuit target, as the native server confirms.
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
	auto * fast = addStack(BattleSide::DEFENDER, CreatureID(4), baitHex, 12);
	ASSERT_NE(fast, nullptr);
	const int speed = static_cast<int>(own->getMovementRange());
	ASSERT_GE(static_cast<int>(fast->getMovementRange()), speed + 2);
	ASSERT_LE(static_cast<int>(slow->getMovementRange()), speed);
	const int distance = BattleHex::getDistance(origin, fast->getPosition());
	// The live Griffin decoy occupies two native VCMI hexes. Verify that
	// the tactical predicate receives their true minimum distance rather
	// than the head-only approximation before playing the complete battle.
	const auto fastCells = fast->getHexes();
	const auto ownCells = own->getHexes();
	ASSERT_EQ(fastCells.size(), 2u);
	int expectedFootprintDistance = GameConstants::BFIELD_SIZE;
	for(const auto & friendlyCell : ownCells)
		for(const auto & enemyCell : fastCells)
			expectedFootprintDistance = std::min(expectedFootprintDistance,
				static_cast<int>(BattleHex::getDistance(friendlyCell, enemyCell)));
	const int footprintDistance = AIIntegration::Ports::TUM::nearestFootprintDistance(
		ownCells, fastCells, [](const BattleHex & lhs, const BattleHex & rhs)
		{
			return BattleHex::getDistance(lhs, rhs);
		});
	ASSERT_EQ(footprintDistance, expectedFootprintDistance);
	ASSERT_LE(footprintDistance, distance);
	ASSERT_LE(footprintDistance, speed * 2 + 1);
	ASSERT_GT(distance, speed + 1) << "The decoy must not be immediately attackable";
	// Fixture-only creature-source incapacity. Its high native speed remains
	// unchanged but it cannot run away; no creature ability proc is claimed.
	fast->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::NOT_ACTIVE, BonusSource::CREATURE_ABILITY,
		1, BonusSourceID(CreatureID(0)), BonusSubtypeID(SpellID(SpellID::PARALYZE))));
	ASSERT_TRUE(fast->hasBonusOfType(BonusType::NOT_ACTIVE));
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
	EXPECT_EQ(callback->action->toString(), baseline.toString())
		<< "An immobilized fast target cannot act as bait and must not be excluded";
	battle()->activeStack = own->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Authoritative VCMI server rejected movement toward the incapacitated target";
	EXPECT_GT(recorder.movementPackets, 0);
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
}

// A genuine VCMI Hypnotize spell transfers the melee unit's control to the
// opposing army. TUM should then attack its former allies by maximizing
// damage dealt plus retaliation received, rather than normal net exchange.
TEST_F(AIIntegrationNativeBattle, tumHypnotizedMeleeTargetsHighestCombinedDamage)
{
	prepareBattle(CreatureID(0), 30, CreatureID(0), 8);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * guard = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			guard = unit.get();
	ASSERT_NE(guard, nullptr);
	std::vector<BattleHex> positions;
	const auto guardPosition = guard->getPosition();
	const auto neighbouringHexes = guardPosition.getNeighbouringTiles();
	for(const auto & hex : neighbouringHexes)
		if(hex.isAvailable() && battle()->battleGetUnitByPos(hex) == nullptr)
			positions.push_back(hex);
	ASSERT_GE(positions.size(), 2u);
	const CStack * weak = addStack(BattleSide::ATTACKER, CreatureID(0), positions[0], 1);
	const CStack * strong = addStack(BattleSide::ATTACKER, CreatureID(0), positions[1], 80);
	ASSERT_NE(weak, nullptr);
	ASSERT_NE(strong, nullptr);
	teachBattleSpell(defenderSideHero, SpellID::HYPNOTIZE, 20);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::HYPNOTIZE, guard));
	ASSERT_TRUE(guard->isHypnotized());
	const auto player = battle()->sideToPlayer(BattleSide::DEFENDER);
	ASSERT_NE(guard->unitOwner(), player);
	ASSERT_EQ(battle()->battleGetOwner(guard), player);
	battle()->activeStack = guard->unitId();

	const auto battleID = battle()->getBattleID();
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	// The native estimator can describe a retaliation even if the first hit
	// certainly kills its victim. No such response can occur on the server.
	BattleAttackInfo lethalHit(guard, weak, 0, false);
	lethalHit.attackerPos = guard->getPosition();
	DamageEstimation hypotheticalRetaliation;
	const auto lethalDamage = view->battleEstimateDamage(lethalHit, &hypotheticalRetaliation);
	ASSERT_GE(static_cast<double>(lethalDamage.damage.min),
		static_cast<double>(weak->getAvailableHealth()))
		<< "A real guaranteed lethal strike is required for this regression";
	const auto score = [&](const CStack * target)
	{
		BattleAttackInfo info(guard, target, 0, false);
		info.attackerPos = guard->getPosition();
		DamageEstimation retaliation;
		const auto damage = view->battleEstimateDamage(info, &retaliation);
		const double health = static_cast<double>(target->getAvailableHealth());
		const double dealt = std::min(health,
			(static_cast<double>(damage.damage.min) + damage.damage.max) / 2.0);
		const double received = static_cast<double>(damage.damage.min) >= health ? 0.0
			: std::min(static_cast<double>(guard->getAvailableHealth()),
				(static_cast<double>(retaliation.damage.min) + retaliation.damage.max) / 2.0);
		return AIIntegration::Ports::TUM::hypnotizedAttackDamageSum(dealt, received);
	};
	ASSERT_GT(score(strong), score(weak))
		<< "A dangerous original ally must outrank a weak target in summed damage";

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
	policy->activeStack(battleID, guard);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(callback->action->target.size(), 2u);
	const auto * chosen = view->battleGetUnitByPos(callback->action->target[1].hexValue);
	ASSERT_NE(chosen, nullptr);
	EXPECT_EQ(chosen->unitId(), strong->unitId())
		<< "TUM Hypnotize must maximize dealt + received damage";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "VCMI server rejected the Hypnotized unit's real attack";
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics", "TUMEnchantedTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
}

// TUM's published Hypnotize damage-sum priority must account for actual
// multihex melee victims. An explicit test-only breath bonus on a Peasant
// exercises the native VCMI affected-unit selector; it does NOT claim that
// Peasants naturally breathe fire or that the closed TUM DLL was recovered.
TEST_F(AIIntegrationNativeBattle, tumHypnotizedBreathScoresAndHitsNativeSecondaryMeleeVictim)
{
	prepareBattle(CreatureID(0), 30, CreatureID(0), 8);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * guard = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			guard = unit.get();
	ASSERT_NE(guard, nullptr);
	const BattleHex origin = guard->getPosition();
	const BattleHex frontHex(origin.toInt() + 1);
	const BattleHex rearHex(origin.toInt() + 2);
	ASSERT_TRUE(frontHex.isAvailable());
	ASSERT_TRUE(rearHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(frontHex), nullptr);
	ASSERT_EQ(battle()->battleGetUnitByPos(rearHex), nullptr);
	const auto * front = addStack(BattleSide::ATTACKER, CreatureID(0), frontHex, 1);
	const auto * rear = addStack(BattleSide::ATTACKER, CreatureID(0), rearHex, 1);
	ASSERT_NE(front, nullptr);
	ASSERT_NE(rear, nullptr);
	guard->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::TWO_HEX_ATTACK_BREATH,
		BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(guard->hasBonusOfType(BonusType::TWO_HEX_ATTACK_BREATH));
	teachBattleSpell(defenderSideHero, SpellID::HYPNOTIZE, 20);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::HYPNOTIZE, guard));
	ASSERT_TRUE(guard->isHypnotized());
	const auto player = battle()->sideToPlayer(BattleSide::DEFENDER);
	ASSERT_EQ(battle()->battleGetOwner(guard), player);
	battle()->activeStack = guard->unitId();

	const auto battleID = battle()->getBattleID();
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	const auto initialSplash = view->getAttackedBattleUnits(
		guard, front, frontHex, false, origin);
	ASSERT_TRUE(std::any_of(initialSplash.begin(), initialSplash.end(),
		[&](const battle::Unit * hit) { return hit->unitId() == rear->unitId(); }))
		<< "The real VCMI melee footprint must include the second former ally";
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *guard);
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
	policy->activeStack(battleID, guard);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(callback->action->target.size(), 2u);
	const BattleHex attackFrom = callback->action->target[0].hexValue;
	const BattleHex aimed = callback->action->target[1].hexValue;
	const auto * primary = view->battleGetUnitByPos(aimed);
	ASSERT_NE(primary, nullptr);
	ASSERT_NE(primary->unitId(), guard->unitId())
		<< "A Hypnotized stack must never target itself after a controller swap";
	ASSERT_NE(view->battleGetOwner(primary), view->battleGetOwner(guard))
		<< "Hypnotized attacks must target the actual opposing controller";
	const auto extra = view->getAttackedBattleUnits(
		guard, primary, aimed, false, attackFrom);
	ASSERT_FALSE(extra.empty())
		<< "Hypnotized TUM must prefer an actual two-victim breath strike"
		<< " proposed=" << callback->action->toString()
		<< " baseline=" << baseline.toString()
		<< " origin=" << origin.toInt()
		<< " front=" << frontHex.toInt()
		<< " rear=" << rearHex.toInt()
		<< " attack_from=" << attackFrom.toInt()
		<< " aimed=" << aimed.toInt()
		<< " primary_id=" << primary->unitId()
		<< " reachable_origin=" << view->getReachability(guard).isReachable(origin)
		<< " legal_origin_front="
		<< view->meleeAttackHexes(guard, front, origin).contains(frontHex);
	std::cout << "AI_TUM_HYPNOTIZE_MELEE_SPLASH homm3=" << baseline.toString()
		<< " tum=" << callback->action->toString() << '\n';
	// Observe the actual BattleAttack packet before its damage applies.
	// A lethal action may end the entire battle and invalidate CStack pointers.
	const int frontId = front->unitId();
	const int rearId = rear->unitId();
	std::set<int> nativeHitIds;
	recorder.onAttackBeforeApply = [&](const BattleAttack & attack)
	{
		for(const auto & injury : attack.bsa)
			if(injury.damageAmount > 0
				&& (injury.stackAttacked == frontId || injury.stackAttacked == rearId))
				nativeHitIds.insert(injury.stackAttacked);
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Authoritative VCMI rejected the independent TUM multihex melee attack";
	recorder.onAttackBeforeApply = {};
	EXPECT_EQ(nativeHitIds.size(), 2u)
		<< "Real server damage must include both the primary and secondary former ally";
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics", "TUMEnchantedTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_GT(recorder.damage, 0);
}

// Source-published TUM Hypnotize values incoming retaliation as a benefit,
// but an already exhausted stack cannot deliver it. Compare the native
// HoMM3 net-exchange baseline with the TUM positive-exchange policy, execute
// TUM's independent choice through the real battle server, and finish combat.

TEST_F(AIIntegrationNativeBattle, tumHypnotizedWideTargetUsesNativeAim)
{
	// TUM's public Hypnotize rule values damage dealt + damage received.
	// Compare both cells of a wide former ally with native VCMI previews,
	// then verify the independent attack and actual server resolution.
	prepareBattle(CreatureID(0), 30, CreatureID(0), 80);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * guard = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			guard = unit.get();
	ASSERT_NE(guard, nullptr);
	const BattleHex near(guard->getPosition().toInt() + 3);
	ASSERT_TRUE(near.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(near), nullptr);
	const CStack * wideAlly = addStack(BattleSide::ATTACKER, CreatureID(4), near, 50);
	ASSERT_NE(wideAlly, nullptr);
	ASSERT_TRUE(wideAlly->doubleWide());
	teachBattleSpell(defenderSideHero, SpellID::HYPNOTIZE, 20);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::HYPNOTIZE, guard));
	ASSERT_TRUE(guard->isHypnotized());
	const auto player = battle()->sideToPlayer(BattleSide::DEFENDER);
	ASSERT_EQ(battle()->battleGetOwner(guard), player);
	battle()->activeStack = guard->unitId();

	const auto battleID = battle()->getBattleID();
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	const auto reach = view->getReachability(guard);
	const auto available = view->battleGetAvailableHexes(reach, guard, false);
	const int64_t health = wideAlly->getAvailableHealth();
	const auto preview = [&](BattleHex from, BattleHex aimed, int distance)
	{
		BattleAttackInfo attack(guard, wideAlly, distance, false);
		attack.attackerPos = from;
		attack.defenderPos = aimed;
		DamageEstimation retaliation;
		const auto estimate = view->battleEstimateDamage(attack, &retaliation);
		const double dealt = std::min(static_cast<double>(health),
			(static_cast<double>(estimate.damage.min) + estimate.damage.max) / 2.0);
		const double received = !wideAlly->ableToRetaliate()
			|| estimate.damage.min >= health ? 0.0
			: std::min(static_cast<double>(guard->getAvailableHealth()),
				(static_cast<double>(retaliation.damage.min) + retaliation.damage.max) / 2.0);
		return AIIntegration::Ports::TUM::hypnotizedAttackDamageSum(dealt, received);
	};
	bool sawHead = false;
	bool sawTail = false;
	double best = -1.0;
	for(const auto & from : available)
	{
		if(!from.isAvailable() || static_cast<int>(reach.distances[from.toInt()]) < 0)
			continue;
		for(const auto & aimed : view->meleeAttackHexes(guard, wideAlly, from))
		{
			sawHead |= aimed == wideAlly->getPosition();
			sawTail |= aimed != wideAlly->getPosition();
			best = std::max(best, preview(from, aimed, reach.distances[from.toInt()]));
			EXPECT_EQ(wideAlly->getAvailableHealth(), health);
		}
	}
	ASSERT_TRUE(sawHead && sawTail) << "Both wide-target cells must be reachable";

	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *guard);
	ASSERT_EQ(baseline.actionType, EActionType::WALK_AND_ATTACK);
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
	policy->activeStack(battleID, guard);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(callback->action->target.size(), 2u);
	const auto from = callback->action->target[0].hexValue;
	const auto aimed = callback->action->target[1].hexValue;
	const auto legal = view->meleeAttackHexes(guard, wideAlly, from);
	EXPECT_NE(std::find(legal.begin(), legal.end(), aimed), legal.end());
	EXPECT_EQ(view->battleGetUnitByPos(aimed), wideAlly);
	ASSERT_GE(reach.distances[from.toInt()], 0);
	EXPECT_GE(preview(from, aimed, reach.distances[from.toInt()]) + 0.0001, best);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Server rejected TUM's Hypnotize wide-target melee action";
	EXPECT_LT(wideAlly->getAvailableHealth(), health);
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics", "TUMEnchantedTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, tumHypnotizedAttackDoesNotRewardExhaustedRetaliation)
{
	prepareBattle(CreatureID(0), 30, CreatureID(0), 8);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * guard = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			guard = unit.get();
	ASSERT_NE(guard, nullptr);

	std::vector<BattleHex> positions;
	const auto neighbouringHexes = guard->getPosition().getNeighbouringTiles();
	for(const auto & hex : neighbouringHexes)
		if(hex.isAvailable() && battle()->battleGetUnitByPos(hex) == nullptr)
			positions.push_back(hex);
	ASSERT_GE(positions.size(), 2u);
	auto * exhausted = addStack(BattleSide::ATTACKER, CreatureID(0), positions[0], 80);
	auto * ready = addStack(BattleSide::ATTACKER, CreatureID(0), positions[1], 80);
	ASSERT_NE(exhausted, nullptr);
	ASSERT_NE(ready, nullptr);
	ASSERT_TRUE(exhausted->ableToRetaliate());
	ASSERT_TRUE(ready->ableToRetaliate());
	exhausted->counterAttacks.use();
	ASSERT_FALSE(exhausted->ableToRetaliate())
		<< "Fixture must genuinely exhaust the native VCMI retaliation allowance";
	ASSERT_TRUE(ready->ableToRetaliate());

	teachBattleSpell(defenderSideHero, SpellID::HYPNOTIZE, 20);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::HYPNOTIZE, guard));
	ASSERT_TRUE(guard->isHypnotized());
	const auto player = battle()->sideToPlayer(BattleSide::DEFENDER);
	ASSERT_EQ(battle()->battleGetOwner(guard), player);
	battle()->activeStack = guard->unitId();

	const auto battleID = battle()->getBattleID();
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	const auto score = [&](const CStack * target)
	{
		BattleAttackInfo info(guard, target, 0, false);
		info.attackerPos = guard->getPosition();
		DamageEstimation retaliation;
		const auto damage = view->battleEstimateDamage(info, &retaliation);
		const double health = static_cast<double>(target->getAvailableHealth());
		const double dealt = std::min(health,
			(static_cast<double>(damage.damage.min) + damage.damage.max) / 2.0);
		const double received = !target->ableToRetaliate()
			|| static_cast<double>(damage.damage.min) >= health ? 0.0
			: std::min(static_cast<double>(guard->getAvailableHealth()),
				(static_cast<double>(retaliation.damage.min) + retaliation.damage.max) / 2.0);
		return AIIntegration::Ports::TUM::hypnotizedAttackDamageSum(dealt, received);
	};
	ASSERT_GT(score(ready), score(exhausted))
		<< "Only the ready target can deliver the incoming damage TUM rewards";

	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *guard);
	ASSERT_EQ(baseline.actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(baseline.target.size(), 2u);
	EXPECT_EQ(view->battleGetUnitByPos(baseline.target[1].hexValue), exhausted)
		<< "The net-exchange HoMM3 baseline should prefer the spent target";

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
	policy->activeStack(battleID, guard);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(callback->action->target.size(), 2u);
	EXPECT_EQ(view->battleGetUnitByPos(callback->action->target[1].hexValue), ready)
		<< "TUM must reward only physically possible received damage";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "VCMI server rejected TUM's independent Hypnotize melee decision";
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics", "TUMEnchantedTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
}

// A native melee FIRST_STRIKE may destroy a hypnotized attacker before it
// makes its opening strike. TUM still values received damage positively,
// but must not also credit a hit that the authoritative VCMI server skips.
TEST_F(AIIntegrationNativeBattle, tumHypnotizedMeleeFirstStrikeCountersBeforeAttack)
{
	prepareBattle(CreatureID(0), 30, CreatureID(0), 8);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * hypnotized = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			hypnotized = unit.get();
	ASSERT_NE(hypnotized, nullptr);
	std::vector<BattleHex> adjacent;
	const auto neighbouringTiles = hypnotized->getPosition().getNeighbouringTiles();
	for(const auto & hex : neighbouringTiles)
		if(hex.isAvailable() && battle()->battleGetUnitByPos(hex) == nullptr)
			adjacent.push_back(hex);
	ASSERT_GE(adjacent.size(), 2u);
	auto * firstStriker = addStack(BattleSide::ATTACKER, CreatureID(0), adjacent[0], 500);
	auto * weak = addStack(BattleSide::ATTACKER, CreatureID(0), adjacent[1], 1);
	ASSERT_NE(firstStriker, nullptr);
	ASSERT_NE(weak, nullptr);
	firstStriker->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::FIRST_STRIKE, BonusSource::CREATURE_ABILITY,
		1, BonusSourceID(CreatureID(0)), BonusSubtypeID(BonusCustomSubtype::damageTypeMelee)));
	ASSERT_TRUE(firstStriker->hasBonus(Selector::typeSubtype(
		BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeMelee)));
	ASSERT_TRUE(firstStriker->ableToRetaliate());

	teachBattleSpell(defenderSideHero, SpellID::HYPNOTIZE, 20);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::HYPNOTIZE, hypnotized));
	ASSERT_TRUE(hypnotized->isHypnotized());
	const auto player = battle()->sideToPlayer(BattleSide::DEFENDER);
	ASSERT_EQ(battle()->battleGetOwner(hypnotized), player);
	battle()->activeStack = hypnotized->unitId();
	const auto battleID = battle()->getBattleID();
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->isMeleeAttackPossible(hypnotized, firstStriker));
	const auto guardHealth = hypnotized->getAvailableHealth();
	const auto targetHealth = firstStriker->getAvailableHealth();
	BattleAttackInfo earlyCounter(firstStriker, hypnotized, 0, false);
	earlyCounter.attackerPos = firstStriker->getPosition();
	const auto firstDamage = view->battleEstimateDamage(earlyCounter, nullptr);
	ASSERT_GE(firstDamage.damage.min, guardHealth);
	BattleAttackInfo staleOpening(hypnotized, firstStriker, 0, false);
	staleOpening.attackerPos = hypnotized->getPosition();
	const auto staleDamage = view->battleEstimateDamage(staleOpening, nullptr);
	ASSERT_GT(staleDamage.damage.min, 0);
	
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *hypnotized);
	ASSERT_EQ(baseline.actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(baseline.target.size(), 2u);
	EXPECT_EQ(view->battleGetUnitByPos(baseline.target[1].hexValue), weak)
		<< "HoMM3's partial net-exchange baseline must prefer the safer target";

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
	policy->activeStack(battleID, hypnotized);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(callback->action->target.size(), 2u);
	EXPECT_EQ(view->battleGetUnitByPos(callback->action->target[1].hexValue), firstStriker)
		<< "TUM's positive incoming-damage score must independently select first strike";
	const int guardId = hypnotized->unitId();
	const int strikerId = firstStriker->unitId();
	std::vector<int> initialAttacks;
	recorder.onAttackBeforeApply = [&](const BattleAttack & attack)
	{
		if(attack.stackAttacking == strikerId && attack.counter())
			initialAttacks.push_back(strikerId);
		else if(attack.stackAttacking == guardId)
			initialAttacks.push_back(guardId);
	};
	const auto beforeDamage = recorder.damage;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The real VCMI server rejected TUM's Hypnotize melee choice";
	ASSERT_EQ(initialAttacks.size(), 1u);
	EXPECT_EQ(initialAttacks[0], strikerId);
	EXPECT_FALSE(hypnotized->alive());
	EXPECT_EQ(firstStriker->getAvailableHealth(), targetHealth)
		<< "A lethal first-strike counter must prevent outgoing melee damage";
	EXPECT_GT(recorder.damage, beforeDamage);
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics", "TUMEnchantedTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
}

// FIRST_STRIKE applies to an ordinary native melee attack even when the
// attacker's front hex is two cells from the target: a double-wide creature
// can strike from its tail. Test a real Hypnotize cast, independent TUM vs
// HoMM3 choice, the authoritative counter-before-hit and completed battle.
// All bonuses and the extra Peasant stacks are controlled fixture setup.
TEST_F(AIIntegrationNativeBattle, tumHypnotizedWideAttackerReceivesFirstStrikeAtTail)
{
	prepareBattle(CreatureID(4), 5, CreatureID(0), 80);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * guard = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(4))
			guard = unit.get();
	ASSERT_NE(guard, nullptr);
	ASSERT_TRUE(guard->doubleWide());
	// The default attacker deployment is against the left battlefield edge;
	// it has no playable tail-only neighboring hex. Reposition this fixture
	// to open ground before starting the action, not through a fake server
	// movement packet. The subsequent chosen attack is engine-validated.
	const BattleHex central(80);
	for(const auto & hex : guard->getHexes(central))
	{
		ASSERT_TRUE(hex.isAvailable());
		ASSERT_EQ(battle()->battleGetUnitByPos(hex), nullptr);
	}
	guard->setPosition(central);
	const BattleHex head = guard->getPosition();
	const auto & occupied = guard->getHexes();
	ASSERT_EQ(occupied.size(), 2u);
	const BattleHex tail = *std::find_if(occupied.begin(), occupied.end(),
		[&](const BattleHex & hex) { return hex != head; });
	ASSERT_TRUE(tail.isValid());

	BattleHex tailOnly = BattleHex::INVALID;
	for(const auto & hex : tail.getNeighbouringTiles())
	{
		if(hex.isAvailable() && BattleHex::getDistance(head, hex) > 1
			&& battle()->battleGetUnitByPos(hex) == nullptr)
		{
			tailOnly = hex;
			break;
		}
	}
	ASSERT_TRUE(tailOnly.isValid()) << "Need a target next to the tail but not the head";
	BattleHex safeTarget = BattleHex::INVALID;
	for(const auto & hex : head.getNeighbouringTiles())
	{
		if(hex.isAvailable() && hex != tailOnly
			&& battle()->battleGetUnitByPos(hex) == nullptr)
		{
			safeTarget = hex;
			break;
		}
	}
	ASSERT_TRUE(safeTarget.isValid());
	auto * firstStriker = addStack(BattleSide::ATTACKER, CreatureID(0), tailOnly, 500);
	auto * weak = addStack(BattleSide::ATTACKER, CreatureID(0), safeTarget, 1);
	ASSERT_NE(firstStriker, nullptr);
	ASSERT_NE(weak, nullptr);
	firstStriker->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::FIRST_STRIKE, BonusSource::CREATURE_ABILITY,
		1, BonusSourceID(CreatureID(0)), BonusSubtypeID(BonusCustomSubtype::damageTypeMelee)));

	teachBattleSpell(defenderSideHero, SpellID::HYPNOTIZE, 20);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::HYPNOTIZE, guard));
	ASSERT_TRUE(guard->isHypnotized());
	// Restrict the fixture to attacking from the occupied double-wide
	// footprint, avoiding a movement option whose head is also adjacent.
	guard->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::CREATURE_ABILITY,
		-100, BonusSourceID(CreatureID(4))));
	ASSERT_EQ(guard->getMovementRange(), 0u);
	ASSERT_EQ(BattleHex::getDistance(head, tailOnly), 2);
	const auto player = battle()->sideToPlayer(BattleSide::DEFENDER);
	ASSERT_EQ(battle()->battleGetOwner(guard), player);
	battle()->activeStack = guard->unitId();
	const auto battleID = battle()->getBattleID();
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->isMeleeAttackPossible(guard, firstStriker, head));
	ASSERT_FALSE(view->meleeAttackHexes(guard, firstStriker, head).empty());
	BattleAttackInfo counter(firstStriker, guard, 0, false);
	counter.attackerPos = firstStriker->getPosition();
	const auto early = view->battleEstimateDamage(counter, nullptr);
	ASSERT_GE(early.damage.min, guard->getAvailableHealth());
	const auto guardHealth = guard->getAvailableHealth();
	const auto strikerHealth = firstStriker->getAvailableHealth();

	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *guard);
	ASSERT_EQ(baseline.actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(baseline.target.size(), 2u);
	EXPECT_EQ(view->battleGetUnitByPos(baseline.target[1].hexValue), weak)
		<< "The source-policy net-exchange baseline avoids the lethal counter";

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
	policy->activeStack(battleID, guard);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(callback->action->target.size(), 2u);
	EXPECT_EQ(callback->action->target[0].hexValue, head);
	EXPECT_EQ(view->battleGetUnitByPos(callback->action->target[1].hexValue), firstStriker)
		<< "TUM's incoming-damage priority must recognize a tail-only first strike";
	EXPECT_GT(BattleHex::getDistance(head, callback->action->target[1].hexValue), 1);
	const int guardId = guard->unitId();
	const int strikerId = firstStriker->unitId();
	std::vector<int> observed;
	recorder.onAttackBeforeApply = [&](const BattleAttack & attack)
	{
		if(attack.stackAttacking == strikerId && attack.counter())
			observed.push_back(strikerId);
		else if(attack.stackAttacking == guardId)
			observed.push_back(guardId);
	};
	const int beforeDamage = recorder.damage;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The native server rejected a tail-contact Hypnotize melee action";
	ASSERT_EQ(observed.size(), 1u);
	EXPECT_EQ(observed.front(), strikerId);
	EXPECT_FALSE(guard->alive());
	EXPECT_EQ(strikerHealth, firstStriker->getAvailableHealth())
		<< "The lethal first strike must prevent the wide attacker's hit";
	EXPECT_GT(recorder.damage, beforeDamage);
	EXPECT_GT(guardHealth, 0);
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics", "TUMEnchantedTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
}

// The published TUM Hypnotize priority is a single comparison across all
// attack types. A legal ranged shot must not suppress a higher-scoring melee
// strike when the latter also receives substantial retaliation.
// Published TUM Hypnotize favors outgoing + incoming damage. Native
// SHOOTS_ALL_ADJACENT hits more former allies when aiming at a wide unit's
// tail rather than its head. Require an actual hero Hypnotize, an independently
// verified splash footprint, a different HoMM3 unit-aim proposal, the server's
// acceptance of TUM's hex-aimed shot and a completed battle.
TEST_F(AIIntegrationNativeBattle, tumHypnotizedArcherAimsAtBetterWideTailShot)
{
	prepareBattle(CreatureID(45), 30, CreatureID(0), 8);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * shooter = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(45))
			shooter = unit.get();
	ASSERT_NE(shooter, nullptr);
	ASSERT_TRUE(shooter->isShooter());
	shooter->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::SHOOTS_ALL_ADJACENT,
		BonusSource::OTHER, 1, BonusSourceID()));
	// Keep the wide target and its tail-adjacent companions outside the
	// hypnotized shooter's one-turn melee reach. The published TUM rule
	// compares outgoing + incoming melee damage, which can legitimately
	// outrank splash fire. This regression isolates the *ranged* aim choice;
	// a separate regression compares melee against shooting explicitly.
	const BattleHex widePosition(shooter->getPosition().toInt() + 11);
	ASSERT_TRUE(widePosition.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(widePosition), nullptr);
	CStack * wide = addStack(BattleSide::ATTACKER, CreatureID(102), widePosition, 10);
	ASSERT_NE(wide, nullptr);
	ASSERT_TRUE(wide->doubleWide());
	// Regression for the native ranged FIRST_STRIKE eligibility gate: a
	// melee-only wide unit can have FIRST_STRIKE without RANGED_RETALIATION.
	// VCMI postpones the attacker's first shot but cannot countershoot.
	// TUM must still count actual adjacent-splash victims at the tail.
	// The bonus is fixture-only and does not imply a natural ability.
	wide->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::FIRST_STRIKE,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(102)),
		BonusSubtypeID(BonusCustomSubtype::damageTypeRanged)));
	ASSERT_TRUE(wide->hasBonus(Selector::typeSubtype(
		BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeRanged)));
	ASSERT_FALSE(wide->hasBonusOfType(BonusType::RANGED_RETALIATION));
	const auto footprint = wide->getHexes();
	ASSERT_EQ(footprint.size(), 2u);
	const BattleHex head = footprint[0];
	const BattleHex tail = footprint[1];
	ASSERT_NE(head, tail);
	std::vector<BattleHex> tailOnly;
	for(const auto & hex : tail.getNeighbouringTiles())
	{
		if(hex.isAvailable() && battle()->battleGetUnitByPos(hex) == nullptr
			&& BattleHex::getDistance(hex, head) > 1)
			tailOnly.push_back(hex);
	}
	ASSERT_GE(tailOnly.size(), 2u);
	const CStack * firstExtra = addStack(BattleSide::ATTACKER, CreatureID(0), tailOnly[0], 1000);
	const CStack * secondExtra = addStack(BattleSide::ATTACKER, CreatureID(0), tailOnly[1], 1000);
	ASSERT_NE(firstExtra, nullptr);
	ASSERT_NE(secondExtra, nullptr);

	teachBattleSpell(defenderSideHero, SpellID::HYPNOTIZE, 20);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::HYPNOTIZE, shooter));
	ASSERT_TRUE(shooter->isHypnotized());
	const auto player = battle()->sideToPlayer(BattleSide::DEFENDER);
	ASSERT_EQ(battle()->battleGetOwner(shooter), player);
	const auto battleID = battle()->getBattleID();
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	// An available melee option changes TUM's published damage-sum
	// optimization. Keep this test strictly about selecting the legal tail
	// hex over the legal head hex for a ranged area attack.
	const auto reach = view->getReachability(shooter);
	const auto reachableHexes = view->battleGetAvailableHexes(reach, shooter, false);
	for(const CStack * victim : {static_cast<const CStack *>(wide), firstExtra, secondExtra})
		for(const auto & origin : reachableHexes)
			ASSERT_TRUE(view->meleeAttackHexes(shooter, victim, origin).empty())
				<< "Wide-tail shooting fixture unexpectedly permits a melee attack";
	ASSERT_TRUE(view->battleCanShoot(shooter, head));
	ASSERT_TRUE(view->battleCanShoot(shooter, tail));
	ASSERT_FALSE(view->battleCanShoot(wide, shooter->getPosition()))
		<< "FIRST_STRIKE alone cannot create a native ranged countershot";
	const auto headHits = view->getAttackedBattleUnits(shooter, wide, head, true);
	const auto tailHits = view->getAttackedBattleUnits(shooter, wide, tail, true);
	const auto hasHit = [](const auto & units, const CStack * needle)
	{
		return std::any_of(units.begin(), units.end(), [&](const auto * unit)
		{
			return unit->unitId() == needle->unitId();
		});
	};
	for(const auto * extra : {firstExtra, secondExtra})
	{
		ASSERT_FALSE(hasHit(headHits, extra))
			<< "Head aim must miss tail-only original allies";
		ASSERT_TRUE(hasHit(tailHits, extra))
			<< "Native tail aim must hit both additional original allies";
	}
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *shooter);
	ASSERT_EQ(baseline.actionType, EActionType::SHOOT);
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
	battle()->activeStack = shooter->unitId();
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	ASSERT_EQ(callback->action->target.size(), 1u);
	EXPECT_EQ(callback->action->target.front().hexValue, tail)
		<< "TUM must select the legal tail shot that damages three original allies";
	std::cout << "AI_TUM_HYPNOTIZE_WIDE_SPLASH homm3=" << baseline.toString()
		<< " tum=" << callback->action->toString() << '\n';
	const int shotsBefore = recorder.shots;
	const auto firstHealth = firstExtra->getAvailableHealth();
	const auto secondHealth = secondExtra->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The real VCMI server rejected TUM's tail-aimed Hypnotize shot";
	EXPECT_GT(recorder.shots, shotsBefore);
	EXPECT_LT(firstExtra->getAvailableHealth(), firstHealth);
	EXPECT_LT(secondExtra->getAvailableHealth(), secondHealth);
	policy.reset();
	// The tail shot is already an independently engine-accepted TUM action,
	// compared above with HoMM3's separate proposal. This fixture may then
	// finish before HoMM3 receives a turn; do not misreport that as a failed
	// mixed-policy duel. Continue under TUM on both sides to require a real
	// terminal battle result without claiming a HoMM3 head-to-head outcome.
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, tumHypnotizedArcherComparesShotAgainstMelee)
{
	prepareBattle(CreatureID(2), 20, CreatureID(0), 8);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * archer = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(2))
			archer = unit.get();
	ASSERT_NE(archer, nullptr);
	ASSERT_TRUE(archer->isShooter());
	const BattleHex targetHex(archer->getPosition().toInt() + 3);
	ASSERT_TRUE(targetHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(targetHex), nullptr);
	const CStack * formerAlly = addStack(BattleSide::ATTACKER, CreatureID(0), targetHex, 100);
	ASSERT_NE(formerAlly, nullptr);
	teachBattleSpell(defenderSideHero, SpellID::HYPNOTIZE, 20);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::HYPNOTIZE, archer));
	ASSERT_TRUE(archer->isHypnotized());
	const auto player = battle()->sideToPlayer(BattleSide::DEFENDER);
	ASSERT_EQ(battle()->battleGetOwner(archer), player);
	battle()->activeStack = archer->unitId();

	const auto battleID = battle()->getBattleID();
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanShoot(archer, formerAlly->getPosition()))
		<< "The regression requires a real legal shot";
	const auto score = [&](BattleHex from, bool shooting, int distance)
	{
		BattleAttackInfo info(archer, formerAlly, distance, shooting);
		info.attackerPos = from;
		DamageEstimation retaliation;
		const auto damage = view->battleEstimateDamage(info, &retaliation);
		const double dealt = std::min(static_cast<double>(formerAlly->getAvailableHealth()),
			(static_cast<double>(damage.damage.min) + damage.damage.max) / 2.0);
		const double received = shooting ? 0.0 : std::min(static_cast<double>(archer->getAvailableHealth()),
			(static_cast<double>(retaliation.damage.min) + retaliation.damage.max) / 2.0);
		return AIIntegration::Ports::TUM::hypnotizedAttackDamageSum(dealt, received);
	};
	const double shotScore = score(archer->getPosition(), true, 0);
	const auto reach = view->getReachability(archer);
	const auto available = view->battleGetAvailableHexes(reach, archer, false);
	double bestMeleeScore = -1;
	for(const auto & hex : available)
		if(hex.isAvailable() && !view->meleeAttackHexes(archer, formerAlly, hex).empty())
			bestMeleeScore = std::max(bestMeleeScore, score(hex, false, reach.distances[hex.toInt()]));
	ASSERT_GT(bestMeleeScore, shotScore)
		<< "Test needs genuine melee superiority over the available shot";

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
	policy->activeStack(battleID, archer);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK)
		<< "TUM must compare legal shot and melee by the same Hypnotize score";
	ASSERT_GE(callback->action->target.size(), 2u);
	EXPECT_EQ(view->battleGetUnitByPos(callback->action->target[1].hexValue), formerAlly);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "VCMI server rejected the Hypnotized archer's chosen melee strike";
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics", "TUMEnchantedTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
}

// General VCMI damage previews omit RANGED_RETALIATION; actual combat
// may countershoot. Test the published TUM Hypnotize damage-sum choice
// against a baseline and the authoritative server's return shot.
TEST_F(AIIntegrationNativeBattle, tumHypnotizedArcherValuesRangedRetaliation)
{
	// Use actual pre-battle army stacks rather than BATTLE_SETUP-pending
	// spawned stacks: only fully initialized archers possess live ammunition.
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(2), 20);
	setArmy(defenderSideHero, CreatureID(0), 8);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(attackerSideHero->id, SlotID(1)), CreatureID(2).toCreature(), 60));
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(attackerSideHero->id, SlotID(2)), CreatureID(2).toCreature(), 60));
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	battle()->obstacles.clear();
	CStack * archer = nullptr;
	CStack * ordinary = nullptr;
	CStack * retaliator = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() != BattleSide::ATTACKER || unit->creatureId() != CreatureID(2))
			continue;
		if(unit->unitSlot() == SlotID(0))
			archer = unit.get();
		else if(unit->unitSlot() == SlotID(1))
			ordinary = unit.get();
		else if(unit->unitSlot() == SlotID(2))
			retaliator = unit.get();
	}
	ASSERT_NE(archer, nullptr);
	ASSERT_NE(ordinary, nullptr);
	ASSERT_NE(retaliator, nullptr);
	ASSERT_TRUE(ordinary->canShoot());
	ASSERT_TRUE(retaliator->canShoot());
	const BattleHex ordinaryHex(archer->getPosition().toInt() + 8);
	const BattleHex counterHex(archer->getPosition().toInt() + 9);
	ASSERT_TRUE(ordinaryHex.isAvailable());
	ASSERT_TRUE(counterHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(ordinaryHex), nullptr);
	ASSERT_EQ(battle()->battleGetUnitByPos(counterHex), nullptr);
	// Fixture-only positions isolate two legal shots from melee alternatives.
	ordinary->setPosition(ordinaryHex);
	retaliator->setPosition(counterHex);
	retaliator->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::RANGED_RETALIATION, BonusSource::CREATURE_ABILITY,
		1, BonusSourceID(CreatureID(2))));
	ASSERT_TRUE(retaliator->hasBonusOfType(BonusType::RANGED_RETALIATION));
	// VCMI battleMatchOwner compares a defender's original battle side,
	// so a normal former ally cannot return fire on its hypnotized side.
	// Give this fixture native Berserk targeting too: the actual server
	// then permits ranged retaliation. This is not a natural TUM proc.
	retaliator->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::ATTACKS_NEAREST_CREATURE, BonusSource::CREATURE_ABILITY,
		1, BonusSourceID(CreatureID(2))));
	ASSERT_TRUE(retaliator->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
	// The ordinary defender may be adjacent to the return-fire shooter.
	// Guarantee the fixture's ranged action with native FREE_SHOOTING,
	// rather than silently accepting a blocked counterattack preview.
	retaliator->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::FREE_SHOOTING, BonusSource::CREATURE_ABILITY,
		1, BonusSourceID(CreatureID(2))));
	ASSERT_TRUE(retaliator->canShootBlocked());
	teachBattleSpell(defenderSideHero, SpellID::HYPNOTIZE, 20);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::HYPNOTIZE, archer));
	ASSERT_TRUE(archer->isHypnotized());
	const auto player = battle()->sideToPlayer(BattleSide::DEFENDER);
	ASSERT_EQ(battle()->battleGetOwner(archer), player);
	battle()->activeStack = archer->unitId();

	const auto battleID = battle()->getBattleID();
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanShoot(archer, ordinary->getPosition()));
	ASSERT_TRUE(view->battleCanShoot(archer, retaliator->getPosition()));
	ASSERT_TRUE(view->battleCanShoot(retaliator, archer->getPosition()));
	ASSERT_TRUE(retaliator->ableToRetaliate());
	const auto reach = view->getReachability(archer);
	const auto available = view->battleGetAvailableHexes(reach, archer, false);
	for(const auto & hex : available)
	{
		if(!hex.isAvailable())
			continue;
		EXPECT_TRUE(view->meleeAttackHexes(archer, ordinary, hex).empty());
		EXPECT_TRUE(view->meleeAttackHexes(archer, retaliator, hex).empty());
	}
	BattleAttackInfo counter(retaliator, archer, 0, true);
	counter.attackerPos = retaliator->getPosition();
	const auto counterEstimate = view->battleEstimateDamage(counter, nullptr);
	ASSERT_GT(counterEstimate.damage.min, 0);
	// The server processes first-shot losses before a ranged return shot.
	// Prove that a copy of the actual target, after the guaranteed minimum
	// shot damage, has less counterfire than the untouched live stack.
	// This is also a guard against preview mutating the authoritative state.
	const auto originalRetaliatorHealth = retaliator->getAvailableHealth();
	BattleAttackInfo firstShot(archer, retaliator, 0, true);
	firstShot.attackerPos = archer->getPosition();
	const auto firstShotEstimate = view->battleEstimateDamage(firstShot, nullptr);
	ASSERT_GT(firstShotEstimate.damage.min, 0);
	auto survivor = retaliator->acquireState();
	auto minimumShotDamage = firstShotEstimate.damage.min;
	survivor->damage(minimumShotDamage);
	ASSERT_TRUE(survivor->alive());
	ASSERT_LT(survivor->getAvailableHealth(), originalRetaliatorHealth);
	BattleAttackInfo survivorCounter(survivor.get(), archer, 0, true);
	survivorCounter.attackerPos = retaliator->getPosition();
	const auto survivorEstimate = view->battleEstimateDamage(survivorCounter, nullptr);
	ASSERT_LT(survivorEstimate.damage.max, counterEstimate.damage.max)
		<< "The fixture must expose casualty-sensitive native ranged retaliation";
	EXPECT_EQ(retaliator->getAvailableHealth(), originalRetaliatorHealth)
		<< "Ranged retaliation preview may not injure the live target";
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *archer);
	ASSERT_EQ(baseline.actionType, EActionType::SHOOT);
	ASSERT_FALSE(baseline.target.empty());
	EXPECT_EQ(baseline.target.front().unitValue, ordinary->unitId())
		<< "This fixture requires a differing baseline decision";

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
	policy->activeStack(battleID, archer);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	ASSERT_FALSE(callback->action->target.empty());
	EXPECT_EQ(callback->action->target.front().unitValue, retaliator->unitId())
		<< "TUM should reward the incoming ranged countershot";
	EXPECT_EQ(retaliator->getAvailableHealth(), originalRetaliatorHealth)
		<< "Independent TUM scoring must not mutate the enemy's live stack";
	bool serverCounterShot = false;
	const int retaliatorId = retaliator->unitId();
	recorder.onAttackBeforeApply = [&](const BattleAttack & attack)
	{
		if(attack.stackAttacking == retaliatorId && attack.shot() && attack.counter())
			serverCounterShot = true;
	};
	const int64_t damageBefore = recorder.damage;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The real VCMI battle processor rejected this choice";
	EXPECT_TRUE(serverCounterShot) << "The VCMI server did not execute ranged retaliation";
	EXPECT_GT(recorder.damage, damageBefore);
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics", "TUMEnchantedTactics");
	EXPECT_GT(recorder.shots, 1);
}

// VCMI's BattleActionProcessor::makeRangedAttack resolves ranged FIRST_STRIKE
// before the first shot, unlike ordinary RANGED_RETALIATION. TUM's published
// Hypnotize sum must value the *uninjured* first-strike countershot. The native
// bonus setup below is a fixture, not a claim about the closed TUM DLL.
TEST_F(AIIntegrationNativeBattle, tumHypnotizedFirstStrikeRangedCounterPrecedesShot)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(2), 20);
	setArmy(defenderSideHero, CreatureID(0), 8);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(attackerSideHero->id, SlotID(1)), CreatureID(2).toCreature(), 60));
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(attackerSideHero->id, SlotID(2)), CreatureID(2).toCreature(), 60));
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	battle()->obstacles.clear();

	CStack * hypnotized = nullptr;
	CStack * ordinary = nullptr;
	CStack * firstStriker = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() != BattleSide::ATTACKER || unit->creatureId() != CreatureID(2))
			continue;
		if(unit->unitSlot() == SlotID(0))
			hypnotized = unit.get();
		else if(unit->unitSlot() == SlotID(1))
			ordinary = unit.get();
		else if(unit->unitSlot() == SlotID(2))
			firstStriker = unit.get();
	}
	ASSERT_NE(hypnotized, nullptr);
	ASSERT_NE(ordinary, nullptr);
	ASSERT_NE(firstStriker, nullptr);
	ASSERT_TRUE(ordinary->canShoot());
	ASSERT_TRUE(firstStriker->canShoot());
	const BattleHex normalHex(hypnotized->getPosition().toInt() + 8);
	const BattleHex firstHex(hypnotized->getPosition().toInt() + 9);
	ASSERT_TRUE(normalHex.isAvailable());
	ASSERT_TRUE(firstHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(normalHex), nullptr);
	ASSERT_EQ(battle()->battleGetUnitByPos(firstHex), nullptr);
	ordinary->setPosition(normalHex);
	firstStriker->setPosition(firstHex);

	const auto ability = [&](BonusType kind, BonusSubtypeID subtype = BonusSubtypeID())
	{
		firstStriker->addNewBonus(std::make_shared<Bonus>(
			BonusDuration::PERMANENT, kind, BonusSource::CREATURE_ABILITY,
			1, BonusSourceID(CreatureID(2)), subtype));
	};
	ability(BonusType::RANGED_RETALIATION);
	ability(BonusType::ATTACKS_NEAREST_CREATURE);
	ability(BonusType::FREE_SHOOTING);
	ability(BonusType::FIRST_STRIKE, BonusSubtypeID(BonusCustomSubtype::damageTypeRanged));
	const auto firstStrikeSelector = Selector::typeSubtype(
		BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeRanged);
	ASSERT_TRUE(firstStriker->hasBonus(firstStrikeSelector));
	ASSERT_TRUE(firstStriker->hasBonusOfType(BonusType::RANGED_RETALIATION));

	teachBattleSpell(defenderSideHero, SpellID::HYPNOTIZE, 20);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::HYPNOTIZE, hypnotized));
	ASSERT_TRUE(hypnotized->isHypnotized());
	const auto player = battle()->sideToPlayer(BattleSide::DEFENDER);
	ASSERT_EQ(battle()->battleGetOwner(hypnotized), player);
	battle()->activeStack = hypnotized->unitId();
	const auto battleID = battle()->getBattleID();
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanShoot(hypnotized, ordinary->getPosition()));
	ASSERT_TRUE(view->battleCanShoot(hypnotized, firstStriker->getPosition()));
	ASSERT_TRUE(view->battleCanShoot(firstStriker, hypnotized->getPosition()));
	ASSERT_TRUE(firstStriker->ableToRetaliate());

	// Prove that first-strike order matters numerically: an undamaged
	// countershot is stronger than one after the initial attack's losses.
	BattleAttackInfo fullCounter(firstStriker, hypnotized, 0, true);
	fullCounter.attackerPos = firstStriker->getPosition();
	const auto fullCounterDamage = view->battleEstimateDamage(fullCounter, nullptr);
	BattleAttackInfo firstShot(hypnotized, firstStriker, 0, true);
	firstShot.attackerPos = hypnotized->getPosition();
	const auto firstDamage = view->battleEstimateDamage(firstShot, nullptr);
	ASSERT_GT(firstDamage.damage.min, 0);
	ASSERT_LT(firstDamage.damage.max, firstStriker->getAvailableHealth());
	const auto originalHealth = firstStriker->getAvailableHealth();
	auto reduced = firstStriker->acquireState();
	auto minShot = firstDamage.damage.min;
	reduced->damage(minShot);
	ASSERT_TRUE(reduced->alive());
	BattleAttackInfo reducedCounter(reduced.get(), hypnotized, 0, true);
	reducedCounter.attackerPos = firstStriker->getPosition();
	const auto reducedCounterDamage = view->battleEstimateDamage(reducedCounter, nullptr);
	ASSERT_GT(fullCounterDamage.damage.max, reducedCounterDamage.damage.max)
		<< "A first-strike ranged counter must not be priced after casualties";

	// TUM must also price its outgoing shot AFTER the server's FIRST_STRIKE,
	// including the possibility that the entire hypnotized stack dies.
	// All previews operate on copies, and must preserve both live health pools.
	const auto hypnotizedHealth = hypnotized->getAvailableHealth();
	const auto previewFollowUp = [&](int64_t counterDamage)
	{
		auto survivors = hypnotized->acquireState();
		survivors->damage(counterDamage);
		if(!survivors->alive())
			return 0.0;
		BattleAttackInfo followUp(survivors.get(), firstStriker, 0, true);
		followUp.attackerPos = hypnotized->getPosition();
		followUp.defenderPos = firstStriker->getPosition();
		const auto estimate = view->battleEstimateDamage(followUp, nullptr);
		return (static_cast<double>(estimate.damage.min) + estimate.damage.max) / 2.0;
	};
	const double postCounterShot = (previewFollowUp(fullCounterDamage.damage.min)
		+ previewFollowUp(fullCounterDamage.damage.max)) / 2.0;
	const double preCounterShot = (static_cast<double>(firstDamage.damage.min)
		+ firstDamage.damage.max) / 2.0;
	ASSERT_LT(postCounterShot, preCounterShot)
		<< "First-strike casualties must reduce TUM's outgoing shot projection";
	EXPECT_EQ(hypnotized->getAvailableHealth(), hypnotizedHealth);
	EXPECT_EQ(firstStriker->getAvailableHealth(), originalHealth);

	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *hypnotized);
	ASSERT_EQ(baseline.actionType, EActionType::SHOOT);
	ASSERT_FALSE(baseline.target.empty());
	EXPECT_EQ(baseline.target.front().unitValue, ordinary->unitId());
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
	policy->activeStack(battleID, hypnotized);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	ASSERT_FALSE(callback->action->target.empty());
	EXPECT_EQ(callback->action->target.front().unitValue, firstStriker->unitId())
		<< "TUM's independent damage-sum choice should include the early countershot";
	EXPECT_EQ(firstStriker->getAvailableHealth(), originalHealth)
		<< "A damage preview cannot modify the actual first-strike target";

	std::vector<int> initialShotOrder;
	const int firstStrikerId = firstStriker->unitId();
	const int hypnotizedId = hypnotized->unitId();
	recorder.onAttackBeforeApply = [&](const BattleAttack & attack)
	{
		if(!attack.shot())
			return;
		if(attack.stackAttacking == firstStrikerId && attack.counter())
			initialShotOrder.push_back(firstStrikerId);
		else if(attack.stackAttacking == hypnotizedId)
			initialShotOrder.push_back(hypnotizedId);
	};
	const auto recordedDamage = recorder.damage;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "VCMI rejected TUM's chosen shot against the first-strike defender";
	ASSERT_GE(initialShotOrder.size(), 2u);
	EXPECT_EQ(initialShotOrder[0], firstStrikerId)
		<< "The authoritative server must execute FIRST_STRIKE before our shot";
	EXPECT_EQ(initialShotOrder[1], hypnotizedId);
	EXPECT_GT(recorder.damage, recordedDamage);
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics", "TUMEnchantedTactics");
	EXPECT_GT(recorder.shots, 1);
}

TEST_F(AIIntegrationNativeBattle, tumTacticsPropagatesDefenderVictory)
{
	prepareBattle(CreatureID(0), 2, CreatureID(0), 100);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(BattleSide::DEFENDER, false, "TUMEnchantedTactics");
}


TEST_F(AIIntegrationNativeBattle, tumCitadelArrowTowerSendsNativeShotBeforeCompletingSiege)
{
	// The pinned TUM document advertises improved arrow-tower targeting, but
	// does not publish its numerical decision rule. Exercise our explicit
	// damage-aware native projection rather than implying closed-DLL parity.
	// A real citadel is required: an ordinary FORT has no arrow-tower unit.
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 160);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(attackerSideHero->id, SlotID(1)), CreatureID(0).toCreature(), 160));
	const auto * town = findFirst<CGTownInstance>();
	ASSERT_NE(town, nullptr);
	if(town->fortificationsLevel().wallsHealth <= 0)
	{
		ASSERT_TRUE(gameHandler->buildStructure(town->id, BuildingID::FORT, true));
	}
	if(town->fortificationsLevel().citadelHealth <= 0)
	{
		ASSERT_TRUE(gameHandler->buildStructure(town->id, BuildingID::CITADEL, true));
	}
	ASSERT_GT(town->fortificationsLevel().citadelHealth, 0);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(town->id, SlotID(0)), CreatureID(0).toCreature(), 80));
	const auto layout = BattleLayout::createDefaultLayout(*gameState(), attackerSideHero, town);
	gameHandler->battles->startBattle(attackerSideHero, town, town->visitablePos(),
		attackerSideHero, nullptr, layout, town);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);

	const CStack * tower = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->alive() && unit->unitSide() == BattleSide::DEFENDER && unit->isTurret())
			tower = unit.get();
	ASSERT_NE(tower, nullptr) << "The actual siege must generate a native arrow-tower stack";
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::DEFENDER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);

	const CStack * expected = nullptr;
	double bestValue = -1;
	for(const auto * enemy : view->battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY))
	{
		if(!enemy->alive() || enemy->isInvincible() || !enemy->getPosition().isValid()
			|| !view->battleCanShoot(tower, enemy->getPosition()))
			continue;
		BattleAttackInfo info(tower, enemy, 0, true);
		info.attackerPos = tower->getPosition();
		DamageEstimation retaliation;
		const auto projection = view->battleEstimateDamage(info, &retaliation);
		const double health = static_cast<double>(enemy->getAvailableHealth());
		if(health <= 0)
			continue;
		const double meanDamage = (static_cast<double>(projection.damage.min)
			+ static_cast<double>(projection.damage.max)) / 2.0;
		const double value = static_cast<double>(enemy->estimateCombatValue())
			* std::clamp(meanDamage, 0.0, health) / health;
		if(!expected || value > bestValue)
		{
			expected = enemy;
			bestValue = value;
		}
	}
	ASSERT_NE(expected, nullptr) << "Native arrow tower must have a legal target";
	// The published TUM prohibition on hitting Blind/Paralyze/Petrification
	// applies to BALLISTA specifically. A tower instead follows this
	// partial port's native projected casualty-value ranking, which can
	// change when the target's combat state changes. Verify it uses the
	// current engine estimate rather than its earlier, stale forecast.
	const int beforeDisableTargetId = expected->unitId();
	CStack * disabledTowerTarget = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitId() == beforeDisableTargetId)
			disabledTowerTarget = unit.get();
	}
	ASSERT_NE(disabledTowerTarget, nullptr);
	disabledTowerTarget->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::NOT_ACTIVE, BonusSource::SPELL_EFFECT,
		1, BonusSourceID(SpellID(SpellID::BLIND)), BonusSubtypeID(SpellID(SpellID::BLIND))));
	ASSERT_TRUE(disabledTowerTarget->hasBonusOfType(BonusType::NOT_ACTIVE));
	ASSERT_TRUE(view->battleCanShoot(tower, disabledTowerTarget->getPosition()))
		<< "Native legal target status and preferred target value are distinct";
	expected = nullptr;
	bestValue = -1;
	for(const auto * enemy : view->battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY))
	{
		if(!enemy->alive() || enemy->isInvincible() || !enemy->getPosition().isValid()
			|| !view->battleCanShoot(tower, enemy->getPosition()))
			continue;
		BattleAttackInfo info(tower, enemy, 0, true);
		info.attackerPos = tower->getPosition();
		DamageEstimation retaliation;
		const auto projection = view->battleEstimateDamage(info, &retaliation);
		const double health = static_cast<double>(enemy->getAvailableHealth());
		if(health <= 0)
			continue;
		const double meanDamage = (static_cast<double>(projection.damage.min)
			+ static_cast<double>(projection.damage.max)) / 2.0;
		const double value = static_cast<double>(enemy->estimateCombatValue())
			* std::clamp(meanDamage, 0.0, health) / health;
		if(!expected || value > bestValue)
		{
			expected = enemy;
			bestValue = value;
		}
	}
	ASSERT_NE(expected, nullptr);
	EXPECT_NE(expected->unitId(), beforeDisableTargetId)
		<< "The native valuation must respond to this real inactive-state bonus";
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *tower);
	ASSERT_EQ(baseline.actionType, EActionType::SHOOT)
		<< "The independent HoMM3 baseline must have a legal tower shot";

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
	policy->activeStack(battleID, tower);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT)
		<< "TUM's native citadel tower may not fall back to ground movement";
	ASSERT_FALSE(callback->action->target.empty());
	EXPECT_EQ(callback->action->target.front().unitValue, expected->unitId())
		<< "The tower must choose the largest engine-projected casualty value";
	std::cout << "AI_TUM_TOWER_DECISION baseline=" << baseline.toString()
		<< " tum=" << callback->action->toString() << '\n';

	// The preview above is not engine evidence. Force this specific native
	// tower activation through the authoritative VCMI server, then continue
	// the same siege until the result is applied to the adventure state.
	battle()->activeStack = tower->unitId();
	const int beforeShots = recorder.shots;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The real server rejected TUM's independently selected tower shot";
	EXPECT_GT(recorder.shots, beforeShots);
	policy.reset();
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.damage, 0);
}

// The closed TUM plugin does not publish its arrow-tower formula. Prove that
// our *explicitly partial* native casualty-value projection can choose a
// different real siege target from the original partial HoMM3 attack score.
// HoMM3 discounts an incapacitated ranged target; this TUM tower projection
// instead estimates the combat value actually removed by its next legal shot.
// This fixture injects Blind as test status, not as an observed creature proc.
TEST_F(AIIntegrationNativeBattle, tumCitadelTowerDiffersFromHoMM3OnDisabledHighValueStack)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 300);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(attackerSideHero->id, SlotID(1)), CreatureID(0).toCreature(), 1));
	const auto * town = findFirst<CGTownInstance>();
	ASSERT_NE(town, nullptr);
	if(town->fortificationsLevel().wallsHealth <= 0)
	{
		ASSERT_TRUE(gameHandler->buildStructure(town->id, BuildingID::FORT, true));
	}
	if(town->fortificationsLevel().citadelHealth <= 0)
	{
		ASSERT_TRUE(gameHandler->buildStructure(town->id, BuildingID::CITADEL, true));
	}
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(town->id, SlotID(0)), CreatureID(0).toCreature(), 60));
	const auto layout = BattleLayout::createDefaultLayout(*gameState(), attackerSideHero, town);
	gameHandler->battles->startBattle(attackerSideHero, town, town->visitablePos(),
		attackerSideHero, nullptr, layout, town);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);

	const CStack * tower = nullptr;
	CStack * largeTarget = nullptr;
	const CStack * smallTarget = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->alive() && unit->unitSide() == BattleSide::DEFENDER && unit->isTurret())
			tower = unit.get();
		else if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
		{
			if(unit->unitSlot() == SlotID(0))
				largeTarget = unit.get();
			else if(unit->unitSlot() == SlotID(1))
				smallTarget = unit.get();
		}
	}
	ASSERT_NE(tower, nullptr);
	ASSERT_NE(largeTarget, nullptr);
	ASSERT_NE(smallTarget, nullptr);
	ASSERT_GT(largeTarget->getCount(), smallTarget->getCount())
		<< "Native siege deployment must preserve the intended value gap";
	// The one-creature alternative is a meaningful low-casualty shot: TUM's
	// actual projected-loss calculation must still prefer the larger disabled
	// stack while HoMM3's documented incapacitation discount favors this one.
	largeTarget->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::NOT_ACTIVE, BonusSource::SPELL_EFFECT,
		1, BonusSourceID(SpellID(SpellID::BLIND)), BonusSubtypeID(SpellID(SpellID::BLIND))));
	ASSERT_TRUE(largeTarget->hasBonusOfType(BonusType::NOT_ACTIVE));

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::DEFENDER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanShoot(tower, largeTarget->getPosition()));
	ASSERT_TRUE(view->battleCanShoot(tower, smallTarget->getPosition()));
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *tower);
	ASSERT_EQ(baseline.actionType, EActionType::SHOOT);
	ASSERT_FALSE(baseline.target.empty());

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
	policy->activeStack(battleID, tower);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	ASSERT_FALSE(callback->action->target.empty());
	std::cout << "AI_TUM_TOWER_DIFFERENTIAL homm3=" << baseline.toString()
		<< " tum=" << callback->action->toString() << '\n';
	EXPECT_EQ(baseline.target.front().unitValue, smallTarget->unitId())
		<< "HoMM3's incapacitation discount should favor the active stack";
	EXPECT_EQ(callback->action->target.front().unitValue, largeTarget->unitId())
		<< "TUM's bounded native projected-loss score should favor the larger stack";
	EXPECT_NE(callback->action->toString(), baseline.toString());

	// Require the specific proposed TUM tower shot to be accepted and applied
	// by the authoritative battle processor, not just a test callback.
	battle()->activeStack = tower->unitId();
	const int shotsBefore = recorder.shots;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action));
	EXPECT_GT(recorder.shots, shotsBefore);
	policy.reset();
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.damage, 0);
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
// The source document requires smart ballista selection without disclosing a
// numeric formula. Our partial TUM projection must value the *casualties of a
// legal shot* rather than blindly target the largest whole-stack AI value.
// Compare both metrics from the same authoritative engine state, then let
// the server execute its normal ballista activation and finish the battle.
// TUM's documented ballista targeting is only reached through the battle-AI
// callback when the native server delegates manual control (for example,
// Artillery). With no Artillery, BattleFlowProcessor owns CPU_CONTROLLED
// war-machine actions before any TUM activeStack() call. Record that boundary
// in a fully resolved server battle: an automatic ballista shot must NOT be
// misreported as an independent TUM tactical decision.
TEST_F(AIIntegrationNativeBattle, tumUnskilledBallistaIsNativeAutomaticNotTUMDecision)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 60);
	setArmy(defenderSideHero, CreatureID(0), 60);
	giveArtifact(attackerSideHero, ArtifactID(ArtifactID::BALLISTA), ArtifactPosition::MACH1);
	ASSERT_EQ(attackerSideHero->valOfBonuses(BonusType::MANUAL_CONTROL,
		BonusSubtypeID(CreatureID(CreatureID::BALLISTA))), 0)
		<< "Fixture must not provide Artillery or another manual-control bonus";
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	const CStack * machine = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER
			&& unit->creatureId() == CreatureID::BALLISTA)
			machine = unit.get();
	ASSERT_NE(machine, nullptr);
	ASSERT_TRUE(machine->hasBonusOfType(BonusType::CPU_CONTROLLED));
	// Both armies contain only melee Peasants. Any real SHOOT packet therefore
	// came from the engine-controlled ballista, not a creature archer.
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_EQ(ballistaDecisions, 0)
		<< "CPU-controlled ballista must not be credited to TUM activeStack";
	EXPECT_GT(recorder.shots, 0)
		<< "The native battle engine must actually fire the automatic ballista";
	EXPECT_GT(recorder.damage, 0);
}

// The native server runs ranged FIRST_STRIKE retaliation before a war
// machine's opening shot. TUM's partial volley ranking must not promise
// casualties from a ballista that is guaranteed to die before firing.
// The unusually large enemy archer stack and injected combat abilities
// are test fixtures, not claims about a naturally occurring TUM creature.
TEST_F(AIIntegrationNativeBattle, tumBallistaAvoidsGuaranteedFatalFirstStrikeCountershot)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(11), 80);
	setArmy(defenderSideHero, CreatureID(2), 5000);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(defenderSideHero->id, SlotID(1)), CreatureID(0).toCreature(), 20));
	giveArtifact(attackerSideHero, ArtifactID(ArtifactID::BALLISTA), ArtifactPosition::MACH1);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::ARTILLERY),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);

	const CStack * ballista = nullptr;
	CStack * firstStriker = nullptr;
	const CStack * safeTarget = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER
			&& unit->creatureId() == CreatureID::BALLISTA)
			ballista = unit.get();
		else if(unit->unitSide() == BattleSide::DEFENDER
			&& unit->creatureId() == CreatureID(2))
			firstStriker = unit.get();
		else if(unit->unitSide() == BattleSide::DEFENDER
			&& unit->creatureId() == CreatureID(0))
			safeTarget = unit.get();
	}
	ASSERT_NE(ballista, nullptr);
	ASSERT_NE(firstStriker, nullptr);
	ASSERT_NE(safeTarget, nullptr);
	firstStriker->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::RANGED_RETALIATION, BonusSource::CREATURE_ABILITY,
		1, BonusSourceID(CreatureID(2))));
	firstStriker->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::FIRST_STRIKE, BonusSource::CREATURE_ABILITY,
		1, BonusSourceID(CreatureID(2)), BonusSubtypeID(BonusCustomSubtype::damageTypeRanged)));
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanShoot(ballista, firstStriker->getPosition()));
	ASSERT_TRUE(view->battleCanShoot(ballista, safeTarget->getPosition()));
	ASSERT_TRUE(view->battleCanShoot(firstStriker, ballista->getPosition()));
	ASSERT_TRUE(firstStriker->ableToRetaliate());
	BattleAttackInfo firstCounter(firstStriker, ballista, 0, true);
	firstCounter.attackerPos = firstStriker->getPosition();
	firstCounter.defenderPos = ballista->getPosition();
	const auto fatal = view->battleEstimateDamage(firstCounter, nullptr);
	ASSERT_GE(fatal.damage.min, ballista->getAvailableHealth())
		<< "Fixture must guarantee a fatal native countershot before the ballista acts";

	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *ballista);
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
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	ASSERT_FALSE(callback->action->target.empty());
	EXPECT_EQ(callback->action->target.front().unitValue, safeTarget->unitId())
		<< "A guaranteed fatal pre-shot counter cannot generate a predicted ballista volley";
	std::cout << "AI_TUM_FIRST_STRIKE_SIEGE baseline=" << baseline.toString()
		<< " tum=" << callback->action->toString() << '\n';

	// The proposal alone does not prove a valid action. Require server
	// acceptance, a real attack event, and an applied final battle result.
	battle()->activeStack = ballista->unitId();
	const int shotsBefore = recorder.shots;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action));
	EXPECT_GT(recorder.shots, shotsBefore);
	policy.reset();
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, tumBallistaValuesEngineProjectedLoss)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(11), 80);
	setArmy(defenderSideHero, CreatureID(0), 350);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(defenderSideHero->id, SlotID(1)), CreatureID(11).toCreature(), 8));
	ASSERT_FALSE(HasFatalFailure());
	giveArtifact(attackerSideHero, ArtifactID(ArtifactID::BALLISTA), ArtifactPosition::MACH1);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::ARTILLERY),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);

	const CStack * ballista = nullptr;
	std::vector<const CStack *> enemies;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID::BALLISTA)
			ballista = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->alive())
			enemies.push_back(unit.get());
	}
	ASSERT_NE(ballista, nullptr);
	ASSERT_EQ(enemies.size(), 2u);
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);

	const CStack * largestStack = nullptr;
	const CStack * greatestProjectedLoss = nullptr;
	double greatestRaw = -1;
	double greatestLoss = -1;
	for(const auto * enemy : enemies)
	{
		ASSERT_TRUE(view->battleCanShoot(ballista, enemy->getPosition()));
		ASSERT_GT(enemy->getAvailableHealth(), 0);
		const double raw = static_cast<double>(enemy->estimateCombatValue());
		BattleAttackInfo info(ballista, enemy, 0, true);
		info.attackerPos = ballista->getPosition();
		DamageEstimation retaliation;
		const auto projected = view->battleEstimateDamage(info, &retaliation);
		const double expectedCasualties = std::clamp(
			(static_cast<double>(projected.damage.min) + static_cast<double>(projected.damage.max)) / 2.0,
			0.0, static_cast<double>(enemy->getAvailableHealth()));
		const double loss = raw * expectedCasualties / static_cast<double>(enemy->getAvailableHealth());
		ASSERT_GT(loss, 0) << "Fixture requires a meaningful native ballista shot";
		if(raw > greatestRaw)
		{
			greatestRaw = raw;
			largestStack = enemy;
		}
		if(loss > greatestLoss)
		{
			greatestLoss = loss;
			greatestProjectedLoss = enemy;
		}
	}
	ASSERT_NE(greatestProjectedLoss, nullptr);
	ASSERT_NE(largestStack, nullptr);
	ASSERT_NE(greatestProjectedLoss->unitId(), largestStack->unitId())
		<< "Fixture must distinguish damage-aware selection from raw whole-stack value";

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
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	ASSERT_FALSE(callback->action->target.empty());
	EXPECT_EQ(callback->action->target.front().unitValue, greatestProjectedLoss->unitId())
		<< "TUM must aim at the greater engine-projected loss, not the largest original stack";
	// Keep the proposal and its native evaluation on the same battlefield
	// snapshot. Other units can deal damage before the ballista's first
	// regularly scheduled activation and change the optimal target.
	// Direct a test-only ballista activation through the real server gate;
	// this is not evidence of the scheduler selecting this activation first.
	battle()->activeStack = ballista->unitId();
	const int shotsBefore = recorder.shots;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The native server rejected the damage-aware ballista shot";
	EXPECT_GT(recorder.shots, shotsBefore) << "The accepted shot produced no native attack event";
	policy.reset();

	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.damage, 0);
}

// Published TUM ballista targeting is numeric-closed-source. This regression
// verifies only the VCMI volley-aware *partial* score: Expert Artillery grants
// extra shots, and a single proposed shot command must cause that many
// authoritative attacks against its selected target before the battle ends.
TEST_F(AIIntegrationNativeBattle, tumBallistaProjectedVolleyUsesRealArtilleryShots)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(11), 80);
	setArmy(defenderSideHero, CreatureID(0), 350);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(defenderSideHero->id, SlotID(1)), CreatureID(11).toCreature(), 8));
	giveArtifact(attackerSideHero, ArtifactID(ArtifactID::BALLISTA), ArtifactPosition::MACH1);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::ARTILLERY),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	const CStack * ballista = nullptr;
	std::vector<const CStack *> targets;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER
			&& unit->creatureId() == CreatureID::BALLISTA)
			ballista = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->alive())
			targets.push_back(unit.get());
	}
	ASSERT_NE(ballista, nullptr);
	ASSERT_EQ(targets.size(), 2u);
	const int heroAttacks = attackerSideHero->valOfBonuses(
		BonusType::HERO_GRANTS_ATTACKS, BonusSubtypeID(ballista->creatureId()));
	const int legalShots = std::min(ballista->shots.available(),
		ballista->getTotalAttacks(true) + heroAttacks);
	ASSERT_GE(legalShots, 2) << "Expert Artillery must grant actual extra shots";
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);

	// Calculate the expectation independently from the new TUM helper,
	// using only live engine damage, combat values and the native shot count.
	const CStack * expected = nullptr;
	double bestLoss = -1;
	std::sort(targets.begin(), targets.end(), [](const CStack * left, const CStack * right)
	{
		return left->unitId() < right->unitId();
	});
	for(const auto * target : targets)
	{
		ASSERT_TRUE(view->battleCanShoot(ballista, target->getPosition()));
		BattleAttackInfo info(ballista, target, 0, true);
		info.attackerPos = ballista->getPosition();
		DamageEstimation retaliation;
		const auto preview = view->battleEstimateDamage(info, &retaliation);
		const double health = static_cast<double>(target->getAvailableHealth());
		ASSERT_GT(health, 0);
		const double perHit = std::max(0.0, (static_cast<double>(preview.damage.min)
			+ static_cast<double>(preview.damage.max)) / 2.0);
		const double volleyLoss = static_cast<double>(target->estimateCombatValue())
			* std::min(health, perHit * legalShots) / health;
		if(!expected || volleyLoss > bestLoss)
		{
			expected = target;
			bestLoss = volleyLoss;
		}
	}
	ASSERT_NE(expected, nullptr);
	const auto homm3 = AIIntegration::HoMM3TacticsBattleAI::chooseAction(
		*view, *ballista);
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
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	ASSERT_FALSE(callback->action->target.empty());
	EXPECT_EQ(callback->action->target.front().unitValue, expected->unitId())
		<< "TUM must rank full actual Artillery volley, not only the first shot";
	std::cout << "AI_TUM_BALLISTA_VOLLEY homm3=" << homm3.toString()
		<< " tum=" << callback->action->toString()
		<< " expected_shots=" << legalShots << '\n';
	battle()->activeStack = ballista->unitId();
	const int shotsBefore = recorder.shots;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player,
		*callback->action)) << "Server rejected TUM's selected Artillery target";
	EXPECT_EQ(recorder.shots - shotsBefore, legalShots)
		<< "A single engine-accepted shot command must resolve the actual volley";
	policy.reset();
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, tumBallistaSelectsUnblindedTargetWhenAnotherIsBlind)
{
	// TUM AI Improvements.txt excludes Blind, Paralyze and Petrification
	// independently. The latter two can come from creature-source NOT_ACTIVE
	// effects, not SPELL_EFFECT. Status injection is fixture setup only;
	// every decision and battle outcome still goes through the real server.
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 60);
	setArmy(defenderSideHero, CreatureID(0), 50);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(defenderSideHero->id, SlotID(1)), CreatureID(0).toCreature(), 40));
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(defenderSideHero->id, SlotID(2)), CreatureID(0).toCreature(), 36));
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(defenderSideHero->id, SlotID(3)), CreatureID(0).toCreature(), 12));
	ASSERT_FALSE(HasFatalFailure());
	giveArtifact(attackerSideHero, ArtifactID(ArtifactID::BALLISTA), ArtifactPosition::MACH1);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::ARTILLERY),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	teachBattleSpell(attackerSideHero, SpellID::BLIND);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);

	const CStack * blinded = nullptr;
	CStack * paralyzed = nullptr;
	CStack * petrified = nullptr;
	CStack * validTarget = nullptr;
	const CStack * ballista = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
		{
			if(unit->getCount() == 50)
				blinded = unit.get();
			else if(unit->getCount() == 40)
				paralyzed = unit.get();
			else if(unit->getCount() == 36)
				petrified = unit.get();
			else if(unit->getCount() == 12)
				validTarget = unit.get();
		}
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID::BALLISTA)
			ballista = unit.get();
	}
	ASSERT_NE(blinded, nullptr);
	ASSERT_NE(paralyzed, nullptr);
	ASSERT_NE(petrified, nullptr);
	ASSERT_NE(validTarget, nullptr);
	ASSERT_NE(ballista, nullptr);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::BLIND, blinded));
	ASSERT_TRUE(blinded->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::BLIND)))));
	const auto addCreatureDisable = [&](CStack * target, SpellID status)
	{
		target->addNewBonus(std::make_shared<Bonus>(
			BonusDuration::PERMANENT, BonusType::NOT_ACTIVE, BonusSource::CREATURE_ABILITY,
			1, BonusSourceID(CreatureID(0)), BonusSubtypeID(status)));
		EXPECT_TRUE(target->hasBonus(Selector::typeSubtype(
			BonusType::NOT_ACTIVE, BonusSubtypeID(status))));
		EXPECT_FALSE(target->hasBonus(Selector::source(
			BonusSource::SPELL_EFFECT, BonusSourceID(status))));
	};
	addCreatureDisable(paralyzed, SpellID(SpellID::PARALYZE));
	addCreatureDisable(petrified, SpellID(SpellID::STONE_GAZE));
	// Negative control: a harmless native SPEED bonus carrying Blind's
	// source ID is NOT a Blind status. The previous broad source lookup
	// incorrectly discarded this otherwise valid ballista target.
	// This is explicit test setup, not a natural spell side effect.
	validTarget->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT,
		0, BonusSourceID(SpellID(SpellID::BLIND))));
	ASSERT_TRUE(validTarget->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::BLIND)))));
	ASSERT_FALSE(validTarget->hasBonusOfType(BonusType::NOT_ACTIVE));
	ASSERT_TRUE(blinded->hasBonusOfType(BonusType::NOT_ACTIVE))
		<< "The hero-cast Blind must remain a genuine native disable";
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
		<< "Ballista must skip real disables but not a harmless bonus with a Blind source ID";

	// All subsequent commands still come from actual active-unit events,
	// and the VCMI engine must accept them and finish this battle.
	const int validTargetId = validTarget->unitId();
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(ballistaShots, 0);
	ASSERT_TRUE(firstBallistaShotTarget.has_value()) << "The native battle never accepted a ballista shot";
	EXPECT_EQ(*firstBallistaShotTarget, validTargetId)
		<< "Native ballista shot must not wake any of the three disabled stacks";
}

// The TUM ballista must not wake an incapacitated enemy indirectly when
// VCMI grants it a real SHOOTS_ALL_ADJACENT attack. The vanilla ballista is
// single-target, so the area bonus is an explicit mod-capability fixture.
// Native impact geometry, an independent HoMM3 choice, an accepted ballista
// shot and the completed server battle are all checked.
TEST_F(AIIntegrationNativeBattle, tumBallistaAvoidsNativeSplashOntoParalyzedEnemy)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 70);
	setArmy(defenderSideHero, CreatureID(0), 1);
	giveArtifact(attackerSideHero, ArtifactID(ArtifactID::BALLISTA), ArtifactPosition::MACH1);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::ARTILLERY),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	battle()->obstacles.clear();

	CStack * ballista = nullptr;
	const CStack * safe = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER
			&& unit->creatureId() == CreatureID::BALLISTA)
			ballista = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER
			&& unit->creatureId() == CreatureID(0))
			safe = unit.get();
	}
	ASSERT_NE(ballista, nullptr);
	ASSERT_NE(safe, nullptr);
	ballista->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::SHOOTS_ALL_ADJACENT,
		BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(ballista->hasBonusOfType(BonusType::SHOOTS_ALL_ADJACENT));

	BattleHex clusterHex;
	BattleHex disabledHex;
	for(int index = 0; index < GameConstants::BFIELD_SIZE && !clusterHex.isValid(); ++index)
	{
		const BattleHex candidate(index);
		if(!candidate.isAvailable() || candidate.getX() < 8 || candidate.getX() > 11
			|| battle()->battleGetUnitByPos(candidate)
			|| BattleHex::getDistance(candidate, safe->getPosition()) < 5)
			continue;
		for(const auto & neighbour : candidate.getNeighbouringTiles())
		{
			if(neighbour.isAvailable() && !battle()->battleGetUnitByPos(neighbour)
				&& BattleHex::getDistance(neighbour, safe->getPosition()) >= 4)
			{
				clusterHex = candidate;
				disabledHex = neighbour;
				break;
			}
		}
	}
	ASSERT_TRUE(clusterHex.isValid());
	ASSERT_TRUE(disabledHex.isValid());
	const CStack * valuable = addStack(BattleSide::DEFENDER, CreatureID(0), clusterHex, 250);
	CStack * disabled = addStack(BattleSide::DEFENDER, CreatureID(0), disabledHex, 1);
	ASSERT_NE(valuable, nullptr);
	ASSERT_NE(disabled, nullptr);
	disabled->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::NOT_ACTIVE, BonusSource::CREATURE_ABILITY,
		1, BonusSourceID(CreatureID(0)), BonusSubtypeID(SpellID(SpellID::PARALYZE))));
	ASSERT_TRUE(disabled->hasBonusOfType(BonusType::NOT_ACTIVE));

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanShoot(ballista, valuable->getPosition()));
	ASSERT_TRUE(view->battleCanShoot(ballista, safe->getPosition()));
	const auto hitsDisabled = [&](const CStack * target)
	{
		const auto hits = view->getAttackedBattleUnits(
			ballista, target, target->getPosition(), true);
		return std::any_of(hits.begin(), hits.end(), [&](const battle::Unit * hit)
		{
			return hit && hit->unitId() == disabled->unitId();
		});
	};
	ASSERT_TRUE(hitsDisabled(valuable))
		<< "The engine must confirm splash from the valuable shot onto Paralyze";
	ASSERT_FALSE(hitsDisabled(safe))
		<< "The alternate native shot must leave Paralyze untouched";
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *ballista);
	ASSERT_EQ(baseline.actionType, EActionType::SHOOT);
	ASSERT_FALSE(baseline.target.empty());
	ASSERT_EQ(baseline.target.front().unitValue, valuable->unitId())
		<< "The fixture must distinguish the partial HoMM3 decision from TUM";

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
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	ASSERT_FALSE(callback->action->target.empty());
	EXPECT_EQ(callback->action->target.front().unitValue, safe->unitId())
		<< "TUM must reject a ballista shot that would wake Paralyze via native splash";
	const auto disabledHealthBefore = disabled->getAvailableHealth();
	battle()->activeStack = ballista->unitId();
	const int shotsBefore = recorder.shots;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player,
		*callback->action)) << "Server rejected the independently selected safe shot";
	EXPECT_GT(recorder.shots, shotsBefore);
	EXPECT_EQ(disabled->getAvailableHealth(), disabledHealthBefore)
		<< "The actual engine shot must not wake the disabled neighbour";
	std::cout << "AI_TUM_SPLASH_GUARD homm3=" << baseline.toString()
		<< " tum=" << callback->action->toString() << '\n';
	policy.reset();
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.damage, 0);
}

// The pinned TUM notes promise smarter ballista targeting but no numeric
// source formula. VCMI SHOOTS_ALL_ADJACENT affects hostile creatures around
// the aimed hex. This native fixture compares a larger isolated enemy against
// two smaller adjacent enemies whose combined projected casualties exceed it.
// The area-shot bonus is fixture-only, not a natural ballista ability.
TEST_F(AIIntegrationNativeBattle, tumBallistaValuesNativeAdjacentEnemySplash)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 70);
	setArmy(defenderSideHero, CreatureID(0), 80);
	giveArtifact(attackerSideHero, ArtifactID(ArtifactID::BALLISTA), ArtifactPosition::MACH1);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::ARTILLERY),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	battle()->obstacles.clear();

	CStack * ballista = nullptr;
	CStack * isolated = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->isBallista())
			ballista = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			isolated = unit.get();
	}
	ASSERT_NE(ballista, nullptr);
	ASSERT_NE(isolated, nullptr);
	// HoMM3's native target-time weighting favors a closer isolated stack.
	// Position it independently of the distant splash cluster, then verify
	// the two policies genuinely disagree on the SAME native battlefield.
	BattleHex closer;
	int shortestDistance = GameConstants::BFIELD_SIZE;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex candidate(index);
		if(!candidate.isAvailable() || candidate.getX() < 5 || candidate.getX() > 6
			|| battle()->battleGetUnitByPos(candidate))
			continue;
		const int distance = BattleHex::getDistance(ballista->getPosition(), candidate);
		if(distance < shortestDistance)
		{
			closer = candidate;
			shortestDistance = distance;
		}
	}
	ASSERT_TRUE(closer.isValid());
	isolated->setPosition(closer);
	ASSERT_EQ(isolated->getPosition(), closer);
	ballista->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::SHOOTS_ALL_ADJACENT,
		BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(ballista->hasBonusOfType(BonusType::SHOOTS_ALL_ADJACENT));

	BattleHex firstHex;
	BattleHex secondHex;
	BattleHex thirdHex;
	for(int index = 0; index < GameConstants::BFIELD_SIZE && !firstHex.isValid(); ++index)
	{
		const BattleHex candidate(index);
		if(!candidate.isAvailable() || candidate.getX() < 12 || candidate.getX() > 14
			|| battle()->battleGetUnitByPos(candidate)
			|| BattleHex::getDistance(candidate, isolated->getPosition()) < 6)
			continue;
		std::vector<BattleHex> neighbours;
		for(const auto & neighbour : candidate.getNeighbouringTiles())
		{
			if(neighbour.isAvailable() && !battle()->battleGetUnitByPos(neighbour)
				&& BattleHex::getDistance(neighbour, isolated->getPosition()) >= 5)
				neighbours.push_back(neighbour);
		}
		if(neighbours.size() >= 2)
		{
			firstHex = candidate;
			secondHex = neighbours[0];
			thirdHex = neighbours[1];
		}
	}
	ASSERT_TRUE(firstHex.isValid());
	ASSERT_TRUE(secondHex.isValid());
	ASSERT_TRUE(thirdHex.isValid());
	const CStack * first = addStack(BattleSide::DEFENDER, CreatureID(0), firstHex, 40);
	const CStack * second = addStack(BattleSide::DEFENDER, CreatureID(0), secondHex, 40);
	const CStack * third = addStack(BattleSide::DEFENDER, CreatureID(0), thirdHex, 40);
	ASSERT_NE(first, nullptr);
	ASSERT_NE(second, nullptr);
	ASSERT_NE(third, nullptr);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	for(const CStack * target : {static_cast<const CStack *>(isolated), first, second, third})
		ASSERT_TRUE(view->battleCanShoot(ballista, target->getPosition()));
	const auto hasHit = [&](const CStack * primary, const CStack * extra)
	{
		const auto impacted = view->getAttackedBattleUnits(
			ballista, primary, primary->getPosition(), true);
		return std::any_of(impacted.begin(), impacted.end(), [&](const battle::Unit * hit)
		{
			return hit && hit->unitId() == extra->unitId();
		});
	};
	ASSERT_TRUE(hasHit(first, second));
	ASSERT_TRUE(hasHit(first, third));
	ASSERT_TRUE(hasHit(second, first));
	ASSERT_FALSE(hasHit(isolated, first));
	ASSERT_FALSE(hasHit(isolated, second));
	ASSERT_FALSE(hasHit(isolated, third));
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *ballista);
	ASSERT_EQ(baseline.actionType, EActionType::SHOOT);
	ASSERT_FALSE(baseline.target.empty());
	ASSERT_EQ(baseline.target.front().unitValue, isolated->unitId())
		<< "HoMM3's target-time score must prefer the closer isolated stack";

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
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	ASSERT_FALSE(callback->action->target.empty());
	const int targetID = callback->action->target.front().unitValue;
	ASSERT_EQ(targetID, first->unitId())
		<< "The TUM projection must value all three enemies around the aimed stack";
	EXPECT_NE(callback->action->toString(), baseline.toString());
	std::cout << "AI_TUM_BALLISTA_SPLASH homm3=" << baseline.toString()
		<< " tum=" << callback->action->toString() << '\n';

	const auto firstHealthBefore = first->getAvailableHealth();
	const auto secondHealthBefore = second->getAvailableHealth();
	const auto thirdHealthBefore = third->getAvailableHealth();
	battle()->activeStack = ballista->unitId();
	const int shotsBefore = recorder.shots;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Native VCMI rejected the independently selected area shot";
	EXPECT_GT(recorder.shots, shotsBefore);
	EXPECT_LT(first->getAvailableHealth(), firstHealthBefore);
	EXPECT_LT(second->getAvailableHealth(), secondHealthBefore);
	EXPECT_LT(third->getAvailableHealth(), thirdHealthBefore);
	policy.reset();
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.damage, 0);
}

// TUM documents smarter ballista aiming but does not publish its DLL formula.
// Score each legal hex of a wide native target instead of assuming that a shot
// at its head has the same splash footprint as a shot at its tail.
// SHOOTS_ALL_ADJACENT is a fixture bonus, not a natural war-machine ability.
TEST_F(AIIntegrationNativeBattle, tumBallistaChoosesWideTailWithNativeSplash)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 70);
	setArmy(defenderSideHero, CreatureID(102), 10);
	giveArtifact(attackerSideHero, ArtifactID(ArtifactID::BALLISTA), ArtifactPosition::MACH1);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::ARTILLERY),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	battle()->obstacles.clear();
	CStack * ballista = nullptr;
	CStack * wide = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->isBallista())
			ballista = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(102))
			wide = unit.get();
	}
	ASSERT_NE(ballista, nullptr);
	ASSERT_NE(wide, nullptr);
	ASSERT_TRUE(wide->doubleWide());
	ASSERT_TRUE(ballista->getPosition().isValid());
	const BattleHex widePosition(ballista->getPosition().toInt() + 11);
	ASSERT_TRUE(widePosition.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(widePosition), nullptr);
	wide->setPosition(widePosition);
	const auto footprint = wide->getHexes();
	ASSERT_EQ(footprint.size(), 2u);
	const BattleHex head = footprint[0];
	const BattleHex tail = footprint[1];
	ASSERT_TRUE(tail.isAvailable());
	ASSERT_NE(head, tail);
	std::vector<BattleHex> tailOnly;
	for(const auto & hex : tail.getNeighbouringTiles())
	{
		if(hex.isAvailable() && !battle()->battleGetUnitByPos(hex)
			&& BattleHex::getDistance(hex, head) > 1)
			tailOnly.push_back(hex);
	}
	ASSERT_GE(tailOnly.size(), 2u);
	const CStack * firstExtra = addStack(BattleSide::DEFENDER, CreatureID(0), tailOnly[0], 1000);
	const CStack * secondExtra = addStack(BattleSide::DEFENDER, CreatureID(0), tailOnly[1], 1000);
	ASSERT_NE(firstExtra, nullptr);
	ASSERT_NE(secondExtra, nullptr);
	ballista->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::SHOOTS_ALL_ADJACENT,
		BonusSource::OTHER, 1, BonusSourceID()));
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanShoot(ballista, head));
	ASSERT_TRUE(view->battleCanShoot(ballista, tail));
	const auto headHits = view->getAttackedBattleUnits(ballista, wide, head, true);
	const auto tailHits = view->getAttackedBattleUnits(ballista, wide, tail, true);
	const auto includes = [](const auto & hits, const CStack * wanted)
	{
		return std::any_of(hits.begin(), hits.end(), [&](const auto * hit)
		{
			return hit && hit->unitId() == wanted->unitId();
		});
	};
	for(const CStack * extra : {firstExtra, secondExtra})
	{
		ASSERT_FALSE(includes(headHits, extra));
		ASSERT_TRUE(includes(tailHits, extra));
	}
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *ballista);
	ASSERT_EQ(baseline.actionType, EActionType::SHOOT);
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
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	ASSERT_EQ(callback->action->target.size(), 1u);
	EXPECT_EQ(callback->action->target.front().hexValue, tail)
		<< "TUM must aim at the legal tail that covers three enemy stacks";
	EXPECT_NE(callback->action->toString(), baseline.toString());
	std::cout << "AI_TUM_BALLISTA_WIDE_AIM homm3=" << baseline.toString()
		<< " tum=" << callback->action->toString() << '\n';
	const auto firstHealthBefore = firstExtra->getAvailableHealth();
	const auto secondHealthBefore = secondExtra->getAvailableHealth();
	const int shotsBefore = recorder.shots;
	battle()->activeStack = ballista->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Native VCMI rejected the independently chosen ballista tail shot";
	EXPECT_GT(recorder.shots, shotsBefore);
	EXPECT_LT(firstExtra->getAvailableHealth(), firstHealthBefore);
	EXPECT_LT(secondExtra->getAvailableHealth(), secondHealthBefore);
	policy.reset();
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.damage, 0);
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

TEST_F(AIIntegrationNativeBattle, tumBallistaDefendsWhenAllTargetsHaveDifferentDisableSources)
{
	// TUM: never break Blind, Paralyze or Petrification with a ballista.
	// Check a mixed native battle with one spell-source and two creature-source
	// disabilities, so no one invalid source lookup can leave an eligible shot.
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 100);
	setArmy(defenderSideHero, CreatureID(0), 50);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(defenderSideHero->id, SlotID(1)), CreatureID(0).toCreature(), 40));
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(defenderSideHero->id, SlotID(2)), CreatureID(0).toCreature(), 30));
	giveArtifact(attackerSideHero, ArtifactID(ArtifactID::BALLISTA), ArtifactPosition::MACH1);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::ARTILLERY),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	teachBattleSpell(attackerSideHero, SpellID::BLIND);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	const CStack * blind = nullptr;
	CStack * paralyzed = nullptr;
	CStack * petrified = nullptr;
	const CStack * ballista = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID::BALLISTA)
			ballista = unit.get();
		else if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
		{
			if(unit->getCount() == 50)
				blind = unit.get();
			else if(unit->getCount() == 40)
				paralyzed = unit.get();
			else if(unit->getCount() == 30)
				petrified = unit.get();
		}
	}
	ASSERT_NE(ballista, nullptr);
	ASSERT_NE(blind, nullptr);
	ASSERT_NE(paralyzed, nullptr);
	ASSERT_NE(petrified, nullptr);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::BLIND, blind));
	const auto disableFromCreature = [](CStack * victim, SpellID spell)
	{
		victim->addNewBonus(std::make_shared<Bonus>(
			BonusDuration::PERMANENT, BonusType::NOT_ACTIVE, BonusSource::CREATURE_ABILITY,
			1, BonusSourceID(CreatureID(0)), BonusSubtypeID(spell)));
	};
	disableFromCreature(paralyzed, SpellID(SpellID::PARALYZE));
	disableFromCreature(petrified, SpellID(SpellID::STONE_GAZE));
	ASSERT_TRUE(blind->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::BLIND)))));
	ASSERT_TRUE(paralyzed->hasBonus(Selector::typeSubtype(
		BonusType::NOT_ACTIVE, BonusSubtypeID(SpellID(SpellID::PARALYZE)))));
	ASSERT_TRUE(petrified->hasBonus(Selector::typeSubtype(
		BonusType::NOT_ACTIVE, BonusSubtypeID(SpellID(SpellID::STONE_GAZE)))));
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *ballista);
	ASSERT_EQ(baseline.actionType, EActionType::SHOOT)
		<< "HoMM3 must still select a shot for a meaningful TUM source-rule difference";
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
	EXPECT_EQ(callback->action->actionType, EActionType::DEFEND)
		<< "The TUM ballista must not shoot any of the disabled stacks";
	const int ballistaId = ballista->unitId();
	policy.reset();
	// The native server, not the exploratory callback, must accept DEFEND and
	// then apply an actual battle result with all effects remaining in play.
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(lastAcceptedActionTypesByUnit[ballistaId][EActionType::DEFEND], 0)
		<< "There was no engine-accepted ballista DEFEND";
	ASSERT_TRUE(lastFirstAcceptedActionTypeByUnit.contains(ballistaId));
	EXPECT_EQ(lastFirstAcceptedActionTypeByUnit.at(ballistaId), EActionType::DEFEND)
		<< "The first authoritative ballista action must spare all disabled targets";
	// Blind applied by a hero spell can expire in later rounds. A later
	// legal shot after expiry is not a violation of the TUM rule.
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
	EXPECT_EQ(callback->action->actionType, EActionType::WAIT)
		<< "TUM melee must preserve its later action while shooters engage neutral melee";
	EXPECT_FALSE(melee->waited()) << "The first neutral-screening action must be a legal initial WAIT";
	// In addition to the differential decision above, complete the battle
	// through the real server. A fabricated winner is never accepted.
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(lastAcceptedActionTypesByPlayer[player][EActionType::WAIT], 0)
		<< "The real server must accept TUM's neutral-screening WAIT, not just a callback proposal";
	EXPECT_GT(recorder.shots, 0);
}



// A ballista's native SHOOTER and legal-fire flags must not turn it into
// the "friendly archers" that justify TUM's neutral-melee screening rule.
// The machine remains controlled by its separate, documented ballista policy.
// This is a VCMI-specific interpretation, not a recovered hidden ERA rule.
TEST_F(AIIntegrationNativeBattle, tumBallistaAloneDoesNotMakeMeleeWaitForShooters)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 60);
	giveArtifact(attackerSideHero, ArtifactID(ArtifactID::BALLISTA), ArtifactPosition::MACH1);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::ARTILLERY),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	gameHandler->createWanderingMonster({9, 9, 0}, CreatureID(0), 80);
	const auto * neutral = findFirst<CGCreature>();
	ASSERT_NE(neutral, nullptr);
	ASSERT_EQ(neutral->getOwner(), PlayerColor::UNFLAGGABLE);
	gameHandler->battles->startBattle(attackerSideHero, neutral);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);

	const CStack * melee = nullptr;
	const CStack * ballista = nullptr;
	const CStack * opponent = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			melee = unit.get();
		else if(unit->unitSide() == BattleSide::ATTACKER
			&& unit->creatureId() == CreatureID::BALLISTA)
			ballista = unit.get();
		else if(unit->unitSide() == BattleSide::DEFENDER && unit->alive())
			opponent = unit.get();
	}
	ASSERT_NE(melee, nullptr);
	ASSERT_NE(ballista, nullptr);
	ASSERT_NE(opponent, nullptr);
	ASSERT_TRUE(ballista->isShooter())
		<< "Ballista must be a native ranged stack to expose the false screen";
	ASSERT_TRUE(ballista->hasBonusOfType(BonusType::SIEGE_WEAPON))
		<< "The VCMI war-machine classifier must cover this ballista";

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanShoot(ballista, opponent->getPosition()))
		<< "The machine must actually have a legal shot against the neutral";
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *melee);
	ASSERT_EQ(baseline.actionType, EActionType::WALK)
		<< "The independent baseline must approach the distant neutral";

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
	EXPECT_EQ(callback->action->toString(), baseline.toString())
		<< "Only a siege machine can shoot, so TUM must use its explicit partial HoMM3 base";
	battle()->activeStack = melee->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Authoritative VCMI server rejected TUM's infantry movement";
	EXPECT_GT(recorder.movementPackets, 0);
	policy.reset();
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, tumBlindNeutralThreatDoesNotBreakShooterScreen)
{
	// TUM's published neutral-shooter screen waits until enemy melee can
	// actually threaten the formation. A Blind stack retains geometric reach,
	// but cannot take its next movement turn. Use an actual neutral opponent
	// and an actual hero Blind cast, not a fake battle callback or mock action.
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 60);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(attackerSideHero->id, SlotID(1)), CreatureID(2).toCreature(), 25));
	gameHandler->createWanderingMonster({9, 9, 0}, CreatureID(0), 180);
	const auto * neutral = findFirst<CGCreature>();
	ASSERT_NE(neutral, nullptr);
	ASSERT_EQ(neutral->getOwner(), PlayerColor::UNFLAGGABLE);
	teachBattleSpell(attackerSideHero, SpellID::BLIND);
	gameHandler->battles->startBattle(attackerSideHero, neutral);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);

	const CStack * melee = nullptr;
	CStack * opponent = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			melee = unit.get();
		else if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			opponent = unit.get();
	}
	ASSERT_NE(melee, nullptr);
	ASSERT_NE(opponent, nullptr);
	ASSERT_EQ(battle()->sideToPlayer(BattleSide::DEFENDER), PlayerColor::NEUTRAL);
	// A test-only speed effect makes the opponent's unmodified geometric reach
	// threaten friendly units, so the regression proves the incapacitation gate
	// rather than merely repeating the existing distant-neutral WAIT fixture.
	opponent->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 25, BonusSourceID()));
	ASSERT_GT(static_cast<int>(opponent->getMovementRange()), 20);
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	const auto reach = view->getReachability(opponent);
	bool mayReach = false;
	for(const auto & hex : melee->getAttackableHexes(opponent))
	{
		if(hex.isValid() && view->isMeleeAttackPossible(opponent, melee, hex)
			&& reach.distances[hex.toInt()] <= static_cast<int>(opponent->getMovementRange()))
			mayReach = true;
	}
	ASSERT_TRUE(mayReach) << "The unblinded neutral must threaten the next turn";

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
	EXPECT_NE(callback->action->actionType, EActionType::WAIT)
		<< "Reachable active neutral melee must not trigger the protected WAIT";
	callback->action.reset();

	ASSERT_TRUE(castOn(attackerSideHero, SpellID::BLIND, opponent));
	ASSERT_TRUE(opponent->hasBonusOfType(BonusType::NOT_ACTIVE));
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *melee);
	EXPECT_NE(baseline.actionType, EActionType::WAIT)
		<< "The HoMM3 movement policy must remain separate from TUM's hold";
	policy->activeStack(battleID, melee);
	ASSERT_TRUE(callback->action.has_value());
	EXPECT_EQ(callback->action->actionType, EActionType::WAIT)
		<< "An immobile neutral must not force a melee charge despite its speed";
	const int meleeId = melee->unitId();
	policy.reset();
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(lastAcceptedActionTypesByUnit[meleeId][EActionType::WAIT], 0)
		<< "The actual VCMI server must accept this melee stack's protective WAIT";
	EXPECT_GT(recorder.damage, 0);
}

// TUM's published neutral screen may WAIT on its first safe activation and
// DEFEND only on the delayed second activation. Test both real server commands:
// a one-shot proposal does not establish that the WAIT/DEFEND sequence is legal.
// Permanent creature-source Paralyze is fixture setup, not an ability proc.
TEST_F(AIIntegrationNativeBattle, tumNeutralScreenWaitThenDefendAreBothAcceptedByServer)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 60);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(attackerSideHero->id, SlotID(1)), CreatureID(2).toCreature(), 25));
	gameHandler->createWanderingMonster({9, 9, 0}, CreatureID(0), 180);
	const auto * neutral = findFirst<CGCreature>();
	ASSERT_NE(neutral, nullptr);
	ASSERT_EQ(neutral->getOwner(), PlayerColor::UNFLAGGABLE);
	gameHandler->battles->startBattle(attackerSideHero, neutral);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);

	const CStack * melee = nullptr;
	CStack * opponent = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			melee = unit.get();
		else if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			opponent = unit.get();
	}
	ASSERT_NE(melee, nullptr);
	ASSERT_NE(opponent, nullptr);
	// Ensure there is a genuine geometric next-turn threat absent the disable.
	opponent->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 25, BonusSourceID()));
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	const auto reach = view->getReachability(opponent);
	bool canThreaten = false;
	for(const auto & hex : melee->getAttackableHexes(opponent))
	{
		if(hex.isValid() && view->isMeleeAttackPossible(opponent, melee, hex)
			&& reach.distances[hex.toInt()] <= static_cast<int>(opponent->getMovementRange()))
		{
			canThreaten = true;
		}
	}
	ASSERT_TRUE(canThreaten) << "The neutral would reach our line without Paralyze";
	opponent->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::NOT_ACTIVE, BonusSource::CREATURE_ABILITY,
		1, BonusSourceID(CreatureID(0)), BonusSubtypeID(SpellID(SpellID::PARALYZE))));
	ASSERT_TRUE(opponent->hasBonusOfType(BonusType::NOT_ACTIVE));

	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *melee);
	ASSERT_NE(baseline.actionType, EActionType::WAIT);
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
	EXPECT_EQ(callback->action->actionType, EActionType::WAIT);
	const int meleeId = melee->unitId();
	policy.reset();

	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	ASSERT_TRUE(lastFirstAcceptedActionTypeByUnit.contains(meleeId));
	EXPECT_EQ(lastFirstAcceptedActionTypeByUnit.at(meleeId), EActionType::WAIT)
		<< "The real server must accept WAIT before the delayed action";
	EXPECT_GT(lastAcceptedActionTypesByUnit[meleeId][EActionType::DEFEND], 0)
		<< "The same stack must legally DEFEND after its first WAIT while the screen remains safe";
	EXPECT_GT(recorder.shots, 0);
	EXPECT_GT(recorder.damage, 0);
}

// A non-melee native unit may have apparent geometric reach yet cannot
// execute the melee attack that TUM's published neutral-screen trigger fears.
// In this modded-creature fixture SIEGE_WEAPON removes melee capability from a
// neutral Peasant. This is explicit test setup, not an upstream creature rule.
TEST_F(AIIntegrationNativeBattle, tumNonMeleeNeutralDoesNotBreakShooterScreen)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 60);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(attackerSideHero->id, SlotID(1)), CreatureID(2).toCreature(), 25));
	gameHandler->createWanderingMonster({9, 9, 0}, CreatureID(0), 80);
	const auto * neutral = findFirst<CGCreature>();
	ASSERT_NE(neutral, nullptr);
	ASSERT_EQ(neutral->getOwner(), PlayerColor::UNFLAGGABLE);
	// Keep a second ordinary melee neutral so the authoritative battle has
	// a damageable, attacking enemy after the first becomes non-melee.
	// Otherwise VCMI can end the encounter without a single injury.
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(neutral->id, SlotID(1)), CreatureID(0).toCreature(), 35));
	gameHandler->battles->startBattle(attackerSideHero, neutral);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);

	const CStack * melee = nullptr;
	CStack * opponent = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			melee = unit.get();
		else if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0)
			&& unit->getCount() == 80)
			opponent = unit.get();
	}
	ASSERT_NE(melee, nullptr);
	ASSERT_NE(opponent, nullptr);
	ASSERT_EQ(battle()->sideToPlayer(BattleSide::DEFENDER), PlayerColor::NEUTRAL);
	opponent->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 25, BonusSourceID()));

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	const auto reach = view->getReachability(opponent);
	bool geometricallyReachable = false;
	for(const auto & hex : melee->getAttackableHexes(opponent))
	{
		if(hex.isValid() && view->isMeleeAttackPossible(opponent, melee, hex)
			&& reach.distances[hex.toInt()] <= static_cast<int>(opponent->getMovementRange()))
			geometricallyReachable = true;
	}
	ASSERT_TRUE(geometricallyReachable)
		<< "The enemy must be able to reach the line if it had melee capability";
	ASSERT_TRUE(view->battleCanAttackUnit(opponent, melee));

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
	EXPECT_NE(callback->action->actionType, EActionType::WAIT)
		<< "A real fast neutral melee threat must first break the screen";
	callback->action.reset();

	// CUnitState/Unit::isMeleeAttacker makes SIEGE_WEAPON non-melee;
	// the callback's battleCanAttackUnit uses the same authoritative rule.
	opponent->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::SIEGE_WEAPON, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_FALSE(opponent->isMeleeAttacker());
	ASSERT_FALSE(opponent->isShooter());
	ASSERT_FALSE(view->battleCanAttackUnit(opponent, melee));
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *melee);
	EXPECT_NE(baseline.actionType, EActionType::WAIT)
		<< "HoMM3 movement remains independent of TUM's neutral-screen condition";
	policy->activeStack(battleID, melee);
	ASSERT_TRUE(callback->action.has_value());
	EXPECT_EQ(callback->action->actionType, EActionType::WAIT)
		<< "Geometric reach without a legal melee attack cannot break TUM's screen";
	const int meleeId = melee->unitId();
	policy.reset();

	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(lastAcceptedActionTypesByUnit[meleeId][EActionType::WAIT], 0)
		<< "The real VCMI server must accept the independently selected WAIT";
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, tumIncapacitatedOnlyShooterCannotHoldMeleeScreen)
{
	// AI Improvements.txt explains that melee holds so friendly shooters can
	// fire against neutral troops. Native canShoot() checks ammunition and
	// Forgetfulness, NOT whether NOT_ACTIVE disables the shooter's turn.
	// A disabled-only firing line must not freeze our melee progress.
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 60);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(attackerSideHero->id, SlotID(1)), CreatureID(2).toCreature(), 25));
	gameHandler->createWanderingMonster({9, 9, 0}, CreatureID(0), 40);
	const auto * neutral = findFirst<CGCreature>();
	ASSERT_NE(neutral, nullptr);
	ASSERT_EQ(neutral->getOwner(), PlayerColor::UNFLAGGABLE);
	gameHandler->battles->startBattle(attackerSideHero, neutral);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);

	const CStack * melee = nullptr;
	CStack * disabledArcher = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() != BattleSide::ATTACKER)
			continue;
		if(unit->creatureId() == CreatureID(0))
			melee = unit.get();
		if(unit->creatureId() == CreatureID(2))
			disabledArcher = unit.get();
	}
	ASSERT_NE(melee, nullptr);
	ASSERT_NE(disabledArcher, nullptr);
	ASSERT_TRUE(disabledArcher->isShooter());
	ASSERT_TRUE(disabledArcher->canShoot());
	// Fixture-only native creature-source paralysis. No ability proc is
	// claimed; this is an actual status in an otherwise genuine VCMI battle.
	disabledArcher->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::NOT_ACTIVE, BonusSource::CREATURE_ABILITY,
		1, BonusSourceID(CreatureID(0)), BonusSubtypeID(SpellID(SpellID::PARALYZE))));
	ASSERT_TRUE(disabledArcher->hasBonusOfType(BonusType::NOT_ACTIVE));
	ASSERT_TRUE(disabledArcher->canShoot())
		<< "Native canShoot() must still count arrows despite inability to act";

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_EQ(battle()->sideToPlayer(BattleSide::DEFENDER), PlayerColor::NEUTRAL);
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *melee);
	ASSERT_NE(baseline.actionType, EActionType::WAIT);
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
	EXPECT_EQ(callback->action->toString(), baseline.toString())
		<< "Do not wait for a shooter the engine cannot activate";
	EXPECT_NE(callback->action->actionType, EActionType::WAIT);

	const int meleeID = melee->unitId();
	policy.reset();
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	ASSERT_TRUE(lastFirstAcceptedActionTypeByUnit.contains(meleeID))
		<< "The actual melee unit never took a server-accepted turn";
	EXPECT_EQ(lastFirstAcceptedActionTypeByUnit.at(meleeID), baseline.actionType)
		<< "The server must accept a real non-WAIT alternative to idle screening";
	EXPECT_GT(recorder.damage, 0);
}

// A paralysed neutral next to the allied archer still blocks native shots.
// TUM's neutral melee screen must not WAIT for a completely blocked shooter.
TEST_F(AIIntegrationNativeBattle, tumBlockedShooterCannotHoldMeleeScreen)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 60);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(attackerSideHero->id, SlotID(1)), CreatureID(2).toCreature(), 25));
	gameHandler->createWanderingMonster({9, 9, 0}, CreatureID(0), 40);
	const auto * neutral = findFirst<CGCreature>();
	ASSERT_NE(neutral, nullptr);
	gameHandler->battles->startBattle(attackerSideHero, neutral);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	const CStack * melee = nullptr;
	const CStack * shooter = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() != BattleSide::ATTACKER)
			continue;
		if(unit->creatureId() == CreatureID(0))
			melee = unit.get();
		if(unit->creatureId() == CreatureID(2))
			shooter = unit.get();
	}
	ASSERT_NE(melee, nullptr);
	ASSERT_NE(shooter, nullptr);
	ASSERT_TRUE(shooter->canShoot());
	ASSERT_TRUE(battle()->battleCanShoot(shooter));
	const BattleHex adjacent(shooter->getPosition().toInt() + 1);
	ASSERT_TRUE(adjacent.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(adjacent), nullptr);
	auto * blocker = addStack(BattleSide::DEFENDER, CreatureID(0), adjacent, 5);
	ASSERT_NE(blocker, nullptr);
	blocker->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::NOT_ACTIVE, BonusSource::CREATURE_ABILITY,
		1, BonusSourceID(CreatureID(0)), BonusSubtypeID(SpellID(SpellID::PARALYZE))));
	ASSERT_TRUE(blocker->hasBonusOfType(BonusType::NOT_ACTIVE));
	ASSERT_TRUE(shooter->canShoot());
	ASSERT_FALSE(battle()->battleCanShoot(shooter))
		<< "The native adjacent-enemy rule must block shooting";

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	for(const auto * target : view->battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY))
	{
		if(target->alive())
		{
			ASSERT_FALSE(view->battleCanShoot(shooter, target->getPosition()));
		}
	}
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *melee);
	ASSERT_NE(baseline.actionType, EActionType::WAIT);
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
	EXPECT_EQ(callback->action->toString(), baseline.toString())
		<< "Do not hold melee when no ranged ally has a legal shot";
	ASSERT_NE(callback->action->actionType, EActionType::WAIT);
	battle()->activeStack = melee->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Server rejected the source-controlled melee action";
	policy.reset();
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, tumOutOfAmmoArcherNeverBecomesMeleeShooterGuard)
{
	// TUM's published neutral screen is a MELEE tactic. An exhausted archer
	// remains a ranged creature even when battleCanShoot() becomes false.
	// A second archer must still have ammunition so the guard condition
	// would otherwise be true and the regression would catch an accidental WAIT.
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 60);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(attackerSideHero->id, SlotID(1)), CreatureID(2).toCreature(), 15));
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(attackerSideHero->id, SlotID(2)), CreatureID(2).toCreature(), 9));
	gameHandler->createWanderingMonster({9, 9, 0}, CreatureID(0), 100);
	const auto * neutral = findFirst<CGCreature>();
	ASSERT_NE(neutral, nullptr);
	gameHandler->battles->startBattle(attackerSideHero, neutral);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	CStack * exhausted = nullptr;
	const CStack * armedShooter = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() != BattleSide::ATTACKER || unit->creatureId() != CreatureID(2))
			continue;
		if(unit->getCount() == 9)
			exhausted = unit.get();
		else if(unit->getCount() == 15)
			armedShooter = unit.get();
	}
	ASSERT_NE(exhausted, nullptr);
	ASSERT_NE(armedShooter, nullptr);
	ASSERT_TRUE(exhausted->isShooter());
	ASSERT_TRUE(armedShooter->canShoot());
	ASSERT_GT(exhausted->shots.total(), 0);
	exhausted->shots.use(exhausted->shots.total());
	ASSERT_EQ(exhausted->shots.available(), 0);
	ASSERT_FALSE(exhausted->canShoot());
	ASSERT_FALSE(battle()->battleCanShoot(exhausted));
	ASSERT_TRUE(exhausted->isShooter())
		<< "An exhausted archer must retain its creature's ranged classification";

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto playerView = callback->getBattle(battleID);
	ASSERT_NE(playerView, nullptr);
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*playerView, *exhausted);
	ASSERT_NE(baseline.actionType, EActionType::WAIT);
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
	policy->activeStack(battleID, exhausted);
	ASSERT_TRUE(callback->action.has_value());
	EXPECT_EQ(callback->action->toString(), baseline.toString())
		<< "A shooter out of ammunition must retain the explicitly partial HoMM3 policy";
	EXPECT_NE(callback->action->actionType, EActionType::WAIT);

	const int exhaustedId = exhausted->unitId();
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	ASSERT_FALSE(lastAcceptedActionTypesByUnit[exhaustedId].empty())
		<< "The exhausted archer never acted in an actual VCMI battle";
	EXPECT_EQ(lastAcceptedActionTypesByUnit[exhaustedId][EActionType::WAIT], 0)
		<< "The real server must never accept a melee-guard WAIT from this ranged unit";
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
		EXPECT_NE(std::string(error.what()).find("attacker"), std::string::npos)
			<< "Strict MMAI failure must identify the missing attacker role";
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


// Pair the same ranged-versus-infantry armies against two genuinely
// different controllers. The MMAI role uses the installed official ONNX
// weights; FHeroes2Tactics uses its independent native archer policy.
// Require both policies to act, and let the server determine the winner.
// These are action-distribution smoke comparisons, not win-rate results.
TEST_F(AIIntegrationNativeBattle, mmaiRangedAttackerAgainstFHeroes2InfantryRealBattle)
{
	if(!std::getenv("VCMI_AI_MMAI_NEURAL_TEST"))
		GTEST_SKIP() << "Requires installed, checksum-verified MMAI ONNX models";
	requireInstalledNeuralRole("attacker");
	ASSERT_FALSE(HasFatalFailure());
	prepareBattle(CreatureID(2), 20, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "MMAI", "FHeroes2Tactics", true);
	EXPECT_GT(recorder.shots, 0) << "Ranged-versus-infantry duel produced no engine-accepted shots";
	EXPECT_GT(mmaiUnitDecisions, 0);
	EXPECT_GT(mmaiNonpassiveDecisions, 0);
}

TEST_F(AIIntegrationNativeBattle, mmaiDefenderAgainstFHeroes2RangedAttackerRealBattle)
{
	if(!std::getenv("VCMI_AI_MMAI_NEURAL_TEST"))
		GTEST_SKIP() << "Requires installed, checksum-verified MMAI ONNX models";
	requireInstalledNeuralRole("defender");
	ASSERT_FALSE(HasFatalFailure());
	prepareBattle(CreatureID(2), 20, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "MMAI", true);
	EXPECT_GT(recorder.shots, 0) << "Ranged-versus-infantry duel produced no engine-accepted shots";
	EXPECT_GT(mmaiUnitDecisions, 0);
	EXPECT_GT(mmaiNonpassiveDecisions, 0);
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

// Exercise the previously untested MMAI siege-attacker route against an
// independent native defender. The official router requests attacker.siege,
// falling back to its installed attacker model if no dedicated siege model
// is configured. The server can resolve catapult strikes without delivering
// an MMAI active-stack event: genuine catapult packets must NOT be confused
// with an ONNX decision or an MMAI automatic-unit decision.
// Both player turns and the battle result come from VCMI's actual engine.
TEST_F(AIIntegrationNativeBattle, mmaiSiegeAttackerAgainstTUMDefenderRealBattle)
{
	if(!std::getenv("VCMI_AI_MMAI_NEURAL_TEST"))
		GTEST_SKIP() << "Requires installed, checksum-verified MMAI ONNX models";
	requireInstalledNeuralRole("attacker");
	ASSERT_FALSE(HasFatalFailure());
	prepareSiege();
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "MMAI", "TUMEnchantedTactics", true);
	EXPECT_GT(mmaiUnitDecisions, 0) << "No ordinary stack was controlled by the neural policy";
	EXPECT_GT(mmaiNonpassiveDecisions, 0);
	// VCMI can emit siege catapult packets on its own. The fixture verifies
	// actual siege processing, not an unobserved MMAI war-machine command.
	EXPECT_FALSE(recorder.catapultAttacks.empty())
		<< "The authoritative engine saw no siege-machine attack";
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


// Validate real spell packets across *different* source-policy event adapters.
// Prior spell regressions used FHeroes2Tactics on both sides; these fixtures
// verify FHeroes2's source-prioritized hero casts are accepted and delivered
// when its opponent uses another independent native policy.
TEST_F(AIIntegrationNativeBattle, mixedFHeroes2AttackerCastsRealSpellAgainstHoMM3Defender)
{
	prepareBattle(CreatureID(0), 30, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::MAGIC_ARROW);
	runToCompletion(std::nullopt, true, "FHeroes2Tactics", "HoMM3OriginalTactics", true);
	EXPECT_GT(heroSpellDecisions, 0);
	EXPECT_FALSE(recorder.castsOf(SpellID::MAGIC_ARROW).empty());
	EXPECT_GT(recorder.spellPackets, 0);
	EXPECT_GT(recorder.injuryPackets, 0);
}

TEST_F(AIIntegrationNativeBattle, mixedFHeroes2DefenderCastsRealSpellAgainstTUMAttacker)
{
	prepareBattle(CreatureID(0), 30, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(defenderSideHero, SpellID::MAGIC_ARROW);
	runToCompletion(std::nullopt, true, "TUMEnchantedTactics", "FHeroes2Tactics", true);
	EXPECT_GT(heroSpellDecisions, 0);
	EXPECT_FALSE(recorder.castsOf(SpellID::MAGIC_ARROW).empty());
	EXPECT_GT(recorder.spellPackets, 0);
	EXPECT_GT(recorder.injuryPackets, 0);
}

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

// Compare all six ordered source-policy pairings with the SAME real
// fast-archer/slow-melee threat fixture for three deterministic RNG seeds.
// The original differential regression already pins HoMM3's legal shot
// versus fheroes2's safe retreat in this exact opening state. Replaying
// the full fight captures how that initial difference develops under both
// side assignments. Three seeds are far too few for a win-rate claim;
// do not pool results with unrelated smoke fixtures.
// Only test seeding and observability change, never production game rules.
#define AI_SEEDED_SOURCE_MATCHUP(testName, attackerPolicy, defenderPolicy, battleRngSeed) \
TEST_F(AIIntegrationNativeBattle, testName) \
{ \
	encounterSeed = battleRngSeed; \
	prepareBattle(CreatureID(19), 25, CreatureID(16), 25); \
	ASSERT_FALSE(HasFatalFailure()); \
	battle()->obstacles.clear(); \
	const CStack * shooter = nullptr; \
	for(const auto & unit : battle()->stacks) \
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(19)) \
			shooter = unit.get(); \
	ASSERT_NE(shooter, nullptr); \
	const BattleHex approachingHex(shooter->getPosition().toInt() + 4); \
	ASSERT_TRUE(approachingHex.isAvailable()); \
	ASSERT_EQ(battle()->battleGetUnitByPos(approachingHex), nullptr); \
	ASSERT_NE(addStack(BattleSide::DEFENDER, CreatureID(16), approachingHex, 25), nullptr); \
	ASSERT_EQ(battle()->battleActiveUnit(), shooter); \
	runToCompletion(std::nullopt, false, attackerPolicy, defenderPolicy, true); \
	EXPECT_GT(recorder.damage, 0) << "Expected engine-applied combat damage"; \
}

AI_SEEDED_SOURCE_MATCHUP(seed1337HoMM3AgainstFHeroes2, "HoMM3OriginalTactics", "FHeroes2Tactics", 1337)
AI_SEEDED_SOURCE_MATCHUP(seed1337FHeroes2AgainstHoMM3, "FHeroes2Tactics", "HoMM3OriginalTactics", 1337)
AI_SEEDED_SOURCE_MATCHUP(seed1337HoMM3AgainstTUM, "HoMM3OriginalTactics", "TUMEnchantedTactics", 1337)
AI_SEEDED_SOURCE_MATCHUP(seed1337TUMAgainstHoMM3, "TUMEnchantedTactics", "HoMM3OriginalTactics", 1337)
AI_SEEDED_SOURCE_MATCHUP(seed1337FHeroes2AgainstTUM, "FHeroes2Tactics", "TUMEnchantedTactics", 1337)
AI_SEEDED_SOURCE_MATCHUP(seed1337TUMAgainstFHeroes2, "TUMEnchantedTactics", "FHeroes2Tactics", 1337)
AI_SEEDED_SOURCE_MATCHUP(seed2027HoMM3AgainstFHeroes2, "HoMM3OriginalTactics", "FHeroes2Tactics", 2027)
AI_SEEDED_SOURCE_MATCHUP(seed2027FHeroes2AgainstHoMM3, "FHeroes2Tactics", "HoMM3OriginalTactics", 2027)
AI_SEEDED_SOURCE_MATCHUP(seed2027HoMM3AgainstTUM, "HoMM3OriginalTactics", "TUMEnchantedTactics", 2027)
AI_SEEDED_SOURCE_MATCHUP(seed2027TUMAgainstHoMM3, "TUMEnchantedTactics", "HoMM3OriginalTactics", 2027)
AI_SEEDED_SOURCE_MATCHUP(seed2027FHeroes2AgainstTUM, "FHeroes2Tactics", "TUMEnchantedTactics", 2027)
AI_SEEDED_SOURCE_MATCHUP(seed2027TUMAgainstFHeroes2, "TUMEnchantedTactics", "FHeroes2Tactics", 2027)
AI_SEEDED_SOURCE_MATCHUP(seed2029HoMM3AgainstFHeroes2, "HoMM3OriginalTactics", "FHeroes2Tactics", 2029)
AI_SEEDED_SOURCE_MATCHUP(seed2029FHeroes2AgainstHoMM3, "FHeroes2Tactics", "HoMM3OriginalTactics", 2029)
AI_SEEDED_SOURCE_MATCHUP(seed2029HoMM3AgainstTUM, "HoMM3OriginalTactics", "TUMEnchantedTactics", 2029)
AI_SEEDED_SOURCE_MATCHUP(seed2029TUMAgainstHoMM3, "TUMEnchantedTactics", "HoMM3OriginalTactics", 2029)
AI_SEEDED_SOURCE_MATCHUP(seed2029FHeroes2AgainstTUM, "FHeroes2Tactics", "TUMEnchantedTactics", 2029)
AI_SEEDED_SOURCE_MATCHUP(seed2029TUMAgainstFHeroes2, "TUMEnchantedTactics", "FHeroes2Tactics", 2029)

#undef AI_SEEDED_SOURCE_MATCHUP

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
