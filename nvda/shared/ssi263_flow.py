# -*- coding: utf-8 -*-
"""Say all's lines as one line to the unit: what the drivers' "Join phrases" does with texts that have only indexes
between them.  Shared by every driver of the add-on; no NVDA module and no unit is imported here.

NVDA holds lines that end no sentence and sends up to ten of them as ONE utterance, an index before each line
(speech/speechWithoutPauses.py; sayAll.MAX_BUFFERED_LINES).  The index is say all's "line reached": it moves the
caret and has NVDA fetch more lines.  Given to a unit as a line each, every line is spoken as a sentence of its own
-- the drop in pitch, the drawn-out ending, a short pause: "1" to "5" on five lines took 1.51 s, as one line 1.18 s
(the Sounding Board; a listener, 2026-11-19: "it pauses after every line.  Most synths don't do that.  It's supposed
to send indexes as indexes, not pause there").  It is not silence that could be cut: between two such lines there are
30 to 40 ms of it, and what sounds like more is the quiet start of the next word (the th of "three", the f of
"four").

So a run of texts with only indexes between them is ONE line to the unit, and each index is reported when playback
reaches its place in that line.

Where that place is.  Several of these units have index marks of their own, made for exactly this (Aicom's manual:
"to track exactly which word in a given text Accent has spoken to"):
  the Braille Lite     Ctrl-F, echoed when speech reaches it, in the middle of a line too (measured).  Its driver
                       uses it (blazie.py): its indexes are the unit's own, exact.
  the Accents          Ctrl-F once ESC YX enables it; reported by the auto-prompt on the serial port or as a count
                       in the status read (the manual, 3.2.4).  NOT USED YET.
  the Sounding Board   Ctrl-E [ x; the markers crossed are read back through INT 17h or 14h (its Appendix A).  The
                       Speak-Out is of the same family.  NOT USED YET.
  the Mockingboard     none: it was not a screen reader's synthesizer.
  the SmarTalk         not looked up.
For a driver that does not use its unit's marks, and for a unit that has none, play_line below ESTIMATES the
place: the index's share of the line's characters, of the line's audio, good to a word or so.  The line's length
is known early because the unit is run ahead of the player (the units render some twenty times faster than they
speak): AHEAD blocks rendered for each one played.

A pitch command (a capital) still keeps two texts apart, and so does "Join phrases" off: then every text is a
sentence of its own and a driver waits for the unit to fall silent before the next, as screen readers did with
synthesizers that had no marks at all.

nvda/tools/say_all_flow.py holds each driver to it.  Python 3.7 to 3.13.
"""

AHEAD = 8                         # blocks rendered for each one played


def lines(items):
    """a driver's items -- (kind, value) -- with each run of texts and indexes that holds two texts or more as one
    ("line", the run); the others as they are"""
    out, run = [], []

    def close():
        if sum(1 for kind, _value in run if kind == "text") > 1:
            out.append(("line", list(run)))
        else:
            out.extend(run)
        del run[:]

    for kind, value in items:
        if kind in ("text", "index"):
            run.append((kind, value))
        else:
            close()
            out.append((kind, value))
    close()
    return out


def line_text(run, prepare):
    """a line's run as the unit gets it: (its texts, each as prepare gives it and none empty; its indexes as (the
    characters of the line before it, the index); the characters in all, a space after each text)"""
    parts, marks, chars = [], [], 0
    for kind, value in run:
        if kind == "index":
            marks.append((chars, value))
            continue
        text = prepare(value)
        if text:
            parts.append(text)
            chars += len(text) + 1
    return parts, marks, chars


def play_line(render, marks, chars, feed, notify, cancelled):
    """The line has been said to the unit: its audio to the player, each index reported at its place.  render: the
    unit's next block, (PCM bytes, done); feed, notify: the driver's player and its index callback; cancelled():
    stop.  marks is emptied as its indexes are reported; what is left when this returns was not reached."""
    blocks, total, fed, done = [], 0, 0, False
    while not cancelled():
        ahead = 0
        while not done and ahead < AHEAD:        # the unit ahead of the player: the line's length is known early
            pcm, done = render()
            ahead += 1
            if pcm:
                blocks.append(pcm)
                total += len(pcm) // 2
        # an index before the first text at once; the others when playback has reached their place, which is
        # known once the whole line is rendered
        while marks and (marks[0][0] == 0 or (done and marks[0][0] * total <= fed * chars)):
            notify(marks.pop(0)[1])
        if blocks:
            pcm = blocks.pop(0)
            if not cancelled():
                feed(pcm)
            fed += len(pcm) // 2
        elif done:
            break
