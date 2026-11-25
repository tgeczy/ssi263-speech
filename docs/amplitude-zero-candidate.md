# Zero-amplitude excitation (0.9.6)

This change addresses the audible E prime written at amplitude zero by
SynPhonix VEST and the Hungarian Braille Lite's zero-amplitude OU prime. Two real
register sessions (`REG_vest_e` and `REG_vest_e2`) keep the E prime silent; the
previous model lets it sound while master volume falls over a phoneme.

`amp_zero_sources` clears voice and noise excitation when a phoneme is **loaded**
while R3 amplitude is zero. Filters and output state retain their tails. The model
remembers that load, so a later positive R3 write restores only the source targets
which that load suppressed. Ordinary writes to an already sounding phoneme keep
the previous interpolation, including zero-and-rise writes without a reload.
The earlier candidate's write-time source clear and blanket precharge mute have
been removed. Positive volume changes retain the smooth path introduced for Accent.

It is not a complete reconstruction of the amplitude scanner.
The volume follow-up establishes that R3 alone can silence and restore sound, but
does not validate an instantaneous current-source clear. Its measured ordinary
transition width also does not justify simply slowing the current ramp: that
experiment added audible waver in the Accent DEV listening test. Neither change is
included here. Recovery uses existing interpolation; its exact hardware timing is
not identified. No change to how speech sounds is cleared without listening.

`tools/check_amp_zero.py` checks E, OU and S in Python and C after speech and a
settled pause. `AMP_ZERO_BREAK=slew` disables the candidate and must fail all six
zero-prime cases. It also checks that positive R3 restores the held sound without
R0. `--scope` requires sample-identical old/new output for zero-and-rise writes
without loads; `AMP_ZERO_BREAK=reload` inserts an unwanted load and must fail that
guard. The checks and controls are registered in both suites.
`tools/check_amp_steps.py` retains its independent positive-volume ticking guard
and stepped-volume control. `tools/check_native_core.py` covers both values of
the new parameter as part of Python/C parity.

The change passes both VEST replays, Hungarian and Slovak live prime checks,
Python/C parity and the no-tick guard. Every voice-table voice was rendered old and
new: the removed leading prime accounts for each changed duration (the SynPhonix
E; the Hungarian, Slovak, Arabic, Croatian, Czech, Greek, Hebrew, Polish, Turkish
and Russian Braille Lites, which write OU at zero before a word; smaller on
Danish, Norwegian, Swedish, the Speak-Out, the Sounding Board, the Accents and
English). Tomi approved it by ear on that set.

The English Braille Lite golden audio changes: its trace includes E loads at
amplitude zero, not only a cancel's volume write. Disabling the fix for just those
E loads restores the pipe golden exactly. All 3,709 writes match either way; the
Spanish goldens match in both architectures. The two English golden fingerprints
were updated after the listening approval, the writes unchanged.

Verified in the fully built tree: the Windows suite (778 of 778, every control) and
the Linux suite on x86-64 and aarch64 (every check passed; the two tools above are
plain Python, as the Linux suite runs on boxes without numpy). No hold-out
recordings were used.
