/*
 * AdventureBattleRiskRegression.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "../StdInc.h"
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
	CArtifactInstance book{&callback, ArtifactID::SPELLBOOK.toArtifact()};
	NK2AI::AIPath path;

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
	const auto cost = hero.getSpellCost(SpellID::MAGIC_ARROW.toSpell());
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
	const auto cost = hero.getSpellCost(SpellID::MAGIC_ARROW.toSpell());
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
}
