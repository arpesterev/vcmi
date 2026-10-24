/*
 * AdventureBattleRiskRegression.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "../StdInc.h"
#include "../../AI/Nullkiller2/Behaviors/CaptureObjectsBehavior.h"
#include "../../AI/Nullkiller2/Engine/Nullkiller.h"
#include "../../AI/Nullkiller2/Goals/ExecuteHeroChain.h"
#include "../../AI/Nullkiller2/Pathfinding/Actions/SpecialAction.h"
#include "../../AI/Nullkiller2/AIUtility.h"
#include "../../AI/Nullkiller2/Pathfinding/AINodeStorage.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/entities/artifact/CArtifactInstance.h"
#include <vcmi/spells/Spell.h>

VCMI_LIB_USING_NAMESPACE

namespace test
{
namespace
{
class CaptureDependency final : public NK2AI::SpecialAction
{
	NK2AI::AIPath dependencyPath;
public:
	explicit CaptureDependency(const NK2AI::AIPath & path) : dependencyPath(path) {}
	NK2AI::Goals::TSubgoal decompose(const NK2AI::Nullkiller *, const CGHeroInstance *) const override
	{
		return NK2AI::Goals::sptr(NK2AI::Goals::ExecuteHeroChain(dependencyPath));
	}
	std::string toString() const override { return "Capture safety dependency control"; }
};

class RiskCallback final : public CCallback
{
public:
	const CArtifactInstance * book = nullptr;
	RiskCallback() : CCallback(nullptr, std::nullopt, nullptr) {}
	const CArtifactInstance * getArtInstance(ArtifactInstanceID id) const override
	{
		return book && book->getId() == id ? book : nullptr;
	}
};
}

class Nullkiller2AdventureBattleRiskRegression : public ::testing::Test
{
protected:
	RiskCallback callback;
	CGHeroInstance hero{&callback};
	CArtifactInstance book{&callback, ArtifactID(ArtifactID::SPELLBOOK).toArtifact()};
	NK2AI::AIPath path;
	NK2AI::Nullkiller ai;

	void SetUp() override
	{
		hero.pushPrimSkill(PrimarySkill::ATTACK, 0);
		hero.pushPrimSkill(PrimarySkill::DEFENSE, 0);
		hero.pushPrimSkill(PrimarySkill::KNOWLEDGE, 10);
		hero.pushPrimSkill(PrimarySkill::SPELL_POWER, 100);
		hero.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::MANA_PER_KNOWLEDGE_PERCENTAGE, BonusSource::OTHER, 1000, BonusSourceID()));
		ASSERT_TRUE(hero.setCreature(SlotID(0), CreatureID(0), 100));
		path.targetHero = &hero;
		path.heroArmy = &hero;
	}

	void prepareCapture()
	{
		ai.cc = std::shared_ptr<RiskCallback>(&callback, [](RiskCallback *) {});
		ai.playerID = PlayerColor(0);
		ai.settings = std::make_unique<NK2AI::Settings>(0);
		ai.heroManager = std::make_unique<NK2AI::HeroManager>(&callback, &ai);
		ai.buildAnalyzer = std::make_unique<NK2AI::BuildAnalyzer>(&ai);
		hero.id = ObjectInstanceID(1);
		hero.tempOwner = ai.playerID;
		hero.setHeroType(HeroTypeID(0));
		path.chainMask = 1;
		path.exchangeCount = 1;
		NK2AI::AIPathNodeInfo node{};
		node.cost = 0.5f;
		node.turns = 0;
		node.coord = int3(0, 0, 0);
		node.layer = EPathfindingLayer::LAND;
		node.targetHero = &hero;
		node.parentIndex = -1;
		node.chainMask = 1;
		path.nodes.push_back(node);
	}

	void captureProvenance(uint64_t destinationDanger, uint64_t earlierLoss, int manaSpent)
	{
		NK2AI::ChainActor actor;
		actor.hero = &hero;
		NK2AI::AIPathNode destination;
		destination.actor = &actor;
		destination.arrivalArmyLoss = earlierLoss;
		destination.armyLoss = earlierLoss;
		destination.manaCost = manaSpent;
		path.setRiskProvenance(destination, destinationDanger, 0);
	}

	void equipBook()
	{
		book.setId(ArtifactInstanceID(0));
		callback.book = &book;
		hero.CArtifactSet::putArtifact(ArtifactPosition::SPELLBOOK, &book);
		ASSERT_TRUE(hero.hasSpellbook());
	}
};

TEST_F(Nullkiller2AdventureBattleRiskRegression, EarlierLossesCanMakeDestinationUnsafe)
{
	const auto army = hero.getArmyStrength();
	path.setDangerProvenance(army / 2, 0);
	ASSERT_TRUE(NK2AI::isSafeToVisit(path, 1.1f));
	path.armyLoss = army * 3 / 4;
	EXPECT_FALSE(NK2AI::isSafeToVisit(path, 1.1f));
}

TEST_F(Nullkiller2AdventureBattleRiskRegression, SmallEarlierLossStillAllowsFavorableAttack)
{
	path.setDangerProvenance(hero.getArmyStrength() / 2, 0);
	path.armyLoss = hero.getArmyStrength() / 10;
	EXPECT_TRUE(NK2AI::isSafeToVisit(path, 1.1f));
}

TEST_F(Nullkiller2AdventureBattleRiskRegression, DestinationLossIsNotChargedTwice)
{
	path.setDangerProvenance(hero.getArmyStrength() / 2, 0);
	path.targetObjectArmyLoss = hero.getArmyStrength();
	EXPECT_TRUE(NK2AI::isSafeToVisit(path, 1.1f));
}

TEST_F(Nullkiller2AdventureBattleRiskRegression, ExcessLossCannotUnderflowIntoHugeArmy)
{
	path.setDangerProvenance(1, 0);
	path.armyLoss = std::numeric_limits<uint64_t>::max();
	EXPECT_FALSE(NK2AI::isSafeToVisit(path, 1.1f));
}

TEST_F(Nullkiller2AdventureBattleRiskRegression, EarlierStrongGuardIsNotComparedToFinalSurvivors)
{
	NK2AI::AIPathNodeInfo node;
	node.targetHero = &hero;
	node.danger = hero.getArmyStrength() / 2;
	path.nodes.push_back(node);
	path.armyLoss = hero.getArmyStrength() * 3 / 4;
	path.setDangerProvenance(1, node.danger); // Retain earlier guard in legacy aggregate.
	EXPECT_TRUE(NK2AI::isSafeToVisit(path, 1.1f));
}

TEST_F(Nullkiller2AdventureBattleRiskRegression, UnaffordableCombatSpellDoesNotMakeAttackSafe)
{
	equipBook();
	hero.addSpellToSpellbook(SpellID::MAGIC_ARROW);
	const auto cost = hero.getSpellCost(SpellID(SpellID::MAGIC_ARROW).toSpell());
	ASSERT_GT(cost, 1);
	hero.mana = cost - 1;
	const auto danger = static_cast<uint64_t>(hero.getArmyStrength() * 1.1);
	// Demonstrate the old production calculation falsely accepted this attack.
	ASSERT_GT(hero.getHeroStrength() * hero.getArmyStrength(), danger);
	EXPECT_FALSE(NK2AI::isSafeToVisit(&hero, danger, 1.0f));
	hero.mana = cost;
	EXPECT_TRUE(NK2AI::isSafeToVisit(&hero, danger, 1.0f));
}

TEST_F(Nullkiller2AdventureBattleRiskRegression, NoManaOrNoBookCannotProvideMagicCredit)
{
	hero.addSpellToSpellbook(SpellID::MAGIC_ARROW);
	hero.mana = 100;
	const auto danger = hero.getArmyStrength();
	EXPECT_FALSE(NK2AI::isSafeToVisit(&hero, danger, 1.0f));
	equipBook();
	hero.mana = 0;
	EXPECT_FALSE(NK2AI::isSafeToVisit(&hero, danger, 1.0f));
}


TEST_F(Nullkiller2AdventureBattleRiskRegression, AggregateGraphDangerDoesNotPretendToBeDestinationOnly)
{
	path.targetObjectDanger = hero.getArmyStrength() / 2;
	path.armyLoss = hero.getArmyStrength() * 3 / 4;
	ASSERT_FALSE(path.hasDestinationRiskProvenance);
	EXPECT_TRUE(NK2AI::isSafeToVisit(path, 1.1f));
}

TEST_F(Nullkiller2AdventureBattleRiskRegression, RouteSpellsCannotReuseManaInDestinationBattle)
{
	equipBook();
	hero.addSpellToSpellbook(SpellID::MAGIC_ARROW);
	const auto cost = hero.getSpellCost(SpellID(SpellID::MAGIC_ARROW).toSpell());
	hero.mana = cost;
	path.setDangerProvenance(static_cast<uint64_t>(hero.getArmyStrength() * 1.1), 0);
	ASSERT_TRUE(NK2AI::isSafeToVisit(path, 1.0f));
	path.destinationHeroManaCost = 1;
	EXPECT_FALSE(NK2AI::isSafeToVisit(path, 1.0f));
	EXPECT_EQ(hero.mana, cost); // Planning must not spend live mana.
}

TEST_F(Nullkiller2AdventureBattleRiskRegression, InvalidSafetyRatioCannotAuthorizeBattle)
{
	EXPECT_FALSE(NK2AI::isSafeToVisit(&hero, 1, -1.0f));
	EXPECT_FALSE(NK2AI::isSafeToVisit(&hero, 1, std::numeric_limits<float>::quiet_NaN()));
	EXPECT_TRUE(NK2AI::isSafeToVisit(&hero, 0, 1.1f));
}

TEST_F(Nullkiller2AdventureBattleRiskRegression, CaptureProducerRejectsPriorLossAndKeepsSafeRoute)
{
	prepareCapture();
	const auto army = hero.getArmyStrength();
	captureProvenance(army / 2, army / 10, 0);
	auto safe = NK2AI::Goals::CaptureObjectsBehavior::getVisitGoals({path}, &ai);
	ASSERT_EQ(safe.size(), 1u);
	EXPECT_FALSE(safe.front()->invalid());
	captureProvenance(army / 2, army * 3 / 4, 0);
	ASSERT_TRUE(NK2AI::isSafeToVisit(&hero, path.heroArmy, path.getTotalDanger(), ai.settings->getSafeAttackRatio()));
	auto unsafe = NK2AI::Goals::CaptureObjectsBehavior::getVisitGoals({path}, &ai);
	ASSERT_EQ(unsafe.size(), 1u);
	EXPECT_TRUE(unsafe.front()->invalid());
}

TEST_F(Nullkiller2AdventureBattleRiskRegression, CaptureProducerCannotReuseRouteSpellMana)
{
	prepareCapture();
	equipBook();
	hero.addSpellToSpellbook(SpellID::MAGIC_ARROW);
	const auto cost = hero.getSpellCost(SpellID(SpellID::MAGIC_ARROW).toSpell());
	hero.mana = cost;
	captureProvenance(hero.getArmyStrength(), 0, 0);
	auto affordable = NK2AI::Goals::CaptureObjectsBehavior::getVisitGoals({path}, &ai);
	ASSERT_EQ(affordable.size(), 1u);
	ASSERT_FALSE(affordable.front()->invalid());
	captureProvenance(hero.getArmyStrength(), 0, 1);
	ASSERT_TRUE(NK2AI::isSafeToVisit(&hero, path.heroArmy, path.getTotalDanger(), ai.settings->getSafeAttackRatio()));
	auto spent = NK2AI::Goals::CaptureObjectsBehavior::getVisitGoals({path}, &ai);
	ASSERT_EQ(spent.size(), 1u);
	EXPECT_TRUE(spent.front()->invalid());
	EXPECT_EQ(hero.mana, cost);
}


TEST_F(Nullkiller2AdventureBattleRiskRegression, BlockedDependencyCannotWrapUnsafeCaptureChain)
{
	prepareCapture();
	const auto army = hero.getArmyStrength();
	path.nodes.front().specialAction = std::make_shared<CaptureDependency>(path);
	path.nodes.front().actionIsBlocked = true;
	captureProvenance(army / 2, army / 10, 0);
	auto safeDependency = NK2AI::Goals::CaptureObjectsBehavior::getVisitGoals({path}, &ai);
	ASSERT_EQ(safeDependency.size(), 1u);
	ASSERT_FALSE(safeDependency.front()->invalid());
	captureProvenance(army / 2, army * 3 / 4, 0);
	auto unsafeDependency = NK2AI::Goals::CaptureObjectsBehavior::getVisitGoals({path}, &ai);
	ASSERT_EQ(unsafeDependency.size(), 1u);
	EXPECT_TRUE(unsafeDependency.front()->invalid());
}

}
