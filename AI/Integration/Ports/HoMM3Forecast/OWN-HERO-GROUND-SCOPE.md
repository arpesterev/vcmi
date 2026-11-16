# Own non-casting hero ground expansion

The ordinary ground bridge may now admit our own fighting hero against a side with no fighting hero. Both-hero-absent behavior is unchanged. The opposing player's hero remains unsupported, even when its visible current mana is zero.

Admission uses only public INBATTLE presence information for each side. If the opposite side has a hero, capture returns unavailable before requesting that hero pointer or equipment. For our side only, capture obtains the authorized full hero, checks its owner against the side player, and requires both `!hasSpellbook()` and `!canCastSpells()`.

The second condition matters: VCMI `canCastSpells()` permits a native SPELL bonus whose `parameters` says `castableWithoutSpellbook`. Artifact/free-spell grants therefore cannot be misclassified as no casting. All previous ordinary effective-bonus, map/lifecycle, active-identity and native legality checks remain.

Source basis at homm3-decomp e12906accf96c2662b1abadbc49ddc111991bf21:

- `ai.cpp::canCastSpells(side,true)` returns false for a hero without the spellbook artifact. No book therefore makes both the hero term in `hasRangedAdvantage` and the hero branch in `getAreaEffect` zero, regardless of current or maximum mana or stored spell list.
- Current mana zero is deliberately NOT an admission shortcut. `type_spellvalue` uses maximum mana and `getBestSpellValue(0x8000)` may still be positive. Titan's Lightning Bolt can have zero cost. Native no-book grants are rejected even though the original engine's book requirement is stricter.
- Current public primary attack/defense and supported passive Offence/Armorer records continue through `NativeExchangeFacts`, using source literal float factors and ordinary combat-value minima. No unknown enemy skill or spell list is synthesized.

This remains a source live decision over validated public effective facts, followed by a legal native command. It is not a complete hero spell AI or proof of equivalence for latent/custom proc definitions. The earlier ground exclusions (including shooters, unresolved source move-order ties, defend stance, special movement and created units) remain.

Native tests use a real own hero army and a neutral map army; source scalar/oracle comparisons supply the independently proven zero hero-spell contributions, not a guessed enemy spellbook. Negative fixtures cover own spellbook with current mana zero, no-book artifact casting, and an opposing hero with visible mana zero. The neutral-side controller still cannot use this branch while an opposing own hero is present.

## Own-town preflight certificate (a41)

The live mixed-history bridge now preserves source move-order ranks for an own non-casting hero whose owner has towns but none with a tavern. Pinned `ai.cpp::aiCheckRetreat` counts owned taverns before either `failedSiege` or `simulateCombat`; zero taverns proves this pre-AI branch cannot mutate move order. This generalizes the earlier no-town-only certificate without guessing artifact values, forecast outcomes, hidden enemy facts or Normal-difficulty randomness.

The public own-town list is rechecked before every own decision. Null town records, ownership mismatches, any owned tavern, missing game/player context, own spellbook or native no-book casting revoke the certificate. Merely removing or capturing an opposing tavern is not assumed sufficient: only the current authoritative owner's town list is consulted. Source rank state is preserved, never reconstructed after an unknown-order event. Existing geometry, effective-bonus, source-history, spell-pressure and native-action checks remain mandatory.

Six parameterized native cases cover near/far encounters with a real captured own town, source-rank preservation, live mixed choice/controller action equality, revocation after tavern construction or either spellcasting path, and two server-accepted commands. This is an enabling live-decision domain extension, not implementation of forecast-based withdrawal or full strategic AI.
