# FHeroes2 / HoMM3 play-readiness ledger

Baseline: accepted a47, 2026-11-13. This ledger distinguishes implemented native
behavior, incomplete source fidelity, and unexecuted delivery checks. Test counts
are regression evidence, not a completion percentage. Later WIP is not accepted
until its separate final normal-build evidence is recorded.

## Delivery gates

| Gate | a47 state | Required closing evidence |
| --- | --- | --- |
| Explicit selection | Source has launcher entries `FHeroes2Tactics` and `HoMM3OriginalTactics`; AIFactory constructs those controllers | Installed launcher selection, restart persistence, battle identity log on exact candidate |
| Complete-port identity | Reserved full IDs remain unavailable; entries explicitly say partial/experimental | Full source-function and host-rule coverage before changing identity |
| Linux native regression | Normal Release 3309/3309, directed 123/123, standalone 32/32; unchanged 23 inherited exclusions | Re-run after each candidate; report every exclusion separately |
| Lua surface | 55 exports byte-identical to a10 | Preserve or explicitly review each API change |
| Exact published candidate | Local durable checkpoint only; these later changes not published by this worker | Authorized commit/push and successful CI for exact SHA |
| Apple Silicon delivery | Packaging procedure/workflow exists, not exercised for a47 | Build/audit exact ARM64 package, signatures/dependency closure, install/start on target Mac |
| Real game reliability | Genuine server save/load before and after a factory FHeroes2-vs-FHeroes2 battle passed:7 activations,6 shots,4 melee,3 moves, terminal result and survivor/RNG restoration. No installed GUI or midbattle recovery | Both sides, open field and siege, mixed armies, spells, long combats, save/load and restart |
| Cheat regression | No a47 acceptance evidence for `vcmicolor` followed by mission completion | Reproduce with user scenario/save; fix and verify both with/without restart |
| Comparative strength | No broad controlled strength result | Seeded bidirectional complete battles, crash/illegal-action/draw statistics, confidence intervals |

## FHeroes2 concrete functional scope

| Area | Existing implementation | Remaining before full source/host fidelity |
| --- | --- | --- |
| Creature actions | Native legal melee/ranged/move/wait/defend, source-derived offense/defense/cover/pursuit, compelled Berserk priority | Exact archer kiting and castle positioning; all H3 manual creature abilities need explicit projection or documented non-equivalence |
| Strength scale | Whole-context source strength for certified innate rosters; canonical Crusader double melee and Marksman/Grand Elf double shooting | Unsupported effects/specials and other uncertified units retain whole-context native scale, not source parity |
| Pursuit damage | Pinned H2 endpoints/retaliation for ordinary melee and archer defenders, ordinary double-wide units and certified innate flyers; ordinary melee, shooting, blocked and depleted archer attackers; certified Archery10/25/50 and ordinary long-range source valuation for genuine shots; canonical Beholder/Evil Eye/Zealot no-melee-penalty exemption for blocked shooters; certified Bless/Curse attack and retaliation endpoints through native casts/counters, with canonical caster-school skills admitted; native H3 range reduction remains authoritative; ammo-cart and double-shot classification | Uncertified flying/specials, noncanonical Archery/artifact/specialty modifiers, wall penalties, other unmapped spell damage effects, siege geometry and full effect mapping |
| Damage magic | Source-style damage/kill/wake scoring with bounded direct, Fireball resistance, farewell-area and linked clone-cascade improvements | Complete area/chain/reflect/resistance/timing combinations; unsupported projections are declined rather than invented |
| Buff/control magic | Existing Haste/Slow, Ray, Slayer, Bless/Curse, Stone Skin, Bloodlust, Air Shield, Hypnotize, Blind, Berserk; native legality/preview | Complete source-effect timing, all custom/mixed effects and full source-equivalent dispel valuation |
| Other magic | Restoration, selective Cure/Dispel, summon/Clone, offensive Teleport, Earthquake, observed-spell Anti-Magic | Unobserved enemy spell pressure, full effect combinations; native Clone ammunition differs from H2 source inheritance |
| Withdrawal | Legal retreat/surrender and farewell spell sequence, ownership/autofight restrictions, native reauthorization, deathless-round limit | Complete enemy magic pressure and source/host-equivalent valuation in every ruleset |
| Engine ownership | VCMI remains authoritative for actions, geometry, mechanics, damage and results | Source H2 scoring does not change H3 mechanics; validate each intentional difference rather than claim identity |

## HoMM3 concrete functional scope

| Area | Existing implementation | Remaining before full source/host fidelity |
| --- | --- | --- |
| Ordinary turns | Bounded source all-shooter, all-melee and certified mixed-history dispatch; source-private RNG, native legal actions | General attack-position threat/forecast graph, full path planning, wide/flying/special/defended state coverage |
| History | Known source-facing/rank history, completed supported actions, retained known corpse rows, own noncasting heroes with no owned tavern | Unknown enemy/manual writer provenance and first-kill rank cannot be guessed; broad arbitrary history/caster census |
| Spell policy | Existing source-style damage/restoration/buff/control branches; bounded Prayer cancellation, direct Mirth including negative morale, direct targeted/mass Sorrow with resistance and known projected-dead suppression, direct Fortune with source target/damage projection inside the zero-initial-luck ordinary domain; direct Misfortune on certified positive-luck open-field shooters without crossing below native zero luck | Remaining caster families, exact cancellation/replacement combinations, source forecast equivalence rather than host-value approximations; Sorrow without a usable forecast still uses certified current-state membership, not proven post-simulation survival; Fortune nonzero initial luck; Misfortune melee, cross-zero and negative-luck variants; replacement/cancellation and arbitrary special/modifier targets remain unsupported |
| Retreat | a40 live early failed-siege branch plus a42 ordinary all-shooter plus a43 certified ground melee/mixed two-pass predicted-annihilation retreat for own noncasting >=2000XP AI attacker in open field. Siege branch: certified AI attacker, Hard+, tavern, standard win, legal flee, no equipped Shackles, closed gate, intact walkable wall segments, grounded non-shooting non-summoned attackers outside, ordinary defenders inside. Bounded surviving-army economic retreat supports empty inventory or sole canonical Catapult, zero Diplomacy/discount | Normal difficulty random gate, general uncertain-history/geometry/caster loss forecast and broader artifact/Diplomacy eligibility, defender tunnels, broader siege rosters. Human autofight retreat/surrender UI is intentionally excluded; pinned bot flow does not surrender |
| Specials/host rules | Partial source modifiers and native legality | Complete modifiers including Frenzy, creature specials and full reference traces |

## Acceptance sequence

1. Close a named source/host behavior with oracle and actual controller/server tests;
   preserve source before compile, then run normal all-consumer build and regression.
2. Assemble one exact candidate, preserve source/evidence and record remaining scope.
3. Publish/build only under current authorization; Linux CI and ARM64 packaging are
   separate gates, not implied by local native results.
4. Install and select each experimental backend in the actual launcher. Confirm the
   selected backend in the battle log, then save/load/restart and finish missions.
5. Execute representative mixed/siege/control/withdrawal games and the reported
   `vcmicolor` completion sequence before recommending this build for normal play.

These are combat AIs, not adventure-map planners. Neither a47 nor this ledger
establishes full original-AI parity or a calendar ETA.

Local Apple Silicon build commands, missing submodule pins and safety checks are in `../LOCAL_MAC_ARM64_BUILD.md`. This source-reviewed recipe was not executed on a Mac.
