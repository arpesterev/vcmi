/* GPL-2.0-or-later; see license.txt. Stage-3 reference checks. */
#pragma once
#include "../../AI/Nullkiller2/Engine/Experimental/PlanningBattleSnapshot.h"
#include "../../AI/Nullkiller2/Engine/Experimental/SampledBattleRollout.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/army/CStackInstance.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/json/JsonNode.h"

VCMI_LIB_USING_NAMESPACE

namespace test
{
// Observe the exact serialized RNG state without drawing or reseeding. This
// helper may run before reference actions without changing their random stream.
struct RandomStateCapture
{
	bool saving = true;
	std::string state;
	RandomStateCapture & operator&(std::string & value)
	{
		state = value;
		return *this;
	}
};

inline void verifyPlanningSnapshot(const IGameInfoCallback & source,
	const CArmedInstance & attacker, const CArmedInstance & defender,
	const CBattleInfoCallback & real)
{
	struct NodeRecord
	{
		const CBonusSystemNode * node;
		int version;
		TCNodesVector parents;
		TCNodesVector children;
		std::vector<JsonNode> bonuses;
	};
	std::vector<NodeRecord> records;
	auto capture = [&records](const CBonusSystemNode * node)
	{
		if(std::any_of(records.begin(), records.end(), [node](const auto & item) { return item.node == node; }))
			return;
		NodeRecord record{node, node->getTreeVersion(), node->getParentNodes(), node->getChildrenNodes(), {}};
		for(const auto & bonus : node->getExportedBonusList())
			record.bonuses.push_back(bonus->toJsonNode());
		records.push_back(std::move(record));
	};
	for(const auto * army : {&attacker, &defender})
	{
		capture(army);
		for(const auto & [slot, stack] : army->Slots())
		{
			capture(stack.get());
			capture(stack->getCreature());
		}
	}
	const auto * attackerBattle = attacker.battle;
	const auto * defenderBattle = defender.battle;
	std::vector<int64_t> healthBefore;
	for(const auto * unit : real.battleAliveUnits())
		healthBefore.push_back(unit->getAvailableHealth());
	const auto * hero = dynamic_cast<const CGHeroInstance *>(&attacker);
	const int manaBefore = hero ? hero->mana : 0;

	NK2AI::experimental::PlanningBattleContext context;
	context.tile = real.getBattle()->getLocation();
	context.terrain = real.battleTerrainType();
	context.battlefield = real.battleGetBattlefieldType();
	context.layout = real.getBattle()->getLayout();
	RandomStateCapture randomBefore;
	CRandomGenerator::getDefault().serialize(randomBefore);
	auto snapshot = NK2AI::experimental::PlanningBattleSnapshot::create(source, attacker, defender, context);
	ASSERT_NE(snapshot, nullptr);
	const auto copies = snapshot->battle().battleAliveUnits();
	const auto originals = real.battleAliveUnits();
	ASSERT_EQ(copies.size(), originals.size());
	for(const auto * original : originals)
	{
		const auto found = std::find_if(copies.begin(), copies.end(), [original](const auto * copy)
		{
			return copy->unitSide() == original->unitSide() && copy->unitSlot() == original->unitSlot();
		});
		ASSERT_NE(found, copies.end());
		const auto * copy = *found;
		EXPECT_EQ(copy->getPosition(), original->getPosition());
		EXPECT_EQ(copy->getCount(), original->getCount());
		EXPECT_EQ(copy->getAvailableHealth(), original->getAvailableHealth());
		EXPECT_EQ(copy->getMovementRange(), original->getMovementRange());
		EXPECT_EQ(copy->moraleVal(), original->moraleVal());
		EXPECT_EQ(copy->luckVal(), original->luckVal());
		for(bool ranged : {false, true})
		{
			EXPECT_EQ(copy->getAttack(ranged), original->getAttack(ranged));
			EXPECT_EQ(copy->getDefense(ranged), original->getDefense(ranged));
			EXPECT_EQ(copy->getMinDamage(ranged), original->getMinDamage(ranged));
			EXPECT_EQ(copy->getMaxDamage(ranged), original->getMaxDamage(ranged));
		}
	}
	const auto obstacles = real.battleGetAllObstacles();
	const auto copiedObstacles = snapshot->battle().battleGetAllObstacles();
	ASSERT_EQ(copiedObstacles.size(), obstacles.size());
	for(size_t index = 0; index < obstacles.size(); ++index)
	{
		EXPECT_EQ(copiedObstacles[index]->ID, obstacles[index]->ID);
		EXPECT_EQ(copiedObstacles[index]->pos, obstacles[index]->pos);
		EXPECT_EQ(copiedObstacles[index]->obstacleType, obstacles[index]->obstacleType);
	}
	// Simulation must leave both live inputs and the reusable captured battle
	// unchanged. Support/outcome qualification belongs to the oracle cases.
	const auto physicsBefore = snapshot->physicsState();
	auto repeatedCapture = NK2AI::experimental::PlanningBattleSnapshot::create(source, attacker, defender, context);
	ASSERT_NE(repeatedCapture, nullptr);
	EXPECT_EQ(repeatedCapture->physicsState(), physicsBefore);
	EXPECT_EQ(repeatedCapture->capturedSourceState(), snapshot->capturedSourceState());
	repeatedCapture.reset();
	std::vector<std::pair<int64_t, BattleHex>> capturedUnits;
	for(const auto * unit : copies) capturedUnits.emplace_back(unit->getAvailableHealth(), unit->getPosition());
	(void)NK2AI::experimental::runSampledBattle(snapshot->battle(), snapshot->frozenGameInfo(), 17041);
	EXPECT_EQ(snapshot->physicsState(), physicsBefore);
	for(size_t index = 0; index < copies.size(); ++index)
	{
		EXPECT_EQ(copies[index]->getAvailableHealth(), capturedUnits[index].first);
		EXPECT_EQ(copies[index]->getPosition(), capturedUnits[index].second);
	}
	snapshot.reset();
	EXPECT_EQ(attacker.battle, attackerBattle);
	EXPECT_EQ(defender.battle, defenderBattle);
	EXPECT_EQ(hero ? hero->mana : 0, manaBefore);
	RandomStateCapture randomAfter;
	CRandomGenerator::getDefault().serialize(randomAfter);
	EXPECT_EQ(randomAfter.state, randomBefore.state);
	for(const auto & record : records)
	{
		EXPECT_EQ(record.node->getTreeVersion(), record.version);
		EXPECT_EQ(record.node->getParentNodes(), record.parents);
		EXPECT_EQ(record.node->getChildrenNodes(), record.children);
		std::vector<JsonNode> bonuses;
		for(const auto & bonus : record.node->getExportedBonusList())
			bonuses.push_back(bonus->toJsonNode());
		EXPECT_EQ(bonuses, record.bonuses);
	}
	std::vector<int64_t> healthAfter;
	for(const auto * unit : real.battleAliveUnits())
		healthAfter.push_back(unit->getAvailableHealth());
	EXPECT_EQ(healthAfter, healthBefore);
}
}
