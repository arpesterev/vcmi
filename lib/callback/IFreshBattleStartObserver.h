/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#pragma once
#include "../constants/EntityIdentifiers.h"
#include <optional>

/// Optional provenance channel, NOT a replay of the pre-state battleStartBefore callback.
class DLL_LINKAGE IFreshBattleStartObserver
{
public:
	virtual ~IFreshBattleStartObserver() = default;
	virtual void onFreshBattleStart(const BattleID & battleID) = 0;
};

/// Transient, one-use pairing for the engine's monotonically allocated BattleIDs.
/// No serialized state and no reconstruction from a loaded battle snapshot.
class FreshBattleStartToken
{
	std::optional<BattleID> pending;
	std::optional<BattleID> highestObserved;
public:
	void before(const BattleID & id, bool alreadyInBattle)
	{
		pending.reset();
		if(alreadyInBattle || (highestObserved && id <= *highestObserved))
			return;
		highestObserved = id;
		pending = id;
	}
	bool consume(const BattleID & id)
	{
		const bool matched = pending && *pending == id;
		pending.reset();
		if(!highestObserved || *highestObserved < id)
			highestObserved = id;
		return matched;
	}
	void cancel() { pending.reset(); }
};
