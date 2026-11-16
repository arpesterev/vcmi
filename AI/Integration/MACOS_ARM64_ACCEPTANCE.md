# Four combat AI choices on Apple Silicon

This is an experimental delivery and acceptance procedure for an Apple Silicon
Mac, including MacBook Pro M4 Pro. It does not establish four complete upstream
ports or playing-strength equivalence.

## Scope and source

The reviewed integration base is
`830fdfeb844e30494fab613a370073c3cdff470c`. Its engine version is **1.8.0** in
`cmake_modules/VersionDefinition.cmake`. Record the final build commit after
applying any changes; a result from the base commit does not validate later edits.

The four selectable IDs are:

- `HoMM3OriginalTactics`: partial native HoMM3 tactical policy.
- `FHeroes2Tactics`: partial native fheroes2-derived tactical policy.
- `TUMEnchantedTactics`: documented TUM exceptions on a partial HoMM3 baseline.
- `MMAI`: strict neural-model route, with separate non-neural spell handling.

The reserved complete-port IDs `HoMM3Original`, `FHeroes2` and `TUMEnchanted`
remain unavailable. Do not rename the partial entries to imply full ports.

## Build and package

The workflow `.github/workflows/ai-macos-arm64.yml` supports manual dispatch
and pushes only to `feature/ai-macos-arm64-package-*` branches. It does not run
on pushes to `develop` or ordinary integration branches. After reviewing and
committing a verified candidate, use manual dispatch or push one explicitly
chosen package branch to that commit. No artifact exists until a run succeeds.
It builds the exact event commit, checks `uname -m = arm64`, and uses the
existing `macos-arm-conan-ninja-release` preset and `macos-arm` Conan profile.
The profile targets macOS 11.0 and Conan `armv8`. The dependency recipe requests
ONNX Runtime **1.18.1**. Native operation on a particular macOS version still
requires the runtime checks below.

The build explicitly enables `ENABLE_BATTLE_AI`, `ENABLE_MMAI`,
`ENABLE_STUPID_AI`, `ENABLE_NULLKILLER2_AI`, `ENABLE_CLIENT`, `ENABLE_SERVER`
and `ENABLE_LAUNCHER`, with `CMAKE_OSX_ARCHITECTURES=arm64`. The three partial
policies belong to the BattleAI target; there are no separate CMake flags for
those policies. Tests and neural weights are not packaged.

Dependencies come from the existing official `vcmi/vcmi-dependencies` release
`2026-10-13`, asset `dependencies-mac-arm.txz` (ID `636544830`), verified against
SHA-256 `6ba8215909edffc79b9c55bdfa7638130d0f34de6db14388ac556ded9f9f0126`.
Conan uses `--build=never`; a missing binary dependency fails the build instead
of silently changing the dependency toolchain.

ARM64 packaging requires Python 3.9 or later with its standard library. After
Qt is deployed, `osx/normalize_bundle_rpaths.py` removes non-system absolute
`LC_RPATH` entries from staged app images only. Relative and system search
paths, dylib dependencies and install IDs are retained. External/broken symlinks
and hard-linked images fail cleanup; the source Conan cache is never edited.
Cleanup runs only when `CMAKE_SYSTEM_PROCESSOR=arm64`, where the existing
signing phase re-signs the images. Intel builds do not run cleanup. Every image
needing cleanup must contain exactly one ARM64 slice; a universal image with
non-system absolute rpaths fails before any image is changed. The helper does
not assume that an rpath exists in every slice of a universal image. Cleanup
and signing errors stop packaging, and `codesign --verify --deep --strict`
must pass on the staged ARM64 app.

The mounted DMG is checked with `hdiutil verify`, `lipo`, `otool` and
`codesign --verify --strict` for each image, followed by a separate
`codesign --verify --deep --strict` on the complete app. Every Mach-O image needs
an ARM64 slice and a valid signature. Non-system dependencies must resolve inside the app, including Qt
plugins and any shared ONNX runtime; Homebrew, Conan-cache and runner-only
paths are rejected. Enabled build flags and four backend identity strings in
`libvcmi` are additional checks, not proof of native gameplay.

The ARM64 packaging step applies **ad-hoc signatures** without a Developer ID
certificate. Signing the main launcher was observed to sign its app bundle in
the native build; the explicit staged and mounted app checks now require valid
bundle integrity instead of inferring it from that message. This is not proof
of Developer ID signing, Apple notarization or Gatekeeper acceptance. The
workflow does not import signing credentials or submit anything to Apple. If the existing Platform
Builds workflow is used instead, leave `notarize_macos=false` unless separately
authorized signing and notarization credentials are appropriate.

Do not disable XProtect or Gatekeeper, strip quarantine, or bypass signature
failures. Use normal macOS installation and consent UI. A macOS security block
is an explicit delivery blocker until the user resolves it through the normal
UI or a properly signed/notarized build is supplied.

## Packaged server execution smoke

After the static mounted-bundle audit, CI runs `CI/smoke_macos_ai_bundle.py`
against that same read-only mounted app. It verifies the app seal again and
executes `vcmiserver --help` and `vcmiserver --version` through
`/usr/bin/arch -arm64`, each with a 30-second timeout. The server source handles
these options before game-library initialization, so this check needs no game
assets and does not start a listening game server.

Each smoke run uses a fresh temporary HOME and a minimal environment without
inherited `DYLD_*` overrides. The version output must match the exact source
commit and report all user data/config/log/save/cache paths under that HOME.
The temporary data is removed afterward. `server-smoke.json` records the
commands, exit codes and outputs alongside the static audit. A successful
static audit alone does not satisfy this execution check.

The launcher has no `--help` or `--version` handler at the reviewed source
`bbc23d095500a9a05fe5d6f33df37fd9fddfd645`: `launcher/main.cpp` constructs
`QApplication`, prepares user directories, shows `MainWindow`, then runs the
event loop. Do not execute guessed CLI options or call a timed-out GUI a passed
launcher test. Launcher startup, four-choice persistence, neural inference and
actual gameplay still require separate native acceptance.

## Native build-tree AI regression gate

The extended workflow preserves the audited DMG and server-smoke artifact
before enabling tests in the same build directory. It then configures
`ENABLE_TEST=ON`, `ENABLE_MMAI_TEST=OFF` and `INSTALL_GTEST=OFF`, and builds only
`vcmitest` with three compile jobs. Core/server/MMAI objects are reused. Do not
rerun Conan, change build type, or run CPack again after enabling tests. Test
binaries and fixture data must not enter the application package.

The post-link GoogleTest discovery runs with a temporary HOME. The subsequent
`CI/run_macos_native_ai.py` runner creates another fresh HOME for actual tests,
forces ARM64 execution, verifies the test executable's architecture/signature,
and uses a minimal environment without inherited `DYLD_*`, GoogleTest controls
or neural-model gates. The build-tree resources and Conan dependency paths are
intentionally different from a portable app: do not apply the bundle rpath
normalizer to this executable or change the dependency cache. A loader or
signature failure is a test blocker, not a reason to bypass macOS protection.

The reviewed next-candidate inventory contains 358 `AIIntegrationNativeBattle`
cases, 8 `HoMM3ForecastQueueNative` cases and 10
`HoMM3ForecastLifecycleNative` cases. The runner enumerates the compiled
binary, requires exactly the 20 named model-gated cases, excludes only those
cases, then verifies that the selected inventory and XML identities agree.
Linux CI validates the compiled inventory against the Mac workflow counts
before executing its suites. Require that check before scheduling a new Mac
build to catch stale counts without repeating the longer compilation.
Expected genuine passes are 338 native battle cases, 18 forecast/lifecycle cases,
48 `MMAINNOutputValidation` cases and 145 Nullkiller/resource-trading cases:
**549 executed cases with zero skipped, disabled, failed or errored cases**.
These are required results, not a claim that this Mac run has already passed.
A source change that alters the inventory requires an explicit reviewed update
to the expected counts. XML counts alone cannot substitute for matching names.

The fixture is the existing Linux CI archive from
[vcmi-test-data v2.0](https://github.com/vcmi-mods/vcmi-test-data/releases/tag/v2.0),
asset `345320653`, `heroes3.7z`, 147,594 bytes, SHA-256
`64247aeee0342c6f349576ba700a81e2de46de7b9aa19ca4881b8d59f5e0ced8`.
Its README says "for VCMI Github Actions internal use only" and the repository
has no license grant for redistribution. Use it solely for authorized internal
VCMI CI. Keep it in temporary storage, never in the DMG or uploaded artifacts.
Public availability does not grant broader redistribution rights. The runner
verifies the archive, rejects unsafe paths/links, and checks the extracted
10,478-file, 959,915-byte layout before using it under
`HOME/Library/Application Support/vcmi`. It writes a temporary test-only preset
for `vcmi` and `vcmi-test`, leaving ordinary user presets untouched.

Only explicit JSON/XML/log reports are uploaded as a separate native-test
artifact. A verified application artifact can therefore exist even if this
later gate fails; inspect both results before describing runtime readiness.
The compile estimate is 15–30 additional minutes for roughly 172 translation
units, plus 2–5 minutes of execution, unmeasured on macOS. Test timeouts are
300 seconds for native/forecast, 90 for output contracts and 900 for Nullkiller.
Real neural-model inference is excluded from this gate and remains a separate
acceptance requirement. Neither build-tree passes nor server CLI startup prove
launcher selection persistence or packaged gameplay.

## Install the official MMAI mod

Install and enable MMAI through the launcher's existing mod manager only when
the user chooses to do so. Keep their existing game data, saves, settings and
mod presets. Do not download or redistribute Heroes III game data. The user
must provide their own lawfully obtained files through the normal launcher.

The engine's default mod catalog is
[VCMI 1.8](https://raw.githubusercontent.com/vcmi/vcmi-mods-repository/develop/vcmi-1.8.json).
Catalog blob `7a74fd86de8a14d8b28b4a5cda0a83bdc8f0e805` maps `mmai` to the
[official VCMI 1.8 MMAI release](https://github.com/vcmi-mods/mmai/releases/tag/vcmi-1.8-latest).
The reviewed release is:

- Release ID `426109309`, archive asset ID `683229114`.
- Source commit `c4ea1b3834f72cf7ffd141d80d5a195d318487ce`.
- Mod version **3.1.1**, engine compatibility **1.8.0 through 1.8.9**.
- Archive `mod.zip`, **50,241,590 bytes**.
- SHA-256 `25c997888015386c59c74a426cd0368b5035048dd61b059854d5b7689acdb7d4`.
- Manifest declares Apache License 2.0. Keep upstream license information;
  this procedure uses the official distribution rather than repacking weights.

`vcmi-1.8-latest` is a mutable release label. If its bytes, version, models or
compatibility change, stop and revalidate the new release. Do not rewrite the
manifest compatibility to make it install on an unsupported engine.

Search for **MMAI** in Mods, check the displayed version and compatibility,
choose Install, then Enable it in the intended preset. Refresh the official
catalog if it is absent. Do not replace a user's custom repository settings or
existing mod installation silently. Source ZIPs from GitHub are insufficient:
the repository's model settings are empty until the official release builder
downloads the models.

The default macOS locations are:

- Mod: `~/Library/Application Support/vcmi/Mods/mmai/`.
- Mod manifest: `mod.json` inside that directory.
- Model assignments: `config/mmai-settings.json` inside that directory.
- Weights: `models/` inside that directory.
- Launcher settings: `~/Library/Application Support/vcmi/config/settings.json`.
- Mod presets: `~/Library/Application Support/vcmi/config/modSettings.json`.
- Logs: `~/Library/Logs/vcmi/`.

The exact archive contains these configured files, with no filename rewrite:

- Attacker: `attacker-pdpspars-202610132318.onnx`, model version 15, side 0,
  SHA-256 `03635baa1cf532583fcf607bd2f56f0ae9c91f7dd9e843c9f4fae75e81d21bab`.
- Defender: `defender-zvyspars-202610152155.onnx`, model version 15, side 1,
  SHA-256 `0cc60dd40ad24965f7ec3180050e8edb9be40d8bd63308ec13dd02e29fddac0a`.
- Siege defender: `defender-zvystown-202610140025.onnx`, model version 15, side 1,
  SHA-256 `f59219a9a97e36c8fd3a51ad736ddef6ea79744132bed6544e3be880af0f3a89`.

Each model is 19,255,062 bytes. The checked release settings use temperature 0,
seed 0 and `useCreatureSpells=true`. Its legacy `fallback: BattleAI` field does
not permit substitution when `MMAI` is selected through the strict factory.
Creature/hero spell delegation must not be reported as neural spell inference.

The optional read-only verifier compares every installed file to that release.
It installs nothing and does not change settings:

```sh
python3 verify_mmai_1_8.py \
  --installed-mod "$HOME/Library/Application Support/vcmi/Mods/mmai" \
  --engine-version 1.8.0
```

The script and `mmai_1_8_release.json` must be in the same directory. In a source
checkout the script is `CI/verify_mmai_1_8.py`. To check a separately downloaded
official ZIP without extracting it, use `--archive /path/to/mod.zip` instead.
Custom model settings will fail exact-byte verification; preserve them and test
an unmodified official baseline separately rather than overwriting them.

## Native Mac acceptance checklist

Record the DMG SHA-256, exact source commit, macOS version, hardware, model
hashes and result of each check. Do not mark checks passed before execution.

1. Verify `SHA256SUMS.txt`, mount the DMG normally and copy the app to the user's
   chosen installation location without replacing their existing build silently.
   Start the installed copy, not the application inside the build tree.
2. Confirm the launcher and client are Apple processes in Activity Monitor,
   not Intel/Rosetta. A binary with an ARM64 slice alone does not prove this.
3. In Settings, test all four IDs in each combat role independently: allied
   auto-combat, neutral and enemy. The three partial policies must retain their
   experimental labels; MMAI must retain its model requirement warning.
4. Close and restart the launcher after each selection. Check that the selected
   role remains unchanged and the stored `ai.combatAlliedAI`,
   `ai.combatNeutralAI` or `ai.combatEnemyAI` has the expected ID, not a display
   label. Repeat with all three roles set to different choices. Refresh and
   reopen Settings without silently selecting a different backend.
5. With the intended MMAI mod preset enabled, launch real battles for every
   role. Cover attacker and defender, melee, ranged, spells and siege. Capture
   the selected backend, model-load evidence, accepted engine actions, outcome
   and any unsupported mechanics. Different labels alone are not four policies.
6. Save and reload a game, restart the application, and repeat a battle. Verify
   the selected IDs and active mod preset survive, without damaging old saves.
7. In a disposable acceptance preset, disable MMAI and select it for each
   role. Missing weights must produce an explicit failure, never BattleAI or
   StupidAI substitution. Restore the preset before ordinary play. Test damaged
   or wrong-side weights only in an isolated test data directory, not by editing
   the user's installed models.
8. Human quick-combat failure should return to manual combat with an error.
   Neutral/enemy failure recovery still needs direct runtime verification;
   source-level strict exceptions do not prove a usable GUI recovery path.
9. Exercise surrender/retreat, tactics placement, creature spells and war
   machines. Engine-owned machine actions and BattleAI spell delegation are not
   neural decisions. Record crashes, hangs and rejected actions as failures.

## Remaining limits

- Native ARM64 run [42382614048](https://github.com/arpesterev/vcmi/actions/runs/42382614048)
  at `ecd48d9296d3647685168ac8c03373e5d5479cbc` passed packaging and the mounted
  audit for 35 ARM64 Mach-O images, including the deep/strict app seal. It
  uploaded an artifact. This proves static package integrity at that commit,
  not execution, gameplay or validation of a later source commit.
- The subsequent DA37 run
  [42391258270](https://github.com/arpesterev/vcmi/actions/runs/42391258270)
  passed the mounted audit and actual ARM64 packaged-server `--help` and
  `--version`, with the exact source SHA and isolated HOME. It produced a DMG
  with SHA-256 `11e561522b8cb8e76867f7c2b76762c24e680d0c8f0f6d681f22f2f27e57e5b3`.
  Native test compilation then failed a Clang warning in a test-only lifecycle
  receiver. Its 522 runtime cases did not run. The receiver is now final,
  without warning suppression; the current expanded 549-case gate still needs
  a new exact-commit Mac run.
- No installed launcher, Apple Silicon inference or gameplay test has been
  executed by this delivery preparation.
- Portable cleanup/audit tests use synthetic Mach-O tool responses. The CMake
  sequence tests execute install scripts with fake signing tools. These tests
  check ordering and error propagation, not native signature restoration or dyld.
- Official model archive hashes, manifests, configured filenames and model
  metadata were inspected; no inference was executed by the byte verifier.
- Existing Linux neural tests are separate evidence and do not establish Mac
  runtime behavior. Legacy V13 work is outside this acceptance scope.
- Three complete upstream semantic ports and full policy-equivalence evidence
  remain unfinished. A package passing this checklist is still experimental.
