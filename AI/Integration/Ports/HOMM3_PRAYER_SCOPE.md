# Direct Prayer valuation

Source: `sushi-shi/homm3-decomp`, commit `e12906accf96c2662b1abadbc49ddc111991bf21`, `src/ai_tactical.cpp::getPrayerValue`.

The chooser admits standard Prayer as a direct positive enchantment. Its source composition is defense plus speed plus attack when the target arrival time is one. Each component is separately truncated before addition. Source `winLikely` suppresses attack and speed, not defense; the existing `spellsNotRequired` healing-only gate still suppresses Prayer entirely.

Each component is evaluated on an isolated stack with only its matching Prayer bonus. Defense uses the existing representative incoming-threat projection. Attack uses the existing immediate legal target and native damage estimator. Speed uses the existing arrival and order-value projection. These target, strength, damage and path facts remain native adaptations, not a claim that the complete source target census or forecast has been transferred. A chosen spell is sent through normal native hero casting.

The effect admission requires exactly the three standard additive Prayer bonuses, matching positive duration and unmodified metadata, with the standard mastery amount (+2 at none/basic, +4 at advanced/expert). Attack and defense deltas must match; an independent speed-only native projection must match the actual preview's speed. Clones, summoned units, turrets, inactive units and already-Prayered units are not scored. Noncanonical specialty/mod/script outcomes are unavailable, not assigned an invented score.

Prayer's source speed valuation deliberately uses `m_baseSpeed + bonus`, even when Haste/Slow changes actual current/post-cast speed. The native adapter therefore removes Haste/Slow when reconstructing this source baseline, but separately checks the actual native speed effect. This does not change the engine's spell stacking or the previous Haste/Slow valuation branches.

Prayer cancellation/removal valuation remains unsupported. This addition does not complete all HoMM3 spell families, source target/forecast behavior, GUI-autofight, or platform acceptance.
