#!/bin/sh
# The speech extension's Swift (SsiAudioUnit) as the system drives it, on the Mac, against SSI263Core's macos-arm64
# slice and the repository's English Braille Lite (ExtensionTests.swift).
# SSI263_EXTENSION_BREAK=silent puts one bug back (the controls): the run must then FAIL.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
APPLE="$HERE/.."
ROOT="$(cd "$APPLE/../../.." && pwd)"
OUT="$ROOT/build/apple/test/extension"
LIB="$ROOT/build/apple/slices/macos-arm64/libssi263core.a"
[ -f "$LIB" ] || { echo "no $LIB: run sh src/platforms/apple/build_apple.sh macos first"; exit 1; }
mkdir -p "$OUT"
# a folder of its own: the other Swift tests write a main.swift of theirs in build/apple/test
cp "$HERE/ExtensionTests.swift" "$OUT/main.swift"
SOURCES="$APPLE/shared/SsiShared.swift $APPLE/app/ZipReader.swift $APPLE/app/FirmwareImport.swift
    $APPLE/app/FirmwareInstaller.swift $APPLE/app/UnitCatalog.swift $APPLE/app/UnitFiles.swift
    $APPLE/extension/SsiAudioUnit.swift $APPLE/extension/Upsampler.swift $OUT/main.swift"
FLAGS="-import-objc-header $APPLE/shared/BridgingHeader.h -I $APPLE/core -I $ROOT/src/platforms/android/app/src/main/cpp
    -I $ROOT/src/csrc -I $ROOT/src/csrc/blazie -I $ROOT/src/csrc/cpu"
# shellcheck disable=SC2086
xcrun swiftc -O $FLAGS -o "$OUT/extension_tests" $SOURCES "$LIB" -lc++
"$OUT/extension_tests" "$ROOT"
