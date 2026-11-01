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

Not implemented: complete upstream buff/debuff and creature-spell selection,
retreat/surrender, archer kiting, exact siege positioning, upstream battle
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
two independently implemented overrides derived from the documentation:

- **Ballista:** excludes targets affected by Blind, Paralyze or Stone Gaze;
  with no remaining legal target, it defends instead of waking a disabled
  stack. We check actual VCMI spell-effect bonuses, not `isFrozen()` alone,
  because the latter only detects Stone Gaze.
- **Shooter protection:** when facing neutral melee-only stacks, allied
  ranged troops can shoot, and no neutral stack can reach the friendly army
  in its next movement, the active friendly melee stack defends instead of
  rushing forward. Movement reach is evaluated with VCMI pathfinding. The
  documented exception for neutral-controlled armies outside siege applies.

All remaining attacks, movement and siege-machine behavior explicitly uses
the partial HoMM3 tactical policy, not an unannounced BattleAI/EmptyAI
fallback. The policy never changes VCMI mechanics. The separate launcher
entry is `TUMEnchantedTactics`, **not** the reserved full-port ID
`TUMEnchanted`.

Still unimplemented or not upstream-equivalent: the full TUM defensive spell
aggregation rules, improved tower targeting, clone/antimagic/cure selection,
distraction avoidance, siege valuation, creature ability choices, buff/debuff
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
