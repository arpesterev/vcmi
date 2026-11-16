#include "../../lib/callback/CAdventureAI.h"
#include "../../server/activities/ActivityProcessor.h"
#include "../../server/activities/BattleActivities.h"
#include "../../AI/Integration/BattleEventAdapter.h"

namespace
{
class GroundRayAdventure final : public CAdventureAI
{
public:
	std::string getBattleAIName() const override { return "HoMM3OriginalTactics"; }
	void heroGotLevel(const CGHeroInstance *, PrimarySkill, const std::vector<SecondarySkill> &, bool, QuestionID) override {}
	void commanderGotLevel(const CCommanderInstance *, std::vector<ui32>, bool, QuestionID) override {}
	void showBlockingDialog(const std::string &, const std::vector<Component> &, QuestionID, int, bool, bool, bool) override {}
	void showGarrisonDialog(const CArmedInstance *, const CGHeroInstance *, QuestionID, const MetaString &) override {}
	void showTeleportDialog(const CGHeroInstance *, TeleportChannelID, TTeleportExitsList, bool, QuestionID) override {}
	void showMapObjectSelectDialog(QuestionID, const Component &, const MetaString &, const MetaString &, const std::vector<ObjectInstanceID> &) override {}
	std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID &, const BattleStateInfoForRetreat &) override { return std::nullopt; }
};
}

class AIIntegrationNativeGroundLifecycle : public AIIntegrationNativeBattle,
	public ::testing::WithParamInterface<int>
{
};

TEST_P(AIIntegrationNativeGroundLifecycle, homm3ControllerGroundRayRequiresUninterruptedFreshLifecycle)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(2), 80);
	setArmy(defenderSideHero, CreatureID(6), 15);
	for(auto * hero : {attackerSideHero, defenderSideHero})
	{
		for(const auto skill : {SecondarySkill::ARCHERY, SecondarySkill::OFFENCE, SecondarySkill::ARMORER})
			hero->setSecSkillLevel(SecondarySkill(skill), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
		hero->setPrimarySkill(PrimarySkill::ATTACK, 0, ChangeValueMode::ABSOLUTE);
		hero->setPrimarySkill(PrimarySkill::DEFENSE, 0, ChangeValueMode::ABSOLUTE);
	}
	for(const auto & spell : LIBRARY->spellh->objects) attackerSideHero->removeSpellFromSpellbook(spell->id);
	teachBattleSpell(attackerSideHero, SpellID::DISRUPTING_RAY, 6);
	setMagicMastery(attackerSideHero, MasteryLevel::NONE);
	// The base server fixture sends a genuine newly constructed BattleStart without advancing rounds.
	BattleTestFixture::startBattle(ETerrainId::SAND);
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_EQ(battle()->getRound(), 0);
	// Packet-level fixture construction omits the activity needed for server battle completion.
	// Match NoTargetActionTest::addBattleActivity without advancing the fresh round0 snapshot.
	if(GetParam() == 6)
	{
		gameHandler->activities->addActivity(std::make_shared<BattleActivity>(gameHandler.get(), battle()));
		ASSERT_NE(gameHandler->battles->findBattleActivity(*battle()), nullptr);
	}
	CStack * actor = nullptr;
	CStack * target = nullptr;
	for(const auto & unit : battle()->stacks)
		(unit->unitSide() == BattleSide::ATTACKER ? actor : target) = unit.get();
	ASSERT_NE(actor, nullptr);
	ASSERT_NE(target, nullptr);
	actor->setPosition(BattleHex(93));
	target->setPosition(BattleHex(94));
	const auto id = battle()->getBattleID();
	const auto player = attackerSideHero->getOwner();
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(id);
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AIIntegration::HoMM3TacticsBattleAI controller;
	controller.initBattleInterface(environment, callback);
	GroundRayAdventure adventure;
	adventure.env = environment;
	adventure.cbc = callback;
	CBattleGameInterface & receiver = GetParam() >= 6
		? static_cast<CBattleGameInterface &>(adventure) : static_cast<CBattleGameInterface &>(controller);
	// Only this fixture supplies the start callback sequence; production dispatch is tested separately.
	if(GetParam() != 1 && GetParam() != 7)
		receiver.battleStartBefore(id, attackerSideHero, defenderSideHero, defenderSideHero->visitablePos(), attackerSideHero, defenderSideHero);
	receiver.battleStart(id, attackerSideHero, defenderSideHero, defenderSideHero->visitablePos(), attackerSideHero, defenderSideHero, BattleSide::ATTACKER, false);
	if(GetParam() >= 6)
	{
		ASSERT_NE(adventure.battleAI, nullptr);
		EXPECT_NE(dynamic_cast<AIIntegration::BattleEventAdapter *>(adventure.battleAI.get()), nullptr);
	}
	beginCombat();
	ASSERT_FALSE(HasFatalFailure());
	battle()->activeStack = actor->unitId();
	receiver.battleNewRound(id);
	ASSERT_FALSE(view->battleCanShoot(actor));
	const auto admitted = AIIntegration::HoMM3SpellPolicy::choose(view, environment.get(), *actor, {}, true);
	ASSERT_TRUE(admitted) << "Fixture must offer a source-ground-only Ray opportunity";
	EXPECT_EQ(admitted->spell, SpellID(SpellID::DISRUPTING_RAY));
	EXPECT_FALSE(AIIntegration::HoMM3SpellPolicy::choose(view, environment.get(), *actor, {}, false));
	if(GetParam() == 2) receiver.battleSpellCast(id, nullptr);
	if(GetParam() == 3) receiver.battleObstaclesChanged(id, ObstacleChanges{});
	if(GetParam() == 4) receiver.battleUnitsChanged(id, {});
	if(GetParam() == 5) controller.onFreshBattleStart(id);
	receiver.activeStack(id, actor);
	ASSERT_TRUE(callback->action);
	if(GetParam() == 0 || GetParam() == 6)
	{
		EXPECT_TRUE(callback->spellAction);
		EXPECT_EQ(callback->action->spell, SpellID(SpellID::DISRUPTING_RAY));
		if(GetParam() == 6)
		{
			const auto mana = attackerSideHero->mana;
			const auto packets = recorder.spellPackets;
			const auto defense = target->getDefense(false);
			const auto health = target->getAvailableHealth();
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(id, player, *callback->action));
			EXPECT_LT(attackerSideHero->mana, mana);
			EXPECT_GT(recorder.spellPackets, packets);
			EXPECT_LT(target->getDefense(false), defense);
			EXPECT_EQ(target->getAvailableHealth(), health);
			EXPECT_TRUE(target->alive());
			runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
		}
	}
	else
	{
		EXPECT_FALSE(callback->spellAction);
		EXPECT_NE(callback->action->spell, SpellID(SpellID::DISRUPTING_RAY));
	}
}

INSTANTIATE_TEST_SUITE_P(FreshLateSpellObstacleUnitDuplicate, AIIntegrationNativeGroundLifecycle, ::testing::Range(0, 8));
