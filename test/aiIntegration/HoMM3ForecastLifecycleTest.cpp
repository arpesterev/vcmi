/*
 * HoMM3ForecastLifecycleTest.cpp, part of VCMI engine tests.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Setup follows BattleTestFixture. The event bridge models NetPacksClient.cpp;
 * this is a native pack-order fixture, not an execution of the GUI client dispatcher.
 */
#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "homm3Forecast/NativeLifecycle.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/StackLocation.h"
#include "../../server/CGameHandler.h"
#include "../../server/battles/BattleProcessor.h"

namespace
{
class EventGameServer : public RecordingGameServer
{
public:
	std::function<void(CPackForClient &)> before;
	std::function<void(CPackForClient &)> after;
	void applyPack(CPackForClient & pack) override
	{
		if(before) before(pack);
		RecordingGameServer::applyPack(pack);
		if(after) after(pack);
	}
};

class HoMM3ForecastLifecycleNative : public BattleTestFixture
{
protected:
	EventGameServer eventServer;
	std::array<std::unique_ptr<CPlayerBattleCallback>, 2> views;
	std::array<std::unique_ptr<HoMM3Forecast::NativeLifecycle>, 2> listeners;
	struct ActionObservation
	{
		EActionType action;
		BattleSide side;
		bool nativeWaited;
		std::array<std::optional<bool>, 2> phase;
	};
	std::vector<ActionObservation> beforeActions;
	std::vector<ActionObservation> afterActions;
	std::array<unsigned, 2> activations{0, 0};

	void prepare(bool begin = true)
	{
		startGame(); ASSERT_FALSE(HasFatalFailure());
		eventServer.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(eventServer, gameState());
		gameHandler->randomizer->setSeed(seed);
		ASSERT_TRUE(gameHandler->changeStackType(StackLocation(attackerSideHero->id, SlotID(0)), CreatureID(19).toCreature()));
		ASSERT_TRUE(gameHandler->changeStackCount(StackLocation(attackerSideHero->id, SlotID(0)), 20, ChangeValueMode::ABSOLUTE));
		ASSERT_TRUE(gameHandler->changeStackType(StackLocation(defenderSideHero->id, SlotID(0)), CreatureID(1).toCreature()));
		ASSERT_TRUE(gameHandler->changeStackCount(StackLocation(defenderSideHero->id, SlotID(0)), 10, ChangeValueMode::ABSOLUTE));
		ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(attackerSideHero->id, SlotID(5)), CreatureID(2).toCreature(), 20));
		ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(defenderSideHero->id, SlotID(5)), CreatureID(0).toCreature(), 20));
		startBattle(); ASSERT_FALSE(HasFatalFailure());
		for(std::size_t side = 0; side < 2; ++side)
		{
			views[side] = std::make_unique<CPlayerBattleCallback>(battle(), battle()->sideToPlayer(static_cast<BattleSide>(side)));
			listeners[side] = std::make_unique<HoMM3Forecast::NativeLifecycle>(*views[side]);
			listeners[side]->battleStart(battle()->getBattleID(), nullptr, nullptr, int3(),
			nullptr, nullptr, static_cast<BattleSide>(side), false);
		}
		eventServer.before = [this](CPackForClient & pack)
		{
			if(const auto * action = dynamic_cast<StartAction *>(&pack))
			{
				for(auto & listener : listeners) listener->actionStarted(action->battleID, action->ba);
				if(action->ba.isUnitAction()) beforeActions.push_back(observation(action->ba.actionType));
			}
		};
		eventServer.after = [this](CPackForClient & pack)
		{
			if(const auto * round = dynamic_cast<BattleNextRound *>(&pack))
				for(auto & listener : listeners) listener->battleNewRound(round->battleID);
			if(const auto * active = dynamic_cast<BattleSetActiveStack *>(&pack))
			{
				if(active->reason != BattleUnitTurnReason::AUTOMATIC_ACTION)
				{
					const auto side = static_cast<std::size_t>(views[0]->battleActiveUnit()->controllingSide());
					++activations[side];
					listeners[side]->ownActivation(active->battleID);
				}
			}
			if(const auto * action = dynamic_cast<StartAction *>(&pack))
				if(action->ba.isUnitAction()) afterActions.push_back(observation(action->ba.actionType));
		};
		if(begin) { beginCombat(); ASSERT_FALSE(HasFatalFailure()); }
	}

	ActionObservation observation(EActionType type) const
	{
		const auto * unit = views[0]->battleActiveUnit();
		return {type, unit->controllingSide(), unit->waited(), {listeners[0]->secondPhase(), listeners[1]->secondPhase()}};
	}
	void submit(bool wait)
	{
		const auto * unit = views[0]->battleActiveUnit();
		ASSERT_NE(unit, nullptr);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),
			battle()->sideToPlayer(unit->controllingSide()), wait ? BattleAction::makeWait(unit) : BattleAction::makeDefend(unit)));
	}
	const CStack * initial(BattleSide side, SlotID slot) const
	{
		for(const auto * unit : views[0]->battleGetUnitsIf([](const battle::Unit *) { return true; }))
			if(unit->unitSide() == side && unit->unitSlot() == slot) return dynamic_cast<const CStack *>(unit);
		return nullptr;
	}
	static const HoMM3Forecast::ForecastUnit & dto(const HoMM3Forecast::ForecastInput & input, const battle::Unit * unit)
	{
		return *std::find_if(input.units.begin(), input.units.end(), [unit](const auto & value) { return value.order.id == unit->unitId(); });
	}
	void enterEnemyWait()
	{
		submit(false); ASSERT_FALSE(HasFatalFailure());
		ASSERT_EQ(views[0]->battleActiveUnit()->unitSide(), BattleSide::DEFENDER);
		submit(true); ASSERT_FALSE(HasFatalFailure());
		submit(false); ASSERT_FALSE(HasFatalFailure());
		submit(false); ASSERT_FALSE(HasFatalFailure());
		ASSERT_TRUE(views[0]->battleActiveUnit()->waited());
		ASSERT_EQ(views[0]->battleActiveUnit()->unitSide(), BattleSide::DEFENDER);
	}
};
}

TEST_F(HoMM3ForecastLifecycleNative, startupWaitsForActualPostApplyRound)
{
	prepare(false); ASSERT_FALSE(HasFatalFailure());
	EXPECT_FALSE(listeners[0]->secondPhase().has_value());
	EXPECT_THROW(listeners[0]->snapshot(), HoMM3Forecast::UnsupportedSnapshot);
	beginCombat(); ASSERT_FALSE(HasFatalFailure());
	EXPECT_EQ(listeners[0]->secondPhase(), std::optional<bool>(false));
	EXPECT_EQ(listeners[1]->secondPhase(), std::optional<bool>(false));
	EXPECT_EQ(activations[0], 1u);
	EXPECT_EQ(activations[1], 0u);
	EXPECT_EQ(listeners[0]->snapshot().units.size(), 4u);
}

TEST_F(HoMM3ForecastLifecycleNative, firstWaitDoesNotBecomeSecondPhaseAfterApply)
{
	prepare(); ASSERT_FALSE(HasFatalFailure());
	submit(true); ASSERT_FALSE(HasFatalFailure());
	ASSERT_EQ(beforeActions.size(), 1u); ASSERT_EQ(afterActions.size(), 1u);
	EXPECT_FALSE(beforeActions[0].nativeWaited);
	EXPECT_TRUE(afterActions[0].nativeWaited);
	for(std::size_t side = 0; side < 2; ++side)
	{
		EXPECT_EQ(beforeActions[0].phase[side], std::optional<bool>(false));
		EXPECT_EQ(afterActions[0].phase[side], std::optional<bool>(false));
		listeners[side]->actionFinished(battle()->getBattleID(), BattleAction::makeWait(initial(BattleSide::ATTACKER, SlotID(0))));
		EXPECT_EQ(listeners[side]->secondPhase(), std::optional<bool>(false));
	}
}

TEST_F(HoMM3ForecastLifecycleNative, enemyWaitedActionIsObservedBeforeStatusClears)
{
	prepare(); ASSERT_FALSE(HasFatalFailure());
	enterEnemyWait(); ASSERT_FALSE(HasFatalFailure());
	EXPECT_EQ(listeners[0]->secondPhase(), std::optional<bool>(false));
	EXPECT_EQ(listeners[1]->secondPhase(), std::optional<bool>(true));
	submit(false); ASSERT_FALSE(HasFatalFailure());
	ASSERT_EQ(beforeActions.size(), 5u); ASSERT_EQ(afterActions.size(), 5u);
	EXPECT_TRUE(beforeActions.back().nativeWaited);
	EXPECT_FALSE(afterActions.back().nativeWaited);
	for(std::size_t side = 0; side < 2; ++side)
	{
		EXPECT_EQ(beforeActions.back().phase[side], std::optional<bool>(true));
		EXPECT_EQ(afterActions.back().phase[side], std::optional<bool>(true));
		EXPECT_EQ(listeners[side]->secondPhase(), std::optional<bool>(false)); // next real round
	}
}

TEST_F(HoMM3ForecastLifecycleNative, lateRegistrationDoesNotInventFirstPhase)
{
	prepare(); ASSERT_FALSE(HasFatalFailure());
	submit(true); ASSERT_FALSE(HasFatalFailure());
	listeners[1]->attach(battle()->getBattleID());
	listeners[1]->ownActivation(battle()->getBattleID());
	EXPECT_FALSE(listeners[1]->secondPhase().has_value());
	EXPECT_THROW(listeners[1]->snapshot(), HoMM3Forecast::UnsupportedSnapshot);
	for(int action = 0; action < 4; ++action) { submit(false); ASSERT_FALSE(HasFatalFailure()); }
	EXPECT_EQ(listeners[1]->secondPhase(), std::optional<bool>(false));
	submit(false); ASSERT_FALSE(HasFatalFailure());
	EXPECT_EQ(listeners[1]->snapshot().units.size(), 4u);
}

TEST_F(HoMM3ForecastLifecycleNative, lateWaitedActivationCanRecoverSecondPhase)
{
	prepare(); ASSERT_FALSE(HasFatalFailure());
	enterEnemyWait(); ASSERT_FALSE(HasFatalFailure());
	listeners[1]->attach(battle()->getBattleID());
	EXPECT_FALSE(listeners[1]->secondPhase().has_value());
	listeners[1]->ownActivation(battle()->getBattleID());
	ASSERT_EQ(listeners[1]->secondPhase(), std::optional<bool>(true));
	const auto input = listeners[1]->snapshot();
	EXPECT_TRUE(input.secondPhase);
	for(const auto & unit : input.units) EXPECT_FALSE(unit.order.waiting);
}

TEST_F(HoMM3ForecastLifecycleNative, lateEnemyPreActionCanRecoverWithoutEnemyActivation)
{
	prepare(); ASSERT_FALSE(HasFatalFailure());
	enterEnemyWait(); ASSERT_FALSE(HasFatalFailure());
	listeners[0]->attach(battle()->getBattleID());
	EXPECT_FALSE(listeners[0]->secondPhase().has_value());
	const auto ownActivations = activations[0];
	submit(false); ASSERT_FALSE(HasFatalFailure());
	EXPECT_EQ(beforeActions.back().phase[0], std::optional<bool>(true));
	EXPECT_EQ(afterActions.back().phase[0], std::optional<bool>(true));
	EXPECT_EQ(activations[0], ownActivations + 1); // next-round activation, never the enemy activation
}

TEST_F(HoMM3ForecastLifecycleNative, ghostRetainsCompactSlotAcrossLateAttachment)
{
	prepare(); ASSERT_FALSE(HasFatalFailure());
	const auto * removed = initial(BattleSide::ATTACKER, SlotID(0));
	const auto * later = initial(BattleSide::ATTACKER, SlotID(5));
	submit(false); ASSERT_FALSE(HasFatalFailure());
	injure(removed, removed->getAvailableHealth());
	BattleUnitsChanged removal;
	removal.battleID = battle()->getBattleID();
	removal.changedStacks.emplace_back(removed->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(removal);
	ASSERT_TRUE(removed->isGhost());
	const auto input = listeners[1]->snapshot();
	EXPECT_TRUE(dto(input, removed).order.immobilized);
	EXPECT_EQ(dto(input, later).order.sourceIndex, 1);
	listeners[1]->attach(battle()->getBattleID());
	for(int action = 0; action < 3; ++action) { submit(false); ASSERT_FALSE(HasFatalFailure()); }
	const auto late = listeners[1]->snapshot();
	EXPECT_EQ(dto(late, later).order.sourceIndex, 1);
	EXPECT_TRUE(dto(late, removed).order.immobilized);
}

TEST_F(HoMM3ForecastLifecycleNative, hypnosisChangesControllerWithoutChangingRoster)
{
	prepare(); ASSERT_FALSE(HasFatalFailure());
	const auto * target = initial(BattleSide::DEFENDER, SlotID(0));
	teachSpell(attackerSideHero, SpellID::HYPNOTIZE);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 20, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::HYPNOTIZE, target));
	const auto input = listeners[0]->snapshot();
	EXPECT_EQ(dto(input, target).order.owningSide, HoMM3Forecast::Side::DEFENDER);
	EXPECT_EQ(dto(input, target).controllingSide, HoMM3Forecast::Side::ATTACKER);
	EXPECT_EQ(dto(input, target).order.sourceIndex, 0);
}

TEST_F(HoMM3ForecastLifecycleNative, createdUnitsAndEndedBattleRemainUnsupported)
{
	prepare(); ASSERT_FALSE(HasFatalFailure());
	ASSERT_NE(addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 1), nullptr);
	EXPECT_THROW(listeners[0]->snapshot(), HoMM3Forecast::UnsupportedSnapshot);
	listeners[1]->attach(battle()->getBattleID());
	EXPECT_THROW(listeners[1]->snapshot(), HoMM3Forecast::UnsupportedSnapshot);
	listeners[0]->battleEnd(battle()->getBattleID(), nullptr, QuestionID());
	EXPECT_FALSE(listeners[0]->secondPhase().has_value());
	EXPECT_THROW(listeners[0]->snapshot(), HoMM3Forecast::UnsupportedSnapshot);
}

TEST_F(HoMM3ForecastLifecycleNative, heroActionInvalidatesPlanningUntilAuthoritativeReactivation)
{
	prepare(); ASSERT_FALSE(HasFatalFailure());
	const auto * active = views[0]->battleActiveUnit();
	ASSERT_EQ(active->controllingSide(), BattleSide::ATTACKER);
	ASSERT_EQ(listeners[0]->snapshot().units.size(), 4u);
	teachSpell(attackerSideHero, SpellID::BLESS);
	bool observedBefore = false;
	bool observedAfter = false;
	const auto before = eventServer.before;
	const auto after = eventServer.after;
	eventServer.before = [&, before](CPackForClient & pack)
	{
		before(pack);
		if(const auto * action = dynamic_cast<StartAction *>(&pack))
			if(action->ba.actionType == EActionType::HERO_SPELL)
			{
				observedBefore = true;
				EXPECT_THROW(listeners[0]->snapshot(), HoMM3Forecast::UnsupportedSnapshot);
				EXPECT_EQ(listeners[0]->secondPhase(), std::optional<bool>(false));
			}
	};
	eventServer.after = [&, after](CPackForClient & pack)
	{
		after(pack);
		if(const auto * action = dynamic_cast<StartAction *>(&pack))
			if(action->ba.actionType == EActionType::HERO_SPELL)
			{
				observedAfter = true;
				EXPECT_THROW(listeners[0]->snapshot(), HoMM3Forecast::UnsupportedSnapshot);
			}
	};
	const auto ownActivations = activations[0];
	const auto action = BattleAction::makeHeroSpellcast(BattleSide::ATTACKER,
		battle::Target{battle::Destination(active)}, SpellID::BLESS);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),
		battle()->sideToPlayer(BattleSide::ATTACKER), action));
	EXPECT_TRUE(observedBefore);
	EXPECT_TRUE(observedAfter);
	EXPECT_EQ(views[0]->battleActiveUnit(), active);
	EXPECT_EQ(activations[0], ownActivations + 1);
	EXPECT_EQ(listeners[0]->snapshot().units.size(), 4u);
	eventServer.before = before;
	eventServer.after = after;
}
