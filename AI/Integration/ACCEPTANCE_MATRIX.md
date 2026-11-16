# Four combat AI backends: delivery and acceptance matrix

Status as of 2026-11-07. This is a **requirements inventory**, not a claim that the
four original policies are implemented or equivalent. Keep full-port factory
identifiers unavailable until the corresponding evidence exists. The selected
experimental versions must remain explicit and must never silently invoke
BattleAI, StupidAI, another AI, or an unannounced scripted MMAI fallback.

## Definition of done

Each backend must pass all gates:

1. **Source identity:** pinned revision, license review, a function-level
   source-to-adapter mapping, and explicit host-rule projections.
2. **Functional coverage:** all action classes used by the original policy
   are either implemented or the full-port identifier remains unavailable:
   tactics, melee, ranged, movement, retaliation, spells/abilities, siege
   weapons/defense, hero retreat/surrender, and special creatures.
3. **Engine integration:** selectable in the launcher, model preflight where
   required, all events and errors forwarded with identity, a real command
   acknowledged by the VCMI game handler and genuine game result.
4. **Reference evidence:** deterministic representative battle states
   evaluated independently in the upstream engine (same rules whenever
   possible); normalized decisions and action legality compared to VCMI.
   Translate nonidentical Heroes II/III mechanics explicitly, never hide them.
5. **Reliability:** melee, ranged, control effects, buffs/debuffs, unit sizes,
   path blockers, siege and mixed armies across all 12 ordered pairings;
   no silent fallback, endless loops or unexplained invalid actions.
6. **User acceptance:** actual launcher selection and start in GUI on Linux
   and macOS, installation errors, missing models, restart/save behavior.
7. **Measurement:** repeatable seeded batch results with both orientations,
   actions, damage, spell counts, time, illegal moves, crashes and draws.
   Report confidence intervals; do not use one smoke battle as win-rate proof.

## Source-implementation workstreams

| Backend | Implemented in experimental branch | Missing before full identifier |
| --- | --- | --- |
| HoMM3Original (sushi-shi/homm3-decomp) | Source-style unit/casualty valuation, partial ranged/melee/advance/jousting/siege decisions and hero damage/area, Bless/Bloodlust/Precision, Blind, Cure/limited Dispel and resurrection policies; native VCMI legality | Complete modifier mapping (including Frenzy), future-exchange simulation, full attack-position threat map, original path planning, remaining spellcaster policy and exact buff/debuff/Dispel valuation, creature specials, hero retreat/surrender, full reference trace parity |
| FHeroes2 (ihhub/fheroes2) | Native archer/guard/offense/defense, multi-turn pursuit, area shots, Berserk creature turns, partial hero damage/restore and buffs/debuffs, summons/Clone, offensive Teleport, Earthquake, selective Cure/Dispel, observed-spell Anti-Magic, legal retreat/surrender/farewell and deathless-round limit | Unobserved enemy spell pressure and complete spell-effect timing/Dispel valuation, exact kiting and castle positioning, Heroes III manual creature abilities without a source equivalent, complete host-rule mapping and broad independent Heroes II/native decision comparison beyond the initial executable corpus |
| TUMEnchanted (SadnessPower/TUM-Enchanted) | Explicit HoMM3 partial baseline plus published ballista, neutral shooter protection, siege hold, decoy and Hypnotize overrides | Public AI.erm tower rules and published Battle_AI pursuit-penalty C++ are available, with reuse terms unestablished. Implement and independently validate those rules and the remaining spell/creature/siege behavior, including the automatic server-action boundary. **Source availability and static formula recovery are not runtime parity** |
| MMAI (smanolloff/vcmi-gym and official VCMI MMAI) | Genuine ONNX V13/V15 stack inference; attacker/defender/siege roles; strict no scripted fallback | New spell-capable observation/action schema and newly compatible trained weights before claiming neural hero spells; full special-action coverage beyond existing source-native handlers; installed GUI/model-error acceptance; long symmetrical self-play termination with source-grounded behavior |

## Current implementation order

1. Finish FHeroes2 and HoMM3 source-function coverage, as the requested priority.
   Keep an explicit distinction between source decisions, native Heroes III
   rule projections, and missing branches. Haste/Slow and the initial HoMM3
   hero-spell subset already have native server tests; they are not the full
   remaining scope.
2. Qualify each subsequent spell/timing/path/forecast increment in the real
   engine before publication, then run exact-commit CI. Do not count prepared
   test bodies or standalone scalar checks as executed native evidence.
3. Produce and audit an experimental ARM64 package, then test actual installed
   launcher selection, persistence, game startup and representative battles.
   Static bundle checks do not establish GUI or gameplay acceptance.
4. Preserve TUM and MMAI work while the first two are completed. Resume their
   missing rules/model work separately; do not substitute scripted hero spells
   for trained MMAI hero-spell inference.
5. Run broader seeded, bidirectional comparisons only after the corresponding
   policies and reference fixtures are qualified. No current smoke corpus
   establishes comparative playing strength or complete parity.

## Established evidence boundaries

- The next cover increment follows source `meleeUnitDefense` selection across
  all friendly archers, combining ordered cover positions and blocker outcomes.
  Nineteen focused native cases passed: thirteen new cases and six older cases
  with independently corrected premises. The full local run passed 356 of 376
  cases, with exactly the same 20 model-gated skips. Output contracts (48),
  Nullkiller (145), and the existing four source properties also passed.
  Defensive infantry SKIP now proves it already occupies preferred cover.
  Narrow and wide fortified-town cases prove exact reachable cover steps,
  natural activation, server acceptance and both occupied cells inside walls.
  This does not close the live-tower activation, attack valuation, native
  splash mapping or complete source-policy gaps. Remote exact-commit CI and
  the expanded Mac gate remain required before claiming this increment there.

- Commit `bbc23d095500a9a05fe5d6f33df37fd9fddfd645` passed both
  [pre-merge](https://github.com/arpesterev/vcmi/actions/runs/42384115632) and
  [post-merge](https://github.com/arpesterev/vcmi/actions/runs/42384830098) Linux CI.
  Later commit `da37f6300bd8d92f42274f05ecc072d4ec10d4ea`
  [passed Linux CI](https://github.com/arpesterev/vcmi/actions/runs/42390778484):
  349 main native cases, 329 passed and 20 explicitly model-gated. Separate
  V13/V15 phases exercised official weights. These results are commit-specific.
- That increment added five FHeroes2 generic defensive-area
  cases and ten HoMM3 event-lifecycle cases. The full 349-case local native
  run passed 329 cases with exactly 20 model-gated skips. Output contracts
  (48), Nullkiller regressions (145) and the four mapped source properties
  also passed. The defensive fixtures preserve legal full-footprint movement
  and controller-relative positioning. Old cover tests now isolate their
  intended branch with real server actions, without forced activation or
  weakened command assertions. The later cover increment below separately qualifies multi-archer selection.
- The next HoMM3 unit/loss valuation increment uses pinned reconstructed
  `getUnitCombatValue` and `ComputeAttackEffect` formulas consistently for
  primary attacks, retaliation, collateral, blocking and opportunity costs.
  Six new native scenarios cover wound thresholds, two-shot volleys, original
  wounds during retaliation, clones, primary/splash scale and stable repeated
  decisions. Full local native execution passed 335 of 355 cases with the
  same 20 model-gated skips. The tower comparison now uses native Basic
  Artillery control and an actual pending activation, not a forced active-unit
  assignment; its source and independent casualty-value brackets both pass.
  Public native modifiers and unknown enemy Artillery remain explicit host
  projections. Frenzy conversion is still a separate gap. Scalar comparisons
  against the pinned reconstructed bodies are not retail runtime proof.
- The subsequent Anti-Magic increment records only public enemy hero-cast
  events, then uses visible hero power/mana and native immunity checks. It
  preserves the source positive-effect veto, negative-effect removal bonus,
  resurrection cap and original-friendly target restriction. Eight focused
  native cases and the full 363-case local run passed (343 passed, 20 gated),
  with independent output/Nullkiller/reference checks. A terminal test-observer
  lifetime error and a premature turn-owner premise were corrected without
  changing production policy or the checked protection/ownership assertions.
  Unlike original FHeroes2, this projection cannot protect preemptively from
  an unknown enemy spellbook; mastery/artifact uncertainty is not hidden by
  reading private hero fields. It is not complete source magic parity.
- HoMM3 order/prefix and event-lifecycle kernels remain test-only. The latter
  tracks actual pre-action WAIT evidence and post-apply round changes, keeps
  initial ownership slots, and invalidates planning during hero actions.
  Unknown late-registration phase and created-unit identity are still explicit
  unsupported mappings. No complete production combat forecast, winner
  estimate or retreat policy is claimed. These supporting kernels do not
  activate a production forecast.
- Local execution of [pinned fheroes2](https://github.com/ihhub/fheroes2/tree/0dbfe9f386f33c2500842f4a59cd284d7f03d541) now supplies an independent no-UI source corpus:
  32 initial and 40 additional cases, each run uninstrumented and twice with
  observation. Source outcomes and RNG match across observation modes, and
  traces repeat. This does not equate Heroes II and Heroes III inputs or prove
  adapter parity. Four mapped native properties agree; complete ordered
  decision comparison remains unfinished. The initial 32-case corpus now has
  a separate reproducible CI source job, native four-property capture and
  fail-closed same-checkout pairing. Fresh local compilation of all 249 source
  objects and 96 processes passed with the pinned 965-file archive. Its first
  [remote execution on 8fda4e8d](https://github.com/arpesterev/vcmi/actions/runs/42392784250)
  passed with a clean, matching commit/tree in source, native and pairing
  artifacts. This covers only the four declared coarse properties, not
  identical inputs or complete ordered decisions.
- ARM64 commit `ecd48d9296d3647685168ac8c03373e5d5479cbc`
  [passed packaging](https://github.com/arpesterev/vcmi/actions/runs/42382614048):
  the mounted DMG, 35 thin ARM64 images, dependency closure and deep strict
  application signature were verified. The DMG SHA-256 is
  `3007995fc3a35aafdb4d040e477e72e4dfbea7c9db5e6d1ba53abed2d581e132`.
  This ad-hoc-signed package predates `bbc23d0`; it is not notarized and does
  not establish installed GUI or gameplay acceptance.
- [Mac run 42387085354](https://github.com/arpesterev/vcmi/actions/runs/42387085354)
  at `707183d` passed compilation, disk-image integrity and the strict bundle
  audit. Both server CLI commands exited zero, but version parsing failed;
  package upload and native regression execution were skipped. No artifact
  from that run is a deliverable. Source inspection and a Linux server probe
  reproduce an ANSI reset before the version line. The revised smoke parser
  strips only SGR colors, retains the exact full-SHA and isolated-HOME checks,
  and logs escaped raw output before validation. Exact DA37
  [Mac run 42391258270](https://github.com/arpesterev/vcmi/actions/runs/42391258270)
  passed packaging, static audit and both actual ARM64 server CLI commands.
  Its raw version output confirms the diagnosed SGR prefix. Native test
  compilation then failed Clang's nonvirtual-destructor warning for the
  test-only `NativeLifecycle` receiver, so none of its 522 runtime cases ran.
  The receiver is now explicitly final, matching its concrete ownership and
  lack of subclasses; no warning is suppressed. The current required native
  ARM64 gate is 536 genuine passes, not a report of completed Mac tests. See [the acceptance procedure](MACOS_ARM64_ACCEPTANCE.md).

## Evidence and controls

The existing `CI/check_ai_backend_acceptance.py` intentionally fails when
independent evidence is missing. Do not weaken it to label partial ports as
full ones. The existing `AI/Integration/README.md` documents the JSONL trace
schema and action identity requirements. Review experimental changes on a
staging branch and require exact-commit CI before the authorized non-forced
integration into develop. The dedicated ARM64 workflow may publish requested
experimental build artifacts after its checks pass. Do not change VCMI game
rules, default AI, security settings or release channels, or redistribute game
data/model weights as part of that package. Green build and smoke tests do
**not** establish upstream-equivalent behavior.

## Experimental integration after merge

Merging these experimental backends into `develop` does **not** certify the requested complete `HoMM3Original`, `FHeroes2`, or `TUMEnchanted` algorithms or a fully neural MMAI spell policy. The factory must keep the distinct partial `*Tactics` identifiers; do not alias the reserved full-port identifiers to them. Existing gameplay rules, default AI, release behavior and save compatibility remain unchanged. Future development must close the independent upstream-reference, gameplay, model and GUI gates listed above before claiming the full four-backend deliverable.
