#!/usr/bin/env bash

# Isolate options and cleanup traps when sourced by linux_qt5.sh/linux_qt6.sh.
(
set -euo pipefail

ARCH=$(uname -m)
case "$ARCH" in
	x86_64) RID=linux-x64 ;;
	aarch64) RID=linux-arm64 ;;
	*) echo "ONNX Runtime: unsupported architecture: $ARCH" >&2; exit 1 ;;
esac

# Microsoft's native NuGet package also contains the C/C++ headers.
# Keep the runtime version used by MMAI; GitHub release downloads can return 404.
VERSION=1.18.1
URL="https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/$VERSION/microsoft.ml.onnxruntime.$VERSION.nupkg"
SHA256=e1fdeb5359407c948569905b4eb76af50b8bdb38b2bd3f2ad6516fe4a7186d6a
ROOT=/opt/onnxruntime
for tool in curl sha256sum python3 sudo; do
	command -v "$tool" >/dev/null || { echo "ONNX Runtime: required tool missing: $tool" >&2; exit 1; }
done

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
printf 'Downloading Microsoft ONNX Runtime %s (%s) from %s\n' "$VERSION" "$RID" "$URL"
if ! curl --proto '=https' --proto-redir '=https' -fsSL --retry 3 --connect-timeout 30 --max-time 300 \
	"$URL" -o "$WORK/runtime.nupkg"; then
	echo "ONNX Runtime: download failed: $URL ($RID); installation unchanged" >&2
	exit 1
fi
if ! printf '%s  %s\n' "$SHA256" "$WORK/runtime.nupkg" | sha256sum --check --status; then
	echo "ONNX Runtime: SHA-256 mismatch for $URL; installation unchanged" >&2
	exit 1
fi

# Extract only native files, without needing NuGet/.NET or trusting archive paths.
python3 - "$WORK" "$RID" "$VERSION" <<'PY'
from pathlib import Path
import sys
import zipfile

work, rid, version = sys.argv[1:]
stage = Path(work) / "stage"
(stage / "include").mkdir(parents=True)
(stage / "lib").mkdir()
with zipfile.ZipFile(Path(work) / "runtime.nupkg") as package:
    for name in package.namelist():
        if name.startswith("build/native/include/") and name.endswith(".h"):
            (stage / "include" / Path(name).name).write_bytes(package.read(name))
    (stage / "lib" / f"libonnxruntime.so.{version}").write_bytes(
        package.read(f"runtimes/{rid}/native/libonnxruntime.so"))
    for name in ("LICENSE", "ThirdPartyNotices.txt"):
        (stage / name).write_bytes(package.read(name))
(stage / "lib" / "libonnxruntime.so").symlink_to(f"libonnxruntime.so.{version}")
for name in ("onnxruntime_c_api.h", "onnxruntime_cxx_api.h", "onnxruntime_cxx_inline.h"):
    if not (stage / "include" / name).is_file():
        sys.exit(f"ONNX Runtime: missing required header: {name}")
PY
sudo mkdir -p "$ROOT"
sudo cp -a "$WORK/stage/." "$ROOT/"
printf 'Installed verified ONNX Runtime %s (%s) into %s\n' "$VERSION" "$RID" "$ROOT"
)
