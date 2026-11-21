# The SAPI settings dialog keeps every setting when one is changed (issue #39).
#
# Its Save-Setting ran New-Item -Path $prefKey -Force before writing the value, and on an existing registry key that
# replaces the key with an empty one (New-Item's documentation, Example 9; the registry provider deletes the key's
# tree first): each change erased every other setting, which the engine then read as its default at the next thing
# spoken -- the sample rate back to 22 kHz, inflection on, the whine off ... -- while the dialog still showed them.
#
# This runs the dialog's own Save-Setting and Load-Setting, taken from settings.ps1 by PowerShell's parser, on a
# throwaway key (never the person's: HKCU\Software\SSI-263 SAPI test-<pid>), which it removes after:
#   two         a setting saved, then another: both are there
#   all eight   the dialog's eight settings saved one after another: every one keeps its value
#   fresh       on no key at all, the first save makes it
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File sapi\test_settings_save.ps1        exit 1 on any failure
#   SSI263_SAVE_BREAK=1   Save-Setting's key made with -Force every time, as it was   -- must fail: two, all eight
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$script = Join-Path $here 'settings.ps1'
$tokens = $null; $errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($script, [ref]$tokens, [ref]$errors)
$functions = $ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $true)
$save = ($functions | Where-Object { $_.Name -eq 'Save-Setting' }).Extent.Text
$load = ($functions | Where-Object { $_.Name -eq 'Load-Setting' }).Extent.Text
$bad = 0
if (-not $save -or -not $load) { Write-Output 'FAIL  settings.ps1 has no Save-Setting or Load-Setting'; exit 1 }
if ($env:SSI263_SAVE_BREAK) {
    $save = "function Save-Setting([string]`$name, [int]`$value) {`n    New-Item -Path `$prefKey -Force | Out-Null`n" +
            "    New-ItemProperty -Path `$prefKey -Name `$name -Value `$value -PropertyType DWord -Force | Out-Null`n}"
}
$prefKey = "HKCU:\Software\SSI-263 SAPI test-$PID"
. ([ScriptBlock]::Create($save))
. ([ScriptBlock]::Create($load))

function Check([string]$name, [bool]$ok, [string]$detail) {
    if ($ok) { Write-Output ('ok    {0,-10} {1}' -f $name, $detail) }
    else { Write-Output ('FAIL  {0,-10} {1}' -f $name, $detail); $script:bad++ }
}

try {
    if (Test-Path $prefKey) { Remove-Item $prefKey -Recurse }
    Save-Setting 'SampleRate' 44100
    Check 'fresh' (Test-Path $prefKey) 'the first save on no key makes it'
    Save-Setting 'LineLift' 1
    $rate = Load-Setting 'SampleRate' 22050
    Check 'two' ($rate -eq 44100 -and (Load-Setting 'LineLift' 0) -eq 1) ("SampleRate 44100 saved, then LineLift: SampleRate reads $rate")
    $want = [ordered]@{ Inflection = 0; Whine = 2; Diagnostics = 1; AccentInflection = 40; SampleRate = 11025;
                        RunAhead = 1; BrailleLiteNumbers = 0; LineLift = 1 }
    foreach ($n in $want.Keys) { Save-Setting $n $want[$n] }
    $lost = @($want.Keys | Where-Object { (Load-Setting $_ -12345) -ne $want[$_] })
    Check 'all eight' ($lost.Count -eq 0) ("the dialog's eight settings saved one after another: " +
                                            $(if ($lost.Count) { 'lost ' + ($lost -join ', ') } else { 'all kept' }))
} finally {
    if (Test-Path $prefKey) { Remove-Item $prefKey -Recurse }
}
if ($bad) { Write-Output "settings save: $bad FAILED"; exit 1 }
Write-Output 'settings save: all ok'
exit 0
