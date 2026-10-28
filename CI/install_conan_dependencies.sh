#!/usr/bin/env bash
# Restore the upstream, pinned binary dependency snapshot only.
# Never compile missing packages or publish Conan binaries.
set -euo pipefail

RELEASE_TAG="2026-10-13"
if [[ $# -ne 1 || ! "$1" =~ ^dependencies-[a-zA-Z0-9_-]+$ ]]; then
  echo "Usage: $0 dependencies-<platform>" >&2
  exit 2
fi
FILENAME="$1.txz"
DOWNLOAD_URL="https://github.com/vcmi/vcmi-dependencies/releases/download/$RELEASE_TAG/$FILENAME"
downloadedFile="${RUNNER_TEMP:-/tmp}/$FILENAME"

start=$SECONDS
curl --fail --location --retry 3 --retry-all-errors --proto '=https' \
  --output "$downloadedFile" "$DOWNLOAD_URL"
test -s "$downloadedFile"
echo "Conan binary snapshot: $FILENAME (release $RELEASE_TAG)"
sha256sum "$downloadedFile" 2>/dev/null || shasum -a 256 "$downloadedFile"
conan cache restore "$downloadedFile"
echo "Conan binary restore elapsed: $((SECONDS-start)) seconds"
