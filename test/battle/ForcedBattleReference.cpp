/*
 * Forced server battles: an outcome oracle independent of adventure decisions.
 * See ForcedBattleReference.md for policy, scope and reproducibility limits.
 */
#include "StdInc.h"
#include "mock/mock_MapService.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/StartInfo.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/BattleLayout.h"
#include "../../lib/battle/Unit.h"
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

struct Duel
{
	const char * name;
	int attackerCreature;
	int attackerCount;
	int defenderCreature;
	int defenderCount;
};

class ForcedBattleReference : public ::testing::Test, public MapListener
{
protected:
	ReferenceServer server;
	std::unique_ptr<CGameHandler> handler;
	CMap * map = nullptr;
	int3 battleTile;
	void mapLoaded(CMap * loaded) override { map = loaded; }

	void initialize(int seed)
	{
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
		server.state->init(&mapService, &start, randomizer, progress, false);
		ASSERT_NE(map, nullptr);
		ASSERT_FALSE(map->getHeroesOnMap().empty());
		const auto * contextHero = dynamic_cast<const CGHeroInstance *>(map->getObject(*map->getHeroesOnMap().begin()));
		ASSERT_NE(contextHero, nullptr);
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
		result->pos = battleTile;
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

	void run(const Duel & duel, int seed)
	{
		SCOPED_TRACE(std::string(duel.name) + " seed=" + std::to_string(seed));
		initialize(seed);
		ASSERT_FALSE(HasFatalFailure());
		auto attacker = army(PlayerColor(0), duel.attackerCreature, duel.attackerCount);
		auto defender = army(PlayerColor::NEUTRAL, duel.defenderCreature, duel.defenderCount);
		auto layout = BattleLayout::createDefaultLayout(*server.state, attacker.get(), defender.get());
		layout.obstaclesAllowed = false;
		layout.tacticsAllowed = false;
		const BattleID id = server.state->nextBattleID;
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
		const auto pessimistic = NK2AI::experimental::runDetachedDuel(*initial, false);
		const auto optimistic = NK2AI::experimental::runDetachedDuel(*initial, true);
		ASSERT_EQ(attacker->battle, initial);
		ASSERT_EQ(defender->battle, initial);
		for(const auto * unit : initial->battleGetUnitsIf([](const battle::Unit *) { return true; }))
			EXPECT_EQ(unit->getAvailableHealth(), unit->getTotalHealth()) << "Forecast mutated the real initial battle";
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
			EXPECT_GT(shots, 0);
		if(std::string(duel.name) == "undead_melee")
			EXPECT_GT(server.retaliationPackets, 0);
		const auto losingSide = server.result->winner == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
		const auto losingCreature = CreatureID(losingSide == BattleSide::ATTACKER ? duel.attackerCreature : duel.defenderCreature);
		const auto losingCount = losingSide == BattleSide::ATTACKER ? duel.attackerCount : duel.defenderCount;
		EXPECT_EQ(server.result->casualties[losingSide][losingCreature], losingCount);
		std::cout << "FORCED_BATTLE_REFERENCE case=" << duel.name << " seed=" << seed
			<< " attacker=" << duel.attackerCreature << ':' << duel.attackerCount
			<< " defender=" << duel.defenderCreature << ':' << duel.defenderCount
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
		// Destroy BattleInfo before either army (its destructor detaches them).
		handler.reset();
		server.state.reset();
	}
};

TEST_F(ForcedBattleReference, OrdinaryMelee)
{
	run({"ordinary_melee", 0, 40, 0, 10}, 17041);
}
TEST_F(ForcedBattleReference, RangedAgainstSlowMelee)
{
	run({"ranged_slow_melee", 2, 20, 58, 30}, 17047);
}
TEST_F(ForcedBattleReference, SlowMeleeAgainstRanged)
{
	run({"slow_melee_ranged", 58, 30, 2, 20}, 17053);
}
TEST_F(ForcedBattleReference, UndeadMelee)
{
	run({"undead_melee", 56, 40, 58, 20}, 17041);
}
TEST_F(ForcedBattleReference, FlyingAgainstRanged)
{
	run({"flying_ranged", 30, 30, 2, 20}, 17053);
}
TEST_F(ForcedBattleReference, RangedMirror)
{
	run({"ranged_mirror", 2, 20, 2, 20}, 17047);
}
}
