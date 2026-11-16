/* Owned logical Bonus values and event resolution. GPL-2.0-or-later. */
#pragma once
#include "OwnedBonusEventDomain.h"
class OwnedBonusLogicalDomain;
class DLL_LINKAGE OwnedBonusValueId final
{
 struct Identity;
 std::shared_ptr<const Identity> identity;
 explicit OwnedBonusValueId(std::shared_ptr<const Identity> value);
 friend class OwnedBonusLogicalDomain;
};
class DLL_LINKAGE OwnedLogicalBonusEvent final
{
 struct Storage;
 std::shared_ptr<const Storage> storage;
 explicit OwnedLogicalBonusEvent(std::shared_ptr<const Storage> value);
 friend class OwnedBonusLogicalDomain;
};
/// Exclusive writable branch. Value identity outlives graph membership.
/// No graph insertion/removal or producer migration is implemented here.
class DLL_LINKAGE OwnedBonusLogicalDomain final
{
 struct State;
 std::unique_ptr<State> state;
 explicit OwnedBonusLogicalDomain(std::unique_ptr<State> value);
 friend class OwnedBonusGraphSession;
 OwnedLogicalBonusEvent makeObservedEvent(const std::vector<Bonus> & values, const std::vector<std::shared_ptr<const Bonus>> & observations);
 std::vector<OwnedLogicalBonusEvent> importComponentEvents(const std::vector<std::vector<Bonus>> & groups);
 OwnedBonusLogicalDomain transactionCopy() const;
 std::vector<std::shared_ptr<const Bonus>> graphValues(const BonusGraphSnapshot & graph) const;
 void adoptAppendedGraph(const BonusGraphSnapshot & graph);
 OwnedBonusValueId internOwned(const std::shared_ptr<const Bonus> & value);
public:
 ~OwnedBonusLogicalDomain();
 OwnedBonusLogicalDomain(OwnedBonusLogicalDomain &&) noexcept;
 OwnedBonusLogicalDomain & operator=(OwnedBonusLogicalDomain &&) noexcept;
 OwnedBonusLogicalDomain(const OwnedBonusLogicalDomain &) = delete;
 OwnedBonusLogicalDomain & operator=(const OwnedBonusLogicalDomain &) = delete;
 static OwnedBonusLogicalDomain capture(const BonusGraphSnapshot & graph);
 OwnedBonusLogicalDomain fork() const;
 OwnedBonusValueId capturedValue(BonusGraphSnapshot::BonusId id) const;
 OwnedBonusValueId nestedValue(const OwnedBonusValueId & parent, size_t effectIndex) const;
 std::shared_ptr<const Bonus> value(const OwnedBonusValueId & id) const;
 OwnedBonusValueId defineScalar(const Bonus & value);
 void setTurnsRemain(const OwnedBonusValueId & id, int turns);
 OwnedLogicalBonusEvent fromValues(const std::vector<OwnedBonusValueId> & ids) const;
 OwnedLogicalBonusEvent makeEvent(const std::vector<Bonus> & values) const;
 bool accepts(const OwnedLogicalBonusEvent & event) const;
 std::vector<std::shared_ptr<const Bonus>> resolve(const OwnedLogicalBonusEvent & event) const;
 size_t retainedBonusCount() const;
 size_t retainedParameterCount() const;
};
