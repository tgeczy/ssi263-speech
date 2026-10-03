#!/bin/sh
# release_from_ci.sh -- the Linux release of a commit from its CI build (.github/workflows/linux.yml): the binaries
# built in Debian 11 by GitHub (x86-64, arm64), then tested with the firmware and packaged on the Raspberry Pi, where
# the firmware lives (it never goes to GitHub).  Driven from Git Bash on Windows (gh, git, ssh, scp); the Pi runs this
# same script's on-pi half.
#
#   sh tools/release_from_ci.sh all COMMIT [VERSION]        everything below, in order (VERSION: 0.7.6)
#   sh tools/release_from_ci.sh download COMMIT              the CI run of COMMIT (dispatched on CI_BRANCH, default
#                                                            main, when there is none), awaited; its two artifacts
#                                                            into build/ci/<commit>/, their COMMIT checked
#   sh tools/release_from_ci.sh pi COMMIT [VERSION]          the source (git archive of COMMIT), the artifacts and the
#                                                            release tools to the Pi's ~/ci-release/<commit>/, then
#                                                            each architecture there
#   sh tools/release_from_ci.sh fetch COMMIT [VERSION]       the results into nvda/dist/linux (DEST), hashes checked
#
# Per architecture on the Pi (ARCHES, default "armhf aarch64 x86_64"); results in ~/ci-release/<commit>/out/<arch>/:
# the assets, SHA256SUMS-<arch>.txt and logs-<arch>/ (COMMIT, EXITCODES, STATUS, one log per step):
#   aarch64  the CI build laid into the git archive and run natively on the Pi's own Debian (the binaries need only
#            glibc 2.29, and its Python 3.11+ runs the BT frontend's tests): the whole gate with the firmware
#            (tools/linux_tests.sh, which never builds: it runs build/linux and src/ssi263/_bin/<plat> as they are),
#            then the package, the wheel, the shipped wheel and its control, the emulator archive
#            (tools/package_emu_linux.sh).
#   x86_64   the Pi cannot run it natively: the same packaging steps in the kept amd64 bullseye chroot under
#            qemu-user (/srv/amd64-bullseye; package_linux.sh asks the module for its voices), plus the goldens and
#            the shipped wheel there.  The whole gate is NOT run (35 minutes under qemu, and its faster-than-real-time
#            checks cannot pass emulated): EXITCODES and STATUS say so.
#   armhf    not built in CI (GitHub's arm64 runners cannot run 32-bit ARM code): the Pi's own route, its kept
#            Debian 11 armhf chroot under linux32 (~/arm-work: mkchroot.sh, stage.sh, run_build.sh -- build, gate,
#            package, wheel), then this repository's emulator archive.  Only the emulator archive ships.  The GTK
#            window checks run against the arm64 chroot's X server (the armhf Xvfb crashes on the Pi 5's kernel).
# After each: on the Pi itself, the no-GPL audit of what ships, the GLIBC floor (2.29) of every binary in it and its
# contents (the package: the Speak-Out's and Aicom's firmware, the BT frontend, no Braille 'n Speak 2000; the emulator
# archive: the Braille 'n Speak 2000's four files and the BT frontend).  STATUS's first line is "ok" only when every
# step did what it must (the controls fail); its next lines say what was not run.
#
# The release tools (this script, package_emu_linux.sh, check_glibc_floor.sh) are this checkout's, laid over the
# commit's tree on the Pi (the commit may predate them); everything that ships is the commit's and CI's.
#
# Settings: SSI263_PI (user@host) and SSI263_PI_KEY (its ssh key), or PI and PI_KEY in paths.local.  On the Pi:
# FIRMWARE (default ~/blazie-firmware, with spanish/, tns/ and bns2000/) and SPEAKOUT (default ~/speakout-firmware).
# fetch moves the older files of each architecture and VERSION in DEST into build-<their commit>/ first, and refuses
# an architecture whose STATUS is not ok unless FORCE=1.
set -u
CMD="${1:?usage: release_from_ci.sh all|download|pi|fetch COMMIT [VERSION]}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ARCHES="${ARCHES:-armhf aarch64 x86_64}"
GLIBC_FLOOR=2.29
BUILDER=builder                     # the chroots' build user (uid 1000)

# ---------------------------------------------------------------------------------------------- on the Pi

step() {                            # name, then the command: its log in $L, its exit in $L/EXITCODES
    n="$1"; shift
    "$@" > "$L/$n.log" 2>&1; e=$?
    echo "$n exit=$e" >> "$L/EXITCODES"; echo "$n exit=$e"
}

verdict() {                         # $L/STATUS: ok when every step is as it must be (a _control must fail)
    bad=""
    while read -r n e; do
        case "$e" in exit=*) ;; *) continue;; esac
        e="${e#exit=}"
        case "$n" in *_control) [ "$e" != 0 ] || bad="$bad $n";; *) [ "$e" = 0 ] || bad="$bad $n";; esac
    done < "$L/EXITCODES"
    if [ -z "$bad" ]; then echo ok > "$L/STATUS"; else echo "FAILED:$bad" > "$L/STATUS"; fi
    grep -E "not-run|superseded" "$L/EXITCODES" >> "$L/STATUS"
    echo "STATUS $1: $(head -n 1 "$L/STATUS")"
}

fw_copies() {                       # DIR: DIR/fw-ship (the firmware without bns2000/), DIR/speakout-firmware (beside
    d="$1"                          # it, where package_linux.sh looks), DIR/bns2000 (the emulator archive's)
    mkdir -p "$d/fw-ship"
    (cd "$FIRMWARE" && tar -cf - --exclude=./bns2000 .) | (cd "$d/fw-ship" && tar -xf -)
    [ -d "$SPEAKOUT" ] && cp -r "$SPEAKOUT" "$d/speakout-firmware"
    cp -r "$FIRMWARE/bns2000" "$d/bns2000"
}

lay_out() {                         # SRC ARTIFACT: the commit's tree, the CI build over it, the release tools over that
    s="$1"
    mkdir -p "$s"
    tar -xf "$W/src.tar" -C "$s" && tar -xzf "$W/$2" -C "$s" || return 1
    [ "$(cat "$s/COMMIT")" = "$C" ] || { echo "the CI build is of $(cat "$s/COMMIT"), not $C"; return 1; }
    cp "$W"/tools/*.sh "$s/tools/"
}

# package A V SRC FW_SHIP BNS_DIR DATA LOGS: in the source tree (the Pi's own system, or a chroot), from the CI build
pi_package() {
    A="$1"; V="$2"; L="$7"; FW_SHIP="$4"; BNS_DIR="$5"; DATA="$6"
    cd "$3" || exit 1
    export LANG=C.UTF-8 PYTHON_COLORS=0 PYTHONDONTWRITEBYTECODE=1
    M="$(uname -m)"
    if [ "${GOLDENS:-0}" = 1 ]; then  # the native host's goldens, byte for byte (as linux_tests.sh runs them)
        LIB="$PWD/src/ssi263/_bin/linux-$M/libssi263speech.so"
        mkdir -p nvda/dist/blazie-build/synthDrivers
        ln -sfn "$DATA" nvda/dist/blazie-build/synthDrivers/_ssi263_blazie
        step golden_en env SSI263_LIB="$LIB" python3 nvda/tools/bns_equiv.py --native "$LIB" \
            --against=nvda/tools/golden/blazie_en.txt
        step golden_es env SSI263_LIB="$LIB" python3 nvda/tools/bns_equiv.py --native --es "$LIB" \
            --against=nvda/tools/golden/blazie_es.txt
    fi
    step package sh tools/package_linux.sh "$FW_SHIP" "$V"
    step no_bns2000 sh -c "if tar -tzf build/ssi263-speech-$V-linux-$M.tar.gz | grep -i -E 'bns2000|bs03eng|bs2sll|bs2eng'; \
        then exit 1; fi; echo 'no Braille n Speak 2000 firmware in the package'"
    rm -rf dist
    step wheel python3 python/build_wheel.py --lib-dir build/linux --plat "linux_$M" --out dist
    step wheel_shipped python3 python/test_wheel.py dist/ssi263speech-*.whl "$DATA"
    step wheel_shipped_control env WHEEL_TEST_BREAK=1 python3 python/test_wheel.py dist/ssi263speech-*.whl "$DATA"
    step emu_archive sh tools/package_emu_linux.sh "$BNS_DIR" "$V" "$A"
}

contents() {                        # archive, then kind (full|emu): what must and must not be in it
    t="$(tar -tzf "$1")"; miss=""
    if [ "$2" = full ]; then
        need="bin/sd_ssi263 lib/libssi263speech.so bin/blazie_emu bin/blazie_emu_gtk bin/blazie_files bin/blazie_emu_bt
            bin/blazie_bt README-blazie-bt.md share/ssi263-speech/BL2ENG.BNS share/ssi263-speech/bl2_2003_warm.state
            share/ssi263-speech/BL2SPA.BNS share/ssi263-speech/bl2spa_fresh.state share/ssi263-speech/tns/TNSENG.TNS
            share/ssi263-speech/gw-micro-speakout/SPEAKOUT.HEX share/ssi263-speech/aicom-accent-sa/u2.BIN
            share/ssi263-speech/aicom-accent-sa/u3.BIN share/ssi263-speech/aicom-accent-sa/u4.BIN
            share/ssi263-speech/aicom-accent-mini/SPKEMS.DVC licenses/Aicom-notice.txt
            licenses/Speak-Out-firmware-notice.txt share/ssi263-speech/speech-dispatcher/ssi263.conf"
        never='bns2000|bs03eng|bs2sll|bs2eng'
    else
        need="bin/blazie_emu bin/blazie_emu_gtk bin/blazie_files bin/blazie_emu_bt bin/blazie_bt README-blazie-bt.md
            share/ssi263-speech/BL2ENG.BNS share/ssi263-speech/tns/TNSENG.TNS share/ssi263-speech/bns2000/BS03ENG.BNS
            share/ssi263-speech/bns2000/bs03eng_fresh.state share/ssi263-speech/bns2000/BS2SLL.BNS
            share/ssi263-speech/bns2000/bs2sll_fresh.state"
        never='speech-dispatcher/|bin/sd_ssi263'
    fi
    for f in $need; do echo "$t" | grep -q -E "^[^/]+/$f\$" || miss="$miss $f"; done
    bad="$(echo "$t" | grep -i -E "$never")"
    [ -z "$miss" ] && [ -z "$bad" ] && { echo "ok    $(basename "$1"): $(echo "$need" | wc -w) files present, none of $never"; return 0; }
    [ -n "$miss" ] && echo "FAIL  $(basename "$1"): missing$miss"
    [ -n "$bad" ] && echo "FAIL  $(basename "$1"): must not carry: $bad"
    return 1
}

glibc_of() {                        # archive...: the GLIBC floor of every binary in them, extracted
    g="$(mktemp -d)"
    for a in "$@"; do
        case "$a" in *.whl) python3 -m zipfile -e "$a" "$g/$(basename "$a")";; *) mkdir -p "$g/$(basename "$a")"
            tar -xzf "$a" -C "$g/$(basename "$a")";; esac
    done
    sh "$S/tools/check_glibc_floor.sh" "$GLIBC_FLOOR" $(find "$g" -type f \( -path '*/bin/*' -o -name '*.so' \) | sort)
    r=$?; rm -rf "$g"; return $r
}

pi_verify() {                       # in $O, its assets: audit, GLIBC floor, contents; then the hashes
    cd "$O" || exit 1
    set --
    for a in *.tar.gz *.whl; do [ -f "$a" ] && set -- "$@" "$a"; done
    [ $# -gt 0 ] || { echo "no assets in $O"; echo "assets exit=1" >> "$L/EXITCODES"; return; }
    step audit python3 "$S/tools/check_no_gpl.py" "$@"
    step glibc_floor glibc_of "$@"
    for a in "$@"; do
        case "$a" in ssi263-speech-*) step contents_package contents "$a" full;;
                     blazie-emu-*) step contents_emu contents "$a" emu;; esac
    done
    sha256sum -b "$@" > "SHA256SUMS-$ARCH.txt"
}

# armhf's GTK window checks: the armhf Xvfb segfaults at start on the Pi 5's 16K-page kernel (a test host limit), so the
# gate's three GTK window checks fail there for want of an X server.  When they are the gate's only failures, they run
# again against the arm64 chroot's Xvfb -- the program under test is still the armhf build -- with the gate's own marks
# for the controls; if all hold, the gate's line becomes "tests superseded" with the reason (STATUS shows it).
armhf_gtk() {                       # after the armhf run: $D its chroot, $CH its home, $H the two joined, $L, $AW
    others="$(grep '^FAIL' "$L/tests.log" | grep -v -E \
        '^FAIL  emulator \(GTK\): (the window as Orca reads it|accessibility CONTROL|held keys CONTROL)')"
    [ -z "$others" ] && grep -q '^tests exit=1$' "$L/EXITCODES" || return 0
    A64=/srv/arm64-bullseye
    sh "$AW/mkchroot.sh" arm64 "$A64" bullseye > "$L/chroot_arm64.log" 2>&1 || { echo "arm64 chroot FAILED"; return 0; }
    sudo chroot --userspec=1000:1000 "$A64" /usr/bin/env -i PATH=/usr/bin:/bin HOME="$CH" \
        Xvfb :57 -screen 0 1024x768x24 -ac -listen tcp -nolisten unix > "$L/xvfb_arm64.log" 2>&1 &
    for i in $(seq 60); do           # until it takes connections (display 57: TCP port 6057)
        python3 -c "import socket; socket.create_connection(('127.0.0.1', 6057), 1)" 2>/dev/null && break; sleep 1
    done
    gx() { sudo linux32 chroot --userspec=1000:1000 "$D" /usr/bin/env -i HOME="$CH" PATH=/usr/bin:/bin LANG=C.UTF-8 \
        PYTHONDONTWRITEBYTECODE=1 PYTHON_COLORS=0 DISPLAY=127.0.0.1:57 "$@"; }
    T="cd $CH/ssi263 && dbus-run-session -- python3 src/apps/blazie/test_emu_gtk.py build/linux/blazie_emu_gtk \
        $CH/blazie-firmware"
    step gtk_arm64_x gx sh -c "$T"
    fail=0
    . "$H/ssi263/tools/linux_control.sh"
    { control "emulator (GTK): accessibility CONTROL (the keyboard area unnamed, must fail)" \
        "^ok +menu bar: items by name" "^FAIL +keyboard area: named, focused +role panel, focused, name ''" \
        "^ok +status bar" "^gtk emulator: 1 of 4 FAILED$" \
        -- gx env BLAZIE_GTK_BREAK=noname sh -c "$T --only settings,tree"
      control "emulator (GTK): held keys CONTROL (dots 1 and 4 swapped, must fail)" \
        "^FAIL +i-chord held through the restart +the unit asked \"initialize file system\": no" \
        "^gtk emulator: 1 of 1 FAILED$" -- gx env BLAZIE_KEYS_BREAK=1 sh -c "$T --only held"
    } > "$L/gtk_arm64_x_controls.log" 2>&1
    echo "gtk_arm64_x_controls exit=$fail" >> "$L/EXITCODES"
    sudo pkill -f "^Xvfb :57 "
    if grep -q '^gtk_arm64_x exit=0$' "$L/EXITCODES" && [ $fail -eq 0 ]; then
        sed -i 's/^tests exit=1$/tests superseded: exit=1 only for its GTK window checks under the armhf Xvfb (it segfaults on the Pi 5 16K-page kernel); run again against the arm64 Xvfb: gtk_arm64_x, gtk_arm64_x_controls/' \
            "$L/EXITCODES"
    fi
}

on_pi() {
    ARCH="$1"; C="$2"; V="$3"
    W="$(cd "$HERE/.." && pwd)"; c="$(echo "$C" | cut -c1-7)"
    FIRMWARE="${FIRMWARE:-$HOME/blazie-firmware}"; SPEAKOUT="${SPEAKOUT:-$HOME/speakout-firmware}"
    O="$W/out/$ARCH"; L="$O/logs-$ARCH"
    rm -rf "$O" "$W/work-$ARCH"; mkdir -p "$L"
    echo "$C" > "$L/COMMIT"
    start=$(date +%s)
    case "$ARCH" in
    aarch64)
        S="$W/work-$ARCH/src"
        lay_out "$S" linux-build-arm64.tar.gz > "$L/lay_out.log" 2>&1 || { cat "$L/lay_out.log"; exit 1; }
        fw_copies "$W/work-$ARCH"
        { echo "built in CI: Debian 11 (glibc 2.31); run here: Debian $(cat /etc/debian_version), $(uname -m)"
          ldd --version | head -n 1; python3 --version; } > "$L/toolchain.txt" 2>&1
        cd "$S" || exit 1
        # the whole gate, with the unit's firmware, against the CI build (it never builds)
        step tests sh tools/linux_tests.sh "$FIRMWARE"
        grep -E "^(FAIL|skip)|Linux checks" "$L/tests.log"
        (pi_package aarch64 "$V" "$S" "$W/work-$ARCH/fw-ship" "$W/work-$ARCH/bns2000" "$FIRMWARE" "$L")
        cp "$S/build/ssi263-speech-$V-linux-aarch64.tar.gz" "$S"/dist/ssi263speech-$V-*linux_aarch64.whl \
            "$S/build/blazie-emu-$V-linux-aarch64.tar.gz" "$O/" 2>/dev/null
        ;;
    x86_64)
        D=/srv/amd64-bullseye; CH="/home/$BUILDER"; R="ci-$c"; H="$D$CH/$R"
        [ "$(sudo chroot "$D" uname -m)" = x86_64 ] || { echo "$D does not run as x86_64 (qemu-user binfmt?)"; exit 1; }
        mountpoint -q "$D/proc" || sudo mount -t proc proc "$D/proc"
        mountpoint -q "$D/sys" || { sudo mount --rbind /sys "$D/sys"; sudo mount --make-rslave "$D/sys"; }
        mountpoint -q "$D/dev" || { sudo mount --rbind /dev "$D/dev"; sudo mount --make-rslave "$D/dev"; }
        sudo rm -rf "$H"; sudo mkdir -p "$H"; sudo chown "$(id -u):$(id -g)" "$H"
        S="$H/src"
        lay_out "$S" linux-build-x86_64.tar.gz > "$L/lay_out.log" 2>&1 || { cat "$L/lay_out.log"; exit 1; }
        fw_copies "$H"
        mkdir -p "$H/logs"
        sudo chown -R 1000:1000 "$H"
        # nice: the arm runs' timed tests keep priority
        sudo nice -n 19 chroot --userspec=1000:1000 "$D" /usr/bin/env -i HOME="$CH" PATH=/usr/bin:/bin LANG=C.UTF-8 \
            GOLDENS=1 sh "$CH/$R/src/tools/release_from_ci.sh" package x86_64 "$V" "$CH/$R/src" "$CH/$R/fw-ship" \
            "$CH/$R/bns2000" "$CH/$R/fw-ship" "$CH/$R/logs"
        cp "$H"/logs/* "$L/"
        echo "tests not-run: x86_64 built and firmware-free-tested in CI (tools/linux_ci_checks.sh); the firmware gate (tools/linux_tests.sh) not run on the Pi (qemu: ~35 min, its real-time checks cannot pass); under qemu here: the goldens (en, es), the package's voices, the shipped wheel and its control, the emulator archive's boots" >> "$L/EXITCODES"
        cp "$S/build/ssi263-speech-$V-linux-x86_64.tar.gz" "$S"/dist/ssi263speech-$V-*linux_x86_64.whl \
            "$S/build/blazie-emu-$V-linux-x86_64.tar.gz" "$O/" 2>/dev/null
        ;;
    armhf)
        AW="${ARM_WORK:-$HOME/arm-work}"; D=/srv/armhf-bullseye; CH="/home/$BUILDER"; H="$D$CH"
        sh "$AW/mkchroot.sh" armhf "$D" bullseye > "$L/chroot.log" 2>&1 || { echo "chroot FAILED: $L/chroot.log"; exit 1; }
        sh "$AW/stage.sh" "$D" "$W/src.tar" "$c" > "$L/stage.log" 2>&1 || { echo "stage FAILED: $L/stage.log"; exit 1; }
        sudo cp "$W"/tools/*.sh "$H/ssi263/tools/"; sudo chown -R 1000:1000 "$H/ssi263/tools"
        in_armhf() { sudo linux32 chroot --userspec=1000:1000 "$D" /usr/bin/env -i HOME="$CH" PATH=/usr/bin:/bin \
            LANG=C.UTF-8 VERSION="$V" "$@"; }
        in_armhf sh "$CH/run_build.sh" > "$L/run_build.log" 2>&1
        in_armhf sh -c "cd $CH/ssi263 && sh tools/package_emu_linux.sh $CH/blazie-firmware/bns2000 $V armhf" \
            > "$H/logs/emu_archive.log" 2>&1
        echo "emu_archive exit=$?" >> "$H/logs/EXITCODES"
        cp "$H"/logs/* "$L/"; rm -f "$L/STATUS"
        echo "$C" > "$L/COMMIT"
        grep -E "^(FAIL|skip)|Linux checks" "$L/tests.log"
        S="$H/ssi263"
        armhf_gtk
        cp "$S/build/blazie-emu-$V-linux-armhf.tar.gz" "$O/" 2>/dev/null
        ;;
    *) echo "no such architecture: $ARCH"; exit 2;;
    esac
    (pi_verify)
    echo "wall time: $(( $(date +%s) - start )) s" > "$L/walltime.txt"
    verdict "$ARCH"
    cat "$L/walltime.txt"
    cat "$O/SHA256SUMS-$ARCH.txt" 2>/dev/null
}

case "$CMD" in
    on-pi) shift; on_pi "$@"; exit 0;;
    package) shift; pi_package "$@"; exit 0;;
esac

# ---------------------------------------------------------------------------------------------- on Windows

REPO="$(cd "$HERE/.." && pwd)"
cfg() {                             # KEY: SSI263_KEY, else KEY = value in paths.local
    eval "v=\${SSI263_$1:-}"
    [ -n "$v" ] || v="$(sed -n "s/^$1 *= *//p" "$REPO/paths.local" 2>/dev/null | tail -n 1 | tr -d '\r')"
    [ -n "$v" ] || { echo "set SSI263_$1 or $1 in paths.local" >&2; exit 1; }
    echo "$v"
}
C="$(git -C "$REPO" rev-parse "${2:?commit}^{commit}")" || exit 1
c="$(echo "$C" | cut -c1-7)"
V="${3:-0.7.6}"
CI="$REPO/build/ci/$c"
P="ci-release/$c"                   # on the Pi, under its home

pi_conn() {
    PI="$(cfg PI)" || exit 1; KEY="$(cfg PI_KEY)" || exit 1
    K="-i $KEY -o IdentitiesOnly=yes -o BatchMode=yes"
}
now() { date +%H:%M:%S; }

find_run() {
    gh run list --workflow linux.yml --limit 50 --json databaseId,headSha,displayTitle \
        -q "[.[] | select(.headSha == \"$C\" or .displayTitle == \"Linux build of $C\")][0].databaseId // empty"
}

download() {
    run="$(find_run)"
    if [ -z "$run" ]; then
        gh workflow run linux.yml --ref "${CI_BRANCH:-main}" -f ref="$C" || exit 1
        for i in 1 2 3 4 5 6; do sleep 5; run="$(find_run)"; [ -n "$run" ] && break; done
        [ -n "$run" ] || { echo "the dispatched CI run of $c did not appear"; exit 1; }
    fi
    echo "$(now) CI run $run: $(gh run view "$run" --json url -q .url)"
    gh run watch "$run" --exit-status > /dev/null 2>&1 || { echo "CI run $run did not pass"; exit 1; }
    echo "$(now) CI run $run passed"
    rm -rf "$CI"; mkdir -p "$CI"
    for a in x86_64 arm64; do
        gh run download "$run" -n "linux-build-$a" -D "$CI" || exit 1
        got="$(tar -xzOf "$CI/linux-build-$a.tar.gz" COMMIT)"
        [ "$got" = "$C" ] || { echo "linux-build-$a is of $got, not $C"; exit 1; }
    done
    echo "$run" > "$CI/RUN"
    echo "$(now) downloaded: $(cd "$CI" && ls linux-build-*.tar.gz | tr '\n' ' ')"
}

send_source() {
    pi_conn
    ssh $K "$PI" "mkdir -p $P/tools" </dev/null || exit 1
    git -C "$REPO" archive --format=tar "$C" | ssh $K "$PI" "cat > $P/src.tar" || exit 1
    scp -q $K "$HERE/release_from_ci.sh" "$HERE/package_emu_linux.sh" "$HERE/check_glibc_floor.sh" "$PI:$P/tools/" || exit 1
}

send_builds() {
    pi_conn
    scp -q $K "$CI"/linux-build-*.tar.gz "$PI:$P/" || exit 1
}

pi_arch() {                         # one architecture on the Pi, its summary here
    echo "$(now) $1: on the Pi"
    ssh $K "$PI" "cd $P && sh tools/release_from_ci.sh on-pi $1 $C $V > on-pi-$1.log 2>&1; \
        grep -E '^(FAIL|STATUS|wall time)|not-run' on-pi-$1.log" </dev/null
    echo "$(now) $1: done"
}

pi_all() {                          # armhf (needs no CI build) may already be running: $1 = its pid
    has() { echo " $ARCHES " | grep -q " $1 "; }
    [ -n "${1:-}" ] && wait "$1"
    [ -z "${1:-}" ] && has armhf && pi_arch armhf
    xp=""
    if has x86_64; then pi_arch x86_64 & xp=$!; fi
    has aarch64 && pi_arch aarch64
    [ -n "$xp" ] && wait "$xp"
}

fetch() {
    pi_conn
    DEST="${DEST:-$REPO/nvda/dist/linux}"
    mkdir -p "$DEST"
    for a in $ARCHES; do
        st="$(ssh $K "$PI" "head -n 1 $P/out/$a/logs-$a/STATUS" </dev/null)"
        if [ "$st" != ok ] && [ "${FORCE:-0}" != 1 ]; then
            echo "$a of $c is not clean ($st): not fetched (FORCE=1 to fetch anyway)"; continue
        fi
        old="$(find "$DEST"/logs-$a* -name COMMIT 2>/dev/null | head -n 1)"
        oldc="unknown"; [ -n "$old" ] && oldc="$(cut -c1-7 "$old")"
        moved=0
        for f in "$DEST/ssi263-speech-$V-linux-$a.tar.gz" "$DEST"/ssi263speech-$V-*linux_$a.whl \
                 "$DEST/blazie-emu-$V-linux-$a.tar.gz" "$DEST/SHA256SUMS-$a.txt" "$DEST"/logs-$a*; do
            [ -e "$f" ] || continue
            mkdir -p "$DEST/build-$oldc"
            [ -e "$DEST/build-$oldc/$(basename "$f")" ] && { echo "$DEST/build-$oldc/$(basename "$f") exists: stopped"; exit 1; }
            mv "$f" "$DEST/build-$oldc/"; moved=$((moved + 1))
        done
        [ $moved -gt 0 ] && echo "moved $moved older $a files to build-$oldc/"
        scp -q -r $K "$PI:$P/out/$a/*" "$DEST/" || exit 1
        (cd "$DEST" && sha256sum -c --quiet "SHA256SUMS-$a.txt") && echo "fetched $a of $c: $(cut -c67- "$DEST/SHA256SUMS-$a.txt" | tr '\n' ' ')"
    done
}

case "$CMD" in
    download) download;;
    pi) send_source; [ "$ARCHES" = armhf ] || send_builds; pi_all;;
    fetch) fetch;;
    all)
        t0=$(date +%s)
        send_source
        armhf_pid=""
        if echo " $ARCHES " | grep -q " armhf "; then pi_arch armhf & armhf_pid=$!; fi
        download
        send_builds
        pi_all "$armhf_pid"
        fetch
        echo "$(now) all done in $(( ($(date +%s) - t0) / 60 )) min";;
    *) echo "usage: release_from_ci.sh all|download|pi|fetch COMMIT [VERSION]"; exit 2;;
esac
