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

## Reviewable work queue

Existing scenario names below identify bounded coverage, not exhaustive
parity. Each next slice needs a pinned-source rule, an executable negative
control and a real accepted action or state packet.

| Area | Existing bounded evidence | Next discriminating check |
| --- | --- | --- |
| Cover and retreat | Native cover family, blocked-route escape, vacated-footprint and stopping-obstacle checks; wide-tail escape; `FHeroes2WideCautious` covers obstacle passage and unsupported-threat fallback; center/ring pair above. | Pair equal-ranked cover positions and wide retreat orientations with real turn order; independently verify both cells and inability to escape. Keep flying-enemy and insufficient-speed vetoes. |
| Siege and towers | `fheroes2SiegeAttackerApproachesClosedWallUsingNativeWalk`, wide castle defender, Earthquake; `FHeroes2SiegePressureBattle` checks live/last tower, broken gate and native cover metadata. | Isolate `approachSiegeWall` fallback where no legal step approaches a living defender. Pair standing versus destroyed blocking wall and sealed versus open gate; assert wall state, legal head/tail path, actual activation and accepted action. Existing closed-wall advance alone does not prove this fallback. |
| Special attacks | Native breath and friendly collateral, fatal/preemptive strikes, ranged retaliation; bounded DoubleAttack/DoubleShot/Wolf strength and unsupported-record refusal; native Berserk action/path cases. | Cross wound state, spent retaliation and wide secondary targets at a decision boundary. Require the server's executed strikes to match the valuation assumptions; retain whole-context fallback for uncertified records. |
| Magic | Current-ammunition Shield/Dispel and Clone; observed-spell Anti-Magic; duration/cancellation controls; area/farewell resistance and conservative Mirror handling. | Enumerate simultaneous-effect and expiry combinations per spell. Pair disabled/waiting/completed-turn recipients, removal that genuinely changes native damage, and preview success/failure. Partial reflection and joint stochastic outcomes remain unsupported rather than silently treated as deterministic. |

Take the isolated siege-wall fallback pair next. First demonstrate which
production branch is reached: nearby living defenders can satisfy the
ordinary approach before wall fallback is considered. A wall-state assertion
alone cannot distinguish the two branches. Do not manufacture activation
while a catapult owns the turn. After this pair, extend wide cover/escape
and special-attack boundary matrices, then spell combinations individually.

## Intentional native boundaries and acceptance

VCMI owns board dimensions, obstacles, both cells of a wide stack, wall/gate
state, tower execution, activation, retaliation, ammunition, native effects
and cast legality. Source-inspired ranking is applied only within supported
facts. Existing unsupported-case fallbacks and conservative refusal remain
part of the contract. Native estimates are not certified H2 arithmetic.
Enemy spell intelligence uses public observations, not hidden spellbooks.

The new retreat suite and the existing Clone suite run explicitly in the
focused and full local regressions. They are outside the unchanged exact
Linux native/macOS reviewed selection. This change neither expands those
CI gates nor qualifies these decisions under the historical A53 manifest.
Native Apple Silicon execution of the new revision, per-decision latency,
GUI/save-load and campaign play remain separate acceptance work. A Linux
Clang/libc++ syntax check is not an Apple Clang build or Mac native XML.
