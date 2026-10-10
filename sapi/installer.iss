; SSI-263 SAPI -- the Braille Lite 2000 (English and Spanish), the Speak-Out, the Accent-mini, the Accent SA and the
; Mockingboard as SAPI 5 voices, for any Windows screen reader or program that speaks through SAPI.
;
; It carries the engine DLLs, the native voices beside each (ssi263speech.dll: the SSI-263 chip model, the emulated
; units and each NVDA driver's text preparation, in C, run inside the program that speaks) and each device's own
; firmware.  No Python and no helper process.  The firmware is not ours: it is here, as in the add-ons, so these
; machines can talk again, and it will come down if its rights holders ask.  A fork of outspoken-nvda's
; sapi/installer.iss (panthera-speech's).
;
; Build:  python src\csrc\build_ssi263speech.py
;         powershell -ExecutionPolicy Bypass -File .\sapi\build.ps1
;         ISCC .\sapi\installer.iss
#ifndef StageDir
#define StageDir "..\nvda\dist\sapi"
#endif
#define AppVer "0.8.0"

[Setup]
AppId={{7C3E91A2-5B64-4D0F-9E28-A61D3F84C7B5}
AppName=SSI-263 SAPI voices
AppVersion={#AppVer}
AppPublisher=SSI-263 speech project
AppSupportURL=https://github.com/tgeczy/ssi263-speech
DefaultDirName={autopf}\SSI-263 SAPI
; 32- and 64-bit Windows (ARM64 through its x64 emulation): the x86 engine everywhere, the x64 one on a 64-bit Windows
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
OutputDir={#StageDir}\out
OutputBaseFilename=ssi263-sapi-{#AppVer}-setup
DisableProgramGroupPage=yes
UninstallDisplayName=SSI-263 SAPI voices {#AppVer}

[Files]
Source: "{#StageDir}\x86\ssi263_sapi.dll"; DestDir: "{app}\x86"; Flags: ignoreversion
Source: "{#StageDir}\x86\ssi263speech.dll"; DestDir: "{app}\x86"; Flags: ignoreversion
Source: "{#StageDir}\x64\ssi263_sapi.dll"; DestDir: "{app}\x64"; Flags: ignoreversion; Check: Is64BitInstallMode
Source: "{#StageDir}\x64\ssi263speech.dll"; DestDir: "{app}\x64"; Flags: ignoreversion; Check: Is64BitInstallMode
Source: "{#StageDir}\voices.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\register.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\settings.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\settings.cmd"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\ssi263_settings.exe"; DestDir: "{app}"; Flags: ignoreversion; Check: Is64BitInstallMode
Source: "{#StageDir}\ssi263_settings_x86.exe"; DestDir: "{app}"; DestName: "ssi263_settings.exe"; Flags: ignoreversion; Check: not Is64BitInstallMode
Source: "{#StageDir}\firmware\*"; DestDir: "{app}\firmware"; Flags: recursesubdirs ignoreversion
Source: "{#StageDir}\licenses\*"; DestDir: "{app}\licenses"; Flags: recursesubdirs ignoreversion

[InstallDelete]
; the firmware is replaced whole: a file a voice no longer reads is not left behind
Type: filesandordirs; Name: "{app}\firmware"
; 0.7.0's Python server, its interpreter and the NVDA driver files it ran: gone with the upgrade
Type: filesandordirs; Name: "{app}\python"
Type: filesandordirs; Name: "{app}\synthDrivers"
Type: files; Name: "{app}\ssi_serve.py"

[Icons]
; the launcher rather than the batch file: a GUI-subsystem program creates no console, so nothing flashes or
; steals focus before the dialog appears
Name: "{autoprograms}\SSI-263 SAPI settings"; Filename: "{app}\ssi263_settings.exe"; WorkingDir: "{app}"

[Run]
; regsvr32 for both registry views, then one token per voice in voices.txt.  Every install re-registers: these
; voices carry their firmware, so there is no data folder or choice of voices to preserve.
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\register.ps1"" -Register"; StatusMsg: "Registering the SSI-263 voices..."; Flags: runhidden
Filename: "{app}\ssi263_settings.exe"; Description: "Open SSI-263 SAPI settings"; Flags: postinstall nowait skipifsilent

[UninstallRun]
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\register.ps1"" -Unregister"; RunOnceId: "UnregisterSsi263"; Flags: runhidden

[Code]
{ An upgrade from 0.7.0: a speech program may still hold its server (python.exe) and the Braille Lite emulator
  (bns_live.exe) open from the install folder.  Stop those, and only those, so their files can be removed.  The
  engine itself now runs inside the speech programs: one holding its DLLs is Setup's to ask about (Restart Manager). }
procedure StopOurProcesses;
var
  Code: Integer;
begin
  Exec('powershell.exe', '-NoProfile -ExecutionPolicy Bypass -Command "Get-Process python,bns_live -ErrorAction SilentlyContinue | Where-Object { $_.Path -like ''' + ExpandConstant('{app}') + '\*'' } | Stop-Process -Force"',
       '', SW_HIDE, ewWaitUntilTerminated, Code);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  StopOurProcesses;
  Result := '';
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    StopOurProcesses;
end;
