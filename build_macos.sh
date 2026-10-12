#!/bin/sh
# Build a native AppKit Blazie.app from the portable emulator core.
# Firmware is supplied by the user at run time and is never bundled.
set -eu

ROOT="$(cd "$(dirname "$0")" && pwd)"
export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-12.0}"
OUT="$ROOT/build/macos"
OBJ="$OUT/obj"
APP="$OUT/Blazie.app"
SRC="$ROOT/src/csrc"
SHELL="$ROOT/src/apps/blazie"
mkdir -p "$OBJ" "$APP/Contents/MacOS" "$APP/Contents/Resources"
rm -f "$OBJ"/*.o
CC="${CC:-clang}"
CXX="${CXX:-clang++}"

CHIP="-O2 -std=c99 -ffp-contract=off -Wall -Wextra -Wno-unused-parameter"
BOARD="-O3 -std=gnu89 -ffp-contract=off -DBL_Z180_MAME -w -I$SRC/blazie -I$SRC/cpu -I$SRC"
MAME="-O3 -std=c++17 -fno-exceptions -fno-rtti -ffp-contract=off -I$SRC/cpu -I$SRC"
APPFLAGS="-O2 -std=gnu99 -ffp-contract=off -Wall -Wextra -Wno-unused-parameter -I$SHELL -I$SRC"

for f in ssi263 ssi263dsp; do
    "$CC" $CHIP -c "$SRC/$f.c" -o "$OBJ/$f.o"
done
for f in z180_mame z180_asci; do
    "$CXX" $MAME -c "$SRC/cpu/$f.cpp" -o "$OBJ/$f.o"
done
for f in bl_board flash29 bl_serial bl_idle bl_clock bl_host bl_firmware bl_state tns_board; do
    "$CC" $BOARD -c "$SRC/blazie/$f.c" -o "$OBJ/$f.o"
done
for f in bl_files bl_files_state bl_files_xfer fat_img; do
    "$CC" $APPFLAGS -c "$SRC/blazie/$f.c" -o "$OBJ/$f.o"
done
"$CC" $APPFLAGS "$SHELL/test_basic_repair.c" "$OBJ/bl_files_xfer.o" "$OBJ/bl_files.o" "$OBJ/fat_img.o" \
    -o "$OUT/test_basic_repair"
"$OUT/test_basic_repair"
for f in emu_unit chords; do
    "$CC" $APPFLAGS -c "$SHELL/$f.c" -o "$OBJ/$f.o"
done
"$CC" $APPFLAGS -c "$SHELL/macos/serial_macos.c" -o "$OBJ/serial_macos.o"
"$CC" -O2 -fobjc-arc -Wall -Wextra -Wno-unused-parameter -I"$SHELL" \
    -c "$SHELL/macos/main.m" -o "$OBJ/main_macos.o"
"$CXX" -o "$APP/Contents/MacOS/Blazie" "$OBJ"/*.o \
    -framework Cocoa -framework AudioToolbox -framework Carbon -framework UniformTypeIdentifiers -lm
cp "$SHELL/macos/Info.plist" "$APP/Contents/Info.plist"
codesign --force --sign - "$APP" >/dev/null
printf 'Built %s\n' "$APP"
