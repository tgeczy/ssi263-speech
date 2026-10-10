# SSI-263 SAPI settings -- the voices, their registration, and the settings SAPI's own requests cannot carry.
#
# A trimmed sibling of outspoken-nvda's sapi/settings.ps1 (itself Panthera's), with the same layout: the voice
# list comes from voices.txt (what the native voices found in the installed firmware), registration goes through
# register.ps1 elevated, and the engine settings sit below.  Rate, pitch and volume stay SAPI's own.  The
# settings are this person's (HKCU "Software\SSI-263 SAPI"); the engine DLL reads them before every utterance and
# boots its units again when one they were booted with changes, so a change reaches the next thing spoken, in every
# SAPI program at once.
#
# PowerShell 2.0's dialect, like register.ps1: stock Windows 7 has no newer engine.
# -Check: build the dialog and fill it, print what it shows, and exit without showing it (the tests).
param([switch]$Check)
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()

$stage = Split-Path -Parent $MyInvocation.MyCommand.Path
$registerScript = Join-Path $stage 'register.ps1'
$prefKey = 'HKCU:\Software\SSI-263 SAPI'
$tokenRoots = @('HKLM:\SOFTWARE\Microsoft\Speech\Voices\Tokens', 'HKLM:\SOFTWARE\Wow6432Node\Microsoft\Speech\Voices\Tokens')

function Load-Setting([string]$name, [int]$default) {
    try { [int](Get-ItemProperty -Path $prefKey -Name $name -ErrorAction Stop).$name }
    catch { $default }
}
function Save-Setting([string]$name, [int]$value) {
    New-Item -Path $prefKey -Force | Out-Null
    New-ItemProperty -Path $prefKey -Name $name -Value $value -PropertyType DWord -Force | Out-Null
}

# The voices this installation carries, as the native library listed them from its firmware (voices.txt, made by
# sapi\build.ps1): "id<TAB>name<TAB>language", UTF-8.
function Get-ServerVoices {
    $voices = @()
    try {
        $out = [System.IO.File]::ReadAllText((Join-Path $stage 'voices.txt'), [System.Text.Encoding]::UTF8)
        foreach ($line in ($out -split "`r?`n")) {
            if ($line -match "`t") { $voices += ,($line.Split("`t")) }
        }
    } catch {}
    ,$voices
}
function Test-Registered([string]$id) {
    $key = 'SSI263_' + ($id -replace '[^A-Za-z0-9]', '_')
    foreach ($root in $tokenRoots) {
        if (Test-Path (Join-Path $root $key)) { return $true }
    }
    $false
}

# A cancelled elevation prompt is not a yes: -1, and the caller says nothing (outspoken's lesson).
function Invoke-Elevated([string]$switches) {
    $arguments = '-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File "{0}" {1}' -f $registerScript, $switches
    $process = $null
    try {
        $process = Start-Process powershell.exe -Verb RunAs -Wait -PassThru -ArgumentList $arguments -ErrorAction Stop
    } catch { return -1 }
    if (-not $process) { return -1 }
    $process.ExitCode
}

$form = New-Object Windows.Forms.Form
$form.Text = 'SSI-263 SAPI settings'; $form.Size = New-Object Drawing.Size(640, 678)
$form.StartPosition = 'CenterScreen'
$label = New-Object Windows.Forms.Label
$label.Text = '&Voices:'; $label.AutoSize = $true; $label.Location = New-Object Drawing.Point(12, 14)
$list = New-Object Windows.Forms.ListBox
$list.Name = 'voiceList'; $list.AccessibleName = 'Voices'
$list.AccessibleDescription = 'The emulated SSI-263 voices this installation carries, and whether each is registered with SAPI'
$list.Location = New-Object Drawing.Point(12, 38); $list.Size = New-Object Drawing.Size(600, 120)
$status = New-Object Windows.Forms.Label
$status.Name = 'status'; $status.AccessibleName = 'Status'
$status.Location = New-Object Drawing.Point(12, 166); $status.Size = New-Object Drawing.Size(600, 40)

function Refresh-Voices {
    $list.Items.Clear()
    $voices = Get-ServerVoices
    $registered = 0
    foreach ($v in $voices) {
        $state = 'not registered'
        if (Test-Registered $v[0]) { $state = 'registered'; $registered++ }
        [void]$list.Items.Add(('{0} - {1}' -f $v[1], $state))
    }
    if ($list.Items.Count) { $list.SelectedIndex = 0 }
    if (-not $voices.Count) {
        $status.Text = 'The voices could not be listed: the installation may be damaged. Reinstall SSI-263 SAPI.'
    } elseif ($registered -eq $voices.Count) {
        $status.Text = 'All {0} voices are registered with SAPI, for 32-bit and 64-bit programs.' -f $voices.Count
    } elseif ($registered -eq 0) {
        $status.Text = 'No voice is registered with SAPI. Use Register to make them available.'
    } else {
        $status.Text = '{0} of {1} voices are registered. Use Register to add the rest.' -f $registered, $voices.Count
    }
}

$register = New-Object Windows.Forms.Button; $register.Text = '&Register'; $register.AutoSize = $true
$register.Location = New-Object Drawing.Point(12, 214)
$unregister = New-Object Windows.Forms.Button; $unregister.Text = '&Unregister'; $unregister.AutoSize = $true
$unregister.Location = New-Object Drawing.Point(110, 214)
$close = New-Object Windows.Forms.Button; $close.Text = '&Close'; $close.AutoSize = $true
$close.Location = New-Object Drawing.Point(208, 214)

# The Braille Lite's four settings, as in the NVDA add-on.
$group = New-Object Windows.Forms.GroupBox
$group.Text = 'Braille Lite'; $group.Location = New-Object Drawing.Point(12, 256); $group.Size = New-Object Drawing.Size(600, 198)
$inflection = New-Object Windows.Forms.CheckBox
$inflection.Text = 'Voice &inflection'
$inflection.AccessibleName = 'Voice inflection'
$inflection.AccessibleDescription = 'The unit''s own status-menu setting. Off, questions and sentence ends stay flat.'
$inflection.Location = New-Object Drawing.Point(12, 24); $inflection.AutoSize = $true
$whineLabel = New-Object Windows.Forms.Label
$whineLabel.Text = 'Unit &hiss and whine:'; $whineLabel.AutoSize = $true
$whineLabel.Location = New-Object Drawing.Point(12, 60)
$whine = New-Object Windows.Forms.ComboBox
$whine.DropDownStyle = 'DropDownList'; $whine.AccessibleName = 'Unit hiss and whine'
$whine.AccessibleDescription = 'The faint sound a real Braille Lite makes under its speech, generated from the chip''s clock. Off by default.'
$whine.Location = New-Object Drawing.Point(150, 57); $whine.Size = New-Object Drawing.Size(300, 24)
foreach ($item in @('Off', 'Hiss (even volumes, as the factory setting)', 'Whine (odd volumes)')) { [void]$whine.Items.Add($item) }
# The NVDA add-on's "Run the unit ahead": experimental, off by default; both Braille Lite voices.
$runAhead = New-Object Windows.Forms.CheckBox
$runAhead.Text = 'Run the unit &ahead (experimental)'
$runAhead.AccessibleName = 'Run the unit ahead (experimental)'
$runAhead.AccessibleDescription = 'Experimental, off by default. The unit writes a whole utterance at once and the chip plays it, with shorter pauses. Takes effect with the next thing spoken.'
$runAhead.Location = New-Object Drawing.Point(12, 94); $runAhead.AutoSize = $true
# The NVDA add-on's "Custom number processing" (numberWords), on by default; both Braille Lite voices.  The Accents
# and the Mockingboard keep their own (on); it has nothing else here (rate, pitch and volume are SAPI's).
$numbers = New-Object Windows.Forms.CheckBox
$numbers.Text = 'Read numbers as &words'
$numbers.AccessibleName = 'Read numbers as words'
$numbers.AccessibleDescription = 'On by default. Numbers are read as words, as the NVDA add-on''s custom number processing does; off, the unit''s firmware reads them itself. Takes effect with the next thing spoken.'
$numbers.Location = New-Object Drawing.Point(12, 128); $numbers.AutoSize = $true
# The NVDA add-on's "Lift line starts (as note-taking mode)", off by default; both Braille Lite voices.
$lineLift = New-Object Windows.Forms.CheckBox
$lineLift.Text = '&Lift line starts (as note-taking mode)'
$lineLift.AccessibleName = 'Lift line starts (as note-taking mode)'
$lineLift.AccessibleDescription = 'Off by default. Like the unit when you move by line, the start of a line spoken after you interrupt speech, or after a pause, slides up in pitch. Reading on is never lifted. Takes effect with the next thing spoken.'
$lineLift.Location = New-Object Drawing.Point(12, 162); $lineLift.AutoSize = $true
# The Accent's own voice setting, as its NVDA add-on's Inflection slider (five steps).
$accentGroup = New-Object Windows.Forms.GroupBox
$accentGroup.Text = 'Accent'; $accentGroup.Location = New-Object Drawing.Point(12, 462); $accentGroup.Size = New-Object Drawing.Size(600, 60)
$accentLabel = New-Object Windows.Forms.Label
$accentLabel.Text = 'Accent i&nflection:'; $accentLabel.AutoSize = $true
$accentLabel.Location = New-Object Drawing.Point(12, 26)
$accentInfl = New-Object Windows.Forms.ComboBox
$accentInfl.DropDownStyle = 'DropDownList'; $accentInfl.AccessibleName = 'Accent inflection'
$accentInfl.AccessibleDescription = 'How much the Accent card varies its pitch, as the Inflection slider of its NVDA add-on. 100, the card''s own setting at power-up, is the default.'
$accentInfl.Location = New-Object Drawing.Point(150, 23); $accentInfl.Size = New-Object Drawing.Size(300, 24)
$accentSteps = @(100, 75, 50, 25, 0)
foreach ($item in @('100 (the card''s own, the default)', '75', '50', '25', '0')) { [void]$accentInfl.Items.Add($item) }
# Every voice's output rate, as the add-ons' Sample rate.
$rateLabel = New-Object Windows.Forms.Label
$rateLabel.Text = '&Sample rate (every voice):'; $rateLabel.AutoSize = $true
$rateLabel.Location = New-Object Drawing.Point(12, 538)
$rate = New-Object Windows.Forms.ComboBox
$rate.DropDownStyle = 'DropDownList'; $rate.AccessibleName = 'Sample rate, every voice'
$rate.AccessibleDescription = '22 kHz keeps everything the chip produces. 44 kHz keeps the clock images and the brightest hiss, 11 kHz sounds like a unit''s own speaker. Takes effect with the next thing spoken.'
$rate.Location = New-Object Drawing.Point(190, 535); $rate.Size = New-Object Drawing.Size(260, 24)
$rateSteps = @(11025, 22050, 44100)
foreach ($item in @('11 kHz', '22 kHz (default)', '44 kHz')) { [void]$rate.Items.Add($item) }
$diagnostics = New-Object Windows.Forms.CheckBox
$diagnostics.Text = 'Write a &diagnostic log'
$diagnostics.AccessibleName = 'Write a diagnostic log'
$diagnostics.AccessibleDescription = 'Off by default. Records what the engine did, not what was spoken, to a file in your temp folder. Turn it on only if a bug report asks for it.'
$diagnostics.Location = New-Object Drawing.Point(12, 574); $diagnostics.AutoSize = $true

$inflection.Checked = [bool](Load-Setting 'Inflection' 1)
$whine.SelectedIndex = [Math]::Max(0, [Math]::Min(2, (Load-Setting 'Whine' 0)))
$runAhead.Checked = [bool](Load-Setting 'RunAhead' 0)
$numbers.Checked = [bool](Load-Setting 'BrailleLiteNumbers' 1)
$lineLift.Checked = [bool](Load-Setting 'LineLift' 0)
$ai = [Array]::IndexOf($accentSteps, (Load-Setting 'AccentInflection' 100)); if ($ai -lt 0) { $ai = 0 }
$accentInfl.SelectedIndex = $ai
$ri = [Array]::IndexOf($rateSteps, (Load-Setting 'SampleRate' 22050)); if ($ri -lt 0) { $ri = 1 }
$rate.SelectedIndex = $ri
$diagnostics.Checked = [bool](Load-Setting 'Diagnostics' 0)
$inflection.Add_CheckedChanged({ Save-Setting 'Inflection' ([int]$inflection.Checked) })
$whine.Add_SelectedIndexChanged({ if ($whine.SelectedIndex -ge 0) { Save-Setting 'Whine' $whine.SelectedIndex } })
$runAhead.Add_CheckedChanged({ Save-Setting 'RunAhead' ([int]$runAhead.Checked) })
$numbers.Add_CheckedChanged({ Save-Setting 'BrailleLiteNumbers' ([int]$numbers.Checked) })
$lineLift.Add_CheckedChanged({ Save-Setting 'LineLift' ([int]$lineLift.Checked) })
$accentInfl.Add_SelectedIndexChanged({ if ($accentInfl.SelectedIndex -ge 0) { Save-Setting 'AccentInflection' $accentSteps[$accentInfl.SelectedIndex] } })
$rate.Add_SelectedIndexChanged({ if ($rate.SelectedIndex -ge 0) { Save-Setting 'SampleRate' $rateSteps[$rate.SelectedIndex] } })
$diagnostics.Add_CheckedChanged({ Save-Setting 'Diagnostics' ([int]$diagnostics.Checked) })

$register.Add_Click({
    $code = Invoke-Elevated '-Register'
    if ($code -gt 0) { [Windows.Forms.MessageBox]::Show($form, 'Registration failed.', 'SSI-263 SAPI', 'OK', 'Error') | Out-Null }
    elseif ($code -eq 0) { [Windows.Forms.MessageBox]::Show($form, 'The SSI-263 voices were registered for 32-bit and 64-bit SAPI.', 'SSI-263 SAPI') | Out-Null }
    Refresh-Voices
})
$unregister.Add_Click({
    $code = Invoke-Elevated '-Unregister'
    if ($code -gt 0) { [Windows.Forms.MessageBox]::Show($form, 'Unregistration failed.', 'SSI-263 SAPI', 'OK', 'Error') | Out-Null }
    elseif ($code -eq 0) { [Windows.Forms.MessageBox]::Show($form, 'The SSI-263 voices were unregistered.', 'SSI-263 SAPI') | Out-Null }
    Refresh-Voices
})
$close.Add_Click({ $form.Close() })
$form.CancelButton = $close
$group.Controls.AddRange(@($inflection, $whineLabel, $whine, $runAhead, $numbers, $lineLift))
$accentGroup.Controls.AddRange(@($accentLabel, $accentInfl))
$form.Controls.AddRange(@($label, $list, $status, $register, $unregister, $close, $group, $accentGroup, $rateLabel, $rate, $diagnostics))
Refresh-Voices
if ($Check) {
    foreach ($item in $list.Items) { Write-Output ('voice: ' + $item) }
    Write-Output ('status: ' + $status.Text)
    Write-Output ('inflection: {0}; whine: {1}; run ahead: {2}; numbers as words: {3}; accent inflection: {4}; sample rate: {5}; diagnostics: {6}; line lift: {7}' -f $inflection.Checked, $whine.SelectedItem, $runAhead.Checked, $numbers.Checked, $accentInfl.SelectedItem, $rate.SelectedItem, $diagnostics.Checked, $lineLift.Checked)
    exit 0
}
$form.Add_Shown({ $list.Focus() })
[void]$form.ShowDialog()
