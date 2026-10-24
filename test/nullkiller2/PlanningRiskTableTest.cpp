/* GPL-2.0-or-later; see license.txt. Container tests, NOT combat calibration. */
#include "../StdInc.h"
#include "../../AI/Nullkiller2/Engine/Experimental/PlanningRiskTable.h"

VCMI_LIB_USING_NAMESPACE
using namespace NK2AI::experimental;
namespace
{
PlanningRiskKey keyFor(int id)
{
	return {1, ObjectInstanceID(id), ObjectInstanceID(50), "exact-fixture-state"};
}
CapturedRiskRequest unsupportedRequest(int id)
{
	CapturedRiskRequest request;
	request.key = keyFor(id);
	return request;
}
const PlanningRiskRoute unchanged{true, 0, 0};
}
TEST(PlanningRiskTable, SortedAdmissionDoesNotDependOnInputOrder)
{
	std::vector<CapturedRiskRequest> ascending;
	std::vector<CapturedRiskRequest> descending;
	for(int id = 0; id < 10; ++id)
	{
		ascending.push_back(unsupportedRequest(id));
		descending.push_back(unsupportedRequest(9 - id));
	}
	const auto a = PlanningRiskTable::build(std::move(ascending));
	const auto b = PlanningRiskTable::build(std::move(descending));
	for(int id = 0; id < 10; ++id)
	{
		EXPECT_EQ(a->find(keyFor(id), unchanged) != nullptr, id < PlanningRiskTable::MAX_REQUESTS);
		EXPECT_EQ(b->find(keyFor(id), unchanged) != nullptr, id < PlanningRiskTable::MAX_REQUESTS);
	}
}
TEST(PlanningRiskTable, RejectsChangedRouteOrSource)
{
	std::vector<CapturedRiskRequest> requests;
	requests.push_back(unsupportedRequest(0));
	const auto table = PlanningRiskTable::build(std::move(requests));
	ASSERT_NE(table->find(keyFor(0), unchanged), nullptr);
	EXPECT_FALSE(table->find(keyFor(0), unchanged)->allScenariosFinished);
	EXPECT_EQ(table->find(keyFor(0), {false, 0, 0}), nullptr);
	EXPECT_EQ(table->find(keyFor(0), {true, 1, 0}), nullptr);
	EXPECT_EQ(table->find(keyFor(0), {true, 0, 1}), nullptr);
	auto changed = keyFor(0);
	++changed.epoch;
	EXPECT_EQ(table->find(changed, unchanged), nullptr);
	changed = keyFor(0);
	changed.capturedState += "changed mana/count/bonus/context";
	EXPECT_EQ(table->find(changed, unchanged), nullptr);
}
TEST(PlanningRiskTable, AmbiguousDuplicateCaptureIsUnknown)
{
	std::vector<CapturedRiskRequest> requests;
	requests.push_back(unsupportedRequest(0));
	requests.push_back(unsupportedRequest(0));
	const auto table = PlanningRiskTable::build(std::move(requests));
	EXPECT_EQ(table->find(keyFor(0), unchanged), nullptr);
}
TEST(PlanningRiskTable, PhysicsMemoSurvivesSourceEpochButNotTurn)
{
	// Tests reuse protocol only, not any chosen combat prediction.
	auto memo = std::make_shared<PlanningRiskMemo>();
	memo->turn = 4;
	memo->consumedScenarios = PlanningRiskTable::MAX_SCENARIOS_PER_TURN;
	memo->entries.emplace("same-physics", PlanningRiskRecord{});
	auto request = unsupportedRequest(0);
	request.physicsState = "same-physics";
	request.key.epoch = 9;
	request.unmodelledOwnMagic = true;
	std::vector<CapturedRiskRequest> requests;
	requests.push_back(std::move(request));
	const auto table = PlanningRiskTable::build(std::move(requests), 4, 42, memo);
	auto key = keyFor(0);
	key.epoch = 9;
	ASSERT_NE(table->find(key, unchanged), nullptr);
	EXPECT_TRUE(table->find(key, unchanged)->reused);
	EXPECT_TRUE(table->find(key, unchanged)->unmodelledOwnMagic);
	EXPECT_EQ(table->scenariosUsed(), 0);
	EXPECT_EQ(table->memo()->consumedScenarios, 42);
	const auto freshTurn = PlanningRiskTable::build({}, 5, 42, table->memo());
	EXPECT_TRUE(freshTurn->memo()->entries.empty());
	EXPECT_EQ(freshTurn->memo()->consumedScenarios, 0);
}
TEST(PlanningRiskTable, CheapAdmissionAlternatesHeroesBeforeCapture)
{
	std::vector<PlanningRiskCandidate> candidates;
	for(int hero = 0; hero < 10; ++hero)
		for(int target = 0; target < 3; ++target)
			candidates.push_back({ObjectInstanceID(hero), ObjectInstanceID(100 + target), static_cast<uint32_t>(target)});
	const auto first = selectRiskCandidates(candidates, 0);
	std::reverse(candidates.begin(), candidates.end());
	const auto second = selectRiskCandidates(candidates, 1);
	ASSERT_EQ(first.size(), PlanningRiskTable::MAX_REQUESTS);
	ASSERT_EQ(second.size(), PlanningRiskTable::MAX_REQUESTS);
	for(size_t index = 0; index < first.size(); ++index)
	{
		EXPECT_EQ(first[index].hero, ObjectInstanceID(index));
		EXPECT_EQ(second[index].hero, ObjectInstanceID(index + 1));
		EXPECT_EQ(first[index].target, ObjectInstanceID(100));
	}
}

#include "../../AI/Nullkiller2/Engine/Experimental/DetachedDefendBonuses.h"
#include "../../lib/bonuses/CBonusSystemNode.h"
#include "../../lib/bonuses/BonusCache.h"

TEST(DetachedDefendBonuses, NativePercentageExpiresOnActivationAndInvalidatesCache)
{
	CBonusSystemNode source(BonusNodeType::STACK_INSTANCE, true);
	source.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::PRIMARY_SKILL,
		BonusSource::OTHER, 10, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE), BonusValueType::BASE_NUMBER));
	const auto sourceVersion = source.getTreeVersion();
	DetachedDefendBonuses owned(source);
	BonusValueCache defense(&owned, Selector::typeSubtype(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::DEFENSE)));
	EXPECT_EQ(defense.getValue(), 10);
	owned.defend();
	EXPECT_EQ(defense.getValue(), 12);
	EXPECT_TRUE(owned.hasBonusOfType(BonusType::UNIT_DEFENDING));
	// No round boundary expiry: the explicit next-activation call removes it.
	EXPECT_EQ(defense.getValue(), 12);
	owned.beforeActivation();
	EXPECT_EQ(defense.getValue(), 10);
	EXPECT_FALSE(owned.hasBonusOfType(BonusType::UNIT_DEFENDING));
	EXPECT_EQ(source.getTreeVersion(), sourceVersion);
	EXPECT_EQ(source.valOfBonuses(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::DEFENSE)), 10);
}

TEST(DetachedDefendBonuses, NativeLowDefenseFallbackAndRepeatedDefendDoNotStack)
{
	CBonusSystemNode source(BonusNodeType::STACK_INSTANCE, true);
	source.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::PRIMARY_SKILL,
		BonusSource::OTHER, 4, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE), BonusValueType::BASE_NUMBER));
	DetachedDefendBonuses owned(source);
	BonusValueCache defense(&owned, Selector::typeSubtype(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::DEFENSE)));
	owned.defend();
	EXPECT_EQ(defense.getValue(), 5);
	owned.defend();
	EXPECT_EQ(defense.getValue(), 5);
	owned.beforeActivation();
	EXPECT_EQ(defense.getValue(), 4);
}
