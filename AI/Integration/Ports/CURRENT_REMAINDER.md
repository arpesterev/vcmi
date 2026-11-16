# Combat-port remainder (accepted a53)

This catalog describes implementation boundaries, not estimated percentages or an ETA. The user-facing a51 Apple Silicon source handoff is frozen separately. Accepted a53 has3369/3369 Linux native cases; its checkpoint README records evidence and limits.

## FHeroes2

- Ordinary damage/strength, blocked-archer melee, direct ranged and ordinary melee priorities have real source-backed paths. Unsupported effects/creature abilities and native-only interactions still fall back to documented H3 projections. They are not complete H2 arithmetic for arbitrary armies.
- Cautious advance has an a52 bounded open-field single-hex path-risk change. Source threat is evaluated at CURRENT positions; reachability uses visible native H3 obstacles and occupancy. Wider footprints, siege and uncertified combinations retain prior fallback. H2/H3 path and geometry differences are not erased.
- Cover, archer escape, castle positioning and special/area attacks exist, but lack exhaustive decision-by-decision source parity across all geometry and effect combinations. Their existence is not a completion certificate.
- Spells, cancellation, duration and simultaneous-effects mapping remain bounded. Random resistance/reflection outcomes are not a complete joint simulation. Enemy magic intelligence deliberately uses public observations rather than private enemy state.

## HoMM3

- General target census, path/retaliation production and combat-to-completion simulation are incomplete. Existing ordinary shooter/ground/defense forecasts support certified domains, not arbitrary wide/special/siege/roster-changing battles.
- Direct Weakness, Mirth, Sorrow, Fortune and Misfortune branches are PRESENT; older lists marking them absent are stale. Their certificates remain bounded. Hypnotize/Berserk, Anti-Magic, Teleport, Sacrifice and Earthquake are not generally admitted by the direct chooser; completion scope must distinguish source behavior from H3-only features.
- Cancellation is narrower than direct casting. Attack buffs, Curse, Blind, Paralyze, Mirth (including bounded negative-base cancellation in a53) and bounded Prayer removals do not imply arbitrary protective/speed/economic or multi-effect cancellation parity.
- Early failed-siege retreat exists. General loss forecasting, economic/artifact evaluation, normal-difficulty randomness, defender escape-tunnel and surrender coverage remain incomplete. Human/autofight protection remains intentional.
- Native damage rounding, special traits and joint stochastic outcomes still have explicit projection/refusal boundaries.

## Playability and scope

- Neither combat port is an adventure-map planner.
- Fresh Mac ARM64 build, launcher choice persistence, GUI combat/save-load, campaign transitions and real per-decision latency remain separate acceptance work. Linux suite runtime is not decision latency.
- vcmicolor followed by mission completion requires client/render/video reproduction. Headless battle tests cannot establish its fix.
- No parity percentage, complete-AI claim, fresh DMG claim or Mac pass follows from this catalog.
