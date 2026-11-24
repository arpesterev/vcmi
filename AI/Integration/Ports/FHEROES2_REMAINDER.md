# FHeroes2 tactical verification remainder

This working catalog supplements, rather than rewrites, the historical
[accepted a53 catalog](CURRENT_REMAINDER.md). Its starting VCMI revision is
`7d3a0f71c43e88f86c4aff8bfe3304129a1e9f15`; the reference implementation is
fheroes2 `0dbfe9f386f33c2500842f4a59cd284d7f03d541`. This is a FHeroes2-only
scope, with no HoMM3 Mirth work, workflow changes or new Mac qualification.
Two corrected decisions do not establish a complete tactical port.

## Retreat and Clone corrections

Pinned `ai_battle.cpp`, `BattlePlanner::archerDecision`, ranks safe positions
by nearest-enemy distance, then inverse center distance. Both distance zero
and distance one receive `1.0`; equal keys retain the first ordered position.
`battle_cell.cpp`, `Position::operator<`, orders head and then tail indices.
The port therefore compares `max(1, centerDistance)` before its head-index
tie break. It does not import the H2 board into VCMI.

`SpeedGate/FHeroes2RetreatCenterRing.orderedSafeTieUsesNativeQueueAndServer`
uses nonnative sand, an archer at 115, enemies at 98 and 175, and speeds
6/3/3. `SafeTie` requires 76 rather than 93, with the real first activation,
queue, legal reachability and server submission. `InsufficientLead` lowers
only the archer speed to five and requires a server-accepted non-WALK action.
Before the correction, SafeTie chose and reached 93; InsufficientLead passed.

Pinned `ai_battle_spell.cpp`, `spellEffectValue`, values MIRRORIMAGE using
`target.GetStrength()` and the target's archer/flying/speed classification.
`battle_troop.h::GetShots` exposes remaining shots, so an exhausted shooter
loses the source archer premium. The port now values the pre-cast original,
only after a real preview creates a new clone associated with that original.
Native Clone creation, ammunition and spell legality are unchanged.

`OriginalAmmo/FHeroes2CloneAmmoBattle.valuesOriginalAmmunitionWithoutChangingNativeCloneStock`
pairs `PartiallySpent` and `Exhausted` under otherwise identical conditions.
Both preview Clone on the original, verify untouched original ammunition,
and execute the chosen cast through the server. The larger loaded original
wins; the exhausted original loses to the smaller loaded alternative. The
created native clone retains full ammunition in both cases. Before the
correction, only Exhausted failed. These are native VCMI decisions informed
by H2 ranking, not identical H2 spell execution or geometry.

## Siege fallback and current controller

The follow-up to the retreat/Clone candidate `e6a97b6dacc0684e3f9465b0f775dd065cb2de94`
is tested in `FHeroes2SiegeFallbackTest.cpp`, using a real FORT BattleLayout
without machines, towers or moat. Native activation is never overwritten.

- Standing Upper Wall: attacker 44, defender 46, permanent barrier 45.
  No available step decreases defender distance and no attack position is
  reachable. The existing fallback already chooses 28 toward wall 29, and
  the server accepts the WALK without damage.
- Destroyed Upper Wall: an authoritative wall-damage packet opens 29. The
  existing chooser selects WALK_AND_ATTACK through 29 and the server deals
  damage. Both wall cases passed before the new controller correction.
- Closed/open gate packets with unchanged intact gate wall: attacker 93,
  defender 97. Closed chooses 44 without damage; open selects an attack
  from 79 whose native predecessor path crosses 95 and 96. Both actions
  are accepted. This isolates gate-state consumers, not the full lifecycle
  by which a defender opens and holds the drawbridge.
- Current controller: real Hypnotize transfers a defender at 44 to the
  attacker while another defender remains at 46. Valid native turns advance
  until the controlled stack actually becomes active. Its closed-gate access
  follows the attacker, but the old fallback incorrectly tested its original
  defender side and returned SKIP. Use `controllingSide()` in that guard.
  The paired unenchanted defender still approaches its actual enemy instead
  of taking the wall-fallback move.

Pinned `analyzeBattleState` sets `_myColor` from `GetCurrentColor()` before
assigning castle attack/defense. This supports controller-based policy, not
identity between H2 and H3 siege layouts. H2 enables castle stance only while
a tower remains. VCMI has a towerless FORT with blocking walls: preserving
its existing legal wall approach is an intentional native extension. No
wall ranking, native gate rule, Hypnotize effect or other AI is changed.

The controlled-attacker case is the production red control; the already
working wall/gate cases are positive and negative branch evidence. Initial
94/96 gate-destination guesses and a single-step turn-order assumption were
fixture probes, not claimed tactical defects. The final tests follow actual
native paths and queue transitions.

## Moat entry and the following wide move

Pinned `meleeUnitOffense` enters a moat-ending path to gain movement freedom
on the following turn. VCMI stops native BFS when either occupied cell first
enters a stopping obstacle and ignores the starting footprint on the next
move. Its wall, drawbridge and two-cell rules remain authoritative; this is
not an identity between the H2 and H3 grids.

`FHeroes2MoatWide` starts a real server battle with `BattleLayout`, a built
Citadel and the native Castle moat spell. The keep is destroyed by a native
packet to isolate movement. No manual obstacle replaces the real moat.
At origin 26 against defender 46 with Upper Wall 29 destroyed, the old fallback
chose 11 solely by geometric tie order. A wide stack then stopped again at
head 29 / tail 28 instead of attacking on the next turn. Entry 28 / tail 27
allows the following attack from 30 / tail 29. A single-cell stack instead
attacks from 29. Entering the moat costs the native 70 HP; the server accepts
both turns and applies real attack damage.

The bounded correction applies when the current graph supplies no attack
route and the attacker is not already in the moat.
Compare legal moat entries using a second native BFS from each complete
footprint, including the vacated original footprint. Require a native attack
landing within the following movement range; entering another stopping tile
cannot be treated as an attack. Only the first legal WALK is submitted.
Future actions, damage and movement range are recalculated on the real turn.
Routes needing further moat stops retain the old fallback.

The farther defender at 63 already used entry 28 and remains unchanged.
Intact Upper Wall blocks the passage, including a wide tail in wall 29.
Closed-gate cases retain entry 44; an authoritative opened-gate packet permits
single-cell attack from 79 or wide attack from 96 / tail 95 without a moat
trigger. These are gate-state consumers, not a complete drawbridge-opening
lifecycle. All cases assert actual server movement and occupancy.

The standalone suite is exercised by focused and broad local regression.
It is outside the unchanged exact CI/reviewed filters and is not qualified
by the historical A53 evidence. The paired live-keep cases use the same breach with native tower activation enabled. Native
`isValidTarget` already excludes turrets from the movement-target census; no
separate tower-target fix is needed. Removing the keep must include its shooter
ID in the CatapultAttack packet, not only change the wall state. Multiple
movable targets are covered by the bounded extension below.

## Cure with a live Berserk window

The historical single-friendly-stack native fixture did not establish a
hero-magic opportunity while Berserk was live. Pinned `planUnitTurn` bypasses
spell selection for the compelled stack; the port retains that rule. A later
Cure after native expiry can heal damage without proving control removal.
The Mac failure on `7d3a0f7` alone therefore does not establish a platform bug.

`fheroes2CuresNativeBerserkAndCompletesBattle` now uses two friendly stacks.
A real enemy hero casts Berserk and its active unit defends. The actual next
unit is an unenchanted ally, while the full-health recipient remains under
Berserk. Check cast legality and a real hypothetical preview before requesting
the AI action; require server-accepted Cure, exact mana expenditure, removal
of the native effect, unchanged recipient health and an accepted unit action.
A zero-mana control rejects spell selection. `FHeroes2CureWindow` separately
checks the compelled-active branch and preserves the original free-running
setup as diagnostic evidence, counting Cure while control is live rather than
mistaking later healing for control removal.

Pinned `spellDispelValue` values removal of negative friendly effects. VCMI
owns actual Berserk duration, Cure mechanics and mana cost. No spell-policy or
native effect changes are part of this fixture correction. The existing exact
native identity remains in the Linux/Mac gate; the two extra diagnostic cases
run in focused and full local regression outside that unchanged selection.
Mac execution of the corrected fixture remains separate acceptance work.

## Reviewable work queue

Existing scenario names below identify bounded coverage, not exhaustive
parity. Each next slice needs a pinned-source rule, an executable negative
control and a real accepted action or state packet.

| Area | Existing bounded evidence | Next discriminating check |
| --- | --- | --- |
| Cover and retreat | Native cover family, blocked-route escape, vacated-footprint and stopping-obstacle checks; wide-tail escape; `FHeroes2WideCautious` covers obstacle passage and unsupported-threat fallback; center/ring pair above. | Pair equal-ranked cover positions and wide retreat orientations with real turn order; independently verify both cells and inability to escape. Keep flying-enemy and insufficient-speed vetoes. |
| Siege and towers | `fheroes2SiegeAttackerApproachesClosedWallUsingNativeWalk`, wide castle defender, Earthquake; `FHeroes2SiegePressureBattle` checks live/last tower, broken gate and native cover metadata. | The isolated FORT wall/gate/controller pairs above now cover this fallback. The native moat/next-turn/footprint pairs cover single and multiple targets. Live/destroyed keep pairs also cover this route; next distinguish the full drawbridge-opening lifecycle and routes needing more than one additional stop. |
| Special attacks | Native breath and friendly collateral, fatal/preemptive strikes, ranged retaliation; bounded DoubleAttack/DoubleShot/Wolf strength and unsupported-record refusal; native Berserk action/path cases. | Cross wound state, spent retaliation and wide secondary targets at a decision boundary. Require the server's executed strikes to match the valuation assumptions; retain whole-context fallback for uncertified records. |
| Magic | Current-ammunition Shield/Dispel and Clone; observed-spell Anti-Magic; duration/cancellation controls; area/farewell resistance and conservative Mirror handling. | Enumerate simultaneous-effect and expiry combinations per spell. Pair disabled/waiting/completed-turn recipients, removal that genuinely changes native damage, and preview success/failure. Partial reflection and joint stochastic outcomes remain unsupported rather than silently treated as deterministic. |

The next confirmed magic boundary is direct negative magic against partial
native `MAGIC_MIRROR`, combined with resistance. `FHeroes2SpellPolicy::choose`
explicitly rejects every positive reflection value because `castEval` does not
execute the native before-cast/reflection path. This is an unsupported joint
outcome distribution, not evidence of an incorrect deterministic preview or
an absent H2 Magic Mirror spell. The controlled zero/partial/full reflection and partial-resistance cases
below establish the refusal boundary, real mana expenditure and reflected
recipient effects without evaluating the joint distribution. Native H3 reflection must not be confused with the pinned
H2 Mirror Image / VCMI Clone mapping.

Longer-horizon moat pursuit, wide cover/escape and wounded or
retaliation-spent recipients of special attacks remain separate work.
The Cure/Berserk window above is bounded cancellation evidence, not a
certificate for all simultaneous-effect choices.

## Intentional native boundaries and acceptance

VCMI owns board dimensions, obstacles, both cells of a wide stack, wall/gate
state, tower execution, activation, retaliation, ammunition, native effects
and cast legality. Source-inspired ranking is applied only within supported
facts. Existing unsupported-case fallbacks and conservative refusal remain
part of the contract. Native estimates are not certified H2 arithmetic.
Enemy spell intelligence uses public observations, not hidden spellbooks.

The retreat, Clone and new siege suites run explicitly in the
focused and full local regressions. They are outside the unchanged exact
Linux native/macOS reviewed selection. This change neither expands those
CI gates nor qualifies these decisions under the historical A53 manifest.
Native Apple Silicon execution of the new revision, per-decision latency,
GUI/save-load and campaign play remain separate acceptance work. A Linux
Clang/libc++ syntax check is not an Apple Clang build or Mac native XML.

## Multiple-target moat entry

The extension from `3ab09bc6b7fafcd14ba8130cb1609a4033a62e40` retains the
same bounded second-turn native search, but collects the shortest legal entry
for each target. It then uses the existing distant-target comparator: targets
that cannot evade first, then threat divided by path distance, with the existing
stable visitation tie behavior. This follows pinned `meleeUnitOffense`; the
native path and ordinary/unsupported threat boundary remain VCMI contracts.
No future attack is cached or submitted speculatively.

`FHeroes2MoatMultiTarget` places 100 Cavaliers at 94, 300 Pikemen at 46 and
a second Pikeman stack at 184 in a native Citadel battle. With both wall
breaches open and 3000 lower defenders, the accepted entry is head 181 / tail
180, followed by an accepted attack from 167 / 166. Reducing only the lower
stack to ten selects entry 28 / 27 and attacks the upper target from 30 / 29.
Keeping 3000 lower defenders but leaving Bottom Wall intact also selects the
upper route. Both candidate moat entries are reachable; the following path
must cross the selected destroyed wall and the server must damage that target.
The shared-upper-breach pair separately covers single and wide attackers.
The Cavalier charge context keeps the existing native threat fallback; these
cases certify target/route selection, not identical H2 damage arithmetic.
All five cases fail against the previous production header, which chooses 11.
The other twelve prior moat cases remain positive/negative regression controls.

The first oversized-defender probe used only ten Cavaliers and ended the battle
on retaliation, invalidating its fixture pointers. The final two-breach fixture
keeps the battle alive and verifies actual post-command head/tail positions;
that preliminary runner failure is not a tactical result.

## Reflection and resistance boundary

Pinned `spellDamageValue` scales damage by `(100 - GetMagicResist()) / 100`.
Its Mirror Image is a copy spell, not native H3 Magic Mirror. Importing that
scalar as the joint reflection/resistance distribution would be incorrect.
Native `BattleSpellMechanics::beforeCast` rolls resistance over living units,
then tests reflection. Reflection selects an opposite-side recipient and calls
`beforeCast` again, including new resistance rolls and possible further
reflection. `castEval` bypasses that entire path. It therefore cannot certify
expected joint outcomes, even when a deterministic preview succeeds.

The conservative direct-negative-spell refusal for every positive Mirror
value remains unchanged. The new reflection suite distinguishes the public
AIFactory action from forced, legal diagnostic casts: only the latter exercise
reflected outcomes. This is a documented unsupported valuation group, not a
claim that reflection is absent from the engine or that H2 supplies its rules.

`Mirror/FHeroes2ReflectionResistance` invokes `AIFactory::activeStack` on the
real first unenchanted allied activation with only Arrow available. With 50%
resistance on each stack, no Mirror selects Arrow; 50% and 100% Mirror select
an accepted ordinary WALK with unchanged mana and no spell packet. Each choice
is submitted to the server; selection itself cannot emit server casts or spend mana.
`Seed/FHeroes2ReflectionOutcomes` forces a separate legal Arrow for diagnosis.
The same preview always removes 310 enemy HP and no allied HP. Native seeds
0, 2 and 4 respectively produce target resistance, reflection plus allied
resistance, and reflection causing 310 allied HP damage. All consume exactly
the native Arrow mana cost once. These three controlled outcomes are not a
statistical probability estimate or an exhaustive reflection-chain matrix.
The production Mirror guard is unchanged.

## Completion criteria for this port

A group is closed only when its supported contexts have a pinned rule, a public
AI decision in a real native battle, accepted server effects, and discriminating
negative controls. Pure helper arithmetic and successful preview alone are
insufficient. Record intentional native differences explicitly, and preserve
unsupported-context refusal until its replacement has the same evidence.

Remaining groups are cover/retreat orientations and occupancy, complete gate
lifecycle and longer siege routes, wounded/retaliation-spent special attacks,
and interacting magic duration/cancellation/stochastic outcomes. Each needs
its own finite scenario matrix with boundaries and reproducible XML identities.
The joint reflection distribution remains open; bounded moat entry does not
close all siege tactics. Completion also requires review of every open catalog
entry, explicit acceptance or supported implementation of each native boundary,
exact CI inventory admission of new scenarios, native Mac execution on the same
candidate SHA, and recorded performance and gameplay acceptance. None of these
broader criteria is satisfied merely by passing the local suites in this slice.

## Friendly secondary melee priority

Pinned `battle_troop.cpp::evaluateThreatForUnit` applies a `-2` multiplier to
friendly victims. `ai_battle.cpp::doubleCellAttackValue` feeds the secondary
victim into that function, and `optimalAttackValue` includes it in the melee
comparison. The existing port already collects native breath/sweep victims,
handles native first strike, and evaluates multiple legal aims. Its remaining
error in this slice was charging friendly secondary loss only once.

The correction applies the source multiplier to the existing native loss
estimate. It does not replace VCMI damage, attack geometry, retaliation or
ownership with H2 rules, and does not change the separate blocked-archer raw-HP
policy. Basic breath and Hydra collection were already implemented; an audit
of wide hypothetical footprints did not establish an additional bug here.

`Secondary/FHeroes2BreathPriority.secondaryOwnershipChangesActualPrimaryChoice`
uses a real server battle and its actual first activation. The actor at 90
receives native breath and zero movement range to isolate two legal adjacent
primary choices: archers at 91 and pikemen at 73. The secondary pikemen at 92
are hostile or friendly in the paired cases. The chooser is invoked through
AIFactory, and its selected command is submitted to the server. Before this
correction, Friendly chooses 91 and kills all 500 allied HP at 92. Hostile is
an unchanged positive control: the same secondary damage is desirable.
After correction, Friendly selects 73, deals 901 native HP to that enemy and
spares all 500 allied HP. Hostile still selects 91 and deals 973 primary plus
500 secondary HP. Identical test bytes give one failing case before and two
passing cases after; the server accepts every selected command.
These two cases close this ranking-weight boundary, not the full special-attack
matrix. Native estimates remain an intentional approximation to H2 threat.

## Remaining groups: implementation, native rules, verification

| Group | Missing implementation or bounded fallback | Still unverified | Intentional native boundary |
| --- | --- | --- | --- |
| Cover and retreat | No new algorithmic defect established by this slice; unsupported threat contexts retain native fallback. | Equal-ranked cover placements, wide retreat orientations, both occupied cells and inability to escape across the full context matrix. | VCMI board, occupancy, activation and speed vetoes. |
| Siege and towers | Moat pursuit looks ahead one new stopping turn, including when already standing in the moat; general longer chains still retain the old fallback. | Other gate blockers/corpses and town-specific bridge variants, routes beyond the verified two-stop corridor, and broader combinations of tower/wall state and target priority. | H3 walls, drawbridge, two-cell stopping and tower execution; towerless FORT approach is a native extension. |
| Special attacks | Uncertified ability combinations retain native estimates rather than certified H2 threat arithmetic. | Wounded and retaliation-spent exchanges, wide secondary targets, combined extra strikes and native reaction effects; the paired friendly-breath weight is now covered. | Native damage and recipient geometry, FIRST_STRIKE/ranged counters and modded abilities do not imply H2 mechanic identity. |
| Magic | Exact joint reflection/resistance valuation is absent and direct-negative Mirror refusal remains conservative. Other spell contexts have bounded supported subsets. | Simultaneous effects, expiry/cancellation, disabled/waiting/completed recipients, preview failure and all relevant spell combinations; multi-recipient reflection chains remain open. | Native H3 duration, immunity/resistance rolls, Clone ammunition, mana and cast legality; Mirror reflection has no H2 Mirror Image equivalence. |
| Acceptance | New standalone cases have not been admitted to the exact CI/reviewed inventories; this patch does not alter those contracts. | Same-SHA native Apple Silicon XML, per-decision latency, GUI/save-load and campaign acceptance. | Linux compiler/syntax and local server tests are not Mac execution or historical A53 qualification. |

An unverified group above is not automatically a proven missing algorithm.
Completion requires resolving every bounded fallback or explicitly accepting
its supported-domain boundary, plus the scenario and platform criteria above.

## Possible reflection evaluator interface (design only)

A future read-only native evaluator would accept an immutable battle snapshot,
public caster/target facts and a legal spell action. Its result must distinguish
`Unsupported(reason)` from certified outcomes with probability mass, recipient
order and state deltas (health/count/effects/owner), plus mana and action cost.
It must preserve native reflection recipient selection, fresh resistance rolls,
chain reflection and immunity semantics. Exact enumeration or certified bounds
must account for all probability mass; sampled RNG traces cannot silently stand
in for expected utility. The AI would apply its policy to those outcomes and
retain refusal when the result is unsupported. No such API, server mechanic or
reflection-policy change is implemented by this patch.

## Continuing from a native moat stop

Pinned `battle_pathfinding.cpp` (commit `0dbfe9f386f33c2500842f4a59cd284d7f03d541`)
tracks the starting moat cell separately and charges a new moat penalty later
in the path. The native adapter previously rejected every already-in-moat
position before evaluating another entry. It now considers entries whose
footprint contains a moat cell outside the current footprint. Native movement,
wall accessibility and server damage remain authoritative.

`FHeroes2MoatContinuation.twoStopsReachTheBreach` uses a real Castle CITADEL,
a destroyed Upper Wall and a server-installed permanent blocking obstacle.
The obstacle isolates a corridor, rather than claiming that an unobstructed
standard castle requires this route. A wide attacker starts at head 43/tail 42;
the defender is at 46. The server accepts 43 to 44/43, then 28/27 on the next
actual activation, then an attack from 30/29. Each entry inflicts native moat
damage. Before the fix, the same scenario reaches 44/43 and then skips instead
of choosing the legal next entry at 28/27. The paired shorter passage leaves
26 open and goes directly to 28/27; the intact-wall control cannot attack
through 29. Neither control requires a production change.

This is receding one-stop lookahead, not a general multi-turn route planner.
The first step in this corridor is supplied by the existing geometric fallback;
the fix permits continuation from that stop. The helper evaluates at most one
native BFS per available new-moat head (bounded by the fixed battlefield size),
with unchanged target/adjacent-goal ranking and no recursion or persistent
cache. Starting outside the moat retains the same candidate entries. Other
long chains, route costs across H2/H3 geometry, and future blocker changes are
not certified. The four standalone cases belong to the broad local regression;
the exact current CI and reviewed Mac manifests are unchanged.

`FHeroes2MoatContinuation.defenderHoldsAndReleasesGate` separately exercises
server-driven gate state without injected gate-state packets. The attacker at
93 defends; the real defender walks from 97 to 95 and holds the gate open. The
AI attacks from 94/93 and the server deals damage. After the defender returns
to 97, the gate closes in `BLOCKED` state because the attacker occupies bridge
94. Its next AI move vacates the bridge, returning the gate to `CLOSED`; the
intact gate excludes attack and access through 95/96 while closed/blocked.
This is existing VCMI behavior, verified without a gate-policy change. The
initial expectation of immediate `CLOSED` was corrected to the native contract;
that fixture correction is not a red/green AI regression. Corpses, Fortress's
long bridge, force fields on the bridge and other tower states remain open.

## Native projection boundary checked on 2026-11-24

The pinned H2 pathfinder exempts its starting moat cell; it does not forbid
all planning from an existing moat stop. The VCMI adapter keeps native
per-cell stopping and orientation, rather than importing H2 path costs or
zero-cost reversals. Its virtual BFS clears the old footprint for occupancy,
but that also exempts those cells from native stopper checks. The adapter
therefore inspects the chosen predecessor chain and refuses any further moat
cell outside the projected entry footprint, including a vacated original
moat cell. This guard is bounded by the battlefield size and is conservative;
no separate native red case is claimed for the guard itself.

The baseline Apple Silicon run 45047489791 at `30375e683411c20fb053e614fbe1323074a2047b`
completed packaging, mounted-image verification and both existing native
partitions. This is a usable baseline, not qualification of the new continuation
cases or full tactical parity. The current continuation candidate still needs
explicit inventory admission and same-SHA native Mac execution before its new
scenarios can be called Mac-tested.

## Current Mac inventory admission

The four `FHeroes2MoatContinuation` cases are now explicitly listed with the
six HoMM3 moat cases in `CI/macos_ai_a102f718_inventory.json` and run in the
mandatory core `moat-route-regressions` suite. The new reviewed total is 909:
764 core and the unchanged 145 slow cases, with 20 separate model exclusions.
Earlier statements about these four cases being outside the gate describe the
preceding implementation stage; other standalone suites are not silently
admitted by this change. A53 and the previous 899-case list remain unchanged.
Local Linux selection/XML validation does not qualify this revision on ARM64;
a same-SHA Mac run and mounted DMG checks remain required.
