/* Owned bonus value transactions. GPL-2.0-or-later. */
#pragma once
#include "BonusGraphSnapshot.h"

/// Values originate only in a deeply owned graph. Component configuration stays
/// immutable and keeps its identity; mutable Bonus/parameter values are copied
/// with one memo across roots and recursive combat payloads.
class DLL_LINKAGE OwnedBonusValues final
{
 std::vector<std::shared_ptr<const Bonus>> roots;
 explicit OwnedBonusValues(std::vector<std::shared_ptr<const Bonus>> values);
public:
 using BonusId = BonusGraphSnapshot::BonusId;
 static OwnedBonusValues capture(const BonusGraphSnapshot & graph);
 const std::vector<std::shared_ptr<const Bonus>> & bonuses() const;
 OwnedBonusValues withTurnsRemain(BonusId id, int turns) const;
 OwnedBonusValues withTurnsRemain(const std::vector<std::pair<BonusId, int>> & changes) const;
 /// Returned mutable roots are private to this call, including nested aliases.
 /// New parameters are NOT automatically certified by an event domain.
 std::vector<std::shared_ptr<Bonus>> query(const std::vector<BonusId> & ids) const;
};
