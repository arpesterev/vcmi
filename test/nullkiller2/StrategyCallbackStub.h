/*
 * StrategyCallbackStub.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "../../lib/callback/CCallback.h"
#include "../../lib/GameSettings.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/ResourceSet.h"

VCMI_LIB_USING_NAMESPACE

namespace test
{

// No CGameState is needed: every state query reached by these fixtures is
// overridden. Unanticipated real-state accesses must fail, not silently pass.
class StrategyCallbackStub final : public CCallback
{
public:
	using CCallback::getResourceAmount;

	std::vector<const CGTownInstance *> towns;
	std::vector<const CGHeroInstance *> heroes;
	TResources resources;
	int dayOfWeek = 7;
	std::unique_ptr<GameSettings> settings;

	StrategyCallbackStub()
		: CCallback(nullptr, std::nullopt, nullptr)
	{
		setAccumulation(false);
	}

	void setAccumulation(bool enabled)
	{
		settings = std::make_unique<GameSettings>();
		JsonNode value;
		value.Bool() = enabled;
		settings->addOverride(EGameSettings::DWELLINGS_ACCUMULATE_WHEN_OWNED, value);
	}

	const IGameSettings & getSettings() const override { return *settings; }
	int getDate(Date mode) const override
	{
		EXPECT_EQ(mode, Date::DAY_OF_WEEK);
		return dayOfWeek;
	}

	PlayerRelations getPlayerRelations(PlayerColor, PlayerColor) const override
	{
		return PlayerRelations::ENEMIES;
	}

	int3 getMapSize() const override { return int3(2, 1, 1); }
	std::vector<const CGTownInstance *> getTownsInfo(bool = true) const override { return towns; }
	std::vector<const CGHeroInstance *> getHeroesInfo() const override { return heroes; }
	TResources getResourceAmount() const override { return resources; }
};

}
