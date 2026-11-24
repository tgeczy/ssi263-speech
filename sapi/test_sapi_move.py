"""SAPI "Move voices for all users": register.ps1's real -MigrateData block on temporary folders.  The block is taken out
of register.ps1 by its syntax tree (as Panthera's settings_test.ps1 runs its own), dataroot.ps1's functions are the
real ones, the voices are the -Dev stage's voices.txt; only the machine's data folder is recorded instead of written,
and nothing registers (the block sets $Register and stops there).  No elevation is needed: the folders are this
account's own.

  plain      the per-user folder moved to the target: the program there, the machine's data folder set to it
  empty      a target that already has the voice's folder, empty: the program moved into it (the move used to skip
             such a folder, move nothing, set the machine's folder to it and re-register from it: the Super Voice's
             token gone, "done" said -- the 0.9.5 audit)
  nothing    a source folder without the program: exit 5, the machine's folder not set, nothing moved
  links      a junction inside the source: refused, exit 6, nothing moved (elevated, its target's permissions would
             be reset)

    python sapi/test_sapi_move.py          (after sapi\\build.ps1 -Dev)
Controls (SSI263_SAPI_REGISTER_BREAK): move-skip (the folder already there skipped, as it was) -> empty, nothing;
move-links (links not looked for) -> links.
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
DEV = os.path.join(REPO, "nvda", "dist", "sapi-dev")
REL = os.path.join("speech-systems-super-voice", "SUPERTOK.BIN")
results = []

DRIVER = r"""
param([string]$MigrateFrom, [string]$DataRoot, [string]$Stage, [string]$Record)
$ErrorActionPreference = 'Stop'
$MigrateData = $true
. (Join-Path $PSScriptRoot 'dataroot.ps1')
function Set-MachineDataPath([string]$path) { Set-Content -LiteralPath $Record -Value $path }
$userVoices = Get-UserDataVoices $Stage
$source = Get-Content -Raw -Encoding UTF8 (Join-Path $PSScriptRoot 'register.ps1')
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseInput($source, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw $errors[0] }
$block = @($ast.EndBlock.Statements | Where-Object {
    $_ -is [Management.Automation.Language.IfStatementAst] -and $_.Clauses[0].Item1.Extent.Text -eq '$MigrateData' })
if ($block.Count -ne 1) { Write-Host 'the move block is missing or not one'; exit 9 }
. ([scriptblock]::Create($block[0].Extent.Text))
exit 0
"""


def check(name, ok, detail=""):
    results.append(ok)
    print("%-5s %-8s %s" % ("ok" if ok else "FAIL", name, detail))


def main():
    if not os.path.isfile(os.path.join(DEV, "voices.txt")):
        print("no %s: run sapi\\build.ps1 -Dev first" % DEV)
        return 1
    tmp = tempfile.mkdtemp(prefix="ssi263-sapimove-")
    try:
        tools = os.path.join(tmp, "tools")
        os.makedirs(tools)
        for n in ("register.ps1", "dataroot.ps1"):
            shutil.copy2(os.path.join(HERE, n), tools)
        with open(os.path.join(tools, "move.ps1"), "w", encoding="utf-8") as f:
            f.write(DRIVER)

        def case(tag, make):
            base = os.path.join(tmp, tag)
            src, dst, record = os.path.join(base, "user"), os.path.join(base, "common"), os.path.join(base, "machine.txt")
            os.makedirs(src)
            os.makedirs(dst)
            make(src, dst)
            r = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                                os.path.join(tools, "move.ps1"), "-MigrateFrom", src, "-DataRoot", dst, "-Stage", DEV,
                                "-Record", record], capture_output=True, text=True, timeout=120)
            machine = open(record, encoding="utf-8-sig").read().strip() if os.path.isfile(record) else ""
            moved = os.path.isfile(os.path.join(dst, REL))
            subprocess.run(["icacls", base, "/reset", "/T", "/C", "/Q"], capture_output=True)   # removable again
            return r.returncode, moved, machine, (r.stdout + r.stderr).strip().splitlines()[-1:] or [""]

        def program(src, _dst):
            os.makedirs(os.path.join(src, "speech-systems-super-voice"))
            with open(os.path.join(src, REL), "wb") as f:
                f.write(b"stand-in")

        code, moved, machine, last = case("plain", program)
        check("plain", code == 0 and moved and machine.endswith("common"), "exit %d, moved %s, machine %r %s" % (
            code, moved, machine, last[0]))

        def empty(src, dst):
            program(src, dst)
            os.makedirs(os.path.join(dst, "speech-systems-super-voice"))
        code, moved, machine, last = case("empty", empty)
        check("empty", code == 0 and moved and machine.endswith("common"), "exit %d, moved %s, machine %r %s" % (
            code, moved, machine, last[0]))

        def nothing(src, _dst):
            os.makedirs(os.path.join(src, "speech-systems-super-voice"))
        code, moved, machine, last = case("nothing", nothing)
        check("nothing", code == 5 and not moved and machine == "", "exit %d, moved %s, machine %r %s" % (
            code, moved, machine, last[0]))

        def links(src, dst):
            program(src, dst)
            elsewhere = os.path.join(os.path.dirname(src), "elsewhere")
            os.makedirs(elsewhere)
            subprocess.run(["cmd", "/c", "mklink", "/J", os.path.join(src, "speech-systems-super-voice", "j"),
                            elsewhere], capture_output=True)
        code, moved, machine, last = case("links", links)
        check("links", code == 6 and not moved and machine == "", "exit %d, moved %s, machine %r %s" % (
            code, moved, machine, last[0]))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("sapi move: %s" % ("all passed" if all(results) else "%d FAILED" % results.count(False)))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
