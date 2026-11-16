/* Typed immutable bonus events. GPL-2.0-or-later. */
#pragma once
#include "BonusGraphSnapshot.h"
class OwnedBonusEventDomain;
class DLL_LINKAGE OwnedBonusEvent final
{
 struct Storage;
 std::shared_ptr<const Storage> storage;
 explicit OwnedBonusEvent(std::shared_ptr<const Storage> storage);
 friend class OwnedBonusEventDomain;
public:
 const std::vector<std::shared_ptr<const Bonus>> & values() const;
};

/// Copying retains one branch capability; fork() creates a distinct child.
/// No constructor admits an arbitrary native component or mutable shared closure.
class DLL_LINKAGE OwnedBonusEventDomain final
{
 friend class OwnedBonusLogicalDomain;
 OwnedBonusEvent makeObservedEvent(const std::vector<Bonus> & values, const std::vector<std::shared_ptr<const Bonus>> & observations) const;
 struct State;
 std::shared_ptr<const State> state;
 explicit OwnedBonusEventDomain(std::shared_ptr<const State> state);
public:
 static OwnedBonusEventDomain capture(const BonusGraphSnapshot & graph);
 OwnedBonusEventDomain fork() const;
 OwnedBonusEvent fromCaptured(const std::vector<BonusGraphSnapshot::BonusId> & ids) const;
 /// Scalars can vary; non-null components must belong to this captured owned closure.
 /// This is not an importer for externally-created native components.
 OwnedBonusEvent makeEvent(const std::vector<Bonus> & values) const;
 bool accepts(const OwnedBonusEvent & event) const;
};
