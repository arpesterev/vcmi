# Policy-v2 reference overlay (staged, not executed)

The original 58 outcome cases and 19 decision cases are unchanged. Only the
movement branch of the documented fixed battle policy changes: use the engine's
reachability to legal enemy attack-origin hexes, select by distance/enemy ID/hex,
and backtrack walking predecessors to a legal move. Flying movement uses only
engine-approved landings. Shooting and immediate melee ordering stay unchanged.
The sampled model and oracle driver share this deterministic movement chooser;
the server alone executes legal actions, damage, initiative and battle results.
This is an explicitly shared action policy, not an independent tactical-policy
implementation and not optimal battle play.

All relevant output records now declare policy_version=2. The strict parsers
reject missing or different versions, so policy-v1 outcomes cannot silently join
policy-v2 evidence. The legacy min/max DetachedDuelRollout remains version1 and
its endpoint fields explicitly say forecast_policy_version=1. They are not the
sampled model and are not included in sampled accuracy/support counts.

Three separate `ForcedMovementReference.*` controls require real server actions:

- NativeObstacleDetourProgress: seven walking-dead stacks on each side, native
  default layout and engine-generated obstacles, fixed seed17041 and public
  MiniTest battle tile. No geometry or outcome search. At least one actual move
  must be on a route measurably lengthened by obstacles, and must reduce remaining
  engine BFS distance to legal contact. To attribute that effect to obstacles,
  compare with engine BFS on a local accessibility copy changing only OBSTACLE
  cells to ACCESSIBLE. This diagnostic copy never chooses a live action or changes
  live geometry, units, bonuses or RNG. The control fails if the fixed generated
  board does not exercise such a route; merely decorative obstacles do not pass.
- NativeDistantFlyerAdvances: native stone gargoyles must make a legal first move,
  not defend while distant. No flight bonuses are granted.
- NativeDoubleWideAdvanceLegal: native centaurs must make a legal first move with
  actual double-wide geometry. No geometry flags are overridden.

Every move is checked against current engine-available hexes and accepted by the
real BattleProcessor. Controls run to actual BattleResult (2000-action ceiling),
log move/progress counts and winner, and do not preselect a winner. Source RNG is
unchanged by policy/diagnostic queries. Stat values, penalties, natural abilities,
allvariant decision budget (2000ms/384 draws) and support gates are unchanged.
The three mechanics controls do not join the58/19 terminal-outcome parsers.

Source review and local Python parser tests are not native execution evidence.
No obstacle, flyer or double-wide pass is claimed until the real controls run.
