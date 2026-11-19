# Local Apple Silicon build from a source checkpoint

This is a source-reviewed, **not Mac-executed** build recipe for the current
experimental ports. It does not certify installation, GUI gameplay, or bitwise
reproducibility. No GitHub publication is needed. Nothing below changes macOS
security settings, installs system packages, or supplies Heroes III game data.

Prerequisites already installed: native ARM64 macOS, Apple command-line compiler
and SDK (`xcrun --find clang`), Git, Python 3, CMake, Ninja, and Conan 2.25.x.
If any are missing, stop and arrange installation separately. Use your legally
obtained Heroes III data only after building; they are not in this checkpoint.
The pinned dependency profile selects macOS11/armv8 and Apple-Clang16 package ABI.
A missing/incompatible binary dependency is a stop condition, not permission to
change the profile or compile arbitrary replacement dependencies.

## 1. Verify and prepare a fresh source copy

Verify the downloaded checkpoint SHA-256 against its accompanying receipt, then
extract it normally. Work in its `a38_candidate/source` directory. The folder name
is historical; use the latest `README-A*.md` to identify the checkpoint. Do not run
this recipe in an existing personal Git checkout: use a fresh archive copy.

The Linux checkpoints include googletest but omit three GUI/Conan submodules.
These exact gitlinks were independently read from base commit
`aafa3de7cd5d93dbec49dda1702ebc5c699e0827` in `arpesterev/vcmi`; they are not floating
branches. None of the three contains nested submodules.

```sh
set -eu
test "$(uname -s)" = Darwin
test "$(uname -m)" = arm64
xcrun --find clang
for tool in git python3 cmake ninja conan; do command -v "$tool"; done
conan --version
# Confirm Conan is 2.25.x before continuing.
test ! -e .git
fetch_pin() {
  target="$1"; url="$2"; pin="$3"
  test ! -e "$target" || { echo "Use a fresh source copy: $target already exists"; return 1; }
  git clone --no-checkout "$url" "$target"
  git -C "$target" checkout --detach "$pin"
  test "$(git -C "$target" rev-parse HEAD)" = "$pin"
}
fetch_pin dependencies https://github.com/vcmi/vcmi-dependencies.git 68f3f22614a3b17c80aa603b0c013181db845048
fetch_pin launcher/lib/innoextract https://github.com/vcmi/innoextract.git 96e9566a35fb51ebf13ffbdadfd49a93c1ae5c1a
fetch_pin client/lib/discord-presence https://github.com/vcmi/discord-presence.git 52501e140db1c18d9da37dff9a9529bc4a9943c2

# Local-only metadata: the macOS bundle needs a real source revision.
# This commit is a new local snapshot, NOT the upstream/base commit.
git init
git add .
git -c user.name='Local source snapshot' -c user.email='local-build@invalid' commit -m 'Verified local VCMI source checkpoint'
SNAPSHOT=$(git rev-parse HEAD)
```

Do not use `git submodule update` on this reconstructed snapshot: googletest is
already vendored, and the three missing inputs above were pinned explicitly.
No remote is added to the root checkout and nothing is pushed.

## 2. Restore pinned binaries into an isolated cache and build

Continue in the same shell and source directory. Cache and downloaded archive
live beside the source, not in `~/.conan2`. The commands mirror the checked-in
`.github/workflows/ai-macos-arm64.yml`; `--build=never` is intentional.

```sh
WORK="$(pwd)/../local-mac-work"
mkdir -p "$WORK"
export CONAN_HOME="$WORK/conan-home"
SNAPSHOT_FILE="$WORK/dependencies-mac-arm.txz"
curl --fail --location --proto '=https' --retry 3 \
  https://github.com/vcmi/vcmi-dependencies/releases/download/2026-10-13/dependencies-mac-arm.txz \
  --output "$SNAPSHOT_FILE"
printf '%s  %s\n' 6ba8215909edffc79b9c55bdfa7638130d0f34de6db14388ac556ded9f9f0126 "$SNAPSHOT_FILE" | shasum -a 256 -c -
conan cache restore "$SNAPSHOT_FILE"
conan profile detect
conan install . --output-folder=conan-generated --build=never \
  --profile=dependencies/conan_profiles/macos-arm \
  --profile=dependencies/conan_profiles/base/apple-system \
  --conf=tools.cmake.cmaketoolchain:generator=Ninja \
  -o '&:with_onnxruntime=True'
export VCMI_ENABLE_LTO=OFF VCMI_PACKAGE_GOLDMASTER=OFF
export VCMI_PACKAGE_NAME_SUFFIX='Local experimental arm64'
export VCMI_PACKAGE_FILE_NAME="vcmi-local-arm64-$SNAPSHOT"
cmake --preset macos-arm-conan-ninja-release \
  -DCMAKE_OSX_ARCHITECTURES:STRING=arm64 \
  -DENABLE_CLIENT:BOOL=ON -DENABLE_SERVER:BOOL=ON \
  -DENABLE_LAUNCHER:BOOL=ON -DENABLE_BATTLE_AI:BOOL=ON \
  -DENABLE_MMAI:BOOL=ON -DENABLE_STUPID_AI:BOOL=ON \
  -DENABLE_NULLKILLER2_AI:BOOL=ON -DENABLE_GITVERSION:BOOL=ON \
  -DENABLE_TEST:BOOL=OFF -DENABLE_CCACHE:BOOL=OFF
cmake --build --preset macos-arm-conan-ninja-release --parallel 3
(cd out/build/macos-arm-conan-ninja-release && cpack -C Release)
```

The output should be
`out/build/macos-arm-conan-ninja-release/vcmi-local-arm64-<local-snapshot>.dmg`.
It is an experimental ad-hoc-signed build, not Apple-notarized software. Do not
remove quarantine, disable Gatekeeper/XProtect, or ignore a failed signature.

## 3. Verify the actual package before using it

```sh
BUILD="$(pwd)/out/build/macos-arm-conan-ninja-release"
MOUNT="$WORK/mounted-dmg"
mkdir -p "$MOUNT"
hdiutil verify "$BUILD/$VCMI_PACKAGE_FILE_NAME.dmg"
hdiutil attach -readonly -nobrowse -mountpoint "$MOUNT" "$BUILD/$VCMI_PACKAGE_FILE_NAME.dmg"
python3 CI/check_macos_ai_bundle.py --app "$MOUNT/VCMI.app" \
  --build-cache "$BUILD/CMakeCache.txt" --expected-sha "$SNAPSHOT" \
  --output "$WORK/bundle-audit.json"
python3 CI/smoke_macos_ai_bundle.py --app "$MOUNT/VCMI.app" \
  --expected-sha "$SNAPSHOT" --output "$WORK/server-smoke.json"
hdiutil detach "$MOUNT"
```

Stop on any failed command. A successful build/package/server smoke is still not
GUI acceptance. Install through normal macOS UI, select `FHeroes2Tactics` or
`HoMM3OriginalTactics` explicitly, check persistence after restart, then run the
save/load, mixed-army, siege and mission-completion checks in
`Ports/PLAY_READINESS.md`. MMAI weights are a separate requirement; no weights or
scripted substitute are supplied here. The reserved complete-port names remain
unavailable.
