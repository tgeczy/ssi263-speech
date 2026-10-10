# sapi/ -- the SSI-263 voices as a SAPI 5 engine

Since 0.7.5 the voices run inside the program that speaks, in C, with no Python and no helper process: the engine DLL
loads `ssi263speech.dll` (the native voices, `src/csrc/voices.h`) from its own folder and the firmware from
`{app}\firmware`.

The voices, one SAPI token each (en-US unless said), when their firmware is in the folder the build is given (a voice
whose files are not there is simply not staged): the Braille Lite 2000 (June 2003) and the Braille Lite 2000 (español,
es-ES), the Speak-Out, the Accent-mini, the Accent SA, and the Mockingboard (Sweet Micro Systems; new in 0.8: Sweet
Micro's own text-to-speech on a 6502, rate, pitch and volume only -- nothing in the settings dialog is its; its
firmware's notice and the 6502's credits, Fake6502 by way of EchoTalk, are in `{app}\licenses`).

| File | What it is |
| --- | --- |
| `ssi263_sapi.cpp`, `.def` | The engine DLL (x86, x64; MSVC `/MT`): COM class, SAPI's fragments and bookmarks, the settings from HKCU, the voices in-process. |
| `ssi_native.c`, `.h` | Loads `ssi263speech.dll` and maps the dialog's settings and SAPI's rate and pitch onto the voice table. Shared by the DLL and `ssi_serve.c`. |
| `ssi_serve.c` | `ssi263_serve.exe`: the old pipe protocol over the native voices, a test and build tool (`--list` and `--files` make the stage's `voices.txt` and firmware). Never installed. |
| `ssi_serve.py` | 0.7.0's Python server over the NVDA drivers. It is no longer shipped and stays as the reference the native voices are held to. |
| `reference_drivers.py` | The tests' reference: `ssi_serve.py` on 0.7.0's Python drivers (`nvda/tools/legacy_drivers.py`, by `SSI263_SAPI_DRIVERS`), never `nvda/dist`'s native ones, checked by `ssi_serve.py --drivers` before each test (control: `SSI263_SAPI_REF_BREAK=dist`). |
| `sapi_harness.cpp` | Drives the development DLL through `ISpTTSEngine` with nothing registered (`build.ps1 -Dev`). |
| `build.ps1` | The stage, `nvda\dist\sapi` (`-Dev`: `nvda\dist\sapi-dev`). Build `python src\csrc\build_ssi263speech.py` first. |
| `installer.iss` | The Inno Setup installer: DLLs, firmware, licences, `voices.txt`, settings. |
| `register.ps1` | The COM class and one token per voice in `voices.txt`, in both registry views (run elevated). |
| `settings.ps1`, `settings_launcher.c` | The settings dialog and its console-free launcher. |
| `test_native.py` | The native voices against `ssi_serve.py`, byte for byte over the wire, 64- and 32-bit; every voice listed on both widths; the Mockingboard, which `ssi_serve.py` never had, held to itself (run_tests). |
| `test_sapi_engine.py` | The development DLL through SAPI's interface against `ssi_serve.py` (the Mockingboard: against `ssi263_serve.exe`), byte for byte (run_tests). |
| `test_serve.py` | The reference server's own checks (run_tests). |
| `test_sapi.ps1`, `test_sapi_settings.ps1` | Through SAPI and System.Speech, for an installed build (machine-wide tokens: SAPI refuses voice tokens under HKCU). |
