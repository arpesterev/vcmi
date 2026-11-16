#include "StdInc.h"
#include "lib/bonuses/ClosedBonusCapture.h"
#include "lib/bonuses/CBonusSystemNode.h"
#include <atomic>
#include <thread>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<ClosedBonusCapture>);
static_assert(!std::is_default_constructible_v<ClosedBonusCapture::Slot>);
static_assert(std::is_same_v<decltype(std::declval<const ClosedBonusCapture &>().capture({}).snapshot.totalValue(0)), int>);

namespace
{
constexpr auto speed = BonusType::STACKS_SPEED;
using Value = ClosedBonusCapture::Value;
}

TEST(ClosedBonusCaptureTest, MatchesNativeScalarEvaluationAndQueryOrder)
{
 const std::vector<Value> values = {{speed, 17, BonusValueType::BASE_NUMBER}, {speed, -3},
  {speed, 11, BonusValueType::PERCENT_TO_BASE}, {speed, 7, BonusValueType::PERCENT_TO_ALL},
  {BonusType::FLYING, 1}};
 ClosedBonusCapture domain({values});
 CBonusSystemNode native(BonusNodeType::UNKNOWN);
 for(const auto & value : values)
 {
  auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, value.type, BonusSource::OTHER, value.value, BonusSourceID());
  bonus->valType = value.valueType;
  native.addNewBonus(bonus);
 }
 const auto capture = domain.capture({{0, speed, -9}, {0, BonusType::FLYING}, {0, speed, 9}});
 EXPECT_EQ(capture.revision, 0);
 EXPECT_EQ(capture.snapshot.size(), 3);
 EXPECT_EQ(capture.snapshot.totalValue(0), native.getAllBonuses(Selector::type()(speed))->totalValue(-9));
 EXPECT_EQ(capture.snapshot.totalValue(1), native.getAllBonuses(Selector::type()(BonusType::FLYING))->totalValue());
 EXPECT_EQ(capture.snapshot.totalValue(2), native.getAllBonuses(Selector::type()(speed))->totalValue(9));
 EXPECT_THROW(capture.snapshot.totalValue(3), std::out_of_range);
}

TEST(ClosedBonusCaptureTest, ConcurrentWriterCapturesHaveOneRevisionAndCoherentValues)
{
 ClosedBonusCapture domain({{{speed, 0}}, {{speed, 0}}});
 const auto first = domain.slot(0, 0), second = domain.slot(1, 0);
 std::atomic<bool> start{false}, valid{true};
 std::vector<std::thread> readers;
 for(int reader = 0; reader < 4; ++reader)
  readers.emplace_back([&]
  {
   while(!start.load()) std::this_thread::yield();
   for(int i = 0; i < 2000; ++i)
   {
    const auto result = domain.capture({{0, speed}, {1, speed}});
    const int expected = static_cast<int>(result.revision % 101);
    if(result.snapshot.totalValue(0) != expected || result.snapshot.totalValue(1) != -expected)
     valid.store(false);
   }
  });
 start.store(true);
 for(int i = 1; i <= 2000; ++i) domain.setValues({{first, i % 101}, {second, -(i % 101)}});
 for(auto & reader : readers) reader.join();
 EXPECT_TRUE(valid.load());
 EXPECT_EQ(domain.capture({}).revision, 2000);
}

TEST(ClosedBonusCaptureTest, RejectedBatchDoesNotPartiallyCommit)
{
 ClosedBonusCapture domain({{{speed, 5}, {speed, 7}}});
 ClosedBonusCapture foreign({{{speed, 9}}});
 const auto first = domain.slot(0, 0), second = domain.slot(0, 1);
 EXPECT_THROW(domain.setValues({{first, 20}, {foreign.slot(0, 0), 10}}), std::invalid_argument);
 EXPECT_THROW(domain.setValues({{first, 20}, {second, 101}}), std::invalid_argument);
 EXPECT_THROW(domain.setValues({{first, 20}, {first, 21}}), std::invalid_argument);
 domain.setValues({});
 const auto result = domain.capture({{0, speed}});
 EXPECT_EQ(result.revision, 0);
 EXPECT_EQ(result.snapshot.totalValue(0), 12);
}

TEST(ClosedBonusCaptureTest, ForkMintsIndependentProvenanceAndSnapshotsOutliveOwner)
{
 const auto snapshot = []
 {
  ClosedBonusCapture original({{{speed, 12}}});
  const auto slot = original.slot(0, 0);
  original.setValues({{slot, 13}});
  auto fork = original.fork();
  EXPECT_THROW(fork->setValues({{slot, 8}}), std::invalid_argument);
  const auto forkSlot = fork->slot(0, 0);
  EXPECT_THROW(original.setValues({{forkSlot, 8}}), std::invalid_argument);
  original.setValues({{slot, 15}});
  EXPECT_EQ(fork->capture({{0, speed}}).snapshot.totalValue(0), 13);
  fork->setValues({{forkSlot, 17}});
  EXPECT_EQ(original.capture({{0, speed}}).snapshot.totalValue(0), 15);
  return fork->capture({{0, speed}});
 }();
 EXPECT_EQ(snapshot.revision, 2);
 EXPECT_EQ(snapshot.snapshot.totalValue(0), 17);
 std::thread reader([snapshot] { EXPECT_EQ(snapshot.snapshot.totalValue(0), 17); });
 reader.join();
}

TEST(ClosedBonusCaptureTest, RejectsUnsupportedTypesAndExplicitResourceLimits)
{
 EXPECT_THROW(ClosedBonusCapture({{{speed, 101}}}), std::invalid_argument);
 EXPECT_THROW(ClosedBonusCapture({{{BonusType::NONE, 1}}}), std::invalid_argument);
 EXPECT_THROW(ClosedBonusCapture({{{speed, 1, BonusValueType::PERCENT_TO_SOURCE}}}), std::invalid_argument);
 EXPECT_THROW(ClosedBonusCapture({{{speed, 1, static_cast<BonusValueType>(255)}}}), std::invalid_argument);
 EXPECT_THROW(ClosedBonusCapture(std::vector<std::vector<Value>>(65)), std::length_error);
 EXPECT_THROW(ClosedBonusCapture({std::vector<Value>(17, {speed, 1})}), std::length_error);
 ClosedBonusCapture domain({{{speed, 1}}});
 EXPECT_THROW(domain.slot(1, 0), std::out_of_range);
 EXPECT_THROW(domain.slot(0, 1), std::out_of_range);
 EXPECT_THROW(domain.capture({{1, speed}}), std::out_of_range);
 EXPECT_THROW(domain.capture({{0, speed, 10001}}), std::invalid_argument);
 EXPECT_THROW(domain.capture({{0, BonusType::BUILTIN_BONUSES_COUNT}}), std::invalid_argument);
 EXPECT_THROW(domain.capture(std::vector<SealedBonusSnapshot::Query>(257, {0, speed})), std::length_error);
 EXPECT_EQ(domain.capture({}).snapshot.size(), 0);
}

TEST(ClosedBonusCaptureTest, SupportsIndependentBoundsAndCopiesInputValues)
{
 std::vector<std::vector<Value>> input = {{{speed, 20, BonusValueType::INDEPENDENT_MIN}, {speed, 10, BonusValueType::INDEPENDENT_MAX}, {speed, 15}}};
 ClosedBonusCapture domain(input);
 input[0][2].value = 99;
 CBonusSystemNode native(BonusNodeType::UNKNOWN);
 for(const auto & value : std::vector<Value>{{speed, 20, BonusValueType::INDEPENDENT_MIN}, {speed, 10, BonusValueType::INDEPENDENT_MAX}, {speed, 15}})
 {
  auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, value.type, BonusSource::OTHER, value.value, BonusSourceID());
  bonus->valType = value.valueType;
  native.addNewBonus(bonus);
 }
 EXPECT_EQ(domain.capture({{0, speed}}).snapshot.totalValue(0), native.getAllBonuses(Selector::type()(speed))->totalValue());
}

TEST(ClosedBonusCaptureTest, AcceptedExtremesMatchNativeListAcrossSupportedModes)
{
 const std::vector<BonusValueType> modes = {BonusValueType::ADDITIVE_VALUE, BonusValueType::BASE_NUMBER,
  BonusValueType::PERCENT_TO_BASE, BonusValueType::PERCENT_TO_ALL,
  BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX};
 for(size_t mode = 0; mode < modes.size(); ++mode)
  for(const bool mixed : {false, true})
   for(const int sign : {-1, 1})
   {
    std::vector<Value> values;
    BonusList native;
    for(size_t slot = 0; slot < ClosedBonusCapture::maxSlotsPerRoot; ++slot)
    {
     const auto valueType = modes.at(mixed ? (mode + slot) % modes.size() : mode);
     const int value = ClosedBonusCapture::maxMagnitude * sign * (mixed && slot % 2 ? -1 : 1);
     values.push_back({speed, value, valueType});
     auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, speed, BonusSource::OTHER, value, BonusSourceID());
     bonus->valType = valueType;
     native.push_back(bonus);
    }
    ClosedBonusCapture domain({values});
    for(const int base : {-ClosedBonusCapture::maxBaseMagnitude, ClosedBonusCapture::maxBaseMagnitude})
    {
     SCOPED_TRACE(::testing::Message() << "mode=" << mode << " mixed=" << mixed << " sign=" << sign << " base=" << base);
     EXPECT_EQ(domain.capture({{0, speed, base}}).snapshot.totalValue(0), native.totalValue(base));
    }
   }
}

TEST(ClosedBonusCaptureTest, ConcurrentForksPreserveCoherentRevisionsAndIndependentProvenance)
{
 ClosedBonusCapture domain({{{speed, 0}}, {{speed, 0}}});
 const auto first = domain.slot(0, 0), second = domain.slot(1, 0);
 std::atomic<bool> start{false}, valid{true};
 std::vector<std::thread> readers;
 for(int reader = 0; reader < 4; ++reader)
  readers.emplace_back([&]
  {
   while(!start.load()) std::this_thread::yield();
   for(int i = 0; i < 500; ++i)
   {
    auto fork = domain.fork();
    const auto captured = fork->capture({{0, speed}, {1, speed}});
    const int expected = static_cast<int>(captured.revision % 101);
    if(captured.snapshot.totalValue(0) != expected || captured.snapshot.totalValue(1) != -expected)
     valid.store(false);
    const auto forkFirst = fork->slot(0, 0), forkSecond = fork->slot(1, 0);
    EXPECT_THROW(fork->setValues({{first, 99}}), std::invalid_argument);
    EXPECT_THROW(domain.setValues({{forkFirst, 99}}), std::invalid_argument);
    fork->setValues({{forkFirst, -100}, {forkSecond, 100}});
    const auto changed = fork->capture({{0, speed}, {1, speed}});
    if(changed.revision != captured.revision + 1 || changed.snapshot.totalValue(0) != -100 || changed.snapshot.totalValue(1) != 100)
     valid.store(false);
    if(captured.snapshot.totalValue(0) != expected || captured.snapshot.totalValue(1) != -expected)
     valid.store(false);
    const auto live = domain.capture({{0, speed}, {1, speed}});
    const int liveExpected = static_cast<int>(live.revision % 101);
    if(live.snapshot.totalValue(0) != liveExpected || live.snapshot.totalValue(1) != -liveExpected)
     valid.store(false);
   }
  });
 start.store(true);
 for(int i = 1; i <= 2000; ++i) domain.setValues({{first, i % 101}, {second, -(i % 101)}});
 for(auto & reader : readers) reader.join();
 EXPECT_TRUE(valid.load());
 EXPECT_EQ(domain.capture({}).revision, 2000);
}
