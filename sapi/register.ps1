param([switch]$Register, [switch]$Unregister, [string]$DataRoot = '', [switch]$MigrateData,
      [string]$MigrateFrom = '', [string]$ImportFile = '')
# Voice tokens for the SSI-263 SAPI engine: the COM class (both DLL bitnesses) and one token per voice in voices.txt
# (the native library's list, made at build time), in both registry views.  A trimmed fork of outspoken-nvda's
# sapi/register.ps1.  The voices this installation carries come with their firmware ({app}\firmware); a voice no
# package carries (voices.txt's "data:" rows: the Super Voice) gets a token only where a data root holds its file,
# and the token carries that root (DataPath) -- Panthera's system (dataroot.ps1).  The token names and VoiceIds are
# 0.7.0's, so a voice someone chose stays chosen across the upgrade.
#
# PowerShell 2.0's dialect on purpose (stock Windows 7): no $PSScriptRoot, the 32-bit view written directly.
# Run elevated:
#   -Register [-DataRoot <root>]                 the COM class and every token; without -DataRoot (the installer) a
#                                                voice no package carries keeps the root its token had, else the first
#                                                root holding its file (dataroot.ps1), else gets no token
#   -ImportFile <file> -DataRoot <root> -Register   the Super Voice's program, already read and checked by the settings
#                                                program, checked again (sha256) and put in <root>, then -Register
#   -MigrateData -MigrateFrom <per-user root> -DataRoot <%ProgramData% root>   "Move voices for all users"
#   -Unregister
# Exit codes: 2 bad arguments, 3 a -DataRoot that is not there, 4 the move or the copy failed, 5 moved but not
# registered.  Every elevated trip names its root itself: the elevated account's %APPDATA% and HKCU may be another's.
$ErrorActionPreference = 'Stop'
$stage = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $stage 'dataroot.ps1')
$clsid = '{616e7e0a-7b1a-4b94-8812-bbb597d2c025}'
$prefix = 'SSI263_'
$views = @('HKLM:\SOFTWARE\Microsoft\Speech\Voices\Tokens')
if ([IntPtr]::Size -eq 8 -or $env:PROCESSOR_ARCHITEW6432) {
    $views += 'HKLM:\SOFTWARE\Wow6432Node\Microsoft\Speech\Voices\Tokens'
}
$windir = $env:SystemRoot
# The 64-bit regsvr32 lives in System32 -- from a 32-bit PowerShell that name is redirected, Sysnative is the way in.
$sys64 = Join-Path $windir 'Sysnative\regsvr32.exe'
if (!(Test-Path $sys64)) { $sys64 = Join-Path $windir 'System32\regsvr32.exe' }
$sys32 = Join-Path $windir 'SysWOW64\regsvr32.exe'
$x64dll = Join-Path $stage 'x64\ssi263_sapi.dll'
$x86dll = Join-Path $stage 'x86\ssi263_sapi.dll'

# What Unregister runs: (regsvr32, DLL) for each DLL this Windows has registered, as -Register registers them below.
# A 64-bit Windows (SysWOW64 there): the x64 DLL by the 64-bit regsvr32, the x86 one by SysWOW64's; a 32-bit Windows:
# the x86 DLL by its only regsvr32.  The 32-bit case was missing, and its COM class stayed registered after an
# uninstall, pointing at a deleted DLL (#43; sapi\test_unregister.ps1).
function Unregister-Plan {
    $plan = @()
    if (Test-Path $sys32) {
        if (Test-Path $x64dll) { $plan += ,@($sys64, $x64dll) }
        if (Test-Path $x86dll) { $plan += ,@($sys32, $x86dll) }
    } elseif (Test-Path $x86dll) {
        $plan += ,@($sys64, $x86dll)
    }
    return ,$plan
}

function Remove-Voices {
    foreach ($view in $views) {
        if (!(Test-Path $view)) { continue }
        Get-ChildItem $view | Where-Object { $_.PSChildName -like "$prefix*" } | ForEach-Object {
            Remove-Item -Recurse -Force $_.PSPath
        }
    }
}

# The token's Language: the Windows LCID (hex) of the voice's language, the full tag first (pt-BR, ar-AE), then the
# language alone in its own country.  It knew four once and gave every other voice 409, US English (#41;
# sapi\test_languages.ps1 holds each voice list language to Windows' own culture data).
function Language-Code([string]$lang) {
    $tag = $lang.Replace('_', '-').ToLower()
    switch ($tag) {
        'pt-br' { return '416' }
        'ar-ae' { return '3801' }
    }
    switch ($tag.Split('-')[0]) {
        'en' { return '409' }
        'es' { return '40A' }
        'de' { return '407' }
        'it' { return '410' }
        'sk' { return '41B' }
        'ar' { return '401' }
        'pt' { return '816' }
        'hr' { return '41A' }
        'cs' { return '405' }
        'da' { return '406' }
        'fr' { return '40C' }
        'el' { return '408' }
        'he' { return '40D' }
        'hu' { return '40E' }
        'ko' { return '412' }
        'no' { return '414' }
        'nb' { return '414' }
        'pl' { return '415' }
        'sv' { return '41D' }
        'tr' { return '41F' }
        'ru' { return '419' }
        default { return '409' }
    }
}

if ($Unregister) {
    Remove-Voices
    foreach ($step in (Unregister-Plan)) { & $step[0] /s /u $step[1] }
    Write-Host 'SSI-263 SAPI voices removed.'
    exit 0
}

# The data root a token of a voice no package carries has now (they all carry the same one), or ''.
function Get-TokenDataPath {
    foreach ($view in $views) {
        if (!(Test-Path $view)) { continue }
        foreach ($k in @(Get-ChildItem $view | Where-Object { $_.PSChildName -like "$prefix*" })) {
            try { $v = [string](Get-ItemProperty -Path $k.PSPath -Name DataPath -ErrorAction Stop).DataPath; if ($v) { return $v } } catch {}
        }
    }
    ''
}

$userVoices = Get-UserDataVoices $stage
if ($DataRoot -and -not (Test-Path -LiteralPath $DataRoot)) {
    if ($MigrateData) { New-Item -ItemType Directory -Force -Path $DataRoot | Out-Null }
    elseif ($ImportFile) { New-Item -ItemType Directory -Force -Path $DataRoot | Out-Null }
    else { Write-Host "no such data folder: $DataRoot"; exit 3 }
}
if (($ImportFile -or $MigrateData) -and -not $DataRoot) { Write-Host 'that needs -DataRoot'; exit 2 }

if ($ImportFile) {
    # the settings program read the file with the library and wrote the program alone; here, elevated, it is checked
    # once more by its sha256 before it goes into a folder SYSTEM reads
    try {
        $state = Install-SuperVoiceProgram $DataRoot ([System.IO.File]::ReadAllBytes($ImportFile))
        Write-Host "the Super Voice's program: $state in $DataRoot"
    } catch { Write-Host "the Super Voice's program could not be put in ${DataRoot}: $($_.Exception.Message)"; exit 4 }
}

if ($MigrateData) {
    if (-not $MigrateFrom -or -not (Test-Path -LiteralPath $MigrateFrom)) { Write-Host 'nothing to move from'; exit 3 }
    # only the voices' own folders, by name (the first part of each file's path), never anything else of the source;
    # a folder already at the target is left (a run cut short can run again)
    try {
        foreach ($v in $userVoices) {
            $sub = $v.File.Split('\')[0]
            $src = Join-Path $MigrateFrom $sub
            $dst = Join-Path $DataRoot $sub
            if ((Test-Path -LiteralPath $src) -and -not (Test-Path -LiteralPath $dst)) { Move-Item -LiteralPath $src -Destination $dst }
        }
        # read-only for Users, full for SYSTEM and the Administrators, by SID (group names are localised); a move within
        # a volume keeps the descriptor it had, and /reset on the root would give Users back %ProgramData%'s right to
        # create files.  So: the root's own ACL replaced, inheritance off -- then everything below reset to inherit
        # from it.  The grant with /T, as Panthera's move runs it, left every FILE with an empty ACL (the (OI)(CI)
        # inheritance flags take nothing on a file, after /inheritance:r had taken the inherited entries): nobody could
        # read the program, SAPI refused the voice (sapi\test_sapi_userdata.ps1 found it, 2026-11-24)
        & "$env:SystemRoot\System32\icacls.exe" $DataRoot /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' '*S-1-5-32-545:(OI)(CI)RX' /C /Q | Out-Null
        if ($LASTEXITCODE) { throw "icacls could not set $DataRoot's permissions" }
        foreach ($child in @(Get-ChildItem -LiteralPath $DataRoot -Force)) {
            if ($env:SSI263_SAPI_REGISTER_BREAK -eq 'acl-t') { break }      # the control: the children left as moved
            & "$env:SystemRoot\System32\icacls.exe" $child.FullName /reset /T /C /Q | Out-Null
        }
        if ($env:SSI263_SAPI_REGISTER_BREAK -eq 'acl-t') {                    # ... with the grant down the tree, as it was
            & "$env:SystemRoot\System32\icacls.exe" $DataRoot /inheritance:r /grant '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' '*S-1-5-32-545:(OI)(CI)RX' /T /C /Q | Out-Null
        }
        Set-MachineDataPath $DataRoot
        if (@(Get-ChildItem -LiteralPath $MigrateFrom -Force).Count -eq 0) { Remove-Item -LiteralPath $MigrateFrom -Force }
    } catch { Write-Host "the move failed: $($_.Exception.Message)"; exit 4 }
    $Register = $true
}

if (!$Register) { Write-Host 'Use -Register or -Unregister (elevated).'; exit 2 }

# the root for the voices no package carries: as given; else the root their token had, while it still holds them
# (an upgrade keeps the person's choice); else the first holding one of their files (dataroot.ps1)
$root = $DataRoot
if (-not $root) {
    $had = Get-TokenDataPath
    if ($had -and (Test-RootHasData $had $userVoices)) { $root = $had }
    else {
        foreach ($p in (Get-DataCandidates)) { if (Test-RootHasData $p $userVoices) { $root = $p; break } }
    }
}

# The COM class, in each view: the x64 DLL for 64-bit programs, the x86 DLL for 32-bit ones.  A 32-bit Windows has
# only the x86 DLL (no SysWOW64 there: its own regsvr32 is the 32-bit one).
if (Test-Path $sys32) {
    if (Test-Path $x64dll) {
        $p = Start-Process -FilePath $sys64 -ArgumentList @('/s', "`"$x64dll`"") -Wait -PassThru
        if ($p.ExitCode) { throw "regsvr32 (x64) failed: $($p.ExitCode)" }
    }
    $p = Start-Process -FilePath $sys32 -ArgumentList @('/s', "`"$x86dll`"") -Wait -PassThru
    if ($p.ExitCode) { throw "regsvr32 (x86) failed: $($p.ExitCode)" }
} else {
    $p = Start-Process -FilePath $sys64 -ArgumentList @('/s', "`"$x86dll`"") -Wait -PassThru
    if ($p.ExitCode) { throw "regsvr32 (x86) failed: $($p.ExitCode)" }
}

# The voices, as the native library listed them from the installed firmware when the stage was built (sapi\build.ps1,
# voices.txt): "id<TAB>name<TAB>language", UTF-8; a voice no package carries adds "<TAB>data:<its file>".
$listing = [System.IO.File]::ReadAllText((Join-Path $stage 'voices.txt'), [System.Text.Encoding]::UTF8)
$voices = @($listing -split "`r?`n" | Where-Object { $_ -match "`t" })
if ($voices.Count -eq 0) { throw 'voices.txt lists no voices' }

Remove-Voices
foreach ($line in $voices) {
    $f = $line.Split("`t")
    $id, $name, $lang = $f[0], $f[1], $f[2]
    $dataPath = ''
    if ($f.Count -gt 3 -and $f[3].StartsWith('data:')) {     # a voice no package carries: only where its file is
        if (-not $root -or -not (Test-Path -LiteralPath (Join-Path $root $f[3].Substring(5).Replace('/', '\')))) {
            Write-Host "not registered: $name (its file is in no data folder)"
            continue
        }
        $dataPath = $root
    }
    $key = $prefix + ($id -replace '[^A-Za-z0-9]', '_')
    foreach ($view in $views) {
        if (!(Test-Path $view)) { New-Item -Path $view -Force | Out-Null }
        $tok = Join-Path $view $key
        New-Item -Path $tok -Force | Out-Null
        Set-ItemProperty -Path $tok -Name '(default)' -Value $name
        Set-ItemProperty -Path $tok -Name 'CLSID' -Value $clsid
        Set-ItemProperty -Path $tok -Name 'VoiceId' -Value $id
        if ($dataPath) { Set-ItemProperty -Path $tok -Name 'DataPath' -Value $dataPath }
        $attr = Join-Path $tok 'Attributes'
        New-Item -Path $attr -Force | Out-Null
        Set-ItemProperty -Path $attr -Name 'Name' -Value $name
        Set-ItemProperty -Path $attr -Name 'Gender' -Value 'Male'
        Set-ItemProperty -Path $attr -Name 'Age' -Value 'Adult'
        Set-ItemProperty -Path $attr -Name 'Language' -Value (Language-Code $lang)
        Set-ItemProperty -Path $attr -Name 'Vendor' -Value 'SSI-263 emulation'
    }
    Write-Host "registered $name ($id)"
}
