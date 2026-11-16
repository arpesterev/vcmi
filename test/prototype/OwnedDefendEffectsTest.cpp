#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "AI/BattleAI/StackWithBonuses.h"
#include "lib/battle/actions/NoTargetActions.h"
#include "lib/bonuses/BonusList.h"
#include "lib/bonuses/OwnedBonusLogicalDomain.h"
#include "lib/bonuses/BonusSelector.h"
#include "lib/json/JsonNode.h"

class OwnedDefendEffectsTest : public BattleTestFixture
{
public:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		startGame();
		startBattle();
	}

	static CSelector defenseSelector()
	{
		return Selector::typeSubtype(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::DEFENSE));
	}

	static int defense(const IBonusBearer & unit)
	{
		return unit.valOfBonuses(defenseSelector());
	}

	CStack * prepare(int value, int stance = 0)
	{
		auto * stack = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
		beginCombat();
		stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::PRIMARY_SKILL,
			BonusSource::OTHER, value - defense(*stack), BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE), BonusValueType::ADDITIVE_VALUE));
		if(stance)
			stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::DEFENSIVE_STANCE,
				BonusSource::OTHER, stance, BonusSourceID()));
		return stack;
	}

	static std::vector<JsonNode> snapshot(const IBonusBearer & unit)
	{
		std::vector<JsonNode> result;
		const auto bonuses = unit.getAllBonuses(Selector::all);
		for(const auto & bonus : *bonuses)
			result.push_back(bonus->toJsonNode());
		return result;
	}

	static void sameBonuses(const IBonusBearer & owned, const IBonusBearer & native)
	{
		const auto actual = owned.getAllBonuses(Selector::all);
		const auto expected = native.getAllBonuses(Selector::all);
		ASSERT_EQ(actual->size(), expected->size());
		for(size_t i = 0; i < actual->size(); ++i)
		{
			EXPECT_EQ((*actual)[i]->toJsonNode(), (*expected)[i]->toJsonNode());
			EXPECT_EQ((*actual)[i]->bonusOwner, (*expected)[i]->bonusOwner);
			EXPECT_NE((*actual)[i].get(), (*expected)[i].get());
		}
	}

	void compareNative(int initial, int stance, int expectedIncrease, BonusValueType expectedType)
	{
		auto * stack = prepare(initial, stance);
		ASSERT_EQ(defense(*stack), initial);
		const auto nativeBefore = snapshot(*stack);
		auto parent = HypotheticBattle::captureOwnedFixture(*battle());
		auto child = parent->forkOwnedFixture();
		auto sibling = parent->forkOwnedFixture();
		auto owned = child->getForUpdate(stack->unitId());
		const auto oldTurnStatus = owned->getTurnStatus();

		const auto produced = makeDefendBonusEffects(*owned);
		ASSERT_EQ(produced.bonuses.size(), 3);
		EXPECT_EQ(produced.defenseIncrease, expectedIncrease);
		EXPECT_EQ(produced.bonuses[0].valType, expectedType);
		EXPECT_EQ(produced.bonuses[1].val, stance);
		EXPECT_EQ(produced.bonuses[2].type, BonusType::UNIT_DEFENDING);
		for(const auto & bonus : produced.bonuses)
		{
			EXPECT_EQ(bonus.duration, BonusDuration::STACK_GETS_TURN);
			EXPECT_EQ(bonus.source, BonusSource::OTHER);
			EXPECT_FALSE(bonus.limiter);
			EXPECT_FALSE(bonus.updater);
			EXPECT_FALSE(bonus.propagator);
			EXPECT_FALSE(bonus.propagationUpdater);
			EXPECT_FALSE(bonus.parameters);
		}

		EXPECT_EQ(child->applyOwnedDefendBonuses(stack->unitId()), expectedIncrease);
		EXPECT_EQ(defense(*owned), initial + expectedIncrease);
		EXPECT_EQ(owned->getTurnStatus(), oldTurnStatus); // The owned entrypoint applies bonuses only.
		EXPECT_TRUE(owned->hasBonusOfType(BonusType::UNIT_DEFENDING));
		EXPECT_EQ(snapshot(*stack), nativeBefore);
		EXPECT_EQ(snapshot(*parent->getForUpdate(stack->unitId())), nativeBefore);
		EXPECT_EQ(snapshot(*sibling->getForUpdate(stack->unitId())), nativeBefore);

		ASSERT_TRUE(defend(stack)); // Actual native action, including production bonus generation.
		EXPECT_EQ(defense(*stack), initial + expectedIncrease);
		sameBonuses(*owned, *stack);
		EXPECT_EQ(snapshot(*parent->getForUpdate(stack->unitId())), nativeBefore);
		EXPECT_EQ(snapshot(*sibling->getForUpdate(stack->unitId())), nativeBefore);
	}
};

TEST_F(OwnedDefendEffectsTest, LowDefenseUsesAdditiveFallback)
{
	compareNative(2, 0, 1, BonusValueType::ADDITIVE_VALUE);
}

TEST_F(OwnedDefendEffectsTest, NormalDefenseUsesTwentyPercent)
{
	compareNative(10, 0, 2, BonusValueType::PERCENT_TO_ALL);
}

TEST_F(OwnedDefendEffectsTest, DefensiveStanceUsesProductionArithmetic)
{
	compareNative(10, 3, 5, BonusValueType::PERCENT_TO_ALL);
}

TEST_F(OwnedDefendEffectsTest, StancePreventsLowDefenseFallback)
{
	compareNative(2, 3, 4, BonusValueType::PERCENT_TO_ALL);
}

TEST_F(OwnedDefendEffectsTest, ProducerReadsForkStateAndDoesNotMutateSource)
{
	auto * stack = prepare(10);
	const auto original = snapshot(*stack);
	auto parent = HypotheticBattle::captureOwnedFixture(*battle());
	auto child = parent->forkOwnedFixture();
	Bonus stance(BonusDuration::PERMANENT, BonusType::DEFENSIVE_STANCE, BonusSource::OTHER, 4, BonusSourceID());
	child->addUnitBonus(stack->unitId(), child->makeOwnedBonusEvent({stance}));
	EXPECT_EQ(child->applyOwnedDefendBonuses(stack->unitId()), 6);
	EXPECT_EQ(defense(*child->getForUpdate(stack->unitId())), 16);
	EXPECT_EQ(snapshot(*stack), original);
	EXPECT_EQ(snapshot(*parent->getForUpdate(stack->unitId())), original);
	parent.reset();
	EXPECT_EQ(defense(*child->getForUpdate(stack->unitId())), 16);
}
