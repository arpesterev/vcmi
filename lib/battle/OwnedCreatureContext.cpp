/* Owned creature frame context. GPL-2.0-or-later. */
#include "StdInc.h"
#include "OwnedCreatureContext.h"

OwnedCreatureContext::OwnedCreatureContext(std::shared_ptr<const OwnedCreatureSnapshot> snapshot)
 : current(std::move(snapshot)) {}

OwnedCreatureContextController::OwnedCreatureContextController(BonusEvaluationSnapshot evaluation, uint64_t revision, std::shared_ptr<const OwnedBonusGraphSession> session)
 : modelRevision(revision)
{
 if(!revision || !evaluation.graph().sameOrigin(evaluation.graph()))
  throw std::invalid_argument("Owned creature context requires a captured evaluation and nonzero revision");
 auto snapshot = std::shared_ptr<OwnedCreatureSnapshot>(new OwnedCreatureSnapshot(std::move(evaluation)));
 snapshot->session = std::move(session);
 context = std::shared_ptr<OwnedCreatureContext>(new OwnedCreatureContext(std::move(snapshot)));
}

std::shared_ptr<const OwnedCreatureContext> OwnedCreatureContextController::view() const
{
 if(!context)
  throw std::logic_error("Moved-from owned creature controller");
 return context;
}

OwnedCreatureContextController::PreparedPublication OwnedCreatureContextController::prepare(BonusEvaluationSnapshot evaluation, uint64_t revision, std::shared_ptr<const OwnedBonusGraphSession> session) const
{
 if(!context)
  throw std::logic_error("Moved-from owned creature controller");
 const auto previous = context->current;
 const auto & original = previous->evaluation.graph();
 const auto & replacement = evaluation.graph();
 if(!original.sameOrigin(replacement) || revision < modelRevision)
  throw std::invalid_argument("Owned creature publication requires the same capture and monotonic revision");
 if(replacement.nodes().size() < original.nodes().size())
  throw std::invalid_argument("Owned creature publication removed captured nodes");
 for(size_t node = 0; node < original.nodes().size(); ++node)
  if(original.nodes().at(node).type != replacement.nodes().at(node).type
   || original.nodes().at(node).hypothetical != replacement.nodes().at(node).hypothetical)
   throw std::invalid_argument("Owned creature publication changed captured node kind");
 if(revision == modelRevision && session == previous->session)
  return PreparedPublication(this, context, previous, previous, modelRevision, revision);
 if(previous->revision == std::numeric_limits<int32_t>::max())
  throw std::overflow_error("Owned creature cache revision exhausted");
 auto next = std::shared_ptr<OwnedCreatureSnapshot>(new OwnedCreatureSnapshot(std::move(evaluation), previous->revision + 1));
 next->session = std::move(session);
 return PreparedPublication(this, context, previous, std::move(next), modelRevision, revision);
}

OwnedCreatureContextController::PreparedPublication::PreparedPublication(
 const OwnedCreatureContextController * owner, std::shared_ptr<OwnedCreatureContext> target,
 std::shared_ptr<const OwnedCreatureSnapshot> expected, std::shared_ptr<const OwnedCreatureSnapshot> next,
 uint64_t expectedRevision, uint64_t nextRevision) noexcept
 : owner(owner), target(std::move(target)), expected(std::move(expected)), next(std::move(next)),
 expectedRevision(expectedRevision), nextRevision(nextRevision) {}

void OwnedCreatureContextController::commit(PreparedPublication && publication) noexcept
{
 // Moving the controller or committing another publication invalidates an outstanding token.
 if(publication.owner != this || publication.target != context || !publication.next
  || publication.expected != context->current || publication.expectedRevision != modelRevision)
  std::terminate();
 context->current.swap(publication.next);
 modelRevision = publication.nextRevision;
 publication.owner = nullptr;
}
