# nvda/ssi263: the 0.8 add-on, every unit in one synthesizer

`ssi263-speech-<version>.nvda-addon` (manifest id `ssi263_speech`, driver `ssi263`, shown in NVDA as
"Votrax SC-02 / SSI-263 (emulated)"). Built by `nvda/build_ssi263.py`; the design is
`investigation/design-0.8-nvda-addon.md` (its revisions after Astra's Reply 161 lead).

| File | What it is |
|---|---|
| `manifest.ini` | The add-on's manifest (with the Braille Lite's stacked-question-mark symbols, `nvda/blazie/locale`). |
| `synthDrivers/ssi263.py` | The driver: one unit at a time by its firmware type, NVDA's speech, settings and notifications passed through. Each firmware type's values live only in NVDA's config as `fw_<type>_<setting>` (the plain keys are never read), loaded firmware type first; a unit becomes the current one only once its worker has booted the requested firmware and language. |
| `globalPlugins/ssi263Speech/__init__.py` | The plugin: the settings page and Tools item, the one-time migration with its live switch, and removing the 0.7 add-ons (only once NVDA speaks with this driver and every profile is migrated). |
| `globalPlugins/ssi263Speech/migrate.py` | The migration on raw profiles: each 0.7 section into its types' own keys, the Accent model read through inheritance, every write logged for a rollback. |
| `globalPlugins/ssi263Speech/updates.py` | Check for updates, by hand only, checked against the release's SHA256SUMS.txt. |
| `globalPlugins/ssi263Speech/panel.py` | The Voice panel's refresh after a firmware change. |
| `globalPlugins/ssi263Speech/ring.py` | The settings ring's change also written to the firmware's own key. |
| `globalPlugins/ssi263Speech/data.py` | **SSI-263 speech data** (Tools menu, and a button on the settings page), the shape of outSPOKEN's and Panthera's data dialogs: every firmware type and whether its files are here; for the Super Voice, which no package carries, **Download the Super Voice** (by hand only, asked first) and **Import a file**, on a worker thread, and **Open the folder**. Never on a secure screen. |

The units are the 0.7 add-ons' drivers, unchanged, moved whole by the build into `synthDrivers/_ssi263_unified`:

| Unit | Its driver (source) | Its engine package |
|---|---|---|
| Braille Lite 2000 (en, es) | `nvda/blazie/synthDrivers/blazie.py` | `_ssi263_blazie` (`build_blazie.py`) |
| Speak-Out | `nvda/speakout/synthDrivers/speakout.py` | `_ssi263_speakout` (`build_speakout.py`) |
| Accent-PC, Accent-mini | `nvda/accent/synthDrivers/accentmini.py` | `_ssi263_accent` (`build_accent.py`) |
| Mockingboard | `nvda/mockingboard/synthDrivers/mockingboard.py` | `_ssi263_mockingboard` (`build_ssi263.py`; only with its firmware file) |
| SmarTalk | `nvda/smartalk/synthDrivers/smartalk.py` | `_ssi263_smartalk` (`build_ssi263.py`; only with its S_TALK.EXE) |
| Artic SynPhonix | `nvda/synphonix/synthDrivers/synphonix.py` | `_ssi263_synphonix` (`build_ssi263.py`; only with its VEST.COM, the copy with the serial replaced) |
| Speech Systems Super Voice | `nvda/supervoice/synthDrivers/supervoice.py` | `_ssi263_supervoice` (`build_ssi263.py`; always, never with its program: `ssi263_userfw.py` finds the user's in NVDA's configuration folder) |

Each unit's `notifySynth` is set to this driver, so NVDA's speech manager accepts its index and done notifications.

**Say all and index marks.** A driver reports an index when the audio before it has been played, which is exact
between two texts spoken apart. Say all's lines that end no sentence come as one utterance with an index before
each line, and with "Join phrases" on they go to the unit as one line so that they flow
(`nvda/shared/ssi263_flow.py`). Inside such a line the Braille Lite's indexes are its unit's own index marks
(Ctrl-F, echoed when speech reaches it), exact, on the releases measured for them: the English, Danish, Norwegian
and Swedish ones (the release table's `marks` column; on the Spanish and German units a mark inside a line changes the speech).
The Speak-Out's and the Sounding Board's are their own too (Ctrl-E `[` and a byte, which the box sends back and
the card's driver counts), the Accent-PC's (Ctrl-F, which the unit answers), the Accent-mini's and the SmarTalk's
(Ctrl-F, which their programs count): exact, and the speech is the same with them, but for one pause frame of 8 ms
the Accent-mini puts in front of a sentence whose first word is marked. The Mockingboard's are estimated, by the
index's share of the text, good to a word or so
(`docs/index-marks.md`). With "Join phrases" off every text is a sentence of its own again, and every index exact.

Tests (`nvda/tools`, run by `run_tests.py`), each with must-fail controls: `unified_driver_equiv.py` (every unit
through this driver, byte for byte against its own driver, the notifications naming this driver),
`unified_settings_test.py` (per-firmware memory, load order, Cancel, profiles, the ring's type-only write, a missing
firmware, a unit whose worker fails to boot, under NVDA's own settings order), `unified_plugin_test.py` (updates,
migration and its failures, removal readiness, the panel and ring hooks), `userdata_test.py` (the Super Voice's
download and import: NVDA's configuration folder, the pinned sources and their mirror, the library's check) and
`addon_archive_test.py` (the release build's archive check, `build_ssi263.py` without `--dev`, which refuses the Super
Voice's program by name and content); `tools/check_uniform.py` reads this add-on for the NVDA column.

The user firmware folder of design sections 4-5, built for the one firmware that must stay out of the bundle (0.9.5,
Tomi 2026-11-23): `<NVDA's configuration folder>\ssi263-speech\firmware\` (`%APPDATA%\nvda\ssi263-speech\firmware` for
an installed NVDA), the repository's `firmware/` layout below it -- outside the add-on's folder, so an update keeps it,
and inside NVDA's, so a portable copy carries it and the sign-in screen gets it with the user's settings. The SAPI
engine reads it too (`sapi/dataroot.ps1`). Every other unit's firmware is bundled.
