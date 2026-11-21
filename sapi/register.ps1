param([switch]$Register, [switch]$Unregister)
# Voice tokens for the SSI-263 SAPI engine: the COM class (both DLL bitnesses) and one token per voice in voices.txt
# (the native library's list, made at build time), in both registry views.  A trimmed fork of outspoken-nvda's
# sapi/register.ps1: the voices here come with their firmware, so there is no data root to find.  The token names
# and VoiceIds are 0.7.0's, so a voice someone chose stays chosen across the upgrade.
#
# PowerShell 2.0's dialect on purpose (stock Windows 7): no $PSScriptRoot, the 32-bit view written directly.
# Run elevated: -Register or -Unregister.
$ErrorActionPreference = 'Stop'
$stage = Split-Path -Parent $MyInvocation.MyCommand.Path
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
if (!$Register) { Write-Host 'Use -Register or -Unregister (elevated).'; exit 2 }

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
# voices.txt): "id<TAB>name<TAB>language", UTF-8.
$listing = [System.IO.File]::ReadAllText((Join-Path $stage 'voices.txt'), [System.Text.Encoding]::UTF8)
$voices = @($listing -split "`r?`n" | Where-Object { $_ -match "`t" })
if ($voices.Count -eq 0) { throw 'voices.txt lists no voices' }

Remove-Voices
foreach ($line in $voices) {
    $f = $line.Split("`t")
    $id, $name, $lang = $f[0], $f[1], $f[2]
    $key = $prefix + ($id -replace '[^A-Za-z0-9]', '_')
    foreach ($view in $views) {
        if (!(Test-Path $view)) { New-Item -Path $view -Force | Out-Null }
        $tok = Join-Path $view $key
        New-Item -Path $tok -Force | Out-Null
        Set-ItemProperty -Path $tok -Name '(default)' -Value $name
        Set-ItemProperty -Path $tok -Name 'CLSID' -Value $clsid
        Set-ItemProperty -Path $tok -Name 'VoiceId' -Value $id
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
