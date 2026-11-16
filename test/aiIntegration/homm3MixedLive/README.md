# Ordinary mixed live source dispatch

This adds an isolated source-level `chooseOrdinaryLiveMixed` entrypoint. It chooses
for the supplied actual active unit, dispatching ordinary ranged or ground melee
and friendly-shooter defense through the reconstructed `doCompAI` call chain.
It is not yet a VCMI native consumer and does not complete HoMM3 AI.

Pin: sushi-shi/homm3-decomp `e12906accf96c2662b1abadbc49ddc111991bf21`.
This is parity with the reconstructed CC0 source, not retail executable identity.
`generate_oracle.py` reproduces four added exact method bodies and hashes.
The oracle reuses the previous independent scalar exchange/path/census host and
pinned bodies. It runs original `doCompAI`, not just its individual chooser leaf.
The only source edit is const qualification of the host getHex result. Macro
renaming adds a defense-call counter without changing branching; existing trace
wrappers observe original actions. The source's guarded uninitialized defense
locals trigger a GCC false-positive warning, suppressed only around those bodies.

## Caller state

See CALLER-AUDIT.md. Prior retreat and spell simulation may leave target scratch
and expected damage. Live melee rebuilds its own census; live shooter rebuilds
opposing census. Physical HP, exchange and combat value ignore expected damage in
non-simulated mode. The API preserves that ledger unchanged, including values
above current HP. No replay of the prior simulator is necessary.

One retained field is NOT scratch-independent: findMoveOrder excludes immobilized
rows without resetting their prior rank. Enemy census can read these ranks even
for processed corpses. Live melee therefore requires explicit priorMoveRank for
those rows; missing knowledge rejects. Live shooter does not read move rank and
needs no rank-order certificate. Roster indices are retained, never compacted.

## Supported domain and proof boundaries

Ordinary single-hex walking/shooting units, existing scalar ordinary exchange
facts, complete public ground map and pair facts, actual current-side active unit.
The melee actor requires noCreatureSpellKnown. Special creatures, siege,
placement, flying, double-wide, teleport, control changes, area attacks and
unsupported source arithmetic remain outside the ordinary-domain certificate.
Hero area-loss/ranged contribution are explicitly supplied source-valued facts;
this component does not read hidden spellbooks or solve hero spell selection.
Melee move-order ties retain the reviewed invariant-order certificate/budget.
Defense travelTime requires positive speed when that source division is reached.

Facing is supplied explicitly. Source initial facing is 1-side and supported
ordinary commands restore it, but no global action-boundary reset exists. A
future native mapper needs a clean supported-history certificate or known facing;
it must not assume facing from side on arbitrary loaded state. Similarly, native
corpse rank needs provenance or a conservative fallback.

Defense preserves six-neighbor direction order, openHex denominator including
unreachable empty neighbors, client-facing screen-X ties, later-client tie wins,
and the original last-examined-hex (not bestHex) DEFEND quirk. Movement toward a
client uses no danger map and disables WAIT, exactly as the caller supplies null.

## Tests and next native step

The standalone driver checks 4,900 mixed roster scenarios and 200 wall-isolated
friendly-shooter defense cases, explicit facing variation, prior poisoned target
scratch and excessive expected-damage ledger, prior two-prefix simulation,
processed corpses with varied retained ranks, unknown-rank rejection, area-loss
bypass, and no-melee-penalty client exclusion. Comparisons include actual actor,
action, target, movement/attack hex, melee value/danger, consumed RNG and ledger.
The older strict melee, forecast and tie-proof tests remain regression targets.

Next: native mapper must combine existing public ranged and melee pair producers,
retain current canShoot versus intrinsic SHOOTER distinction, certify facing,
map actual active and real public reach, and server-validate returned action.
No native bridge or live-user game evidence is claimed by this source checkpoint.

### Target-changing retained-rank regression

`setup(3,3,91)` with positions `[38,108,59]` / `[57,37,76]`, all melee,
side0/current0, difficulty4, phasefalse, draw90 and enemy slot2 corpse: retained
rank6 chooses enemy102 at attack hex38 with value1726; rank0 chooses enemy101
at attack hex56 with value426. Both variants execute original doCompAI and are
checked explicitly. This is not just an internal diagnostic difference.

The previously accepted native strict mapper is unaffected: it rejects all dead
units, NativeSnapshot sets immobilized exactly from !alive, and the strict source
liveMeleeChoice additionally requires every roster row's liveHealth>0. The new
mixed corpse admission must not be merged into that mapper without provenance.

Pinned rank initialization audit found no assignment in army constructor,
initClean or initialize; m_expectedMoveOrder is a plain field. Only findMoveOrder
writes it (ai.cpp599/620). Virtual native rank starts unknown, not0; it becomes
known only after a certified source-equivalent ordering event and is retained on
death. Retreat/caster simulateCombat also recomputes these ranks, so a future
history adapter must model those call boundaries explicitly or invalidate.
