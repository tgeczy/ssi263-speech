#!/bin/sh
# The app's import (FirmwareInstaller) as the app runs it, on the Mac, against SSI263Core's macos-arm64 slice and the
# repository's firmware, in a temporary folder in place of the App Group's (InstallerTests.swift).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
APPLE="$HERE/.."
ROOT="$(cd "$APPLE/../../.." && pwd)"
OUT="$ROOT/build/apple/test"
LIB="$ROOT/build/apple/slices/macos-arm64/libssi263core.a"
[ -f "$LIB" ] || { echo "no $LIB: run sh src/platforms/apple/build_apple.sh macos first"; exit 1; }
mkdir -p "$OUT"
cp "$HERE/InstallerTests.swift" "$OUT/main.swift"
xcrun swiftc -O -import-objc-header "$APPLE/shared/BridgingHeader.h" -I "$APPLE/core" \
    -I "$ROOT/src/platforms/android/app/src/main/cpp" -I "$ROOT/src/csrc" -I "$ROOT/src/csrc/blazie" \
    -I "$ROOT/src/csrc/cpu" -o "$OUT/installer_tests" "$APPLE/shared/SsiShared.swift" "$APPLE/app/ZipReader.swift" \
    "$APPLE/app/FirmwareImport.swift" "$APPLE/app/FirmwareInstaller.swift" "$OUT/main.swift" "$LIB" -lc++
# the toolkit's disk image, never in the repository: paths.local's MOCKINGBOARD_DISKS (or SSI263_MOCKINGBOARD_DISKS)
DISKS="${SSI263_MOCKINGBOARD_DISKS:-$( [ -f "$ROOT/paths.local" ] && sed -n 's/^[[:space:]]*MOCKINGBOARD_DISKS[[:space:]]*=[[:space:]]*//p' "$ROOT/paths.local" | tail -1)}"
"$OUT/installer_tests" "$ROOT" ${DISKS:+"$DISKS"}
