#!/bin/sh
# The speech-dispatcher installer against scratch folders (Garrett, issue #10 / #11): install.sh and uninstall.sh
# from src/platforms/speechd, on a stand-in package, with speechd.conf as each kind of system has it.
#   - a stock config (no active AddModule: autodiscovery on) is left byte for byte as it was;
#   - an earlier install's line (0.7.0-0.7.6's active one; #11's commented one) is removed;
#   - a config that lists its modules itself (Raspberry Pi OS: AddModule "espeak-ng") gets this one listed too;
#   - --default sets DefaultModule, and uninstall.sh gives back each config exactly as it was.
# SSI263_INSTALL_BREAK=1 (the control) puts 0.7.6's always-added line back: the stock and earlier-install checks
# must fail.
#   sh tools/speechd_install_test.sh
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
FAIL=0
ok()   { printf 'ok   %-44s %s\n' "$1" "$2"; }
fail() { printf 'FAIL %-44s %s\n' "$1" "$2"; FAIL=1; }

# the stand-in package: the files install.sh copies, none of them real
PKG="$T/pkg"
mkdir -p "$PKG/share/ssi263-speech/speech-dispatcher" "$PKG/lib" "$PKG/bin"
: > "$PKG/share/ssi263-speech/BL2ENG.BNS"
: > "$PKG/share/ssi263-speech/bl2.state"
printf 'DefaultVoice "blazie"\n' > "$PKG/share/ssi263-speech/speech-dispatcher/ssi263.conf"
: > "$PKG/lib/libssi263speech.so"
printf '#!/bin/sh\n' > "$PKG/bin/sd_ssi263"
cp "$ROOT/src/platforms/speechd/install.sh" "$ROOT/src/platforms/speechd/uninstall.sh" "$PKG/"

STOCK='# speechd.conf, as a distribution ships it
DefaultModule espeak-ng
#AddModule "espeak"       "sd_espeak"    "espeak.conf"
#AddModule "espeak-ng"    "sd_espeak-ng" "espeak-ng.conf"
#AddModule "festival"     "sd_festival"  "festival.conf"
DefaultLanguage en
'
LISTED='# speechd.conf that lists its modules (autodiscovery off)
DefaultModule espeak-ng
AddModule "espeak-ng"    "sd_espeak-ng" "espeak-ng.conf"
#AddModule "festival"     "sd_festival"  "festival.conf"
'
MARK='# --- the Braille Lite 2000 voice (ssi263-speech install.sh) ---'

run() {     # run CASE CONF-TEXT [install args]: a fresh system, the config written, install.sh run
    D="$T/$1"
    rm -rf "$D"
    mkdir -p "$D/etc" "$D/modules" "$D/prefix"
    printf '%s' "$2" > "$D/etc/speechd.conf"
    cp "$D/etc/speechd.conf" "$D/before.conf"
    shift 2
    ( cd "$PKG" && PREFIX="$D/prefix" SSI263_SPD_MODULES="$D/modules" SSI263_SPD_CONF="$D/etc/speechd.conf" \
        sh ./install.sh "$@" ) > "$D/install.log" 2>&1
}
uninstall() {
    ( cd "$PKG" && PREFIX="$D/prefix" SSI263_SPD_MODULES="$D/modules" SSI263_SPD_CONF="$D/etc/speechd.conf" \
        sh ./uninstall.sh ) > "$D/uninstall.log" 2>&1
}
active() { grep -c '^[[:space:]]*AddModule[[:space:]]*"ssi263"' "$D/etc/speechd.conf"; }

# 1. stock: nothing written to speechd.conf; the module and its settings where speech-dispatcher looks
run stock "$STOCK"
if cmp -s "$D/before.conf" "$D/etc/speechd.conf"; then ok "stock config unchanged" "autodiscovery stays on"
else fail "stock config unchanged" "$(diff "$D/before.conf" "$D/etc/speechd.conf" | tr '\n' ' ')"; fi
if [ -x "$D/modules/sd_ssi263" ] && grep -q '^SSI263DataDir' "$D/etc/modules/ssi263.conf" 2>/dev/null; then
    ok "module and settings installed" "modules/sd_ssi263, modules/ssi263.conf"
else fail "module and settings installed" "$(ls "$D/modules" "$D/etc/modules" 2>&1 | tr '\n' ' ')"; fi
uninstall
if cmp -s "$D/before.conf" "$D/etc/speechd.conf" && [ ! -e "$D/modules/sd_ssi263" ]; then
    ok "uninstall (stock)" "config as before, module gone"
else fail "uninstall (stock)" "$(diff "$D/before.conf" "$D/etc/speechd.conf" | tr '\n' ' ')"; fi

# 2. an earlier install's active line (0.7.0-0.7.6), and #11's commented one: removed, the config as stock
for old in 'AddModule "ssi263" "sd_ssi263" "ssi263.conf"' '#AddModule "ssi263" "sd_ssi263" "ssi263.conf"'; do
    run upgrade "$STOCK$MARK
$old
"
    printf '%s' "$STOCK" > "$D/want.conf"
    if cmp -s "$D/want.conf" "$D/etc/speechd.conf"; then ok "earlier install's line removed" "$old"
    else fail "earlier install's line removed" "$old: $(diff "$D/want.conf" "$D/etc/speechd.conf" | tr '\n' ' ')"; fi
done

# 3. a config that lists its modules: this one listed too, the others untouched
run listed "$LISTED"
if [ "$(active)" = 1 ] && grep -q '^AddModule "espeak-ng"' "$D/etc/speechd.conf" \
        && [ "$(head -c "$(printf '%s' "$LISTED" | wc -c)" "$D/etc/speechd.conf")" = "$(printf '%s' "$LISTED")" ]; then
    ok "listed config: this module listed too" "espeak-ng kept, ssi263 added after"
else fail "listed config: this module listed too" "$(cat "$D/etc/speechd.conf" | tr '\n' '|')"; fi
uninstall
if cmp -s "$D/before.conf" "$D/etc/speechd.conf"; then ok "uninstall (listed)" "config as before"
else fail "uninstall (listed)" "$(diff "$D/before.conf" "$D/etc/speechd.conf" | tr '\n' ' ')"; fi

# 4. --default on a stock config: DefaultModule, no AddModule; uninstall restores the old default
run default "$STOCK" --default
if grep -q '^DefaultModule ssi263$' "$D/etc/speechd.conf" && [ "$(active)" = 0 ] \
        && grep -q '^# (before ssi263) DefaultModule espeak-ng' "$D/etc/speechd.conf"; then
    ok "--default" "DefaultModule ssi263, no AddModule"
else fail "--default" "$(cat "$D/etc/speechd.conf" | tr '\n' '|')"; fi
uninstall
if cmp -s "$D/before.conf" "$D/etc/speechd.conf"; then ok "uninstall (--default)" "the old default back"
else fail "uninstall (--default)" "$(diff "$D/before.conf" "$D/etc/speechd.conf" | tr '\n' ' ')"; fi

if [ $FAIL = 0 ]; then echo "speechd install: all passed"; else echo "speechd install: FAILED"; exit 1; fi
