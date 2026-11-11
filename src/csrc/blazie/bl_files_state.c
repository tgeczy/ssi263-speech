/* bl_files_state.c -- see bl_files_state.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bl_files.h"
#include "bl_files_state.h"

#define BL_RAM 0xC0000L               /* 768 KB, 40000h-FFFFFh (bl_board.h, bl_save_state) ... */
#define BL_RAM_OLD 0x40000L           /* ... or its first 256 KB, while the rest is all 00h */
#define BL_RAM_AT 0x40000L
#define BL_FLASH 0x200000L
#define BL_FLASH_OLD 0x80000L
#define TNS_RAM 0x100000L
#define TNS_FLASH 0x400000L
#define TAIL 64L

static int fail(char *err, int errlen, const char *msg)
{
    if (err && errlen > 0)
        snprintf(err, (size_t)errlen, "%s", msg);
    return 0;
}

/* a Braille Lite state of `size` with ram_n bytes of RAM: its flash's first 512 KB or all 2 MB, the tail or not */
static int bl_sizes(long size, long ram_n)
{
    return size == ram_n + BL_FLASH_OLD || size == ram_n + BL_FLASH_OLD + TAIL || size == ram_n + BL_FLASH
           || size == ram_n + BL_FLASH + TAIL;
}

void bls_free(bls_unit *u)
{
    free(u->ram);
    free(u->flash);
    u->ram = u->flash = NULL;
}

int bls_load(const char *path, bls_unit *u, char *err, int errlen)
{
    FILE *f = fopen(path, "rb");
    long size, ram_n, flash_n, tail_n = 0;
    int ok;
    memset(u, 0, sizeof *u);
    if (!f)
        return fail(err, errlen, "cannot open the state file");
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size == TNS_RAM + TNS_FLASH || size == TNS_RAM + TNS_FLASH + TAIL) {
        u->model = BLF_TYPE_N_SPEAK;
        ram_n = TNS_RAM;
        flash_n = TNS_FLASH;
        u->flash_size = TNS_FLASH;
    } else if (bl_sizes(size, BL_RAM_OLD) || bl_sizes(size, BL_RAM)) {
        u->model = BLF_BRAILLE_LITE;
        ram_n = bl_sizes(size, BL_RAM) ? BL_RAM : BL_RAM_OLD;
        flash_n = size - ram_n >= BL_FLASH ? BL_FLASH : BL_FLASH_OLD;
        u->flash_size = BL_FLASH;
    } else {
        fclose(f);
        return fail(err, errlen, "not a Braille Lite or Type 'n Speak state file (its size)");
    }
    tail_n = size - ram_n - flash_n;
    u->ram = (unsigned char *)calloc(1, 0x100000);
    u->flash = (unsigned char *)malloc((size_t)u->flash_size);
    if (!u->ram || !u->flash) {
        fclose(f);
        bls_free(u);
        return fail(err, errlen, "out of memory");
    }
    memset(u->flash, 0xFF, (size_t)u->flash_size);
    ok = fread(u->ram + (u->model == BLF_BRAILLE_LITE ? BL_RAM_AT : 0), 1, (size_t)ram_n, f) == (size_t)ram_n
         && fread(u->flash, 1, (size_t)flash_n, f) == (size_t)flash_n;
    if (ok && tail_n == TAIL) {
        ok = fread(u->tail, 1, TAIL, f) == TAIL;
        u->has_tail = 1;
    }
    fclose(f);
    if (!ok) {
        bls_free(u);
        return fail(err, errlen, "cannot read the state file");
    }
    return 1;
}

int bls_save(const char *path, const bls_unit *u, char *err, int errlen)
{
    char tmp[1100];
    FILE *f;
    long flash_n = u->flash_size, ram_n = BL_RAM, i;
    int ok;
    if (strlen(path) + 5 > sizeof tmp)
        return fail(err, errlen, "the state file's name is too long");
    if (u->model == BLF_BRAILLE_LITE) {
        for (i = BL_FLASH_OLD; i < BL_FLASH && u->flash[i] == 0xFF; i++) ;
        if (i == BL_FLASH)
            flash_n = BL_FLASH_OLD;   /* the rest erased: the 768 KB format every reader knows (bl_save_state) */
        for (i = BL_RAM_AT + BL_RAM_OLD; i < BL_RAM_AT + BL_RAM && !u->ram[i]; i++) ;
        if (i == BL_RAM_AT + BL_RAM)
            ram_n = BL_RAM_OLD;       /* RAM above 7FFFFh all 00h: its first 256 KB, as bl_save_state keeps it */
    }
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (!f)
        return fail(err, errlen, "cannot write the state file");
    if (u->model == BLF_BRAILLE_LITE)
        ok = fwrite(u->ram + BL_RAM_AT, 1, (size_t)ram_n, f) == (size_t)ram_n;
    else
        ok = fwrite(u->ram, 1, TNS_RAM, f) == TNS_RAM;
    ok = ok && fwrite(u->flash, 1, (size_t)flash_n, f) == (size_t)flash_n;
    if (ok && u->has_tail)
        ok = fwrite(u->tail, 1, TAIL, f) == TAIL;
    if (fclose(f) != 0)
        ok = 0;
    if (!ok) {
        remove(tmp);
        return fail(err, errlen, "cannot write the state file");
    }
    remove(path);
    if (rename(tmp, path) != 0)
        return fail(err, errlen, "cannot replace the state file (the new one is beside it, .tmp)");
    return 1;
}
