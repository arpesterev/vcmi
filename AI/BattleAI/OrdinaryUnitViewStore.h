/* Preview-local store prototype. GPL-2.0-or-later. */
#pragma once
#include "StackWithBonuses.h"

/// Local unit roster and bonus graph shared by reads, writable aliases and acquired states.
/// Capture/fork/mutation require one serialized owner excluding cache-filling reads.
/// Ordinary captures borrow the caller-owned immutable native definitions.
class OrdinaryUnitViewStore final : public battle::IUnitEnvironment
{
 std::shared_ptr<HypotheticFrameArena> arena;
 mutable uint32_t nextId = 0xF00000;
 explicit OrdinaryUnitViewStore(std::shared_ptr<HypotheticFrameArena> value);
public:
 /// Write-exposed aliases only. Direct replacement/insertion by callers is unsupported.
 std::map<uint32_t, std::shared_ptr<StackWithBonuses>> stackStates;
 /// Caller retains all native graph/definition inputs and excludes every concurrent reader/writer.
 static std::shared_ptr<OrdinaryUnitViewStore> captureFixture(const BattleInfo & battle);
 static std::shared_ptr<OrdinaryUnitViewStore> captureOrdinary(const BattleInfo & battle);
 std::shared_ptr<OrdinaryUnitViewStore> fork() const;
 std::shared_ptr<StackWithBonuses> unit(uint32_t id) const;
 battle::Units units(const battle::UnitFilter & predicate) const;
 std::shared_ptr<StackWithBonuses> getForUpdate(uint32_t id);
 std::shared_ptr<StackWithBonuses> copyUnitState(uint32_t id, const battle::CUnitState & state);
 bool removeUnit(uint32_t id);
 void updateUnit(uint32_t id, const JsonNode & data, int64_t healthDelta);
 void addUnit(const battle::UnitInfo & info);
 void addUnit(uint32_t id, const JsonNode & data);
 void moveUnit(uint32_t id, const BattleHex & destination);
 void applyStackEffect(const SetStackEffect & effect);
 void applyStateUpdates(const CPackForClient & packet);
 void nextRound();
 void nextTurn(uint32_t id, BattleUnitTurnReason reason);
 int32_t revision() const;
 uint32_t nextUnitId() const;
 std::weak_ptr<const battle::IUnitEnvironment> environmentLease() const;
 const IBonusBearer * getBonusBearer() const;
 bool unitHasAmmoCart(const battle::Unit * value) const override;
 PlayerColor unitEffectiveOwner(const battle::Unit * value) const override;
};
