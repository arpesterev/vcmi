/*
 * SetStackEffect.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "NetPacksBase.h"

#include "../bonuses/Bonus.h"

class IBattleState;
class HypotheticFrameArena;
namespace scripting::api { struct BonusDescriptor; }

struct DLL_LINKAGE SetStackEffect : public CPackForClient
{
	BattleID battleID = BattleID::NONE;
	std::vector<std::pair<ui32, std::vector<Bonus>>> toAdd;
	std::vector<std::pair<ui32, std::vector<Bonus>>> toUpdate;
	std::vector<std::pair<ui32, std::vector<Bonus>>> toRemove;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		if(!h.saving)
			descriptorEffect.reset();
		h & battleID;
		h & toAdd;
		h & toUpdate;
		h & toRemove;
		assert(battleID != BattleID::NONE);
	}
private:
	struct DescriptorEffect
	{
		BattleID battleID;
		ui32 unit;
		bool cumulative;
		Bonus value;
		std::vector<std::byte> configuration;
	};
	// Local producer proof is not part of the network/save format.
	std::shared_ptr<const DescriptorEffect> descriptorEffect;
	static std::vector<std::byte> snapshotDescriptorValue(const Bonus & value);
	friend struct scripting::api::BonusDescriptor;
	friend class HypotheticFrameArena;
};
