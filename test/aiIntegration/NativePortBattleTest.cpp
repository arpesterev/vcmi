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
	void runToCompletion()
	{
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
			callbacks.emplace(player, callback);
			policies.emplace(player, policy);
		}

		beginCombat();
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
			const auto * active = dynamic_cast<const CStack *>(battle()->battleActiveUnit());
			ASSERT_NE(active, nullptr) << "Battle stopped without a result or an active stack";
			const auto player = battle()->battleGetOwner(active);
			auto & callback = callbacks.at(player);
			callback->action.reset();
			policies.at(player)->activeStack(BattleID(0), active);
			ASSERT_TRUE(callback->action.has_value()) << "Backend did not respond to the actual active-stack event";
			const auto action = *callback->action;
			const auto before = health();
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), player, action)) << "The engine rejected the selected action";
			++decisions;
			if(server.battleResults.empty())
				damageObserved += std::max<int64_t>(0, before - health());
		}
		ASSERT_FALSE(server.battleResults.empty()) << "No winner after 512 actual stack activations";
		EXPECT_GT(decisions, 0);
		EXPECT_GT(damageObserved, 0);
		EXPECT_TRUE(server.battleResults.back().winner == BattleSide::ATTACKER || server.battleResults.back().winner == BattleSide::DEFENDER);
	}
};

TEST_F(AIIntegrationNativeBattle, completesMeleeBattleThroughFactory)
{
	startGame();
	startBattle();
	addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 30);
	addStack(BattleSide::DEFENDER, CreatureID(0), BattleHex(rightHex + 3), 20);
	runToCompletion();
}

TEST_F(AIIntegrationNativeBattle, completesRangedBattleThroughFactory)
{
	startGame();
	startBattle();
	addStack(BattleSide::ATTACKER, CreatureID(2), BattleHex(leftHex), 20);
	addStack(BattleSide::DEFENDER, CreatureID(0), BattleHex(rightHex + 3), 30);
	runToCompletion();
}

TEST_F(AIIntegrationNativeBattle, completePortNameCannotSilentlySelectThePartialPolicy)
{
	EXPECT_THROW(AIFactory::createBattleAI("FHeroes2"), std::runtime_error);
	EXPECT_THROW(AIFactory::createBattleAI("HoMM3Original"), std::runtime_error);
	EXPECT_THROW(AIFactory::createBattleAI("TUMEnchanted"), std::runtime_error);
}
