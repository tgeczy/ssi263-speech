/* bl_files_state.h -- a unit's saved state file (what the emulator keeps while the unit is switched off) as the
 * memory images bl_files.h works on, and back, in the same format.
 *
 *   Braille Lite: its 768 KB of RAM (40000h-FFFFFh) -- its first 256 KB while the rest is all 00h -- then the file
 *     flash -- its first 512 KB while the rest is erased, else all 2 MB (bl_board.h, bl_save_state) -- then, when
 *     the clock controller was on, its 64 bytes.
 *   Type 'n Speak: 1 MB of RAM (the program at its start), the 4 MB flash, the clock's 64 bytes (tns_board.h).
 * The unit is told by the file's size.  The clock's bytes are kept as they are.  Portable C99 (stdio only).
 */
#ifndef BL_FILES_STATE_H
#define BL_FILES_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int model;                      /* BLF_BRAILLE_LITE or BLF_TYPE_N_SPEAK (bl_files.h) */
    unsigned char *ram;             /* the 1 MB address space (the Braille Lite's RAM at 40000h) */
    unsigned char *flash;
    long flash_size;                /* 2 MB or 4 MB */
    unsigned char tail[64];         /* the clock controller's bytes, when has_tail */
    int has_tail;
} bls_unit;

/* 1, or 0 with the reason in err */
int bls_load(const char *path, bls_unit *u, char *err, int errlen);
/* a temporary file beside `path`, then renamed over it: a failure leaves the old state whole */
int bls_save(const char *path, const bls_unit *u, char *err, int errlen);
void bls_free(bls_unit *u);

#ifdef __cplusplus
}
#endif
#endif
