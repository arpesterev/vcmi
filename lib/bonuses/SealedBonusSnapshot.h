/* Sealed scalar snapshot reads. GPL-2.0-or-later. */
#pragma once
#include "BonusEnum.h"

class CBonusSystemNode;

/// Capture requires the caller's consistent native read epoch through completion.
/// Published copies contain only immutable integers and support concurrent reads.
class DLL_LINKAGE SealedBonusSnapshot final
{
 friend class ClosedBonusCapture;
 const std::vector<int> values;
 explicit SealedBonusSnapshot(std::vector<int> results);
public:
 struct Query
 {
  size_t root;
  BonusType type;
  int baseValue = 0;
 };
 static SealedBonusSnapshot capture(const std::vector<const CBonusSystemNode *> & roots, const std::vector<Query> & queries);
 size_t size() const;
 int totalValue(size_t query) const;
};
