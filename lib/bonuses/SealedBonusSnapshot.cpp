#include "StdInc.h"
#include "SealedBonusSnapshot.h"
#include "BonusEvaluationSnapshot.h"
#include "BonusList.h"

SealedBonusSnapshot::SealedBonusSnapshot(std::vector<int> results) : values(std::move(results)) {}

SealedBonusSnapshot SealedBonusSnapshot::capture(const std::vector<const CBonusSystemNode *> & roots, const std::vector<Query> & queries)
{
 for(const auto & query : queries)
  if(query.root >= roots.size()) throw std::out_of_range("Sealed bonus query root out of range");
 const auto evaluation = BonusEvaluationSnapshot::capture(roots);
 std::vector<int> results;
 results.reserve(queries.size());
 for(const auto & query : queries)
  results.push_back(evaluation.getAllBonuses(evaluation.graph().roots().at(query.root), Selector::type()(query.type))->totalValue(query.baseValue));
 return SealedBonusSnapshot(std::move(results));
}

size_t SealedBonusSnapshot::size() const { return values.size(); }
int SealedBonusSnapshot::totalValue(size_t query) const { return values.at(query); }
