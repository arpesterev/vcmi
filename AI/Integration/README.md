# Event-driven AI integration

## Status

**The full four-backend gameplay acceptance requirement is NOT met.**

This integration branch combines three **partial source-derived native combat
policies** with a strict neural-only factory route to VCMI MMAI. The source
ports make real decisions inside VCMI, rather than delegating all actions to
BattleAI, StupidAI or a fake match runner. Their tactical projections are not
complete or source-equivalent implementations.

| Requested complete ID | Selectable experimental backend | Verified implementation scope |
| --- | --- | --- |
| `HoMM3Original` | `HoMM3OriginalTactics` | Partial native damage, movement and ranged priorities from `sushi-shi/homm3-decomp` |
| `FHeroes2` | `FHeroes2Tactics` | Partial native archer, melee, defense and offensive/restorative spell policies from `ihhub/fheroes2` |
| `TUMEnchanted` | `TUMEnchantedTactics` | Documented TUM adjustments on the partial HoMM3 baseline; the original compiled plugin is not ported |
| `MMAI` | `MMAI` | VCMI MMAI neural model inference; factory selection refuses scripted-model fallback, while hero spell selection is not neural |

The three reserved complete IDs are deliberately rejected by `AIFactory`
instead of being aliases for partial implementations. The partial IDs and
`MMAI` are combined in the launcher selection and tested through real
native VCMI battle callbacks and server-applied actions. The test harness
records the actual winner and each backend's engine-accepted action histogram;
both sides must act in mixed-policy battles. Optional public, checksum-pinned
MMAI ONNX models are only installed temporarily by the development CI jobs.
The full four-policy equivalence checker below has **not** been satisfied.

`BattleEventAdapter`, `EventChannel`, `DecisionSession` and
`BackendRegistry` are integration and protocol contracts. They are not
proofs of policy equivalence or replacements for independently ported combat
decisions. Legacy direct `Router(false)` clients may still explicitly
permit scripted MMAI fallback; factory-selected `MMAI` uses strict mode.
No gameplay rules, save format, default AI or release workflow are changed.

## Boundaries

The engine already delivers combat events through `IBattleEventsReceiver` and
`CBattleGameInterface`. `BattleEventAdapter` forwards every callback, including
both initialization overloads, tactics, spell effects, obstacles and siege
state. Delivery is synchronous on the engine's callback thread; native pointers
are never saved for later use. `EventChannel` adds per-instance sequence and
begin/complete/failed receipts. It does not add a second game-rules event bus.
An event-completion receipt confirms delivery, not that a game command succeeded.

`DecisionSession` is the protocol for future semantic ports and worker bridges.
It is not yet connected to native MMAI commands or the adventure planner.
Its owner thread consumes owned observations and explicit decision events. Only
player-visible state may enter an observation; the engine remains authoritative
for legal actions and applies commands through its normal callback machinery.

A command includes session, request, triggering event and observation revision.
One request may be pending at a time. Acknowledgement is matched by request ID;
accepted commands need an applied, newer observation before another decision.
Rejected actions cannot repeat on unchanged state. Timeouts with unknown outcome,
missing decisions, stale state, event gaps and rule mismatches fail the session.
Reentrant acknowledgements are queued; there is no polling or implicit retry.
Each new game/load must allocate a fresh session ID. The embedding engine must
supply timeout events and a transport; this header does not create worker threads.

`BackendRegistry` requires a real factory, pinned source and adapter commits,
and explicit ruleset support. Registration freezes after a backend is created.
This metadata is necessary but not sufficient to prove an authentic upstream
implementation. It is a contract for ports, not a loader for ERA DLLs or an
automatic Heroes II-to-Heroes III rules translator.

## Checks

Run the portable contract and evidence-checker tests:

```sh
cmake -S AI/Integration -B out/ai-adapter-contracts -DAI_ADAPTER_SANITIZERS=ON
cmake --build out/ai-adapter-contracts
ctest --test-dir out/ai-adapter-contracts --output-on-failure
```

These tests use synthetic policies and traces. They are not gameplay evidence,
not a full VCMI build, and not proof of any project's playing strength.

`CI/check_ai_backend_acceptance.py evidence/report.json` checks actual battle
evidence supplied by independent upstream and VCMI runners. Missing evidence
fails; there is no passing example report checked into this repository.

The report has schema version 1 and four distinct `providers`. Each provider
requires `id`, `selected_in_vcmi`, pinned `source_revision` and `adapter_revision`,
and four `cases`: `melee`, `ranged`, `spells` and `siege`. Each case specifies
`initial_state_sha256`, normalized `ruleset`, `seed`, and separate `upstream` /
`vcmi` trace descriptions containing a relative `path` and `sha256`.

Each JSONL trace has contiguous `seq` values. Its first event is `backend_loaded`
with provider, source revision, initial-state hash, ruleset and seed. MMAI also
needs a loaded `model_sha256`. Every `decision` names the actual provider/source,
normalized `state_sha256`, `action` and `legal_actions`. `damage_applied` records
actual nonnegative damage, and the final `battle_finished` names an `attacker`
or `defender` winner. At least one decision and positive applied damage are
required. Decisions and outcomes must match the independently run upstream
reference. Fallback, unsupported mechanics, errors and timeouts fail acceptance.

The checker validates evidence consistency, not the honesty of its producer.
Independent source runners, engine trace export in this schema, UI selection
proof and the three missing semantic ports remain required. Copying a trace or
renaming the same backend cannot establish project equivalence. Do not weaken
this requirement to make four unavailable entries appear selectable.
