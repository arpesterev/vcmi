/* Owned detached unit proposal. GPL-2.0-or-later. */
#pragma once
#include "CUnitState.h"
#include "OwnedBattleBonusModel.h"
#include "OwnedCreature.h"

class HypotheticFrameArena;
class OwnedUnitEnvironmentSnapshot;

/// Native definitions follow the ordinary battle's library lifetime contract.
/// Fixture descriptors own their values and never expose a native definition.
class DLL_LINKAGE PreviewCreatureBinding final
{
 friend class HypotheticFrameArena;
 std::shared_ptr<const OwnedCreature> owned;
 const Creature * native = nullptr;
 explicit PreviewCreatureBinding(const Creature & creature) : native(&creature) {}
 static PreviewCreatureBinding borrow(const Creature & creature) { return PreviewCreatureBinding(creature); }
public:
 PreviewCreatureBinding(std::shared_ptr<const OwnedCreature> creature) : owned(std::move(creature))
 {
  if(!owned) throw std::invalid_argument("Missing owned preview creature");
 }
 const Creature * get() const { return owned ? owned.get() : native; }
 const Creature * operator->() const { return get(); }
 explicit operator bool() const { return get() != nullptr; }
 PreviewCreatureBinding rebind(std::shared_ptr<const OwnedCreatureContext> context) const
 { return owned ? PreviewCreatureBinding(owned->rebind(std::move(context))) : *this; }
};

class DLL_LINKAGE OwnedUnitEnvironmentHandle
{
 friend class HypotheticFrameArena;
 friend class OwnedUnitEnvironmentSnapshot;
 explicit OwnedUnitEnvironmentHandle(std::shared_ptr<const battle::IUnitEnvironment> environment);
 std::shared_ptr<const battle::IUnitEnvironment> environment;
public:
 OwnedUnitEnvironmentHandle(const OwnedUnitEnvironmentHandle &) = default;
 OwnedUnitEnvironmentHandle(OwnedUnitEnvironmentHandle &&) = default;
 OwnedUnitEnvironmentHandle & operator=(const OwnedUnitEnvironmentHandle &) = default;
 OwnedUnitEnvironmentHandle & operator=(OwnedUnitEnvironmentHandle &&) = default;
 const battle::IUnitEnvironment * get() const { return environment.get(); }
};

class DLL_LINKAGE OwnedUnitEnvironmentSnapshot final : public battle::IUnitEnvironment
{
 const OwnedBattleBonusModel model;
 explicit OwnedUnitEnvironmentSnapshot(OwnedBattleBonusModel model);
public:
 static OwnedUnitEnvironmentHandle capture(OwnedBattleBonusModel model);
 bool unitHasAmmoCart(const battle::Unit * unit) const override;
 PlayerColor unitEffectiveOwner(const battle::Unit * unit) const override;
};

namespace battle
{
class DLL_LINKAGE OwnedDetachedUnit final : public CUnitState
{
 struct Identity
 {
  int32_t baseAmount;
  uint32_t id;
  BattleSide side;
  PlayerColor owner;
  SlotID slot;
  int32_t level;
 };
 const Identity identity;
 const PreviewCreatureBinding creature;
 using FrozenBonuses = std::vector<std::shared_ptr<const Bonus>>;
 const std::shared_ptr<const FrozenBonuses> frozenBonuses;
 const OwnedUnitEnvironmentHandle environment;
 OwnedDetachedUnit(Identity identity, PreviewCreatureBinding creature,
  std::shared_ptr<const FrozenBonuses> frozenBonuses, OwnedUnitEnvironmentHandle environment);
public:
 // State, model and descriptor must describe the same protected capture boundary.
 static std::shared_ptr<OwnedDetachedUnit> capture(const CUnitState & state,
  OwnedBattleBonusModel model, uint32_t unitId, PreviewCreatureBinding creature,
  OwnedUnitEnvironmentHandle environment);
 OwnedDetachedUnit & operator=(const CUnitState & other);
 std::shared_ptr<Unit> acquire() const override;
 std::shared_ptr<CUnitState> acquireState() const override;
 int32_t unitBaseAmount() const override { return identity.baseAmount; }
 uint32_t unitId() const override { return identity.id; }
 BattleSide unitSide() const override { return identity.side; }
 PlayerColor unitOwner() const override { return identity.owner; }
 SlotID unitSlot() const override { return identity.slot; }
 int32_t unitLevel() const override { return identity.level; }
 const Creature * unitType() const override { return creature.get(); }
 TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & cache = "") const override;
 int32_t getTreeVersion() const override { return 1; }
 void spendMana(ServerCallback * server, int spellCost) const override;
};
}
