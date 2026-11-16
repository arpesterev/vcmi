# Invariant source-comparator ties

This extends the strict a30/a31 ordinary source model without choosing an
arbitrary std::sort order. The source comparator is `(key descending, original
roster index ascending)` and intentionally ignores side. A cross-side tie may
therefore have two comparator-valid orders. The subsequent original side swaps
can erase that difference or change identities/ranks. Neither native unit IDs
nor the current host's library tie behavior are substituted as source truth.

## Proof and explicit budget

Process each equal-key block in descending source-key order. Original side swaps
never cross a key boundary, so the entering desired side and final identity/rank
sequence fully determine the next block's state. Within a validated source
roster, each equal-key/index class has at most two members, one per owning side.

If every index class in a block is paired, the current desired member is always
inside the current two-element pair. The original algorithm either emits that
member or swaps only these two cells. It then emits the other side. After the
pair, desired side is restored; induction proves all 2^N input orientations
produce exactly the same identity sequence. This fast proof covers complete
mirrored 21-slot source rosters without enumerating 2^21 cases.

For a mixed singleton/pair block, enumerate every two-element tie orientation,
run the unchanged production transcription of original side swaps, and accept
only if every complete ordered identity/rank vector equals the canonical vector.
The explicit default limit is 4096 orientations per key block. Native seven-slot
rosters require at most 128. A larger mixed block above that proof budget remains
unavailable; this is recorded as unproven, not treated as source equality.

Counterexample retained: equal-key `A0 A1 A2 B2` and `A0 A1 B2 A2`, entering A,
yield `A0 B2 A2 A1` and `A0 B2 A1 A2`. A blanket removal of the tie guard is wrong.
A 1v1 mirrored battle and wholly paired blocks are provably invariant and can
now be admitted. `containsComparatorTies` remains true, preserving provenance.

The only ground production change replaces projectMoveOrder plus blanket tie
rejection with projectInvariantMoveOrder and its availability check. Existing
shooter/forecast callers outside this ground model are not changed.

## Independent evidence

PinnedInvariantOrder.h compiles exact upstream getMoveOrder, comparator and
findMoveOrder bodies from sushi-shi/homm3-decomp at
`e12906accf96c2662b1abadbc49ddc111991bf21`; source-manifest.json contains hashes.
The only instrumentation injects a caller-supplied comparator-valid permutation
after the original std::sort. Original key, swap and rank statements are intact.
PinnedGroundPermutation.h applies the same injection to the independently pinned
full ordinary forecast/live chooser oracle. No production ordering function is
called by either reference.

`generate_oracle.py /path/to/upstream/ai.cpp` reproduces these fixtures using the
adjacent reviewed ground/two-pass reference bodies.

The order test exhausts every subset of both seven-slot equal-key rosters, both
entering sides and both phases, and every comparator tie orientation; additional
random multi-key and source-flag inputs test block composition. It preserves the
mixed counterexample and the explicit budget rejection. Full two-prefix and
actual-active live tests compare all paired orientations for 1..7 slots, both
acting/requested sides and phases, including shared ledgers, full danger maps,
values, action/target/hex and draw order.

Native strict snapshot/test IDs are not silently changed by this delta. Migration
must preserve the former mirrored-tie test identity and explicitly change its
expectation to accepted original-permutation-equivalent native action, retaining
an unproven/non-invariant mixed-tie rejection test and before/after evidence.
