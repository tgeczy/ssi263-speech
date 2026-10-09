/* bl_files_xfer.c -- see bl_files_xfer.h. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bl_cp850.h"
#include "bl_cp852.h"
#include "bl_files_xfer.h"
#include "fat_img.h"

#define EMPTY_FOLDER_DATE 0x0021    /* 1 January 1980: a folder with no files to date it */

/* ---- the report ------------------------------------------------------------------------------------------------- */
void blx_report_init(blx_report *r)
{
    memset(r, 0, sizeof *r);
}

void blx_report_free(blx_report *r)
{
    free(r->log);
    memset(r, 0, sizeof *r);
}

static void say(blx_report *r, const char *fmt, ...)
{
    char line[1200];
    va_list ap;
    size_t n;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    n = strlen(line);
    if (r->len + n + 2 > r->cap) {
        size_t cap = (r->len + n + 2) * 2;
        char *nl = (char *)realloc(r->log, cap);
        if (!nl) return;
        r->log = nl;
        r->cap = cap;
    }
    memcpy(r->log + r->len, line, n);
    r->len += n;
    r->log[r->len++] = '\n';
    r->log[r->len] = 0;
}

static int fail(char *err, int errlen, const char *msg)
{
    if (err && errlen > 0)
        snprintf(err, (size_t)errlen, "%s", msg);
    return 0;
}

/* ---- names ------------------------------------------------------------------------------------------------------ */
static const unsigned short *high_half(int codepage)
{
    return codepage == BLX_CP852 ? bl_cp852_high : bl_cp850_high;
}

void blx_unit_to_utf8(const char *unit, char *out, size_t cap)
{
    blx_unit_to_utf8_cp(BLX_CP850, unit, out, cap);
}

void blx_utf8_to_unit(const char *utf8, char *out, size_t cap)
{
    blx_utf8_to_unit_cp(BLX_CP850, utf8, out, cap);
}

void blx_unit_to_utf8_cp(int codepage, const char *unit, char *out, size_t cap)
{
    const unsigned short *high = high_half(codepage);
    const unsigned char *s = (const unsigned char *)unit;
    size_t n = 0;
    for (; *s && n + 4 < cap; s++) {
        unsigned c = *s < 0x80 ? *s : high[*s - 0x80];
        if (c < 0x80) out[n++] = (char)c;
        else if (c < 0x800) { out[n++] = (char)(0xC0 | (c >> 6)); out[n++] = (char)(0x80 | (c & 0x3F)); }
        else { out[n++] = (char)(0xE0 | (c >> 12)); out[n++] = (char)(0x80 | ((c >> 6) & 0x3F)); out[n++] = (char)(0x80 | (c & 0x3F)); }
    }
    out[n] = 0;
}

void blx_utf8_to_unit_cp(int codepage, const char *utf8, char *out, size_t cap)
{
    const unsigned short *high = high_half(codepage);
    const unsigned char *s = (const unsigned char *)utf8;
    size_t n = 0;
    while (*s && n + 1 < cap) {
        unsigned long c = *s;
        int k, more = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
        if (more < 0) { s++; out[n++] = '_'; continue; }
        c &= more ? (0x3F >> more) : 0x7F;
        for (k = 1; k <= more && (s[k] & 0xC0) == 0x80; k++)
            c = (c << 6) | (s[k] & 0x3F);
        s += k;
        if (c < 0x80)
            out[n++] = (char)c;
        else {
            int i;
            for (i = 0; i < 128 && high[i] != c; i++) ;
            out[n++] = i < 128 ? (char)(0x80 + i) : '_';
        }
    }
    out[n] = 0;
}

/* a name made into one the unit takes (bl_files.h's rules); 0 if nothing is left of it */
static int unit_name(const char *in, char *out)
{
    char tmp[300];
    const char *dot;
    size_t n, i, base_n, ext_n = 0;
    static const char *other = "_^$~!#%&-{}()@'` ";
    snprintf(tmp, sizeof tmp, "%s", in);
    n = strlen(tmp);
    while (n && (tmp[n - 1] == ' ' || tmp[n - 1] == '.'))
        tmp[--n] = 0;
    dot = strrchr(tmp, '.');
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)tmp[i];
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
        if (tmp + i == dot) continue;
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c && strchr(other, c))))
            c = '_';
        tmp[i] = (char)c;
    }
    if (dot) {
        ext_n = strlen(dot + 1);
        if (ext_n > 3) ext_n = 3;
        base_n = (size_t)(dot - tmp);
    } else
        base_n = n;
    if (base_n + (ext_n ? ext_n + 1 : 0) > BLF_NAME_MAX)
        base_n = BLF_NAME_MAX - (ext_n ? ext_n + 1 : 0);
    if (!base_n && !ext_n)
        return 0;
    memcpy(out, tmp, base_n);
    if (ext_n) {
        out[base_n] = '.';
        memcpy(out + base_n + 1, dot + 1, ext_n);
    }
    out[base_n + (ext_n ? ext_n + 1 : 0)] = 0;
    n = strlen(out);
    while (n && (out[n - 1] == ' ' || out[n - 1] == '.'))
        out[--n] = 0;
    return n && blf_name_ok(out);
}

static int ci_eq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return *a == *b;
}

/* ---- export ----------------------------------------------------------------------------------------------------- */
unsigned char *blx_export(const blf_fs *fs, int model, unsigned long *size, blx_report *r, char *err, int errlen)
{
    return blx_export_cp(fs, model, BLX_CP850, size, r, err, errlen);
}

unsigned char *blx_export_cp(const blf_fs *fs, int model, int codepage, unsigned long *size, blx_report *r, char *err,
                             int errlen)
{
    fat_builder *b = fat_new(model == BLF_BRAILLE_LITE ? "BRAILLELITE" : "TYPENSPEAK");
    int dir_of[BLF_FOLDERS], f, i;
    char u8[200], fu8[200];
    unsigned char *img;
    if (!b) { fail(err, errlen, "out of memory"); return NULL; }
    for (f = 0; f < BLF_FOLDERS; f++) {
        const blf_folder *fo = blf_folder_get(fs, f);
        unsigned date = 0, time = 0;
        dir_of[f] = 0;
        if (!fo->name[0])
            continue;
        for (i = 0; i < blf_count(fs); i++) {   /* the folder dated by its newest file */
            const blf_file *x = blf_get(fs, i);
            if (x->folder == f && (x->dos_date > date || (x->dos_date == date && x->dos_time > time))) {
                date = x->dos_date;
                time = x->dos_time;
            }
        }
        blx_unit_to_utf8_cp(codepage, fo->name, u8, sizeof u8);
        dir_of[f] = fat_add_dir(b, 0, u8, time, date ? date : EMPTY_FOLDER_DATE);
        if (dir_of[f] < 0) {
            say(r, "folder %s: its name cannot be a folder in the image (another like it?); its files are at the top", u8);
            dir_of[f] = 0;
        }
    }
    for (i = 0; i < blf_count(fs); i++) {
        const blf_file *x = blf_get(fs, i);
        unsigned long n;
        const unsigned char *d = blf_data(fs, i, &n);
        int parent = x->folder < BLF_FOLDERS ? dir_of[x->folder] : 0;
        blx_unit_to_utf8_cp(codepage, x->name, u8, sizeof u8);
        if (parent)
            blx_unit_to_utf8_cp(codepage, blf_folder_get(fs, x->folder)->name, fu8, sizeof fu8);
        else
            snprintf(fu8, sizeof fu8, "the top");
        if (!fat_add_file(b, parent, u8, d, n, x->dos_time, x->dos_date, (x->prot & 2) != 0)) {
            say(r, "not exported: %s (its name cannot be a file name in the image)", u8);
            r->skipped++;
            continue;
        }
        r->exported++;
        say(r, "%s: %s, %lu bytes%s%s", fu8, u8, n, x->in_flash ? ", flash" : "", x->open ? " (the open file)" : "");
    }
    img = fat_build(b, size, err, errlen);
    fat_free(b);
    return img;
}

/* ---- import ----------------------------------------------------------------------------------------------------- */
typedef struct {
    char name[BLF_NAME_MAX + 1];
    char path[800];                 /* where it is in the image (for the report) */
    int folder;                     /* a unit folder, or -1 - k: the k-th folder to make */
    int entry;                      /* its FAT entry */
    int ro;
    unsigned t, d;
} want_t;

static int is_junk(const char *n)
{
    static const char *const junk[] = {"desktop.ini", "thumbs.db", "system volume information", "$recycle.bin",
                                       "recycled", NULL};
    int i;
    if (n[0] == '.')
        return 1;                   /* .DS_Store, .Trashes, ._x (macOS), .fseventsd ... */
    for (i = 0; junk[i]; i++)
        if (ci_eq(n, junk[i]))
            return 1;
    return 0;
}

/* A PC's text, to the unit's line ends: CR LF and lone LF made CR, in place; the new length.  Only text: a file with
   a NUL byte in it is not a PC's text but binary -- a compiled BASIC program (COMPILE.BNS's .BAS, a document by its
   name) ends every statement with 00h, and its line numbers and lengths hold 0Ah and 0Dh bytes that must arrive as
   they are (issue #16: its first statement ran, the next were garbage).  blf_break 6 converts it anyway. */
static unsigned long to_unit_lines(unsigned char *d, unsigned long n)
{
    unsigned long i, o = 0;
    if (blf_break != 6 && memchr(d, 0, n))
        return n;
    for (i = 0; i < n; i++) {
        if (d[i] == '\r' && i + 1 < n && d[i + 1] == '\n') { d[o++] = '\r'; i++; }
        else if (d[i] == '\n') d[o++] = '\r';
        else d[o++] = d[i];
    }
    return o;
}

int blx_import(blf_fs *fs, const unsigned char *img, unsigned long size, blx_report *r, char *err, int errlen)
{
    return blx_import_cp(fs, BLX_CP850, img, size, r, err, errlen);
}

int blx_import_cp(blf_fs *fs, int codepage, const unsigned char *img, unsigned long size, blx_report *r, char *err,
                  int errlen)
{
    fat_volume *v;
    want_t *want = NULL;
    int n_want = 0, i, k, root_folder;
    int *top_folder = NULL;          /* per FAT entry: the unit folder of a top-level image folder */
    char new_names[BLF_FOLDERS][BLF_NAME_MAX + 1];
    int new_index[BLF_FOLDERS], n_new = 0;
    char e[300];
    if (!blf_folders_ok(fs))
        return fail(err, errlen, "the unit's folders were never set up (on the unit: the cold reset, answering yes "
                    "to the file system, flash and folder questions)");
    v = fat_read(img, size, err, errlen);
    if (!v)
        return 0;
    root_folder = blf_flash_ok(fs) && blf_folder_get(fs, 1)->type == 'f' ? 1 : 0;
    want = (want_t *)calloc((size_t)fat_count(v) + 1, sizeof *want);
    top_folder = (int *)calloc((size_t)fat_count(v) + 1, sizeof *top_folder);
    if (!want || !top_folder) {
        free(want); free(top_folder); fat_close(v);
        return fail(err, errlen, "out of memory");
    }
    /* what the image holds: its top folders to the unit's folders, its files to unit names */
    for (i = 0; i < fat_count(v); i++) {
        const fat_entry *fe = fat_get(v, i);
        char un[300];
        top_folder[i] = -100;
        if (fe->parent >= 0 && top_folder[fe->parent] == -100)
            continue;               /* inside something skipped */
        if (is_junk(fe->name) || ((fe->attr & 0x06) == 0x06 && fe->dir)) {
            continue;               /* the PC's own (thumbnails, desktop.ini, a trash folder): not the unit's */
        }
        if (fe->dir) {
            int f;
            if (fe->parent >= 0) {
                say(r, "skipped folder %s/%s: the unit's folders hold no folders", fat_get(v, fe->parent)->name, fe->name);
                r->skipped++;
                continue;
            }
            blx_utf8_to_unit_cp(codepage, fe->name, un, sizeof un);
            f = blf_folder_find(fs, un);
            if (f < 0) {
                char clean[BLF_NAME_MAX + 1];
                if (!unit_name(un, clean)) {
                    say(r, "skipped folder %s: no folder name the unit takes can be made of it", fe->name);
                    r->skipped++;
                    continue;
                }
                f = blf_folder_find(fs, clean);
                if (f < 0) {
                    for (k = 0; k < n_new; k++)
                        if (ci_eq(new_names[k], clean))
                            break;
                    if (k == n_new) {
                        if (n_new == BLF_FOLDERS) {
                            say(r, "skipped folder %s: the unit holds 20 folders", fe->name);
                            r->skipped++;
                            continue;
                        }
                        snprintf(new_names[n_new++], sizeof new_names[0], "%s", clean);
                    }
                    f = -1 - k;     /* made below */
                }
            }
            top_folder[i] = f;
            continue;
        }
        if (fe->parent >= 0 && fat_get(v, fe->parent)->parent >= 0)
            continue;               /* in a skipped deeper folder */
        blx_utf8_to_unit_cp(codepage, fe->name, un, sizeof un);
        if (!unit_name(un, want[n_want].name)) {
            say(r, "skipped %s: no file name the unit takes can be made of it", fe->name);
            r->skipped++;
            continue;
        }
        if (fe->parent >= 0)
            snprintf(want[n_want].path, sizeof want[n_want].path, "%s/%s", fat_get(v, fe->parent)->name, fe->name);
        else
            snprintf(want[n_want].path, sizeof want[n_want].path, "%s", fe->name);
        for (k = 0; k < n_want; k++)
            if (ci_eq(want[k].name, want[n_want].name))
                break;
        if (k < n_want) {
            say(r, "skipped %s: the unit's names are one list, and %s is %s already", want[n_want].path,
                want[n_want].name, want[k].path);
            r->skipped++;
            continue;
        }
        if (strcmp(un, want[n_want].name))
            say(r, "%s is named %s on the unit", want[n_want].path, want[n_want].name);
        want[n_want].folder = fe->parent >= 0 ? top_folder[fe->parent] : root_folder;
        want[n_want].entry = i;
        want[n_want].ro = (fe->attr & 0x01) != 0;
        want[n_want].t = fe->dos_time;
        want[n_want].d = fe->dos_date;
        n_want++;
    }
    /* the folders to make */
    for (k = 0; k < n_new; k++) {
        int type = blf_flash_ok(fs) ? 'f' : 'r';
        new_index[k] = blf_add_folder(fs, new_names[k], type, e, sizeof e);
        if (new_index[k] < 0) {
            say(r, "folder %s not made: %s", new_names[k], e);
            r->skipped++;
        } else {
            r->folders_added++;
            say(r, "new %s folder: %s", type == 'f' ? "flash" : "RAM", new_names[k]);
        }
    }
    for (k = 0; k < n_want; k++)
        if (want[k].folder < 0)
            want[k].folder = new_index[-1 - want[k].folder];
    /* the unit's files the image lacks: gone (first, so their room is free) */
    for (i = blf_count(fs) - 1; i >= 0; i--) {
        const blf_file *x = blf_get(fs, i);
        char name[BLF_NAME_MAX + 1];
        for (k = 0; k < n_want; k++)
            if (ci_eq(want[k].name, x->name))
                break;
        if (k < n_want)
            continue;
        snprintf(name, sizeof name, "%s", x->name);
        if (x->system || x->open) {
            say(r, "kept %s: %s", name, x->system ? "the unit's own file" : "the unit has it open");
            r->kept++;
            continue;
        }
        if (!blf_delete(fs, i, e, sizeof e)) {
            say(r, "kept %s: %s", name, e);
            r->kept++;
        } else {
            say(r, "deleted %s", name);
            r->deleted++;
        }
    }
    /* the image's files */
    for (k = 0; k < n_want; k++) {
        want_t *w = &want[k];
        unsigned long n;
        unsigned char *data = fat_data(v, w->entry, &n);
        int u, type;
        if (w->folder < 0) {
            say(r, "skipped %s: its folder could not be made", w->path);
            r->skipped++;
            free(data);
            continue;
        }
        if (!data) {
            say(r, "skipped %s: damaged in the image", w->path);
            r->skipped++;
            continue;
        }
        u = blf_find(fs, w->name);
        type = u >= 0 ? blf_get(fs, u)->type : blf_type_for_name(fs, w->name);
        if (u >= 0) {
            const blf_file *x = blf_get(fs, u);
            unsigned long un;
            const unsigned char *ud = blf_data(fs, u, &un);
            int same = un == n && !memcmp(ud, data, n);
            int fkind = blf_folder_get(fs, w->folder)->type, same_kind = fkind == (x->in_flash ? 'f' : 'r');
            if (!same && (type == 'A' || type == 'B')) {
                n = to_unit_lines(data, n);
                same = un == n && !memcmp(ud, data, n);
            }
            if (same && x->folder == w->folder) {
                r->unchanged++;
                free(data);
                continue;
            }
            if (x->folder != w->folder && same_kind) {
                if (!blf_move(fs, u, w->folder, e, sizeof e)) {
                    say(r, "%s not moved: %s", w->path, e);
                    r->skipped++;
                    free(data);
                    continue;
                }
                r->moved++;
                say(r, "moved %s to %s", w->name, blf_folder_get(fs, w->folder)->name);
                if (same) { free(data); continue; }
                u = blf_find(fs, w->name);
            } else if (x->folder != w->folder) {   /* RAM to flash or back: the unit's copy goes, the image's comes */
                if (!blf_delete(fs, u, e, sizeof e)) {
                    say(r, "%s not moved: %s", w->path, e);
                    r->skipped++;
                    free(data);
                    continue;
                }
                u = -1;
            }
            if (u >= 0) {
                if (!blf_replace(fs, u, data, n, w->t, w->d, e, sizeof e)) {
                    say(r, "%s not rewritten: %s", w->path, e);
                    r->skipped++;
                } else {
                    r->replaced++;
                    say(r, "rewrote %s (%lu bytes)", w->name, n);
                }
                free(data);
                continue;
            }
        }
        if (type == 'A' || type == 'B')
            n = to_unit_lines(data, n);
        if (!n && blf_folder_get(fs, w->folder)->type == 'f') {
            say(r, "%s is empty: kept in %s (the unit keeps no empty file in flash)", w->path,
                blf_folder_get(fs, 0)->name);
            w->folder = 0;
        }
        if (!blf_add(fs, w->folder, w->name, data, n, w->t, w->d, w->ro, e, sizeof e)) {
            say(r, "%s not added: %s", w->path, e);
            r->skipped++;
        } else {
            r->added++;
            say(r, "added %s to %s (%lu bytes)", w->name, blf_folder_get(fs, w->folder)->name, n);
        }
        free(data);
    }
    free(want);
    free(top_folder);
    fat_close(v);
    return 1;
}
