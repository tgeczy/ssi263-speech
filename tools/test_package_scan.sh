#!/bin/sh
# The Linux package's last refusal (tools/package_linux.sh never_shipped, run alone by --scan): the Super Voice's
# program and its disk never ship, found by name or by content -- under a folder name with a space too, which the
# old `for f in $(find ...)` split and never looked at (the 0.9.5 audit).
#
#   sh tools/test_package_scan.sh              exit 1 on any failure
#   PACKAGE_SCAN_BREAK=split  (the control)    the old loop: "named" and "content" must FAIL
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
T="$(mktemp -d)"
bad=0
check() {    # name, ok (0/1), detail
    if [ "$2" = 1 ]; then echo "ok    $1: $3"; else echo "FAIL  $1: $3"; bad=$((bad + 1)); fi
}
mkdir -p "$T/clean/share/ssi263-speech/aicom-accent-sa" "$T/named/share/a folder/x y" "$T/content/my voices"
echo "a ROM" > "$T/clean/share/ssi263-speech/aicom-accent-sa/u2.BIN"
echo "a stand-in" > "$T/named/share/a folder/x y/SUPERTOK.BIN"
out="$(sh "$ROOT/tools/package_linux.sh" --scan "$T/clean")"; code=$?
check clean "$([ $code = 0 ] && echo 1 || echo 0)" "exit $code: $out"
out="$(sh "$ROOT/tools/package_linux.sh" --scan "$T/named")"; code=$?
check named "$([ $code = 1 ] && echo "$out" | grep -q 'x y/SUPERTOK.BIN' && echo 1 || echo 0)" \
    "a SUPERTOK.BIN under a folder named with spaces: exit $code"
if [ -f "$ROOT/firmware/speech-systems-super-voice/SUPERTOK.BIN" ]; then
    cp "$ROOT/firmware/speech-systems-super-voice/SUPERTOK.BIN" "$T/content/my voices/renamed program.bin"
    out="$(sh "$ROOT/tools/package_linux.sh" --scan "$T/content")"; code=$?
    check content "$([ $code = 1 ] && echo "$out" | grep -q 'renamed program.bin' && echo 1 || echo 0)" \
        "the program renamed, under a folder named with a space, by its sha256: exit $code"
else
    echo "skip  content: no firmware/speech-systems-super-voice/SUPERTOK.BIN"
fi
rm -rf "$T"
if [ $bad -eq 0 ]; then echo "package scan: all passed"; exit 0; fi
echo "package scan: $bad FAILED"; exit 1
