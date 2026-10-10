#!/bin/sh
# Package the Blazie emulator alone, for people who want only it: from the full package's stage, so after
# tools/package_linux.sh (same version), in this machine's build.  Any architecture; ARCHNAME is only the archive's
# name (aarch64, armhf, x86_64; default uname -m).
#
#   ./build_linux.sh && tools/package_linux.sh <firmware folder> VERSION
#   tools/package_emu_linux.sh <Braille 'n Speak 2000 folder> [VERSION] [ARCHNAME]
#
#   build/blazie-emu-VERSION-linux-ARCHNAME.tar.gz
#     blazie-emu-VERSION-linux-ARCHNAME/
#       bin/blazie_*                     every blazie_ program the package has (blazie_emu, blazie_files, the GTK
#                                        emulator, the BT Speak / BT Braille frontend blazie_emu_bt and its worker)
#       README-blazie-emu.md  LICENSE    (README-blazie-bt.md too, with the BT frontend)
#       licenses/Casso-MIT.txt  licenses/MAME-Z180-core-BSD-3-Clause.txt
#       share/ssi263-speech/...          the package's firmware folder as it is, without speech-dispatcher/
#       share/applications/...           the GTK emulator's menu entry, when the package has one
#       share/ssi263-speech/bns2000/     the Braille 'n Speak 2000, English and Slovak (Tomi: a preview for Slovak
#                                        testers, in the emulator archives only, never the full package): exactly
#                                        the four files below, each checked against the sha256 Tomi approved
#
# Then: the no-GPL audit, and from a clean folder: --help, a headless Braille Lite, Type 'n Speak and Braille 'n Speak
# 2000 (bns-en, bns-sk) booted with no --firmware (so bin/../share/ssi263-speech is what finds the firmware), and
# blazie_files' usage.  Exit 0 only if all hold.  No firmware is in the repository: the Braille 'n Speak 2000 folder
# is the person's own.
BNS_DIR="$(cd "${1:?usage: package_emu_linux.sh <Braille 'n Speak 2000 folder> [VERSION] [ARCHNAME]}" && pwd)"
V="${2:-0.8.0}"; A="${3:-$(uname -m)}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 1
export LANG=C.UTF-8 PYTHON_COLORS=0 PYTHONDONTWRITEBYTECODE=1
PKG="build/package/ssi263-speech-$V-linux-$(uname -m)"
NAME="blazie-emu-$V-linux-$A"
S="build/emu-stage/$NAME"
# the Braille 'n Speak 2000's files and the first 16 hex digits of the sha256 of each one Tomi approved
BNS_FILES="BS03ENG.BNS:9ee0af633beb744c bs03eng_fresh.state:12112afda9be2809 BS2SLL.BNS:8c01c59845b5609c bs2sll_fresh.state:6818720dc50c5f7d"
[ -x "$PKG/bin/blazie_emu" ] || { echo "no package stage with bin/blazie_emu at $PKG (tools/package_linux.sh first)"; exit 1; }
rm -rf build/emu-stage "build/$NAME.tar.gz"; mkdir -p "$S/bin" "$S/licenses" "$S/share/ssi263-speech"
cp -p "$PKG"/bin/blazie_* "$S/bin/"
cp -p "$PKG/README-blazie-emu.md" "$PKG/LICENSE" "$S/"
[ -f "$PKG/README-blazie-bt.md" ] && cp -p "$PKG/README-blazie-bt.md" "$S/"
cp -p "$PKG/licenses/Casso-MIT.txt" "$PKG/licenses/MAME-Z180-core-BSD-3-Clause.txt" "$S/licenses/"
(cd "$PKG/share/ssi263-speech" && tar -cf - --exclude=./speech-dispatcher .) | (cd "$S/share/ssi263-speech" && tar -xf -)
[ -d "$PKG/share/applications" ] && cp -pr "$PKG/share/applications" "$S/share/"
mkdir -p "$S/share/ssi263-speech/bns2000"
for fh in $BNS_FILES; do
    f="${fh%%:*}"; want="${fh#*:}"
    got="$(sha256sum "$BNS_DIR/$f" 2>/dev/null | cut -c1-16)"
    [ "$got" = "$want" ] || { echo "FAIL: $f in the Braille 'n Speak 2000 folder: sha256 ${got:-missing}, expected $want..."; exit 1; }
    cp -p "$BNS_DIR/$f" "$S/share/ssi263-speech/bns2000/"
done
(cd build/emu-stage && tar --owner=0 --group=0 -czf "../$NAME.tar.gz" "$NAME") || exit 1
bad=0
echo "== contents"; tar -tvzf "build/$NAME.tar.gz"
echo "== audit"; python3 tools/check_no_gpl.py "build/$NAME.tar.gz" || bad=1
# the Braille 'n Speak 2000: exactly its four files, in share/ssi263-speech/bns2000/, and no other BNS image anywhere
got="$(tar -tzf "build/$NAME.tar.gz" | grep -i -E 'bns2000/.|bs03eng|bs2sll|bs2eng' | sort | tr '\n' ' ')"
want="$(for f in BS03ENG.BNS BS2SLL.BNS bs03eng_fresh.state bs2sll_fresh.state; do
    echo "$NAME/share/ssi263-speech/bns2000/$f"; done | sort | tr '\n' ' ')"
if [ "$got" = "$want" ]; then echo "ok: the Braille 'n Speak 2000's four files, and no other"
else echo "FAIL: Braille 'n Speak 2000 files: $got (expected $want)"; bad=1; fi
T="$(mktemp -d)"; C="$(mktemp -d)"
tar -xzf "build/$NAME.tar.gz" -C "$T"
cd "$T" || exit 1
for p in "$NAME"/bin/blazie_emu*; do
    echo "== $p --help"; timeout 20 "./$p" --help > help.txt 2>&1; e=$?; echo "exit=$e"; head -n 3 help.txt
    [ "$p" = "$NAME/bin/blazie_emu" ] && [ $e -ne 0 ] && bad=1
done
echo "== boot, Braille Lite (no --firmware; a clean folder)"
"./$NAME/bin/blazie_emu" --config "$C" --null --rate 22050 --unit bl-en --seconds 10 --rms 0:6 > boot.txt 2>&1
echo "exit=$?"; cat boot.txt
grep -q "^starting bl-en from .*/$NAME/bin/\.\./share/ssi263-speech/bl2_2003_warm.state" boot.txt || bad=1
awk '$1 == "rms" && $2 == "0.00-6.00" { ok = $NF > 0.01 } END { exit !ok }' boot.txt || bad=1
echo "== boot, Type 'n Speak (no --firmware)"
"./$NAME/bin/blazie_emu" --config "$C" --null --rate 22050 --unit tns-en --flash-instant --seconds 6 --rms 0:6 > tns.txt 2>&1
echo "exit=$?"; cat tns.txt
awk '$1 == "rms" && $2 == "0.00-6.00" { ok = $NF > 0.01 } END { exit !ok }' tns.txt || bad=1
for u in bns-en bns-sk; do
    echo "== boot, Braille 'n Speak 2000 $u (no --firmware)"
    "./$NAME/bin/blazie_emu" --config "$C" --null --rate 22050 --unit $u --seconds 8 --rms 0:6 > bns.txt 2>&1
    echo "exit=$?"; cat bns.txt
    grep -q "^starting $u from .*/$NAME/bin/\\.\\./share/ssi263-speech/bns2000/" bns.txt || bad=1
    awk '$1 == "rms" && $2 == "0.00-6.00" { ok = $NF > 0.01 } END { exit !ok }' bns.txt || bad=1
done
echo "== blazie_files (no arguments: its usage)"; "./$NAME/bin/blazie_files" > files.txt 2>&1; echo "exit=$?"; head -n 2 files.txt
grep -q "^usage: blazie_files" files.txt || bad=1
cd /; rm -rf "$T" "$C"
[ $bad -eq 0 ] && echo "emulator archive: ok ($NAME.tar.gz)" || echo "emulator archive: FAILED"
exit $bad
