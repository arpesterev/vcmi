# Event-driven AI integration

## Status

**The four-backend gameplay acceptance requirement is NOT met.**

This change adds a source-aware registration contract, an event/command protocol,
a native callback adapter and an evidence checker. It does not contain semantic
ports of Heroes III, fheroes2 or TUM Enchanted. The existing MMAI is forwarded
through `BattleEventAdapter`; its policy, models and intentional internal
fallbacks are not replaced or improved by this wrapper.

| ID | Source | Native VCMI implementation in this change |
| --- | --- | --- |
| `HoMM3Original` | `sushi-shi/homm3-decomp` | Not implemented |
| `FHeroes2` | `ihhub/fheroes2` | Not implemented |
| `TUMEnchanted` | `SadnessPower/TUM-Enchanted` | Not implemented |
| `MMAI` | `smanolloff/vcmi-gym`, VCMI `AI/MMAI` | Existing native implementation, event wrapper added |

Unimplemented IDs are not exposed as working launcher choices. The factory
rejects these names instead of silently constructing `EmptyAI`. Existing
`BattleAI`, `StupidAI`, `Nullkiller2` and explicit `EmptyAI` keep their identities.
No save format, game rules, default AI selection or private game data is changed.

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
