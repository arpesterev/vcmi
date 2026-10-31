/*
 * NativePortBattleTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later.
 */
#include "StdInc.h"

#include "server/battles/BattleTestFixture.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/callback/AIFactory.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/CBattleGameInterface.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../server/CGameHandler.h"
#include "../../server/battles/BattleProcessor.h"

namespace
{
class DecisionCallback final : public CBattleCallback
{
public:
	explicit DecisionCallback(PlayerColor player) : CBattleCallback(player, nullptr) {}
	std::optional<BattleAction> action;

	void battleMakeUnitAction(const BattleID &, const BattleAction & value) override
	{
		if(action)
			throw std::logic_error("Backend submitted two unit actions for one activation");
		action = value;
	}
	void battleMakeTacticAction(const BattleID &, const BattleAction & value) override { action = value; }
	void battleMakeSpellAction(const BattleID &, const BattleAction &) override
	{
		throw std::logic_error("Partial port unexpectedly submitted an unimplemented hero-spell decision");
	}
};
}

class AIIntegrationNativeBattle : public BattleTestFixture
{
protected:
	void prepareBattle(CreatureID attackerType, int attackerCount, CreatureID defenderType, int defenderCount)
	{
		startGame();
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		const StackLocation attackerSlot(attackerSideHero->id, SlotID(0));
		const StackLocation defenderSlot(defenderSideHero->id, SlotID(0));
		ASSERT_TRUE(gameHandler->changeStackType(attackerSlot, attackerType.toCreature()));
		ASSERT_TRUE(gameHandler->changeStackType(defenderSlot, defenderType.toCreature()));
		ASSERT_TRUE(gameHandler->changeStackCount(attackerSlot, attackerCount, ChangeValueMode::ABSOLUTE));
		ASSERT_TRUE(gameHandler->changeStackCount(defenderSlot, defenderCount, ChangeValueMode::ABSOLUTE));

		// Production startup registers the battle activity and starts the normal turn flow.
		gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
		ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	}

	void runToCompletion()
	{
		ASSERT_EQ(gameState()->currentBattles.size(), 1u);
		const auto battleID = battle()->getBattleID();
		const auto attackerPlayer = battle()->sideToPlayer(BattleSide::ATTACKER);
		std::map<PlayerColor, std::shared_ptr<DecisionCallback>> callbacks;
		std::map<PlayerColor, std::shared_ptr<CBattleGameInterface>> policies;
		for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			const auto player = battle()->sideToPlayer(side);
			auto callback = std::make_shared<DecisionCallback>(player);
			callback->onBattleStarted(battle());
			auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
			ASSERT_NE(policy, nullptr);
			EXPECT_EQ(policy->dllName, "FHeroes2Tactics");
			policy->playerID = player;
			policy->initBattleInterface(nullptr, callback);
			policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
				battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
				battle()->getSideHero(BattleSide::ATTACKER), battle()->getSideHero(BattleSide::DEFENDER), side, false);
			callbacks.emplace(player, callback);
			policies.emplace(player, policy);
		}

		if(battle()->tacticDistance > 0)
		{
			const auto player = battle()->sideToPlayer(battle()->tacticsSide);
			auto & callback = callbacks.at(player);
			policies.at(player)->yourTacticPhase(battleID, battle()->tacticDistance);
			ASSERT_TRUE(callback->action.has_value());
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action));
		}

		int decisions = 0;
		int64_t damageObserved = 0;
		const auto health = [this]
		{
			int64_t total = 0;
			for(const auto & unit : battle()->stacks)
				total += unit->getTotalHealth();
			return total;
		};
		while(server.battleResults.empty() && decisions < 512)
		{
			ASSERT_FALSE(gameState()->currentBattles.empty());
			const auto * active = dynamic_cast<const CStack *>(battle()->battleActiveUnit());
			ASSERT_NE(active, nullptr) << "Battle stopped without a result or an active stack";
			const auto player = battle()->battleGetOwner(active);
			auto & callback = callbacks.at(player);
			callback->action.reset();
			policies.at(player)->activeStack(battleID, active);
			ASSERT_TRUE(callback->action.has_value()) << "Backend did not respond to the actual active-stack event";
			const auto action = *callback->action;
			const auto before = health();
			for(auto & entry : policies)
				entry.second->actionStarted(battleID, action);
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action)) << "The engine rejected the selected action";
			++decisions;
			if(server.battleResults.empty())
			{
				damageObserved += std::max<int64_t>(0, before - health());
				for(auto & entry : policies)
					entry.second->actionFinished(battleID, action);
			}
		}
		ASSERT_EQ(server.battleResults.size(), 1u) << "No unique result after 512 actual stack activations";
		const auto result = server.battleResults.back();
		EXPECT_GT(decisions, 0);
		EXPECT_GT(damageObserved, 0);
		EXPECT_TRUE(result.winner == BattleSide::ATTACKER || result.winner == BattleSide::DEFENDER);
		for(auto & entry : policies)
			entry.second->battleEnd(battleID, &result, result.questionID);
		if(result.questionID != QuestionID::NONE)
			ASSERT_TRUE(gameHandler->answerQuestion(result.questionID, 0, attackerPlayer));
		EXPECT_TRUE(gameState()->currentBattles.empty()) << "The battle result was not applied to the adventure state";
		for(auto & entry : callbacks)
			entry.second->onBattleEnded(battleID);
	}
};

TEST_F(AIIntegrationNativeBattle, completesMeleeBattleThroughFactory)
{
	prepareBattle(CreatureID(0), 30, CreatureID(0), 20);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion();
}

TEST_F(AIIntegrationNativeBattle, completesRangedBattleThroughFactory)
{
	prepareBattle(CreatureID(2), 20, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion();
}

TEST_F(AIIntegrationNativeBattle, completePortNameCannotSilentlySelectThePartialPolicy)
{
	EXPECT_THROW(AIFactory::createBattleAI("FHeroes2"), std::runtime_error);
	EXPECT_THROW(AIFactory::createBattleAI("HoMM3Original"), std::runtime_error);
	EXPECT_THROW(AIFactory::createBattleAI("TUMEnchanted"), std::runtime_error);
}
