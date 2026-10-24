/*
 * Forced server battles: an outcome oracle independent of adventure decisions.
 * See ForcedBattleReference.md for policy, scope and reproducibility limits.
 */
#include "StdInc.h"
#include "PlanningBattleSnapshotChecks.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/callback/CGameInfoCallback.h"
#include "../../lib/serializer/BinarySerializer.h"
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

struct Duel
{
	const char * name;
	int attackerCreature;
	int attackerCount;
	int defenderCreature;
	int defenderCount;
	std::vector<std::pair<int, int>> attackerExtras;
	std::vector<std::pair<int, int>> defenderExtras;
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
		const int split = monster->getNumberOfStacks(hero);
		ASSERT_GT(split, 1);
		const auto fields = visible.getBattlefieldCandidates(monster->visitablePos());
		ASSERT_FALSE(fields.empty());
		const auto oracleRandomBefore = RandomizerCapture::capture(*handler->randomizer);
		std::vector<std::pair<BattleField, std::unique_ptr<NK2AI::experimental::PlanningBattleSnapshot>>> variants;
		for(const auto field : fields)
		{
			NK2AI::experimental::PlanningBattleContext context{monster->visitablePos(),
				visible.getTile(monster->visitablePos())->getTerrainID(), field,
				BattleLayout::createDefaultLayout(visible, hero, monster)};
			std::string reason;
			variants.emplace_back(field, NK2AI::experimental::PlanningBattleSnapshot::createNeutralEncounter(visible, *hero, *monster, context, &reason));
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
		auto selected = std::find_if(variants.begin(), variants.end(), [real](const auto & item) { return item.first == real->getBattlefieldType(); });
		EXPECT_NE(selected, variants.end());
		if(selected != variants.end() && selected->second)
		{
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
		}
		else std::cout << "FORCED_NEUTRAL_CAPTURE selected=unsupported_or_unavailable" << std::endl;
		const auto oracleRandomAfterPreparation = RandomizerCapture::capture(*handler->randomizer);
		if(selected != variants.end() && selected->second)
		{
			// A previously admitted capture must also survive the stronger
			// invariance check. Failure stays visible, but no returned pointer
			// from that helper is used to drive the independent real battle.
			test::verifyPlanningSnapshot(visible, *hero, *monster, *real);
		}
		if(selected != variants.end() && selected->second)
			for(const int sampleSeed : {17041, 17047, 17053})
			{
				const auto outcome = NK2AI::experimental::runSampledBattle(selected->second->battle(), selected->second->frozenGameInfo(), sampleSeed);
				std::cout << "FORCED_NEUTRAL_SAMPLE upgrade=" << upgrade << " specialty=" << specialty << " sample_seed=" << sampleSeed
					<< " status=" << static_cast<int>(outcome.status) << " winner=" << static_cast<int>(outcome.winner)
					<< " reason=" << outcome.unsupportedReason << std::endl;
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
			ASSERT_TRUE(handler->battles->makePlayerBattleAction(id, battle->battleGetOwner(unit), action));
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
		auto snapshot = NK2AI::experimental::PlanningBattleSnapshot::create(*server.state, *attacker, *defender, context, &captureReason);
		std::cout << "FORCED_BATTLE_CAPTURE case=" << duel.name << " seed=" << seed
			<< " admitted=" << static_cast<bool>(snapshot) << " reason=" << captureReason << std::endl;
		if(snapshot)
		{
			test::verifyPlanningSnapshot(*server.state, *attacker, *defender, *initial);
		}
		std::vector<NK2AI::experimental::SampledBattleOutcome> samples;
		if(snapshot)
			for(const int sampleSeed : {17041, 17047, 17053})
				samples.push_back(NK2AI::experimental::runSampledBattle(snapshot->battle(), snapshot->frozenGameInfo(), sampleSeed));
		snapshot.reset();
		EXPECT_EQ(RandomizerCapture::capture(*handler->randomizer), oracleRandomBefore)
			<< "Forecast changed authoritative randomizer, including bias history";
		const auto pessimistic = NK2AI::experimental::runDetachedDuel(*initial, false);
		const auto optimistic = NK2AI::experimental::runDetachedDuel(*initial, true);
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
}
