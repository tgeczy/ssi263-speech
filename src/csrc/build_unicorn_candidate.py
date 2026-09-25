"""Reproducible build of the ARM-fixed Unicorn (UNICORN-ARM-FIX.md): its internal jumps kept out of the CRT.

Does not replace the engine's installed/baseline Unicorn DLL. Requires GCC x64,
the pinned source tarball in ssi263-speech, and the existing configure path patch.
"""
import difflib
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[2]
ARCHIVE = Path(__file__).with_name("unicorn-2.1.4.tar.gz")
SOURCE = ROOT / "build/unicorn-source/unicorn-2.1.4/src"
BUILD = ROOT / "build/unicorn-no-crt-unwind"
# w64devkit's x86_64 bin folder (gcc, cmake, ninja): SSI263_W64DEVKIT, else wherever gcc is
# on the PATH.  This script also ships inside the add-ons, away from the repo's tools/,
# so it doesn't read paths.local.
GCC = Path(os.environ.get("SSI263_W64DEVKIT") or os.path.dirname(shutil.which("gcc") or "gcc"))

OLD = """#define sigjmp_buf jmp_buf
#define sigsetjmp(env, savemask) setjmp(env)
#define siglongjmp(env, val) longjmp(env, val)"""
NEW = """#define sigjmp_buf jmp_buf
#if defined(UC_NO_CRT_UNWIND) && defined(__x86_64__) && defined(__GNUC__) && !defined(__clang__)
/* Unicorn's two x86 siglongjmp sites both return 1 to a caller frame. Keep
 * these paired internal jumps inside GCC instead of asking the Windows CRT
 * to unwind through TCG-generated code under x64-on-ARM emulation.
 * Existing jmp_buf storage is larger and at least as aligned as the five
 * pointer-sized words GCC requires. Do NOT mix this buffer with CRT jumps.
 * The constant value restriction is enforced by __builtin_longjmp itself.
 * This build option is a candidate until validated on the affected ARM host.
 */
typedef char uc_gcc_jmp_storage_check[(sizeof(jmp_buf) >= 5 * sizeof(void *)) ? 1 : -1];
#define sigsetjmp(env, savemask) __builtin_setjmp((void *)(env))
#define siglongjmp(env, val) __builtin_longjmp((void *)(env), (val))
#else
#define sigsetjmp(env, savemask) setjmp(env)
#define siglongjmp(env, val) longjmp(env, val)
#endif"""


def main():
    digest = hashlib.sha256(ARCHIVE.read_bytes()).hexdigest()
    if digest != "00567a70e323f749b419cd86bee4f9115beab7ebba32194581c090cbb7c59cff":
        raise ValueError("unexpected Unicorn source archive")
    SOURCE.parent.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(ARCHIVE) as t:
        # Read pristine input every time; extraction is filtered and confined.
        t.extractall(SOURCE.parent.parent, filter="data")
    configure = SOURCE / "qemu/configure"
    configure.write_text(configure.read_text().replace("[[:space:]:]", "[[:space:]]"), newline="\n")
    header = SOURCE / "qemu/include/sysemu/os-win32.h"
    original = header.read_text()
    if original.count(OLD) != 1: raise ValueError("unexpected os-win32.h")
    changed = original.replace(OLD, NEW)
    header.write_text(changed, newline="\n")
    patch = "".join(difflib.unified_diff(original.splitlines(True), changed.splitlines(True),
                                       "a/qemu/include/sysemu/os-win32.h", "b/qemu/include/sysemu/os-win32.h"))
    Path(__file__).with_name("unicorn-2.1.4-no-crt-unwind.patch").write_text(patch, newline="\n")
    env = dict(os.environ, PATH=str(GCC) + os.pathsep + os.environ["PATH"])
    subprocess.run([str(GCC / "cmake.exe"), "-S", str(SOURCE), "-B", str(BUILD), "-G", "Ninja",
                    "-DCMAKE_BUILD_TYPE=Release", "-DUNICORN_ARCH=x86", "-DUNICORN_BUILD_TESTS=ON",
                    "-DUNICORN_INSTALL=OFF", "-DUNICORN_LEGACY_STATIC_ARCHIVE=OFF",
                    "-DBUILD_SHARED_LIBS=ON", "-DCMAKE_C_COMPILER=gcc",
                    "-DCMAKE_C_FLAGS=-DUC_NO_CRT_UNWIND", "-DCMAKE_SHARED_LINKER_FLAGS=-static -static-libgcc",
                    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"], env=env, check=True)
    subprocess.run([str(GCC / "cmake.exe"), "--build", str(BUILD), "--parallel", "8"], env=env, check=True)
    print("Candidate DLL:", BUILD / "libunicorn.dll")


if __name__ == "__main__":
    main()
