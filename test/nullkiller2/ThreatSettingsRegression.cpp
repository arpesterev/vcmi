/*
 * ThreatSettingsRegression.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "../StdInc.h"
#include "../../AI/Nullkiller2/Engine/Settings.h"
#include "../../lib/GameConstants.h"
#include "../../lib/constants/StringConstants.h"
#include "../../lib/json/JsonUtils.h"

VCMI_LIB_USING_NAMESPACE

namespace test
{
TEST(Nullkiller2ThreatSettingsRegression, LoadsThreatScanDistanceForEveryDifficulty)
{
	const auto config = JsonUtils::assembleFromFiles("config/ai/nk2ai/nk2ai-settings");
	for(int difficulty = 0; difficulty < 5; ++difficulty)
	{
		const auto & name = GameConstants::DIFFICULTY_NAMES[difficulty];
		SCOPED_TRACE(name);
		const NK2AI::Settings settings(difficulty);
		EXPECT_EQ(settings.getThreatTurnDistanceLimit(), config[name]["threatTurnDistanceLimit"].Integer());
	}
}
}
