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

- Native ARM64 run [42378843774](https://github.com/arpesterev/vcmi/actions/runs/42378843774)
  at `18eecbddc10c41eeea82f55f79f89d41420d3261` built the app and DMG, but failed
  the mounted audit on a Conan-cache `LC_RPATH` in `libQt5Widgets.5.dylib`. It
  uploaded no artifact. The cleanup repair requires a new successful native run.
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
