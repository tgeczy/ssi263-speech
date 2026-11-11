/* bl_files.c -- see bl_files.h.  Every address and rule here was measured on the running firmware (bl_files.h);
 * test_files.c checks them against the units' own commands. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bl_files.h"

int blf_break;

/* ---- the layout (both units) ------------------------------------------------------------------------------------ */
#define DIR_AT 0x44000UL                /* RAM directory: 128 slots of 64 bytes */
#define SLOT 64
#define FOLDER_NAMES_AT 0x43E48UL       /* 20 names of 21 bytes, then 20 type bytes */
#define FOLDER_TYPES_AT (FOLDER_NAMES_AT + BLF_FOLDERS * 21)
#define CLIPBOARD_AT 0x46000UL          /* slot 1 on an initialised unit; slot 2 (the datebook) follows */
#define LIVE_LO 0x40000UL               /* where the open file's live pointers are looked for: the working memory */
#define LIVE_HI FOLDER_NAMES_AT
#define PAGE 4096UL                     /* RAM files are whole pages */
#define FLASH_PTR 0x100000UL            /* a flash file's pointers: this + its offset in the flash */
#define BLOCK 512UL                     /* flash files are whole blocks */
#define ENTRY_AREA_END 0x10000UL        /* the flash's entry list stays in its first 64 KB ... */
#define FIRST_FILE_BLOCK (ENTRY_AREA_END / BLOCK)   /* ... and no file goes below it */
#define HEADER 16UL
#define B_FREE 3
#define B_USED 1
#define B_GONE 0                        /* a deleted file's block, until the firmware erases its sector */

/* the entry: offsets of its fields */
enum { E_NAME = 0, E_FOLDER = 21, E_CURSOR = 22, E_END_TEXT = 26, E_START = 30, E_END_ALLOC = 34, E_COLUMN = 38,
       E_TYPE = 39, E_PROT = 40, E_MARK = 41, E_FORMAT = 45, E_PASSWORD = 51, E_TIME = 58, E_DATE = 60 };

/* the format bytes every file made on these units carried (line length, margins, page length, ...) */
static const unsigned char DEFAULT_FORMAT[6] = {0x20, 0x4B, 0x0A, 0x3C, 0x06, 0x50};

typedef struct {
    unsigned long entry;            /* the entry's address: RAM, or its offset in the flash */
    unsigned long start, end_text, end_alloc;
} where_t;

struct blf_fs {
    int model;
    unsigned char *ram, *flash;
    long flash_size;
    blf_file *files;
    where_t *where;
    int n, cap;
    blf_folder folders[BLF_FOLDERS];
    int flash_ok, ram_ok, folders_ok, language;
    unsigned long blocks, table_bytes, entries_at, next_entry;
    int n_entries;                  /* flash entries, deleted ones included */
    int n_slots;                    /* RAM slots in use: 1..n_slots */
    long live;                      /* the open file's live pointers (address), or -1 */
    long open_no;                   /* the open file's number (address), or -1 */
    int open_kind;                  /* -1 none found, 0 a RAM slot (open_index: 0 = help), 1 a flash entry */
    int open_index;
    unsigned long ram_top;
    int open_no_back;
};

static unsigned long rd32(const unsigned char *p) { return p[0] | (p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24); }
static unsigned rd16(const unsigned char *p) { return (unsigned)(p[0] | (p[1] << 8)); }
static void wr32(unsigned char *p, unsigned long v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24); }
static void wr16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }

/* case-insensitive comparison of the first n characters (n < 0: all), ASCII */
static int ci_cmp(const char *a, const char *b, int n)
{
    for (; n; n--, a++, b++) {
        int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b);
        if (x != y) return x - y;
        if (!x) return 0;
    }
    return 0;
}

static int fail(char *err, int errlen, const char *msg)
{
    if (err && errlen > 0)
        snprintf(err, (size_t)errlen, "%s", msg);
    return 0;
}

/* ---- the flash: its block table, and programming (bits only from 1 to 0, as the chip allows) ------------------ */
static int block_state(const blf_fs *fs, unsigned long b)
{
    return (fs->flash[HEADER + b / 4] >> ((b % 4) * 2)) & 3;
}

static int program(blf_fs *fs, unsigned long off, const unsigned char *src, unsigned long n)
{
    unsigned long i;
    for (i = 0; i < n; i++)
        if ((fs->flash[off + i] & src[i]) != src[i])
            return 0;               /* would need a bit from 0 to 1: only an erase does that */
    for (i = 0; i < n; i++)
        fs->flash[off + i] &= src[i];
    return 1;
}

static int set_block(blf_fs *fs, unsigned long b, int state)
{
    unsigned char *p = &fs->flash[HEADER + b / 4];
    int sh = (int)(b % 4) * 2;
    unsigned char v = (unsigned char)((*p & ~(3 << sh)) | (state << sh));
    return program(fs, HEADER + b / 4, &v, 1);
}

/* ---- reading the images ----------------------------------------------------------------------------------------- */
static int push(blf_fs *fs, const blf_file *f, const where_t *w)
{
    if (fs->n == fs->cap) {
        int cap = fs->cap ? fs->cap * 2 : 64;
        blf_file *nf = (blf_file *)realloc(fs->files, sizeof(blf_file) * (size_t)cap);
        where_t *nw;
        if (!nf) return 0;
        fs->files = nf;
        nw = (where_t *)realloc(fs->where, sizeof(where_t) * (size_t)cap);
        if (!nw) return 0;
        fs->where = nw;
        fs->cap = cap;
    }
    fs->files[fs->n] = *f;
    fs->where[fs->n] = *w;
    fs->n++;
    return 1;
}

static void entry_file(const unsigned char *e, blf_file *f)
{
    memset(f, 0, sizeof *f);
    memcpy(f->name, e + E_NAME, BLF_NAME_MAX);
    f->name[BLF_NAME_MAX] = 0;
    f->folder = e[E_FOLDER];
    f->type = e[E_TYPE];
    f->prot = e[E_PROT];
    f->has_password = e[E_PASSWORD] != 0;
    f->dos_time = rd16(e + E_TIME);
    f->dos_date = rd16(e + E_DATE);
}

/* the open file: a block of six pointers whose start and end of allocation are a file's, its end of text inside */
static void find_open(blf_fs *fs)
{
    unsigned long a;
    int hits = 0;
    long at = -1;
    int kind = -1, index = -1;
    fs->live = fs->open_no = -1;
    fs->open_kind = -1;
    if (blf_break == 1)
        return;
    for (a = LIVE_LO; a + 24 <= LIVE_HI; a++) {
        const unsigned char *p = fs->ram + a;
        unsigned long start = rd32(p + 8), end_text = rd32(p + 12), end_alloc = rd32(p + 16);
        int s;
        if (start == 0 || end_alloc < start || end_text + 1 < start || end_text > end_alloc)
            continue;
        for (s = 0; s <= fs->n_slots; s++) {
            const unsigned char *e = fs->ram + DIR_AT + (unsigned long)s * SLOT;
            if (rd32(e + E_START) == start && rd32(e + E_END_ALLOC) == end_alloc) {
                hits++; at = (long)a; kind = 0; index = s;
            }
        }
        if (fs->flash_ok && start >= FLASH_PTR) {
            int k;
            for (k = 0; k < fs->n_entries; k++) {
                const unsigned char *e = fs->flash + fs->entries_at + (unsigned long)k * SLOT;
                if (e[E_FOLDER] && rd32(e + E_START) == start && rd32(e + E_END_ALLOC) == end_alloc) {
                    hits++; at = (long)a; kind = 1; index = k;
                }
            }
        }
    }
    if (hits != 1)
        return;                     /* none, or not one beyond doubt: treated as unknown */
    fs->live = at;
    fs->open_kind = kind;
    fs->open_index = index;
    {
        long no = fs->live - fs->open_no_back;
        unsigned want = kind == 0 ? (unsigned)index : (unsigned)(BLF_RAM_SLOTS + index);
        if (no >= (long)LIVE_LO && rd16(fs->ram + no) == want)
            fs->open_no = no;
    }
}

static int scan(blf_fs *fs)
{
    int f, s, k;
    fs->n = 0;
    /* folders */
    for (f = 0; f < BLF_FOLDERS; f++) {
        const unsigned char *nm = fs->ram + FOLDER_NAMES_AT + (unsigned long)f * 21;
        memcpy(fs->folders[f].name, nm, BLF_NAME_MAX);
        fs->folders[f].name[BLF_NAME_MAX] = 0;
        fs->folders[f].type = fs->ram[FOLDER_TYPES_AT + f];
        if (!fs->folders[f].name[0])
            fs->folders[f].type = 0;
    }
    fs->folders_ok = fs->folders[0].type == 'r' && fs->folders[1].type == 'f';
    /* the flash's header */
    fs->flash_ok = 0;
    if (fs->flash && fs->flash_size >= (long)ENTRY_AREA_END && rd32(fs->flash) == 0x12344321UL
            && rd32(fs->flash + 4) == 0x43211234UL) {
        fs->table_bytes = rd16(fs->flash + 8);
        fs->blocks = rd16(fs->flash + 10);
        fs->entries_at = HEADER + fs->table_bytes;
        if (fs->table_bytes * 4 >= fs->blocks && fs->blocks * BLOCK <= (unsigned long)fs->flash_size
                && fs->blocks > FIRST_FILE_BLOCK && fs->entries_at + SLOT <= ENTRY_AREA_END)
            fs->flash_ok = 1;
    }
    fs->n_entries = 0;
    if (fs->flash_ok) {
        unsigned long at = fs->entries_at;
        while (at + SLOT <= ENTRY_AREA_END && rd32(fs->flash + at) != 0xFFFFFFFFUL) {
            fs->n_entries++;
            at += SLOT;
        }
        fs->next_entry = at;
    }
    /* RAM slots */
    for (s = 1; s < BLF_RAM_SLOTS; s++)
        if (rd32(fs->ram + DIR_AT + (unsigned long)s * SLOT + E_START) == 0)
            break;
    fs->n_slots = s - 1;
    fs->ram_ok = fs->n_slots >= 2 && rd32(fs->ram + DIR_AT + SLOT + E_START) == CLIPBOARD_AT;
    fs->language = !strncmp((const char *)fs->ram + DIR_AT, "ayuda", 6) ? 1 : 0;
    find_open(fs);
    /* the files */
    for (s = 1; s <= fs->n_slots; s++) {
        const unsigned char *e = fs->ram + DIR_AT + (unsigned long)s * SLOT;
        blf_file fl;
        where_t w;
        entry_file(e, &fl);
        w.entry = DIR_AT + (unsigned long)s * SLOT;
        w.start = rd32(e + E_START);
        w.end_text = rd32(e + E_END_TEXT);
        w.end_alloc = rd32(e + E_END_ALLOC);
        if (fs->open_kind == 0 && fs->open_index == s) {
            w.end_text = rd32(fs->ram + fs->live + 12);
            fl.open = 1;
        }
        if (w.end_alloc >= 0x100000UL || w.start > w.end_alloc || w.end_text + 1 < w.start || w.end_text > w.end_alloc)
            w.end_text = w.start - 1;   /* damaged: read as empty */
        fl.size = w.end_text + 1 - w.start;
        fl.slot = s;
        fl.system = fs->ram_ok && s <= 2;
        if (!push(fs, &fl, &w)) return 0;
    }
    for (k = 0; k < fs->n_entries; k++) {
        const unsigned char *e = fs->flash + fs->entries_at + (unsigned long)k * SLOT;
        blf_file fl;
        where_t w;
        unsigned long lo = FIRST_FILE_BLOCK * BLOCK, hi = fs->blocks * BLOCK;
        if (!e[E_FOLDER])
            continue;               /* deleted */
        entry_file(e, &fl);
        fl.in_flash = 1;
        fl.slot = k;
        fl.open = fs->open_kind == 1 && fs->open_index == k;
        w.entry = fs->entries_at + (unsigned long)k * SLOT;
        w.start = rd32(e + E_START);
        w.end_text = rd32(e + E_END_TEXT);
        w.end_alloc = rd32(e + E_END_ALLOC);
        if (w.start < FLASH_PTR + lo || w.end_alloc >= FLASH_PTR + hi || w.end_text + 1 < w.start
                || w.end_text > w.end_alloc)
            w.end_text = w.start - 1;
        fl.size = w.end_text + 1 - w.start;
        if (!push(fs, &fl, &w)) return 0;
    }
    return 1;
}

blf_fs *blf_open(int model, unsigned char *ram, unsigned char *flash, long flash_size, char *err, int errlen)
{
    blf_fs *fs;
    if (model != BLF_BRAILLE_LITE && model != BLF_TYPE_N_SPEAK) { fail(err, errlen, "unknown unit"); return NULL; }
    fs = (blf_fs *)calloc(1, sizeof *fs);
    if (!fs) { fail(err, errlen, "out of memory"); return NULL; }
    fs->model = model;
    fs->ram = ram;
    fs->flash = flash;
    fs->flash_size = flash_size;
    fs->ram_top = 0x100000UL;       /* the top of RAM, as the firmware counts its free pages: the Braille Lite's 768 KB
                                       end at FFFFFh (its power-on sizing finds them; it gives files RAM up there:
                                       test_ram_full.c), the Type 'n Speak said 192 pages free above 3FE40h */
    fs->open_no_back = model == BLF_BRAILLE_LITE ? 48 : 51;
    if (!scan(fs)) { blf_close(fs); fail(err, errlen, "out of memory"); return NULL; }
    return fs;
}

void blf_close(blf_fs *fs)
{
    if (!fs) return;
    free(fs->files);
    free(fs->where);
    free(fs);
}

int blf_count(const blf_fs *fs) { return fs->n; }
const blf_file *blf_get(const blf_fs *fs, int i) { return i >= 0 && i < fs->n ? &fs->files[i] : NULL; }
int blf_flash_ok(const blf_fs *fs) { return fs->flash_ok; }
int blf_ram_ok(const blf_fs *fs) { return fs->ram_ok; }
int blf_folders_ok(const blf_fs *fs) { return fs->folders_ok; }
int blf_open_known(const blf_fs *fs) { return fs->live >= 0 && fs->open_no >= 0; }
int blf_language(const blf_fs *fs) { return fs->language; }
const blf_folder *blf_folder_get(const blf_fs *fs, int f) { return f >= 0 && f < BLF_FOLDERS ? &fs->folders[f] : NULL; }

const unsigned char *blf_data(const blf_fs *fs, int i, unsigned long *n)
{
    const where_t *w;
    if (i < 0 || i >= fs->n) { *n = 0; return NULL; }
    w = &fs->where[i];
    *n = fs->files[i].size;
    return fs->files[i].in_flash ? fs->flash + (w->start - FLASH_PTR) : fs->ram + w->start;
}

int blf_find(const blf_fs *fs, const char *name)
{
    int i;
    for (i = 0; i < fs->n; i++)
        if (!ci_cmp(fs->files[i].name, name, -1))
            return i;
    return -1;
}

int blf_folder_find(const blf_fs *fs, const char *name)
{
    int f;
    for (f = 0; f < BLF_FOLDERS; f++)
        if (fs->folders[f].name[0] && !ci_cmp(fs->folders[f].name, name, -1))
            return f;
    return -1;
}

static unsigned long ram_end(const blf_fs *fs)   /* the byte after the last RAM file */
{
    if (!fs->n_slots) return 0;
    return rd32(fs->ram + DIR_AT + (unsigned long)fs->n_slots * SLOT + E_END_ALLOC) + 1;
}

void blf_free_space(const blf_fs *fs, unsigned long *ram_free, unsigned long *flash_free)
{
    unsigned long b, n = 0, end = ram_end(fs);
    if (ram_free)
        *ram_free = fs->ram_ok && end < fs->ram_top ? (fs->ram_top - end) / PAGE * PAGE : 0;
    if (fs->flash_ok)
        for (b = 0; b < fs->blocks; b++)
            n += block_state(fs, b) == B_FREE;
    if (flash_free)
        *flash_free = n * BLOCK;
}

/* ---- names and types (the units' own rules, checked on the running firmware for the cases test_files makes) -- */
int blf_name_ok(const char *name)
{
    static const char *other = "_^$~!#%&-{}()@'` ";
    size_t n = strlen(name), i;
    const char *dot = NULL;
    if (n < 1 || n > BLF_NAME_MAX || name[n - 1] == ' ' || name[n - 1] == '.')
        return 0;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c == '.') {
            if (dot) return 0;
            dot = name + i;
        } else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || strchr(other, c)))
            return 0;
    }
    return !dot || strlen(dot + 1) <= 3;
}

int blf_type_for_name(const blf_fs *fs, const char *name)
{
    /* extension -> type: '?' other (binary), 'X' a program, 'B' grade 2 braille */
    static const char *const bl_en[] = {"brl", "B", "com", "?", "exe", "?", "dic", "?", "bin", "?", "zip", "?", "sys", "?",
                                        "bns", "X", "bfm", "B", "bcf", "?", NULL};
    static const char *const bl_es[] = {"bra", "B", "com", "?", "exe", "?", "dic", "?", "bin", "?", "zip", "?", "sys", "?",
                                        "bns", "X", "bfm", "B", "brf", "B", "bcf", "?", NULL};
    static const char *const tns_en[] = {"meg", "B", "brf", "B", "brl", "B", "com", "?", "exe", "?", "dic", "?", "bin", "?",
                                         "sys", "?", "zip", "?", "bns", "X", "tns", "X", "bfm", "B", "bcf", "?", NULL};
    static const char *const tns_es[] = {"meg", "B", "brf", "B", "bra", "B", "com", "?", "exe", "?", "dic", "?", "bin", "?",
                                         "sys", "?", "zip", "?", "bns", "X", "tns", "X", "bfm", "B", "bcf", "?", NULL};
    const char *const *t = fs->model == BLF_BRAILLE_LITE ? (fs->language ? bl_es : bl_en)
                                                          : (fs->language ? tns_es : tns_en);
    const char *dot = strrchr(name, '.');
    int i;
    if (!ci_cmp(name, "help", -1))
        return 'A';
    if (dot && dot[1])
        for (i = 0; t[i]; i += 2)
            if (!ci_cmp(dot + 1, t[i], -1))
                return t[i + 1][0];
    if (fs->model == BLF_TYPE_N_SPEAK)
        return 'A';                 /* a text unit: anything else is text */
    if (!dot || !dot[1] || !ci_cmp(dot + 1, "br", 2))
        return 'B';                 /* the Braille Lite: no extension, or .br?, is grade 2 */
    return 'A';
}

/* ---- changes: RAM --------------------------------------------------------------------------------------------- */
static void fill_entry(unsigned char *e, const char *name, int folder, int type, int prot, unsigned long start,
                       unsigned long n, unsigned long alloc, int column, const unsigned char *format, unsigned t,
                       unsigned d)
{
    memset(e, 0, SLOT);
    memcpy(e + E_NAME, name, strlen(name));
    e[E_FOLDER] = (unsigned char)folder;
    wr32(e + E_CURSOR, start);
    wr32(e + E_END_TEXT, start + n - 1 - (blf_break == 3 && !(start >= FLASH_PTR) && n ? 1 : 0));
    wr32(e + E_START, start);
    wr32(e + E_END_ALLOC, start + alloc - 1);
    e[E_COLUMN] = (unsigned char)column;
    e[E_TYPE] = (unsigned char)type;
    e[E_PROT] = (unsigned char)prot;
    wr32(e + E_MARK, start);
    memcpy(e + E_FORMAT, format, 6);
    wr16(e + E_TIME, t);
    wr16(e + E_DATE, d);
}

static const unsigned char *format_bytes(const blf_fs *fs)
{
    return fs->ram_ok ? fs->ram + DIR_AT + SLOT + E_FORMAT : DEFAULT_FORMAT;   /* the clipboard's, as a new file's */
}

static unsigned long pages_for(unsigned long n)
{
    return (n ? (n + PAGE - 1) / PAGE : 1) * PAGE;
}

/* the open RAM file's live end of text, cursor and mark into its entry, as the unit does when it closes a file */
static void fold_open(blf_fs *fs)
{
    unsigned char *e;
    if (fs->live < 0 || fs->open_kind != 0 || fs->open_index < 1)
        return;
    e = fs->ram + DIR_AT + (unsigned long)fs->open_index * SLOT;
    memcpy(e + E_CURSOR, fs->ram + fs->live, 4);
    memcpy(e + E_END_TEXT, fs->ram + fs->live + 12, 4);
    memcpy(e + E_MARK, fs->ram + fs->live + 20, 4);
}

static int ram_add(blf_fs *fs, int folder, const char *name, int type, int prot, const unsigned char *data,
                   unsigned long n, unsigned t, unsigned d, char *err, int errlen)
{
    unsigned long start = ram_end(fs), alloc = pages_for(n);
    int s = fs->n_slots + 1;
    if (!fs->ram_ok)
        return fail(err, errlen, "the unit's RAM file system is not initialised");
    if (s >= BLF_RAM_SLOTS)
        return fail(err, errlen, "the unit's RAM holds 127 files already");
    if (start + alloc > fs->ram_top)
        return fail(err, errlen, "no room in the unit's RAM");
    memcpy(fs->ram + start, data, n);
    memset(fs->ram + start + n, 0, alloc - n);
    fill_entry(fs->ram + DIR_AT + (unsigned long)s * SLOT, name, folder, type, prot, start, n, alloc, 1,
               format_bytes(fs), t, d);
    return scan(fs) || fail(err, errlen, "out of memory");
}

/* RAM slot s deleted (del) or given new text: the files after it move, the open file's live pointers and number
   with them */
static int ram_change(blf_fs *fs, int s, int del, const unsigned char *data, unsigned long n, unsigned t, unsigned d,
                      char *err, int errlen)
{
    unsigned char *e = fs->ram + DIR_AT + (unsigned long)s * SLOT, *ram = fs->ram;
    unsigned long start = rd32(e + E_START), old_alloc = rd32(e + E_END_ALLOC) + 1 - start;
    unsigned long new_alloc = del ? 0 : pages_for(n), end = ram_end(fs), after = start + old_alloc;
    long delta;
    int k, open_slot = fs->open_kind == 0 ? fs->open_index : -1, new_open = open_slot;
    if (s <= 2 && fs->ram_ok) {     /* the clipboard and datebook stay where the firmware put them */
        if (del)
            return fail(err, errlen, "the clipboard and the datebook cannot be deleted");
        if (n > old_alloc)
            return fail(err, errlen, "the clipboard and the datebook keep their size");
        new_alloc = old_alloc;
    }
    if (del && open_slot == s)
        return fail(err, errlen, "the unit has this file open");
    if (del && open_slot > s)
        new_open = open_slot - 1;
    if (new_open != open_slot && fs->open_no < 0)
        return fail(err, errlen, "the unit's open file could not be followed");
    delta = (long)new_alloc - (long)old_alloc;
    if (fs->live < 0)               /* it may be this file or one after it: nothing in RAM is changed */
        return fail(err, errlen, "the unit's open file could not be found, so its RAM files stay as they are");
    if ((long)end + delta > (long)fs->ram_top)
        return fail(err, errlen, "no room in the unit's RAM");
    fold_open(fs);
    memmove(ram + after + delta, ram + after, end - after);
    if (delta < 0)
        memset(ram + end + delta, 0, (size_t)-delta);
    if (!del) {
        memcpy(ram + start, data, n);
        memset(ram + start + n, 0, new_alloc - n);
        wr32(e + E_CURSOR, start);
        wr32(e + E_END_TEXT, start + n - 1);
        wr32(e + E_END_ALLOC, start + new_alloc - 1);
        wr32(e + E_MARK, start);
        e[E_COLUMN] = 1;
        wr16(e + E_TIME, t);
        wr16(e + E_DATE, d);
    }
    for (k = s + 1; k <= fs->n_slots; k++) {
        unsigned char *x = ram + DIR_AT + (unsigned long)k * SLOT;
        static const int ptrs[] = {E_CURSOR, E_END_TEXT, E_START, E_END_ALLOC, E_MARK};
        int p;
        for (p = 0; p < 5; p++)
            wr32(x + ptrs[p], rd32(x + ptrs[p]) + (unsigned long)delta);
    }
    if (del) {
        memmove(e, e + SLOT, (size_t)(fs->n_slots - s) * SLOT);
        memset(ram + DIR_AT + (unsigned long)fs->n_slots * SLOT, 0, SLOT);
    }
    if (open_slot >= 1 && fs->live >= 0) {
        unsigned char *lp = ram + fs->live;
        if (open_slot == s) {       /* its new text: the cursor at the top */
            wr32(lp, start);
            wr32(lp + 4, start - 1);
            wr32(lp + 8, start);
            wr32(lp + 12, start + n - 1);
            wr32(lp + 16, start + new_alloc - 1);
            wr32(lp + 20, start);
        } else if (open_slot > s) {
            int p;
            for (p = 0; p < 24; p += 4)
                wr32(lp + p, rd32(lp + p) + (unsigned long)delta);
        }
        if (new_open != open_slot && blf_break != 5)
            wr16(ram + fs->open_no, (unsigned)new_open);
    }
    return scan(fs) || fail(err, errlen, "out of memory");
}

/* ---- changes: flash --------------------------------------------------------------------------------------------- */
static int entry_append(blf_fs *fs, const unsigned char *e, char *err, int errlen)
{
    unsigned long at = fs->next_entry, next = at + SLOT;
    if (next > ENTRY_AREA_END)
        return fail(err, errlen, "the flash's file list is full");
    if (!program(fs, at, e, SLOT))
        return fail(err, errlen, "the flash's file list is damaged (an entry not erased)");
    if (at / BLOCK != next / BLOCK && next / BLOCK + 1 < fs->blocks && block_state(fs, next / BLOCK + 1) == B_FREE)
        set_block(fs, next / BLOCK + 1, B_USED);   /* as the unit does: the block after the one the list grows into */
    fs->next_entry = next;
    return 1;
}

static int flash_add(blf_fs *fs, const unsigned char *proto, const char *name, int folder, const unsigned char *data,
                     unsigned long n, unsigned t, unsigned d, char *err, int errlen)
{
    unsigned long need = (n + BLOCK - 1) / BLOCK, b, run = 0, first = 0, start, i;
    unsigned char e[SLOT];
    int found = 0;
    if (!fs->flash_ok)
        return fail(err, errlen, "the unit's flash is not initialised");
    if (!n)
        return fail(err, errlen, "an empty file cannot go to flash (the unit refuses it too)");
    for (b = fs->blocks; b-- > FIRST_FILE_BLOCK; ) {   /* the topmost run of free blocks, as the unit picks it */
        if (block_state(fs, b) != B_FREE) { run = 0; continue; }
        if (++run == need) { first = b; found = 1; break; }
    }
    if (!found)
        return fail(err, errlen, "no room in the unit's flash");
    for (i = first * BLOCK; i < (first + need) * BLOCK; i++)
        if (fs->flash[i] != 0xFF)
            return fail(err, errlen, "the flash's free blocks are not erased");
    if (fs->next_entry + SLOT > ENTRY_AREA_END)
        return fail(err, errlen, "the flash's file list is full");
    if (blf_break != 2)
        for (b = first; b < first + need; b++)
            set_block(fs, b, B_USED);
    program(fs, first * BLOCK, data, n);
    start = FLASH_PTR + first * BLOCK;
    if (proto)
        memcpy(e, proto, SLOT);
    else
        memset(e, 0, SLOT);
    fill_entry(e, name, blf_break == 7 && !proto ? 0 : folder, proto ? proto[E_TYPE] : blf_type_for_name(fs, name), 1,
               start, n, need * BLOCK, 0,
               proto ? proto + E_FORMAT : format_bytes(fs), t, d);
    if (proto)
        memcpy(e + E_PASSWORD, proto + E_PASSWORD, 7);
    if (!entry_append(fs, e, err, errlen))
        return 0;
    return 1;
}

static int flash_mark_deleted(blf_fs *fs, int i, int free_blocks)
{
    const where_t *w = &fs->where[i];
    unsigned char zero = 0;
    if (!program(fs, w->entry + E_FOLDER, &zero, 1))
        return 0;
    if (free_blocks) {
        unsigned long b, first = (w->start - FLASH_PTR) / BLOCK, nb = (w->end_text + BLOCK - w->start) / BLOCK;
        for (b = first; b < first + nb && b < fs->blocks; b++)
            if (block_state(fs, b) == B_USED)
                set_block(fs, b, B_GONE);
    }
    return 1;
}

/* ---- the public changes ----------------------------------------------------------------------------------------- */
int blf_add(blf_fs *fs, int folder, const char *name, const unsigned char *data, unsigned long n,
            unsigned dos_time, unsigned dos_date, int user_prot, char *err, int errlen)
{
    int type;
    if (blf_break == 4)
        dos_time = dos_date = 0;
    if (!blf_name_ok(name))
        return fail(err, errlen, "not a name the unit takes");
    if (blf_find(fs, name) >= 0)
        return fail(err, errlen, "the unit has a file of that name");
    if (!fs->folders_ok || folder < 0 || folder >= BLF_FOLDERS || !fs->folders[folder].type)
        return fail(err, errlen, "no such folder on the unit");
    if (fs->folders[folder].type == 'f') {
        if (!flash_add(fs, NULL, name, folder, data, n, dos_time, dos_date, err, errlen))
            return 0;
        return scan(fs) || fail(err, errlen, "out of memory");
    }
    type = blf_type_for_name(fs, name);
    return ram_add(fs, folder, name, type, (user_prot ? 2 : 0) | (type == 'X' || type == '?' ? 1 : 0), data, n,
                   dos_time, dos_date, err, errlen);
}

int blf_replace(blf_fs *fs, int i, const unsigned char *data, unsigned long n, unsigned dos_time, unsigned dos_date,
                char *err, int errlen)
{
    const blf_file *f = blf_get(fs, i);
    unsigned char proto[SLOT];
    char name[BLF_NAME_MAX + 1];
    if (!f)
        return fail(err, errlen, "no such file");
    if (!f->in_flash)
        return ram_change(fs, f->slot, 0, data, n, dos_time, dos_date, err, errlen);
    memcpy(proto, fs->flash + fs->where[i].entry, SLOT);
    snprintf(name, sizeof name, "%s", f->name);
    if (!flash_add(fs, proto, name, f->folder, data, n, dos_time, dos_date, err, errlen))
        return 0;                   /* the old one stays as it was */
    flash_mark_deleted(fs, i, 1);
    return scan(fs) || fail(err, errlen, "out of memory");
}

int blf_delete(blf_fs *fs, int i, char *err, int errlen)
{
    const blf_file *f = blf_get(fs, i);
    if (!f)
        return fail(err, errlen, "no such file");
    if (f->open)
        return fail(err, errlen, "the unit has this file open");
    if (!f->in_flash)
        return ram_change(fs, f->slot, 1, NULL, 0, 0, 0, err, errlen);
    if (!flash_mark_deleted(fs, i, 1))
        return fail(err, errlen, "the flash entry could not be marked");
    return scan(fs) || fail(err, errlen, "out of memory");
}

int blf_move(blf_fs *fs, int i, int folder, char *err, int errlen)
{
    const blf_file *f = blf_get(fs, i);
    unsigned char e[SLOT];
    if (!f)
        return fail(err, errlen, "no such file");
    if (folder < 0 || folder >= BLF_FOLDERS || fs->folders[folder].type != (f->in_flash ? 'f' : 'r'))
        return fail(err, errlen, "not a folder of the file's kind");
    if (folder == f->folder)
        return 1;
    if (!f->in_flash) {
        fs->ram[fs->where[i].entry + E_FOLDER] = (unsigned char)folder;
        return scan(fs) || fail(err, errlen, "out of memory");
    }
    memcpy(e, fs->flash + fs->where[i].entry, SLOT);   /* as the unit renames: a new entry, the old one marked */
    e[E_FOLDER] = (unsigned char)folder;
    if (!entry_append(fs, e, err, errlen))
        return 0;
    flash_mark_deleted(fs, i, 0);
    return scan(fs) || fail(err, errlen, "out of memory");
}

int blf_add_folder(blf_fs *fs, const char *name, int type, char *err, int errlen)
{
    int f;
    if (!fs->folders_ok)
        return fail(err, errlen, "the unit's folders are not initialised") - 1;
    if (!blf_name_ok(name) || (type != 'r' && type != 'f'))
        return fail(err, errlen, "not a folder name the unit takes") - 1;
    if (blf_folder_find(fs, name) >= 0)
        return fail(err, errlen, "the unit has a folder of that name") - 1;
    for (f = 0; f < BLF_FOLDERS; f++)
        if (!fs->folders[f].name[0]) {
            memset(fs->ram + FOLDER_NAMES_AT + (unsigned long)f * 21, 0, 21);
            memcpy(fs->ram + FOLDER_NAMES_AT + (unsigned long)f * 21, name, strlen(name));
            fs->ram[FOLDER_TYPES_AT + f] = (unsigned char)type;
            if (!scan(fs)) { fail(err, errlen, "out of memory"); return -1; }
            return f;
        }
    return fail(err, errlen, "the unit has 20 folders already") - 1;
}

/* ---- flash files a unit without folders lost (bl_files.h blf_lost_get) -------------------------------------------- */
static int lost_entry(const blf_fs *fs, int k, where_t *w)
{
    const unsigned char *e = fs->flash + fs->entries_at + (unsigned long)k * SLOT;
    unsigned long b, first, nb, lo = FIRST_FILE_BLOCK * BLOCK, hi = fs->blocks * BLOCK;
    int j;
    if (e[E_FOLDER] || !e[E_NAME])
        return 0;
    w->entry = fs->entries_at + (unsigned long)k * SLOT;
    w->start = rd32(e + E_START);
    w->end_text = rd32(e + E_END_TEXT);
    w->end_alloc = rd32(e + E_END_ALLOC);
    if (w->start < FLASH_PTR + lo || w->end_alloc >= FLASH_PTR + hi || w->end_text + 1 < w->start
            || w->end_text > w->end_alloc || (w->start - FLASH_PTR) % BLOCK)
        return 0;
    first = (w->start - FLASH_PTR) / BLOCK;
    nb = (w->end_text + BLOCK - w->start) / BLOCK;
    for (b = first; b < first + nb; b++)
        if (block_state(fs, b) != B_USED)
            return 0;               /* freed with the mark: the firmware's own delete */
    for (j = 0; j < fs->n_entries; j++) {   /* a live entry on the same blocks (a rename), or a later lost copy */
        const unsigned char *o = fs->flash + fs->entries_at + (unsigned long)j * SLOT;
        if (j != k && rd32(o + E_START) == w->start && (o[E_FOLDER] || j > k))
            return 0;
    }
    return 1;
}

const unsigned char *blf_lost_get(const blf_fs *fs, int i, blf_file *f, unsigned long *n)
{
    int k;
    where_t w;
    if (!fs->flash_ok)
        return NULL;
    for (k = 0; k < fs->n_entries; k++)
        if (lost_entry(fs, k, &w) && i-- == 0) {
            entry_file(fs->flash + w.entry, f);
            f->in_flash = 1;
            f->slot = k;
            f->size = w.end_text + 1 - w.start;
            *n = f->size;
            return fs->flash + (w.start - FLASH_PTR);
        }
    return NULL;
}

/* ---- the rules, checked ------------------------------------------------------------------------------------------ */
int blf_check(const blf_fs *fs, char *err, int errlen)
{
    char msg[200];
    int s;
    if (fs->ram_ok) {
        unsigned long prev = CLIPBOARD_AT - 1;
        for (s = 1; s <= fs->n_slots; s++) {
            const unsigned char *e = fs->ram + DIR_AT + (unsigned long)s * SLOT;
            unsigned long start = rd32(e + E_START), et = rd32(e + E_END_TEXT), ea = rd32(e + E_END_ALLOC);
            if (fs->open_kind == 0 && fs->open_index == s && fs->live >= 0)
                et = rd32(fs->ram + fs->live + 12);
            if (start != prev + 1 || (ea + 1 - start) % PAGE || et + 1 < start || et > ea || ea >= fs->ram_top) {
                snprintf(msg, sizeof msg, "RAM slot %d (%.20s): start %lX end %lX/%lX after %lX", s, (const char *)e,
                         start, et, ea, prev);
                return fail(err, errlen, msg);
            }
            prev = ea;
        }
    }
    if (fs->flash_ok) {
        unsigned char *owner = (unsigned char *)calloc(fs->blocks, 1);
        int i;
        if (!owner)
            return fail(err, errlen, "out of memory");
        for (i = 0; i < fs->n; i++) {
            unsigned long b, first, nb;
            if (!fs->files[i].in_flash) continue;
            first = (fs->where[i].start - FLASH_PTR) / BLOCK;
            nb = (fs->where[i].end_text + BLOCK - fs->where[i].start) / BLOCK;
            if ((fs->where[i].start - FLASH_PTR) % BLOCK || fs->where[i].end_alloc + 1 != fs->where[i].start + nb * BLOCK) {
                snprintf(msg, sizeof msg, "flash file %s: not on whole blocks", fs->files[i].name);
                free(owner);
                return fail(err, errlen, msg);
            }
            for (b = first; b < first + nb; b++) {
                if (b >= fs->blocks || owner[b] || block_state(fs, b) != B_USED) {
                    snprintf(msg, sizeof msg, "flash file %s: block %lu %s", fs->files[i].name, b,
                             b >= fs->blocks ? "outside the flash" : owner[b] ? "shared with another file"
                             : "not marked used");
                    free(owner);
                    return fail(err, errlen, msg);
                }
                owner[b] = 1;
            }
        }
        for (s = (int)FIRST_FILE_BLOCK; s < (int)fs->blocks; s++)
            if (block_state(fs, (unsigned long)s) == B_USED && !owner[s]) {
                snprintf(msg, sizeof msg, "flash block %d marked used but no file's", s);
                free(owner);
                return fail(err, errlen, msg);
            }
        free(owner);
    }
    return 1;
}
