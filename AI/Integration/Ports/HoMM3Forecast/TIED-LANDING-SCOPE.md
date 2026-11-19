# Ordinary melee forecast with tied landing costs

This increment starts at `5d93910aa515133ac3a3993ed7ef9ae6fada5f0f`.
It extends the existing native ownership-prefix forecast; it does not extend
the general two-pass target census or complete-battle simulator.

## Production behavior

`NativeShooterModel::meleeExchange` previously refused an otherwise supported
moving duel when two adjacent attack cells shared the cheapest path cost.
It now visits the defender's adjacent cells in the original direction order
and retains the first minimum. A supported lethal forecast can therefore reach
the existing healing-only spell filter: the controller conserves mana and
executes its ordinary melee action instead of casting a redundant damage spell.
A surviving target does not acquire a winning forecast from this change.

The admission remains a clear, non-siege, ordinary single-hex duel with no
obstacles, enemy hero, extra retained units, special attacks, charge, unsupported
bonuses, or ambiguous lifecycle. Both units must have positive speed. The enemy
must reach the actor's current position, preserving the source dangerous-enemy
branch; the actor must reach an adjacent cell this turn. Existing non-Easy,
ownership, unchanged attack/defense, nonnegative exchange-value and arithmetic
guards remain. Adjacent exchanges retain their existing domain.

## Source and adaptation boundary

The reference is the CC0 reconstruction `sushi-shi/homm3-decomp` at
`e12906accf96c2662b1abadbc49ddc111991bf21`, not a retail executable.
`type_AI_attack_hex_chooser::findAttackHex` visits directions 0 through 5.
`checkAdjacentHexes` retains the earlier cell when attack time, hex value and
cost are equal for ordinary non-charging attackers. In this admitted duel,
landing threat and blocked-shooter value are zero. The existing source-derived
`GroundPathDetail::adjacent` supplies that direction order.

Native reachability, attack legality and midpoint damage remain VCMI
projections. This change neither replaces native damage rules nor certifies
retail integer rounding. The forecast's internal landing is not exposed by its
result. Tests compare source adjacency/costs, the source chooser, projected
health and observable controller behavior; they do not claim an independent
trace of the internal forecast landing or source-equivalent live movement.

## Regression coverage

The separate `HoMM3TiedLandingTest.cpp` uses pinned original chooser/path bodies
and a real native battle fixture. Its scenarios cover lethal spell suppression
with a server-accepted melee victory and unchanged mana; a surviving target
with a real server spell cast; mirrored defender-side melee; a geometry matrix
across row parities and directions; and refusal for insufficient enemy reach,
Easy difficulty and an unsupported retaliation ability. Forecast checks retain
live position, health and retaliation counts.

Two old HoMM3 expectations are deliberately migrated: hex 112 in
`movingMeleeRefusesUnprovedGeometryDifficultyAndThreat` becomes admitted, and
`tiedOffRowLandingRemainsUnavailable` is renamed
`tiedOffRowLandingPreservesBoundedForecast` with admission assertions. No
FHeroes2 test is changed. Native execution and build results are recorded in
the handoff evidence; source code alone is not passing-test evidence.

## Local validation (2026-11-19)

Debian 13 x86_64, GCC 14.2, normal Release build of `vcmitest` and `vcmi`:

- Directed native run: 30/30, including all five new tests and 144 geometry
  cases recorded in XML; no skipped or disabled tests.
- Broader HoMM3 run: 575/575 across 38 suites, matching the compiled inventory,
  with no failures, errors or skips. This is the HoMM3 selection, not a new
  full-engine checkpoint. Its final wall time was about 146 seconds.
- Standalone contract/source suite: 32/32. All 19 original forecast function
  hashes match the pinned upstream source. The upstream adjacency file also
  matches its recorded SHA-256 and Git blob SHA.

The first new-test link exposed an oracle namespace collision; the oracle is
now isolated using the repository's existing namespace-remapping pattern.
The initial run lacked build-tree resources until the standard `vcmi` target
deployed them. The first actual runs passed 29/30 and 574/575: the defender
fixture incorrectly used a neutral attacker where the server requires a valid
player. A player-owned monster army without a hero fixes that setup. The
namespace, resource and fixture failure logs are retained in the handoff.
Production admission was not weakened to resolve these failures.

MMAI stayed enabled in the build, using the locally extracted Debian ONNX
Runtime 1.21 package; no neural-model or CI-installation qualification is
claimed. No paid CI, GitHub write, Mac/GUI acceptance or sibling-port changes
were performed. The frozen a53 inventory remains unchanged; integration must
account for the five new test IDs and the explicitly reviewed rename above.
