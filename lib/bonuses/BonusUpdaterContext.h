/*
 * BonusUpdaterContext.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "BonusEnum.h"
#include "../constants/EntityIdentifiers.h"

class CBonusSystemNode;

struct DLL_LINKAGE BonusUpdaterArmyStack
{
	CreatureID creature;
	FactionID faction;
	int level;
	int count;

	CreatureID getCreatureID() const { return creature; }
	FactionID getFactionID() const { return faction; }
	int getLevel() const { return level; }
	int getCount() const { return count; }
};

/// Facts read by bonus updaters, separate from bonus graph storage.
class DLL_LINKAGE BonusUpdaterContext
{
public:
	virtual ~BonusUpdaterContext() = default;
	virtual BonusNodeType nodeType() const = 0;
	virtual PlayerColor owner() const = 0;
	virtual int heroLevel() const = 0;
	virtual int stackCount() const = 0;
	virtual int stackLevel() const = 0;
	virtual std::vector<BonusUpdaterArmyStack> armyStacks() const = 0;
	virtual int armySize(CreatureID creature, FactionID faction, int level) const = 0;
};

class DLL_LINKAGE NativeBonusUpdaterContext final : public BonusUpdaterContext
{
	const CBonusSystemNode & node;
public:
	explicit NativeBonusUpdaterContext(const CBonusSystemNode & node);
	BonusNodeType nodeType() const override;
	PlayerColor owner() const override;
	int heroLevel() const override;
	int stackCount() const override;
	int stackLevel() const override;
	std::vector<BonusUpdaterArmyStack> armyStacks() const override;
	int armySize(CreatureID creature, FactionID faction, int level) const override;
};

/// Owns updater input facts; it does not capture a bonus graph or unit state.
class DLL_LINKAGE SnapshotBonusUpdaterContext final : public BonusUpdaterContext
{
	friend class OwnedBattleBonusModel;
	const BonusNodeType type;
	const PlayerColor originalOwner;
	std::optional<int> levelOfHero;
	std::optional<int> countOfStack;
	std::optional<int> levelOfStack;
	std::optional<std::vector<BonusUpdaterArmyStack>> army;
public:
	explicit SnapshotBonusUpdaterContext(const BonusUpdaterContext & source);
	BonusNodeType nodeType() const override;
	PlayerColor owner() const override;
	int heroLevel() const override;
	int stackCount() const override;
	int stackLevel() const override;
	std::vector<BonusUpdaterArmyStack> armyStacks() const override;
	int armySize(CreatureID creature, FactionID faction, int level) const override;
};
