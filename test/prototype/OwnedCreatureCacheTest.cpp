#include "StdInc.h"
#include "lib/CCreatureHandler.h"
#include "lib/battle/OwnedCreature.h"
#include "lib/bonuses/BonusCache.h"
#include "lib/bonuses/BonusEvaluationSnapshot.h"

TEST(OwnedCreatureBoundary, FirstNativeBonusCacheReadUsesCapturedNonzeroValue)
{
 CCreature source;
 source.addBonus(21, BonusType::STACK_HEALTH);
 auto snapshot = BonusEvaluationSnapshot::capture({&source}, [](const CBonusSystemNode & node)
 {
  BonusLimiterFacts facts;
  facts.type = node.getNodeType(); facts.originalOwner = node.getOwner();
  return facts;
 });
 const auto node = snapshot.graph().roots().at(0);
 OwnedCreature creature(OwnedCreature::Values{}, std::move(snapshot), node);
 EXPECT_EQ(creature.getBonusBearer()->valOfBonuses(BonusType::STACK_HEALTH), 21);
 BonusValueCache cache(creature.getBonusBearer(), Selector::type()(BonusType::STACK_HEALTH));
 EXPECT_EQ(cache.getValue(), 21);
 EXPECT_EQ(cache.getValue(), 21);
 BonusDerivedValueCache derived(creature.getBonusBearer());
 EXPECT_EQ(derived.getValue([] { return 37; }, 0), 37);
}
