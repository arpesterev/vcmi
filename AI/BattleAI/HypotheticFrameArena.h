/* Owned hypothetical frame prototype. GPL-2.0-or-later. */
#pragma once
#include "../../lib/battle/OwnedDetachedUnit.h"
#include "StackWithBonuses.h"
#include "../../lib/battle/OwnedCreatureContext.h"
#include "../../lib/battle/BattleSide.h"
#include "../../lib/bonuses/OwnedBonusValueCopier.h"
#include <mutex>
class BattleInfo;

// Capture requires the caller to exclude all live session, graph and definition
// writes and cache-filling reads, including spell definitions, proxy caches, obstacle vectors and geometry definitions,
// and keeps those objects alive for the call.
// No callback, native stack, obstacle, spell definition or native environment is retained.
// captureOrdinary borrows only native Creature definitions for the library lifetime.
class HypotheticFrameArena final : public battle::IUnitEnvironment, public IBonusBearer,
 public std::enable_shared_from_this<HypotheticFrameArena>
{
 friend class HypotheticBattle;
 friend class OrdinaryPreviewDefinitionGuardTestPeer;
 friend class OrdinaryPreviewCacheAccessTestPeer;
 friend class OrdinaryUnitViewStore;
 mutable std::recursive_mutex accessMutex;
 bool ordinaryStoreMutators = false;
 bool nativeDefinitions = false;
 mutable OwnedBonusValueCopier ordinaryBonusCopies;
 TConstBonusListPtr queryUnit(uint32_t id, const CSelector & selector, const std::string & key) const;
 int32_t unitVersion(uint32_t id) const;
 void removeBonuses(uint32_t id, const CSelector & selector);
 void mutateLocalBonuses(uint32_t id, const std::vector<Bonus> & values, bool remove, bool forceAdd);
 // Only freshly constructed component configurations enter through this owner chain.
 std::vector<OwnedLogicalBonusEvent> importComponentEvents(const std::vector<std::vector<Bonus>> & groups) const;

 class FrameUnit;
 mutable OwnedBattleBonusModel model;
 mutable std::optional<OwnedCreatureContextController> definitionContext;
 mutable int32_t nativeDefinitionRevision = 1;
 struct DefinitionPublication
 {
  std::optional<OwnedCreatureContextController::PreparedPublication> owned;
  int32_t nativeRevision;
  const HypotheticFrameArena * owner;
  int32_t expectedNativeRevision;
 };
 DefinitionPublication prepareDefinitionPublication(const OwnedBattleBonusModel & candidate) const;
 void commitDefinitionPublication(DefinitionPublication && publication) const noexcept;
 void commitModel(OwnedBattleBonusModel candidate) const;
 std::map<CreatureID, PreviewCreatureBinding> definitions;
 std::vector<std::unique_ptr<FrameUnit>> storage;
 struct SideState
 {
  uint32_t castSpells = 0;
  int32_t enchanterCounter = 0;
  std::vector<SpellID> usedSpells;
 };
 BattleSideArray<SideState> sides;
 int32_t round = 0;
 uint8_t tacticDistance = 0;
 BattleSide tacticsSide = BattleSide::NONE;
 bool hasObstacles = false;
 std::shared_ptr<const BattleHexArray> blockedHexes;
 bool resurrectionBlocked(const FrameUnit & oldState) const;
 bool complete = false;
 enum class DefinitionMode { OwnedFixture, BorrowedNative };
 explicit HypotheticFrameArena(OwnedBattleBonusModel model, DefinitionMode definitions);
 static std::shared_ptr<HypotheticFrameArena> captureImpl(const BattleInfo & battle, std::optional<OrdinaryPreviewDefinitionGuard> * qualification, DefinitionMode definitions = DefinitionMode::OwnedFixture);
 void synchronize() const;
 FrameUnit * find(uint32_t id) const;
public:
 ~HypotheticFrameArena();
 static std::shared_ptr<HypotheticFrameArena> capture(const BattleInfo & battle);
 /// Captures local state under caller exclusion; original definitions remain borrowed.
 static std::shared_ptr<HypotheticFrameArena> captureOrdinary(const BattleInfo & battle);
 /// Preparatory opt-in route; native definitions in the guard remain borrowed.
 static std::pair<std::shared_ptr<HypotheticFrameArena>, OrdinaryPreviewDefinitionGuard> captureForOrdinarySeed(const BattleInfo & battle);
 std::shared_ptr<HypotheticFrameArena> fork() const;
 std::shared_ptr<StackWithBonuses> unit(uint32_t id);
 battle::Units units(const battle::UnitFilter & predicate) const;
 bool removeUnit(uint32_t id);
 void moveUnit(uint32_t id, const BattleHex & destination);
 void addUnit(const battle::UnitInfo & info);
 OwnedLogicalBonusEvent makeBonusEvent(const std::vector<Bonus> & values) const;
 OwnedLogicalBonusEvent eventFromUnitExports(uint32_t id, const CSelector & selector) const;
 void applyBonusEvent(uint32_t id, const OwnedLogicalBonusEvent & event, bool forceAdd);
 void expireBonuses(uint32_t id, OwnedBonusExpiry event);
 /// Atomic bounded produced-state packet updates, including native expiry phases.
 void applyStateUpdates(const CPackForClient & packet);
 void nextRound();
 /// Existing ordinary AI round/turn behavior, without fixture-only restrictions.
 void nextOrdinaryRound();
 void nextOrdinaryTurn(uint32_t id, BattleUnitTurnReason reason);
 void applyOrdinaryStackEffect(const SetStackEffect & effect);
 void removeBonusEvent(uint32_t id, const OwnedLogicalBonusEvent & event);
 void addUnit(uint32_t id, const JsonNode & data);
 int32_t revision() const;
 /// Battle-root queries use the current owned session and its native cache epoch.
 TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & key = {}) const override;
 int32_t getTreeVersion() const override;
 PlayerColor sidePlayer(BattleSide side) const;
 bool unitHasAmmoCart(const battle::Unit * unit) const override;
 PlayerColor unitEffectiveOwner(const battle::Unit * unit) const override;
};
