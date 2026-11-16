> Historical inventory: this document predates the a10–a14 native validation and contains superseded build/coverage statements. See [STATUS.md](STATUS.md) for the current verified boundary and remaining work. The old text is retained for history, not current readiness.

# Remaining FHeroes2 and HoMM3 gaps

Review baseline: VCMI aafa3de7cd5d93dbec49dda1702ebc5c699e0827 plus the
unpublished source candidates prepared on 2026-11-08 (candidate revision 13).
The selectable policies remain FHeroes2Tactics and HoMM3OriginalTactics.
The reserved complete-port identities are not implemented by these candidates.

An implemented candidate is not native-qualified. Source-model checks, exact
production slices with doubles, native engine tests and retail equivalence
are separate evidence classes. Passing one does not close another.

## Highest priority: qualify the combined candidate

**Open, toolchain and fixture prerequisites missing.** The complete pinned
main source and required googletest snapshot have been restored and verified,
including a byte-exact replay of the candidate overlay. A Linux runner now
verifies exact inventories, fixture bytes, source identity and completed XML;
its portable contract tests do not execute the engine. This is a materialized
source tree without Git history, not a native build. Obtain compatible build
tools/development headers and pinned native fixtures; then build real policy
headers and the engine and execute the required inventory on that revision.

Directed native cases must cover:

- Temporary/permanent resurrection, pre-existing temporary troops,
  winning/equal armies and count-neutral healing.
- Cure/Dispel on allies/enemies: Berserk, Blind, Paralyze, overlapping effects,
  final-enemy context, low duration, acted/defended/waited units, healing and
  mass casts with mixed turn states.
- Ranged splash head/tail choices with server-accepted destinations and damage;
  spell planning based on the same native primary damage position.
- Resistance on awake/disabled targets, lethal/partial hits, and melee charge
  estimates with obstructed routes and actual server path length.
- Shared FHeroes2 stance across creature decisions, Haste application/removal
  and offensive Teleport, including siege footprints, ownership and speed
  after moved/defended/waited turns.
- Haste/Slow approach casts against distant goals, stable goal choice,
  duration/round caps, flyers, obstacles and projected speed compatibility.
- Stone Skin, Shield and Air Shield projections: mixed ranged/melee threats,
  overkill, casualty thresholds, low duration, mass casts, projected Unit
  identity, no surviving mitigation and actual pathfinding costs.

Require preview isolation, accepted actions, observed effects and completed
battles. Standalone checks do not establish these native outcomes. No win-rate
or full-source-equivalence result exists.

## Next FHeroes2 source-fidelity work

1. **Full stance inputs, open.** Creature and spell policies now share the same
   existing native stance/footprint projection. All three Haste paths use it,
   enemy Haste removal uses the planner's enemy average speed, and offensive
   Teleport is suppressed while defensive. Source hero-spell/tower ranged
   strength, Heroes II geometry and exact creature strength remain unported.
   Sharing a projection closes the inconsistency, not original stance parity.
   Ranged strength, army descriptors, Haste/Slow ratios and defensive cover
   now require usable ammunition, preserving the supplied native environment. Zero-movement shooters now contribute to stance ranged strength. Native
   NOT_ACTIVE remains a broader projection than source Blind/Paralyze immovability. Offensive Teleport and Earthquake now use the same usable-ammunition
   classification. Coupled source tower strength and surviving-tower siege
   gating remain open; these two consumer corrections do not complete them.
2. **Broader effect and turn-order equivalence, open.** Lethal damage now maps
   native moved/defended to source already-moved speed suppression; WAIT stays
   eligible. Native spell duration, ownership, geometry and creature strength
   still differ. Air Shield application/removal now checks actual ranged-factor
   transitions independently of retained melee defense. Fixed source weight for
   partial protection changes, final damage rounding/other zeroing factors and
   other simultaneous removals remain unqualified. Disabled Haste removal now
   preserves the pinned STANDING-to-Haste weight for native Blind/Paralyze
   markers with NOT_ACTIVE, including retained disables. This is a source
   heuristic, not immediate restored movement. Disabled Slow now preserves
   archer/caster-side castle early-return weight and avoids false restored speed.
   Other incapacitation mappings remain open.
3. **Observed Anti-Magic threat, intentional information limit.**
   FHeroes2ObservedMagic uses public hero-cast events, public mana/power and
   untrained native costs/damage. Unknown enemy mastery, artifacts and spells
   are not reconstructed. This is not full upstream strategic magic value.

Exact archer kiting, screen placement, siege geometry, upstream strength/threat
calculations and adventure-map planning remain broader work.

## Next HoMM3 source and native-projection work

1. **Haste/Slow exchange and forecast, partial.** Stable native goals/path
   distances and source-shaped arrival arithmetic remain. Speed-crossing now
   adds two ordered melee exchanges on separate state copies for supported
   ordinary single-hex pairs. It includes first attack, survivor retaliation,
   optional second hit and the surviving opponent's attack sequence. Slow
   selects the greatest eligible friendly exchange contribution, not their sum.
   Native damage estimates and proportional combat-value losses are projections;
   exact original damage/clone/loss semantics are not supplied. Ranged exchange,
   wide/special attacks, charge-dependent and other excluded effects remain open.
   Current paths/targets do not predict future occupancy or casualties, and
   unsupported native speed/counterspell arithmetic is rejected.
2. **Winner forecast integration, partial bounded candidate.** The pinned
   spellcaster calls simulateCombat(side,false): it simulates the current
   friendly ownership prefix, then scans projected surviving enemies. Production
   lifecycle/order/snapshot and bounded ordinary shooter damage now feed
   this prefix. Available spellsNotRequired enables the original healing-only
   spell filter. Native midpoint damage on detached projected-HP states is an
   explicit adaptation, not source integer damage parity. Unknown lifecycle,
   comparator ties, multiple active targets, unsupported melee, unsupported units/bonuses
   and changing rosters return unavailable. Active-before-incapacitated
   priority is recomputed after each projected kill. Multiple incapacitated
   non-shooter peers can be ranked through original loss-value arithmetic
   when stat modifiers are zero, damage modifiers and sieges are absent, and
   all four killsOnly/difficulty modes choose the same target. Equal values
   use original roster slot. Active-peer target graphs, broader modifiers,
   mode-dependent winners and general melee/path/retaliation remain unavailable.
   A known non-EASY match also admits ordinary adjacent single-hex melee
   against one living enemy, with no opposing hero, obstacles or siege and
   nonnegative bounded exchange scores. Detached native survivor retaliation
   can now produce own casualties in a supported win. This is not retail damage parity.
   This is not full battle simulation or an army-strength comparison.
   Forecast hits retain original incapacitation statuses.
3. **Protective buffs, partial local candidate.** Stone Skin, Shield and Air
   Shield now have a separate native incoming-threat census and source-derived
   mitigation/casualty/duration value. They use real projected Unit damage
   estimates, with baseline and per-projection movement caches. The original
   winner-dependent expected-damage suppression now consumes matching per-unit
   health/wound and side facts from a supported ordinary ownership prefix. In
   casualty wins it suppresses targets with no expected creature loss while
   retaining casualty targets. Unknown, non-winning, mismatched or unsupported
   per-unit facts preserve the non-winning branch and can still overcast.
   The census uses immediate native legal threats, not the complete
   original target/retaliation simulation. Native damage means/ratios replace
   the original integer mastery-rate arithmetic; source-kernel comparisons
   cover integer inputs only. Zero-damage/infinite-ratio outcomes
   are unsupported. Full native behavior, trait-specific immediate timing,
   additional defensive families and protective-effect removal remain open.
4. **Winner-dependent choices, partial.** Healing-only mana conservation now
   consumes a supported shooter or bounded melee-prefix outcome. Direct Haste/Slow now suppress value only when the existing bounded forecast
   reports a win, including wins with casualties. Unknown forecasts preserve
   existing selection; cancellation and opposing-caster context are unchanged.
   Kill-only mode, additional protective families, and broader summoning
   exceptions remain unported. Supported shooter and melee-prefix wins now permit
   overcapacity Resurrection/Animate Dead below the usual casualty threshold,
   reject below-Advanced Resurrection and enable immediate restoration timing,
   as in considerResurrect. Unknown/undecided forecasts retain prior behavior.
   Native projected counts and mastery remain host adaptations; permanent
   healing does not convert previously temporary troops. Generic native NOT_ACTIVE is not
   equivalent to all reconstructed source immobilization flags. Coarse army
   strength is not the missing forecast.
5. **Cancellation combinations, open.** removalSupported permits only attack
   buffs, Curse and Blind. Unknown removed effects, mixed attack-effect
   durations and Blind with attack effects are conservatively rejected. This
   still includes removal of the protective and speed families. Add each missing
   effect with correct before/after ownership, duration and target context.
6. **Joint random spell outcomes, open.** Resistance weights each successful
   outcome, but does not enumerate changed Chain Lightning paths, joint
   survival gates or random secondary effects. Direct hostile spells subject
   to Magic Mirror remain conservatively excluded.
7. **Retreat/surrender and other spell families, open.** Source retreat needs
   scenario, siege, treasury, hero-value and forecast context. Teleport,
   Sacrifice, Earthquake and other control spells remain outside this hero
   policy. No fallback to another AI completes these decisions.

## Native behavior needing further investigation

- Audit melee defender-position forwarding against the authoritative primary
  damage route. The ranged correction deliberately preserved melee behavior.
- Future landing-threat bounds omit some enemy-hero grants and random on-hit
  effects. Public callback information limits remain explicit.
- Profile spell selection in large real battles. Contract tests constrain
  additional path and availability queries; they are not a runtime benchmark.
- Arbitrary mods, manual creature spells and wider siege reachability lack
  comprehensive validation. Standalone native-API doubles cannot detect all
  real type/header/engine integration failures.

## Implemented locally, native validation still open

Permanent-resurrection winning value; wide ranged splash aiming; Berserk
removal context; native resistance outcome weighting; Cure/Dispel duration;
ranged spell-preview positioning; final-enemy Blind/Paralyze removal; charge
path distance; shared stance for Haste and offensive Teleport; completed-turn
lethal speed; partial incoming-threat valuation for three protective spells; stable-target
Haste/Slow approach value and bounded ordered melee exchanges; ranged-factor
transitions for FHeroes2 Air Shield; source-mapped disabled Haste/Slow removal; and bounded ownership-prefix
shooter projection with healing-only spell filtering and source incapacitation
class priority, bounded incapacitated-peer numeric ranking and ordinary
adjacent melee against one enemy under proven source chooser gates; ammunition-aware
FHeroes2 ranged-stance strength, army descriptors, speed ratios, cover,
offensive Teleport and Earthquake.

Source references: pinned FHeroes2 0dbfe9f386f33c2500842f4a59cd284d7f03d541,
src/fheroes2/ai/ai_battle.cpp, ai_battle_spell.cpp and battle/battle_troop.cpp;
pinned reconstructed HoMM3 e12906accf96c2662b1abadbc49ddc111991bf21,
src/ai_tactical.cpp. Historical README results do not qualify new candidates.


## Hypothetical ammunition supply, open

Generic state copying preserves the provided environment but cannot correct
HypotheticBattle::unitHasAmmoCart returning false. A faithful repair must
preserve first-cart ordering, asymmetric ownership, artifact fallback and
removed-cart bonus detachment. No host supply repair is included. Current
new spell paths classify live before states. Native supply and battle
verification remain open.

The revision12 first-cart query experiment is excluded from this production
overlay. Static/query diagnostics pass, but parity deliberately fails on
inherited-control removal and reverse clone cleanup. A preview-owned bonus
graph retaining native limiter/updater context, provenance and propagation
links is required before lifecycle repair can be sound. Public mutable preview
state and indirect bonus changes prevented a meaningful safe bounded fallback.

## Revision 13 qualification limits

Four FHeroes2 archer classification gates now consume ammunition-aware state.
The per-target HoMM3 winning-defense suppression is reachable for Stone Skin
and Shield in the admitted casualty-winning melee domain. Air Shield shares
the gate and has wiring coverage only; effective ranged mitigation has not
been demonstrated in that domain. Native compilation, compiled test inventory
and battles remain unrun. The source-derived expected fixture count remains
553. No game-data archive password, extraction or content validation is claimed.

The separate bonus-model design requires materialized propagated bonuses,
exports, topology and pointer-identity groups in immutable snapshots. It does
not repair the excluded ammunition prototype's two lifecycle parity failures.
