# Zero-amplitude excitation candidate

This research branch addresses the audible E prime written at amplitude zero by
SynPhonix VEST and the Hungarian Braille Lite's zero-amplitude OU prime. Two real
register sessions (`REG_vest_e` and `REG_vest_e2`) keep the E prime silent; the
previous model lets it sound while master volume falls over a phoneme.

`amp_zero_sources` clears the voice and noise excitation when an applied R3 write
sets amplitude zero. A phoneme loaded while amplitude is zero keeps those targets
zero. Filters and output state retain their tails. A later nonzero R3 write restores
the held phoneme's source targets and retains the model's ordinary interpolation.
Positive volume changes still use the smooth volume path introduced for Accent.

This is a candidate, not a complete reconstruction of the amplitude scanner.
The recordings do not yet distinguish a mid-phoneme zero write from a zero write
followed by a load. Recovery timing, nonzero transitions and rate dependence need
the prepared register experiment before the final rule is frozen. A source stop
does not imply that the output becomes zero at the same instant: existing filters
continue to ring. No change to how speech sounds is cleared without listening.

`tools/check_amp_zero.py` checks E, OU and S in Python and C after speech and a
settled pause. `AMP_ZERO_BREAK=slew` disables the candidate and must fail all six
zero-prime cases. The normal check and its control are registered in both suites.
`tools/check_amp_steps.py` retains its independent positive-volume ticking guard
and stepped-volume control. `tools/check_native_core.py` covers both values of
the new parameter as part of Python/C parity.

The private review also contains both complete register replays, the Hungarian
voice-table writes and their replay, three Accent DEV comparisons, all 31 table
voices before/after, the source audit and a frozen follow-up session. Changed
waveforms are not automatically improvements; that inventory is for review and
listening. The isolated checkout's first full Windows run was not green: 83 of
189 checks passed. Many failures name missing built add-ons and reference
libraries; the remaining failures have not all been diagnosed. Linux and release
listening remain outstanding. No hold-out recordings were used.
