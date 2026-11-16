/*
 * OwnedBattleBonusModel.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#pragma once
#include "BattleHexArray.h"
#include "OrdinaryPreviewDefinitionGuard.h"
#include "../bonuses/OwnedBonusGraphSession.h"
class BattleInfo;
namespace battle { class CUnitState; class UnitInfo; }

/// Owned bonus-context lifecycle, not a replacement for full battle unit state.
class DLL_LINKAGE OwnedBattleBonusModel
{
public:
 OwnedBattleBonusModel();
 ~OwnedBattleBonusModel();
 OwnedBattleBonusModel(const OwnedBattleBonusModel & other);
 OwnedBattleBonusModel & operator=(const OwnedBattleBonusModel & other);
 OwnedBattleBonusModel(OwnedBattleBonusModel &&) noexcept;
 OwnedBattleBonusModel & operator=(OwnedBattleBonusModel &&) noexcept;
 struct UnitFacts
 {
  uint32_t id;
  BonusGraphSnapshot::NodeId node;
  CreatureID creature;
  BattleSide side;
  PlayerColor originalOwner;
  bool doubleWide;
  bool ammoCart;
  int32_t count;
  int32_t cloneID;
  bool alive;
  bool ghost;
  bool ghostPending;
  bool validPosition;
  BattleHexArray hexes;
  BattleHexArray surrounding;
  bool operator==(const UnitFacts &) const = default;
 };
 /// Caller excludes native writes and cache-filling reads for the whole capture.
 static OwnedBattleBonusModel capture(const BattleInfo & battle, OwnedSpellPersistenceFacts persistence = {}, const std::vector<TConstBonusListPtr> & extraLists = {});
 /// Opt-in qualification; guard and model share the actual capture graph.
 static std::pair<OwnedBattleBonusModel, OrdinaryPreviewDefinitionGuard> captureForOrdinarySeed(const BattleInfo & battle, OwnedSpellPersistenceFacts persistence = {}, const std::vector<TConstBonusListPtr> & extraLists = {});
 const std::vector<UnitFacts> & units() const;
 const BonusEvaluationSnapshot & evaluation() const;
 /// Fresh diagnostic/value-model evaluation; never warms the observable cache bank.
 TConstBonusListPtr getAllBonuses(uint32_t unit, const CSelector & selector) const;
 /// Native-observable accepted/request caches, used by protocol replay and units.
 TConstBonusListPtr queryBonuses(uint32_t unit, const CSelector & selector, const std::string & key = "") const;
 int32_t nodeVersion(uint32_t unit) const;
 void invalidateUnit(uint32_t unit);
 void invalidateBattle();
 /// Publishes all native-ordered removal effects together; false means missing ID.
 bool removeUnit(uint32_t unit);
 void addUnit(const battle::UnitInfo & info);
 OwnedLogicalBonusEvent makeBonusEvent(const std::vector<Bonus> & values) const;
 OwnedLogicalBonusEvent eventFromUnitExports(uint32_t unit, const CSelector & selector) const;
 /// Each successful item commits in native order; unsupported failing item is atomic.
 bool applyBonusEvent(uint32_t unit, const OwnedLogicalBonusEvent & event, bool forceAdd);
 BonusGraphSnapshot::NodeId sideArmyNode(BattleSide side) const;
 /// Exact complete observations from the preview owner; no operation flags or hash admission.
 UnitFacts observeUnit(uint32_t id, const battle::CUnitState & state) const;
 bool synchronizeUnits(const std::vector<UnitFacts> & observations);
 uint64_t revision() const;
 PlayerColor unitEffectiveOwner(uint32_t id) const;
 bool unitHasAmmoCart(uint32_t id) const;
 PlayerColor ownerForUnit(BattleSide side, bool hypnotized) const;
 bool ammoForUnit(uint32_t id, BattleSide side, bool hypnotized) const;
 BonusGraphSnapshot::NodeId creatureNode(CreatureID creature) const;
private:
 friend class HypotheticFrameArena;
 friend class OrdinaryPreviewDefinitionGuardTestPeer;
 static OwnedBattleBonusModel captureImpl(const BattleInfo & battle, OwnedSpellPersistenceFacts persistence,
  const std::vector<TConstBonusListPtr> & extraLists, std::optional<OrdinaryPreviewDefinitionGuard> * qualification);
 std::vector<OwnedLogicalBonusEvent> importComponentEvents(const std::vector<std::vector<Bonus>> & groups) const;
 size_t bonusEventSize(const OwnedLogicalBonusEvent & event) const;
 void applyBonusEventItem(uint32_t unit, const OwnedLogicalBonusEvent & event, size_t index, bool forceAdd);
 bool expireBonuses(uint32_t unit, OwnedBonusExpiry event);
 bool removeNonpersistentSpells(uint32_t unit);
 bool removeBonuses(uint32_t unit, const CSelector & selector);
 bool removeBonusEventItem(uint32_t unit, const OwnedLogicalBonusEvent & event, size_t index);
 enum class CopyKind { Fork, Transaction };
 OwnedBattleBonusModel(const OwnedBattleBonusModel & other, CopyKind kind);
 OwnedBattleBonusModel transactionCopy() const;
 std::shared_ptr<OwnedBonusGraphSession> bonusSession;
 std::vector<UnitFacts> unitFacts;
 uint64_t stateRevision = 1;
 std::array<PlayerColor, 2> sidePlayers;
 std::array<bool, 2> artifactCarts{};
 std::map<CreatureID, BonusGraphSnapshot::NodeId> creatureNodes;
 std::map<CreatureID, std::pair<bool, bool>> creatureGeometryAndCart;
 std::array<std::optional<BonusGraphSnapshot::NodeId>, 2> sideArmyNodes;
 std::array<std::optional<BonusGraphSnapshot::NodeId>, 2> sideHeroNodes;
 TerrainId battleTerrain;
 BonusGraphSnapshot::NodeId battleNode = 0;
 void refreshUnitContexts();
 UnitFacts * find(uint32_t unit);
};
