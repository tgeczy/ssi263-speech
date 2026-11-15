#!/bin/sh
# sd_ssi263 under speech-dispatcher itself: a private server (its own config, socket and log; the system's is left
# alone) with this module as its only one, its audio into a null sink so nothing is heard.  A client speaks a long
# message, pauses it, resumes it and quits; what the server made of the module's answers is read from its own log.
#
#   src/platforms/speechd/test_real_server.sh <sd_ssi263> <data folder>
#
#   pause and resume   after PAUSE and RESUME the server speaks the message again (two "Module speak!", and its
#                      "Resume requested").  With the module answering 704 PAUSE it never does: the server waits for
#                      an index mark the module does not report, and the rest of the message is lost.
#   quit               the server closes the module on its "210 OK QUIT" ("module closed successfully"), no broken pipe
#
# Controls: SD_SSI263_TEST_PAUSE_704=1, the module answers a PAUSE with 704 -- pause and resume must FAIL;
# SD_SSI263_TEST_OLD_REPLIES=1, QUIT unanswered -- quit must FAIL.
# Needs speech-dispatcher, python3 and a PulseAudio (or PipeWire) server for the null sink: without one of them it
# says "skip" and exits 0, which is not a pass.
MOD="$1"; DATA="$(cd "$2" && pwd)"
command -v speech-dispatcher > /dev/null || { echo "skip  real server: speech-dispatcher is not installed"; exit 0; }
command -v pactl > /dev/null || { echo "skip  real server: no pactl (PulseAudio or PipeWire) here"; exit 0; }
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
SINK="ssi263test$$"
SINK_ID="$(timeout 10 pactl load-module module-null-sink sink_name=$SINK 2>/dev/null)"
[ -n "$SINK_ID" ] || { echo "skip  real server: no sound server answers here"; exit 0; }
DIR="$(mktemp -d)"; SOCK="$DIR/speechd.sock"; SERVER=""
cleanup() {
    [ -n "$SERVER" ] && kill "$SERVER" 2>/dev/null
    timeout 10 pactl unload-module "$SINK_ID" 2>/dev/null
    rm -rf "$DIR"
}
trap cleanup EXIT INT TERM
mkdir -p "$DIR/conf/modules" "$DIR/modules" "$DIR/log"
cp "$MOD" "$DIR/modules/sd_ssi263"
cat > "$DIR/conf/speechd.conf" <<EOF
CommunicationMethod "unix_socket"
AudioOutputMethod "pulse"
LogLevel 5
AddModule "ssi263" "sd_ssi263" "ssi263.conf"
DefaultModule ssi263
DefaultLanguage "en"
EOF
echo "SSI263DataDir \"$DATA\"" > "$DIR/conf/modules/ssi263.conf"
PULSE_SINK=$SINK speech-dispatcher -s -c unix_socket -S "$SOCK" -P "$DIR/speechd.pid" -C "$DIR/conf" \
    -m "$DIR/modules" -L "$DIR/log" -t 60 > /dev/null 2>&1 &
SERVER=$!
i=0; while [ ! -S "$SOCK" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
[ -S "$SOCK" ] || { echo "FAIL  real server: the private speech-dispatcher did not start"; exit 1; }
python3 - "$SOCK" <<'EOF'
import socket, sys, time
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); f = s.makefile("rwb")
def cmd(line):
    f.write(line.encode() + b"\r\n"); f.flush()
    while True:
        r = f.readline().decode().rstrip()
        if len(r) < 4 or r[3] == " ":
            return r
cmd("SET self CLIENT_NAME test:sd_ssi263:main")
cmd("SET self OUTPUT_MODULE ssi263")
f.write(b"SPEAK\r\n"); f.flush(); f.readline()
f.write(b"This is a long message for the pause test, with a comma or two, that keeps going well past the moment "
        b"the client says pause. It has a second sentence as well, and a third one after it.\r\n.\r\n"); f.flush()
while f.readline().decode()[3:4] != " ":
    pass
time.sleep(1.0)
cmd("PAUSE self")
time.sleep(1.5)
cmd("RESUME self")
time.sleep(4.0)
cmd("QUIT")
EOF
sleep 0.5
kill "$SERVER" 2>/dev/null; wait "$SERVER" 2>/dev/null; SERVER=""
LOG="$DIR/log/speech-dispatcher.log"
n() { grep -c -- "$1" "$LOG"; }
FAIL=0
if [ "$(n 'Module pause!')" = 1 ] && [ "$(n 'Resume requested')" = 1 ] && [ "$(n 'Module speak!')" = 2 ]; then
    echo "ok    pause and resume: the server spoke the message again after RESUME"
else
    echo "FAIL  pause and resume: $(n 'Module pause!') pause, $(n 'Resume requested') resume taken up, $(n 'Module speak!') speak (want 1, 1, 2); the module's answers: $(n 'got paused') paused, $(n 'got stopped') stopped"
    FAIL=1
fi
if [ "$(n 'module closed successfully')" = 1 ] && [ "$(n 'Broken pipe to module')" = 0 ]; then
    echo "ok    quit: the server closed the module on its answer"
else
    echo "FAIL  quit: $(n 'module closed successfully') clean close, $(n 'Broken pipe to module') broken pipe"
    FAIL=1
fi
if [ $FAIL = 0 ]; then echo "real server: all passed"; else echo "real server: FAILED"; exit 1; fi
