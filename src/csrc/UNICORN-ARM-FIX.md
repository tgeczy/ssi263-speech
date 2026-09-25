# Unicorn on Windows on ARM: the unwind fix

On ARM64 Windows, NVDA runs as x64 under emulation. There the stock Unicorn 2.1.4 DLL
died with exit code 0xC00000FF on its first execution, in both the Accent and Speak-Out
add-ons. The chip, ctypes, the Blazie add-on (no Unicorn) and NVDA's ARM64EC audio all
worked. A tester's machine (Windows 11 ARM64, build 26200) reproduced it in isolation.

The cause: Unicorn leaves TCG-generated code with QEMU's sigsetjmp / siglongjmp, which
on Win32 are the CRT's setjmp / longjmp, and the CRT's longjmp unwinds through the
generated code. Under x64 emulation on ARM that unwind fails.

`unicorn-2.1.4-no-crt-unwind.patch` makes those paired internal jumps GCC's
`__builtin_setjmp` / `__builtin_longjmp` in an x64 GCC build with `UC_NO_CRT_UNWIND`
defined. x86, MSVC and non-Windows builds are unchanged. GCC's jump buffers are not
compatible with the CRT's: both sides must stay paired. Unicorn's x86-core jump values
are all 1 and jump back to caller frames.
Reference: https://gcc.gnu.org/onlinedocs/gcc/Nonlocal-Gotos.html

Build (needs C:\w64devkit\bin with GCC, CMake and Ninja; checks the pinned tarball's
SHA-256; stages under build/):

```
C:\Python313\python.exe src\csrc\build_unicorn_candidate.py
```

Output: `build/unicorn-no-crt-unwind/libunicorn.dll`, shipped as `bin/x64/unicorn.dll`
(sha256 e138d444388e0d73ac6c42fde2e462faa6660b7f7eb0d116beab1a52bdbe7c71).

Verified 2026-09-25, on the ARM machine: the stock DLL still fails (the control); the
patched one passes minimal execution, and the packaged Accent and Speak-Out drivers boot,
speak, cancel and shut down. On x64: Unicorn's x86 and control unit suites and 17 memory
tests pass (the large-memory test needs UC_ARCH_ARM64, not in this x86-only build), and
the Accent and Speak-Out test phrases are byte-identical to the stock DLL's.

A first-chance access violation logged at `mem_map` appears in successful runs too: it
is handled, and is not the 0xC00000FF failure.
