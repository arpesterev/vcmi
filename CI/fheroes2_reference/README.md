# Original FHeroes2 battle reference

This Linux/GNU linker reference compiles unchanged public FHeroes2 source and
runs its real `AI::BattlePlanner::BattleTurn`, `Battle::Arena::Turns` scheduler,
and `Battle::Arena::ApplyAction`. It does not replace the game engine with a
translation of the planner. No proprietary game files are loaded or downloaded.

The upstream commit is `0dbfe9f386f33c2500842f4a59cd284d7f03d541` in
[ihhub/fheroes2](https://github.com/ihhub/fheroes2/tree/0dbfe9f386f33c2500842f4a59cd284d7f03d541).
`upstream.json` pins the official archive SHA-256 and all 965 Git blob hashes.
The upstream [GPL-2.0-or-later license](https://github.com/ihhub/fheroes2/blob/0dbfe9f386f33c2500842f4a59cd284d7f03d541/LICENSE)
applies to that source. Upstream source and binaries are not vendored here or
included in CI evidence uploads.

## Run

On Ubuntu 24.04, install `g++`, `gcc`, `pkg-config`, `libsdl2-dev`,
`libsdl2-mixer-dev`, and `zlib1g-dev` from the official Ubuntu repositories.
Python 3.10 or later is required.

```sh
python3 -m unittest discover -s CI/fheroes2_reference -p 'test_*.py' -v
python3 CI/fheroes2_reference/reference.py source \
  --repository . --work-dir out/fheroes2-reference --jobs 2
```

The work directory must be new or empty. `--archive` accepts an existing archive
but still requires the pinned SHA-256. Every run compiles all 249 upstream
translation units anew, excluding only the original application entry point.
It links the same objects twice: a baseline executable and an observed executable
with GNU `--wrap` at the planner and action boundaries. The fixture entry point
creates battle-only maps, armies and optional heroes through engine APIs.
It never edits upstream files or installs the complete game.

The corpus is eight scenarios at seeds 1, 42, 123456 and 4294967295:
melee, archers, mixed armies, flying units, wide units, damage spells, Hypnotize,
and human auto-combat with its source permission gate. Every case runs baseline,
observed and observed again, for 32 cases and 96 processes. The checker requires:

- Byte-identical final outcomes for all three processes, including battle RNG.
- Byte-identical repeated observed traces, with actual planner/action activity.
- Paired callbacks, sequential event indices and initial/final boundaries.
- No ambient RNG change during battle observation.
- Unchanged source blobs and original object hashes after execution.

RNG object bytes are native-endian. Comparisons are within the same run and
platform, not a promise of identical bytes across compilers or architectures.
The explicit 16-byte PCG layout and mangled wrapper names intentionally fail if
this pinned ABI is no longer supported. No compiler-object cache is used.
Dependencies come from official packages; their versions and compiler versions
are recorded, but package binaries are not globally pinned. This is source and
behavior reproducibility, not bit-identical build reproducibility.

For an isolated local investigation, `source --local` and `native --local`
record an unbound checkout without inventing a Git commit or tree. Native local
capture does not establish the relation between supplied source and binary;
retain their independently verified manifests alongside it.
An optional `--dependency-prefix` supports an already provisioned Linux x86-64
sysroot and records library hashes. CI uses neither option. Local unbound
source or native results cannot pass the cross-engine comparator. The default
CI path still requires a clean Git checkout; `compare` has no local override.

## Native pairing

The parallel `fheroes2-reference` job produces source-only evidence. The existing
Linux native build runs four tests in a fresh process after `build_targets` and
produces a separate artifact. It uses the native job's existing test-data setup;
this change does not add another game-asset download.

```sh
python3 CI/fheroes2_reference/reference.py native \
  --repository . --test-binary out/build/linux-gcc-test/bin/vcmitest \
  --work-dir out/fheroes2-native-reference
python3 CI/fheroes2_reference/reference.py compare \
  --repository . --source-evidence out/fheroes2-reference/evidence \
  --native-evidence out/fheroes2-native-reference \
  --output out/fheroes2-property-comparison.json
```

`properties.json` maps only four coarse properties:

- Damage spell selected: H2 Lightning Bolt versus H3 Magic Arrow.
- Hypnotize selected and effective control transferred by each engine.
- Spell permission gate respected, with different source and native gates.
- At least one engine-applied ranged attack.

Units, boards, damage rules, targets, spell mechanics and permission mechanisms
differ. These are not identical-input decision comparisons, numeric valuation
comparisons, RNG equivalence, battle-outcome equivalence, exhaustive policy
coverage, or proof that the FHeroes2 port is complete. HoMM3 policy qualification
is a separate task. The additional Dispel/buff/defensive/anti-magic source
fixtures are intentionally outside this initial bounded CI corpus.

The comparator requires the same clean checkout commit and tree, harness hashes,
upstream pin, all 32 source cases, all four completed native tests, reviewed test
body hashes, and a checked default FHeroes2 native backend. It rereads raw source
traces/outcomes and native XML instead of trusting success flags alone. Changed
native test bodies require a review of the mapping, not automatic hash updates.
The native binary and adjacent shared libraries are hashed before execution and
checked afterward. Same-job build logs provide the source-to-binary provenance;
a GTest XML file by itself cannot prove which source produced an old binary.
GitHub run identifiers are retained in native evidence.

## Failures and cost

Missing, skipped, failed, stale or mismatched evidence fails the comparison and
remains visible. An absent native artifact is not source parity. `always()`
artifact uploads retain stage status, compiler/link logs, partial runs and raw
traces for seven days, without game data, upstream binaries or object files.
GitHub cancellation or runner termination may prevent artifact upload.

The source job has a 20-minute bound, each compile/link a 180-second bound, each
battle a 30-second bound, and the native quartet a 120-second bound. The pairing
job is bounded at five minutes. Local measurements must not be presented as a
GitHub runner benchmark. The source job does not alter existing triggers,
permissions, native tests or trusted compiler-cache rules.
