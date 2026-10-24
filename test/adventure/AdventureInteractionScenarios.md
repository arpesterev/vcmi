# Bounded interaction drafts, separate from frozen qualification

Status: first native qualification compiled and ran but failed the initial budget
precondition (actual 1200 versus declared 2200), before any planner behavior. The
generator correction below is source-supported; repaired native execution is
pending. No production source changes are required.

## Defence, shared spending and recovery: first priority

`AdventureInteractionScenarios.UrgentTownPurchaseThenThreeTurnRecovery` extends
the already reviewed synchronous planner/server fixture, using real callbacks,
real owned lock capabilities and real `CGameHandler` requests. It does not add a
client renderer, fake successful request acknowledgments or battle resolver.

The public 64x48 map has two red towns, one existing visiting hero and a visible
blue threat. Initial gold must be exactly 2200 after the normal map event;
if it is not, fixture qualification fails. Expert AI starts with 10000 gold
(config/difficulty.json; CGameState::initDifficulty). NewTurnProcessor.cpp692-696
explicitly skips income on the first turn, so the one-shot map event must subtract
7800, not 8800. The original draft incorrectly included 1000 day1 town income. The
C++ 2200 assertion and all later behavior/deadline assertions remain unchanged. A pair of remote ordinary red ore mines keep
the retreat destinations visible, without a reveal cheat or raw hidden lookup.

Before the first planner turn, actual production threat analysis must establish
arrival within 0/1 turn at the threatened town and no 0/1 turn threat at the safe town.
The combined local army counts each actual upper/visiting army once. The first
successful spending must serve the threatened town, and actual recruitment must
increase its local army. Every recruitment reconciles actual destination troop
increase, dwelling stock decrease and exact engine creature cost; the whole red
turn reconciles resources spent against real balances. It does not infer success
from PackageApplied(true).

The blue fixture driver makes 15 adjacent legal southward moves on each of two
blue turns, checking actual position/MP debit at every step. Blue does not attack.
Each endpoint must remain visible through an ordinary owned ore mine. Day2 must
still be within the actual 0/1-turn threat horizon; only after the second retreat
must day3 production analysis show no remaining 0/1-turn town threat. Mine ownership
must remain neutral after both earlier red turns, then change to red during the
third planner turn with productive movement,
while retaining both towns and its hero. Failure of these geometry/threat
preconditions is an invalid draft scenario, not evidence of poor play. A legal AI
battle decision remains unsupported and fails; do not weaken the battle gate to
force this interaction case green.

Newly supported request paths are RecruitCreatures, BuildStructure, ArrangeStacks,
BulkMoveArmy and GarrisonHeroSwap. Their real native state packets forward ordered
post-application garrisonsChanged, availableCreaturesChanged, buildChanged,
heroInGarrisonChange and heroVisitsTown notifications. Unknown packets/requests,
battles, hiring and blocking dialogs still fail explicitly. No artifact or Scholar
exchange paths are implicitly accepted.

This is a three-turn planner/transaction interaction, not a survival-against-an-active
opponent or full-game/win-rate test. It complements synthetic-callback
TownRecruitmentRegression and JointTownDefenceRegression.

## Courier handoff: scoped draft, intentionally unsupported

Intended acceptance: drive production ExecuteHeroChain for a courier to a fixed
recipient, perform real server-validated troop handoff, prove species/count
conservation and leave each hero a legal army, then on the following turn verify
recipient progress toward a fixed objective with no oscillating rendezvous.
This proves execution of a supplied production chain, NOT autonomous chain choice.
A separate later planner-choice case would be needed for that claim.

Blocking dependency found in source: `CGameHandler::heroExchange` emits
ExchangeDialog BEFORE adding its CGarrisonDialogQuery. `AIGateway::heroExchangeStarted`
then schedules asynchronous work which calls pickBestCreatures/pickBestArtifacts
and answerQuery. The synchronous bridge cannot simply acknowledge the dialog,
reply inline before the query exists, or omit real transfer policy callbacks.
It continues to reject ExchangeDialog. No fake courier success test is supplied.

A faithful small next extension would use a bounded request queue: planner and
normal gateway asynchronous policy threads release their actual shared state lock
while waiting; one server driver processes queued requests in order, applies
state under the writer lock, and delivers before/after callbacks in native order.
Only after the original move handler returns may queued query replies execute.
Require request IDs, accepted-query ownership, no pending requests at EndTurn,
real transfer deltas and stop-on-timeout. This must receive its own lock/order
review; it is not necessary to compile or release the already-reviewed fixes.

## Qualification contract

Only after review, overlay these files and include AdventureInteractionReference.cmake
from test/CMakeLists.txt. The optional target is EXCLUDE_FROM_ALL. Run from build/bin
so CVcmiTestConfig mounts generated test/testdata/AdventureInteractions/*.vmap.
Use the same strict warm-only controls: at most 12 new target/server objects,
180 seconds compilation,180 seconds per case and600 seconds total. Missing XML,
unsupported event, timeout or nonzero process result is incomplete, never a pass.
Do not automatically add this target to the frozen workflow/manifest.

Keep the same public-data hash, seed 17041, Expert difficulty and unchanged no-cheat
settings in baseline and candidate. Record request ordering, town IDs, actual
spending/stock/army changes, threat arrival before/after and productive completion.
Do not tune expected values to a candidate run or claim better enjoyment from green
transaction tests. No user-owned save/campaign data is used.

Assertion geometry is declared before any native run: blue starts anchor(45,10),
withdraws to(45,25), then(45,40); the threatened town visitable tile is(43,7).
A single normal turn cannot move a nearby attacker beyond the planner's two-turn
urgency horizon, so the draft explicitly uses three red turns. If actual path
costs do not produce the declared day2/day3 transition, reject fixture validity
rather than changing an expectation to match the candidate. Early mine capture
is likewise not poor play; it means this fixture cannot demonstrate later recovery.

## Native transaction evidence and neutral-owner correction

The corrected-budget run executed real urgent recruitment, garrison exchange and
resource/stock accounting successfully, then stopped at the day1 mine-neutrality
assertion: owner was UNFLAGGABLE instead of NEUTRAL. The object was a genuine gold
mine, not a resource. Explicit owner="neutral" is not registered by
IdentifierStorage.cpp31-32, so JsonSerializeFormat.h321-328 did not replace the
CGObjectInstance constructor's UNFLAGGABLE owner. The native serializer's neutral
default is selected by omitting owner (CGObjectInstance::serializeJsonOwner and
JsonSerializeFormat's empty-field branch).

Qualification5 changes only this interaction objective's options to {}, asserts
that exact omission in generation tests, and verifies actual CGMine type, gold
production, empty army and NEUTRAL ownership before any planner turn. Owned town
and observer preconditions are explicit too. Day1/day2 NEUTRAL and day3 RED
assertions remain unchanged. Prior failed evidence is retained; later retreat,
threat-transition and recovery behavior remains unexecuted.
