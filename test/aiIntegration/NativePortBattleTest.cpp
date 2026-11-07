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
#include "../../lib/entities/artifact/ArtifactUtils.h"
#include "../../lib/entities/artifact/CArtifact.h"
#include "../../lib/entities/artifact/CArtifactInstance.h"
#include "../../lib/entities/building/TownFortifications.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/CPlayerState.h"
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
	int surrenderRetreatDecisionRequests = 0;

	// Headless native fixture has no IClient. Returning no surrender/retreat
	// decision preserves the engine's legal combat path instead of calling
	// CBattleCallback's UI-dependent default through a null client pointer.
	std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID &,
		const BattleStateInfoForRetreat &) override
	{
		++surrenderRetreatDecisionRequests;
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
	int mmaiUntargetedFirstAidActions = 0;
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
		int defenderCount = 30, CreatureID attackerType = CreatureID(10), int attackerCount = 30)
	{
		startRecordedGame();
		ASSERT_FALSE(HasFatalFailure());
		setArmy(attackerSideHero, attackerType, attackerCount);
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
		bool requireBothSidesAct = false,
		std::function<void(const BattleAction &, BattleSide, BattleHex)> onAcceptedAction = {})
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
		// A directed native regression may subscribe to actual server unit
		// changes before starting the runner. Preserve that observer while
		// delivering the same packet to every experimental AI adapter.
		// Previously this assignment silently replaced the summon observer,
		// yielding a false-negative even though unit-change packets arrived.
		const auto priorUnitChangeObserver = recorder.onUnitsChangedAfterApply;
		recorder.onUnitsChangedAfterApply = [&, priorUnitChangeObserver](const BattleUnitsChanged & units)
		{
			for(auto & entry : policies)
				entry.second->battleUnitsChanged(units.battleID, units.changedStacks);
			++deliveredUnitChangeEvents;
			if(priorUnitChangeObserver)
				priorUnitChangeObserver(units);
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
		// A directed native spell regression may already observe teleport
		// movement packets. Preserve the observer while still forwarding
		// the packet to both real battle policy adapters.
		const auto priorMovementObserver = recorder.onMovementBeforeApply;
		recorder.onMovementBeforeApply = [&, priorMovementObserver](const BattleStackMoved & movement)
		{
			const auto * movedStack = battle()->battleGetStackByID(movement.stack);
			ASSERT_NE(movedStack, nullptr) << "Movement callback must see the pre-move live stack";
			for(auto & entry : policies)
				entry.second->battleStackMoved(movement.battleID, movedStack,
					movement.tilesToMove, movement.distance, movement.teleporting);
			++deliveredMovementEvents;
			if(priorMovementObserver)
				priorMovementObserver(movement);
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
			if(activePolicy == "MMAI")
			{
				EXPECT_NE(action.actionType, EActionType::RETREAT)
					<< "Strict ONNX inference must not translate a training reset into live hero retreat";
			}
			if(activePolicy == "MMAI" && !callback->spellAction)
			{
				// VCMI MMAI's published v13/v15 backends handle first aid,
				// catapults and arrow towers through deterministic auto-actions.
				// They are genuine MMAI actions, but NOT ONNX-selected creature
				// decisions. Keep them out of neural inference evidence.
				if(active->isFirstAidTent() || active->isCatapult() || active->isTurret())
				{
					++mmaiAutomaticDecisions;
					if(active->isFirstAidTent())
					{
						EXPECT_EQ(action.actionType, EActionType::STACK_HEAL)
							<< "First aid is engine-controlled, not a V15 ONNX actor";
						if(action.actionType == EActionType::STACK_HEAL && action.target.empty())
							++mmaiUntargetedFirstAidActions;
					}
				}
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
			// Capture pre-apply geometry: the authoritative server can move or
			// remove the active stack while accepting the selected action.
			const auto actingSide = active->unitSide();
			const BattleHex origin = active->getPosition();
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action))
				<< "The engine rejected the selected action; last native pre-apply state:\n"
				<< trace.str();
			if(onAcceptedAction)
				onAcceptedAction(action, actingSide, origin);
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
#ifdef ENABLE_MMAI
		// The V13/V15 strict policies cannot outsource retreat/surrender to
		// the adventure-layer decision callback. This assertion runs on every
		// real, server-accepted ONNX combat scenario (including both siege sides).
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			const auto & activeBackend = side == BattleSide::ATTACKER ? attackerAI : defenderAI;
			if(activeBackend == "MMAI")
			{
				const auto player = side == BattleSide::ATTACKER ? attackerPlayer : defenderPlayer;
				EXPECT_EQ(callbacks.at(player)->surrenderRetreatDecisionRequests, 0)
					<< "Strict MMAI consulted scripted surrender/retreat instead of its ONNX policy";
			}
		}
#endif
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

// fheroes2 BattlePlanner::isLimitOfTurnsExceeded: an attacking AI retreats
// after 50 consecutive *actual* VCMI rounds without casualties. The fixture
// deliberately makes both armies DEFEND for the preceding rounds, so it can
// exercise this rare stalemate without changing combat stats or fabricating
// round events. The policy's final RETREAT must be accepted by the server.
TEST_F(AIIntegrationNativeBattle, fheroes2RetreatsAfterFiftyRealDeathlessRounds)
{
	prepareBattle(CreatureID(19), 25, CreatureID(16), 25);
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_EQ(battle()->tacticDistance, 0);
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanFlee()) << "The source policy may only retreat when VCMI permits it";
	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	// Direct AI registration uses the two-argument overload. The three-
	// argument variant is reserved for human-invoked autocombat in VCMI.
	policy->initBattleInterface(environment, callback);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	const auto initialRound = battle()->battleGetRound();
	ASSERT_GE(initialRound, 0);

	for(int elapsed = 0; elapsed <= 50; ++elapsed)
	{
		ASSERT_TRUE(recorder.battleResults.empty()) << "Battle ended without the source AI retreat";
		ASSERT_EQ(battle()->battleGetRound(), initialRound + elapsed)
			<< "Count engine rounds, not callbacks or unit activations";
		const auto * active = dynamic_cast<const CStack *>(battle()->battleActiveUnit());
		ASSERT_NE(active, nullptr);
		ASSERT_EQ(active->unitSide(), BattleSide::ATTACKER)
			<< "This scenario requires a faster attacking Grand Elf";
		for(const auto & unit : battle()->stacks)
			EXPECT_EQ(unit->getKilled(), 0) << "This is a deathless-round regression";
		callback->action.reset();
		policy->activeStack(battleID, active);
		ASSERT_TRUE(callback->action.has_value());
		if(elapsed < 50)
		{
			EXPECT_NE(callback->action->actionType, EActionType::RETREAT)
				<< "The native port must not retreat before the source's 50-round limit";
			// Apply genuine defending actions for all stacks and advance via
			// the authoritative server instead of forging round packets.
			endRound();
			ASSERT_FALSE(HasFatalFailure());
			continue;
		}

		EXPECT_EQ(callback->action->actionType, EActionType::RETREAT)
			<< "The source cap must produce a native retreat instead of another DEFEND";
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
			<< "VCMI rejected the source-derived retreat";
		ASSERT_EQ(recorder.battleResults.size(), 1u);
		EXPECT_EQ(recorder.battleResults.back().winner, BattleSide::DEFENDER);
		if(recorder.battleResults.back().questionID != QuestionID::NONE)
		{
			ASSERT_TRUE(gameHandler->answerQuestion(recorder.battleResults.back().questionID, 0, player));
		}
		EXPECT_TRUE(gameState()->currentBattles.empty()) << "Native retreat result not applied to adventure state";
		EXPECT_EQ(recorder.acceptedResults, 1);
	}
	callback->onBattleEnded(battleID);
}

// A human player's auto-combat installs its battle AI using the three-
// argument overload (CPlayerInterface::prepareAutoFightingAI). Its inherited
// CBattleGameInterface::human flag defaults to false, so guarding only that
// flag would incorrectly force a human hero to retreat after 50 quiet rounds.
// Verify the human-origin path using native, not fabricated, round transitions.
TEST_F(AIIntegrationNativeBattle, fheroes2AutocombatDoesNotForceHumanRetreat)
{
	prepareBattle(CreatureID(19), 25, CreatureID(16), 25);
	ASSERT_FALSE(HasFatalFailure());
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

	for(int elapsed = 0; elapsed <= 50; ++elapsed)
	{
		ASSERT_TRUE(recorder.battleResults.empty());
		const auto * active = dynamic_cast<const CStack *>(battle()->battleActiveUnit());
		ASSERT_NE(active, nullptr);
		ASSERT_EQ(active->unitSide(), BattleSide::ATTACKER);
		callback->action.reset();
		policy->activeStack(battleID, active);
		ASSERT_TRUE(callback->action.has_value());
		EXPECT_NE(callback->action->actionType, EActionType::RETREAT)
			<< "Human autocombat must not silently retreat on deathless round " << elapsed;
		if(elapsed < 50)
		{
			endRound();
			ASSERT_FALSE(HasFatalFailure());
		}
	}
	// The policy's non-retreat action must be acceptable to the VCMI server.
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action));
	// This real shot may immediately eliminate the opposing army; otherwise
	// continue the same battle to its terminal result. Never require a living
	// defender after a legitimate server-accepted non-retreat command.
	if(recorder.battleResults.empty())
		runToCompletion();
	else
	{
		const auto & result = recorder.battleResults.front();
		EXPECT_EQ(result.winner, BattleSide::ATTACKER);
		if(result.questionID != QuestionID::NONE)
		{
			ASSERT_TRUE(gameHandler->answerQuestion(result.questionID, 0, player));
		}
		EXPECT_TRUE(gameState()->currentBattles.empty());
		EXPECT_EQ(recorder.acceptedResults, 1);
	}
}

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
	// Both sides have durable cavalry. A one-hit kill of a token garrison
	// could end the siege before the catapult's first *observed* activation;
	// setup-time catapult packets are not adapter-delivery evidence.
	prepareSiege(false, CreatureID(10), 40, CreatureID(10), 30);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion();
	EXPECT_FALSE(recorder.catapultAttacks.empty());
	EXPECT_GT(replayedCatapultEvents, 0)
		<< "A post-subscription siege-machine packet must reach both adapters";
}


// The pinned Heroes II policy moves towards castle-wall approach cells when
// no garrison unit is reachable. Only the VCMI server establishes legality
// and the battle outcome: this wall heuristic is an explicit rule projection.
TEST_F(AIIntegrationNativeBattle, fheroes2SiegeAttackerApproachesClosedWallUsingNativeWalk)
{
	prepareSiege(false, CreatureID(0), 100, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_NE(battle()->battleGetDefendedTown(), nullptr);
	ASSERT_NE(battle()->battleGetWallState(EWallPart::GATE), EWallState::NONE);
	const CStack * infantry = nullptr;
	const CStack * defender = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			infantry = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			defender = unit.get();
	}
	ASSERT_NE(infantry, nullptr);
	ASSERT_NE(defender, nullptr);
	const BattleHex defenderHex = defender->getPosition();
	bool acceptedApproach = false;
	// This observer is invoked only AFTER the normal battle runner has
	// submitted a genuinely active unit's decision to the real server.
	// The former test fabricated an infantry activation while the catapult
	// could still be the active stack, so rejection proved no policy defect.
	const auto acceptedAction = [&](const BattleAction & action, BattleSide side, BattleHex origin)
	{
		if(side != BattleSide::ATTACKER || action.actionType != EActionType::WALK
			|| action.target.empty() || !origin.isValid())
			return;
		const BattleHex selected = action.target.front().hexValue;
		if(selected.isValid() && BattleHex::getDistance(selected, defenderHex)
			< BattleHex::getDistance(origin, defenderHex))
			acceptedApproach = true;
	};
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics", false, acceptedAction);
	EXPECT_TRUE(acceptedApproach)
		<< "FHeroes2 must advance toward the walled garrison through a server-accepted WALK";
	EXPECT_FALSE(recorder.catapultAttacks.empty());
	EXPECT_GT(recorder.damage, 0);
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
	EXPECT_EQ(callback->action->actionType, EActionType::NO_ACTION)
		<< "Source defense without a legal command must SKIP, not gain a Heroes III defense bonus";
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

// Heroes II's archerDecision is not a policy for Heroes III ballistae.
// A VCMI ballista with fixture-local FREE_SHOOTING may legally fire despite
// adjacent contact. The source creature archer hand-fighting branch must not
// turn that legal native machine shot into a generic DEFEND. Activate the
// ballista explicitly for this directed action, then finish the real battle.
TEST_F(AIIntegrationNativeBattle, fheroes2EngagedBallistaRetainsLegalNativeShot)
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
	battle()->obstacles.clear();
	const CStack * ballista = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->isBallista())
			ballista = unit.get();
	ASSERT_NE(ballista, nullptr);
	BattleHex adjacent;
	for(const auto & hex : ballista->getSurroundingHexes())
	{
		if(hex.isAvailable() && battle()->battleGetUnitByPos(hex) == nullptr)
		{
			adjacent = hex;
			break;
		}
	}
	ASSERT_TRUE(adjacent.isAvailable());
	ASSERT_NE(addStack(BattleSide::DEFENDER, CreatureID(0), adjacent, 1), nullptr);
	ASSERT_TRUE(battle()->battleIsUnitBlocked(ballista));
	const_cast<CStack *>(ballista)->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::FREE_SHOOTING,
		BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(battle()->battleCanShoot(ballista))
		<< "The native engine must independently permit the shot despite contact";

	// The fixture chooses the exact acting stack only for this directed
	// decision. Real server validation and all subsequent turns are intact.
	battle()->activeStack = ballista->unitId();
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
	policy->activeStack(battleID, ballista);
	ASSERT_TRUE(callback->action.has_value());
	EXPECT_FALSE(callback->spellAction);
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT)
		<< "VCMI war machine must not inherit the Heroes II mobile archer contact rule";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Authoritative server rejected the independent native ballista shot";
	policy.reset();
	runToCompletion(std::nullopt, false);
	EXPECT_GT(recorder.shots, 0);
	EXPECT_GT(recorder.damage, 0);
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

// Heroes II only values Teleport for an offensive grounded melee creature
// lacking a current melee attack. VCMI adds the second destination target
// through teleport.lua. This checks a real server-accepted two-hex target
// spell and a real teleport movement packet, not just a scalar valuation.
TEST_F(AIIntegrationNativeBattle, fheroes2TeleportsMeleeTowardEnemyInRealBattle)
{
	prepareBattle(CreatureID(0), 100, CreatureID(2), 110);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::TELEPORT, 12);
	bool sawTeleportMove = false;
	recorder.onMovementBeforeApply = [&](const BattleStackMoved & movement)
	{
		if(movement.teleporting)
			sawTeleportMove = true;
	};
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.castsOf(SpellID::TELEPORT).empty())
		<< "FHeroes2 must independently choose native two-target Teleport";
	EXPECT_TRUE(sawTeleportMove)
		<< "VCMI must apply a genuine teleport movement, not just record a spell decision";
	EXPECT_GT(recorder.damage, 0);
}

// fheroes2 Earthquake considers surviving castle walls, ground-melee force
// and enemy shooters. The actual castle is damaged by native VCMI siege
// packets. Source valuation cannot be accepted based on a fake projected hit.
TEST_F(AIIntegrationNativeBattle, fheroes2EarthquakeUsesNativeWallsAndCompletesSiege)
{
	prepareSiege(false, CreatureID(2), 70, CreatureID(0), 80);
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_TRUE(battle()->hasFortifications());
	teachBattleSpell(attackerSideHero, SpellID::EARTHQUAKE, 12);
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.castsOf(SpellID::EARTHQUAKE).empty())
		<< "The source policy should independently cast Earthquake against a defended shooter garrison";
	EXPECT_FALSE(recorder.catapultAttacks.empty())
		<< "Authoritative VCMI siege effects must actually occur";
	EXPECT_GT(recorder.damage, 0);
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

// Pinned fheroes2 spellDispelValue assigns a positive source value to
// removing Berserker from an allied stack. Verify a real hostile cast and
// a distinct, independently chosen native Cure: no scripted policy and
// no score for a resisted or unchanged spell-effect projection.
// fheroes2's berserkTurn is a special creature turn, not a normal target
// scorer. An authoritative native Berserk cast must route the active unit
// through the independent FHeroes2 controller and produce a legal action.
TEST_F(AIIntegrationNativeBattle, fheroes2BerserkUnitExecutesNativeActionAndBattleCompletes)
{
	prepareBattle(CreatureID(0), 80, CreatureID(0), 80);
	ASSERT_FALSE(HasFatalFailure());
	const CStack * acting = dynamic_cast<const CStack *>(battle()->battleActiveUnit());
	ASSERT_NE(acting, nullptr);
	const auto side = acting->unitSide();
	auto * caster = side == BattleSide::ATTACKER ? defenderSideHero : attackerSideHero;
	teachBattleSpell(caster, SpellID::BERSERK, 6);
	ASSERT_TRUE(castOn(caster, SpellID::BERSERK, acting))
		<< "A real server spell must apply the controlled-turn status";
	ASSERT_TRUE(acting->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
	// Source planUnitTurn dispatches the compelled Berserk creature turn
	// before all hero magic. Provide a legal damaging spell to expose the
	// previously incorrect ordering in the actual controller.
	auto * friendlyHero = side == BattleSide::ATTACKER ? attackerSideHero : defenderSideHero;
	teachBattleSpell(friendlyHero, SpellID::MAGIC_ARROW, 12);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(side);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	ASSERT_EQ(callback->getBattle(battleID)->battleCanCastSpell(friendlyHero, spells::Mode::HERO),
		ESpellCastProblem::OK)
		<< "The controller's real VCMI hero must be eligible to cast Magic Arrow";
	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = true;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), side, false);
	policy->activeStack(battleID, acting);
	ASSERT_TRUE(callback->action.has_value()) << "FHeroes2 special turn returned no action";
	EXPECT_FALSE(callback->spellAction)
		<< "Compelled Berserk creature must act before a legal hero Magic Arrow";
	EXPECT_NE(callback->action->actionType, EActionType::HERO_SPELL);
	EXPECT_TRUE(callback->action->actionType == EActionType::WALK
		|| callback->action->actionType == EActionType::WALK_AND_ATTACK
		|| callback->action->actionType == EActionType::SHOOT
		|| callback->action->actionType == EActionType::DEFEND)
		<< "Berserk may only select a native combat/movement action";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "VCMI rejected the source-derived Berserk action";
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics");
	EXPECT_GT(recorder.damage, 0);
}

// Pinned fheroes2 ai_battle.cpp::berserkTurn: if no unit can be attacked
// this turn, findNearestCellNextToUnit supplies a reachable future melee
// position and getUnitMovementTarget produces the first legal move. The VCMI
// port must follow a real path predecessor, not merely minimize hex distance.
// This is native path evidence, not an executable Heroes II oracle.
TEST_F(AIIntegrationNativeBattle, fheroes2BerserkPursuesReachableFutureAttackCellOnNativePath)
{
	prepareBattle(CreatureID(0), 45, CreatureID(0), 40);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const auto * acting = dynamic_cast<const CStack *>(battle()->battleActiveUnit());
	ASSERT_NE(acting, nullptr);
	ASSERT_FALSE(acting->isShooter());
	const BattleSide side = acting->unitSide();
	const BattleSide enemySide = side == BattleSide::ATTACKER
		? BattleSide::DEFENDER : BattleSide::ATTACKER;
	const CStack * opponent = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == enemySide && unit->alive())
			opponent = unit.get();
	ASSERT_NE(opponent, nullptr);

	auto * caster = enemySide == BattleSide::ATTACKER ? attackerSideHero : defenderSideHero;
	teachBattleSpell(caster, SpellID::BERSERK, 6);
	ASSERT_TRUE(castOn(caster, SpellID::BERSERK, acting));
	ASSERT_TRUE(acting->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
	ASSERT_EQ(battle()->battleActiveUnit(), acting);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(side);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	const auto route = view->getReachability(acting);
	const auto available = view->battleGetAvailableHexes(route, acting, false);
	int shortest = GameConstants::BFIELD_SIZE;
	BattleHex goal;
	for(const auto & from : opponent->getAttackableHexes(acting))
	{
		if(!from.isValid() || !view->isMeleeAttackPossible(acting, opponent, from))
			continue;
		const int cost = static_cast<int>(route.distances[from.toInt()]);
		if(cost < shortest || (cost == shortest
			&& (!goal.isValid() || from.toInt() < goal.toInt())))
		{
			shortest = cost;
			goal = from;
		}
	}
	ASSERT_TRUE(goal.isValid());
	ASSERT_GT(shortest, static_cast<int>(acting->getMovementRange()))
		<< "Fixture must require future-turn melee pursuit";
	ASSERT_LT(shortest, GameConstants::BFIELD_SIZE);
	BattleHex firstStep;
	for(int i = 0; i < GameConstants::BFIELD_SIZE; ++i)
	{
		if(available.contains(goal) && goal != acting->getPosition())
		{
			firstStep = goal;
			break;
		}
		const auto previous = route.predecessors[goal.toInt()];
		if(!previous.isValid() || previous == goal)
			break;
		goal = previous;
	}
	ASSERT_TRUE(firstStep.isValid())
		<< "Fixture needs a complete native multi-turn predecessor route";

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
		battle()->getSideHero(BattleSide::DEFENDER), side, false);
	policy->activeStack(battleID, acting);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK)
		<< "Berserk must approach its reachable future melee target";
	ASSERT_FALSE(callback->action->target.empty());
	EXPECT_EQ(callback->action->target.front().hexValue, firstStep)
		<< "Source-derived pursuit must use native path, not geometric guess";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The authoritative server rejected a compelled future-target move";
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics");
	EXPECT_GT(recorder.damage, 0);
}

// Pinned fheroes2 ai_battle.cpp::berserkTurn shoots nearestUnits.front(),
// never the first more distant target the native API happens to allow.
// VCMI's restricted FREE_SHOOTING permits firing while engaged, but not at
// an adjacent unit. The old partial port incorrectly skipped the closer
// melee-only target and shot the remote archer. This directed server battle
// requires a different, independently selected native melee command.
TEST_F(AIIntegrationNativeBattle, fheroes2BerserkShooterDoesNotSkipAdjacentNearestForDistantShot)
{
	prepareBattle(CreatureID(2), 70, CreatureID(2), 50);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const auto * acting = dynamic_cast<const CStack *>(battle()->battleActiveUnit());
	ASSERT_NE(acting, nullptr);
	ASSERT_TRUE(acting->isShooter());
	const BattleSide side = acting->unitSide();
	const BattleSide enemySide = side == BattleSide::ATTACKER
		? BattleSide::DEFENDER : BattleSide::ATTACKER;
	const CStack * remote = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == enemySide && unit->isShooter() && unit->alive())
			remote = unit.get();
	ASSERT_NE(remote, nullptr);
	const BattleHex nearHex(acting->getPosition().toInt()
		+ (side == BattleSide::ATTACKER ? 1 : -1));
	ASSERT_TRUE(nearHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(nearHex), nullptr);
	ASSERT_GT(BattleHex::getDistance(acting->getPosition(), remote->getPosition()), 1);
	const CStack * nearest = addStack(enemySide, CreatureID(0), nearHex, 15);
	ASSERT_NE(nearest, nullptr);
	ASSERT_EQ(BattleHex::getDistance(acting->getPosition(), nearest->getPosition()), 1);
	ASSERT_EQ(battle()->battleActiveUnit(), acting);

	// Native "Steel Elves" rule projection: a blocked archer can shoot
	// past the adjacent creature, but must melee that adjacent creature.
	// This bonus is fixture-only: no creature or gameplay rule is changed.
	auto * mutableActing = const_cast<CStack *>(acting);
	mutableActing->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::FREE_SHOOTING, BonusSource::OTHER, 1, BonusSourceID(),
		BonusSubtypeID(BonusCustomSubtype::freeShootingExceptAdjacent)));
	const auto * caster = enemySide == BattleSide::ATTACKER ? attackerSideHero : defenderSideHero;
	teachBattleSpell(const_cast<CGHeroInstance *>(caster), SpellID::BERSERK, 6);
	ASSERT_TRUE(castOn(const_cast<CGHeroInstance *>(caster), SpellID::BERSERK, acting));
	ASSERT_TRUE(acting->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
	ASSERT_TRUE(battle()->battleCanShoot(acting))
		<< "Restricted native free shooting must allow a distant shot while engaged";
	ASSERT_FALSE(battle()->battleCanShoot(acting, nearHex))
		<< "The adjacent Berserk target must be melee-only under native rules";
	ASSERT_TRUE(battle()->battleCanShoot(acting, remote->getPosition()))
		<< "A distant native-legal shot is required to expose the old retargeting bug";

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(side);
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
		battle()->getSideHero(BattleSide::DEFENDER), side, false);
	policy->activeStack(battleID, acting);
	ASSERT_TRUE(callback->action.has_value());
	EXPECT_FALSE(callback->spellAction);
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK)
		<< "Berserk archer must not skip its nearest melee-only unit to shoot farther";
	ASSERT_GE(callback->action->target.size(), 2u);
	EXPECT_EQ(battle()->battleGetUnitByPos(callback->action->target[1].hexValue), nearest);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Authoritative VCMI server rejected nearest-unit Berserk melee";
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_GT(recorder.damage, 0);
}

// The pinned ai_battle.cpp::archerDecision returns an empty command list if
// no enemy can be selected; planUnitTurn emits SKIP, not DEFEND. Heroes III
// adds native invincibility, which has no exact Heroes II equivalent: project
// it through VCMI's own legality rather than inventing an attack or defense.
// The independent FHeroes2 decision must reach the real server and complete.
TEST_F(AIIntegrationNativeBattle, fheroes2UntargetableArcherUsesSourceSkipWithoutNativeDefendBonus)
{
	prepareBattle(CreatureID(45), 30, CreatureID(0), 40);
	ASSERT_FALSE(HasFatalFailure());
	CStack * shooter = nullptr;
	CStack * invincibleEnemy = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(45))
			shooter = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			invincibleEnemy = unit.get();
	}
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(invincibleEnemy, nullptr);
	ASSERT_TRUE(shooter->isShooter());
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);
	ASSERT_FALSE(shooter->hasBonusOfType(BonusType::UNIT_DEFENDING));
	// Temporary native fixture bonus. It is removed after the first server
	// action so the remaining real battle can finish without changing rules.
	invincibleEnemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::INVINCIBLE, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(invincibleEnemy->isInvincible());
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
	ASSERT_FALSE(callback->spellAction);
	ASSERT_EQ(callback->action->actionType, EActionType::NO_ACTION)
		<< "Normal archer with no selectable opponent must use source SKIP, not VCMI DEFEND";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The authoritative server rejected FHeroes2's independently chosen SKIP";
	const auto * after = battle()->battleGetStackByID(shooter->unitId());
	ASSERT_NE(after, nullptr);
	EXPECT_FALSE(after->hasBonusOfType(BonusType::UNIT_DEFENDING))
		<< "A source SKIP must not grant native Heroes III defense";
	EXPECT_EQ(recorder.shots, 0);
	EXPECT_EQ(recorder.meleeAttacks, 0);
	EXPECT_EQ(recorder.damage, 0);
	invincibleEnemy->removeBonuses(Selector::type()(BonusType::INVINCIBLE));
	ASSERT_FALSE(invincibleEnemy->isInvincible());
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

// In the pinned ai_battle.cpp::planUnitTurn a meleeUnitDefense result with
// no target cell produces SKIP. VCMI DEFEND is not an equivalent no-op:
// it applies UNIT_DEFENDING. Keep the defensive decision independent of
// whether native movement or an attack happens to be available.
TEST_F(AIIntegrationNativeBattle, fheroes2DefensiveInfantryWithoutCommandSkipsWithoutDefendBonus)
{
	prepareBattle(CreatureID(0), 40, CreatureID(0), 20);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * guard = nullptr;
	const CStack * enemy = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			guard = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			enemy = unit.get();
	}
	ASSERT_NE(guard, nullptr);
	ASSERT_NE(enemy, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), guard);
	const BattleHex archerHex(guard->getPosition().toInt() + GameConstants::BFIELD_WIDTH);
	ASSERT_TRUE(archerHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(archerHex), nullptr);
	const auto * archer = addStack(BattleSide::ATTACKER, CreatureID(2), archerHex, 15);
	ASSERT_NE(archer, nullptr);
	ASSERT_TRUE(archer->isShooter());
	ASSERT_GT(archer->getMovementRange(), 0);
	// A nearby friendly archer produces the source defensive stance, but
	// the enemy is too far to threaten that shooter or receive melee.
	ASSERT_GT(BattleHex::getDistance(enemy->getPosition(), archerHex),
		static_cast<int>(enemy->getMovementRange()) + 1);
	ASSERT_GT(BattleHex::getDistance(enemy->getPosition(), guard->getPosition()),
		static_cast<int>(guard->getMovementRange()) + 1);
	const double friendlyStrength = static_cast<double>(guard->estimateCombatValue())
		+ archer->estimateCombatValue();
	ASSERT_GT(static_cast<double>(archer->estimateCombatValue()) / friendlyStrength, 0.15);
	ASSERT_LT(friendlyStrength, 10.0 * enemy->estimateCombatValue());
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
	ASSERT_EQ(callback->action->actionType, EActionType::NO_ACTION)
		<< "No source melee defense command means SKIP, never native DEFEND: "
		<< callback->action->toString();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Authoritative native server rejected the independent source SKIP";
	const auto * after = battle()->battleGetStackByID(guard->unitId());
	ASSERT_NE(after, nullptr);
	EXPECT_FALSE(after->hasBonusOfType(BonusType::UNIT_DEFENDING));
	EXPECT_EQ(recorder.shots, 0);
	EXPECT_EQ(recorder.meleeAttacks, 0);
	EXPECT_EQ(recorder.damage, 0);
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

// Source ai_battle.cpp::berserkTurn uses SKIP when no target is reachable.
// The native equivalent is NO_ACTION, not DEFEND: the latter grants a
// defense bonus and emits a DEFEND event. Exercise the actual server action
// and then finish the remaining battle, rather than just checking an enum.
TEST_F(AIIntegrationNativeBattle, fheroes2SourceSkipUsesServerNoActionWithoutDefenseBonus)
{
	prepareBattle(CreatureID(0), 35, CreatureID(0), 15);
	ASSERT_FALSE(HasFatalFailure());
	const auto * acting = dynamic_cast<const CStack *>(battle()->battleActiveUnit());
	ASSERT_NE(acting, nullptr);
	ASSERT_FALSE(acting->hasBonusOfType(BonusType::UNIT_DEFENDING));
	const auto unitId = acting->unitId();
	const auto side = acting->unitSide();
	const auto player = battle()->sideToPlayer(side);
	const auto battleID = battle()->getBattleID();

	const BattleAction skip = BattleAction::makeNoAction(acting);
	ASSERT_EQ(skip.actionType, EActionType::NO_ACTION);
	ASSERT_EQ(skip.stackNumber, unitId);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, skip))
		<< "VCMI must accept the source SKIP projection as a real unit action";
	const auto * after = battle()->battleGetStackByID(unitId);
	ASSERT_NE(after, nullptr);
	EXPECT_FALSE(after->hasBonusOfType(BonusType::UNIT_DEFENDING))
		<< "Skipping a compelled turn must not grant the Heroes III defend bonus";
	EXPECT_EQ(after->getPosition(), acting->getPosition());
	EXPECT_EQ(recorder.meleeAttacks, 0);
	EXPECT_EQ(recorder.shots, 0);
	EXPECT_EQ(recorder.damage, 0)
		<< "A source SKIP must not generate a native attack or damage packet";

	runToCompletion(std::nullopt, false, "FHeroes2Tactics");
	EXPECT_GT(recorder.damage, 0);
}

// Independent pinned-source regression: fheroes2 berserkTurn checks
// !isHandFighting() before any ranged command. VCMI's unrestricted
// FREE_SHOOTING is a Heroes III mechanic and does not override that
// source-policy ordering. Verify an actual alternative legal SHOOT
// exists, yet FHeroes2 chooses melee and the server accepts it.
TEST_F(AIIntegrationNativeBattle, fheroes2BerserkBlockedShooterPrefersMeleeEvenWithNativeFreeShooting)
{
	prepareBattle(CreatureID(2), 70, CreatureID(2), 50);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const auto * acting = dynamic_cast<const CStack *>(battle()->battleActiveUnit());
	ASSERT_NE(acting, nullptr);
	ASSERT_TRUE(acting->isShooter());
	const BattleSide side = acting->unitSide();
	const BattleSide enemySide = side == BattleSide::ATTACKER
		? BattleSide::DEFENDER : BattleSide::ATTACKER;
	const CStack * remote = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == enemySide && unit->isShooter() && unit->alive())
			remote = unit.get();
	ASSERT_NE(remote, nullptr);
	const BattleHex nearHex(acting->getPosition().toInt()
		+ (side == BattleSide::ATTACKER ? 1 : -1));
	ASSERT_TRUE(nearHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(nearHex), nullptr);
	ASSERT_GT(BattleHex::getDistance(acting->getPosition(), remote->getPosition()), 1);
	const CStack * nearest = addStack(enemySide, CreatureID(0), nearHex, 15);
	ASSERT_NE(nearest, nullptr);
	ASSERT_EQ(BattleHex::getDistance(acting->getPosition(), nearest->getPosition()), 1);
	ASSERT_EQ(battle()->battleActiveUnit(), acting);

	// Heroes II berserkTurn requires !isHandFighting() before shooting.
	// VCMI unrestricted FREE_SHOOTING (Bow of the Sharpshooter effect)
	// permits a shot even while engaged. Keep the source's hand-fighting
	// branch, projecting its decision onto a legal native melee attack.
	// This bonus is fixture-only and does not modify gameplay rules.
	auto * mutableActing = const_cast<CStack *>(acting);
	mutableActing->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::FREE_SHOOTING, BonusSource::OTHER, 1, BonusSourceID()));
	const auto * caster = enemySide == BattleSide::ATTACKER ? attackerSideHero : defenderSideHero;
	teachBattleSpell(const_cast<CGHeroInstance *>(caster), SpellID::BERSERK, 6);
	ASSERT_TRUE(castOn(const_cast<CGHeroInstance *>(caster), SpellID::BERSERK, acting));
	ASSERT_TRUE(acting->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
	ASSERT_TRUE(battle()->battleCanShoot(acting))
		<< "Unrestricted native free shooting must allow a shot while engaged";
	ASSERT_TRUE(battle()->battleIsUnitBlocked(acting))
		<< "Adjacent hostile must block the native Berserk shooter";
	ASSERT_TRUE(battle()->battleCanShoot(acting, nearHex))
		<< "Unrestricted native free shooting must allow the adjacent shot";
	ASSERT_TRUE(battle()->battleCanShoot(acting, remote->getPosition()))
		<< "Native free shooting must allow ranged targets, unlike source hand-fighting priority";

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(side);
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
		battle()->getSideHero(BattleSide::DEFENDER), side, false);
	policy->activeStack(battleID, acting);
	ASSERT_TRUE(callback->action.has_value());
	EXPECT_FALSE(callback->spellAction);
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK)
		<< "Pinned berserkTurn must select melee while hand-fighting, even if VCMI permits shooting";
	ASSERT_GE(callback->action->target.size(), 2u);
	EXPECT_EQ(battle()->battleGetUnitByPos(callback->action->target[1].hexValue), nearest);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Authoritative VCMI server rejected the source-ordered Berserk melee";
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_GT(recorder.damage, 0);
}

// Pinned ai_battle.cpp::archerDecision tests isHandFighting() before
// considering shots. In Heroes III, unrestricted FREE_SHOOTING can make
// both the adjacent and distant shots legal despite contact. Check the
// normal (non-Berserk) policy still independently chooses the native
// melee branch, and that the authoritative battle server accepts it.
TEST_F(AIIntegrationNativeBattle, fheroes2EngagedArcherPrefersMeleeDespiteNativeFreeShooting)
{
	prepareBattle(CreatureID(2), 70, CreatureID(2), 50);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const auto * acting = dynamic_cast<const CStack *>(battle()->battleActiveUnit());
	ASSERT_NE(acting, nullptr);
	ASSERT_TRUE(acting->isShooter());
	ASSERT_FALSE(acting->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
	const BattleSide side = acting->unitSide();
	const BattleSide enemySide = side == BattleSide::ATTACKER
		? BattleSide::DEFENDER : BattleSide::ATTACKER;
	const CStack * remote = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == enemySide && unit->isShooter() && unit->alive())
			remote = unit.get();
	ASSERT_NE(remote, nullptr);
	const BattleHex nearHex(acting->getPosition().toInt()
		+ (side == BattleSide::ATTACKER ? 1 : -1));
	ASSERT_TRUE(nearHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(nearHex), nullptr);
	ASSERT_GT(BattleHex::getDistance(acting->getPosition(), remote->getPosition()), 1);
	const CStack * blocker = addStack(enemySide, CreatureID(2), nearHex, 15);
	ASSERT_NE(blocker, nullptr);
	ASSERT_GE(static_cast<int>(blocker->getMovementRange()) + 2,
		static_cast<int>(acting->getMovementRange()))
		<< "The adjacent archer must make a source safe-retreat attempt pointless";
	ASSERT_EQ(battle()->battleActiveUnit(), acting);

	// Only alter this battle fixture's bonus, never game rules.
	const_cast<CStack *>(acting)->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::FREE_SHOOTING,
		BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(battle()->battleIsUnitBlocked(acting));
	ASSERT_TRUE(battle()->battleCanShoot(acting));
	ASSERT_TRUE(battle()->battleCanShoot(acting, nearHex));
	ASSERT_TRUE(battle()->battleCanShoot(acting, remote->getPosition()))
		<< "The source policy must reject a genuinely legal alternative native shot";

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(side);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto viewedBattle = callback->getBattle(battleID);
	ASSERT_NE(viewedBattle, nullptr);
	ASSERT_FALSE(viewedBattle->meleeAttackHexes(acting, blocker,
		acting->getPosition()).empty());
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
		battle()->getSideHero(BattleSide::DEFENDER), side, false);
	policy->activeStack(battleID, acting);
	ASSERT_TRUE(callback->action.has_value());
	EXPECT_FALSE(callback->spellAction);
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK)
		<< "Original archerDecision enters hand-to-hand branch before ranged targeting";
	ASSERT_GE(callback->action->target.size(), 2u);
	EXPECT_EQ(viewedBattle->battleGetUnitByPos(callback->action->target[1].hexValue), blocker);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Native server rejected FHeroes2's independently selected archer melee";
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2CuresNativeBerserkAndCompletesBattle)
{
	prepareBattle(CreatureID(0), 80, CreatureID(0), 80);
	ASSERT_FALSE(HasFatalFailure());
	const CStack * friendly = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			friendly = unit.get();
	ASSERT_NE(friendly, nullptr);
	const auto healthBefore = friendly->getAvailableHealth();
	teachBattleSpell(defenderSideHero, SpellID::BERSERK, 6);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::BERSERK, friendly))
		<< "The original debuff must be applied by the real server";
	const auto berserk = Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BERSERK)));
	ASSERT_TRUE(friendly->hasBonus(berserk));
	ASSERT_TRUE(friendly->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
	ASSERT_EQ(friendly->getAvailableHealth(), healthBefore)
		<< "There must be no missing health to explain Cure's selection";
	teachBattleSpell(attackerSideHero, SpellID::CURE, 6);
	bool sawNativeControlRestoration = false;
	// Like the existing Cure/Blind/Slow regressions, inspect after the
	// injury/effect packet has applied. BattleSpellCast arrives earlier
	// than the status-removal packet on the authoritative server.
	recorder.onInjuriesAfterApply = [&](const StacksInjured &)
	{
		if(!recorder.castsOf(SpellID::CURE).empty())
			sawNativeControlRestoration |= !friendly->hasBonus(berserk)
				&& !friendly->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE);
	};
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.castsOf(SpellID::CURE).empty())
		<< "Source-derived FHeroes2 must independently select Cure for Berserk";
	EXPECT_TRUE(sawNativeControlRestoration)
		<< "The real engine must remove Berserk control, not merely accept Cure";
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
// fheroes2 spellDispelValue considers *friendly* negative magic, not only
// enemy positive effects. Verify an independent hero Dispel cast restores
// Slow from an unwounded stack through actual server effect packets.
TEST_F(AIIntegrationNativeBattle, fheroes2DispelsFriendlySlowAndCompletesBattle)
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
		<< "The Dispel priority must value a debuff, not pre-existing damage";
	teachBattleSpell(attackerSideHero, SpellID::DISPEL, 6);
	bool observedNativeSpeedRecovery = false;
	recorder.onInjuriesAfterApply = [&](const StacksInjured &)
	{
		if(!recorder.castsOf(SpellID::DISPEL).empty())
			observedNativeSpeedRecovery |= !friendly->hasBonus(slow)
				&& friendly->getMovementRange() == originalSpeed;
	};
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.castsOf(SpellID::DISPEL).empty())
		<< "FHeroes2 must select Dispel for a real Slow, not just lost HP";
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

// Source spellDragonSlayerValue must never waste mana on a non-dragon
// army. Heroes III uses its own Slayer target classifications and bonus.
TEST_F(AIIntegrationNativeBattle, fheroes2CastsNativeSlayerAgainstDragonAndCompletesBattle)
{
	prepareBattle(CreatureID(0), 100, CreatureID(132), 1);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::SLAYER, 6);
	const CStack * buffed = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			buffed = unit.get();
	ASSERT_NE(buffed, nullptr);
	bool nativeSlayerApplied = false;
	recorder.onInjuriesAfterApply = [&](const StacksInjured &)
	{
		if(!recorder.castsOf(SpellID::SLAYER).empty())
			nativeSlayerApplied |= buffed->hasBonusOfType(BonusType::SLAYER);
	};
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.castsOf(SpellID::SLAYER).empty())
		<< "Source-derived Dragon Slayer must produce a real hero cast against dragons";
	EXPECT_TRUE(nativeSlayerApplied) << "The server must apply the native Slayer effect";
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

// fheroes2 spellSummonValue values the actual summoned troop, with a
// reduction when already overwhelming the defender. This native VCMI
// projection must create elemental stacks through the server, rather than
// merely recording the spell choice in the decision callback.
TEST_F(AIIntegrationNativeBattle, fheroes2UsesNativeElementalSummonAndCompletesBattle)
{
	prepareBattle(CreatureID(0), 60, CreatureID(0), 100);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::SUMMON_AIR_ELEMENTAL, 12);
	bool sawNativeElemental = false;
	recorder.onUnitsChangedAfterApply = [&](const BattleUnitsChanged &)
	{
		for(const auto & unit : battle()->stacks)
		{
			if(unit->alive() && unit->unitSide() == BattleSide::ATTACKER
				&& unit->creatureId() == CreatureID(CreatureID::AIR_ELEMENTAL))
				sawNativeElemental = true;
		}
	};
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.castsOf(SpellID::SUMMON_AIR_ELEMENTAL).empty())
		<< "The independent source-policy must select the elemental summon";
	EXPECT_TRUE(sawNativeElemental)
		<< "The game server must create a living elemental stack";
	EXPECT_GT(replayedUnitChangeEvents, 0)
		<< "Summon insertion must be delivered through native unit-change events";
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


TEST_F(AIIntegrationNativeBattle, fheroes2BlindedEnemyShooterDropsSourceRangedThreatInRealBattle)
{
	// Pinned ai_battle.cpp::analyzeBattleState excludes isImmovable()
	// shooters from ranged strength; native Blind may retain raw speed.
	// Unlike Forgetfulness, checked in the preceding regression, this
	// unit cannot fire in its next turn or count as a moving archer.
	prepareBattle(CreatureID(0), 60, CreatureID(2), 30);
	ASSERT_FALSE(HasFatalFailure());
	const CStack * enemyArcher = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(2))
			enemyArcher = unit.get();
	ASSERT_NE(enemyArcher, nullptr);
	ASSERT_TRUE(enemyArcher->isShooter());
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_GT(AIIntegration::FHeroes2SpellPolicy::sourceArcherStrength(*view, player, false), 0);

	teachBattleSpell(attackerSideHero, SpellID::BLIND);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::FIRE_MAGIC),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::BLIND, enemyArcher));
	ASSERT_TRUE(enemyArcher->hasBonusOfType(BonusType::NOT_ACTIVE));
	ASSERT_GT(enemyArcher->getMovementRange(), 0)
		<< "Fixture requires raw native movement speed under real Blind";
	EXPECT_DOUBLE_EQ(AIIntegration::FHeroes2SpellPolicy::sourceArcherStrength(*view, player, false), 0);

	const auto * active = dynamic_cast<const CStack *>(battle()->battleActiveUnit());
	ASSERT_NE(active, nullptr);
	ASSERT_EQ(active->unitSide(), BattleSide::ATTACKER);
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
	policy->activeStack(battleID, active);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_FALSE(callback->spellAction);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Real VCMI server rejected the FHeroes2 decision after Blind";
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2FarewellSpellChoosesNativeDamageAndCompletesBattle)
{
	// Pinned planUnitTurn::farewellSpellcast uses selectBestSpell(..., true)
	// before queued RETREAT/SURRENDER. Check the damage-only mode against
	// native VCMI spell legality and complete the resulting real battle.
	// This case verifies the spell component, not the separate strategic
	// retreat decision or the entire two-event withdrawal sequence.
	prepareBattle(CreatureID(0), 30, CreatureID(0), 40);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::MAGIC_ARROW, 6);
	teachSpell(attackerSideHero, SpellID::HASTE);
	const auto * active = dynamic_cast<const CStack *>(battle()->battleActiveUnit());
	ASSERT_NE(active, nullptr);
	ASSERT_EQ(active->unitSide(), BattleSide::ATTACKER);
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	const auto * hero = callback->getBattle(battleID)->battleGetMyHero();
	ASSERT_NE(hero, nullptr);
	const auto initialMana = hero->mana;
	const auto selected = AIIntegration::FHeroes2SpellPolicy::choose(
		callback->getBattle(battleID), environment.get(), active, true);
	ASSERT_TRUE(selected.has_value()) << "Legal damage spell is required for the farewell fixture";
	ASSERT_EQ(selected->actionType, EActionType::HERO_SPELL);
	const auto * spell = LIBRARY->spellh->getById(selected->spell);
	ASSERT_NE(spell, nullptr);
	EXPECT_TRUE(spell->isDamage()) << "Farewell must not choose a legal non-damaging buff";
	EXPECT_EQ(hero->mana, initialMana) << "Native spell preview must not consume live mana";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *selected))
		<< "Authoritative server rejected the independently selected farewell damage spell";
	EXPECT_GT(recorder.spellPackets, 0);
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2FarewellIgnoresSpellWithNoCreatureDeaths)
{
	// Pinned ai_battle_spell.cpp::spellDamageValue(retreating=true)
	// counts killed creatures, not fractional hit-point loss. A 20-damage
	// Magic Arrow cannot kill a fresh 25-HP Griffin and must not be cast
	// just before leaving the battlefield. Native VCMI owns spell damage.
	prepareBattle(CreatureID(0), 20, CreatureID(4), 20);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::MAGIC_ARROW, 1);
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	const auto * active = dynamic_cast<const CStack *>(battle()->battleActiveUnit());
	ASSERT_NE(active, nullptr);
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	const auto selected = AIIntegration::FHeroes2SpellPolicy::choose(view, environment.get(), active, true);
	EXPECT_FALSE(selected.has_value())
		<< "Farewell must not waste mana on a wound with no killed creatures";
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2StrategicFarewellCastThenNativeRetreat)
{
	// In the pinned planUnitTurn, a doomed AI hero with a valuable artifact
	// casts a damage spell and then retreats. VCMI's single-action callback
	// requires two distinct authoritative server submissions.
	prepareBattle(CreatureID(19), 1, CreatureID(16), 600);
	ASSERT_FALSE(HasFatalFailure());
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	gameState()->players.at(player).human = false;
	ASSERT_TRUE(gameHandler->giveHeroNewArtifact(attackerSideHero,
		ArtifactID(7), ArtifactPosition::RIGHT_HAND))
		<< "Test hero must carry a valuable worn weapon to trigger source retreat";
	ASSERT_NE(attackerSideHero->getArt(ArtifactPosition::RIGHT_HAND), nullptr);
	teachBattleSpell(attackerSideHero, SpellID::MAGIC_ARROW, 6);
	const auto * active = dynamic_cast<const CStack *>(battle()->battleActiveUnit());
	ASSERT_NE(active, nullptr);
	ASSERT_EQ(active->unitSide(), BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanFlee());
	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	policy->initBattleInterface(environment, callback); // AI, not human autocombat
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	policy->activeStack(battleID, active);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::HERO_SPELL)
		<< "Pinned fheroes2 queues a legal damage spell before abandoning a lost battle";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Native VCMI rejected the source-derived farewell spell";
	EXPECT_GT(recorder.spellPackets, 0);
	ASSERT_TRUE(recorder.battleResults.empty()) << "The farewell spell must not finish the fight";
	callback->action.reset();
	const auto * resumed = dynamic_cast<const CStack *>(battle()->battleActiveUnit());
	ASSERT_NE(resumed, nullptr);
	ASSERT_EQ(resumed->unitSide(), BattleSide::ATTACKER);
	policy->activeStack(battleID, resumed);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::RETREAT)
		<< "The queued native withdrawal must follow, rather than repeat, the hero spell";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Authoritative VCMI rejected the legal retreat after farewell casting";
	ASSERT_EQ(recorder.battleResults.size(), 1u);
	EXPECT_EQ(recorder.battleResults.back().winner, BattleSide::DEFENDER);
	if(recorder.battleResults.back().questionID != QuestionID::NONE)
	{
		ASSERT_TRUE(gameHandler->answerQuestion(recorder.battleResults.back().questionID, 0, player));
	}
	EXPECT_TRUE(gameState()->currentBattles.empty());
	EXPECT_EQ(recorder.acceptedResults, 1);
	callback->onBattleEnded(battleID);
}


// Pinned planUnitTurn::hasValuableArtifacts checks artifact *bonuses*,
// not mere inventory occupancy. The Heroes III Grail has no combat bonus
// in core artifact data, and no direct equivalent in Heroes II. This narrow
// source-predicate projection intentionally does not assign it an invented
// bonus. The second decision in the SAME live battle adds a bonus-bearing
// backpack weapon to isolate that distinction before a server RETREAT.
TEST_F(AIIntegrationNativeBattle, fheroes2BonusFreeGrailDoesNotTriggerSourceRetreat)
{
	prepareBattle(CreatureID(19), 1, CreatureID(16), 600);
	ASSERT_FALSE(HasFatalFailure());
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	gameState()->players.at(player).human = false;
	teachSpell(attackerSideHero, SpellID::HASTE);
	attackerSideHero->mana = 0;
	const auto * book = attackerSideHero->getArt(ArtifactPosition::SPELLBOOK);
	ASSERT_NE(book, nullptr);
	ASSERT_NE(book->getType(), nullptr);
	EXPECT_TRUE(book->getType()->getExportedBonusList().empty());
	EXPECT_TRUE(book->getType()->instanceBonuses.empty());
	EXPECT_TRUE(book->getExportedBonusList().empty())
		<< "The book must not contain an intrinsic combat bonus";
	for(const auto & slot : ArtifactUtils::commonWornSlots())
	{
		if(slot != ArtifactPosition::SPELLBOOK)
		{
			ASSERT_EQ(attackerSideHero->getArt(slot), nullptr)
				<< "The baseline hero must carry no other equipment";
		}
	}
	// Distinguish a bare Heroes III inventory entry from a bonus-bearing
	// original-style artifact. The Grail is economically important in H3,
	// but core config/artifacts.json defines no combat bonuses for it.
	const ArtifactPosition grailSlot(ArtifactPosition::BACKPACK_START);
	ASSERT_TRUE(gameHandler->giveHeroNewArtifact(attackerSideHero, ArtifactID::GRAIL, grailSlot));
	const auto * grail = attackerSideHero->getArt(grailSlot);
	ASSERT_NE(grail, nullptr);
	ASSERT_NE(grail->getType(), nullptr);
	EXPECT_TRUE(grail->getType()->getExportedBonusList().empty());
	EXPECT_TRUE(grail->getType()->instanceBonuses.empty());
	EXPECT_TRUE(grail->getExportedBonusList().empty());
	ASSERT_EQ(attackerSideHero->artifactsInBackpack.size(), 1u);
	const auto * active = dynamic_cast<const CStack *>(battle()->battleActiveUnit());
	ASSERT_NE(active, nullptr);
	ASSERT_EQ(active->unitSide(), BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanFlee());
	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	// Use the native AI overload: the three-argument callback denotes
	// human autocombat and deliberately bypasses source hero withdrawal.
	// Hero mana is zero above, so farewell casting is already impossible.
	policy->initBattleInterface(environment, callback);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	policy->activeStack(battleID, active);
	ASSERT_TRUE(callback->action.has_value());
	EXPECT_NE(callback->action->actionType, EActionType::RETREAT)
		<< "An H3 Grail carries no source-equivalent bonus and must not force source retreat";
	EXPECT_NE(callback->action->actionType, EActionType::SURRENDER);

	const ArtifactPosition backpackSlot(ArtifactPosition::BACKPACK_START + 1);
	ASSERT_TRUE(gameHandler->giveHeroNewArtifact(attackerSideHero, ArtifactID(7), backpackSlot));
	const auto * weapon = attackerSideHero->getArt(backpackSlot);
	ASSERT_NE(weapon, nullptr);
	ASSERT_NE(weapon->getType(), nullptr);
	EXPECT_FALSE(weapon->getType()->getExportedBonusList().empty())
		<< "The source-positive artifact must actually carry a native VCMI bonus";
	callback->action.reset();
	policy->activeStack(battleID, active);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::RETREAT)
		<< "Bonus-bearing backpack artifact should independently change the source decision";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The VCMI server must authorize and accept the policy-selected retreat";
	ASSERT_EQ(recorder.battleResults.size(), 1u);
	EXPECT_EQ(recorder.battleResults.back().winner, BattleSide::DEFENDER);
	if(recorder.battleResults.back().questionID != QuestionID::NONE)
		{
			ASSERT_TRUE(gameHandler->answerQuestion(recorder.battleResults.back().questionID, 0, player));
		}
	EXPECT_TRUE(gameState()->currentBattles.empty());
	EXPECT_EQ(recorder.acceptedResults, 1);
	callback->onBattleEnded(battleID);
}

// Pinned ihhub/fheroes2 ai_battle.cpp::planUnitTurn checks the entire
// BagArtifacts when deciding to save a hero from an overwhelming army.
// Heroes III keeps unequipped artifacts in a separate backpack; carrying
// an artifact there must not silently turn the same source retreat into FIGHT.
TEST_F(AIIntegrationNativeBattle, fheroes2CarriedBackpackArtifactTriggersNativeRetreat)
{
	prepareBattle(CreatureID(19), 1, CreatureID(16), 600);
	ASSERT_FALSE(HasFatalFailure());
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	gameState()->players.at(player).human = false;
	const ArtifactPosition backpackSlot(ArtifactPosition::BACKPACK_START);
	ASSERT_TRUE(gameHandler->giveHeroNewArtifact(attackerSideHero, ArtifactID(7), backpackSlot))
		<< "A real carried artifact must be placed in the hero's backpack";
	ASSERT_NE(attackerSideHero->getArt(backpackSlot), nullptr);
	ASSERT_EQ(attackerSideHero->getArt(ArtifactPosition::RIGHT_HAND), nullptr)
		<< "The source BagArtifacts decision must not depend on wearing the weapon";
	attackerSideHero->mana = 0; // Isolate withdrawal without farewell spells.

	const auto * active = dynamic_cast<const CStack *>(battle()->battleActiveUnit());
	ASSERT_NE(active, nullptr);
	ASSERT_EQ(active->unitSide(), BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanFlee());
	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	policy->initBattleInterface(environment, callback);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	policy->activeStack(battleID, active);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::RETREAT)
		<< "FHeroes2 must value carried backpack artifacts in its source withdrawal decision";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Authoritative VCMI server rejected the policy-selected retreat";
	ASSERT_EQ(recorder.battleResults.size(), 1u);
	EXPECT_EQ(recorder.battleResults.back().winner, BattleSide::DEFENDER);
	if(recorder.battleResults.back().questionID != QuestionID::NONE)
		{
			ASSERT_TRUE(gameHandler->answerQuestion(recorder.battleResults.back().questionID, 0, player));
		}
	EXPECT_TRUE(gameState()->currentBattles.empty());
	EXPECT_EQ(recorder.acceptedResults, 1);
	callback->onBattleEnded(battleID);
}

TEST_F(AIIntegrationNativeBattle, fheroes2WithdrawalPrefersRetreatWhenNativeSurrenderIsAlsoLegal)
{
	// Pinned ai_battle.cpp:828-870 chooses SURRENDER only if RETREAT is
	// unavailable. In VCMI CBattleInfoEssentials::battleCanSurrender
	// explicitly requires battleCanFlee, so that source branch cannot be
	// reached under current Heroes III rules. Do not silently equate them.
	prepareBattle(CreatureID(19), 1, CreatureID(16), 600);
	ASSERT_FALSE(HasFatalFailure());
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	gameState()->players.at(player).human = false;
	ASSERT_TRUE(gameHandler->giveHeroNewArtifact(attackerSideHero,
		ArtifactID(7), ArtifactPosition::RIGHT_HAND));
	attackerSideHero->mana = 0; // No farewell cast: isolate withdrawal priority.
	gameHandler->giveResource(player, GameResID::GOLD, 100000);
	const auto * active = dynamic_cast<const CStack *>(battle()->battleActiveUnit());
	ASSERT_NE(active, nullptr);
	ASSERT_EQ(active->unitSide(), BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanFlee());
	ASSERT_TRUE(view->battleCanSurrender(player))
		<< "Both VCMI withdrawal actions must be legal for this Heroes II/III contrast";
	ASSERT_GE(view->battleGetSurrenderCost(), 0);
	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	policy->initBattleInterface(environment, callback);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	policy->activeStack(battleID, active);
	ASSERT_TRUE(callback->action.has_value());
	EXPECT_EQ(callback->action->actionType, EActionType::RETREAT)
		<< "Source chooses retreat first, even when VCMI permits paid surrender";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Authoritative server rejected the independently selected withdrawal";
	ASSERT_EQ(recorder.battleResults.size(), 1u);
	EXPECT_EQ(recorder.battleResults.back().winner, BattleSide::DEFENDER);
	if(recorder.battleResults.back().questionID != QuestionID::NONE)
	{
		ASSERT_TRUE(gameHandler->answerQuestion(recorder.battleResults.back().questionID, 0, player));
	}
	EXPECT_TRUE(gameState()->currentBattles.empty());
	EXPECT_EQ(recorder.acceptedResults, 1);
	callback->onBattleEnded(battleID);
}

TEST_F(AIIntegrationNativeBattle, fheroes2NativeSurrenderPaymentAndResultAreServerValidated)
{
	// This intentionally tests VCMI's surrender action, NOT a claim that
	// source FHeroes2Tactics selected SURRENDER. Unlike Heroes II, VCMI
	// requires fleeing to be permitted whenever surrender is permitted.
	prepareBattle(CreatureID(19), 1, CreatureID(16), 600);
	ASSERT_FALSE(HasFatalFailure());
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	gameHandler->giveResource(player, GameResID::GOLD, 100000);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanFlee());
	ASSERT_TRUE(view->battleCanSurrender(player));
	const int price = view->battleGetSurrenderCost();
	ASSERT_GE(price, 0);
	const int beforeGold = gameState()->players.at(player).resources[GameResID::GOLD];
	ASSERT_GE(beforeGold, price);
	const auto surrender = BattleAction::makeSurrender(BattleSide::ATTACKER);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, surrender))
		<< "Native VCMI rejected an authorized surrender with sufficient gold";
	ASSERT_EQ(recorder.battleResults.size(), 1u);
	EXPECT_EQ(recorder.battleResults.back().winner, BattleSide::DEFENDER);
	if(recorder.battleResults.back().questionID != QuestionID::NONE)
	{
		ASSERT_TRUE(gameHandler->answerQuestion(recorder.battleResults.back().questionID, 0, player));
	}
	EXPECT_TRUE(gameState()->currentBattles.empty());
	EXPECT_EQ(recorder.acceptedResults, 1);
	EXPECT_EQ(gameState()->players.at(player).resources[GameResID::GOLD], beforeGold - price)
		<< "Native surrender must charge the actual Heroes III server price";
	callback->onBattleEnded(battleID);
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
	// Source meleeUnitDefense does not merely DEFEND when a blocked
	// archer has an enemy beyond this turn's melee reach: it pursues the
	// blocker on a native legal route before generic offensive targets.
	ASSERT_EQ(callback->action->actionType, EActionType::WALK)
		<< "Blocked friendly archer should trigger source-derived defensive pursuit: "
		<< callback->action->toString();
	ASSERT_FALSE(callback->action->target.empty());
	const BattleHex destination = callback->action->target.front().hexValue;
	EXPECT_LT(BattleHex::getDistance(destination, blockerHex),
		BattleHex::getDistance(infantry->getPosition(), blockerHex));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Authoritative VCMI rejected the source-derived defensive step";
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

TEST_F(AIIntegrationNativeBattle, fheroes2ForgetfulnessDoesNotCancelSourceShooterScreen)
{
	// In pinned ai_battle.cpp::meleeUnitDefense, friendly isArchers()
	// stacks participate in screening even if their present shot is not
	// available. Native Heroes III Forgetfulness suppresses shooting, not
	// the archer classification. An unrelated adjacent melee bait must
	// not bypass the earlier source defensive-cover decision.
	prepareBattle(CreatureID(0), 40, CreatureID(0), 15);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * guard = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			guard = unit.get();
	ASSERT_NE(guard, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), guard);
	const BattleHex origin = guard->getPosition();
	const BattleHex shooterHex(origin.toInt() + 3 * GameConstants::BFIELD_WIDTH);
	const BattleHex threatHex(shooterHex.toInt() + 5);
	const BattleHex baitHex(origin.toInt() + 1);
	for(const auto & hex : {shooterHex, threatHex, baitHex})
	{
		ASSERT_TRUE(hex.isAvailable());
		ASSERT_EQ(battle()->battleGetUnitByPos(hex), nullptr);
	}
	const auto * shooter = addStack(BattleSide::ATTACKER, CreatureID(2), shooterHex, 120);
	const auto * threat = addStack(BattleSide::DEFENDER, CreatureID(0), threatHex, 15);
	const auto * bait = addStack(BattleSide::DEFENDER, CreatureID(0), baitHex, 1);
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(threat, nullptr);
	ASSERT_NE(bait, nullptr);
	ASSERT_TRUE(shooter->isShooter());
	ASSERT_TRUE(battle()->battleCanShoot(shooter));
	ASSERT_LE(BattleHex::getDistance(shooterHex, threatHex),
		static_cast<int>(threat->getMovementRange()) + 1);
	ASSERT_GT(BattleHex::getDistance(origin, threatHex),
		static_cast<int>(guard->getMovementRange()) + 1);
	ASSERT_FALSE(battle()->meleeAttackHexes(guard, bait, origin).empty())
		<< "The unrelated adjacent bait must be a genuine native melee option";

	teachBattleSpell(defenderSideHero, SpellID::FORGETFULNESS);
	defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::WATER_MAGIC),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::FORGETFULNESS, shooter))
		<< "Use the native spell handler, not a mocked shooter state";
	ASSERT_TRUE(shooter->isShooter());
	ASSERT_FALSE(battle()->battleCanShoot(shooter));
	ASSERT_GT(shooter->getMovementRange(), 0);
	ASSERT_EQ(battle()->battleActiveUnit(), guard);

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
		<< "Forgetfulness may disable a shot, but must not divert the guard into unrelated melee: "
		<< callback->action->toString();
	ASSERT_FALSE(callback->action->target.empty());
	const BattleHex destination = callback->action->target.front().hexValue;
	EXPECT_EQ(BattleHex::getDistance(destination, shooterHex), 1)
		<< "The independent source-derived decision must actually screen the threatened archer";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The native battle server rejected the selected legal cover movement";
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2UnblockedShooterScreenOutranksUnrelatedMeleeAttack)
{
	// Pinned ihhub/fheroes2 ai_battle.cpp::meleeUnitDefense first screens
	// threatened archers, before its generic best melee attack phase.
	// The immediate melee bait below does not threaten our shooter.
	prepareBattle(CreatureID(0), 40, CreatureID(0), 15);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * guard = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			guard = unit.get();
	ASSERT_NE(guard, nullptr);
	const BattleHex original = guard->getPosition();
	const BattleHex shooterHex(original.toInt() + 3 * GameConstants::BFIELD_WIDTH);
	const BattleHex distantThreatHex(shooterHex.toInt() + 5);
	const BattleHex baitHex(original.toInt() + 1);
	for(const auto hex : {shooterHex, distantThreatHex, baitHex})
	{
		ASSERT_TRUE(hex.isAvailable());
		ASSERT_EQ(battle()->battleGetUnitByPos(hex), nullptr);
	}
	const auto * shooter = addStack(BattleSide::ATTACKER, CreatureID(2), shooterHex, 120);
	const auto * distantThreat = addStack(BattleSide::DEFENDER, CreatureID(0), distantThreatHex, 15);
	const auto * bait = addStack(BattleSide::DEFENDER, CreatureID(0), baitHex, 1);
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(distantThreat, nullptr);
	ASSERT_NE(bait, nullptr);
	ASSERT_TRUE(shooter->isShooter());
	ASSERT_TRUE(battle()->battleCanShoot(shooter)) << "Shooter must be unblocked for the source screening priority";
	ASSERT_FALSE(battle()->meleeAttackHexes(guard, bait, original).empty())
		<< "Unrelated bait must really be immediately attackable";
	ASSERT_LE(BattleHex::getDistance(shooterHex, distantThreatHex),
		static_cast<int>(distantThreat->getMovementRange()) + 1)
		<< "Distant melee stack must threaten the friendly archer next turn";
	ASSERT_GT(BattleHex::getDistance(original, distantThreatHex),
		static_cast<int>(guard->getMovementRange()) + 1);

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
		<< "Source defense screens shooter rather than striking unrelated adjacent bait: "
		<< callback->action->toString();
	ASSERT_FALSE(callback->action->target.empty());
	const BattleHex destination = callback->action->target.front().hexValue;
	EXPECT_EQ(BattleHex::getDistance(destination, shooterHex), 1);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Authoritative VCMI server rejected the chosen defensive screen";
	policy.reset();
	runToCompletion(std::nullopt, false);
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2GuardAttacksAdjacentShooterBlockerBeforeMeleeBait)
{
	// Pinned fheroes2 ai_battle.cpp::meleeUnitDefense (1774, 2012-2039)
	// prioritizes an enemy in contact with a blocked friendly archer over
	// a more valuable, immediately attackable, unrelated enemy.
	prepareBattle(CreatureID(0), 40, CreatureID(0), 15);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * guard = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			guard = unit.get();
	ASSERT_NE(guard, nullptr);
	const BattleHex original = guard->getPosition();
	const BattleHex shooterHex(original.toInt() + 2 * GameConstants::BFIELD_WIDTH);
	const BattleHex blockerHex(shooterHex.toInt() + 1);
	const BattleHex baitHex(original.toInt() + 1);
	for(const auto hex : {shooterHex, blockerHex, baitHex})
	{
		ASSERT_TRUE(hex.isAvailable());
		ASSERT_EQ(battle()->battleGetUnitByPos(hex), nullptr);
	}
	const auto * shooter = addStack(BattleSide::ATTACKER, CreatureID(2), shooterHex, 120);
	const auto * blocker = addStack(BattleSide::DEFENDER, CreatureID(0), blockerHex, 1);
	const auto * bait = addStack(BattleSide::DEFENDER, CreatureID(0), baitHex, 40);
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(blocker, nullptr);
	ASSERT_NE(bait, nullptr);
	ASSERT_TRUE(shooter->isShooter());
	ASSERT_EQ(BattleHex::getDistance(shooterHex, blockerHex), 1);
	ASSERT_FALSE(battle()->battleCanShoot(shooter));
	ASSERT_GT(bait->estimateCombatValue(), blocker->estimateCombatValue());
	ASSERT_EQ(battle()->battleActiveUnit(), guard);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto viewedBattle = callback->getBattle(battleID);
	ASSERT_NE(viewedBattle, nullptr);
	const auto routes = viewedBattle->getReachability(guard);
	const auto available = viewedBattle->battleGetAvailableHexes(routes, guard, false);
	const auto hasNativeMelee = [&](const CStack * victim)
	{
		return std::any_of(available.begin(), available.end(), [&](const BattleHex & from)
		{
			return from.isAvailable()
				&& !viewedBattle->meleeAttackHexes(guard, victim, from).empty();
		});
	};
	ASSERT_TRUE(hasNativeMelee(blocker))
		<< "The blocker must be directly attackable by the native guard";
	ASSERT_TRUE(hasNativeMelee(bait))
		<< "The unrelated bait must also offer a legal melee attack";

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
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK)
		<< "Independent FHeroes2 defense must choose a native blocker attack";
	ASSERT_GE(callback->action->target.size(), 2u);
	EXPECT_EQ(viewedBattle->battleGetUnitByPos(callback->action->target[1].hexValue), blocker)
		<< "The more valuable melee bait must not displace the archer's adjacent blocker";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "VCMI rejected a supposedly legal source-derived guard attack";
	policy.reset();
	runToCompletion(std::nullopt, false);
	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2GuardPursuesRemoteArcherBlockerBeforeMeleeBait)
{
	// fheroes2 ai_battle.cpp::meleeUnitDefense keeps the best *future*
	// attack position on an engaged friendly archer's blocker. Exercise
	// the VCMI projection where the blocker cannot be attacked this turn.
	prepareBattle(CreatureID(0), 40, CreatureID(0), 15);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * guard = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			guard = unit.get();
	ASSERT_NE(guard, nullptr);
	const BattleHex original = guard->getPosition();
	const BattleHex shooterHex(original.toInt() + 6);
	const BattleHex blockerHex(shooterHex.toInt() + 1);
	const BattleHex baitHex(original.toInt() + GameConstants::BFIELD_WIDTH);
	for(const auto hex : {shooterHex, blockerHex, baitHex})
	{
		ASSERT_TRUE(hex.isAvailable());
		ASSERT_EQ(battle()->battleGetUnitByPos(hex), nullptr);
	}
	const auto * shooter = addStack(BattleSide::ATTACKER, CreatureID(2), shooterHex, 120);
	const auto * blocker = addStack(BattleSide::DEFENDER, CreatureID(0), blockerHex, 1);
	const auto * bait = addStack(BattleSide::DEFENDER, CreatureID(0), baitHex, 40);
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(blocker, nullptr);
	ASSERT_NE(bait, nullptr);
	ASSERT_TRUE(shooter->isShooter());
	ASSERT_EQ(BattleHex::getDistance(shooterHex, blockerHex), 1);
	ASSERT_FALSE(battle()->battleCanShoot(shooter));
	ASSERT_EQ(battle()->battleActiveUnit(), guard);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	const auto routes = view->getReachability(guard);
	const auto available = view->battleGetAvailableHexes(routes, guard, false);
	const auto canMeleeNow = [&](const CStack * victim)
	{
		return std::any_of(available.begin(), available.end(), [&](const BattleHex & from)
		{
			return from.isAvailable() && !view->meleeAttackHexes(guard, victim, from).empty();
		});
	};
	ASSERT_FALSE(canMeleeNow(blocker))
		<< "The engaged archer's blocker must require more than one turn";
	ASSERT_TRUE(canMeleeNow(bait))
		<< "The guard must have a genuine alternative immediate melee attack";
	bool futureAttackPath = false;
	for(const auto & from : blocker->getAttackableHexes(guard))
	{
		if(!from.isValid() || !view->isMeleeAttackPossible(guard, blocker, from))
			continue;
		const int travel = static_cast<int>(routes.distances[from.toInt()]);
		futureAttackPath |= travel > static_cast<int>(guard->getMovementRange())
			&& travel <= static_cast<int>(guard->getMovementRange()) * 2;
	}
	ASSERT_TRUE(futureAttackPath)
		<< "Fixture must offer a finite two-turn VCMI path to the blocker";

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
		<< "FHeroes2 should advance on the remote shooter blocker before melee bait: "
		<< callback->action->toString();
	ASSERT_FALSE(callback->action->target.empty());
	const BattleHex destination = callback->action->target.front().hexValue;
	ASSERT_TRUE(available.contains(destination))
		<< "The model's multi-turn first step must be legal this turn";
	EXPECT_LT(BattleHex::getDistance(destination, blockerHex),
		BattleHex::getDistance(original, blockerHex));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Native VCMI server rejected independent multi-turn blocker pursuit";
	policy.reset();
	runToCompletion(std::nullopt, false);
	EXPECT_GT(recorder.damage, 0);
}

// An engaged ally keeps source blocker priority even when its shot is legal.
// Source: ihhub/fheroes2@0dbfe9f::ai_battle.cpp::meleeUnitDefense.
// Heroes III FREE_SHOOTING changes native legality, not source contact priority.
// fheroes2 ai_battle.cpp::analyzeBattleState excludes immovable archers
// from _myRangedUnitsOnly, which meleeUnitDefense uses as its distance
// penalty. Blind does not erase a stack from the source's covering loop.
// Exercise a real native Blind cast against a reserve archer: it must change
// pursuit weights without suppressing the two active blocked archers.
// The native server, rather than a scripted outcome, must accept the move.
TEST_F(AIIntegrationNativeBattle, fheroes2BlindedReserveArcherDoesNotDistortMultiTurnBlockerPriority)
{
	prepareBattle(CreatureID(0), 40, CreatureID(0), 15);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * guard = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			guard = unit.get();
	ASSERT_NE(guard, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), guard);
	const BattleHex origin = guard->getPosition();
	const BattleHex highHex(origin.toInt() + 7);
	const BattleHex highBlockerHex(highHex.toInt() + 1);
	// The preceding native server run measured five route steps here, exactly
	// the guard's five-hex movement range. Place the lower-priority blocker
	// farther away so BOTH choices exercise multi-turn pursuit.
	const BattleHex lowHex(origin.toInt() + 4 * GameConstants::BFIELD_WIDTH + 5);
	const BattleHex lowBlockerHex(lowHex.toInt() + 1);
	const BattleHex reserveHex(origin.toInt() - 2 * GameConstants::BFIELD_WIDTH);
	for(const auto & hex : {highHex, highBlockerHex, lowHex, lowBlockerHex, reserveHex})
	{
		ASSERT_TRUE(hex.isAvailable());
		ASSERT_EQ(battle()->battleGetUnitByPos(hex), nullptr);
	}
	const auto * high = addStack(BattleSide::ATTACKER, CreatureID(2), highHex, 200);
	const auto * highBlocker = addStack(BattleSide::DEFENDER, CreatureID(0), highBlockerHex, 2000);
	const auto * low = addStack(BattleSide::ATTACKER, CreatureID(2), lowHex, 100);
	const auto * lowBlocker = addStack(BattleSide::DEFENDER, CreatureID(0), lowBlockerHex, 1);
	const auto * reserve = addStack(BattleSide::ATTACKER, CreatureID(2), reserveHex, 1500);
	ASSERT_NE(high, nullptr);
	ASSERT_NE(highBlocker, nullptr);
	ASSERT_NE(low, nullptr);
	ASSERT_NE(lowBlocker, nullptr);
	ASSERT_NE(reserve, nullptr);
	ASSERT_EQ(BattleHex::getDistance(highHex, highBlockerHex), 1);
	ASSERT_EQ(BattleHex::getDistance(lowHex, lowBlockerHex), 1);
	ASSERT_EQ(battle()->battleActiveUnit(), guard);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	const auto reachability = view->getReachability(guard);
	const auto available = view->battleGetAvailableHexes(reachability, guard, false);
	const int speed = static_cast<int>(guard->getMovementRange());
	const auto futureStep = [&](const CStack * blocker) -> std::pair<int, BattleHex>
	{
		int distance = GameConstants::BFIELD_SIZE;
		BattleHex goal;
		for(const auto & from : blocker->getAttackableHexes(guard))
		{
			if(!from.isValid() || !view->isMeleeAttackPossible(guard, blocker, from))
				continue;
			const int candidate = static_cast<int>(reachability.distances[from.toInt()]);
			if(candidate < distance || (candidate == distance
				&& (!goal.isValid() || from.toInt() < goal.toInt())))
			{
				distance = candidate;
				goal = from;
			}
		}
		BattleHex step;
		for(int i = 0; goal.isValid() && i < GameConstants::BFIELD_SIZE; ++i)
		{
			if(available.contains(goal) && goal != origin)
			{
				step = goal;
				break;
			}
			const auto previous = reachability.predecessors[goal.toInt()];
			if(!previous.isValid() || previous == goal)
				break;
			goal = previous;
		}
		return {distance, step};
	};
	const auto [highTravel, highStep] = futureStep(highBlocker);
	const auto [lowTravel, lowStep] = futureStep(lowBlocker);
	ASSERT_GT(highTravel, lowTravel) << "Two distinct, finite source-priority distances are required";
	ASSERT_GT(lowTravel, speed) << "Neither blocker may be attacked on this turn";
	ASSERT_LT(highTravel, GameConstants::BFIELD_SIZE);
	ASSERT_TRUE(highStep.isValid());
	ASSERT_TRUE(lowStep.isValid());
	ASSERT_NE(highStep, lowStep);

	const double highValue = high->estimateCombatValue();
	const double lowValue = low->estimateCombatValue();
	const double reserveValue = reserve->estimateCombatValue();
	const double activeTotal = highValue + lowValue;
	const double beforeTotal = activeTotal + reserveValue;
	// The source's defensive stance needs BOTH the power-ratio and the
	// mobile-shooter-fraction gates. An overwhelmingly large reserve made
	// the old fixture offensive, so it exercised generic approach rather
	// than either archer-blocker priority. Keep the blinded shooters in
	// army strength, but exclude them from the mobile-shooter numerator.
	double enemyStrength = 0;
	for(const auto & unit : battle()->stacks)
		if(unit->alive() && unit->unitSide() == BattleSide::DEFENDER)
			enemyStrength += unit->estimateCombatValue();
	ASSERT_GT(enemyStrength, 0);
	ASSERT_LE(guard->estimateCombatValue() + beforeTotal, 10.0 * enemyStrength);
	ASSERT_GT(activeTotal / (guard->estimateCombatValue() + beforeTotal), 0.15);
	ASSERT_LT(highValue - beforeTotal * highTravel / 15.0,
		lowValue - beforeTotal * lowTravel / 15.0)
		<< "Before Blind, a massive mobile reserve must favor the closer screen";
	ASSERT_GT(highValue - activeTotal * highTravel / 15.0,
		lowValue - activeTotal * lowTravel / 15.0)
		<< "After Blind, source distance weighting must favor the stronger archer";

	const auto decide = [&]() -> std::optional<BattleAction>
	{
		auto decision = std::make_shared<DecisionCallback>(player);
		decision->onBattleStarted(battle());
		auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
		if(!policy)
			return std::nullopt;
		policy->playerID = player;
		auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), decision);
		AutocombatPreferences prefs;
		prefs.enableSpellsUsage = false;
		policy->initBattleInterface(environment, decision, prefs);
		policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
			battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
			battle()->getSideHero(BattleSide::ATTACKER),
			battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
		policy->activeStack(battleID, guard);
		return decision->action;
	};
	const auto before = decide();
	ASSERT_TRUE(before.has_value());
	ASSERT_EQ(before->actionType, EActionType::WALK) << before->toString();
	ASSERT_FALSE(before->target.empty());
	EXPECT_EQ(before->target.front().hexValue, lowStep)
		<< "Source defense-distance penalty should initially favor the closer blocked shooter";

	teachBattleSpell(defenderSideHero, SpellID::BLIND, 6);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::BLIND, reserve));
	ASSERT_TRUE(reserve->hasBonusOfType(BonusType::NOT_ACTIVE));
	ASSERT_TRUE(reserve->isShooter());
	ASSERT_EQ(battle()->battleActiveUnit(), guard);
	const auto after = decide();
	ASSERT_TRUE(after.has_value());
	ASSERT_EQ(after->actionType, EActionType::WALK) << after->toString();
	ASSERT_FALSE(after->target.empty());
	EXPECT_EQ(after->target.front().hexValue, highStep)
		<< "Incapacitated reserve must not inflate source _myRangedUnitsOnly";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *after))
		<< "The authoritative VCMI server rejected the independently chosen pursuit move";
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2GuardUnblocksFreeShootingArcherBeforeUnrelatedMelee)
{
	// Pinned fheroes2 ai_battle.cpp::meleeUnitDefense (1774, 2012-2039)
	// prioritizes an enemy in contact with a blocked friendly archer over
	// a more valuable, immediately attackable, unrelated enemy.
	prepareBattle(CreatureID(0), 40, CreatureID(0), 15);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * guard = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			guard = unit.get();
	ASSERT_NE(guard, nullptr);
	const BattleHex original = guard->getPosition();
	const BattleHex shooterHex(original.toInt() + 2 * GameConstants::BFIELD_WIDTH);
	const BattleHex blockerHex(shooterHex.toInt() + 1);
	const BattleHex baitHex(original.toInt() + 1);
	for(const auto hex : {shooterHex, blockerHex, baitHex})
	{
		ASSERT_TRUE(hex.isAvailable());
		ASSERT_EQ(battle()->battleGetUnitByPos(hex), nullptr);
	}
	const auto * shooter = addStack(BattleSide::ATTACKER, CreatureID(2), shooterHex, 120);
	const auto * blocker = addStack(BattleSide::DEFENDER, CreatureID(0), blockerHex, 1);
	const auto * bait = addStack(BattleSide::DEFENDER, CreatureID(0), baitHex, 40);
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(blocker, nullptr);
	ASSERT_NE(bait, nullptr);
	ASSERT_TRUE(shooter->isShooter());
	ASSERT_EQ(BattleHex::getDistance(shooterHex, blockerHex), 1);
	ASSERT_FALSE(battle()->battleCanShoot(shooter))
		<< "Verify adjacent enemy actually blocks the ordinary native shot";
	// Only this native battle fixture receives the Heroes III bonus.
	const_cast<CStack *>(shooter)->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::FREE_SHOOTING,
		BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(battle()->battleIsUnitBlocked(shooter))
		<< "An enemy must still be physically adjacent";
	ASSERT_TRUE(battle()->battleCanShoot(shooter))
		<< "The alternative Heroes III shot must genuinely be legal";
	ASSERT_GT(bait->estimateCombatValue(), blocker->estimateCombatValue());
	ASSERT_EQ(battle()->battleActiveUnit(), guard);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto viewedBattle = callback->getBattle(battleID);
	ASSERT_NE(viewedBattle, nullptr);
	const auto routes = viewedBattle->getReachability(guard);
	const auto available = viewedBattle->battleGetAvailableHexes(routes, guard, false);
	const auto hasNativeMelee = [&](const CStack * victim)
	{
		return std::any_of(available.begin(), available.end(), [&](const BattleHex & from)
		{
			return from.isAvailable()
				&& !viewedBattle->meleeAttackHexes(guard, victim, from).empty();
		});
	};
	ASSERT_TRUE(hasNativeMelee(blocker))
		<< "The blocker must be directly attackable by the native guard";
	ASSERT_TRUE(hasNativeMelee(bait))
		<< "The unrelated bait must also offer a legal melee attack";

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
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK)
		<< "Independent FHeroes2 defense must choose a native blocker attack";
	ASSERT_GE(callback->action->target.size(), 2u);
	EXPECT_EQ(viewedBattle->battleGetUnitByPos(callback->action->target[1].hexValue), blocker)
		<< "The more valuable melee bait must not displace the archer's adjacent blocker";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "VCMI rejected a supposedly legal source-derived guard attack";
	policy.reset();
	runToCompletion(std::nullopt, false);
	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_GT(recorder.damage, 0);
}

// An adjacent blocker still drives a legal multi-turn pursuit under FREE_SHOOTING.
// Source: ihhub/fheroes2@0dbfe9f::ai_battle.cpp::meleeUnitDefense.
// Heroes III FREE_SHOOTING changes native legality, not source contact priority.
TEST_F(AIIntegrationNativeBattle, fheroes2GuardPursuesFreeShootingArcherBlockerOnNativeRoute)
{
	// fheroes2 ai_battle.cpp::meleeUnitDefense keeps the best *future*
	// attack position on an engaged friendly archer's blocker. Exercise
	// the VCMI projection where the blocker cannot be attacked this turn.
	prepareBattle(CreatureID(0), 40, CreatureID(0), 15);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * guard = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			guard = unit.get();
	ASSERT_NE(guard, nullptr);
	const BattleHex original = guard->getPosition();
	const BattleHex shooterHex(original.toInt() + 6);
	const BattleHex blockerHex(shooterHex.toInt() + 1);
	const BattleHex baitHex(original.toInt() + GameConstants::BFIELD_WIDTH);
	for(const auto hex : {shooterHex, blockerHex, baitHex})
	{
		ASSERT_TRUE(hex.isAvailable());
		ASSERT_EQ(battle()->battleGetUnitByPos(hex), nullptr);
	}
	const auto * shooter = addStack(BattleSide::ATTACKER, CreatureID(2), shooterHex, 120);
	const auto * blocker = addStack(BattleSide::DEFENDER, CreatureID(0), blockerHex, 1);
	const auto * bait = addStack(BattleSide::DEFENDER, CreatureID(0), baitHex, 40);
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(blocker, nullptr);
	ASSERT_NE(bait, nullptr);
	ASSERT_TRUE(shooter->isShooter());
	ASSERT_EQ(BattleHex::getDistance(shooterHex, blockerHex), 1);
	ASSERT_FALSE(battle()->battleCanShoot(shooter))
		<< "Verify adjacent enemy actually blocks the ordinary native shot";
	// Only this native battle fixture receives the Heroes III bonus.
	const_cast<CStack *>(shooter)->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::FREE_SHOOTING,
		BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(battle()->battleIsUnitBlocked(shooter))
		<< "An enemy must still be physically adjacent";
	ASSERT_TRUE(battle()->battleCanShoot(shooter))
		<< "The alternative Heroes III shot must genuinely be legal";
	ASSERT_EQ(battle()->battleActiveUnit(), guard);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	const auto routes = view->getReachability(guard);
	const auto available = view->battleGetAvailableHexes(routes, guard, false);
	const auto canMeleeNow = [&](const CStack * victim)
	{
		return std::any_of(available.begin(), available.end(), [&](const BattleHex & from)
		{
			return from.isAvailable() && !view->meleeAttackHexes(guard, victim, from).empty();
		});
	};
	ASSERT_FALSE(canMeleeNow(blocker))
		<< "The engaged archer's blocker must require more than one turn";
	ASSERT_TRUE(canMeleeNow(bait))
		<< "The guard must have a genuine alternative immediate melee attack";
	bool futureAttackPath = false;
	for(const auto & from : blocker->getAttackableHexes(guard))
	{
		if(!from.isValid() || !view->isMeleeAttackPossible(guard, blocker, from))
			continue;
		const int travel = static_cast<int>(routes.distances[from.toInt()]);
		futureAttackPath |= travel > static_cast<int>(guard->getMovementRange())
			&& travel <= static_cast<int>(guard->getMovementRange()) * 2;
	}
	ASSERT_TRUE(futureAttackPath)
		<< "Fixture must offer a finite two-turn VCMI path to the blocker";

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
		<< "FHeroes2 should advance on the remote shooter blocker before melee bait: "
		<< callback->action->toString();
	ASSERT_FALSE(callback->action->target.empty());
	const BattleHex destination = callback->action->target.front().hexValue;
	ASSERT_TRUE(available.contains(destination))
		<< "The model's multi-turn first step must be legal this turn";
	EXPECT_LT(BattleHex::getDistance(destination, blockerHex),
		BattleHex::getDistance(original, blockerHex));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Native VCMI server rejected independent multi-turn blocker pursuit";
	policy.reset();
	runToCompletion(std::nullopt, false);
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2AreaShotThreatSpreadsArcherCover)
{
	// Pinned ihhub/fheroes2 ai_battle.cpp: analyzeBattleState sets
	// _avoidStackingUnits above 10% active enemy AREA_SHOT strength;
	// meleeUnitDefense then screens shooters one cell farther away.
	// Use the native SHOOTS_ALL_ADJACENT bonus and a real VCMI server.
	prepareBattle(CreatureID(0), 40, CreatureID(0), 15);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * guard = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			guard = unit.get();
	ASSERT_NE(guard, nullptr);
	const BattleHex original = guard->getPosition();
	const BattleHex archerHex(original.toInt() + 3 * GameConstants::BFIELD_WIDTH);
	const BattleHex threatHex(archerHex.toInt() + 5);
	ASSERT_TRUE(archerHex.isAvailable());
	ASSERT_TRUE(threatHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(archerHex), nullptr);
	ASSERT_EQ(battle()->battleGetUnitByPos(threatHex), nullptr);
	const auto * archer = addStack(BattleSide::ATTACKER, CreatureID(2), archerHex, 120);
	const auto * threat = addStack(BattleSide::DEFENDER, CreatureID(0), threatHex, 15);
	ASSERT_NE(archer, nullptr);
	ASSERT_NE(threat, nullptr);
	ASSERT_TRUE(archer->isShooter());
	ASSERT_GT(BattleHex::getDistance(original, threatHex),
		static_cast<int>(guard->getMovementRange()) + 1);
	ASSERT_LE(BattleHex::getDistance(archerHex, threatHex),
		static_cast<int>(threat->getMovementRange()) + 1);

	BattleHex splashHex;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex candidate(index);
		if(!candidate.isAvailable() || candidate.getX() < 11
			|| battle()->battleGetUnitByPos(candidate) != nullptr
			|| BattleHex::getDistance(candidate, original) <= static_cast<int>(guard->getMovementRange()) + 1
			|| BattleHex::getDistance(candidate, threatHex) < 3)
			continue;
		splashHex = candidate;
		break;
	}
	ASSERT_TRUE(splashHex.isValid());
	auto * splash = addStack(BattleSide::DEFENDER, CreatureID(45), splashHex, 12);
	ASSERT_NE(splash, nullptr);
	ASSERT_TRUE(splash->isShooter());
	ASSERT_TRUE(battle()->battleCanShoot(splash))
		<< "Enemy must have a real, unobstructed ranged attack before gaining splash";
	ASSERT_GT(static_cast<double>(splash->estimateCombatValue()) /
		(static_cast<double>(splash->estimateCombatValue()) + threat->estimateCombatValue()), 0.1);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	const auto routes = view->getReachability(guard);
	const auto available = view->battleGetAvailableHexes(routes, guard, false);
	const bool hasSeparatedScreen = std::any_of(available.begin(), available.end(),
		[&](const BattleHex & hex)
		{
			return hex.isAvailable() && hex != original
				&& BattleHex::getDistance(hex, archerHex) == 2
				&& routes.distances[hex.toInt()] <= guard->getMovementRange();
		});
	ASSERT_TRUE(hasSeparatedScreen)
		<< "This scenario needs a legal two-hex cover position";

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

	// Hold both armies and all hexes fixed; only the native area-shot
	// ability changes between the two independent policy decisions.
	policy->activeStack(battleID, guard);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK)
		<< "Baseline guard must choose a normal archer screen";
	ASSERT_FALSE(callback->action->target.empty());
	EXPECT_EQ(BattleHex::getDistance(callback->action->target.front().hexValue, archerHex), 1);
	// One DecisionCallback records exactly one command per stack activation.
	// Compare two separately initialized AI decisions on the same unchanged
	// authoritative battle rather than faking a second activation event.
	policy.reset();
	callback.reset();
	splash->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SHOOTS_ALL_ADJACENT, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(splash->hasBonusOfType(BonusType::SHOOTS_ALL_ADJACENT));
	ASSERT_TRUE(battle()->battleCanShoot(splash));

	auto splashCallback = std::make_shared<DecisionCallback>(player);
	splashCallback->onBattleStarted(battle());
	auto splashPolicy = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(splashPolicy, nullptr);
	splashPolicy->playerID = player;
	auto splashEnvironment = std::make_shared<NativePortEnvironment>(
		makeCallback(player), splashCallback);
	splashPolicy->initBattleInterface(splashEnvironment, splashCallback, prefs);
	splashPolicy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	splashPolicy->activeStack(battleID, guard);
	ASSERT_TRUE(splashCallback->action.has_value());
	ASSERT_EQ(splashCallback->action->actionType, EActionType::WALK)
		<< "Source-derived splash warning should change the selected screen";
	ASSERT_FALSE(splashCallback->action->target.empty());
	const BattleHex destination = splashCallback->action->target.front().hexValue;
	EXPECT_EQ(BattleHex::getDistance(destination, archerHex), 2)
		<< "Splash-aware guard should not cluster adjacent to its archer";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *splashCallback->action))
		<< "The native server rejected the independently selected cover movement";
	splashPolicy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

// Pinned ai_battle.cpp::analyzeBattleState counts AREA_SHOT pressure only
// when the area shooter is NOT hand-fighting. Heroes III FREE_SHOOTING can
// still authorize a real native shot during physical contact; that native
// permission must not silently override the Heroes II spacing policy.
// Compare two independent decisions on the same server battle, then submit
// the resulting movement to the authoritative engine and finish the fight.
TEST_F(AIIntegrationNativeBattle, fheroes2EngagedAreaShooterDoesNotSpreadCoverDespiteNativeFreeShooting)
{
	prepareBattle(CreatureID(0), 40, CreatureID(0), 15);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * guard = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			guard = unit.get();
	ASSERT_NE(guard, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), guard);
	const BattleHex original = guard->getPosition();
	const BattleHex archerHex(original.toInt() + 3 * GameConstants::BFIELD_WIDTH);
	const BattleHex threatHex(archerHex.toInt() + 5);
	for(const auto & hex : {archerHex, threatHex})
	{
		ASSERT_TRUE(hex.isAvailable());
		ASSERT_EQ(battle()->battleGetUnitByPos(hex), nullptr);
	}
	const auto * archer = addStack(BattleSide::ATTACKER, CreatureID(2), archerHex, 120);
	const auto * threat = addStack(BattleSide::DEFENDER, CreatureID(0), threatHex, 15);
	ASSERT_NE(archer, nullptr);
	ASSERT_NE(threat, nullptr);
	ASSERT_LE(BattleHex::getDistance(archerHex, threatHex),
		static_cast<int>(threat->getMovementRange()) + 1);
	ASSERT_GT(BattleHex::getDistance(original, threatHex),
		static_cast<int>(guard->getMovementRange()) + 1);

	BattleHex splashHex;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex candidate(index);
		if(!candidate.isAvailable() || candidate.getX() < 11
			|| battle()->battleGetUnitByPos(candidate) != nullptr
			|| BattleHex::getDistance(candidate, original) <= static_cast<int>(guard->getMovementRange()) + 1
			|| BattleHex::getDistance(candidate, threatHex) < 3)
			continue;
		splashHex = candidate;
		break;
	}
	ASSERT_TRUE(splashHex.isValid());
	auto * splash = addStack(BattleSide::DEFENDER, CreatureID(45), splashHex, 12);
	ASSERT_NE(splash, nullptr);
	ASSERT_TRUE(splash->isShooter());
	splash->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SHOOTS_ALL_ADJACENT, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(splash->hasBonusOfType(BonusType::SHOOTS_ALL_ADJACENT));
	ASSERT_FALSE(battle()->battleIsUnitBlocked(splash));
	ASSERT_TRUE(battle()->battleCanShoot(splash));

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	const auto decide = [&]() -> std::optional<BattleAction>
	{
		auto callback = std::make_shared<DecisionCallback>(player);
		callback->onBattleStarted(battle());
		auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
		if(!policy)
			return std::nullopt;
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
		return callback->action;
	};
	const auto before = decide();
	ASSERT_TRUE(before.has_value());
	ASSERT_EQ(before->actionType, EActionType::WALK);
	ASSERT_FALSE(before->target.empty());
	EXPECT_EQ(BattleHex::getDistance(before->target.front().hexValue, archerHex), 2)
		<< "Unengaged area shooter requires a two-hex screen";

	BattleHex engagedHex;
	for(const auto & hex : splash->getSurroundingHexes())
		if(hex.isAvailable() && battle()->battleGetUnitByPos(hex) == nullptr)
		{
			engagedHex = hex;
			break;
		}
	ASSERT_TRUE(engagedHex.isValid());
	ASSERT_NE(addStack(BattleSide::ATTACKER, CreatureID(0), engagedHex, 1), nullptr);
	ASSERT_TRUE(battle()->battleIsUnitBlocked(splash))
		<< "A real hostile stack must engage the area shooter";
	// Keep native shooting legal to expose the original battleCanShoot bug.
	splash->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::FREE_SHOOTING, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(battle()->battleCanShoot(splash))
		<< "Native Heroes III permission must survive actual melee contact";
	ASSERT_EQ(battle()->battleActiveUnit(), guard);
	const auto after = decide();
	ASSERT_TRUE(after.has_value());
	ASSERT_EQ(after->actionType, EActionType::WALK)
		<< "Source engaged-area rule should keep ordinary close cover";
	ASSERT_FALSE(after->target.empty());
	EXPECT_EQ(BattleHex::getDistance(after->target.front().hexValue, archerHex), 1)
		<< "Physical contact, not VCMI shot legality, controls source splash spacing";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *after))
		<< "VCMI rejected the source-ordered native cover step";
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

// Pinned fheroes2 analyzeBattleState counts AREA_SHOT pressure only for
// mobile archers. Blind does not erase the VCMI creature's area-shot bonus,
// but it must remove the extra spacing requirement from shooter coverage.
// Both distinct decisions use the same live battle, and the final move is
// accepted by the authoritative game handler before combat completes.
// The pinned source's meleeUnitDefense should not treat a Blind melee stack
// as a mobile future threat to our archer. In VCMI the stack can retain its
// raw movement range despite NOT_ACTIVE, so compare independent actions on
// identical native positions before and after a real Blind spell.
TEST_F(AIIntegrationNativeBattle, fheroes2BlindedMeleeStopsProjectingShooterCoverThreat)
{
	prepareBattle(CreatureID(0), 40, CreatureID(0), 15);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * guard = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			guard = unit.get();
	ASSERT_NE(guard, nullptr);
	const BattleHex origin = guard->getPosition();
	const BattleHex archerHex(origin.toInt() + 3 * GameConstants::BFIELD_WIDTH);
	const BattleHex threatHex(archerHex.toInt() + 5);
	ASSERT_TRUE(archerHex.isAvailable());
	ASSERT_TRUE(threatHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(archerHex), nullptr);
	ASSERT_EQ(battle()->battleGetUnitByPos(threatHex), nullptr);
	const auto * archer = addStack(BattleSide::ATTACKER, CreatureID(2), archerHex, 120);
	const auto * threat = addStack(BattleSide::DEFENDER, CreatureID(0), threatHex, 15);
	ASSERT_NE(archer, nullptr);
	ASSERT_NE(threat, nullptr);
	ASSERT_TRUE(archer->isShooter());
	ASSERT_GT(BattleHex::getDistance(archerHex, threatHex), 1);
	ASSERT_LE(BattleHex::getDistance(archerHex, threatHex),
		static_cast<int>(threat->getMovementRange()) + 1);
	ASSERT_GT(BattleHex::getDistance(origin, threatHex),
		static_cast<int>(guard->getMovementRange()) + 1);
	ASSERT_EQ(battle()->battleActiveUnit(), guard);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	const auto decide = [&]() -> std::optional<BattleAction>
	{
		auto callback = std::make_shared<DecisionCallback>(player);
		callback->onBattleStarted(battle());
		auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
		if(!policy)
			return std::nullopt;
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
		return callback->action;
	};
	const auto before = decide();
	ASSERT_TRUE(before.has_value());
	ASSERT_EQ(before->actionType, EActionType::WALK);
	ASSERT_FALSE(before->target.empty());
	EXPECT_EQ(BattleHex::getDistance(before->target.front().hexValue, archerHex), 1)
		<< "A mobile enemy approaching the archer demands a close screen";

	teachBattleSpell(attackerSideHero, SpellID::BLIND, 6);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::BLIND, threat))
		<< "An authoritative spell must disable the enemy instead of a mock state flag";
	ASSERT_TRUE(threat->hasBonusOfType(BonusType::NOT_ACTIVE));
	ASSERT_GT(threat->getMovementRange(), 0)
		<< "This regression needs raw VCMI speed to remain nonzero under Blind";
	ASSERT_EQ(battle()->battleActiveUnit(), guard);
	const auto after = decide();
	ASSERT_TRUE(after.has_value());
	EXPECT_EQ(after->actionType, EActionType::NO_ACTION)
		<< "An incapacitated distant enemy must not force a screen or invent DEFEND";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *after))
		<< "The native server rejected the source-derived choice after disabling the threat";
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2BlindedAreaShooterNoLongerSpreadsFriendlyArcherCover)
{
	prepareBattle(CreatureID(0), 40, CreatureID(0), 15);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * guard = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			guard = unit.get();
	ASSERT_NE(guard, nullptr);
	const BattleHex original = guard->getPosition();
	const BattleHex archerHex(original.toInt() + 3 * GameConstants::BFIELD_WIDTH);
	const BattleHex threatHex(archerHex.toInt() + 5);
	ASSERT_TRUE(archerHex.isAvailable());
	ASSERT_TRUE(threatHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(archerHex), nullptr);
	ASSERT_EQ(battle()->battleGetUnitByPos(threatHex), nullptr);
	const auto * archer = addStack(BattleSide::ATTACKER, CreatureID(2), archerHex, 120);
	const auto * threat = addStack(BattleSide::DEFENDER, CreatureID(0), threatHex, 15);
	ASSERT_NE(archer, nullptr);
	ASSERT_NE(threat, nullptr);
	ASSERT_TRUE(archer->isShooter());
	ASSERT_LE(BattleHex::getDistance(archerHex, threatHex),
		static_cast<int>(threat->getMovementRange()) + 1);

	BattleHex splashHex;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex candidate(index);
		if(!candidate.isAvailable() || candidate.getX() < 11
			|| battle()->battleGetUnitByPos(candidate) != nullptr
			|| BattleHex::getDistance(candidate, original) <= static_cast<int>(guard->getMovementRange()) + 1
			|| BattleHex::getDistance(candidate, threatHex) < 3)
			continue;
		splashHex = candidate;
		break;
	}
	ASSERT_TRUE(splashHex.isValid());
	auto * splash = addStack(BattleSide::DEFENDER, CreatureID(45), splashHex, 12);
	ASSERT_NE(splash, nullptr);
	splash->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SHOOTS_ALL_ADJACENT, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(splash->hasBonusOfType(BonusType::SHOOTS_ALL_ADJACENT));
	ASSERT_TRUE(battle()->battleCanShoot(splash));
	ASSERT_GT(static_cast<double>(splash->estimateCombatValue()) /
		(static_cast<double>(splash->estimateCombatValue()) + threat->estimateCombatValue()), 0.1);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	const auto decide = [&]() -> std::optional<BattleAction>
	{
		auto callback = std::make_shared<DecisionCallback>(player);
		callback->onBattleStarted(battle());
		auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
		if(!policy)
			return std::nullopt;
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
		return callback->action;
	};

	ASSERT_EQ(battle()->battleActiveUnit(), guard);
	const auto activeSplash = decide();
	ASSERT_TRUE(activeSplash.has_value());
	ASSERT_EQ(activeSplash->actionType, EActionType::WALK);
	ASSERT_FALSE(activeSplash->target.empty());
	EXPECT_EQ(BattleHex::getDistance(activeSplash->target.front().hexValue, archerHex), 2)
		<< "Active native area shot should keep the melee guard away from its shooter";

	teachBattleSpell(attackerSideHero, SpellID::BLIND, 6);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::BLIND, splash))
		<< "The actual engine must apply Blind to the enemy area shooter";
	ASSERT_TRUE(splash->hasBonusOfType(BonusType::NOT_ACTIVE));
	ASSERT_TRUE(splash->hasBonusOfType(BonusType::SHOOTS_ALL_ADJACENT))
		<< "Incapacitation must not need to delete the creature's area attack ability";
	ASSERT_EQ(battle()->battleActiveUnit(), guard);
	const auto blindedSplash = decide();
	ASSERT_TRUE(blindedSplash.has_value());
	ASSERT_EQ(blindedSplash->actionType, EActionType::WALK);
	ASSERT_FALSE(blindedSplash->target.empty());
	EXPECT_EQ(BattleHex::getDistance(blindedSplash->target.front().hexValue, archerHex), 1)
		<< "A blinded enemy cannot force extra area-shot spacing in source defense";
	EXPECT_NE(blindedSplash->target.front().hexValue, activeSplash->target.front().hexValue)
		<< "Removing the actual splash threat should change independent movement";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *blindedSplash))
		<< "Native VCMI rejected the independent post-Blind guard movement";
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
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
	EXPECT_EQ(callback->action->actionType, EActionType::NO_ACTION)
		<< "A hypnotized enemy must SKIP instead of screening its controller's archers: "
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
	EXPECT_EQ(callback->action->actionType, EActionType::NO_ACTION)
		<< "A phantom tail must not trigger cover or an unearned DEFEND: " << callback->action->toString();
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

// Source-derived cautious offense must stop before projected next-turn
// ground-melee exposure. Check an independent choice, the native server
// acceptance of that movement, and a completed engine battle.
TEST_F(AIIntegrationNativeBattle, fheroes2CautiousMultiTurnApproachAvoidsNextTurnMeleeThreat)
{
	prepareBattle(CreatureID(0), 40, CreatureID(0), 1);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * infantry = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			infantry = unit.get();
	ASSERT_NE(infantry, nullptr);
	const BattleHex origin = infantry->getPosition();
	const int speed = static_cast<int>(infantry->getMovementRange());
	ASSERT_GT(speed, 0);
	const BattleHex guardHex(origin.toInt() + 2 * speed + 1);
	ASSERT_TRUE(guardHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(guardHex), nullptr);
	const auto * guard = addStack(BattleSide::DEFENDER, CreatureID(0), guardHex, 35);
	ASSERT_NE(guard, nullptr);
	ASSERT_GT(BattleHex::getDistance(origin, guardHex), speed + 1);

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
		<< "Independent cautious offense must approach: " << callback->action->toString();
	ASSERT_FALSE(callback->action->target.empty());
	const BattleHex selected = callback->action->target.front().hexValue;
	EXPECT_GT(BattleHex::getDistance(selected, origin), 0);
	EXPECT_LT(BattleHex::getDistance(selected, origin), speed)
		<< "A full-speed step would enter projected enemy melee reach";
	EXPECT_GT(BattleHex::getDistance(selected, guardHex), guard->getMovementRange() + 1)
		<< "Selected cautious step should be beyond next-turn melee reach";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The native server rejected the movement";
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

// Pinned fheroes2 isUnitAbleToApproachPosition uses GetSpeed(false, true):
// a Blind/Paralyze-immobilized enemy cannot threaten future path steps.
// VCMI still reports nonzero raw movement range under native Blind, so
// the source-aware path scorer must not stop early for a fictitious attack.
TEST_F(AIIntegrationNativeBattle, fheroes2BlindedEnemyDoesNotBlockCautiousMultiTurnAdvance)
{
	prepareBattle(CreatureID(0), 40, CreatureID(0), 1);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * infantry = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			infantry = unit.get();
	ASSERT_NE(infantry, nullptr);
	const BattleHex origin = infantry->getPosition();
	const int speed = static_cast<int>(infantry->getMovementRange());
	ASSERT_GT(speed, 0);
	const BattleHex guardHex(origin.toInt() + 2 * speed + 1);
	ASSERT_TRUE(guardHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(guardHex), nullptr);
	const auto * guard = addStack(BattleSide::DEFENDER, CreatureID(0), guardHex, 35);
	ASSERT_NE(guard, nullptr);
	ASSERT_GT(BattleHex::getDistance(origin, guardHex), speed + 1);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	const auto decide = [&]() -> std::optional<BattleAction>
	{
		auto callback = std::make_shared<DecisionCallback>(player);
		callback->onBattleStarted(battle());
		auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
		if(!policy)
			return std::nullopt;
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
		return callback->action;
	};
	const auto before = decide();
	ASSERT_TRUE(before.has_value());
	ASSERT_EQ(before->actionType, EActionType::WALK);
	ASSERT_FALSE(before->target.empty());
	const BattleHex cautiousStep = before->target.front().hexValue;
	ASSERT_LT(BattleHex::getDistance(origin, cautiousStep), speed);

	teachBattleSpell(attackerSideHero, SpellID::BLIND, 6);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::BLIND, guard))
		<< "The authoritative server must Blind the ground melee threat";
	ASSERT_TRUE(guard->hasBonusOfType(BonusType::NOT_ACTIVE));
	ASSERT_GT(guard->getMovementRange(), 0)
		<< "The fixture must expose native raw-speed versus effective-speed divergence";
	const auto after = decide();
	ASSERT_TRUE(after.has_value());
	ASSERT_EQ(after->actionType, EActionType::WALK)
		<< "Independent FHeroes2 must still approach after native Blind";
	ASSERT_FALSE(after->target.empty());
	const BattleHex advancedStep = after->target.front().hexValue;
	EXPECT_GT(BattleHex::getDistance(origin, advancedStep),
		BattleHex::getDistance(origin, cautiousStep))
		<< "The immobilized enemy must no longer reduce safe forward progress";
	EXPECT_LE(BattleHex::getDistance(origin, advancedStep), speed);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *after))
		<< "VCMI rejected the independently selected source-derived WALK";
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


// Pinned fheroes2 meleeUnitOffense / Unit::GetSpeed(false, true):
// Blind and Paralyze remove a fast unit's ability to evade a later melee
// engagement, even when its base speed and FLYING bonus remain unchanged.
// The ordinary high-value target must not conceal that source priority.
// Both actions below are decided by the actual FHeroes2Tactics backend.
TEST_F(AIIntegrationNativeBattle, fheroes2BlindTurnsFastFlyerIntoPriorityDistantTarget)
{
	prepareBattle(CreatureID(0), 1000, CreatureID(0), 1);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * infantry = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			infantry = unit.get();
	ASSERT_NE(infantry, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), infantry);
	const BattleHex origin = infantry->getPosition();
	const BattleHex activeHex(origin.toInt() + 7 - 2 * GameConstants::BFIELD_WIDTH);
	const BattleHex flyerHex(origin.toInt() + 8 + 2 * GameConstants::BFIELD_WIDTH);
	ASSERT_TRUE(activeHex.isAvailable());
	ASSERT_TRUE(flyerHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(activeHex), nullptr);
	ASSERT_EQ(battle()->battleGetUnitByPos(flyerHex), nullptr);
	const CStack * activeEnemy = addStack(BattleSide::DEFENDER, CreatureID(0), activeHex, 400);
	const CStack * flyer = addStack(BattleSide::DEFENDER, CreatureID(4), flyerHex, 1);
	ASSERT_NE(activeEnemy, nullptr);
	ASSERT_NE(flyer, nullptr);
	ASSERT_TRUE(flyer->hasBonusOfType(BonusType::FLYING));
	ASSERT_GT(static_cast<int>(flyer->getMovementRange()),
		static_cast<int>(infantry->getMovementRange()));
	ASSERT_GT(BattleHex::getDistance(origin, activeHex),
		static_cast<int>(infantry->getMovementRange()) + 1);
	ASSERT_GT(BattleHex::getDistance(origin, flyerHex),
		static_cast<int>(infantry->getMovementRange()) + 1);
	ASSERT_GT(activeEnemy->estimateCombatValue(), flyer->estimateCombatValue());

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	const auto decide = [&]() -> std::optional<BattleAction>
	{
		auto callback = std::make_shared<DecisionCallback>(player);
		callback->onBattleStarted(battle());
		auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
		if(!policy)
			return std::nullopt;
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
		return callback->action;
	};
	const auto before = decide();
	ASSERT_TRUE(before.has_value());
	ASSERT_EQ(before->actionType, EActionType::WALK) << before->toString();
	ASSERT_FALSE(before->target.empty());
	const BattleHex beforeStep = before->target.front().hexValue;

	teachBattleSpell(attackerSideHero, SpellID::BLIND, 6);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::BLIND, flyer))
		<< "The authoritative server must apply Blind to the flying target";
	ASSERT_TRUE(flyer->hasBonusOfType(BonusType::NOT_ACTIVE));
	ASSERT_GT(flyer->getMovementRange(), 0)
		<< "Raw VCMI movement speed must remain positive under Blind";
	ASSERT_EQ(battle()->battleActiveUnit(), infantry);
	const auto after = decide();
	ASSERT_TRUE(after.has_value());
	ASSERT_EQ(after->actionType, EActionType::WALK)
		<< "Source-derived pursuit must use a legal native first step: " << after->toString();
	ASSERT_FALSE(after->target.empty());
	const BattleHex afterStep = after->target.front().hexValue;
	EXPECT_NE(afterStep, beforeStep)
		<< "The disabled flyer must displace the active enemy as the priority pursuit";
	EXPECT_LT(BattleHex::getDistance(afterStep, flyerHex),
		BattleHex::getDistance(beforeStep, flyerHex))
		<< "Pursuit must advance toward the now-immobile, otherwise evasive flyer";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *after))
		<< "The real VCMI server rejected source-derived movement";
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

// Pinned fheroes2 ai_battle.cpp::evaluatePotentialAttackPositions first
// takes the best adjacent melee attack at a head position, then ADDS
// the value of each distinct adjacent archer. BestAttackOutcome compares
// that position score before an individual enemy's attack score.
// Heroes III creature values, damage, hexes and action legality remain native.
TEST_F(AIIntegrationNativeBattle, fheroes2MeleePositionAggregatesTwoAdjacentArcherThreats)
{
	prepareBattle(CreatureID(1), 100, CreatureID(0), 25);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * attacker = nullptr;
	CStack * isolated = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(1))
			attacker = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			isolated = unit.get();
	}
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(isolated, nullptr);
	ASSERT_FALSE(attacker->isShooter());
	const BattleHex center(GameConstants::BFIELD_SIZE / 2);
	ASSERT_TRUE(center.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(center), nullptr);
	attacker->setPosition(center);
	BattleHex isolatedHex;
	for(const auto & hex : center.getNeighbouringTiles())
	{
		if(hex.isAvailable() && hex.getX() < center.getX()
			&& battle()->battleGetUnitByPos(hex) == nullptr)
		{
			isolatedHex = hex;
			break;
		}
	}
	ASSERT_TRUE(isolatedHex.isValid());
	isolated->setPosition(isolatedHex);
	ASSERT_FALSE(battle()->meleeAttackHexes(attacker, isolated, center).empty());

	// Find two legal attack targets adjacent to the SAME reachable hex
	// but separated from the individually more valuable melee enemy.
	BattleHex clusterFrom;
	std::vector<BattleHex> clusterHexes;
	const auto initialReachability = battle()->getReachability(attacker);
	const auto initialAvailable = battle()->battleGetAvailableHexes(initialReachability, attacker, false);
	for(const auto & from : initialAvailable)
	{
		if(!from.isAvailable() || from.getX() < center.getX() + 2
			|| BattleHex::getDistance(center, from) < 2)
			continue;
		std::vector<BattleHex> neighbours;
		for(const auto & hex : from.getNeighbouringTiles())
		{
			if(hex.isAvailable() && hex.getX() >= from.getX()
				&& battle()->battleGetUnitByPos(hex) == nullptr
				&& BattleHex::getDistance(hex, isolatedHex) > 3)
				neighbours.push_back(hex);
		}
		if(neighbours.size() >= 2)
		{
			clusterFrom = from;
			clusterHexes.assign(neighbours.begin(), neighbours.begin() + 2);
			break;
		}
	}
	ASSERT_TRUE(clusterFrom.isValid());
	const auto * firstArcher = addStack(BattleSide::DEFENDER, CreatureID(2), clusterHexes[0], 10);
	const auto * secondArcher = addStack(BattleSide::DEFENDER, CreatureID(2), clusterHexes[1], 10);
	ASSERT_NE(firstArcher, nullptr);
	ASSERT_NE(secondArcher, nullptr);
	ASSERT_TRUE(firstArcher->isShooter());
	ASSERT_TRUE(secondArcher->isShooter());
	const auto reachability = battle()->getReachability(attacker);
	ASSERT_TRUE(battle()->battleGetAvailableHexes(reachability, attacker, false).contains(clusterFrom));
	ASSERT_FALSE(battle()->meleeAttackHexes(attacker, firstArcher, clusterFrom).empty());
	ASSERT_FALSE(battle()->meleeAttackHexes(attacker, secondArcher, clusterFrom).empty());
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto viewedBattle = callback->getBattle(battleID);
	ASSERT_NE(viewedBattle, nullptr);
	const auto nativeAttackValue = [&](const CStack * enemy, const BattleHex & from)
	{
		BattleAttackInfo info(attacker, enemy, reachability.distances[from.toInt()], false);
		info.attackerPos = from;
		DamageEstimation retaliation;
		const auto damage = viewedBattle->battleEstimateDamage(info, &retaliation);
		const auto lostValue = [](const CStack * unit, const DamageEstimation & estimate)
		{
			const double health = static_cast<double>(unit->getAvailableHealth());
			if(health <= 0)
				return 0.0;
			const double hitPoints = std::min(health,
				(static_cast<double>(estimate.damage.min) + estimate.damage.max) / 2.0);
			return unit->estimateCombatValue() * hitPoints / health;
		};
		return lostValue(enemy, damage) - lostValue(attacker, retaliation);
	};
	const double isolatedValue = nativeAttackValue(isolated, center);
	const double firstValue = nativeAttackValue(firstArcher, clusterFrom);
	const double secondValue = nativeAttackValue(secondArcher, clusterFrom);
	ASSERT_GT(isolatedValue, std::max(firstValue, secondValue))
		<< "The old independent-target scorer should prefer the isolated enemy";
	ASSERT_GT(firstValue + secondValue, isolatedValue)
		<< "The source position scorer must instead prefer blocking both archers";

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
	ASSERT_EQ(battle()->battleActiveUnit(), attacker);
	policy->activeStack(battleID, attacker);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK)
		<< "Source ranking must select a native melee action";
	ASSERT_GE(callback->action->target.size(), 2u);
	const BattleHex selectedTarget = callback->action->target[1].hexValue;
	const auto * selected = battle()->battleGetUnitByPos(selectedTarget);
	EXPECT_TRUE(selected == firstArcher || selected == secondArcher)
		<< "One attack position threatens both archers and must outrank the isolated enemy";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Real server rejected the independent FHeroes2 melee command";
	EXPECT_GT(recorder.damage, 0);
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2MeleeSplashChoosesActualNativeMultiTargetAttack)
{
	// Pinned ai_battle.cpp::optimalAttackValue scores all opponents hit by
	// all-adjacent melee. The previous port scored only the primary target.
	// Use a fixture-local VCMI Hydra bonus on an ordinary melee unit, so
	// creature definitions and battle rules are not modified.
	prepareBattle(CreatureID(1), 100, CreatureID(0), 28);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * attacker = nullptr;
	CStack * isolated = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(1))
			attacker = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			isolated = unit.get();
	}
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(isolated, nullptr);
	ASSERT_FALSE(attacker->isShooter());
	const BattleHex center(GameConstants::BFIELD_SIZE / 2);
	ASSERT_TRUE(center.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(center), nullptr);
	attacker->setPosition(center);
	BattleHex isolatedHex;
	for(const auto & candidate : center.getNeighbouringTiles())
	{
		if(candidate.isAvailable() && candidate.getX() < center.getX()
			&& battle()->battleGetUnitByPos(candidate) == nullptr)
		{
			isolatedHex = candidate;
			break;
		}
	}
	ASSERT_TRUE(isolatedHex.isValid());
	isolated->setPosition(isolatedHex);
	ASSERT_EQ(battle()->battleGetUnitByPos(isolatedHex), isolated);
	ASSERT_EQ(battle()->battleActiveUnit(), attacker);

	// Find an actual reachable attack-from hex with three free hostile
	// adjacent cells, separated from the strong isolated target. A direct
	// strike at any one of those smaller troops has lower value than the
	// isolated strike, but the native three-victim attack has higher value.
	BattleHex clusterFrom;
	std::vector<BattleHex> clusterHexes;
	const auto reachability = battle()->getReachability(attacker);
	const auto available = battle()->battleGetAvailableHexes(reachability, attacker, false);
	for(const auto & from : available)
	{
		if(!from.isAvailable() || from.getX() < center.getX() + 2
			|| BattleHex::getDistance(center, from) < 2)
			continue;
		std::vector<BattleHex> neighbours;
		for(const auto & hex : from.getNeighbouringTiles())
		{
			if(hex.isAvailable() && hex.getX() >= from.getX()
				&& battle()->battleGetUnitByPos(hex) == nullptr
				&& BattleHex::getDistance(hex, isolatedHex) > 3)
				neighbours.push_back(hex);
		}
		if(neighbours.size() >= 3)
		{
			clusterFrom = from;
			clusterHexes.assign(neighbours.begin(), neighbours.begin() + 3);
			break;
		}
	}
	ASSERT_TRUE(clusterFrom.isValid());
	std::vector<const CStack *> cluster;
	for(const auto & hex : clusterHexes)
	{
		const auto * additional = addStack(BattleSide::DEFENDER, CreatureID(0), hex, 20);
		ASSERT_NE(additional, nullptr);
		cluster.push_back(additional);
	}
	ASSERT_TRUE(battle()->battleGetAvailableHexes(battle()->getReachability(attacker), attacker, false)
		.contains(clusterFrom));
	for(const auto * defender : cluster)
	{
		ASSERT_FALSE(battle()->meleeAttackHexes(attacker, defender, clusterFrom).empty());
	}

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	const auto decide = [&]() -> std::optional<BattleAction>
	{
		auto callback = std::make_shared<DecisionCallback>(player);
		callback->onBattleStarted(battle());
		auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
		if(!policy)
			return std::nullopt;
		policy->playerID = player;
		auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
		AutocombatPreferences prefs;
		prefs.enableSpellsUsage = false;
		policy->initBattleInterface(environment, callback, prefs);
		policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
			battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
			battle()->getSideHero(BattleSide::ATTACKER),
			battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
		policy->activeStack(battleID, attacker);
		return callback->action;
	};

	const auto ordinary = decide();
	ASSERT_TRUE(ordinary.has_value());
	ASSERT_EQ(ordinary->actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(ordinary->target.size(), 2u);
	EXPECT_EQ(battle()->battleGetUnitByPos(ordinary->target[1].hexValue), isolated)
		<< "Single-target melee should prefer the stronger isolated stack";

	attacker->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::ATTACKS_ALL_ADJACENT, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(attacker->hasBonusOfType(BonusType::ATTACKS_ALL_ADJACENT));
	const auto nativeSplash = battle()->getAttackedBattleUnits(
		attacker, cluster.front(), cluster.front()->getPosition(), false, clusterFrom);
	ASSERT_GE(nativeSplash.size(), 3u)
		<< "The actual VCMI melee geometry must include all adjacent victims";
	const auto sourceProjection = decide();
	ASSERT_TRUE(sourceProjection.has_value());
	ASSERT_EQ(sourceProjection->actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(sourceProjection->target.size(), 2u);
	const BattleHex from = sourceProjection->target[0].hexValue;
	const BattleHex aimed = sourceProjection->target[1].hexValue;
	const auto * primary = battle()->battleGetUnitByPos(aimed);
	ASSERT_NE(primary, nullptr);
	EXPECT_NE(primary, isolated)
		<< "Multi-target valuation must change the independently selected target";
	const auto struck = battle()->getAttackedBattleUnits(attacker, primary, aimed, false, from);
	ASSERT_GE(struck.size(), 2u)
		<< "The selected action must truly hit multiple native enemies";

	std::vector<int> actualVictims;
	recorder.onAttackBeforeApply = [&](const BattleAttack & attack)
	{
		for(const auto & injury : attack.bsa)
		{
			if(injury.damageAmount > 0
				&& std::find(actualVictims.begin(), actualVictims.end(), injury.stackAttacked)
					== actualVictims.end())
				actualVictims.push_back(injury.stackAttacked);
		}
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *sourceProjection))
		<< "VCMI rejected the independently selected multi-target melee action";
	int hitCluster = 0;
	for(const auto * defender : cluster)
	{
		if(std::find(actualVictims.begin(), actualVictims.end(), defender->unitId())
			!= actualVictims.end())
			++hitCluster;
	}
	EXPECT_GE(hitCluster, 2)
		<< "The native server must injure multiple clustered enemies";
	recorder.onAttackBeforeApply = {};
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2DefensiveArcherUnblockValuesNativeMultiTargetMelee)
{
	// Pinned ai_battle.cpp::meleeUnitDefense uses BestAttackOutcome when
	// freeing a shooter. Its optimalAttackValue/Vector includes the other
	// creatures struck by a Hydra-style attack. Native VCMI owns the attack
	// geometry: the fixture grants the special ability to one live stack
	// without modifying creature definitions or gameplay rules.
	prepareBattle(CreatureID(1), 100, CreatureID(0), 45);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * guard = nullptr;
	CStack * strongBlocker = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(1))
			guard = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			strongBlocker = unit.get();
	}
	ASSERT_NE(guard, nullptr);
	ASSERT_NE(strongBlocker, nullptr);
	ASSERT_FALSE(guard->isShooter());
	// Attacker's left half is required by pinned analyzeBattleState:
	// otherwise this would exercise offensive melee rather than
	// meleeUnitDefense and conceal the source-policy regression.
	const BattleHex center(GameConstants::BFIELD_SIZE / 2 - 1);
	const BattleHex shooterHex(center.toInt() + 2 * GameConstants::BFIELD_WIDTH);
	const BattleHex strongHex(shooterHex.toInt() + 1);
	const BattleHex weakHex(shooterHex.toInt() - 1);
	const BattleHex secondaryHex(shooterHex.toInt() - 2);
	for(const auto & hex : {center, shooterHex, strongHex, weakHex, secondaryHex})
	{
		ASSERT_TRUE(hex.isAvailable());
		ASSERT_EQ(battle()->battleGetUnitByPos(hex), nullptr)
			<< "Directed unblock regression requires unobstructed native cells";
	}
	guard->setPosition(center);
	strongBlocker->setPosition(strongHex);
	const CStack * archer = addStack(BattleSide::ATTACKER, CreatureID(2), shooterHex, 120);
	const CStack * weakBlocker = addStack(BattleSide::DEFENDER, CreatureID(0), weakHex, 20);
	const CStack * secondary = addStack(BattleSide::DEFENDER, CreatureID(0), secondaryHex, 70);
	ASSERT_NE(archer, nullptr);
	ASSERT_NE(weakBlocker, nullptr);
	ASSERT_NE(secondary, nullptr);
	ASSERT_TRUE(archer->isShooter());
	ASSERT_EQ(BattleHex::getDistance(shooterHex, strongHex), 1);
	ASSERT_EQ(BattleHex::getDistance(shooterHex, weakHex), 1);
	ASSERT_GT(BattleHex::getDistance(shooterHex, secondaryHex), 1)
		<< "Secondary victim must not itself qualify as an archer blocker";
	ASSERT_FALSE(battle()->battleCanShoot(archer));
	ASSERT_EQ(battle()->battleActiveUnit(), guard);
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	const auto decide = [&]() -> std::optional<BattleAction>
	{
		auto callback = std::make_shared<DecisionCallback>(player);
		callback->onBattleStarted(battle());
		auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
		if(!policy)
			return std::nullopt;
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
		return callback->action;
	};

	const auto ordinary = decide();
	ASSERT_TRUE(ordinary.has_value());
	ASSERT_EQ(ordinary->actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(ordinary->target.size(), 2u);
	EXPECT_EQ(battle()->battleGetUnitByPos(ordinary->target[1].hexValue), strongBlocker)
		<< "Without area melee the stronger archer blocker should be attacked";

	guard->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::ATTACKS_ALL_ADJACENT, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(guard->hasBonusOfType(BonusType::ATTACKS_ALL_ADJACENT));
	const auto routes = battle()->getReachability(guard);
	const auto available = battle()->battleGetAvailableHexes(routes, guard, false);
	bool nativeMultiVictimVectorExists = false;
	for(const auto & from : available)
	{
		if(!from.isAvailable() || battle()->meleeAttackHexes(guard, weakBlocker, from).empty())
			continue;
		const auto victims = battle()->getAttackedBattleUnits(
			guard, weakBlocker, weakHex, false, from);
		nativeMultiVictimVectorExists |= std::find(victims.begin(), victims.end(), secondary)
			!= victims.end();
	}
	ASSERT_TRUE(nativeMultiVictimVectorExists)
		<< "The fixture must have a reachable real area attack on the second victim";

	const auto chosen = decide();
	ASSERT_TRUE(chosen.has_value());
	ASSERT_EQ(chosen->actionType, EActionType::WALK_AND_ATTACK)
		<< "Defensive FHeroes2 should still free the blocked shooter by melee";
	ASSERT_GE(chosen->target.size(), 2u);
	const BattleHex from = chosen->target[0].hexValue;
	const BattleHex aimed = chosen->target[1].hexValue;
	EXPECT_EQ(battle()->battleGetUnitByPos(aimed), weakBlocker)
		<< "Including native secondary damage must change the independent blocker choice";
	const auto selectedVictims = battle()->getAttackedBattleUnits(
		guard, weakBlocker, aimed, false, from);
	EXPECT_NE(std::find(selectedVictims.begin(), selectedVictims.end(), secondary),
		selectedVictims.end()) << "The selected attack must include a real second victim";
	std::vector<int> injured;
	recorder.onAttackBeforeApply = [&](const BattleAttack & attack)
	{
		for(const auto & hit : attack.bsa)
			if(hit.damageAmount > 0 && std::find(injured.begin(), injured.end(),
				hit.stackAttacked) == injured.end())
				injured.push_back(hit.stackAttacked);
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *chosen))
		<< "Authoritative VCMI rejected the source-derived unblock vector";
	EXPECT_NE(std::find(injured.begin(), injured.end(), weakBlocker->unitId()), injured.end());
	EXPECT_NE(std::find(injured.begin(), injured.end(), secondary->unitId()), injured.end())
		<< "A native melee splash must injure the non-blocking secondary target";
	recorder.onAttackBeforeApply = {};
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
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
	// The fheroes2 source uses an ordered set of occupied unit heads to
	// evaluate AREA_SHOT. The native shot must likewise apply one injury
	// to each wide or single-cell victim, not one per occupied hex.
	std::map<int, int> firstShotInjuries;
	recorder.onAttackBeforeApply = [&](const BattleAttack & attack)
	{
		for(const auto & injury : attack.bsa)
			if(injury.damageAmount > 0)
				++firstShotInjuries[injury.stackAttacked];
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The real engine rejected the source-derived wide-target area shot";
	EXPECT_EQ(firstShotInjuries[wideEnemy->unitId()], 1)
		<< "A wide stack occupies two hexes but is only one shot victim";
	EXPECT_EQ(firstShotInjuries[firstExtra->unitId()], 1);
	EXPECT_EQ(firstShotInjuries[secondExtra->unitId()], 1);
	recorder.onAttackBeforeApply = {};
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

TEST_F(AIIntegrationNativeBattle, fheroes2BlindedFastMeleeNoLongerBlocksArcherRetreat)
{
	// Pinned fheroes2 battle_troop.cpp Unit::GetSpeed(false, true) returns
	// STANDING for Blind/Paralyze. Native VCMI Blind supplies NOT_ACTIVE,
	// but its raw creature movement range may still be high. A blocked
	// Grand Elf must escape an adjacent slow melee enemy even when a
	// distant, much faster melee stack is currently Blind and cannot chase.
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(19), 30); // Grand Elf
	setArmy(defenderSideHero, CreatureID(16), 15); // Melee pursuer
	teachBattleSpell(attackerSideHero, SpellID::BLIND);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::FIRE_MAGIC),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	battle()->obstacles.clear();

	const CStack * shooter = nullptr;
	const CStack * blindedMelee = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(19))
			shooter = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(16))
			blindedMelee = unit.get();
	}
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(blindedMelee, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);
	ASSERT_FALSE(blindedMelee->isShooter());
	ASSERT_GT(BattleHex::getDistance(shooter->getPosition(), blindedMelee->getPosition()), 1);
	const BattleHex blockerHex(shooter->getPosition().toInt() + 1);
	ASSERT_TRUE(blockerHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(blockerHex), nullptr);
	const CStack * blocker = addStack(BattleSide::DEFENDER, CreatureID(16), blockerHex, 10);
	ASSERT_NE(blocker, nullptr);
	ASSERT_FALSE(battle()->battleCanShoot(shooter));
	ASSERT_GT(static_cast<int>(shooter->getMovementRange()),
		static_cast<int>(blocker->getMovementRange()) + 2);

	ASSERT_TRUE(castOn(attackerSideHero, SpellID::BLIND, blindedMelee));
	ASSERT_TRUE(blindedMelee->hasBonusOfType(BonusType::NOT_ACTIVE))
		<< "Actual VCMI Blind must incapacitate the distant melee pursuer";
	ASSERT_FALSE(blindedMelee->isShooter());
	// Deliberately make the Blind melee enemy faster on the raw VCMI
	// movement field. This is a fixture modifier, not a changed game rule.
	const_cast<CStack *>(blindedMelee)->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 25, BonusSourceID()));
	ASSERT_GT(static_cast<int>(blindedMelee->getMovementRange()),
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
		<< "A remotely blinded melee unit cannot veto the fast archer's escape: "
		<< callback->action->toString();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Real VCMI engine rejected retreat with a blinded distant melee enemy";
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

// The pinned type_AI_combat_parameters::simulateAttack executes a second
// creatureTwoAttacks melee strike only when both stacks survive the first
// hit and the intervening retaliation. The VCMI projection must be used for
// choosing a legal attack; the server itself proves the second strike exists.
// This is not a claim of damage or decision parity with retail Heroes III.
TEST_F(AIIntegrationNativeBattle, homm3CrusaderSecondStrikeSurvivesRetaliationAndExecutes)
{
	prepareBattle(CreatureID(7), 6, CreatureID(6), 6); // Crusaders vs Swordsmen
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();

	const CStack * crusaders = nullptr;
	CStack * swordsmen = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(7))
			crusaders = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(6))
			swordsmen = unit.get();
	}
	ASSERT_NE(crusaders, nullptr);
	ASSERT_NE(swordsmen, nullptr);
	ASSERT_GT(crusaders->getTotalAttacks(false), 1)
		<< "The regression requires a real native two-strike creature";
	ASSERT_EQ(battle()->battleActiveUnit(), crusaders);

	const BattleHex nearby(crusaders->getPosition().toInt() + 5);
	ASSERT_TRUE(nearby.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(nearby), nullptr);
	swordsmen->setPosition(nearby);
	const int64_t before = swordsmen->getAvailableHealth();

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	BattleAttackInfo first(crusaders, swordsmen, 0, false);
	const auto firstEstimate = view->battleEstimateDamage(first);
	ASSERT_LT(firstEstimate.damage.max, before)
		<< "The first predicted hit must leave a defender for the second strike";

	// The pinned source computes first hit -> surviving retaliation -> second
	// hit. A full-strength native counter-preview must not stand in for a
	// retaliation after the first Crusader blow has killed Swordsmen.
	BattleAttackInfo fullCounter(swordsmen, crusaders, 0, false);
	fullCounter.attackerPos = swordsmen->getPosition();
	fullCounter.defenderPos = crusaders->getPosition();
	const auto fullCounterEstimate = view->battleEstimateDamage(fullCounter);
	ASSERT_GT(fullCounterEstimate.damage.max, 0);
	const auto oldDefenderCount = swordsmen->getCount();
	auto reducedDefender = swordsmen->acquireState();
	int64_t firstHit = firstEstimate.damage.min
		+ (firstEstimate.damage.max - firstEstimate.damage.min) / 2;
	firstHit = std::clamp<int64_t>(firstHit, 0, reducedDefender->getAvailableHealth());
	reducedDefender->damage(firstHit);
	ASSERT_TRUE(reducedDefender->alive());
	ASSERT_LT(reducedDefender->getCount(), oldDefenderCount);
	BattleAttackInfo reducedCounter(reducedDefender.get(), crusaders, 0, false);
	reducedCounter.attackerPos = swordsmen->getPosition();
	reducedCounter.defenderPos = crusaders->getPosition();
	const auto reducedCounterEstimate = view->battleEstimateDamage(reducedCounter);
	EXPECT_LT(reducedCounterEstimate.damage.max, fullCounterEstimate.damage.max)
		<< "Only post-hit surviving Swordsmen may retaliate before strike two";
	EXPECT_EQ(swordsmen->getAvailableHealth(), before)
		<< "Sequential preview must not mutate the authoritative defender";

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
	policy->activeStack(battleID, crusaders);
	ASSERT_TRUE(callback->action.has_value());
	const auto action = *callback->action;
	ASSERT_EQ(action.actionType, EActionType::WALK_AND_ATTACK)
		<< "The source-derived sequential projection must select legal native melee";
	ASSERT_GE(action.target.size(), 2u);
	EXPECT_EQ(view->battleGetUnitByPos(action.target[1].hexValue), swordsmen);

	int crusaderStrikes = 0;
	int swordsmenRetaliations = 0;
	recorder.onAttackBeforeApply = [&](const BattleAttack & attack)
	{
		if(attack.stackAttacking == static_cast<ui32>(crusaders->unitId())
			&& !attack.shot() && !attack.counter())
			++crusaderStrikes;
		if(attack.stackAttacking == static_cast<ui32>(swordsmen->unitId())
			&& !attack.shot() && attack.counter())
			++swordsmenRetaliations;
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action))
		<< "The real VCMI server rejected the Crusaders' double strike";
	recorder.onAttackBeforeApply = {};
	EXPECT_EQ(crusaderStrikes, 2)
		<< "The engine must execute both Crusader hits after the first retaliation";
	EXPECT_EQ(swordsmenRetaliations, 1)
		<< "The server must apply one intervening Swordsmen retaliation";
	EXPECT_LT(swordsmen->getAvailableHealth(), before);
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_GT(recorder.damage, 0);
}

// HoMM3's pinned simulateAttack has one optional second melee strike,
// whereas VCMI performAttackSequence also honors ADDITIONAL_ATTACK and hero
// grants. A third native strike must be priced as a surviving follow-up,
// without charging another ordinary retaliation or mutating live stacks.
// This tests actual server events, not equivalence with retail Heroes III.
TEST_F(AIIntegrationNativeBattle, homm3NativeThirdMeleeStrikeExecutesWithoutExtraRetaliation)
{
	prepareBattle(CreatureID(7), 8, CreatureID(6), 20); // Crusaders vs Swordsmen
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * crusaders = nullptr;
	CStack * swordsmen = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER)
			crusaders = unit.get();
		else
			swordsmen = unit.get();
	}
	ASSERT_NE(crusaders, nullptr);
	ASSERT_NE(swordsmen, nullptr);
	ASSERT_EQ(crusaders->getTotalAttacks(false), 2);
	crusaders->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::ADDITIONAL_ATTACK,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(7))));
	ASSERT_EQ(crusaders->getTotalAttacks(false), 3);
	battle()->activeStack = crusaders->unitId();
	const BattleHex nearby(crusaders->getPosition().toInt() + 5);
	ASSERT_TRUE(nearby.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(nearby), nullptr);
	swordsmen->setPosition(nearby);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	const int64_t initialAttackerHealth = crusaders->getAvailableHealth();
	const int64_t initialDefenderHealth = swordsmen->getAvailableHealth();
	BattleAttackInfo first(crusaders, swordsmen, 0, false);
	const auto opening = view->battleEstimateDamage(first);
	ASSERT_GT(opening.damage.min, 0);
	ASSERT_LT(opening.damage.max * 3, initialDefenderHealth)
		<< "All three host strikes must have a surviving defender";

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
	policy->activeStack(battleID, crusaders);
	ASSERT_TRUE(callback->action.has_value());
	const auto action = *callback->action;
	ASSERT_EQ(action.actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(action.target.size(), 2u);
	EXPECT_EQ(view->battleGetUnitByPos(action.target[1].hexValue), swordsmen);
	EXPECT_EQ(crusaders->getAvailableHealth(), initialAttackerHealth);
	EXPECT_EQ(swordsmen->getAvailableHealth(), initialDefenderHealth);

	const auto attackerID = static_cast<ui32>(crusaders->unitId());
	const auto defenderID = static_cast<ui32>(swordsmen->unitId());
	int strikes = 0;
	int counters = 0;
	recorder.onAttackBeforeApply = [&](const BattleAttack & packet)
	{
		if(packet.stackAttacking == attackerID && !packet.shot() && !packet.counter())
			++strikes;
		if(packet.stackAttacking == defenderID && !packet.shot() && packet.counter())
			++counters;
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action))
		<< "Server rejected HoMM3's selected three-strike melee action";
	recorder.onAttackBeforeApply = {};
	EXPECT_EQ(strikes, 3) << "VCMI must execute all three legal melee strikes";
	EXPECT_EQ(counters, 1) << "Defender has one retaliation, not one per strike";
	EXPECT_LT(swordsmen->getAvailableHealth(), initialDefenderHealth);
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// Pinned retail simulateAttack prices only one ranged strike. VCMI's actual
// makeRangedAttack supports additional shots and spends ammunition on each.
// Confirm that the HoMM3-only projection chooses a legal real engine shot,
// while the authoritative server independently proves the second volley.
// This is native host-engine behavior, not source-identical Heroes III AI.
TEST_F(AIIntegrationNativeBattle, homm3MarksmenExecuteTwoNativeShotsAfterIndependentDecision)
{
	prepareBattle(CreatureID(3), 12, CreatureID(6), 20); // Marksmen vs Swordsmen
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * marksmen = nullptr;
	CStack * swordsmen = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER)
			marksmen = unit.get();
		else
			swordsmen = unit.get();
	}
	ASSERT_NE(marksmen, nullptr);
	ASSERT_NE(swordsmen, nullptr);
	ASSERT_EQ(marksmen->getTotalAttacks(true), 2);
	ASSERT_GE(marksmen->shots.available(), 2);
	battle()->activeStack = marksmen->unitId();

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanShoot(marksmen, swordsmen->getPosition()));
	const int64_t beforeMarksmen = marksmen->getAvailableHealth();
	const int64_t beforeSwordsmen = swordsmen->getAvailableHealth();
	BattleAttackInfo first(marksmen, swordsmen, 0, true);
	const auto estimate = view->battleEstimateDamage(first);
	ASSERT_GT(estimate.damage.min, 0);
	ASSERT_LT(estimate.damage.max * 2, beforeSwordsmen)
		<< "Both modelled ranged hits must have a live target";

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
	policy->activeStack(battleID, marksmen);
	ASSERT_TRUE(callback->action.has_value());
	const auto action = *callback->action;
	ASSERT_EQ(action.actionType, EActionType::SHOOT)
		<< "HoMM3OriginalTactics must independently select the ranged attack";
	ASSERT_FALSE(action.target.empty());
	EXPECT_EQ(action.target[0].hexValue, swordsmen->getPosition());
	EXPECT_EQ(marksmen->getAvailableHealth(), beforeMarksmen);
	EXPECT_EQ(swordsmen->getAvailableHealth(), beforeSwordsmen);

	int shotPackets = 0;
	const auto marksmenID = static_cast<ui32>(marksmen->unitId());
	recorder.onAttackBeforeApply = [&](const BattleAttack & packet)
	{
		if(packet.stackAttacking == marksmenID && packet.shot() && !packet.counter())
			++shotPackets;
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action))
		<< "The VCMI server rejected HoMM3's selected double-shot attack";
	recorder.onAttackBeforeApply = {};
	EXPECT_EQ(shotPackets, 2) << "The actual server must fire both Marksman volleys";
	EXPECT_LT(swordsmen->getAvailableHealth(), beforeSwordsmen);
	EXPECT_EQ(marksmen->getAvailableHealth(), beforeMarksmen);
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.shots, 1);
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// Retail HoMM3 simulateAttack models one shot; the native VCMI server honors
// HERO_GRANTS_ATTACKS from the commanding hero. The HoMM3-only exchange
// projection must include that second shot without rewriting the pinned
// source formula or inventing a model decision. Observe both actual packets.
// This tests host-engine adaptation, NOT equivalence with retail HoMM3.
TEST_F(AIIntegrationNativeBattle, homm3HeroGrantedRangedVolleyExecutesOnNativeServer)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(2), 12); // Archers: naturally one shot.
	setArmy(defenderSideHero, CreatureID(6), 40); // Swordsmen: survive both volleys.
	ASSERT_FALSE(HasFatalFailure());
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::HERO_GRANTS_ATTACKS,
		BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(CreatureID(2))));
	ASSERT_EQ(attackerSideHero->valOfBonuses(BonusType::HERO_GRANTS_ATTACKS,
		BonusSubtypeID(CreatureID(2))), 1);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	battle()->obstacles.clear();

	CStack * archer = nullptr;
	CStack * swordsmen = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(2))
			archer = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(6))
			swordsmen = unit.get();
	}
	ASSERT_NE(archer, nullptr);
	ASSERT_NE(swordsmen, nullptr);
	ASSERT_EQ(archer->getTotalAttacks(true), 1);
	ASSERT_GE(archer->shots.available(), 2);
	battle()->activeStack = archer->unitId();

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanShoot(archer, swordsmen->getPosition()));
	BattleAttackInfo opening(archer, swordsmen, 0, true);
	const auto forecast = view->battleEstimateDamage(opening);
	const int64_t initialArcherHealth = archer->getAvailableHealth();
	const int64_t initialDefenderHealth = swordsmen->getAvailableHealth();
	ASSERT_GT(forecast.damage.min, 0);
	ASSERT_LT(forecast.damage.max * 2, initialDefenderHealth)
		<< "Two hero-granted shots must leave a target for the native sequence";

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
	policy->activeStack(battleID, archer);
	ASSERT_TRUE(callback->action.has_value());
	const auto action = *callback->action;
	ASSERT_EQ(action.actionType, EActionType::SHOOT)
		<< "The HoMM3 partial policy must independently select a native shot";
	ASSERT_FALSE(action.target.empty());
	EXPECT_EQ(action.target.front().unitValue, swordsmen->unitId());
	EXPECT_EQ(archer->getAvailableHealth(), initialArcherHealth);
	EXPECT_EQ(swordsmen->getAvailableHealth(), initialDefenderHealth);

	int attackerShots = 0;
	const auto archerID = static_cast<ui32>(archer->unitId());
	recorder.onAttackBeforeApply = [&](const BattleAttack & packet)
	{
		if(packet.stackAttacking == archerID && packet.shot() && !packet.counter())
			++attackerShots;
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action))
		<< "VCMI rejected the hero-granted HoMM3 ranged decision";
	recorder.onAttackBeforeApply = {};
	EXPECT_EQ(attackerShots, 2)
		<< "The real server must grant the second shot from the hero bonus";
	EXPECT_LT(swordsmen->getAvailableHealth(), initialDefenderHealth);
	EXPECT_EQ(archer->getAvailableHealth(), initialArcherHealth);
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// The pinned simulateAttack subtracts casualties before retaliation,
// even for a single-blow melee attacker. Native VCMI battleEstimateDamage
// already provides a post-hit damage interval in that case. Confirm this
// contract and an actual legal engine exchange without replacing the
// interval by a biased midpoint or claiming retail HoMM3 numeric parity.
TEST_F(AIIntegrationNativeBattle, homm3SingleStrikeUsesSurvivingDefenderRetaliationAndExecutes)
{
	prepareBattle(CreatureID(6), 8, CreatureID(6), 8); // Swordsmen vs Swordsmen
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * attacker = nullptr;
	CStack * defender = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER)
			attacker = unit.get();
		else
			defender = unit.get();
	}
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(defender, nullptr);
	ASSERT_EQ(attacker->getTotalAttacks(false), 1)
		<< "This test must cover the previously unpriced single-strike case";
	battle()->activeStack = attacker->unitId();
	const BattleHex nearby(attacker->getPosition().toInt() + 5);
	ASSERT_TRUE(nearby.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(nearby), nullptr);
	defender->setPosition(nearby);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	BattleAttackInfo opening(attacker, defender, 0, false);
	DamageEstimation originalRetaliation;
	const auto openingEstimate = view->battleEstimateDamage(opening, &originalRetaliation);
	ASSERT_GT(openingEstimate.damage.min, 0);
	ASSERT_LT(openingEstimate.damage.max, defender->getAvailableHealth());
	ASSERT_GT(originalRetaliation.damage.max, 0);
	const auto fullHealth = defender->getAvailableHealth();
	const auto fullCount = defender->getCount();
	auto survivingDefender = defender->acquireState();
	int64_t firstHit = openingEstimate.damage.min
		+ (openingEstimate.damage.max - openingEstimate.damage.min) / 2;
	firstHit = std::clamp<int64_t>(firstHit, 0, survivingDefender->getAvailableHealth());
	survivingDefender->damage(firstHit);
	ASSERT_TRUE(survivingDefender->alive());
	ASSERT_LT(survivingDefender->getCount(), fullCount);
	BattleAttackInfo reducedCounter(survivingDefender.get(), attacker, 0, false);
	reducedCounter.attackerPos = defender->getPosition();
	reducedCounter.defenderPos = attacker->getPosition();
	const auto reducedEstimate = view->battleEstimateDamage(reducedCounter);
	// CBattleInfoCallback::battleEstimateDamage already simulates the
	// defender AFTER the hit at both damage-range endpoints. Its envelope
	// should contain the representative midpoint; it is not the naive
	// full-strength counter. A dedicated call with an untouched defender
	// is the correct baseline for testing the actual loss of retaliators.
	EXPECT_LE(originalRetaliation.damage.min, reducedEstimate.damage.min);
	EXPECT_GE(originalRetaliation.damage.max, reducedEstimate.damage.max);
	BattleAttackInfo untouchedCounter(defender, attacker, 0, false);
	untouchedCounter.attackerPos = defender->getPosition();
	untouchedCounter.defenderPos = attacker->getPosition();
	const auto fullCounter = view->battleEstimateDamage(untouchedCounter);
	EXPECT_LT(reducedEstimate.damage.max, fullCounter.damage.max)
		<< "The first blow must leave fewer Swordsmen to retaliate";
	EXPECT_EQ(defender->getAvailableHealth(), fullHealth);

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
	policy->activeStack(battleID, attacker);
	ASSERT_TRUE(callback->action.has_value());
	const auto action = *callback->action;
	ASSERT_EQ(action.actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(action.target.size(), 2u);
	EXPECT_EQ(view->battleGetUnitByPos(action.target[1].hexValue), defender);
	EXPECT_EQ(defender->getAvailableHealth(), fullHealth);

	int firstBlows = 0;
	int responses = 0;
	const auto attackerID = static_cast<ui32>(attacker->unitId());
	const auto defenderID = static_cast<ui32>(defender->unitId());
	recorder.onAttackBeforeApply = [&](const BattleAttack & packet)
	{
		if(packet.stackAttacking == attackerID && !packet.shot() && !packet.counter())
			++firstBlows;
		if(packet.stackAttacking == defenderID && !packet.shot() && packet.counter())
			++responses;
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action))
		<< "Native VCMI server rejected HoMM3's single-strike melee command";
	recorder.onAttackBeforeApply = {};
	EXPECT_EQ(firstBlows, 1);
	EXPECT_EQ(responses, 1);
	EXPECT_LT(defender->getAvailableHealth(), fullHealth);
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// Pinned simulateAttack stops as soon as the first melee blow kills the
// defender. The HoMM3-only value projection cannot charge a counterattack
// that a guaranteed lethal first hit makes impossible. Observe the actual
// VCMI packet sequence rather than assuming the damage preview resolves it.
TEST_F(AIIntegrationNativeBattle, homm3GuaranteedLethalFirstHitHasNoPhantomCounterattack)
{
	prepareBattle(CreatureID(7), 20, CreatureID(0), 1); // Crusaders vs a Peasant
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * crusaders = nullptr;
	CStack * peasant = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(7))
			crusaders = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			peasant = unit.get();
	}
	ASSERT_NE(crusaders, nullptr);
	ASSERT_NE(peasant, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), crusaders);
	const BattleHex adjacent(crusaders->getPosition().toInt() + 5);
	ASSERT_TRUE(adjacent.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(adjacent), nullptr);
	peasant->setPosition(adjacent);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	BattleAttackInfo first(crusaders, peasant, 0, false);
	DamageEstimation hypotheticalRetaliation;
	const auto firstEstimate = view->battleEstimateDamage(first, &hypotheticalRetaliation);
	ASSERT_GE(firstEstimate.damage.min, peasant->getAvailableHealth())
		<< "The first native hit must be certain to kill for this regression";

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
	policy->activeStack(battleID, crusaders);
	ASSERT_TRUE(callback->action.has_value());
	const auto action = *callback->action;
	ASSERT_EQ(action.actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(action.target.size(), 2u);
	EXPECT_EQ(view->battleGetUnitByPos(action.target[1].hexValue), peasant);

	const auto crusaderID = static_cast<ui32>(crusaders->unitId());
	const auto peasantID = static_cast<ui32>(peasant->unitId());
	int firstStrikes = 0;
	int counterattacks = 0;
	recorder.onAttackBeforeApply = [&](const BattleAttack & attack)
	{
		if(attack.stackAttacking == crusaderID && !attack.shot() && !attack.counter())
			++firstStrikes;
		if(attack.stackAttacking == peasantID && attack.counter())
			++counterattacks;
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action))
		<< "VCMI server rejected HoMM3's legal guaranteed-lethal melee";
	recorder.onAttackBeforeApply = {};
	EXPECT_EQ(firstStrikes, 1) << "A second strike cannot hit an already dead defender";
	EXPECT_EQ(counterattacks, 0) << "A defender killed by the opening hit cannot retaliate";
	EXPECT_EQ(recorder.battleResults.size(), 1u)
		<< "The real game handler must terminate this one-on-one combat";
	EXPECT_GT(recorder.damage, 0);
}

// Pinned simulateAttack does not project a second ranged shot. The partial
// VCMI landing map now prices an enemy Marksman's two actual volleys when
// choosing a separate nearby melee target. Exercise that choice through the
// native server, without asserting decision parity with the retail HoMM3 AI.
TEST_F(AIIntegrationNativeBattle, homm3EnemyMarksmenDoubleVolleyThreatExecutesNativeBattle)
{
	prepareBattle(CreatureID(7), 15, CreatureID(3), 50); // Crusaders vs Marksmen
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * crusader = nullptr;
	const CStack * marksmen = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(7))
			crusader = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(3))
			marksmen = unit.get();
	}
	ASSERT_NE(crusader, nullptr);
	ASSERT_NE(marksmen, nullptr);
	ASSERT_EQ(marksmen->getTotalAttacks(true), 2);
	ASSERT_GE(marksmen->shots.available(), 2);
	ASSERT_EQ(battle()->battleActiveUnit(), crusader);
	BattleHex adjacent;
	for(const auto & hex : crusader->getSurroundingHexes())
	{
		if(hex.isAvailable() && !battle()->battleGetUnitByPos(hex))
		{
			adjacent = hex;
			break;
		}
	}
	ASSERT_TRUE(adjacent.isValid());
	const CStack * peasant = addStack(BattleSide::DEFENDER, CreatureID(0), adjacent, 100);
	ASSERT_NE(peasant, nullptr);
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanShoot(marksmen, crusader->getPosition()));
	const auto crusaderHealth = crusader->getAvailableHealth();
	const auto marksmenHealth = marksmen->getAvailableHealth();
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
	policy->activeStack(battleID, crusader);
	ASSERT_TRUE(callback->action.has_value());
	const auto action = *callback->action;
	ASSERT_EQ(action.actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(action.target.size(), 2u);
	EXPECT_EQ(view->battleGetUnitByPos(action.target[1].hexValue), peasant);
	EXPECT_EQ(crusader->getAvailableHealth(), crusaderHealth);
	EXPECT_EQ(marksmen->getAvailableHealth(), marksmenHealth)
		<< "Ranged threat projections must not mutate either live stack";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action))
		<< "Native VCMI server rejected the HoMM3 melee decision";
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_GT(recorder.shots, 0);
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// Pinned ai_tactical.cpp::findEnemyAttacks excludes enemies which cannot
// attack. A Ballista can shoot, but it is not a melee threat just because
// its occupied hex is adjacent to a prospective attack landing cell.
// Prove the negative engine predicate and execute an independently chosen
// HoMM3OriginalTactics command through the real server. This does NOT prove
// a different attack hex from the prior approximation or retail parity.
TEST_F(AIIntegrationNativeBattle, homm3RangedOnlyBallistaCannotProjectMeleeLandingThreat)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(7), 8); // Crusaders
	setArmy(defenderSideHero, CreatureID(0), 25); // Peasants
	ASSERT_FALSE(HasFatalFailure());
	giveArtifact(defenderSideHero, ArtifactID(ArtifactID::BALLISTA), ArtifactPosition::MACH1);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	battle()->obstacles.clear();

	const CStack * crusaders = nullptr;
	CStack * peasants = nullptr;
	const CStack * ballista = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(7))
			crusaders = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			peasants = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID::BALLISTA)
			ballista = unit.get();
	}
	ASSERT_NE(crusaders, nullptr);
	ASSERT_NE(peasants, nullptr);
	ASSERT_NE(ballista, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), crusaders);
	const BattleHex adjacent(crusaders->getPosition().toInt() + 5);
	ASSERT_TRUE(adjacent.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(adjacent), nullptr);
	peasants->setPosition(adjacent);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanShoot(ballista))
		<< "The fixture requires a genuine shooting machine";
	ASSERT_FALSE(view->battleCanAttackUnit(ballista, crusaders))
		<< "VCMI must reject a hypothetical melee attack by the Ballista";

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
	policy->activeStack(battleID, crusaders);
	ASSERT_TRUE(callback->action.has_value());
	const auto action = *callback->action;
	ASSERT_EQ(action.actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(action.target.size(), 2u);
	EXPECT_EQ(view->battleGetUnitByPos(action.target[1].hexValue), peasants);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action))
		<< "VCMI server rejected the HoMM3 attack while a ranged-only machine was present";
	EXPECT_GT(recorder.meleeAttacks, 0);
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// Pinned checkAdjacentHexes consumes a precomputed landing-threat field,
// but the VCMI projection must not assume that moving next to a shooter
// always silences it. Actual unrestricted FREE_SHOOTING keeps the native
// shot legal from adjacent hexes. The bonus is fixture-only; no game rules
// or default AI are changed. A second enemy forces multi-target evaluation.
TEST_F(AIIntegrationNativeBattle, homm3FreeShootingThreatAndEngineShotAtAdjacentAttacker)
{
	prepareBattle(CreatureID(7), 4, CreatureID(2), 25); // Crusaders vs Archers
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();

	const CStack * crusader = nullptr;
	CStack * archer = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(7))
			crusader = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(2))
			archer = unit.get();
	}
	ASSERT_NE(crusader, nullptr);
	ASSERT_NE(archer, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), crusader);

	BattleHex adjacent;
	const auto neighboringHexes = crusader->getPosition().getNeighbouringTiles();
	for(const BattleHex candidate : neighboringHexes)
	{
		if(candidate.isAvailable() && !battle()->battleGetUnitByPos(candidate))
		{
			adjacent = candidate;
			break;
		}
	}
	ASSERT_TRUE(adjacent.isValid());
	ASSERT_EQ(BattleHex::getDistance(adjacent, crusader->getPosition()), 1);
	archer->setPosition(adjacent);
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->isMeleeAttackPossible(archer, crusader));
	ASSERT_FALSE(view->battleCanShoot(archer, crusader->getPosition()))
		<< "Ordinary adjacent Archer must be blocked from shooting";

	archer->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::FREE_SHOOTING, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(archer->canShootBlocked());
	ASSERT_TRUE(view->battleCanShoot(archer, crusader->getPosition()))
		<< "Engine must permit unrestricted free shooting against an adjacent foe";
	EXPECT_FALSE(AIIntegration::Ports::HoMM3::nativeShooterSuppressionPossible(
		view->battleCanShoot(archer), archer->canShootBlocked(), true))
		<< "HoMM3-only native scoring must not reward silencing a shooter that still fires";
	ASSERT_TRUE(AIIntegration::Ports::HoMM3::projectedNativeRangedThreat(
		view->battleCanShoot(archer), true, archer->canShootBlocked(),
		true, false, 0, 1))
		<< "The source-inspired native threat projection must retain the adjacent shot";

	BattleHex extra;
	for(int i = 0; i < GameConstants::BFIELD_SIZE; ++i)
	{
		const BattleHex candidate(i);
		if(candidate.isAvailable() && !battle()->battleGetUnitByPos(candidate)
			&& BattleHex::getDistance(candidate, crusader->getPosition()) >= 7
			&& BattleHex::getDistance(candidate, archer->getPosition()) >= 5)
		{
			extra = candidate;
			break;
		}
	}
	ASSERT_TRUE(extra.isValid());
	ASSERT_NE(addStack(BattleSide::DEFENDER, CreatureID(0), extra, 1), nullptr);

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
	policy->activeStack(battleID, crusader);
	ASSERT_TRUE(callback->action.has_value());
	const auto action = *callback->action;
	ASSERT_EQ(action.actionType, EActionType::WALK_AND_ATTACK)
		<< "HoMM3 must independently select a legal native melee command";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action))
		<< "Real VCMI server rejected the free-shooting encounter command";
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.shots, 0)
		<< "The real native Archer must take its permitted free shot";
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// Original getRangedAttackValue cannot model return shots. Under native VCMI
// rules an eligible shooter can have RANGED_RETALIATION. Both equal-range,
// equal-strength targets initially have identical source-style shot values.
// A new return-fire bonus must switch the independent HoMM3-only decision
// to the otherwise equivalent safe target, not silently assume zero risk.
TEST_F(AIIntegrationNativeBattle, homm3NativeRangedRetaliationChangesChosenTarget)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(2), 12); // Archer
	setArmy(defenderSideHero, CreatureID(2), 35);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(defenderSideHero->id, SlotID(1)), CreatureID(2).toCreature(), 35));
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	battle()->obstacles.clear();

	CStack * shooter = nullptr;
	std::vector<CStack *> targets;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->creatureId() != CreatureID(2))
			continue;
		if(unit->unitSide() == BattleSide::ATTACKER)
			shooter = unit.get();
		else
			targets.push_back(unit.get());
	}
	ASSERT_NE(shooter, nullptr);
	ASSERT_EQ(targets.size(), 2u);
	battle()->activeStack = shooter->unitId();
	// Find two unoccupied and nonadjacent hexes at precisely the same
	// distance: neither path length nor blocked shooting should decide.
	BattleHex first, second;
	for(int i = 0; i < GameConstants::BFIELD_SIZE && !first.isValid(); ++i)
	{
		const BattleHex a(i);
		if(!a.isAvailable() || battle()->battleGetUnitByPos(a))
			continue;
		const int radius = BattleHex::getDistance(a, shooter->getPosition());
		if(radius < 5 || radius > 10)
			continue;
		for(int j = i + 1; j < GameConstants::BFIELD_SIZE; ++j)
		{
			const BattleHex b(j);
			if(!b.isAvailable() || battle()->battleGetUnitByPos(b))
				continue;
			if(BattleHex::getDistance(b, shooter->getPosition()) == radius
				&& BattleHex::getDistance(a, b) >= 3)
			{
				first = a;
				second = b;
				break;
			}
		}
	}
	ASSERT_TRUE(first.isValid());
	ASSERT_TRUE(second.isValid());
	targets.front()->setPosition(first);
	targets.back()->setPosition(second);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	for(const auto * target : targets)
	{
		ASSERT_TRUE(view->battleCanShoot(shooter, target->getPosition()));
		ASSERT_TRUE(view->battleCanShoot(target, shooter->getPosition()));
		ASSERT_TRUE(target->ableToRetaliate());
	}
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *shooter);
	ASSERT_EQ(baseline.actionType, EActionType::SHOOT);
	ASSERT_FALSE(baseline.target.empty());
	CStack * formerChoice = nullptr;
	CStack * safeTarget = nullptr;
	for(auto * target : targets)
	{
		if(target->unitId() == baseline.target.front().unitValue)
			formerChoice = target;
		else
			safeTarget = target;
	}
	ASSERT_NE(formerChoice, nullptr);
	ASSERT_NE(safeTarget, nullptr);
	BattleAttackInfo opening(shooter, formerChoice, 0, true);
	ASSERT_LT(view->battleEstimateDamage(opening).damage.max, formerChoice->getAvailableHealth());
	formerChoice->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::RANGED_RETALIATION,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(2))));
	ASSERT_TRUE(formerChoice->hasBonusOfType(BonusType::RANGED_RETALIATION));
	BattleAttackInfo counter(formerChoice, shooter, 0, true);
	ASSERT_GT(view->battleEstimateDamage(counter).damage.min, 0);
	const auto reranked = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *shooter);
	ASSERT_EQ(reranked.actionType, EActionType::SHOOT);
	ASSERT_FALSE(reranked.target.empty());
	EXPECT_EQ(reranked.target.front().unitValue, safeTarget->unitId())
		<< "Native return-fire exposure should reverse an otherwise equal shot choice";

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
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	ASSERT_FALSE(callback->action->target.empty());
	EXPECT_EQ(callback->action->target.front().unitValue, safeTarget->unitId());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action));
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.shots, 0);
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// HoMM3's pinned simulateAttack does not support ranged preemptive strikes.
// Under VCMI rules FIRST_STRIKE plus RANGED_RETALIATION is resolved BEFORE
// the attacking Archer fires. Validate real packet order, a reduced native
// damage preview and an independently submitted, server-accepted command.
// This synthetic creature bonus is a host-engine extension, not retail parity.
TEST_F(AIIntegrationNativeBattle, homm3NativeRangedFirstStrikeExecutesBeforeOpeningShot)
{
	prepareBattle(CreatureID(2), 20, CreatureID(2), 35);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * shooter = nullptr;
	CStack * defender = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER)
			shooter = unit.get();
		else
			defender = unit.get();
	}
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(defender, nullptr);
	defender->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::RANGED_RETALIATION,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(2))));
	defender->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::FIRST_STRIKE,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(2))));
	ASSERT_TRUE(defender->hasBonusOfType(BonusType::FIRST_STRIKE));
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto view = callback->getBattle(battleID);
	ASSERT_TRUE(view->battleCanShoot(shooter, defender->getPosition()));
	ASSERT_TRUE(view->battleCanShoot(defender, shooter->getPosition()));
	ASSERT_TRUE(defender->ableToRetaliate());
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);

	BattleAttackInfo opening(shooter, defender, 0, true);
	const auto oldShot = view->battleEstimateDamage(opening);
	BattleAttackInfo counter(defender, shooter, 0, true);
	const auto firstCounter = view->battleEstimateDamage(counter);
	ASSERT_GT(firstCounter.damage.min, 0);
	auto reducedShooter = shooter->acquireState();
	int64_t expectedCounter = firstCounter.damage.min
		+ (firstCounter.damage.max - firstCounter.damage.min) / 2;
	expectedCounter = std::clamp<int64_t>(
		expectedCounter, 0, reducedShooter->getAvailableHealth());
	reducedShooter->damage(expectedCounter);
	ASSERT_TRUE(reducedShooter->alive());
	ASSERT_LT(reducedShooter->getCount(), shooter->getCount());
	BattleAttackInfo reducedOpening(reducedShooter.get(), defender, 0, true);
	reducedOpening.attackerPos = shooter->getPosition();
	EXPECT_LT(view->battleEstimateDamage(reducedOpening).damage.max, oldShot.damage.max)
		<< "FIRST_STRIKE makes the surviving attacking shooters weaker";
	const int64_t originalShooterHealth = shooter->getAvailableHealth();
	const int64_t originalDefenderHealth = defender->getAvailableHealth();

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
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	ASSERT_FALSE(callback->action->target.empty());
	EXPECT_EQ(callback->action->target.front().unitValue, defender->unitId());
	EXPECT_EQ(shooter->getAvailableHealth(), originalShooterHealth);
	EXPECT_EQ(defender->getAvailableHealth(), originalDefenderHealth)
		<< "A tactical forecast must never mutate live stacks";

	std::vector<int> orderedShotVictims;
	recorder.onAttackBeforeApply = [&](const BattleAttack & hit)
	{
		if(!hit.shot())
			return;
		for(const auto & victim : hit.bsa)
			if(victim.stackAttacked == shooter->unitId() || victim.stackAttacked == defender->unitId())
				orderedShotVictims.push_back(victim.stackAttacked);
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "VCMI server rejected the independent HoMM3 ranged command";
	ASSERT_GE(orderedShotVictims.size(), 2u)
		<< "The authoritative engine must execute both the countershot and original shot";
	EXPECT_EQ(orderedShotVictims.front(), shooter->unitId())
		<< "The defender's FIRST_STRIKE must land before the initial shot";
	EXPECT_EQ(orderedShotVictims[1], defender->unitId());
	recorder.onAttackBeforeApply = {};
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// The pinned HoMM3 ranged score prices only one primary victim. The native
// SHOOTS_ALL_ADJACENT extension must not forecast full-strength collateral
// after a preemptive ranged FIRST_STRIKE has killed some shooters. Verify
// the detached preview, independent port command and real server hit order.
// Neither ability combination nor its valuation is a retail HoMM3 rule.
TEST_F(AIIntegrationNativeBattle, homm3NativeFirstStrikeReducesAreaShotCollateralAndExecutes)
{
	prepareBattle(CreatureID(2), 20, CreatureID(2), 35);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * shooter = nullptr;
	CStack * defender = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER)
			shooter = unit.get();
		else
			defender = unit.get();
	}
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(defender, nullptr);
	BattleHex neighbouringCell;
	// getNeighbouringTiles() can refer to storage in its BattleHex receiver.
	// Keep the receiver alive through iteration instead of borrowing a temporary.
	const BattleHex defenderPosition = defender->getPosition();
	for(const auto & hex : defenderPosition.getNeighbouringTiles())
	{
		if(hex.isAvailable() && battle()->battleGetUnitByPos(hex) == nullptr)
		{
			neighbouringCell = hex;
			break;
		}
	}
	ASSERT_TRUE(neighbouringCell.isValid());
	auto * neighbour = addStack(BattleSide::DEFENDER, CreatureID(2), neighbouringCell, 35);
	ASSERT_NE(neighbour, nullptr);
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SHOOTS_ALL_ADJACENT, BonusSource::OTHER, 1, BonusSourceID()));
	for(CStack * rangedDefender : {defender, neighbour})
	{
		rangedDefender->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::RANGED_RETALIATION, BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(2))));
		rangedDefender->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::FIRST_STRIKE, BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(2))));
	}

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto view = callback->getBattle(battleID);
	ASSERT_TRUE(view->battleCanShoot(shooter, defender->getPosition()));
	ASSERT_TRUE(view->battleCanShoot(shooter, neighbour->getPosition()));
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);
	const auto firstDecision = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *shooter);
	ASSERT_EQ(firstDecision.actionType, EActionType::SHOOT);
	ASSERT_FALSE(firstDecision.target.empty());
	CStack * primary = firstDecision.target.front().unitValue == defender->unitId() ? defender : neighbour;
	CStack * secondary = primary == defender ? neighbour : defender;
	ASSERT_EQ(firstDecision.target.front().unitValue, primary->unitId());
	ASSERT_TRUE(view->battleCanShoot(primary, shooter->getPosition()));
	ASSERT_TRUE(primary->ableToRetaliate());
	const auto nativeSplash = view->getAttackedBattleUnits(
		shooter, primary, primary->getPosition(), true);
	ASSERT_NE(std::find(nativeSplash.begin(), nativeSplash.end(), secondary), nativeSplash.end())
		<< "The actual area shot must reach a second living enemy";
	BattleAttackInfo counter(primary, shooter, 0, true);
	const auto preemptive = view->battleEstimateDamage(counter);
	ASSERT_GT(preemptive.damage.min, 0);
	auto survivingShooter = shooter->acquireState();
	int64_t counterHit = preemptive.damage.min
		+ (preemptive.damage.max - preemptive.damage.min) / 2;
	counterHit = std::clamp<int64_t>(counterHit, 0, survivingShooter->getAvailableHealth());
	survivingShooter->damage(counterHit);
	ASSERT_TRUE(survivingShooter->alive());
	ASSERT_LT(survivingShooter->getCount(), shooter->getCount());
	BattleAttackInfo originalCollateral(shooter, secondary, 0, true);
	BattleAttackInfo reducedCollateral(survivingShooter.get(), secondary, 0, true);
	reducedCollateral.attackerPos = shooter->getPosition();
	EXPECT_LT(view->battleEstimateDamage(reducedCollateral).damage.max,
		view->battleEstimateDamage(originalCollateral).damage.max)
		<< "Preemptive casualties must reduce the projected area volley";
	const int64_t oldShooterHealth = shooter->getAvailableHealth();
	const int64_t oldSecondaryHealth = secondary->getAvailableHealth();

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
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	ASSERT_FALSE(callback->action->target.empty());
	EXPECT_EQ(callback->action->target.front().unitValue, firstDecision.target.front().unitValue);
	std::vector<int> shotVictims;
	recorder.onAttackBeforeApply = [&](const BattleAttack & hit)
	{
		if(!hit.shot())
			return;
		for(const auto & victim : hit.bsa)
			shotVictims.push_back(victim.stackAttacked);
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action));
	recorder.onAttackBeforeApply = {};
	ASSERT_GE(shotVictims.size(), 3u);
	EXPECT_EQ(shotVictims.front(), shooter->unitId())
		<< "FIRST_STRIKE must damage the shooter before its area hit";
	EXPECT_LT(shooter->getAvailableHealth(), oldShooterHealth);
	EXPECT_LT(secondary->getAvailableHealth(), oldSecondaryHealth)
		<< "The server must actually damage the secondary area-shot victim";
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// Retail simulateAttack predicts first melee hit -> retaliation -> second hit.
// VCMI's damageTypeMelee FIRST_STRIKE reverses the first two steps. The
// HoMM3-only detached-state projection must account for this native rule,
// then submit its own legal action and observe the server's actual packets.
// The synthetic bonus does NOT imply that retail Heroes III has FIRST_STRIKE.
TEST_F(AIIntegrationNativeBattle, homm3NativeMeleeFirstStrikeCountersBeforeCrusaderOpening)
{
	prepareBattle(CreatureID(7), 6, CreatureID(6), 6); // Crusaders vs Swordsmen
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * crusaders = nullptr;
	CStack * swordsmen = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER)
			crusaders = unit.get();
		else
			swordsmen = unit.get();
	}
	ASSERT_NE(crusaders, nullptr);
	ASSERT_NE(swordsmen, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), crusaders);
	const BattleHex adjacent(crusaders->getPosition().toInt() + 5);
	ASSERT_TRUE(adjacent.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(adjacent), nullptr);
	swordsmen->setPosition(adjacent);
	swordsmen->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::FIRST_STRIKE,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(6)),
		BonusSubtypeID(BonusCustomSubtype::damageTypeMelee)));
	ASSERT_TRUE(swordsmen->hasBonus(Selector::typeSubtype(
		BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeMelee)));
	ASSERT_GT(crusaders->getTotalAttacks(false), 1);
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanAttackUnit(swordsmen, crusaders));
	ASSERT_TRUE(swordsmen->ableToRetaliate());
	ASSERT_FALSE(view->isLongWeaponAttack(crusaders, swordsmen, crusaders->getPosition()));
	BattleAttackInfo firstCounter(swordsmen, crusaders, 0, false);
	firstCounter.attackerPos = swordsmen->getPosition();
	firstCounter.defenderPos = crusaders->getPosition();
	const auto initialDamage = view->battleEstimateDamage(firstCounter);
	ASSERT_GT(initialDamage.damage.min, 0);
	auto surviving = crusaders->acquireState();
	int64_t firstLoss = initialDamage.damage.min
		+ (initialDamage.damage.max - initialDamage.damage.min) / 2;
	firstLoss = std::clamp<int64_t>(firstLoss, 0, surviving->getAvailableHealth());
	surviving->damage(firstLoss);
	ASSERT_TRUE(surviving->alive());
	ASSERT_LT(surviving->getCount(), crusaders->getCount());
	const auto beforeAttacker = crusaders->getAvailableHealth();
	const auto beforeDefender = swordsmen->getAvailableHealth();

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
	policy->activeStack(battleID, crusaders);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(callback->action->target.size(), 2u);
	EXPECT_EQ(view->battleGetUnitByPos(callback->action->target[1].hexValue), swordsmen);
	EXPECT_EQ(crusaders->getAvailableHealth(), beforeAttacker);
	EXPECT_EQ(swordsmen->getAvailableHealth(), beforeDefender);

	std::vector<int> attackOrder;
	const auto crusaderID = static_cast<ui32>(crusaders->unitId());
	const auto swordsmanID = static_cast<ui32>(swordsmen->unitId());
	recorder.onAttackBeforeApply = [&](const BattleAttack & packet)
	{
		if(!packet.shot()
			&& (packet.stackAttacking == swordsmanID || packet.stackAttacking == crusaderID))
			attackOrder.push_back(static_cast<int>(packet.stackAttacking));
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "VCMI rejected a native HoMM3 melee decision against FIRST_STRIKE";
	recorder.onAttackBeforeApply = {};
	ASSERT_GE(attackOrder.size(), 2u);
	EXPECT_EQ(attackOrder[0], static_cast<int>(swordsmanID))
		<< "The engine must execute the defender's preemptive counterattack first";
	EXPECT_EQ(attackOrder[1], static_cast<int>(crusaderID))
		<< "Only the surviving attacker may deliver its opening blow";
	EXPECT_LT(crusaders->getAvailableHealth(), beforeAttacker);
	EXPECT_LT(swordsmen->getAvailableHealth(), beforeDefender);
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// A melee-only FIRST_STRIKE must not fire before a ranged attack.
// Even with RANGED_RETALIATION, the server first resolves the attacking shot.
// This exercises the subtype distinction used by the HoMM3-only projection.
TEST_F(AIIntegrationNativeBattle, homm3MeleeOnlyFirstStrikeDoesNotPreemptRangedAttack)
{
	prepareBattle(CreatureID(2), 20, CreatureID(2), 35);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * shooter = nullptr;
	CStack * defender = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER)
			shooter = unit.get();
		else
			defender = unit.get();
	}
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(defender, nullptr);
	defender->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::RANGED_RETALIATION,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(2))));
	defender->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::FIRST_STRIKE,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(2)),
		BonusSubtypeID(BonusCustomSubtype::damageTypeMelee)));
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanShoot(shooter, defender->getPosition()));
	ASSERT_TRUE(view->battleCanShoot(defender, shooter->getPosition()));
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
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	EXPECT_EQ(callback->action->target.front().unitValue, defender->unitId());
	std::vector<int> orderedVictims;
	recorder.onAttackBeforeApply = [&](const BattleAttack & hit)
	{
		if(!hit.shot())
			return;
		for(const auto & victim : hit.bsa)
			if(victim.stackAttacked == shooter->unitId() || victim.stackAttacked == defender->unitId())
				orderedVictims.push_back(victim.stackAttacked);
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action));
	recorder.onAttackBeforeApply = {};
	ASSERT_GE(orderedVictims.size(), 2u);
	EXPECT_EQ(orderedVictims[0], defender->unitId())
		<< "The shooter's opening hit comes before a melee-only FIRST_STRIKE";
	EXPECT_EQ(orderedVictims[1], shooter->unitId())
		<< "RANGED_RETALIATION remains legal but follows the first shot";
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// The original HoMM3 getSimpleAttackEffect subtracts loss to the attacker:
// a poor return-fire exchange must remain NEGATIVE, not be clamped to zero.
// Use real VCMI preview values from a hopeless one-Archer attack against a
// native ranged-retaliating stack. The decision itself is still made by the
// HoMM3OriginalTactics adapter and executed by the authoritative game server;
// an independent retail HoMM3 target ranking is not established.
TEST_F(AIIntegrationNativeBattle, homm3NativeReturnFireProducesSignedLosingShotAndExecutes)
{
	prepareBattle(CreatureID(2), 1, CreatureID(2), 35);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * attacker = nullptr;
	CStack * defender = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER)
			attacker = unit.get();
		else
			defender = unit.get();
	}
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(defender, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), attacker);
	defender->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::RANGED_RETALIATION,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(2))));
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanShoot(attacker, defender->getPosition()));
	ASSERT_TRUE(view->battleCanShoot(defender, attacker->getPosition()));
	ASSERT_TRUE(defender->ableToRetaliate());
	BattleAttackInfo shot(attacker, defender, 0, true);
	const auto projectedShot = view->battleEstimateDamage(shot);
	ASSERT_GT(projectedShot.damage.min, 0);
	ASSERT_LT(projectedShot.damage.max, defender->getAvailableHealth());
	auto survivingDefender = defender->acquireState();
	int64_t firstHit = projectedShot.damage.min
		+ (projectedShot.damage.max - projectedShot.damage.min) / 2;
	firstHit = std::clamp<int64_t>(firstHit, 0, survivingDefender->getAvailableHealth());
	survivingDefender->damage(firstHit);
	ASSERT_TRUE(survivingDefender->alive());
	BattleAttackInfo counter(survivingDefender.get(), attacker, 0, true);
	counter.attackerPos = defender->getPosition();
	const auto projectedCounter = view->battleEstimateDamage(counter);
	const auto scaledLoss = [](const CStack & unit, const DamageEstimation & estimate)
	{
		const double suffered = std::min<double>(unit.getAvailableHealth(),
			static_cast<double>(estimate.damage.min)
			+ (static_cast<double>(estimate.damage.max) - estimate.damage.min) / 2);
		return unit.estimateCombatValue() * suffered / unit.getAvailableHealth();
	};
	const double netEffect = scaledLoss(*defender, projectedShot)
		- scaledLoss(*attacker, projectedCounter);
	ASSERT_LT(netEffect, -1.0)
		<< "This fixture requires a genuinely losing native return-fire exchange";
	EXPECT_LT(AIIntegration::Ports::HoMM3::signedProjectedAttackEffect(netEffect), 0);
	const auto planned = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *attacker);
	ASSERT_EQ(planned.actionType, EActionType::SHOOT);
	ASSERT_FALSE(planned.target.empty());
	EXPECT_EQ(planned.target.front().unitValue, defender->unitId());
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
	policy->activeStack(battleID, attacker);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Authoritative engine rejected the independently chosen losing shot";
	policy.reset();
	// The defender's real return shot can destroy the sole attacker and
	// finish this battle *inside* makePlayerBattleAction. In that case the
	// server has already applied its terminal result; do not start a second
	// fixture driver on an already released battle.
	if(!gameState()->currentBattles.empty())
		runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.shots, 0);
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// Independent real-server confirmation that RANGED_RETALIATION causes a
// countershot, instead of trusting a generic damage preview or a fake model.
TEST_F(AIIntegrationNativeBattle, homm3RangedRetaliationExecutesOnNativeServer)
{
	prepareBattle(CreatureID(2), 12, CreatureID(2), 35);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * shooter = nullptr;
	CStack * defender = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER)
			shooter = unit.get();
		else
			defender = unit.get();
	}
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(defender, nullptr);
	defender->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::RANGED_RETALIATION,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(2))));
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_TRUE(view->battleCanShoot(shooter, defender->getPosition()));
	ASSERT_TRUE(view->battleCanShoot(defender, shooter->getPosition()));
	ASSERT_TRUE(defender->ableToRetaliate());
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);

	// Pinned simulateAttack models the first hit before any counterattack.
	// A VCMI ranged countershot is an extension; its predicted damage must
	// come from the surviving shooters, not the defender's untouched count.
	BattleAttackInfo firstShot(shooter, defender, 0, true);
	const auto firstEstimate = view->battleEstimateDamage(firstShot);
	ASSERT_GT(firstEstimate.damage.min, 0);
	ASSERT_LT(firstEstimate.damage.max, defender->getAvailableHealth());
	BattleAttackInfo originalCounter(defender, shooter, 0, true);
	originalCounter.attackerPos = defender->getPosition();
	const auto fullCounterEstimate = view->battleEstimateDamage(originalCounter);
	ASSERT_GT(fullCounterEstimate.damage.max, 0);
	const auto originalDefenderHealth = defender->getAvailableHealth();
	const auto originalDefenderCount = defender->getCount();
	auto survivingDefender = defender->acquireState();
	int64_t firstHit = firstEstimate.damage.min
		+ (firstEstimate.damage.max - firstEstimate.damage.min) / 2;
	firstHit = std::clamp<int64_t>(firstHit, 0, survivingDefender->getAvailableHealth());
	survivingDefender->damage(firstHit);
	ASSERT_TRUE(survivingDefender->alive());
	ASSERT_LT(survivingDefender->getCount(), originalDefenderCount);
	BattleAttackInfo reducedCounter(survivingDefender.get(), shooter, 0, true);
	reducedCounter.attackerPos = defender->getPosition();
	const auto reducedEstimate = view->battleEstimateDamage(reducedCounter);
	EXPECT_LT(reducedEstimate.damage.max, fullCounterEstimate.damage.max)
		<< "Reduced ranged retaliation must use the post-hit unit count";
	EXPECT_EQ(defender->getAvailableHealth(), originalDefenderHealth)
		<< "The HoMM3 preview must not change the live defender";
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
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	const int before = recorder.shots;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action));
	EXPECT_GE(recorder.shots - before, 2)
		<< "A real native countershot must execute before the following turn";
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// The source threat field is evaluated for a FUTURE landing position.
// A native archer may be unable to shoot now only because the moving
// Cavalier is in contact; after an engine-accepted move away, its shot
// must recover. HoMM3's approximation must not mistake the old blocking
// status for an intrinsic loss of ranged capability. This is VCMI host
// behavior, not a claim of recovered Heroes III threat-map parity.
TEST_F(AIIntegrationNativeBattle, homm3PinnedArcherRecoversRealShootingAfterMoverLeaves)
{
	prepareBattle(CreatureID(10), 6, CreatureID(2), 1); // Cavalier, Archer
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * cavalier = nullptr;
	CStack * archer = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER)
			cavalier = unit.get();
		else if(unit->creatureId() == CreatureID(2))
			archer = unit.get();
	}
	ASSERT_NE(cavalier, nullptr);
	ASSERT_NE(archer, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), cavalier);
	BattleHex pinned;
	for(const auto & hex : cavalier->getSurroundingHexes())
		if(hex.isAvailable() && !battle()->battleGetUnitByPos(hex))
		{
			pinned = hex;
			break;
		}
	ASSERT_TRUE(pinned.isValid());
	archer->setPosition(pinned);
	BattleHex otherTarget;
	for(int i = 0; i < GameConstants::BFIELD_SIZE; ++i)
	{
		const BattleHex hex(i);
		if(!hex.isAvailable() || battle()->battleGetUnitByPos(hex))
			continue;
		const int fromMover = BattleHex::getDistance(hex, cavalier->getPosition());
		const int fromShooter = BattleHex::getDistance(hex, archer->getPosition());
		if(fromMover >= 4 && fromMover <= 6 && fromShooter >= 5)
		{
			otherTarget = hex;
			break;
		}
	}
	ASSERT_TRUE(otherTarget.isValid());
	const CStack * peasants = addStack(BattleSide::DEFENDER, CreatureID(0), otherTarget, 80);
	ASSERT_NE(peasants, nullptr);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(archer->canShoot());
	ASSERT_FALSE(view->battleCanShoot(archer))
		<< "The native shooter must first be blocked by the active mover";

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
	policy->activeStack(battleID, cavalier);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK)
		<< "The HoMM3 port must independently choose a native-legal melee action";
	ASSERT_GE(callback->action->target.size(), 2u);
	ASSERT_EQ(view->battleGetUnitByPos(callback->action->target[1].hexValue), peasants)
		<< "The strong distant target must induce a real move away from the pinned Archer";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The VCMI server rejected HoMM3's selected melee movement";
	EXPECT_TRUE(view->battleCanShoot(archer))
		<< "The surviving native Archer must regain shooting after the mover leaves";
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.shots, 0) << "Native future archer fire should execute";
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// Pinned getHexAttackValue / checkAdjacentHexes use the opponent's actual
// shooting capability. In the native threat projection, an active Cavalier
// may release its own pin but cannot release ANOTHER live friendly blocker.
// Cache this stable blocker census once per choice, then execute the chosen
// action on the real battle server. No retail source equivalence is claimed.
TEST_F(AIIntegrationNativeBattle, homm3PinnedArcherRemainsBlockedWhenAnotherEnemyStackPinsIt)
{
	prepareBattle(CreatureID(10), 6, CreatureID(2), 1); // Cavalier, Archer
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * cavalier = nullptr;
	CStack * archer = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER)
			cavalier = unit.get();
		else if(unit->creatureId() == CreatureID(2))
			archer = unit.get();
	}
	ASSERT_NE(cavalier, nullptr);
	ASSERT_NE(archer, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), cavalier);
	BattleHex pinned;
	for(const auto & hex : cavalier->getSurroundingHexes())
		if(hex.isAvailable() && !battle()->battleGetUnitByPos(hex))
		{
			pinned = hex;
			break;
		}
	ASSERT_TRUE(pinned.isValid());
	archer->setPosition(pinned);
	// The second native attacker-side stack must stay adjacent after the
	// Cavalier leaves. Without the other-blocker census the future-threat
	// approximation would incorrectly revive this Archer's ranged shot.
	BattleHex secondBlocker;
	for(const auto & hex : archer->getSurroundingHexes())
		if(hex.isAvailable() && !battle()->battleGetUnitByPos(hex)
			&& BattleHex::getDistance(hex, cavalier->getPosition()) > 1)
		{
			secondBlocker = hex;
			break;
		}
	ASSERT_TRUE(secondBlocker.isValid());
	const auto * blockingPeasant = addStack(BattleSide::ATTACKER, CreatureID(0), secondBlocker, 1);
	ASSERT_NE(blockingPeasant, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), cavalier);
	BattleHex otherTarget;
	for(int i = 0; i < GameConstants::BFIELD_SIZE; ++i)
	{
		const BattleHex hex(i);
		if(!hex.isAvailable() || battle()->battleGetUnitByPos(hex))
			continue;
		const int fromMover = BattleHex::getDistance(hex, cavalier->getPosition());
		const int fromShooter = BattleHex::getDistance(hex, archer->getPosition());
		if(fromMover >= 4 && fromMover <= 6 && fromShooter >= 5)
		{
			otherTarget = hex;
			break;
		}
	}
	ASSERT_TRUE(otherTarget.isValid());
	const CStack * peasants = addStack(BattleSide::DEFENDER, CreatureID(0), otherTarget, 80);
	ASSERT_NE(peasants, nullptr);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(archer->canShoot());
	ASSERT_FALSE(view->battleCanShoot(archer))
		<< "The native shooter must first be blocked by the active mover";

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
	policy->activeStack(battleID, cavalier);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK)
		<< "The HoMM3 port must independently choose a native-legal melee action";
	ASSERT_GE(callback->action->target.size(), 2u);
	ASSERT_EQ(view->battleGetUnitByPos(callback->action->target[1].hexValue), peasants)
		<< "The strong distant target must induce a real move away from the pinned Archer";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The VCMI server rejected HoMM3's selected melee movement";
	EXPECT_FALSE(view->battleCanShoot(archer))
		<< "The Archer must remain blocked by the other live adjacent enemy stack";
	EXPECT_TRUE(blockingPeasant->alive())
		<< "The second blocker must still exist when the Cavalier leaves";
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.meleeAttacks, 0) << "The actual VCMI battle must execute melee";
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// Pinned ai_tactical.cpp::checkAdjacentHexes compares signed long value
// before the Cavalier's path-cost tie break. Force several native legal
// approaches to the same certainly lethal one-Peasant strike. The remote
// immobile enemy keeps the server battle alive without creating landing
// threat, so the independent HoMM3 policy must select the longer charge.
TEST_F(AIIntegrationNativeBattle, homm3CavalierUsesSourceLongTieBreakInServerBattle)
{
	prepareBattle(CreatureID(10), 2, CreatureID(0), 100);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * cavalier = nullptr;
	CStack * remote = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(10))
			cavalier = unit.get();
		else if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			remote = unit.get();
	}
	ASSERT_NE(cavalier, nullptr);
	ASSERT_NE(remote, nullptr);
	ASSERT_TRUE(cavalier->hasBonusOfType(BonusType::JOUSTING));
	remote->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::STACKS_SPEED,
		BonusSource::OTHER, -100, BonusSourceID()));
	ASSERT_EQ(std::max(0, static_cast<int>(remote->getMovementRange())), 0);

	const BattleHex nearbyHex(cavalier->getPosition().toInt() + 5);
	ASSERT_TRUE(nearbyHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(nearbyHex), nullptr);
	const CStack * nearTarget = addStack(BattleSide::DEFENDER, CreatureID(0), nearbyHex, 1);
	ASSERT_NE(nearTarget, nullptr);
	const auto reach = battle()->getReachability(cavalier);
	const auto available = battle()->battleGetAvailableHexes(reach, cavalier, false);
	int longest = -1;
	int shortest = std::numeric_limits<int>::max();
	for(const auto & hex : available)
	{
		if(!hex.isAvailable() || battle()->meleeAttackHexes(cavalier, nearTarget, hex).empty())
			continue;
		const int distance = reach.distances[hex.toInt()];
		if(distance < 0)
			continue;
		longest = std::max(longest, distance);
		shortest = std::min(shortest, distance);
	}
	ASSERT_LT(shortest, longest) << "A native equal-kill Cavalier charge needs distinct path costs";

	const BattleID battleID = battle()->getBattleID();
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
	EXPECT_EQ(callback->action->target[1].hexValue, nearTarget->getPosition());
	ASSERT_TRUE(from.isValid());
	EXPECT_EQ(reach.distances[from.toInt()], longest)
		<< "Source long-valued equal attack ranks must favor the longest Cavalier charge";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The real VCMI server rejected HoMM3's independently chosen charge";
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// Pinned ai_tactical.cpp::checkAdjacentHexes accounts for a Cavalier's
// charge path in its source ordering. The HoMM3-only landing-threat adapter
// must pass each opposing melee origin (and its actual path cost) to the
// authoritative VCMI damage preview, rather than pricing every origin as
// a zero-distance strike from the stack's current position. This fixture
// establishes distinct native origin/path inputs and an accepted real
// server battle; it does NOT assume different native damage estimates or
// establish source-executable decision parity.
TEST_F(AIIntegrationNativeBattle, homm3NativeJousterPathDependentDamagePreviewAndBattle)
{
	prepareBattle(CreatureID(10), 4, CreatureID(0), 100);
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
	const CStack * nearTarget = addStack(BattleSide::DEFENDER, CreatureID(0), nearbyHex, 300);
	ASSERT_NE(nearTarget, nullptr);

	const auto reach = battle()->getReachability(cavalier);
	const auto origins = battle()->battleGetAvailableHexes(reach, cavalier, true);
	int shortest = std::numeric_limits<int>::max();
	int longest = -1;
	int64_t shortDamage = 0;
	int64_t longDamage = 0;
	for(const auto & origin : origins)
	{
		if(!origin.isAvailable() || !battle()->meleeAttackHexes(cavalier, nearTarget, origin).contains(nearbyHex))
			continue;
		const int pathCost = reach.distances[origin.toInt()];
		if(pathCost < 0)
			continue;
		BattleAttackInfo info(cavalier, nearTarget, pathCost, false);
		info.attackerPos = origin;
		info.defenderPos = nearbyHex;
		const auto estimated = battle()->battleEstimateDamage(info);
		const int64_t representative = estimated.damage.min
			+ (estimated.damage.max - estimated.damage.min) / 2;
		if(pathCost < shortest)
		{
			shortest = pathCost;
			shortDamage = representative;
		}
		if(pathCost > longest)
		{
			longest = pathCost;
			longDamage = representative;
		}
	}
	ASSERT_LT(shortest, longest) << "Threat preview needs distinct native Cavalier melee origins";
	// The native estimator may give the same representative damage at
	// different legal origins in this particular fight. Preserve their
	// distinct charge-distance inputs without fabricating a difference
	// in calculated damage or claiming exact retail parity.
	EXPECT_GT(shortDamage, 0);
	EXPECT_GT(longDamage, 0);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto inspected = callback->getBattle(battleID);
	ASSERT_NE(inspected, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), cavalier);
	const auto action = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*inspected, *cavalier);
	ASSERT_EQ(action.actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action))
		<< "Server rejected independent HoMM3 opening melee command";
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

TEST_F(AIIntegrationNativeBattle, homm3JousterLandingRiskPrecedesEqualDamageTieBreak)
{
	// Pinned HoMM3 checkAdjacentHexes adds enemy_attacks[hex] BEFORE the
	// Cavalier's longer-path tie break. These two legal attacks kill the
	// same nearby Peasant, but a second enemy threatens one landing hex.
	// Native next-turn risk may legitimately break the damage-value tie;
	// the source's longer-route predicate is verified independently by
	// the pure contract suite. The far defender also keeps combat alive.
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
	EXPECT_GE(reach.distances[from.toInt()], shortest);
	EXPECT_LT(reach.distances[from.toInt()], longest)
		<< "The separate enemy's projected landing threat must precede the jousting tie break";
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

// Native FIRST_STRIKE is not part of pinned HoMM3 simulateAttack. A guaranteed
// lethal preemptive counter must suppress projected multi-target damage: the
// attacker never gets to deliver its own splash. The engine, not a mock,
// decides the order and the battle outcome.
TEST_F(AIIntegrationNativeBattle, homm3LethalNativeFirstStrikePreventsPhantomSplash)
{
	prepareBattle(CreatureID(7), 1, CreatureID(0), 500); // Crusader vs Peasants
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * crusader = nullptr;
	CStack * primary = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER)
			crusader = unit.get();
		else
			primary = unit.get();
	}
	ASSERT_NE(crusader, nullptr);
	ASSERT_NE(primary, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), crusader);
	crusader->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::ATTACKS_ALL_ADJACENT,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(7))));
	ASSERT_TRUE(crusader->hasBonusOfType(BonusType::ATTACKS_ALL_ADJACENT));
	std::vector<BattleHex> adjacent;
	for(const auto & hex : crusader->getSurroundingHexes())
		if(hex.isAvailable() && battle()->battleGetUnitByPos(hex) == nullptr)
			adjacent.push_back(hex);
	ASSERT_GE(adjacent.size(), 2u);
	primary->setPosition(adjacent[0]);
	CStack * secondary = const_cast<CStack *>(
		addStack(BattleSide::DEFENDER, CreatureID(0), adjacent[1], 500));
	ASSERT_NE(secondary, nullptr);
	const auto addPreemptiveCounter = [](CStack * defender)
	{
		defender->addNewBonus(std::make_shared<Bonus>(
			BonusDuration::PERMANENT, BonusType::FIRST_STRIKE,
			BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(0)),
			BonusSubtypeID(BonusCustomSubtype::damageTypeMelee)));
	};
	addPreemptiveCounter(primary);
	addPreemptiveCounter(secondary);
	const auto beforeCrusader = crusader->getAvailableHealth();
	const ui32 crusaderID = static_cast<ui32>(crusader->unitId());

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	for(const CStack * defender : {primary, secondary})
	{
		BattleAttackInfo firstCounter(defender, crusader, 0, false);
		firstCounter.attackerPos = defender->getPosition();
		firstCounter.defenderPos = crusader->getPosition();
		const auto preview = view->battleEstimateDamage(firstCounter);
		ASSERT_GE(preview.damage.min, beforeCrusader)
			<< "The real host must guarantee a fatal first strike for either target";
	}
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
	policy->activeStack(battleID, crusader);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(callback->action->target.size(), 2u);
	const CStack * chosen = dynamic_cast<const CStack *>(
		view->battleGetUnitByPos(callback->action->target[1].hexValue));
	ASSERT_TRUE(chosen == primary || chosen == secondary);
	const ui32 chosenID = static_cast<ui32>(chosen->unitId());
	std::vector<ui32> order;
	recorder.onAttackBeforeApply = [&](const BattleAttack & packet)
	{
		if(!packet.shot())
			order.push_back(packet.stackAttacking);
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "VCMI rejected the independent HoMM3 melee command";
	recorder.onAttackBeforeApply = {};
	ASSERT_FALSE(order.empty());
	EXPECT_EQ(order.front(), chosenID)
		<< "The engine must execute the chosen target's FIRST_STRIKE first";
	EXPECT_EQ(std::count(order.begin(), order.end(), crusaderID), 0)
		<< "A killed attacker must not execute its hypothetical multi-target strike";
	// A terminal battle frees its CStack objects. Check native attack packets
	// and the recorded result instead of dereferencing ended-battle units.
	policy.reset();
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty())
		<< "The server must record a terminal defeat when the last attacking stack dies";
}

// In VCMI FIRST_STRIKE may kill only SOME attackers before a Hydra-like
// adjacent splash. The source does not have this preemptive rule. The real
// server must accept the HoMM3 decision and execute the weakened splash.
TEST_F(AIIntegrationNativeBattle, homm3SurvivingFirstStrikeShrinksMultiTargetAttack)
{
	prepareBattle(CreatureID(7), 6, CreatureID(6), 6);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * attacker = nullptr;
	CStack * primary = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER)
			attacker = unit.get();
		else
			primary = unit.get();
	}
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(primary, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), attacker);
	attacker->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::ATTACKS_ALL_ADJACENT,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(7))));
	std::vector<BattleHex> adjacent;
	for(const auto & hex : attacker->getSurroundingHexes())
		if(hex.isAvailable() && battle()->battleGetUnitByPos(hex) == nullptr)
			adjacent.push_back(hex);
	ASSERT_GE(adjacent.size(), 2u);
	primary->setPosition(adjacent[0]);
	CStack * secondary = const_cast<CStack *>(
		addStack(BattleSide::DEFENDER, CreatureID(6), adjacent[1], 6));
	ASSERT_NE(secondary, nullptr);
	for(CStack * defender : {primary, secondary})
		defender->addNewBonus(std::make_shared<Bonus>(
			BonusDuration::PERMANENT, BonusType::FIRST_STRIKE,
			BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(6)),
			BonusSubtypeID(BonusCustomSubtype::damageTypeMelee)));
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	for(const CStack * defender : {primary, secondary})
	{
		BattleAttackInfo counter(defender, attacker, 0, false);
		counter.attackerPos = defender->getPosition();
		counter.defenderPos = attacker->getPosition();
		const auto estimate = view->battleEstimateDamage(counter);
		auto remaining = attacker->acquireState();
		int64_t loss = estimate.damage.min + (estimate.damage.max - estimate.damage.min) / 2;
		loss = std::clamp<int64_t>(loss, 0, remaining->getAvailableHealth());
		remaining->damage(loss);
		ASSERT_TRUE(remaining->alive());
		ASSERT_LT(remaining->getCount(), attacker->getCount());
		BattleAttackInfo whole(attacker, secondary, 0, false);
		whole.attackerPos = attacker->getPosition();
		BattleAttackInfo weakened(remaining.get(), secondary, 0, false);
		weakened.attackerPos = attacker->getPosition();
		EXPECT_LT(view->battleEstimateDamage(weakened).damage.max,
			view->battleEstimateDamage(whole).damage.max);
	}
	const auto attackerHealth = attacker->getAvailableHealth();
	const auto primaryHealth = primary->getAvailableHealth();
	const auto secondaryHealth = secondary->getAvailableHealth();
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
	policy->activeStack(battleID, attacker);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(callback->action->target.size(), 2u);
	const auto * chosen = view->battleGetUnitByPos(callback->action->target[1].hexValue);
	ASSERT_TRUE(chosen == primary || chosen == secondary);
	EXPECT_EQ(attacker->getAvailableHealth(), attackerHealth);
	EXPECT_EQ(primary->getAvailableHealth(), primaryHealth);
	EXPECT_EQ(secondary->getAvailableHealth(), secondaryHealth);
	const ui32 chosenID = static_cast<ui32>(chosen->unitId());
	const ui32 attackerID = static_cast<ui32>(attacker->unitId());
	std::vector<ui32> order;
	recorder.onAttackBeforeApply = [&](const BattleAttack & packet)
	{
		if(!packet.shot())
			order.push_back(packet.stackAttacking);
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action));
	recorder.onAttackBeforeApply = {};
	ASSERT_GE(order.size(), 2u);
	EXPECT_EQ(order.front(), chosenID);
	EXPECT_EQ(order[1], attackerID);
	EXPECT_LT(attacker->getAvailableHealth(), attackerHealth);
	EXPECT_LT(primary->getAvailableHealth(), primaryHealth);
	EXPECT_LT(secondary->getAvailableHealth(), secondaryHealth);
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_FALSE(recorder.battleResults.empty());
}

// The pinned HoMM3 checkAdjacentHexes subtracts a defender's secondary
// multi-head retaliation. The native adapter must price that risk, and the
// real server must actually apply a Hydra's extra counterattack victim.
TEST_F(AIIntegrationNativeBattle, homm3HydraRetaliationReachesAdditionalAlliedStack)
{
	prepareBattle(CreatureID(10), 6, CreatureID(110), 2); // Cavalier vs Hydra
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * cavalier = nullptr;
	CStack * hydra = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(10))
			cavalier = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(110))
			hydra = unit.get();
	}
	ASSERT_NE(cavalier, nullptr);
	ASSERT_NE(hydra, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), cavalier);
	ASSERT_TRUE(hydra->hasBonusOfType(BonusType::ATTACKS_ALL_ADJACENT));
	const BattleHex nearby(cavalier->getPosition().toInt() + 7);
	ASSERT_TRUE(nearby.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(nearby), nullptr);
	hydra->setPosition(nearby);
	for(const auto & occupied : hydra->getHexes())
		ASSERT_TRUE(occupied.isAvailable());

	BattleHex escortHex;
	for(const auto & hex : hydra->getSurroundingHexes())
		if(hex.isAvailable() && !battle()->battleGetUnitByPos(hex))
		{
			escortHex = hex;
			break;
		}
	ASSERT_TRUE(escortHex.isValid());
	const CStack * escort = addStack(BattleSide::ATTACKER, CreatureID(0), escortHex, 150);
	ASSERT_NE(escort, nullptr);
	const int64_t escortBefore = escort->getAvailableHealth();
	const int64_t cavalierBefore = cavalier->getAvailableHealth();
	const int64_t hydraBefore = hydra->getAvailableHealth();

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
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
	policy->activeStack(battleID, cavalier);
	ASSERT_TRUE(callback->action.has_value());
	const auto & action = *callback->action;
	ASSERT_EQ(action.actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(action.target.size(), 2u);
	const BattleHex from = action.target.front().hexValue;
	const BattleHex aimed = action.target[1].hexValue;
	const auto legalAims = view->meleeAttackHexes(cavalier, hydra, from);
	EXPECT_NE(std::find(legalAims.begin(), legalAims.end(), aimed), legalAims.end());
	// Native wide-target damage previews must use the exact legal aim chosen
	// by HoMM3, including when a multi-target defender is expected to
	// retaliate. The preview does not injure any of the three live stacks.
	const auto reach = view->getReachability(cavalier);
	BattleAttackInfo chosenHit(cavalier, hydra, reach.distances[from.toInt()], false);
	chosenHit.attackerPos = from;
	chosenHit.defenderPos = aimed;
	DamageEstimation returnHit;
	const auto initialHit = view->battleEstimateDamage(chosenHit, &returnHit);
	EXPECT_GT(initialHit.damage.max, 0);
	EXPECT_GT(returnHit.damage.max, 0);
	EXPECT_EQ(cavalier->getAvailableHealth(), cavalierBefore);
	EXPECT_EQ(hydra->getAvailableHealth(), hydraBefore);
	EXPECT_EQ(escort->getAvailableHealth(), escortBefore);
	const auto affected = view->getAttackedBattleUnits(
		hydra, cavalier, from, false, hydra->getPosition(), from);
	EXPECT_NE(std::find(affected.begin(), affected.end(), escort), affected.end())
		<< "The VCMI Hydra return swing must cover the additional allied stack";

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action))
		<< "Server rejected HoMM3's legal attack into a multi-head counterattack";
	EXPECT_LT(cavalier->getAvailableHealth(), cavalierBefore);
	EXPECT_LT(escort->getAvailableHealth(), escortBefore)
		<< "A real engine multi-head retaliation must damage the allied escort";
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_GT(recorder.damage, 0);
}

// Retail ai_tactical.cpp::checkAdjacentHexes prices the defender's multi-head
// counter after our opening strike. The VCMI FIRST_STRIKE extension changes
// this order: a full-strength Hydra's adjacent collateral fires BEFORE the
// opening attack, even when a hypothetical first blow might have killed it.
// Check the actual future landing footprint, independent AI command,
// preemptive server packet order and damage to the additional allied stack.
// This does not imply retail HoMM3 has a FIRST_STRIKE creature ability.
TEST_F(AIIntegrationNativeBattle, homm3NativeFirstStrikeHydraRetaliationSplashesBeforeAttack)
{
	prepareBattle(CreatureID(10), 6, CreatureID(110), 2); // Cavalier vs Hydra
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * cavalier = nullptr;
	CStack * hydra = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(10))
			cavalier = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(110))
			hydra = unit.get();
	}
	ASSERT_NE(cavalier, nullptr);
	ASSERT_NE(hydra, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), cavalier);
	ASSERT_TRUE(hydra->hasBonusOfType(BonusType::ATTACKS_ALL_ADJACENT));
	const BattleHex nearby(cavalier->getPosition().toInt() + 7);
	ASSERT_TRUE(nearby.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(nearby), nullptr);
	hydra->setPosition(nearby);
	for(const auto & hex : hydra->getHexes())
		ASSERT_TRUE(hex.isAvailable());
	BattleHex escortHex;
	for(const auto & hex : hydra->getSurroundingHexes())
		if(hex.isAvailable() && !battle()->battleGetUnitByPos(hex))
		{
			escortHex = hex;
			break;
		}
	ASSERT_TRUE(escortHex.isValid());
	const CStack * escort = addStack(BattleSide::ATTACKER, CreatureID(0), escortHex, 150);
	ASSERT_NE(escort, nullptr);
	hydra->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::FIRST_STRIKE,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(110)),
		BonusSubtypeID(BonusCustomSubtype::damageTypeMelee)));
	ASSERT_TRUE(hydra->hasBonus(Selector::typeSubtype(
		BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeMelee)));
	const auto cavalierBefore = cavalier->getAvailableHealth();
	const auto hydraBefore = hydra->getAvailableHealth();
	const auto escortBefore = escort->getAvailableHealth();
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);

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
	policy->activeStack(battleID, cavalier);
	ASSERT_TRUE(callback->action.has_value());
	const auto action = *callback->action;
	ASSERT_EQ(action.actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(action.target.size(), 2u);
	const BattleHex from = action.target.front().hexValue;
	const BattleHex aimed = action.target[1].hexValue;
	const auto legalAims = view->meleeAttackHexes(cavalier, hydra, from);
	ASSERT_NE(std::find(legalAims.begin(), legalAims.end(), aimed), legalAims.end());
	ASSERT_FALSE(view->isLongWeaponAttack(cavalier, hydra, from));
	ASSERT_TRUE(hydra->ableToRetaliate());
	BattleAttackInfo beforeOpening(hydra, cavalier, 0, false);
	beforeOpening.attackerPos = hydra->getPosition();
	beforeOpening.defenderPos = from;
	ASSERT_GT(view->battleEstimateDamage(beforeOpening).damage.max, 0);
	const auto collateral = view->getAttackedBattleUnits(
		hydra, cavalier, from, false, hydra->getPosition(), from);
	ASSERT_NE(std::find(collateral.begin(), collateral.end(), escort), collateral.end())
		<< "Hydra FIRST_STRIKE must reach an additional friendly at the chosen landing hex";
	EXPECT_EQ(cavalier->getAvailableHealth(), cavalierBefore);
	EXPECT_EQ(hydra->getAvailableHealth(), hydraBefore);
	EXPECT_EQ(escort->getAvailableHealth(), escortBefore);

	std::vector<ui32> attackers;
	std::vector<ui32> firstVictims;
	const ui32 hydraID = static_cast<ui32>(hydra->unitId());
	const ui32 escortID = static_cast<ui32>(escort->unitId());
	recorder.onAttackBeforeApply = [&](const BattleAttack & packet)
	{
		if(packet.shot())
			return;
		attackers.push_back(packet.stackAttacking);
		if(attackers.size() == 1)
			for(const auto & victim : packet.bsa)
				firstVictims.push_back(victim.stackAttacked);
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action))
		<< "VCMI rejected HoMM3's independent attack into preemptive multi-head retaliation";
	recorder.onAttackBeforeApply = {};
	ASSERT_FALSE(attackers.empty());
	EXPECT_EQ(attackers.front(), hydraID)
		<< "Hydra's FIRST_STRIKE must run before the Cavalier's opening blow";
	EXPECT_NE(std::find(firstVictims.begin(), firstVictims.end(), escortID), firstVictims.end())
		<< "The preemptive packet must actually include the allied escort";
	EXPECT_LT(cavalier->getAvailableHealth(), cavalierBefore);
	EXPECT_LT(escort->getAvailableHealth(), escortBefore);
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

TEST_F(AIIntegrationNativeBattle, homm3HydraValuesAndExecutesNativeMultiTargetMelee)
{
	// Pinned ai_tactical.cpp getMultiHeadBonus credits OTHER enemy stacks
	// reached from a legal attack hex. VCMI supplies the real attack area
	// and authoritative injury packets; this is not original damage parity.
	prepareBattle(CreatureID(110), 16, CreatureID(0), 1); // Hydra, Peasant
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * hydra = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(110))
			hydra = unit.get();
	ASSERT_NE(hydra, nullptr);
	ASSERT_TRUE(hydra->hasBonusOfType(BonusType::ATTACKS_ALL_ADJACENT));
	ASSERT_EQ(battle()->battleActiveUnit(), hydra);
	std::vector<BattleHex> adjacent;
	for(const auto & hex : hydra->getSurroundingHexes())
		if(hex.isAvailable() && battle()->battleGetUnitByPos(hex) == nullptr)
			adjacent.push_back(hex);
	ASSERT_GE(adjacent.size(), 2u);
	const CStack * first = addStack(BattleSide::DEFENDER, CreatureID(0), adjacent[0], 25);
	const CStack * second = addStack(BattleSide::DEFENDER, CreatureID(0), adjacent[1], 25);
	ASSERT_NE(first, nullptr);
	ASSERT_NE(second, nullptr);
	const int64_t firstBefore = first->getAvailableHealth();
	const int64_t secondBefore = second->getAvailableHealth();

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
	policy->activeStack(battleID, hydra);
	ASSERT_TRUE(callback->action.has_value());
	const auto & action = *callback->action;
	ASSERT_EQ(action.actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(action.target.size(), 2u);
	const BattleHex from = action.target.front().hexValue;
	const BattleHex attacked = action.target[1].hexValue;
	const auto potential = callback->getBattle(battleID)->getAttackedCreatures(hydra, attacked, false, from).first;
	EXPECT_NE(std::find(potential.begin(), potential.end(), first), potential.end())
		<< "HoMM3 should prefer a native attack footprint hitting both close defenders";
	EXPECT_NE(std::find(potential.begin(), potential.end(), second), potential.end())
		<< "HoMM3 must score the additional Hydra victim exactly once";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action))
		<< "Server rejected HoMM3's real multi-target melee action";
	EXPECT_LT(first->getAvailableHealth(), firstBefore);
	EXPECT_LT(second->getAvailableHealth(), secondBefore);
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_GT(recorder.damage, 0);
}


// Pinned HoMM3 getMultiHeadBonus prices one legal splash swing. VCMI's
// performAttackSequence may instead execute a second melee hit if the
// primary and attacker survive. This fixture records each actual server
// injury packet and guards against granting repeat collateral from a
// dead primary. It does not establish Heroes III retail policy parity.
TEST_F(AIIntegrationNativeBattle, homm3NativeRepeatedHydraSplashSurvivesPrimary)
{
	prepareBattle(CreatureID(110), 20, CreatureID(0), 1); // Hydra vs Peasant
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * hydra = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(110))
			hydra = unit.get();
	ASSERT_NE(hydra, nullptr);
	ASSERT_TRUE(hydra->hasBonusOfType(BonusType::ATTACKS_ALL_ADJACENT));
	hydra->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::ADDITIONAL_ATTACK,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(110))));
	ASSERT_EQ(hydra->getTotalAttacks(false), 2);
	ASSERT_EQ(battle()->battleActiveUnit(), hydra);

	std::vector<BattleHex> adjacent;
	for(const auto & hex : hydra->getSurroundingHexes())
		if(hex.isAvailable() && battle()->battleGetUnitByPos(hex) == nullptr)
			adjacent.push_back(hex);
	ASSERT_GE(adjacent.size(), 2u);
	const CStack * first = addStack(BattleSide::DEFENDER, CreatureID(0), adjacent[0], 8000);
	const CStack * second = addStack(BattleSide::DEFENDER, CreatureID(0), adjacent[1], 8000);
	ASSERT_NE(first, nullptr);
	ASSERT_NE(second, nullptr);
	const int64_t beforeFirst = first->getAvailableHealth();
	const int64_t beforeSecond = second->getAvailableHealth();

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
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
	policy->activeStack(battleID, hydra);
	ASSERT_TRUE(callback->action.has_value());
	const auto action = *callback->action;
	ASSERT_EQ(action.actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(action.target.size(), 2u);
	const auto from = action.target.front().hexValue;
	const auto aim = action.target[1].hexValue;
	const auto affected = view->getAttackedCreatures(hydra, aim, false, from).first;
	EXPECT_NE(std::find(affected.begin(), affected.end(), first), affected.end());
	EXPECT_NE(std::find(affected.begin(), affected.end(), second), affected.end());
	for(const auto * victim : {first, second})
	{
		BattleAttackInfo projected(hydra, victim, 0, false);
		projected.attackerPos = from;
		const auto estimate = view->battleEstimateDamage(projected);
		ASSERT_LT(estimate.damage.max * 2, victim->getAvailableHealth())
			<< "Both native Hydra swings must have a surviving bystander";
	}
	int nativeStrikes = 0;
	int firstInjuries = 0;
	int secondInjuries = 0;
	const auto hydraID = static_cast<ui32>(hydra->unitId());
	recorder.onAttackBeforeApply = [&](const BattleAttack & packet)
	{
		if(packet.stackAttacking != hydraID || packet.shot() || packet.counter())
			return;
		++nativeStrikes;
		for(const auto & injury : packet.bsa)
		{
			if(injury.stackAttacked == first->unitId())
				++firstInjuries;
			if(injury.stackAttacked == second->unitId())
				++secondInjuries;
		}
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action))
		<< "Native server rejected HoMM3's chosen repeated Hydra splash";
	recorder.onAttackBeforeApply = {};
	EXPECT_EQ(nativeStrikes, 2);
	EXPECT_EQ(firstInjuries, 2);
	EXPECT_EQ(secondInjuries, 2);
	EXPECT_LT(first->getAvailableHealth(), beforeFirst);
	EXPECT_LT(second->getAvailableHealth(), beforeSecond);
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// The original checkAdjacentHexes visits each occupied hex of a wide enemy.
// In VCMI an attacker may also have several legal aim hexes from one landing;
// the particular aim changes the engine's splash/breath footprint. This test
// validates those native alternatives and the selected action on a real server.
// It does not claim the original HoMM3 evaluates VCMI breath identically.
TEST_F(AIIntegrationNativeBattle, homm3WideEnemyMeleeAimIsNativeAndExecutes)
{

	// Keep the first breath melee nonterminal: a massed dragon stack can
	// eliminate the only defender during the directed action itself, before
	// the next server-run phase. This test must exercise both phases.
	prepareBattle(CreatureID(82), 5, CreatureID(4), 50); // Red Dragon (dungeon ID 82) vs Griffin
	ASSERT_FALSE(HasFatalFailure());

	battle()->obstacles.clear();
	const CStack * dragon = nullptr;
	CStack * griffin = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(82))
			dragon = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(4))
			griffin = unit.get();
	}
	ASSERT_NE(dragon, nullptr);
	ASSERT_NE(griffin, nullptr);
	ASSERT_TRUE(dragon->hasBonusOfType(BonusType::TWO_HEX_ATTACK_BREATH));
	ASSERT_TRUE(griffin->doubleWide());
	ASSERT_EQ(battle()->battleActiveUnit(), dragon);

	// A central, unobstructed wide target must be within the active dragon's
	// native first-turn reach, rather than relying on scripted movement.
	const BattleHex nearby( dragon->getPosition().toInt() + 7 );
	ASSERT_TRUE(nearby.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(nearby), nullptr);

	griffin->setPosition(nearby);
	ASSERT_EQ(battle()->battleGetUnitByPos(nearby), griffin);
	ASSERT_EQ(griffin->getHexes().size(), 2u);
	for(const auto & occupied : griffin->getHexes())
		ASSERT_TRUE(occupied.isAvailable());

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);

	const auto reach = view->getReachability(dragon);
	const auto available = view->battleGetAvailableHexes(reach, dragon, false);
	bool hasMultipleAims = false;
	for(const auto & hex : available)
	{
		if(!hex.isAvailable())
			continue;
		hasMultipleAims |= view->meleeAttackHexes(dragon, griffin, hex).size() > 1;
	}
	ASSERT_TRUE(hasMultipleAims)
		<< "A wide target must offer two real melee aim hexes at a legal landing";

	// The pinned checkAdjacentHexes visits each target footprint hex. Both
	// forecasts must be accepted by VCMI's REAL damage calculator, not just
	// by a source-shaped helper. The port now forwards the selected hex into
	// BattleAttackInfo (including the second-hit preview) without touching
	// the live defender while it evaluates candidate aims.
	int nativeAimsPreviewed = 0;
	const auto healthBeforePreview = griffin->getAvailableHealth();
	for(const auto & candidate : available)
	{
		if(!candidate.isAvailable())
			continue;
		const auto legal = view->meleeAttackHexes(dragon, griffin, candidate);
		if(legal.size() < 2)
			continue;
		for(const auto & hit : legal)
		{
			BattleAttackInfo forecast(dragon, griffin, reach.distances[candidate.toInt()], false);
			forecast.attackerPos = candidate;
			forecast.defenderPos = hit;
			const auto result = view->battleEstimateDamage(forecast);
			EXPECT_GT(result.damage.max, 0);
			EXPECT_EQ(griffin->getAvailableHealth(), healthBeforePreview)
				<< "Preview must never mutate the real defender";
			++nativeAimsPreviewed;
		}
		break;
	}
	ASSERT_GE(nativeAimsPreviewed, 2);

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

	policy->activeStack(battleID, dragon);
	ASSERT_TRUE(callback->action.has_value());
	const auto & action = *callback->action;
	ASSERT_EQ(action.actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(action.target.size(), 2u);
	const BattleHex from = action.target.front().hexValue;
	const BattleHex aimed = action.target[1].hexValue;
	// The source's checkAdjacentHexes evaluates BOTH future occupied cells
	// for enemy-attacks exposure. These are real VCMI damage forecasts
	// with the dragon still at its initial position, not fabricated values.
	// Neither hypothetical aim is allowed to change the living stacks.
	const auto landingCells = dragon->getHexes(from);
	ASSERT_EQ(landingCells.size(), 2u);
	const auto dragonHealthBeforeCounterPreviews = dragon->getAvailableHealth();
	const auto griffinHealthBeforeCounterPreviews = griffin->getAvailableHealth();
	for(const auto & cell : landingCells)
	{
		BattleAttackInfo counter(griffin, dragon, 0, false);
		counter.attackerPos = griffin->getPosition();
		counter.defenderPos = cell;
		const auto projected = view->battleEstimateDamage(counter);
		EXPECT_GT(projected.damage.max, 0)
			<< "Both head and tail must be independently forecastable by native VCMI";
		EXPECT_EQ(dragon->getAvailableHealth(), dragonHealthBeforeCounterPreviews);
		EXPECT_EQ(griffin->getAvailableHealth(), griffinHealthBeforeCounterPreviews);
	}
	const auto legalAims = view->meleeAttackHexes(dragon, griffin, from);
	EXPECT_NE(std::find(legalAims.begin(), legalAims.end(), aimed), legalAims.end())
		<< "HoMM3 must submit an engine-approved aim at the wide target";
	const int64_t before = griffin->getAvailableHealth();

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action))
		<< "Real VCMI game handler rejected the HoMM3 wide-target melee aim";
	EXPECT_LT(griffin->getAvailableHealth(), before);

	policy.reset();

	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");

	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, homm3MeleeEngagesLiveShooterAndCompletesBattle)
{
	// A live opposing shooter exercises the pinned getHexAttackValue branch
	// through native VCMI attack positions and damage previews. The source
	// uses a different per-creature combat-value calculation, so this is
	// engine-action evidence, NOT a claim of complete HoMM3 score parity.
	prepareBattle(CreatureID(0), 400, CreatureID(2), 15);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.meleeAttacks, 0)
		<< "The real server must accept melee attacks against the shooter";
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, homm3WideCavalierUsesNativeAttackGeometryAgainstShooter)
{
	// The pinned checkAdjacentHexes evaluates the attacker's second occupied
	// cell when considering a shooter pin. The geometric predicate is covered
	// by CI/source_tactics_test.cpp; this fixture additionally requires a real
	// two-hex stack, legal first engine melee attack and terminal battle result.
	// Native VCMI valuations do NOT prove exact decompiled HoMM3 decisions.
	prepareBattle(CreatureID(10), 12, CreatureID(2), 20);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * cavalier = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(10))
			cavalier = unit.get();
	ASSERT_NE(cavalier, nullptr);
	ASSERT_TRUE(cavalier->doubleWide());
	ASSERT_EQ(cavalier->getHexes().size(), 2u);
	ASSERT_EQ(battle()->battleActiveUnit(), cavalier);

	const BattleHex nearbyHex(cavalier->getPosition().toInt() + 5);
	ASSERT_TRUE(nearbyHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(nearbyHex), nullptr);
	const auto * closerShooter = addStack(BattleSide::DEFENDER, CreatureID(2), nearbyHex, 8);
	ASSERT_NE(closerShooter, nullptr);
	ASSERT_TRUE(closerShooter->isShooter());
	ASSERT_GT(BattleHex::getDistance(cavalier->getPosition(), nearbyHex), 1);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	// Pinned getHexAttackValue checks the shooter's ability to fire at
	// THIS active attacker, not just a generic legal shot in the battle.
	ASSERT_TRUE(view->battleCanShoot(closerShooter, cavalier->getPosition()))
		<< "This native shooter-pin fixture must exercise the target-specific source gate";
	const auto reach = view->getReachability(cavalier);
	const auto available = view->battleGetAvailableHexes(reach, cavalier, false);
	bool legalContact = false;
	for(const auto & candidate : available)
	{
		if(!candidate.isAvailable()
			|| view->meleeAttackHexes(cavalier, closerShooter, candidate).empty())
			continue;
		const auto footprint = cavalier->getHexes(candidate);
		for(const auto & ownHex : footprint)
			for(const auto & shooterHex : closerShooter->getHexes())
				legalContact |= BattleHex::getDistance(ownHex, shooterHex) == 1;
	}
	ASSERT_TRUE(legalContact) << "The server must expose a legal wide-stack attack position";

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
	policy->activeStack(battleID, cavalier);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK)
		<< "The independent HoMM3 policy must select legal native melee: "
		<< callback->action->toString();
	ASSERT_GE(callback->action->target.size(), 2u);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Real VCMI game handler rejected wide Cavalier's melee command";
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, homm3TwoEnemyThreatProjectsIntoNativeMeleeBattle)
{
	// A nearby melee enemy plus a ranged opponent exercise the landing
	// threat evaluation before the server accepts native commands. This
	// proves neither exact source threat values nor decision parity.
	prepareBattle(CreatureID(10), 24, CreatureID(2), 16);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * cavalier = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(10))
			cavalier = unit.get();
	ASSERT_NE(cavalier, nullptr);
	const BattleHex adjacentEnemy(cavalier->getPosition().toInt() + 5);
	ASSERT_TRUE(adjacentEnemy.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(adjacentEnemy), nullptr);
	ASSERT_NE(addStack(BattleSide::DEFENDER, CreatureID(0), adjacentEnemy, 6), nullptr);
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_GT(recorder.damage, 0);
}

// Distinct native hostile stacks must now contribute to the landing-risk
// approximation rather than being collapsed into the strongest ONE.
// An ordinary archer and two separate Peasant stacks provide three legal
// threats in this test. An independently chosen action must be accepted by
// the actual server and the fight must reach a terminal result. This does
// not establish an isolated position-choice delta or retail HoMM3 parity.
TEST_F(AIIntegrationNativeBattle, homm3IndependentEnemyThreatsCompleteNativeServerBattle)
{
	prepareBattle(CreatureID(10), 24, CreatureID(2), 16);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * cavalier = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(10))
			cavalier = unit.get();
	ASSERT_NE(cavalier, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), cavalier);
	const BattleHex first(cavalier->getPosition().toInt() + 5);
	const BattleHex second(cavalier->getPosition().toInt() + 3);
	ASSERT_TRUE(first.isAvailable());
	ASSERT_TRUE(second.isAvailable());
	ASSERT_NE(first, second);
	ASSERT_EQ(battle()->battleGetUnitByPos(first), nullptr);
	ASSERT_EQ(battle()->battleGetUnitByPos(second), nullptr);
	ASSERT_NE(addStack(BattleSide::DEFENDER, CreatureID(0), first, 6), nullptr);
	ASSERT_NE(addStack(BattleSide::DEFENDER, CreatureID(0), second, 6), nullptr);

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
	policy->activeStack(battleID, cavalier);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK)
		<< "The partial HoMM3 scorer must choose a real melee command";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The native game handler rejected HoMM3's independently chosen action";
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// Pinned ai_tactical.cpp::type_AI_spellcaster::findEnemyAttacks visits
// every enemy army, independent of the active stack's legal attack targets.
// An INVINCIBLE VCMI creature cannot be hit but can still threaten a landing
// hex. Exercise the independent HoMM3 choice with such an opponent, then
// remove this fixture-only bonus so the real battle can terminate. This does
// not isolate a change in chosen hex or imply retail threat-map parity.
TEST_F(AIIntegrationNativeBattle, homm3NonTargetableEnemyStillPresentDuringNativeThreatSelection)
{
	prepareBattle(CreatureID(10), 24, CreatureID(2), 16);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * cavalier = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(10))
			cavalier = unit.get();
	ASSERT_NE(cavalier, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), cavalier);

	const BattleHex near(cavalier->getPosition().toInt() + 5);
	ASSERT_TRUE(near.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(near), nullptr);
	const auto * invincibleOpponent = addStack(BattleSide::DEFENDER, CreatureID(0), near, 6);
	ASSERT_NE(invincibleOpponent, nullptr);
	auto * mutableOpponent = const_cast<CStack *>(invincibleOpponent);
	auto temporaryProtection = std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::INVINCIBLE, BonusSource::OTHER, 1, BonusSourceID());
	mutableOpponent->addNewBonus(temporaryProtection);
	ASSERT_TRUE(invincibleOpponent->isInvincible());

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	const auto opponents = view->battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY);
	ASSERT_NE(std::find(opponents.begin(), opponents.end(), invincibleOpponent), opponents.end())
		<< "The invincible opponent is still an enemy on the actual server";
	ASSERT_TRUE(view->battleCanAttackUnit(invincibleOpponent, cavalier))
		<< "Non-targetable opponent must retain native melee attack capability";
	const auto reach = view->getReachability(invincibleOpponent);
	ASSERT_FALSE(view->battleGetAvailableHexes(reach, invincibleOpponent, true).empty())
		<< "Opponent must have real native reachable attack origins";

	const auto chosen = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *cavalier);
	ASSERT_TRUE(chosen.actionType == EActionType::WALK_AND_ATTACK
		|| chosen.actionType == EActionType::WALK)
		<< "A remaining target must yield a native advancing action";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, chosen))
		<< "Real VCMI server rejected the independent HoMM3 action";

	// Test-only invincibility is removed after the checked first decision.
	// The genuine server then resolves the same armies to a terminal result.
	mutableOpponent->removeBonus(temporaryProtection);
	ASSERT_FALSE(invincibleOpponent->isInvincible());
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_FALSE(recorder.battleResults.empty());
	EXPECT_GT(recorder.damage, 0);
}

// Pinned getHexAttackValue does not filter hostile shooters by whether
// the active stack can damage them. Check this HoMM3-only census against a
// native INVINCIBLE archer, then resolve an actual VCMI server battle.
// This does not establish a decision difference or retail parity.
TEST_F(AIIntegrationNativeBattle, homm3NonTargetableShooterPinCensusExecutesNativeBattle)
{
	prepareBattle(CreatureID(10), 24, CreatureID(0), 20);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * cavalier = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(10))
			cavalier = unit.get();
	ASSERT_NE(cavalier, nullptr);
	ASSERT_TRUE(cavalier->doubleWide());
	ASSERT_EQ(battle()->battleActiveUnit(), cavalier);

	const BattleHex shooterHex(cavalier->getPosition().toInt() + 5);
	const BattleHex meleeHex(cavalier->getPosition().toInt() + 3);
	ASSERT_TRUE(shooterHex.isAvailable());
	ASSERT_TRUE(meleeHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(shooterHex), nullptr);
	ASSERT_EQ(battle()->battleGetUnitByPos(meleeHex), nullptr);
	const auto * shooter = addStack(BattleSide::DEFENDER, CreatureID(2), shooterHex, 12);
	const auto * meleeTarget = addStack(BattleSide::DEFENDER, CreatureID(0), meleeHex, 8);
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(meleeTarget, nullptr);
	auto * mutableShooter = const_cast<CStack *>(shooter);
	auto protection = std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::INVINCIBLE, BonusSource::OTHER, 1, BonusSourceID());
	mutableShooter->addNewBonus(protection);
	ASSERT_TRUE(shooter->isInvincible());

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	const auto opponents = view->battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY);
	ASSERT_NE(std::find(opponents.begin(), opponents.end(), shooter), opponents.end());
	ASSERT_TRUE(view->battleCanShoot(shooter, cavalier->getPosition()))
		<< "Non-targetable shooter must retain a real ranged threat";
	ASSERT_TRUE(view->battleCanAttackUnit(cavalier, meleeTarget));
	const auto reach = view->getReachability(cavalier);
	const auto available = view->battleGetAvailableHexes(reach, cavalier, false);
	bool legalMelee = false;
	for(const auto & from : available)
		if(from.isAvailable() && !view->meleeAttackHexes(cavalier, meleeTarget, from).empty())
			legalMelee = true;
	ASSERT_TRUE(legalMelee);
	const auto chosen = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *cavalier);
	ASSERT_EQ(chosen.actionType, EActionType::WALK_AND_ATTACK)
		<< "HoMM3 must independently select native melee: " << chosen.toString();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, chosen))
		<< "Native server rejected the HoMM3 decision";
	mutableShooter->removeBonus(protection);
	ASSERT_FALSE(shooter->isInvincible());
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_FALSE(recorder.battleResults.empty());
	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, homm3BlindEnemyDoesNotProjectAFalseLandingThreat)
{
	// Pinned HoMM3 findEnemyAttacks excludes incapacitated stacks. Apply
	// Blind through VCMI's real spell handler, then demand an independent
	// HoMM3 melee command accepted by the game server and a terminal fight.
	// This is not proof of equality with retail HoMM3's threat values.
	prepareBattle(CreatureID(10), 24, CreatureID(2), 16);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * cavalier = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(10))
			cavalier = unit.get();
	ASSERT_NE(cavalier, nullptr);
	const BattleHex adjacentHex(cavalier->getPosition().toInt() + 5);
	ASSERT_TRUE(adjacentHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(adjacentHex), nullptr);
	const auto * disabledEnemy = addStack(BattleSide::DEFENDER, CreatureID(0), adjacentHex, 6);
	ASSERT_NE(disabledEnemy, nullptr);
	teachBattleSpell(attackerSideHero, SpellID::BLIND);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::BLIND, disabledEnemy))
		<< "The real game server must apply Blind before policy evaluation";
	ASSERT_TRUE(disabledEnemy->hasBonusOfType(BonusType::NOT_ACTIVE));

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	ASSERT_EQ(battle()->battleActiveUnit(), cavalier);
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
	policy->activeStack(battleID, cavalier);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK)
		<< "The partial HoMM3 policy must independently choose native melee";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "VCMI rejected the HoMM3 decision with a genuinely blinded enemy";
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, homm3TacticsCompletesRealRangedBattle)
{
	prepareBattle(CreatureID(2), 20, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.shots, 0);
}

// Retail getRangedAttackValue forecasts one shot. The native Marksman fires
// twice, and SHOOTS_ALL_ADJACENT can injure a bystander on BOTH volleys.
// This integration case proves the partial HoMM3 policy selects the legal
// native shot, the server performs two distinct splash packets, and the battle
// terminates. The bonus combination is VCMI-specific, not retail parity.
TEST_F(AIIntegrationNativeBattle, homm3MultiVolleyAreaShotDamagesSecondaryOnBothNativeShots)
{
	prepareBattle(CreatureID(3), 12, CreatureID(6), 60); // Marksmen vs Swordsmen
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * marksmen = nullptr;
	CStack * swordsmen = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER)
			marksmen = unit.get();
		else
			swordsmen = unit.get();
	}
	ASSERT_NE(marksmen, nullptr);
	ASSERT_NE(swordsmen, nullptr);
	ASSERT_EQ(marksmen->getTotalAttacks(true), 2);
	ASSERT_GE(marksmen->shots.available(), 2);
	const BattleHex primaryPosition = swordsmen->getPosition();
	BattleHex adjacent;
	for(const auto & hex : primaryPosition.getNeighbouringTiles())
	{
		if(hex.isAvailable() && battle()->battleGetUnitByPos(hex) == nullptr)
		{
			adjacent = hex;
			break;
		}
	}
	ASSERT_TRUE(adjacent.isValid());
	CStack * neighbour = addStack(BattleSide::DEFENDER, CreatureID(6), adjacent, 60);
	ASSERT_NE(neighbour, nullptr);
	marksmen->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SHOOTS_ALL_ADJACENT, BonusSource::OTHER, 1, BonusSourceID()));
	battle()->activeStack = marksmen->unitId();

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanShoot(marksmen, swordsmen->getPosition()));
	ASSERT_TRUE(view->battleCanShoot(marksmen, neighbour->getPosition()));
	const auto action = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *marksmen);
	ASSERT_EQ(action.actionType, EActionType::SHOOT);
	ASSERT_FALSE(action.target.empty());
	const CStack * primary = action.target.front().unitValue == swordsmen->unitId() ? swordsmen : neighbour;
	const CStack * secondary = primary == swordsmen ? neighbour : swordsmen;
	ASSERT_EQ(action.target.front().unitValue, primary->unitId());
	const auto nativeAffected = view->getAttackedBattleUnits(
		marksmen, primary, primary->getPosition(), true);
	ASSERT_NE(std::find(nativeAffected.begin(), nativeAffected.end(), secondary), nativeAffected.end());
	const auto primaryHealth = primary->getAvailableHealth();
	const auto secondaryHealth = secondary->getAvailableHealth();
	BattleAttackInfo opening(marksmen, primary, 0, true);
	const auto estimate = view->battleEstimateDamage(opening);
	ASSERT_LT(estimate.damage.max * 2, primaryHealth)
		<< "The primary must survive long enough for both engine volleys";
	BattleAttackInfo splash(marksmen, secondary, 0, true);
	ASSERT_LT(view->battleEstimateDamage(splash).damage.max * 2, secondaryHealth);

	int volleys = 0;
	int splashVolleys = 0;
	const auto shooterID = static_cast<ui32>(marksmen->unitId());
	recorder.onAttackBeforeApply = [&](const BattleAttack & packet)
	{
		if(packet.stackAttacking != shooterID || !packet.shot() || packet.counter())
			return;
		++volleys;
		for(const auto & victim : packet.bsa)
			if(victim.stackAttacked == secondary->unitId())
			{
				++splashVolleys;
				break;
			}
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action))
		<< "Native VCMI server rejected the independently selected HoMM3 splash shot";
	recorder.onAttackBeforeApply = {};
	EXPECT_EQ(volleys, 2);
	EXPECT_EQ(splashVolleys, 2) << "Both native shots must damage the same surviving secondary stack";
	EXPECT_LT(primary->getAvailableHealth(), primaryHealth);
	EXPECT_LT(secondary->getAvailableHealth(), secondaryHealth);
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// A lethal first shot must stop the native multi-volley sequence, even
// when an additional enemy would survive a longer battle. Pinned HoMM3
// simulateAttack models one shot, while the VCMI adaptation replays later
// volleys only if their original target remains alive. Use the actual
// server's attack packets to prove the host-engine stopping rule.
TEST_F(AIIntegrationNativeBattle, homm3LethalFirstAreaShotStopsFurtherNativeVolleys)
{
	prepareBattle(CreatureID(3), 500, CreatureID(12), 1); // Marksmen vs Angel
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * marksmen = nullptr;
	CStack * angel = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER)
			marksmen = unit.get();
		else
			angel = unit.get();
	}
	ASSERT_NE(marksmen, nullptr);
	ASSERT_NE(angel, nullptr);
	ASSERT_EQ(marksmen->getTotalAttacks(true), 2);
	const BattleHex targetHex = angel->getPosition();
	BattleHex neighbour;
	for(const auto & hex : targetHex.getNeighbouringTiles())
	{
		if(hex.isAvailable() && battle()->battleGetUnitByPos(hex) == nullptr)
		{
			neighbour = hex;
			break;
		}
	}
	ASSERT_TRUE(neighbour.isValid());
	CStack * bystander = addStack(BattleSide::DEFENDER, CreatureID(0), neighbour, 2);
	ASSERT_NE(bystander, nullptr);
	// Keep one distant enemy alive after the lethal splash so the battle
	// remains active while the server finishes delivering its first action.
	// A terminal shot can destroy the test callback during its own call.
	BattleHex reserveHex;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex candidate(index);
		if(candidate.isAvailable() && battle()->battleGetUnitByPos(candidate) == nullptr
			&& BattleHex::getDistance(candidate, targetHex) >= 5
			&& BattleHex::getDistance(candidate, marksmen->getPosition()) >= 3)
		{
			reserveHex = candidate;
			break;
		}
	}
	ASSERT_TRUE(reserveHex.isValid());
	CStack * reserve = addStack(BattleSide::DEFENDER, CreatureID(0), reserveHex, 1);
	ASSERT_NE(reserve, nullptr);
	marksmen->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SHOOTS_ALL_ADJACENT, BonusSource::OTHER, 1, BonusSourceID()));
	battle()->activeStack = marksmen->unitId();

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanShoot(marksmen, angel->getPosition()));
	const auto affected = view->getAttackedBattleUnits(
		marksmen, angel, angel->getPosition(), true);
	ASSERT_NE(std::find(affected.begin(), affected.end(), bystander), affected.end())
		<< "The first shot must have a real second enemy in its native area";
	BattleAttackInfo firstShot(marksmen, angel, 0, true);
	ASSERT_GE(view->battleEstimateDamage(firstShot).damage.min, angel->getAvailableHealth());
	const auto bystanderHealth = bystander->getAvailableHealth();
	const auto action = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *marksmen);
	ASSERT_EQ(action.actionType, EActionType::SHOOT);
	ASSERT_FALSE(action.target.empty());
	ASSERT_EQ(action.target.front().unitValue, angel->unitId())
		<< "The valuable guaranteed-kill Angel must be the chosen first target";

	int volleys = 0;
	int splashVolleys = 0;
	const auto shooterID = static_cast<ui32>(marksmen->unitId());
	recorder.onAttackBeforeApply = [&](const BattleAttack & packet)
	{
		if(packet.stackAttacking != shooterID || !packet.shot() || packet.counter())
			return;
		++volleys;
		for(const auto & victim : packet.bsa)
			if(victim.stackAttacked == bystander->unitId())
			{
				++splashVolleys;
				break;
			}
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action))
		<< "VCMI rejected the independent HoMM3 lethal area shot";
	recorder.onAttackBeforeApply = {};
	EXPECT_EQ(volleys, 1) << "The second volley cannot target a killed Angel";
	EXPECT_EQ(splashVolleys, 1);
	EXPECT_LT(bystander->getAvailableHealth(), bystanderHealth);
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_FALSE(recorder.battleResults.empty());
}

// Vanilla HoMM3 getRangedAttackValue prices one target. The real VCMI
// SHOOTS_ALL_ADJACENT bonus adds a second signed effect: nearby enemy stacks
// increase the shot value and friendly casualties decrease it. Prove that
// HoMM3's independent choice changes and that the server accepts/damages the
// actual splash victims. This is an explicit VCMI extension, NOT parity with
// the retail HoMM3 shooter AI and not part of the TUM baseline.
TEST_F(AIIntegrationNativeBattle, homm3NativeAreaShotCollateralChangesTargetAndExecutes)
{
	prepareBattle(CreatureID(45), 30, CreatureID(0), 60);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * shooter = nullptr;
	const CStack * isolated = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(45))
			shooter = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			isolated = unit.get();
	}
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(isolated, nullptr);
	ASSERT_TRUE(shooter->isShooter());
	ASSERT_FALSE(shooter->hasBonusOfType(BonusType::SHOOTS_ALL_ADJACENT));
	BattleHex centerHex;
	std::vector<BattleHex> neighbours;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex candidate(index);
		if(!candidate.isAvailable() || candidate.getX() < 8 || candidate.getX() > 12
			|| battle()->battleGetUnitByPos(candidate) != nullptr
			|| BattleHex::getDistance(candidate, isolated->getPosition()) < 5)
			continue;
		std::vector<BattleHex> nearby;
		for(const auto & hex : candidate.getNeighbouringTiles())
			if(hex.isAvailable() && battle()->battleGetUnitByPos(hex) == nullptr
				&& BattleHex::getDistance(hex, isolated->getPosition()) >= 4)
				nearby.push_back(hex);
		if(nearby.size() >= 3)
		{
			centerHex = candidate;
			neighbours.assign(nearby.begin(), nearby.begin() + 3);
			break;
		}
	}
	ASSERT_TRUE(centerHex.isValid());
	std::vector<const CStack *> cluster;
	cluster.push_back(addStack(BattleSide::DEFENDER, CreatureID(0), centerHex, 1));
	for(const auto & hex : neighbours)
		cluster.push_back(addStack(BattleSide::DEFENDER, CreatureID(0), hex, 4));
	for(const auto * unit : cluster)
		ASSERT_NE(unit, nullptr);
	ASSERT_TRUE(battle()->battleCanShoot(shooter, isolated->getPosition()));
	ASSERT_TRUE(battle()->battleCanShoot(shooter, centerHex));
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	const auto before = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *shooter);
	ASSERT_EQ(before.actionType, EActionType::SHOOT);
	ASSERT_FALSE(before.target.empty());
	ASSERT_NE(before.target.front().unitValue, cluster.front()->unitId())
		<< "Without area damage the native single-target scorer should choose a larger stack, not the one-creature cluster center";

	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SHOOTS_ALL_ADJACENT, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(shooter->hasBonusOfType(BonusType::SHOOTS_ALL_ADJACENT));
	const auto after = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *shooter);
	ASSERT_EQ(after.actionType, EActionType::SHOOT);
	ASSERT_FALSE(after.target.empty());
	const auto chosenID = after.target.front().unitValue;
	ASSERT_NE(chosenID, before.target.front().unitValue)
		<< "Area damage must change HoMM3's independently chosen shot target";
	const auto chosen = std::find_if(cluster.begin(), cluster.end(),
		[&](const CStack * unit) { return unit->unitId() == chosenID; });
	ASSERT_NE(chosen, cluster.end())
		<< "Native area collateral must attract the independent HoMM3 shot";
	const auto splash = battle()->getAttackedBattleUnits(
		shooter, *chosen, (*chosen)->getPosition(), true);
	ASSERT_GE(splash.size(), 2u) << "The chosen native shot must have a genuine secondary victim";
	const CStack * secondary = nullptr;
	for(const auto * victim : splash)
		if(victim->unitId() != chosenID)
		{
			secondary = dynamic_cast<const CStack *>(victim);
			break;
		}
	ASSERT_NE(secondary, nullptr);
	const auto secondaryHealth = secondary->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, after))
		<< "Native server rejected independently selected HoMM3 area shot";
	EXPECT_LT(secondary->getAvailableHealth(), secondaryHealth)
		<< "The real battle must damage an additional splash victim";
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.shots, 0);
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// Ranged-target timing is a bounded native projection, not the full retail
// getAITargetTime graph. Exercise its stationary-unit host adaptation in a
// real server battle: a rooted melee opponent can still be shot, but cannot
// magically travel across the field during the target-value preview.
TEST_F(AIIntegrationNativeBattle, homm3RangedStationaryEnemyUsesNativeLegalShot)
{
	// Leave the defending Peasants alive after the first shot so the full
	// native server loop, not only the opening command, finishes the fight.
	prepareBattle(CreatureID(2), 8, CreatureID(0), 100);
	ASSERT_FALSE(HasFatalFailure());
	const CStack * archer = nullptr;
	CStack * grounded = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER)
			archer = unit.get();
		else
			grounded = unit.get();
	}
	ASSERT_NE(archer, nullptr);
	ASSERT_NE(grounded, nullptr);
	const_cast<CStack *>(grounded)->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::STACKS_SPEED,
		BonusSource::OTHER, -100, BonusSourceID()));
	ASSERT_EQ(std::max(0, static_cast<int>(grounded->getMovementRange())), 0);
	ASSERT_GT(BattleHex::getDistance(archer->getPosition(), grounded->getPosition()), 1);
	const BattleID battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_TRUE(view->battleCanShoot(archer, grounded->getPosition()));
	ASSERT_EQ(battle()->battleActiveUnit(), archer);
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
	policy->activeStack(battleID, archer);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Native VCMI rejected HoMM3's independent stationary-target shot";
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.shots, 0);
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// The source getRangedAttackValue checks cannotAttack() rather than
// 'can attack in melee'. Native Ballista cannot melee but can fire, even
// without movement. Verify that the predicate is grounded in a LIVE
// engine battle, and that HoMM3's own shot is server-accepted and the
// entire encounter reaches a terminal result. This does not prove retail
// HoMM3's complete target-selection equivalence.
TEST_F(AIIntegrationNativeBattle, homm3RangedOnlyWarMachineIsAnActiveNativeThreat)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	// The opening native Archer shot must not end the fight by wiping out
	// the only ordinary defender stack before runToCompletion can start.
	setArmy(attackerSideHero, CreatureID(2), 20); // Archers
	setArmy(defenderSideHero, CreatureID(0), 100); // Peasants
	giveArtifact(defenderSideHero, ArtifactID(ArtifactID::BALLISTA), ArtifactPosition::MACH1);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	battle()->obstacles.clear();

	const CStack * archer = nullptr;
	const CStack * ballista = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(2))
			archer = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID::BALLISTA)
			ballista = unit.get();
	}
	ASSERT_NE(archer, nullptr);
	ASSERT_NE(ballista, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), archer);
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_FALSE(view->battleCanAttackUnit(ballista, archer))
		<< "The Ballista must not be mistaken for a native melee attacker";
	ASSERT_TRUE(view->battleCanShoot(ballista, archer->getPosition()))
		<< "The Ballista must be able to make a real ranged attack";
	ASSERT_TRUE(AIIntegration::Ports::HoMM3::nativeTargetCanAttack(
		view->battleCanAttackUnit(ballista, archer),
		view->battleCanShoot(ballista, archer->getPosition())))
		<< "The HoMM3-only ranged-value adapter must recognize the native threat";

	const auto decision = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *archer);
	ASSERT_EQ(decision.actionType, EActionType::SHOOT)
		<< "The independent HoMM3 policy must choose a legal ranged action";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, decision))
		<< "The real VCMI server rejected HoMM3's selected shot";
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// Pinned getRangedAttackValue tests the enemy's globally chosen target, not
// merely whether it can shoot the currently active friendly stack. A limited-
// range enemy Archer can legally shoot another friendly while the active
// attacker remains too far away to be shot. The source-inspired HoMM3 adapter
// must preserve this live native shooting threat. Positions and bonus are
// fixture-only, and the actual HoMM3 shot is submitted to the real server.
TEST_F(AIIntegrationNativeBattle, homm3CountsEnemyShotAtAnotherFriendlyAndCompletesBattle)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(2), 30); // Active Archers
	setArmy(defenderSideHero, CreatureID(2), 45); // Limited-range Archers
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(attackerSideHero->id, SlotID(1)), CreatureID(0).toCreature(), 100));
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	battle()->obstacles.clear();

	CStack * attacker = nullptr;
	CStack * otherFriendly = nullptr;
	CStack * enemyShooter = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(2))
			attacker = unit.get();
		else if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			otherFriendly = unit.get();
		else if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(2))
			enemyShooter = unit.get();
	}
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(otherFriendly, nullptr);
	ASSERT_NE(enemyShooter, nullptr);
	battle()->activeStack = attacker->unitId();
	// The VCMI-specific range bonus does not change game rules.
	enemyShooter->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::LIMITED_SHOOTING_RANGE,
		BonusSource::OTHER, 3, BonusSourceID()));

	const BattleID battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanShoot(attacker, enemyShooter->getPosition()));
	ASSERT_FALSE(view->battleCanShoot(enemyShooter, attacker->getPosition()))
		<< "The limited-range Archer must be unable to return fire at the active stack";

	BattleHex alternate;
	for(int i = 0; i < GameConstants::BFIELD_SIZE; ++i)
	{
		const BattleHex candidate(i);
		if(!candidate.isAvailable() || battle()->battleGetUnitByPos(candidate))
			continue;
		const int distance = BattleHex::getDistance(candidate, enemyShooter->getPosition());
		if(distance < 2 || distance > 3)
			continue;
		otherFriendly->setPosition(candidate);
		if(view->battleCanShoot(enemyShooter, otherFriendly->getPosition()))
		{
			alternate = candidate;
			break;
		}
	}
	ASSERT_TRUE(alternate.isValid())
		<< "A live, legal nearby enemy shot must exist for the global target regression";
	ASSERT_TRUE(view->battleCanShoot(enemyShooter, otherFriendly->getPosition()));
	ASSERT_FALSE(view->battleCanShoot(enemyShooter, attacker->getPosition()));
	ASSERT_TRUE(AIIntegration::Ports::HoMM3::nativeTargetCanAttack(false, true))
		<< "The HoMM3 global threat must count the real shot at the other friendly";

	const auto decision = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *attacker);
	ASSERT_EQ(decision.actionType, EActionType::SHOOT);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, decision))
		<< "The native server rejected the independently chosen HoMM3 ranged action";
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.shots, 0);
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
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

TEST_F(AIIntegrationNativeBattle, homm3RangedDiscountsNonSpellInactiveTargetAndServerAcceptsShot)
{
	// Pinned ai_tactical.cpp getRangedAttackValue discounts stacks for which
	// army::isIncapacitated() is true. VCMI can carry NOT_ACTIVE from a
	// non-spell source, with the native Paralyze subtype. Inject this only into the fixture;
	// do not claim that a particular creature ability was exercised here.
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(2), 200);
	setArmy(defenderSideHero, CreatureID(0), 35);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(defenderSideHero->id, SlotID(1)), CreatureID(0).toCreature(), 14));
	ASSERT_FALSE(HasFatalFailure());
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);

	const CStack * shooter = nullptr;
	const CStack * disabled = nullptr;
	const CStack * active = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(2))
			shooter = unit.get();
		else if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
		{
			if(unit->getCount() == 35)
				disabled = unit.get();
			else if(unit->getCount() == 14)
				active = unit.get();
		}
	}
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(disabled, nullptr);
	ASSERT_NE(active, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	const auto before = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *shooter);
	ASSERT_EQ(before.actionType, EActionType::SHOOT);
	ASSERT_FALSE(before.target.empty());
	EXPECT_EQ(before.target.front().unitValue, disabled->unitId());
	const auto tumBefore = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *shooter, true);
	ASSERT_EQ(tumBefore.actionType, EActionType::SHOOT);
	ASSERT_FALSE(tumBefore.target.empty());

	const_cast<CStack *>(disabled)->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::NOT_ACTIVE, BonusSource::OTHER, 1,
		BonusSourceID(), BonusSubtypeID(SpellID(SpellID::PARALYZE))));
	ASSERT_TRUE(disabled->hasBonusOfType(BonusType::NOT_ACTIVE));
	ASSERT_TRUE(disabled->hasBonus(Selector::typeSubtype(BonusType::NOT_ACTIVE,
		BonusSubtypeID(SpellID(SpellID::PARALYZE)))));
	ASSERT_FALSE(disabled->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::BLIND)))));
	const auto after = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *shooter);
	ASSERT_EQ(after.actionType, EActionType::SHOOT);
	ASSERT_FALSE(after.target.empty());
	EXPECT_EQ(after.target.front().unitValue, active->unitId())
		<< "Non-spell NOT_ACTIVE must receive the source incapacitation penalty";
	const auto tumAfter = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *shooter, true);
	ASSERT_EQ(tumAfter.actionType, tumBefore.actionType);
	ASSERT_FALSE(tumAfter.target.empty());
	EXPECT_EQ(tumAfter.target.front().unitValue, tumBefore.target.front().unitValue)
		<< "The separate TUM baseline must not inherit this HoMM3-only scoring change";

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
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	ASSERT_FALSE(callback->action->target.empty());
	EXPECT_EQ(callback->action->target.front().unitValue, active->unitId());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The actual VCMI server rejected the independently chosen shot";
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.shots, 0);
	EXPECT_GT(recorder.damage, 0);
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
	EXPECT_EQ(callback->action->actionType, EActionType::NO_ACTION)
		<< "A sheltered wide castle guard without a legal source command must SKIP";
	runToCompletion(std::nullopt, false, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.catapultAttacks.empty());
}

// HoMM3OriginalTactics has no complete retail siege policy yet. Exercise the
// independent partial adapter on BOTH sides of a real, fortified VCMI battle.
// The server handles catapult turns itself; catapult packets are evidence of
// native siege progression, NOT a HoMM3-selected machine command. This is
// deliberately a completion regression, not a retail decision-parity claim.
TEST_F(AIIntegrationNativeBattle, homm3NativeAttackerAndDefenderCompleteFortifiedSiege)
{
	prepareSiege(false, CreatureID(0), 10);
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_EQ(battle()->battleGetGateState(), EGateState::CLOSED);
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics", "", true);
	EXPECT_FALSE(recorder.catapultAttacks.empty())
		<< "Native siege must resolve real server catapult packets";
	EXPECT_FALSE(recorder.battleResults.empty())
		<< "Both partial HoMM3 adapters must reach a native terminal result";
}

// The pinned getRangedAttackValue reads the enemy's goal time, not its
// geometrical separation. A real castle wall can prevent a fast Cavalier
// from reaching an archer even when their footprints are nearby. Assert the
// server's path differs from straight-line contact, then exercise both
// HoMM3OriginalTactics adapters through a terminal fortified siege.
// This validates native reachability and execution, not retail AI parity.
TEST_F(AIIntegrationNativeBattle, homm3RangedTargetTimeUsesNativeSiegeReachability)
{
	prepareSiege(true, CreatureID(0), 12);
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_EQ(battle()->battleGetGateState(), EGateState::CLOSED);
	const CStack * cavalry = nullptr;
	const CStack * garrisonArcher = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(!unit->alive())
			continue;
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(10))
			cavalry = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(2))
			garrisonArcher = unit.get();
	}
	ASSERT_NE(cavalry, nullptr);
	ASSERT_NE(garrisonArcher, nullptr);
	ASSERT_TRUE(battle()->battleIsInsideWalls(garrisonArcher->getPosition()));
	int geometricDistance = GameConstants::BFIELD_SIZE;
	for(const auto & a : cavalry->getHexes())
		for(const auto & b : garrisonArcher->getHexes())
			geometricDistance = std::min(geometricDistance, static_cast<int>(BattleHex::getDistance(a, b)));
	ASSERT_GT(geometricDistance, 0);
	const auto reach = battle()->getReachability(cavalry);
	const uint32_t pathSteps = reach.distToNearestNeighbour(cavalry, garrisonArcher);
	EXPECT_GT(pathSteps, static_cast<uint32_t>(geometricDistance - 1))
		<< "Closed fortifications must not be valued as unobstructed hex distance";
	// HoMM3OriginalTactics now uses these exact native reachable origins
	// when projecting a melee enemy's next-turn landing threat. Prove the
	// real cavalry cannot attack the sheltered archer through closed walls
	// even though straight-line hex distance is much shorter.
	const auto attackOrigins = battle()->battleGetAvailableHexes(reach, cavalry, true);
	ASSERT_FALSE(attackOrigins.empty());
	EXPECT_TRUE(std::none_of(attackOrigins.begin(), attackOrigins.end(),
		[&](BattleHex origin)
		{
			return !battle()->meleeAttackHexes(
				cavalry, garrisonArcher, origin, garrisonArcher->getPosition()).empty();
		})) << "Blocked native melee paths must not become projected enemy attacks";
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics", "", true);
	EXPECT_FALSE(recorder.battleResults.empty());
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

TEST_F(AIIntegrationNativeBattle, tumSiegeGuardStopsHoldingWhenHypnotizedGarrisonBreachesFromInside)
{
	// TUM's siege hold is justified only when the enemy is outside intact
	// fortifications. A real Hypnotize can transfer an archer still standing
	// behind the walls to the attacking player without changing unitSide().
	prepareSiege(true, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_EQ(battle()->battleGetGateState(), EGateState::CLOSED);
	const CStack * guard = nullptr;
	const CStack * archer = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(!unit->alive() || unit->unitSide() != BattleSide::DEFENDER)
			continue;
		if(unit->creatureId() == CreatureID(0))
			guard = unit.get();
		else if(unit->creatureId() == CreatureID(2))
			archer = unit.get();
	}
	ASSERT_NE(guard, nullptr);
	ASSERT_NE(archer, nullptr);
	ASSERT_TRUE(battle()->battleIsInsideWalls(guard->getPosition()));
	ASSERT_TRUE(battle()->battleIsInsideWalls(archer->getPosition()));

	const auto battleID = battle()->getBattleID();
	const auto defender = battle()->sideToPlayer(BattleSide::DEFENDER);
	const auto attacker = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(defender);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	auto policy = AIFactory::createBattleAI("TUMEnchantedTactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = defender;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(defender), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::DEFENDER, false);
	policy->activeStack(battleID, guard);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::DEFEND)
		<< "The genuine closed-gate siege must initially justify the TUM hold";
	callback->action.reset();

	teachBattleSpell(attackerSideHero, SpellID::HYPNOTIZE, 30);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::HYPNOTIZE, archer))
		<< "The attacker must capture the interior garrison through a native spell";
	ASSERT_TRUE(archer->isHypnotized());
	ASSERT_EQ(view->battleGetOwner(archer), attacker);
	ASSERT_EQ(view->battleGetOwner(guard), defender);
	ASSERT_EQ(archer->unitSide(), BattleSide::DEFENDER)
		<< "The engine must retain the original side while transferring control";
	ASSERT_TRUE(view->battleIsInsideWalls(archer->getPosition()));
	const auto staleEnemies = view->battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY);
	ASSERT_TRUE(std::none_of(staleEnemies.begin(), staleEnemies.end(),
		[&](const CStack * enemy) { return enemy->unitId() == archer->unitId(); }))
		<< "This fixture must exercise the original-side enemy-list mismatch";

	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *guard);
	ASSERT_NE(baseline.actionType, EActionType::DEFEND)
		<< "The explicit partial HoMM3 baseline must distinguish the broken siege hold";
	policy->activeStack(battleID, guard);
	ASSERT_TRUE(callback->action.has_value());
	EXPECT_EQ(callback->action->actionType, baseline.actionType)
		<< "An interior controlled enemy must disable TUM's siege-only DEFEND override";
	std::cout << "AI_TUM_SIEGE_HYPNOTIZE homm3=" << baseline.toString()
		<< " tum=" << callback->action->toString() << '\n';

	battle()->activeStack = guard->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, defender, *callback->action))
		<< "Native VCMI refused the post-Hypnotize TUM siege action";
	policy.reset();
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.damage, 0);
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
// Hypnotize changes the current controller but does not change every native
// legality rule's use of ORIGINAL battle sides. With both armies captured,
// the engine currently cannot SHOOT an original ally even when hostile now.
// Preserve the partial HoMM3 DEFEND rather than inventing a cross-side shot.
TEST_F(AIIntegrationNativeBattle, tumDoubleHypnotizeRespectsNativeOriginalSideShotGate)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(2), 5);
	setArmy(defenderSideHero, CreatureID(2), 5);
	teachBattleSpell(attackerSideHero, SpellID::HYPNOTIZE, 30);
	teachBattleSpell(defenderSideHero, SpellID::HYPNOTIZE, 30);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);

	const CStack * formerAttacker = nullptr;
	const CStack * formerDefender = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->creatureId() != CreatureID(2))
			continue;
		if(unit->unitSide() == BattleSide::ATTACKER)
			formerAttacker = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER)
			formerDefender = unit.get();
	}
	ASSERT_NE(formerAttacker, nullptr);
	ASSERT_NE(formerDefender, nullptr);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::HYPNOTIZE, formerDefender));
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::HYPNOTIZE, formerAttacker));
	ASSERT_TRUE(formerAttacker->isHypnotized());
	ASSERT_TRUE(formerDefender->isHypnotized());

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	ASSERT_EQ(battle()->battleGetOwner(formerDefender), player);
	ASSERT_NE(battle()->battleGetOwner(formerAttacker), player);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	const auto originalEnemies = view->battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY);
	const auto originalFriends = view->battleGetStacks(CBattleInfoEssentials::ONLY_MINE);
	ASSERT_TRUE(std::any_of(originalEnemies.begin(), originalEnemies.end(),
		[&](const CStack * unit) { return unit->unitId() == formerDefender->unitId(); }));
	ASSERT_TRUE(std::any_of(originalFriends.begin(), originalFriends.end(),
		[&](const CStack * unit) { return unit->unitId() == formerAttacker->unitId(); }));
	ASSERT_FALSE(view->battleCanShoot(formerDefender, formerAttacker->getPosition()))
		<< "Native VCMI still forbids a shot at an original ally despite changed control";
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *formerDefender);

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
	policy->activeStack(battleID, formerDefender);
	ASSERT_TRUE(callback->action.has_value());
	EXPECT_EQ(callback->action->toString(), baseline.toString())
		<< "When no original-side attack is legal TUM preserves the partial HoMM3 baseline";
	EXPECT_EQ(callback->action->actionType, EActionType::DEFEND);
	std::cout << "AI_TUM_DOUBLE_HYPNOTIZE_NATIVE_GATE homm3=" << baseline.toString()
		<< " tum=" << callback->action->toString() << '\n';

	battle()->activeStack = formerDefender->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The real server should accept the legal TUM fallback DEFEND";
	policy.reset();
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.damage, 0);
}

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
	// The manually accepted Hypnotize breath strike may decide the exchange
	// before the opposing policy receives a turn. The independent HoMM3
	// first decision was already sampled above; continuation here proves a
	// genuine terminal server result, not a mixed-policy head-to-head duel.
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics", "TUMEnchantedTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_GT(recorder.damage, 0);
}

// TUM Hypnotize: a survivable FIRST_STRIKE counters before the breath
// still hits two former allies. Creature bonuses are test-only fixtures;
// independent HoMM3 and TUM decisions use the same native battlefield.
TEST_F(AIIntegrationNativeBattle, tumHypnotizedBreathAfterSurvivableFirstStrike)
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
	auto * front = addStack(BattleSide::ATTACKER, CreatureID(0), frontHex, 6);
	const auto * rear = addStack(BattleSide::ATTACKER, CreatureID(0), rearHex, 1);
	ASSERT_NE(front, nullptr);
	ASSERT_NE(rear, nullptr);
	guard->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::TWO_HEX_ATTACK_BREATH,
		BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(guard->hasBonusOfType(BonusType::TWO_HEX_ATTACK_BREATH));
	front->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::FIRST_STRIKE, BonusSource::CREATURE_ABILITY,
		1, BonusSourceID(CreatureID(0)), BonusSubtypeID(BonusCustomSubtype::damageTypeMelee)));
	ASSERT_TRUE(front->ableToRetaliate());
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
	BattleAttackInfo preemptive(front, guard, 0, false);
	preemptive.attackerPos = frontHex;
	preemptive.defenderPos = origin;
	const auto counter = view->battleEstimateDamage(preemptive, nullptr);
	ASSERT_GT(counter.damage.min, 0);
	ASSERT_LT(counter.damage.max, guard->getAvailableHealth())
		<< "Fixture requires a nonfatal native FIRST_STRIKE";
	auto survivors = guard->acquireState();
	int64_t worstCounterDamage = counter.damage.max;
	survivors->damage(worstCounterDamage);
	ASSERT_TRUE(survivors->alive());
	BattleAttackInfo postCounterBreath(survivors.get(), rear, 0, false);
	postCounterBreath.attackerPos = origin;
	postCounterBreath.defenderPos = rearHex;
	ASSERT_GT(view->battleEstimateDamage(postCounterBreath, nullptr).damage.min, 0);
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
	EXPECT_EQ(aimed, frontHex)
		<< "TUM should value the surviving extra breath victim";
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
	const int guardId = guard->unitId();
	std::vector<int> nativeAttackOrder;
	std::set<int> nativeHitIds;
	recorder.onAttackBeforeApply = [&](const BattleAttack & attack)
	{
		if(attack.stackAttacking == frontId && attack.counter())
			nativeAttackOrder.push_back(frontId);
		else if(attack.stackAttacking == guardId)
			nativeAttackOrder.push_back(guardId);
		for(const auto & injury : attack.bsa)
			if(injury.damageAmount > 0
				&& (injury.stackAttacked == frontId || injury.stackAttacked == rearId))
				nativeHitIds.insert(injury.stackAttacked);
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Authoritative VCMI rejected the independent TUM multihex melee attack";
	recorder.onAttackBeforeApply = {};
	ASSERT_GE(nativeAttackOrder.size(), 2u);
	EXPECT_EQ(nativeAttackOrder[0], frontId);
	EXPECT_EQ(nativeAttackOrder[1], guardId);
	EXPECT_EQ(nativeHitIds.size(), 2u)
		<< "Real server damage must include both the primary and secondary former ally";
	policy.reset();
	// The manually accepted Hypnotize breath strike may decide the exchange
	// before the opposing policy receives a turn. The independent HoMM3
	// first decision was already sampled above; continuation here proves a
	// genuine terminal server result, not a mixed-policy head-to-head duel.
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics", "TUMEnchantedTactics");
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

// Native VCMI regression for survivor-aware Hypnotize ranged splash.
// This is a bounded engine adaptation, not equivalence to TUM's closed ERA/DLL.
TEST_F(AIIntegrationNativeBattle, tumHypnotizedFirstStrikePricesNativeRangedSplashAfterCounter)
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
	ASSERT_EQ(BattleHex::getDistance(normalHex, firstHex), 1);
	// Native adjacent splash from the hypnotized stack must strike
	// the ordinary shooter beside the early-countering first striker.
	hypnotized->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::SHOOTS_ALL_ADJACENT,
		BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(hypnotized->hasBonusOfType(BonusType::SHOOTS_ALL_ADJACENT));

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
	const auto impacted = view->getAttackedBattleUnits(
		hypnotized, firstStriker, firstStriker->getPosition(), true);
	ASSERT_TRUE(std::any_of(impacted.begin(), impacted.end(), [&](const battle::Unit * hit)
	{
		return hit && hit->unitId() == ordinary->unitId();
	})) << "The actual VCMI ranged shot must include the adjacent original ally";
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
	// Independently price collateral from the post-FIRST_STRIKE survivors:
	// the real server cannot fire this splash at full pre-counter strength.
	const auto estimateCollateral = [&](const battle::Unit & attacker)
	{
		BattleAttackInfo splash(&attacker, ordinary, 0, true);
		splash.attackerPos = hypnotized->getPosition();
		splash.defenderPos = ordinary->getPosition();
		const auto estimate = view->battleEstimateDamage(splash, nullptr);
		return (static_cast<double>(estimate.damage.min) + estimate.damage.max) / 2.0;
	};
	const auto collateralAfterCounter = [&](int64_t damage)
	{
		auto survivors = hypnotized->acquireState();
		survivors->damage(damage);
		return survivors->alive() ? estimateCollateral(*survivors) : 0.0;
	};
	const double collateralBeforeCounter = estimateCollateral(*hypnotized);
	const double collateralAfter = (collateralAfterCounter(fullCounterDamage.damage.min)
		+ collateralAfterCounter(fullCounterDamage.damage.max)) / 2.0;
	ASSERT_LT(collateralAfter, collateralBeforeCounter);
	ASSERT_GT(collateralAfter, 0.0);
	const auto ordinaryHealthBefore = ordinary->getAvailableHealth();
	const double preCounterShot = (static_cast<double>(firstDamage.damage.min)
		+ firstDamage.damage.max) / 2.0;
	ASSERT_LT(postCounterShot, preCounterShot)
		<< "First-strike casualties must reduce TUM's outgoing shot projection";
	EXPECT_EQ(hypnotized->getAvailableHealth(), hypnotizedHealth);
	EXPECT_EQ(firstStriker->getAvailableHealth(), originalHealth);

	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *hypnotized);
	ASSERT_EQ(baseline.actionType, EActionType::SHOOT);
	ASSERT_FALSE(baseline.target.empty());
	// The partial HoMM3 baseline may also value native splash; no forced
	// divergence or exact unpublished TUM equivalence is claimed.
	std::cout << "AI_TUM_HYPNOTIZE_RANGED_SPLASH baseline=" << baseline.toString()
		<< " collateral_before=" << collateralBeforeCounter
		<< " collateral_after=" << collateralAfter << '\n';
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
	EXPECT_LT(ordinary->getAvailableHealth(), ordinaryHealthBefore)
		<< "The real server must damage the secondary victim after FIRST_STRIKE";
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

// The authoritative VCMI shooter eligibility query still uses a stack's
// ORIGINAL battle side. After a real Hypnotize, a tower can therefore receive
// a nominally legal shot against a former enemy now controlled by its owner.
// TUM's partial native tower projection must respect the CURRENT owner when
// choosing among those engine-legal targets. This regression proves a genuine
// spell cast, a separately computed HoMM3 proposal, an accepted TUM tower
// shot and a completed server siege. This does not establish an HoMM3
// differential or the closed ERA/DLL's target formula.
TEST_F(AIIntegrationNativeBattle, tumCitadelTowerRespectsRealHypnotizeOwnership)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(4), 5);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(attackerSideHero->id, SlotID(1)), CreatureID(0).toCreature(), 15));
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
		StackLocation(town->id, SlotID(0)), CreatureID(0).toCreature(), 80));
	teachBattleSpell(defenderSideHero, SpellID::HYPNOTIZE, 30);
	const auto layout = BattleLayout::createDefaultLayout(*gameState(), attackerSideHero, town);
	gameHandler->battles->startBattle(attackerSideHero, town, town->visitablePos(),
		attackerSideHero, defenderSideHero, layout, town);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);

	const CStack * tower = nullptr;
	const CStack * controlled = nullptr;
	const CStack * actualEnemy = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->alive() && unit->unitSide() == BattleSide::DEFENDER && unit->isTurret())
			tower = unit.get();
		else if(unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(0)
			&& unit->creatureId() == CreatureID(4))
			controlled = unit.get();
		else if(unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(1)
			&& unit->creatureId() == CreatureID(0))
			actualEnemy = unit.get();
	}
	ASSERT_NE(tower, nullptr);
	ASSERT_NE(controlled, nullptr);
	ASSERT_NE(actualEnemy, nullptr);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::HYPNOTIZE, controlled))
		<< "A real defending hero must cast Hypnotize in this native siege";
	ASSERT_TRUE(controlled->isHypnotized());
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::DEFENDER);
	ASSERT_EQ(battle()->battleGetOwner(controlled), player);
	ASSERT_NE(battle()->battleGetOwner(actualEnemy), player);

	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanShoot(tower, controlled->getPosition()))
		<< "Native shot legality deliberately preserves the former enemy side";
	ASSERT_TRUE(view->battleCanShoot(tower, actualEnemy->getPosition()));
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *tower);
	ASSERT_EQ(baseline.actionType, EActionType::SHOOT);
	ASSERT_FALSE(baseline.target.empty());
	// The partial HoMM3 baseline can independently prefer the same legal target.
	// Equality here is not evidence of an upstream TUM-vs-HoMM3 differential.

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
	EXPECT_EQ(callback->action->target.front().unitValue, actualEnemy->unitId())
		<< "Tower must not shoot an original attacker now controlled by the defender";
	EXPECT_NE(callback->action->target.front().unitValue, controlled->unitId())
		<< "TUM tower must not select the currently allied, Hypnotized unit";
	std::cout << "AI_TUM_TOWER_HYPNOTIZE homm3=" << baseline.toString()
		<< " tum=" << callback->action->toString() << '\n';

	const auto beforeHealth = actualEnemy->getAvailableHealth();
	const int beforeShots = recorder.shots;
	battle()->activeStack = tower->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Native server rejected the independent TUM tower shot";
	EXPECT_GT(recorder.shots, beforeShots);
	EXPECT_LT(actualEnemy->getAvailableHealth(), beforeHealth);
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
// Native VCMI first-strike retaliation can have a damage interval that
// straddles a war machine's remaining health. A fatal high roll prevents its
// volley, while a survivable low roll still permits an attack. The closed TUM
// ballista formula is unavailable; this fixture checks only that our native
// casualty projection uses both outcomes without changing any live stack.
// No fake action is credited: the chosen shot must pass the real server gate
// and the complete battle must terminate.
TEST_F(AIIntegrationNativeBattle, tumBallistaPricesSurvivalOfVariableFirstStrikeCounter)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(11), 80);
	setArmy(defenderSideHero, CreatureID(2), 200);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(defenderSideHero->id, SlotID(1)), CreatureID(0).toCreature(), 150));
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
	ASSERT_TRUE(view->battleCanShoot(firstStriker, ballista->getPosition()));
	ASSERT_TRUE(view->battleCanShoot(ballista, safeTarget->getPosition()));
	ASSERT_TRUE(firstStriker->ableToRetaliate());

	BattleAttackInfo preemptive(firstStriker, ballista, 0, true);
	preemptive.attackerPos = firstStriker->getPosition();
	preemptive.defenderPos = ballista->getPosition();
	const auto counter = view->battleEstimateDamage(preemptive, nullptr);
	const auto originalHealth = ballista->getAvailableHealth();
	ASSERT_GT(counter.damage.min, 0);
	ASSERT_LT(counter.damage.min, originalHealth)
		<< "The low native FIRST_STRIKE roll must leave the machine alive";
	ASSERT_GE(counter.damage.max, originalHealth)
		<< "The high native FIRST_STRIKE roll must destroy the machine";

	const int heroAttacks = attackerSideHero->valOfBonuses(
		BonusType::HERO_GRANTS_ATTACKS, BonusSubtypeID(ballista->creatureId()));
	const int legalShots = std::min(ballista->shots.available(),
		ballista->getTotalAttacks(true) + heroAttacks);
	ASSERT_GT(legalShots, 0);
	const auto lossFrom = [&](const battle::Unit & attacker, const CStack & victim)
	{
		BattleAttackInfo attack(&attacker, &victim, 0, true);
		attack.attackerPos = ballista->getPosition();
		attack.defenderPos = victim.getPosition();
		const auto hit = view->battleEstimateDamage(attack, nullptr);
		const double perShot = std::max(0.0,
			(static_cast<double>(hit.damage.min) + hit.damage.max) / 2.0);
		const double health = static_cast<double>(victim.getAvailableHealth());
		return static_cast<double>(victim.estimateCombatValue())
			* std::min(health, perShot * legalShots) / health;
	};
	const double fullStrength = lossFrom(*ballista, *firstStriker);
	const auto survivingLoss = [&](int64_t suffered)
	{
		auto survivors = ballista->acquireState();
		survivors->damage(suffered);
		return survivors->alive() ? lossFrom(*survivors, *firstStriker) : 0.0;
	};
	const double corrected = (survivingLoss(counter.damage.min)
		+ survivingLoss(counter.damage.max)) / 2.0;
	ASSERT_GT(fullStrength, corrected)
		<< "The missing fatal high roll must reduce projected siege casualties";
	const double safeLoss = lossFrom(*ballista, *safeTarget);
	const CStack * expected = corrected > safeLoss ? firstStriker : safeTarget;
	const auto homm3 = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *ballista);

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
		<< "TUM's native volley valuation must account for pre-shot death";
	EXPECT_EQ(ballista->getAvailableHealth(), originalHealth);
	std::cout << "AI_TUM_FIRST_STRIKE_VARIABLE homm3=" << homm3.toString()
		<< " tum=" << callback->action->toString()
		<< " native_counter_min=" << counter.damage.min
		<< " native_counter_max=" << counter.damage.max << '\n';

	battle()->activeStack = ballista->unitId();
	const int shotEventsBefore = recorder.shots;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action));
	EXPECT_GT(recorder.shots, shotEventsBefore)
		<< "Native engine must execute the accepted attack or its first-strike counter";
	policy.reset();
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.damage, 0);
}

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

// The battlefield's side-based enemy list may still contain a defender
// temporarily controlled by our hero's genuine Hypnotize spell. A TUM
// manually controlled ballista must use current native ownership, not that
// unit's original side, when choosing a shot. Unlike the hidden ERA/DLL,
// this is a verifiable VCMI action-legality invariant.
TEST_F(AIIntegrationNativeBattle, tumBallistaCannotTargetHypnotizedFriendlyFormerEnemy)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 70);
	setArmy(defenderSideHero, CreatureID(0), 30);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(defenderSideHero->id, SlotID(1)), CreatureID(0).toCreature(), 15));
	giveArtifact(attackerSideHero, ArtifactID(ArtifactID::BALLISTA), ArtifactPosition::MACH1);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::ARTILLERY),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	// Keep the controlled stack within the native Hypnotize health limit;
	// 80 Peasants exceed the verified basic-mastery cast threshold.
	teachBattleSpell(attackerSideHero, SpellID::HYPNOTIZE, 30);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);

	const CStack * ballista = nullptr;
	const CStack * controlled = nullptr;
	const CStack * actualEnemy = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->isBallista())
			ballista = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
		{
			if(unit->getCount() == 30)
				controlled = unit.get();
			else if(unit->getCount() == 15)
				actualEnemy = unit.get();
		}
	}
	ASSERT_NE(ballista, nullptr);
	ASSERT_NE(controlled, nullptr);
	ASSERT_NE(actualEnemy, nullptr);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::HYPNOTIZE, controlled))
		<< "A real hero spell must change control, not merely add a fixture flag";
	ASSERT_TRUE(controlled->isHypnotized());
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	ASSERT_EQ(battle()->battleGetOwner(controlled), player);
	ASSERT_NE(battle()->battleGetOwner(actualEnemy), player);

	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanShoot(ballista, actualEnemy->getPosition()));
	ASSERT_TRUE(view->battleCanShoot(ballista, controlled->getPosition()))
		<< "Engine legal-shot query still uses the target's original battle side; TUM must add an ownership veto";
	const auto visibleEnemies = view->battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY);
	const bool formerEnemyInList = std::any_of(visibleEnemies.begin(), visibleEnemies.end(),
		[&](const CStack * enemy) { return enemy->unitId() == controlled->unitId(); });
	ASSERT_TRUE(formerEnemyInList)
		<< "The regression must exercise the native side-based target list despite Hypnotize ownership";
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
	EXPECT_EQ(callback->action->target.front().unitValue, actualEnemy->unitId())
		<< "A TUM war machine must not shoot its current Hypnotized ally";
	std::cout << "AI_TUM_HYPNOTIZED_BALLISTA former_enemy_listed=" << formerEnemyInList
		<< " baseline=" << baseline.toString() << " tum=" << callback->action->toString() << '\n';

	const auto enemyHealthBefore = actualEnemy->getAvailableHealth();
	const int shotsBefore = recorder.shots;
	battle()->activeStack = ballista->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "VCMI server rejected the real ballista shot at the remaining hostile stack";
	EXPECT_GT(recorder.shots, shotsBefore);
	EXPECT_LT(actualEnemy->getAvailableHealth(), enemyHealthBefore);
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
	// In addition to the differential proposal above, verify the SAME
	// melee stack is issued WAIT by the live engine. A player-level WAIT
	// counter alone could pass because a different friendly unit waited.
	// Capture the ID before BattleResult removes the authoritative stack.
	const int screeningMeleeId = melee->unitId();
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(lastAcceptedActionTypesByUnit[screeningMeleeId][EActionType::WAIT], 0)
		<< "The actual screening melee must WAIT on the server, not only in a probe";
	ASSERT_TRUE(lastFirstAcceptedActionTypeByUnit.contains(screeningMeleeId));
	EXPECT_EQ(lastFirstAcceptedActionTypeByUnit.at(screeningMeleeId), EActionType::WAIT)
		<< "The first authoritative melee decision must differ from HoMM3's approach";
	EXPECT_GT(recorder.shots, 0);
}



// A native double-wide neutral still exposes two independent aimed cells.
// A fixture-only Paralyze status isolates the shooter-support rule from the
// next-turn reachability check. The boundary with head blocked and tail
// legal is explicitly covered by the separate source predicate contract.
TEST_F(AIIntegrationNativeBattle, tumWideNeutralShooterScreenUsesNativeFootprint)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 60);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(attackerSideHero->id, SlotID(1)), CreatureID(2).toCreature(), 25));
	gameHandler->createWanderingMonster({9, 9, 0}, CreatureID(102), 1);
	const auto * neutral = findFirst<CGCreature>();
	ASSERT_NE(neutral, nullptr);
	gameHandler->battles->startBattle(attackerSideHero, neutral);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	battle()->obstacles.clear();
	const CStack * melee = nullptr;
	const CStack * archer = nullptr;
	CStack * wideEnemy = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER)
		{
			if(unit->creatureId() == CreatureID(0))
				melee = unit.get();
			else if(unit->creatureId() == CreatureID(2))
				archer = unit.get();
		}
		else if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(102))
			wideEnemy = unit.get();
	}
	ASSERT_NE(melee, nullptr);
	ASSERT_NE(archer, nullptr);
	ASSERT_NE(wideEnemy, nullptr);
	ASSERT_EQ(wideEnemy->getHexes().size(), 2u);
	ASSERT_FALSE(wideEnemy->isShooter());
	wideEnemy->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::NOT_ACTIVE, BonusSource::CREATURE_ABILITY,
		1, BonusSourceID(CreatureID(102)), BonusSubtypeID(SpellID(SpellID::PARALYZE))));
	ASSERT_TRUE(wideEnemy->hasBonusOfType(BonusType::NOT_ACTIVE));

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	ASSERT_EQ(battle()->sideToPlayer(BattleSide::DEFENDER), PlayerColor::NEUTRAL);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	const auto occupied = wideEnemy->getHexes();
	const bool headShot = view->battleCanShoot(archer, occupied[0]);
	const bool tailShot = view->battleCanShoot(archer, occupied[1]);
	ASSERT_TRUE(headShot || tailShot) << "Native wide neutral must expose a legal archer shot";
	ASSERT_TRUE(AIIntegration::Ports::TUM::shooterCanHitAnyOccupiedCell(occupied,
		[&](const BattleHex & hex) { return view->battleCanShoot(archer, hex); }));
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
	EXPECT_NE(callback->action->toString(), baseline.toString());
	// Record the exact protected archer before BattleResult releases the
	// native unit. An arbitrary SHOOT from another stack must not count
	// as evidence that TUM's melee WAIT enabled this firing line.
	const int protectedArcherId = archer->unitId();
	battle()->activeStack = melee->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Server rejected TUM's wide-neutral shooter-screen WAIT";
	policy.reset();
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(lastAcceptedActionTypesByUnit[protectedArcherId][EActionType::SHOOT], 0)
		<< "The protected archer must actually fire a native engine-accepted shot";
	EXPECT_GT(recorder.shots, 0);
	EXPECT_GT(recorder.damage, 0);
	ASSERT_EQ(recorder.battleResults.size(), 1u);
	std::cout << "AI_TUM_WIDE_NEUTRAL_SCREEN archer=" << protectedArcherId
		<< " archer_shots=" << lastAcceptedActionTypesByUnit[protectedArcherId][EActionType::SHOOT]
		<< " winner=" << (recorder.battleResults.back().winner == BattleSide::ATTACKER
			? "ATTACKER" : "DEFENDER") << '\n';
}


// Real VCMI splash bonuses make a wide enemy's tail differ from its head.
// Compare an independent HoMM3 shooter decision with TUM's native tail aim;
// check accepted SHOOT, both collateral injuries and completed server battle.
// The bonus is a fixture, not the unavailable original TUM ERA/DLL formula.
TEST_F(AIIntegrationNativeBattle, tumArcherChoosesWideTailWithNativeSplash)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(2), 35);
	setArmy(defenderSideHero, CreatureID(102), 10);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	battle()->obstacles.clear();
	CStack * archer = nullptr;
	CStack * wide = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(2))
			archer = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(102))
			wide = unit.get();
	}
	ASSERT_NE(archer, nullptr);
	ASSERT_NE(wide, nullptr);
	ASSERT_TRUE(wide->doubleWide());
	ASSERT_TRUE(archer->getPosition().isValid());
	const BattleHex widePosition(archer->getPosition().toInt() + 11);
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
	// A head shot at either collateral victim must NOT splash onto the
	// other. The tail is the only shared adjacent aim for all three targets.
	// Otherwise a shot at one extra Peasant can tie the tail's damage,
	// correctly preserving the partial HoMM3 baseline on an equal score.
	BattleHex firstCollateral = BattleHex::INVALID;
	BattleHex secondCollateral = BattleHex::INVALID;
	for(size_t i = 0; i < tailOnly.size(); ++i)
	{
		for(size_t j = i + 1; j < tailOnly.size(); ++j)
		{
			if(BattleHex::getDistance(tailOnly[i], tailOnly[j]) > 1)
			{
				firstCollateral = tailOnly[i];
				secondCollateral = tailOnly[j];
				break;
			}
		}
		if(firstCollateral.isValid())
			break;
	}
	ASSERT_TRUE(firstCollateral.isValid());
	ASSERT_TRUE(secondCollateral.isValid());
	const CStack * firstExtra = addStack(BattleSide::DEFENDER, CreatureID(0), firstCollateral, 1000);
	const CStack * secondExtra = addStack(BattleSide::DEFENDER, CreatureID(0), secondCollateral, 1000);
	ASSERT_NE(firstExtra, nullptr);
	ASSERT_NE(secondExtra, nullptr);
	archer->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::SHOOTS_ALL_ADJACENT,
		BonusSource::OTHER, 1, BonusSourceID()));
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanShoot(archer, head));
	ASSERT_TRUE(view->battleCanShoot(archer, tail));
	const auto headHits = view->getAttackedBattleUnits(archer, wide, head, true);
	const auto tailHits = view->getAttackedBattleUnits(archer, wide, tail, true);
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
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *archer);
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
	policy->activeStack(battleID, archer);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	ASSERT_EQ(callback->action->target.size(), 1u);
	EXPECT_EQ(callback->action->target.front().hexValue, tail)
		<< "TUM must aim at the legal tail that covers three enemy stacks";
	EXPECT_NE(callback->action->toString(), baseline.toString());
	std::cout << "AI_TUM_ARCHER_WIDE_AIM homm3=" << baseline.toString()
		<< " tum=" << callback->action->toString() << '\n';
	const auto firstHealthBefore = firstExtra->getAvailableHealth();
	const auto secondHealthBefore = secondExtra->getAvailableHealth();
	const int shotsBefore = recorder.shots;
	battle()->activeStack = archer->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Native VCMI rejected the independently chosen archer tail shot";
	EXPECT_GT(recorder.shots, shotsBefore);
	EXPECT_LT(firstExtra->getAvailableHealth(), firstHealthBefore);
	EXPECT_LT(secondExtra->getAvailableHealth(), secondHealthBefore);
	policy.reset();
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.damage, 0);
}

// A ballista's native SHOOTER and legal-fire flags must not turn it into
// the "friendly archers" that justify TUM's neutral-melee screening rule.
// The machine remains controlled by its separate, documented ballista policy.
// This is a VCMI-specific interpretation, not a recovered hidden ERA rule.

/*
 * The published TUM neutral hold protects allied archers using MELEE troops.
 * A native SIEGE_WEAPON bonus can remove an otherwise ordinary creature's
 * melee capability without turning it into a shooter. Do not strand that
 * non-melee unit in a WAIT/DEFEND screening loop. The bonus is fixture setup,
 * not a claim about a normal Peasant or the unavailable ERA/DLL internals.
 */
// Hypnotize changes control, but VCMI's ONLY_ENEMY/ONLY_MINE lists can
// continue exposing each stack under its original side. A controlled former
// neutral is now our ally and must not cancel the published TUM rule that
// melee WAIT while archers engage the remaining neutral-only opposition.
// Require a real spell, separate HoMM3 decision, server-accepted WAIT and
// a terminal native battle; this is not proof of closed ERA/DLL equivalence.
TEST_F(AIIntegrationNativeBattle, tumHypnotizedFormerNeutralDoesNotCancelShooterScreen)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 60);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(attackerSideHero->id, SlotID(1)), CreatureID(2).toCreature(), 25));
	gameHandler->createWanderingMonster({9, 9, 0}, CreatureID(0), 40);
	const auto * neutral = findFirst<CGCreature>();
	ASSERT_NE(neutral, nullptr);
	ASSERT_EQ(neutral->getOwner(), PlayerColor::UNFLAGGABLE);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(neutral->id, SlotID(1)), CreatureID(0).toCreature(), 10));
	teachBattleSpell(attackerSideHero, SpellID::HYPNOTIZE, 30);
	gameHandler->battles->startBattle(attackerSideHero, neutral);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);

	const CStack * melee = nullptr;
	const CStack * controlled = nullptr;
	CStack * hostile = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			melee = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
		{
			if(unit->getCount() == 10)
				controlled = unit.get();
			else if(unit->getCount() == 40)
				hostile = unit.get();
		}
	}
	ASSERT_NE(melee, nullptr);
	ASSERT_NE(controlled, nullptr);
	ASSERT_NE(hostile, nullptr);
	// The original neutral stacks deploy near each other. After Hypnotize,
	// the remaining neutral can legitimately reach its former ally, which
	// would disable the published hold for a DIFFERENT reason. Isolate the
	// owner-list bug with an immobile neutral while archers can still shoot.
	// Creature-source Paralyze is fixture setup; Hypnotize below is real.
	hostile->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::NOT_ACTIVE, BonusSource::CREATURE_ABILITY,
		1, BonusSourceID(CreatureID(0)), BonusSubtypeID(SpellID(SpellID::PARALYZE))));
	ASSERT_TRUE(hostile->hasBonusOfType(BonusType::NOT_ACTIVE));
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::HYPNOTIZE, controlled))
		<< "The neutral must change owner through an actual hero spell";
	ASSERT_TRUE(controlled->isHypnotized());
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	ASSERT_EQ(battle()->battleGetOwner(controlled), player);
	ASSERT_EQ(battle()->battleGetOwner(hostile), PlayerColor::NEUTRAL);

	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	const auto visibleEnemies = view->battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY);
	ASSERT_TRUE(std::any_of(visibleEnemies.begin(), visibleEnemies.end(),
		[&](const CStack * unit) { return unit->unitId() == controlled->unitId(); }))
		<< "The fixture must reproduce the stale native side-based enemy list";
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
	ASSERT_EQ(callback->action->actionType, EActionType::WAIT)
		<< "A currently friendly Hypnotized former neutral cannot cancel neutral shooter protection";
	std::cout << "AI_TUM_HYPNOTIZED_NEUTRAL_SCREEN homm3=" << baseline.toString()
		<< " tum=" << callback->action->toString() << '\n';
	battle()->activeStack = melee->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The native VCMI server rejected TUM's protective WAIT";
	policy.reset();
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.shots, 0);
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, tumNonMeleeFriendlyCannotEnterNeutralShooterScreen)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 60);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(attackerSideHero->id, SlotID(1)), CreatureID(2).toCreature(), 25));
	gameHandler->createWanderingMonster({9, 9, 0}, CreatureID(0), 55);
	const auto * neutral = findFirst<CGCreature>();
	ASSERT_NE(neutral, nullptr);
	ASSERT_EQ(neutral->getOwner(), PlayerColor::UNFLAGGABLE);
	gameHandler->battles->startBattle(attackerSideHero, neutral);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);

	CStack * melee = nullptr;
	const CStack * archer = nullptr;
	const CStack * opponent = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			melee = unit.get();
		else if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(2))
			archer = unit.get();
		else if(unit->unitSide() == BattleSide::DEFENDER && unit->alive())
			opponent = unit.get();
	}
	ASSERT_NE(melee, nullptr);
	ASSERT_NE(archer, nullptr);
	ASSERT_NE(opponent, nullptr);
	ASSERT_TRUE(melee->isMeleeAttacker());
	ASSERT_FALSE(melee->isShooter());

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_EQ(battle()->sideToPlayer(BattleSide::DEFENDER), PlayerColor::NEUTRAL);
	ASSERT_TRUE(view->battleCanShoot(archer, opponent->getPosition()));

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

	const auto ordinaryBaseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *melee);
	ASSERT_NE(ordinaryBaseline.actionType, EActionType::WAIT);
	policy->activeStack(battleID, melee);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WAIT)
		<< "A genuine melee unit screens a live neutral encounter for friendly archers";
	callback->action.reset();

	melee->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::SIEGE_WEAPON, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_FALSE(melee->isMeleeAttacker());
	ASSERT_FALSE(melee->isShooter());
	ASSERT_FALSE(view->battleCanAttackUnit(melee, opponent));
	const auto nonMeleeBaseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *melee);
	policy->activeStack(battleID, melee);
	ASSERT_TRUE(callback->action.has_value());
	EXPECT_EQ(callback->action->toString(), nonMeleeBaseline.toString())
		<< "Non-melee units must use the explicitly partial HoMM3 fallback";
	ASSERT_NE(callback->action->actionType, EActionType::WAIT);

	battle()->activeStack = melee->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The authoritative server rejected the chosen non-melee action";
	const int unitId = melee->unitId();
	policy.reset();
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(lastAcceptedActionTypesByUnit[unitId][nonMeleeBaseline.actionType], 0);
	EXPECT_GT(recorder.shots, 0);
}

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
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto policy = AIFactory::createBattleAI("MMAI");
	ASSERT_NE(policy, nullptr);
	EXPECT_EQ(policy->dllName, "MMAI");
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	try
	{
		// Strict MMAI preloads configured roles at interface initialization.
		// A missing model must be rejected before the first battle event,
		// not after a turn has already been offered to the neural policy.
		policy->initBattleInterface(environment, callback, AutocombatPreferences{});
		FAIL() << "MMAI silently accepted missing ONNX weights during preload";
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
	// The checksum-pinned real V15 ONNX instance must reject a missing
	// event state before inference, not crash or use a scripted substitute.
	try
	{
		(void)model->getAction(nullptr);
		FAIL() << "MMAI accepted a null observation";
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_NE(std::string(error.what()).find("null state before ONNX inference"), std::string::npos);
	}

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

TEST_F(AIIntegrationNativeBattle, mmaiFirstAidWithoutWoundsUsesEngineAutoHealRealBattle)
{
	// Real VCMI 1.8 ONNX pack is required: a first-aid tent is not a V15
	// graph action. Before the fix, an uninjured army made the adapter return
	// nullptr and attempt to infer an action for the military machine.
	if(!std::getenv("VCMI_AI_MMAI_NEURAL_TEST"))
		GTEST_SKIP() << "Requires installed, checksum-verified MMAI ONNX models";
	requireInstalledNeuralRole("attacker");

	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 40);
	setArmy(defenderSideHero, CreatureID(0), 35);
	ASSERT_FALSE(HasFatalFailure());
	giveArtifact(attackerSideHero, ArtifactID(ArtifactID::FIRST_AID_TENT), ArtifactPosition::MACH3);
	// Without First Aid mastery the server executes HEAL automatically before
	// delivering an active-stack event. Grant real in-game manual control so
	// this test actually exercises the MMAI V15 machine adapter as well.
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::FIRST_AID),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	ASSERT_GT(attackerSideHero->valOfBonuses(BonusType::MANUAL_CONTROL,
		BonusSubtypeID(CreatureID(CreatureID::FIRST_AID_TENT))), 0)
		<< "The hero needs game-granted manual tent control for this adapter regression";
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	bool foundTent = false;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->isFirstAidTent())
			foundTent = true;
		else if(unit->unitSide() == BattleSide::ATTACKER && unit->alive())
		{
			EXPECT_EQ(unit->getFirstHPleft(), unit->getMaxHealth())
				<< "The first aid scenario must start with no wounded allies";
		}
	}
	ASSERT_TRUE(foundTent) << "The artifact did not install the actual first aid tent";
	runToCompletion(std::nullopt, false, "MMAI", "HoMM3OriginalTactics");
	EXPECT_GT(mmaiUntargetedFirstAidActions, 0)
		<< "The first aid tent never made its automatic, untargeted engine HEAL";
	EXPECT_GT(mmaiUnitDecisions, 0)
		<< "The actual ONNX policy never controlled a regular creature";
	EXPECT_GT(recorder.damage, 0) << "No authoritative battle damage was recorded";
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
	ASSERT_TRUE(observer->getBattle(battle()->getBattleID())->battleCanFlee())
		<< "A legal retreat must be available to exercise strict neural-only routing";

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

// Consolidated from green isolated source-parity FHeroes2 Draft regression.
TEST_F(AIIntegrationNativeBattle, fheroes2RemoteFlyerVetoesFastArcherEscape)
{
	prepareBattle(CreatureID(19), 25, CreatureID(16), 10);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * shooter = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(19))
			shooter = unit.get();
	ASSERT_NE(shooter, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);
	const BattleHex initial = shooter->getPosition();
	const BattleHex blockerHex(initial.toInt() + 1);
	ASSERT_TRUE(blockerHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(blockerHex), nullptr);
	const auto * blocker = addStack(BattleSide::DEFENDER, CreatureID(16), blockerHex, 10);
	ASSERT_NE(blocker, nullptr);
	ASSERT_FALSE(battle()->battleCanShoot(shooter));
	ASSERT_GT(static_cast<int>(shooter->getMovementRange()),
		static_cast<int>(blocker->getMovementRange()) + 2);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	const auto decide = [&]() -> std::optional<BattleAction>
	{
		auto callback = std::make_shared<DecisionCallback>(player);
		callback->onBattleStarted(battle());
		auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
		if(!policy)
			return std::nullopt;
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
		return callback->action;
	};
	const auto noFlyer = decide();
	ASSERT_TRUE(noFlyer.has_value());
	ASSERT_EQ(noFlyer->actionType, EActionType::WALK) << noFlyer->toString();

	BattleHex farHex;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex candidate(index);
		if(candidate.isAvailable() && candidate.getX() >= 10
			&& battle()->battleGetUnitByPos(candidate) == nullptr
			&& BattleHex::getDistance(candidate, initial) >= 6
			&& BattleHex::getDistance(candidate, blockerHex) >= 4)
		{
			farHex = candidate;
			break;
		}
	}
	ASSERT_TRUE(farHex.isValid());
	const auto * flyer = addStack(BattleSide::DEFENDER, CreatureID(4), farHex, 1);
	ASSERT_NE(flyer, nullptr);
	ASSERT_TRUE(flyer->hasBonusOfType(BonusType::FLYING));
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);
	const auto withFlyer = decide();
	ASSERT_TRUE(withFlyer.has_value());
	ASSERT_EQ(withFlyer->actionType, EActionType::WALK_AND_ATTACK)
		<< "A remote flyer forces the source's adjacent-blocker melee: " << withFlyer->toString();
	EXPECT_NE(withFlyer->actionType, noFlyer->actionType);
	// Native makeMeleeAttack stores [attack-from, attacked-hex] in that order.
	ASSERT_GE(withFlyer->target.size(), 2u);
	EXPECT_EQ(battle()->battleGetUnitByPos(withFlyer->target.back().hexValue), blocker);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *withFlyer))
		<< "Server rejected the chosen melee action";
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}


// Consolidated from green isolated source-parity FHeroes2 Draft regression.
TEST_F(AIIntegrationNativeBattle, fheroes2NativeBlindAppliesSourceNonlethalDamageWakePenalty)
{
	prepareBattle(CreatureID(0), 70, CreatureID(0), 140);
	ASSERT_FALSE(HasFatalFailure());
	const CStack * target = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			target = unit.get();
	ASSERT_NE(target, nullptr);
	ASSERT_GT(target->getAvailableHealth(), 4);
	const auto damage = target->getAvailableHealth() / 4;
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto playerView = callback->getBattle(battle()->getBattleID());
	ASSERT_NE(playerView, nullptr);
	const double initial = AIIntegration::FHeroes2SpellPolicy::sourceDamageValue(
		*playerView, *target, damage);
	EXPECT_GT(initial, 0) << "An active target must have positive nonlethal damage value";

	teachBattleSpell(attackerSideHero, SpellID::BLIND, 6);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::BLIND, target));
	ASSERT_TRUE(target->hasBonusOfType(BonusType::NOT_ACTIVE))
		<< "The authoritative server must apply a genuine cannot-act status";
	ASSERT_FALSE(target->isFrozen())
		<< "VCMI isFrozen means Stone Gaze, not the native Blind status";
	ASSERT_GT(target->estimateCombatValue(), 0)
		<< "The directed target must retain nonzero combat value under Blind";
	const double after = AIIntegration::FHeroes2SpellPolicy::sourceDamageValue(
		*playerView, *target, damage);
	const double fraction = static_cast<double>(damage) / target->getAvailableHealth();
	EXPECT_NEAR(after, static_cast<double>(target->estimateCombatValue())
		* (2 * fraction - 1), 1e-6);
	EXPECT_LT(after, 0)
		<< "A sub-half-health strike would wake Blind: source values it negatively";

	// Independent pinned-source lethal branch: ai_battle_spell.cpp
	// spellDamageValue uses Unit::GetSpeed(), which is STANDING under Blind.
	// A lone disabled defender also has zero projected army speed, so the
	// death premium is 3.5%, not the 7% reserved for faster units. Native
	// getMovementRange() need not drop when the server applies NOT_ACTIVE.
	const auto lethalDamage = target->getAvailableHealth();
	const double lethal = AIIntegration::FHeroes2SpellPolicy::sourceDamageValue(
		*playerView, *target, lethalDamage);
	EXPECT_NEAR(lethal, static_cast<double>(target->estimateCombatValue()) * 1.035, 1e-6)
		<< "Lethally damaging Blind must use source STANDING speed for the kill premium";

	runToCompletion(std::nullopt, false, "FHeroes2Tactics");
	EXPECT_GT(recorder.damage, 0) << "Native armies must still fight to a result";
}



// Preserved native regression from the FHeroes2 parity branch.
TEST_F(AIIntegrationNativeBattle, fheroes2HasteDoesNotRewardBlindedArcherSpeed)
{
	prepareBattle(CreatureID(0), 1, CreatureID(0), 70);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * active = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			active = unit.get();
	ASSERT_NE(active, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), active);
	const BattleHex shooterHex(active->getPosition().toInt() + GameConstants::BFIELD_WIDTH);
	ASSERT_TRUE(shooterHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(shooterHex), nullptr);
	const CStack * shooter = addStack(BattleSide::ATTACKER, CreatureID(2), shooterHex, 25);
	ASSERT_NE(shooter, nullptr);
	ASSERT_TRUE(shooter->isShooter());

	teachBattleSpell(attackerSideHero, SpellID::HASTE, 6);
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	const auto decide = [&]()
	{
		auto callback = std::make_shared<DecisionCallback>(player);
		callback->onBattleStarted(battle());
		auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
		EXPECT_NE(policy, nullptr);
		if(!policy)
			return std::make_pair(std::optional<BattleAction>{}, false);
		policy->playerID = player;
		auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
		AutocombatPreferences prefs;
		prefs.enableSpellsUsage = true;
		policy->initBattleInterface(environment, callback, prefs);
		policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
			battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
			battle()->getSideHero(BattleSide::ATTACKER),
			battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
		policy->activeStack(battleID, active);
		return std::make_pair(callback->action, callback->spellAction);
	};
	const auto targetsShooter = [&](const BattleAction & action)
	{
		return std::any_of(action.target.begin(), action.target.end(), [&](const auto & target)
		{
			return target.unitValue == shooter->unitId() || target.hexValue == shooter->getPosition();
		});
	};
	const auto before = decide();
	ASSERT_TRUE(before.first.has_value());
	ASSERT_TRUE(before.second) << "Uncontrolled high-value archer should attract native Haste: "
		<< before.first->toString();
	EXPECT_EQ(before.first->spell, SpellID(SpellID::HASTE));
	EXPECT_TRUE(targetsShooter(*before.first));

	teachBattleSpell(defenderSideHero, SpellID::BLIND, 6);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::BLIND, shooter));
	ASSERT_TRUE(shooter->hasBonusOfType(BonusType::NOT_ACTIVE));
	ASSERT_GT(shooter->getMovementRange(), 0u)
		<< "Native blinded stack must retain raw speed to exercise the regression";
	ASSERT_EQ(battle()->battleActiveUnit(), active);
	const int64_t activeHealthBefore = active->getAvailableHealth();
	const int64_t shooterHealthBefore = shooter->getAvailableHealth();
	const auto after = decide();
	ASSERT_TRUE(after.first.has_value());
	EXPECT_EQ(active->getAvailableHealth(), activeHealthBefore)
		<< "Spell valuation must not injure the live active unit";
	EXPECT_EQ(shooter->getAvailableHealth(), shooterHealthBefore)
		<< "Projected spell valuation must not damage the blinded archer";
	if(after.second)
	{
		EXPECT_EQ(after.first->spell, SpellID(SpellID::HASTE));
		EXPECT_FALSE(targetsShooter(*after.first))
			<< "A still-blinded archer has STANDING effective speed: " << after.first->toString();
	}
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *after.first))
		<< "The real server rejected the independently selected action";
	runToCompletion(std::nullopt, true, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

// Preserved native regression from the FHeroes2 parity branch.
TEST_F(AIIntegrationNativeBattle, fheroes2DispelPrefersStrongerBlindedHastedAlly)
{
	prepareBattle(CreatureID(0), 1, CreatureID(0), 60);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * active = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			active = unit.get();
	ASSERT_NE(active, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), active);

	const BattleHex hastedHex(active->getPosition().toInt() + GameConstants::BFIELD_WIDTH);
	const BattleHex plainHex(active->getPosition().toInt() + 2 * GameConstants::BFIELD_WIDTH);
	ASSERT_TRUE(hastedHex.isAvailable());
	ASSERT_TRUE(plainHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(hastedHex), nullptr);
	ASSERT_EQ(battle()->battleGetUnitByPos(plainHex), nullptr);
	const CStack * hasted = addStack(BattleSide::ATTACKER, CreatureID(0), hastedHex, 21);
	const CStack * plain = addStack(BattleSide::ATTACKER, CreatureID(0), plainHex, 20);
	ASSERT_NE(hasted, nullptr);
	ASSERT_NE(plain, nullptr);

	teachBattleSpell(attackerSideHero, SpellID::HASTE, 6);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::HASTE, hasted));
	teachBattleSpell(defenderSideHero, SpellID::BLIND, 6);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::BLIND, hasted));
	// The native engine permits only one hero spell per side and round.
	// Advance through real DEFEND actions instead of forcing a second cast.
	endRound();
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::BLIND, plain));
	// Start the independent decision on an actual allied activation.
	for(int attempt = 0; attempt < 8 && battle()->battleActiveUnit()
		&& battle()->battleActiveUnit()->unitSide() != BattleSide::ATTACKER; ++attempt)
		ASSERT_TRUE(act(BattleAction::makeDefend(battle()->battleActiveUnit())));
	active = dynamic_cast<const CStack *>(battle()->battleActiveUnit());
	ASSERT_NE(active, nullptr);
	ASSERT_EQ(active->unitSide(), BattleSide::ATTACKER);
	const auto haste = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::HASTE)));
	const auto blind = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLIND)));
	ASSERT_TRUE(hasted->hasBonus(haste));
	ASSERT_TRUE(hasted->hasBonus(blind));
	ASSERT_TRUE(plain->hasBonus(blind));
	ASSERT_TRUE(hasted->hasBonusOfType(BonusType::NOT_ACTIVE));
	ASSERT_TRUE(plain->hasBonusOfType(BonusType::NOT_ACTIVE));
	ASSERT_GT(hasted->getMovementRange(), 0u);
	ASSERT_GT(plain->getMovementRange(), 0u);
	ASSERT_GT(hasted->estimateCombatValue(), plain->estimateCombatValue());

	teachBattleSpell(attackerSideHero, SpellID::DISPEL, 6);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::WATER_MAGIC),
		MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = true;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	const auto hastedHealth = hasted->getAvailableHealth();
	const auto plainHealth = plain->getAvailableHealth();
	policy->activeStack(battleID, active);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_TRUE(callback->spellAction);
	EXPECT_EQ(callback->action->spell, SpellID(SpellID::DISPEL));
	const bool targetsHasted = std::any_of(callback->action->target.begin(),
		callback->action->target.end(), [&](const auto & target)
		{
			return target.unitValue == hasted->unitId() || target.hexValue == hasted->getPosition();
		});
	EXPECT_TRUE(targetsHasted)
		<< "The stronger Blind target must not be devalued for losing inactive Haste: "
		<< callback->action->toString();
	EXPECT_EQ(hasted->getAvailableHealth(), hastedHealth);
	EXPECT_EQ(plain->getAvailableHealth(), plainHealth)
		<< "HypotheticBattle must not mutate real stack health";

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The authoritative engine rejected FHeroes2's independently selected Dispel";
	EXPECT_FALSE(hasted->hasBonus(blind));
	EXPECT_FALSE(hasted->hasBonus(haste));
	EXPECT_TRUE(plain->hasBonus(blind))
		<< "Advanced Water Magic Dispel must target only the selected native stack";
	runToCompletion(std::nullopt, false, "FHeroes2Tactics");
	EXPECT_GT(recorder.damage, 0);
}

// Preserved native regression from the FHeroes2 parity branch.
TEST_F(AIIntegrationNativeBattle, fheroes2MagicArrowAvoidsWakingBlindedEnemy)
{
	prepareBattle(CreatureID(0), 25, CreatureID(0), 80);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * blinded = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			blinded = unit.get();
	ASSERT_NE(blinded, nullptr);
	const BattleHex otherHex(blinded->getPosition().toInt() + GameConstants::BFIELD_WIDTH);
	ASSERT_TRUE(otherHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(otherHex), nullptr);
	const CStack * other = addStack(BattleSide::DEFENDER, CreatureID(0), otherHex, 80);
	ASSERT_NE(other, nullptr);
	teachBattleSpell(attackerSideHero, SpellID::BLIND, 6);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::BLIND, blinded));
	// This regression isolates damage-spell target valuation: the setup
	// Blind was cast by the real server, but must not compete as an
	// additional hero spell in the subsequent independent decision.
	attackerSideHero->removeSpellFromSpellbook(SpellID::BLIND);
	ASSERT_FALSE(attackerSideHero->spellbookContainsSpell(SpellID::BLIND));
	ASSERT_TRUE(blinded->hasBonusOfType(BonusType::NOT_ACTIVE));
	ASSERT_FALSE(blinded->isFrozen());
	ASSERT_GT(blinded->getMovementRange(), 0u);
	teachBattleSpell(attackerSideHero, SpellID::MAGIC_ARROW, 6);
	// Real round change restores spell availability after the setup cast.
	endRound();
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_TRUE(blinded->alive());
	ASSERT_TRUE(other->alive());
	ASSERT_TRUE(blinded->hasBonusOfType(BonusType::NOT_ACTIVE))
		<< "Blind must survive the setup round for wake-up risk to matter";
	const CStack * active = dynamic_cast<const CStack *>(battle()->battleActiveUnit());
	for(int i = 0; i < 12 && active && active->unitSide() != BattleSide::ATTACKER; ++i)
	{
		ASSERT_TRUE(defend(active));
		active = dynamic_cast<const CStack *>(battle()->battleActiveUnit());
	}
	ASSERT_NE(active, nullptr);
	ASSERT_EQ(active->unitSide(), BattleSide::ATTACKER);
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	const auto blindedHP = blinded->getAvailableHealth();
	const auto otherHP = other->getAvailableHealth();
	auto observer = std::make_shared<DecisionCallback>(player);
	observer->onBattleStarted(battle());
	auto ai = AIFactory::createBattleAI("FHeroes2Tactics");
	ASSERT_NE(ai, nullptr);
	ai->playerID = player;
	auto env = std::make_shared<NativePortEnvironment>(makeCallback(player), observer);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = true;
	ai->initBattleInterface(env, observer, prefs);
	ai->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	ai->activeStack(battleID, active);
	ASSERT_TRUE(observer->action.has_value());
	ASSERT_TRUE(observer->spellAction) << "Expected independent FHeroes2 hero spell";
	const auto action = *observer->action;
	ASSERT_EQ(action.spell, SpellID(SpellID::MAGIC_ARROW));
	EXPECT_TRUE(std::any_of(action.target.begin(), action.target.end(), [&](const auto & aim)
	{
		return aim.unitValue == other->unitId() || aim.hexValue == other->getPosition();
	})) << "Magic Arrow must select the active enemy: " << action.toString();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action));
	ASSERT_EQ(recorder.castsOf(SpellID::MAGIC_ARROW).size(), 1u);
	EXPECT_EQ(blinded->getAvailableHealth(), blindedHP);
	EXPECT_LT(other->getAvailableHealth(), otherHP);
	ASSERT_TRUE(other->alive()) << "This regression must exercise partial, not lethal, spell damage";
	runToCompletion(std::nullopt, true, "FHeroes2Tactics");
	EXPECT_GT(recorder.damage, 0);
}

// Preserved native regression from the FHeroes2 parity branch.
TEST_F(AIIntegrationNativeBattle, fheroes2SpellThreatExcludesNativeBlindedEnemyArcher)
{
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
	ASSERT_GT(AIIntegration::FHeroes2SpellPolicy::sourceArcherStrength(*playerView, player, false), 0);

	teachBattleSpell(attackerSideHero, SpellID::BLIND);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::BLIND, defenderArcher));
	ASSERT_TRUE(defenderArcher->hasBonusOfType(BonusType::NOT_ACTIVE));
	ASSERT_GT(defenderArcher->getMovementRange(), 0u);
	EXPECT_DOUBLE_EQ(AIIntegration::FHeroes2SpellPolicy::sourceArcherStrength(*playerView, player, false), 0.0)
		<< "Blinded enemy archer must not inflate FHeroes2 spell-conservation threat";

	runToCompletion(std::nullopt, false, "FHeroes2Tactics");
	EXPECT_GT(recorder.damage, 0);
}

// Preserved native regression from the FHeroes2 parity branch.
TEST_F(AIIntegrationNativeBattle, fheroes2BlockedBreathArcherAvoidsFriendlySplash)
{
	prepareBattle(CreatureID(2), 30, CreatureID(0), 1);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * shooter = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(2))
			shooter = unit.get();
	ASSERT_NE(shooter, nullptr);
	ASSERT_TRUE(shooter->isShooter());
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::TWO_HEX_ATTACK_BREATH, BonusSource::OTHER, 1, BonusSourceID()));
	const BattleHex original = shooter->getPosition();
	BattleHex risky;
	BattleHex behind;
	BattleHex safe;
	for(const auto & neighbor : original.getNeighbouringTiles())
	{
		if(!neighbor.isAvailable() || battle()->battleGetUnitByPos(neighbor))
			continue;
		const auto direction = BattleHex::mutualPosition(original, neighbor);
		if(direction == BattleHex::NONE)
			continue;
		const auto back = neighbor.cloneInDirection(direction, false);
		if(!back.isAvailable() || battle()->battleGetUnitByPos(back))
			continue;
		for(const auto & other : original.getNeighbouringTiles())
			if(other.isAvailable() && other != neighbor && other != back
				&& !battle()->battleGetUnitByPos(other))
			{
				risky = neighbor;
				behind = back;
				safe = other;
				break;
			}
		if(risky.isValid())
			break;
	}
	ASSERT_TRUE(risky.isValid());
	ASSERT_TRUE(behind.isValid());
	ASSERT_TRUE(safe.isValid());
	const CStack * riskyPrimary = addStack(BattleSide::DEFENDER, CreatureID(0), risky, 1);
	const CStack * safePrimary = addStack(BattleSide::DEFENDER, CreatureID(0), safe, 1);
	const CStack * ally = addStack(BattleSide::ATTACKER, CreatureID(0), behind, 120);
	ASSERT_NE(riskyPrimary, nullptr);
	ASSERT_NE(safePrimary, nullptr);
	ASSERT_NE(ally, nullptr);
	ASSERT_FALSE(battle()->battleCanShoot(shooter))
		<< "Adjacent attackers must force the archer into native melee";
	ASSERT_FALSE(battle()->meleeAttackHexes(shooter, riskyPrimary, original).empty());
	ASSERT_FALSE(battle()->meleeAttackHexes(shooter, safePrimary, original).empty());
	const auto riskySplash = battle()->getAttackedBattleUnits(
		shooter, riskyPrimary, risky, false, original);
	ASSERT_TRUE(std::any_of(riskySplash.begin(), riskySplash.end(), [&](const auto * victim)
	{
		return victim->unitId() == ally->unitId();
	})) << "The engine must really threaten the allied secondary victim";

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
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK)
		<< "FHeroes2 must independently choose a legal blocked-archer melee strike";
	ASSERT_GE(callback->action->target.size(), 2u);
	const BattleHex from = callback->action->target[0].hexValue;
	const BattleHex hitHex = callback->action->target[1].hexValue;
	const auto * target = battle()->battleGetUnitByPos(hitHex);
	ASSERT_NE(target, nullptr);
	EXPECT_EQ(target->unitId(), safePrimary->unitId())
		<< "The safe primary has equivalent direct damage without allied breath collateral";
	const auto actualSplash = battle()->getAttackedBattleUnits(
		shooter, target, hitHex, false, from);
	EXPECT_TRUE(std::none_of(actualSplash.begin(), actualSplash.end(), [&](const auto * victim)
	{
		return victim->unitId() == ally->unitId();
	}));
	const auto allyHealthBefore = ally->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "The VCMI server rejected the independent blocked-archer strike";
	EXPECT_EQ(ally->getAvailableHealth(), allyHealthBefore);
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

// Preserved native regression from the FHeroes2 parity branch.
TEST_F(AIIntegrationNativeBattle, fheroes2NativeBreathAvoidsFriendlyCollateral)
{
	prepareBattle(CreatureID(0), 30, CreatureID(0), 1);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * attacker = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			attacker = unit.get();
	ASSERT_NE(attacker, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), attacker);
	attacker->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::TWO_HEX_ATTACK_BREATH, BonusSource::OTHER, 1, BonusSourceID()));
	const BattleHex original = attacker->getPosition();
	BattleHex risky;
	BattleHex rear;
	BattleHex alternative;
	for(const auto & neighbor : original.getNeighbouringTiles())
	{
		if(!neighbor.isAvailable() || battle()->battleGetUnitByPos(neighbor))
			continue;
		const auto direction = BattleHex::mutualPosition(original, neighbor);
		if(direction == BattleHex::NONE)
			continue;
		const auto behind = neighbor.cloneInDirection(direction, false);
		if(!behind.isAvailable() || battle()->battleGetUnitByPos(behind))
			continue;
		for(const auto & other : original.getNeighbouringTiles())
			if(other.isAvailable() && other != neighbor && other != behind
				&& !battle()->battleGetUnitByPos(other))
			{
				risky = neighbor;
				rear = behind;
				alternative = other;
				break;
			}
		if(risky.isValid())
			break;
	}
	ASSERT_TRUE(risky.isValid());
	ASSERT_TRUE(rear.isValid());
	ASSERT_TRUE(alternative.isValid());
	const CStack * primary = addStack(BattleSide::DEFENDER, CreatureID(0), risky, 1);
	const CStack * safePrimary = addStack(BattleSide::DEFENDER, CreatureID(0), alternative, 1);
	const CStack * ally = addStack(BattleSide::ATTACKER, CreatureID(0), rear, 120);
	ASSERT_NE(primary, nullptr);
	ASSERT_NE(safePrimary, nullptr);
	ASSERT_NE(ally, nullptr);
	const auto riskySplash = battle()->getAttackedBattleUnits(attacker, primary, risky, false, original);
	ASSERT_TRUE(std::any_of(riskySplash.begin(), riskySplash.end(), [&](const auto * victim)
	{
		return victim->unitId() == ally->unitId();
	})) << "The native breath attack must really threaten a friendly stack";

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
	policy->activeStack(battleID, attacker);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK)
		<< "FHeroes2 must select a real native melee attack independently";
	ASSERT_GE(callback->action->target.size(), 2u);
	const BattleHex from = callback->action->target[0].hexValue;
	const BattleHex targetHex = callback->action->target[1].hexValue;
	const auto * target = battle()->battleGetUnitByPos(targetHex);
	ASSERT_NE(target, nullptr);
	const auto actualSplash = battle()->getAttackedBattleUnits(attacker, target, targetHex, false, from);
	EXPECT_TRUE(std::none_of(actualSplash.begin(), actualSplash.end(), [&](const auto * victim)
	{
		return victim->unitId() == ally->unitId();
	})) << "An equally valuable safe strike exists: never choose the allied breath victim";
	const auto allyHealthBefore = ally->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Authoritative VCMI server rejected the independently selected melee strike";
	EXPECT_EQ(ally->getAvailableHealth(), allyHealthBefore)
		<< "Actual engine damage must spare the friendly secondary target";
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

// Preserved native regression from the FHeroes2 parity branch.
TEST_F(AIIntegrationNativeBattle, fheroes2FatalFirstStrikeDoesNotRewardUnexecutedBreath)
{
	prepareBattle(CreatureID(0), 1, CreatureID(0), 25);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * attacker = nullptr;
	CStack * secondary = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			attacker = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			secondary = unit.get();
	}
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(secondary, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), attacker);
	attacker->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::TWO_HEX_ATTACK_BREATH, BonusSource::OTHER, 1, BonusSourceID()));
	const BattleHex attackerHex = attacker->getPosition();
	BattleHex front;
	BattleHex behind;
	BattleHex safe;
	for(const auto & nearby : attackerHex.getNeighbouringTiles())
	{
		if(!nearby.isAvailable() || battle()->battleGetUnitByPos(nearby))
			continue;
		const auto direction = BattleHex::mutualPosition(attackerHex, nearby);
		if(direction == BattleHex::NONE)
			continue;
		const auto rear = nearby.cloneInDirection(direction, false);
		if(!rear.isAvailable() || battle()->battleGetUnitByPos(rear))
			continue;
		for(const auto & other : attackerHex.getNeighbouringTiles())
		{
			if(other.isAvailable() && other != nearby && other != rear
				&& !battle()->battleGetUnitByPos(other))
			{
				front = nearby;
				behind = rear;
				safe = other;
				break;
			}
		}
		if(safe.isValid())
			break;
	}
	ASSERT_TRUE(front.isValid());
	ASSERT_TRUE(behind.isValid());
	ASSERT_TRUE(safe.isValid());
	secondary->setPosition(behind);
	CStack * dangerous = addStack(BattleSide::DEFENDER, CreatureID(0), front, 100);
	CStack * alternative = addStack(BattleSide::DEFENDER, CreatureID(0), safe, 1);
	ASSERT_NE(dangerous, nullptr);
	ASSERT_NE(alternative, nullptr);
	dangerous->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::FIRST_STRIKE, BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(0))));
	const auto splash = battle()->getAttackedBattleUnits(
		attacker, dangerous, front, false, attacker->getPosition());
	ASSERT_TRUE(std::any_of(splash.begin(), splash.end(), [&](const auto * unit)
	{
		return unit->unitId() == secondary->unitId();
	})) << "The unsafe strike must have a real native breath target behind it";

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	BattleAttackInfo counter(dangerous, attacker, 0, false);
	counter.attackerPos = front;
	counter.defenderPos = attacker->getPosition();
	ASSERT_GE(view->battleEstimateDamage(counter).damage.min, attacker->getAvailableHealth())
		<< "Native first strike must certainly kill before any projected breath hit";

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
	const int64_t originalHealth = attacker->getAvailableHealth();
	policy->activeStack(battleID, attacker);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(callback->action->target.size(), 2u);
	EXPECT_NE(callback->action->target[1].hexValue, front)
		<< "Fatal first strike must not earn fictitious breath damage against the secondary stack";
	EXPECT_EQ(attacker->getAvailableHealth(), originalHealth)
		<< "The first-strike estimate must not modify the active native unit";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action));
	policy.reset();
	// The first attacking activation was already submitted and counted above.
	// The shared mixed-policy runner counts only its own later activations;
	// a one-creature attacker can die before getting another turn. Use the
	// same independent partial controller on both sides to require genuine
	// subsequent server actions and a terminal result without making a false
	// mixed-policy activation claim.
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "FHeroes2Tactics");
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// Preserved native regression from the FHeroes2 parity branch.
TEST_F(AIIntegrationNativeBattle, fheroes2NativeBreathMeleeChoosesAndExecutesSecondaryHit)
{
	prepareBattle(CreatureID(0), 30, CreatureID(0), 1);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * attacker = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			attacker = unit.get();
	ASSERT_NE(attacker, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), attacker);
	attacker->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::TWO_HEX_ATTACK_BREATH, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(attacker->hasBonusOfType(BonusType::TWO_HEX_ATTACK_BREATH));

	BattleHex front;
	BattleHex behind;
	const BattleHex attackerPosition = attacker->getPosition();
	for(const auto & nearby : attackerPosition.getNeighbouringTiles())
	{
		if(!nearby.isAvailable() || battle()->battleGetUnitByPos(nearby))
			continue;
		const auto dir = BattleHex::mutualPosition(attacker->getPosition(), nearby);
		if(dir == BattleHex::NONE)
			continue;
		const auto rear = nearby.cloneInDirection(dir, false);
		if(rear.isAvailable() && !battle()->battleGetUnitByPos(rear))
		{
			front = nearby;
			behind = rear;
			break;
		}
	}
	ASSERT_TRUE(front.isValid());
	ASSERT_TRUE(behind.isValid());
	const auto * primary = addStack(BattleSide::DEFENDER, CreatureID(0), front, 1);
	const auto * secondary = addStack(BattleSide::DEFENDER, CreatureID(0), behind, 30);
	ASSERT_NE(primary, nullptr);
	ASSERT_NE(secondary, nullptr);
	const auto expectedSplash = battle()->getAttackedBattleUnits(
		attacker, primary, front, false, attacker->getPosition());
	ASSERT_TRUE(std::any_of(expectedSplash.begin(), expectedSplash.end(),
		[&](const auto * unit) { return unit->unitId() == secondary->unitId(); }))
		<< "Native VCMI must recognize the secondary creature in the breath path";

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
	policy->activeStack(battleID, attacker);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK)
		<< "FHeroes2 must independently select a legal native melee attack";
	ASSERT_GE(callback->action->target.size(), 2u);
	const auto & action = *callback->action;
	const BattleHex from = action.target[0].hexValue;
	const BattleHex aimedHex = action.target[1].hexValue;
	const auto * aimedUnit = battle()->battleGetUnitByPos(aimedHex);
	ASSERT_NE(aimedUnit, nullptr);
	const auto affected = battle()->getAttackedBattleUnits(attacker, aimedUnit, aimedHex, false, from);
	EXPECT_FALSE(affected.empty())
		<< "The source-derived position and strike vector should reach a secondary native target";
	bool nativeMultipleHit = false;
	recorder.onAttackBeforeApply = [&](const BattleAttack & attack)
	{
		if(!attack.shot() && attack.bsa.size() > 1)
			nativeMultipleHit = true;
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action))
		<< "VCMI rejected FHeroes2's breath-position melee action";
	EXPECT_TRUE(nativeMultipleHit)
		<< "The authoritative server must actually hit multiple creatures, not just preview them";
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

// Preserved native regression from the FHeroes2 parity branch.
TEST_F(AIIntegrationNativeBattle, fheroes2MeleeUsesNativeHitHexOfWideTarget)
{
	// Pinned fheroes2 BattlePlanner::optimalAttackVector checks the actual
	// attacked cell of a two-cell defender. The selected VCMI hit hex must
	// be passed to BattleAttackInfo::defenderPos for damage preview.
	// Heroes II directional damage is not assumed equivalent to Heroes III.
	prepareBattle(CreatureID(10), 24, CreatureID(4), 1);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * attacker = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(10))
			attacker = unit.get();
	ASSERT_NE(attacker, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), attacker);
	const BattleHex nearbyHead(attacker->getPosition().toInt() + 5);
	ASSERT_TRUE(nearbyHead.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(nearbyHead), nullptr);
	const auto * nearbyWide = addStack(BattleSide::DEFENDER, CreatureID(4), nearbyHead, 12);
	ASSERT_NE(nearbyWide, nullptr);
	ASSERT_TRUE(nearbyWide->doubleWide());
	const auto occupied = nearbyWide->getHexes();
	ASSERT_EQ(occupied.size(), 2u);
	for(const auto & hex : occupied)
		ASSERT_EQ(battle()->battleGetUnitByPos(hex), nearbyWide);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	const auto reachability = view->getReachability(attacker);
	const auto available = view->battleGetAvailableHexes(reachability, attacker, false);
	bool hasLegalStrike = false;
	for(const auto & from : available)
		if(from.isAvailable() && !view->meleeAttackHexes(attacker, nearbyWide, from).empty())
			hasLegalStrike = true;
	ASSERT_TRUE(hasLegalStrike) << "The server must expose a genuine attack on the wide target";

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
	policy->activeStack(battleID, attacker);
	ASSERT_TRUE(callback->action.has_value());
	const auto & action = *callback->action;
	ASSERT_EQ(action.actionType, EActionType::WALK_AND_ATTACK)
		<< "FHeroes2 must choose melee independently: " << action.toString();
	ASSERT_GE(action.target.size(), 2u);
	const BattleHex from = action.target.front().hexValue;
	const BattleHex hit = action.target[1].hexValue;
	ASSERT_TRUE(available.contains(from));
	ASSERT_EQ(view->battleGetUnitByPos(hit), nearbyWide)
		<< "The source-derived target scorer should prefer the reachable, stronger wide stack";
	EXPECT_FALSE(view->meleeAttackHexes(attacker, nearbyWide, from).empty());
	BattleAttackInfo preview(attacker, nearbyWide, reachability.distances[from.toInt()], false);
	preview.attackerPos = from;
	preview.defenderPos = hit;
	const auto damage = view->battleEstimateDamage(preview);
	EXPECT_GE(damage.damage.max, damage.damage.min);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action))
		<< "The actual server rejected the selected wide-footprint melee attack";
	policy.reset();
	// The verified first melee action is deliberately submitted BEFORE the
	// continuation harness; requiring another attacker turn from the runner
	// would incorrectly fail when that opening strike ends the engagement.
	runToCompletion(std::nullopt, false);
	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_GT(recorder.damage, 0);
}

// Preserved native regression from the FHeroes2 parity branch.
TEST_F(AIIntegrationNativeBattle, fheroes2ArcherRetreatReleasesEnemyShooterFromHandFighting)
{
	prepareBattle(CreatureID(2), 30, CreatureID(2), 4);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * shooter = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(2))
			shooter = unit.get();
	ASSERT_NE(shooter, nullptr);
	ASSERT_TRUE(shooter->isShooter());
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);
	BattleHex blockerHex;
	const BattleHex shooterPosition = shooter->getPosition();
	for(const auto & hex : shooterPosition.getNeighbouringTiles())
		if(hex.isAvailable() && battle()->battleGetUnitByPos(hex) == nullptr)
		{
			blockerHex = hex;
			break;
		}
	ASSERT_TRUE(blockerHex.isValid());
	CStack * blocker = addStack(BattleSide::DEFENDER, CreatureID(2), blockerHex, 4);
	ASSERT_NE(blocker, nullptr);
	ASSERT_TRUE(blocker->isShooter());
	ASSERT_EQ(BattleHex::getDistance(shooter->getPosition(), blocker->getPosition()), 1);
	// Fixture-only bonuses make the enemy's *raw* speed exceed the maximum
	// possible distance on this board, while the active shooter remains
	// strictly more than two speed points faster as upstream requires.
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::STACKS_SPEED, BonusSource::OTHER, 20, BonusSourceID()));
	blocker->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::STACKS_SPEED, BonusSource::OTHER, 16, BonusSourceID()));
	ASSERT_GT(static_cast<int>(shooter->getMovementRange()),
		static_cast<int>(blocker->getMovementRange()) + 2);
	ASSERT_FALSE(battle()->battleCanShoot(shooter));
	ASSERT_EQ(std::count_if(battle()->stacks.begin(), battle()->stacks.end(),
		[&](const auto & unit)
		{
			return unit.get() != shooter && unit->alive()
				&& unit->unitSide() == BattleSide::ATTACKER;
		}), 0) << "The moving shooter must be the enemy archer's only melee engagement";

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
		<< "An archer engaged only by its moving target cannot project long-range melee pursuit: "
		<< callback->action->toString();
	ASSERT_FALSE(callback->action->target.empty());
	const auto destination = callback->action->target.front().hexValue;
	const auto distance = BattleHex::getDistance(destination, blocker->getPosition());
	EXPECT_GT(distance, 1) << "The escape must unblock the moving shooter's shot";
	EXPECT_LE(distance, static_cast<int>(blocker->getMovementRange()) + 1)
		<< "This retreat would be rejected if the enemy archer incorrectly kept melee pursuit";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Native VCMI server rejected the source-derived retreat";
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

// Preserved native regression from the FHeroes2 parity branch.
TEST_F(AIIntegrationNativeBattle, fheroes2BlockedArcherAvoidsPreemptiveMeleeCounter)
{
	prepareBattle(CreatureID(19), 60, CreatureID(16), 8);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * shooter = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(19))
			shooter = unit.get();
	ASSERT_NE(shooter, nullptr);
	ASSERT_TRUE(shooter->isShooter());
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);
	const BattleHex shooterPosition = shooter->getPosition();
	std::vector<BattleHex> adjacent;
	for(const auto & hex : shooterPosition.getNeighbouringTiles())
		if(hex.isAvailable() && !battle()->battleGetUnitByPos(hex))
			adjacent.push_back(hex);
	ASSERT_GE(adjacent.size(), 2u);
	auto * first = addStack(BattleSide::DEFENDER, CreatureID(19), adjacent[0], 12);
	auto * second = addStack(BattleSide::DEFENDER, CreatureID(19), adjacent[1], 12);
	ASSERT_NE(first, nullptr);
	ASSERT_NE(second, nullptr);
	ASSERT_FALSE(battle()->battleCanShoot(shooter));
	ASSERT_GE(static_cast<int>(first->getMovementRange()) + 2,
		static_cast<int>(shooter->getMovementRange()))
		<< "The engaged archer must not take the safe-escape branch";
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_FALSE(view->meleeAttackHexes(shooter, first, shooter->getPosition()).empty());
	ASSERT_FALSE(view->meleeAttackHexes(shooter, second, shooter->getPosition()).empty());
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
	const auto * originallyChosen = view->battleGetUnitByPos(callback->action->target[1].hexValue);
	ASSERT_TRUE(originallyChosen == first || originallyChosen == second);
	const auto * safeTarget = originallyChosen == first ? second : first;
	CStack * chosenStack = originallyChosen == first ? first : second;
	chosenStack->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::FIRST_STRIKE,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(19))));
	ASSERT_TRUE(originallyChosen->ableToRetaliate());
	BattleAttackInfo counter(originallyChosen, shooter, 0, false);
	counter.attackerPos = originallyChosen->getPosition();
	counter.defenderPos = shooter->getPosition();
	const auto forecast = view->battleEstimateDamage(counter);
	ASSERT_GT(forecast.damage.min, 0);
	ASSERT_LT(forecast.damage.max, shooter->getAvailableHealth());
	const auto initialHealth = shooter->getAvailableHealth();
	const auto initialCount = shooter->getCount();
	callback->action.reset();
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(callback->action->target.size(), 2u);
	EXPECT_EQ(view->battleGetUnitByPos(callback->action->target[1].hexValue), safeTarget)
		<< "A native preemptive counter must reduce the blocked archer's expected melee value";
	EXPECT_EQ(shooter->getAvailableHealth(), initialHealth);
	EXPECT_EQ(shooter->getCount(), initialCount)
		<< "First-strike preview must not damage a live stack";
	const int beforeMelee = recorder.meleeAttacks;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "Native server rejected the independently chosen safe melee target";
	EXPECT_GT(recorder.meleeAttacks, beforeMelee)
		<< "The accepted action must execute a real native melee attack";
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// Preserved native regression from the FHeroes2 parity branch.
TEST_F(AIIntegrationNativeBattle, fheroes2NativeRangedRetaliationChangesChosenTarget)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(2), 12); // Archer
	setArmy(defenderSideHero, CreatureID(2), 35);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(defenderSideHero->id, SlotID(1)), CreatureID(2).toCreature(), 35));
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	battle()->obstacles.clear();

	CStack * shooter = nullptr;
	std::vector<CStack *> targets;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->creatureId() != CreatureID(2))
			continue;
		if(unit->unitSide() == BattleSide::ATTACKER)
			shooter = unit.get();
		else
			targets.push_back(unit.get());
	}
	ASSERT_NE(shooter, nullptr);
	ASSERT_EQ(targets.size(), 2u);
	battle()->activeStack = shooter->unitId();
	// Find two unoccupied and nonadjacent hexes at precisely the same
	// distance: neither path length nor blocked shooting should decide.
	BattleHex first, second;
	for(int i = 0; i < GameConstants::BFIELD_SIZE && !first.isValid(); ++i)
	{
		const BattleHex a(i);
		if(!a.isAvailable() || battle()->battleGetUnitByPos(a))
			continue;
		const int radius = BattleHex::getDistance(a, shooter->getPosition());
		if(radius < 5 || radius > 10)
			continue;
		for(int j = i + 1; j < GameConstants::BFIELD_SIZE; ++j)
		{
			const BattleHex b(j);
			if(!b.isAvailable() || battle()->battleGetUnitByPos(b))
				continue;
			if(BattleHex::getDistance(b, shooter->getPosition()) == radius
				&& BattleHex::getDistance(a, b) >= 3)
			{
				first = a;
				second = b;
				break;
			}
		}
	}
	ASSERT_TRUE(first.isValid());
	ASSERT_TRUE(second.isValid());
	targets.front()->setPosition(first);
	targets.back()->setPosition(second);

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	for(const auto * target : targets)
	{
		ASSERT_TRUE(view->battleCanShoot(shooter, target->getPosition()));
		ASSERT_TRUE(view->battleCanShoot(target, shooter->getPosition()));
		ASSERT_TRUE(target->ableToRetaliate());
	}
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
	const auto baseline = *callback->action;
	callback->action.reset();
	ASSERT_EQ(baseline.actionType, EActionType::SHOOT);
	ASSERT_FALSE(baseline.target.empty());
	CStack * formerChoice = nullptr;
	CStack * safeTarget = nullptr;
	for(auto * target : targets)
	{
		if(target->unitId() == baseline.target.front().unitValue)
			formerChoice = target;
		else
			safeTarget = target;
	}
	ASSERT_NE(formerChoice, nullptr);
	ASSERT_NE(safeTarget, nullptr);
	BattleAttackInfo opening(shooter, formerChoice, 0, true);
	ASSERT_LT(view->battleEstimateDamage(opening).damage.max, formerChoice->getAvailableHealth());
	formerChoice->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::RANGED_RETALIATION,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(2))));
	ASSERT_TRUE(formerChoice->hasBonusOfType(BonusType::RANGED_RETALIATION));
	BattleAttackInfo counter(formerChoice, shooter, 0, true);
	const auto untouchedCounter = view->battleEstimateDamage(counter);
	ASSERT_GT(untouchedCounter.damage.min, 0);
	// Native RANGED_RETALIATION acts after the primary hit, with fewer
	// surviving archers. Compare detached forecasts, then verify that the
	// policy neither changes the live target nor invents a new action.
	const int64_t liveHealthBeforePreview = formerChoice->getAvailableHealth();
	const int64_t liveCountBeforePreview = formerChoice->getCount();
	auto afterShot = formerChoice->acquireState();
	const auto openingEstimate = view->battleEstimateDamage(opening);
	int64_t expectedHit = openingEstimate.damage.min
		+ (openingEstimate.damage.max - openingEstimate.damage.min) / 2;
	expectedHit = std::clamp<int64_t>(expectedHit, 0, afterShot->getAvailableHealth());
	afterShot->damage(expectedHit);
	ASSERT_TRUE(afterShot->alive());
	ASSERT_LT(afterShot->getCount(), liveCountBeforePreview);
	BattleAttackInfo reducedCounter(afterShot.get(), shooter, 0, true);
	const auto survivingCounter = view->battleEstimateDamage(reducedCounter);
	EXPECT_LT(survivingCounter.damage.min, untouchedCounter.damage.min);
	EXPECT_LE(survivingCounter.damage.max, untouchedCounter.damage.max);
	EXPECT_EQ(formerChoice->getAvailableHealth(), liveHealthBeforePreview);
	EXPECT_EQ(formerChoice->getCount(), liveCountBeforePreview);
	policy->activeStack(battleID, shooter);
	// FHeroes2 itself must evaluate the hypothetical survivor without
	// applying damage or deaths to the authoritative battle state.
	EXPECT_EQ(formerChoice->getAvailableHealth(), liveHealthBeforePreview);
	EXPECT_EQ(formerChoice->getCount(), liveCountBeforePreview);
	ASSERT_TRUE(callback->action.has_value());
	const auto reranked = *callback->action;
	ASSERT_EQ(reranked.actionType, EActionType::SHOOT);
	ASSERT_FALSE(reranked.target.empty());
	EXPECT_EQ(reranked.target.front().unitValue, safeTarget->unitId())
		<< "Native return-fire exposure should reverse an otherwise equal shot choice";

	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	ASSERT_FALSE(callback->action->target.empty());
	EXPECT_EQ(callback->action->target.front().unitValue, safeTarget->unitId());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action));
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics");
	EXPECT_GT(recorder.shots, 0);
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// Preserved native regression from the FHeroes2 parity branch.
TEST_F(AIIntegrationNativeBattle, fheroes2RangedRetaliationExecutesOnNativeServer)
{
	prepareBattle(CreatureID(2), 12, CreatureID(2), 35);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * shooter = nullptr;
	CStack * defender = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER)
			shooter = unit.get();
		else
			defender = unit.get();
	}
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(defender, nullptr);
	defender->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::RANGED_RETALIATION,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(2))));
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_TRUE(view->battleCanShoot(shooter, defender->getPosition()));
	ASSERT_TRUE(view->battleCanShoot(defender, shooter->getPosition()));
	ASSERT_TRUE(defender->ableToRetaliate());
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);
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
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	const int before = recorder.shots;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action));
	EXPECT_GE(recorder.shots - before, 2)
		<< "A real native countershot must execute before the following turn";
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics");
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// Preserved native regression from the FHeroes2 parity branch.
TEST_F(AIIntegrationNativeBattle, fheroes2NativeMeleeFirstStrikePrecedesOwnHit)
{
	prepareBattle(CreatureID(4), 40, CreatureID(4), 15);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * attacker = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER)
			attacker = unit.get();
	ASSERT_NE(attacker, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), attacker);
	BattleHex adjacent;
	const BattleHex attackerHex = attacker->getPosition();
	for(const auto & hex : attackerHex.getNeighbouringTiles())
		if(hex.isAvailable() && !battle()->battleGetUnitByPos(hex))
		{
			adjacent = hex;
			break;
		}
	ASSERT_TRUE(adjacent.isValid());
	auto * defender = addStack(BattleSide::DEFENDER, CreatureID(4), adjacent, 15);
	ASSERT_NE(defender, nullptr);
	defender->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::FIRST_STRIKE,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(4))));
	ASSERT_TRUE(defender->ableToRetaliate());
	ASSERT_FALSE(battle()->meleeAttackHexes(
		attacker, defender, attacker->getPosition()).empty());
	const int64_t initialHealth = attacker->getAvailableHealth();

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	BattleAttackInfo counterPreview(defender, attacker, 0, false);
	counterPreview.attackerPos = defender->getPosition();
	counterPreview.defenderPos = attacker->getPosition();
	EXPECT_GT(view->battleEstimateDamage(counterPreview).damage.min, 0)
		<< "The fixture must actually expose the attacker to a damaging preemptive hit";
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
	policy->activeStack(battleID, attacker);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(callback->action->target.size(), 2u);
	EXPECT_EQ(view->battleGetUnitByPos(callback->action->target[1].hexValue), defender);
	EXPECT_EQ(attacker->getAvailableHealth(), initialHealth)
		<< "Detached preemptive-damage forecast must not change the live stack";

	std::vector<ui32> meleeOrder;
	std::vector<bool> counterOrder;
	recorder.onAttackBeforeApply = [&](const BattleAttack & attack)
	{
		if(!attack.shot())
		{
			meleeOrder.push_back(attack.stackAttacking);
			counterOrder.push_back(attack.counter());
		}
	};
	const int before = recorder.meleeAttacks;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(
		battleID, player, *callback->action));
	ASSERT_GE(recorder.meleeAttacks - before, 2);
	ASSERT_GE(meleeOrder.size(), 2u);
	EXPECT_EQ(meleeOrder[0], defender->unitId());
	EXPECT_TRUE(counterOrder[0]);
	EXPECT_EQ(meleeOrder[1], attacker->unitId());
	EXPECT_LT(attacker->getAvailableHealth(), initialHealth);
	recorder.onAttackBeforeApply = {};
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics");
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

// Preserved native regression from the FHeroes2 parity branch.
TEST_F(AIIntegrationNativeBattle, fheroes2NativeFirstStrikeRangedCounterPrecedesOwnShot)
{
	prepareBattle(CreatureID(2), 90, CreatureID(2), 35);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * shooter = nullptr;
	CStack * defender = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER)
			shooter = unit.get();
		else
			defender = unit.get();
	}
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(defender, nullptr);
	defender->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::RANGED_RETALIATION,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(2))));
	defender->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::FIRST_STRIKE,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(2))));
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanShoot(shooter, defender->getPosition()));
	ASSERT_TRUE(view->battleCanShoot(defender, shooter->getPosition()));
	ASSERT_TRUE(defender->ableToRetaliate());
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);
	const int64_t initialHealth = shooter->getAvailableHealth();
	const int64_t initialCount = shooter->getCount();

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
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	EXPECT_EQ(shooter->getAvailableHealth(), initialHealth);
	EXPECT_EQ(shooter->getCount(), initialCount)
		<< "Forecast must not apply first-strike casualties to live stacks";

	std::vector<ui32> shotOrder;
	std::vector<bool> isCounter;
	recorder.onAttackBeforeApply = [&](const BattleAttack & attack)
	{
		if(attack.shot())
		{
			shotOrder.push_back(attack.stackAttacking);
			isCounter.push_back(attack.counter());
		}
	};
	const auto before = recorder.shots;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action));
	ASSERT_GE(recorder.shots - before, 2);
	ASSERT_GE(shotOrder.size(), 2u);
	EXPECT_EQ(shotOrder[0], defender->unitId())
		<< "VCMI FIRST_STRIKE must be preemptive, not a post-hit retaliation";
	EXPECT_TRUE(isCounter[0]);
	EXPECT_EQ(shotOrder[1], shooter->unitId());
	EXPECT_LT(shooter->getAvailableHealth(), initialHealth);
	EXPECT_GT(recorder.damage, 0);
	recorder.onAttackBeforeApply = {};
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics");
	EXPECT_FALSE(recorder.battleResults.empty());
}

// Preserved native regression from the FHeroes2 parity branch.
TEST_F(AIIntegrationNativeBattle, fheroes2BlindedFirstStrikeCannotPreemptRangedShot)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(2), 12);
	setArmy(defenderSideHero, CreatureID(2), 35);
	teachBattleSpell(attackerSideHero, SpellID::BLIND);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(defenderSideHero->id, SlotID(1)), CreatureID(2).toCreature(), 35));
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	battle()->obstacles.clear();

	CStack * shooter = nullptr;
	std::vector<CStack *> targets;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->creatureId() != CreatureID(2))
			continue;
		if(unit->unitSide() == BattleSide::ATTACKER)
			shooter = unit.get();
		else
			targets.push_back(unit.get());
	}
	ASSERT_NE(shooter, nullptr);
	ASSERT_EQ(targets.size(), 2u);
	battle()->activeStack = shooter->unitId();
	BattleHex first, second;
	for(int i = 0; i < GameConstants::BFIELD_SIZE && !first.isValid(); ++i)
	{
		const BattleHex a(i);
		if(!a.isAvailable() || battle()->battleGetUnitByPos(a))
			continue;
		const int radius = BattleHex::getDistance(a, shooter->getPosition());
		if(radius < 5 || radius > 10)
			continue;
		for(int j = i + 1; j < GameConstants::BFIELD_SIZE; ++j)
		{
			const BattleHex b(j);
			if(b.isAvailable() && !battle()->battleGetUnitByPos(b)
				&& BattleHex::getDistance(b, shooter->getPosition()) == radius
				&& BattleHex::getDistance(a, b) >= 3)
			{
				first = a;
				second = b;
				break;
			}
		}
	}
	ASSERT_TRUE(first.isValid());
	ASSERT_TRUE(second.isValid());
	targets.front()->setPosition(first);
	targets.back()->setPosition(second);
	for(auto * target : targets)
		target->addNewBonus(std::make_shared<Bonus>(
			BonusDuration::PERMANENT, BonusType::RANGED_RETALIATION,
			BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(2))));

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	for(const auto * target : targets)
	{
		ASSERT_TRUE(view->battleCanShoot(shooter, target->getPosition()));
		ASSERT_TRUE(view->battleCanShoot(target, shooter->getPosition()));
		ASSERT_TRUE(target->ableToRetaliate());
	}
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
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	ASSERT_FALSE(callback->action->target.empty());
	const auto baselineTarget = callback->action->target.front().unitValue;
	CStack * chosen = targets[0]->unitId() == baselineTarget ? targets[0] : targets[1];
	ASSERT_EQ(chosen->unitId(), baselineTarget);
	BattleAttackInfo preview(shooter, chosen, 0, true);
	ASSERT_LT(view->battleEstimateDamage(preview).damage.max, chosen->getAvailableHealth());

	chosen->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::FIRST_STRIKE,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(2)),
		BonusCustomSubtype::damageTypeMelee));
	ASSERT_TRUE(chosen->hasBonusOfType(BonusType::FIRST_STRIKE));
	ASSERT_FALSE(chosen->hasBonus(
		Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeAll)
			.Or(Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeRanged))));
	const int64_t shooterHealth = shooter->getAvailableHealth();
	const int64_t targetHealth = chosen->getAvailableHealth();
	callback->action.reset();
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	ASSERT_FALSE(callback->action->target.empty());
	EXPECT_EQ(callback->action->target.front().unitValue, baselineTarget)
		<< "Melee-only FIRST_STRIKE must not change the equivalent ranged-retaliation target score";
	EXPECT_EQ(shooter->getAvailableHealth(), shooterHealth);
	EXPECT_EQ(chosen->getAvailableHealth(), targetHealth);

	// Heroes II has no FIRST_STRIKE; the native VCMI processor allows
	// ranged FIRST_STRIKE only while the defender can act. Before Blind,
	// this must change FHeroes2's choice. A real server Blind must reverse
	// that choice without modifying either stack during damage preview.
	chosen->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::FIRST_STRIKE,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(2)),
		BonusCustomSubtype::damageTypeRanged));
	ASSERT_TRUE(chosen->hasBonus(
		Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeRanged)));
	callback->action.reset();
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	ASSERT_FALSE(callback->action->target.empty());
	EXPECT_NE(callback->action->target.front().unitValue, baselineTarget)
		<< "An active native FIRST_STRIKE ranged counter must reduce the expected shot value";

	ASSERT_TRUE(castOn(attackerSideHero, SpellID::BLIND, chosen))
		<< "The status change must come from the authoritative spell processor";
	ASSERT_TRUE(chosen->hasBonusOfType(BonusType::NOT_ACTIVE));
	// The exception matters even when the disabled shooter still has
	// ammunition and a legal return-fire line: only FIRST_STRIKE is
	// suppressed by the native ranged sequence's NOT_ACTIVE check.
	ASSERT_TRUE(view->battleCanShoot(chosen, shooter->getPosition()));
	ASSERT_TRUE(chosen->ableToRetaliate());
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);
	callback->action.reset();
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	ASSERT_FALSE(callback->action->target.empty());
	EXPECT_EQ(callback->action->target.front().unitValue, baselineTarget)
		<< "VCMI cannot preemptively return fire while the defender is Blind";
	EXPECT_EQ(shooter->getAvailableHealth(), shooterHealth);
	EXPECT_EQ(chosen->getAvailableHealth(), targetHealth);

	std::vector<ui32> shotOrder;
	std::vector<bool> counters;
	recorder.onAttackBeforeApply = [&](const BattleAttack & attack)
	{
		if(attack.shot())
		{
			shotOrder.push_back(attack.stackAttacking);
			counters.push_back(attack.counter());
		}
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action));
	ASSERT_FALSE(shotOrder.empty());
	EXPECT_EQ(shotOrder.front(), shooter->unitId())
		<< "Server must execute the shooter's attack before any return fire from a blinded target";
	EXPECT_FALSE(counters.front());
	if(shotOrder.size() > 1)
	{
		EXPECT_EQ(shotOrder[1], chosen->unitId());
		EXPECT_TRUE(counters[1]);
	}
	recorder.onAttackBeforeApply = {};
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics");
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}
