/* bl_fopen.h -- fopen for a path in UTF-8, on every system.
 *
 * Windows' fopen reads its path in the ANSI code page, so a path in UTF-8 holding a letter outside ASCII (a user
 * folder named José, or a Cyrillic one) named no file, and the Braille Lite's release checks found no release
 * (#62).  Here a path that is valid UTF-8 and holds such a letter is opened through _wfopen; any other path (plain
 * ASCII, or an ANSI one from a caller that still sends those, which is not valid UTF-8) with fopen, as before.
 * Elsewhere fopen takes UTF-8 itself.  BL_FOPEN_BREAK=1 (the tests' control, nvda/tools/unicode_paths.py): fopen
 * always, as it was.  MIT.
 */
#ifndef BL_FOPEN_H
#define BL_FOPEN_H

#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <wchar.h>
__declspec(dllimport) int __stdcall MultiByteToWideChar(unsigned int, unsigned long, const char *, int, wchar_t *, int);
#endif

static FILE *bl_fopen(const char *path, const char *mode)
{
#ifdef _WIN32
    const unsigned char *p;
    for (p = (const unsigned char *)path; *p && *p < 0x80; p++)
        ;
    if (*p && !getenv("BL_FOPEN_BREAK")) {
        wchar_t wpath[1024], wmode[8];
        /* 65001 = CP_UTF8, 8 = MB_ERR_INVALID_CHARS: an ANSI path is not valid UTF-8, and falls through to fopen */
        if (MultiByteToWideChar(65001, 8, path, -1, wpath, 1024) > 0 &&
            MultiByteToWideChar(65001, 0, mode, -1, wmode, 8) > 0)
            return _wfopen(wpath, wmode);
    }
#endif
    return fopen(path, mode);
}

#endif
