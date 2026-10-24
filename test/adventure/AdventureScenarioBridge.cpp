/*
 * Public-map planner integration through real callback/server state transitions.
 * Staged EXCLUDE_FROM_ALL experiment. See AdventureScenarioBridge.md.
 */
#include "StdInc.h"
#include "../../AI/Nullkiller2/AIGateway.h"
#include "../../AI/Nullkiller2/AIUtility.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/pathfinder/CGPathNode.h"
#include "../../AI/Nullkiller2/Engine/Nullkiller.h"
#include "../../lib/CConfigHandler.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/StartInfo.h"
#include "../../lib/LoadProgress.h"
#include "../../lib/UnlockGuard.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/ConditionalWait.h"
#include "../../lib/logging/CLogger.h"
#include <iostream>
#include "../../lib/callback/CCallback.h"
#include "../../lib/callback/IClient.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/filesystem/ISimpleResourceLoader.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/mapping/CMapService.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include "../../lib/serializer/CTypeList.h"
#include "../../server/CGameHandler.h"
#include "../../server/IGameServer.h"
#include "../../server/processors/TurnOrderProcessor.h"
#include <vcmi/Environment.h>
#include <vcmi/events/EventBus.h>
#include <chrono>
#include <thread>
#include <shared_mutex>

namespace NK2AI
{
// This executable does not link PlanningRiskSchedulerRegression.cpp. Reuse its
// private regression friend without exposing a new production test API.
class PlanningRiskSchedulerRegressionAccess
{
public:
	static void plannerTurn(AIGateway & gateway)
	{
		static_assert(!AIGateway::ENABLE_STAGED_RISK_PREPARATION,
			"Map bridge must not silently activate experimental risk preparation");
		const auto generation = gateway.planningTurnGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
		gateway.status.startedTurn();
		gateway.withPlanningTurnLocks([&gateway, generation](std::shared_lock<std::shared_mutex> & gameStateLock)
		{
			gateway.nullkiller->makingTurnInterruption.reset();
			Nullkiller::planningTurnLocks->ensureReady = [&gateway, &gameStateLock, generation]
			{
				gateway.ensurePlanningTurnReady(gameStateLock, generation);
			};
			gateway.ensurePlanningTurnReady(gameStateLock, generation);
			AIGateway::memorizeVisitableObjs(gateway.nullkiller->memory, gateway.nullkiller->dangerHitMap,
				gateway.playerID, gateway.cc);
			AIGateway::memorizeRevisitableObjs(gateway.nullkiller->memory, gateway.playerID, gateway.cc);
			gateway.nullkiller->makeTurn();
			gateway.ensurePlanningTurnReady(gameStateLock, generation);
		}, generation);
	}
};
}

namespace
{
// Single-threaded fixture driver calls the production planner directly. It does
// NOT pretend to implement the asynchronous GUI/network YourTurn lifecycle.
class ScenarioBridge final : public IGameServer, public IClient
{
public:
	std::unique_ptr<CGameHandler> handler;
	std::shared_ptr<CCallback> callback;
	NK2AI::AIGateway * gateway = nullptr;
	EServerState status = EServerState::GAMEPLAY;
	PlayerColor active = PlayerColor::NEUTRAL;
	int requests = 0;
	int productiveMoves = 0;
	int turnStarts = 0;
	int turnEnds = 0;
	int recruitmentRequests = 0;
	bool redWon = false;
	std::vector<std::string> packets;
	std::vector<std::string> unsupported;
	std::thread::id driver = std::this_thread::get_id();
	std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

	[[noreturn]] void reject(const std::string & reason)
	{
		unsupported.push_back(reason);
		throw std::runtime_error("UNSUPPORTED_ADVENTURE_BRIDGE: " + reason);
	}

	void traceHeroes(const char * phase) const
	{
		if(!callback) return;
		for(const auto * hero : callback->getHeroesInfo())
		{
			const auto position = hero->anchorPos();
			std::cout << "ADVENTURE_BRIDGE phase=" << phase << " request=" << requests
				<< " hero=" << hero->id.getNum() << " x=" << position.x << " y=" << position.y << " z=" << position.z
				<< " movement=" << hero->movementPointsRemaining() << std::endl;
		}
	}

	void checkBudget()
	{
		if(std::this_thread::get_id() != driver)
			reject("Asynchronous query/worker request reached synchronous driver");
		if(requests > 5000 || std::chrono::steady_clock::now() - started > std::chrono::seconds(30))
			reject("Request or wall-time budget exhausted");
	}

	void setState(EServerState value) override { status = value; }
	EServerState getState() const override { return status; }
	bool isPlayerHost(const PlayerColor & color) const override { return color == PlayerColor(0); }
	bool hasPlayerAt(PlayerColor player, GameConnectionID connection) const override
	{
		return player.isValidPlayer() && connection == GameConnectionID(0);
	}
	bool hasBothPlayersAtSameConnection(PlayerColor, PlayerColor) const override { return true; }

	bool supportedStatePacket(CPackForClient & pack) const
	{
		return dynamic_cast<NewTurn *>(&pack) || dynamic_cast<PlayerStartsTurn *>(&pack)
			|| dynamic_cast<PlayerEndsTurn *>(&pack) || dynamic_cast<PlayerEndsGame *>(&pack)
			|| dynamic_cast<PlayerBlocked *>(&pack) || dynamic_cast<TryMoveHero *>(&pack)
			|| dynamic_cast<HeroVisit *>(&pack) || dynamic_cast<HeroVisitCastle *>(&pack)
			|| dynamic_cast<SetResources *>(&pack) || dynamic_cast<FoWChange *>(&pack)
			|| dynamic_cast<SetMovePoints *>(&pack) || dynamic_cast<SetMana *>(&pack)
			|| dynamic_cast<SetObjectProperty *>(&pack) || dynamic_cast<ChangeObjectVisitors *>(&pack)
			|| dynamic_cast<SetAvailableHero *>(&pack) || dynamic_cast<SetAvailableCreatures *>(&pack)
			|| dynamic_cast<SetAvailableArtifacts *>(&pack) || dynamic_cast<DaysWithoutTown *>(&pack)
			|| dynamic_cast<TurnTimeUpdate *>(&pack) || dynamic_cast<InfoWindow *>(&pack)
			|| dynamic_cast<ChangeFormation *>(&pack) || dynamic_cast<RemoveObject *>(&pack);
	}

	void applyPack(CPackForClient & pack) override
	{
		checkBudget();
		packets.push_back(typeid(pack).name());
		// Queries, battles and unsupported state mutations are
		// rejected BEFORE application, rather than silently losing AI callbacks.
		if(!supportedStatePacket(pack))
			reject(std::string("server packet ") + typeid(pack).name());
		if(auto * turn = dynamic_cast<PlayerStartsTurn *>(&pack))
		{
			if(turn->queryID != QueryID::NONE)
				reject("PlayerStartsTurn has a blocking query");
			active = turn->player;
			++turnStarts;
		}
		if(dynamic_cast<PlayerEndsTurn *>(&pack))
			++turnEnds;
		if(auto * block = dynamic_cast<PlayerBlocked *>(&pack))
			if(block->reason == PlayerBlocked::UPCOMING_BATTLE)
				reject("Battle requires separately qualified battle/event bridge");
		if(auto * move = dynamic_cast<TryMoveHero *>(&pack))
			if(move->result != TryMoveHero::SUCCESS && move->result != TryMoveHero::FAILED && move->result != TryMoveHero::BLOCKING_VISIT)
				reject("Teleport/boat movement not implemented");
		if(gateway)
			if(auto * property = dynamic_cast<SetObjectProperty *>(&pack))
			{
				const auto * object = callback->getObj(property->id, false);
				if(object && callback->isVisible(object)) gateway->beforeObjectPropertyChanged(property);
			}
		if(auto * removal = dynamic_cast<RemoveObject *>(&pack))
		{
			const auto * object = handler->gameState().getObjInstance(removal->objectID);
			if(!object) reject("RemoveObject references missing object");
			const auto * hero = dynamic_cast<const CGHeroInstance *>(object);
			if(hero && hero->inBoat()) reject("Boat removal callbacks not qualified");
			// Match the client's pre-delete notification ordering, without its
			// hidden enemy-hero deletion exception: never grant unseen knowledge.
			if(gateway && callback->isVisible(object))
				gateway->objectRemoved(object, removal->initiator);
			std::cout << "ADVENTURE_BRIDGE remove=" << removal->objectID.getNum()
				<< " owner=" << object->getOwner().getNum() << std::endl;
		}
		// The state is shared with the player callback: apply exactly once.
		handler->gs->apply(pack);
		if(auto * move = dynamic_cast<TryMoveHero *>(&pack))
		{
			const auto * hero = handler->gameState().getHero(move->id);
			if(move->result == TryMoveHero::SUCCESS && hero && hero->anchorPos() == move->end && move->end != move->start)
				++productiveMoves;
		}
		if(auto * property = dynamic_cast<SetObjectProperty *>(&pack))
		{
			const auto * object = handler->gameState().getObjInstance(property->id);
			if(object && callback && callback->isVisible(object))
				std::cout << "ADVENTURE_BRIDGE object=" << object->instanceName
					<< " owner=" << object->getOwner().getNum() << std::endl;
		}
		if(auto * end = dynamic_cast<PlayerEndsGame *>(&pack))
			if(end->player == PlayerColor(0))
				redWon = end->victoryLossCheckResult.victory();
		if(!gateway)
			return;

		// Forward only information the real player's callbacks can see.
		if(auto * move = dynamic_cast<TryMoveHero *>(&pack))
		{
			const auto * hero = handler->gameState().getHero(move->id);
			if(hero && (hero->getOwner() == PlayerColor(0) || callback->isVisible(hero)))
			{
				if(hero->getOwner() == PlayerColor(0))
					gateway->tileRevealed(move->fowRevealed);
				gateway->heroMoved(*move, hero->getOwner() == PlayerColor(0));
			}
		}
		else if(auto * visit = dynamic_cast<HeroVisit *>(&pack))
		{
			if(visit->player == PlayerColor(0))
				gateway->heroVisit(callback->getHero(visit->heroId), callback->getObj(visit->objId, false), visit->starting);
		}
		else if(auto * block = dynamic_cast<PlayerBlocked *>(&pack))
		{
			if(block->player == PlayerColor(0))
				gateway->playerBlocked(block->reason, block->startOrEnd == PlayerBlocked::BLOCKADE_STARTED);
		}
		else if(auto * resource = dynamic_cast<SetResources *>(&pack))
		{
			if(resource->player == PlayerColor(0)) gateway->receivedResource();
		}
		else if(auto * property = dynamic_cast<SetObjectProperty *>(&pack))
		{
			const auto * object = callback->getObj(property->id, false);
			if(object && callback->isVisible(object)) gateway->objectPropertyChanged(property);
		}
		else if(auto * fow = dynamic_cast<FoWChange *>(&pack))
		{
			if(fow->player == PlayerColor(0))
			{
				if(fow->mode == ETileVisibility::REVEALED) gateway->tileRevealed(fow->tiles);
				else gateway->tileHidden(fow->tiles);
			}
		}
		else if(auto * endTurn = dynamic_cast<PlayerEndsTurn *>(&pack))
			gateway->playerEndsTurn(endTurn->player);
		else if(auto * end = dynamic_cast<PlayerEndsGame *>(&pack))
			gateway->gameOver(end->player, end->victoryLossCheckResult);

		// Conservative invalidation for every applied state delta. This changes
		// caching only, never grants information or fabricates success.
		gateway->invalidatePaths();
		if(dynamic_cast<RemoveObject *>(&pack)) gateway->objectRemovedAfter();
	}

	void sendPack(CPackForClient & pack, GameConnectionID) override
	{
		if(auto * applied = dynamic_cast<PackageApplied *>(&pack))
		{
			if(gateway && applied->player == PlayerColor(0)) gateway->requestRealized(applied);
			return;
		}
		if(dynamic_cast<PackageReceived *>(&pack)) return;
		reject(std::string("direct packet ") + typeid(pack).name());
	}

	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor, const BattleID &, const BattleStateInfoForRetreat &) override
	{
		reject("Battle retreat decision");
	}

	int sendRequest(const CPackForServer & request, PlayerColor player, bool waitTillRealize) override
	{
		checkBudget();
		if(!(dynamic_cast<const MoveHero *>(&request) || dynamic_cast<const EndTurn *>(&request)
			|| dynamic_cast<const SetFormation *>(&request)))
			reject(std::string("client request ") + typeid(request).name());
		request.player = player;
		request.requestID = ++requests;
		if(gateway && player == PlayerColor(0)) gateway->requestSent(&request, request.requestID);
		if(dynamic_cast<const RecruitCreatures *>(&request)) ++recruitmentRequests;
		// Real ownership, active-turn, movement, resource and server-query checks.
		// PackageApplied(true) is intentionally NOT treated as action success.
		const int movesBefore = productiveMoves;
		traceHeroes("before");
		std::cout << "ADVENTURE_BRIDGE request=" << requests << " day=" << handler->gameState().getDate(Date::DAY)
			<< " player=" << player.getNum() << " type=" << typeid(request).name() << std::endl;
		if(waitTillRealize)
		{
			auto unlock = vstd::makeUnlockSharedGuard(CGameState::mutex);
			handler->handleReceivedPack(GameConnectionID(0), const_cast<CPackForServer &>(request));
		}
		else
			handler->handleReceivedPack(GameConnectionID(0), const_cast<CPackForServer &>(request));
		std::cout << "ADVENTURE_BRIDGE applied_request=" << requests << " productive_moves_delta=" << productiveMoves - movesBefore
			<< " red_won=" << redWon << std::endl;
		traceHeroes("after");
		return request.requestID;
	}
};

class PlayerEnvironment final : public Environment
{
	std::shared_ptr<CCallback> callback;
	events::EventBus bus;
public:
	explicit PlayerEnvironment(std::shared_ptr<CCallback> value) : callback(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID &) const override { throw std::runtime_error("Unsupported battle environment"); }
	const GameCb * game() const override { return callback.get(); }
	vstd::CLoggerBase * logger() const override { return logAi; }
	events::EventBus * eventBus() const override { return const_cast<events::EventBus *>(&bus); }
};

class AdventureScenarioBridge : public ::testing::Test
{
protected:
	ScenarioBridge bridge;
	std::shared_ptr<PlayerEnvironment> environment;
	std::unique_ptr<NK2AI::AIGateway> gateway;

	void initialize(const std::string & scenario)
	{
		Settings seed = settings.write["server"]["seed"];
		seed->Integer() = 17041;
		StartInfo start;
		start.mapname = "test/AdventureEvaluation/" + scenario;
		start.difficulty = 3;
		start.mode = EStartMode::NEW_GAME;
		CMapService maps;
		ASSERT_TRUE(CResourceHandler::get()->existsResource(ResourcePath(start.mapname, EResType::MAP)))
			<< "Generated map missing: run from build/bin after vcmi_adventure_scenario_maps";
		auto header = maps.loadMapHeader(ResourcePath(start.mapname, EResType::MAP));
		ASSERT_NE(header, nullptr);
		for(size_t i = 0; i < header->players.size(); ++i)
		{
			const auto & info = header->players[i];
			if(!info.canHumanPlay && !info.canComputerPlay) continue;
			auto & player = start.playerInfos[PlayerColor(i)];
			player.color = PlayerColor(i);
			player.name = "Public scenario player";
			player.castle = info.defaultCastle();
			player.hero = info.defaultHero();
			player.bonus = PlayerStartingBonus::RESOURCE; // Normal explicit bonus, not random artifact/gold.
			player.connectedPlayerIDs.clear(); // actual AI players, no omniscient human callback
		}
		bridge.handler = std::make_unique<CGameHandler>(bridge);
		Load::ProgressAccumulator progress;
		bridge.handler->init(&start, progress); // Initializes real event bus, scripting and turn order.
		bridge.callback = std::make_shared<CCallback>(bridge.handler->gs, PlayerColor(0), &bridge);
		// Refuse ambiguous/all-of-type victory controls before running a turn.
		for(const auto & event : bridge.handler->gameState().getMap().triggeredEvents)
		{
			event.trigger.morph([this](const EventCondition & condition) -> EventExpression::Variant
			{
				if(condition.condition == EventCondition::CONTROL)
				{
					const auto * expected = object(condition.objectInstanceName);
					if(!expected || condition.objectID != expected->id || condition.position != expected->visitablePos())
						throw std::runtime_error("Fixture CONTROL did not resolve exact named object/visitable position");
				}
				return condition;
			});
		}
		bridge.handler->start(false);
		ASSERT_EQ(bridge.active, PlayerColor(0));
		ASSERT_EQ(bridge.handler->gameState().getDate(Date::DAY), 1);
	}

	void initializePlanner()
	{
		environment = std::make_shared<PlayerEnvironment>(bridge.callback);
		gateway = std::make_unique<NK2AI::AIGateway>();
		gateway->initGameInterface(environment, bridge.callback);
		bridge.gateway = gateway.get();
		ASSERT_FALSE(gateway->nullkiller->isOpenMap()) << "No map-reveal cheat allowed in fixture";
	}

	const CGObjectInstance * object(const std::string & name) const
	{
		for(const auto * candidate : bridge.handler->gameState().getMap().getObjects())
			if(candidate->instanceName == name) return candidate;
		return nullptr;
	}

	void plannerTurnsUntil(int deadline)
	{
		while(bridge.handler->gameState().getDate(Date::DAY) <= deadline
			&& bridge.callback->getPlayerStatus(PlayerColor(0)) == EPlayerStatus::INGAME)
		{
			bridge.checkBudget();
			if(bridge.active == PlayerColor(0))
			{
				try
				{
					NK2AI::PlanningRiskSchedulerRegressionAccess::plannerTurn(*gateway);
				}
				catch(const InterruptionRequestedException &)
				{
					if(bridge.callback->getPlayerStatus(PlayerColor(0)) == EPlayerStatus::INGAME) throw;
				}
			}
			if(bridge.callback->getPlayerStatus(PlayerColor(0)) != EPlayerStatus::INGAME) break;
			// Blue is a deliberately idle opponent in the first map-only cases.
			CCallback activeCallback(bridge.handler->gs, bridge.active, &bridge);
			activeCallback.endTurn();
		}
	}

	void TearDown() override
	{
		bridge.gateway = nullptr;
		if(gateway) gateway->finish();
		gateway.reset();
		EXPECT_TRUE(bridge.unsupported.empty());
	}
};

TEST_F(AdventureScenarioBridge, RealCallbackMoveConsumesMovementAndChangesPosition)
{
	ASSERT_NO_FATAL_FAILURE(initialize("productive_move"));
	const auto * hero = bridge.callback->getHeroesInfo().at(0);
	const auto before = hero->anchorPos();
	const auto movement = hero->movementPointsRemaining();
	bridge.callback->moveHero(hero, before + int3(1, 0, 0), false);
	EXPECT_EQ(hero->anchorPos(), before + int3(1, 0, 0));
	EXPECT_LT(hero->movementPointsRemaining(), movement);
	EXPECT_EQ(bridge.productiveMoves, 1);
}

TEST_F(AdventureScenarioBridge, ProductionPlannerCapturesReachableMine)
{
	ASSERT_NO_FATAL_FAILURE(initialize("productive_move"));
	ASSERT_NO_FATAL_FAILURE(initializePlanner());
	const auto * mine = object("safe_objective");
	ASSERT_NE(mine, nullptr);
	ASSERT_TRUE(bridge.callback->isVisible(mine));
	plannerTurnsUntil(2);
	EXPECT_EQ(mine->getOwner(), PlayerColor(0));
	EXPECT_GT(bridge.productiveMoves, 0);
	EXPECT_TRUE(bridge.redWon);
}

TEST_F(AdventureScenarioBridge, StableVisibleGoalsMakeMultiTurnProgress)
{
	ASSERT_NO_FATAL_FAILURE(initialize("stable_goal_progress"));
	ASSERT_NO_FATAL_FAILURE(initializePlanner());
	const auto * north = object("objective_north");
	const auto * south = object("objective_south");
	ASSERT_NE(north, nullptr); ASSERT_NE(south, nullptr);
	ASSERT_TRUE(bridge.callback->isVisible(north)); ASSERT_TRUE(bridge.callback->isVisible(south));
	plannerTurnsUntil(6);
	EXPECT_EQ(north->getOwner(), PlayerColor(0));
	EXPECT_EQ(south->getOwner(), PlayerColor(0));
	EXPECT_GT(bridge.productiveMoves, 0);
	EXPECT_TRUE(bridge.redWon);
}

TEST_F(AdventureScenarioBridge, GuardedObjectiveDoesNotPreventSafeAlternative)
{
	ASSERT_NO_FATAL_FAILURE(initialize("blocked_goal_alternative"));
	ASSERT_NO_FATAL_FAILURE(initializePlanner());
	const auto * safe = object("safe_objective");
	const auto * guarded = object("guarded_reward");
	ASSERT_NE(safe, nullptr); ASSERT_NE(guarded, nullptr);
	ASSERT_TRUE(bridge.callback->isVisible(safe)); ASSERT_TRUE(bridge.callback->isVisible(guarded));
	const auto * monster = dynamic_cast<const CGCreature *>(guarded);
	ASSERT_NE(monster, nullptr) << "Known-danger fixture requires exposed monster, not hidden resource guards";
	ASSERT_EQ(monster->CArmedInstance::stacksCount(), 1);
	ASSERT_EQ(monster->getStackCount(SlotID(0)), 999);
	ASSERT_EQ(monster->CArmedInstance::getCreature(SlotID(0))->getId(), CreatureID(0)); // core pikeman
	const auto danger = gateway->nullkiller->dangerEvaluator->evaluateDanger(guarded);
	ASSERT_GT(danger, 0u);
	for(const auto * hero : bridge.callback->getHeroesInfo())
	{
		ASSERT_EQ(hero->getStackCount(SlotID(0)), 20);
		ASSERT_EQ(hero->getCreature(SlotID(0))->getId(), CreatureID(0));
		ASSERT_FALSE(NK2AI::isSafeToVisit(hero, danger, gateway->nullkiller->settings->getSafeAttackRatio()));
		std::cout << "ADVENTURE_BRIDGE known_guard=" << guarded->id.getNum()
			<< " guard_count=" << monster->getStackCount(SlotID(0)) << " guard_strength=" << monster->getArmyStrength()
			<< " evaluated_danger=" << danger << " hero=" << hero->id.getNum()
			<< " hero_strength=" << hero->getArmyStrength() << std::endl;
	}
	const auto * helper = dynamic_cast<const CGHeroInstance *>(object("hero_helper"));
	ASSERT_NE(helper, nullptr);
	CGPath safePath;
	ASSERT_TRUE(gateway->nullkiller->getPathsInfo(helper)->getPath(safePath, safe->visitablePos()));
	ASSERT_GT(safePath.nodes.size(), 1u);
	ASSERT_LE(safePath.lastNode().turns, 1);
	for(const auto & node : safePath.nodes)
	{
		ASSERT_NE(node.action, EPathNodeAction::BATTLE);
		ASSERT_NE(node.action, EPathNodeAction::TELEPORT_BATTLE);
		ASSERT_EQ(gateway->nullkiller->dangerEvaluator->evaluateDanger(node.coord, helper, true), 0u);
	}
	std::cout << "ADVENTURE_BRIDGE safe_mine=" << safe->id.getNum()
		<< " helper=" << helper->id.getNum() << " route_turns=" << int(safePath.lastNode().turns) << std::endl;
	plannerTurnsUntil(2);
	EXPECT_EQ(safe->getOwner(), PlayerColor(0));
	EXPECT_NE(object("guarded_reward"), nullptr);
	EXPECT_NE(object("blocked_reward"), nullptr);
	EXPECT_EQ(bridge.callback->getHeroesInfo().size(), 2u);
	EXPECT_GT(bridge.productiveMoves, 0);
	EXPECT_TRUE(bridge.redWon);
}
}
