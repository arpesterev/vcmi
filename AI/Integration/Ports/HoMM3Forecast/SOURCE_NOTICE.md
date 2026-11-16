# Bounded native ownership-prefix projection

Source semantics: sushi-shi/homm3-decomp e12906accf96c2662b1abadbc49ddc111991bf21, CC0-1.0. Native target: arpesterev/vcmi aafa3de7cd5d93dbec49dda1702ebc5c699e0827.

MoveOrder, OwnershipPrefix, Lifecycle, NativeSnapshot and NativeLifecycle were promoted from existing test support into shared header-only production definitions. Test paths are include-only compatibility wrappers. The source comparator and skip-before-ownership-boundary behavior are preserved. Comparator ambiguity is unavailable, not resolved as winner certainty.

ShooterPrefix implements the bounded incapacitation-priority branch of ai.cpp chooseShooterTarget (402–460), chooseShooterAction (526–547), simulateActions/simulateCombat (2196–2259), then ai_tactical.cpp checkSimulation/spellsNotRequired (2860–2903). There is no combat-to-completion or strength-ratio winner inference. Zero remaining enemy targets means no action. The chooser always prefers non-incapacitated enemies over incapacitated enemies, independently of attack value and difficulty/human gates. A sole target in that preferred class is supported even with alternatives in the other class. Multiple active peers still require a source target-time graph; bounded incapacitated-peer ranking is described below. The preferred class is recomputed after every projected kill. Source spell slots 62/70/74 define incapacitation; damage projection does not clear these status slots. Equal-valued peers use the later source army in the original chooser; the bounded peer extension uses that original roster order explicitly. An encountered melee or blocked shooter is unavailable except for the bounded melee extension below.

NativeShooterPrefix supplies actual native eligibility and native min/max midpoint damage using detached Unit states with projected remaining HP. This is an intentional VCMI projection, NOT exact retail adjustDamage or rounding parity. Source computes floor(troops * average) before adjustment; midpoint of adjusted extrema need not match. Expected damage and live state remain separate. No movement, shots, or authoritative battle state is mutated.

The native bonus allowlist excludes area/splash attacks, ranged retaliation, additional attacks, attack-triggered spells, drains, hypnosis, and unknown mechanics. Machines, clones, summons, altered roster, unknown phase and unsupported status lifetime are unavailable. Unsupported states preserve existing spell selection.

Only available native projection plus spellsNotRequired enables the source healing-only branch (RESURRECTION / ANIMATE_DEAD). This is a bounded native policy, not native-qualified original AI. Controlled-double and source-body tests cannot replace a native engine/data run.

Tenth increment: multiple incapacitated non-shooter peers can be ranked using loss combat value in an unmodified-stat, non-siege native projection. All ordinary/kills-only and easy/other difficulty score modes must select the same target, including /10 truncation ties resolved by original sourceIndex. Multiple active peers, damage-reduction modifiers (including full Stone Gaze), shooting victims, and mode-dependent choices remain unavailable. Blind/Paralyze retaliation-only bundles are narrowly accepted. Native tests remain UNRUN. See the tenth-increment README and ranking-check source-chain oracle.

Twelfth increment: an explicitly non-EASY public match difficulty enables a narrow melee ownership prefix. The sole living enemy must be ordinary, adjacent, positive-speed and have no hero; the actor must also have positive movement speed. Every live unit must be an unmodified-attack/defense, single-hex, single-attack non-shooter without unsupported bonuses or status effects. Sieges, obstacles and tactics are excluded. Native attack legality is checked. Source markEnemyAttacks marks the adjacent enemy dangerous without adding threat; with no enemy area damage, markFriendlyArmies adds none. Ordinary attack-hex ties choose the zero-cost current hex. Nonnegative simple exchange value in both kills-only modes avoids the ranged-advantage branch; getAttackChange cannot decrease it. Conservative score and HP bounds keep source arithmetic in signed32 range.

The new branch follows ai.cpp simulateSimpleAttack/simulateMeleeAttack (2101–2192), not the speed-exchange heuristic: detached native midpoint first hit, survivor retaliation recomputed from remaining health, and no simulated consumption of retaliation availability. Own expected losses reach the existing casualty scan independently of the enemy survivor scan. A supported win can now leave spellsNotRequired false, retaining non-healing spells and the v11 Resurrection exception. No new winner consumer is introduced. Native midpoint estimates are still not source damage or retail parity. Native engine/battle qualification remains UNRUN.

Post-r2 moving ordinary melee: the adjacent restriction is relaxed only for exactly two units on one clear battlefield row. The nearest adjacent cell must have native distance equal to row separation minus one and be reachable this turn. Row separation must also fit the enemy's movement range, preserving the source dangerous-enemy/zero-threat branch. The source ordinary attack-hex tie-break therefore has a unique cheapest cell. Existing bonus, difficulty, ownership, score, hero, obstacle, siege, width and exchange-value exclusions remain intact. The detached native attacker is placed at that cell before the same midpoint exchange; live unit positions and health are unchanged. This does not admit general movement, extra-stack obstruction, off-row path choices or charge mechanics. Extracted-original chooser/exchange control and real native CStack/BattleProcessor diagnostic evidence are supplied separately; final sealed full-build qualification is required for the assembled candidate.

## Bounded constant melee defense extension

`NativeShooterModel::meleeExchange` admits one ordinary all-damage non-spell reduction (ADDITIVE_VALUE or
BASE_NUMBER) and one additive melee Shield spell reduction per unit.
Each value must be 0..100. Only permanent, one-battle, or positive remaining
N_TURNS durations are admitted. Conditional effect ranges, limiters, propagators,
updaters, parameters, stacking identifiers, repeated categories, ranged subtypes,
non-Shield melee sources and spell all-damage reductions are refused.

Source `army.cpp` at reconstruction pin e12906accf96c2662b1abadbc49ddc111991bf21:
`getUnitCombatValue` (2285) multiplies the defense term by
`computeDefenderDamageReduction` (2687), then takes the square root and scales
by base fight value. The latter composes Shield and hero defense factors.
`getLossCombatValue` (2364) uses that value except for its kills-only override.
`ai_tactical.cpp:218-223` subtracts the actor's ordinary value loss from the
opponent's mode-dependent loss. Accordingly, both the melee admission score and
its signed-32-bit overflow bound use the composed source combat value. The
existing attack/defense baseline ensures zero differences in this narrow domain.

This changes no native damage rules. Detached native midpoint damage remains a
host adaptation, not original `adjustDamage` or retail parity. Peer ranged losses
are unchanged. Unique landing, threat, unknown-bonus, nonnegative score, owner,
role, attack/defense and arithmetic guards remain in force. A source-controlled
combat-value test compares composed factors and wound rounding with the existing
pinned reconstructed function bodies. Native tests exercise adjacent and unique
off-row landing, actor/target/both reductions, survivor retaliation, lethal hits,
BattleProcessor acceptance, nonmutation and refusal cases. Native execution must
be recorded separately; test source alone is not qualification.

Vanilla `config/skills.json` Armorer uses BASE_NUMBER, which equals its single
record value in the admitted category. Mixed or multiple records and specialty
PERCENT_TO_BASE modifiers remain unsupported. Two native actor-only cases set
real expert Armorer and cast Shield through the server with basic Earth Magic,
check the inherited skill encoding and separate 15% factors, then validate the
exchange and accepted attack. They do not synthesize the Armorer or Shield bonus.

Ranged peer-defense successor: incapacitated non-shooter peers now admit the same
strict constant nonspell all-damage Armorer category plus one additive Air Shield
spell record in the ranged category. The shared combat-value helper selects the
matching attack category; melee retains its prior Shield-only domain. All other
records, duplicates, specialty modifiers, invalid durations, values and effect
ranges remain unavailable. A valid melee Shield is still outside peerLosses.

Pinned army.cpp:2285 computes ranged defender reduction before its canShoot
fallback; a non-shooter victim therefore retains the Air Shield/Armorer defense
factor. computeDefenderDamageReduction:2687 composes these factors. peerLosses
uses that factor for ordinary loss and retains the 1000 kills-only override.
Every live unit must pass strict record and unchanged-stat guards. Four score
modes must still agree, including original-slot ties; factor-only disagreement
is explicitly unavailable. Native damage still uses detached midpoint estimates.
At 100% reduction native damage clamps to one while source combat value is zero.
This is not retail damage or rounding parity.

Four new native cases cover actual expert Armorer plus server-cast expert Air
Shield, accepted native shooting without retaliation, HP/ammunition/position
isolation, wounded top units, 0/100 boundaries, supported durations/value types,
factor-only admission/refusal, and malformed/duplicate/specialty-style records.
Source-only or portable checks do not qualify native execution; record the
compiled inventory and runtime XML separately. Previous melee/unique-landing,
status, ownership, offensive-modifier and arithmetic restrictions are unchanged.

Off-channel protection successor deliberately replaces the former off-channel
refusal boundary above. A single validated melee Shield and a single validated
ranged Air Shield may coexist with bounded Armorer. Both records are validated
before the irrelevant category is skipped. Pinned army.cpp:2687-2703 and native
damageCalculator.lua select only the matching attack category. Duplicate,
expired, action-expiring, malformed and specialty-style records remain refused.
Stone Gaze and existing geometry/ownership/offense restrictions remain unchanged.
