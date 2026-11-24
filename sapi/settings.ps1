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
# -ShowDataPlan: print the data root (dataroot.ps1), whether it holds a voice's file, and what "Move voices for all
#   users" would do, then exit (sapi\test_sapi_data.py).
# -ShowRegisterArguments: print what the Register button hands register.ps1, then exit (sapi\test_sapi_data.py).
# -ExtractFrom <file> -ExtractTo <file>: the Super Voice's program from a file as Import a file reads it (sapi\
#   test_sapi_data.py); -DownloadTo <file>: from its disk as Download fetches it, the sources SSI263_SAPI_DATA_TEST_SOURCES
#   names when it is set ("url|size|sha256|kind;...", a test's file: URLs), else the pinned ones.
param([switch]$Check, [switch]$ShowDataPlan, [switch]$ShowRegisterArguments, [string]$ExtractFrom = '', [string]$ExtractTo = '', [string]$DownloadTo = '')
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()

$stage = Split-Path -Parent $MyInvocation.MyCommand.Path
$registerScript = Join-Path $stage 'register.ps1'
. (Join-Path $stage 'updates.ps1')     # Check for updates, by hand only
. (Join-Path $stage 'dataroot.ps1')    # where the voices no package carries find their files (Panthera's system)
$userVoices = Get-UserDataVoices $stage
if ($env:SSI263_SAPI_DATA_TEST_SOURCES) {
    $SuperVoiceSources = @(foreach ($s in $env:SSI263_SAPI_DATA_TEST_SOURCES.Split(';')) {
        $f = $s.Split('|'); ,@('test', $f[0], [int]$f[1], $f[2], $f[3]) })
}
if ($ShowDataPlan) {
    $root = Resolve-DataRoot $userVoices
    $plan = Get-MigrationPlan $root $userVoices
    Write-Output ('root: ' + $root)
    Write-Output ('has data: ' + (Test-RootHasData $root $userVoices))
    Write-Output ('plan: {0} | {1} | {2} | {3}' -f $plan.Action, $plan.From, $plan.To, $plan.Reason)
    exit 0
}
if ($ExtractFrom -or $DownloadTo) {
    try {
        if ($DownloadTo) { $data = (Get-SuperVoiceDisk $null).Bytes; $ExtractTo = $DownloadTo }
        else { $data = [System.IO.File]::ReadAllBytes($ExtractFrom) }
        [System.IO.File]::WriteAllBytes($ExtractTo, (Get-SuperVoiceProgram $stage $data))
        Write-Output 'extracted'
        exit 0
    } catch { Write-Output ('refused: ' + $_.Exception.Message); exit 1 }
}
$prefKey = 'HKCU:\Software\SSI-263 SAPI'
$tokenRoots = @('HKLM:\SOFTWARE\Microsoft\Speech\Voices\Tokens', 'HKLM:\SOFTWARE\Wow6432Node\Microsoft\Speech\Voices\Tokens')

function Load-Setting([string]$name, [int]$default) {
    try { [int](Get-ItemProperty -Path $prefKey -Name $name -ErrorAction Stop).$name }
    catch { $default }
}
function Save-Setting([string]$name, [int]$value) {
    # made only when it is not there: New-Item -Force on an existing key empties it, every other setting with it (#39)
    if (-not (Test-Path $prefKey)) { New-Item -Path $prefKey -Force | Out-Null }
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

# A path as one argument: quoted, a trailing backslash doubled so it does not escape the closing quote (Panthera #35:
# a drive root's own backslash swallowed the quote, and the rest of the line came with it).
function ConvertTo-Argument([string]$value) {
    '"' + ($value -replace '(\\+)$', '$1$1') + '"'
}
# The Register button's arguments.  register.ps1 runs elevated, and that may be another account, whose HKCU and
# %APPDATA% are not this person's: with no token left (after Unregister) it looked for the Super Voice's program in the
# administrator's own folders, registered without it, and said the voices were registered (found by the 0.9.5 audit).
# So the data root is resolved here, in this person's account, and handed over while it holds a voice's file, as
# Import and Move do.  SSI263_SAPI_SETTINGS_BREAK=register-root, the tests' control: the bare -Register it was.
function Get-RegisterArguments {
    $root = Resolve-DataRoot $userVoices
    if ($env:SSI263_SAPI_SETTINGS_BREAK -ne 'register-root' -and (Test-RootHasData $root $userVoices)) {
        return ('-Register -DataRoot {0}' -f (ConvertTo-Argument $root))
    }
    '-Register'
}
if ($ShowRegisterArguments) { Write-Output ('register: ' + (Get-RegisterArguments)); exit 0 }

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
$form.Text = 'SSI-263 SAPI settings'; $form.Size = New-Object Drawing.Size(640, 808)
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
$unregister = New-Object Windows.Forms.Button; $unregister.Text = 'Unre&gister'; $unregister.AutoSize = $true
$unregister.Location = New-Object Drawing.Point(110, 214)
$updates = New-Object Windows.Forms.Button; $updates.Text = 'Check for &updates...'; $updates.AutoSize = $true
$updates.AccessibleName = 'Check for updates'
$updates.Location = New-Object Drawing.Point(208, 214)
$close = New-Object Windows.Forms.Button; $close.Text = '&Close'; $close.AutoSize = $true
$close.Location = New-Object Drawing.Point(370, 214)

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

# Speech data: the voices no package carries (Speech Systems' Super Voice), from a data folder (dataroot.ps1).
$dataGroup = New-Object Windows.Forms.GroupBox
$dataGroup.Text = 'Speech data (voices no package carries)'
$dataGroup.Location = New-Object Drawing.Point(12, 606); $dataGroup.Size = New-Object Drawing.Size(600, 150)
$dataInfo = New-Object Windows.Forms.TextBox
$dataInfo.Name = 'dataInfo'; $dataInfo.AccessibleName = 'Speech data'
$dataInfo.Multiline = $true; $dataInfo.ReadOnly = $true
$dataInfo.Location = New-Object Drawing.Point(12, 22); $dataInfo.Size = New-Object Drawing.Size(576, 56)
$svDownload = New-Object Windows.Forms.Button; $svDownload.Text = 'Download the Su&per Voice...'; $svDownload.AutoSize = $true
$svDownload.AccessibleName = 'Download the Super Voice'
$svDownload.AccessibleDescription = 'Fetches Speech Systems'' Super Voice disk from the Color Computer Archive, checks it, and keeps only its program in the data folder.'
$svDownload.Location = New-Object Drawing.Point(12, 84)
$svImport = New-Object Windows.Forms.Button; $svImport.Text = 'Import a &file...'; $svImport.AutoSize = $true
$svImport.AccessibleName = 'Import a file'
$svImport.AccessibleDescription = 'Your own copy: SUPERTOK.BIN, the disk image SUPERV.DSK, or a zip holding either.'
$svImport.Location = New-Object Drawing.Point(200, 84)
$dataLocation = New-Object Windows.Forms.Button; $dataLocation.Text = 'Data l&ocation...'; $dataLocation.AutoSize = $true
$dataLocation.AccessibleName = 'Data location'
$dataLocation.Location = New-Object Drawing.Point(320, 84)
$dataMove = New-Object Windows.Forms.Button; $dataMove.Text = '&Move voices for all users...'; $dataMove.AutoSize = $true
$dataMove.AccessibleName = 'Move voices for all users'
$dataMove.Location = New-Object Drawing.Point(12, 116)
$dataOpen = New-Object Windows.Forms.Button; $dataOpen.Text = 'Open data fold&er'; $dataOpen.AutoSize = $true
$dataOpen.AccessibleName = 'Open data folder'
$dataOpen.Location = New-Object Drawing.Point(200, 116)
$dataGroup.Controls.AddRange(@($dataInfo, $svDownload, $svImport, $dataLocation, $dataMove, $dataOpen))

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
    $code = Invoke-Elevated (Get-RegisterArguments)
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
# Check for updates (updates.ps1): the newest installer newer than the installed one, from its own name across the
# releases; downloaded, checked against its release's SHA256SUMS.txt, then run -- its own pages and UAC prompt ask.
$updates.Add_Click({
    $title = 'SSI-263 SAPI'
    $installed = Get-InstalledSapiVersion
    if (-not $installed) {
        [Windows.Forms.MessageBox]::Show($form, 'No installed copy of the SSI-263 SAPI voices was found to compare against. Install them with their setup program first.', $title, 'OK', 'Information') | Out-Null
        return
    }
    $status.Text = 'Checking for updates...'
    $form.Refresh()
    $offer = $null; $problem = $null
    try { $offer = Get-UpdateOffer $installed } catch { $problem = $_.Exception.Message }
    Refresh-Voices
    if ($problem) {
        [Windows.Forms.MessageBox]::Show($form, ("Could not check for updates:`n`n{0}" -f $problem), $title, 'OK', 'Warning') | Out-Null
        return
    }
    if (-not $offer) {
        [Windows.Forms.MessageBox]::Show($form, ("You have the newest version, {0}." -f $installed), $title, 'OK', 'Information') | Out-Null
        return
    }
    $answer = [Windows.Forms.MessageBox]::Show($form, ("A newer version is available: {0}. You have {1}.`n`nDownload it, check it against the release's checksum, and run its installer? The installer asks before changing anything." -f $offer.version, $installed), $title, 'YesNo', 'Question')
    if ($answer -ne 'Yes') { return }
    $status.Text = 'Downloading the update...'
    $form.Refresh()
    $file = $null
    try { $file = Save-UpdateInstaller $offer $env:TEMP } catch { $problem = $_.Exception.Message }
    Refresh-Voices
    if ($problem) {
        [Windows.Forms.MessageBox]::Show($form, ("Nothing was installed. {0}" -f $problem), $title, 'OK', 'Warning') | Out-Null
        return
    }
    try { Start-Process -FilePath $file } catch {
        [Windows.Forms.MessageBox]::Show($form, ("The update was downloaded and checked, but its installer could not be started. It is saved at:`n`n{0}" -f $file), $title, 'OK', 'Warning') | Out-Null
    }
})
# ---- speech data (dataroot.ps1) ---------------------------------------------------------------------------------------
function Refresh-Data {
    $root = Resolve-DataRoot $userVoices
    $lines = @('Data folder: ' + $root)
    foreach ($v in $userVoices) {
        $here = Test-Path -LiteralPath (Join-Path $root $v.File)
        $state = if ($here -and (Test-Registered $v.Id)) { 'here, and registered' }
                 elseif ($here) { 'here, not registered yet (Register)' }
                 else { 'not here: download it or import your own copy' }
        $lines += ('{0}: {1}' -f $v.Name, $state)
    }
    $dataInfo.Text = $lines -join "`r`n"
}

# The program, read and checked here (the library's own reader), into the data folder and registered: one elevated
# trip, which checks it once more (register.ps1 -ImportFile).
function Install-SuperVoice([byte[]]$prog, [string]$from) {
    $root = Resolve-DataRoot $userVoices
    $tmp = Join-Path $env:TEMP ('ssi263-supertok-{0}.bin' -f [guid]::NewGuid())
    [System.IO.File]::WriteAllBytes($tmp, $prog)
    try {
        $code = Invoke-Elevated ('-ImportFile {0} -DataRoot {1} -Register' -f (ConvertTo-Argument $tmp), (ConvertTo-Argument $root))
    } finally { Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue }
    Refresh-Voices; Refresh-Data
    if ($code -lt 0) { return }
    if ($code -ne 0) {
        [Windows.Forms.MessageBox]::Show($form, ("The Super Voice's program could not be put in {0} (code {1})." -f $root, $code), 'SSI-263 SAPI', 'OK', 'Error') | Out-Null
        return
    }
    [Windows.Forms.MessageBox]::Show($form, ("Speech Systems Super Voice is installed (from {0}) and registered: choose it in your program's voice list. Its program is in {1}." -f $from, $root), 'SSI-263 SAPI') | Out-Null
}

$svDownload.Add_Click({
    $title = 'SSI-263 SAPI'
    $answer = [Windows.Forms.MessageBox]::Show($form, "Download Speech Systems' Super Voice disk from the Color Computer Archive (about 28 KB)? It is checked against its known checksum, and only its program, SUPERTOK.BIN, is kept. Nothing about you is sent with the request.", $title, 'YesNo', 'Question')
    if ($answer -ne 'Yes') { return }
    $status.Text = 'Downloading the Super Voice''s disk...'; $form.Refresh()
    try {
        $disk = Get-SuperVoiceDisk $null
        $prog = Get-SuperVoiceProgram $stage $disk.Bytes
    } catch {
        Refresh-Voices
        [Windows.Forms.MessageBox]::Show($form, ("Nothing was installed. {0}" -f $_.Exception.Message), $title, 'OK', 'Warning') | Out-Null
        return
    }
    Install-SuperVoice $prog $disk.Where
})
$svImport.Add_Click({
    $dlg = New-Object Windows.Forms.OpenFileDialog
    $dlg.Title = 'Choose the Super Voice''s program, its disk image, or a zip holding either'
    $dlg.Filter = 'Super Voice files (*.bin;*.dsk;*.zip)|*.bin;*.dsk;*.zip|All files (*.*)|*.*'
    if ($dlg.ShowDialog($form) -ne 'OK') { return }
    try {
        if ((Get-Item -LiteralPath $dlg.FileName).Length -gt 64MB) { throw 'it is too large to be the disk or a zip holding it' }
        $prog = Get-SuperVoiceProgram $stage ([System.IO.File]::ReadAllBytes($dlg.FileName))
    } catch {
        [Windows.Forms.MessageBox]::Show($form, ("Nothing was installed from {0}. {1}" -f $dlg.FileName, $_.Exception.Message), 'SSI-263 SAPI', 'OK', 'Warning') | Out-Null
        return
    }
    Install-SuperVoice $prog ([System.IO.Path]::GetFileName($dlg.FileName))
})
# Data location (Panthera's Invoke-ChooseRoot): this person's choice; the registered voices follow it when it holds them.
$dataLocation.Add_Click({
    $dlg = New-Object Windows.Forms.FolderBrowserDialog
    $dlg.Description = 'Choose the folder that holds the voices no package carries (speech-systems-super-voice\SUPERTOK.BIN below it).'
    if ($dlg.ShowDialog($form) -ne 'OK') { return }
    $chosen = $dlg.SelectedPath
    if (-not (Test-RootHasData $chosen $userVoices)) {
        $a = [Windows.Forms.MessageBox]::Show($form, ("{0} holds no voice's file yet. Use it anyway? Downloads and imports will go there." -f $chosen), 'SSI-263 SAPI', 'YesNo', 'Question')
        if ($a -ne 'Yes') { return }
    }
    Set-ChosenDataPath $chosen
    if (@($userVoices | Where-Object { Test-Registered $_.Id }).Count -and (Test-RootHasData $chosen $userVoices)) {
        Invoke-Elevated ('-Register -DataRoot {0}' -f (ConvertTo-Argument $chosen)) | Out-Null
    }
    Refresh-Voices; Refresh-Data
})
# Move voices for all users (Panthera's Invoke-Migration): a button, never a prompt; each refusal says why.
$dataMove.Add_Click({
    $root = Resolve-DataRoot $userVoices
    $plan = Get-MigrationPlan $root $userVoices
    if ($plan.Action -ne 'migrate') {
        [Windows.Forms.MessageBox]::Show($form, ("Nothing was moved: {0}." -f $plan.Reason), 'SSI-263 SAPI', 'OK', 'Information') | Out-Null
        return
    }
    $a = [Windows.Forms.MessageBox]::Show($form, ("Move the voices from {0} to {1}, where every account on this computer and the sign-in screen read them?" -f $plan.From, $plan.To), 'SSI-263 SAPI', 'YesNo', 'Question')
    if ($a -ne 'Yes') { return }
    $code = Invoke-Elevated ('-MigrateData -MigrateFrom {0} -DataRoot {1}' -f (ConvertTo-Argument $plan.From), (ConvertTo-Argument $plan.To))
    Refresh-Voices; Refresh-Data
    if ($code -gt 0) { [Windows.Forms.MessageBox]::Show($form, ("The move did not finish (code {0}); nothing you had is lost." -f $code), 'SSI-263 SAPI', 'OK', 'Error') | Out-Null }
    elseif ($code -eq 0) { [Windows.Forms.MessageBox]::Show($form, ("The voices are in {0} now, for every account." -f $plan.To), 'SSI-263 SAPI') | Out-Null }
})
$dataOpen.Add_Click({
    $root = Resolve-DataRoot $userVoices
    try {
        if (-not (Test-Path -LiteralPath $root)) { New-Item -ItemType Directory -Force -Path $root | Out-Null }
        Invoke-Item -LiteralPath $root
    } catch {
        [Windows.Forms.MessageBox]::Show($form, ("The folder could not be opened: {0}" -f $_.Exception.Message), 'SSI-263 SAPI', 'OK', 'Error') | Out-Null
    }
})

# A registered voice whose folder lost its file (Panthera's Offer-Rebind): registered again from where it is now, or
# its token taken away -- spoken, SetObjectToken refuses it anyway.
function Offer-Rebind {
    $lost = @($userVoices | Where-Object { Test-Registered $_.Id })
    if (-not $lost.Count) { return }
    $root = Resolve-DataRoot $userVoices
    $gone = @()
    foreach ($v in $lost) {
        $key = 'SSI263_' + ($v.Id -replace '[^A-Za-z0-9]', '_')
        $had = ''
        foreach ($r in $tokenRoots) { try { $had = [string](Get-ItemProperty -Path (Join-Path $r $key) -Name DataPath -ErrorAction Stop).DataPath; if ($had) { break } } catch {} }
        if ($had -and -not (Test-Path -LiteralPath (Join-Path $had $v.File))) { $gone += $v }
    }
    if (-not $gone.Count) { return }
    $names = ($gone | ForEach-Object { $_.Name }) -join ', '
    if (Test-RootHasData $root $gone) {
        $a = [Windows.Forms.MessageBox]::Show($form, ("{0}: its program is no longer where it was registered from, but it is in {1}. Register it from there?" -f $names, $root), 'SSI-263 SAPI', 'YesNo', 'Question')
        if ($a -eq 'Yes') { Invoke-Elevated ('-Register -DataRoot {0}' -f (ConvertTo-Argument $root)) | Out-Null }
    } else {
        $a = [Windows.Forms.MessageBox]::Show($form, ("{0}: its program is gone from every data folder, so the voice cannot speak. Take it out of the voice list? Download or import it again to bring it back." -f $names), 'SSI-263 SAPI', 'YesNo', 'Warning')
        if ($a -eq 'Yes') { Invoke-Elevated '-Register' | Out-Null }
    }
    Refresh-Voices; Refresh-Data
}

$close.Add_Click({ $form.Close() })
$form.CancelButton = $close
$group.Controls.AddRange(@($inflection, $whineLabel, $whine, $runAhead, $numbers, $lineLift))
$accentGroup.Controls.AddRange(@($accentLabel, $accentInfl))
$form.Controls.AddRange(@($label, $list, $status, $register, $unregister, $updates, $close, $group, $accentGroup, $rateLabel, $rate, $diagnostics, $dataGroup))
Refresh-Voices
Refresh-Data
if ($Check) {
    foreach ($item in $list.Items) { Write-Output ('voice: ' + $item) }
    Write-Output ('status: ' + $status.Text)
    Write-Output ('data: ' + ($dataInfo.Text -replace "`r`n", ' / '))
    Write-Output ('updates: ' + $updates.Text + ' (installed: ' + $(if (Get-InstalledSapiVersion) { Get-InstalledSapiVersion } else { 'none' }) + ')')
    Write-Output ('inflection: {0}; whine: {1}; run ahead: {2}; numbers as words: {3}; accent inflection: {4}; sample rate: {5}; diagnostics: {6}; line lift: {7}' -f $inflection.Checked, $whine.SelectedItem, $runAhead.Checked, $numbers.Checked, $accentInfl.SelectedItem, $rate.SelectedItem, $diagnostics.Checked, $lineLift.Checked)
    exit 0
}
$form.Add_Shown({ $list.Focus(); Offer-Rebind })
[void]$form.ShowDialog()
