/*
 * HeroChainCostRegression.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "../StdInc.h"
#include "../../AI/Nullkiller2/Engine/Nullkiller.h"
#include "../../AI/Nullkiller2/Pathfinding/Actors.h"
#include "../../lib/mapObjects/CGDwelling.h"

VCMI_LIB_USING_NAMESPACE

namespace test
{
namespace
{
class HeroChainCostCallback final : public CCallback
{
public:
	TResources resources;
	HeroChainCostCallback() : CCallback(nullptr, std::nullopt, nullptr) {}
	TResources getResourceAmount() const override { return resources; }
};

class ChainCostHero final : public CGHeroInstance
{
public:
	explicit ChainCostHero(IGameInfoCallback * cb) : CGHeroInstance(cb) {}
	int3 visitablePos() const override { return int3(0, 0, 0); }
};

class ChainCostHillFort final : public CGDwelling
{
public:
	explicit ChainCostHillFort(IGameInfoCallback * cb) : CGDwelling(cb) { ID = Obj::HILL_FORT; }
	int3 visitablePos() const override { return int3(1, 0, 0); }
};
}

TEST(Nullkiller2HeroChainCostRegression, PaidUpgradeIsChargedExactlyOnceIncludingSpecialActors)
{
	auto callback = std::make_shared<HeroChainCostCallback>();
	NK2AI::Nullkiller ai;
	ai.cc = callback;
	ai.armyManager = std::make_unique<NK2AI::ArmyManager>(callback.get(), &ai);
	callback->resources[EGameResID::GOLD] = 1000000;
	ChainCostHero hero(callback.get());
	hero.setHeroType(HeroTypeID(0));
	hero.secSkills.clear();
	hero.setCreature(SlotID(0), CreatureID(2), 200); // Archers upgrade to marksmen.
	ChainCostHillFort hillFort(callback.get());

	const auto upgrade = ai.armyManager->calculateCreaturesUpgrade(&hero, &hillFort, callback->resources);
	ASSERT_GT(upgrade.upgradeValue, 0);
	ASSERT_GT(upgrade.upgradeCost[EGameResID::GOLD], 0);

	NK2AI::HeroActor carrier(&hero, NK2AI::MAIN, 1, &ai);
	carrier.armyCost[EGameResID::GOLD] = 1234;
	NK2AI::HillFortActor other(&hillFort, 2);
	NK2AI::HeroExchangeMap exchanges(&carrier, &ai);
	const auto result = exchanges.tryExchangeNoLock(&other);
	ASSERT_TRUE(result.lockAcquired);
	ASSERT_NE(result.actor, nullptr);

	const auto expectedCost = carrier.armyCost + other.armyCost + upgrade.upgradeCost;
	EXPECT_EQ(result.actor->armyCost, expectedCost);
	EXPECT_EQ(result.actor->battleActor->armyCost, expectedCost);
	EXPECT_EQ(result.actor->resourceActor->armyCost, expectedCost);
	EXPECT_EQ(exchanges.tryExchangeNoLock(&other).actor, result.actor);
	EXPECT_EQ(result.actor->armyCost, expectedCost);
}

TEST(Nullkiller2HeroChainCostRegression, UnaffordableParentsDoNotProduceExchange)
{
	auto callback = std::make_shared<HeroChainCostCallback>();
	NK2AI::Nullkiller ai;
	ai.cc = callback;
	ChainCostHero hero(callback.get());
	NK2AI::HeroActor carrier(&hero, NK2AI::MAIN, 1, &ai);
	carrier.armyCost[EGameResID::GOLD] = 100;
	ChainCostHillFort hillFort(callback.get());
	NK2AI::HillFortActor other(&hillFort, 2);
	NK2AI::HeroExchangeMap exchanges(&carrier, &ai);
	const auto result = exchanges.tryExchangeNoLock(&other);
	EXPECT_TRUE(result.lockAcquired);
	EXPECT_EQ(result.actor, nullptr);
}
}
