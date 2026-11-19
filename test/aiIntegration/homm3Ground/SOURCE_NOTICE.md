# Ordinary ground path oracle

Reconstructed source: sushi-shi/homm3-decomp commit
`e12906accf96c2662b1abadbc49ddc111991bf21`, CC0-1.0.
This is not a retail-binary oracle.

`bodies.pinned.inc` contains ten byte-exact extracted function bodies:
seedCombatPosition, markEnemy, checkEnemyArmies, findCombatPath,
pushCombatPoint, canFit, getAdjacentCellIndex, setupAdjacencyArray,
hexIsBlocked and hexcell::getArmy. `source-manifest.json` records per-body
SHA256 and source files. `upstream-manifest.json` records full upstream Git
blob SHA1, SHA256 and source URLs; complete audited snapshots remain in
`vcmi_homm3_next/evidence/ground_census_audit/upstream`.

`PinnedGround.h` supplies input-only army/cell records and the original
sorted vector queue. Production uses a separate FIFO graph implementation;
no production geometry, visitation, costs or publication helpers enter the
oracle. Canonical grid arithmetic/accessors are scalar harness services.
The independent source setupAdjacencyArray constructs the actual graph.

The admitted domain is ordinary single-hex, nonshooting, nonflying walking,
normal combat, destination=-1, static blocking cells and unique live troop
occupancy, unchanged owning side. Wide/siege/placement/destination services
throw if reached. `setMoat` is inert and `isMoat` false because every moat,
stopping or movement-effect cell is outside the domain. `is` is false because
all special flags are excluded. buildCombatPath returns false for the sole
permitted invalid destination (-1), matching its first source guard. These
stubs do not qualify excluded branches. Default path storage is zeroed by
original findCombatPath; unvisited/unmarked costs are normalized to 65535
only when comparing the documented production result contract.

Standalone test covers every available actor head against every distinct
live-enemy head at speeds 0,1,6,127; every free/corpse metadata head in all
published maps at speeds 0,1,4,127,INT_MAX with/without movement restriction;
and 2,000 deterministic mixed static-blocker/friendly/enemy occupancy maps.
All 187 cells are compared for visited, cost, validMove and enemyMarked.
Invalid identity, duplicate occupancy, unavailable origin, blocked occupant,
missing actor and unknown-domain contracts are rejection-tested without
calling original code on malformed engine states.

Commands from repository root:

```sh
g++ -std=c++20 -O2 -Wall -Wextra -Werror CI/homm3_ground_path_test.cpp -o /tmp/homm3_ground_path
/tmp/homm3_ground_path
g++ -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=all CI/homm3_ground_path_test.cpp -o /tmp/homm3_ground_path_ubsan
/tmp/homm3_ground_path_ubsan
```

Expected: 111,890 maps; 83,805,624 publication/contract checks.

`GroundPinnedExchange.h` is a separate copy of the existing scalar exchange
harness (whose original bodies/manifests remain under `homm3Exchange`). Its only
functional changes are namespace separation and replacing the former throwing
ground-search service with `PinnedGround::searchArray`. It copies original
published cells into the exchange manager, so its full original findAITargets
uses independently generated source costs. No production path output is fed
into this oracle. The row test compares the complete independently enumerated
candidate stream, each original exchange value and the final source decision.

Boundary qualification: speed127 coverage is not proof of search-budget
exhaustion. No admitted concrete obstacle map reaching costs127/128 has been
constructed; that specific boundary remains unexercised. The unit-edge graph
proof bounds queue insertions by165, below the source cap500, but does not claim
an induced walk of length127 exists on this board.

For UBSan with GCC's additional optimizer diagnostics, add
`-Wno-maybe-uninitialized`: the untouched source adjacency switch covers all six
directions but GCC cannot prove the local `adjacent` initialization. No runtime
sanitizer diagnostic is suppressed.
