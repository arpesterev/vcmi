/*
 * Forced server battles: an outcome oracle independent of adventure decisions.
 * See ForcedBattleReference.md for policy, scope and reproducibility limits.
 */
#include "StdInc.h"
#include "PlanningBattleSnapshotChecks.h"
#include "../../AI/Nullkiller2/Engine/Experimental/PlanningRiskTable.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/callback/CGameInfoCallback.h"
#include "../../lib/serializer/BinarySerializer.h"
#include "mock/mock_MapService.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/StartInfo.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/CBonusTypeHandler.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/BattleLayout.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/callback/CCallback.h"
#include "../../AI/Nullkiller2/Engine/Nullkiller.h"
#include "../../AI/Nullkiller2/Engine/Settings.h"
#include "../../AI/Nullkiller2/AIUtility.h"
#include "../../AI/Nullkiller2/Engine/Experimental/PlanningRiskDecision.h"
#include "../../AI/Nullkiller2/Engine/Experimental/DetachedDefendBonuses.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/mapObjects/army/CArmedInstance.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../server/IGameServer.h"
#include "../../server/CGameHandler.h"
#include "../../server/battles/BattleProcessor.h"
#include "../../AI/Nullkiller2/Engine/Experimental/DetachedDuelRollout.h"

namespace
{
// Read-only diagnostics: state copies do not draw or reseed the oracle RNG.
void traceNeutralState(const CBattleInfoCallback & battle, const std::string & identity,
    const char * source, int step, bool obstacles)
{
    for(const auto * unit : battle.battleAliveUnits())
        std::cout << "FORCED_NEUTRAL_STATE case=" << identity << " source=" << source << " step=" << step
            << " unit=" << unit->unitId() << " side=" << static_cast<int>(unit->unitSide())
            << " slot=" << unit->unitSlot().getNum() << " creature=" << unit->creatureId().getNum()
            << " count=" << unit->getCount() << " health=" << unit->getAvailableHealth()
            << " hex=" << unit->getPosition().toInt() << " luck=" << unit->luckVal()
            << " morale=" << unit->moraleVal() << " ammo=" << unit->acquireState()->shots.available() << std::endl;
    if(obstacles)
        for(const auto & obstacle : battle.battleGetAllObstacles())
        {
            std::cout << "FORCED_NEUTRAL_OBSTACLE case=" << identity << " source=" << source
                << " id=" << obstacle->ID << " unique=" << obstacle->uniqueID
                << " type=" << static_cast<int>(obstacle->obstacleType) << " hex=" << obstacle->pos.toInt()
                << " blocked=";
            for(const auto hex : obstacle->getBlockedTiles()) std::cout << hex.toInt() << ',';
            std::cout << std::endl;
        }
}

class ReferenceServer final : public IGameServer
{
public:
	std::shared_ptr<CGameState> state;
	std::optional<BattleResult> result;
	int retaliationPackets = 0;
	int rounds = 0;
	EServerState status = EServerState::GAMEPLAY;
	void setState(EServerState value) override { status = value; }
	EServerState getState() const override { return status; }
	bool isPlayerHost(const PlayerColor &) const override { return true; }
	bool hasPlayerAt(PlayerColor, GameConnectionID) const override { return true; }
	bool hasBothPlayersAtSameConnection(PlayerColor, PlayerColor) const override { return false; }
	void applyPack(CPackForClient & pack) override
	{
		if(const auto * finished = dynamic_cast<const BattleResult *>(&pack))
			result = *finished;
		if(const auto * attack = dynamic_cast<const BattleAttack *>(&pack))
			retaliationPackets += attack->counter();
		if(dynamic_cast<const BattleNextRound *>(&pack))
			++rounds;
		state->apply(pack);
	}
	void sendPack(CPackForClient &, GameConnectionID) override {}
};

// Serialize the whole authoritative randomizer, including per-army biased
// morale/luck streams, without drawing or reseeding any generator.
class RandomizerCapture final : public IBinaryWriter
{
public:
	std::vector<std::byte> bytes;
	int write(const std::byte * data, unsigned size) override
	{
		bytes.insert(bytes.end(), data, data + size);
		return static_cast<int>(size);
	}
	static std::vector<std::byte> capture(GameRandomizer & randomizer)
	{
		RandomizerCapture output;
		BinarySerializer serializer(&output);
		serializer & randomizer;
		return output.bytes;
	}
};

class PlayerReferenceView final : public CGameInfoCallback
{
	CGameState & state;
	PlayerColor owner;
public:
	PlayerReferenceView(CGameState & state, PlayerColor owner) : state(state), owner(owner) {}
	CGameState & gameState() override { return state; }
	const CGameState & gameState() const override { return state; }
	std::optional<PlayerColor> getPlayerID() const override { return owner; }
};

// An experimental forecast failure must not suppress the independent server
// fight. Keep it a visible test failure; only the forecast boundary is caught.
// Actual setup/actions/results below are deliberately outside these catches.
template<typename Work>
auto inspectForecast(const char * phase, const std::string & identity, std::string & error, Work work) -> decltype(work())
{
	try
	{
		return work();
	}
	catch(const std::exception & failure)
	{
		error = std::string("forecast exception: ") + failure.what();
		std::cout << "FORCED_FORECAST_EXCEPTION case=" << identity << " phase=" << phase << " reason=" << error << std::endl;
		ADD_FAILURE() << identity << " / " << phase << ": " << error;
		return {};
	}
}

struct Duel
{
	const char * name;
	int attackerCreature;
	int attackerCount;
	int defenderCreature;
	int defenderCount;
	std::vector<std::pair<int, int>> attackerExtras = {};
	std::vector<std::pair<int, int>> defenderExtras = {};
};

class ForcedBattleReference : public ::testing::Test, public MapListener
{
protected:
	ReferenceServer server;
	std::unique_ptr<CGameHandler> handler;
	CMap * map = nullptr;
	int3 battleTile;
	std::shared_ptr<const ObjectTemplate> armyAppearance;
	void mapLoaded(CMap * loaded) override { map = loaded; }

	void initialize(int seed)
	{
		std::cout << "FORCED_REFERENCE_PHASE phase=initialize seed=" << seed << std::endl;
		CRandomGenerator::getDefault().setSeed(seed);
		server.state = std::make_shared<CGameState>();
		server.state->preInit(LIBRARY);
		MapServiceMock mapService("test/MiniTest/", this);
		StartInfo start;
		start.mapname = "forced-reference-public-minitest";
		start.difficulty = 0;
		start.mode = EStartMode::NEW_GAME;
		auto header = mapService.loadMapHeader(ResourcePath(start.mapname));
		ASSERT_NE(header, nullptr);
		for(size_t i = 0; i < header->players.size(); ++i)
		{
			const auto & info = header->players[i];
			if(!info.canHumanPlay && !info.canComputerPlay)
				continue;
			auto & settings = start.playerInfos[PlayerColor(i)];
			settings.color = PlayerColor(i);
			settings.name = "Reference player";
			settings.castle = info.defaultCastle();
			settings.hero = info.defaultHero();
			// A human attacker makes the result wait for dialog confirmation.
			// We observe terminal BattleResult, not unrelated adventure aftermath.
			settings.connectedPlayerIDs.insert(static_cast<PlayerConnectionID>(i));
		}
		GameRandomizer randomizer(*server.state);
		randomizer.setSeed(seed);
		Load::ProgressAccumulator progress;
		std::cout << "FORCED_REFERENCE_PHASE phase=map_init_begin seed=" << seed << std::endl;
		server.state->init(&mapService, &start, randomizer, progress, false);
		std::cout << "FORCED_REFERENCE_PHASE phase=map_init_end seed=" << seed << std::endl;
		ASSERT_NE(map, nullptr);
		ASSERT_FALSE(map->getHeroesOnMap().empty());
		const auto * contextHero = dynamic_cast<const CGHeroInstance *>(map->getObject(*map->getHeroesOnMap().begin()));
		ASSERT_NE(contextHero, nullptr);
		armyAppearance = contextHero->appearance;
		ASSERT_NE(armyAppearance, nullptr);
		battleTile = contextHero->visitablePos();
		// Production battlefield selection requires a visitable object. Reuse
		// a real public-map tile, not an unplaced synthetic army's empty tile.
		// The map hero supplies context only: never a battle participant.
		const auto & contextTile = map->getTile(battleTile);
		ASSERT_FALSE(contextTile.visitableObjects.empty());
		const auto * topObject = map->getObject(contextTile.visitableObjects.front());
		ASSERT_NE(dynamic_cast<const CGHeroInstance *>(topObject), nullptr);
		ASSERT_EQ(topObject->getBattlefield(), BattleField::NONE);
		handler = std::make_unique<CGameHandler>(server, server.state);
		handler->randomizer->setSeed(seed);
		// Fresh authoritative RNG also has empty per-army morale/luck streams.
		// Seed legacy thread-local fallback independently, without consuming it.
		CRandomGenerator::getDefault().setSeed(seed);
	}

	std::shared_ptr<CArmedInstance> army(PlayerColor owner, int creature, int count)
	{
		auto result = std::make_shared<CArmedInstance>(server.state.get());
		result->tempOwner = owner;
		result->appearance = armyAppearance;
		result->setAnchorPos(battleTile + result->getVisitableOffset());
		result->id = ObjectInstanceID(map->objects.size());
		// Register identity for BattleStart's getSideArmy lookup. No adventure
		// placement, visits, policy, hero bonuses or private map are involved.
		map->objects.push_back(result);
		result->setCreature(SlotID(0), CreatureID(creature), count);
		return result;
	}

	BattleAction chooseAction(const CBattleInfoCallback & battle, const battle::Unit * active)
	{
		auto enemies = battle.battleGetUnitsIf([active](const battle::Unit * unit)
		{
			return unit->alive() && unit->unitSide() != active->unitSide();
		});
		std::sort(enemies.begin(), enemies.end(), [](const auto * a, const auto * b) { return a->unitId() < b->unitId(); });
		const auto available = battle.battleGetAvailableHexes(active, true);
		for(const auto * enemy : enemies)
			if(battle.battleCanShoot(active, enemy->getPosition()))
				return BattleAction::makeShotAttack(active, enemy);
		for(const auto * enemy : enemies)
			for(const auto hex : enemy->getAttackableHexes(active))
				if(available.contains(hex) && battle.battleCanAttackUnit(active, enemy))
					return BattleAction::makeMeleeAttack(active, enemy, hex);
		BattleHex best = active->getPosition();
		auto distance = [&enemies](BattleHex hex)
		{
			int result = 1000;
			for(const auto * enemy : enemies)
				result = std::min(result, static_cast<int>(BattleHex::getDistance(hex, enemy->getPosition())));
			return result;
		};
		for(const auto hex : available)
			if(distance(hex) < distance(best) || (distance(hex) == distance(best) && hex.toInt() < best.toInt()))
				best = hex;
		return best == active->getPosition() ? BattleAction::makeDefend(active) : BattleAction::makeMove(active, best);
	}

	void runNeutral(bool upgrade, bool specialty = false)
	{
		const int seed = 17041;
		const auto identity = "neutral_" + std::to_string(upgrade) + "_" + std::to_string(specialty);
		ASSERT_NO_FATAL_FAILURE(initialize(seed));
		ASSERT_NE(handler, nullptr);
		ASSERT_NE(map, nullptr);
		ASSERT_NE(server.state, nullptr);
		auto * hero = dynamic_cast<CGHeroInstance *>(map->getObject(*map->getHeroesOnMap().begin()));
		ASSERT_NE(hero, nullptr);
		hero->clearSlots();
		hero->setCreature(SlotID(0), CreatureID(specialty ? 6 : 2), 20);
		hero->setCreature(SlotID(1), CreatureID(specialty ? 2 : 0), 20);
		std::cout << "FORCED_NEUTRAL_HERO type=" << hero->getHeroTypeID().getNum()
			<< " specialty_case=" << specialty << std::endl;
		// Actual object factory registers appearance, blocking/visitable tiles,
		// ownership and bonus parents. No mock split or fake action result.
		const ObjectInstanceID monsterID(map->objects.size());
		handler->createWanderingMonster(int3(3, 2, 0), CreatureID(58), 47);
		auto * monster = dynamic_cast<CGCreature *>(map->getObject(monsterID));
		ASSERT_NE(monster, nullptr);
		monster->neverFlees = true;
		monster->agression = 127; // encounter-choice only: cannot join or flee
		monster->upgradedStackPresence = upgrade ? CGCreature::UpgradedStackPresence::ALWAYS : CGCreature::UpgradedStackPresence::NEVER;
		PlayerReferenceView visible(*server.state, hero->getOwner());
		ASSERT_EQ(visible.getObj(monsterID, false), monster);
		std::cout << "FORCED_NEUTRAL_SOURCE upgrade=" << upgrade << " specialty=" << specialty
			<< " owner=" << monster->getOwner().getNum() << " stacks=" << monster->CArmedInstance::stacksCount()
			<< " slot0=" << monster->getStackCount(SlotID(0)) << std::endl;
		const int split = monster->getNumberOfStacks(hero);
		ASSERT_GT(split, 1);
		const auto fields = visible.getBattlefieldCandidates(monster->visitablePos());
		ASSERT_FALSE(fields.empty());
		const auto oracleRandomBefore = RandomizerCapture::capture(*handler->randomizer);
		const auto originalOwner = monster->getOwner();
		// A visible map monster owned by either player is not an ordinary
		// neutral encounter. Restore the authentic owner even on an exception
		// or assertion return; never normalize it to manufacture eligibility.
		for(const auto ownedBy : {PlayerColor(0), PlayerColor(1)})
		{
			struct RestoreOwner
			{
				CGCreature & creature;
				PlayerColor original;
				~RestoreOwner() { creature.tempOwner = original; }
			} restore{*monster, originalOwner};
			monster->tempOwner = ownedBy;
			NK2AI::experimental::PlanningBattleContext context{monster->visitablePos(),
				visible.getTile(monster->visitablePos())->getTerrainID(), fields.front(),
				BattleLayout::createDefaultLayout(visible, hero, monster)};
			std::string reason;
			auto rejected = inspectForecast("owned_neutral_control", identity, reason, [&]
			{
				return NK2AI::experimental::PlanningBattleSnapshot::createNeutralEncounter(visible, *hero, *monster, context, &reason);
			});
			EXPECT_EQ(rejected, nullptr) << "Player-owned monster must not become a neutral planning encounter";
			std::string requestReason;
			auto rejectedRequest = inspectForecast("owned_neutral_request_control", identity, requestReason, [&]
			{
				return NK2AI::experimental::captureNeutralRisk(visible, *hero, *monster, 1, &requestReason);
			});
			EXPECT_FALSE(rejectedRequest.has_value()) << "Player-owned monster must not become a neutral risk request";
			std::cout << "FORCED_NEUTRAL_OWNERSHIP_CONTROL case=" << identity << " owner=" << ownedBy.getNum()
				<< " rejected=" << !rejected << " reason=" << reason
				<< " request_rejected=" << !rejectedRequest << " request_reason=" << requestReason << std::endl;
		}
		ASSERT_EQ(monster->getOwner(), originalOwner);
		{
			// Build valid context before removing appearance. Every capture API
			// must reject the malformed source before reading its geometry.
			NK2AI::experimental::PlanningBattleContext context{monster->visitablePos(),
				visible.getTile(monster->visitablePos())->getTerrainID(), fields.front(),
				BattleLayout::createDefaultLayout(visible, hero, monster)};
			struct RestoreAppearance
			{
				CGCreature & creature;
				std::shared_ptr<const ObjectTemplate> original;
				~RestoreAppearance() { creature.appearance = original; }
			} restore{*monster, monster->appearance};
			monster->appearance.reset();
			std::string rawReason, neutralReason, requestReason;
			auto raw = inspectForecast("missing_appearance_raw_control", identity, rawReason, [&]
			{
				return NK2AI::experimental::PlanningBattleSnapshot::create(visible, *hero, *monster, context, &rawReason);
			});
			auto neutral = inspectForecast("missing_appearance_neutral_control", identity, neutralReason, [&]
			{
				return NK2AI::experimental::PlanningBattleSnapshot::createNeutralEncounter(visible, *hero, *monster, context, &neutralReason);
			});
			auto request = inspectForecast("missing_appearance_request_control", identity, requestReason, [&]
			{
				return NK2AI::experimental::captureNeutralRisk(visible, *hero, *monster, 1, &requestReason);
			});
			EXPECT_EQ(raw, nullptr);
			EXPECT_EQ(neutral, nullptr);
			EXPECT_FALSE(request.has_value());
			std::cout << "FORCED_MISSING_APPEARANCE_CONTROL case=" << identity
				<< " raw_rejected=" << !raw << " neutral_rejected=" << !neutral << " request_rejected=" << !request
				<< " raw_reason=" << rawReason << " neutral_reason=" << neutralReason << " request_reason=" << requestReason << std::endl;
		}
		ASSERT_NE(monster->appearance, nullptr);
		std::vector<std::pair<BattleField, std::unique_ptr<NK2AI::experimental::PlanningBattleSnapshot>>> variants;
		for(const auto field : fields)
		{
			NK2AI::experimental::PlanningBattleContext context{monster->visitablePos(),
				visible.getTile(monster->visitablePos())->getTerrainID(), field,
				BattleLayout::createDefaultLayout(visible, hero, monster)};
			std::string reason;
			variants.emplace_back(field, inspectForecast("neutral_capture", identity, reason, [&]
			{
				return NK2AI::experimental::PlanningBattleSnapshot::createNeutralEncounter(visible, *hero, *monster, context, &reason);
			}));
			std::cout << "FORCED_NEUTRAL_CAPTURE upgrade=" << upgrade << " specialty=" << specialty
				<< " field=" << field.getNum() << " admitted=" << static_cast<bool>(variants.back().second)
				<< " reason=" << reason << std::endl;
		}
		EXPECT_EQ(RandomizerCapture::capture(*handler->randomizer), oracleRandomBefore);
		EXPECT_EQ(monster->CArmedInstance::stacksCount(), 1);
		EXPECT_EQ(monster->getStackCount(SlotID(0)), 47);
		const auto id = server.state->nextBattleID;
		// This invokes the real private fight path, including actual server
		// stack-move/type-change packets and the singleton-upgrade RNG draw.
		std::cout << "FORCED_REFERENCE_PHASE phase=neutral_visit upgrade=" << upgrade << std::endl;
		monster->onHeroVisit(*handler, hero);
		const auto * real = server.state->getBattle(id);
		ASSERT_NE(real, nullptr);
		EXPECT_EQ(monster->CArmedInstance::stacksCount(), split);
		traceNeutralState(*real, identity, "actual_initial", 0, true);
		auto selected = std::find_if(variants.begin(), variants.end(), [real](const auto & item) { return item.first == real->getBattlefieldType(); });
		EXPECT_NE(selected, variants.end());
		if(selected != variants.end() && selected->second)
		{
			std::string error;
			inspectForecast("neutral_comparison", identity, error, [&]
			{
				traceNeutralState(selected->second->battle(), identity, "snapshot_initial", 0, true);
				const auto predicted = selected->second->battle().battleAliveUnits();
				const auto actual = real->battleAliveUnits();
				EXPECT_EQ(predicted.size(), actual.size());
				for(const auto * unit : actual)
				{
					const auto found = std::find_if(predicted.begin(), predicted.end(), [unit](const auto * copy) { return copy->unitSide() == unit->unitSide() && copy->unitSlot() == unit->unitSlot(); });
					EXPECT_NE(found, predicted.end());
					if(found == predicted.end()) continue;
					EXPECT_EQ((*found)->creatureId(), unit->creatureId());
					EXPECT_EQ((*found)->getCount(), unit->getCount());
					EXPECT_EQ((*found)->getPosition(), unit->getPosition());
					EXPECT_EQ((*found)->getAvailableHealth(), unit->getAvailableHealth());
					EXPECT_EQ((*found)->getMovementRange(), unit->getMovementRange());
					EXPECT_EQ((*found)->moraleVal(), unit->moraleVal());
					for(const bool ranged : {false, true})
					{
						EXPECT_EQ((*found)->getAttack(ranged), unit->getAttack(ranged));
						EXPECT_EQ((*found)->getDefense(ranged), unit->getDefense(ranged));
						EXPECT_EQ((*found)->getMinDamage(ranged), unit->getMinDamage(ranged));
						EXPECT_EQ((*found)->getMaxDamage(ranged), unit->getMaxDamage(ranged));
					}
				}
				return true;
			});
		}
		else std::cout << "FORCED_NEUTRAL_CAPTURE selected=unsupported_or_unavailable" << std::endl;
		const auto oracleRandomAfterPreparation = RandomizerCapture::capture(*handler->randomizer);
		if(selected != variants.end() && selected->second)
		{
			// A previously admitted capture must also survive the stronger
			// invariance check. Failure stays visible, but no returned pointer
			// from that helper is used to drive the independent real battle.
			std::string error;
			inspectForecast("neutral_invariance", identity, error, [&]
			{
				test::verifyPlanningSnapshot(visible, *hero, *monster, *real);
				return true;
			});
		}
		if(selected != variants.end() && selected->second)
			for(const int sampleSeed : {17041, 17047, 17053})
			{
				std::string error;
				std::vector<std::string> trace;
				auto outcome = inspectForecast("neutral_sample", identity, error, [&]
				{
					return NK2AI::experimental::runSampledBattle(selected->second->battle(), selected->second->frozenGameInfo(), sampleSeed, &trace);
				});
				if(!error.empty()) outcome.unsupportedReason = error.c_str();
				std::cout << "FORCED_NEUTRAL_SAMPLE upgrade=" << upgrade << " specialty=" << specialty << " sample_seed=" << sampleSeed
					<< " status=" << static_cast<int>(outcome.status) << " winner=" << static_cast<int>(outcome.winner)
					<< " actions=" << outcome.actions << " bonus=" << outcome.unsupportedBonusType
                    << " bonus_name=" << (outcome.unsupportedBonusType < 0 ? "none" : LIBRARY->bth->bonusToString(static_cast<BonusType>(outcome.unsupportedBonusType)))
                    << " a_survivors=" << outcome.remainingCreatures[0] << " d_survivors=" << outcome.remainingCreatures[1]
                    << " a_lost_value=" << outcome.initialArmyValue[0] - outcome.remainingArmyValue[0]
                    << " d_lost_value=" << outcome.initialArmyValue[1] - outcome.remainingArmyValue[1]
                    << " reason=" << outcome.unsupportedReason << std::endl;
                for(const auto & line : trace)
                    std::cout << "FORCED_NEUTRAL_SAMPLE_TRACE case=" << identity << " seed=" << sampleSeed << ' ' << line << std::endl;
                for(const auto & stack : outcome.stacks)
                    std::cout << "FORCED_NEUTRAL_SAMPLE_STACK case=" << identity << " seed=" << sampleSeed
                        << " unit=" << stack.unit << " creature=" << stack.creature.getNum()
                        << " side=" << static_cast<int>(stack.side) << " initial=" << stack.initialCount
                        << " survivors=" << stack.survivors << std::endl;
			}
		variants.clear();
		EXPECT_EQ(RandomizerCapture::capture(*handler->randomizer), oracleRandomAfterPreparation);
		const auto selectedBattlefield = real->getBattlefieldType();
		int actions = 0;
		while(!server.result && actions < 2000)
		{
			const auto * battle = server.state->getBattle(id);
			ASSERT_NE(battle, nullptr);
			const auto * unit = battle->battleActiveUnit();
			ASSERT_NE(unit, nullptr);
            const auto action = chooseAction(*battle, unit);
            traceNeutralState(*battle, identity, "actual_pre", actions, false);
            std::cout << "FORCED_NEUTRAL_ACTION case=" << identity << " step=" << actions
                << " actor=" << unit->unitId() << " action=" << action.toString() << std::endl;
            ASSERT_TRUE(handler->battles->makePlayerBattleAction(id, battle->battleGetOwner(unit), action));
            // The final action can erase BattleInfo. Never reuse battle/unit here.
            if(const auto * after = server.state->getBattle(id))
                traceNeutralState(*after, identity, "actual_post", actions, false);
			++actions;
		}
		ASSERT_TRUE(server.result.has_value());
		std::cout << "FORCED_NEUTRAL_REFERENCE upgrade=" << upgrade << " specialty=" << specialty
			<< " battlefield=" << selectedBattlefield.getNum() << " seed=" << seed << " split=" << split
			<< " winner=" << static_cast<int>(server.result->winner) << " actions=" << actions;
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			for(const auto & [creature, count] : server.result->casualties[side])
				std::cout << " loss_" << static_cast<int>(side) << '_' << creature.getNum() << '=' << count;
		std::cout << std::endl;
	}

	// Production capture/builder, followed by a verdict-independent server fight.
	void runDecisionHoldout(const char * family, int seed, int ownCreature, int ownCount, int enemyCreature, int enemyCount)
	{
		using namespace NK2AI::experimental;
		ASSERT_NO_FATAL_FAILURE(initialize(seed));
		ASSERT_NE(handler, nullptr);
		ASSERT_NE(map, nullptr);
		auto * hero = dynamic_cast<CGHeroInstance *>(map->getObject(*map->getHeroesOnMap().begin()));
		ASSERT_NE(hero, nullptr);
		hero->clearSlots();
		if(ownCreature < 0)
		{
			hero->setCreature(SlotID(0), CreatureID(2), 20);
			hero->setCreature(SlotID(1), CreatureID(0), 20);
		}
		else hero->setCreature(SlotID(0), CreatureID(ownCreature), ownCount);
		double initialValue = 0;
		for(const auto & slot : hero->Slots())
			initialValue += slot.second->getCount() * slot.second->getCreatureID().toCreature()->getAIValue();
		ASSERT_GT(initialValue, 0);
		const ObjectInstanceID monsterID(map->objects.size());
		handler->createWanderingMonster(int3(3, 2, 0), CreatureID(enemyCreature), enemyCount);
		auto * monster = dynamic_cast<CGCreature *>(map->getObject(monsterID));
		ASSERT_NE(monster, nullptr);
		monster->neverFlees = true;
		monster->agression = 127;
		monster->upgradedStackPresence = CGCreature::UpgradedStackPresence::NEVER;
		NK2AI::Nullkiller ai;
		ai.playerID = hero->getOwner();
		ai.aiGw = nullptr;
		ai.cc = std::make_shared<CCallback>(server.state, ai.playerID, nullptr);
		ai.settings = std::make_unique<NK2AI::Settings>(ai.cc->getStartInfo()->difficulty);
		ASSERT_EQ(ai.cc->getObj(monsterID, false), monster);
		const int split = monster->getNumberOfStacks(hero);
		const double scalarRatio = static_cast<double>(hero->getArmyStrength()) / monster->getArmyStrength();
		const auto oracleBefore = RandomizerCapture::capture(*handler->randomizer);
        // Exact current hero-specific baseline, not the raw creature-value ratio.
        // Log the production predicate; getMagicStrength alone may count magic
        // the adventure predicate correctly excludes as unavailable.
        const bool scalarSafe = NK2AI::isSafeToVisit(hero, hero, monster->getArmyStrength(), ai.settings->getSafeAttackRatio());
        if(ownCreature == 19 && ownCount == 20 && enemyCreature == 2)
        {
            // If fixture context changes, retain the fight but fail the declared
            // baseline-rejection precondition rather than claiming a rescue.
            EXPECT_FALSE(scalarSafe);
        }
        std::cout << "FORCED_DECISION_SCALAR family=" << family << " oracle_seed=" << seed
            << " hero_type=" << hero->getHeroTypeID().getNum()
            << " attack=" << hero->getPrimSkillLevel(PrimarySkill::ATTACK)
            << " defense=" << hero->getPrimSkillLevel(PrimarySkill::DEFENSE)
            << " spell_power=" << hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER)
            << " knowledge=" << hero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE)
            << " fighting_strength=" << hero->getFightingStrength()
            << " magic_strength=" << hero->getMagicStrength()
            << " spellbook=" << hero->hasSpellbook() << " mana=" << hero->mana
            << " own_army=" << hero->getArmyStrength() << " neutral_army=" << monster->getArmyStrength()
            << " safe_ratio=" << ai.settings->getSafeAttackRatio()
            << " scalar_safe=" << scalarSafe
            << " difficulty=" << static_cast<int>(ai.cc->getStartInfo()->difficulty) << std::endl;
        EXPECT_EQ(RandomizerCapture::capture(*handler->randomizer), oracleBefore);
		DecisionRiskBudget::Settings settings;
		settings.wallTime = std::chrono::milliseconds(2000);
		settings.maximumDraws = 3 * 128;
		settings.requestedWorkers = 1;
		settings.availableThreads = 1;
		// One budget starts before production capture, never once per variant.
		DecisionRiskBudget budget(settings);
		RiskAssessment assessment;
		double tolerance = -1, worstLoss = 1;
		bool captured = false, policyCaptured = false, recordFound = false, allFinished = false, revalidated = false;
		bool unmodelledMagic = false;
		size_t variantCount = 0;
		std::string reason, exception;
		inspectForecast("decision_holdout", family, exception, [&]
		{
			std::optional<CapturedRiskRequest> request;
            {
                std::shared_lock lock(CGameState::mutex);
                request = captureNeutralRisk(*ai.cc, *hero, *monster, 1, &reason);
                captured = request.has_value();
                if(!request) return false;
                policyCaptured = captureNeutralLossPolicy(ai, *hero, *request);
            }
			if(!policyCaptured) { reason = "production loss policy unavailable"; return false; }
			tolerance = request->contextualLossTolerance;
			variantCount = request->variants.size();
			unmodelledMagic = request->unmodelledOwnMagic;
			const auto key = request->key;
            for(size_t variant = 0; variant < request->variants.size(); ++variant)
                std::cout << "FORCED_DECISION_VARIANT family=" << family << " oracle_seed=" << seed
                    << " variant=" << variant << " battlefield=" << request->variants[variant]->battle().battleGetBattlefieldType().getNum()
                    << " terrain=" << request->variants[variant]->battle().battleTerrainType().getNum() << std::endl;
            std::vector<CapturedRiskRequest> requests;
			requests.push_back(std::move(*request));
			const auto table = PlanningRiskTable::buildDecision(std::move(requests), 1, budget);
            {
                std::shared_lock lock(CGameState::mutex);
                std::string revalidationReason;
                auto current = captureNeutralRisk(*ai.cc, *hero, *monster, 1, &revalidationReason);
                EXPECT_TRUE(current.has_value()) << revalidationReason;
                if(current)
                {
                    const bool currentPolicy = captureNeutralLossPolicy(ai, *hero, *current);
                    EXPECT_TRUE(currentPolicy);
                    revalidated = currentPolicy && current->key == key;
                    EXPECT_TRUE(revalidated) << "Builder changed the captured live source";
                }
            }
			if(!revalidated) { reason = "source revalidation failed"; return false; }
			const auto * record = table->find(key, PlanningRiskRoute{true, 0, 0, 0, true});
			recordFound = record != nullptr;
			if(!record) { reason = "exact current-army record unavailable"; return false; }
			assessment = record->assess(record->contextualLossTolerance, true);
			worstLoss = record->worstSampledLoss();
			allFinished = record->allScenariosFinished;
			for(size_t variant = 0; variant < record->variantScenarios.size(); ++variant)
				for(size_t index = 0; index < record->variantScenarios[variant].size(); ++index)
				{
					const auto & outcome = record->variantScenarios[variant][index];
					std::cout << "FORCED_DECISION_SAMPLE family=" << family << " oracle_seed=" << seed
						<< " variant=" << variant << " index=" << index << " sampler_seed=" << 104729 + index * 7919
						<< " status=" << static_cast<int>(outcome.status) << " winner=" << static_cast<int>(outcome.winner)
						<< " initial_value=" << outcome.initialArmyValue[0] << " remaining_value=" << outcome.remainingArmyValue[0]
						<< " actions=" << outcome.actions << " bonus=" << outcome.unsupportedBonusType
						<< " reason=" << outcome.unsupportedReason << std::endl;
				}
			return true;
		});
		EXPECT_EQ(RandomizerCapture::capture(*handler->randomizer), oracleBefore);
		const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(DecisionRiskBudget::Clock::now() - budget.startedAt()).count();
		std::cout << "FORCED_DECISION_FORECAST family=" << family << " oracle_seed=" << seed
			<< " captured=" << captured << " policy=" << policyCaptured << " revalidated=" << revalidated << " record=" << recordFound
			<< " variants=" << variantCount << " draws=" << budget.drawsStarted() << " samples=" << assessment.samples
			<< " recommendation=" << static_cast<int>(assessment.recommendation) << " adverse_upper=" << assessment.adverseUpper
			<< " defeat_lower=" << assessment.defeatLower << " tolerance=" << tolerance << " worst_loss=" << worstLoss
			<< " all_finished=" << allFinished << " own_magic=" << unmodelledMagic
			<< " elapsed_us=" << elapsed << " stopped=" << budget.shouldStop()
			<< " reason=" << (exception.empty() ? reason : exception) << std::endl;
		// No forecast admission, deadline, support or recommendation can veto this.
		const auto id = server.state->nextBattleID;
		monster->onHeroVisit(*handler, hero);
		const auto * initial = server.state->getBattle(id);
		ASSERT_NE(initial, nullptr);
		const int field = initial->getBattlefieldType().getNum();
		int actions = 0;
		while(!server.result && actions < 2000)
		{
			const auto * battle = server.state->getBattle(id);
			ASSERT_NE(battle, nullptr);
			const auto * actor = battle->battleActiveUnit();
			ASSERT_NE(actor, nullptr);
			const auto action = chooseAction(*battle, actor);
			ASSERT_TRUE(handler->battles->makePlayerBattleAction(id, battle->battleGetOwner(actor), action));
			++actions;
		}
		ASSERT_TRUE(server.result.has_value());
		double lostValue = 0;
		for(const auto & [creature, count] : server.result->casualties[BattleSide::ATTACKER])
			lostValue += count * creature.toCreature()->getAIValue();
		std::cout << "FORCED_DECISION_REFERENCE family=" << family << " oracle_seed=" << seed
			<< " winner=" << static_cast<int>(server.result->winner) << " actions=" << actions
			<< " battlefield=" << field << " split=" << split << " scalar_ratio=" << scalarRatio
			<< " initial_value=" << initialValue << " lost_value=" << lostValue << " loss_fraction=" << lostValue / initialValue;
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			for(const auto & [creature, count] : server.result->casualties[side])
				std::cout << " loss_" << static_cast<int>(side) << '_' << creature.getNum() << '=' << count;
		std::cout << std::endl;
	}

	// Scripted mechanics control, not a policy-selected battle or outcome sample.
	void verifyDefend(int creature, bool weak, bool positiveMorale, int extraStance = 0)
	{
		ASSERT_NO_FATAL_FAILURE(initialize(17041));
		ASSERT_NE(handler, nullptr);
		auto attacker = army(PlayerColor(0), 18, 20); // native single-shot wood elves
		auto defender = army(PlayerColor::NEUTRAL, creature, 1000);
		if(extraStance)
			defender->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::DEFENSIVE_STANCE, BonusSource::OTHER, extraStance, BonusSourceID()));
		auto layout = BattleLayout::createDefaultLayout(*server.state, attacker.get(), defender.get());
		layout.obstaclesAllowed = false;
		layout.tacticsAllowed = false;
		const auto id = server.state->nextBattleID;
		handler->battles->startBattle(attacker.get(), defender.get(), battleTile, nullptr, nullptr, layout, nullptr);
		const auto * real = server.state->getBattle(id);
		ASSERT_NE(real, nullptr);
		const battle::Unit * shooter = nullptr;
		const battle::Unit * target = nullptr;
		for(const auto * unit : real->battleAliveUnits())
			(unit->unitSide() == BattleSide::ATTACKER ? shooter : target) = unit;
		ASSERT_NE(shooter, nullptr);
		ASSERT_NE(target, nullptr);
		ASSERT_EQ(real->battleActiveUnit(), shooter);
		ASSERT_GT(shooter->getMovementRange(), target->getMovementRange());
		ASSERT_EQ(shooter->getTotalAttacks(true), 1);
		ASSERT_EQ(shooter->luckVal(), 0);
		ASSERT_GE(shooter->moraleVal(), 0);
		ASSERT_GE(target->moraleVal(), 0);
		if(positiveMorale) { ASSERT_GT(target->moraleVal(), 0); }
		const int baseDefense = target->getDefense(false);
		if(weak) { ASSERT_LT(baseDefense, 5); }
		else { ASSERT_GE(baseDefense, 5); }
		const auto constructionRng = RandomizerCapture::capture(*handler->randomizer);
        // This controlled two-stack setup has no ammo cart or control spells.
        struct Environment final : battle::IUnitEnvironment
        {
            bool unitHasAmmoCart(const battle::Unit *) const override { return false; }
            PlayerColor unitEffectiveOwner(const battle::Unit * unit) const override { return unit->unitOwner(); }
        } environment;
		NK2AI::experimental::DetachedDefendBonuses bonuses(*target);
		battle::CUnitStateDetached copy(target, &bonuses);
		copy.localInit(&environment);
		copy = *target->acquireState();
		EXPECT_EQ(RandomizerCapture::capture(*handler->randomizer), constructionRng);
		auto compare = [&](const char * phase)
		{
			const auto rngBefore = RandomizerCapture::capture(*handler->randomizer);
			const auto live = real->calculateDmgRange(BattleAttackInfo(shooter, target, 0, true)).damage;
			const auto detached = real->calculateDmgRange(BattleAttackInfo(shooter, &copy, 0, true)).damage;
			EXPECT_EQ(copy.getDefense(false), target->getDefense(false));
			EXPECT_EQ(copy.getDefense(true), target->getDefense(true));
			EXPECT_EQ(detached.min, live.min);
			EXPECT_EQ(detached.max, live.max);
			EXPECT_EQ(RandomizerCapture::capture(*handler->randomizer), rngBefore);
			std::cout << "FORCED_DEFEND_CONTROL creature=" << creature << " stance=" << extraStance
				<< " phase=" << phase << " round=" << real->battleGetRound()
				<< " defense=" << target->getDefense(false) << " min=" << live.min << " max=" << live.max << std::endl;
		};
		compare("before");
		// DEFEND must not draw good morale, even for positive-morale troops.
		const auto defendOnly = [&](const battle::Unit * actor)
		{
			const auto rngBefore = RandomizerCapture::capture(*handler->randomizer);
			EXPECT_TRUE(handler->battles->makePlayerBattleAction(id, real->battleGetOwner(actor), BattleAction::makeDefend(actor)));
			EXPECT_EQ(RandomizerCapture::capture(*handler->randomizer), rngBefore);
		};
		defendOnly(shooter);
		ASSERT_EQ(real->battleActiveUnit(), target);
		for(int cycle = 0; cycle < 3; ++cycle)
		{
			const auto roundBefore = real->battleGetRound();
			const auto rngBefore = RandomizerCapture::capture(*handler->randomizer);
			bonuses.defend();
			copy.defending = true;
			EXPECT_EQ(RandomizerCapture::capture(*handler->randomizer), rngBefore);
			defendOnly(target);
			ASSERT_EQ(real->battleActiveUnit(), shooter);
			ASSERT_GT(real->battleGetRound(), roundBefore);
			ASSERT_TRUE(target->hasBonusOfType(BonusType::UNIT_DEFENDING));
			const int expected = weak && !extraStance ? baseDefense + 1
				: (baseDefense + extraStance) * 120 / 100;
			EXPECT_EQ(target->getDefense(false), expected);
			compare("next_round_before_own_activation");
			const auto range = real->calculateDmgRange(BattleAttackInfo(shooter, target, 0, true)).damage;
			const auto health = target->getAvailableHealth();
			ASSERT_TRUE(real->battleCanShoot(shooter, target->getPosition()));
			ASSERT_TRUE(handler->battles->makePlayerBattleAction(id, real->battleGetOwner(shooter), BattleAction::makeShotAttack(shooter, target)));
			ASSERT_FALSE(server.result.has_value());
			const auto damage = health - target->getAvailableHealth();
			EXPECT_GE(damage, range.min);
			EXPECT_LE(damage, range.max);
			// A real positive-morale extra shooter turn is legal; consume it by
			// defending, never by altering the server's morale stream.
			if(real->battleActiveUnit() == shooter) defendOnly(shooter);
			ASSERT_EQ(real->battleActiveUnit(), target);
			bonuses.beforeActivation();
			copy = *target->acquireState();
			EXPECT_FALSE(target->hasBonusOfType(BonusType::UNIT_DEFENDING));
			EXPECT_EQ(target->getDefense(false), baseDefense);
			compare("own_activation_expired");
		}
	}

	void run(const Duel & duel, int seed)
	{
		SCOPED_TRACE(std::string(duel.name) + " seed=" + std::to_string(seed));
		ASSERT_NO_FATAL_FAILURE(initialize(seed));
		ASSERT_NE(handler, nullptr);
		ASSERT_NE(map, nullptr);
		ASSERT_NE(server.state, nullptr);
		auto attacker = army(PlayerColor(0), duel.attackerCreature, duel.attackerCount);
		auto defender = army(PlayerColor::NEUTRAL, duel.defenderCreature, duel.defenderCount);
		for(const auto & current : {attacker, defender})
		{
			ASSERT_NE(current->appearance, nullptr);
			ASSERT_TRUE(current->isVisitable());
			ASSERT_EQ(current->visitablePos(), battleTile);
			ASSERT_EQ(current->getCurrentTerrain(), map->getTile(battleTile).getTerrainID());
		}
		int slot = 1;
		for(const auto & extra : duel.attackerExtras)
			attacker->setCreature(SlotID(slot++), CreatureID(extra.first), extra.second);
		slot = 1;
		for(const auto & extra : duel.defenderExtras)
			defender->setCreature(SlotID(slot++), CreatureID(extra.first), extra.second);
		const double scalarRatio = static_cast<double>(attacker->getArmyStrength()) / defender->getArmyStrength();
		auto layout = BattleLayout::createDefaultLayout(*server.state, attacker.get(), defender.get());
		layout.obstaclesAllowed = false;
		layout.tacticsAllowed = false;
		const BattleID id = server.state->nextBattleID;
		std::cout << "FORCED_REFERENCE_PHASE phase=battle_start case=" << duel.name << std::endl;
		handler->battles->startBattle(attacker.get(), defender.get(), battleTile, nullptr, nullptr, layout, nullptr);
		const auto * initial = server.state->getBattle(id);
		ASSERT_NE(initial, nullptr);
		std::cout << "FORCED_BATTLE_CONTEXT case=" << duel.name << " seed=" << seed
			<< " tile=" << battleTile.x << ',' << battleTile.y << ',' << battleTile.z
			<< " terrain=" << initial->getTerrainType().getNum()
			<< " battlefield=" << initial->getBattlefieldType().getNum() << std::endl;
		// Both forecasts consume the same live snapshot read-only. Neither can
		// veto the forced battle or select its real damage rolls/actions.
		for(const auto * unit : initial->battleGetUnitsIf([](const battle::Unit *) { return true; }))
			std::cout << "FORCED_BATTLE_INITIAL case=" << duel.name << " seed=" << seed
				<< " unit=" << unit->unitId() << " side=" << static_cast<int>(unit->unitSide())
				<< " hex=" << unit->getPosition().toInt() << " health=" << unit->getAvailableHealth()
				<< " morale=" << unit->moraleVal() << " luck=" << unit->luckVal() << std::endl;
		const auto oracleRandomBefore = RandomizerCapture::capture(*handler->randomizer);
		NK2AI::experimental::PlanningBattleContext context{
			battleTile, initial->getTerrainType(), initial->getBattlefieldType(), layout};
		std::string captureReason;
		auto snapshot = inspectForecast("capture", duel.name, captureReason, [&]
		{
			return NK2AI::experimental::PlanningBattleSnapshot::create(*server.state, *attacker, *defender, context, &captureReason);
		});
		std::cout << "FORCED_BATTLE_CAPTURE case=" << duel.name << " seed=" << seed
			<< " admitted=" << static_cast<bool>(snapshot) << " reason=" << captureReason << std::endl;
		if(snapshot)
		{
			EXPECT_THROW(snapshot->frozenGameInfo().gameState(), std::logic_error);
			std::string error;
			inspectForecast("invariance", duel.name, error, [&]
			{
				test::verifyPlanningSnapshot(*server.state, *attacker, *defender, *initial);
				return true;
			});
		}
		std::vector<NK2AI::experimental::SampledBattleOutcome> samples;
		// Own exception strings until all rows are emitted; outcome reason
		// pointers must not refer to a destroyed catch-local string.
		std::array<std::string, 3> sampleErrors;
		if(snapshot)
		{
			for(const int sampleSeed : {17041, 17047, 17053})
			{
				auto & error = sampleErrors[samples.size()];
				auto outcome = inspectForecast("sample", duel.name, error, [&]
				{
					return NK2AI::experimental::runSampledBattle(snapshot->battle(), snapshot->frozenGameInfo(), sampleSeed);
				});
				if(!error.empty()) outcome.unsupportedReason = error.c_str();
				samples.push_back(outcome);
			}
		}
		snapshot.reset();
		EXPECT_EQ(RandomizerCapture::capture(*handler->randomizer), oracleRandomBefore)
			<< "Forecast changed authoritative randomizer, including bias history";
		std::string pessimisticError, optimisticError;
		const auto pessimistic = inspectForecast("legacy_pessimistic", duel.name, pessimisticError, [&] { return NK2AI::experimental::runDetachedDuel(*initial, false); });
		const auto optimistic = inspectForecast("legacy_optimistic", duel.name, optimisticError, [&] { return NK2AI::experimental::runDetachedDuel(*initial, true); });
		ASSERT_EQ(attacker->battle, initial);
		ASSERT_EQ(defender->battle, initial);
		for(const auto * unit : initial->battleGetUnitsIf([](const battle::Unit *) { return true; }))
			EXPECT_EQ(unit->getAvailableHealth(), unit->getTotalHealth()) << "Forecast mutated the real initial battle";
		EXPECT_EQ(RandomizerCapture::capture(*handler->randomizer), oracleRandomBefore);
		int actions = 0;
		int shots = 0;
		int melee = 0;
		while(!server.result && actions < 2000)
		{
			const auto * battle = server.state->getBattle(id);
			ASSERT_NE(battle, nullptr);
			const auto * active = battle->battleActiveUnit();
			ASSERT_NE(active, nullptr);
			const auto action = chooseAction(*battle, active);
			std::cout << "FORCED_BATTLE_ACTION case=" << duel.name << " seed=" << seed
				<< " index=" << actions << ' ' << action.toString() << std::endl;
			shots += action.actionType == EActionType::SHOOT;
			melee += action.actionType == EActionType::WALK_AND_ATTACK;
			ASSERT_TRUE(handler->battles->makePlayerBattleAction(id, battle->battleGetOwner(active), action)) << action.toString();
			++actions;
		}
		ASSERT_TRUE(server.result.has_value()) << "No real BattleResult within 2000 actions; NOT a win/loss observation";
		EXPECT_EQ(server.result->result, EBattleResult::NORMAL);
		EXPECT_NE(server.result->winner, BattleSide::NONE);
		EXPECT_GT(melee + shots, 0);
		if(duel.attackerCreature == 2 || duel.defenderCreature == 2)
		{
			EXPECT_GT(shots, 0);
		}
		if(std::string(duel.name) == "undead_melee")
		{
			EXPECT_GT(server.retaliationPackets, 0);
		}
		const auto losingSide = server.result->winner == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
		const auto losingCreature = CreatureID(losingSide == BattleSide::ATTACKER ? duel.attackerCreature : duel.defenderCreature);
		const auto losingCount = losingSide == BattleSide::ATTACKER ? duel.attackerCount : duel.defenderCount;
		std::map<CreatureID, int> losingCounts{{losingCreature, losingCount}};
		for(const auto & extra : losingSide == BattleSide::ATTACKER ? duel.attackerExtras : duel.defenderExtras)
			losingCounts[CreatureID(extra.first)] += extra.second;
		for(const auto & [creature, count] : losingCounts)
			EXPECT_EQ(server.result->casualties[losingSide][creature], count);
		std::cout << "FORCED_BATTLE_REFERENCE case=" << duel.name << " seed=" << seed
			<< " attacker=" << duel.attackerCreature << ':' << duel.attackerCount
			<< " defender=" << duel.defenderCreature << ':' << duel.defenderCount
			<< " scalar_ratio=" << scalarRatio
			<< " scalar_advantage=" << (scalarRatio >= 1.0)
			<< " winner=" << (server.result->winner == BattleSide::ATTACKER ? "attacker" : "defender")
			<< " actions=" << actions << " rounds=" << server.rounds << " retaliations=" << server.retaliationPackets << " shots=" << shots << " melee=" << melee;
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			int64_t value = 0;
			for(const auto & casualty : server.result->casualties[side])
			{
				EXPECT_GE(casualty.second, 0);
				std::cout << " loss_" << (side == BattleSide::ATTACKER ? "a" : "d") << '_' << casualty.first.getNum() << '=' << casualty.second;
				value += int64_t(casualty.second) * casualty.first.toCreature()->getAIValue();
			}
			std::cout << " lost_ai_" << (side == BattleSide::ATTACKER ? "a" : "d") << '=' << value;
		}
		for(const auto & forecast : {pessimistic, optimistic})
		{
			std::cout << " forecast_status=" << static_cast<int>(forecast.status)
				<< " forecast_winner=" << static_cast<int>(forecast.winner)
				<< " forecast_a_survivors=" << forecast.attackerSurvivors
				<< " forecast_d_survivors=" << forecast.defenderSurvivors;
			if(forecast.status == NK2AI::experimental::DuelStatus::FINISHED)
				std::cout << " forecast_winner_agrees=" << (forecast.winner == server.result->winner);
		}
		std::cout << std::endl;
		for(size_t index = 0; index < samples.size(); ++index)
		{
			const auto & sample = samples[index];
			std::cout << "FORCED_BATTLE_SAMPLE case=" << duel.name << " oracle_seed=" << seed
				<< " sample_index=" << index << " status=" << static_cast<int>(sample.status)
				<< " winner=" << static_cast<int>(sample.winner)
				<< " reason=" << sample.unsupportedReason << " bonus=" << sample.unsupportedBonusType
				<< " a_survivors=" << sample.remainingCreatures[0] << " d_survivors=" << sample.remainingCreatures[1]
				<< " a_lost_value=" << sample.initialArmyValue[0] - sample.remainingArmyValue[0]
				<< " d_lost_value=" << sample.initialArmyValue[1] - sample.remainingArmyValue[1];
			if(sample.status == NK2AI::experimental::SampledBattleStatus::FINISHED)
				std::cout << " agrees=" << (sample.winner == server.result->winner);
			std::cout << std::endl;
		}
		// Destroy BattleInfo before either army (its destructor detaches them).
		handler.reset();
		server.state.reset();
	}
};

TEST_F(ForcedBattleReference, OrdinaryMelee)
{
	ASSERT_NO_FATAL_FAILURE(run({"ordinary_melee", 0, 40, 0, 10}, 17041));
}
TEST_F(ForcedBattleReference, RangedAgainstSlowMelee)
{
	ASSERT_NO_FATAL_FAILURE(run({"ranged_slow_melee", 2, 20, 58, 30}, 17047));
}
TEST_F(ForcedBattleReference, SlowMeleeAgainstRanged)
{
	ASSERT_NO_FATAL_FAILURE(run({"slow_melee_ranged", 58, 30, 2, 20}, 17053));
}
TEST_F(ForcedBattleReference, UndeadMelee)
{
	ASSERT_NO_FATAL_FAILURE(run({"undead_melee", 56, 40, 58, 20}, 17041));
}
TEST_F(ForcedBattleReference, FlyingAgainstRanged)
{
	ASSERT_NO_FATAL_FAILURE(run({"flying_ranged", 30, 30, 2, 20}, 17053));
}
TEST_F(ForcedBattleReference, RangedMirror)
{
	ASSERT_NO_FATAL_FAILURE(run({"ranged_mirror", 2, 20, 2, 20}, 17047));
}

// Predeclared whole sweep: no filtering by a model prediction or winner.
struct SweepCase { int count; int seed; bool reverse; };
class ForcedBattleCountSweep : public ForcedBattleReference, public ::testing::WithParamInterface<SweepCase> {};
TEST_P(ForcedBattleCountSweep, ArcherWalkingDead)
{
	const auto param = GetParam();
	const auto name = std::string(param.reverse ? "sweep_slow_ranged_" : "sweep_ranged_slow_")
		+ std::to_string(param.count) + "_" + std::to_string(param.seed);
	const Duel duel = param.reverse
		? Duel{name.c_str(), 58, param.count, 2, 20}
		: Duel{name.c_str(), 2, 20, 58, param.count};
	ASSERT_NO_FATAL_FAILURE(run(duel, param.seed));
}
std::vector<SweepCase> countSweep()
{
	std::vector<SweepCase> cases;
	for(const int count : {10, 20, 30, 40, 60, 80, 120})
		for(const int seed : {17041, 17047, 17053})
			for(const bool reverse : {false, true})
				cases.push_back({count, seed, reverse});
	return cases;
}
INSTANTIATE_TEST_SUITE_P(Predeclared, ForcedBattleCountSweep, ::testing::ValuesIn(countSweep()));
TEST_F(ForcedBattleReference, MixedArmyLowerDefenderCount)
{
	ASSERT_NO_FATAL_FAILURE(run({"mixed_low", 2, 20, 58, 15, {{0, 20}}, {{56, 20}}}, 17041));
}
TEST_F(ForcedBattleReference, MixedArmyHigherDefenderCount)
{
	ASSERT_NO_FATAL_FAILURE(run({"mixed_high", 2, 20, 58, 60, {{0, 20}}, {{56, 80}}}, 17041));
}

TEST_F(ForcedBattleReference, RealNeutralSplit)
{
	ASSERT_NO_FATAL_FAILURE(runNeutral(false));
}
TEST_F(ForcedBattleReference, RealNeutralSingleUpgrade)
{
	ASSERT_NO_FATAL_FAILURE(runNeutral(true));
}

TEST_F(ForcedBattleReference, NativeMarksmanDoubleShot)
{
	ASSERT_NO_FATAL_FAILURE(run({"marksman_double_shot", 3, 20, 58, 50}, 17041));
}
TEST_F(ForcedBattleReference, NativeGrandElfDoubleShot)
{
	ASSERT_NO_FATAL_FAILURE(run({"grand_elf_double_shot", 19, 20, 58, 50}, 17047));
}
TEST_F(ForcedBattleReference, NativeGriffinRetaliation)
{
	ASSERT_NO_FATAL_FAILURE(run({"griffin_retaliation", 4, 20, 56, 40, {}, {{56, 20}}}, 17041));
}
TEST_F(ForcedBattleReference, NativeRoyalGriffinRetaliation)
{
	ASSERT_NO_FATAL_FAILURE(run({"royal_griffin_retaliation", 5, 20, 56, 40, {}, {{56, 20}}}, 17047));
}
TEST_F(ForcedBattleReference, NativeSpriteBlocksRetaliation)
{
	ASSERT_NO_FATAL_FAILURE(run({"sprite_blocks_retaliation", 119, 40, 56, 40}, 17053));
}
TEST_F(ForcedBattleReference, NativeHeroSpecialtyTroops)
{
	ASSERT_NO_FATAL_FAILURE(runNeutral(false, true));
}
// Separate mechanics tests: no terminal oracle outcome or calibration claim.
class ForcedDefendReference : public ForcedBattleReference {};
TEST_F(ForcedDefendReference, DefendNativeMinimumDefense)
{
    ASSERT_NO_FATAL_FAILURE(verifyDefend(84, true, true));
}
TEST_F(ForcedDefendReference, DefendNativePercentageAndMorale)
{
    ASSERT_NO_FATAL_FAILURE(verifyDefend(0, false, true));
}
TEST_F(ForcedDefendReference, DefendExplicitStanceMechanicsOnly)
{
    ASSERT_NO_FATAL_FAILURE(verifyDefend(0, false, true, 3));
}

struct DecisionHoldoutCase { const char * family; int ownCreature, ownCount, enemyCreature, enemyCount, seed; };
class ForcedDecisionHoldout : public ForcedBattleReference, public ::testing::WithParamInterface<DecisionHoldoutCase> {};
TEST_P(ForcedDecisionHoldout, ProducerThenIndependentServer)
{
    const auto c = GetParam();
    ASSERT_NO_FATAL_FAILURE(runDecisionHoldout(c.family, c.seed, c.ownCreature, c.ownCount, c.enemyCreature, c.enemyCount));
}
std::vector<DecisionHoldoutCase> decisionHoldouts()
{
    std::vector<DecisionHoldoutCase> cases;
    for(const int seed : {29009, 29017, 29021})
    {
        cases.push_back({"neutral_challenge", -1, 0, 58, 47, seed});
        cases.push_back({"grandelf_70", 19, 20, 58, 70, seed});
        cases.push_back({"grandelf_80", 19, 20, 58, 80, seed});
        cases.push_back({"sprite_skeleton", 119, 40, 56, 40, seed});
    }
    // Known regression is explicit, not mislabeled as a new holdout.
    cases.push_back({"neutral_known_regression", -1, 0, 58, 47, 17041});
    // Post-original-case hypothesis expansion, fixed before these outcomes.
    for(const int seed : {29009, 29017, 29021})
    {
        cases.push_back({"grandelf_archer60", 19, 20, 2, 60, seed});
        cases.push_back({"grandelf_archer70", 19, 20, 2, 70, seed});
    }
    return cases;
}
INSTANTIATE_TEST_SUITE_P(IndependentServer, ForcedDecisionHoldout, ::testing::ValuesIn(decisionHoldouts()));

}
