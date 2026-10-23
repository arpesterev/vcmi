# Modular builds and strict incremental CI

This branch preserves VCMI's public artifacts and game behavior. It introduces
22 internal OBJECT libraries, rather than 22 new dynamic plugin interfaces.
All their objects are included in the original libraries, preserving static
registration and avoiding archive ordering problems. The installed client,
server, engine, and AI plugin names remain unchanged.

## Compilation structure

- `vcmi`: filesystem, JSON, platform support, battle/bonuses, callbacks, world,
  maps/pathfinding/RMG, content, serialization/network packets, spells.
- `vcmiclientcommon`: rendering, media, input/GUI, widgets, windows,
  adventure-map views, battle UI, lobby/menu, network lag handling.
- `vcmiservercommon`: battles, queries, processors.
- Root/platform glue stays in the original owning library.
- `BattleAI`, `Nullkiller2`, and the other existing AI libraries remain separate.

These are real build targets with separately compiled object sets, not isolated
public APIs. Shared headers still cause every dependent translation unit to
rebuild. Merely splitting target names does not make unchanged sources compile
faster: retaining the verified Ninja state and compiler cache is essential.

## Explicit CI modes

The new workflow runs only in `arpesterev/vcmi`, on
`build-incremental-1.7.5`, with an explicit commit-message prefix:

- `[vcmi-bootstrap] description`: an explicit cold build after toolchain changes,
  first setup, or cache eviction. It discards the restored build tree, while
  retaining compatible dependency downloads and ccache entries.
- `[vcmi-warm] description`: requires both compatible dependency and build-state
  caches. Missing, incompatible, corrupt, or unsafe state fails the job. It
  never silently falls back to an expensive cold compile.
- `[vcmi-probes] description`: warm mode plus reversible, bounded Mac dependency
  probes for client/AI source and private/shared headers. Sources are restored
  and a final no-op is required before snapshotting or packaging.

Ordinary commits are source-only and do not launch expensive builds. These
prefixes authorize a specific bounded run; they are not estimates of duration.
The legacy full AI research qualification remains on its existing shipping
branch. The incremental workflow runs every enabled Linux regression and the
required regression manifest, but does not rerun performance A/B experiments or
mutation experiments for each UI edit.

## What is cached

1. Immutable dependency snapshots, separately from changing build outputs.
   Mac contains only Conan package storage, generated Conan configuration and a
   dedicated tool virtual environment, not the whole home directory or auth
   configuration. Linux contains package downloads and the pinned test archive.
2. Ninja build tree, its `.ninja_log` and `.ninja_deps`, and a verified manifest.
3. A bounded ccache directory, with compiler-content checks and no sloppiness.

Every changed build gets a new immutable key. Restore searches only within its
exact compatibility prefix. A prefix match is usable after verification even
though the Actions `cache-hit` output is false. A no-op does not upload another
identical build generation. Dependency keys do not change with an edit to the
build implementation scripts. Caches remain subject to GitHub quota and eviction;
this is why warm mode has an explicit failure, not a claim of permanent storage.
No workflow deletes unrelated repository caches or changes billing limits.
Mac cache extraction preserves the archived file modes with GNU tar
`--same-permissions`; umask must not turn 0664 into 0644 and invalidate the
manifest. Validation still checks every file mode, hash, and symlink target.

Compatibility includes runner image, OS/architecture, absolute workspace,
compiler, SDK, build options, presets, dependency revision and setup inputs.
Installed toolchain/dependency identity is verified before reusing outputs.
Source mtime restoration is allowed only after content and mode comparison.
Changed sources receive a filesystem-derived timestamp newer than outputs;
removed/new paths remain visible to CMake/Ninja. No command sets all source
files to an old date or restores objects without validating their provenance.

## Evidence, not assumptions

`incremental-evidence` records compatibility identities, restored cache key,
cache verification time, configuration/build times, ccache statistics,
Ninja before/after/no-op logs, executed object counts grouped by target, and
regression results. Object edges may be ccache hits; actual compiler misses are
reported separately. Changed code is expected to have legitimate misses.

An immediate second build must produce no compiler/archive/link edge. This is
checked structurally from the Ninja log as well as its diagnostics. The Mac
package is freshly assembled, ad-hoc signed and verified after extraction; it
is not Apple-notarized and is not a proof of successful gameplay.

## Acceptance procedure

Before reporting an improvement, record both cold and warm GitHub runs for the
same build contract, then test a client-only source edit, an AI-only edit, and
a shared-header edit. Compare actual affected object sets, compile misses,
transfer/dependency-install/configure/build/test/package time separately.
A shared-header change must rebuild all consumers, not just the edited module.
The opt-in probes select a shared header with at most 32 compiled consumers;
this does not validate every header or claim that a global-header edit is cheap.
Generated Qt and external translation units are included in consumer counts.
Each probe reports its exact source path and expected/actual object outputs.
Fixture tests cover this dependency contract, including simulated fresh checkout
and archive timestamp truncation; they do not replace actual VCMI platform runs.

Local structural checks:

```sh
python3 -m unittest discover -s CI -p 'test_incremental*.py' -v
python3 cmake_modules/tests/test_source_modules.py --cmake cmake --ninja ninja
bash -n CI/incremental-build.sh
```

No timings or warm-cache hit rates are claimed until actual final-commit runs
have produced those artifacts.
