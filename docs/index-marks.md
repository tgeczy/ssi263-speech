# Index marks

A screen reader marks places in the text it sends to a synthesizer and asks to be told when speech reaches each
one. That is how its cursor follows the speech, how "say all" knows which line is being read and where it was
stopped, and how a word is highlighted while it is spoken. NVDA calls the mark an index, SAPI a bookmark, SSML a
`mark`; Orca puts one before every word.

Several of the units here were built for exactly this and have index marks in their firmware. A voice that uses its
unit's own marks reports each one where the unit itself says speech is: exact. A voice that cannot has the place
estimated, and says so. This page records what each unit has, what was measured, and what the code does today.

## The rule

`src/csrc/marks.h` is the interface; `src/csrc/voices.h` (`ssv_speak_marked`, `ssv_reached`) is where every front
end outside NVDA gets it. Text goes in with its marks, and each block of audio comes back with the marks reached in
it, each with its sample in that block and whether its place is exact or estimated.

- A text with no marks is spoken exactly as before. Nothing is sent or asked for marks' sake.
- A mark never changes which words are spoken. The text is prepared whole, as without marks, and a mark goes to the
  start of a word. One that stands inside a word, or inside an amount of money that is rewritten, moves to its start.
- Marks that fall on the same word are one mark to the unit and are reported together, in their order.
- Every mark is reported exactly once. At the utterance's end the ones still left come with its last block, so no
  caller is left waiting. After a cancel none of the cancelled text's marks is reported.

## What each unit has

Measured on the emulated units, each finding checked a second time independently (2026-11-19). "Used" says whether
a voice here uses it yet.

| Unit | Its own index mark | How it answers | Used |
|---|---|---|---|
| Braille Lite 2000 (Blazie) | Ctrl-F (06h) anywhere in a line | echoes 06h on its serial line when speech reaches it | yes, on the releases below |
| Speak-Out (GW Micro) | Ctrl-E `[` and one byte (05h 5Bh xx) | sends that byte back on its serial line; on at power-up, Ctrl-E `]` 1 / 0 turns it on and off | yes |
| Sounding Board (GW Micro) | the same command | INT 17h AH=02h on the driver's port: how many marks were passed since the last call, and the last one's value | yes |
| Accent SA (Aicom) | Ctrl-F, after ESC Y X and ESC = Y | one 06h per mark on its serial line; off at power-up | yes |
| Accent-mini (Aicom) | Ctrl-F, after ESC Y X | its driver counts the marks passed (INT 17h AH=02h gives the count after ESC Y Z; the host reads it in the driver's memory) | yes |
| SmarTalk (Automated Functions) | Ctrl-F, always on | INT 17h AH=2: the count | yes |
| Mockingboard (Sweet Micro Systems) | none | the host can read which frame is being spoken | estimated |
| Speech Systems Super Voice (1984) | none | the host sees every phoneme written | not a voice yet |

Two things about them that a front end must know:

- A mark is a word boundary on every unit that has one. Inside a word it splits the word and changes the speech.
- Thirteen of the Speak-Out's 256 mark values must not be used (05h, 18h, 19h, BAh to BFh, FAh, FCh, FDh, FEh): 18h
  is taken as its cancel, the others come back as a different byte and change the next word.

Not every unit of the time had them, and not every program used them. A program could also follow speech with no
mark at all, by watching the synthesizer's busy line: speech has reached the end of what was sent when the line
drops. That gives one place per piece of text sent, which is what the drivers here did before, and still do between
two texts spoken apart.

## The Braille Lite: measured release by release

A mark before every word of five texts, against the same text without marks, at rates 0, 50 and 100, with short
pauses off and on, through `blv_speak_marked` on each release whose firmware was at hand (`bl_languages.txt`, the
`marks` column).

**Own marks** (the English releases of June 2003, September 2000 and January 2000, the Danish, the Norwegian and the
Swedish):

- Every mark is echoed in order, each at a place of its own. On the June 2003 English release the place was
  checked against the audio: in the pause before its word, also across a sentence's end and after a cancel.
- Every phoneme and its R2, R3 and R4 are the unmarked text's. At rates 0 and 50 the times between phonemes are the
  same to 1 ms. The text spoken after a marked one is spoken as before.
- A mark adds one pause frame at its word boundary, inside the pause that is there anyway.
- One thing does change. The unit lowers R1 for the last phonemes of a sentence, its final fall. When a mark stands
  exactly where the fall begins, the fall begins one phoneme late. That is the firmware's own doing with its own
  index marks; writing the Ctrl-F before the space, as a word of its own, or at the end of the line before measured
  the same.
- At rate 100 the unit's pauses move by up to 55 ms with marks. Without marks they move by up to 18 ms from one run
  to the next: the unit is busy there.

**Estimated:**

- The Spanish releases: marks change the speech itself. Three sentences were 52 phoneme loads and 4.64 s without
  marks, 59 loads and 5.45 s with a mark before every word; a list of words with commas between them came out the
  same.
- The German releases: a mark changes a phoneme in some sentences.
- The releases that never echo Ctrl-F: the Italian, the Slovak and most of the language releases of 1998 and 1999.
- Any release while it is run ahead of the listener (the experimental setting): its echoes then come ahead of the
  audio.

## The Speak-Out: measured

The box's mark goes at the front of the marked word, after the space before it, where the box takes it for
nothing but a word boundary; its reply is read off the box's serial transmit line, which the emulated board used
to discard (`src/csrc/speakout/so_voice.h`).

- A mark before every word of two texts against none, at rates 0, 50 and 100, with "Join phrases" and the short
  pauses on and off (24 marked utterances): every phoneme load with its R1 to R4 is the unmarked text's, in order.
  The times between phonemes are the same to 2.0 ms; the same text said twice without marks differs by 2.2 ms.
  So a mark changes nothing that could be measured, unlike on the Braille Lite.
- The reply comes with the first chip frame of the word after the mark. Against the audio (short pauses off, so
  that the box pauses at commas and full stops): each mark in the pause before its word, also across a sentence's
  end and after a cancel.
- The box sends one reply a chip interrupt, so marks stacked on one place would run late; the voice gives the box
  one mark a word.
- The Sounding Board is the same front end behind a DOS driver, which reports marks as a count and a last value
  through INT 17h. The host reads those two bytes of the driver after every slice instead of calling it (a call
  would reset them and run the driver's code). The same 24 marked utterances on it: every phoneme load and its
  R1 to R4 the unmarked text's, the times between them the same to 0.1 ms; each mark in the pause before its
  word.

## The Accent SA: measured

A Ctrl-F at the front of the marked word, after the space before it; the unit is told to prompt (ESC Y X, ESC = Y)
once, with the first marked text, since it powers up with marks off (`src/csrc/accentsa/as_voice.h`).

- A mark before every word of two texts against none, at rates 0, 50 and 100 with the intonation full and flat
  (12 marked utterances): every phoneme load with its R1 to R4 is the unmarked text's, in order. At rates 0 and
  50 the times between them are the same (0.0 ms; the same text twice without marks: 0.7 ms).
- At rate 100 a pause between sentences was up to 27 ms longer with marks, and in a 300-word text six of seven
  such pauses were 28 to 66 ms longer: the unit is busy there. Every write was still the same.
- The unit prompts when the word before the mark has ended. Against the audio: each mark in the pause before its
  word, also after a cancel.
- All prompts are the same byte, so the voice counts them. Marks on one word are sent as one: N marks with no
  word between them speak as N-1 carriage returns.
- The voice holds some blocks of audio back before it gives them out (a silence while the unit still works); a
  mark reached in such a block comes out with that audio, at its sample in it.

## The SmarTalk: measured

A Ctrl-F directly after the word before the marked one ("word<06> next"): the TSR takes it for a space, and that
is the one place where it leaves the speech alone at every rate. The text is cut into the same parts as without
marks. The host reads the TSR's own count of marks passed after every slice (`src/csrc/smartalk/st_voice.h`).

- A mark before every word of two texts against none, at rates 0, 50 and 100 with the inflection on and off (12
  marked utterances): every phoneme and its R1 to R4 the unmarked text's, in order; at rates 0 and 50 the times
  between them the same to 0.1 ms. On a fresh unit the audio with a mark before every word is the unmarked audio
  sample for sample.
- At rate 100 phonemes came up to 20 ms apart from the unmarked run. There the TSR also gives each phoneme one of
  two duration settings in turn, starting with one or the other by what was said before, marks or none.
- The count rises as the chip is given the marked word's first phoneme: in a six-word sentence each mark was 0.0
  ms from its word's first phoneme load. The TSR runs its words together, so the audio shows no word starts to
  check against.

## The Accent-mini: measured

A Ctrl-F directly after the last spoken word before the marked one, its punctuation included: where the manual
says to put it ("right after the word to be marked"). The driver counts a mark when the word before it has ended.
The host reads that count in the driver's memory after every slice; it does not make the Status Read call
(`src/csrc/accentmini/am_host.h`). Two things were measured about that call: made while the chip's request is
waiting it makes the driver write the chip itself, which delays the speech; and it gives no count while an option
command sent after the marks waits to be reached. The count in memory rose on time in both cases.

- **Inside a clause** (after a word, with a comma, a bracket or a quote after it or not): every chip write is the
  unmarked text's, in value and time, at rates 0, 50 and 100. On a fresh unit at the default rate the audio is
  the unmarked audio sample for sample (the other rates' audio was not compared). The mark is reported with the pause frame the chip is given after the word: 0.0 ms from it,
  8 to 33 ms before the next word's first phoneme at the default rate.
- **After a clause end** (. ! ? ; :), which is where nearly every index of a say all stands: ONE pause frame of
  8.4 ms is put in front of the next clause, and its first phoneme begins differently: the duration bits of its
  first frame, and in some texts the articulation or inflection bits of one or two frames, all within the first
  20 ms. Every phoneme is the same. What follows is at the same times (to 0.2 ms) or 8.4 ms later, for each
  such mark: both were seen at the default rate, at the fastest rate always the 8.4 ms. Five one-word
  sentences with a mark before each came out 30 ms longer in all (4.89 s). The mark is reported at the head
  of the next clause.
- The mark could go before the punctuation instead, which costs nothing in an ordinary word. It is not used:
  before the full stop of "Dr." it changed the reading and added 0.67 s, and which words the driver takes for
  abbreviations is not known.
- Two marks with nothing spoken between them cost 62 ms (around a dash that stands alone): marks there share one
  Ctrl-F, after the word before the dash.
- A corpus of twelve sentences (abbreviations, a.m., U.S., decimals, money, a time, brackets, quotes, an
  ellipsis, a dash), a mark after every word: every phoneme the same in all of them.
- With ESC P or ESC R printed while a marked text was being spoken the count still rose on time; with two such
  prints, 0.7 s apart, the last three marks came together at the end. Why is not known. The voice prints no
  command while a text is spoken: a capital's pitch goes before its text and is restored after it, and with it
  the marks were where they are without it.
- Not heard by anyone yet: "the same" means the same chip writes and samples.

## The estimate

A mark is put at its share of the text's characters, as a share of the utterance's samples. The length must be
known for that, so the voice is rendered ahead of the caller (eight blocks for each one returned, the first
returned at once), and a mark is reported once the unit is done and the returned audio has reached its place. It is
good to about a word. A mark in the first eighth or so of an utterance can come late, by at most the time the unit
takes to finish ahead of the caller.

## Front ends

- **NVDA add-on.** Say all's lines go to the unit as one line with their indexes
  (`nvda/shared/ssi263_flow.py`). The Braille Lite driver uses the unit's own marks on the releases above, and
  the Speak-Out and Sounding Board driver the box's and the Accent driver the Accent SA's (the audio is fed up to
  a mark's sample, the index reported, then the rest), and the Accent-mini's and the SmarTalk's as well; the
  Mockingboard is estimated. The drivers do not go through `voices.h` yet.
- **speech-dispatcher (Orca).** The module keeps the message's marks, the server's own after each sentence and
  the one Orca puts before every word, and reports each between the audio before it and the audio after it, cut
  at the mark's sample (`src/platforms/speechd/sd_ssi263.c`). The server holds a mark in its queue with the audio
  and reports it when its playback gets there. A PAUSE is still answered as a stop, as before: whether the
  server's pause and resume work with the marks has not been measured. Not tried with Orca itself yet.
- **SAPI 5.** A bookmark inside a text is a mark for the voice; the engine writes the audio up to it, sends the
  bookmark's event with the bytes written as its offset, then writes the rest (`sapi/ssi263_sapi.cpp`). Up to
  0.9.2 such a bookmark was sent after its whole utterance, with an offset guessed from the text: a program that
  takes a bookmark when its event arrives, as NVDA does from 2025.1, was told of every word at the end (JAWS's
  five-word request in the test: up to 1.35 s late). A bookmark at a text's start or end, or between two
  utterances, was in place before and still is. A spelled text's bookmarks are still sent after it. Not tried
  with NVDA or JAWS themselves yet.
- **Android.** A request carries no marks; an engine can tell the framework which part of the text is being
  spoken (`SynthesisCallback.rangeStart`), and TalkBack takes that (its `FailoverTextToSpeech.onRangeStart`). With
  the app's setting "Tell Android which word is being spoken" on, the service marks every word's start and
  reports each word at its frame of the audio (`SsiTtsService.kt`, `ssa_engine.h` `ssa_start_marked`). The setting
  is OFF by default: nobody asks for these marks as NVDA's say all or Orca do, and a mark at every word moves
  some units' pauses (above). Only a unit's own marks are used (`ssv_marks_exact`): the estimate renders the unit
  eight blocks ahead for each one returned, which a phone may not have the speed for, so the estimated voices
  speak as before and report no words. Not tried on a phone or with TalkBack yet.
- **Apple.** Not done, and not needed for VoiceOver. A synthesis audio unit may send markers back to the system
  (`speechSynthesisOutputMetadataBlock`: words, sentences, paragraphs, phonemes, and bookmarks for SSML mark
  tags); Apple's documentation names word highlighting as what depends on them. Another third-party voice that
  works with VoiceOver sends none (read in its source). So our extension sends none either: what is skipped is
  the highlighting of the word being spoken in Speak Screen. What VoiceOver puts in a request's SSML was not
  captured.

## Tests

- `nvda/tools/voice_marks.py`: the Braille Lite voice. The bytes sent; each mark's place against the pauses the
  audio shows, also across a sentence's end and after a cancel; the chip's writes with marks against without. Five
  must-fail controls.
- `nvda/tools/so_marks.py`: the Speak-Out voice, and with `--soundingboard` the Sounding Board, the same cases as
  the Braille Lite's. Four must-fail controls.
- `nvda/tools/as_marks.py`: the Accent SA voice, the same cases. Four must-fail controls.
- `nvda/tools/am_marks.py`: the Accent-mini voice; inside a clause every chip write is held to the unmarked
  text's, after a clause end to the one pause frame. Four must-fail controls.
- `nvda/tools/st_marks.py`: the SmarTalk voice; its marks are placed against the chip's writes, and its parts
  against the unmarked text's. Four must-fail controls.
- `tools/check_ssv_marks.py`: the voice table, on every voice whose firmware is present: no marks is `ssv_speak`,
  every mark once and in order, exact or estimated as it should be; and again with every voice made to
  estimate, the estimate never early. Two controls.
- `nvda/tools/say_all_flow.py`: the NVDA drivers, say all's lines and their indexes.
- `sapi/test_sapi_requests.py`: JAWS's request, a bookmark between every pair of words: each event arrives when
  the bytes written are its offset. Two controls.
- `src/platforms/speechd/test_sd_ssi263.py`: the speech-dispatcher module, playing the server: a mark before every
  word comes back once, in order, with exactly the audio before it that a reference voice given the same marks
  has; a stop ends them; a message of marks alone reports them. Two controls.

Not tested: a live say all in NVDA with the caret watched, and any real unit. The measurements above are of the
emulated units running the original firmware.
