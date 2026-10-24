/*
 * JointTownDefenceRegression.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "../StdInc.h"
#include "../../AI/Nullkiller2/AIGateway.h"
#include "../../AI/Nullkiller2/Behaviors/DefenceBehavior.h"
#include "../../AI/Nullkiller2/Engine/Nullkiller.h"
#include "../../AI/Nullkiller2/Goals/RecruitHero.h"
#include "../../AI/Nullkiller2/Goals/BuyArmy.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/gameState/TavernHeroesPool.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/StartInfo.h"

VCMI_LIB_USING_NAMESPACE

namespace test
{
namespace
{
// Install only the map through the public serialization boundary. No game
// simulation is fabricated: tavern lookup, town getters and army containers
// below are the real implementations. Other state is supplied by the callback.
struct InstallDefenceMap
{
	enum class Version { NO_RAW_POINTERS_IN_SERIALIZER, RANDOMIZATION_REWORK, SERVER_STATISTICS };
	bool saving = true;
	bool loadingGamestate = false;
	std::unique_ptr<CMap> map;
	bool hasFeature(Version) const { return true; }
	template<typename T> void operator&(T &) {}
	void operator&(std::unique_ptr<CMap> & target) { target = std::move(map); }
};

class DefenceFixtureHero final : public CGHeroInstance
{
public:
	explicit DefenceFixtureHero(IGameInfoCallback * callback) : CGHeroInstance(callback) {}
	// Primary skills and actual troop containers remain real. This fixture
	// deliberately excludes ownership bonus graphs and full game initialization.
	void attachToBonusSystem(CGameState &) override {}
	void detachFromBonusSystem(CGameState &) override {}
};
class JointDefenceTown final : public CGTownInstance
{
	int x;
public:
	JointDefenceTown(IGameInfoCallback * callback, int x) : CGTownInstance(callback), x(x)
	{
		ID = Obj::TOWN;
		subID = MapObjectSubID(0);
		id = ObjectInstanceID(x);
		tempOwner = PlayerColor(0);
		setCustomName("Joint defence fixture");
	}
	int3 visitablePos() const override { return int3(x, 0, 0); }
};

class JointDefenceCallback final : public CCallback
{
	std::shared_ptr<CGameState> fixtureState;

public:
	using CCallback::getResourceAmount;
	std::vector<const CGTownInstance *> towns;
	std::vector<const CGHeroInstance *> heroes;
	TResources resources;
	PlayerSettings playerSettings;
	int hireRequests = 0;
	int forcedHeroCount = -1;
	int recruitRequests = 0;
	int swapRequests = 0;
	bool applyHire = true;
	bool applySwap = true;
	bool applyMerge = true;
	int recruitmentLimit = std::numeric_limits<int>::max();

	explicit JointDefenceCallback(std::shared_ptr<CGameState> state)
		: CCallback(state, PlayerColor(0), nullptr), fixtureState(std::move(state)) {}
	TResources getResourceAmount() const override { return resources; }
	const IGameSettings & getSettings() const override { return fixtureState->getMap().getSettings(); }
	int getDate(Date) const override { return 1; }
	int3 getMapSize() const override { return int3(2, 1, 1); }
	std::vector<const CGTownInstance *> getTownsInfo(bool = true) const override { return towns; }
	std::vector<const CGHeroInstance *> getHeroesInfo() const override { return heroes; }
	int getHeroCount(PlayerColor, bool) const override { return forcedHeroCount >= 0 ? forcedHeroCount : heroes.size(); }
	int howManyTowns() const override { return towns.size(); }
	const PlayerSettings * getPlayerSettings(PlayerColor) const override { return &playerSettings; }
	const PlayerState * getPlayerState(PlayerColor, bool = true) const override { return nullptr; }
	const CGObjectInstance * getObj(ObjectInstanceID id, bool = true) const override
	{
		const auto & objects = fixtureState->getMap().objects;
		return id.hasValue() && id.getNum() < objects.size() ? objects[id.getNum()].get() : nullptr;
	}
	void recruitHero(const CGObjectInstance * location, const CGHeroInstance * hero, const HeroTypeID &) override
	{
		++hireRequests;
		if(!applyHire)
			return;
		auto hired = fixtureState->heroesPool->takeHeroFromPool(hero->getHeroTypeID());
		ASSERT_NE(hired, nullptr);
		resources[EGameResID::GOLD] -= GameConstants::HERO_GOLD_COST;
		hired->tempOwner = PlayerColor(0);
		fixtureState->getMap().objects.at(hired->id.getNum()) = hired;
		heroes.push_back(hired.get());
		const_cast<CGTownInstance *>(dynamic_cast<const CGTownInstance *>(location))->setVisitingHero(hired.get());
	}
	void recruitCreatures(const CGDwelling * source, const CArmedInstance * target, CreatureID creature, ui32 amount, si32 level) override
	{
		++recruitRequests;
		const int actual = std::min<int>(amount, recruitmentLimit);
		if(!actual)
			return;
		auto * town = const_cast<CGTownInstance *>(dynamic_cast<const CGTownInstance *>(source));
		const_cast<CArmedInstance *>(target)->addToSlot(target->getSlotFor(creature), creature, actual);
		town->creatures.at(level).first -= actual;
		resources -= creature.toCreature()->getFullRecruitCost() * actual;
	}
	void swapGarrisonHero(const CGTownInstance * source) override
	{
		++swapRequests;
		if(!applySwap)
			return;
		auto * town = const_cast<CGTownInstance *>(source);
		auto * visitor = const_cast<CGHeroInstance *>(town->getVisitingHero());
		ASSERT_NE(visitor, nullptr);
		ASSERT_EQ(town->getGarrisonHero(), nullptr);
		if(applyMerge)
		{
			for(const auto & slot : town->Slots())
				visitor->addToSlot(visitor->getSlotFor(slot.second->getCreatureID()), slot.second->getCreatureID(), slot.second->getCount());
			town->clearSlots();
		}
		town->setVisitingHero(nullptr);
		town->setGarrisonedHero(visitor);
	}
};
}

class Nullkiller2JointTownDefenceRegression : public ::testing::Test
{
protected:
	std::shared_ptr<CGameState> state = std::make_shared<CGameState>();
	std::shared_ptr<JointDefenceCallback> callback = std::make_shared<JointDefenceCallback>(state);
	NK2AI::AIGateway gateway;
	NK2AI::Nullkiller & ai = *gateway.nullkiller;
	std::shared_ptr<JointDefenceTown> town = std::make_shared<JointDefenceTown>(callback.get(), 0);
	std::shared_ptr<JointDefenceTown> other = std::make_shared<JointDefenceTown>(callback.get(), 1);
	std::shared_ptr<DefenceFixtureHero> candidate = std::make_shared<DefenceFixtureHero>(callback.get());
	std::shared_ptr<DefenceFixtureHero> enemy = std::make_shared<DefenceFixtureHero>(callback.get());
	using Defence = NK2AI::Goals::DefenceBehavior;
	uint64_t requiredStrength = 0;

	void SetUp() override
	{
		InstallDefenceMap archive;
		archive.map = std::make_unique<CMap>(callback.get());
		state->serialize(archive);
		state->getMap().objects = {town, other};
		gateway.cc = callback;
		ai.cc = callback;
		ai.playerID = PlayerColor(0);
		ai.settings = std::make_unique<NK2AI::Settings>(0);
		ai.armyManager = std::make_unique<NK2AI::ArmyManager>(callback.get(), &ai);
		ai.heroManager = std::make_unique<NK2AI::HeroManager>(callback.get(), &ai);
		ai.objectClusterizer = std::make_unique<NK2AI::ObjectClusterizer>(&ai);
		ai.buildAnalyzer = std::make_unique<NK2AI::BuildAnalyzer>(&ai);
		ai.dangerHitMap = std::make_unique<NK2AI::DangerHitMapAnalyzer>(&ai);
		callback->towns = {town.get(), other.get()};
		candidate->setHeroType(HeroTypeID(0));
		candidate->setCreature(SlotID(0), CreatureID(0), 1);
		candidate->tempOwner = PlayerColor(0);
		state->getMap().addToHeroPool(candidate);
		state->heroesPool->addHeroToPool(candidate->getHeroTypeID());
		CSimpleArmy unchanged;
		state->heroesPool->setHeroForPlayer(PlayerColor(0), TavernHeroSlot::NATIVE, candidate->getHeroTypeID(), unchanged, TavernSlotRole::FULL_ARMY, false);
		enemy->id = ObjectInstanceID(state->getMap().objects.size());
		enemy->tempOwner = PlayerColor(1);
		state->getMap().objects.push_back(enemy);
		town->addBuilding(BuildingID::TAVERN);
		town->setCreature(SlotID(0), CreatureID(0), 10);
		town->creatures = {{5, {CreatureID(0)}}};
		other->setCreature(SlotID(0), CreatureID(0), 10);
		other->creatures = {{5, {CreatureID(0)}}};
		callback->resources = CreatureID(0).toCreature()->getFullRecruitCost() * 5;
		callback->resources[EGameResID::GOLD] += GameConstants::HERO_GOLD_COST;
		const auto combined = Defence::combinedDefenceStrength(town.get(), candidate.get());
		const auto added = 5 * CreatureID(0).toCreature()->getAIValue() * candidate->getFightingStrength();
		requiredStrength = static_cast<uint64_t>(combined + added);
		ai.dangerHitMap->updateHitMap();
		threaten(*town, requiredStrength);
	}
	void threaten(const CGTownInstance & target, uint64_t danger)
	{
		auto & node = const_cast<NK2AI::HitMapNode &>(ai.dangerHitMap->getObjectThreat(&target));
		node.fastestDanger.danger = danger;
		node.fastestDanger.turn = 0;
		node.fastestDanger.heroPtr = NK2AI::HeroPtr(enemy.get(), callback.get());
		node.maximumDanger = node.fastestDanger;
	}
	NK2AI::Goals::TSubgoal rescue()
	{
		auto tasks = Defence::decomposeUrgentPurchases(&ai);
		EXPECT_EQ(tasks.size(), 1u);
		return tasks.empty() ? NK2AI::Goals::TSubgoal{} : tasks.front();
	}
};

TEST_F(Nullkiller2JointTownDefenceRegression, JointPlanHiresBuysAndVerifiesActualGarrisonMerge)
{
	ASSERT_LT(Defence::combinedDefenceStrength(town.get(), candidate.get()), requiredStrength);
	ASSERT_LT(town->getArmyStrength() + 5 * CreatureID(0).toCreature()->getAIValue(), requiredStrength);
	auto task = rescue();
	ASSERT_TRUE(task);
	ASSERT_NE(dynamic_cast<NK2AI::Goals::RecruitHero *>(task.get()), nullptr);
	EXPECT_NO_THROW(task->asTask()->accept(&gateway));
	EXPECT_EQ(callback->hireRequests, 1);
	EXPECT_EQ(callback->recruitRequests, 1);
	EXPECT_EQ(callback->swapRequests, 1);
	EXPECT_EQ(town->getGarrisonHero(), candidate.get());
	EXPECT_EQ(town->stacksCount(), 0);
	EXPECT_EQ(candidate->getStackCount(SlotID(0)), 16);
	EXPECT_GE(Defence::combinedDefenceStrength(town.get(), candidate.get()), requiredStrength);
	EXPECT_TRUE(ai.isHeroLocked(candidate.get()));
	EXPECT_TRUE(Defence::decomposeUrgentPurchases(&ai).empty());
	EXPECT_FALSE(ai.getLockedResources().nonZero());
}

TEST_F(Nullkiller2JointTownDefenceRegression, JointPlanRechecksAllCostsAndStockBeforeHiring)
{
	auto task = rescue();
	ASSERT_TRUE(task);
	town->creatures[0].first = 4;
	EXPECT_THROW(task->asTask()->accept(&gateway), NK2AI::cannotFulfillGoalException);
	EXPECT_EQ(callback->hireRequests, 0);
	town->creatures[0].first = 5;
	callback->resources[EGameResID::GOLD]--;
	EXPECT_THROW(task->asTask()->accept(&gateway), NK2AI::cannotFulfillGoalException);
	EXPECT_EQ(callback->hireRequests, 0);
}

TEST_F(Nullkiller2JointTownDefenceRegression, PartialRecruitmentKeepsRealEffectsAndReplansWithoutSecondHire)
{
	callback->recruitmentLimit = 1;
	auto task = rescue();
	ASSERT_TRUE(task);
	EXPECT_THROW(task->asTask()->accept(&gateway), NK2AI::cannotFulfillGoalException);
	EXPECT_EQ(callback->hireRequests, 1);
	EXPECT_EQ(callback->recruitRequests, 1);
	EXPECT_EQ(town->getVisitingHero(), candidate.get());
	EXPECT_EQ(town->getStackCount(SlotID(0)), 11);
	EXPECT_FALSE(ai.isHeroLocked(candidate.get()));
	EXPECT_EQ(callback->swapRequests, 0);
	callback->recruitmentLimit = std::numeric_limits<int>::max();
	auto remaining = rescue();
	ASSERT_TRUE(remaining);
	ASSERT_NE(dynamic_cast<NK2AI::Goals::BuyArmy *>(remaining.get()), nullptr);
	EXPECT_NO_THROW(remaining->asTask()->accept(&gateway));
	EXPECT_TRUE(Defence::decomposeUrgentPurchases(&ai).empty());
	EXPECT_EQ(callback->swapRequests, 1);
	EXPECT_EQ(callback->hireRequests, 1);
	EXPECT_EQ(candidate->getStackCount(SlotID(0)), 16);
	EXPECT_TRUE(ai.isHeroLocked(candidate.get()));
	EXPECT_FALSE(ai.getLockedResources().nonZero());
}

TEST_F(Nullkiller2JointTownDefenceRegression, RejectedSwapOrIncompleteMergeNeverLocksDefender)
{
	callback->applySwap = false;
	auto task = rescue();
	ASSERT_TRUE(task);
	EXPECT_THROW(task->asTask()->accept(&gateway), NK2AI::cannotFulfillGoalException);
	EXPECT_FALSE(ai.isHeroLocked(candidate.get()));
	EXPECT_EQ(town->getVisitingHero(), candidate.get());
	callback->applySwap = true;
	callback->applyMerge = false;
	auto merge = rescue();
	ASSERT_TRUE(merge);
	EXPECT_THROW(merge->asTask()->accept(&gateway), NK2AI::cannotFulfillGoalException);
	EXPECT_FALSE(ai.isHeroLocked(candidate.get()));
	EXPECT_EQ(town->getStackCount(SlotID(0)), 15);
}

TEST_F(Nullkiller2JointTownDefenceRegression, SharedBudgetProtectsHigherStrategicValueNotLowerObjectId)
{
	// Seed the already analyzed town list, not a copied valuation formula.
	auto & development = const_cast<std::vector<NK2AI::TownDevelopmentInfo> &>(ai.buildAnalyzer->getDevelopmentInfo());
	development.emplace_back(town.get());
	development.emplace_back(other.get());
	other->addBuilding(BuildingID::CAPITOL);
	other->addBuilding(BuildingID::CASTLE);
	ASSERT_GT(NK2AI::RewardEvaluator(&ai).getStrategicalValue(other.get()), NK2AI::RewardEvaluator(&ai).getStrategicalValue(town.get()));
	const auto power = CreatureID(0).toCreature()->getAIValue();
	threaten(*town, town->getArmyStrength() + power * 5);
	threaten(*other, other->getArmyStrength() + power * 5);
	callback->resources = CreatureID(0).toCreature()->getFullRecruitCost() * 5;
	auto task = rescue();
	ASSERT_TRUE(task);
	EXPECT_EQ(task->town, other.get());
}

TEST_F(Nullkiller2JointTownDefenceRegression, IndefensibleValuableTownDoesNotConsumeJointRescueBudget)
{
	other->addBuilding(BuildingID::CAPITOL);
	auto & development = const_cast<std::vector<NK2AI::TownDevelopmentInfo> &>(ai.buildAnalyzer->getDevelopmentInfo());
	development.emplace_back(town.get());
	development.emplace_back(other.get());
	ASSERT_GT(NK2AI::RewardEvaluator(&ai).getStrategicalValue(other.get()), NK2AI::RewardEvaluator(&ai).getStrategicalValue(town.get()));
	threaten(*other, std::numeric_limits<uint32_t>::max());
	auto task = rescue();
	ASSERT_TRUE(task);
	EXPECT_EQ(task->town, town.get());
	EXPECT_NO_THROW(task->asTask()->accept(&gateway));
	EXPECT_EQ(callback->hireRequests, 1);
	EXPECT_EQ(town->getGarrisonHero(), candidate.get());
}
TEST_F(Nullkiller2JointTownDefenceRegression, ChangedHeroCapAndMergeSlotsStopBeforeFirstSpend)
{
	auto task = rescue();
	ASSERT_TRUE(task);
	callback->forcedHeroCount = 10000;
	EXPECT_THROW(task->asTask()->accept(&gateway), NK2AI::cannotFulfillGoalException);
	EXPECT_EQ(callback->hireRequests, 0);
	callback->forcedHeroCount = -1;
	for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
		town->setCreature(SlotID(i), CreatureID(i * 2), 10);
	candidate->clearSlots();
	candidate->setCreature(SlotID(0), CreatureID(14), 1);
	EXPECT_THROW(task->asTask()->accept(&gateway), NK2AI::cannotFulfillGoalException);
	EXPECT_EQ(callback->hireRequests, 0);
}

TEST_F(Nullkiller2JointTownDefenceRegression, RejectedHireNeverAttemptsTroopPurchaseOrGarrison)
{
	callback->applyHire = false;
	const auto before = callback->resources;
	auto task = rescue();
	ASSERT_TRUE(task);
	EXPECT_THROW(task->asTask()->accept(&gateway), NK2AI::cannotFulfillGoalException);
	EXPECT_EQ(callback->hireRequests, 1);
	EXPECT_EQ(callback->recruitRequests, 0);
	EXPECT_EQ(callback->swapRequests, 0);
	EXPECT_EQ(callback->resources, before);
}

TEST_F(Nullkiller2JointTownDefenceRegression, OrdinaryHireKeepsPreviousNoPurchaseNoGarrisonBehavior)
{
	NK2AI::Goals::RecruitHero task(town.get(), candidate.get());
	EXPECT_NO_THROW(task.accept(&gateway));
	EXPECT_EQ(callback->hireRequests, 1);
	EXPECT_EQ(callback->recruitRequests, 0);
	EXPECT_EQ(callback->swapRequests, 0);
	EXPECT_EQ(town->getVisitingHero(), candidate.get());
	EXPECT_FALSE(ai.isHeroLocked(candidate.get()));
}

TEST_F(Nullkiller2JointTownDefenceRegression, SharedCandidateIsNotAllocatedToTwoJointPlans)
{
	other->addBuilding(BuildingID::TAVERN);
	threaten(*other, requiredStrength);
	callback->resources *= 2;
	const auto before = callback->resources;
	for(int pass = 0; pass < 2; ++pass)
	{
		auto task = rescue();
		ASSERT_TRUE(task);
		EXPECT_EQ(task->town, town.get());
		EXPECT_EQ(callback->resources, before);
		EXPECT_EQ(callback->hireRequests, 0);
		EXPECT_FALSE(ai.getLockedResources().nonZero());
	}
}

}
