# Forced battle reference (opt-in)

This is an engine outcome reference, not a completed calibration claim. Run the
registered `ForcedBattleReference.*` gtests from the configured `bin` directory
with the public upstream test resources and the normal initialized game library.
Build the explicit `vcmi_forced_battle_reference` target (available with static
Nullkiller2 tests), then run `bin/vcmi_forced_battle_reference
--gtest_filter=ForcedBattleReference.*`. It is EXCLUDE_FROM_ALL; the default
`vcmitest` target and default build do not compile it. No cached option switch
is necessary. Reuse the validated build/cache; do not make
a separate cold client build to execute this fixture.

## Independent ground truth

The public `MiniTest` JSON map is loaded via existing `MapServiceMock`. Two fresh
ordinary `CArmedInstance` objects are registered in that disposable game state:
RED attacker and neutral defender. The fixture does not use either map hero,
private saves, campaigns, adventure planning, an attack-approval policy, or a
handwritten damage formula. Both armies have one stack. Default formation is
used; the explicit layout disables obstacles and tactics. There are no heroes,
spells, siege, equipment, artificial stat boosts or removed ranged penalties.
Normal army/creature morale and luck remain intact.

`BattleProcessor::startBattle` starts the fight unconditionally. The small
`IGameServer` adapter applies every engine pack to `CGameState` and records the
real `BattleResult`. `makePlayerBattleAction` validates and executes every unit
action; engine code owns initiative, morale turns, movement, ammunition, damage,
retaliation, deaths, and completion. The human attacker setting only keeps the
terminal result dialog pending so unrelated adventure aftermath is not run.
No UI, socket, or actual human is required.

## Fixed action policy

For either side, order living enemy stacks by unit ID. Shoot the first legal
target. Otherwise attack the first enemy from the first engine-reported attack
hex in legal movement range. Otherwise move to the legal hex minimizing hex
distance to a living enemy, breaking ties by hex index; defend if unchanged.
There are no hero casts, waits, flee/surrender or prediction-dependent choices.
The policy is deliberately simple, not a claim of optimal tactical play.

Only a real terminal `BattleResult` counts as an observation. Invalid actions,
missing active stack/battle, or reaching 2,000 actions without a result fail the
test; a cap is never converted into a victory or loss. Casualties come directly
from `BattleResult`, excluding temporary summoned stacks by engine convention;
AI-value loss is casualties multiplied by the loaded creature's native AI value.
These are battle-result permanent losses, not post-battle resurrection or
necromancy/hero-level-up effects.

## Initial matrix

| Case | Attacker | Neutral defender | Seed |
|---|---|---|---|
| ordinary_melee | 40 pikemen (0) | 10 pikemen (0) | 17041 |
| ranged_slow_melee | 20 archers (2) | 30 walking dead (58) | 17047 |
| slow_melee_ranged | 30 walking dead (58) | 20 archers (2) | 17053 |
| undead_melee | 40 skeletons (56) | 20 walking dead (58) | 17041 |
| flying_ranged | 30 stone gargoyles (30) | 20 archers (2) | 17053 |
| ranged_mirror | 20 archers (2) | 20 archers (2) | 17047 |

`FORCED_BATTLE_INITIAL` records initial unit identities/positions/health/morale/
luck, `FORCED_BATTLE_ACTION` the exact submitted action sequence, and
`FORCED_BATTLE_REFERENCE` the real winner, round/action/shot/melee/retaliation
counts and per-creature losses. Preserve logs with source revision, fixture
SHA-256, executable SHA-256 and upstream resource revision/hashes. A seed is
reproducible only for the same engine/configuration/resources.

## Experimental comparison and limits

Before any submitted action, the detached experimental evaluator consumes the
same initialized snapshot twice (unfavourable/favourable damage endpoints).
Its status, winner, survivors and winner agreement with the observed battle are
logged after the real result. It cannot stop or change the forced fight. The
first forecast is unfavourable, the second favourable to the attacker.
UNSUPPORTED is 0, BUDGET_EXHAUSTED is 1, FINISHED is 2; winner attacker is 0,
defender is 1, no winner is -1. Unsupported/budget results are NOT safe fights.
These endpoints are not proven bounds or calibrated probabilities.

Ordinary living one-faction armies have normal positive morale and may be
correctly rejected by the evaluator's initial zero-morale subset. The undead
case is included to exercise that narrower region without hidden bonuses.
No evaluator gate is loosened merely to make reference cases pass. Mixed
armies, heroes/mana/spells, obstacles, sieges, double-wide units, special attacks,
resurrection, summons and post-battle adventure application are not covered by
this initial fixture. In particular these tests alone do not validate general
adventure risk estimates or make a skipped adventure attack a forced-battle loss.

Compilation/execution must be reported separately from source implementation;
do not describe cases as passed or publish outcomes before actual logs exist.
