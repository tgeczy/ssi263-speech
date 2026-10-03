"""A stand-in for NVDA 2023.2+'s nvwave.WavePlayer on WASAPI, faithful where the drivers' completion depends on it.

fake_nvda_driver_test.py's FakePlayer calls onDone inside feed(), at once.  The real player (NVDA 2026.2:
source/nvwave.py over nvdaHelper/local/wasapi.cpp) does not, and every test on FakePlayer is blind to that:
- a 400 ms device buffer and a play clock that runs in real time (here / SIM_SPEED) while the stream plays;
- feed() blocks while the buffer is more than half full; a stop() from another thread wakes it, and it returns;
- an onDone fires only when playback has passed the end of the audio fed before it, and ONLY from inside a later
  feed() or sync() call, on the calling thread (WasapiPlayer::maybeFireCallback); nothing fires it on its own;
- sync() returns at once unless the stream is playing, so an onDone fed to a stopped stream (after stop(), or before
  any audio since) waits for the next feed of audio;
- stop() marks the stream "stopping" (the next feed resets it: ids from 0, the pending onDones dropped) and drops
  the Python side's callbacks; pause(True) stops the clock, pause(False) restarts it, both only while playing;
- idle() is sync() (nvwave.WavePlayer.idle; WASAPI's own idle runs only from NVDA's 10 s idle check, idle_check()).

    from wasapi_player import WasapiPlayer
"""
import threading
import time

BUFFER_S = 0.4                      # wasapi.cpp BUFFER_MS


class WasapiPlayer:
    def __init__(self, *a, samplesPerSec=44100, speed=1.0, **k):
        self.rate = samplesPerSec
        self.speed = speed
        self.buf = int(BUFFER_S * self.rate)
        self.cond = threading.Condition()
        self.state = "stopped"              # stopped | playing | stopping (wasapi.cpp PlayState)
        self.running = False                # the client started and not paused: the clock runs
        self.base = 0.0                     # frames played up to t0
        self.t0 = 0.0
        self.sent = 0
        self.next_id = 0
        self.ends = []                      # (id, end in ms) -- feedEnds
        self.callbacks = {}                 # nvwave's _doneCallbacks
        self.chunks, self.events, self.lock = [], [], threading.Lock()
        self.pace = False                   # FakePlayer's switch; this one is always paced

    # ---- the clock ----------------------------------------------------------------------------------------------
    def _pos(self):
        if self.running:
            p = self.base + (time.perf_counter() - self.t0) * self.rate * self.speed
        else:
            p = self.base
        return min(p, self.sent)            # nothing plays past the audio written

    def _ms(self, frames):
        return int(frames * 1000 // self.rate)

    def _run(self, on):
        if on and not self.running:
            self.t0 = time.perf_counter()
            self.running = True
        elif not on and self.running:
            self.base = self._pos()
            self.running = False

    def _complete_stop(self):
        self.next_id = 0
        self.sent = 0
        self.base = 0.0
        self.running = False
        self.ends = []
        self.state = "stopped"

    def _due(self):
        """maybeFireCallback's ids, taken under the lock; called back after it"""
        pos = self._ms(self._pos())
        due = [i for i, e in self.ends if pos >= e]
        self.ends = [(i, e) for i, e in self.ends if pos < e]
        return due

    def _fire(self, ids):
        for i in ids:
            cb = self.callbacks.pop(i, None)
            if cb:
                cb()

    def _wait(self, max_s):
        """waitUntilNeeded: until the next onDone is due or max_s, or a stop wakes it"""
        if self.ends:
            nxt = (self.ends[0][1] - self._ms(self._pos())) / 1000.0
            max_s = min(max_s, max(nxt, 0.0))
        self.cond.wait(max(max_s / self.speed, 0.0005))

    def keep(self, data):
        """what chunks holds of a feed (a test may want it decoded)"""
        return bytes(data)

    # ---- nvwave.WavePlayer --------------------------------------------------------------------------------------
    def feed(self, data, size=None, onDone=None):
        frames = (size if size is not None else len(data)) // 2
        if data:
            with self.lock:
                self.chunks.append(self.keep(data))
        fid = 0                             # the c_uint stays 0 when the C++ feed returns early
        stopped_out = False
        with self.cond:
            if self.state == "stopping":
                self._complete_stop()
            while frames > 0:
                if self.state == "stopping":
                    self._complete_stop()
                    stopped_out = True
                    break
                pad = self.sent - self._pos()
                if pad > self.buf / 2:
                    self._wait((pad - self.buf / 2) / self.rate)
                    if self.state == "stopping":
                        self._complete_stop()
                        stopped_out = True
                        break
                    pad = self.sent - self._pos()
                send = int(min(frames, self.buf - pad))
                if send <= 0:
                    continue
                self.sent += send
                if self.state == "stopped":
                    self.state = "playing"
                    self._run(True)
                due = self._due()
                self.cond.release()
                try:
                    self._fire(due)
                finally:
                    self.cond.acquire()
                frames -= send
            if not stopped_out:
                due = self._due() if self.state == "playing" else []
                if onDone:
                    fid = self.next_id
                    self.next_id += 1
                    self.ends.append((fid, self._ms(self.sent)))
            else:
                due = []
        self._fire(due)
        if onDone:
            self.callbacks[fid] = onDone
            with self.lock:
                self.events.append(("feed-done", fid))

    def sync(self):
        with self.cond:
            sent_ms = self._ms(self.sent)
            while self._ms(self._pos()) < sent_ms:
                if self.state != "playing":
                    return
                due = self._due()
                if due:
                    self.cond.release()
                    try:
                        self._fire(due)
                    finally:
                        self.cond.acquire()
                    continue
                self._wait((sent_ms - self._ms(self._pos())) / 1000.0)
            due = self._due() if self.state == "playing" else []
        self._fire(due)

    def idle(self):
        self.sync()

    def stop(self):
        with self.cond:
            self._run(False)
            self.state = "stopping"
            self.cond.notify_all()
        self.callbacks = {}
        with self.lock:
            self.events.append(("stop", 0))

    def pause(self, switch):
        with self.cond:
            if self.state == "playing":
                self._run(not switch)
            self.cond.notify_all()

    def close(self):
        self.stop()

    def idle_check(self):
        """NVDA's _idleCheck, 10 s after the last feed: wasPlay_idle -- sync, stop, reset"""
        self.sync()
        with self.cond:
            self._run(False)
            self._complete_stop()

    def pending(self):
        """onDones fed and not yet called"""
        with self.cond:
            return [i for i, e in self.ends]
