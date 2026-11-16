#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "lib/bonuses/SealedBonusSnapshot.h"
#include "lib/bonuses/BonusEvaluationSnapshot.h"
#include "lib/bonuses/Updaters.h"
#include <future>
#include <atomic>
#include <thread>
#include <type_traits>

static_assert(std::is_same_v<decltype(std::declval<const SealedBonusSnapshot &>().totalValue(0)), int>);
static_assert(std::is_copy_constructible_v<SealedBonusSnapshot>);
static_assert(!std::is_copy_assignable_v<SealedBonusSnapshot>);
static_assert(!std::is_default_constructible_v<SealedBonusSnapshot>);

class SealedBonusSnapshotTest : public BattleTestFixture
{
public:
 void SetUp() override { BattleTestFixture::SetUp(); startGame(); startBattle(); }
 CStack * add() { return addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5,5), 10); }
};

TEST_F(SealedBonusSnapshotTest, ConcurrentReadersRemainIndependentOfLiveValuesAndUpdaterConfiguration)
{
 auto * stack = add();
 auto updater = std::make_shared<TimesStackSizeUpdater>();
 auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 3, BonusSourceID());
 bonus->updater = updater;
 stack->addNewBonus(bonus);
 const auto snapshot = SealedBonusSnapshot::capture({stack}, {{0, BonusType::STACKS_SPEED}});
 const auto expected = stack->getAllBonuses(Selector::type()(BonusType::STACKS_SPEED))->totalValue();
 ASSERT_EQ(snapshot.totalValue(0), expected);
 std::atomic<bool> start{false};
 std::vector<std::future<bool>> readers;
 for(int i = 0; i < 4; ++i)
  readers.push_back(std::async(std::launch::async, [&, copy = snapshot]
  {
   while(!start.load()) std::this_thread::yield();
   for(int j = 0; j < 100; ++j)
    if(copy.totalValue(0) != expected || snapshot.totalValue(0) != expected) return false;
   return true;
  }));
 start.store(true);
 for(int i = 0; i < 100; ++i)
 {
  bonus->val = i;
  *updater = TimesStackSizeUpdater(0, 1000, 1, i + 2);
 }
 for(auto & reader : readers) EXPECT_TRUE(reader.get());
 EXPECT_EQ(snapshot.totalValue(0), expected);
}

TEST_F(SealedBonusSnapshotTest, RejectsInvalidQueriesAndPreservesRequestedOrder)
{
 auto * stack = add();
 const auto snapshot = SealedBonusSnapshot::capture({stack}, {{0, BonusType::STACKS_SPEED, 7}, {0, BonusType::FLYING}});
 EXPECT_EQ(snapshot.size(), 2);
 EXPECT_EQ(snapshot.totalValue(0), stack->getAllBonuses(Selector::type()(BonusType::STACKS_SPEED))->totalValue(7));
 EXPECT_EQ(snapshot.totalValue(1), stack->getAllBonuses(Selector::type()(BonusType::FLYING))->totalValue());
 EXPECT_THROW(snapshot.totalValue(2), std::out_of_range);
 EXPECT_THROW(SealedBonusSnapshot::capture({stack}, {{1, BonusType::STACKS_SPEED}}), std::out_of_range);
 EXPECT_EQ(SealedBonusSnapshot::capture({stack}, {}).size(), 0);
}

TEST_F(SealedBonusSnapshotTest, ReadersOutliveDestroyedSourceGraphAndComponents)
{
 const auto snapshot = []
 {
  CBonusSystemNode node(BonusNodeType::UNKNOWN);
  auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 19, BonusSourceID());
  node.addNewBonus(bonus);
  return SealedBonusSnapshot::capture({&node}, {{0, BonusType::STACKS_SPEED}});
 }();
 std::vector<std::future<int>> readers;
 for(int i = 0; i < 4; ++i)
  readers.push_back(std::async(std::launch::async, [copy = snapshot] { return copy.totalValue(0); }));
 for(auto & reader : readers) EXPECT_EQ(reader.get(), 19);
}

TEST_F(SealedBonusSnapshotTest, RejectsUnsupportedLiveComponentsBeforePublication)
{
 class UnsupportedUpdater final : public TimesStackSizeUpdater {};
 auto * stack = add();
 auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 3, BonusSourceID());
 bonus->updater = std::make_shared<UnsupportedUpdater>();
 stack->addNewBonus(bonus);
 EXPECT_THROW(SealedBonusSnapshot::capture({stack}, {{0, BonusType::STACKS_SPEED}}), std::runtime_error);
}

namespace
{
struct RaiseMinimum
{
 int field = 0;
 void operator &(IUpdater &) {}
 void operator &(int & value) { if(field++ == 0) value = 50; }
};
}

TEST_F(SealedBonusSnapshotTest, NativeSerializerMutationDemonstratesIdentityIsolationConflict)
{
 auto * stack = add();
 auto updater = std::make_shared<TimesStackSizeUpdater>();
 auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 3, BonusSourceID());
 bonus->updater = updater;
 stack->addNewBonus(bonus);
 const auto evaluation = BonusEvaluationSnapshot::capture({stack});
 const auto root = evaluation.graph().roots().at(0);
 const auto selected = Selector::type()(BonusType::STACKS_SPEED).And(Selector::sourceType()(BonusSource::OTHER));
 const auto before = evaluation.getAllBonuses(root, selected);
 ASSERT_EQ(before->size(), 1);
 ASSERT_EQ(before->front()->val, 10);
 EXPECT_NE(before->front()->updater.get(), updater.get());
 RaiseMinimum write;
 updater->serialize(write);
 stack->nodeHasChanged();
 EXPECT_EQ(stack->getAllBonuses(selected)->front()->val, 50);
 EXPECT_EQ(evaluation.getAllBonuses(root, selected)->front()->val, 10);
}
