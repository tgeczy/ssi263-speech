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

The units are the 0.7 add-ons' drivers, unchanged, moved whole by the build into `synthDrivers/_ssi263_unified`:

| Unit | Its driver (source) | Its engine package |
|---|---|---|
| Braille Lite 2000 (en, es) | `nvda/blazie/synthDrivers/blazie.py` | `_ssi263_blazie` (`build_blazie.py`) |
| Speak-Out | `nvda/speakout/synthDrivers/speakout.py` | `_ssi263_speakout` (`build_speakout.py`) |
| Accent SA, Accent-mini | `nvda/accent/synthDrivers/accentmini.py` | `_ssi263_accent` (`build_accent.py`) |
| Mockingboard | `nvda/mockingboard/synthDrivers/mockingboard.py` | `_ssi263_mockingboard` (`build_ssi263.py`; only with its firmware file) |

Each unit's `notifySynth` is set to this driver, so NVDA's speech manager accepts its index and done notifications.

Tests (`nvda/tools`, run by `run_tests.py`), each with must-fail controls: `unified_driver_equiv.py` (every unit
through this driver, byte for byte against its own driver, the notifications naming this driver),
`unified_settings_test.py` (per-firmware memory, load order, Cancel, profiles, the ring's type-only write, a missing
firmware, a unit whose worker fails to boot, under NVDA's own settings order), `unified_plugin_test.py` (updates,
migration and its failures, removal readiness, the panel and ring hooks) and `addon_archive_test.py` (the release
build's archive check, `build_ssi263.py` without `--dev`); `tools/check_uniform.py` reads this add-on for the NVDA
column.

Not built (scope revised, Tomi 2026-10-09): the user firmware folder of design sections 4-5. Every supported unit's
firmware is bundled; the folder comes if a firmware must leave the bundle (then also a download for the Mockingboard).
