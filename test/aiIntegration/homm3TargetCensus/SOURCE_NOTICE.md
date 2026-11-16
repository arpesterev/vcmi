# HoMM3 target census reducer

Source: `sushi-shi/homm3-decomp`, commit
`e12906accf96c2662b1abadbc49ddc111991bf21`, CC0-1.0.

- https://github.com/sushi-shi/homm3-decomp/blob/e12906accf96c2662b1abadbc49ddc111991bf21/src/army.cpp
- https://github.com/sushi-shi/homm3-decomp/blob/e12906accf96c2662b1abadbc49ddc111991bf21/src/ai.cpp

Full fetched file bytes are retained as `*.snapshot`; SHA-256 values are in
`provenance.json`. `PinnedTargetCensus.h` preserves the exact `considerAttack`
and `getAITargetTime(long)` bodies, with only `inline` added. Its small army
supplies input fields and trivial source accessors. It is not a retail binary.

The production reducer consumes a **complete source-filtered candidate stream**
in original roster order, exact exchange values and source attack distances.
It does not implement `findAITargets` filtering, pathfinding, attack-effect
simulation, native adaptation, source damage, or Ray spell selection.

Unknown stream, unknown value/distance, negative speed/distance, repeated roster
identity, out-of-range bit positions, or source signed-32-bit ceil overflow
return unavailable. A known empty stream returns an available result with no
target. These are intentionally different outcomes. No unknown partial stream
can publish a positive or negative census result.

The oracle runs with host `long`; the tested accepted input domain avoids
source signed-32-bit overflow and shift undefined behavior. The production
reducer rejects those boundaries rather than reproducing undefined behavior.
It preserves source first-on-tie selection, even for non-positive values,
nearest-turn priority, possible-target masks, zero speed, and shooter target
clock behavior. `findAITargets` supplies distance 1 for shooters. The matrix
also exercises the reducer's more general source-field input combinations.

Standalone checks:

```
g++ -std=c++20 -O2 -Wall -Wextra -Werror CI/homm3_target_census_test.cpp -o target-census
./target-census
g++ -std=c++20 -O1 -g -fsanitize=undefined -fno-sanitize-recover=all -Wall -Wextra -Werror CI/homm3_target_census_test.cpp -o target-census-ubsan
./target-census-ubsan
```

These tests establish bounded reducer/source-body agreement, not native battle
integration or full original-AI parity.
