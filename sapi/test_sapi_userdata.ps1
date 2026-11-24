# The voice no package carries (the Super Voice) through real SAPI, with register.ps1's elevated steps for real -- run
# ELEVATED on a machine where this stage is (or may be) the registered SSI-263 SAPI engine; it re-registers the stage
# at the end, its voices as they were, the Super Voice's token gone.  Not in run_tests.py: it needs an administrator
# and changes the machine's voice tokens while it runs.
#
#   powershell -ExecutionPolicy Bypass -File sapi\test_sapi_userdata.ps1 [-Stage nvda\dist\sapi]
#
# What it checks, each step's result printed (exit 1 on any FAIL):
#   import     register.ps1 -ImportFile SUPERTOK.BIN -DataRoot <a per-user root> -Register: the program in the root, the
#              token written in both registry views with DataPath = the root
#   speaks     SAPI (SAPI.SpVoice, 64-bit and 32-bit PowerShell) speaks the Super Voice's token to a WAV file
#   refused    the program taken away: selecting the voice fails (SetObjectToken refuses it), nothing spoken
#   migrate    register.ps1 -MigrateData -MigrateFrom <the per-user root> -DataRoot <a %ProgramData%-style root>: the
#              folder moved, the source left empty removed, Users read-only on the target (no write, by SID), the
#              machine's DataPath written (to a test key: SSI263_SAPI_DATA_MACHINE_KEY), the token following it
#   speaks2    SAPI speaks it again from the moved folder
#   restored   the stage registered again as the person would: a Super Voice token only when one of their own data
#              roots holds the program
# PowerShell 2.0's dialect, as the scripts it drives.
#   -Control   SSI263_SAPI_REGISTER_BREAK=acl-t: the move's permissions granted down the tree, as Panthera's move does
#              -- every file left with an empty ACL: "migrate" (the program unreadable) and "speaks2" must FAIL
param([string]$Stage = '', [switch]$Control)
if ($Control) { $env:SSI263_SAPI_REGISTER_BREAK = 'acl-t' }
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $Stage) { $Stage = Join-Path $repo 'nvda\dist\sapi' }
$register = Join-Path $Stage 'register.ps1'
$bin = Join-Path $repo 'firmware\speech-systems-super-voice\SUPERTOK.BIN'
if (-not (Test-Path $bin)) { Write-Output 'skip  no SUPERTOK.BIN in firmware\speech-systems-super-voice'; exit 0 }
$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { Write-Output 'FAIL  not elevated'; exit 1 }

$tmp = Join-Path $env:TEMP ('ssi263-userdata-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$userRoot = Join-Path $tmp 'Roaming\ssi263-speech\firmware'
$commonRoot = Join-Path $tmp 'ProgramData\ssi263-speech\firmware'
$machineKey = 'Software\SSI-263 SAPI test machine userdata'
$env:SSI263_SAPI_DATA_MACHINE_KEY = $machineKey      # register.ps1, a child, inherits it: the machine's DataPath there
$tokenKey = 'SSI263_supervoice_supertok'
$views = @('HKLM:\SOFTWARE\Microsoft\Speech\Voices\Tokens', 'HKLM:\SOFTWARE\Wow6432Node\Microsoft\Speech\Voices\Tokens')
$script:bad = 0
function Check([string]$name, [bool]$ok, [string]$detail) {
    if (-not $ok) { $script:bad++ }
    Write-Output ('{0,-5} {1,-9} {2}' -f $(if ($ok) { 'ok' } else { 'FAIL' }), $name, $detail)
}
function Invoke-Register([string]$switches) {
    $p = Start-Process powershell.exe -Wait -PassThru -WindowStyle Hidden -ArgumentList ('-NoProfile -ExecutionPolicy Bypass -File "{0}" {1}' -f $register, $switches)
    $p.ExitCode
}
function Token-DataPaths {
    @(foreach ($v in $views) { try { [string](Get-ItemProperty -Path (Join-Path $v $tokenKey) -Name DataPath -ErrorAction Stop).DataPath } catch { '' } })
}
# SAPI speaking the Super Voice to a WAV, in a PowerShell of the given bitness: its size in bytes, or the error
function Speak-To([string]$exe, [string]$wav) {
    $cmd = @"
`$ErrorActionPreference = 'Stop'
try {
  `$v = New-Object -ComObject SAPI.SpVoice
  `$t = @(`$v.GetVoices() | Where-Object { `$_.Id -like '*\$tokenKey' })[0]
  if (-not `$t) { 'no token'; exit 2 }
  `$v.Voice = `$t
  `$f = New-Object -ComObject SAPI.SpFileStream
  `$f.Open('$wav', 3)
  `$v.AudioOutputStream = `$f
  [void]`$v.Speak('This is the Speech Systems Super Voice, speaking through SAPI.')
  `$f.Close()
  (Get-Item '$wav').Length
} catch { 'error: ' + `$_.Exception.Message; exit 3 }
"@
    $out = & $exe -NoProfile -ExecutionPolicy Bypass -Command $cmd 2>&1
    ($out | Out-String).Trim()
}
$ps64 = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$ps32 = Join-Path $env:SystemRoot 'SysWOW64\WindowsPowerShell\v1.0\powershell.exe'

try {
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null
    $code = Invoke-Register ('-ImportFile "{0}" -DataRoot "{1}" -Register' -f $bin, $userRoot)
    $paths = Token-DataPaths
    Check 'import' ($code -eq 0 -and (Test-Path (Join-Path $userRoot 'speech-systems-super-voice\SUPERTOK.BIN')) -and
                    @($paths | Where-Object { $_ -eq $userRoot }).Count -eq 2) ("exit $code; token DataPath: " + ($paths -join ' | '))

    foreach ($pair in @(@('x64', $ps64), @('x86', $ps32))) {
        $r = Speak-To $pair[1] (Join-Path $tmp ('speak-' + $pair[0] + '.wav'))
        Check 'speaks' ($r -match '^\d+$' -and [int64]$r -gt 100000) ("{0}: {1} bytes of WAV" -f $pair[0], $r)
    }

    Rename-Item (Join-Path $userRoot 'speech-systems-super-voice\SUPERTOK.BIN') 'SUPERTOK.away'
    $r = Speak-To $ps64 (Join-Path $tmp 'refused.wav')
    Check 'refused' ($r -like 'error:*' -or $r -eq 'no token') ("x64: " + $r)
    Rename-Item (Join-Path $userRoot 'speech-systems-super-voice\SUPERTOK.away') 'SUPERTOK.BIN'

    $code = Invoke-Register ('-MigrateData -MigrateFrom "{0}" -DataRoot "{1}"' -f $userRoot, $commonRoot)
    $paths = Token-DataPaths
    $acl = Get-Acl $commonRoot
    $usersWrite = @($acl.Access | Where-Object {
        $_.IdentityReference.Translate([Security.Principal.SecurityIdentifier]).Value -eq 'S-1-5-32-545' -and
        ($_.FileSystemRights.ToString() -match 'Write|Modify|FullControl|CreateFiles|AppendData') }).Count
    $machine = try { [string](Get-ItemProperty -Path ('HKCU:\' + $machineKey) -Name DataPath -ErrorAction Stop).DataPath } catch { '' }
    # the program itself readable by Users (and so by SYSTEM, the sign-in screen and every account): its own ACL
    $moved = Join-Path $commonRoot 'speech-systems-super-voice\SUPERTOK.BIN'
    $fileRead = @((Get-Acl $moved).Access | Where-Object {
        $_.IdentityReference.Translate([Security.Principal.SecurityIdentifier]).Value -eq 'S-1-5-32-545' -and
        $_.AccessControlType -eq 'Allow' -and ($_.FileSystemRights.ToString() -match 'Read') }).Count
    Check 'migrate' ($code -eq 0 -and (Test-Path $moved) -and $fileRead -gt 0 -and
                     -not (Test-Path $userRoot) -and $usersWrite -eq 0 -and $machine -eq $commonRoot -and
                     @($paths | Where-Object { $_ -eq $commonRoot }).Count -eq 2) (
        "exit $code; source gone: $(-not (Test-Path $userRoot)); Users may write: $usersWrite; the program readable by Users: $($fileRead -gt 0); machine DataPath: $machine; token: " + ($paths -join ' | '))

    $r = Speak-To $ps64 (Join-Path $tmp 'speak2.wav')
    Check 'speaks2' ($r -match '^\d+$' -and [int64]$r -gt 100000) ("x64, from the moved folder: $r bytes of WAV")
} finally {
    if (Test-Path $tmp) { & "$env:SystemRoot\System32\icacls.exe" $tmp /reset /T /C /Q | Out-Null }   # (the control's empty ACLs)
    Remove-Item -Recurse -Force -LiteralPath $tmp -ErrorAction SilentlyContinue
    Remove-Item env:SSI263_SAPI_REGISTER_BREAK -ErrorAction SilentlyContinue
    Remove-Item -Path ('HKCU:\' + $machineKey) -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item env:SSI263_SAPI_DATA_MACHINE_KEY -ErrorAction SilentlyContinue
    # the stage registered as the person would register it: a Super Voice token exactly when one of THEIR data roots
    # holds the program (dataroot.ps1, as register.ps1 decides) -- it expected none, and on a machine where the person
    # keeps the Super Voice it said FAIL and left that token registered (the 0.9.5 audit)
    . (Join-Path $Stage 'dataroot.ps1')
    $theirs = @(Get-DataCandidates | Where-Object { Test-RootHasData $_ (Get-UserDataVoices $Stage) })
    $code = Invoke-Register '-Register'
    $left = @($views | Where-Object { Test-Path (Join-Path $_ $tokenKey) }).Count
    Check 'restored' ($code -eq 0 -and (($left -gt 0) -eq ($theirs.Count -gt 0))) ("exit $code; Super Voice tokens: $left; the person's roots holding it: " + $(if ($theirs.Count) { $theirs -join ', ' } else { 'none' }))
}
Write-Output ('sapi userdata: ' + $(if ($script:bad) { "$script:bad FAILED" } else { 'all passed' }))
exit $(if ($script:bad) { 1 } else { 0 })
