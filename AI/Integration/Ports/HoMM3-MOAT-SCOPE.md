# HoMM3 bounded native moat approach

Reference: `sushi-shi/homm3-decomp` at
`e12906accf96c2662b1abadbc49ddc111991bf21` (CC0-1.0).
The upstream `src/ai.cpp::combatManager::moveToward` (686-779) walks an
already reconstructed route and stops movement when either occupied cell is
in a moat (755-759). `src/ai_tactical.cpp::getAttackTime` (417-427) adds a
turn for a moat destination. These are not rules that an unreachable native
attack cell means a permanently unreachable opponent.

The source also contains a siege danger field and a separate no-target wall
approach in `chooseMeleeAction`. This change does not transplant those scores,
source path publication, waiting decisions, or castle-row geometry.

VCMI native reachability stops expanding a newly entered stopping obstacle.
The previous fallback requires a finite route to an attack hex and otherwise
defends. Consequently a breached castle can have a legal useful moat entry
but no published attack route on the current turn.

The additional HoMM3-only fallback runs only after the usual full-route
search fails, for a ground attacker outside a visible native moat in a siege.
For each reachable moat entry, one second native reachability search checks
whether a legal attack landing is available on the following turn. Both
occupied cells, walls, gate state and stopping tiles remain native. The
original footprint is vacated for that hypothetical search. The shortest
combined route wins, then the smaller entry index, matching the existing
fallback's distance preference rather than claiming the source target score.
Only the first WALK is submitted. The next actual activation recalculates
movement, targets and damage from live state.

The board bounds this to at most 187 candidate entry searches, each using
native fixed-size reachability; only reachable moat-touching entries actually
invoke a search. It is not an unbounded route search. Routes needing another
stop, ordinary non-siege failures, flyers, units already in a moat and
unsupported no-route cases retain the previous result. A normal full route,
including an open gate, takes precedence. The TUM composed baseline remains
unchanged.

`Footprint/HoMM3MoatApproach.*` supplies six native scenarios: single/wide
breached-wall entry followed by attack, intact-wall refusal, and opened-gate
ordinary attack. The first pair must use the real queue, AIFactory controller,
server-accepted WALK, native moat damage, next activation and accepted attack.
The wide case checks both cells through the broken wall. No manual obstacle or
manual activation substitutes for native BattleLayout and Castle moat rules.

These standalone identities are outside the unchanged exact CI/reviewed
selection. Record their actual before/after XML separately. No historical A53
qualification or native Apple Silicon result follows from local Linux tests.
The source siege danger map, moat-damage economics, general multi-stop pursuit,
and complete siege parity remain open.
