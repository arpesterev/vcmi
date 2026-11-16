/* Owned creature boundary proposal. GPL-2.0-or-later. */
#pragma once
#include <vcmi/Creature.h>
#include "../ResourceSet.h"
#include "../bonuses/BonusEvaluationSnapshot.h"
#include "../bonuses/IBonusBearer.h"

class OwnedBonusGraphSession;
class DLL_LINKAGE OwnedCreatureSnapshot final
{
 friend class OwnedCreature;
 friend class OwnedCreatureContextController;
 friend class OwnedCreatureContext;
 const BonusEvaluationSnapshot evaluation;
 const int32_t revision;
 std::shared_ptr<const OwnedBonusGraphSession> session;
 explicit OwnedCreatureSnapshot(BonusEvaluationSnapshot evaluation, int32_t revision = 1);
public:
 static std::shared_ptr<const OwnedCreatureSnapshot> create(BonusEvaluationSnapshot evaluation);
};

class OwnedCreatureContext;

class DLL_LINKAGE OwnedCreature final : public Creature, private IBonusBearer
{
public:
 struct Icon { int32_t index, group; std::string list, image; };
 struct Values
 {
  int32_t getIndex;
  int32_t getIconIndex;
  std::string getJsonKey;
  std::string getModScope;
  std::string getNamePluralTranslated;
  std::string getNameSingularTranslated;
  std::string getNamePluralTextID;
  std::string getNameSingularTextID;
  CreatureID getId;
  FactionID getFactionID;
  ArtifactID getWarMachine;
  int32_t getAdvMapAmountMin;
  int32_t getAdvMapAmountMax;
  int32_t getAIValue;
  int32_t getFightValue;
  int32_t getLevel;
  int32_t getGrowth;
  int32_t getHorde;
  int32_t getBaseAttack;
  int32_t getBaseDefense;
  int32_t getBaseDamageMin;
  int32_t getBaseDamageMax;
  int32_t getBaseHitPoints;
  int32_t getBaseSpellPoints;
  int32_t getBaseSpeed;
  int32_t getBaseShots;
  bool hasUpgrades;
  bool isDoubleWide;
  ResourceSet recruitCost;
  std::set<TerrainId> factionNativeTerrains;
  std::vector<Icon> icons;
 };
 // Capture all values and the evaluator in the same externally protected read epoch.
 static Values captureValues(const Creature & source, std::set<TerrainId> factionNativeTerrains);
 OwnedCreature(Values values, BonusEvaluationSnapshot snapshot, BonusEvaluationSnapshot::NodeId node);
 OwnedCreature(Values values, std::shared_ptr<const OwnedCreatureSnapshot> snapshot, BonusEvaluationSnapshot::NodeId node);
 // Frame-bound base stats read current raw creature exports; frozen constructors retain captured scalars.
 OwnedCreature(Values values, std::shared_ptr<const OwnedCreatureContext> context, BonusEvaluationSnapshot::NodeId node);
 std::shared_ptr<const OwnedCreature> rebind(std::shared_ptr<const OwnedCreatureContext> context) const;
 // Retains the same view without retaining a frame or its unit environment.
 std::shared_ptr<const OwnedCreature> retain() const;
 int32_t getIndex() const override { return values.getIndex; }
 int32_t getIconIndex() const override { return values.getIconIndex; }
 std::string getJsonKey() const override { return values.getJsonKey; }
 std::string getModScope() const override { return values.getModScope; }
 std::string getNamePluralTranslated() const override { return values.getNamePluralTranslated; }
 std::string getNameSingularTranslated() const override { return values.getNameSingularTranslated; }
 std::string getNamePluralTextID() const override { return values.getNamePluralTextID; }
 std::string getNameSingularTextID() const override { return values.getNameSingularTextID; }
 CreatureID getId() const override { return values.getId; }
 FactionID getFactionID() const override { return values.getFactionID; }
 ArtifactID getWarMachine() const override { return values.getWarMachine; }
 int32_t getAdvMapAmountMin() const override { return values.getAdvMapAmountMin; }
 int32_t getAdvMapAmountMax() const override { return values.getAdvMapAmountMax; }
 int32_t getAIValue() const override { return values.getAIValue; }
 int32_t getFightValue() const override { return values.getFightValue; }
 int32_t getLevel() const override { return values.getLevel; }
 int32_t getGrowth() const override { return values.getGrowth; }
 int32_t getHorde() const override { return values.getHorde; }
 int32_t getBaseAttack() const override;
 int32_t getBaseDefense() const override;
 int32_t getBaseDamageMin() const override;
 int32_t getBaseDamageMax() const override;
 int32_t getBaseHitPoints() const override;
 int32_t getBaseSpellPoints() const override;
 int32_t getBaseSpeed() const override;
 int32_t getBaseShots() const override;
 bool hasUpgrades() const override { return values.hasUpgrades; }
 bool isDoubleWide() const override { return values.isDoubleWide; }
 int32_t getRecruitCost(GameResID resource) const override;
 const ResourceSet & getFullRecruitCost() const override { return values.recruitCost; }
 void registerIcons(const IconRegistar & callback) const override;
 bool isNativeTerrain(TerrainId terrain) const override;
 // Source-following definition observer: fresh values and publication revision.
 // Does not read or warm the native cache bank, including named queries.
 const IBonusBearer * getBonusBearer() const override { return this; }
 // Explicit native observable view: named request cache and per-node epoch.
 // Retains the owned definition context, not a frame or borrowed native bearer.
 // Requires a frame-bound session; snapshot-only definitions have no native bank.
 std::shared_ptr<const IBonusBearer> retainNativeBonusBearer() const;
private:
 class NativeBonusBearer;
 std::string getNameTranslated() const override { return values.getNameSingularTranslated; }
 std::string getNameTextID() const override { return values.getNameSingularTextID; }
 TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & cache = "") const override;
 int32_t getTreeVersion() const override;
 std::shared_ptr<const OwnedCreatureSnapshot> pin() const;
 int32_t rawBaseValue(const CSelector & selector) const;
 const Values values;
 const std::shared_ptr<const OwnedCreatureSnapshot> snapshot;
 const std::shared_ptr<const OwnedCreatureContext> context;
 const BonusEvaluationSnapshot::NodeId node;
};
