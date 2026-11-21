/* main_win.c -- the Windows shell of the Blazie emulator: one window, a menu, the keyboard, the sound card.
 *
 * While the window is in front:
 *   Braille Lite: the braille keys (default S D F J K L = dots 3 2 1 4 5 6, the space bar, and A or ; for the
 *   advance bar) go to the unit as chords (chords.h); every other key goes to Windows as usual, so Alt opens the
 *   menu and Alt+F4 closes it.
 *   Type 'n Speak: the whole keyboard is the unit's (tns_keymap_win.c) -- Alt and F10 included -- except F11, which
 *   opens this program's menu.
 * Sound: waveOut, blocks of 10 ms ([sound] block_ms, 5-20), each rendered by the unit when the card gives one back --
 * the card's clock paces the unit.  A key reaches the unit at the next block rendered, which plays behind the blocks
 * already queued.  How many are kept queued is Settings > Sound buffer (audio_pace.h): automatic (60 ms, growing to at
 * most 250 ms each time the card is found to have run dry), medium (100 ms) or long (250 ms, for Remote Desktop); the
 * 0.7.0 draft's 40 ms is no longer offered (audio_pace.h).  The minute's save runs on the sound thread with the queue rendered
 * ahead, so the card plays on while the memory is written (Tomi: the emulator's speech stutters, the add-on's
 * doesn't).  BLAZIE_EMU_AUDIO_LOG=file logs each wake of the sound thread (its renders, the queue, the gaps found);
 * BLAZIE_EMU_AUDIO_STALL=ms holds the thread up to that long before 5% of its blocks (a test of the queue).
 * Settings: blazie_emu.ini beside the program.  Settings > Serial port plugs the unit's serial port
 * into a COM port (serial_win.c), for WinDisk, PCDISK or a terminal on the other end.  Settings > Double-speed upgrade
 * ([unit] double_speed, off) lets the Braille Lite's own switch double its CPU (emu_set_double_speed).
 * Firmware > Restart the unit saves it and starts it from what it saved: a power cycle, its files kept.
 *
 * Firmware: firmware\ beside the program (a release carries it), or firmware_dir= in the settings.  The Braille 'n
 * Speak 2000 (bns2000\, its factory states made by make_state.exe) is in the menu when its firmware is there, and
 * takes the Braille Lite's keys.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>
#include "audio_pace.h"
#include "brl_win.h"
#include "chords.h"
#include "emu_catalog.h"
#include "emu_unit.h"
#include "serial_win.h"
#include "tns_keymap_win.h"
#include "tns_rescue.h"
#include "tns_setup.h"
#include "../../csrc/blazie/bl_files.h"
#include "../../csrc/blazie/bl_files_state.h"
#include "../../csrc/blazie/bl_files_xfer.h"
#include "../../csrc/blazie/fat_img.h"

#define RATE_MAX 48000
#define BLOCK_MAX (RATE_MAX / 50)
#define BLOCK_MS_DEFAULT 10
#define WM_SAVE_FAILED (WM_APP + 1)        /* the sound thread's save failed: the message box, from the window */

enum { ID_EN = 100, ID_ES, ID_TNS_EN, ID_TNS_ES, ID_BNS_EN, ID_BNS_SK, ID_DE, ID_IT, ID_FACTORY, ID_EXIT, ID_EXPORT, ID_IMPORT,
       ID_ADDPROG,
       ID_RELEASE = 600,                    /* ID_RELEASE + k: g_units[k], a release unit (More languages) */
       ID_HISS = 200, ID_WHINE, ID_QUIET, ID_UNITSOUND,
       ID_OPEN_OFF = 210, ID_OPEN_UNTIL, ID_OPEN_ALWAYS, ID_POPCLICK = 215, ID_TICK, ID_QUICK = 218, ID_BRAILLE,
       ID_RATE = 220,
       ID_BUFFER = 230,                     /* ID_BUFFER + AP_AUTO .. AP_LONG: Sound buffer */
       ID_DOUBLE = 240, ID_RESTART, ID_ONEHAND_ON, ID_ONEHAND_OFF,
       ID_KEYS = 300, ID_ABOUT,
       ID_SERIAL_NONE = 400, ID_SERIAL_PORT };   /* ID_SERIAL_PORT + k: g_ports[k] */

/* The units the firmware folder can run: emu_catalog.h's list, the one every shell reads (made at start).  A unit with
   no factory state is a cold start (the Type 'n Speak's cold reset asks how to set itself up: TNS_FIRST_START), or a
   release unit whose factory state is made the first time it starts (emc_make_factory). */
static emc_unit g_units[EMC_MAX];
static int g_n_units;
/* the units the menus always had, each with its own menu item */
static const struct { const char *key; int id; } MENU_IDS[] = {
    {"bl-en", ID_EN}, {"bl-es", ID_ES}, {"tns-en", ID_TNS_EN}, {"tns-es", ID_TNS_ES}, {"bns-en", ID_BNS_EN},
    {"bns-sk", ID_BNS_SK}, {"bl-de", ID_DE}, {"bl-it", ID_IT},
};
#define N_MENU_IDS ((int)(sizeof MENU_IDS / sizeof *MENU_IDS))

/* a unit's menu command, and the unit a command starts (-1: not a unit's) */
static int unit_id(int k)
{
    int i;
    for (i = 0; i < N_MENU_IDS; i++)
        if (!strcmp(g_units[k].key, MENU_IDS[i].key))
            return MENU_IDS[i].id;
    return ID_RELEASE + k;
}

static int unit_of_id(int id)
{
    int k;
    for (k = 0; k < g_n_units; k++)
        if (unit_id(k) == id)
            return k;
    return -1;
}

static int unit_by_key(const char *key)
{
    int k;
    for (k = 0; k < g_n_units; k++)
        if (!strcmp(g_units[k].key, key))
            return k;
    return -1;
}

static HWND g_wnd;
static CRITICAL_SECTION g_lock;
static emu_unit *g_unit;
static int g_kind, g_whine = 3;          /* the idle channel: 0 silent, 1 hiss, 2 whine, 3 as the unit (by volume) */
static int g_keep_open = 1;              /* 0 off, 1 until the unit clicks off, 2 always */
static int g_popclick = 1, g_tick = 1;   /* the channel's pop and click-off, its 10 Hz tick */
static chord_state g_chord;
static HWAVEOUT g_wave;
static WAVEHDR g_hdr[AP_MAX_BLOCKS];
static short g_buf[AP_MAX_BLOCKS][BLOCK_MAX];
static int g_n_hdr;                      /* the blocks in use (ap_capacity) */
static int g_audio_ok;                   /* the card open and its thread running */
static volatile LONG g_buffer_mode = AP_AUTO;   /* Settings > Sound buffer, read by the sound thread */
static volatile LONG g_save_due;         /* the minute's save, waiting for the sound thread */
static int g_rate = 44100;               /* the sound card's rate; the unit renders at it */
static int g_block_ms = BLOCK_MS_DEFAULT;
static int g_quick;                      /* quick key response (emu_set_quick): off, as the real unit */
static int g_double;                     /* the double-speed upgrade (emu_set_double_speed): off, as most units */
static const int RATES[] = {11025, 16000, 22050, 32000, 44100, 48000};
#define N_RATES ((int)(sizeof RATES / sizeof RATES[0]))
static HANDLE g_wave_event, g_thread;
static volatile LONG g_quit;
static char g_dir[MAX_PATH], g_ini[MAX_PATH], g_fw_dir[MAX_PATH], g_save_dir[MAX_PATH];
static int g_keymap[256];                  /* virtual key -> chord bit */
static com_link *g_link;                   /* the COM port the unit's serial port is plugged into; NULL: none */
static char g_com[16];                     /* its name ("COM10"), or "" */
static HMENU g_serial_menu;
static com_port_info g_ports[COM_MAX];     /* as the Serial port menu last listed them */
static int g_n_ports;
static HWND g_brl;                         /* the braille display field (brl_win.h) */
static int g_braille;                      /* Settings > Braille display: shown; off by default */
static int g_bars;                         /* the bars held from the keyboard's Alt+Shift+Right, Left (emu_braille_bars) */
#define BRAILLE_TIMER 2
#define BRAILLE_MS 50

/* the window's title, which a screen reader reads on focus: the unit, and the COM port it is plugged into */
static void status(const char *text)
{
    char title[256];
    if (g_link)
        snprintf(title, sizeof title, "%s, serial port on %s - Blazie emulator", text, g_com);
    else
        snprintf(title, sizeof title, "%s - Blazie emulator", text);
    SetWindowTextA(g_wnd, title);
}

/* ---- the keyboard map --------------------------------------------------------------------------------------- */
static int vk_of(const char *name)
{
    /* by name, as the Linux shells' settings take them (keys.c), the character itself being awkward in a list */
    static const struct { const char *name; int vk; } NAMED[] = {
        {"comma", VK_OEM_COMMA}, {"period", VK_OEM_PERIOD}, {"semicolon", VK_OEM_1}, {"apostrophe", VK_OEM_7},
        {"slash", VK_OEM_2}, {"backslash", VK_OEM_5}, {"minus", VK_OEM_MINUS}, {"equals", VK_OEM_PLUS},
        {"grave", VK_OEM_3}, {"lbracket", VK_OEM_4}, {"rbracket", VK_OEM_6}};
    unsigned i;
    for (i = 0; i < sizeof NAMED / sizeof NAMED[0]; i++)
        if (!_stricmp(name, NAMED[i].name))
            return NAMED[i].vk;
    if (!strcmp(name, "space")) return VK_SPACE;
    if (!strcmp(name, ";")) return VK_OEM_1;
    if (!strcmp(name, ",")) return VK_OEM_COMMA;
    if (!strcmp(name, ".")) return VK_OEM_PERIOD;
    if (!strcmp(name, "/")) return VK_OEM_2;
    if (strlen(name) == 1 && ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= '0' && name[0] <= '9')))
        return name[0];
    if (strlen(name) == 1 && name[0] >= 'a' && name[0] <= 'z')
        return name[0] - 'a' + 'A';
    return 0;
}

static void map_keys(const char *setting, const char *dflt, int bit)
{
    char v[128], *tok;
    GetPrivateProfileStringA("keys", setting, dflt, v, sizeof v, g_ini);
    for (tok = strtok(v, " ,"); tok; tok = strtok(NULL, " ,")) {
        int vk = vk_of(tok);
        if (vk)
            g_keymap[vk] = bit;
    }
}

static void load_keymap(void)
{
    memset(g_keymap, 0, sizeof g_keymap);
    map_keys("dot1", "F", CHORD_DOT(1));
    map_keys("dot2", "D", CHORD_DOT(2));
    map_keys("dot3", "S", CHORD_DOT(3));
    map_keys("dot4", "J", CHORD_DOT(4));
    map_keys("dot5", "K", CHORD_DOT(5));
    map_keys("dot6", "L", CHORD_DOT(6));
    map_keys("space", "space", CHORD_SPACE);
    /* the bars: period and comma too, which no screen reader takes in a text field (Alt+Shift+Right and Left are
       JAWS's and Orca's table navigation); A the back bar, as on Linux -- before 0.8 it was a second advance */
    map_keys("advance", "semicolon period", CHORD_ADVANCE);
    map_keys("back", "A comma", CHORD_BACK);
}

/* ---- the unit ------------------------------------------------------------------------------------------------ */
static int exists(const char *path)
{
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

/* what the unit keeps while switched off (its files and settings): %APPDATA%\ssi263-speech\blazie-emu\<kind>.state */
static void saved_path(int kind, char *out, int cap)
{
    snprintf(out, cap, "%s\\%s", g_save_dir, g_units[kind].saved);
}

static void save_unit(void)
{
    char path[MAX_PATH];
    int ok = 1;
    if (!g_unit)
        return;
    CreateDirectoryA(g_save_dir, NULL);
    saved_path(g_kind, path, sizeof path);
    EnterCriticalSection(&g_lock);
    ok = emu_save_whole(g_unit, path);    /* whole or not at all (#37) */
    LeaveCriticalSection(&g_lock);
    if (!ok)
        MessageBoxA(g_wnd, "Could not save the unit's memory; what you wrote this time is lost.", "Blazie emulator",
                    MB_OK | MB_ICONWARNING);
}

/* A saved Type 'n Speak that was never set up (tns_rescue.h: the 0.6 and 0.7 previews' first start missed the unit's
   cold reset): the person chooses -- set it up now keeping its RAM files, the factory state (its own questions), or
   as it is.  0: start it from no state (the factory's cold start); 1: from st. */
static int offer_setup(int kind, const char *fw, const char *st)
{
    char msg[4000], lost[300] = "", err[300], before[MAX_PATH + 20];
    tns_rescue_report r;
    int answer;
    emu_unit *old;
    if (tns_needs_setup(st, &r) != 1)
        return 1;
    EnterCriticalSection(&g_lock);          /* the running unit (already saved) switched off: the minute's save, */
    old = g_unit;                           /* which runs while a message box is up, must not write it over the */
    g_unit = NULL;                          /* state set up here */
    LeaveCriticalSection(&g_lock);
    emu_destroy(old);
    if (r.lost_flash)
        snprintf(lost, sizeof lost, ", and %d it lost when moving them to flash (their text was never written: "
                 "only their names are left)", r.lost_flash);
    snprintf(before, sizeof before, "%s.before-setup", st);
    snprintf(msg, sizeof msg, "This %s was never set up: its file system and folders were not made when it first "
             "started (the 0.6 and 0.7 previews' first start missed the unit's cold reset; or a setup question was "
             "answered n). On it a new file can lose its first letter, and a file moved to flash is lost.\n\n"
             "Its files: %d in RAM%s.\n\n"
             "Yes: set it up now and keep its RAM files (in its RAM startup folder). Its settings go back to the "
             "factory's.\n"
             "No: start it from the factory state: it asks its own setup questions, and its files are not kept.\n"
             "Cancel: start it as it is.\n\n"
             "Yes and No keep the old memory as %s.", g_units[kind].name, r.ram_files, lost, before);
    answer = MessageBoxA(g_wnd, msg, "Type 'n Speak not set up", MB_YESNOCANCEL | MB_ICONWARNING);
    if (answer == IDNO) {
        if (!CopyFileA(st, before, FALSE)) {
            MessageBoxA(g_wnd, "Could not keep the old memory; the unit starts as it is.", "Blazie emulator",
                        MB_OK | MB_ICONERROR);
            return 1;
        }
        DeleteFileA(st);
        return 0;
    }
    if (answer != IDYES)
        return 1;
    status("Setting up the Type 'n Speak (a few seconds)");
    if (!tns_rescue(fw, st, 1, &r, err, sizeof err)) {
        snprintf(msg, sizeof msg, "Could not set the unit up: %s\n\nIt starts as it is.", err);
        MessageBoxA(g_wnd, msg, "Blazie emulator", MB_OK | MB_ICONERROR);
        return 1;
    }
    snprintf(msg, sizeof msg, "The %s is set up: %d files kept%s. The old memory is in %s.\n\n%s", g_units[kind].name,
             r.carried, r.first_lost ? " (one had lost its first letter on the old unit)" : "", before, r.log);
    MessageBoxA(g_wnd, msg, "Type 'n Speak set up", MB_OK | MB_ICONINFORMATION);
    return 1;
}

/* keys the next start_unit holds as the unit is switched on (emu_hold_at_start): one-handed mode's dot 6 or dot 3 */
static int g_hold_at_start;

static int start_unit(int kind)
{
    char fw[MAX_PATH], st[MAX_PATH], err[256];
    const char *state = st;
    emu_unit *u;
    save_unit();                            /* first: the unit being left keeps its memory (it may be this kind) */
    snprintf(fw, sizeof fw, "%s", g_units[kind].firmware);
    saved_path(kind, st, sizeof st);
    if (exists(st) && g_units[kind].kind == EMU_TYPE_N_SPEAK && !offer_setup(kind, fw, st))
        state = NULL;                       /* the factory state: the unit's cold reset, its own questions */
    else if (!exists(st) && emc_needs_factory(&g_units[kind])) {
        /* a release unit's first start: its factory state made from its firmware, by its own recipe, as its memory */
        char msg[700];
        status("Setting up the unit for its first start (a few seconds)");
        CreateDirectoryA(g_save_dir, NULL);
        if (!emc_make_factory(&g_units[kind], st, NULL, NULL, err, sizeof err)) {
            snprintf(msg, sizeof msg, "Could not set up the %s for its first start.\n\n%s", g_units[kind].name, err);
            MessageBoxA(g_wnd, msg, "Blazie emulator", MB_OK | MB_ICONERROR);
            return 0;
        }
    } else if (!exists(st)) {               /* the first time: the unit as it left the factory */
        if (g_units[kind].factory[0])
            snprintf(st, sizeof st, "%s", g_units[kind].factory);
        else
            state = NULL;                   /* a cold start */
    }
    if (!state && g_units[kind].kind == EMU_TYPE_N_SPEAK)
        MessageBoxA(g_wnd, TNS_FIRST_START, g_units[kind].name, MB_OK | MB_ICONINFORMATION);
    status("Starting");
    u = emu_create(g_units[kind].kind, fw, state, g_rate, g_whine, err, sizeof err);
    if (!u) {
        char msg[700];
        snprintf(msg, sizeof msg, "Could not start the %s.\n\n%s\n\nFirmware looked for in:\n%s", g_units[kind].name,
                 err, g_fw_dir);
        MessageBoxA(g_wnd, msg, "Blazie emulator", MB_OK | MB_ICONERROR);
        return 0;
    }
    if (g_link)
        emu_serial_attach(u, 1);            /* the new unit takes the old one's place on the COM port */
    emu_set_idle(u, g_whine, g_keep_open, g_popclick, g_tick);
    emu_set_quick(u, g_quick);
    emu_set_double_speed(u, g_double);
    emu_hold_at_start(u, g_hold_at_start);  /* before the sound thread runs it: held from its first instant */
    g_hold_at_start = 0;
    EnterCriticalSection(&g_lock);
    emu_destroy(g_unit);
    g_unit = u;
    g_kind = kind;
    chord_reset(&g_chord);
    LeaveCriticalSection(&g_lock);
    status(g_units[kind].name);
    {
        int k;
        for (k = 0; k < g_n_units; k++)
            CheckMenuItem(GetMenu(g_wnd), unit_id(k), MF_BYCOMMAND | MF_UNCHECKED);
        CheckMenuRadioItem(GetMenu(g_wnd), unit_id(kind), unit_id(kind), unit_id(kind), MF_BYCOMMAND);
        /* one-handed mode is the braille keyboard's: the Type 'n Speak's manual has none */
        for (k = ID_ONEHAND_ON; k <= ID_ONEHAND_OFF; k++)
            EnableMenuItem(GetMenu(g_wnd), k, MF_BYCOMMAND |
                           (g_units[kind].kind == EMU_TYPE_N_SPEAK ? MF_GRAYED : MF_ENABLED));
    }
    WritePrivateProfileStringA("unit", "kind", g_units[kind].ini, g_ini);
    return 1;
}

/* ---- files in and out: the unit's files as a FAT disk image (../../csrc/blazie/bl_files_xfer.h) --------------- */
static void tns_release_all(void);

static int image_dialog(int save, char *path, int cap)
{
    OPENFILENAMEA o;
    memset(&o, 0, sizeof o);
    o.lStructSize = sizeof o;
    o.hwndOwner = g_wnd;
    o.lpstrFilter = "Disk images (*.img)\0*.img\0All files (*.*)\0*.*\0";
    o.lpstrFile = path;
    o.nMaxFile = (DWORD)cap;
    o.lpstrDefExt = "img";
    o.lpstrTitle = save ? "Export the unit's files to a disk image" : "Import files from a disk image";
    o.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    return save ? GetSaveFileNameA(&o) : GetOpenFileNameA(&o);
}

/* the report's first lines, after a summary, for a message box a screen reader reads out */
static void report_text(char *out, size_t cap, const char *summary, const blx_report *r)
{
    const char *p = r->log ? r->log : "";
    int lines = 0;
    size_t n;
    snprintf(out, cap, "%s", summary);
    n = strlen(out);
    while (*p && lines < 25 && n + 4 < cap) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (n + len + 3 >= cap) break;
        out[n++] = '\n';
        memcpy(out + n, p, len);
        n += len;
        out[n] = 0;
        lines++;
        p = e ? e + 1 : p + len;
    }
    if (*p && n + 8 < cap)
        snprintf(out + n, cap - n, "\n...");
}

/* the state as saved, opened for bl_files; 0 with a message box */
static int open_saved(const char *st, bls_unit *u, blf_fs **fs)
{
    char err[300], msg[700];
    if (!bls_load(st, u, err, sizeof err)) {
        snprintf(msg, sizeof msg, "Could not read the unit's memory (%s): %s", st, err);
        MessageBoxA(g_wnd, msg, "Blazie emulator", MB_OK | MB_ICONERROR);
        return 0;
    }
    *fs = blf_open(u->model, u->ram, u->flash, u->flash_size, err, sizeof err);
    if (!*fs) {
        bls_free(u);
        snprintf(msg, sizeof msg, "Could not read the unit's files: %s", err);
        MessageBoxA(g_wnd, msg, "Blazie emulator", MB_OK | MB_ICONERROR);
        return 0;
    }
    return 1;
}

static int write_bytes(const char *path, const unsigned char *d, unsigned long n)
{
    FILE *f = fopen(path, "wb");
    int ok;
    if (!f) return 0;
    ok = fwrite(d, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

static unsigned char *read_bytes(const char *path, unsigned long *n)
{
    FILE *f = fopen(path, "rb");
    long size;
    unsigned char *d;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    d = size > 0 ? (unsigned char *)malloc((size_t)size) : NULL;
    if (d && fread(d, 1, (size_t)size, f) != (size_t)size) { free(d); d = NULL; }
    fclose(f);
    *n = (unsigned long)size;
    return d;
}

/* every file the unit holds, as it holds them now (the unit runs on) */
static void export_files(void)
{
    char path[MAX_PATH], st[MAX_PATH], err[300], summary[600], msg[4000];
    bls_unit u;
    blf_fs *fs;
    blx_report r;
    unsigned char *img;
    unsigned long size;
    if (!g_unit) return;
    tns_release_all();
    snprintf(path, sizeof path, "%s files.img", g_units[g_kind].name);
    if (!image_dialog(1, path, sizeof path)) return;
    save_unit();                            /* its memory as it is this moment */
    saved_path(g_kind, st, sizeof st);
    if (!open_saved(st, &u, &fs)) return;
    blx_report_init(&r);
    img = blx_export_cp(fs, u.model, g_units[g_kind].codepage, &size, &r, err, sizeof err);
    if (!img || !write_bytes(path, img, size)) {
        snprintf(msg, sizeof msg, "Could not export the files: %s", img ? "the image could not be written" : err);
        MessageBoxA(g_wnd, msg, "Export files", MB_OK | MB_ICONERROR);
    } else {
        snprintf(summary, sizeof summary, "Exported %d files to %s.\n\nEach of the unit's folders is a folder in "
                 "the image; 7-Zip opens it. Text files keep the unit's line ends (a lone carriage return).",
                 r.exported, path);
        report_text(msg, sizeof msg, summary, &r);
        MessageBoxA(g_wnd, msg, "Export files", MB_OK | MB_ICONINFORMATION);
    }
    free(img);
    blx_report_free(&r);
    blf_close(fs);
    bls_free(&u);
}

/* the unit switched off for its files to change: only while it is not writing its flash; its memory saved to st
   first.  1 off, or 0 with a message box (`doing`: "Import the files", ...) and the unit running on */
static int unit_off(const char *title, const char *doing, char *st, size_t cap)
{
    emu_unit *old;
    int busy, ok;
    char msg[300];
    CreateDirectoryA(g_save_dir, NULL);
    saved_path(g_kind, st, cap);
    EnterCriticalSection(&g_lock);
    busy = emu_flash(g_unit, NULL);
    ok = !busy && emu_save_whole(g_unit, st);
    old = ok ? g_unit : NULL;
    if (ok)
        g_unit = NULL;                      /* switched off: nothing runs while its memory changes */
    LeaveCriticalSection(&g_lock);
    if (!ok) {
        if (busy)
            snprintf(msg, sizeof msg, "The unit is writing its flash memory. %s when it has finished (when it is "
                     "quiet).", doing);
        else
            snprintf(msg, sizeof msg, "Could not save the unit's memory; nothing was changed.");
        MessageBoxA(g_wnd, msg, title, MB_OK | MB_ICONWARNING);
        return 0;
    }
    emu_destroy(old);
    return 1;
}

/* the image's files into the unit: only while it is not writing its flash; it is switched off, its files changed,
   and switched on again -- the firmware then finds them as if it had written them */
static void import_files(void)
{
    char path[MAX_PATH] = "", st[MAX_PATH], err[300], summary[1200], msg[4000];
    unsigned char *img;
    unsigned long size;
    fat_volume *v;
    bls_unit u;
    blf_fs *fs;
    blx_report r;
    int ok, cancelled = 0;
    if (!g_unit) return;
    tns_release_all();
    if (!image_dialog(0, path, sizeof path)) return;
    img = read_bytes(path, &size);
    v = img ? fat_read(img, size, err, sizeof err) : NULL;
    if (!v) {
        snprintf(msg, sizeof msg, "%s is not a disk image the emulator can read: %s", path, img ? err : "unreadable");
        MessageBoxA(g_wnd, msg, "Import files", MB_OK | MB_ICONERROR);
        free(img);
        return;
    }
    fat_close(v);
    if (!unit_off("Import files", "Import the files", st, sizeof st)) {
        free(img);
        return;
    }
    blx_report_init(&r);
    ok = open_saved(st, &u, &fs);
    if (ok) {
        ok = blx_import_cp(fs, g_units[g_kind].codepage, img, size, &r, err, sizeof err)
             && blf_check(fs, err, sizeof err);
        if (ok && r.deleted) {              /* an image holding only new files would empty the unit: ask */
            char ask[3000];
            const char *line = r.log ? r.log : "";
            size_t n = (size_t)snprintf(ask, sizeof ask, "The image does not have %d of the unit's files. Importing "
                                        "it deletes them from the unit:\n", r.deleted);
            while (*line && n + 80 < sizeof ask) {   /* the report's "deleted NAME" lines */
                const char *e = strchr(line, '\n');
                size_t len = e ? (size_t)(e - line) : strlen(line);
                if (len > 8 && !strncmp(line, "deleted ", 8))
                    n += (size_t)snprintf(ask + n, sizeof ask - n, "\n%.*s", (int)(len - 8), line + 8);
                line = e ? e + 1 : line + len;
            }
            snprintf(ask + n, sizeof ask - n, "\n\nDelete them and import? No imports nothing.");
            if (MessageBoxA(g_wnd, ask, "Import files", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
                ok = 0, cancelled = 1;
        }
        if (ok) {                           /* the unit as it was, kept beside it: undone by one file copy */
            char before[MAX_PATH + 20];
            snprintf(before, sizeof before, "%s.before-import", st);
            CopyFileA(st, before, FALSE);
            ok = bls_save(st, &u, err, sizeof err);
        }
        blf_close(fs);
        bls_free(&u);
    }
    free(img);
    start_unit(g_kind);                     /* switched on again */
    if (cancelled)
        MessageBoxA(g_wnd, "Nothing was imported. The unit's files are as they were.", "Import files",
                    MB_OK | MB_ICONINFORMATION);
    else if (!ok) {
        snprintf(summary, sizeof summary, "Nothing was imported: %s\n\nThe unit was restarted with its files as "
                 "they were.", err);
        report_text(msg, sizeof msg, summary, &r);
        MessageBoxA(g_wnd, msg, "Import files", MB_OK | MB_ICONERROR);
    } else {
        snprintf(summary, sizeof summary, "Imported from %s: %d added, %d rewritten, %d moved, %d deleted, %d "
                 "unchanged, %d kept, %d skipped%s. The unit was restarted. (The unit as it was before is kept in "
                 "%s.before-import.)", path, r.added, r.replaced, r.moved, r.deleted, r.unchanged, r.kept, r.skipped,
                 r.folders_added ? ", new folders made" : "", st);
        report_text(msg, sizeof msg, summary, &r);
        MessageBoxA(g_wnd, msg, "Import files", MB_OK | (r.skipped || r.kept ? MB_ICONWARNING : MB_ICONINFORMATION));
    }
    blx_report_free(&r);
}

/* Firmware > Load programs and files into flash (programs, BASIC, text, braille): one or more files chosen together
   go into the flash startup folder as one batch, all or nothing (bl_files_xfer.h blx_add_cp), the unit switched off
   and on once, as import does */
#define ADD_MAX 256

static int add_dialog(wchar_t *buf, DWORD cap)
{
    OPENFILENAMEW o;
    memset(&o, 0, sizeof o);
    o.lStructSize = sizeof o;
    o.hwndOwner = g_wnd;
    o.lpstrFilter = L"Programs and documents (*.bns, *.bas, *.txt, *.brl, *.brf)\0*.bns;*.bas;*.txt;*.brl;*.brf\0"
                    L"Blazie programs (*.bns)\0*.bns\0All files (*.*)\0*.*\0";
    o.lpstrFile = buf;
    o.nMaxFile = cap;
    o.lpstrTitle = L"Load programs and files into the unit's flash";
    o.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    buf[0] = 0;
    return GetOpenFileNameW(&o);
}

/* a chosen file read whole, its name (no folder) in UTF-8 and its last write as DOS time; NULL when unreadable */
static unsigned char *read_chosen(const wchar_t *dir, const wchar_t *name, char *name8, int cap8, unsigned long *n,
                                  unsigned *t, unsigned *d)
{
    wchar_t path[MAX_PATH * 2];
    WIN32_FILE_ATTRIBUTE_DATA a;
    FILETIME local;
    WORD dd = 0, dt = 0;
    FILE *f;
    long size;
    unsigned char *data = NULL;
    const wchar_t *base;
    if (name) _snwprintf(path, MAX_PATH * 2, L"%ls\\%ls", dir, name);
    else _snwprintf(path, MAX_PATH * 2, L"%ls", dir);
    path[MAX_PATH * 2 - 1] = 0;
    base = wcsrchr(path, L'\\') ? wcsrchr(path, L'\\') + 1 : path;
    WideCharToMultiByte(CP_UTF8, 0, base, -1, name8, cap8, NULL, NULL);
    if (GetFileAttributesExW(path, GetFileExInfoStandard, &a) && FileTimeToLocalFileTime(&a.ftLastWriteTime, &local))
        FileTimeToDosDateTime(&local, &dd, &dt);
    *t = dt;
    *d = dd;
    f = _wfopen(path, L"rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0
            && (data = (unsigned char *)malloc(size ? (size_t)size : 1)) != NULL
            && fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        data = NULL;
    }
    fclose(f);
    *n = (unsigned long)(data ? size : 0);
    return data;
}

static void add_programs(void)
{
    static char name8[ADD_MAX][800];
    const char *names[ADD_MAX];
    const unsigned char *data[ADD_MAX];
    unsigned long sizes[ADD_MAX];
    unsigned times[ADD_MAX], dates[ADD_MAX];
    char st[MAX_PATH], err[400] = "the unit's memory could not be read", summary[1600], msg[4000];
    const DWORD cap = 64 * 1024;
    wchar_t *buf, *p;
    bls_unit u;
    blf_fs *fs;
    blx_report r;
    int n = 0, i, ok = 0, got = 0, cancelled = 0;
    if (!g_unit) return;
    tns_release_all();
    buf = (wchar_t *)calloc(cap, sizeof(wchar_t));
    if (!buf || !add_dialog(buf, cap)) { free(buf); return; }
    /* one file: its full path; several: the folder, then each name, each ended by a NUL, the list by two */
    p = buf + wcslen(buf) + 1;
    if (!*p) {
        data[0] = read_chosen(buf, NULL, name8[0], sizeof name8[0], &sizes[0], &times[0], &dates[0]);
        n = 1;
    } else
        for (; *p && n < ADD_MAX; p += wcslen(p) + 1, n++)
            data[n] = read_chosen(buf, p, name8[n], sizeof name8[n], &sizes[n], &times[n], &dates[n]);
    free(buf);
    for (i = 0; i < n; i++) {
        names[i] = name8[i];
        if (!data[i]) {
            snprintf(msg, sizeof msg, "Could not read %s. Nothing was loaded.", name8[i]);
            MessageBoxA(g_wnd, msg, "Load into flash", MB_OK | MB_ICONERROR);
            goto done;
        }
    }
    if (!unit_off("Load into flash", "Load the files", st, sizeof st))
        goto done;
    blx_report_init(&r);
    if (open_saved(st, &u, &fs)) {
        got = blx_add_cp(fs, g_units[g_kind].codepage, n, names, data, sizes, times, dates, 0, &r, err, sizeof err);
        if (got == BLX_ADD_EXISTS) {        /* names the unit has: asked once for the whole batch */
            char ask[3000];
            const char *line = r.log ? r.log : "";
            size_t k = (size_t)snprintf(ask, sizeof ask, "The unit has files of these names already:\n");
            while (*line && k + 80 < sizeof ask) {
                const char *e = strchr(line, '\n');
                size_t len = e ? (size_t)(e - line) : strlen(line);
                if (len > 7 && !strncmp(line, "exists ", 7))
                    k += (size_t)snprintf(ask + k, sizeof ask - k, "\n%.*s", (int)(len - 7), line + 7);
                line = e ? e + 1 : line + len;
            }
            snprintf(ask + k, sizeof ask - k, "\n\nReplace them (each rewritten where it is)? No loads nothing.");
            if (MessageBoxA(g_wnd, ask, "Load into flash", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
                blx_report_free(&r);
                blx_report_init(&r);
                got = blx_add_cp(fs, g_units[g_kind].codepage, n, names, data, sizes, times, dates, 1, &r, err,
                                 sizeof err);
            } else
                cancelled = 1;
        }
        ok = got == 1 && blf_check(fs, err, sizeof err);
        if (ok) {                           /* the unit as it was, kept beside it: undone by one file copy */
            char before[MAX_PATH + 20];
            snprintf(before, sizeof before, "%s.before-import", st);
            CopyFileA(st, before, FALSE);
            ok = bls_save(st, &u, err, sizeof err);
        }
        blf_close(fs);
        bls_free(&u);
    }
    start_unit(g_kind);                     /* switched on again */
    if (cancelled)
        MessageBoxA(g_wnd, "Nothing was loaded. The unit's files are as they were.", "Load into flash",
                    MB_OK | MB_ICONINFORMATION);
    else if (!ok) {
        snprintf(summary, sizeof summary, "Nothing was loaded: %s\n\nThe unit was restarted with its files as they "
                 "were.", err);
        report_text(msg, sizeof msg, summary, &r);
        MessageBoxA(g_wnd, msg, "Load into flash", MB_OK | MB_ICONERROR);
    } else {
        int tns = g_units[g_kind].kind == EMU_TYPE_N_SPEAK;
        snprintf(summary, sizeof summary, "Loaded %d file%s into flash: %d added, %d replaced. The unit was "
                 "restarted. Run a program with %s, its name, then %s; open a document with %s, its name, then %s. "
                 "(The unit as it was before is kept in %s.before-import.)", n, n == 1 ? "" : "s", r.added,
                 r.replaced, tns ? "F9 x" : "o-chord x", tns ? "Enter" : "e-chord", tns ? "F1 o" : "o-chord f o",
                 tns ? "Enter" : "e-chord", st);
        report_text(msg, sizeof msg, summary, &r);
        MessageBoxA(g_wnd, msg, "Load into flash", MB_OK | MB_ICONINFORMATION);
    }
    blx_report_free(&r);
done:
    for (i = 0; i < n; i++)
        free((void *)data[i]);
}

/* ---- the sound card: the unit renders each block as the card hands it back ---------------------------------- */
static double now_ms(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER c;
    if (!freq.QuadPart)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)freq.QuadPart;
}

/* the card's played position in ms since it opened (unwrapped); -1 when the card does not tell */
static double played_ms(DWORD *last, unsigned long long *total)
{
    MMTIME mt;
    DWORD cur;
    mt.wType = TIME_SAMPLES;
    if (waveOutGetPosition(g_wave, &mt, sizeof mt) != MMSYSERR_NOERROR)
        return -1.0;
    if (mt.wType == TIME_SAMPLES)
        cur = mt.u.sample;
    else if (mt.wType == TIME_BYTES)
        cur = mt.u.cb / 2;
    else
        return -1.0;
    *total += (DWORD)(cur - *last);
    *last = cur;
    return (double)*total * 1000.0 / g_rate;
}

/* the minute's save, on the sound thread with the queue rendered ahead (audio_pace.h): the card plays on meanwhile,
   and no key waits for it */
static double autosave(void)
{
    char path[MAX_PATH];
    int ok = 1;
    double t0 = now_ms();
    EnterCriticalSection(&g_lock);
    if (g_unit) {                           /* (none: switched off, as while Back to the factory state asks) */
        CreateDirectoryA(g_save_dir, NULL);
        saved_path(g_kind, path, sizeof path);
        ok = emu_save_whole(g_unit, path);
    }
    LeaveCriticalSection(&g_lock);
    if (!ok)
        PostMessageA(g_wnd, WM_SAVE_FAILED, 0, 0);
    return now_ms() - t0;
}

static DWORD WINAPI audio_thread(LPVOID arg)
{
    int i, n = g_rate * g_block_ms / 1000, mode = (int)g_buffer_mode, stall = 0;
    audio_pace pace;
    DWORD pos_last = 0;
    unsigned long long pos_total = 0, blocks_out = 0;
    FILE *log = NULL;
    const char *env;
    (void)arg;
    ap_init(&pace, mode, g_block_ms);
    if ((env = getenv("BLAZIE_EMU_AUDIO_LOG")) != NULL && *env && (log = fopen(env, "a")) != NULL)
        fprintf(log, "open: %d Hz, blocks of %d ms, %d blocks, sound buffer %s (%d ms)\n", g_rate, g_block_ms, g_n_hdr,
                ap_mode_name(mode), ap_target(&pace) * g_block_ms);
    if ((env = getenv("BLAZIE_EMU_AUDIO_STALL")) != NULL)
        stall = atoi(env);
    while (!g_quit) {
        int in_flight = 0, want, wrote = 0, save;
        double t, played, gap = 0, worst = 0;
        WaitForSingleObject(g_wave_event, 100);
        if (g_quit)
            break;
        if ((int)g_buffer_mode != mode) {   /* Settings > Sound buffer changed */
            mode = (int)g_buffer_mode;
            ap_set_mode(&pace, mode);
        }
        for (i = 0; i < g_n_hdr; i++)
            in_flight += !(g_hdr[i].dwFlags & WHDR_DONE);
        t = now_ms();
        played = played_ms(&pos_last, &pos_total);
        if (played >= 0)
            gap = ap_observe(&pace, t, played);
        save = g_save_due != 0;
        want = ap_want(&pace, in_flight, save);
        for (i = 0; i < g_n_hdr && wrote < want && !g_quit; i++) {
            double r0;
            if (!(g_hdr[i].dwFlags & WHDR_DONE))
                continue;
            r0 = now_ms();
            EnterCriticalSection(&g_lock);
            if (g_unit)
                emu_render(g_unit, g_buf[i], n);
            else
                memset(g_buf[i], 0, sizeof g_buf[i]);
            com_kick(g_link);               /* the unit ran: its serial bytes may be waiting, both ways */
            LeaveCriticalSection(&g_lock);
            if (now_ms() - r0 > worst)
                worst = now_ms() - r0;
            if (stall > 0 && rand() % 20 == 0)
                Sleep((DWORD)(rand() % stall));
            g_hdr[i].dwFlags &= ~WHDR_DONE;
            waveOutWrite(g_wave, &g_hdr[i], sizeof(WAVEHDR));
            wrote++;
        }
        if (log) {
            if (gap > 0)
                fprintf(log, "%.1f GAP %.1f ms: sound buffer now %d ms\n", t, gap, ap_target(&pace) * g_block_ms);
            if (wrote)              /* ahead: written and not yet played, as the wake found it */
                fprintf(log, "%.1f queued %d, wrote %d, worst render %.3f ms, ahead %.1f ms\n", t, in_flight, wrote,
                        worst, played >= 0 ? (double)blocks_out * g_block_ms - played : -1.0);
            blocks_out += (unsigned long long)wrote;
        }
        if (ap_save_now(&pace, in_flight + wrote, save)) {
            double took = autosave();
            InterlockedExchange(&g_save_due, 0);
            if (log)
                fprintf(log, "%.1f saved in %.2f ms with %d blocks queued\n", now_ms(), took, in_flight + wrote);
        }
    }
    if (log) {
        fprintf(log, "close: %d gaps, %.0f ms lost; sound buffer %d ms\n", pace.gaps, pace.gap_ms,
                ap_target(&pace) * g_block_ms);
        fclose(log);
    }
    return 0;
}

static int open_audio(void)
{
    WAVEFORMATEX f;
    int i;
    memset(&f, 0, sizeof f);
    f.wFormatTag = WAVE_FORMAT_PCM;
    f.nChannels = 1;
    f.nSamplesPerSec = g_rate;
    f.wBitsPerSample = 16;
    f.nBlockAlign = 2;
    f.nAvgBytesPerSec = g_rate * 2;
    g_wave_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (waveOutOpen(&g_wave, WAVE_MAPPER, &f, (DWORD_PTR)g_wave_event, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR)
        return 0;
    g_n_hdr = ap_capacity(g_block_ms);     /* the longest queue and a save's render-ahead (audio_pace.h) */
    for (i = 0; i < g_n_hdr; i++) {
        memset(&g_hdr[i], 0, sizeof g_hdr[i]);
        g_hdr[i].lpData = (LPSTR)g_buf[i];
        g_hdr[i].dwBufferLength = (DWORD)(sizeof(short) * (g_rate * g_block_ms / 1000));
        waveOutPrepareHeader(g_wave, &g_hdr[i], sizeof(WAVEHDR));
        g_hdr[i].dwFlags |= WHDR_DONE;     /* all free: the thread fills as many as the queue keeps */
    }
    g_thread = CreateThread(NULL, 0, audio_thread, NULL, 0, NULL);
    SetThreadPriority(g_thread, THREAD_PRIORITY_TIME_CRITICAL);
    SetEvent(g_wave_event);
    g_audio_ok = g_thread != NULL;
    return 1;
}

static void close_audio(void)
{
    int i;
    InterlockedExchange(&g_quit, 1);
    SetEvent(g_wave_event);
    WaitForSingleObject(g_thread, 2000);
    g_audio_ok = 0;
    waveOutReset(g_wave);
    for (i = 0; i < g_n_hdr; i++)
        waveOutUnprepareHeader(g_wave, &g_hdr[i], sizeof(WAVEHDR));
    waveOutClose(g_wave);
}

static void check_rate(void)
{
    int k;
    for (k = 0; k < N_RATES; k++)
        if (RATES[k] == g_rate)
            CheckMenuRadioItem(GetMenu(g_wnd), ID_RATE, ID_RATE + N_RATES - 1, ID_RATE + k, MF_BYCOMMAND);
}

/* a new sample rate: the sound card reopened at it, and the unit restarted to render at it (its memory kept) */
static void set_rate(int r)
{
    char v[16];
    if (r == g_rate)
        return;
    close_audio();
    CloseHandle(g_thread);
    CloseHandle(g_wave_event);
    g_rate = r;
    InterlockedExchange(&g_quit, 0);
    start_unit(g_kind);
    if (!open_audio())
        MessageBoxA(g_wnd, "Could not open the sound card at that rate.", "Blazie emulator", MB_OK | MB_ICONERROR);
    check_rate();
    snprintf(v, sizeof v, "%d", r);
    WritePrivateProfileStringA("sound", "rate", v, g_ini);
}

/* ---- the window ------------------------------------------------------------------------------------------------ */
static const char *const IDLE_NAMES[] = {"off", "hiss", "whine", "unit"};
static const int IDLE_IDS[] = {ID_QUIET, ID_HISS, ID_WHINE, ID_UNITSOUND};
static const char *const OPEN_NAMES[] = {"off", "until", "always"};

/* the idle channel's settings to the unit (bl_idle.h: the sound, when it is heard, the pop and click, the tick), the
   menu's marks and the settings file */
static void apply_idle(void)
{
    HMENU m = GetMenu(g_wnd);
    EnterCriticalSection(&g_lock);
    if (g_unit)
        emu_set_idle(g_unit, g_whine, g_keep_open, g_popclick, g_tick);
    LeaveCriticalSection(&g_lock);
    CheckMenuRadioItem(m, ID_HISS, ID_UNITSOUND, IDLE_IDS[g_whine], MF_BYCOMMAND);
    CheckMenuRadioItem(m, ID_OPEN_OFF, ID_OPEN_ALWAYS, ID_OPEN_OFF + g_keep_open, MF_BYCOMMAND);
    CheckMenuItem(m, ID_POPCLICK, MF_BYCOMMAND | (g_popclick ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, ID_TICK, MF_BYCOMMAND | (g_tick ? MF_CHECKED : MF_UNCHECKED));
    WritePrivateProfileStringA("sound", "idle", IDLE_NAMES[g_whine], g_ini);
    WritePrivateProfileStringA("sound", "keep_open", OPEN_NAMES[g_keep_open], g_ini);
    WritePrivateProfileStringA("sound", "pop_click", g_popclick ? "1" : "0", g_ini);
    WritePrivateProfileStringA("sound", "tick", g_tick ? "1" : "0", g_ini);
}

static void load_idle(void)
{
    char v[32];
    int k;
    GetPrivateProfileStringA("sound", "idle", "unit", v, sizeof v, g_ini);
    for (k = 0; k < 4; k++)
        if (!strcmp(v, IDLE_NAMES[k]))
            g_whine = k;
    GetPrivateProfileStringA("sound", "keep_open", "until", v, sizeof v, g_ini);
    for (k = 0; k < 3; k++)
        if (!strcmp(v, OPEN_NAMES[k]))
            g_keep_open = k;
    g_popclick = GetPrivateProfileIntA("sound", "pop_click", 1, g_ini) != 0;
    g_tick = GetPrivateProfileIntA("sound", "tick", 1, g_ini) != 0;
}

/* ---- the serial port ------------------------------------------------------------------------------------------ */
/* plugs the unit's serial port into COM port `name` ("" = none, unplugged); 1 on success.  at_start: the port saved
   last time -- if it is gone, the unit starts unplugged and the setting stays for next time */
static int set_serial(const char *name, int at_start)
{
    char err[300], msg[600];
    com_link *old = g_link, *link = NULL;
    EnterCriticalSection(&g_lock);
    g_link = NULL;                          /* the sound thread stops kicking it */
    LeaveCriticalSection(&g_lock);
    com_close(old);                         /* (its thread takes the lock: never hold it here) */
    EnterCriticalSection(&g_lock);
    if (g_unit)
        emu_serial_attach(g_unit, 0);
    LeaveCriticalSection(&g_lock);
    g_com[0] = 0;
    if (name[0]) {
        EnterCriticalSection(&g_lock);
        if (g_unit)
            emu_serial_attach(g_unit, 1);   /* first, so the port's first status is the unit's */
        LeaveCriticalSection(&g_lock);
        link = com_open(name, &g_lock, &g_unit, err, sizeof err);
        if (!link) {
            EnterCriticalSection(&g_lock);
            if (g_unit)
                emu_serial_attach(g_unit, 0);
            LeaveCriticalSection(&g_lock);
            snprintf(msg, sizeof msg, "Could not open %s. %s\n\nThe unit's serial port is not connected.%s", name,
                     err, at_start ? " Settings, Serial port chooses another port." : "");
            MessageBoxA(g_wnd, msg, "Blazie emulator", MB_OK | MB_ICONWARNING);
        } else {
            snprintf(g_com, sizeof g_com, "%s", name);
            EnterCriticalSection(&g_lock);
            g_link = link;
            LeaveCriticalSection(&g_lock);
            com_kick(link);
        }
    }
    if (!at_start || link)
        WritePrivateProfileStringA("serial", "port", link ? name : "none", g_ini);
    if (g_unit)
        status(g_units[g_kind].name);
    return link != NULL;
}

/* the Serial port menu, listed afresh each time it opens (a com0com pair made meanwhile shows up) */
static void fill_serial_menu(void)
{
    int k, current = -1;
    while (GetMenuItemCount(g_serial_menu) > 0)
        DeleteMenu(g_serial_menu, 0, MF_BYPOSITION);
    g_n_ports = com_list(g_ports, COM_MAX);
    AppendMenuA(g_serial_menu, MF_STRING, ID_SERIAL_NONE, "&None (not connected)");
    for (k = 0; k < g_n_ports; k++) {
        AppendMenuA(g_serial_menu, MF_STRING, ID_SERIAL_PORT + k, g_ports[k].label);
        if (g_link && !_stricmp(g_ports[k].name, g_com))
            current = k;
    }
    if (!g_n_ports)
        AppendMenuA(g_serial_menu, MF_STRING | MF_GRAYED, ID_SERIAL_PORT + COM_MAX, "No COM ports found");
    CheckMenuRadioItem(g_serial_menu, ID_SERIAL_NONE, ID_SERIAL_PORT + COM_MAX, current < 0 ? ID_SERIAL_NONE
                       : ID_SERIAL_PORT + current, MF_BYCOMMAND);
}

/* a unit the menus always had is offered: its firmware is in the folder, or it is one of the four always listed */
static int listed(const char *key)
{
    int k = unit_by_key(key);
    return k >= 0 && (g_units[k].present || g_units[k].always);
}

static HMENU make_menu(void)
{
    HMENU bar = CreateMenu(), unit = CreatePopupMenu(), sound = CreatePopupMenu(), help = CreatePopupMenu();
    HMENU rates = CreatePopupMenu(), buffer = CreatePopupMenu(), more = NULL;
    int k;
    AppendMenuA(unit, MF_STRING, ID_EN, "Braille Lite 2000, &English");
    AppendMenuA(unit, MF_STRING, ID_ES, "Braille Lite 2000, &Spanish");
    if (listed("bl-de"))
        AppendMenuA(unit, MF_STRING, ID_DE, "Braille Lite 2000, &German");
    if (listed("bl-it"))
        AppendMenuA(unit, MF_STRING, ID_IT, "Braille Lite 2000, It&alian");
    AppendMenuA(unit, MF_STRING, ID_TNS_EN, "&Type 'n Speak, English");
    AppendMenuA(unit, MF_STRING, ID_TNS_ES, "Type 'n Speak, S&panish");
    if (listed("bns-en"))
        AppendMenuA(unit, MF_STRING, ID_BNS_EN, "Braille 'n Speak 2000, E&nglish");
    if (listed("bns-sk"))
        AppendMenuA(unit, MF_STRING, ID_BNS_SK, "Braille 'n Speak 2000, Sl&ovak");
    /* the release units found in the firmware folder (emu_catalog.h), by name */
    for (k = 0; k < g_n_units; k++)
        if (g_units[k].release >= 0) {
            if (!more)
                more = CreatePopupMenu();
            AppendMenuA(more, MF_STRING, ID_RELEASE + k, g_units[k].name);
        }
    if (more)
        AppendMenuA(unit, MF_POPUP, (UINT_PTR)more, "&More languages");
    AppendMenuA(unit, MF_SEPARATOR, 0, NULL);
    AppendMenuA(unit, MF_STRING, ID_EXPORT, "Exp&ort files to disk image (.img)...");
    AppendMenuA(unit, MF_STRING, ID_IMPORT, "&Import files from disk image (.img)...");
    AppendMenuA(unit, MF_STRING, ID_ADDPROG, "&Load programs and files into flash...");
    AppendMenuA(unit, MF_SEPARATOR, 0, NULL);
    AppendMenuA(unit, MF_STRING, ID_RESTART, "&Restart the unit (keeps its files)");
    AppendMenuA(unit, MF_STRING, ID_ONEHAND_ON, "Restart in o&ne-handed mode (dot 6 held as it starts)");
    AppendMenuA(unit, MF_STRING, ID_ONEHAND_OFF, "Restart in t&wo-handed mode (dot 3 held as it starts)");
    AppendMenuA(unit, MF_STRING, ID_FACTORY, "Back to the &factory state (erases this unit's files)...");
    AppendMenuA(unit, MF_STRING, ID_EXIT, "E&xit");
    AppendMenuA(sound, MF_STRING, ID_UNITSOUND, "Idle channel: as the &unit (hiss at even volumes, whine at odd)");
    AppendMenuA(sound, MF_STRING, ID_HISS, "Idle channel: &hiss");
    AppendMenuA(sound, MF_STRING, ID_WHINE, "Idle channel: &whine");
    AppendMenuA(sound, MF_STRING, ID_QUIET, "Idle channel: &silent");
    AppendMenuA(sound, MF_SEPARATOR, 0, NULL);
    AppendMenuA(sound, MF_STRING, ID_OPEN_OFF, "Keep the channel open: &off (silent as soon as speech ends)");
    AppendMenuA(sound, MF_STRING, ID_OPEN_UNTIL, "Keep the channel open: until the unit &clicks off");
    AppendMenuA(sound, MF_STRING, ID_OPEN_ALWAYS, "Keep the channel open: &always");
    AppendMenuA(sound, MF_SEPARATOR, 0, NULL);
    AppendMenuA(sound, MF_STRING, ID_POPCLICK, "The &pop when the channel opens, the click when it clicks off");
    AppendMenuA(sound, MF_STRING, ID_TICK, "The 10 Hz &tick of the open channel");
    AppendMenuA(sound, MF_SEPARATOR, 0, NULL);
    AppendMenuA(sound, MF_STRING, ID_QUICK, "&Quick key response (faster than the real unit)");
    AppendMenuA(sound, MF_STRING, ID_DOUBLE, "Double-speed up&grade installed (Braille Lite: its status menu's switch "
                "then works)");
    AppendMenuA(sound, MF_STRING, ID_BRAILLE, "Braille &display in the window\tAlt+Shift+B");
    AppendMenuA(help, MF_STRING, ID_KEYS, "&Keys");
    AppendMenuA(help, MF_STRING, ID_ABOUT, "&About");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)unit, "&Firmware");
    for (k = 0; k < N_RATES; k++) {
        char label[32];
        snprintf(label, sizeof label, "%d Hz", RATES[k]);
        AppendMenuA(rates, MF_STRING, ID_RATE + k, label);
    }
    AppendMenuA(sound, MF_SEPARATOR, 0, NULL);
    AppendMenuA(sound, MF_POPUP, (UINT_PTR)rates, "Sample &rate");
    /* the queue at the sound card (audio_pace.h): a key's answer is heard that much later; too short, it chops */
    {   /* the labels from audio_pace.h's own numbers; no short (AP_OFFERED: it broke up on a ROG Ally at 40 and 50) */
        char lab[96];
        snprintf(lab, sizeof lab, "&Automatic (%d ms, longer when the sound breaks up)", AP_AUTO_START_MS);
        AppendMenuA(buffer, MF_STRING, ID_BUFFER + AP_AUTO, lab);
        snprintf(lab, sizeof lab, "&Medium (%d ms)", AP_MEDIUM_MS);
        AppendMenuA(buffer, MF_STRING, ID_BUFFER + AP_MEDIUM, lab);
        snprintf(lab, sizeof lab, "&Long (%d ms, for Remote Desktop)", AP_LONG_MS);
        AppendMenuA(buffer, MF_STRING, ID_BUFFER + AP_LONG, lab);
    }
    AppendMenuA(sound, MF_POPUP, (UINT_PTR)buffer, "Sound &buffer");
    g_serial_menu = CreatePopupMenu();
    AppendMenuA(g_serial_menu, MF_STRING, ID_SERIAL_NONE, "&None (not connected)");   /* filled when it opens */
    AppendMenuA(sound, MF_POPUP, (UINT_PTR)g_serial_menu, "Serial &port");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)sound, "S&ettings");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)help, "&Help");
    return bar;
}

/* the Type 'n Speak's keys held now (their down codes), so a key held when the window loses the keyboard is let go */
static unsigned char g_tns_held[256];

static int tns_active(void)
{
    return g_unit && emu_kind(g_unit) == EMU_TYPE_N_SPEAK;
}

static void tns_release_all(void)
{
    int vk;
    EnterCriticalSection(&g_lock);
    for (vk = 0; vk < 256; vk++)
        if (g_tns_held[vk]) {
            if (tns_active())
                emu_key(g_unit, g_tns_held[vk] & 0x7F);
            g_tns_held[vk] = 0;
        }
    LeaveCriticalSection(&g_lock);
}

/* the unit's cells into the field, as the firmware last latched them */
static void show_braille(void)
{
    unsigned char cells[EMU_CELLS];
    int n = 0;
    EnterCriticalSection(&g_lock);
    if (g_unit)
        n = emu_braille(g_unit, cells, EMU_CELLS);
    LeaveCriticalSection(&g_lock);
    brlw_show(g_brl, cells, n);
}

static void set_braille(int on)
{
    g_braille = on;
    brlw_visible(g_brl, on);
    SetFocus(on ? g_brl : g_wnd);
    CheckMenuItem(GetMenu(g_wnd), ID_BRAILLE, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
    WritePrivateProfileStringA("braille", "display", on ? "1" : "0", g_ini);
}

static void bars(int bits)
{
    EnterCriticalSection(&g_lock);
    g_bars = bits;
    if (g_unit)
        emu_braille_bars(g_unit, bits);
    LeaveCriticalSection(&g_lock);
}

static LRESULT CALLBACK wndproc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    /* Alt+Shift+F always opens this program's menu, whatever unit has the keyboard (the Type 'n Speak takes Alt for
       itself); the unit is told its held keys came up */
    if ((msg == WM_SYSKEYDOWN || msg == WM_KEYDOWN) && wp == 'F' && (GetKeyState(VK_MENU) & 0x8000)
            && (GetKeyState(VK_SHIFT) & 0x8000)) {
        tns_release_all();
        PostMessageA(w, WM_SYSCOMMAND, SC_KEYMENU, 0);
        return 0;
    }
    /* Alt+Shift+Right and Left: the advance and back bars, held as long as the arrow is -- for a braille display's
       user, whose display's keys stay the screen reader's (the Braille Lite's ; and A too).  Not the Type 'n Speak's:
       it has no bars, and its keyboard is its own */
    /* Alt+Shift+B: the braille display field on or off (Settings > Braille display); the Type 'n Speak's
       keyboard keeps it.  Its character is swallowed: no beep from the menu bar, which has no B */
    if (!tns_active() && wp == 'B' && (GetKeyState(VK_MENU) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000)) {
        if (msg == WM_SYSKEYDOWN || msg == WM_KEYDOWN) {
            if (!(lp & 0x40000000))         /* not auto-repeat */
                PostMessageA(w, WM_COMMAND, ID_BRAILLE, 0);
            return 0;
        }
        if (msg == WM_SYSCHAR || msg == WM_CHAR)
            return 0;
    }
    if (!tns_active() && (msg == WM_SYSCHAR || msg == WM_CHAR) && (wp == 'b' || wp == 'B')
            && (GetKeyState(VK_MENU) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000))
        return 0;
    if (!tns_active() && (wp == VK_RIGHT || wp == VK_LEFT)
            && (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYUP)) {
        int bit = wp == VK_RIGHT ? 1 : 2;
        if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && (GetKeyState(VK_MENU) & 0x8000)
                && (GetKeyState(VK_SHIFT) & 0x8000)) {
            if (!(g_bars & bit))            /* the first down only */
                bars(g_bars | bit);
            return 0;
        }
        if ((msg == WM_KEYUP || msg == WM_SYSKEYUP) && (g_bars & bit)) {
            bars(g_bars & ~bit);
            return 0;
        }
    }
    if (tns_active() && (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYUP)) {
        int down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN, code;
        if (wp == VK_F11) {                 /* this program's own key: its menu */
            if (down)
                PostMessageA(w, WM_SYSCOMMAND, SC_KEYMENU, 0);
            return 0;
        }
        code = wp < 256 ? tns_code_for_key(wp, lp) : 0;
        if (code) {
            EnterCriticalSection(&g_lock);
            if (down && !g_tns_held[wp]) {  /* the first down only: auto-repeat is the unit's business */
                g_tns_held[wp] = (unsigned char)code;
                emu_key(g_unit, code);
            } else if (!down && g_tns_held[wp]) {
                emu_key(g_unit, g_tns_held[wp] & 0x7F);
                g_tns_held[wp] = 0;
            }
            LeaveCriticalSection(&g_lock);
        }
        return 0;                           /* nothing of the unit's keyboard reaches Windows (no menu on Alt) */
    }
    if (tns_active() && (msg == WM_CHAR || msg == WM_SYSCHAR))
        return 0;
    switch (msg) {
    case WM_KEYDOWN:
        if (wp < 256 && g_keymap[wp]) {
            EnterCriticalSection(&g_lock);
            chord_down(&g_chord, g_keymap[wp]);
            if (g_unit)
                emu_keys_down(g_unit, g_chord.down);   /* held: the unit sees them as it starts (i-chord: cold reset) */
            LeaveCriticalSection(&g_lock);
            return 0;
        }
        break;
    case WM_KEYUP:
        if (wp < 256 && g_keymap[wp]) {
            int chord;
            EnterCriticalSection(&g_lock);
            chord = chord_up(&g_chord, g_keymap[wp]);
            if (g_unit)
                emu_keys_down(g_unit, g_chord.down);
            if (chord && g_unit)
                emu_key(g_unit, chord);
            LeaveCriticalSection(&g_lock);
            return 0;
        }
        break;
    case WM_CHAR:
        return 0;                           /* no beeps for the braille keys */
    case WM_KILLFOCUS:
        EnterCriticalSection(&g_lock);
        chord_reset(&g_chord);              /* a chord half-pressed when the window lost the keyboard is dropped */
        if (g_unit)
            emu_keys_down(g_unit, 0);
        LeaveCriticalSection(&g_lock);
        tns_release_all();                  /* and the Type 'n Speak's held keys come up */
        if (g_bars)
            bars(0);                        /* and the bars */
        break;
    case WM_SETFOCUS:                       /* the field keeps the keyboard while it is shown */
        if (g_braille)
            SetFocus(g_brl);
        return 0;
    case WM_SIZE:
        brlw_layout(g_brl, LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_COMMAND:
        if (unit_of_id(LOWORD(wp)) >= 0) {  /* a unit chosen in the Firmware menu */
            tns_release_all();
            start_unit(unit_of_id(LOWORD(wp)));
            return 0;
        }
        switch (LOWORD(wp)) {
        case ID_EXIT: DestroyWindow(w); return 0;
        case ID_EXPORT: export_files(); return 0;
        case ID_IMPORT: import_files(); return 0;
        case ID_ADDPROG: add_programs(); return 0;
        case ID_FACTORY: {
            char path[MAX_PATH];
            emu_unit *old;
            if (MessageBoxA(w, "Put this unit back as it left the factory? Its files and settings are erased, and a "
                            "firmware update it was given.",
                            "Blazie emulator", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
                return 0;
            EnterCriticalSection(&g_lock);
            old = g_unit;
            g_unit = NULL;                  /* so start_unit saves nothing over the deletion */
            LeaveCriticalSection(&g_lock);
            emu_destroy(old);
            saved_path(g_kind, path, sizeof path);
            DeleteFileA(path);
            if (strlen(path) + 9 <= sizeof path) {   /* and the program flash an update wrote (emu_save) */
                strcat(path, ".program");
                DeleteFileA(path);
            }
            start_unit(g_kind);
            return 0;
        }
        case ID_HISS: g_whine = 1; apply_idle(); return 0;
        case ID_WHINE: g_whine = 2; apply_idle(); return 0;
        case ID_QUIET: g_whine = 0; apply_idle(); return 0;
        case ID_UNITSOUND: g_whine = 3; apply_idle(); return 0;
        case ID_OPEN_OFF: case ID_OPEN_UNTIL: case ID_OPEN_ALWAYS:
            g_keep_open = LOWORD(wp) - ID_OPEN_OFF;
            apply_idle();
            return 0;
        case ID_POPCLICK: g_popclick = !g_popclick; apply_idle(); return 0;
        case ID_TICK: g_tick = !g_tick; apply_idle(); return 0;
        case ID_SERIAL_NONE: set_serial("", 0); return 0;
        case ID_QUICK:
            g_quick = !g_quick;
            EnterCriticalSection(&g_lock);
            if (g_unit)
                emu_set_quick(g_unit, g_quick);
            LeaveCriticalSection(&g_lock);
            CheckMenuItem(GetMenu(w), ID_QUICK, MF_BYCOMMAND | (g_quick ? MF_CHECKED : MF_UNCHECKED));
            WritePrivateProfileStringA("unit", "quick_keys", g_quick ? "1" : "0", g_ini);
            return 0;
        case ID_DOUBLE:
            g_double = !g_double;
            EnterCriticalSection(&g_lock);
            if (g_unit)
                emu_set_double_speed(g_unit, g_double);
            LeaveCriticalSection(&g_lock);
            CheckMenuItem(GetMenu(w), ID_DOUBLE, MF_BYCOMMAND | (g_double ? MF_CHECKED : MF_UNCHECKED));
            WritePrivateProfileStringA("unit", "double_speed", g_double ? "1" : "0", g_ini);
            if (g_double)
                MessageBoxA(w, "The double-speed upgrade is installed. To use it, switch it on in the unit: the "
                            "status menu, then + (dots 3-4-6), answered y.\n\n"
                            "As on a real unit, programs that play sounds can misbehave with it on: their tones can "
                            "come out as a low rumble, and a game can stop the unit. Blazie's advice was to switch "
                            "double speed off for them. Firmware > Restart the unit starts a stopped unit again.",
                            "Double speed", MB_OK | MB_ICONINFORMATION);
            return 0;
        case ID_RESTART:
            tns_release_all();
            start_unit(g_kind);             /* saved first, then started from it: a power cycle, files kept */
            return 0;
        case ID_ONEHAND_ON:
        case ID_ONEHAND_OFF:                /* the same power cycle, dot 6 (on) or dot 3 (off) held as it starts */
            if (g_units[g_kind].kind == EMU_TYPE_N_SPEAK)
                return 0;
            g_hold_at_start = LOWORD(wp) == ID_ONEHAND_ON ? EMU_ONE_HAND_ON : EMU_ONE_HAND_OFF;
            start_unit(g_kind);
            return 0;
        case ID_BRAILLE:
            if (!g_braille)
                MessageBoxA(w, "The unit's braille display is shown in a field in this window, which your screen "
                            "reader shows on your braille display. The field keeps the keyboard: the unit's keys "
                            "work as before.\n\n"
                            "Your braille display's own keys stay your screen reader's, so they cannot press the "
                            "unit's advance bars, and the display does not advance by itself at the end of the "
                            "field. Advance from your PC keyboard instead:\n\n"
                            "Period = the advance bar\nComma = the back bar\n"
                            "(and ; and A, the Braille Lite's own; Alt+Shift+Right and Left Arrow too, where your "
                            "screen reader does not take them for its table navigation, as JAWS does)\n\n"
                            "Please turn off any braille translation done by your screen reader: it can interfere "
                            "with the unit's braille cells.", "Braille display", MB_OK | MB_ICONINFORMATION);
            set_braille(!g_braille);
            return 0;
        default:
            if (LOWORD(wp) >= ID_BUFFER && LOWORD(wp) < ID_BUFFER + AP_N_MODES) {
                InterlockedExchange(&g_buffer_mode, LOWORD(wp) - ID_BUFFER);   /* the sound thread takes it */
                CheckMenuRadioItem(GetMenu(w), ID_BUFFER, ID_BUFFER + AP_N_MODES - 1, LOWORD(wp), MF_BYCOMMAND);
                WritePrivateProfileStringA("sound", "buffer", ap_mode_name(LOWORD(wp) - ID_BUFFER), g_ini);
                return 0;
            }
            if (LOWORD(wp) >= ID_RATE && LOWORD(wp) < ID_RATE + N_RATES) {
                set_rate(RATES[LOWORD(wp) - ID_RATE]);
                return 0;
            }
            if (LOWORD(wp) >= ID_SERIAL_PORT && LOWORD(wp) < ID_SERIAL_PORT + g_n_ports) {
                char name[16];
                snprintf(name, sizeof name, "%s", g_ports[LOWORD(wp) - ID_SERIAL_PORT].name);
                set_serial(name, 0);
                return 0;
            }
            break;
        case ID_KEYS:
            MessageBoxA(w, "Braille Lite, while this window is in front:\n\n"
                        "F D S = dots 1 2 3\nJ K L = dots 4 5 6\nSpace bar = space\n"
                        "Period or ; = advance bar, comma or A = back bar\n"
                        "Alt+Shift+Right, Left Arrow = the advance bar, the back bar (with NVDA)\n"
                        "These can be changed in blazie_emu.ini, section [keys] (advance, back).\n"
                        "Alt+Shift+B = the braille display in the window, on or off\n"
                        "The Braille 'n Speak 2000 takes the same keys; it has no advance bar.\n\n"
                        "Press the keys of a chord together; it goes to the unit when you let go of them.\n"
                        "Keys held while the unit starts are read as it starts: p-chord, l restarts it; "
                        "hold i-chord at once for the cold reset.\n"
                        "One-handed mode: Firmware > Restart in one-handed mode (dot 6 held as it starts); "
                        "Restart in two-handed mode (dot 3) leaves it. The unit keeps it until then.\n"
                        "If the unit stops answering, Firmware > Restart the unit starts it again, its files kept.\n"
                        "The keys can be changed in blazie_emu.ini, section [keys].\n"
                        "Alt opens this program's menu; Alt+F4 closes it.\n\n"
                        "Type 'n Speak: the whole keyboard is the unit's, Alt and the function keys included.\n"
                        "Alt+Shift+F (or F11) opens this program's menu (Firmware > Exit closes it).\n\n"
                        TNS_FIRST_START, "Keys", MB_OK);
            return 0;
        case ID_ABOUT:
            MessageBoxA(w, "Blazie emulator, part of ssi263-speech.\n\n"
                        "It runs Blazie Engineering's own firmware on an emulated board with an emulated SSI-263 "
                        "speech chip. The firmware is shared with permission. This program is not a product of "
                        "Blazie Engineering.\n\n"
                        "Licence: MIT (see LICENSE beside the program), with Casso's MIT notice, which the chip "
                        "model draws on. The Z180 core is MAME's and keeps its BSD-3-Clause licence. Both "
                        "notices are in the licenses folder.", "About", MB_OK);
            return 0;
        }
        break;
    case WM_INITMENUPOPUP:
        if ((HMENU)wp == g_serial_menu)
            fill_serial_menu();
        break;
    case WM_TIMER:
        if (wp == BRAILLE_TIMER) {
            if (g_braille)
                show_braille();
            return 0;
        }
        /* the unit's memory saved every minute: nothing is lost if the */
        if (g_audio_ok) {                   /* program is ended without closing its window -- by the sound thread, */
            InterlockedExchange(&g_save_due, 1);   /* which renders ahead first (audio_pace.h) */
            SetEvent(g_wave_event);
        } else
            save_unit();
        return 0;
    case WM_SAVE_FAILED:
        MessageBoxA(g_wnd, "Could not save the unit's memory; what you wrote this time is lost.", "Blazie emulator",
                    MB_OK | MB_ICONWARNING);
        return 0;
    case WM_QUERYENDSESSION:
        save_unit();                        /* Windows logging off or shutting down */
        return TRUE;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

/* firmware_dir= in the settings; else firmware\ beside the program; else the source tree's firmware\blazie\ when
   the program runs from nvda\dist\blazie-emu\ */
static void find_firmware(void)
{
    char probe[MAX_PATH];
    GetPrivateProfileStringA("unit", "firmware_dir", "", g_fw_dir, sizeof g_fw_dir, g_ini);
    if (g_fw_dir[0])
        return;
    snprintf(g_fw_dir, sizeof g_fw_dir, "%s\\firmware", g_dir);
    snprintf(probe, sizeof probe, "%s\\BL2ENG.BNS", g_fw_dir);
    if (exists(probe))
        return;
    snprintf(probe, sizeof probe, "%s\\..\\..\\..\\firmware\\blazie\\BL2ENG.BNS", g_dir);
    if (exists(probe))
        snprintf(g_fw_dir, sizeof g_fw_dir, "%s\\..\\..\\..\\firmware\\blazie", g_dir);
}

/* Windows 11 may slow a process down to save power (EcoQoS: on battery, its window not in front -- a handheld PC);
   the unit must keep up with the sound card, so this one asks not to be.  (Windows 8 and later; a precaution, not
   seen on this desktop.) */
static void no_power_throttling(void)
{
    typedef struct { ULONG Version, ControlMask, StateMask; } throttling_state;   /* PROCESS_POWER_THROTTLING_STATE */
    typedef BOOL (WINAPI *set_info_fn)(HANDLE, int, LPVOID, DWORD);
    throttling_state s = {1, 1, 0};        /* ..._CURRENT_VERSION; EXECUTION_SPEED controlled, and off */
    set_info_fn set_info = (set_info_fn)(void (*)(void))GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                                                     "SetProcessInformation");
    if (set_info)
        set_info(GetCurrentProcess(), 4 /* ProcessPowerThrottling */, &s, sizeof s);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASSA wc;
    MSG m;
    char *slash, v[32];
    (void)prev; (void)cmd;
    GetModuleFileNameA(NULL, g_dir, sizeof g_dir);
    if ((slash = strrchr(g_dir, '\\')) != NULL)
        *slash = 0;
    snprintf(g_ini, sizeof g_ini, "%s\\blazie_emu.ini", g_dir);
    {
        char appdata[MAX_PATH];
        if (!GetEnvironmentVariableA("APPDATA", appdata, sizeof appdata))
            snprintf(appdata, sizeof appdata, "%s", g_dir);
        snprintf(g_save_dir, sizeof g_save_dir, "%s\\ssi263-speech", appdata);
        CreateDirectoryA(g_save_dir, NULL);
        snprintf(g_save_dir, sizeof g_save_dir, "%s\\ssi263-speech\\blazie-emu", appdata);
    }
    find_firmware();
    g_n_units = emc_list(g_fw_dir, g_units, EMC_MAX);
    load_keymap();
    no_power_throttling();
    InitializeCriticalSection(&g_lock);
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "BlazieEmulator";
    RegisterClassA(&wc);
    g_wnd = CreateWindowA("BlazieEmulator", "Blazie emulator", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                          480, 240, NULL, make_menu(), inst, NULL);
    g_brl = brlw_create(g_wnd, inst, wndproc);
    {
        RECT r;
        GetClientRect(g_wnd, &r);
        brlw_layout(g_brl, r.right, r.bottom);
    }
    ShowWindow(g_wnd, show);
    SetTimer(g_wnd, 1, 60000, NULL);
    SetTimer(g_wnd, BRAILLE_TIMER, BRAILLE_MS, NULL);
    set_braille(GetPrivateProfileIntA("braille", "display", 0, g_ini) != 0);
    load_idle();
    apply_idle();
    g_block_ms = GetPrivateProfileIntA("sound", "block_ms", BLOCK_MS_DEFAULT, g_ini);
    if (g_block_ms < 5 || g_block_ms > 20)
        g_block_ms = BLOCK_MS_DEFAULT;
    {
        int b;
        GetPrivateProfileStringA("sound", "buffer", "auto", v, sizeof v, g_ini);
        if ((b = ap_mode_of(v)) >= 0)
            g_buffer_mode = b;
        CheckMenuRadioItem(GetMenu(g_wnd), ID_BUFFER, ID_BUFFER + AP_N_MODES - 1, ID_BUFFER + (int)g_buffer_mode,
                           MF_BYCOMMAND);
    }
    g_quick = GetPrivateProfileIntA("unit", "quick_keys", 0, g_ini) != 0;
    CheckMenuItem(GetMenu(g_wnd), ID_QUICK, MF_BYCOMMAND | (g_quick ? MF_CHECKED : MF_UNCHECKED));
    g_double = GetPrivateProfileIntA("unit", "double_speed", 0, g_ini) != 0;
    CheckMenuItem(GetMenu(g_wnd), ID_DOUBLE, MF_BYCOMMAND | (g_double ? MF_CHECKED : MF_UNCHECKED));
    {
        int k, r = GetPrivateProfileIntA("sound", "rate", 44100, g_ini);
        for (k = 0; k < N_RATES; k++)
            if (RATES[k] == r)
                g_rate = r;
        check_rate();
    }
    if (!open_audio())
        MessageBoxA(g_wnd, "Could not open the sound card.", "Blazie emulator", MB_OK | MB_ICONERROR);
    GetPrivateProfileStringA("unit", "kind", "english", v, sizeof v, g_ini);
    {
        start_unit(emc_find(g_units, g_n_units, v));
    }
    GetPrivateProfileStringA("serial", "port", "none", v, sizeof v, g_ini);
    if (_stricmp(v, "none") && v[0])
        set_serial(v, 1);                   /* the COM port chosen last time */
    while (GetMessageA(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    set_serial("", 1);                      /* unplugged: the port closed (the setting kept) */
    close_audio();
    save_unit();                            /* switched off: the memory is kept */
    emu_destroy(g_unit);
    return 0;
}
