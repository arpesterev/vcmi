# Native mixed bridge: bounded current-input decision adapter

The native adapter consumes the exact mixed source leaf on validated public
current effective facts and submits only a currently legal VCMI command. It is
not a certificate of future execution equivalence for arbitrary mods or latent
custom procs. Existing shooter/melee/legacy fallbacks remain available.

Fresh round-zero, uninterrupted ordinary ground-map history initializes virtual
source facing to `1-side`. It does not inspect client sprite orientation. Only a
completed ordinary MOVE/MELEE/SHOOT/WAIT/DEFEND, including announced retaliation
and damage, restores survivor facing. Death retains source roster identity/rank
and clears dead facing. Corpse rows are not compacted. Unknown unit/spell/effect,
replacement, control, resurrection, unpaired damage, interrupted action, siege
or map events invalidate the corresponding bridge. An ordinary WALK is also
_tactics-capable_ in VCMI: actual tactic distance, not `isTacticsAction()`, selects
the tactics boundary.

Ranks begin unknown. A certified live melee chooser executes original order
projection and records eligible ranks. A shooter does not write them. Completed
own staged physical actions preserve that history across death and round change.
Unstaged/manual/opponent actions conservatively clear living rank knowledge;
known corpse ranks remain. Thus an enemy/manual kill may leave its new corpse
rank unknown and reject subsequent melee. Arbitrary ordinary deaths are NOT
claimed supported. Shooter-first kills without a previous certified writer also
leave unknown ranks and reject melee. This is deliberate, not guessed rank zero.

Own no-book/non-casting heroes retain the accepted instantaneous domain. Before
own hero planning, optional source retreat/spell valuation may have written order:
living ranks are invalidated, existing corpses retained. Source melee rebuilds
eligible ranks; source shooting does not. Opposing heroes remain unsupported
without querying their full private spell state. Current mana zero is not used
as a proof. Current defended snapshots and broader special-creature/effect domains
remain mapper fallbacks even when their history boundary is understood.

## Focused native tests

`HoMM3MixedNativeTest.cpp` has eight tests, run at O3 against accepted a33 native
production objects. Real server packet forwarding follows client ApplyFirst
attack/damage and movement, ApplyAfter EndAction. Tests cover:

- Four actual-controller mixed actions including SHOOT, distinguishable from
  legacy fallback, followed through legal native execution.
- Source order writer, own shooter kill, exact retained corpse rank, new round,
  real controller continued melee with retained corpse row.
- Shooter-first kill rejects unknown corpse rank and preserves candidate RNG.
- Blocked native archer takes melee with intrinsic shooter penalty, native legal
  execution including retaliation, and pinned source agreement.
- Unstaged manual WAIT does not manufacture source writer/rank knowledge.
- Wrong actor/map rejection preserves candidate RNG; unpaired damage and
  interrupted action invalidate; unknown spell/unit callback falls back.

Direct native creature stats, public health/positions/ammunition/retaliation,
validated history facing/ranks feed a separately namespaced reconstructed-source
`doCompAI` oracle. Source algorithm bodies are unchanged. Its host fixture adds
optional independent public native wall/distance query callbacks (synthetic
standalone defaults remain unchanged). It does not reverse-engineer original
stats from the adapter's reduced pair facts. Compared outputs include actual
actor, order, target, destination/attack hex, consumed random count and melee
value/danger set. This is scalar source parity, not retail executable parity.

Final combined-build regression and independent review are separate acceptance
steps, not implied by these focused tests.
