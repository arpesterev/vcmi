# Ordinary exchange source oracle

Pinned reconstruction: `sushi-shi/homm3-decomp` commit
`e12906accf96c2662b1abadbc49ddc111991bf21`, CC0-1.0.

The source bodies in `exchange-bodies.pinned.inc` are exact text extracted from
`src/army.cpp` and `src/ai_tactical.cpp`. Function hashes and source paths are in
`source-manifest.json`. `hero-factors.pinned.inc` contains the exact three hero
factor functions from the same pin's `src/hero.cpp`.

`PinnedExchange.h` supplies input-only engine API objects. It evaluates the
entire damage pipeline internally and never delegates damage/exchange to the
production helper. It is reconstructed source, not a retail binary oracle.
The host uses `long`; differential test inputs must stay inside the original
signed-32-bit intermediate domain, including multiplication, additions and
float-to-integer conversions.

The supported ordinary test model includes damage range and Bless/Curse;
attack/defense; wounded stacks; shooting eligibility; distance and wall penalty
facts; standard hero Archery/Offense/Armorer levels; double attacks; retaliation
availability and easy-AI/human policy; kills-only and optional simulated health.
Special creature IDs, hatred, hero specialties, moat, fire shield, residual
Blind/Paralyze and random hit abilities are not supplied by this harness's
ordinary tests. Their source branches are retained, but their engine services
are inert stubs or fail if randomness is reached. Those excluded branches are
not qualified by the test results.

Important source facts:
- `getArcheryFactor` tables are float `{0,.1,.25,.5}`; Offense
  `{0,.1,.2,.3}`; Defense reductions `{0,.05,.1,.15}`.
- Defense returns `1.0f-factor`, not double subtraction. The later damage
  pipeline widens this float.
- A controller with zero skill still takes the float conversion/addition in
  `computeAttackerBonus`; no controller takes the integer `bonus` directly.
- Archery artifacts add `.05f`, `.1f`, `.15f` only if the skill is nonzero.
- `getAverageDamage(...,limitDamage=true)` caps against live total HP,
  independently of the exchange's mutable survivor HP.
- Ranged two-attacks are doubled inside `getAverageDamage`, before that cap.
  Melee two-attacks run a second survivor-count calculation after retaliation.
- The loss-value call uses incoming ranged/melee channel, not simply whether
  the victim is a shooter.

Hero source URL:
https://github.com/sushi-shi/homm3-decomp/blob/e12906accf96c2662b1abadbc49ddc111991bf21/src/hero.cpp

`census-bodies.pinned.inc` additionally retains the complete `findAITargets`,
`considerAttack` and `getAITargetTime(long)` bodies. The test passes real scalar
army arrays through this original loop. `searchArray` intentionally throws if
a ground-path branch is reached. `clearAIValues` and `getSpellTime` are trivial
input-field accessors; the complete original roster, including filtered slots,
is presented to both implementations.

Qualification commands from repository root:

```
g++ -std=c++20 -O2 -Wall -Wextra -Werror CI/homm3_exchange_test.cpp -o exchange
./exchange
g++ -std=c++20 -O2 -Wall -Wextra -Werror CI/homm3_ranged_census_test.cpp -o census
./census
# Repeat both with -O1 -g -fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=all
```

Current totals: 71,351 exchange checks; 14,647 complete ranged-row checks.
Both optimized and sanitizer runs pass. This is source-core qualification;
no native fact adapter, engine run, general ground path, or retail equivalence
is established by these results.
