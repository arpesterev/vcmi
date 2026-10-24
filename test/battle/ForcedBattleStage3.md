# Staged expanded reference qualification

This file and the staged ForcedBattleReference.cpp are NOT part of the frozen
first oracle candidate. They are source drafts awaiting compile/runtime review.
Do not report results or calibration before actual execution.

## Predeclared matrix

Preserve all six first-oracle cases. Add 42 symmetric count-sweep cases:
20 archers versus 10/20/30/40/60/80/120 walking dead, each in both attacker and
defender orientation, each using oracle seed 17041, 17047, 17053. Add two mixed
cases sharing an attacker of 20 archers + 20 pikemen: defenders are respectively
15 walking dead + 20 skeletons and 60 walking dead + 80 skeletons. These are
labelled low/high counts, not predeclared victories or model-agreeing results.

Every one of these 50 cases forces the real server battle independently of
prediction support or forecast winner. A null/unsupported snapshot records explicit capture-admitted=false and its
reason; it does not stop the actual fight or masquerade as a successful forecast. Source-invariance,
invalid server action or missing engine result failures must not be counted as
valid observations. Same-source same-resource reruns must retain full logs.

Before actions, check raw planning snapshot invariance using the staged helper;
run three sampled forecasts at fixed seeds 17041/17047/17053 on an independently
owned snapshot. Binary-serialize the whole authoritative GameRandomizer before
and after all forecast construction/execution/destruction, including per-army
biased morale/luck streams. A byte difference is a test failure. No oracle
random draws or reseeding are used to make this comparison. The helper separately
checks thread-default RNG and live bonus-graph/health/mana invariance.

Log scalar army-strength ratio, actual winner and per-creature casualties, plus
sample statuses, winners, survivors and AI-value losses. Ratio >= 1 only denotes
scalar numerical advantage: it is NOT asserted to be adventure safety. Any
false-safe/false-dangerous analysis must state its scalar threshold and acceptable
loss criterion explicitly and include unsupported/incomplete rows separately.
No model-agreement assertions are used to select or discard cases.

## Real neutral preparation

Two further tests use a real public MiniTest hero with explicitly configured
20 archers + 20 pikemen and a factory-created, map-placed 47-walking-dead monster
at (3,2,0). The monster cannot join/flee; this changes encounter choice only,
not combat stats. One test forces no upgraded stack; one forces the ordinary
singleton upgraded stack option. The engine decides actual split count through
getNumberOfStacks. onHeroVisit invokes the real private fight path, server stack
move/change-type packets, singleton upgrade RNG draw, and startBattle. It does
not start an unsplit synthetic monster fight and call that neutral preparation.

A player-bound visibility callback enumerates supported field variants before
fight. createNeutralEncounter builds copies before real split/upgrade; after the
server starts, compare the variant matching the actual field to actual initial
stacks: side/slot/type/count/positions/health/speed/morale/attack/defense/damage.
Forecast support cannot veto the subsequent forced real fight. Independent
sample results and final real casualties are logged. Each forecast is destroyed
before action execution and authoritative RNG is checked unchanged.

No production source is changed by this fixture. It needs staged snapshot,
sampled rollout, bonus-inspection and battlefield-candidate APIs supplied by the
risk owner, and CMake registration controlled by the integration owner. The
first frozen oracle must complete before these additions are promoted.

## Native mechanics follow-on rows (58 total)

Six additional rows were requested during risk-owner review: native marksmen
and grand elves against walking dead, griffin and royal griffin against two
skeleton stacks, sprite against skeletons, and the public MiniTest hero with
swordsmen to exercise its native specialty. The hero type is logged, not
silently assumed; stats/bonus definitions are not granted or removed to pass
support. The specialty row also runs the live-graph invariance helper after
actual neutral preparation. These rows remain uncompiled source drafts.

No low-ammunition edit or artificial retaliation bonus is used. Exhausted ammo,
blocked-edge double-wide movement, and obstacle-rich references still need
explicit qualification; merely allowing these mechanics in the sampler is not
passing them. The same actual server actions/results remain the oracle.


## Fatal failures and diagnostics

Both run helpers wrap initialize in ASSERT_NO_FATAL_FAILURE and explicitly
require non-null handler, map and game state before dereference. Outer tests
wrap their run helper as well. Flushed initialization/map/battle/visit phase
markers distinguish setup failures from engine combat failures. Invariance
helpers are invoked only for admitted captures; their failures remain gtest
failures, but their internal snapshots are not returned or dereferenced by the
caller. Independently initialized real battle state still supplies the forced
reference even if an invariant fails; such failed-test outcomes cannot qualify
as valid calibration rows. Captures and rollouts each log their own support
status and reason, never combining unsupported with a modeled loss.


## Evidence acceptance contract

The parser requires exactly 55 direct and 3 real-neutral terminal identities.
Neutral capture/reference/sample rows carry both upgrade and specialty; the
terminal row carries the actual selected battlefield so variant captures can be
matched. Nonselected field variants are capture opportunities, not extra battles
or samples. Rejected selected captures legitimately produce zero samples;
admitted selected captures must produce exactly three sample executions. Reasons
with spaces are retained verbatim. Unsupported/budget sample outcomes remain
separate from finished winners and are never counted as losses. Legacy min/max
endpoint straddles are not an accuracy score.

Predeclared minimum supported smoke rows are ordinary_melee, ranged_slow_melee
undead_melee and neutral_0_0: each must capture successfully and execute all three sampled
rollouts to a terminal result. Winner agreement is not required, so observed
disagreement cannot remove a case. These assertions prevent an all-unknown model
from qualifying merely because actual forced fights completed. The neutral_0_0 gate also requires the player-bound ordinary-neutral split capture used by production, rather than only direct battle snapshots. No assumption
that all 174 possible samples are supported is made.

The exact 58 gtest names and evidence identities are exported by
CI/forced_battle_evidence.py (expected_cases / EXPECTED_TEST_NAMES), with
CI/forced-battle-cases.json as a machine-readable copy.


## Forecast-failure isolation and eligibility controls

Only experimental capture, comparison, invariance and forecast calls have a
std::exception boundary. Such an exception emits its phase and exact reason,
records ADD_FAILURE, and becomes an unsupported forecast/null capture so the
independent server fight can still produce evidence. Real server initialization,
visits, actions and terminal results are never caught as successful outcomes.
Existing live-state/RNG assertions remain mandatory; an exception or mutation
still fails qualification. Sample error strings remain owned through logging.

Within the three existing neutral rows, temporary player-owned targets must be
rejected by both neutral snapshot and risk-request APIs. A missing-appearance
target must be rejected by raw/neutral/request capture before geometry reads.
RAII restores authentic ownership and appearance before the real encounter.
Admitted snapshots also require unsupported access to live gameState to throw
std::logic_error. Full server RNG comparison surrounds these controls. Exact
custom obstacle-subclass rejection and coastal native-terrain comparisons remain
separate unexecuted follow-on controls; no coverage is claimed for them here.
