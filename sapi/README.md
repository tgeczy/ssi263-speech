# sapi/ -- the SSI-263 voices as a SAPI 5 engine

Since 0.7.5 the voices run inside the program that speaks, in C, with no Python and no helper process: the engine DLL
loads `ssi263speech.dll` (the native voices, `src/csrc/voices.h`) from its own folder and the firmware from
`{app}\firmware`.

The voices, one SAPI token each (en-US unless said), when their firmware is in the folder the build is given (a voice
whose files are not there is simply not staged): the Braille Lite 2000 (June 2003) and the Braille Lite 2000 (Spanish,
es-ES), the Speak-Out, the Accent-mini, the Accent-PC, and the Mockingboard in its two versions (Sweet Micro Systems;
new in 0.8: Sweet Micro's own text-to-speech on a 6502 -- "Mockingboard", 1.1 from the Developers Toolkit,
`mockingboard-tts-1.1.bin`, and "Mockingboard, early", Mockingboard disk 1's, `mockingboard-tts-early.bin`, each staged
when its file is there; rate, pitch and volume only -- nothing in the settings dialog is theirs; the firmware's notice
and the 6502's credits, Fake6502 by way of EchoTalk, are in `{app}\licenses`); and Automated Functions' SmarTalk,
its own DOS text-to-speech `S_TALK.EXE` (in the repository, staged with its notice beside it): rate, pitch and volume
are SAPI's, its inflection follows the dialog's Accent intonation (monotone below 50), its tone and number words are its
own.

**Speech Systems' Super Voice (0.9.5) is the one voice no package carries** (`voices.h` `SSV_USER_DATA`): the settings
program downloads its disk from the Color Computer Archive or imports a file you have, keeps only its program
(`SUPERTOK.BIN`, read by the library's own `sv_firmware_from`), and registers its token with the folder it is in
(`DataPath`); the engine speaks it from there through a bank of its own, and refuses the token (SetObjectToken,
`ERROR_FILE_NOT_FOUND`) when the program has gone, so the screen reader takes another voice. Where that folder is,
Panthera SAPI's system, ported (`dataroot.ps1`): the folder you chose (**Data location**), the machine's
(`HKLM\SOFTWARE\SSI-263 SAPI\DataPath`, both registry views), NVDA's own (`%APPDATA%\nvda\ssi263-speech\firmware`, where
the add-on downloads it: nothing copied twice), `%ProgramData%\ssi263-speech\firmware`, then yours
(`%APPDATA%\ssi263-speech\firmware`) -- a folder holding a voice's file first, one that merely exists second. **Move
voices for all users** moves your own folder (never NVDA's, never one you chose) to `%ProgramData%`, its permissions
reset to Users read, SYSTEM and the Administrators full. Setup never touches these folders. The build refuses to stage
the program or the disk, by name and sha256.

| File | What it is |
| --- | --- |
| `ssi263_sapi.cpp`, `.def` | The engine DLL (x86, x64; MSVC `/MT`): COM class, SAPI's fragments and bookmarks, the settings from HKCU, the voices in-process. |
| `ssi_native.c`, `.h` | Loads `ssi263speech.dll` and maps the dialog's settings and SAPI's rate and pitch onto the voice table. Shared by the DLL and `ssi_serve.c`. |
| `ssi_serve.c` | `ssi263_serve.exe`: the old pipe protocol over the native voices, a test and build tool (`--list`, `--files` and `--user-voices` make the stage's `voices.txt` and firmware; `--files` never names a voice no package carries). Never installed. |
| `ssi_serve.py` | 0.7.0's Python server over the NVDA drivers. It is no longer shipped and stays as the reference the native voices are held to. |
| `reference_drivers.py` | The tests' reference: `ssi_serve.py` on 0.7.0's Python drivers (`nvda/tools/legacy_drivers.py`, by `SSI263_SAPI_DRIVERS`), never `nvda/dist`'s native ones, checked by `ssi_serve.py --drivers` before each test (control: `SSI263_SAPI_REF_BREAK=dist`). |
| `sapi_harness.cpp` | Drives the development DLL through `ISpTTSEngine` with nothing registered (`build.ps1 -Dev`): its own engine site, which can take part of a Write or none, ask for a skip or an abort, and notes when each bookmark's event arrives. |
| `build.ps1` | The stage, `nvda\dist\sapi` (`-Dev`: `nvda\dist\sapi-dev`). Build `python src\csrc\build_ssi263speech.py` first. |
| `installer.iss` | The Inno Setup installer: DLLs, firmware, licences, `voices.txt`, settings. |
| `register.ps1` | The COM class and one token per voice in `voices.txt`, in both registry views (run elevated); a voice no package carries only where a data folder holds its file, its token carrying that folder; the elevated steps of the data folders (`-ImportFile`, `-MigrateData`). |
| `settings.ps1`, `settings_launcher.c` | The settings dialog and its console-free launcher; its **Speech data** group (download, import, data location, move for all users, open the folder). |
| `dataroot.ps1` | The data folders (Panthera's system): the root's order and two passes, the move's plan, the Super Voice's program read through the library and its disk downloaded and checked. Dot-sourced by both scripts above. |
| `test_sapi_data.py` | The data folders through `settings.ps1`'s own switches, registry and folders isolated, no network: the order, the two passes, the plans, the program from itself, a disk, a zip and a download. `SSI263_SAPI_DATA_BREAK=onepass`, `movenvda` and `nozip` are its must-fail controls (run_tests). |
| `test_sapi_datapath.py` | The engine and a token with a `DataPath` (the harness's), x64 and x86, under a name outside the code page: the voice table's samples, and the token refused without its program (`--control`: taken unchecked). |
| `test_stage_files.py` | The stage's firmware list never names the Super Voice's program (control: `SSI263_SERVE_BREAK=userdata`). |
| `test_sapi_userdata.ps1` | Run elevated, by hand (it changes the machine's voice tokens, then puts them back): register.ps1's elevated steps for real and the Super Voice through real SAPI, 64- and 32-bit -- imported and registered with its DataPath, spoken, refused without its program, moved for all users (the program still readable: each file's ACL inherited from the root's, never the empty one a grant down the tree leaves), spoken from there, and the stage registered again without it. `-Control`: the permissions as Panthera's move sets them, which must fail. |
| `updates.ps1` | The dialog's Check for updates, by hand only: the NVDA add-on's rules (`updates.py`) -- drafts and prereleases skipped, the version from the installer's own name, the newest across the releases -- and the download checked against its release's SHA256SUMS.txt before it runs (fails closed). |
| `test_updates.ps1` | Its tests on GitHub's shapes, no network; `SSI263_UPDATES_BREAK=drafts` and `=hash` are its must-fail controls (run_tests). |
| `test_native.py` | The native voices against `ssi_serve.py`, byte for byte over the wire, 64- and 32-bit; every voice listed on both widths; the Mockingboard (1.1 and early), which `ssi_serve.py` never had, held to itself (run_tests). |
| `test_sapi_engine.py` | The development DLL through SAPI's interface against `ssi_serve.py` (both Mockingboards: against `ssi263_serve.exe`), byte for byte (run_tests). |
| `test_sapi_requests.py` | What SAPI asks beyond plain text, the development DLL held to itself: partial and refused writes (Windows 7), the program's volume, a fragment's rate, volume and pitch, `<spell>`, a skip request, the deadline, and when bookmarks arrive. `SSI263_SAPI_REQUESTS_BREAK=partial-write`, `deadline-wall`, `markup`, `spell`, `skip` and `marks-end` are its must-fail controls (run_tests). |
| `test_serve.py` | The reference server's own checks (run_tests). |
| `test_sapi.ps1`, `test_sapi_settings.ps1` | Through SAPI and System.Speech, for an installed build (machine-wide tokens: SAPI refuses voice tokens under HKCU). |
