/* bl_files.h -- the files a Blazie unit keeps, read and written in its memory images (Tomi: files in and out).
 *
 * Both units keep files the same way (measured by running the Braille Lite 2000 and Type 'n Speak firmware in the
 * emulator, making files with the units' own commands and reading their memory back):
 *
 *   RAM files: a directory of 128 slots of 64 bytes at 44000h.  Slot 0 is the help file (its text is in the
 *     firmware); on an initialised unit slot 1 is the clipboard (46000h) and slot 2 the datebook (47000h), each one
 *     4 KB page; the user's files follow in slot order, packed: each file's first byte is the byte after the
 *     previous file's last page, every file a whole number of 4 KB pages.  The list ends at the first slot whose
 *     start pointer is 0.  A file's text runs from its start to its end-of-text pointer (both included; empty: the
 *     end one byte before the start).  A new line is a lone CR.
 *   The open file: while a file is open the unit edits it through six pointers in its working memory (cursor,
 *     second cursor, start, end of text, end of allocation, mark: 4 bytes each, together), and writes them back
 *     into the directory only when the file is closed -- a warm start goes on from those pointers.  Its directory
 *     entry can therefore be out of date in a saved state; this library reads the live pointers (found by matching
 *     the file's start and end) and the open file's number (a 16-bit word 48 bytes before them on the Braille Lite,
 *     51 on the Type 'n Speak).
 *   Flash files: a 16-byte header at the start of the flash (two ID words, the table size, the number of 512-byte
 *     blocks), then a table of 2 bits per block (3 free, 1 used, 0 deleted until the firmware erases its sector),
 *     then 64-byte entries in the rest of the first 64 KB, ended by FF FF FF FF.  A file is one run of blocks,
 *     allocated from the top of the flash down; its pointers are 100000h + the byte's offset in the flash.  An entry
 *     whose folder byte is 0 is deleted (flash bits only go from 1 to 0: the firmware rewrites nothing in place).
 *   Folders: 20 names of 21 bytes at 43E48h and 20 type bytes after them ('r' a RAM folder, 'f' a flash one).
 *     Folder 0 is the RAM startup folder, 1 the flash startup folder.  A RAM file's folder is a RAM folder, a flash
 *     file's a flash folder (never 0, the deleted mark).
 *   A file's entry: name (20 characters + NUL, lower case, one dot, an extension of up to 3), folder, the pointers,
 *     column, type ('A' text, 'B' grade 2 braille, 'X' program, '?' other), protection (1 system, 2 the user's),
 *     mark, six format bytes, password (6 + NUL), time and date in DOS format.
 *
 * Writing keeps every rule above: RAM files stay packed (the ones after a changed file move, the open file's live
 * pointers and number with them), flash files go into free blocks only, with the table and an entry appended, and a
 * deleted flash file is only marked (its blocks become "deleted", reclaimed by the firmware when it next tidies the
 * flash).  Bits in the flash only ever go from 1 to 0, as the chip allows.  The unit must not be running while its
 * images are changed; started again from them (a power-on), it sees the files as if it had written them.
 *
 * Portable C99, no platform calls.  Not thread-safe per blf_fs.
 */
#ifndef BL_FILES_H
#define BL_FILES_H

#ifdef __cplusplus
extern "C" {
#endif

enum { BLF_BRAILLE_LITE, BLF_TYPE_N_SPEAK };

#define BLF_NAME_MAX 20
#define BLF_FOLDERS 20
#define BLF_RAM_SLOTS 128

typedef struct {
    char name[BLF_NAME_MAX + 1];
    int folder;                    /* 0..19 */
    int in_flash;                  /* 0: a RAM file, 1: a flash file */
    int type;                      /* 'A', 'B', 'X', '?' */
    int prot;                      /* the protection byte (1 system, 2 the user's) */
    int has_password;
    unsigned dos_time, dos_date;
    unsigned long size;            /* bytes of text (the open file's from its live pointers) */
    int slot;                      /* RAM slot (1..127) or flash entry number */
    int system;                    /* the clipboard or the datebook (RAM slots 1 and 2 on an initialised unit) */
    int open;                      /* the file the unit has open */
} blf_file;

typedef struct {
    char name[BLF_NAME_MAX + 1];   /* "" unused */
    int type;                      /* 'r', 'f', or 0 */
} blf_folder;

typedef struct blf_fs blf_fs;

/* A view over a unit's images, which stay the caller's and are changed in place: ram is its 1 MB address space (the
   Braille Lite's RAM at 40000h-7FFFFh; below it nothing is read), flash its file flash (flash_size bytes: 2 MB on the
   Braille Lite, 4 MB on the Type 'n Speak).  NULL on failure, the reason in err. */
blf_fs *blf_open(int model, unsigned char *ram, unsigned char *flash, long flash_size, char *err, int errlen);
void blf_close(blf_fs *fs);

int blf_count(const blf_fs *fs);
const blf_file *blf_get(const blf_fs *fs, int i);
/* a file's text, contiguous in the image (valid until the next change); its length in *n */
const unsigned char *blf_data(const blf_fs *fs, int i, unsigned long *n);
/* the file named `name` (case-insensitive, any folder), or -1 */
int blf_find(const blf_fs *fs, const char *name);
const blf_folder *blf_folder_get(const blf_fs *fs, int f);
/* the folder named `name` (case-insensitive), or -1 */
int blf_folder_find(const blf_fs *fs, const char *name);

/* the unit's state as this library found it */
int blf_flash_ok(const blf_fs *fs);              /* the flash file system is initialised */
int blf_ram_ok(const blf_fs *fs);                /* the RAM file system is initialised (clipboard at 46000h) */
int blf_folders_ok(const blf_fs *fs);            /* folder 0 is a RAM folder, folder 1 a flash one */
int blf_open_known(const blf_fs *fs);            /* the open file's live pointers and number were both found */
int blf_language(const blf_fs *fs);              /* 0 English, 1 Spanish (from the help file's name) */
/* free space: RAM bytes after the last file (whole pages), flash bytes in free blocks (as the unit counts them) */
void blf_free_space(const blf_fs *fs, unsigned long *ram_free, unsigned long *flash_free);

/* The file type the unit gives a new file of that name (its extension), and whether a name is one the unit takes. */
int blf_type_for_name(const blf_fs *fs, const char *name);
int blf_name_ok(const char *name);

/* Changes.  Each returns 1, or 0 with the reason in err and the images unchanged.  dos_time/dos_date: the file's
   time and date (DOS format, as the unit keeps them).  user_prot: protected by the user (the unit's p command). */
/* a new file in `folder` (a RAM folder: a RAM file after the last one; a flash folder: a flash file) */
int blf_add(blf_fs *fs, int folder, const char *name, const unsigned char *data, unsigned long n,
            unsigned dos_time, unsigned dos_date, int user_prot, char *err, int errlen);
/* new text for file i, kept where it is (a RAM file is resized in place, a flash file written anew); its name,
   folder, type and protection kept */
int blf_replace(blf_fs *fs, int i, const unsigned char *data, unsigned long n, unsigned dos_time, unsigned dos_date,
                char *err, int errlen);
/* file i gone (the clipboard and datebook cannot be; the open file cannot be) */
int blf_delete(blf_fs *fs, int i, char *err, int errlen);
/* file i into another folder of the same kind (RAM to RAM, flash to flash) */
int blf_move(blf_fs *fs, int i, int folder, char *err, int errlen);
/* a new folder ('r' or 'f'); its number, or -1 */
int blf_add_folder(blf_fs *fs, const char *name, int type, char *err, int errlen);

/* Flash files the unit lost: entries marked deleted (folder 0) whose blocks are all still marked used and that no
   live entry shares.  A Type 'n Speak whose folders were never set up (the 0.6 and 0.7 previews' first start, which
   missed the unit's cold reset) wrote a file moved to flash as such an entry -- in folder 0, the deleted mark -- and
   removed it from RAM without writing its text to its blocks (measured: they stay erased, FFh): the name is all that
   is left.  The firmware's own delete frees the blocks with the mark, and a rename leaves a live entry on the same
   blocks.  The i-th one (0..), its entry in *f and its blocks' bytes (*n); NULL past the last. */
const unsigned char *blf_lost_get(const blf_fs *fs, int i, blf_file *f, unsigned long *n);

/* Checks every rule above on the images (packing, pointers inside RAM, each live flash file's blocks marked used,
   no two files on the same block, the table's free count); 1 if all hold, else 0 with the first broken one in err. */
int blf_check(const blf_fs *fs, char *err, int errlen);

/* Tests' must-fail controls (an app never sets it): 1 the open file's live pointers are ignored (its directory entry
   is read as it stands), 2 a new flash file's blocks are not marked used, 3 a new RAM file's end-of-text one byte
   short, 4 a new file's time and date dropped, 5 the open file's number left as it was when an earlier file goes,
   6 (bl_files_xfer.c) an imported binary document's line ends converted as a PC's text (issue #16). */
extern int blf_break;

#ifdef __cplusplus
}
#endif
#endif
