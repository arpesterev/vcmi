/*
 * HoMM3ForecastQueueTest.cpp, part of VCMI engine tests.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Native setup follows VCMI's GPL-licensed BattleTestFixture/NativePortBattleTest.
 * Reconstructed source bodies and control kernels are separately marked CC0-1.0.
 * See homm3Forecast/SOURCE_NOTICE.md for the source boundary and provenance.
 */
#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "homm3Forecast/NativeSnapshot.h"
#include "homm3Forecast/SourcePrefixReference.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/StackLocation.h"
#include "../../server/CGameHandler.h"
#include "../../server/battles/BattleProcessor.h"

namespace
{
class HoMM3ForecastQueueNative : public BattleTestFixture
{
protected:
	HoMM3Forecast::PhaseHistory history;
	std::unique_ptr<CPlayerBattleCallback> view;

	void prepare(bool thirdAlly = false)
	{
		startGame();
		ASSERT_FALSE(HasFatalFailure());
		const StackLocation attacker(attackerSideHero->id, SlotID(0));
		const StackLocation defender(defenderSideHero->id, SlotID(0));
		ASSERT_TRUE(gameHandler->changeStackType(attacker, CreatureID(19).toCreature()));
		ASSERT_TRUE(gameHandler->changeStackCount(attacker, 20, ChangeValueMode::ABSOLUTE));
		ASSERT_TRUE(gameHandler->changeStackType(defender, CreatureID(1).toCreature()));
		ASSERT_TRUE(gameHandler->changeStackCount(defender, 10, ChangeValueMode::ABSOLUTE));
		ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(attackerSideHero->id, SlotID(5)), CreatureID(2).toCreature(), 20));
		ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(defenderSideHero->id, SlotID(5)), CreatureID(0).toCreature(), 20));
		if(thirdAlly)
		{
			ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(attackerSideHero->id, SlotID(3)), CreatureID(0).toCreature(), 5));
		}
		startBattle();
		ASSERT_FALSE(HasFatalFailure());
		beginCombat();
		ASSERT_FALSE(HasFatalFailure());
		view = std::make_unique<CPlayerBattleCallback>(battle(), battle()->sideToPlayer(BattleSide::ATTACKER));
		history.onNewRound(view->battleGetRound());
		history.observeActivation(*view);
	}

	const CStack * initial(BattleSide side, SlotID slot) const
	{
		for(const auto * unit : view->battleGetUnitsIf([](const battle::Unit *) { return true; }))
			if(unit->unitSide() == side && unit->unitSlot() == slot)
				return dynamic_cast<const CStack *>(unit);
		return nullptr;
	}

	static const HoMM3Forecast::ForecastUnit & dto(const HoMM3Forecast::ForecastInput & input, const battle::Unit * unit)
	{
		const auto found = std::find_if(input.units.begin(), input.units.end(), [unit](const auto & entry)
		{
			return entry.order.id == unit->unitId();
		});
		if(found == input.units.end()) throw std::logic_error("fixture unit absent from snapshot");
		return *found;
	}

	void expectPair(const HoMM3Forecast::ForecastInput & input,
		const std::vector<HoMM3PrefixReference::ScriptedDamage> & script = {})
	{
		for(auto side : {HoMM3Forecast::Side::ATTACKER, HoMM3Forecast::Side::DEFENDER})
			EXPECT_EQ(HoMM3PrefixReference::projectedTrace(input, side, script),
				HoMM3PrefixReference::sourceTrace(input, side, script));
	}

	void submitActive(bool wait)
	{
		const auto * active = view->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		const auto beforeRound = view->battleGetRound();
		const auto action = wait ? BattleAction::makeWait(active) : BattleAction::makeDefend(active);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),
			battle()->sideToPlayer(active->controllingSide()), action));
		if(view->battleGetRound() != beforeRound) history.onNewRound(view->battleGetRound());
		history.observeActivation(*view);
	}

	void teach(CGHeroInstance * hero, SpellID spell, int power)
	{
		teachSpell(hero, spell);
		hero->setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
		hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 10, ChangeValueMode::ABSOLUTE);
		hero->mana = 100;
	}
};
}

TEST_F(HoMM3ForecastQueueNative, sparseArmySlotsBecomeCompactSourceIndices)
{
	prepare(); ASSERT_FALSE(HasFatalFailure());
	const auto input = HoMM3Forecast::captureInitialTroops(*view, history);
	ASSERT_EQ(input.units.size(), 4u);
	const auto * first = initial(BattleSide::ATTACKER, SlotID(0));
	const auto * later = initial(BattleSide::ATTACKER, SlotID(5));
	ASSERT_NE(first, nullptr); ASSERT_NE(later, nullptr);
	EXPECT_EQ(dto(input, first).order.sourceIndex, 0);
	EXPECT_EQ(dto(input, later).order.sourceIndex, 1);
	EXPECT_EQ(later->unitSlot().getNum(), 5);
	EXPECT_EQ(dto(input, later).order.effectiveSpeed, later->getInitiative(0));
	const auto trace = HoMM3PrefixReference::projectedTrace(input, HoMM3Forecast::Side::ATTACKER, {});
	EXPECT_EQ(trace.opportunities, std::vector<HoMM3Forecast::UnitId>{first->unitId()});
	expectPair(input);
}

TEST_F(HoMM3ForecastQueueNative, waitPhaseKeepsDoneUnitsAndClearsSourceWaitingFlags)
{
	prepare(); ASSERT_FALSE(HasFatalFailure());
	const auto * waiting = initial(BattleSide::ATTACKER, SlotID(0));
	ASSERT_EQ(view->battleActiveUnit(), waiting);
	const auto round = view->battleGetRound();
	submitActive(true); ASSERT_FALSE(HasFatalFailure());
	const auto firstPhase = HoMM3Forecast::captureInitialTroops(*view, history);
	EXPECT_FALSE(firstPhase.secondPhase);
	EXPECT_TRUE(dto(firstPhase, waiting).order.waiting);
	for(int step = 0; step < 3; ++step)
	{
		ASSERT_NE(view->battleActiveUnit(), waiting);
		submitActive(false); ASSERT_FALSE(HasFatalFailure());
	}
	ASSERT_EQ(view->battleActiveUnit(), waiting);
	ASSERT_EQ(view->battleGetRound(), round);
	ASSERT_TRUE(waiting->waited());
	const auto secondPhase = HoMM3Forecast::captureInitialTroops(*view, history);
	EXPECT_TRUE(secondPhase.secondPhase);
	EXPECT_FALSE(dto(secondPhase, waiting).order.waiting);
	const auto * defended = initial(BattleSide::ATTACKER, SlotID(5));
	ASSERT_TRUE(defended->defended());
	EXPECT_TRUE(dto(secondPhase, defended).order.done);
	const auto trace = HoMM3PrefixReference::projectedTrace(secondPhase, HoMM3Forecast::Side::ATTACKER, {});
	EXPECT_EQ(trace.opportunities, (std::vector<HoMM3Forecast::UnitId>{waiting->unitId()}));
	HoMM3Forecast::OwnershipPrefix prefix(secondPhase, HoMM3Forecast::Side::ATTACKER);
	EXPECT_EQ(prefix.moveOrder().units.size(), secondPhase.units.size());
	EXPECT_NE(std::find_if(prefix.moveOrder().units.begin(), prefix.moveOrder().units.end(),
		[defended](const auto & entry) { return entry.id == defended->unitId(); }), prefix.moveOrder().units.end());
	expectPair(firstPhase); expectPair(secondPhase);
}

TEST_F(HoMM3ForecastQueueNative, hypnotizeChangesControllerWithoutChangingOwningSlot)
{
	prepare(); ASSERT_FALSE(HasFatalFailure());
	const auto * controlled = initial(BattleSide::DEFENDER, SlotID(0));
	ASSERT_NE(controlled, nullptr);
	teach(attackerSideHero, SpellID::HYPNOTIZE, 20);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::HYPNOTIZE, controlled));
	ASSERT_TRUE(controlled->isHypnotized());
	const auto input = HoMM3Forecast::captureInitialTroops(*view, history);
	EXPECT_EQ(dto(input, controlled).order.owningSide, HoMM3Forecast::Side::DEFENDER);
	EXPECT_EQ(dto(input, controlled).controllingSide, HoMM3Forecast::Side::ATTACKER);
	EXPECT_EQ(dto(input, controlled).order.sourceIndex, 0);
	const auto trace = HoMM3PrefixReference::projectedTrace(input, HoMM3Forecast::Side::ATTACKER, {});
	EXPECT_EQ(trace.opportunities, (std::vector<HoMM3Forecast::UnitId>{initial(BattleSide::ATTACKER, SlotID(0))->unitId(),
		controlled->unitId(), initial(BattleSide::ATTACKER, SlotID(5))->unitId()}));
	expectPair(input);
}

TEST_F(HoMM3ForecastQueueNative, berserkOpponentIsSkippedBeforeOwnershipBoundary)
{
	prepare(); ASSERT_FALSE(HasFatalFailure());
	const auto * skipped = initial(BattleSide::DEFENDER, SlotID(0));
	teach(attackerSideHero, SpellID::BERSERK, 3);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::BERSERK, skipped));
	ASSERT_TRUE(skipped->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
	const auto input = HoMM3Forecast::captureInitialTroops(*view, history);
	EXPECT_TRUE(dto(input, skipped).berserk);
	const auto trace = HoMM3PrefixReference::projectedTrace(input, HoMM3Forecast::Side::ATTACKER, {});
	EXPECT_EQ(trace.opportunities, (std::vector<HoMM3Forecast::UnitId>{initial(BattleSide::ATTACKER, SlotID(0))->unitId(),
		initial(BattleSide::ATTACKER, SlotID(5))->unitId()}));
	expectPair(input);
}

TEST_F(HoMM3ForecastQueueNative, blindDurationUsesActiveStatusAndIsNotDeath)
{
	prepare(); ASSERT_FALSE(HasFatalFailure());
	const auto * target = initial(BattleSide::DEFENDER, SlotID(0));
	teach(attackerSideHero, SpellID::BLIND, 2);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::BLIND, target));
	const auto input = HoMM3Forecast::captureInitialTroops(*view, history);
	EXPECT_EQ(dto(input, target).order.sourceSpell62Turns, 2);
	EXPECT_FALSE(dto(input, target).order.immobilized);
	EXPECT_TRUE(target->alive());
	HoMM3Forecast::MoveOrderInput order;
	order.actingSide = input.actingSide;
	for(const auto & unit : input.units) order.units.push_back(unit.order);
	const auto candidates = HoMM3Forecast::makeCandidates(order);
	const auto found = std::find_if(candidates.begin(), candidates.end(), [target](const auto & entry) { return entry.id == target->unitId(); });
	ASSERT_NE(found, candidates.end());
	EXPECT_EQ(found->key, -10000);
	expectPair(input);
}

TEST_F(HoMM3ForecastQueueNative, retainedCorpseDoesNotRenumberLaterSourceSlot)
{
	prepare(true); ASSERT_FALSE(HasFatalFailure());
	const auto * casualty = initial(BattleSide::ATTACKER, SlotID(3));
	const auto * later = initial(BattleSide::ATTACKER, SlotID(5));
	ASSERT_NE(casualty, nullptr); ASSERT_NE(later, nullptr);
	const auto before = HoMM3Forecast::captureInitialTroops(*view, history);
	ASSERT_EQ(dto(before, later).order.sourceIndex, 2);
	injure(casualty, casualty->getAvailableHealth());
	ASSERT_FALSE(casualty->alive());
	const auto after = HoMM3Forecast::captureInitialTroops(*view, history);
	EXPECT_TRUE(dto(after, casualty).order.immobilized);
	EXPECT_EQ(dto(after, later).order.sourceIndex, 2);
	HoMM3Forecast::OwnershipPrefix prefix(after, HoMM3Forecast::Side::ATTACKER);
	EXPECT_EQ(prefix.moveOrder().units.size(), after.units.size() - 1);
	expectPair(after);
}

TEST_F(HoMM3ForecastQueueNative, projectedCasualtyRemovesBoundaryWithoutMutatingNativeHealth)
{
	prepare(); ASSERT_FALSE(HasFatalFailure());
	const auto * attacker = initial(BattleSide::ATTACKER, SlotID(0));
	const auto * target = initial(BattleSide::DEFENDER, SlotID(0));
	const auto health = target->getAvailableHealth();
	const auto input = HoMM3Forecast::captureInitialTroops(*view, history);
	const std::vector<HoMM3PrefixReference::ScriptedDamage> projectedDamage{{attacker->unitId(), target->unitId(), static_cast<std::int32_t>(health)}};
	const auto trace = HoMM3PrefixReference::projectedTrace(input, HoMM3Forecast::Side::ATTACKER, projectedDamage);
	EXPECT_EQ(trace.opportunities, (std::vector<HoMM3Forecast::UnitId>{attacker->unitId(), initial(BattleSide::ATTACKER, SlotID(5))->unitId()}));
	EXPECT_EQ(trace.projectedHealth.at(target->unitId()), 0);
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_TRUE(target->alive());
	expectPair(input, projectedDamage);
}

TEST_F(HoMM3ForecastQueueNative, missingPhaseHistoryAndCreatedUnitsAreExplicitlyUnsupported)
{
	prepare(); ASSERT_FALSE(HasFatalFailure());
	HoMM3Forecast::PhaseHistory missing;
	EXPECT_THROW(HoMM3Forecast::captureInitialTroops(*view, missing), HoMM3Forecast::UnsupportedSnapshot);
	const auto * created = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 1);
	ASSERT_NE(created, nullptr);
	ASSERT_FALSE(created->unitSlot().validSlot());
	EXPECT_THROW(HoMM3Forecast::captureInitialTroops(*view, history), HoMM3Forecast::UnsupportedSnapshot);
}
