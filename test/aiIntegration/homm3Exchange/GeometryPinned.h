/* CC0-1.0 source reconstruction; GPL-2.0-or-later native test harness.
 * Minimal input-only stubs around exact source bodies; no native distance call.
 * The source grid stride and count are pinned in GeometrySourceManifest.json.
 */
#pragma once
#include <algorithm>
#include <cstdlib>
namespace HoMM3PinnedGeometry
{
using std::abs;
using std::max;
enum { ARTIFACT_GOLDEN_BOW = 1, ARTIFACT_BOW_OF_THE_SHARPSHOOTER = 2,
 CREATURE_ARROW_TOWER = 3, CREATURE_SHARPSHOOTER = 4, creatureDoubleWide = 1 };
struct hero
{
 bool goldenBow = false, sharpshooterBow = false;
 bool isWieldingArtifact(int id) const
 { return id == ARTIFACT_GOLDEN_BOW ? goldenBow : id == ARTIFACT_BOW_OF_THE_SHARPSHOOTER && sharpshooterBow; }
};
struct army
{
 int side = 0, m_creatureType = 0, m_gridIndex = 0, tail = 0;
 bool wide = false;
 int getControllingSide() const { return side; }
 bool is(int flag) const { return flag == creatureDoubleWide && wide; }
 int getSecondGridIndex() const { return tail; }
};
struct combatManager
{
 const hero * m_heroes[2] = {nullptr, nullptr};
 static int gridX(int index) { return index % 0x11; }
 static int gridY(int index) { return index / 0x11; }
 static long getDistance(long start, long stop);
 bool shotIsNotOptimal(const army * attacker, const army * defender) const;
};
#include "GeometrySourceBodies.inc"
}
