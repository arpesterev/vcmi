/*
 * BonusLimiterContext.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#pragma once
#include "Limiters.h"
#include "BonusList.h"

// A closed query description does not hand callers pointers into owned lists.
struct DLL_LINKAGE BonusLimiterQuery
{
 std::optional<BonusType> type;
 std::optional<BonusSubtypeID> subtype;
 std::optional<BonusSource> source;
 std::optional<BonusSourceID> sourceID;
};

class DLL_LINKAGE BonusLimiterContext
{
public:
 virtual ~BonusLimiterContext() = default;
 virtual BonusNodeType nodeType() const = 0;
 virtual PlayerColor owner() const = 0;
 virtual PlayerColor bonusOwner() const = 0;
 virtual BonusSource bonusSource() const = 0;
 virtual BonusSourceID bonusSourceID() const = 0;
 virtual bool hasCreature() const = 0;
 virtual CreatureID creatureID() const = 0;
 virtual bool creatureUpgradeOf(CreatureID ancestor) const = 0;
 virtual int32_t creatureLevel() const = 0;
 virtual EAlignment creatureAlignment() const = 0;
 virtual bool hasStackInstance() const = 0;
 virtual bool isCommander() const = 0;
 virtual int rank() const = 0;
 virtual bool hasBattleStack() const = 0;
 virtual BattleHexArray hexes() const = 0;
 virtual bool validPosition() const = 0;
 virtual bool adjacentTo(CreatureID target) const = 0;
 virtual ETerrainId currentTerrain() const = 0;
 virtual bool nativeTerrain(ETerrainId terrain) const = 0;
 virtual bool hasFaction() const = 0;
 virtual FactionID faction() const = 0;
 virtual FactionID sourceCreatureFaction() const = 0;
 virtual bool accepted(const BonusLimiterQuery & query) const = 0;
 virtual bool undecided(const BonusLimiterQuery & query) const = 0;
 virtual int acceptedValue(const BonusLimiterQuery & query) const = 0;
 virtual std::optional<int> firstAcceptedValue(const BonusLimiterQuery & query) const = 0;
};

class DLL_LINKAGE NativeBonusLimiterContext final : public BonusLimiterContext
{
 const BonusLimitationContext & context;
 mutable std::optional<const CCreature *> creature;
 const CCreature * getCreature() const;
public:
 explicit NativeBonusLimiterContext(const BonusLimitationContext & context);
 BonusNodeType nodeType() const override;
 PlayerColor owner() const override;
 PlayerColor bonusOwner() const override;
 BonusSource bonusSource() const override;
 BonusSourceID bonusSourceID() const override;
 bool hasCreature() const override;
 CreatureID creatureID() const override;
 bool creatureUpgradeOf(CreatureID ancestor) const override;
 int32_t creatureLevel() const override;
 EAlignment creatureAlignment() const override;
 bool hasStackInstance() const override;
 bool isCommander() const override;
 int rank() const override;
 bool hasBattleStack() const override;
 BattleHexArray hexes() const override;
 bool validPosition() const override;
 bool adjacentTo(CreatureID target) const override;
 ETerrainId currentTerrain() const override;
 bool nativeTerrain(ETerrainId terrain) const override;
 bool hasFaction() const override;
 FactionID faction() const override;
 FactionID sourceCreatureFaction() const override;
 bool accepted(const BonusLimiterQuery & query) const override;
 bool undecided(const BonusLimiterQuery & query) const override;
 int acceptedValue(const BonusLimiterQuery & query) const override;
 std::optional<int> firstAcceptedValue(const BonusLimiterQuery & query) const override;
};

class DLL_LINKAGE CapturedCreatureSourceFactionTable final
{
 const std::map<CreatureID, FactionID> values;
 explicit CapturedCreatureSourceFactionTable(const std::map<CreatureID, FactionID> & source);
public:
 static std::shared_ptr<const CapturedCreatureSourceFactionTable> capture(const std::map<CreatureID, FactionID> & source);
 FactionID at(CreatureID creature) const;
};

// The caller supplies definition-derived facts under a consistent read epoch.
// Missing required facts throw; absent capabilities remain NOT_APPLICABLE.
struct DLL_LINKAGE BonusLimiterFacts
{
 BonusNodeType type = BonusNodeType::GLOBAL_EFFECTS;
 std::optional<PlayerColor> originalOwner;
 bool creature = false;
 std::optional<CreatureID> creatureID;
 std::optional<std::set<CreatureID>> upgradeAncestors;
 std::optional<int32_t> creatureLevel;
 std::optional<EAlignment> creatureAlignment;
 bool stackInstance = false;
 std::optional<bool> commander;
 std::optional<int> rank;
 bool battleStack = false;
 std::optional<BattleHexArray> hexes;
 std::optional<bool> validPosition;
 std::optional<std::vector<CreatureID>> adjacentCreatures;
 std::optional<ETerrainId> currentTerrain;
 std::optional<std::set<ETerrainId>> nativeTerrains;
 bool factionProvider = false;
 std::optional<FactionID> faction;
 // Explicit caller facts override the immutable table captured from definitions.
 std::optional<std::map<CreatureID, FactionID>> creatureSourceFactions;
 std::shared_ptr<const CapturedCreatureSourceFactionTable> capturedCreatureSourceFactions;
};

class DLL_LINKAGE SnapshotBonusLimiterContext final : public BonusLimiterContext
{
 const BonusLimiterFacts facts;
 const PlayerColor subjectOwner;
 const BonusSource subjectSource;
 const BonusSourceID subjectSourceID;
 BonusList acceptedBonuses;
 BonusList undecidedBonuses;
public:
 SnapshotBonusLimiterContext(BonusLimiterFacts facts, const Bonus & bonus,
  const BonusList & accepted, const BonusList & undecided);
 BonusNodeType nodeType() const override;
 PlayerColor owner() const override;
 PlayerColor bonusOwner() const override;
 BonusSource bonusSource() const override;
 BonusSourceID bonusSourceID() const override;
 bool hasCreature() const override;
 CreatureID creatureID() const override;
 bool creatureUpgradeOf(CreatureID ancestor) const override;
 int32_t creatureLevel() const override;
 EAlignment creatureAlignment() const override;
 bool hasStackInstance() const override;
 bool isCommander() const override;
 int rank() const override;
 bool hasBattleStack() const override;
 BattleHexArray hexes() const override;
 bool validPosition() const override;
 bool adjacentTo(CreatureID target) const override;
 ETerrainId currentTerrain() const override;
 bool nativeTerrain(ETerrainId terrain) const override;
 bool hasFaction() const override;
 FactionID faction() const override;
 FactionID sourceCreatureFaction() const override;
 bool accepted(const BonusLimiterQuery & query) const override;
 bool undecided(const BonusLimiterQuery & query) const override;
 int acceptedValue(const BonusLimiterQuery & query) const override;
 std::optional<int> firstAcceptedValue(const BonusLimiterQuery & query) const override;
};

// Exact supported native types only; custom subclasses require their own adapter.
DLL_LINKAGE ILimiter::EDecision limitBonus(const ILimiter & limiter, const BonusLimiterContext & context);
