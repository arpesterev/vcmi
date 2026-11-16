# Weakness direct spell integration

Pinned source sushi-shi/homm3-decomp e12906accf96c2662b1abadbc49ddc111991bf21, ai_tactical.cpp getWeaknessValue, getAttackSkillValue and getAttackBoostValue. Adds targeted basic/advanced and expert mass Weakness to the native chooser.

Source valuation deliberately uses a capped positive hypothetical attack-skill bonus even though the native spell reduces attack. This behavior is preserved, not replaced by a native damage-savings heuristic. Ordinary certified opposing target/time and enemyCanAttack membership are required; killsOnly and winning forecast suppress pricing. Exact fixed-base100 estimated-damage ratio and current troop average damage with target-health cap use the existing source ordinary damage ladder. Nonzero luck, unsupported specialties and forecast profiles remain outside the domain. Existing Bloodlust/Precision on the target are excluded to keep source base attack distinct from channel-adjusted attack. Existing Weakness and effect cancellation are unsupported.

The original Weakness helper does not apply a separate resistance multiplier; this is preserved rather than borrowed from Misfortune. Native spell legality, immunity and effect validation remain in force. Preview must add exactly canonical attack -3/-6 and preserve other mechanical bonuses, count, health and position. Native effect application is distinct from source pricing.

This is a bounded direct spell consumer, not full global caster parity or complete post-simulation projected-state reconstruction. Validated in a48 native3331/3331 with the inherited23 exclusions; later a51 full3353/3353 remains green. This does not expand the stated domain.
