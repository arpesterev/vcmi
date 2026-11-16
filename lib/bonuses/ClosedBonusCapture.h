/* Synchronized component-free bonus domain. GPL-2.0-or-later. */
#pragma once
#include "SealedBonusSnapshot.h"
#include <memory>
#include <vector>

/// Independent scalar roots only. No native graph or component ingress.
class DLL_LINKAGE ClosedBonusCapture final
{
 struct Identity {};
 struct State;
 std::unique_ptr<State> state;
public:
 struct Value
 {
  BonusType type;
  int value;
  BonusValueType valueType = BonusValueType::ADDITIVE_VALUE;
 };
 class Slot final
 {
  friend class ClosedBonusCapture;
  std::shared_ptr<const Identity> owner;
  size_t root;
  size_t index;
  Slot(std::shared_ptr<const Identity> domain, size_t r, size_t i) : owner(std::move(domain)), root(r), index(i) {}
 };
 struct Change { Slot slot; int value; };
 struct Capture { uint64_t revision; SealedBonusSnapshot snapshot; };
 static constexpr size_t maxRoots = 64;
 static constexpr size_t maxSlotsPerRoot = 16;
 static constexpr size_t maxQueries = 256;
 static constexpr int maxMagnitude = 100;
 static constexpr int maxBaseMagnitude = 10000;
 explicit ClosedBonusCapture(std::vector<std::vector<Value>> roots);
 ~ClosedBonusCapture();
 ClosedBonusCapture(const ClosedBonusCapture &) = delete;
 ClosedBonusCapture & operator=(const ClosedBonusCapture &) = delete;
 Slot slot(size_t root, size_t index) const;
 /// Validates the entire batch before committing; duplicate slots are rejected.
 void setValues(std::vector<Change> changes);
 Capture capture(std::vector<SealedBonusSnapshot::Query> queries) const;
 std::unique_ptr<ClosedBonusCapture> fork() const;
};
