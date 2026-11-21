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
| Siege and towers | `fheroes2SiegeAttackerApproachesClosedWallUsingNativeWalk`, wide castle defender, Earthquake; `FHeroes2SiegePressureBattle` checks live/last tower, broken gate and native cover metadata. | The isolated FORT wall/gate/controller pairs above now cover this fallback. Next pair moat stopping and subsequent movement with standing/destroyed walls, then wide footprints; keep full drawbridge-opening lifecycle separate. |
| Special attacks | Native breath and friendly collateral, fatal/preemptive strikes, ranged retaliation; bounded DoubleAttack/DoubleShot/Wolf strength and unsupported-record refusal; native Berserk action/path cases. | Cross wound state, spent retaliation and wide secondary targets at a decision boundary. Require the server's executed strikes to match the valuation assumptions; retain whole-context fallback for uncertified records. |
| Magic | Current-ammunition Shield/Dispel and Clone; observed-spell Anti-Magic; duration/cancellation controls; area/farewell resistance and conservative Mirror handling. | Enumerate simultaneous-effect and expiry combinations per spell. Pair disabled/waiting/completed-turn recipients, removal that genuinely changes native damage, and preview success/failure. Partial reflection and joint stochastic outcomes remain unsupported rather than silently treated as deterministic. |

Take moat stopping and subsequent movement next, then wide cover/escape and
special-attack boundary matrices, then spell combinations individually. The
pinned source explicitly rushes a pursuit path ending in the moat, but native
VCMI BFS stops expansion there; a missing textual branch alone is not proof
of a wrong native decision. Establish a distinguishing real scenario before
changing that policy. Separately diagnose the Cure/Berserk cast window using
a live, unenchanted ally's actual turn; a free-running battle can consume the
control effect before a useful Cure opportunity.

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
