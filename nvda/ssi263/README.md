# nvda/ssi263: the 0.8 add-on, every unit in one synthesizer

`ssi263-speech-<version>.nvda-addon` (manifest id `ssi263_speech`, driver `ssi263`, shown in NVDA as
"Votrax SC-02 / SSI-263 (emulated)"). Built by `nvda/build_ssi263.py`; the design is
`investigation/design-0.8-nvda-addon.md` (its revisions after Astra's Reply 161 lead).

| File | What it is |
|---|---|
| `manifest.ini` | The add-on's manifest (with the Braille Lite's stacked-question-mark symbols, `nvda/blazie/locale`). |
| `synthDrivers/ssi263.py` | The driver: one unit at a time by its firmware type, NVDA's speech, settings and notifications passed through; each firmware type's own values in NVDA's config (`fw_<type>_<setting>`), loaded firmware type first. |

The units are the 0.7 add-ons' drivers, unchanged, moved whole by the build into `synthDrivers/_ssi263_unified`:

| Unit | Its driver (source) | Its engine package |
|---|---|---|
| Braille Lite 2000 (en, es) | `nvda/blazie/synthDrivers/blazie.py` | `_ssi263_blazie` (`build_blazie.py`) |
| Speak-Out | `nvda/speakout/synthDrivers/speakout.py` | `_ssi263_speakout` (`build_speakout.py`) |
| Accent SA, Accent-mini | `nvda/accent/synthDrivers/accentmini.py` | `_ssi263_accent` (`build_accent.py`) |
| Mockingboard | `nvda/mockingboard/synthDrivers/mockingboard.py` | `_ssi263_mockingboard` (`build_ssi263.py`; only with its firmware file) |

Each unit's `notifySynth` is set to this driver, so NVDA's speech manager accepts its index and done notifications.

Tests (`nvda/tools`, run by `run_tests.py`): `unified_driver_equiv.py` (every unit through this driver, byte for byte
against its own driver, the notifications naming this driver) and `unified_settings_test.py` (per-firmware memory,
load order, Cancel, profiles, a missing or failing firmware, under NVDA's own settings order), each with must-fail
controls; `tools/check_uniform.py` reads this add-on for the NVDA column.
