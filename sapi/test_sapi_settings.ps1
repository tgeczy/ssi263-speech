# The settings dialog's values reach the voices through SAPI: the Braille Lite speaks the same line with the
# defaults, the defaults again (the control: identical), inflection off, the whine on and "run the unit ahead" (each
# must differ: run ahead keeps the phonemes and changes the timing, sapi/test_serve.py; the English and the Spanish
# voice both); "read numbers as words" off for "1,234,567" and Spain's "1.234.567" (must differ; no value must be the
# same as on); "lift line starts" on (a fresh unit's first line, lifted: must differ); the Accent with its inflection at 0 (must differ from its default); and every sample rate (the WAV SAPI writes carries
# that rate and lasts as long as the default's), with the diagnostic log on for the timing.  This user's settings
# are put back as they were afterwards.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File sapi\test_sapi_settings.ps1
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Speech
$key = 'HKCU:\Software\SSI-263 SAPI'
$names = @('Inflection', 'Whine', 'Diagnostics', 'AccentInflection', 'SampleRate', 'RunAhead', 'BrailleLiteNumbers', 'LineLift')
$saved = @{}
if (Test-Path $key) { foreach ($n in $names) { try { $saved[$n] = (Get-ItemProperty $key -Name $n -ErrorAction Stop).$n } catch {} } }
New-Item -Path $key -Force | Out-Null
function Set-S([string]$n, [int]$v) { New-ItemProperty -Path $key -Name $n -Value $v -PropertyType DWord -Force | Out-Null }
$log = Join-Path $env:TEMP 'ssi263_sapi.log'
if (Test-Path $log) { Remove-Item $log }
function Say([string]$tag, [string]$voice = 'Braille Lite 2000 (June 2003)', [string]$text = 'Is it ready?') {
    $s = New-Object System.Speech.Synthesis.SpeechSynthesizer
    $s.SelectVoice($voice)
    $wav = Join-Path $env:TEMP "sapi_setting_$tag.wav"
    $s.SetOutputToWaveFile($wav); $s.Speak($text); $s.SetOutputToNull(); $s.Dispose()
    [System.IO.File]::ReadAllBytes($wav)
}
function Same($a, $b) { if ($a.Length -ne $b.Length) { return $false }; for ($i = 0; $i -lt $a.Length; $i++) { if ($a[$i] -ne $b[$i]) { return $false } }; $true }
# a canonical PCM WAV: the rate at byte 24, the data chunk's size after its 'data' tag
function WavRate($b) { [BitConverter]::ToInt32($b, 24) }
function WavSeconds($b) {
    for ($i = 12; $i -lt $b.Length - 8; ) {
        $id = [Text.Encoding]::ASCII.GetString($b, $i, 4); $size = [BitConverter]::ToInt32($b, $i + 4)
        if ($id -eq 'data') { return $size / 2.0 / (WavRate $b) }
        $i += 8 + $size
    }
    -1
}
$bad = 0
try {
    Set-S 'Diagnostics' 1; Set-S 'Inflection' 1; Set-S 'Whine' 0; Set-S 'AccentInflection' 100; Set-S 'SampleRate' 22050
    Set-S 'BrailleLiteNumbers' 1; Set-S 'LineLift' 0
    $default = Say 'default'
    # every boot setting changed boots the units again; the control must be fresh too, so toggle one and come back
    Set-S 'Whine' 1; $null = Say 'toggle'; Set-S 'Whine' 0
    $again = Say 'again'
    Set-S 'Inflection' 0
    $flat = Say 'inflection_off'
    Set-S 'Inflection' 1; Set-S 'Whine' 2
    $whine = Say 'whine'
    Set-S 'Whine' 0
    $spanish = 'Braille Lite 2000 (espa' + [char]0x00F1 + 'ol)'     # this file stays ASCII (PowerShell 5.1 reads it as ANSI)
    $esText = 'Hola, como estas?'
    # run ahead: each voice's first utterance on freshly booted units (the Whine change above reboots them), against its
    # default, also its first on fresh units -- so a setting the DLL dropped compares like with like and fails
    Set-S 'RunAhead' 1
    $ahead = Say 'run_ahead'
    $esAhead = Say 'es_run_ahead' $spanish $esText
    Set-S 'RunAhead' 0
    Set-S 'Whine' 1; $null = Say 'toggle_es'; Set-S 'Whine' 0
    $esDefault = Say 'es_default' $spanish $esText
    # "Read numbers as words" (BrailleLiteNumbers): each voice's number on freshly booted units (the Whine toggle
    # reboots them), 1 against 0 (must differ) and against no value at all (must be the same: the default is on)
    $english = 'Braille Lite 2000 (June 2003)'; $numEn = '1,234,567'; $numEs = '1.234.567'
    Set-S 'Whine' 1; $null = Say 'toggle_n1'; Set-S 'Whine' 0
    $numOn = Say 'numbers_on' $english $numEn; $esNumOn = Say 'es_numbers_on' $spanish $numEs
    Set-S 'BrailleLiteNumbers' 0
    Set-S 'Whine' 1; $null = Say 'toggle_n0'; Set-S 'Whine' 0
    $numOff = Say 'numbers_off' $english $numEn; $esNumOff = Say 'es_numbers_off' $spanish $numEs
    Remove-ItemProperty -Path $key -Name 'BrailleLiteNumbers'
    Set-S 'Whine' 1; $null = Say 'toggle_nx'; Set-S 'Whine' 0
    $numUnset = Say 'numbers_unset' $english $numEn; $esNumUnset = Say 'es_numbers_unset' $spanish $numEs
    Set-S 'BrailleLiteNumbers' 1
    # "Lift line starts": a fresh unit's first line (the Whine toggle reboots them) is lifted, against the default's
    Set-S 'LineLift' 1
    Set-S 'Whine' 1; $null = Say 'toggle_lift'; Set-S 'Whine' 0
    $lifted = Say 'line_lift'
    Set-S 'LineLift' 0
    $accent = Say 'accent_default' 'Accent-mini'
    Set-S 'AccentInflection' 0
    $accentFlat = Say 'accent_inflection0' 'Accent-mini'
    Set-S 'AccentInflection' 100
    $rates = @{}
    foreach ($r in 11025, 22050, 44100) { Set-S 'SampleRate' $r; $rates[$r] = Say "rate_$r" }
    Set-S 'SampleRate' 22050
    $checks = @(@('the same settings twice give identical audio', (Same $default $again)),
                @('inflection off changes the sound', -not (Same $default $flat)),
                @('the whine changes the sound', -not (Same $default $whine)),
                @('run ahead changes the Braille Lite''s sound (English)', -not (Same $default $ahead)),
                @('run ahead changes the Braille Lite''s sound (Spanish)', -not (Same $esDefault $esAhead)),
                @('numbers as words off changes the Braille Lite''s "1,234,567" (English)', -not (Same $numOn $numOff)),
                @('numbers as words off changes the Braille Lite''s "1.234.567" (Spanish)', -not (Same $esNumOn $esNumOff)),
                @('numbers as words with no value is on (English)', (Same $numOn $numUnset)),
                @('numbers as words with no value is on (Spanish)', (Same $esNumOn $esNumUnset)),
                @('lift line starts changes the Braille Lite''s first line', -not (Same $default $lifted)),
                @('the Accent''s inflection 0 changes its sound', -not (Same $accent $accentFlat)))
    # System.Speech writes its WAV in its own default format and converts what the engine gives it, so the file's
    # header cannot show the engine's rate.  Two checks instead: the engine's own log (the last three utterances)
    # shows the bytes it produced scaling with the rate, and the file lasts as long at every rate -- which it only
    # does if SAPI read the rate the engine declared (a wrong declaration plays the audio fast or slow).
    $written = @(Get-Content $log | Select-String 'bytes-written=(\d+)' | ForEach-Object { [int]$_.Matches[0].Groups[1].Value })
    $n = $written.Count
    $bytes = @{ 11025 = $written[$n - 3]; 22050 = $written[$n - 2]; 44100 = $written[$n - 1] }
    $base = WavSeconds $rates[22050]
    foreach ($r in 11025, 22050, 44100) {
        $secs = WavSeconds $rates[$r]; $ratio = $bytes[$r] / [double]$bytes[22050]
        $checks += ,@(('sample rate {0}: the engine wrote {1} bytes ({2:N3} x the 22 kHz), the file lasts {3:N3} s (22 kHz: {4:N3} s)' -f $r, $bytes[$r], $ratio, $secs, $base),
                      (([Math]::Abs($ratio - $r / 22050.0) -le 0.05 * $r / 22050.0) -and ([Math]::Abs($secs - $base) -le 0.05 * $base)))
    }
    foreach ($c in $checks) { if (-not $c[1]) { $bad++ }; Write-Host ('{0,-4} {1}' -f $(if ($c[1]) { 'ok' } else { 'FAIL' }), $c[0]) }
} finally {
    foreach ($n in $names) {
        if ($saved.ContainsKey($n)) { Set-S $n $saved[$n] } else { Remove-ItemProperty -Path $key -Name $n -ErrorAction SilentlyContinue }
    }
}
if (Test-Path $log) { Get-Content $log | Select-String 'speak done' | ForEach-Object { Write-Host ('log: ' + ($_.Line -replace '^.*speak done: ', '')) } }
if ($bad) { exit 1 } else { exit 0 }
