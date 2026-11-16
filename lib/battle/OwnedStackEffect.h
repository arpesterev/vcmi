/* Owned effect packet. GPL-2.0-or-later. */
#pragma once
#include "../bonuses/OwnedBonusLogicalDomain.h"

/// Certified payloads; dispatch preserves native phase, recipient and item order.
struct DLL_LINKAGE OwnedStackEffect
{
 using Entry = std::pair<uint32_t, OwnedLogicalBonusEvent>;
 std::vector<Entry> toRemove;
 std::vector<Entry> toUpdate;
 std::vector<Entry> toAdd;
};

/// Fresh producer intent, not a converter for component-bearing native packets.
/// Equal indices share a new component; equal configurations at distinct indices do not.
struct DLL_LINKAGE OwnedFreshStackEffect
{
 struct Updater
 {
  int minimum = std::numeric_limits<int>::min();
  int maximum = std::numeric_limits<int>::max();
  int stepSize = 1;
  int stepValue = 1;
 };
 struct Limiter
 {
  uint32_t minimum = std::numeric_limits<uint32_t>::min();
  uint32_t maximum = std::numeric_limits<uint32_t>::max();
 };
 struct Value
 {
  Bonus scalar;
  std::optional<size_t> updater;
  std::optional<size_t> propagationUpdater;
  std::optional<size_t> limiter;
  std::optional<size_t> propagator;
 };
 using Entry = std::pair<uint32_t, std::vector<Value>>;
 BattleID battleID = BattleID::NONE;
 std::vector<Updater> updaters;
 std::vector<Limiter> limiters;
 std::vector<BonusNodeType> propagators;
 std::vector<Entry> toRemove;
 std::vector<Entry> toUpdate;
 std::vector<Entry> toAdd;
};
