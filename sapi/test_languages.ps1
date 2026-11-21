# Each SAPI voice is registered in its own language (issue #41).
#
# register.ps1's Language-Code knew Spanish, German, Italian and Slovak; every other language fell to 409, US English,
# and was written as the voice token's Language: the Arabic, Portuguese, Croatian, Czech, Danish, French, Greek,
# Hebrew, Hungarian, Korean, Norwegian, Polish, Swedish, Turkish and Russian Braille Lites were English to every SAPI
# program -- listed under English, never found for their own language, and a screen reader's switch to a language
# could hand its text to another voice.
#
# This runs register.ps1's own Language-Code (taken by PowerShell's parser; nothing is registered) on every language
# in the voice list (voices.txt, as sapi\build.ps1 makes it from the voice table), and holds the code to Windows' own
# culture data:
#   language    the code's culture is the voice's language (English only for English)
#   region      a voice with a region (pt-BR, ar-AE) gets that region; the Portuguese voice without one, Portugal's
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File sapi\test_languages.ps1 [voices.txt]     exit 1 on any failure
#   SSI263_LANG_BREAK=1   Language-Code as it was (four languages, the rest 409)   -- must fail: language
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$voices = $(if ($args.Count) { $args[0] } else { Join-Path (Split-Path -Parent $here) 'nvda\dist\sapi\voices.txt' })
if (-not (Test-Path $voices)) { Write-Output "FAIL  no voice list at $voices (sapi\build.ps1 makes it)"; exit 1 }
$tokens = $null; $errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile((Join-Path $here 'register.ps1'), [ref]$tokens, [ref]$errors)
$fn = ($ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $true) |
       Where-Object { $_.Name -eq 'Language-Code' }).Extent.Text
if (-not $fn) { Write-Output 'FAIL  register.ps1 has no Language-Code'; exit 1 }
if ($env:SSI263_LANG_BREAK) {
    $fn = "function Language-Code([string]`$lang) { switch (`$lang.Split('_')[0].Split('-')[0].ToLower()) { 'es' { '40A' } " +
          "'de' { '407' } 'it' { '410' } 'sk' { '41B' } default { '409' } } }"
}
. ([ScriptBlock]::Create($fn))

$langs = @()
foreach ($line in ([System.IO.File]::ReadAllText($voices, [System.Text.Encoding]::UTF8) -split "`r?`n")) {
    $f = $line.Split("`t")
    if ($f.Count -ge 3 -and $f[2] -and ($langs -notcontains $f[2])) { $langs += $f[2] }
}
$bad = 0
foreach ($tag in $langs) {
    $code = Language-Code $tag
    $c = [System.Globalization.CultureInfo]::GetCultureInfo([Convert]::ToInt32($code, 16))
    $want = $tag.Split('-')[0].ToLower()
    $lang = $c.TwoLetterISOLanguageName -eq $want -or ($want -eq 'no' -and $c.TwoLetterISOLanguageName -eq 'nb')
    $region = $true
    if ($tag.Contains('-')) { $region = $c.Name -eq $tag }
    elseif ($want -eq 'pt') { $region = $c.Name -eq 'pt-PT' }
    $ok = $lang -and $region
    if (-not $ok) { $bad++ }
    Write-Output ('{0,-4}  {1,-6} {2,-5} {3}' -f $(if ($ok) { 'ok' } else { 'FAIL' }), $tag, $code, $c.Name)
}
if ($langs.Count -lt 10) { Write-Output "FAIL  only $($langs.Count) languages in $voices"; $bad++ }
if ($bad) { Write-Output "sapi languages: $bad of $($langs.Count) FAILED"; exit 1 }
Write-Output "sapi languages: $($langs.Count) of $($langs.Count) in their own language"
exit 0
