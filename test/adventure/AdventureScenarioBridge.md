# Staged asset-free adventure scenario reference

Status: generated and Python-tested, **C++ not compiled or executed**. Not part
of the first frozen qualification. No production changes, private maps/saves,
renderer assets, graphical client, headless client, or external network transport.

## Build and run after independent review and integration approval

The integrator may include `adventure/AdventureScenarioReference.cmake` from
`test/CMakeLists.txt`. The target is EXCLUDE_FROM_ALL and needs the existing static
Nullkiller2/server/test configuration, public pinned test data and installed VCMI
configuration files, just like `vcmitest` and `vcmi_forced_battle_reference`.

```bash
cmake --build <existing-build> --target vcmi_adventure_scenario_reference
cd <existing-build>/bin
./vcmi_adventure_scenario_reference --gtest_filter=AdventureScenarioBridge.RealCallbackMoveConsumesMovementAndChangesPosition --gtest_output=xml:adventure-move.xml
./vcmi_adventure_scenario_reference --gtest_filter=AdventureScenarioBridge.ProductionPlannerCapturesReachableMine --gtest_output=xml:adventure-planner.xml
```

Use a subprocess supervisor with a 180-second hard cap per scenario and
600-second cap for the initial qualification; nonzero exit, unsupported event,
missing XML, crash or timeout is incomplete/failure, never pass. Internal request
and time checks are cooperative, not a replacement for that supervisor. Start
with the move test, then planner smoke, before enabling either draft quality case.
Do not launch graphical smoke: public test data does not establish renderability.

## What runs

Generated native `.vmap` inputs derive solely from repository `MiniTest` and
`ObjectPropertyTest` JSON. `generate_maps.py` emits per-map/source SHA256 hashes in
`provenance.json`. The fixed native maps use ordinary creatures, normal player
visibility, Expert difficulty, seed 17041, and an explicit normal RESOURCE starting
bonus. No reveal cheat or nonstandard army bonus is allowed.

`CGameHandler::init` creates the real seeded state, server event bus and turn
processor. A real player-specific `CCallback` sends requests to
`CGameHandler::handleReceivedPack`; state packs apply exactly once. Calls into the
real production `Nullkiller::makeTurn` use visible-object memory preparation from
the gateway. Blue deliberately ends turns without moving in these first cases.

The bridge synchronously forwards the supported movement, visit, resource,
visibility, ownership, blocked and game-over callbacks. All state updates
conservatively invalidate planning caches. This differs from normal cache timing
and is not a performance benchmark. Only MoveHero, EndTurn and SetFormation requests are supported. Recruitment, garrison exchange, construction and artifact changes are not yet qualified. Unknown requests/state packs, battles,
blocking queries, teleports and asynchronous requests fail explicitly.
`PackageApplied(true)` is NOT action-success evidence: this server emits it even
when a visitor rejects a request. Assertions require actual position/movement
point deltas, mine ownership, surviving heroes and real victory state.

This is production planner-turn/map-state integration, **not a full client game**.
It does not exercise asynchronous YourTurn/network ordering. Its synchronous
callback ordering requires independent review before qualification. Tests do not
prove an enjoyable opponent or improved win rate.

## Cases and acceptance

1. `productive_move`: real adjacent callback move must change hero position and
   debit movement. Planner smoke must capture an initially visible mine by day 2.
2. `stable_goal_progress`: visible symmetric mine goals across a water barrier;
   own both by day 6. Before interpreting oscillation, verify real initial routes
   exceed one turn and neither is bypassed by a spell/boat. Capture alone is not
   proof of fewer oscillations.
3. `blocked_goal_alternative`: an unsafe guarded reward and another hero with a
   reachable mine; capture mine by day 2, keep both heroes, leave reward uncollected.
   This is NOT an injected executeTask-failure regression.
4. `urgent_two_town_defence` is a generator draft only, deliberately not in this
   target. It needs qualified battles, recruitment/garrison state callbacks and
   validated initial gold/income/stock. Do not assert survival while blue is idle.
5. Wider search needs measured initial pathfinder costs outside scout range plus
   safe reachable objective completion. Not claimed by these three fixtures.

Pair baseline and candidate with identical map hashes, seed, public-data hash,
settings and target source. Record exact commit/toolchain, stdout/XML and generated
provenance. `ADVENTURE_BRIDGE` traces record day/player/request type and observed
productive-move deltas plus each own hero position/movement before and after requests, not synthetic successful acknowledgments. Supplement with
existing planner traces for goal ID, hero, priorities, retention/expiry/failure,
and route costs. Compare productive captures/moves, idle turns, goal switching and
completion day; do not reward blanket refusal. Initial smoke uses seed 17041;
quality qualification must also exercise 17047 and 17053 by explicitly
parameterizing the fixture, which is not implemented yet.

Real server forced-battle outcomes belong to the separate
`vcmi_forced_battle_reference` target. Paired equal-scalar mismatches must count
false-safe attacks and false-dangerous refusals using completed battles, not the
planner's own formula. Neither target currently calibrates skipped attacks.

## First engine evidence and fixture correction

The first callback move case passed on the qualification runner. The production
planner reached the deadline, where the bridge rejected legitimate RemoveObject
loss cleanup. This is not a completed planner smoke or opponent-quality result.
Inspection found that this baseline resolves CONTROL via visitable position, not
the parsed object name. Name-only hero control accidentally required every hero
of that type, including blue. Corrected fixtures retain names and add exact
visitable positions; initialization verifies resolved IDs against named objects.
Deadlines and explicit mine ownership/survival assertions are unchanged.
RemoveObject now sends visible-object removal notifications before actual state
delete, then after-removal notification and path invalidation. Boat removal remains
unsupported. The bridge does not reproduce the graphical client's hidden enemy
hero deletion notification exception, so it grants no extra map knowledge.

## Known guardian versus surprise resource guards

The next executed run passed callback movement, mine capture and the multi-turn
mine case. The original blocked-alternative fixture entered a guarded resource
battle and remained unsupported, not passed. Its main hero (ID 1, Christian,
20 pikemen) moved anchors (5,5) -> (6,5) -> (7,5), then request 3 visited the
resource at (7,5). The fixture encoded 999 pikemen but omitted guardMessage.
FuzzyHelper intentionally returns zero danger for unvisited resources/artifacts;
CGResource immediately starts its guarded battle when no message is present.
Visibility of a resource therefore did not establish knowledge of its guard.
The log did not print loaded stack strength; no numerical engine-strength claim
is inferred from that failed run.

The corrected known-danger fixture uses an exposed neutral monster of the same
999 pikemen at (7,5), with the 600-crystal resource at (8,5) behind its guard area.
Both red armies remain 20 pikemen; mine/deadline/survival assertions are unchanged.
Before planning, real engine assertions require the exact visible monster stack,
nonzero production danger, unsafe attacks for both heroes, and a safe helper
path reaching the mine within the deadline with no battle actions or danger.
The engine prints actual guard/hero scalar strengths and route turns. This is a
fixture correction, NOT a production AI bug fix or permission to reveal guards.

The old composition remains generator-accessible as `surprise_guarded_resource`,
marked unsupported and excluded from the acceptance target. The original failed
evidence remains in the qualification artifacts for commit a44df12f73773033176f42304ab035abb25e79dd; it is not overwritten or reclassified as passed.

## Qualification 3 lock-contract adaptation

The bridge-only existing private regression friend calls the real gateway
`withPlanningTurnLocks`, acquiring shared path storage before game state. Each
synchronous red turn advances the actual generation, installs the actual scoped
readiness closure and checks real server turn ownership before and after planning.
It never constructs a synthetic capability or calls the risk-preparation hook; a
static assertion requires the experimental hook to remain disabled. PlayerEndsTurn
packets forward the production gateway callback after state application.
The separate executable must NOT link PlanningRiskSchedulerRegression.cpp, which
defines its own richer version of this test-only friend. This remains synchronous
planner-turn integration, not qualification of asynchronous client/network threads.
Qualification 2 is unchanged. This adaptation has not been compiled or executed.
