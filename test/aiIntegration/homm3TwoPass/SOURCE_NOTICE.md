# Original-body two-pass oracle

Reconstructed source, not retail executable equivalence. Pin:
`sushi-shi/homm3-decomp@e12906accf96c2662b1abadbc49ddc111991bf21`, CC0-1.0.

Original sources:
- https://github.com/sushi-shi/homm3-decomp/blob/e12906accf96c2662b1abadbc49ddc111991bf21/src/ai.cpp
- https://github.com/sushi-shi/homm3-decomp/blob/e12906accf96c2662b1abadbc49ddc111991bf21/src/ai_tactical.cpp
- https://github.com/sushi-shi/homm3-decomp/blob/e12906accf96c2662b1abadbc49ddc111991bf21/src/army.cpp
- https://github.com/sushi-shi/homm3-decomp/blob/e12906accf96c2662b1abadbc49ddc111991bf21/include/army.h
- https://github.com/sushi-shi/homm3-decomp/blob/e12906accf96c2662b1abadbc49ddc111991bf21/include/ai_tactical.h

`two-pass-bodies.pinned.inc` holds exact extracted original function bodies.
`ai_tactical.h.snapshot` preserves signed-long constructor fields. The harness
uses existing adjacent homm3Exchange exact scalar/census/helper pins and its
host-binding scaffolding, copied and extended without a production chooser.
Status helpers are the source army.h definitions also pinned in homm3Forecast.

The instrumented include renames four methods for observer wrappers and inserts
one raw-damage recording assignment. Wrappers collect candidates before/after
killsOnly retry, actions, cursor boundaries and per-action ledger. They do not
choose targets, recompute damage or call production logic. Initial trace records
are completed when the next action/pass starts; skipped entries cannot alter
ledger in this admitted source model. Action scratch fields are restored by the
original simulateCombat body, not reconstructed by the test.

Source `long` arithmetic compiles as int32_t via a temporary macro around exact
body includes; harness declarations use matching explicit int32_t. This avoids
silently validating source-overflow expressions with host LP64 long. Source
floating calculations/casts remain unchanged. Invalid/overflow-domain tests do
NOT execute undefined original arithmetic; they check production unavailability
separately and are labelled accordingly. The declaration macro is undefined
after inclusion. Original-body warnings narrowly suppress unused parameters and
signed-loop comparison; driver builds with Wall/Wextra/Wpedantic/Werror.

Ground/area/cyclops/special/random branches throw in this harness and are not
covered by the ordinary shooter domain. Input is generated in independent
source objects; their pinned constructor/common minima and damage/value methods
produce the typed production input. Source no-survivor results are not native
combat or retreat authorization. General ground target times2..5 are not claimed:
admitted ordinary shooter census target times are1, while incapacity/fallback
weight divisors are independently exercised.

`generate_oracle.py` records the extraction/extension recipe. Regeneration needs
the named restored upstream audit snapshots at the documented workspace paths;
normal compilation needs only checked-in files and adjacent existing pins.
Source-manifest hashes identify original extracted bodies, supplemental header
and inherited includes. Generator is not invoked by CMake.

Independence disclosure: reviewer initially requested the production header for
API structs and the tool also displayed its implementation. Oracle decisions
were subsequently built solely from the exact upstream bodies above, not from
that implementation; trace comparisons and generated inputs use the public
schema. No claim of blind implementation review is made.

## Differential coverage

The driver covers2..7×2..7 rosters over60 deterministic profiles per pairing,
with wounded stacks, difficulty/human modes, controller Archery/Armorer factors,
Bless/Curse, luck, shield/distance/wall factors and source double-attack ranking
(the actual simple shot remains one original simulateSimpleAttack). These are
pure scalar supported inputs, not a broadened native double-shot admission.
Directed cases cover A/B/A stop, absent requested-first side, later roster tie,
retained corpse/live-head overlap, out-of-order nonzero ledger, no target, empty
order, zero speed, Blind/Stone1/2 and a zero-best killsOnly retry. Full order,
both cursors, initial/final candidate ranks, raw shot damage, every action ledger,
final ledger and restored four scratch fields are compared. Invalid vectors are
production-only refusal checks, never calls into source undefined arithmetic.

Original capacity declaration is preserved in cmbtmgr.h.snapshot (line765):
army m_armies[2][21]. Harness matches21;22-peer malformed input is tested only
as unavailable, never executed in the fixed-array source oracle. Header URL:
https://github.com/sushi-shi/homm3-decomp/blob/e12906accf96c2662b1abadbc49ddc111991bf21/include/cmbtmgr.h
