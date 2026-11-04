/*
 * nn_model_resolution.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "schema/base.h"
#include <exception>
#include <cstddef>
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
// V15 action nodes are generated for a particular active stack. If an
// event adapter becomes stale, a node may refer to a different creature.
// Runtime validation is required even in release builds (assert is disabled).
// Keep this check independent of the concrete graph types for regression tests.
template<typename ActionPtr, typename Stack>
inline void requireNNV15ActionActor(const ActionPtr & action, const Stack * activeStack)
{
	if(!action || !activeStack || !action->by || &action->by->cstack != activeStack)
		throw std::runtime_error("MMAI V15: model action has no matching active creature");
}


// A V15 ONNX graph action must still be active in the exact state used
// for server dispatch. The graph's legal-action mask is not a substitute
// for a release-build runtime check when asynchronous events invalidate it.
template<typename ActionPtr>
inline void requireNNV15ActiveAction(const ActionPtr & action)
{
	if(!action || !action->isActive)
		throw std::runtime_error("MMAI V15: model selected an inactive graph action");
}

// ACTION_RESET is a gym episode command, not a legal live battle move.
// The original adapter turns it into a hero retreat to reset training.
// V15 may legitimately select graph action ID 0, which numerically equals
// Schema::ACTION_RETREAT. Only the negative RESET sentinel is unambiguous.
// Refuse its translation only in launcher-selected strict neural combat.
inline void enforceStrictNNLiveBattleAction(bool strictNeural, int action)
{
	if(strictNeural && action == MMAI::Schema::ACTION_RESET)
		throw std::runtime_error("MMAI: neural model returned training reset during a live battle; refusing scripted retreat");
}

// The V13/V15 gym interfaces can ask for ANSI rendering repeatedly. Strict
// neural combat is not an interactive training renderer: a broken model that
// returns ACTION_RENDER_ANSI forever must not hang the battle server. Preserve
// upstream unlimited rendering for explicit non-strict gym/hybrid callers.
inline constexpr unsigned STRICT_NN_RENDER_REQUEST_LIMIT = 16;

inline void requireStrictNNRenderProgress(bool strictNeural, unsigned renderRequests)
{
	if(strictNeural && renderRequests > STRICT_NN_RENDER_REQUEST_LIMIT)
		throw std::runtime_error("MMAI: neural model repeatedly requested ANSI render without a battle action ("
			+ std::to_string(renderRequests) + " requests); refusing unbounded inference");
}

// A completed ONNX episode must return the gym terminal sentinel, never
// a regular graph action. The debug-only ASSERT in the battle-end adapters
// previously disappeared in release builds, silently accepting a broken
// model/event contract. This is a validation check, not a scripted action.
inline void requireNNModelTerminalReset(int action)
{
	if(action != MMAI::Schema::ACTION_RESET)
		throw std::runtime_error("MMAI: terminal ONNX observation returned action "
			+ std::to_string(action) + " instead of ACTION_RESET");
}

}
