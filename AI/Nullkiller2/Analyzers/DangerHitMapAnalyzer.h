/*
* DangerHitMapAnalyzer.h, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/
#pragma once

#include "../AIUtility.h"
#include "../Pathfinding/AIPathfinder.h"

namespace NK2AI
{

struct AIPath;

// Relative enemy urgency is discounted by movement, never made negative.
float discountedEnemyThreat(uint64_t strength, float movementCost);

struct HitMapInfo
{
	static const HitMapInfo NoThreat;

	uint64_t danger = 0;
	uint8_t turn = PathfinderSettings::MaxTurnDistanceLimit;
	float threat = 0.f;
	HeroPtr heroPtr = HeroPtr(nullptr, nullptr);

	double value() const
	{
		return danger / std::sqrt(turn / 3.0f + 1);
	}
};

struct HitMapNode
{
	// Nonnegative urgency; raw combat strength remains in danger.
	HitMapInfo maximumDanger = HitMapInfo::NoThreat;
	// Nonnegative urgency; raw combat strength remains in danger.
	HitMapInfo fastestDanger = HitMapInfo::NoThreat;
	const CGTownInstance * closestTown = nullptr;

	void reset()
	{
		maximumDanger = HitMapInfo::NoThreat;
		fastestDanger = HitMapInfo::NoThreat;
		closestTown = nullptr;
	}
};

// The last accessible tile before each planned change of turn.
std::vector<std::pair<int3, uint8_t>> collectPlannedOvernightStops(const CGPathNode * destination);

// Evaluates only already observed threats. Never predicts hidden enemy armies.
bool isKnownUnsafeAtEndOfTurn(const HitMapNode & tile, const CGHeroInstance * hero,
	const CCreatureSet * army, uint8_t arrivalTurn, float safeAttackRatio);

// Never blocks special-action or multi-carrier paths whose physical itinerary
// cannot be reproduced by the single-hero pathfinder.
bool hasUnsafeKnownOvernightStop(const AIPath & path, const Nullkiller * aiNk);

struct EnemyHeroAccessibleObject
{
	const CGHeroInstance * hero;
	const CGObjectInstance * obj;

	EnemyHeroAccessibleObject(const CGHeroInstance * hero, const CGObjectInstance * obj)
		:hero(hero), obj(obj)
	{
	}
};

class DangerHitMapAnalyzer
{
private:
	boost::multi_array<HitMapNode, 3> hitMap;
	tbb::concurrent_vector<EnemyHeroAccessibleObject> enemyHeroAccessibleObjects;
	bool hitMapUpToDate = false;
	bool tileOwnersUpToDate = false;
	const Nullkiller * aiNk;
	std::map<ObjectInstanceID, std::vector<HitMapInfo>> townThreats;

public:
	DangerHitMapAnalyzer(const Nullkiller * aiNk) :aiNk(aiNk) {}

	void updateHitMap();
	void calculateTileOwners();
	uint64_t enemyCanKillOurHeroesAlongThePath(const AIPath & path) const;
	const HitMapNode & getObjectThreat(const CGObjectInstance * obj) const;
	const HitMapNode & getTileThreat(const int3 & tile) const;
	std::set<const CGObjectInstance *> getOneTurnAccessibleObjects(const CGHeroInstance * enemy) const;
	void resetHitmap() {hitMapUpToDate = false;}
	bool isHitMapUpToDate() const { return hitMapUpToDate; }
	void resetTileOwners() { tileOwnersUpToDate = false; }
	bool isTileOwnersUpToDate() const { return tileOwnersUpToDate; }
	PlayerColor getTileOwner(const int3 & tile) const;
	const CGTownInstance * getClosestTown(const int3 & tile) const;
	const std::vector<HitMapInfo> & getTownThreats(const CGTownInstance * town) const;
};

}
