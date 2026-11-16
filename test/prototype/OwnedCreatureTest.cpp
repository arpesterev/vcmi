#include "lib/StdInc.h"
#include <gtest/gtest.h>
#include "lib/CCreatureHandler.h"
#include "lib/battle/OwnedCreature.h"

TEST(OwnedCreatureBoundary, ValuesAndBonusesSurviveSourceDestruction)
{
 auto source = std::make_unique<CCreature>();
 source->addBonus(21, BonusType::STACK_HEALTH);
 auto snapshot = BonusEvaluationSnapshot::capture({source.get()}, [](const CBonusSystemNode & node)
 {
  BonusLimiterFacts facts;
  facts.type = node.getNodeType();
  facts.originalOwner = node.getOwner();
  return facts;
 });
 const auto root = snapshot.graph().roots().at(0);
 OwnedCreature::Values values{};
 values.getId = CreatureID(17);
 values.getNameSingularTranslated = "Owned unit";
 values.getNamePluralTranslated = "Owned units";
 values.getWarMachine = ArtifactID::BALLISTA;
 values.getBaseHitPoints = 21;
 values.getBaseDamageMin = 3;
 values.getBaseDamageMax = 7;
 values.factionNativeTerrains.insert(TerrainId(2));
 values.recruitCost[EGameResID::GOLD] = 150;
 values.icons.push_back({17, 0, "TWCRPORT", "owned.png"});
 OwnedCreature owned(values, std::move(snapshot), root);
 source->addBonus(50, BonusType::STACK_HEALTH);
 values.getNamePluralTranslated = "Mutated";
 values.recruitCost[EGameResID::GOLD] = 999;
 values.factionNativeTerrains.clear();
 source.reset();
 EXPECT_EQ(owned.getMaxHealth(), 21u);
 EXPECT_EQ(owned.getNamePluralTranslated(), "Owned units");
 EXPECT_EQ(owned.getRecruitCost(EGameResID::GOLD), 150);
 EXPECT_EQ(owned.getWarMachine(), ArtifactID::BALLISTA);
 EXPECT_EQ(owned.getBaseDamageMin(), 3);
 EXPECT_EQ(owned.getBaseDamageMax(), 7);
 EXPECT_TRUE(owned.isNativeTerrain(TerrainId(2)));
 EXPECT_FALSE(owned.isNativeTerrain(TerrainId(3)));
 EXPECT_EQ(owned.getBonusBearer()->getTreeVersion(), 1);
 int count = 0;
 owned.registerIcons([&](int32_t index, int32_t group, const std::string & list, const std::string & image)
 {
  ++count;
  EXPECT_EQ(index, 17); EXPECT_EQ(group, 0);
  EXPECT_EQ(list, "TWCRPORT"); EXPECT_EQ(image, "owned.png");
 });
 EXPECT_EQ(count, 1);
}

TEST(OwnedCreatureBoundary, RejectsMissingNode)
{
 BonusEvaluationSnapshot empty;
 EXPECT_THROW((OwnedCreature(OwnedCreature::Values{}, std::move(empty), 0)), std::out_of_range);
}
