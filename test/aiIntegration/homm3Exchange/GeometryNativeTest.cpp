/* GPL-2.0-or-later. Actual native BattleHex API vs independent pinned source. */
#include "StdInc.h"
#include "../../../lib/battle/BattleHex.h"
#include "GeometryPinned.h"

TEST(HoMM3NativeSourceGeometry, everyAvailableHexPairMatchesPinnedDistanceAndThreshold)
{
 int pairs = 0, atTen = 0, atEleven = 0;
 HoMM3PinnedGeometry::combatManager source;
 for(int a = 0; a < GameConstants::BFIELD_SIZE; ++a)
 {
  const BattleHex from(a);
  if(!from.isAvailable()) continue;
  for(int b = 0; b < GameConstants::BFIELD_SIZE; ++b)
  {
   const BattleHex to(b);
   if(!to.isAvailable()) continue;
   const long expected = HoMM3PinnedGeometry::combatManager::getDistance(a, b);
   EXPECT_EQ(BattleHex::getDistance(from, to), expected) << "from=" << a << " to=" << b;
   HoMM3PinnedGeometry::army attacker, defender;
   attacker.m_gridIndex = a;
   defender.m_gridIndex = b;
   EXPECT_EQ(BattleHex::getDistance(from, to) > 10, source.shotIsNotOptimal(&attacker, &defender))
    << "from=" << a << " to=" << b;
   atTen += expected == 10;
   atEleven += expected == 11;
   ++pairs;
  }
 }
 EXPECT_EQ(pairs, 165 * 165);
 EXPECT_GT(atTen, 0);
 EXPECT_GT(atEleven, 0);
}

TEST(HoMM3NativeSourceGeometry, everyValidHexPairMatchesIncludingMargins)
{
 // Margins are not native troop landing cells, but retaining this comparison
 // documents equal indexing across all 187 source/native grid entries.
 int pairs = 0;
 for(int a = 0; a < GameConstants::BFIELD_SIZE; ++a)
  for(int b = 0; b < GameConstants::BFIELD_SIZE; ++b)
  {
   EXPECT_EQ(BattleHex::getDistance(BattleHex(a), BattleHex(b)),
    HoMM3PinnedGeometry::combatManager::getDistance(a, b)) << a << "," << b;
   ++pairs;
  }
 EXPECT_EQ(pairs, 187 * 187);
}

TEST(HoMM3NativeSourceGeometry, sourceExemptionsRequireExplicitArtifactOrCreatureFacts)
{
 // These verify the pinned oracle, not a native artifact-provenance adapter.
 HoMM3PinnedGeometry::combatManager source;
 HoMM3PinnedGeometry::army attacker, defender;
 attacker.m_gridIndex = 1;
 defender.m_gridIndex = 15;
 ASSERT_TRUE(source.shotIsNotOptimal(&attacker, &defender));
 HoMM3PinnedGeometry::hero controller;
 source.m_heroes[0] = &controller;
 controller.goldenBow = true;
 EXPECT_FALSE(source.shotIsNotOptimal(&attacker, &defender));
 controller.goldenBow = false;
 controller.sharpshooterBow = true;
 EXPECT_FALSE(source.shotIsNotOptimal(&attacker, &defender));
 controller.sharpshooterBow = false;
 attacker.m_creatureType = HoMM3PinnedGeometry::CREATURE_SHARPSHOOTER;
 EXPECT_FALSE(source.shotIsNotOptimal(&attacker, &defender));
 attacker.m_creatureType = HoMM3PinnedGeometry::CREATURE_ARROW_TOWER;
 EXPECT_FALSE(source.shotIsNotOptimal(&attacker, &defender));
}

TEST(HoMM3NativeSourceGeometry, wideSourceTailRulesAreNotHeadDistanceSubstitutions)
{
 // Wide creatures remain outside the ordinary native adapter domain. These
 // source-only witnesses prevent extending the narrow proof accidentally.
 HoMM3PinnedGeometry::combatManager source;
 HoMM3PinnedGeometry::army attacker, defender;
 attacker.m_gridIndex = 1; attacker.tail = 2; attacker.wide = true;
 defender.m_gridIndex = 12;
 EXPECT_EQ(HoMM3PinnedGeometry::combatManager::getDistance(1, 12), 11);
 EXPECT_FALSE(source.shotIsNotOptimal(&attacker, &defender));
 attacker.wide = false;
 defender.wide = true; defender.tail = 11;
 EXPECT_FALSE(source.shotIsNotOptimal(&attacker, &defender));
 defender.wide = false;
 EXPECT_TRUE(source.shotIsNotOptimal(&attacker, &defender));
}
