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
// A legacy hybrid policy may invoke BattleEvaluator only for a still-living
// actor. In particular, a failed activeStack event must never send a null/dead
// actor into scripted fallback, and strict neural policy never uses it at all.
template<typename Stack>
inline bool canNNUseLegacyBattleFallback(bool strictNeural, const Stack * activeStack)
{
	return !strictNeural && activeStack && activeStack->alive();
}

// A V15 activeStack event may refer to a creature eliminated by a previous
// battle callback. Such an event must not enter ONNX inference, automatic
// war-machine dispatch, or action conversion. This check is also repeated
// after inference, since action graph nodes retain the original stack.
template<typename Stack>
inline void requireNNV15LivingActiveStack(const Stack * activeStack)
{
	if(!activeStack || !activeStack->alive())
		throw std::runtime_error("MMAI V15: active creature is missing or no longer alive");
}

// V15 action nodes are generated for a particular active stack. If an
// event adapter becomes stale, a node may refer to a different creature.
// Runtime validation is required even in release builds (assert is disabled).
// Keep this check independent of the concrete graph types for regression tests.
template<typename ActionPtr, typename Stack>
inline void requireNNV15ActionActor(const ActionPtr & action, const Stack * activeStack)
{
	requireNNV15LivingActiveStack(activeStack);
	if(!action || !action->by || &action->by->cstack != activeStack)
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

// V15 MOVE graph actions are legal only while their destination is still
// reachable by the active creature in the current engine battle state.
// FastBFS builds the ONNX action graph, but the VCMI server owns the final
// movement rules (walls, gates, obstacles, speed and double-wide stacks).
// Never silently substitute a different movement or scripted policy.
template<typename Hex, typename Reachability, typename Distance>
inline void requireNNV15LegalMove(const Hex & destination, const Reachability & reachability, Distance movementRange)
{
	if(!destination.isAvailable() || !reachability.isReachable(destination) ||
	   reachability.distances[destination.toInt()] > movementRange)
		throw std::runtime_error("MMAI V15: model MOVE destination is not reachable this turn");
}

// A V15 AMOVE destination is an ONNX snapshot too. Even if its actor and
// target remain alive, live speed, gates or obstacles can invalidate the
// selected melee approach. Never resample or replace the ONNX destination.
template<typename Hex, typename Reachability, typename Distance>
inline void requireNNV15LegalMeleeApproach(const Hex & destination, const Reachability & reachability, Distance movementRange)
{
	if(!destination.isAvailable() || !reachability.isReachable(destination) ||
	   reachability.distances[destination.toInt()] > movementRange)
		throw std::runtime_error("MMAI V15: model AMOVE approach is not reachable this turn");
}

// V13's hex MOVE mask is also an observation snapshot. Revalidate the
// model-selected destination against the live server reachability immediately
// before dispatch, without replacing the policy's chosen hex. A MOVE onto
// the active creature's own position is DEFEND and is handled separately.
template<typename Hex, typename Reachability, typename Distance>
inline void requireNNV13LegalMove(const Hex & destination, const Reachability & reachability, Distance movementRange)
{
	if(!destination.isAvailable() || !reachability.isReachable(destination) ||
	   reachability.distances[destination.toInt()] > movementRange)
		throw std::runtime_error("MMAI V13: model MOVE destination is not reachable this turn");
}

// V13/V15 tower shots are engine-owned actions, not ONNX predictions.
// Select the highest-value enemy that is still alive and can actually be
// shot according to the current VCMI battle callback. An empty/blocked
// target set must produce no target, never dereference an end iterator.
template<typename Targets, typename IsLegal, typename Value>
inline auto selectLegalNNWarMachineTarget(const Targets & targets, IsLegal isLegal, Value value)
{
	using Target = typename Targets::value_type;
	Target best = nullptr;
	for(const auto & candidate : targets)
	{
		if(!candidate || !candidate->alive() || !isLegal(candidate))
			continue;
		if(!best || value(candidate) > value(best))
			best = candidate;
	}
	return best;
}

// V13 also handles engine-owned military machines before building its neural
// observation. Validate the event actor first: a dead or missing machine must
// neither dispatch a server action nor reach ONNX. This does not make machine
// choices neural predictions.
template<typename Stack>
inline void requireNNV13LivingActiveStack(const Stack * activeStack)
{
	if(!activeStack || !activeStack->alive())
		throw std::runtime_error("MMAI V13: active creature is missing or no longer alive");
}

// V13 reconstructs an action from a cached Battlefield observation. Its
// active-stack wrapper must still refer to the creature delivered by the
// current VCMI activeStack event. Otherwise a valid ONNX hex/action mask can
// dispatch a command for a different or already destroyed actor.
// Validate both before inference and again immediately before conversion.
template<typename ObservedStack, typename Stack>
inline void requireNNV13ActionActor(const ObservedStack * observed, const Stack * activeStack)
{
	if(!observed || !activeStack || observed->cstack != activeStack || !activeStack->alive())
		throw std::runtime_error("MMAI V13: model action has no matching living active creature");
}

// A V15 graph action refers to a concrete creature, not only to a hex.
// A target may disappear between the graph observation and engine dispatch;
// checking the shooting/melee hex alone cannot establish that this specific
// stack is still alive. Fail closed without resampling or scripted fallback.
// LegalCheck must query the live VCMI battle callback, not the graph mask.
template<typename Stack, typename LegalCheck>
inline void requireNNV15LegalTarget(const Stack * target, LegalCheck legalCheck, const char * action)
{
	if(!target || !target->alive() || !legalCheck(target))
		throw std::runtime_error(std::string("MMAI V15: model ") + action + " has no legal live target");
}

// V15 remembers the model-selected target's head hex, not its later mutable
// CStack position. Reject moved/replaced targets instead of redirecting the
// neural SHOOT/AMOVE to a different unit or location.
template<typename Stack, typename Hex, typename LiveLookup>
inline void requireNNV15ObservedTargetAtHex(const Stack * target, const Hex & observedHex,
                                           LiveLookup liveAtHex, const char * action)
{
	if(!target || !target->alive() || !(target->getPosition() == observedHex) || liveAtHex(observedHex) != target)
		throw std::runtime_error(std::string("MMAI V15: model ") + action
			+ " target no longer occupies its observed hex");
}

// V13/V15 decisions are bound to the actor position observed by ONNX.
// A later event can move the living actor; never reinterpret a model choice
// from its new hex. The occupant lookup is the live VCMI battle callback.
template<typename Stack, typename Hex, typename LiveLookup>
inline void requireNNObservedActorAtHex(const Stack * actor, const Hex & observedHex,
                                        LiveLookup liveAtHex, const char * version)
{
	if(!actor || !actor->alive() || !(actor->getPosition() == observedHex) || liveAtHex(observedHex) != actor)
		throw std::runtime_error(std::string("MMAI ") + version
			+ ": model actor no longer occupies its observed hex");
}

// V13 SHOOT encodes the selected target through an observation hex.
// Re-check that exact hex at dispatch: a still-living observed stack might
// have moved elsewhere, and reconstructing a shot using its *new* position
// would silently redirect a neural decision to a different location.
// This is an identity check, not a replacement target-selection policy.
template<typename Target>
inline void requireNNV13SelectedShootHexTarget(const Target * observedTarget, const Target * liveTargetAtSelectedHex)
{
	if(!observedTarget || liveTargetAtSelectedHex != observedTarget)
		throw std::runtime_error("MMAI V13: model SHOOT selected hex no longer contains the observed target");
}

// V13 AMOVE encodes the attacker's destination and an adjacent target hex.
// A replacement creature at the latter hex may also be legally attackable,
// but is not the one selected by the ONNX observation. This applies to
// normal and double-wide melee attacks alike.
template<typename Target>
inline void requireNNV13SelectedMeleeHexTarget(const Target * observedTarget, const Target * liveTargetAtSelectedHex)
{
	if(!observedTarget || liveTargetAtSelectedHex != observedTarget)
		throw std::runtime_error("MMAI V13: model AMOVE target hex no longer contains the observed target");
}

// V13's battle action masks are derived from a preceding observation.
// They cannot guarantee that a target is still alive and legal at dispatch.
// A stale target must be rejected before consulting the live VCMI callback,
// even in release builds where the original ASSERT checks are compiled out.
template<typename Target, typename LegalCheck>
inline void requireNNV13LegalTarget(const Target * target, LegalCheck legalCheck, const char * action)
{
	if(!target || !target->alive() || !legalCheck(target))
		throw std::runtime_error(std::string("MMAI V13: model ") + action + " has no legal live target");
}

// The V13 ONNX schema contains an explicit RETREAT action. The original
// adapter rejected model-selected retreat even when VCMI allowed it, while
// converting ACTION_RESET to retreat as a gym episode control. Preserve gym
// reset behavior for legacy callers, but allow a strict neural decision to
// retreat only when the live battle engine permits fleeing. No scripted
// surrender/retreat callback or replacement policy is involved.
inline bool canNNV13DispatchRetreat(bool strictNeural, bool trainingReset, bool engineCanFlee)
{
	return trainingReset || (strictNeural && engineCanFlee);
}

// V13 historically re-sampled an invalid or stale model-selected action up
// to eleven times. In strict combat this silently changes the single ONNX
// decision, unlike V15's fail-closed path. Reject on the first invalid
// decoded action; retain upstream retry behavior for explicit hybrid/gym use.
inline void enforceNNV13SingleLiveAction(bool strictNeural, bool legalAction, int action)
{
	if(strictNeural && !legalAction)
		throw std::runtime_error("MMAI V13: invalid live neural action " + std::to_string(action)
			+ "; refusing resampling or scripted fallback");
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
