# Wide cautious advance: implementation pending native validation

Base: `5d93910aa515133ac3a3993ed7ef9ae6fada5f0f` from arpesterev/vcmi develop.
Source inspected at `ihhub/fheroes2@0dbfe9f386f33c2500842f4a59cd284d7f03d541`.

## Change

`FHeroes2BattleAI::approach` admits ordinary ground two-cell actors into the
existing source incoming-threat path-risk branch when every enemy is single-cell
and there is no defended town. It tests `stack.getSurroundingHexes(step)`, the
native perimeter of both cells, excluding the projected footprint itself.
Previously any wide actor forced the geometric/raw-strength fallback.
Unsupported ordinary-threat contexts, wide enemies and siege retain that fallback.
Flying actors still use the existing earlier flight branch.

Pinned `src/fheroes2/ai/ai_battle.cpp:319-343` checks reachable positions around
the complete candidate footprint with next-turn speed. Lines 399-465 build
reachable route positions, omit flyers/unengaged archers, use CURRENT-position
threat and prefer farther progress on a 0.001 tie. `battle_board.cpp:793` expands
the perimeter of wide positions. These are the source policy being adapted.

VCMI `Unit::getSurroundingHexes` supplies native side-dependent geometry.
`CBattleInfoCallback::makeBFS` checks accessibility with `params.doubleWide` at
each step; `isInObstacle` also visits both occupied hexes. Neither the H2 board
nor its orientation/reversal rules are imported. This is not H2/H3 path parity.

## Native scenarios (added, NOT run)

Separate translation unit: `test/aiIntegration/FHeroes2WideCautiousTest.cpp`.
Minimal CMake integration: one source entry added in `test/CMakeLists.txt`.
No changes to NativePortBattleTest.cpp, HoMM3 or CI.

- `blockedEnemyAllowsFullWideAdvanceAcceptedByServer`: a real Force Field ring
  prevents a guard's approach; a Centaur should advance its full four steps.
  Every predecessor's head and tail are checked against the obstacle and enemy
  occupancy. Factory FHeroes2Tactics command goes through BattleProcessor;
  final position and unchanged health are checked.
- `unsupportedThreatRetainsShorterNativeAdvanceAcceptedByServer`: same geometry
  with unsupported Magic Mirror restores whole-context fallback and expects
  shorter movement, then checks server acceptance and final position.
- `blockedTailPreventsPassageDespiteClearHead`: the vertical head corridor is
  open but its tail cells are blocked. Native reachability must reject crossing;
  the controller's NO_ACTION is submitted to the server.

These assertions and fixture expectations require execution in a provisioned
native build. No completed-battle or native pass is claimed by this patch.

## Checks performed in the fresh Linux environment

- `g++ -std=c++20 -O2 -Wall -Wextra -Werror -pedantic`:
  existing `CI/fheroes2_source_parity_test.cpp`: **39,805 checks passed**;
  scope is distant target, melee outcome and area-shot cutoff, NOT wide movement.
- Same flags, existing `CI/source_tactics_test.cpp`: **2,093 checks passed**.
- Both binaries rebuilt with `-O2 -fsanitize=undefined
  -fno-sanitize-recover=all`: **39,805 / 2,093 passed**, no UBSan diagnostic.
- `git diff --check`: passed.
- Full native CMake configure: exit 127, `cmake: command not found`.
- Direct syntax-only attempt: exit 1 at `test/StdInc.h:11`, missing
  `gtest/gtest.h`. Boost headers are also absent in `/usr/include/boost`.

No native compilation, native scenario execution, runtime latency measurement,
full regression, Mac/GUI acceptance or remote CI run was performed. Existing
standalone passes do not validate the changed native controller. This proposal
must not be recorded as a new accepted checkpoint before native verification.
