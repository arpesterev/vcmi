/* GPL-2.0-or-later; see license.txt. */
#pragma once
namespace NK2AI
{
// Preserve the established scorer's fallback, including no-castle urgency.
inline float maxWillingArmyLoss(float configured, float powerRatio, bool withoutCastle)
{
	const float scaled = configured * powerRatio;
	return withoutCastle ? 1.0f : scaled > 0 ? scaled : 1.0f;
}
}
