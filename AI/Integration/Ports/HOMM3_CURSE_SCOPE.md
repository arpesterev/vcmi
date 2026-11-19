# Direct Curse: bounded source-derived native projection

This increment implements direct hero Curse in HoMM3SpellPolicy. It does not claim complete HoMM3 AI or retail runtime parity. Experimental owned-state execution remains OFF. Existing Cure/Dispel/cancellation and every inherited identity file are unchanged.

## Pinned source

sushi-shi/homm3-decomp e12906accf96c2662b1abadbc49ddc111991bf21:

- src/ai_tactical.cpp: type_AI_combat_parameters constructor (line 285), getDuration (1755), considerSingleEnchantment (2017), considerEnchantment (2065), getCurseValue (2347), getEnchantmentFunction (2410), findEnemyAttacks (2716).
- src/army.cpp: getAverageDamage (2188), getUnitCombatValue (2285), getTotalCombatValue (2345).

getEnchantmentFunction dispatches Curse to getCurseValue. Single/mass enchantment selection excludes cannotAttack, invalid targets, and existing same-spell duration. Single hostile casting uses shouldAttackNow; mass casts are immediate. getCurseValue requires enemyCanAttack, !killsOnly, and !winLikely. It takes source integer total combat value, computes old per-creature average (Bless high+bonus, Curse max(low-bonus,1), otherwise (low+high)/2), and new max(low-Curse mastery bonus,1). Its three separate truncations are:

1. long(total - sqrt(new/old) * total)
2. long(value * getDuration(duration, completed))
3. long(workChance * value), when resistance is checked.

The independent PinnedCurse.h harness contains the exact getCurseValue function text, with input-only substitutes for the source API. Its provenance records the exact function hash. Portable test inputs stay in signed 32-bit long domain even on hosts with 64-bit long.

## Native adaptation, not equivalence

Native mechanics provide spell availability, mana cost, legal target enumeration, mastery, duration, actual castEval effect installation, Bless cancellation, and native damage estimates. A real legal immediate attack must lose native damage after projection. The native stack-damage midpoint is NEVER used as the source per-creature average: target modifiers and integer rounding do not commute with that ratio.

The old source enemyCanAttack census is replaced by the existing legal immediate native attack search. Thus an enemy reachable only by the unported multi-turn source census remains outside this increment. Resistance is the existing nativeSpellSuccessPercent mapping; reflection uses the existing conservative Magic Mirror exclusion because castEval omits beforeCast. Native smart/mass targeting remains engine-driven. Native action timing and final mana weighting remain inherited host adaptations.

## Context and killsOnly

The pinned constructor sets killsOnly when (friendlyTotal * 2 < enemyTotal) AND (difficulty > 0 OR IsHuman[ourGroup]). Generic VCMI estimateCombatValue is not that total and is NOT used for this gate.

For Curse only, ordinary source total formulas are reconstructed over public native facts: lowest attack/defense modifiers are the minimum of current native stat minus native creature base stat; native fight value supplies source baseFightValue; shooter capability and melee penalty use native callbacks; supported Bless/Curse affects the source unit-value ratio; total is truncated available-health * unit-value / max-health. Sleeping/inactive units remain in side totals and are excluded only from awake totals used for the source planning horizon. This is source arithmetic over native data, not proof of matching retail creature tables or modifiers.

Only friendly*2 >= enemy is admitted. That sufficient projected !killsOnly condition is independent of unknown difficulty/human mapping. All underdog contexts remain unsupported, including the original easy nonhuman exception. No unknown killsOnly is silently treated as false. Signed-long bounds reject friendly totals beyond INT32_MAX/2, enemy beyond INT32_MAX, and per-stack totals outside [0,INT32_MAX]. Empty sides are excluded. Integer-domain boundaries have separate portable checks.

The context rejects live turrets, siege weapons, clones, summons, changed control, any GENERAL_DAMAGE_REDUCTION, multiple attacks, modified native min/max damage, invalid damage ranges, simultaneous Bless/Curse roll flags, and nonstandard roll bonuses. Roll bonuses must be exactly one matching spell-origin N_TURNS INDEPENDENT_MAX bonus, value 0 or 1, with no limiter, propagator, updater, parameters, stacking, or restricted effect range. Direct Curse also requires the installed bonus value to match basic/advanced mastery and removal of old Bless. Unsupported context never rejects unrelated spell families.

## Forecast and duration

Known winLikely suppresses direct Curse, including wins with losses. The inherited spellsNotRequired gate still suppresses non-resurrection casts. Unknown forecasts retain Curse only if the separate ordinary context above is supported. This does not infer a win or loss from forecast absence. Completed targets lose one useful duration turn after the source duration cap. Existing short Bless enters the source current average; its remaining duration is not independently subtracted because getCurseValue does not do so.

## Verification limits

Portable independent source comparisons, boundary checks, exact source manifest, baseline preservation and patch replay are required for this candidate. Native server tests cover basic/advanced/mass Curse, Bless cancellation, one-turn defended targets, full resistance, Magic Mirror, undead targeting, unreachable threat, unsupported reductions/underdog context, repeated previews and sibling-fork isolation, mana, real effect application, forecast-known versus unknown behavior, alternate damage spells, and completion. These tests are added, not claimed passed until a clean native build/run produces evidence. Native build queue ownership is external to this increment.

Remaining gaps include full source census, underdog easy-AI context, special-unit and reduction contexts, source-native data equivalence, reflection valuation, full retail AI policy, and existing unrelated Clone winning-state suppression. Cancellation retains its previous continuous net-effect approximation; this change deliberately does not reinterpret it using direct Curse's integer ladder.

## a02 source-integer audit

The first candidate missed the constructor's signed-long awake-low * 5 intermediate. a02 rejects awake totals outside [0, INT32_MAX], and rejects min(awake totals) above INT32_MAX/5 before entering the horizon formula. The concrete 500,000,000 versus 500,000,000 state is unsupported, not assigned seven rounds. Boundary tests compare the exact pinned constructor tail only where its arithmetic is defined.

Additional explicit guards preserve the source arithmetic domain: low+high must fit signed32 (also proving high+Bless<=INT32_MAX for admitted bonus0/1); projected raw attack/defense modifiers and differences from their minima fit signed32; raw maxHealth*count fits signed32 before wound subtraction; available health is positive and no greater than that raw product. Native getFightValue returns int32_t, and negative values are rejected. The source operation order remains `availableHealth * double unitValue / maxHealth`, then truncation. Side totals are checked before addition; awake totals are nonnegative subsets of those bounded totals. No source overflow is silently replaced with wider-host arithmetic.

Seven additional native cases distinguish two-enemy aggregation from a one-enemy price by placing actual native Magic Arrow between those scores, verify friendly exclusion, exercise sleeping-unit inclusion, asymmetric source modifier admission/rejection, changed ownership rejection, and exhausted-shooter unreachable versus legal-melee behavior. They supplement, rather than replace, every original v20 test. Native compilation/execution remains pending.

## a03 native-fixture correction

The actual a02 native focused run compiled 1,997 tests (1,974 eligible) and completed 16/20 focused cases successfully. Its four failures exposed fixture premises: the normal BattleProcessor route uses the map's grass terrain, whose native bonuses give these Castle creatures +1 speed, +1 attack and +1 defense. The test fixture's neutral hero setup does not remove battle terrain bonuses. Therefore an enemy at hex100 can reach a melee landing next to hex94 in five steps, and explicit hero modifiers10/40 produce native stack modifiers11/41. This was not evidence for changing direct Curse admission or widening its supported source domain.

a03 leaves all production AI and the pinned source fixtures byte-identical. In each distant-threat case, new assertions first preserve the legal five-step boundary and require Curse selection, then move the friendly target to hex93, prove nearest melee landing distance6 exceeds movement5 and prove there is no available neighboring landing or legal shot. The original negative spell assertion follows unchanged, including zero-ammunition checks for the exhausted Archer. The asymmetric-modifier cases prove the native +1 on both sides, remove only the fixture battle's TERRAIN_NATIVE PRIMARY_SKILL bonuses, and retain the original exact0-versus10/40 assertions. The10 context remains admitted and the40 underdog context remains unsupported. No semantic assertion is weakened or removed.

The 20 cumulative test identities and all historical identity registries are unchanged. Portable checks additionally establish that a shared native modifier cancels against the source minimum without changing asymmetric context admission. Original a02 failure evidence is retained separately. Dispatch and aggregation implementation-only controls are preserved; no threat implementation mutation is warranted because there is no threat implementation change. a03 native compilation, focused/full execution and native controls remain pending and belong to the native build owner.

## a04 explicit native bonus membership cleanup

The a03 native gate passed both corrected threat cases and finished 18/20 focused cases, but the two asymmetric stat cases still observed modifiers 11/41 after removing the battle's exported terrain-stat bonuses. The native mutation path already calls nodeHasChanged and invalidates descendants. The remaining +1 is explained by active membership: setupBattle installs the bonuses with addNewBonus, which exports each one immediately, and the subsequent localInit calls exportBonuses again. Native read stacking deduplicates the identical pointers, while removeBonus erases only the first active occurrence. Removing the exported definition therefore leaves another active occurrence of that same bonus.

a04 makes this fixture setup explicit without changing production lifecycle or cache behavior. It retains the original attack and defense bonus pointers, asserts the exported definitions are absent and descendant versions advanced after the original removal, and asserts each same pointer remains in the battle's raw local list. It removes that remaining occurrence with the existing removeBonus API, then requires exported, local and inherited membership to be absent and both native attack/defense offsets to be exactly zero. Native movement remains five. Only then does the existing fixture set modifiers 10/40 and execute all unchanged admission/rejection assertions. All a03 threat cases and every prior test line remain present.

A standalone portable contract using the unchanged shared BonusGraphMutation kernel demonstrates two exports, first-occurrence removal with version advancement, and complete removal on the second call. This is not a native runtime proof. The actual a03 18/20 failure evidence is retained alongside a02 evidence. a04 production source is byte-identical to a03; no cache refresh, lifecycle normalization, source-domain widening or historical identity change is introduced. Independent review and native compilation/execution remain required.
