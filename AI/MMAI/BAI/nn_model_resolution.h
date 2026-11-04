/*
 * nn_model_resolution.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "schema/base.h"
#include <exception>
#include <map>
#include <stdexcept>
#include <string>

namespace MMAI::BAI
{
// Only unconfigured keys may inherit a parent model as a cache alias.
inline bool mayCacheResolvedNNModel(const std::map<std::string, std::string> & configuredPaths,
                                   const std::string & key, const std::string & resolvedPath)
{
	const auto configured = configuredPaths.find(key);
	return configured == configuredPaths.end() || configured->second == resolvedPath;
}

// Strict MMAI must never replace a broken configured ONNX model with a
// different neural policy; intentional hybrid fallback is still supported.
inline void enforceStrictNNModelLoad(bool strictNeural, const std::string & key,
                                     const std::string & configuredPath, const std::exception & cause)
{
	if(strictNeural)
		throw std::runtime_error("MMAI: configured neural model '" + key + "' (" + configuredPath
			+ ") failed to load: " + cause.what() + "; refusing substitute neural policy");
}
// ACTION_RESET is a gym episode command, not a legal live battle move.
// The original adapter turns it into a hero retreat to reset training.
// The published V13/V15 neural action spaces do not learn retreat choices.
// Refuse this translation only in launcher-selected strict neural combat.
inline void enforceStrictNNLiveBattleAction(bool strictNeural, int action)
{
	if(strictNeural && (action == MMAI::Schema::ACTION_RESET || action == MMAI::Schema::ACTION_RETREAT))
		throw std::runtime_error("MMAI: neural model returned training reset/retreat during a live battle; refusing scripted retreat");
}

}
