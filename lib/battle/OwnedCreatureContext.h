/* Owned creature frame context. GPL-2.0-or-later. */
#pragma once
#include "OwnedCreature.h"

class HypotheticFrameArena;

/// Retains the last publication after the controller is destroyed. Serialized owner only.
class DLL_LINKAGE OwnedCreatureContext final
{
 friend class OwnedCreature;
 friend class OwnedCreatureContextController;
 std::shared_ptr<const OwnedCreatureSnapshot> current;
 explicit OwnedCreatureContext(std::shared_ptr<const OwnedCreatureSnapshot> snapshot);
};

/// Only the frame owner can publish. Views have no reference back to the owner.
class DLL_LINKAGE OwnedCreatureContextController final
{
 friend class HypotheticFrameArena;
 std::shared_ptr<OwnedCreatureContext> context;
 uint64_t modelRevision;
 class PreparedPublication final
 {
  friend class OwnedCreatureContextController;
  const OwnedCreatureContextController * owner;
  std::shared_ptr<OwnedCreatureContext> target;
  std::shared_ptr<const OwnedCreatureSnapshot> expected;
  std::shared_ptr<const OwnedCreatureSnapshot> next;
  uint64_t expectedRevision;
  uint64_t nextRevision;
  PreparedPublication(const OwnedCreatureContextController * owner, std::shared_ptr<OwnedCreatureContext> target,
   std::shared_ptr<const OwnedCreatureSnapshot> expected, std::shared_ptr<const OwnedCreatureSnapshot> next,
   uint64_t expectedRevision, uint64_t nextRevision) noexcept;
 public:
  PreparedPublication(const PreparedPublication &) = delete;
  PreparedPublication & operator=(const PreparedPublication &) = delete;
  PreparedPublication(PreparedPublication &&) noexcept = default;
  PreparedPublication & operator=(PreparedPublication &&) noexcept = default;
 };
 PreparedPublication prepare(BonusEvaluationSnapshot evaluation, uint64_t revision, std::shared_ptr<const OwnedBonusGraphSession> session = {}) const;
 void commit(PreparedPublication && publication) noexcept;
public:
 OwnedCreatureContextController(BonusEvaluationSnapshot evaluation, uint64_t revision, std::shared_ptr<const OwnedBonusGraphSession> session = {});
 OwnedCreatureContextController(const OwnedCreatureContextController &) = delete;
 OwnedCreatureContextController & operator=(const OwnedCreatureContextController &) = delete;
 OwnedCreatureContextController(OwnedCreatureContextController &&) noexcept = default;
 OwnedCreatureContextController & operator=(OwnedCreatureContextController &&) noexcept = default;
 std::shared_ptr<const OwnedCreatureContext> view() const;
};
