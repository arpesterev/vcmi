#include "StdInc.h"
#include "ClosedBonusCapture.h"
#include "BonusList.h"
#include <mutex>
#include <set>

struct ClosedBonusCapture::State
{
 mutable std::mutex mutex;
 const std::shared_ptr<const Identity> identity = std::make_shared<const Identity>();
 std::vector<std::vector<Value>> values;
 std::vector<BonusList> lists;
 uint64_t revision = 0;
};

namespace
{
void validateValue(int value)
{
 if(value < -ClosedBonusCapture::maxMagnitude || value > ClosedBonusCapture::maxMagnitude)
  throw std::invalid_argument("Closed bonus value exceeds scalar bound");
}
void validateType(BonusType type)
{
 if(type <= BonusType::NONE || type >= BonusType::BUILTIN_BONUSES_COUNT)
  throw std::invalid_argument("Closed bonus type is not a built-in bonus");
}
void validateValueType(BonusValueType type)
{
 switch(type)
 {
 case BonusValueType::ADDITIVE_VALUE:
 case BonusValueType::BASE_NUMBER:
 case BonusValueType::PERCENT_TO_BASE:
 case BonusValueType::PERCENT_TO_ALL:
 case BonusValueType::INDEPENDENT_MIN:
 case BonusValueType::INDEPENDENT_MAX: return;
 default: throw std::invalid_argument("Closed bonus source-dependent value type is unsupported");
 }
}
}

ClosedBonusCapture::ClosedBonusCapture(std::vector<std::vector<Value>> roots) : state(std::make_unique<State>())
{
 if(roots.size() > maxRoots) throw std::length_error("Too many closed bonus roots");
 state->lists.resize(roots.size());
 for(size_t r = 0; r < roots.size(); ++r)
 {
  if(roots[r].size() > maxSlotsPerRoot) throw std::length_error("Too many closed bonus slots");
  for(const auto & value : roots[r])
  {
   validateType(value.type);
   validateValue(value.value);
   validateValueType(value.valueType);
   auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, value.type, BonusSource::OTHER, value.value, BonusSourceID());
   bonus->valType = value.valueType;
   state->lists[r].push_back(bonus);
  }
 }
 state->values = std::move(roots);
}
ClosedBonusCapture::~ClosedBonusCapture() = default;

ClosedBonusCapture::Slot ClosedBonusCapture::slot(size_t root, size_t index) const
{
 std::lock_guard lock(state->mutex);
 (void)state->values.at(root).at(index);
 return Slot(state->identity, root, index);
}

void ClosedBonusCapture::setValues(std::vector<Change> changes)
{
 std::lock_guard lock(state->mutex);
 if(changes.size() > maxRoots * maxSlotsPerRoot) throw std::length_error("Too many closed bonus changes");
 std::set<std::pair<size_t, size_t>> seen;
 for(const auto & change : changes)
 {
  if(change.slot.owner != state->identity) throw std::invalid_argument("Foreign closed bonus slot");
  validateValue(change.value);
  if(!seen.emplace(change.slot.root, change.slot.index).second) throw std::invalid_argument("Duplicate closed bonus slot");
 }
 if(changes.empty()) return;
 if(state->revision == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Closed bonus revision exhausted");
 for(const auto & change : changes)
 {
  state->values.at(change.slot.root).at(change.slot.index).value = change.value;
  state->lists.at(change.slot.root)[change.slot.index]->val = change.value;
 }
 ++state->revision;
}

ClosedBonusCapture::Capture ClosedBonusCapture::capture(std::vector<SealedBonusSnapshot::Query> queries) const
{
 std::lock_guard lock(state->mutex);
 if(queries.size() > maxQueries) throw std::length_error("Too many closed bonus queries");
 for(const auto & query : queries)
 {
  if(query.root >= state->lists.size()) throw std::out_of_range("Closed bonus query root out of range");
  validateType(query.type);
  if(query.baseValue < -maxBaseMagnitude || query.baseValue > maxBaseMagnitude)
   throw std::invalid_argument("Closed bonus base exceeds scalar bound");
 }
 std::vector<int> values;
 values.reserve(queries.size());
 for(const auto & query : queries)
  values.push_back(state->lists.at(query.root).valOfBonuses(Selector::type()(query.type), query.baseValue));
 return {state->revision, SealedBonusSnapshot(std::move(values))};
}

std::unique_ptr<ClosedBonusCapture> ClosedBonusCapture::fork() const
{
 std::lock_guard lock(state->mutex);
 auto result = std::make_unique<ClosedBonusCapture>(state->values);
 result->state->revision = state->revision;
 return result;
}
