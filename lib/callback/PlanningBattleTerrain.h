/* GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../constants/EntityIdentifiers.h"
#include "../int3.h"

VCMI_LIB_NAMESPACE_BEGIN
class CGameInfoCallback;
// Visible neutral encounters only; unknown neighbours yield no terrain.
// Town native-terrain overrides are intentionally outside this query.
DLL_LINKAGE std::optional<TerrainId> getNeutralBattleTerrain(const CGameInfoCallback & visibleInfo, int3 pos);
VCMI_LIB_NAMESPACE_END
