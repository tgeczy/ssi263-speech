#!/bin/sh
# Build the Apple apps' native core, SSI263Core.xcframework: the SSI-263 chip, every voice's board, host and voice on
# MAME's Z180, 8085, V40 and 8086 cores (C++17 interpreters, no JIT, so iOS allows them), the voice table
# (src/csrc/voices.c), Android's plain-C front end (src/platforms/android/app/src/main/cpp: ssa_engine, ssa_map,
# ssa_import, used unchanged) and the Apple front end's own C (src/platforms/apple/core) -- the same sources and flags
# as build_android.sh's objects(), built with Xcode's clang for each Apple platform, as static libraries:
#
#   macos            arm64 + x86_64 (one fat library)
#   ios              arm64 (devices)
#   ios-simulator    arm64 + x86_64
#
#   sh src/platforms/apple/build_apple.sh              every slice, then the XCFramework
#   sh src/platforms/apple/build_apple.sh macos        one platform's slices only (no XCFramework)
#
# Output (gitignored): build/apple/<platform>/libssi263core.a and build/apple/SSI263Core.xcframework, with the
# headers the Swift bridge imports (build/apple/include).  The apps link it with -lc++ (MAME's cores are C++).
#
# No firmware is built in, fetched or staged: the apps ship hollow and each user imports their own (README.md).
set -e

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
SRC="$ROOT/src/csrc"
CPP="$ROOT/src/platforms/android/app/src/main/cpp"
CORE="$ROOT/src/platforms/apple/core"
OUT="$ROOT/build/apple"
MACOS_MIN=13.0                          # AVSpeechSynthesisProviderAudioUnit: macOS 13, iOS 16 (as TGSpeechBox's)
IOS_MIN=16.0

# As build_android.sh (and build_linux.sh).  -ffp-contract=off keeps the Python reference's arithmetic (no fused
# multiply-adds, which arm64 would otherwise use), so the PCM is the other platforms' byte for byte.  No -fPIC: these
# are static libraries.  -fvisibility=hidden as there: the app calls the C API, nothing else.
CHIP="-O2 -std=c99 -ffp-contract=off -fvisibility=hidden -Wall -Wextra -Wno-unused-parameter"
BOARD="-O3 -std=gnu89 -ffp-contract=off -fvisibility=hidden -DBL_Z180_MAME -w -I$SRC/blazie -I$SRC/cpu -I$SRC"
FRONT="-O2 -std=c99 -ffp-contract=off -fvisibility=hidden -Wall -Wextra -Wno-unused-parameter -I$SRC"
Z180CXX="-O3 -std=c++17 -fno-exceptions -fno-rtti -ffp-contract=off -fvisibility=hidden -Wall -I$SRC/cpu -I$SRC"
MAME="-O2 -std=c++17 -fno-exceptions -fno-rtti -ffp-contract=off -fvisibility=hidden -Wall -Wno-sign-compare -I$SRC/cpu -I$SRC"
ACCENT="-O2 -std=gnu89 -ffp-contract=off -fvisibility=hidden -Wall -I$SRC/cpu -I$SRC/accentsa -I$SRC"
SPEAKOUT="-O2 -std=gnu89 -ffp-contract=off -fvisibility=hidden -Wall -I$SRC/cpu -I$SRC/speakout -I$SRC"
# Every voice: the Accent-mini when its sources are in the tree, as build_android.sh decides
MINI=0
HAVE="-DSSV_HAVE_SPEAKOUT"
if [ -f "$SRC/accentmini/am_voice.c" ]; then
    MINI=1
    HAVE="$HAVE -DSSV_HAVE_ACCENTMINI"
fi

# slice -> SDK and clang target
sdk() {
    case "$1" in
        macos-*) echo macosx ;;
        ios-arm64) echo iphoneos ;;
        iossim-*) echo iphonesimulator ;;
    esac
}
triple() {
    case "$1" in
        macos-arm64) echo "arm64-apple-macos$MACOS_MIN" ;;
        macos-x86_64) echo "x86_64-apple-macos$MACOS_MIN" ;;
        ios-arm64) echo "arm64-apple-ios$IOS_MIN" ;;
        iossim-arm64) echo "arm64-apple-ios$IOS_MIN-simulator" ;;
        iossim-x86_64) echo "x86_64-apple-ios$IOS_MIN-simulator" ;;
        *) echo "unknown slice $1" >&2; exit 1 ;;
    esac
}

slice() {
    S="$1"; O="$OUT/slices/$S"
    SDK="$(sdk "$S")"; T="--target=$(triple "$S")"
    SYSROOT="-isysroot $(xcrun --sdk "$SDK" --show-sdk-path)"
    CLANG="$(xcrun --sdk "$SDK" --find clang)"; CLANGXX="$(xcrun --sdk "$SDK" --find clang++)"
    cc() { "$CLANG" $T $SYSROOT "$@"; }
    cxx() { "$CLANGXX" $T $SYSROOT "$@"; }
    # a fresh folder: the library takes every object in it, and none from an older build may come back in
    rm -rf "$O"
    mkdir -p "$O"
    echo "=== $S ==="
    cc $CHIP -c -o "$O/ssi263.o" "$SRC/ssi263.c"
    cc $CHIP -c -o "$O/ssi263dsp.o" "$SRC/ssi263dsp.c"
    cxx $Z180CXX -c -o "$O/z180_mame.o" "$SRC/cpu/z180_mame.cpp"
    cxx $Z180CXX -c -o "$O/z180_asci.o" "$SRC/cpu/z180_asci.cpp"
    for f in bl_board flash29 bl_serial bl_idle bl_clock bl_host bl_voice bl_firmware bl_state; do
        cc $BOARD -c -o "$O/$f.o" "$SRC/blazie/$f.c"
    done
    cxx $MAME -c -o "$O/i8085_mame.o" "$SRC/cpu/i8085_mame.cpp"
    for f in as_board as_usart as_host; do
        cc $ACCENT -c -o "$O/$f.o" "$SRC/accentsa/$f.c"
    done
    cc $FRONT -c -o "$O/as_voice.o" "$SRC/accentsa/as_voice.c"
    cc $FRONT -c -o "$O/numwords.o" "$SRC/numwords.c"
    cc $FRONT -c -o "$O/accent_text.o" "$SRC/accent_text.c"
    cc $FRONT -c -o "$O/numwords_es.o" "$SRC/numwords_es.c"
    cc $FRONT -I"$SRC/blazie" -c -o "$O/bl_numbers.o" "$SRC/blazie/bl_numbers.c"
    cc $FRONT $HAVE -c -o "$O/voices.o" "$SRC/voices.c"
    cxx $MAME -c -o "$O/v40_mame.o" "$SRC/cpu/v40_mame.cpp"
    for f in so_board so_icu so_scu so_hex so_host; do
        cc $SPEAKOUT -c -o "$O/$f.o" "$SRC/speakout/$f.c"
    done
    cc $FRONT -c -o "$O/so_voice.o" "$SRC/speakout/so_voice.c"
    if [ "$MINI" = 1 ]; then
        cxx $MAME -c -o "$O/i86_mame.o" "$SRC/cpu/i86_mame.cpp"
        for f in pc86/pc86 accentmini/am_host accentmini/am_voice; do
            cc $ACCENT -I$SRC/pc86 -c -o "$O/${f##*/}.o" "$SRC/$f.c"
        done
    fi
    # Android's front end, unchanged, and the Apple front end's own C
    for f in ssa_map ssa_engine ssa_import; do
        cc $FRONT -c -o "$O/$f.o" "$CPP/$f.c"
    done
    for f in "$CORE"/*.c; do
        [ -f "$f" ] || continue
        cc $FRONT -I"$CPP" -I"$CORE" -c -o "$O/apple_$(basename "$f" .c).o" "$f"
    done
    xcrun ar rcs "$O/libssi263core.a" "$O"/*.o
}

# one platform's slices as one library (lipo when it has two architectures)
platform() {
    P="$1"; shift
    for s in "$@"; do slice "$s"; done
    mkdir -p "$OUT/$P"
    LIBS=""
    for s in "$@"; do LIBS="$LIBS $OUT/slices/$s/libssi263core.a"; done
    xcrun lipo -create $LIBS -output "$OUT/$P/libssi263core.a"
    echo "  -> build/apple/$P/libssi263core.a ($(xcrun lipo -archs "$OUT/$P/libssi263core.a"))"
}

headers() {
    H="$OUT/include"
    rm -rf "$H"
    mkdir -p "$H"
    cp "$CPP/ssa_engine.h" "$CPP/ssa_map.h" "$CPP/ssa_import.h" "$H/"
    for f in "$CORE"/*.h; do [ -f "$f" ] && cp "$f" "$H/"; done
    cat > "$H/module.modulemap" <<'EOF'
module SSI263Core {
    umbrella "."
    export *
}
EOF
}

# What the apps show under "Licenses and source", into build/apple/licenses (the project takes the folder as it is):
# the project's MIT, Casso's, and MAME's BSD-3-Clause notices for the four cores, as build_android.sh stages them.
# No firmware notice: the apps carry no firmware.
licenses() {
    L="$OUT/licenses"
    rm -rf "$L"
    mkdir -p "$L"
    cp "$ROOT/LICENSE" "$L/1 SSI-263 Speech (MIT).txt"
    cp "$SRC/cpu/mame_z180/LICENSE-BSD-3-Clause.txt" "$L/2 MAME Z180 core (BSD-3-Clause).txt"
    cp "$SRC/cpu/mame_i8085/LICENSE-BSD-3-Clause.txt" "$L/3 MAME 8085 core (BSD-3-Clause).txt"
    cp "$SRC/cpu/mame_nec/LICENSE-BSD-3-Clause.txt" "$L/4 MAME NEC V40 core (BSD-3-Clause).txt"
    if [ "$MINI" = 1 ]; then
        cp "$SRC/cpu/mame_i86/LICENSE-BSD-3-Clause.txt" "$L/5 MAME 8086 core (BSD-3-Clause).txt"
    fi
    cp "$ROOT/third_party/casso/LICENSE" "$L/6 Casso (MIT).txt"
}

case "${1:-all}" in
    macos) platform macos macos-arm64 macos-x86_64; licenses ;;
    ios) platform ios ios-arm64 ;;
    ios-simulator) platform ios-simulator iossim-arm64 iossim-x86_64 ;;
    all)
        platform macos macos-arm64 macos-x86_64
        platform ios ios-arm64
        platform ios-simulator iossim-arm64 iossim-x86_64
        headers
        licenses
        rm -rf "$OUT/SSI263Core.xcframework"
        xcodebuild -create-xcframework \
            -library "$OUT/macos/libssi263core.a" -headers "$OUT/include" \
            -library "$OUT/ios/libssi263core.a" -headers "$OUT/include" \
            -library "$OUT/ios-simulator/libssi263core.a" -headers "$OUT/include" \
            -output "$OUT/SSI263Core.xcframework" >/dev/null
        echo "  -> build/apple/SSI263Core.xcframework"
        ;;
    *) echo "usage: build_apple.sh [macos|ios|ios-simulator]"; exit 1 ;;
esac
echo "done."
