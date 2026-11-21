# Unregister takes off every DLL the install registered, on 64-bit and 32-bit Windows alike (issue #43).
#
# register.ps1 registers the x86 DLL with SysWOW64's regsvr32 on a 64-bit Windows and with System32's on a 32-bit one,
# but unregistered it only when SysWOW64's existed: on a 32-bit Windows (stock Windows 7 is one target) an uninstall
# removed the voices and left the engine's COM class registered, pointing at a DLL the uninstaller had deleted.
#
# This runs register.ps1's own Unregister-Plan (taken by PowerShell's parser; nothing is unregistered) on two made-up
# Windows folders in a scratch folder:
#   64-bit      SysWOW64 there, both DLLs: the x64 DLL by System32's regsvr32, the x86 one by SysWOW64's
#   32-bit      no SysWOW64, the x86 DLL only: the x86 DLL by System32's regsvr32
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File sapi\test_unregister.ps1        exit 1 on any failure
#   SSI263_UNREG_BREAK=1   the plan as it was (the x86 DLL only with SysWOW64)   -- must fail: 32-bit
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$tokens = $null; $errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile((Join-Path $here 'register.ps1'), [ref]$tokens, [ref]$errors)
$fn = ($ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $true) |
       Where-Object { $_.Name -eq 'Unregister-Plan' }).Extent.Text
if ($env:SSI263_UNREG_BREAK) {
    $fn = "function Unregister-Plan { `$plan = @(); if (Test-Path `$x64dll) { `$plan += ,@(`$sys64, `$x64dll) }; " +
          "if ((Test-Path `$sys32) -and (Test-Path `$x86dll)) { `$plan += ,@(`$sys32, `$x86dll) }; return ,`$plan }"
}
if (-not $fn) { Write-Output 'FAIL  register.ps1 has no Unregister-Plan'; exit 1 }
. ([ScriptBlock]::Create($fn))

$bad = 0
$tmp = Join-Path $env:TEMP ("ssi_unreg_" + $PID)
function Layout([bool]$wow, [bool]$x64) {
    if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
    $win = Join-Path $tmp 'Windows'; $stage = Join-Path $tmp 'stage'
    New-Item -ItemType Directory -Force (Join-Path $win 'System32'), (Join-Path $stage 'x86') | Out-Null
    'x' | Set-Content (Join-Path $win 'System32\regsvr32.exe')
    if ($wow) { New-Item -ItemType Directory -Force (Join-Path $win 'SysWOW64') | Out-Null; 'x' | Set-Content (Join-Path $win 'SysWOW64\regsvr32.exe') }
    'x' | Set-Content (Join-Path $stage 'x86\ssi263_sapi.dll')
    if ($x64) { New-Item -ItemType Directory -Force (Join-Path $stage 'x64') | Out-Null; 'x' | Set-Content (Join-Path $stage 'x64\ssi263_sapi.dll') }
    $script:sys64 = Join-Path $win 'System32\regsvr32.exe'
    $script:sys32 = Join-Path $win 'SysWOW64\regsvr32.exe'
    $script:x64dll = Join-Path $stage 'x64\ssi263_sapi.dll'
    $script:x86dll = Join-Path $stage 'x86\ssi263_sapi.dll'
}
function Show($plan) { ($plan | ForEach-Object { (Split-Path -Leaf (Split-Path -Parent $_[0])) + ' ' + (Split-Path -Leaf (Split-Path -Parent $_[1])) }) -join ', ' }
function Check([string]$name, [bool]$ok, [string]$detail) {
    if ($ok) { Write-Output ('ok    {0,-7} {1}' -f $name, $detail) } else { Write-Output ('FAIL  {0,-7} {1}' -f $name, $detail); $script:bad++ }
}
try {
    Layout $true $true
    $plan = Unregister-Plan
    Check '64-bit' ((Show $plan) -eq 'System32 x64, SysWOW64 x86') ("SysWOW64 there, both DLLs: " + $(if ($plan.Count) { Show $plan } else { 'nothing' }))
    Layout $false $false
    $plan = Unregister-Plan
    Check '32-bit' ((Show $plan) -eq 'System32 x86') ("no SysWOW64, the x86 DLL: " + $(if ($plan.Count) { Show $plan } else { 'nothing unregistered' }))
} finally {
    if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
}
if ($bad) { Write-Output "sapi unregister: $bad FAILED"; exit 1 }
Write-Output 'sapi unregister: every registered DLL taken off'
exit 0
