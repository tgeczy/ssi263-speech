# Zero-amplitude excitation candidate

This research branch addresses the audible E prime written at amplitude zero by
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

This is a candidate, not a complete reconstruction of the amplitude scanner.
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

The narrowed candidate passes both VEST replays, Hungarian and Slovak live prime
checks, Python/C parity and the no-tick guard. The private review includes three
Accent DEV comparisons and all 31 compiled voice-table entries before/after.
Changed waveforms are not automatically improvements; listening remains required.

The English Braille Lite golden audio still changes: its trace includes E loads
at amplitude zero, not only a cancel's volume write. Disabling the fix for just
those E loads restores the pipe golden exactly. All 3,709 writes match either way;
the Spanish goldens match in both architectures. Existing golden files are kept
unchanged pending review and listening of the intended audio difference.

The isolated Windows full-suite attempt passed 211 of 328 checks. It lacks the
complete add-on/reference-build environment; four English golden audio failures
are explained above, while remaining failures are not all classified. This is not
a green release run. Linux and installed-app listening remain outstanding. No
hold-out recordings were used.
