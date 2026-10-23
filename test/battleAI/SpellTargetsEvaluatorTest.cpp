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
#include <algorithm>
#include <set>
#include <utility>

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

TEST_F(SpellTargetEvaluatorTest, NonTeleportCreatureLocationSignatureRemainsExhaustive)
{
	spellTargetTypes({AimType::CREATURE, AimType::LOCATION});
	ON_CALL(mechMock, getSpellId()).WillByDefault(Return(SpellID(SpellID::SACRIFICE)));
	addStack(BattleHex(20), casterSide);
	addStack(BattleHex(40), casterSide);
	ON_CALL(battleMock, battleGetAllUnits(false)).WillByDefault(Return(allUnits));
	EXPECT_CALL(mechMock, canBeCastAt(_, _))
		.Times(2 * GameConstants::BFIELD_SIZE)
		.WillRepeatedly(Return(true));
	const auto targets = SpellTargetEvaluator::getViableTargets(&mechMock);
	ASSERT_EQ(targets.size(), 2u * GameConstants::BFIELD_SIZE);
	std::set<std::pair<int, int>> pairs;
	for(const auto & target : targets)
	{
		ASSERT_EQ(target.size(), 2u);
		pairs.emplace(target.front().hexValue.toInt(), target.back().hexValue.toInt());
	}
	EXPECT_EQ(pairs.size(), targets.size());
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

namespace teleportprobe
{
using Key = std::pair<uint32_t, int>;

void configure(BattleCast & cast)
{
	cast.setSpellLevel(3);
	cast.setEffectPower(10);
	cast.setEffectDuration(3);
}

void primeSnapshots(callerprobe::Scenario & scene)
{
	// Serialization includes the lazy retaliation-capacity cache. Prime it on
	// both borrowed sources and hypothetical wrappers, without spending a turn.
	for(const auto & source : scene.sources)
	{
		source->state.counterAttacks.total();
		scene.state->getForUpdate(source->state.unitId())->counterAttacks.total();
	}
}

std::vector<Key> keys(const CBattleInfoCallback & battle, const std::vector<Target> & targets)
{
	std::vector<Key> result;
	for(const auto & target : targets)
	{
		EXPECT_EQ(target.size(), 2u);
		if(target.size() != 2)
			return {};
		const auto * source = battle.battleGetUnitByPos(target.front().hexValue);
		EXPECT_NE(source, nullptr);
		if(!source)
			return {};
		result.emplace_back(source->unitId(), target.back().hexValue.toInt());
	}
	return result;
}

void expectLegalUniqueMoves(const Mechanics & mechanics, const std::vector<Target> & targets)
{
	EXPECT_LE(targets.size(), 8u);
	std::set<Key> seen;
	for(const auto & target : targets)
	{
		ASSERT_EQ(target.size(), 2u);
		const auto * source = mechanics.battle()->battleGetUnitByPos(target.front().hexValue);
		ASSERT_NE(source, nullptr);
		EXPECT_TRUE(source->alive());
		EXPECT_NE(target.back().hexValue, source->getPosition());
		EXPECT_TRUE(mechanics.canBeCastAt(target));
		EXPECT_TRUE(seen.emplace(source->unitId(), target.back().hexValue.toInt()).second);
	}
}

size_t legalMoveCount(const Mechanics & mechanics)
{
	size_t count = 0;
	for(const auto * source : mechanics.battle()->battleGetAllUnits(false))
		for(int hex = 0; hex < GameConstants::BFIELD_SIZE; ++hex)
			if(BattleHex(hex) != source->getPosition()
				&& mechanics.canBeCastAt(Target{Destination(source->getPosition()), Destination(BattleHex(hex))}))
				++count;
	return count;
}

std::shared_ptr<HypotheticBattle> castInChild(callerprobe::Scenario & scene,
	const Target & target, uint32_t casterId, bool disableSmart = false)
{
	auto child = std::make_shared<HypotheticBattle>(scene.environment.get(), scene.state);
	auto caster = child->getForUpdate(casterId);
	const auto * spell = SpellID(SpellID::TELEPORT).toSpell();
	BattleCast cast(child.get(), caster.get(), Mode::PASSIVE, spell);
	configure(cast);
	if(disableSmart)
		cast.smart = false;
	auto mechanics = spell->battleMechanics(&cast);
	EXPECT_TRUE(mechanics->canBeCastAt(target));
	cast.castEval(child->getServerCallback(), target);
	return child;
}

// Vary the public enumeration itself. Reversing Scenario::sources alone is
// insufficient: HypotheticBattle enumerates its materialized state map instead.
class EnumerationOrderBattle : public HypotheticBattle
{
public:
	using HypotheticBattle::HypotheticBattle;
	bool reversed = false;

	::battle::Units getUnitsIf(const ::battle::UnitFilter & predicate) const override
	{
		auto result = HypotheticBattle::getUnitsIf(predicate);
		if(reversed)
			std::reverse(result.begin(), result.end());
		return result;
	}
};
}

TEST(SpellTargetRealMechanicsTest, TeleportShortlistIsBoundedLegalRepeatableAndSourcePure)
{
	const auto * spell = SpellID(SpellID::TELEPORT).toSpell();
	ASSERT_NE(spell, nullptr);
	for(const int count : {6, 14, 28})
	{
		SCOPED_TRACE(count);
		callerprobe::Scenario scene(callerprobe::benchmarkSpecs(count, true));
		teleportprobe::primeSnapshots(scene);
		const auto before = scene.snapshot();
		const auto activeBefore = scene.state->getActiveStackID();
		// PASSIVE unit casters cannot target themselves. Keep active unit 1
		// teleportable by casting from a different friendly unit.
		auto caster = scene.state->getForUpdate(2);
		BattleCast cast(scene.state.get(), caster.get(), Mode::PASSIVE, spell);
		teleportprobe::configure(cast);
		auto mechanics = spell->battleMechanics(&cast);
		ASSERT_EQ(mechanics->getTargetTypes(), std::vector<AimType>({AimType::CREATURE, AimType::LOCATION}));
		ASSERT_GT(teleportprobe::legalMoveCount(*mechanics), 8u);
		const auto targets = SpellTargetEvaluator::getViableTargets(mechanics.get());
		ASSERT_FALSE(targets.empty());
		teleportprobe::expectLegalUniqueMoves(*mechanics, targets);
		const auto repeated = SpellTargetEvaluator::getViableTargets(mechanics.get());
		EXPECT_EQ(teleportprobe::keys(*scene.state, targets), teleportprobe::keys(*scene.state, repeated));
		for(const auto & target : targets)
		{
			const auto * source = scene.state->battleGetUnitByPos(target.front().hexValue);
			ASSERT_NE(source, nullptr);
			const auto child = teleportprobe::castInChild(scene, target, 2);
			const auto * moved = child->battleGetUnitByID(source->unitId());
			ASSERT_NE(moved, nullptr);
			EXPECT_EQ(moved->getPosition(), target.back().hexValue);
			EXPECT_EQ(scene.snapshot(), before);
			EXPECT_EQ(scene.state->getActiveStackID(), activeBefore);
		}
		EXPECT_EQ(scene.snapshot(), before);
	}
}

TEST(SpellTargetRealMechanicsTest, TeleportShortlistIgnoresSourceEnumerationOrder)
{
	callerprobe::Scenario scene(callerprobe::benchmarkSpecs(14, true));
	auto ordered = std::make_shared<teleportprobe::EnumerationOrderBattle>(scene.environment.get(), scene.callback);
	scene.state = ordered;
	teleportprobe::primeSnapshots(scene);
	const auto before = scene.snapshot();
	const auto * spell = SpellID(SpellID::TELEPORT).toSpell();
	ASSERT_NE(spell, nullptr);
	auto caster = ordered->getForUpdate(2);
	BattleCast cast(ordered.get(), caster.get(), Mode::PASSIVE, spell);
	teleportprobe::configure(cast);
	auto mechanics = spell->battleMechanics(&cast);
	const auto forwardUnits = ordered->battleGetAllUnits(false);
	const auto forward = SpellTargetEvaluator::getViableTargets(mechanics.get());
	ordered->reversed = true;
	auto reverseUnits = ordered->battleGetAllUnits(false);
	ASSERT_NE(forwardUnits, reverseUnits) << "Fixture must actually change the enumerated order";
	std::reverse(reverseUnits.begin(), reverseUnits.end());
	ASSERT_EQ(forwardUnits, reverseUnits);
	const auto reverse = SpellTargetEvaluator::getViableTargets(mechanics.get());
	ASSERT_FALSE(forward.empty());
	teleportprobe::expectLegalUniqueMoves(*mechanics, reverse);
	EXPECT_EQ(teleportprobe::keys(*ordered, forward), teleportprobe::keys(*ordered, reverse));
	EXPECT_EQ(scene.snapshot(), before);
}

TEST(SpellTargetRealMechanicsTest, TeleportShortlistRetainsActiveMeleeContactsWithDifferentDefenders)
{
	callerprobe::Scenario scene({
		{1, BattleSide::ATTACKER, BattleHex(3, 5), 20, 100, 20, 4, false},
		{2, BattleSide::ATTACKER, BattleHex(3, 2), 10, 100, 5, 4, false},
		{3, BattleSide::ATTACKER, BattleHex(3, 8), 10, 100, 5, 4, false},
		{4, BattleSide::DEFENDER, BattleHex(11, 2), 10, 100, 10, 4, true},
		{5, BattleSide::DEFENDER, BattleHex(11, 5), 10, 100, 10, 4, false},
		{6, BattleSide::DEFENDER, BattleHex(11, 8), 10, 100, 10, 4, false}
	});
	teleportprobe::primeSnapshots(scene);
	const auto before = scene.snapshot();
	const auto * active = scene.state->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	ASSERT_EQ(active->unitId(), 1u);
	for(const auto * enemy : scene.state->battleAliveUnits(BattleSide::DEFENDER))
		ASSERT_FALSE(CStack::isMeleeAttackPossible(active, enemy));
	const auto * spell = SpellID(SpellID::TELEPORT).toSpell();
	ASSERT_NE(spell, nullptr);
	auto caster = scene.state->getForUpdate(2);
	BattleCast cast(scene.state.get(), caster.get(), Mode::PASSIVE, spell);
	teleportprobe::configure(cast);
	auto mechanics = spell->battleMechanics(&cast);
	const auto targets = SpellTargetEvaluator::getViableTargets(mechanics.get());
	teleportprobe::expectLegalUniqueMoves(*mechanics, targets);
	std::set<uint32_t> contactedDefenders;
	for(const auto & target : targets)
	{
		if(target.front().hexValue != active->getPosition())
			continue;
		const auto child = teleportprobe::castInChild(scene, target, 2);
		const auto * moved = child->battleGetUnitByID(1);
		ASSERT_NE(moved, nullptr);
		EXPECT_EQ(moved->getPosition(), target.back().hexValue);
		for(const auto * enemy : child->battleAliveUnits(BattleSide::DEFENDER))
			if(child->battleCanAttackUnit(moved, enemy) && CStack::isMeleeAttackPossible(moved, enemy))
				contactedDefenders.insert(enemy->unitId());
	}
	// The two attack slots should not both be wasted around the same defender.
	EXPECT_GE(contactedDefenders.size(), 2u);
	EXPECT_EQ(scene.snapshot(), before);
}

TEST(SpellTargetRealMechanicsTest, TeleportShortlistUnblocksFriendlyShooterAfterRealCast)
{
	callerprobe::Scenario scene({
		{1, BattleSide::ATTACKER, BattleHex(3, 2), 20, 100, 10, 4, false},
		{2, BattleSide::ATTACKER, BattleHex(5, 5), 20, 100, 10, 4, true},
		{3, BattleSide::ATTACKER, BattleHex(3, 8), 10, 100, 5, 4, false},
		{4, BattleSide::DEFENDER, BattleHex(6, 5), 10, 100, 10, 4, false},
		{5, BattleSide::DEFENDER, BattleHex(12, 2), 10, 100, 10, 4, true},
		{6, BattleSide::DEFENDER, BattleHex(12, 8), 10, 100, 10, 4, false}
	});
	teleportprobe::primeSnapshots(scene);
	const auto before = scene.snapshot();
	const auto * shooter = scene.state->battleGetUnitByID(2);
	ASSERT_NE(shooter, nullptr);
	ASSERT_TRUE(shooter->canShoot());
	ASSERT_TRUE(scene.state->battleIsUnitBlocked(shooter));
	ASSERT_FALSE(scene.state->battleCanShoot(shooter));
	const auto * spell = SpellID(SpellID::TELEPORT).toSpell();
	ASSERT_NE(spell, nullptr);
	auto caster = scene.state->getForUpdate(3);
	BattleCast cast(scene.state.get(), caster.get(), Mode::PASSIVE, spell);
	teleportprobe::configure(cast);
	auto mechanics = spell->battleMechanics(&cast);
	const auto targets = SpellTargetEvaluator::getViableTargets(mechanics.get());
	teleportprobe::expectLegalUniqueMoves(*mechanics, targets);
	size_t escapes = 0;
	size_t activeAttacks = 0;
	size_t otherFriendlyAttacks = 0;
	for(const auto & target : targets)
	{
		const auto * source = scene.state->battleGetUnitByPos(target.front().hexValue);
		ASSERT_NE(source, nullptr);
		const auto enemies = scene.state->battleAliveUnits(BattleSide::DEFENDER);
		const bool contact = std::any_of(enemies.begin(), enemies.end(), [&](const ::battle::Unit * enemy)
		{
			return scene.state->battleCanAttackUnit(source, enemy)
				&& CStack::isMeleeAttackPossible(source, enemy, target.back().hexValue);
		});
		activeAttacks += contact && source->unitId() == 1;
		otherFriendlyAttacks += contact && source->unitId() == 2;
		if(target.front().hexValue != shooter->getPosition())
			continue;
		const auto child = teleportprobe::castInChild(scene, target, 3);
		const auto * moved = child->battleGetUnitByID(2);
		ASSERT_NE(moved, nullptr);
		if(child->battleIsUnitBlocked(moved))
			continue; // Other categories may also choose attacks with this shooter.
		++escapes;
		EXPECT_EQ(moved->getPosition(), target.back().hexValue);
		EXPECT_TRUE(child->battleCanShoot(moved));
		for(const auto * enemy : child->battleAliveUnits(BattleSide::DEFENDER))
		{
			for(const auto & occupied : enemy->getHexes())
				EXPECT_FALSE(moved->getSurroundingHexes().contains(occupied));
			EXPECT_TRUE(child->battleCanShoot(moved, enemy->getPosition()));
		}
	}
	EXPECT_GE(activeAttacks, 2u);
	EXPECT_GE(otherFriendlyAttacks, 2u);
	EXPECT_GT(escapes, 0u);
	EXPECT_EQ(scene.snapshot(), before);
}

TEST(SpellTargetRealMechanicsTest, TeleportShortlistBackfillsEmptyCategoriesAndExcludesLegalNoOp)
{
	callerprobe::Scenario scene({
		{1, BattleSide::ATTACKER, BattleHex(3, 2), 10, 100, 5, 4, false},
		{2, BattleSide::ATTACKER, BattleHex(6, 5), 10, 100, 10, 4, false},
		{3, BattleSide::DEFENDER, BattleHex(7, 5), 10, 100, 10, 4, false}
	});
	const auto * spell = SpellID(SpellID::TELEPORT).toSpell();
	ASSERT_NE(spell, nullptr);
	auto caster = scene.state->getForUpdate(1);
	BattleCast cast(scene.state.get(), caster.get(), Mode::PASSIVE, spell);
	teleportprobe::configure(cast);
	auto mechanics = spell->battleMechanics(&cast);
	const auto * ally = scene.state->battleGetUnitByID(2);
	ASSERT_NE(ally, nullptr);
	// Real Teleport permits the current square. Only the shortlist removes it;
	// not casting remains the unchanged BattleEvaluator baseline, not a target.
	ASSERT_TRUE(mechanics->canBeCastAt(Target{Destination(ally->getPosition()), Destination(ally->getPosition())}));
	ASSERT_GT(teleportprobe::legalMoveCount(*mechanics), 8u);
	const auto targets = SpellTargetEvaluator::getViableTargets(mechanics.get());
	teleportprobe::expectLegalUniqueMoves(*mechanics, targets);
	// No active-unit spell target and no shooters: empty quotas must be reused.
	EXPECT_EQ(targets.size(), 8u);
	for(const auto & target : targets)
		EXPECT_EQ(target.front().hexValue, ally->getPosition());
}

TEST(SpellTargetRealMechanicsTest, TeleportWithNoLegalSourceProducesNoShortlist)
{
	callerprobe::Scenario scene({
		{1, BattleSide::ATTACKER, BattleHex(3, 5), 10, 100, 10, 4, false},
		{2, BattleSide::DEFENDER, BattleHex(11, 5), 10, 100, 10, 4, false}
	});
	const auto * spell = SpellID(SpellID::TELEPORT).toSpell();
	ASSERT_NE(spell, nullptr);
	auto caster = scene.state->getForUpdate(1);
	BattleCast cast(scene.state.get(), caster.get(), Mode::PASSIVE, spell);
	teleportprobe::configure(cast);
	auto mechanics = spell->battleMechanics(&cast);
	ASSERT_EQ(mechanics->getTargetTypes(), std::vector<AimType>({AimType::CREATURE, AimType::LOCATION}));
	// The only friendly source is the passive caster, which cannot target itself.
	ASSERT_EQ(teleportprobe::legalMoveCount(*mechanics), 0u);
	EXPECT_TRUE(SpellTargetEvaluator::getViableTargets(mechanics.get()).empty());
}

TEST(SpellTargetRealMechanicsTest, TeleportShortlistEscapesRearHexBlockingForBothDoubleWideSides)
{
	const auto * wideType = CreatureID(10).toCreature(); // Core cavalier: width only; bonuses remain fixture-owned.
	ASSERT_NE(wideType, nullptr);
	ASSERT_TRUE(wideType->isDoubleWide());
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		SCOPED_TRACE(static_cast<int>(side));
		const bool attacking = side == BattleSide::ATTACKER;
		const auto enemySide = attacking ? BattleSide::DEFENDER : BattleSide::ATTACKER;
		callerprobe::Scenario scene({
			{1, side, BattleHex(attacking ? 3 : 13, 2), 20, 100, 10, 4, false},
			{2, side, BattleHex(attacking ? 5 : 11, 5), 20, 100, 10, 4, true},
			{3, side, BattleHex(attacking ? 3 : 13, 8), 10, 100, 5, 4, false},
			{4, enemySide, BattleHex(attacking ? 3 : 13, 5), 10, 100, 10, 4, false},
			{5, enemySide, BattleHex(attacking ? 12 : 4, 2), 10, 100, 10, 4, true},
			{6, enemySide, BattleHex(attacking ? 12 : 4, 8), 10, 100, 10, 4, false}
		});
		ON_CALL(scene.sources[1]->info, unitType()).WillByDefault(Return(wideType));
		// Type is copied into StackWithBonuses; rebuild wrappers after the local
		// source override rather than changing the shared Scenario fixture.
		scene.state = std::make_shared<HypotheticBattle>(scene.environment.get(), scene.callback);
		teleportprobe::primeSnapshots(scene);
		const auto before = scene.snapshot();
		const auto * shooter = scene.state->battleGetUnitByID(2);
		const auto * blocker = scene.state->battleGetUnitByID(4);
		ASSERT_NE(shooter, nullptr);
		ASSERT_NE(blocker, nullptr);
		ASSERT_TRUE(shooter->doubleWide());
		ASSERT_FALSE(shooter->getPosition().getNeighbouringTiles().contains(blocker->getPosition()));
		ASSERT_TRUE(shooter->occupiedHex().getNeighbouringTiles().contains(blocker->getPosition()));
		ASSERT_TRUE(scene.state->battleIsUnitBlocked(shooter));
		ASSERT_FALSE(scene.state->battleCanShoot(shooter));
		const auto * spell = SpellID(SpellID::TELEPORT).toSpell();
		ASSERT_NE(spell, nullptr);
		auto caster = scene.state->getForUpdate(3);
		BattleCast cast(scene.state.get(), caster.get(), Mode::PASSIVE, spell);
		teleportprobe::configure(cast);
		auto mechanics = spell->battleMechanics(&cast);
		const auto targets = SpellTargetEvaluator::getViableTargets(mechanics.get());
		teleportprobe::expectLegalUniqueMoves(*mechanics, targets);
		size_t escapes = 0;
		for(const auto & target : targets)
		{
			if(target.front().hexValue != shooter->getPosition())
				continue;
			const auto child = teleportprobe::castInChild(scene, target, 3);
			const auto * moved = child->battleGetUnitByID(2);
			ASSERT_NE(moved, nullptr);
			ASSERT_TRUE(moved->doubleWide());
			EXPECT_EQ(moved->getPosition(), target.back().hexValue);
			EXPECT_EQ(moved->occupiedHex(), BattleHex(target.back().hexValue.toInt() + (attacking ? -1 : 1)));
			if(child->battleIsUnitBlocked(moved))
				continue;
			++escapes;
			EXPECT_TRUE(child->battleCanShoot(moved, blocker->getPosition()));
			for(const auto * enemy : child->battleAliveUnits(enemySide))
				for(const auto & occupied : enemy->getHexes())
					EXPECT_FALSE(moved->getSurroundingHexes().contains(occupied));
		}
		EXPECT_GT(escapes, 0u);
		EXPECT_EQ(scene.snapshot(), before);
	}
}

TEST(SpellTargetRealMechanicsTest, TeleportShortlistClassifiesHypnotizedActiveByEffectiveOwner)
{
	callerprobe::Scenario scene({
		{1, BattleSide::DEFENDER, BattleHex(11, 5), 20, 100, 20, 4, false},
		{2, BattleSide::ATTACKER, BattleHex(3, 2), 10, 100, 5, 4, false},
		{3, BattleSide::ATTACKER, BattleHex(3, 8), 10, 100, 5, 4, false},
		{4, BattleSide::DEFENDER, BattleHex(6, 5), 10, 100, 10, 4, true},
		{5, BattleSide::DEFENDER, BattleHex(8, 8), 10, 100, 10, 4, false}
	});
	const auto * wideType = CreatureID(10).toCreature();
	ASSERT_NE(wideType, nullptr);
	ASSERT_TRUE(wideType->isDoubleWide());
	ON_CALL(scene.sources[0]->info, unitType()).WillByDefault(Return(wideType));
	scene.sources[0]->add(BonusType::HYPNOTIZED, 1);
	scene.state = std::make_shared<HypotheticBattle>(scene.environment.get(), scene.callback);
	teleportprobe::primeSnapshots(scene);
	const auto before = scene.snapshot();
	const auto * active = scene.state->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	ASSERT_EQ(active->unitId(), 1u);
	ASSERT_EQ(active->unitSide(), BattleSide::DEFENDER);
	ASSERT_TRUE(active->doubleWide());
	ASSERT_TRUE(active->isHypnotized());
	auto caster = scene.state->getForUpdate(2);
	ASSERT_EQ(scene.state->battleGetOwner(active), scene.state->battleGetOwner(caster.get()));
	const auto * spell = SpellID(SpellID::TELEPORT).toSpell();
	ASSERT_NE(spell, nullptr);
	BattleCast cast(scene.state.get(), caster.get(), Mode::PASSIVE, spell);
	teleportprobe::configure(cast);
	// Deliberate non-default real-cast configuration: core smart Teleport uses
	// initial target ownership and cannot target this hypnotized enemy-side ally.
	// Disable smart filtering only here to isolate effective-owner shortlisting.
	cast.smart = false;
	auto mechanics = spell->battleMechanics(&cast);
	const auto targets = SpellTargetEvaluator::getViableTargets(mechanics.get());
	teleportprobe::expectLegalUniqueMoves(*mechanics, targets);
	size_t activeContacts = 0;
	for(const auto & target : targets)
	{
		if(target.front().hexValue != active->getPosition())
			continue;
		const auto child = teleportprobe::castInChild(scene, target, 2, true);
		const auto * moved = child->battleGetUnitByID(1);
		ASSERT_NE(moved, nullptr);
		EXPECT_EQ(moved->unitSide(), BattleSide::DEFENDER);
		EXPECT_EQ(moved->occupiedHex(), BattleHex(target.back().hexValue.toInt() + 1));
		for(const uint32_t enemyId : {4u, 5u})
		{
			const auto * enemy = child->battleGetUnitByID(enemyId);
			ASSERT_NE(enemy, nullptr);
			if(child->battleCanAttackUnit(moved, enemy) && CStack::isMeleeAttackPossible(moved, enemy))
				++activeContacts;
		}
	}
	EXPECT_GT(activeContacts, 0u);
	EXPECT_EQ(scene.snapshot(), before);
}

}
