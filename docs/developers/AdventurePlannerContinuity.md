# Nullkiller2 adventure planning continuity

This change applies to Nullkiller2. It does not change the player's difficulty,
resources, visibility, creature statistics or the combat AI's legal information.
It is not a measured claim of improved win rate.

## Planning policy

- Widening the scan to `ALL_FULL` invalidates cached path data even if no map event
  occurred. The next `updateState` rebuilds paths with the wider settings. There
  is at most one widening per normal turn; normal pass limits still apply.
- A local `cannotFulfillGoalException` suppresses that structural task for the
  current turn, invalidates paths/threat/territory state, and abandons the stale
  selected plan. Up to three fresh recovery passes are allowed per turn. The
  failed task is not retried merely because it was allocated at a new address.
  Affected-object IDs, concrete hero and spell ID distinguish independent work.
- Recovery cannot issue alternatives during an outstanding movement, battle,
  visit, channel probe or query answer. The gateway also waits for these states
  before sending end-turn, releasing the shared game-state lock while waiting.
  The wait remains interruptible. Rejected or unresolved queries are not silently
  answered again; this is a safety boundary, not a new query-retry mechanism.
- Failed chain locks are released; startup and defense locks remain. A normal
  route that needs another day is not treated as a failure. A promised same-day
  arrival that did not happen is treated as a failure rather than success.
- ID/coordinate intentions retain a currently feasible journey through small
  priority changes. The comparison-only bonus is 20%, a policy default rather
  than an empirically optimal parameter. It never turns nonpositive, nonfinite,
  missing-object or safety-rejected goals into viable ones. Higher-priority
  tiers retain their existing ordering. Defense candidates disable the bonus
  for involved heroes; >20% better opportunities can replace a commitment.
- Intentions expire after two subsequent turns without progress. Hero/target
  disappearance and target ownership changes invalidate them. Current-turn
  candidates are freshly generated and evaluated; paths and goal pointers are
  never retained. Each courier remembers its own final planned rendezvous, which
  can differ from the recipient's strategic destination. Courier-only movement
  can establish shared progress. Existing affected-object conflict filtering
  prevents two selected tasks from simultaneously consuming the same hero.
- Intentions are in-memory only. Save/reload intentionally starts without them;
  this patch does not change save serialization or promise reload continuity.

## Validation and limits

`test/nullkiller2/PlannerContinuityRegression.cpp` exercises actual production
selection, execution, chain failure propagation, town-swap isolation and idle
state gating across reconstructed tasks and successive turns. Its cache fixture
calls the real `updateState` and `AIPathfinder::updatePaths`, observing callback
path computations after otherwise-clean cache reuse. That fixture uses an empty
world: it proves cache lifecycle, not discovery or movement toward a distant
scout objective.

Scripted journeys isolate planner continuity, not complete server gameplay.
The public-map qualification harness and real-server battle reference target are
separate evidence. Unit test counts, passing smoke initialization, and successful
compilation alone do not demonstrate a stronger opponent.

The intention bookkeeping is bounded by active heroes and chain participants;
it adds no search-tree expansion. Recovery adds at most three full state/path
regenerations beyond ordinary successful passes. Actual turn time still depends
on map size and pathfinder load and must be measured on paired fixtures.
