# Ordinary ground/shooter two-prefix source core

This is an isolated forecast model, not the live HoMM3 controller and not a
claim of retail-binary equivalence. The reference is the CC0 reconstruction
`sushi-shi/homm3-decomp` at `e12906accf96c2662b1abadbc49ddc111991bf21`.

The implementation runs the two ownership prefixes of `simulateCombat` with a
shared expected-damage ledger. Ground decisions include source path queue and
predecessor ordering, target census, retaliation-removal opportunity value,
enemy/friendly danger maps, attack-hex selection, randomized target ranking,
move-toward, WAIT/DEFEND, and ordinary melee/retaliation/double strike. Shooters
use the same ground-aware census. MOVE/WAIT/DEFEND do not move simulated units:
that is the source forecast's behavior, not a missing live movement update.

## Explicit input contract

`ordinaryDomainKnown` is false by default. A producer must prove all of:

- Ordinary single-hex walking units or ordinary shooters; static complete map,
  no siege, moat, placement, flying, teleport, two-hex units, machines, towers,
  charge, multi-head, breath, harpy return, special attacks/procs, retaliation
  exceptions beyond the supplied pair facts, or firewall effects.
- Unchanged ownership/control, complete contiguous original rosters (at most
  21 per side), unique live heads, no ambiguous source move-order comparator
  ties, and a source path speed/budget in the checked range.
- Exact source-valued ordinary unit and pair facts, not VCMI battle estimates.
  Melee pair facts may be required for same-side and self pairs: the source
  `getAttackChange` uses persistent `m_currentSide` even on the second prefix.
- Initially cleared per-unit AI target/census scratch. The source updates only
  the requested side, and this model preserves those updates across prefixes;
  it does not accept arbitrary pre-existing target scratch from a live battle.
- Source hero ranged-contribution and enemy area-spell loss supplied externally.
  This model neither reconstructs those spellbooks nor invents their values.
- Explicit 75..100 random draws in source invocation order. Missing draws or
  pair facts and checked source signed-32-bit overflow make the result unavailable.
- Ground path actors need a valid occupied source head. Live retained corpses
  without occupancy are not a universally supported domain; simulated-dead
  units retain their original physical map occupancy as in the source forecast.

No native producer or consumer is enabled. Full battle mechanics, actual
complete native live chooseMeleeAction (including casting/shooter defense), general
spell hypothetical-state forecasting, and complete HoMM3 AI remain separate.
A checked closed-domain non-simulated melee source leaf is now described in LIVE-MELEE.md.

## Independent oracle and test

`PinnedGroundTwoPass.h` runs pinned reconstructed function bodies, not production
choosers. `PinnedForecastPath.h` runs the independently pinned original path
search and buildCombatPath, with copied map/unit inputs. `long` is fixed to
signed 32-bit in the source bodies. `source-manifest.json` records hashes of
exact upstream methods. `ground-bodies.pinned.inc` retains those exact methods;
`ground-bodies.instrumented.inc` renames the wrapped chooser, records danger
maps, and substitutes explicit hero ranged values/no siege only in
hasRangedAdvantage. `getAreaEffect` is a supplied-value ordinary-domain shim.

`generate_oracle.py /path/to/pinned/upstream` regenerates the harness; the
upstream directory must contain ai.cpp, ai_tactical.cpp, findpath.cpp, cmbtmgr.cpp.
The harness reuses adjacent independently pinned exchange/census/move-order
bodies. It never includes production forecast or path headers.

The driver uses exceptions rather than assert, including with `-DNDEBUG`.
It compares order, both stopped cursors, restored scratch, chosen action/target,
move destination/attack hex, melee value, all 187 danger cells, dangerous-enemy
mask, every action ledger, final ledger, and random draw count. Inputs include
both requested sides and current-side mismatch, varied difficulty/humans,
wait/done/phase states, immobilizing spells, blessing/curse/luck, double attacks,
non-retaliation, bound/zero-speed units, summoned flags, nonzero area/hero values,
obstacle corridors and closures. Invalid-domain checks run only on production,
not through source expressions with undefined arithmetic.
