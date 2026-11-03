# Native source-policy ports

## FHeroes2Tactics

`FHeroes2Tactics` is a partial, experimental combat policy constructed by the
normal VCMI `AIFactory` and called through `BattleEventAdapter`. It is not the
complete fheroes2 AI and is not registered as the acceptance ID `FHeroes2`.
It never delegates decisions to BattleAI, StupidAI or MMAI.

The stance predicate and melee-outcome comparator come from fheroes2
`0dbfe9f386f33c2500842f4a59cd284d7f03d541`, `src/fheroes2/ai/ai_battle.cpp`.
VCMI supplies legal positions, pathfinding, damage estimates and combat-value
projections. Those projections are not fheroes2's `GetStrength` or
`evaluateThreatForUnit`; identical behavior to upstream is not claimed.

Implemented engine actions: ranged attack, reachable melee attack, movement,
defence, catapult shots, first aid and ending the tactics phase. One unit
command is submitted for each active-stack event. Command validity and all
state changes remain the responsibility of the ordinary VCMI game handler.

Implemented since PR #9: deterministic legal-target scanning, damaging hero
spells and health restoration via VCMI spell simulation, with mana conservation
and no duplicate stack command during spell resolution. This is a projection of
source priorities, not an upstream decision-equivalence claim.

The fheroes2 stance analysis now counts a live ranged stack with remaining
movement as ranged strength even when adjacent enemy melee temporarily blocks
its shot. This follows upstream `analyzeBattleState`'s `isArchers()` check
instead of conflating unit classification with VCMI's immediate
`battleCanShoot()` legality. A native battle regression checks the actual
blocked shooter and defensive response before completing the fight.

The fast-archer retreat applies even when adjacent slow melee enemies
block the current shot. This matches the upstream order of operations:
`archerDecision` checks for a safe retreat before its blocked-shooter melee
branch. A differential native battle test verifies that a blocked Grand Elf
moves away under FHeroes2Tactics while HoMM3OriginalTactics takes a different
action, and the game engine accepts the retreat and finishes the battle.

If an archer's shot is blocked and no safe retreat exists, the pinned upstream
`archerDecision` selects an adjacent melee target by inflicted hit-point damage
minus estimated retaliatory hit-point damage. Our VCMI projection applies that
priority before the general melee combat-value scorer, including native
**two-hex ranged creatures** when VCMI confirms legal melee contact. Tests
verify the damage-based target choice and an engine-accepted blocked Medusa Queen
melee attack followed by a completed battle. Wide-archer retreat now checks both occupied hexes of the archer and
its enemies, including hypothetical legal VCMI destination footprints. A
native Medusa Queen regression places slower infantry adjacent only to its
tail, applies a test-only speed bonus to satisfy the upstream retreat gate,
and verifies a real server-accepted escape followed by a finished battle.
This models the pinned fheroes2 `Battle::Position` decision rather than just
the head tile; enemy path reach, danger scoring and exact upstream damage
simulation remain approximations.

The pinned fheroes2 `BattlePlanner::isPositionLocatedInDefendedArea` also checks
**both occupied cells** of a double-wide melee stack, including in castle
defense. The VCMI projection now uses both native occupied hexes and the
authoritative inside-walls classification during an actual fortified siege,
rather than classifying only the stack's head by the battlefield midpoint.
A native regression checks a sheltered double-wide castle guard's decision
and completes the siege through the engine. Boundary head/tail cases are
separately pinned in the deterministic predicate test; their physical
reachability in live castle layouts is not yet established.

Pinned fheroes2 `meleeUnitOffense` replaces the prior distant movement
target when the new threat/distance priority is **equal**, not only greater.
The native predicate now preserves this last-visited tie behavior. VCMI visits
candidate targets in sorted unit-ID order; original fheroes2 army enumeration
may differ. This correction has deterministic contract coverage, not a new
source-equivalence or win-rate claim.

Source-derived experimental fheroes2 extensions also cover melee pursuit of
non-evasive targets, fast-shooter retreat from slower ground enemies, and
melee screens for threatened friendly archers, including **double-wide archers
and double-wide melee guards**. Cover placement and immediate melee threats
consider both cells of a wide shooter. Candidate guard positions use the
full footprint of a wide melee guard and VCMI's legal head destinations, rather
than rejecting wide guards or measuring from their heads alone. Native
regressions exercise a real Medusa Queen protected by infantry and by a
double-wide Hydra, require engine-accepted cover movement, and then finish
the battles through the game engine. These are bounded VCMI projections of
pinned fheroes2 `meleeUnitDefense`, not a full source-equivalent port:
source-specific flank preferences, defensive spacing, kiting and spell-area
responses remain incomplete.

Not implemented: complete upstream buff/debuff and creature-spell selection,
retreat/surrender, complete archer kiting and screen placement, exact siege positioning, upstream battle
valuation, adventure-map planning, and support validation for arbitrary mods.
Do not use this partial policy as a quality replacement for the default AI.
No source-policy equivalence evidence has been produced.

## HoMM3OriginalTactics

`HoMM3OriginalTactics` is a separate, selectable experimental implementation.
It must **not** be confused with the reserved complete-port ID
`HoMM3Original`. It constructs its own `HoMM3TacticsBattleAI` through the
same event adapter, without calling BattleAI, FHeroes2Tactics, StupidAI or MMAI
for action selection.

The source-derived ranged target score is based on
`sushi-shi/homm3-decomp` at
`e12906accf96c2662b1abadbc49ddc111991bf21`,
`src/ai_tactical.cpp`, `type_AI_combat_parameters::getRangedAttackValue`.
This recovered source is CC0-1.0. Candidate damage and counterattack cost
use native VCMI projections instead of the original game combat simulator.
The original target-time and incapacitation conditions are only approximated
with VCMI distance/speed and frozen status.

The pinned HoMM3 `type_AI_attack_hex_chooser::check_adjacent_hexes`
breaks equal-value attack-position ties within the same arrival turn by
preferring a longer path for Cavalier/Champion jousting and a shorter path
for other troops. The native port applies this rule only after VCMI legal
move/attack filtering and equal projected exchange scores. Contract tests
pin both directions; a completed engine battle checks a Cavalier's
first real charge against a nearby one-hit target. This is not the full
HoMM3 attack-position evaluator or its global enemy-threat map.

The first implemented actions are ranged attack, melee attack, approach,
defence, catapult, first aid and end of tactics. There is no source-equivalence
claim for movement, spell selection, hero retreat, battle outcome or campaign
planning. UI labels this as a partial port and does not claim that the
complete original HoMM3 AI has been delivered.

## TUMEnchantedTactics

`TUMEnchantedTactics` is an **experimental, selectable, partial** tactical
projection of the published TUM Enchanted combat changes. TUM Enchanted is an
ERA mod of HoMM3 rather than a standalone AI implementation. Its upstream
[AI Improvements.txt](https://github.com/SadnessPower/TUM-Enchanted/blob/475d80bbf3e935ac854bcb6e68ef02019efb8de3/AI%20Improvements.txt)
documents combat behavior, but the released combat modifications are in
compiled ERA/DLL components, not source suitable for direct porting.

The TUM backend explicitly composes the experimental HoMM3 baseline with
five independently implemented overrides derived from the documentation:

- **Ballista:** excludes targets affected by Blind, Paralyze or Stone Gaze;
  with no remaining legal target, it defends instead of waking a disabled
  stack. We check actual VCMI spell-effect bonuses, not `isFrozen()` alone,
  because the latter only detects Stone Gaze.
- **Shooter protection:** when facing neutral melee-only stacks, allied
  ranged troops still have usable shots, and no neutral stack can reach the
  friendly army in its next movement, the active friendly melee stack defends
  instead of rushing forward. Presence of enemy shooters is checked using
  the native ranged-unit classification, not immediate `battleCanShoot()`:
  an archer under Forgetfulness is still an enemy shooter for this conservative
  interpretation of the published TUM rule. Friendly shooters need remaining
  usable shots, even if their current firing line is blocked. Movement reach
  is evaluated with VCMI pathfinding. The documented exception for
  neutral-controlled armies outside siege applies.
- **Fast-stack decoys:** when no immediate melee attack or ranged shot is
  available, prefer an accessible slower enemy over a nearby enemy that is
  at least two speed points faster than the acting melee stack and located
  within roughly two of its movement turns. Proximity uses the **nearest
  occupied hex of each stack**, including double-wide enemies and actors,
  rather than the heads alone. The deterministic test pins the boundary
  where a wide target's head falls outside the radius but its tail is inside;
  the native Griffin encounter checks actual occupied cells and completes a
  VCMI battle. If no movement toward the slower stack is possible, preserve
  the original HoMM3 route. The published TUM mod describes avoiding nearby
  high-speed distractions but does not disclose the precise scoring. This
  remains a bounded movement-only approximation, not original equivalence.
- **Hypnotized attack preference:** a genuinely hypnotized creature chooses
  between all legal native shots and reachable melee attacks using estimated
  damage dealt **plus retaliation received**, rather than automatically
  prioritizing shooting. The comparison is based on VCMI damage estimates,
  not TUM's unpublished damage simulator. A live Hypnotize regression with
  legal ranged and melee alternatives checks that the higher-scoring action
  is accepted by the game server and the battle reaches an actual result.
- **Fortified siege defense:** melee defenders entirely inside castle walls
  hold while the gate remains closed or blocked and no enemy has breached
  the walls or reached melee. They resume normal actions after a breach
  or an opened/destroyed gate. Both the defending unit and enemies are
  checked by **their full occupied footprint**: a wide defender with an
  exposed tail cannot use the fortified hold, and a wide attacker with an
  inside-wall tail counts as a breach. The isolated predicates have head/tail
  regression checks; native siege completion is checked separately, but
  either boundary case has not been validated as a reachable live siege
  configuration. This is a deliberately conservative VCMI interpretation
  of TUM's documented increased preference for staying within fortifications,
  not a reconstruction of its unpublished scoring.

All remaining attacks, movement and siege-machine behavior explicitly uses
the partial HoMM3 tactical policy, not an unannounced BattleAI/EmptyAI
fallback. The policy never changes VCMI mechanics. The separate launcher
entry is `TUMEnchantedTactics`, **not** the reserved full-port ID
`TUMEnchanted`.

Still unimplemented or not upstream-equivalent: the full TUM defensive spell
aggregation rules, improved tower targeting, clone/antimagic/cure selection,
distraction avoidance, full siege valuation, exact fast-stack pursuit scoring, creature ability choices, buff/debuff
planning and the rest of the compiled ERA AI modifications. Native battle
smoke tests do not establish those behaviors or a match win-rate improvement.

## Native battle regressions

`AIIntegrationNativeBattle` creates actual VCMI battles, obtains the active
stack from the engine, requests an action through the factory-created policy,
and sends that action to `BattleProcessor::makePlayerBattleAction`. The test
never chooses an action or forces a winner. Each battle must reach an actual
`BattleResult`, cause damage and stay below 512 stack activations.

The in-process harness is not the graphical client or a full adventure-map
match. Passing it proves that the partial policy can execute these battles,
not that it reproduces the complete upstream algorithm.

The reserved complete-port names must still fail explicitly. In particular,
`FHeroes2` and `HoMM3Original` must not silently create the partial ports.

## MMAI special-unit behavior and neural evidence

The upstream MMAI V13/V15 backends use deterministic, built-in native
auto-actions for first-aid tents, catapults and arrow towers; these actions
are **not** inferred from ONNX. This is source-native MMAI handling, not a
fallback to VCMI BattleAI, and battle results still come from the real
engine. Model-backed creature turns must be distinguished from these
automatic unit turns when evaluating combat behavior.

The model-gated native harness now records
`mmai_model_eligible_activations` separately from
`mmai_automatic_activations`. Its neural decision assertions exclude
the three kinds of source-automatic war machines; the numbers remain
execution evidence, not independently instrumented inference counts.
The dedicated MMAI siege-attacker versus TUM partial-policy fixture
checks that model-eligible creatures act, both controllers receive real
turns, authoritative siege catapult packets occur and the engine decides
the result. In this fixture, the catapult packets are generated by ordinary
siege processing; no MMAI active-stack callback for a catapult was observed.
They must **not** be attributed to MMAI's neural decisions or even to its
source-native special-unit handler. The router selects the generic attacker
model when no `attacker.siege` model is configured. Neither case proves
that MMAI has learned siege-machine or hero-spell decisions.

## MMAI neural-only hero spells

The installed, official MMAI 1.8 ONNX policies choose unit actions, not hero
spell targets. Factory-selected `MMAI` must never ask the unrelated scripted
`BattleEvaluator` to choose spells while reporting a neural-only backend.
Both neural action schema versions (V13 and V15) therefore suppress the
legacy spell heuristic in strict mode, even when autocombat spells are enabled,
and issue an explicit battle-start warning. Existing intentionally hybrid
`Router(false)` usage retains the upstream spell heuristic.

This is a **known unsupported feature**, not a neural spell implementation.
The real-model regression `mmaiStrictNeuralDoesNotCastScriptedHeroSpellRealBattle`
teaches an attacker hero Magic Arrow, confirms hero casting is legal, then
checks that a completed ONNX-controlled battle has accepted neural unit actions
but no scripted spell cast. Spell policy learning or an explicit non-neural
hybrid mode will be needed before full MMAI feature parity can be claimed.
