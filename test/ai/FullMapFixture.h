/*
 * FullMapFixture.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "../mock/TinyH3MBuilder.h"
#include "../../lib/VCMIDirs.h"

/// Exports a playable map; all decisions and battles run in the ordinary client.
inline void exportFullMapFixture()
{
	const auto pikeman = CreatureID(CreatureID::decode("pikeman"));
	const auto archer = CreatureID(CreatureID::decode("archer"));
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name("AI full-map regression")
		.description("Two AI players, town development, resources and a guarded corridor artifact.")
		.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
		.playerFactions(PlayerColor(0), 1).playerFactions(PlayerColor(1), 1)
		.town({6, 8, 0}, FactionID::CASTLE, PlayerColor(0))
		.hero({8, 9, 0}, HeroTypeID(0), PlayerColor(0))
		.heroGarrison({{pikeman, 30}, {archer, 20}})
		.heroPrimary(2, 2, 1, 1).heroSecondarySkills({}).heroSpells({})
		.resource({9, 9, 0}, GameResID(GameResID::WOOD), 20)
		.resource({9, 10, 0}, GameResID(GameResID::ORE), 20)
		.town({30, 26, 0}, FactionID::CASTLE, PlayerColor(1))
		.hero({28, 28, 0}, HeroTypeID(1), PlayerColor(1))
		.heroGarrison({{pikeman, 30}, {archer, 20}})
		.heroPrimary(2, 2, 1, 1).heroSecondarySkills({}).heroSpells({})
		.resource({27, 28, 0}, GameResID(GameResID::WOOD), 20)
		.resource({27, 27, 0}, GameResID(GameResID::ORE), 20)
		.monster({17, 18, 0}, pikeman, 25, 4)
		.artifact({18, 18, 0}, ArtifactID(ArtifactID::decode("centaursAxe")));

	for(int y = 0; y < 36; ++y)
		if(y != 18)
		{
			builder.terrain({17, y, 0}, TerrainId(ETerrainId::ROCK));
			builder.terrain({18, y, 0}, TerrainId(ETerrainId::ROCK));
		}

	builder.buildAndDump("AI_FullMap_Regression");
	const auto path = VCMIDirs::get().userCachePath() / "testMaps" / "AI_FullMap_Regression.h3m";
	if(!boost::filesystem::exists(path))
		throw std::runtime_error("Full-map fixture was not exported");
	std::cout << "AI_MATCH_MAP=" << path.string() << std::endl;
}
