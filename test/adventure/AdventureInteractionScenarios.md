# Urgent defence, productive economy and actual return

The normative interaction is
`AdventureInteractionScenarios.CoveredEconomyThenActualDefenceOnEnemyApproach`.
It runs the production planner through real CCallback/CGameHandler requests and
ordered state notifications, with owned planning locks and public synthetic data.
It is a controlled three-turn response, not a full client-server game or battle.

## Required behavior

- Day1: a visible urgent threat and constrained 2200 gold budget. Successful spending
  first serves the threatened town; actual recruitment increases local strength.
  Every purchase reconciles troop increase, dwelling-stock decrease and exact
  resource cost. Both towns and the hero survive.
- Day2: after a legal visible retreat, the enemy still has a real turn1 route to
  the town. The planner captures the mine productively. A sufficient stationed
  force or a real timely current-army/no-loss/unblocked return route is required.
  A future route is only an opportunity, never a completed defence.
- Day3: the same enemy legally approaches visitable(49,20). Actual paths must show
  enemy town arrival0, sufficient own return0 and no immediate own counterattack.
  The real red planner must then station sufficient actual force in town, at least
  enemy total strength times the existing safe-attack ratio. Remote heroes and
  unperformed army merges cannot satisfy this assertion. Ownership and hero/army
  survival remain required.

All actions are server-validated. Unknown requests, battles and unsupported query
paths fail explicitly. There are no fake success acknowledgments, private saves,
extra troop bonuses or vision cheats. Existing native ore observers provide the
known corridor and normal ore income, not gold; market/exchange conversion is
unsupported. Initial gold/armies/map/seed 17041/Expert settings are unchanged.

## Why the former timing assertion was replaced

`UrgentTownPurchaseThenThreeTurnRecovery` required the mine to remain neutral
through day2 and change owner only on day3 after further withdrawal. That was a
fixture assumption, not the user's requested behavior. It failed when the planner
captured the mine on day2. The failure was retained while actual coverage was
investigated; it was never reclassified as a pass.

Frozen qualification8 commit 9ebbf96e4615d5be24bb25a07261bba47a75e585 and run 40118626275 preserve both cases and
the original failure. The replacement case passed natively in that same run:
the town was empty after day2 economy, but a real return existed. After the legal
enemy approach, the planner actually returned hero 0 and garrisoned 24pikemen and
9 archers, raw army 3054, with 819 movement remaining and DEFENCE lock 2. Enemy arrival
was 0 with strength 2640. The original timing prohibition therefore rejected this
demonstrated productive response. Qualification9 removes only that obsolete test
body/registration and keeps the stronger, already-executed replacement unchanged.
Historical evidence remains in qualification8, not silently erased or turned green.

This does not prove survival in a resolved battle, optimal response to arbitrary
enemy moves, a win-rate increase or a more enjoyable opponent. The result supports
only the stated urgent spending, conservation, productive action and actual
controlled defensive-return requirements.

## Fixture corrections retained in the audit

Earlier invalid premises were diagnosed rather than weakening acceptance:
- First-turn income is skipped, so the one-shot event subtracts 7800 from normal
  Expert-AI 10000 gold to produce the declared 2200 budget.
- Omitted mine owner uses the native NEUTRAL default; the invalid explicit
  playerColor.neutral identifier previously left the object UNFLAGGABLE.
- Ordinary owned ore observers fill native visibility gaps. A disconnected fog
  path is not interpreted as genuine threat removal.
- The controlled approach avoids mine footprints and the engine's discounted
  final-diagonal movement exception; actual route premises remain mandatory.

## Bounds and unsupported scope

The target remains excluded from default builds and uses the existing 180-second
compile guard, 180-second case limit and 600-second aggregate qualification limit.
Require successful process exit plus exact testcase XML and artifact/map/source
hashes. Timeouts or unsupported actions are incomplete, never passes.

Courier exchange remains unsupported by this synchronous bridge. A future test
needs ordered query creation and real queued server acknowledgments; no troop
handoff success is claimed here.

Audit artifact: [incremental Linux qualification8 evidence](https://github.com/arpesterev/vcmi/actions/runs/40118626275/artifacts/12569679015),
ID 12569679015, name incremental-linux-9ebbf96e4615d5be24bb25a07261bba47a75e585-1.
Verified ZIP SHA256: 79ff1c7508065d7136bba68a5861cbabc6feb3cf474d459bcbc810f71c4e2c25.
The artifact expires 2026-10-31; the source commit and exact evidence summary above
remain durable references, not a promise of permanent artifact availability.

The acceptance-metadata description was updated for qualification9, but generated
map bytes are identical to qualification8:
SHA256 043dc4fcf13675ca13e9ad2d37fe9881fa7d2409020e43ffcb0927b74b95b88b.
