#include "../../lib/callback/CAdventureAI.h"
#include "../../AI/Integration/BattleEventAdapter.h"

namespace
{
class FreshBridgeAdventure final : public CAdventureAI
{
public:
	std::string getBattleAIName() const override { return "EmptyAI"; }
	void heroGotLevel(const CGHeroInstance *, PrimarySkill, const std::vector<SecondarySkill> &, bool, QuestionID) override {}
	void commanderGotLevel(const CCommanderInstance *, std::vector<ui32>, bool, QuestionID) override {}
	void showBlockingDialog(const std::string &, const std::vector<Component> &, QuestionID, int, bool, bool, bool) override {}
	void showGarrisonDialog(const CArmedInstance *, const CGHeroInstance *, QuestionID, const MetaString &) override {}
	void showTeleportDialog(const CGHeroInstance *, TeleportChannelID, TTeleportExitsList, bool, QuestionID) override {}
	void showMapObjectSelectDialog(QuestionID, const Component &, const MetaString &, const MetaString &, const std::vector<ObjectInstanceID> &) override {}
	std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID &, const BattleStateInfoForRetreat &) override { return std::nullopt; }
};
class FreshBridgeRecorder final : public CBattleGameInterface, public IFreshBattleStartObserver
{
public:
	std::vector<std::string> order;
	void initBattleInterface(std::shared_ptr<Environment>, std::shared_ptr<CBattleCallback>) override { order.push_back("init"); }
	void onFreshBattleStart(const BattleID &) override { order.push_back("fresh"); }
	void battleStartBefore(const BattleID &, const CCreatureSet *, const CCreatureSet *, int3, const CGHeroInstance *, const CGHeroInstance *) override { order.push_back("before"); }
	void battleStart(const BattleID &, const CCreatureSet *, const CCreatureSet *, int3, const CGHeroInstance *, const CGHeroInstance *, BattleSide, bool) override { order.push_back("start"); }
	void activeStack(const BattleID &, const CStack *) override {}
	void yourTacticPhase(const BattleID &, int) override {}
};
}

TEST(NativeFreshBattleStartBridge, TokenMatchesOnlyOnceAndRejectsDuplicatesStaleOrUnmatched)
{
	FreshBattleStartToken token;
	EXPECT_FALSE(token.consume(BattleID(1)));
	token.before(BattleID(2), false);
	EXPECT_TRUE(token.consume(BattleID(2)));
	EXPECT_FALSE(token.consume(BattleID(2)));
	token.before(BattleID(2), false);
	EXPECT_FALSE(token.consume(BattleID(2)));
	token.before(BattleID(3), false);
	token.before(BattleID(3), false);
	EXPECT_FALSE(token.consume(BattleID(3)));
	token.before(BattleID(4), false);
	EXPECT_FALSE(token.consume(BattleID(5)));
	EXPECT_FALSE(token.consume(BattleID(4)));
	token.before(BattleID(6), true);
	EXPECT_FALSE(token.consume(BattleID(6)));
	token.before(BattleID(7), false);
	token.cancel();
	EXPECT_FALSE(token.consume(BattleID(7)));
	token.before(BattleID(8), false);
	EXPECT_TRUE(token.consume(BattleID(8)));
}

TEST(NativeFreshBattleStartBridge, AdapterPreservesProvenanceOrderWithoutSyntheticBefore)
{
	auto impl = std::make_shared<FreshBridgeRecorder>();
	AIIntegration::BattleEventAdapter adapter("fixture", impl);
	adapter.initBattleInterface(nullptr, nullptr);
	auto * observer = dynamic_cast<IFreshBattleStartObserver *>(&adapter);
	ASSERT_NE(observer, nullptr);
	observer->onFreshBattleStart(BattleID(1));
	adapter.battleStart(BattleID(1), nullptr, nullptr, int3(), nullptr, nullptr, BattleSide::ATTACKER, false);
	EXPECT_EQ(impl->order, (std::vector<std::string>{"init", "fresh", "start"}));
}

TEST(NativeFreshBattleStartBridge, AdventureBridgePreservesWaitTillRealizeAcrossFreshAndUnmatchedStarts)
{
	for(bool original : {false, true})
	{
		auto callback = std::make_shared<DecisionCallback>(PlayerColor(0));
		callback->waitTillRealize = original;
		FreshBridgeAdventure owner;
		owner.cbc = callback;
		owner.battleStartBefore(BattleID(1), nullptr, nullptr, int3(), nullptr, nullptr);
		owner.battleStart(BattleID(1), nullptr, nullptr, int3(), nullptr, nullptr, BattleSide::ATTACKER, false);
		EXPECT_FALSE(callback->waitTillRealize);
		owner.battleEnd(BattleID(1), nullptr, QuestionID(0));
		EXPECT_EQ(callback->waitTillRealize, original);
		owner.battleStart(BattleID(2), nullptr, nullptr, int3(), nullptr, nullptr, BattleSide::ATTACKER, false);
		EXPECT_FALSE(callback->waitTillRealize);
		owner.battleEnd(BattleID(2), nullptr, QuestionID(0));
		EXPECT_EQ(callback->waitTillRealize, original);
	}
}

TEST(NativeFreshBattleStartBridge, AdventureDestructionRestoresWaitTillRealize)
{
	auto callback = std::make_shared<DecisionCallback>(PlayerColor(0));
	callback->waitTillRealize = true;
	{
		FreshBridgeAdventure owner;
		owner.cbc = callback;
		owner.battleStartBefore(BattleID(1), nullptr, nullptr, int3(), nullptr, nullptr);
		owner.battleStart(BattleID(1), nullptr, nullptr, int3(), nullptr, nullptr, BattleSide::ATTACKER, false);
		EXPECT_FALSE(callback->waitTillRealize);
	}
	EXPECT_TRUE(callback->waitTillRealize);
}
