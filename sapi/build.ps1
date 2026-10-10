param([string]$Stage = "", [switch]$Dev)
# Stage the SSI-263 SAPI engine: both DLL bitnesses, each beside the native voices of its width (ssi263speech.dll),
# the firmware those voices need, their licences, and the voice list the installer registers.  No Python, no pipe
# server, no NVDA driver files: the voices run in the caller's process (ssi263_sapi.cpp).  Template: outspoken-nvda's
# sapi/build.ps1 (panthera-speech's).
#
# First: python src\csrc\build_ssi263speech.py  (build\win\{x86,x64}\ssi263speech.dll and the serve host that lists
# the voices; a release build of the same exports may stand in its place).
#
# The stage lands in nvda\dist\sapi: it carries the firmware, so it is never committed.  -Dev builds the development
# engine instead (its own COM class and settings key, the tests' hooks: sapi\test_sapi_dev.ps1) into
# nvda\dist\sapi-dev; it is never installed.
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
if (!$Stage) { $Stage = Join-Path $repo $(if ($Dev) { "nvda\dist\sapi-dev" } else { "nvda\dist\sapi" }) }
$pf86 = ${env:ProgramFiles(x86)}
$msvc = Get-ChildItem (Join-Path $pf86 "Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC") -Directory | Sort-Object Name | Select-Object -Last 1
$sdk = Get-ChildItem (Join-Path $pf86 "Windows Kits\10\Include") -Directory | Sort-Object Name | Select-Object -Last 1
if (!$msvc -or !$sdk) { throw "MSVC Build Tools and the Windows SDK are required" }
# Fresh every time: a stage that is only ever added to keeps whatever an earlier build left in it (0.7.0's python\
# and synthDrivers\ above all).
if (Test-Path $Stage) { Get-ChildItem $Stage -Exclude "out" | Remove-Item -Recurse -Force }
New-Item -ItemType Directory -Force $Stage,(Join-Path $Stage "x86"),(Join-Path $Stage "x64") | Out-Null
$defs = @("/DUNICODE", "/D_UNICODE")
if ($Dev) { $defs += "/DSSI263_SAPI_DEV" }
foreach ($arch in "x86","x64") {
  $cl = Join-Path $msvc.FullName "bin\Hostx64\$arch\cl.exe"
  $out = Join-Path $Stage $arch
  # /MT: the static CRT, so the DLL needs no VC++ redistributable (dumpbin below)
  & $cl /nologo /EHsc /O2 /MT /LD $defs "/I$($msvc.FullName)\include" "/I$($sdk.FullName)\um" "/I$($sdk.FullName)\shared" "/I$($sdk.FullName)\ucrt" (Join-Path $PSScriptRoot "ssi263_sapi.cpp") (Join-Path $PSScriptRoot "ssi_native.c") "/Fe$out\ssi263_sapi.dll" "/Fo$out\" /link "/DEF:$PSScriptRoot\ssi263_sapi.def" "/LIBPATH:$($msvc.FullName)\lib\$arch" "/LIBPATH:$($sdk.Parent.Parent.FullName)\Lib\$($sdk.Name)\um\$arch" "/LIBPATH:$($sdk.Parent.Parent.FullName)\Lib\$($sdk.Name)\ucrt\$arch" sapi.lib ole32.lib advapi32.lib shell32.lib
  if ($LASTEXITCODE) { throw "$arch SAPI DLL build failed ($LASTEXITCODE)" }
  if ($Dev) {
    # the engine driven as SAPI drives it, with no registration (sapi_harness.cpp, for sapi\test_sapi_engine.py)
    & $cl /nologo /EHsc /O2 /MT $defs "/I$($msvc.FullName)\include" "/I$($sdk.FullName)\um" "/I$($sdk.FullName)\shared" "/I$($sdk.FullName)\ucrt" (Join-Path $PSScriptRoot "sapi_harness.cpp") "/Fe$out\sapi_harness.exe" "/Fo$out\" /link "/LIBPATH:$($msvc.FullName)\lib\$arch" "/LIBPATH:$($sdk.Parent.Parent.FullName)\Lib\$($sdk.Name)\um\$arch" "/LIBPATH:$($sdk.Parent.Parent.FullName)\Lib\$($sdk.Name)\ucrt\$arch" ole32.lib advapi32.lib
    if ($LASTEXITCODE) { throw "$arch SAPI harness build failed ($LASTEXITCODE)" }
  }
  Remove-Item (Join-Path $out "*.obj"),(Join-Path $out "*.exp"),(Join-Path $out "*.lib") -ErrorAction SilentlyContinue
  $voices = Join-Path $repo "build\win\$arch\ssi263speech.dll"
  if (!(Test-Path $voices)) { throw "$voices is missing: run python src\csrc\build_ssi263speech.py first" }
  Copy-Item $voices $out
}
# Static CRT for every native DLL: nothing but the system's own libraries (no VCRUNTIME, MSVCP, UCRT forwarders,
# libstdc++ or libgcc DLLs).
$dumpbin = Join-Path $msvc.FullName "bin\Hostx64\x64\dumpbin.exe"
$allowed = @("KERNEL32.dll", "msvcrt.dll", "ADVAPI32.dll", "ole32.dll", "OLEAUT32.dll", "SHELL32.dll", "USER32.dll")
foreach ($dll in Get-ChildItem -Recurse $Stage -Filter *.dll) {
  $deps = @(& $dumpbin /nologo /dependents $dll.FullName | Where-Object { $_ -match '^\s+\S+\.dll\s*$' } | ForEach-Object { $_.Trim() })
  $bad = @($deps | Where-Object { $allowed -notcontains $_ })
  if ($bad.Count) { throw "$($dll.FullName) imports $($bad -join ', '): not a static-CRT build" }
}
if (!$Dev) {
  # The console-free way into the settings dialog: a GUI-subsystem launcher, so no console flashes and steals focus.
  # Keep native PowerShell on each OS: the x64 launcher on 64-bit Windows, x86 on 32-bit Windows.
  # Running the x86 launcher everywhere would redirect its PowerShell registry view on 64-bit Windows.
  foreach ($launcherArch in "x86","x64") {
    $launcherName = if ($launcherArch -eq "x86") { "ssi263_settings_x86.exe" } else { "ssi263_settings.exe" }
    $launcherCl = Join-Path $msvc.FullName "bin\Hostx64\$launcherArch\cl.exe"
    & $launcherCl /nologo /O2 /MT /W3 "/I$($msvc.FullName)\include" "/I$($sdk.FullName)\ucrt" "/I$($sdk.FullName)\um" "/I$($sdk.FullName)\shared" (Join-Path $PSScriptRoot "settings_launcher.c") "/Fe$Stage\$launcherName" "/Fo$Stage\" /link /SUBSYSTEM:WINDOWS "/LIBPATH:$($msvc.FullName)\lib\$launcherArch" "/LIBPATH:$($sdk.Parent.Parent.FullName)\Lib\$($sdk.Name)\ucrt\$launcherArch" "/LIBPATH:$($sdk.Parent.Parent.FullName)\Lib\$($sdk.Name)\um\$launcherArch" user32.lib kernel32.lib
    if ($LASTEXITCODE) { throw "$launcherArch settings launcher build failed ($LASTEXITCODE)" }
    $launcherBytes = [IO.File]::ReadAllBytes((Join-Path $Stage $launcherName))
    $peOffset = [BitConverter]::ToInt32($launcherBytes, 0x3c)
    $machine = [BitConverter]::ToUInt16($launcherBytes, $peOffset + 4)
    $expectedMachine = if ($launcherArch -eq "x86") { 0x14c } else { 0x8664 }
    if ($machine -ne $expectedMachine) { throw "$launcherName has the wrong CPU architecture" }
  }
  Remove-Item (Join-Path $Stage "*.obj") -ErrorAction SilentlyContinue
  Set-Content -Encoding ASCII (Join-Path $Stage "settings.cmd") '@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -STA -File "%~dp0settings.ps1"'
  Copy-Item (Join-Path $PSScriptRoot "register.ps1") $Stage
  Copy-Item (Join-Path $PSScriptRoot "settings.ps1") $Stage
}
# The firmware the voices need, as the library itself lists it (the serve host's --files), in the repository's
# firmware\ layout under {app}\firmware; and the voices it can make from it (--list), for register.ps1.  Each
# maker's note travels with its files.
$serve = Join-Path $repo "build\win\x64\ssi263_serve.exe"
$fwSrc = Join-Path $repo "firmware"
$fw = Join-Path $Stage "firmware"
$files = @(& $serve --files --firmware $fwSrc)
if ($LASTEXITCODE -or $files.Count -eq 0) { throw "the native voices found no firmware in $fwSrc" }
foreach ($rel in $files) {
  $dst = Join-Path $fw $rel
  New-Item -ItemType Directory -Force (Split-Path -Parent $dst) | Out-Null
  Copy-Item (Join-Path $fwSrc $rel) $dst
}
foreach ($rel in "AICOM.txt", "blazie/README.txt", "gw-micro-speakout/README.txt") {   # beside the files they speak of
  $src = Join-Path $fwSrc $rel
  $dst = Join-Path $fw $rel
  if ((Test-Path $src) -and ($rel -ne "AICOM.txt" -or (Test-Path (Join-Path $fw "aicom-*"))) -and (Test-Path (Split-Path -Parent $dst))) {
    Copy-Item $src $dst
  }
}
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $serve
$psi.Arguments = "--list --firmware `"$fw`""
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.StandardOutputEncoding = [System.Text.Encoding]::UTF8
$proc = [System.Diagnostics.Process]::Start($psi)
$listing = $proc.StandardOutput.ReadToEnd()
$proc.WaitForExit()
if ($proc.ExitCode -or $listing -notmatch "`t") { throw "the native voices listed nothing from the stage's firmware" }
[System.IO.File]::WriteAllText((Join-Path $Stage "voices.txt"), $listing, (New-Object System.Text.UTF8Encoding($false)))
# The licences: ours (MIT), Casso's (MIT) and each extracted MAME core's BSD notice with its provenance, as the
# add-ons carry them (nvda/build_common.py); the 6502's; the Mockingboard firmware's notice.
$lic = Join-Path $Stage "licenses"
New-Item -ItemType Directory -Force $lic | Out-Null
Copy-Item (Join-Path $repo "LICENSE") (Join-Path $lic "LICENSE-MIT.txt")
Copy-Item (Join-Path $repo "third_party\casso\LICENSE") (Join-Path $lic "LICENSE-Casso-MIT.txt")
foreach ($core in "z180","i8085","nec","i86") {
  $d = Join-Path $repo "src\csrc\cpu\mame_$core"
  Copy-Item (Join-Path $d "LICENSE-BSD-3-Clause.txt") (Join-Path $lic "LICENSE-$core-BSD-3-Clause.txt")
  Copy-Item (Join-Path $d "PINNED.txt") (Join-Path $lic "MAME-$core-provenance.txt")
}
# The Mockingboard's 6502: Fake6502 (Mike Chambers, public domain) by way of Jayson Smith's EchoTalk (BSD-3-Clause),
# in ssi263speech.dll whether or not its firmware is staged.
$d = Join-Path $repo "src\csrc\cpu\fake6502"
Copy-Item (Join-Path $d "LICENSE-EchoTalk-BSD-3-Clause.txt") (Join-Path $lic "LICENSE-EchoTalk-BSD-3-Clause.txt")
Copy-Item (Join-Path $d "PINNED.txt") (Join-Path $lic "Fake6502-provenance.txt")
# Sweet Micro Systems' text-to-speech is the Braille Lite's and the Speak-Out's kind of file: never in the repository,
# taken from the firmware folder the build is given (--files lists it only when it is there; absent, the voice is
# simply not staged), and its notice goes with it.
if (Test-Path (Join-Path $fw "sweet-micro-mockingboard\mockingboard-tts-1.1.bin")) {
  [System.IO.File]::WriteAllText((Join-Path $lic "Mockingboard-firmware-notice.txt"), "The Mockingboard's firmware -- notice

This installation carries the Mockingboard's own text-to-speech
(firmware\sweet-micro-mockingboard\mockingboard-tts-1.1.bin: Sweet Micro Systems' version 1.1 of 1985, by Mike
LePage, from the Mockingboard Developers Toolkit). It is not ours; it is here so the card can speak again, and it
will be removed if its rights holders ask. It is not covered by this package's MIT license.
", (New-Object System.Text.UTF8Encoding($false)))
}
Write-Host "SSI-263 SAPI stage$(if ($Dev) { ' (development)' }): $Stage"
Write-Host ("voices: " + (($listing -split "`r?`n" | Where-Object { $_ -match "`t" } | ForEach-Object { $_.Split("`t")[0] }) -join ', '))
