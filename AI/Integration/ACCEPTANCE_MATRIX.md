# Four combat AI backends: delivery and acceptance matrix

Status as of 2026-11-03. This is a **requirements inventory**, not a claim that the
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
| HoMM3Original (sushi-shi/homm3-decomp) | Independent partial ranged/melee/advance/jousting/siege decisions; native VCMI legality | Original combat-value and future-exchange simulation, full attack-position threat map, original path planning, spellcaster policy including buffs/debuffs/dispels, creature specials, hero retreat/surrender, full reference trace parity |
| FHeroes2 (ihhub/fheroes2) | Native archer/guard/offense/defense, area shots, partial hero damage/restore and projected Haste/Slow speed effects | Remaining spell effects/dispels, summons, creature spells, multi-turn melee/retreat, castle positioning, Hero II source-equivalence and explicit Hero II vs VCMI rule mapping |
| TUMEnchanted (SadnessPower/TUM-Enchanted) | Explicit HoMM3 partial baseline plus published ballista, neutral shooter protection, siege hold, decoy and Hypnotize overrides | ERA/DLL implementation not available in equivalent source. Reconstruct and independently validate remaining documented tower/spell/creature/siege rules. **Exact parity cannot be asserted from the public description** |
| MMAI (smanolloff/vcmi-gym and official VCMI MMAI) | Genuine ONNX V13/V15 stack inference; attacker/defender/siege roles; strict no scripted fallback | New spell-capable observation/action schema and newly compatible trained weights before claiming neural hero spells; declared policy for war machines and special actions; model preflight/GUI failures; long symmetrical self-play termination with source-grounded behavior |

## Ordered implementation slices

| Milestone | Deliverable | Hard gate |
| --- | --- | --- |
| M1 (in progress) | Source-derived FHeroes2 Haste/Slow decisions using hypothetical VCMI battle and real combat | Model-free source contracts, engine-accepted buff and debuff casts, completed battles, all existing CI |
| M2 | HoMM3 attack/retaliation and position scoring, then original hero spell policy | Independent comparison with pinned decomp code, distinct decisions in real engine battles |
| M3 | Remaining fheroes2 buffs/debuffs and special creature abilities | Spell-by-spell coverage report and real battle demonstrations |
| M4 | TUM documented tower/creature/siege/spell differences | Published rule to test mapping, explicit uncertainty for inaccessible DLL behavior |
| M5 | MMAI neural action/schema + model training, neural spell actions | Successful spell-selection inference with published/proven model, no scripted substitute |
| M6 | Real launcher and platform acceptance, batch statistical comparisons | Every acceptance gate, all PR checks, reviewed human sign-off before merge |

## Evidence and controls

The existing `CI/check_ai_backend_acceptance.py` intentionally fails when
independent evidence is missing. Do not weaken it to label partial ports as
full ones. The existing `AI/Integration/README.md` documents the JSONL trace
schema and action identity requirements. All new work goes to draft branches;
do not change VCMI game rules, default AI, release workflows, or publish
binaries/models/Actions artifacts. Green build and smoke tests do **not**
establish upstream-equivalent behavior.
