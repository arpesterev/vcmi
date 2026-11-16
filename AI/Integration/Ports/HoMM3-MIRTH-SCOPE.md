# HoMM3 direct Mirth bridge

Source: sushi-shi/homm3-decomp commit e12906accf96c2662b1abadbc49ddc111991bf21,
src/ai_tactical.cpp: getMirthValue, valueOfLuckAndMorale,
considerSingleEnchantment and considerEnchantment.

The direct casting path now prices canonical +1/+2 Mirth at base morale -3 through
+3. The scalar matches the pinned source body, including its negative-morale
arithmetic, duration scaling, completed-turn deduction and integer truncation.
Targeted spells retain active-stack/last-action timing; mass spells may cast now.
Unlike several other buffs, source Mirth has no separate winLikely rejection.
The existing spellsNotRequired forecast gate is unchanged.

Native legality and HypotheticBattle apply the effects. Admission requires one
canonical timed Mirth morale bonus, stable owner/health/count/position and
unchanged non-Mirth bonus values. Preview bonuses are independently owned, so
pointer identity is not a preservation test. Mechanical scalar fields are compared
directly; native-terrain sentinel limiters and aggregate limiters compare by value
without resolving sentinel IDs as config names. Other component configurations
are compared by value; unavailable component serialization declines the candidate.
Cosmetic description, hidden flag and icon are not mechanical comparison fields.

Existing Mirth, Sorrow cancellation, clones, inactive units and noncanonical
custom side effects remain outside this direct bridge. Native combat values and
the broader spell ranking remain host projections, not a complete source forecast
or a claim that the original hero spell AI is finished.

Regression cases include basic/advanced targeted casts, expert mass cast,
negative morale, capped morale, immunity, existing Mirth, Sorrow exclusion,
preview ownership preservation and changed bonus-value/owner/limiter rejection.

## a53 cancellation extension

Canonical sole Mirth removal now admits cleaned morale -3 through+3. Pinned getCancelValue cancels the positive effect on a copy and revalues it with the owner caster and ownership sign; getMirthValue has no win/attack-membership guard. The existing native combat-value/duration projection is retained. This does not establish complete opposing-caster forecast parity.

Native preview must preserve count, health, position, effective owner and all non-Mirth mechanical bonuses. Canonical +1/+2 timed effects only; noninvertible morale clipping, immunity/no change, mixed removal and negative friendly value remain unavailable or nonbeneficial. Original negative-morale arithmetic is unchanged. Targeted Dispel restores native negative morale; it does not invent H2/H3 morale rules.

Controlled active-stack fixtures cover all negative baseline/mastery pairs, waiting/completed scaling, friendly cost, mixed refusal and a real three-faction army. Positive cases use factory decisions, authoritative server cast/mana/effect application and completed battles. Refusal cases stop after their negative assertions. These are not unforced scheduling or Mac GUI acceptance.

Accepted a53: directed33/33 and complete Linux native3369/3369, same23 exclusions. No Mac execution.
