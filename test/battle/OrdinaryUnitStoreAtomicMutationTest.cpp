/* Explicit prototype direct-mutator atomicity. GPL-2.0-or-later. */
#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "AI/BattleAI/OrdinaryUnitViewStore.h"

class OrdinaryUnitStoreAtomicMutationTest : public BattleTestFixture
{
public:
 void SetUp() override { BattleTestFixture::SetUp(); startGame(); startBattle(); }
 static Bonus effect(SpellID id, int turns)
 {
  Bonus result(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, 7, id);
  result.turnsRemain = turns; return result;
 }
 void rejectedBatch(bool update)
 {
  auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
  const auto haste = SpellID(SpellID::HASTE), slow = SpellID(SpellID::SLOW);
  if(update) source->addNewBonus(std::make_shared<Bonus>(effect(haste, 2)));
  auto store = OrdinaryUnitViewStore::captureFixture(*battle()); auto sibling = store->fork();
  auto retained = store->getForUpdate(source->unitId()); const auto state = retained->save();
  auto before = retained->getAllBonuses(Selector::all); std::vector<JsonNode> values;
  for(const auto & bonus : *before) values.push_back(bonus->toJsonNode());
  const auto epoch = retained->getTreeVersion(), rootEpoch = store->getBonusBearer()->getTreeVersion();
  const std::vector<Bonus> batch{effect(haste, 6), effect(slow, 0)};
  if(update) EXPECT_THROW(retained->updateUnitBonus(batch), std::invalid_argument);
  else EXPECT_THROW(retained->addUnitBonus(batch), std::invalid_argument);
  auto after = retained->getAllBonuses(Selector::all); ASSERT_EQ(after->size(), values.size());
  for(size_t i = 0; i < values.size(); ++i) EXPECT_EQ((*after)[i]->toJsonNode(), values[i]);
  EXPECT_EQ(retained->getTreeVersion(), epoch); EXPECT_EQ(store->getBonusBearer()->getTreeVersion(), rootEpoch);
  EXPECT_EQ(retained->save(), state); EXPECT_EQ(store->unit(source->unitId()).get(), retained.get());
  EXPECT_EQ(store->getForUpdate(source->unitId()).get(), retained.get());
  EXPECT_EQ(store->stackStates.at(source->unitId()).get(), retained.get());
  EXPECT_FALSE(retained->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, slow)));
  const auto localHaste = retained->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT, haste));
  if(update) { ASSERT_EQ(localHaste->size(), 1); EXPECT_EQ(localHaste->front()->turnsRemain, 2); }
  else EXPECT_TRUE(localHaste->empty());
  auto unchanged = sibling->unit(source->unitId())->getAllBonuses(Selector::all);
  auto native = source->getAllBonuses(Selector::all); ASSERT_EQ(unchanged->size(), values.size()); ASSERT_EQ(native->size(), values.size());
  for(size_t i = 0; i < values.size(); ++i)
  { EXPECT_EQ((*unchanged)[i]->toJsonNode(), values[i]); EXPECT_EQ((*native)[i]->toJsonNode(), values[i]); }
 }
};
TEST_F(OrdinaryUnitStoreAtomicMutationTest, AddRejectsLateInvalidEffectWithoutPublishingFirst)
{ rejectedBatch(false); }
TEST_F(OrdinaryUnitStoreAtomicMutationTest, UpdateRejectsLateInvalidEffectWithoutExtendingExistingDuration)
{ rejectedBatch(true); }
