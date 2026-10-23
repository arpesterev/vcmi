/*
 * SpellTargetsEvaluatorTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../mock/BattleFake.h"
#include "../mock/mock_battle_Unit.h"
#include "../mock/mock_spells_Mechanics.h"
#include "AI/BattleAI/SpellTargetsEvaluator.h"
#include "BattleCallerFixture.h"
#include "lib/battle/CObstacleInstance.h"
#include "lib/spells/CSpellHandler.h"
#include "lib/CStack.h"
#include "lib/battle/CBattleInfoCallback.h"

namespace test
{
using namespace ::testing;
using namespace spells;
using namespace battle;
using PossiblePositions = std::vector<BattleHex>;

class CBattleInfoCallbackMock : public CBattleInfoCallback
{
public:
	MOCK_CONST_METHOD1(battleGetAllUnits, battle::Units(bool));
	MOCK_CONST_METHOD0(getBattle, IBattleInfo *());
	MOCK_CONST_METHOD0(getPlayerID, std::optional<PlayerColor>());
#if SCRIPTING_ENABLED
	MOCK_CONST_METHOD0(getContextPool, scripting::Pool *());
#endif
};

class CStackMock : public CStack
{
public:
	MOCK_CONST_METHOD0(unitSide, BattleSide());
};

class SpellTargetEvaluatorTest : public ::testing::Test
{
public:
	MechanicsMock mechMock;
	CBattleInfoCallbackMock battleMock;
	battle::Units allUnits;
	TStacks allStacks;
	BattleSide casterSide = BattleSide::ATTACKER;
	BattleSide enemySide = BattleSide::DEFENDER;

	void SetUp() override
	{
		mechMock.casterSide = casterSide;
		ON_CALL(mechMock, battle()).WillByDefault(Return(&battleMock));
		ON_CALL(mechMock, canBeCastAt(_, _)).WillByDefault(Return(true));
	}

	void TearDown() override
	{
		for(const auto * unit : allUnits)
			delete unit;
		allUnits.clear();
		for(const auto * stack : allStacks)
			delete stack;
		allStacks.clear();
	}

	void spellTargetTypes(std::vector<AimType> targetTypes)
	{
		ON_CALL(mechMock, getTargetTypes()).WillByDefault(Return(std::move(targetTypes)));
	}

	CStackMock * addStack(BattleHex position, BattleSide battleSide, bool isSuspectible = true)
	{
		auto * stack = new CStackMock();
		ON_CALL(*stack, unitSide()).WillByDefault(Return(battleSide));
		allStacks.push_back(stack);
		auto * unit = new UnitMock();
		ON_CALL(*unit, getPosition()).WillByDefault(Return(position));
		allUnits.push_back(unit);

		if(!isSuspectible)
			ON_CALL(mechMock, canBeCastAt(Contains(Field(&Destination::hexValue, position)), _)).WillByDefault(Return(false));
		return stack;
	}

	void setAffectedStacksForCast(BattleHex position, std::vector<const CStack *> stacks)
	{
		ON_CALL(mechMock, getAffectedStacks(Contains(Field(&Destination::hexValue, position)))).WillByDefault(Return(stacks));
	}

	void confirmResults(std::vector<PossiblePositions> allRequiredCasts)
	{
		std::vector<Target> result = SpellTargetEvaluator::getViableTargets(&mechMock);
		basicCheck(result);
		ASSERT_EQ(result.size(), allRequiredCasts.size());

		std::vector<BattleHex> targetedHexes;
		targetedHexes.reserve(result.size());
		for(Target target : result)
			targetedHexes.push_back(target.front().hexValue);

		for(const PossiblePositions & requiredCast : allRequiredCasts)
			EXPECT_TRUE(containCommonValue(requiredCast, targetedHexes));
	}

	void basicCheck(std::vector<Target> & result)
	{
		for(const Target & target : result)
			EXPECT_EQ(target.size(), 1); //multi-destination spells are not handled by targetEvaluator
	}

	template<typename T>
	bool containCommonValue(const std::vector<T> & v1, const std::vector<T> & v2)
	{
		for(T val1 : v1)
		{
			for(T val2 : v2)
			{
				if(val1 == val2)
					return true;
			}
		}
		return false;
	}
};

TEST_F(SpellTargetEvaluatorTest, EnumeratesAndValidatesCompleteCreatureLocationTargets)
{
	spellTargetTypes({AimType::CREATURE, AimType::LOCATION});
	addStack(BattleHex(20), casterSide);
	addStack(BattleHex(40), casterSide);
	ON_CALL(battleMock, battleGetAllUnits(false)).WillByDefault(Return(allUnits));
	EXPECT_CALL(mechMock, canBeCastAt(_, _))
		.Times(2 * GameConstants::BFIELD_SIZE)
		.WillRepeatedly(Invoke([](const Target & target, Problem &)
		{
			EXPECT_EQ(target.size(), 2u); // Never validate incomplete prefixes.
			return target.size() == 2 &&
				((target[0].hexValue == BattleHex(20) && target[1].hexValue == BattleHex(80)) ||
				 (target[0].hexValue == BattleHex(40) && target[1].hexValue == BattleHex(90)));
		}));
	const auto result = SpellTargetEvaluator::getViableTargets(&mechMock);
	ASSERT_EQ(result.size(), 2u);
	EXPECT_EQ(result[0][0].hexValue, BattleHex(20));
	EXPECT_EQ(result[0][1].hexValue, BattleHex(80));
	EXPECT_EQ(result[1][0].hexValue, BattleHex(40));
	EXPECT_EQ(result[1][1].hexValue, BattleHex(90));
}

TEST_F(SpellTargetEvaluatorTest, ObstacleTargetsUseMechanicsLegality)
{
	spellTargetTypes({AimType::OBSTACLE});
	EXPECT_CALL(mechMock, canBeCastAt(_, _))
		.Times(GameConstants::BFIELD_SIZE)
		.WillRepeatedly(Invoke([](const Target & target, Problem &)
		{
			return target.size() == 1 && target[0].hexValue == BattleHex(80);
		}));
	confirmResults({{BattleHex(80)}});
}

TEST_F(SpellTargetEvaluatorTest, EmptySignatureAndUnavailableFirstTargetProduceNoCasts)
{
	spellTargetTypes({});
	EXPECT_TRUE(SpellTargetEvaluator::getViableTargets(&mechMock).empty());
	spellTargetTypes({AimType::CREATURE, AimType::LOCATION});
	ON_CALL(battleMock, battleGetAllUnits(false)).WillByDefault(Return(allUnits));
	EXPECT_CALL(mechMock, canBeCastAt(_, _)).Times(0);
	EXPECT_TRUE(SpellTargetEvaluator::getViableTargets(&mechMock).empty());
}

TEST_F(SpellTargetEvaluatorTest, ReturnSingleEmptyDestinationIfTargetIsNone)
{
	spellTargetTypes({AimType::NO_TARGET});
	std::vector<Target> result = SpellTargetEvaluator::getViableTargets(&mechMock);
	EXPECT_EQ(result.size(), 1);
	EXPECT_TRUE(result.front().empty());
}

TEST_F(SpellTargetEvaluatorTest, ReturnsSuspectibleCreaturePositionsIfSpellTargetsCreatures)
{
	spellTargetTypes({AimType::CREATURE});

	addStack(BattleHex(1), casterSide);
	addStack(BattleHex(2), enemySide, false);
	addStack(BattleHex(3), casterSide);
	ON_CALL(battleMock, battleGetAllUnits(Eq(false))).WillByDefault(Return(allUnits));

	confirmResults({{BattleHex(1)}, {BattleHex(3)}});
}

TEST_F(SpellTargetEvaluatorTest, ReturnsSuspectibleCreaturePositionsAndSingleRandomSurroundingHexForEachStackIfNeutralLocationSpell)
{
	spellTargetTypes({AimType::LOCATION});
	ON_CALL(mechMock, isNeutralSpell()).WillByDefault(Return(true));

	addStack(BattleHex(72), casterSide);
	addStack(BattleHex(159), enemySide, false);
	addStack(BattleHex(23), casterSide);
	ON_CALL(battleMock, battleGetAllUnits(Eq(false))).WillByDefault(Return(allUnits));

	confirmResults(
		{{BattleHex(72)},
		 BattleHex(72).getAllNeighbouringTiles().toVector(),
		 BattleHex(159).getAllNeighbouringTiles().toVector(),
		 {BattleHex(23)},
		 BattleHex(23).getAllNeighbouringTiles().toVector()}
	);
}

TEST_F(SpellTargetEvaluatorTest, ReturnsOneCaseOfEachOptimalCastIfNegativeLocationSpell)
{
	spellTargetTypes({AimType::LOCATION});
	ON_CALL(mechMock, isNegativeSpell()).WillByDefault(Return(true));

	auto * enemyStack1 = addStack(BattleHex(90), enemySide);
	auto * enemyStack2 = addStack(BattleHex(106), enemySide);
	auto * enemyStack3 = addStack(BattleHex(1), enemySide);
	auto * enemyStack4 = addStack(BattleHex(37), enemySide);
	auto * enemyStack5 = addStack(BattleHex(41), enemySide);

	auto * alliedStack1 = addStack(BattleHex(107), casterSide);
	auto * alliedStack2 = addStack(BattleHex(19), casterSide);

	//optimal
	setAffectedStacksForCast(BattleHex(71), {enemyStack1, enemyStack2});
	setAffectedStacksForCast(BattleHex(88), {enemyStack1, enemyStack2});
	//suboptimal
	setAffectedStacksForCast(BattleHex(55), {enemyStack1});
	setAffectedStacksForCast(BattleHex(89), {enemyStack1, enemyStack2, alliedStack1});

	//optimal
	setAffectedStacksForCast(BattleHex(18), {enemyStack3, alliedStack2});
	setAffectedStacksForCast(BattleHex(2), {enemyStack3, alliedStack2});
	//suboptimal
	setAffectedStacksForCast(BattleHex(53), {alliedStack2});

	//optimal
	setAffectedStacksForCast(BattleHex(39), {enemyStack4, enemyStack5});
	//suboptimal
	setAffectedStacksForCast(BattleHex(21), {enemyStack4});
	setAffectedStacksForCast(BattleHex(25), {enemyStack5});

	confirmResults({
		{BattleHex(71), BattleHex(88)},
        {BattleHex(2), BattleHex(18)},
        {BattleHex(39)}
	});
}

TEST(SpellTargetRealMechanicsTest, RemoveObstacleFindsRemoteTargetsAndKeepsParentsUnchanged)
{
	callerprobe::Scenario scene(callerprobe::benchmarkSpecs(6, true));
	IBattleInfo::ObstacleCList obstacles;
	std::vector<JsonNode> obstacleSnapshots;
	for(int index = 0; index < 3; ++index)
	{
		auto obstacle = std::make_shared<SpellCreatedObstacle>();
		obstacle->uniqueID = 100 + index;
		obstacle->ID = SpellID::FORCE_FIELD;
		obstacle->pos = BattleHex(8, 3 + index);
		obstacle->customSize.insert(obstacle->pos);
		obstacle->casterSide = BattleSide::DEFENDER;
		obstacle->trigger = SpellID::NONE;
		ObstacleChanges snapshot;
		obstacle->toInfo(snapshot);
		obstacleSnapshots.push_back(snapshot.data);
		obstacles.push_back(obstacle);
	}
	// Override setupEmptyBattlefield's explicit empty-obstacle expectation.
	EXPECT_CALL(*scene.callback, getAllObstacles()).WillRepeatedly(Return(obstacles));
	ASSERT_EQ(scene.callback->battleGetAllObstacles(), obstacles);
	ASSERT_EQ(scene.state->battleGetAllObstacles(), obstacles);
	scene.populateCache();
	const auto unitsBefore = scene.snapshot();
	const auto * spell = SpellID(SpellID::REMOVE_OBSTACLE).toSpell();
	ASSERT_NE(spell, nullptr);
	ASSERT_EQ(spell->getTargetType(), AimType::OBSTACLE);
	auto caster = scene.state->getForUpdate(1);
	BattleCast cast(scene.state.get(), caster.get(), Mode::PASSIVE, spell);
	cast.setSpellLevel(3);
	cast.setEffectPower(10);
	cast.setEffectDuration(3);
	auto mechanics = spell->battleMechanics(&cast);
	// Exercise the real OBSTACLE -> LOCATION conversion, not a mocked signature.
	ASSERT_EQ(mechanics->getTargetTypes(), std::vector<AimType>({AimType::LOCATION}));
	const auto targets = SpellTargetEvaluator::getViableTargets(mechanics.get());
	ASSERT_EQ(targets.size(), obstacles.size());
	std::set<BattleHex> actual;
	for(const auto & target : targets)
	{
		ASSERT_EQ(target.size(), 1u);
		EXPECT_TRUE(mechanics->canBeCastAt(target));
		actual.insert(target.front().hexValue);
		auto child = std::make_shared<HypotheticBattle>(scene.environment.get(), scene.state);
		ASSERT_EQ(child->battleGetAllObstacles(), obstacles);
		auto childCaster = child->getForUpdate(1);
		BattleCast childCast(child.get(), childCaster.get(), Mode::PASSIVE, spell);
		childCast.setSpellLevel(3);
		childCast.setEffectPower(10);
		childCast.setEffectDuration(3);
		childCast.castEval(child->getServerCallback(), target);
		const auto remaining = child->battleGetAllObstacles();
		ASSERT_EQ(remaining.size(), obstacles.size() - 1);
		for(const auto & obstacle : remaining)
			EXPECT_NE(obstacle->pos, target.front().hexValue);
		EXPECT_EQ(scene.state->battleGetAllObstacles(), obstacles);
		EXPECT_EQ(scene.callback->battleGetAllObstacles(), obstacles);
	}
	const std::set<BattleHex> expected{BattleHex(8, 3), BattleHex(8, 4), BattleHex(8, 5)};
	EXPECT_EQ(actual, expected);
	EXPECT_EQ(scene.snapshot(), unitsBefore);
	for(size_t index = 0; index < obstacles.size(); ++index)
	{
		ObstacleChanges snapshot;
		const_cast<CObstacleInstance *>(obstacles[index].get())->toInfo(snapshot);
		EXPECT_EQ(snapshot.data, obstacleSnapshots[index]);
	}
}

TEST(SpellTargetRealMechanicsTest, RemoveObstacleWithoutLegalObstacleHasNoTargets)
{
	callerprobe::Scenario scene(callerprobe::benchmarkSpecs(6, true));
	ASSERT_TRUE(scene.state->battleGetAllObstacles().empty());
	const auto * spell = SpellID(SpellID::REMOVE_OBSTACLE).toSpell();
	ASSERT_NE(spell, nullptr);
	auto caster = scene.state->getForUpdate(1);
	BattleCast cast(scene.state.get(), caster.get(), Mode::PASSIVE, spell);
	cast.setSpellLevel(3);
	auto mechanics = spell->battleMechanics(&cast);
	EXPECT_TRUE(mechanics->getTargetTypes().empty());
	EXPECT_TRUE(SpellTargetEvaluator::getViableTargets(mechanics.get()).empty());

	// Force Field cannot be removed at base/basic/advanced skill. Exhaustive
	// obstacle locations must not bypass real mechanics' availability check.
	auto obstacle = std::make_shared<SpellCreatedObstacle>();
	obstacle->uniqueID = 100;
	obstacle->ID = SpellID::FORCE_FIELD;
	obstacle->pos = BattleHex(8, 5);
	obstacle->customSize.insert(obstacle->pos);
	obstacle->casterSide = BattleSide::DEFENDER;
	obstacle->trigger = SpellID::NONE;
	IBattleInfo::ObstacleCList obstacles{obstacle};
	EXPECT_CALL(*scene.callback, getAllObstacles()).WillRepeatedly(Return(obstacles));
	ASSERT_EQ(scene.state->battleGetAllObstacles(), obstacles);
	for(int level : {0, 1, 2})
	{
		cast.setSpellLevel(level);
		auto unavailable = spell->battleMechanics(&cast);
		EXPECT_TRUE(unavailable->getTargetTypes().empty());
		EXPECT_TRUE(SpellTargetEvaluator::getViableTargets(unavailable.get()).empty());
	}
}

TEST(SpellTargetRealMechanicsTest, OrdinaryLocationSpellsKeepNearUnitHeuristic)
{
	callerprobe::Scenario scene(callerprobe::benchmarkSpecs(6, true));
	const auto units = scene.state->battleAliveUnits();
	std::set<BattleHex> nearUnits;
	for(const auto * unit : units)
	{
		nearUnits.insert(unit->getPosition());
		for(const auto & hex : unit->getSurroundingHexes())
			nearUnits.insert(hex);
	}
	for(const auto spellId : {SpellID::FORCE_FIELD, SpellID::FIRE_WALL})
	{
		const auto * spell = SpellID(spellId).toSpell();
		ASSERT_NE(spell, nullptr);
		ASSERT_EQ(spell->getTargetType(), AimType::LOCATION);
		auto caster = scene.state->getForUpdate(1);
		BattleCast cast(scene.state.get(), caster.get(), Mode::PASSIVE, spell);
		cast.setSpellLevel(3);
		cast.setEffectPower(10);
		cast.setEffectDuration(3);
		auto mechanics = spell->battleMechanics(&cast);
		ASSERT_EQ(mechanics->getTargetTypes(), std::vector<AimType>({AimType::LOCATION}));
		ASSERT_TRUE(mechanics->isNeutralSpell());
		const auto targets = SpellTargetEvaluator::getViableTargets(mechanics.get());
		EXPECT_LE(targets.size(), 2 * units.size());
		for(const auto & target : targets)
		{
			ASSERT_EQ(target.size(), 1u);
			EXPECT_EQ(nearUnits.count(target.front().hexValue), 1u);
			EXPECT_TRUE(mechanics->canBeCastAt(target));
		}
		size_t fullBoardLegalCount = 0;
		for(int hex = 0; hex < GameConstants::BFIELD_SIZE; ++hex)
			fullBoardLegalCount += mechanics->canBeCastAt(Target{Destination(BattleHex(hex))});
		EXPECT_GT(fullBoardLegalCount, 2 * units.size());
	}
}

}
