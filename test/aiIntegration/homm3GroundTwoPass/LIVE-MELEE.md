# Live ordinary melee leaf

`chooseOrdinaryLiveMelee(OrdinaryLiveMeleeInput)` chooses for the explicitly
provided `activeId`, not the first action in a forecast. It never applies
expected damage or advances an ownership prefix. It returns a source decision
plus the count of source random draws consumed; it is not a native BattleAction.

This uses non-simulated `chooseMeleeAction` semantics in a closed initial domain:
all of the ordinary source model contract plus all roster units alive, zero
retained forecast damage, an active ordinary melee unit on `currentSide` which
can act and is not berserk, and **no friendly units currently able to shoot**.
The latter proves `attemptShooterDefense`'s client scan has no eligible client.
Opponent shooters remain supported, including source blocking valuation.

`noCreatureSpellKnown` must certify source `m_hasSpell == 0` or source
`canCastSpells(side,0) == false`. Missing knowledge is rejected, not treated as
false. This is the early-return guard in source `chooseSpellAction`; it does not
certify the complete hero spell pipeline, which is outside this leaf.

Live-specific branches include `moveToward`'s considerWaiting policy, difficulty
and second-phase gates, path danger comparisons and terminal WAIT decisions,
and exact ordinary `chooseToRun` scanning when no attack target is available.
The oracle retains the original non-simulated chooser and chooseToRun bodies.
Its only excluded-branch shims are the source m_hasSpell-zero early return and
no-shooter-client early return. Both throw outside those explicit domains.

The source differential suite has 90603 checks, including a directed no-target
run-away escape. 178 decisions differ from the same source simulated chooser.

The model is unavailable for unknown domain, missing active ID, mismatched
current side, active shooter/disabled/berserk unit, friendly shooter clients,
retained simulated damage, missing draw stream/pair facts, and the existing
source arithmetic/map/roster exclusions. It does not route those cases to an
invented heuristic. The caller must retain an explicit native fallback.

## Native bridge still required

Bind activeId to the actual callback active stack, capture only current public
facts under an uninterrupted battle lifecycle, and reject stale snapshots.
Supply exact source ordinary values and closed-domain certificates. Validate
source-selected move destination or melee attack hex/target against native
reachability and current legal attack geometry before constructing a native
command. WAIT/DEFEND must likewise be legal for that activation. Do not substitute
a forecast action, arbitrary reachable tile, hidden enemy book, or native value
estimate for an unavailable source fact. Native execution is not enabled here.

`CI/homm3_live_melee_test.cpp` compares the source non-simulated chooser for an
explicit actual active across 4901 deterministic scenarios. 3827 choose an active
different from the first source move-order entry. MOVE/DEFEND/MELEE/WAIT are each
exercised, source draw order and full danger map/value/target/hex are compared,
and every returned ledger remains zero. Negative tests cover the live guards.
