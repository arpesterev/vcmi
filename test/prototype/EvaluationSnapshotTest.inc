#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "lib/bonuses/BonusEvaluationSnapshot.h"
#include "lib/bonuses/Updaters.h"
#include "lib/GameLibrary.h"
#include "lib/CCreatureHandler.h"

class EvaluationSnapshotTest : public BattleTestFixture
{
public:
 void SetUp() override { BattleTestFixture::SetUp(); startGame(); startBattle(); }
 static BonusLimiterFacts facts(const CBonusSystemNode & n)
 {
  Bonus dummy; BonusList empty; BonusLimitationContext raw{dummy,n,empty,empty}; NativeBonusLimiterContext c(raw);
  BonusLimiterFacts f; f.type=n.getNodeType(); f.originalOwner=n.getOwner();
  f.creature=c.hasCreature(); if(f.creature) {f.creatureID=c.creatureID();f.creatureLevel=c.creatureLevel();f.creatureAlignment=c.creatureAlignment();f.upgradeAncestors.emplace();}
  f.stackInstance=c.hasStackInstance();if(f.stackInstance){f.commander=c.isCommander();if(!*f.commander)f.rank=c.rank();}
  f.battleStack=c.hasBattleStack();if(f.battleStack){f.hexes=c.hexes();f.validPosition=c.validPosition();f.adjacentCreatures.emplace();}
  f.factionProvider=c.hasFaction();if(f.factionProvider) f.faction=c.faction();
  f.currentTerrain=c.currentTerrain();f.nativeTerrains.emplace();if(f.factionProvider && c.nativeTerrain(*f.currentTerrain))f.nativeTerrains->insert(*f.currentTerrain);
  f.creatureSourceFactions.emplace();
  LIBRARY->creatures()->forEach([&](const Creature * creature,bool &){auto id=creature->getId();f.creatureSourceFactions->emplace(id,creature->getFactionID());if(f.creature && c.creatureUpgradeOf(id))f.upgradeAncestors->insert(id);if(f.battleStack && *f.validPosition && c.adjacentTo(id))f.adjacentCreatures->push_back(id);});
  return f;
 }
 static BonusEvaluationSnapshot capture(const CBonusSystemNode & n) { return BonusEvaluationSnapshot::capture({&n},facts); }
 static std::shared_ptr<Bonus> bonus(BonusType t, int v)
 { return std::make_shared<Bonus>(BonusDuration::PERMANENT,t,BonusSource::OTHER,v,ArtifactID(777)); }
 static CSelector selector() { return Selector::source(BonusSource::OTHER,ArtifactID(777)); }
 void parity(const CBonusSystemNode & node)
 {
  auto s=capture(node); auto expected=node.getAllBonuses(selector()); auto actual=s.getAllBonuses(s.graph().roots().front(),selector());
  ASSERT_EQ(actual->size(),expected->size());
  for(size_t i=0;i<actual->size();++i) { EXPECT_EQ((*actual)[i]->type,(*expected)[i]->type); EXPECT_EQ((*actual)[i]->val,(*expected)[i]->val); }
 }
};
TEST_F(EvaluationSnapshotTest, NativeParentOrderAndSelectorAfterLimiting)
{
 CBonusSystemNode node(BonusNodeType::GLOBAL_EFFECTS), low(BonusNodeType::GLOBAL_EFFECTS), high(BonusNodeType::GLOBAL_EFFECTS);
 low.addNewBonus(bonus(BonusType::ARTIFACT_CHARGE,0)); high.addNewBonus(bonus(BonusType::ARTIFACT_CHARGE,5));
 auto gated=bonus(BonusType::STACKS_SPEED,101); gated->limiter=std::make_shared<HasChargesLimiter>(2); node.addNewBonus(gated);
 node.attachTo(low); node.attachTo(high); parity(node);
 auto speed=selector().And(Selector::type()(BonusType::STACKS_SPEED)); auto a=capture(node); EXPECT_TRUE(a.getAllBonuses(a.graph().roots().front(),speed)->empty());
 node.detachFrom(low);node.detachFrom(high);node.attachTo(high);node.attachTo(low);parity(node);
 auto b=capture(node); EXPECT_EQ(b.getAllBonuses(b.graph().roots().front(),speed)->size(),1);
}
TEST_F(EvaluationSnapshotTest, SelfDependentChargeRemainsUnresolved)
{
 CBonusSystemNode node(BonusNodeType::GLOBAL_EFFECTS); auto charge=bonus(BonusType::ARTIFACT_CHARGE,5);charge->limiter=std::make_shared<HasChargesLimiter>(1);node.addNewBonus(charge);parity(node);
 auto s=capture(node);EXPECT_TRUE(s.getAllBonuses(s.graph().roots().front(),selector())->empty());
}
TEST_F(EvaluationSnapshotTest, DiamondSharedIdentityDeduplicatesOnlyAtStacking)
{
 CBonusSystemNode root(BonusNodeType::GLOBAL_EFFECTS), a(BonusNodeType::GLOBAL_EFFECTS), b(BonusNodeType::GLOBAL_EFFECTS), leaf(BonusNodeType::GLOBAL_EFFECTS);
 root.addNewBonus(bonus(BonusType::STACKS_SPEED,3));a.attachTo(root);b.attachTo(root);leaf.attachTo(a);leaf.attachTo(b);parity(leaf);
 auto s=capture(leaf);EXPECT_EQ(s.getAllBonuses(s.graph().roots().front(),selector())->size(),1);
}
TEST_F(EvaluationSnapshotTest, OwnedValuesSurviveNativeAndResultMutation)
{
 std::optional<BonusEvaluationSnapshot> s;
 { CBonusSystemNode node(BonusNodeType::GLOBAL_EFFECTS);auto value=bonus(BonusType::STACKS_SPEED,17);node.addNewBonus(value);s=capture(node);value->val=99; }
 auto root=s->graph().roots().front();auto a=s->getAllBonuses(root,selector());ASSERT_EQ(a->size(),1);EXPECT_EQ(a->front()->val,17);a->front()->val=44;
 EXPECT_EQ(s->getAllBonuses(root,selector())->front()->val,17);
}
TEST_F(EvaluationSnapshotTest, RealStackUpdaterUsesCapturedCount)
{
 auto *stack=addStack(BattleSide::ATTACKER,creatureByName("core:woodElf"),BattleHex(5,5),10);
 auto value=bonus(BonusType::STACKS_SPEED,3);value->updater=std::make_shared<TimesStackSizeUpdater>();stack->addNewBonus(value);parity(*stack);
 auto s=capture(*stack);EXPECT_EQ(s.updaterContext(s.graph().roots().front()).stackCount(),10);auto expected=stack->getAllBonuses(selector());ASSERT_FALSE(expected->empty());
 EXPECT_EQ(s.getAllBonuses(s.graph().roots().front(),selector())->front()->val,expected->front()->val);
}
TEST_F(EvaluationSnapshotTest, InvalidCaptureFactsAndNodeAreRejected)
{
 CBonusSystemNode node(BonusNodeType::GLOBAL_EFFECTS);
 EXPECT_THROW(BonusEvaluationSnapshot::capture({&node},{}),std::invalid_argument);
 EXPECT_THROW(BonusEvaluationSnapshot::capture({&node},[](const auto &n){auto f=facts(n);f.type=BonusNodeType::HERO;return f;}),std::invalid_argument);
 auto s=capture(node);EXPECT_THROW(s.getAllBonuses(999999,selector()),std::out_of_range);
}
TEST_F(EvaluationSnapshotTest, DiamondUpdaterAllocationsRemainDistinct)
{
 CBonusSystemNode root(BonusNodeType::GLOBAL_EFFECTS), a(BonusNodeType::GLOBAL_EFFECTS), b(BonusNodeType::GLOBAL_EFFECTS), leaf(BonusNodeType::GLOBAL_EFFECTS);
 auto value=bonus(BonusType::STACKS_SPEED,3);value->updater=std::make_shared<OwnerUpdater>();root.addNewBonus(value);
 a.attachTo(root);b.attachTo(root);leaf.attachTo(a);leaf.attachTo(b);parity(leaf);
 auto s=capture(leaf);auto result=s.getAllBonuses(s.graph().roots().front(),selector());ASSERT_EQ(result->size(),2);EXPECT_NE((*result)[0].get(),(*result)[1].get());
}
TEST_F(EvaluationSnapshotTest, DuplicateIdentityCountsInLimiterBeforeFinalDeduplication)
{
 CBonusSystemNode root(BonusNodeType::GLOBAL_EFFECTS), a(BonusNodeType::GLOBAL_EFFECTS), b(BonusNodeType::GLOBAL_EFFECTS), leaf(BonusNodeType::GLOBAL_EFFECTS);
 root.addNewBonus(bonus(BonusType::STACKS_SPEED,3));a.attachTo(root);b.attachTo(root);leaf.attachTo(a);leaf.attachTo(b);
 auto gated=bonus(BonusType::FLYING,1);auto limiter=std::make_shared<HasAnotherBonusLimiter>(BonusType::STACKS_SPEED);limiter->minValue=limiter->maxValue=6;gated->limiter=limiter;leaf.addNewBonus(gated);parity(leaf);
 auto s=capture(leaf);auto result=s.getAllBonuses(s.graph().roots().front(),selector());ASSERT_EQ(result->size(),2);
 EXPECT_EQ(s.getAllBonuses(s.graph().roots().front(),selector().And(Selector::type()(BonusType::FLYING)))->size(),1);
}
