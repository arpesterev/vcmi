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
| HoMM3Original (sushi-shi/homm3-decomp) | Independent partial ranged/melee/advance/jousting/siege decisions and hero damage/area, Bless/Bloodlust/Precision, Blind, Cure/limited Dispel and resurrection policies; native VCMI legality | Original combat-value and future-exchange simulation, full attack-position threat map, original path planning, remaining spellcaster policy and exact buff/debuff/Dispel valuation, creature specials, hero retreat/surrender, full reference trace parity |
| FHeroes2 (ihhub/fheroes2) | Native archer/guard/offense/defense, multi-turn pursuit, area shots, Berserk creature turns, partial hero damage/restore and buffs/debuffs, summons/Clone, offensive Teleport, Earthquake, selective Cure/Dispel, legal retreat/surrender/farewell and deathless-round limit | Anti-Magic and complete spell-effect timing/Dispel valuation, exact kiting and castle positioning, Heroes III manual creature abilities without a source equivalent, complete host-rule mapping and broad independent Heroes II/native decision comparison beyond the initial executable corpus |
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

- Commit `747784ed0f1984055bc70efc283e655662783d8d` passed both
  [pre-merge](https://github.com/arpesterev/vcmi/actions/runs/42380278212) and
  [post-merge](https://github.com/arpesterev/vcmi/actions/runs/42380715599) Linux
  CI. The main native phase enumerated 291 tests: 271 passed and 20 were
  explicitly model-gated. Separate V13/V15 phases exercised supplied official
  weights. Those results apply to that commit, not automatically to later work.
- Local execution of [pinned fheroes2](https://github.com/ihhub/fheroes2/tree/0dbfe9f386f33c2500842f4a59cd284d7f03d541) now supplies an independent no-UI source corpus:
  32 initial and 40 additional cases, each run uninstrumented and twice with
  observation. Source outcomes and RNG match across observation modes, and
  traces repeat. This does not equate Heroes II and Heroes III inputs or prove
  adapter parity. Four mapped native properties agree; complete ordered
  decision comparison remains unfinished. This reference corpus is not yet a CI gate.
- ARM64 commit `18eecbddc10c41eeea82f55f79f89d41420d3261` compiled and created
  a DMG, but the mounted audit rejected a Conan-cache rpath. No artifact from
  that run is a deliverable. A repaired build must pass the strict audit and
  upload before it can be offered for installation.

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
