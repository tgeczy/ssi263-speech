# -*- coding: utf-8 -*-
"""The first audio of an utterance, as NVDA's player gets it.  Shared by every driver of the add-on; no NVDA module
and no unit is imported here.

A listener, 2026-11-20 (the Sounding Board at rate 80, "blank" read again and again): "every so often, there's a pop
at the beginning".  His recording: 7 of 22 begin with a blip of 2 ms, then some 14 ms of silence, then the word.

The voices give their audio in 30 ms blocks and trim the silence before a word: the first block a driver feeds is
10 ms of lead-in and whatever is left of the block the sound began in -- in 33 of 60 such utterances 0 to 3 ms of
it.  NVDA 2025 and later trim leading silence themselves (nvdaHelper/local/wasapi.cpp: everything before the first
sample over 1/1024 of full scale goes), so the lead-in is gone, the stream starts on that sliver and has nothing
more until the next block comes: the blip, the gap, the word.

FirstFeed stands between a driver and its player.  From a stop() or an idle() on -- where NVDA starts trimming
again -- it holds what the driver feeds until there are MIN_MS of sound in it, counted from the first sample over
NVDA's threshold, and feeds that as one piece; after that every feed goes straight through.  A feed that carries a
callback (an index reached, the utterance's end) lets go of what is held, with the callback on it: a callback must
never wait for audio that may not come.  Everything else is the player's.

An utterance that feeds no audio at all (issue #32: text NVDA left as spaces, an index alone).  NVDA's WASAPI player
calls an onDone only from a later feed() or sync(), and sync() does nothing while the stream is stopped -- as it is
after every stop(), and once NVDA's 10 s idle check has closed it.  A driver gives its indexes as feed(b"", onDone),
so with no audio behind them nothing ever started the stream, the indexes were never called, and NVDA, waiting for
the utterance's last index, said nothing more until the next key press.  So when callbacks have gone down since a
stop or an idle with no audio at all, FirstFeed gives the player one silent sample before its idle() (and before an
empty feed, a driver's way of having its callbacks serviced): that starts the stream, and the callbacks are called.
An utterance with any audio is not touched.
"""

MIN_MS = 30.0                     # of sound in an utterance's first feed: a block
THRESHOLD = 32                    # NVDA's 1/1024 of full scale, in 16-bit samples
SILENCE = b"\x00\x00"             # one sample of 16-bit mono: enough to start a stopped stream
HOLD = True                       # (the tests' controls take these away)
KICK = True


def sound_bytes(pcm):
    """how many bytes of 16-bit mono PCM lie at and after its first sample over NVDA's threshold"""
    view = memoryview(pcm).cast("h") if len(pcm) % 2 == 0 else memoryview(pcm[:len(pcm) - 1]).cast("h")
    for k, v in enumerate(view):
        if v > THRESHOLD or v < -THRESHOLD:
            return 2 * (len(view) - k)
    return 0



class FirstFeed(object):
    """A player whose first feed after a stop or an idle is never a sliver (the module's text)."""

    def __init__(self, player, sample_rate):
        self._p = player
        self._need = 2 * int(sample_rate * MIN_MS / 1000.0)
        self._held = b""
        self._first = True
        self._started = False                     # audio has gone down since the last stop or idle
        self._owed = False                        # ... and callbacks have, with none behind them

    def __getattr__(self, name):                  # close, pause, sync, and whatever a test's stand-in has
        return getattr(self._p, name)

    def _down(self, data, onDone=None, **kw):
        """everything that reaches the player goes through here"""
        if data:
            self._started = True
        elif onDone is not None and not self._started:
            self._owed = True
        if onDone is not None or kw:
            return self._p.feed(data, onDone=onDone, **kw)
        return self._p.feed(data)

    def _kick(self):
        """callbacks given to a stream that nothing has started: start it, so they are called (issue #32)"""
        if KICK and self._owed and not self._started:
            self._owed = False
            self._down(SILENCE)

    def _let_go(self, onDone=None):
        held, self._held = self._held, b""
        if held or onDone is not None:
            self._down(held, onDone)

    def feed(self, data, onDone=None, **kw):
        data = bytes(data or b"")
        if not data and onDone is None and not kw:
            self._kick()                          # an empty feed: the driver having its callbacks serviced
            return self._p.feed(data)
        if not (HOLD and self._first):
            return self._down(data, onDone, **kw)
        self._held += data
        if onDone is not None:                    # a callback does not wait
            self._let_go(onDone)
            return None
        sound = sound_bytes(self._held)
        if sound >= self._need:
            self._first = False
            self._let_go()
        elif len(self._held) >= 10 * self._need:  # a long quiet stretch (a unit's idle audio): not kept back
            self._first = sound == 0
            self._let_go()
        return None

    def stop(self):
        self._held = b""                          # cancelled: what was held is not to be heard
        self._first = True
        self._started = self._owed = False
        return self._p.stop()

    def idle(self):
        self._let_go()                            # a short utterance: all of it, now
        self._kick()
        self._first = True
        self._started = self._owed = False
        return self._p.idle()
