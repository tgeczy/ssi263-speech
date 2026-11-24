# SSI-263 SAPI -- where the voices no package carries find their files: Panthera SAPI's data-folder system, ported
# (the panthera-speech repository's sapi/settings.ps1 Resolve-DataRoot, Get-MigrationPlan, -MigrateData; its README, "The
# MacinTalk data folder is resolved in this order").  Dot-sourced by settings.ps1 and register.ps1, as updates.ps1 is.
#
# The voices this installation carries sit in {app}\firmware, as always.  A voice no package carries (the native
# library's SSV_USER_DATA: Speech Systems' Super Voice, whose disk the Color Computer Archive keeps) is downloaded or
# imported by its user into a DATA ROOT, the repository's firmware\ layout below it
# (<root>\speech-systems-super-voice\SUPERTOK.BIN), and its voice token carries that root (DataPath): the engine reads
# the token, never searches (at the sign-in screen SYSTEM's %APPDATA% is not the user's).
#
# The root, in this order -- each first-class, a folder WITH a voice's file before one that merely exists (two
# passes: Tomi's NVDA folder held only its settings and beat the real data beside it, Panthera's lesson):
#   1. the folder this person chose (Data location: HKCU\Software\SSI-263 SAPI, DataPath)
#   2. the folder set for the whole machine (HKLM\SOFTWARE\SSI-263 SAPI, DataPath; both registry views: the 32-bit
#      engine and NVDA see Wow6432Node)
#   3. NVDA's own: %APPDATA%\nvda\ssi263-speech\firmware -- where the NVDA add-on downloads or imports the Super Voice
#      (Tomi, 2026-11-23), so an NVDA user registers SAPI voices from what they already have, nothing copied twice
#   4. %ProgramData%\ssi263-speech\firmware, every account's (the Move button puts it there)
#   5. %APPDATA%\ssi263-speech\firmware, this person's -- where a fresh download goes when nothing above exists
# Only the per-user folder ever moves; NVDA's never does (a portable NVDA carries its own configuration, and NVDA's
# sign-in screen reads only its copy of that folder).  A moved folder's permissions are reset to SYSTEM and the
# Administrators full, Users read: a same-volume move keeps the descriptor it had.
#
# PowerShell 2.0's dialect, like settings.ps1 and register.ps1: no [pscustomobject], the 32-bit view by its
# Wow6432Node path, SHA-256 and DEFLATE through .NET 2.0.
#
# The tests (sapi\test_sapi_data.py) isolate the registry and the folders: SSI263_SAPI_DATA_KEY (an HKCU subkey in
# place of the user's) and SSI263_SAPI_DATA_MACHINE_KEY (an HKCU subkey in place of the machine's), APPDATA and
# ProgramData set by the test.  Their controls, SSI263_SAPI_DATA_BREAK (never set otherwise): "onepass" takes the first
# folder that exists, files or not; "movenvda" plans NVDA's folder as movable; "nozip" looks into no zip.

$DataUserKey = 'HKCU:\Software\SSI-263 SAPI'
if ($env:SSI263_SAPI_DATA_KEY) { $DataUserKey = 'HKCU:\' + $env:SSI263_SAPI_DATA_KEY }
$DataMachineKeys = @('HKLM:\SOFTWARE\SSI-263 SAPI', 'HKLM:\SOFTWARE\Wow6432Node\SSI-263 SAPI')
if ($env:SSI263_SAPI_DATA_MACHINE_KEY) { $DataMachineKeys = @('HKCU:\' + $env:SSI263_SAPI_DATA_MACHINE_KEY) }
$DataLeaf = 'ssi263-speech'
# the Super Voice's program and the disk it comes off (src/csrc/supervoice/sv_host.h SV_SHA256, sv_dsk.h)
$SuperVoiceSha256 = '4de0d8f369cf3b0cc4f0b01cfc6061f4882118098158a4bca233ce8fbe86155c'
$SuperVoiceRel = 'speech-systems-super-voice\SUPERTOK.BIN'
# (where, url, size, sha256, kind): the same disk in both, byte for byte (docs/super-voice.md; nvda/shared/ssi263_userfw.py)
$SuperVoiceSources = @(
    @('the Color Computer Archive', 'https://colorcomputerarchive.com/repo/Disks/Applications/Super%20Voice%20%28Speech%20Systems%29.zip',
      28456, '46a37e083a002dfda269dc69be4f06f3b87ef4272a444a78c670a98e888b81fa', 'zip'),
    @('apps.irata.online', 'https://apps.irata.online/COCO/Applications/Super%20Voice%20%28Speech%20Systems%29/SUPERV.DSK',
      161280, '7dda3902a42cdc189096e023a0ad27ca6f3aba50c76a38a883892f969987b784', 'dsk'))

function Get-DataBreak { if ($env:SSI263_SAPI_DATA_BREAK) { $env:SSI263_SAPI_DATA_BREAK } else { '' } }

function Get-CommonDataRoot {
    $common = if ($env:ProgramData) { $env:ProgramData } else { $env:ALLUSERSPROFILE }
    if ($common) { Join-Path (Join-Path $common $DataLeaf) 'firmware' } else { $null }
}
function Get-NvdaDataRoot { if ($env:APPDATA) { Join-Path $env:APPDATA "nvda\$DataLeaf\firmware" } else { $null } }
function Get-UserDataRoot { if ($env:APPDATA) { Join-Path (Join-Path $env:APPDATA $DataLeaf) 'firmware' } else { $null } }

function Get-ChosenDataPath {
    try { [string](Get-ItemProperty -Path $DataUserKey -Name DataPath -ErrorAction Stop).DataPath } catch { '' }
}
function Set-ChosenDataPath([string]$path) {
    if (-not (Test-Path $DataUserKey)) { New-Item -Path $DataUserKey -Force | Out-Null }
    New-ItemProperty -Path $DataUserKey -Name DataPath -Value $path -PropertyType String -Force | Out-Null
}
function Get-MachineDataPath {
    foreach ($k in $DataMachineKeys) {
        try {
            $v = [string](Get-ItemProperty -Path $k -Name DataPath -ErrorAction Stop).DataPath
            if ($v) { return $v }
        } catch {}
    }
    ''
}
function Set-MachineDataPath([string]$path) {
    foreach ($k in $DataMachineKeys) {
        if (-not (Test-Path $k)) { New-Item -Path $k -Force | Out-Null }
        New-ItemProperty -Path $k -Name DataPath -Value $path -PropertyType String -Force | Out-Null
    }
}

# The files of the voices no package carries, as voices.txt lists them ("id<TAB>name<TAB>language<TAB>data:<file>").
function Get-UserDataVoices([string]$stage) {
    $out = @()
    try {
        $text = [System.IO.File]::ReadAllText((Join-Path $stage 'voices.txt'), [System.Text.Encoding]::UTF8)
        foreach ($line in ($text -split "`r?`n")) {
            $f = $line.Split("`t")
            if ($f.Count -gt 3 -and $f[3].StartsWith('data:')) {
                $out += ,(New-Object PSObject -Property @{ Id = $f[0]; Name = $f[1]; Lang = $f[2];
                                                           File = $f[3].Substring(5).Replace('/', '\') })
            }
        }
    } catch {}
    ,$out
}
function Test-RootHasData([string]$root, $voices) {
    if (-not $root) { return $false }
    foreach ($v in $voices) { if (Test-Path -LiteralPath (Join-Path $root $v.File)) { return $true } }
    $false
}

# The candidates, in the order above (blank ones left out).
function Get-DataCandidates {
    $c = @()
    foreach ($p in @((Get-ChosenDataPath), (Get-MachineDataPath), (Get-NvdaDataRoot), (Get-CommonDataRoot),
                     (Get-UserDataRoot))) {
        if ($p) { $c += $p }
    }
    ,$c
}

# The data root: the first candidate holding a voice's file, else the first that exists, else this person's own
# (never %ProgramData%: a folder a standard account makes there is writable by every account, and SYSTEM reads it).
function Resolve-DataRoot($voices) {
    $candidates = Get-DataCandidates
    if ((Get-DataBreak) -ne 'onepass') {
        foreach ($p in $candidates) { if (Test-RootHasData $p $voices) { return $p } }
    }
    foreach ($p in $candidates) { if (Test-Path -LiteralPath $p) { return $p } }
    $own = Get-UserDataRoot
    if ($own) { return $own }
    $candidates | Select-Object -First 1
}

function Get-ComparablePath([string]$p) { if ($p) { $p.TrimEnd('\').ToLowerInvariant() } else { '' } }

# What "Move voices for all users" would do with $root: Action none | done | nvda | chosen | migrate, From, To, Reason.
function Get-MigrationPlan([string]$root, $voices) {
    $plan = New-Object PSObject -Property @{ Action = 'none'; From = $root; To = (Get-CommonDataRoot); Reason = '' }
    if (-not $plan.To) { $plan.Reason = 'this machine has no ProgramData folder'; return $plan }
    if (-not (Test-RootHasData $root $voices)) { $plan.Reason = 'there is nothing in the data folder to move'; return $plan }
    $r = Get-ComparablePath $root
    if ($r -eq (Get-ComparablePath $plan.To)) { $plan.Action = 'done'; $plan.Reason = 'the voices are already shared by every account'; return $plan }
    if ($r -eq (Get-ComparablePath (Get-NvdaDataRoot)) -and (Get-DataBreak) -ne 'movenvda') {
        $plan.Action = 'nvda'
        $plan.Reason = "they are in NVDA's own folder, which NVDA needs where it is (a portable copy and the sign-in screen read it there); SAPI reads them from there for this account"
        return $plan
    }
    if ($r -ne (Get-ComparablePath (Get-UserDataRoot)) -and (Get-DataBreak) -ne 'movenvda') {
        $plan.Action = 'chosen'; $plan.Reason = 'you chose that folder yourself, so it stays where you put it'; return $plan
    }
    $plan.Action = 'migrate'
    $plan
}

# ---- the Super Voice's program: its disk downloaded, or a file the user picks, read by the library itself ------------
# The native library's own reader (sv_dsk.h sv_firmware_from: the program, or the Disk BASIC image holding it, the known
# file only by its sha256), from this PowerShell's own bitness; a zip opened here with .NET 2.0's DEFLATE, one level.
$DataNativeSource = @'
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Runtime.InteropServices;
using System.Text;
public static class SsiData {
    [DllImport("kernel32", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr LoadLibraryW(string path);
    [DllImport("ssi263speech.dll", CallingConvention = CallingConvention.Cdecl)]
    static extern int sv_firmware_from(byte[] data, UIntPtr n, out IntPtr prog, out UIntPtr progN, StringBuilder err, int errlen);
    [DllImport("ssi263speech.dll", CallingConvention = CallingConvention.Cdecl)]
    static extern void sv_free(IntPtr p);
    public static void Load(string dll) { if (LoadLibraryW(dll) == IntPtr.Zero) throw new Exception("cannot load " + dll); }
    // the program, or null with the library's reason in why
    public static byte[] Program(byte[] data, out string why) {
        IntPtr p; UIntPtr n; StringBuilder err = new StringBuilder(300);
        why = null;
        if (sv_firmware_from(data, (UIntPtr)data.Length, out p, out n, err, 300) == 0) { why = err.ToString(); return null; }
        byte[] r = new byte[(int)n.ToUInt32()];
        Marshal.Copy(p, r, 0, r.Length);
        sv_free(p);
        return r;
    }
    static int U16(byte[] b, int at) { return b[at] | (b[at + 1] << 8); }
    static int U32(byte[] b, int at) { return U16(b, at) | (U16(b, at + 2) << 16); }
    // a zip's members (stored or deflated) of at most cap bytes, through its central directory; none when it is no zip
    public static List<byte[]> ZipMembers(byte[] z, int cap) {
        List<byte[]> outList = new List<byte[]>();
        int end = -1;
        for (int i = z.Length - 22; i >= 0 && i >= z.Length - 65557; i--)
            if (z[i] == 0x50 && z[i + 1] == 0x4B && z[i + 2] == 5 && z[i + 3] == 6) { end = i; break; }
        if (end < 0) return outList;
        int count = U16(z, end + 10), at = U32(z, end + 16);
        for (int k = 0; k < count && at + 46 <= z.Length && U32(z, at) == 0x02014B50; k++) {
            int method = U16(z, at + 10), csize = U32(z, at + 20), usize = U32(z, at + 24);
            int nlen = U16(z, at + 28), xlen = U16(z, at + 30), clen = U16(z, at + 32), local = U32(z, at + 42);
            at += 46 + nlen + xlen + clen;
            if (usize > cap || local + 30 > z.Length) continue;
            int data = local + 30 + U16(z, local + 26) + U16(z, local + 28);
            if (data + csize > z.Length) continue;
            if (method == 0) { byte[] m = new byte[csize]; Array.Copy(z, data, m, 0, csize); outList.Add(m); }
            else if (method == 8) {
                using (DeflateStream d = new DeflateStream(new MemoryStream(z, data, csize), CompressionMode.Decompress)) {
                    byte[] m = new byte[usize]; int got = 0, r;
                    while (got < usize && (r = d.Read(m, got, usize - got)) > 0) got += r;
                    if (got == usize) outList.Add(m);
                }
            }
        }
        return outList;
    }
}
'@
$DataNativeLoaded = $false
function Initialize-DataNative([string]$stage) {
    if ($script:DataNativeLoaded) { return }
    if (-not ('SsiData' -as [type])) { Add-Type -TypeDefinition $DataNativeSource -Language CSharp }
    $arch = if ([IntPtr]::Size -eq 8) { 'x64' } else { 'x86' }
    [SsiData]::Load((Join-Path (Join-Path $stage $arch) 'ssi263speech.dll'))
    $script:DataNativeLoaded = $true
}

# The Super Voice's program from bytes the user brought or downloaded: the program, the disk, or a zip holding either.
# Throws with the reason when none of it is the known program.
function Get-SuperVoiceProgram([string]$stage, [byte[]]$data) {
    Initialize-DataNative $stage
    $why = $null
    $prog = [SsiData]::Program($data, [ref]$why)
    if ($prog) { return ,$prog }
    if ((Get-DataBreak) -ne 'nozip') {
        foreach ($m in [SsiData]::ZipMembers($data, 4 * 161280)) {
            $w = $null
            $p = [SsiData]::Program($m, [ref]$w)
            if ($p) { return ,$p }
        }
    }
    throw "No Super Voice program was found in it: $why."
}

function Get-DataSha256([byte[]]$data) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    -join ($sha.ComputeHash($data) | ForEach-Object { $_.ToString('x2') })
}

# The disk, downloaded by hand: the first source whose download is exactly its pinned file (size and sha256), else
# a throw naming what went wrong with each.  $fetch: a scriptblock url -> bytes (the tests' stand-in), else WebClient.
function Get-SuperVoiceDisk($fetch) {
    $problems = @()
    foreach ($s in $SuperVoiceSources) {
        try {
            if ($fetch) { $bytes = & $fetch $s[1] } else {
                try { [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor 3072 } catch {}
                $wc = New-Object Net.WebClient
                $wc.Headers.Add('User-Agent', 'ssi263-sapi-settings')
                $bytes = $wc.DownloadData($s[1])
            }
            if ($bytes.Length -ne $s[2] -or (Get-DataSha256 $bytes) -ne $s[3]) {
                throw ("the download is not the file it should be ({0} bytes)" -f $bytes.Length)
            }
            return New-Object PSObject -Property @{ Bytes = $bytes; Where = $s[0] }
        } catch {
            $problems += ('{0}: {1}' -f $s[0], $_.Exception.Message)
        }
    }
    throw ("The Super Voice's disk could not be downloaded. " + ($problems -join ' '))
}

# The program into $root (a temporary file, then moved into place): 'installed', or 'same' when it is there already.
function Install-SuperVoiceProgram([string]$root, [byte[]]$prog) {
    if ((Get-DataSha256 $prog) -ne $SuperVoiceSha256) { throw 'not the Super Voice program (unknown sha256)' }
    $dest = Join-Path $root $SuperVoiceRel
    if (Test-Path -LiteralPath $dest) {
        if ((Get-DataSha256 ([System.IO.File]::ReadAllBytes($dest))) -eq $SuperVoiceSha256) { return 'same' }
    }
    $dir = Split-Path -Parent $dest
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    $tmp = $dest + '.tmp'
    [System.IO.File]::WriteAllBytes($tmp, $prog)
    if (Test-Path -LiteralPath $dest) { Remove-Item -LiteralPath $dest -Force }
    Move-Item -LiteralPath $tmp -Destination $dest
    'installed'
}
