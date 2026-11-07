/*
 * FHeroes2DispelValuation.h, experimental VCMI source-policy projection.
 * GPL-2.0-or-later.
 *
 * fheroes2@0dbfe9f386f33c2500842f4a59cd284d7f03d541
 * src/fheroes2/ai/ai_battle_spell.cpp, BattlePlanner::spellDispelValue.
 */
#pragma once
#include <cmath>
#include <stdexcept>

namespace AIIntegration::Ports::FHeroes2
{
// Source spellDispelValue: removing harmful effects from our troops OR
// beneficial effects from enemies is useful. Removing the opposite type
// costs tactical value. This is the sign for an ACTUALLY removed effect;
// source-specific spell weights are determined separately by caller.
inline double dispelledEffectValue(double projectedStrength, double effectRatio,
	bool friendlyStack, bool harmfulEffect, bool actuallyRemoved)
{
	if(!std::isfinite(projectedStrength) || projectedStrength < 0
		|| !std::isfinite(effectRatio) || effectRatio < 0)
		throw std::invalid_argument("FHeroes2 Dispel requires nonnegative finite weights");
	if(!actuallyRemoved)
		return 0;
	const bool helpful = friendlyStack == harmfulEffect;
	return (helpful ? 1.0 : -1.0) * projectedStrength * effectRatio;
}
// Hypnotize changes effective ownership. Charge the source 1.5 control ratio
// only when removing the native effect actually transfers the stack.
inline double dispelledHypnotizeValue(double projectedStrength,
	bool friendlyBefore, bool friendlyAfter)
{
	if(!std::isfinite(projectedStrength) || projectedStrength < 0)
		throw std::invalid_argument("FHeroes2 Hypnotize requires finite nonnegative strength");
	if(friendlyBefore == friendlyAfter)
		return 0;
	return (friendlyAfter ? 1.5 : -1.5) * projectedStrength;
}

}
