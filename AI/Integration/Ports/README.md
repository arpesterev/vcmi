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

Siege assault now projects pinned fheroes2 `meleeUnitOffense`'s
last-resort advance towards walls when no enemy attack position is reachable.
Heroes II targets cells beneath walls; VCMI uses a nearest-garrison-footprint
approximation and native reachable head hexes. The Heroes III wall geometry
and siege rules differ, so this is not exact Heroes II pathfinding. Native
siege action acceptance and result require exact-head server CI confirmation.

Cautious melee movement now projects the pinned fheroes2
`findOptimalPositionForSubsequentAttack`: inspect native-reachable steps
of a multi-turn pursuit, prefer minimal projected enemy melee exposure,
and choose the farthest step on a risk tie. Its VCMI footprint-distance
threat bound is not the original Heroes II pathfinder or combat-value
function, especially around walls and obstacles. Native server evidence
for this particular change is pending exact-head CI.

Elemental summoning is now an explicit experimental projection of pinned
fheroes2 `spellSummonValue`: value the newly created or reinforced friendly
elemental stack in a `HypotheticBattle` preview, halving that value if our
army is already more than twice as strong. The VCMI server must accept the
hero spell, create the elemental and deliver unit-change events in the
native regression. This uses Heroes III elemental placement, spell cost
and creature strength, **not** a claim of identical Heroes II summoning.
The live regression is unverified until its exact-head CI run succeeds.

Spell selection also values the pinned `getSpellDisruptingRayRatio`
(0.2 of target strength for a full defense reduction, with the source's
weaker-army discount). VCMI's hypothetical cast supplies the actual target
defense before/after, including immunity and already depleted defense, so
an ineffectual cast is never given a fictitious benefit. A native test requires
an engine-accepted Disrupting Ray cast and a completed battle. This is not
source-identical spell damage/stat arithmetic or a complete buff/debuff policy.

Spell selection also applies the pinned fheroes2 `spellEffectValue` base
ratios for **Bless** and **Curse**: 0.15 of the relevant unit's projected
combat strength (0.30 when counteracting the opposite effect), and zero
for creatures with fixed base damage. The partial VCMI projection checks
that a hypothetical cast actually adds the native ALWAYS_MAXIMUM_DAMAGE or
ALWAYS_MINIMUM_DAMAGE damage-roll bonus. VCMI does not change the stack's
getMinDamage/getMaxDamage bounds for these spells; it changes which bound
is rolled. Already-effective, immune or no-op casts receive no fictitious value. Standalone contracts check the ratio and
negative cases; native battle fixtures require each spell to be cast by
the actual FHeroes2Tactics controller, the new damage-roll effect to be
observed after the engine's effect packets, and the battle to end normally.
This is not complete fheroes2 spell planning, spell duration parity or
identical Heroes II/III combat arithmetic.

Stone Skin also uses the pinned fheroes2 `spellEffectValue` base ratio of
0.10 of allied stack strength. The native hypothetical cast must increase
VCMI defense before the spell earns any value; resisted, already-active and
otherwise ineffectual casts earn zero. A live-battle regression checks the
actual defense increase after effect application and a completed battle.
This does not establish parity for Heroes II spell duration or defense math.

Bloodlust follows the pinned fheroes2 `spellEffectValue` 0.10
strength ratio, but only when a real VCMI hypothetical spellcast increases
the unit's **melee** attack. A resisted or already-active Bloodlust has no
value. The live-battle regression checks the accepted spell and the
subsequent melee attack increase after server effect packets. This does
not establish Heroes II spell-duration or attack arithmetic equivalence.

The fheroes2 `spellEffectValue` Shield priority uses 0.30 times the
enemy ranged-strength share (1.25x for friendly archers). This is deliberately
mapped to VCMI **Air Shield**, not VCMI Shield: the latter protects against
melee damage in Heroes III, whereas the Heroes II source prioritizes shielding
against ranged troops. A native hypothetical cast must actually add Air
Shield before a unit receives a positive score; zero enemy ranged strength
cannot justify the spell. The deterministic ratio contracts and a real
engine-accepted Air Shield battle establish this *cross-game semantic
projection*, not identical spell identities, damage rules or upstream parity.

Hypnotize now uses the pinned fheroes2 `spellEffectValue` control ratio of
1.5 times the enemy stack's strength. The VCMI hypothetical cast must
actually switch tactical ownership of a living enemy; resisted, redundant
or already disabled targets cannot receive fictitious priority. A native
multi-stack battle regression requires the selected spell, an observable
post-packet ownership change, real damage and an engine-decided battle.
This is not identical Heroes II control duration or spell-cost arithmetic.

Blind now uses the pinned fheroes2 `spellEffectValue` control ratios:
0.80 of an enemy stack's strength, or 0.40 against the final enemy when
retaliation remains possible and is not unlimited. The native hypothetical
cast must introduce a new Blind spell effect; an immune, already blinded
or otherwise incapacitated unit receives no value. A server-side battle
regression checks that the actual status was applied and combat completed.
This is a bounded source-derived priority, not Heroes II/III spell-duration
or retaliation-behavior equivalence.

Spell selection now also evaluates `Haste` and `Slow` using the pinned
`BattlePlanner::getSpellHasteRatio` and `getSpellSlowRatio` priorities,
but derives speed changes from an actual VCMI hypothetical spellcast.
The target's current movement and relative army speed, shooting class,
existing opposing speed effect and distance influence value; no speed
change gives no value. Native engine battles check non-damage Haste
and Slow decisions and real combat resolution. This bounded port does
not implement all upstream buff/debuff evaluation, dispels or exact
Heroes II battlefield-geometry and tactical-stance semantics.

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

Current additional coverage includes source-derived hero retreat/surrender,
farewell damage before withdrawal, deathless-round retreat, Berserk creature
turns, Clone, offensive Teleport, Earthquake, Slayer, and selective Cure/Dispel
valuation. Retreat reads the public match difficulty and uses the pinned
Easy/Normal/Hard/Expert/Impossible thresholds, with native legality, hero-value
and player-control gates retained. These implementations use native VCMI
legality and state previews; the older incremental descriptions above are not
a complete feature inventory. See `../ACCEPTANCE_MATRIX.md` for pinned CI
evidence; subsequent revisions require their own exact-head qualification.

Heroes II Berserker targets enemies, while VCMI's advanced/expert Berserk is
indiscriminate area magic. The port now rejects an area that would newly compel
an allied living stack, using the actual hypothetical effect transition.
Immune or already-compelled allies do not exclude a cast. Enemy-only candidates
retain the existing source-derived ratios. This conservative host projection
does not claim an optimal friendly-fire tradeoff or upstream area-spell parity.
The mixed-cluster native regression must pass server CI before this change is
considered engine-validated.

Not implemented or equivalent: source Anti-Magic valuation, complete
effect-duration/turn-order and Dispel valuation, complete archer kiting and
screen placement, exact siege positioning, upstream battle valuation,
adventure-map planning, and support validation for arbitrary mods. Native
Heroes III manual creature-spell choices are also unsupported; the pinned
Heroes II unit planner does not supply an equivalent manual-ability policy.
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

The pinned `type_AI_attack_hex_chooser::getHexAttackValue` also rewards
attack hexes adjacent to an opposing shooter that was not already engaged.
The source helper's survivor-health scaling and minimum-one contribution are
covered by contract tests. A narrow native projection now uses VCMI damage
estimates (incoming ranged vs melee) as the engagement value. Both
occupied hexes of wide attackers and wide shooters are now evaluated; each
newly engaged shooter contributes at most once, as in the source's shared
checked mask. Shooters already blocked by the moving stack at its current
position are excluded. Pure tests cover tail-only and invalid contacts; real
server battles cover ordinary melee-versus-shooter and a wide Cavalier
issuing a legal attack against a shooter. This is NOT the original HoMM3
per-creature combat-value pricing, and does not reconstruct the complete
multi-head or breath attacks, path-wide threat map, or original threat-array
scoring. Engine acceptance alone does not prove source decision parity.

The next independent HoMM3-only slice projects the signed landing-risk term
of the source `checkAdjacentHexes`: for a legal melee destination, native
damage previews approximate the strongest next attack from another enemy.
Reach uses a hex-distance upper bound, not a real enemy path; shooter status
uses currently legal native shooting. Primary-target direct retaliation is
already accounted for separately. For a wide stack, the more exposed cell
dominates via the source's `min(enemy_attacks[head], enemy_attacks[tail])`.
This is NOT an accurate reconstruction of HoMM3's precomputed threat field,
all possible special attacks or obstacle-aware reachable positions.
The TUM composition remains unchanged. A two-enemy VCMI battle regression
exercises this projection and a terminal server battle result.

The bounded HoMM3-only VCMI landing-threat approximation now preserves
unrestricted `FREE_SHOOTING` threats even when a future attack footprint
engages the shooter, while respecting native `LIMITED_SHOOTING_RANGE`.
A real server regression checks ordinary and unrestricted adjacent shot
legality, an independently selected and accepted HoMM3 melee command,
observed ranged attacks, damage and a terminal result. Pure tests cover
restricted/free shooting and shooting-range boundaries. This does not
reconstruct the original `enemy_attacks` field or prove that the original
HoMM3 would choose the same attack hex. The TUM-composed baseline is not
changed by this HoMM3-only projection.

The pinned `type_AI_combat_parameters::simulateAttack` applies its first
melee blow, the surviving defender's retaliation, and then a possible
`creatureTwoAttacks` second blow. For the HoMM3-only two-strike case, the
VCMI estimator now previews the retaliation from a detached defender after
representative first-hit casualties, instead of charging the untouched stack's
full counterdamage. The Crusader/Swordsmen server regression verifies reduced
counterdamage estimates without mutating a live stack, one actual intervening
retaliation, two Crusader hits and a terminal battle. This is an approximate
expected-damage projection, not the retail HoMM3 combat simulator; independent
change of tactical target choice is not established. TUM's partial baseline
is not changed.

Native VCMI `FIRST_STRIKE` is not part of the pinned HoMM3 combat
sequence. A preemptive hit can reduce, but not kill, a multi-target attacker.
For HoMM3-only collateral previews, `projectedCollateral` now applies the
representative counterdamage to a detached attacker before valuing splash
against secondary stacks. A separate server regression exercises partial
preemptive casualties, independent `WALK_AND_ATTACK` selection, real
attack packets and battle completion. This remains approximate damage
valuation, not retail HoMM3 behavior or proof of identical target choice.
The TUM-composed partial baseline remains independent.

The implemented unit actions are ranged attack, melee attack, approach,
defence, catapult, first aid and end of tactics. There is no source-equivalence
claim for movement, spell selection, hero retreat, battle outcome or campaign
planning. UI labels this as a partial port and does not claim that the
complete original HoMM3 AI has been delivered.

### Independent HoMM3 hero spells

`HoMM3SpellPolicy` selects hero spells without calling any other AI policy.
It shares only VCMI's `HypotheticBattle` rule simulation. Each candidate uses
native legality, mana cost, targeting, immunity and actual projected effects.
`castEval` omits the native pre-cast random processing. Direct hostile spells
with any positive native Magic Mirror chance are therefore conservatively
excluded using the engine's massive/range/negative-spell conditions. Area,
mass, positive and neutral spells are not excluded merely because a target
has Magic Mirror. Native resistance probabilities now discount negative magical
spells: the already-capped native health-loss preview is discounted before the
wake-up penalty. This is a host approximation of `getDamageValue`, whose original
raw damage is discounted BEFORE casualty capping. Overkill therefore can rank
differently. Blind value is discounted after its duration/retaliation formula,
following `getBlindValue`. Positive, neutral and nonmagical effects bypass this
native resistance gate. This is deterministic valuation, not sampled execution or
a complete joint outcome forecast. Raw pre-cap damage for every effect and Chain
Lightning hop is not exposed by the preview used here; exact source ordering
remains outstanding.
The selected command goes through the ordinary server spell callback, with
no unit command submitted until the spell has been applied. Disabling spells
in autocombat preferences disables this policy. Siege machines and compelled
Berserk activations do not open a hero spell decision.

The pinned `src/ai_tactical.cpp` supplies the following policy components:

- `getDamageValue`, `getMassDamageEffect`, `considerAreaEffect` and
  `considerChainLightning`: damage is capped, damaging an incapacitated
  enemy can have negative value, and area spells must improve both the
  absolute exchange and the proportional army loss. Native spell previews
  determine affected creatures, collateral damage and chain jumps.
- `getAttackBoostValue`, `getBlessValue`, `getBloodLustValue` and
  `getPrecisionValue`: Bless, Bloodlust and Precision use square-root attack
  improvement, damage caps and duration. Bloodlust requires melee;
  Precision requires a legal shot, including native ranged-only creatures
  without melee capability. Native immediate attacks substitute for
  the original precomputed AI targets.
- `getBlindValue`: Blind requires a currently threatening enemy and uses
  the source duration/last-awake-enemy retaliation distinction.
- `getCancelValue` and `getCureValue`: Cure restores real health; Cure and
  Dispel value removal of Blind, Curse, Bless, Bloodlust and Precision.
  Removing friendly beneficial magic is charged against the gain. A cast
  that also removes an unmodeled spell effect is conservatively rejected.
  Combined attack-effect removal with equal remaining durations is a signed
  native damage-delta approximation; mixed durations and Blind combined
  with attack effects are conservatively rejected.
  releasing Blind uses its remaining-duration value, without reproducing
  the original opposing-caster forecast.
- `considerResurrect`: Resurrection and Animate Dead value actual restored
  creatures. A cast that can restore no more than the actual casualty count
  is eligible even below three-quarters losses. Over-capacity healing still
  requires that heavy-loss threshold without the unported predicted-win
  exception. Capacity comes from native heal.lua's pre-cap effect formula.
  The source stronger-army/one-round urgency multiplier is applied using
  the existing native combat-value horizon.
- `type_AI_combat_parameters`, `getDuration`, `castSpell`: the original
  seven-rung planning horizon, completed-turn duration adjustment and
  integer-affordable-casts mana weight are retained with native combat
  values. Stable iteration replaces the original per-spell random factor.
  Native acted and defended stacks have completed their turn; waited stacks
  remain eligible for their later action and do not receive that deduction.

The four elemental summoning spells now follow `considerSummon`'s normal
created-creature valuation, using native simulated new/reinforced elemental
counts and native combat value in place of the source creature AI-value table.
A legal candidate with no new troops has zero value. `getCloneValue` prices the
original stack's immediate attack against its selected target, only if the
original has not completed its turn and the preview creates a new living clone.
Creation is read from the simulated Unit view, not the delegated CStack view.
The source predicted-win and kill-only branches remain absent for these spells.

Single-target hostile spells follow the available `shouldAttackNow` branches:
cast on the last friendly action, before the active stack's immediate melee
exchange unless it blocks retaliation, or before a threatening enemy acts ahead
of every other ally. Native immediate targets and the current native turn order
replace original precomputed AI targets and expectedMoveOrder. A missing enemy
in the current queue does not invent imminent danger. The source kill-only
branch and predicted-winner mana conservation remain unimplemented.
Friendly spells can also cast for the active stack; mass/area damage and Chain
Lightning can cast immediately. Creature value mathematics, full resistance/reflection outcome distributions,
kill-only valuation when badly outnumbered, other buffs/control,
Teleport, Sacrifice, Earthquake and complete cancellation remain unported.

Retreat and surrender remain unsupported. The source decision is in pinned
`src/ai.cpp::aiCheckRetreat`, not `ai_tactical.cpp`: it requires tavern and
siege escape checks, scenario victory conditions, artifact/experience value,
treasury reserves, difficulty and a combat forecast. This slice does not
replace those checks with an invented strength ratio or another AI.

`ai_homm3_spell_source` tests the scalar source projections independently.
The `homm3Hero*`, `homm3Bless*`, `homm3Blind*`, `homm3Cure*`, `homm3Dispel*`
and `homm3Resurrection*` native cases check selected server-accepted casts,
real effects, mana/health preview isolation, preferences and battle results.
They require the native VCMI test build; standalone checks do not establish
that these cases passed or that the retail source makes identical decisions.

## TUMEnchantedTactics

`TUMEnchantedTactics` is an **experimental, selectable, partial** tactical
projection of the published TUM Enchanted combat changes. TUM Enchanted is an
ERA mod of HoMM3 rather than a standalone AI implementation. Its upstream
[AI Improvements.txt](https://github.com/SadnessPower/TUM-Enchanted/blob/475d80bbf3e935ac854bcb6e68ef02019efb8de3/AI%20Improvements.txt)
documents combat behavior. Public source also includes the tower-selection
functions `FU15050`/`FU15051` in
[`Data/s/AI.erm`](https://github.com/SadnessPower/TUM-Enchanted/blob/475d80bbf3e935ac854bcb6e68ef02019efb8de3/Data/s/AI.erm)
and Ben80's published
[`Battle_AI` pursuit-penalty C++](https://handbookhmm.ru/forum/viewtopic.php?f=56&start=40&t=529).
The tower script supplies numerical weights, excludes Blind/Stone/Paralyze,
and falls back to the original AI if all targets are excluded. Its tie-break
uses creature minimum damage. Those rules have not yet been integrated into
VCMI's automatic tower/ballista path. The pursuit formula is also not yet
implemented here. Public availability does not establish reuse terms: no
explicit reusable-code license was established for these components. The
remaining ERA/DLL behavior is not fully recovered or runtime-validated.

The TUM backend explicitly composes the experimental HoMM3 baseline with
five independently implemented overrides derived from the documentation:

- **Ballista:** excludes targets affected by Blind, Paralyze or Stone Gaze;
  with no remaining legal target, it defends instead of waking a disabled
  stack. We check actual VCMI spell-effect bonuses, not `isFrozen()` alone,
  because the latter only detects Stone Gaze. VCMI Artillery may grant
  additional ranged attacks; the partial native targeting score now ranks
  **cumulative projected loss over the legal volley**, capped by both live
  target HP and remaining machine ammunition. A native battle checks an
  expert-Artillery ballista proposal, server-accepted attack sequence and
  actual result, without claiming the original TUM numerical formula.
  **Manual-control boundary:** this TUM decision requires a server-delivered
  unit activation, e.g. from Artillery. Without a native `MANUAL_CONTROL`
  bonus, `BattleFlowProcessor` resolves `CPU_CONTROLLED` ballista turns
  before `TUMEnchantedTactics::activeStack` is called. A separate native
  server regression distinguishes automatic ballista shots from TUM decisions.
  The published rule against waking disabled enemies is therefore **not yet
  enforced on automatic ballista turns**. Changing that requires an explicitly
  authorized integration at the automatic-action boundary, not an invented
  tactical activation.
- **Arrow towers (blocked at the policy boundary):** VCMI
  `BattleFlowProcessor::tryMakeAutomaticActionOfRangedUnit` handles
  CPU-controlled citadel towers before `CBattleGameInterface::activeStack`.
  A live citadel regression confirmed that a proposed tower action in the
  experimental adapter is never received as an engine-accepted unit action.
  The attempted override was reverted. The documented but unpublished TUM
  tower targeting cannot honestly be claimed as a functioning port without
  a separate, explicitly scoped engine extension, which this branch forbids.
- **Shooter protection:** when facing neutral melee-only stacks, allied
  ranged troops still have usable shots, and no neutral stack can reach the
  friendly army in its next movement, the active friendly melee stack first
  **waits** (if it has not already waited), then defends on a second safe
  activation instead of rushing forward. This preserves a legal later-turn
  response; the native neutral battle asserts an engine-accepted WAIT and a
  completed result. Presence of enemy shooters is checked using
  the native ranged-unit classification, not immediate `battleCanShoot()`:
  an archer under Forgetfulness is still an enemy shooter for this conservative
  interpretation of the published TUM rule. Friendly shooters must be able to
  act and have at least one legal VCMI shot against a live enemy; otherwise a
  melee guard does not wait for a firing line that cannot shoot. An out-of-ammo
  archer remains a ranged creature, never a melee screen merely because
  `battleCanShoot()` is false. A native exhausted-archer encounter now checks
  the first TUM decision against the partial HoMM3 baseline and records
  actions actually accepted by the VCMI server. Movement reach
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
  If the minimum projected first-strike damage kills the target, the scorer
  assigns zero retaliation rather than valuing a counterstrike that the
  real server cannot execute. A native Hypnotize battle checks this guaranteed
  lethal bound and an engine-accepted subsequent attack.
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
