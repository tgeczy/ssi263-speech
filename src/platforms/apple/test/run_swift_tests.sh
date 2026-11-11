#!/bin/sh
# The Apple apps' Swift that needs no device, on the Mac: FirmwareImport.swift, ZipReader.swift and WavExport.swift,
# compiled with the tests (FirmwareImportTests.swift) into one program and run.
# SSI263_IMPORT_BREAK=1|unopened|state|state-sizes|speakout|mockingboard|accent|release|soundingboard|smartalk|smartalkname|export puts one bug back (the
# controls): the run must then FAIL.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
APP="$HERE/../app"
OUT="$HERE/../../../../build/apple/test"
mkdir -p "$OUT"
# the tests' top-level code: swiftc runs only a main.swift's
cp "$HERE/FirmwareImportTests.swift" "$OUT/main.swift"
xcrun swiftc -O -o "$OUT/firmware_import_tests" "$APP/ZipReader.swift" "$APP/FirmwareImport.swift" "$APP/WavExport.swift" \
    "$OUT/main.swift"
"$OUT/firmware_import_tests"
