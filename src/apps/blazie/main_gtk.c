/* main_gtk.c -- the GTK shell of the Blazie emulator, for a Linux desktop and its screen reader (Orca): the same unit
 * as the Windows app (emu_unit.c: the Braille Lite 2000, the Braille 'n Speak 2000 or the Type 'n Speak running its
 * own firmware) in one window with a menu bar, as main_win.c is on Windows (Tomi: the emulator in GTK, for Orca).
 * The Braille 'n Speak 2000 (English, Slovak) is in the menu when its firmware and factory state are there, and takes
 * the Braille Lite's keys; the Slovak unit's file names go in and out in code page 852.
 *
 * The window (README-linux.md, "The desktop app"):
 *   the menu bar -- Firmware (the units, files in and out as a disk image, the factory state, Exit), Settings (the
 *   idle channel, keep open, pop and click, tick, quick key response, sample rate, sound buffer, serial port), Help
 *   (Keys, About) --
 *   with mnemonics; F11 and Alt+Shift+F open it whichever unit runs, as on Windows;
 *   the keyboard area, the one place that takes the focus: its accessible name says the unit and how to reach the
 *   menu, so Orca says it on focus.  GDK reports each key going down AND coming up, so the keys are an input
 *   device's (keys.h KE_DOWN / KE_UP, by the key's place: X's and Wayland's key codes are Linux's plus 8, mapped by
 *   evdev_linux.c's table): the Braille Lite's chords and keys held come from bl_keys.c (i-chord held through a
 *   restart works as on Windows), the Type 'n Speak's keys from tns_term.c's codes (tns_keys.h);
 *   the status line, whose short results ("Switched to ...", "Exported 12 files to ...") are announced to the screen
 *   reader (ATK's announcement); longer reports and every question are GTK message dialogs, which Orca reads.
 * Nothing is grabbed: keys the unit does not take go on to GTK, and Orca's own keys (its modifier, Insert or Caps
 * Lock) are Orca's before the window sees them.
 *
 * Sound, settings and memory are the terminal shell's (main_linux.c): audio_linux.c on a thread that renders each
 * block from the unit as the card drains one, the card's clock pacing it (with no card, the system clock), with the
 * Windows app's Settings > Sound buffer (audio_pace.h, [sound] buffer=); the same settings file and
 * memory folder, ~/.config/ssi263-speech/blazie-emu/ (blazie_emu.ini, <unit>.state), so both shells share them.  The
 * memory is saved every minute, when the window closes, on switching units, when the session ends (GtkApplication's
 * query-end) and on SIGTERM or SIGHUP.
 *
 * Licence: MIT.  GTK 3 (LGPL-2.1-or-later) is linked dynamically, as the system's own library.
 *
 * For the tests (tools/linux_tests.sh, test_emu_gtk.py): --no-sound, --trace FILE (the unit's level every 100 ms of
 * its time, every key action, the status lines, --ram-has texts as the unit's memory first holds them), and
 * BLAZIE_GTK_BREAK=noname (the keyboard area left without its accessible name: the must-fail control).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <gtk/gtk.h>
#include <glib-unix.h>
#include "audio_linux.h"
#include "audio_pace.h"
#include "bl_keys.h"
#include "emu_unit.h"
#include "evdev_linux.h"
#include "ini.h"
#include "keys.h"
#include "serial_linux.h"
#include "tns_rescue.h"
#include "tns_setup.h"
#include "tns_term.h"
#include "../../csrc/blazie/bl_files.h"
#include "../../csrc/blazie/bl_files_state.h"
#include "../../csrc/blazie/bl_files_xfer.h"
#include "../../csrc/blazie/fat_img.h"

#define BLOCK_MS_DEFAULT 10
#define RATE_MAX 48000
#define AUTOSAVE_S 60
#define MAX_RAM_HAS 8

/* the units, as main_linux.c's: the firmware folder's layout as the repository's firmware/blazie (spanish/, tns/,
   bns2000/), or all in one folder (the package's share/ssi263-speech).  state NULL: a cold start (tns_setup.h).
   codepage: the code page of the unit's file and folder names, for files in and out (bl_files_xfer.h), as
   main_win.c's: 850, the Slovak Braille 'n Speak 2000's 852 (its "fles subory" folder). */
typedef struct {
    const char *name, *id;
    int kind;
    const char *firmware[2], *state[2];
    const char *saved, *ini;
    int codepage;
} unit_kind;
static const unit_kind KINDS[] = {
    {"Braille Lite 2000 (English)", "bl-en", EMU_BRAILLE_LITE, {"BL2ENG.BNS", NULL}, {"bl2_2003_warm.state", NULL},
     "english.state", "english", BLX_CP850},
    {"Braille Lite 2000 (Spanish)", "bl-es", EMU_BRAILLE_LITE, {"spanish/BL2SPA.BNS", "BL2SPA.BNS"},
     {"spanish/bl2spa_fresh.state", "bl2spa_fresh.state"}, "spanish.state", "spanish", BLX_CP850},
    {"Type 'n Speak (English)", "tns-en", EMU_TYPE_N_SPEAK, {"tns/TNSENG.TNS", "TNSENG.TNS"}, {NULL, NULL},
     "tns_english.state", "tns_english", BLX_CP850},
    {"Type 'n Speak (Spanish)", "tns-es", EMU_TYPE_N_SPEAK, {"tns/TNSSPA.TNS", "TNSSPA.TNS"}, {NULL, NULL},
     "tns_spanish.state", "tns_spanish", BLX_CP850},
    /* the Braille 'n Speak 2000 on the Braille Lite's board (emu_unit.h emu_model), in the menu when its firmware and
       its factory state (make_state.c) are there */
    {"Braille 'n Speak 2000 (English)", "bns-en", EMU_BRAILLE_LITE, {"bns2000/BS03ENG.BNS", "BS03ENG.BNS"},
     {"bns2000/bs03eng_fresh.state", "bs03eng_fresh.state"}, "bns_english.state", "bns_english", BLX_CP850},
    {"Braille 'n Speak 2000 (Slovak)", "bns-sk", EMU_BRAILLE_LITE, {"bns2000/BS2SLL.BNS", "BS2SLL.BNS"},
     {"bns2000/bs2sll_fresh.state", "bs2sll_fresh.state"}, "bns_slovak.state", "bns_slovak", BLX_CP852},
};
#define N_KINDS ((int)(sizeof KINDS / sizeof KINDS[0]))
#define N_FIRST_KINDS 4                 /* always in the menu; the kinds after them only when they can run */
/* the mnemonics: each letter once in the Firmware menu (GTK only moves between items sharing one): Windows' Sl&ovak
   would share Export's o */
static const char *const KIND_MENU[N_KINDS] = {"Braille Lite 2000, _English", "Braille Lite 2000, _Spanish",
                                               "_Type 'n Speak, English", "Type 'n Speak, S_panish",
                                               "Braille 'n Speak 2000, E_nglish", "Braille 'n Speak 2000, S_lovak"};
static const int RATES[] = {11025, 16000, 22050, 32000, 44100, 48000};
#define N_RATES ((int)(sizeof RATES / sizeof RATES[0]))
static const char *const IDLE_NAMES[] = {"off", "hiss", "whine", "unit"};
static const char *const IDLE_MENU[] = {"Idle channel: _silent", "Idle channel: _hiss", "Idle channel: _whine",
                                        "Idle channel: as the _unit (hiss at even volumes, whine at odd)"};
static const char *const IDLE_TEXT[] = {"silent", "hiss", "whine", "as the unit"};
static const char *const OPEN_NAMES[] = {"off", "until", "always"};
static const char *const OPEN_MENU[] = {"Keep the channel open: _off (silent as soon as speech ends)",
                                        "Keep the channel open: until the unit _clicks off",
                                        "Keep the channel open: _always"};
static const char *const OPEN_TEXT[] = {"off", "until the unit clicks off", "always"};
/* Settings > Sound buffer (audio_pace.h AP_AUTO .. AP_LONG), as the Windows app's */
/* the offered ones only (no short: AP_OFFERED), their text from audio_pace.h's own numbers; menu: with a mnemonic */
static void buffer_text(int mode, int menu, char *out, size_t cap)
{
    if (mode == AP_AUTO)
        snprintf(out, cap, menu ? "_Automatic (%d ms, longer when the sound breaks up)" : "automatic (%d ms)",
                 AP_AUTO_START_MS);
    else if (mode == AP_LONG)
        snprintf(out, cap, menu ? "_Long (%d ms: a remote session, or a sound device shared with a screen reader)"
                 : "long (%d ms)", AP_LONG_MS);
    else
        snprintf(out, cap, menu ? "_Medium (%d ms)" : "medium (%d ms)", AP_MEDIUM_MS);
}
static const char *const SERIAL_PATTERNS[] = {"/dev/ttyUSB%d", "/dev/ttyACM%d", "/dev/ttyS%d", "/dev/ttyAMA%d"};
#define MAX_PORTS 32

/* ---- the state ------------------------------------------------------------------------------------------------- */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;   /* the unit, its keys, its serial line */
static emu_unit *g_unit;
static int g_kind;
static int g_rate = 44100, g_block_ms = BLOCK_MS_DEFAULT;
static int g_whine = 3, g_keep_open = 1, g_popclick = 1, g_tick = 1, g_quick;
static char g_cfg_dir[PATH_MAX], g_ini_path[PATH_MAX], g_fw_dir[PATH_MAX];
static char g_serial_want[PATH_MAX], g_serial_shown[PATH_MAX + 64];
static ini *g_ini;
static bl_keys g_blk;
static key_combo g_menu_bl[6], g_menu_tns[6];
static int g_n_menu_bl, g_n_menu_tns;
static unsigned char g_tns_held[0x80];          /* the Type 'n Speak's keys down now */
static tty_link *g_tty;
static int g_no_sound, g_break_name;

/* sound */
static audio_out *g_audio;
static pthread_t g_audio_thread;
static int g_audio_running;
static volatile int g_audio_stop;
static int g_save_due;                         /* the minute's save, waiting for the sound thread (__atomic) */
static int g_buffer_mode = AP_AUTO;            /* Settings > Sound buffer (audio_pace.h; __atomic) */

/* the tests' trace */
static FILE *g_trace;
static pthread_mutex_t g_trace_lock = PTHREAD_MUTEX_INITIALIZER;
static const char *g_ram_has[MAX_RAM_HAS];
static int g_n_ram_has, g_ram_found[MAX_RAM_HAS];

/* the window */
static GtkApplication *g_app;
static GtkWidget *g_win, *g_area, *g_status, *g_bar, *g_firmware_item;
static GtkWidget *g_kind_items[N_KINDS], *g_idle_items[4], *g_open_items[3], *g_rate_items[N_RATES];
static GtkWidget *g_buffer_items[AP_N_MODES];
static GtkWidget *g_popclick_item, *g_tick_item, *g_quick_item, *g_serial_menu;
static char g_ports[MAX_PORTS][32];
static int g_n_ports;
static int g_updating;                          /* the menu's marks being set by the program, not chosen */
static char g_area_text[512];

static double mono(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static void trace(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void trace(const char *fmt, ...)
{
    va_list ap;
    if (!g_trace)
        return;
    pthread_mutex_lock(&g_trace_lock);
    va_start(ap, fmt);
    vfprintf(g_trace, fmt, ap);
    va_end(ap);
    fputc('\n', g_trace);
    fflush(g_trace);
    pthread_mutex_unlock(&g_trace_lock);
}

/* ---- the status line, the title, the keyboard area's name -------------------------------------------------------- */
/* AtkLive came with ATK 2.50 (the "notification" signal's politeness); Debian 11 has 2.36 and Debian 12 2.46.  A build
   there names the value itself; at run time a newer ATK's signal is still found by name and used. */
#if ATK_CHECK_VERSION(2, 50, 0)
#define LIVE_POLITE ((gint)ATK_LIVE_POLITE)
#else
#define LIVE_POLITE ((gint)1)                   /* ATK 2.50's AtkLive: NONE 0, POLITE 1, ASSERTIVE 2 */
#endif
/* the status line's text; announce: also said by the screen reader now (ATK's announcement, at-spi's
   object:announcement, which Orca speaks) */
static void set_status(int announce, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void set_status(int announce, const char *fmt, ...)
{
    char text[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    trace("status %s", text);
    if (!g_status)
        return;
    gtk_label_set_text(GTK_LABEL(g_status), text);
    if (announce) {                             /* ATK's signals, looked up: "notification" (ATK 2.50: polite), else
                                                   "announcement" (2.46); an older ATK: the label's text only */
        AtkObject *a = gtk_widget_get_accessible(g_status);
        if (g_signal_lookup("notification", G_OBJECT_TYPE(a)))
            g_signal_emit_by_name(a, "notification", text, LIVE_POLITE);
        else if (g_signal_lookup("announcement", G_OBJECT_TYPE(a)))
            g_signal_emit_by_name(a, "announcement", text);
    }
}

/* the window's title (Orca reads it when the window comes to the front) and the keyboard area's name (read when it
   takes the focus): the unit, the serial port, the way to the menu */
static void update_names(void)
{
    char title[PATH_MAX + 200], name[300];
    const char *unit = g_unit ? KINDS[g_kind].name : "No unit running";
    int tns = KINDS[g_kind].kind == EMU_TYPE_N_SPEAK;
    if (g_tty)
        snprintf(title, sizeof title, "%s, serial port on %s - Blazie emulator", unit, g_serial_shown);
    else
        snprintf(title, sizeof title, "%s - Blazie emulator", unit);
    if (g_win)
        gtk_window_set_title(GTK_WINDOW(g_win), title);
    snprintf(name, sizeof name, "%s: keyboard. F11 or Alt+Shift+F opens the menu", unit);
    snprintf(g_area_text, sizeof g_area_text, "%s\n\n%s\n\nF11 or Alt+Shift+F: the menu.", unit, tns
             ? "The whole keyboard is the unit's." : "F D S = dots 1 2 3, J K L = dots 4 5 6, the space bar, "
             "; = the advance bar, A = the back bar. Alt and the letter: the menus.");
    if (g_area) {
        AtkObject *a = gtk_widget_get_accessible(g_area);
        if (!g_break_name)                      /* the tests' control leaves it without its name */
            atk_object_set_name(a, name);
        atk_object_set_description(a, tns ? "The Type 'n Speak's keyboard: every key goes to the unit."
                                   : "The Braille Lite's keys: F D S and J K L are dots 1 to 6, the space bar is "
                                     "space, semicolon the advance bar and A the back bar; press a chord's keys together.");
        gtk_widget_queue_draw(g_area);
    }
}

/* ---- dialogs: GTK's own, which Orca reads (their text is labels in an alert) ------------------------------------- */
/* the keyboard back to the unit's area, its window in front (after a dialog: with no window manager to give the
   focus back, X would leave it nowhere) */
static void focus_area(void)
{
    if (!g_area)
        return;
    gtk_window_present(GTK_WINDOW(g_win));
    /* the window never lost the keyboard (a dialog that did not take it): the area's focus moved away and back, so
       the screen reader is told where the focus is again (a window manager giving the window back does the same) */
    if (gtk_window_is_active(GTK_WINDOW(g_win)) && gtk_widget_has_focus(g_area))
        gtk_window_set_focus(GTK_WINDOW(g_win), NULL);
    gtk_widget_grab_focus(g_area);
}

/* the dialog's title, also its accessible name (GTK names a message dialog "Information", "Warning" ...: Orca would
   say that, not what it is about) */
static void titled(GtkWidget *d, const char *title)
{
    gtk_window_set_title(GTK_WINDOW(d), title);
    atk_object_set_name(gtk_widget_get_accessible(d), title);
}

static void message(GtkMessageType type, const char *title, const char *text)
{
    GtkWidget *d = gtk_message_dialog_new(g_win ? GTK_WINDOW(g_win) : NULL, GTK_DIALOG_MODAL |
                                          GTK_DIALOG_DESTROY_WITH_PARENT, type, GTK_BUTTONS_OK, "%s", text);
    titled(d, title);
    trace("dialog %s", title);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    focus_area();
}

/* 1 for Yes; No is the default (Enter alone is No) */
static int ask_yes_no(const char *title, const char *text)
{
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(g_win), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                          GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO, "%s", text);
    int r;
    titled(d, title);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_NO);
    trace("dialog %s", title);
    r = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    focus_area();
    return r == GTK_RESPONSE_YES;
}

/* ---- paths ----------------------------------------------------------------------------------------------------- */
static int exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static void make_dirs(const char *path)
{
    char p[PATH_MAX];
    char *s;
    snprintf(p, sizeof p, "%s", path);
    for (s = p + 1; *s; s++)
        if (*s == '/') {
            *s = 0;
            mkdir(p, 0755);
            *s = '/';
        }
    mkdir(p, 0755);
}

static void saved_path(int kind, char *out, int cap)
{
    snprintf(out, (size_t)cap, "%s/%s", g_cfg_dir, KINDS[kind].saved);
}

static void fw_file(const char *const names[2], char *out, int cap)
{
    int i;
    out[0] = 0;
    for (i = 0; i < 2; i++)
        if (names[i]) {
            snprintf(out, (size_t)cap, "%s/%s", g_fw_dir, names[i]);
            if (exists(out))
                return;
        }
    if (names[0])
        snprintf(out, (size_t)cap, "%s/%s", g_fw_dir, names[0]);
}

/* a kind the menu offers: the first four always (a missing firmware is said when it is chosen); the Braille 'n Speak
   2000 only when its firmware and its factory state are both in the firmware folder */
static int kind_offered(int kind)
{
    char fw[PATH_MAX], st[PATH_MAX];
    if (kind < N_FIRST_KINDS)
        return 1;
    fw_file(KINDS[kind].firmware, fw, sizeof fw);
    fw_file(KINDS[kind].state, st, sizeof st);
    return exists(fw) && exists(st);
}

/* --firmware, else firmware_dir in the settings, else beside the program (main_linux.c's places) */
static void find_firmware(const char *given)
{
    char exe[PATH_MAX], probe[PATH_MAX + 64], *slash;
    const char *set = ini_get(g_ini, "unit", "firmware_dir", "");
    static const char *const NEAR[] = {"../share/ssi263-speech", "firmware", "../../firmware/blazie"};
    ssize_t n;
    unsigned i;
    if (given && *given) {
        snprintf(g_fw_dir, sizeof g_fw_dir, "%s", given);
        return;
    }
    if (*set) {
        snprintf(g_fw_dir, sizeof g_fw_dir, "%s", set);
        return;
    }
    n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    exe[n > 0 ? n : 0] = 0;
    if ((slash = strrchr(exe, '/')) != NULL)
        *slash = 0;
    for (i = 0; i < sizeof NEAR / sizeof NEAR[0]; i++) {
        snprintf(probe, sizeof probe, "%s/%s/BL2ENG.BNS", exe, NEAR[i]);
        if (exists(probe)) {
            snprintf(g_fw_dir, sizeof g_fw_dir, "%s/%s", exe, NEAR[i]);
            return;
        }
    }
    snprintf(g_fw_dir, sizeof g_fw_dir, "%s/%s", exe, NEAR[0]);
}

/* ---- the settings file: main_linux.c's, shared (test_emu_gtk.py checks the two texts are the same) --------------- */
static const char DEFAULT_INI[] =
    "; The Blazie emulator's settings (Linux).  Lines starting with ; are comments.  The program changes the [unit],\n"
    "; [sound] and [serial] settings from its menu (F11); the keys are yours to change here.  Key names: a letter or\n"
    "; symbol, space, enter, tab, backspace, esc, up, down, left, right, home, end, pageup, pagedown, insert, delete,\n"
    "; f1-f12, brl_dot1-brl_dot8 (a braille keyboard's own dot keys), code:N (an input device's key code N); with\n"
    "; ctrl-, alt-, shift- in front for a combination.  Several keys: separated by spaces.\n"
    "\n"
    "[keys]\n"
    "; The Braille Lite from a terminal: keys (each key a dot, typed together: F D S = dots 1 2 3, J K L = 4 5 6)\n"
    "; or letters (each character typed is its braille cell in computer braille -- the BTSpeak's own keyboard).\n"
    "mode = keys\n"
    "dot1 = f brl_dot1\n"
    "dot2 = d brl_dot2\n"
    "dot3 = s brl_dot3\n"
    "dot4 = j brl_dot4\n"
    "dot5 = k brl_dot5\n"
    "dot6 = l brl_dot6\n"
    "space = space\n"
    "; the Braille Lite 2000's two advance bars: advance (forward) and back\n"
    "advance = ; brl_dot8\n"
    "back = a brl_dot7\n"
    "; keys mode: keys typed within chord_ms of each other are one chord; the same key again within repeat_ms is\n"
    "; the keyboard's auto-repeat (dropped)\n"
    "chord_ms = 80\n"
    "repeat_ms = 150\n"
    "; the next chord is held down until this key is pressed again (p-chord, hold, i-chord, l: the cold reset)\n"
    "hold = f12 ctrl-k\n"
    "; this program's menu, for the Braille Lite and for the Type 'n Speak (Alt+Shift+F always opens it too)\n"
    "menu = f11 ctrl-o\n"
    "tns_menu = f11\n"
    "\n"
    "[letters]\n"
    "; letters mode: the chord prefix (the next character is its chord, with the space bar), a capital letter as its\n"
    "; chord (dot 7 on a BTSpeak), and the keys that stand for chords (the BTSpeak's own keys for chords, mapped\n"
    "; back): key = chord, the chord as e-chord, 1-chord, 2-5-6-chord, dots 1 3, space, advance, back or none\n"
    "prefix = ctrl-c\n"
    "capital_is_chord = 1\n"
    "; up = 1-chord\n"
    "; enter = e-chord\n"
    "; backspace = b-chord\n"
    "; ctrl-a = advance\n"
    "; ctrl-b = back\n"
    "\n"
    "[btspeak]\n"
    "; The BT Speak's and BT Braille's own keyboard, from the device's keyboard server: auto (when there is one), or\n"
    "; off.  Dots 1-6 and the space bar are the Braille Lite's keys, dots 7 and 8 its back and advance bars ([keys]\n"
    "; back, advance), and on a BT Braille L2 or R2 and L3 or R3 (and the keys that pan its display) are the bars\n"
    "; too.  M-chord with dot 7 opens this program's menu, Z-chord with dot 7 saves and leaves; gesture_ms is how\n"
    "; long a 7 or 8 waits for a chord's other keys.\n"
    "keyboard = auto\n"
    "gesture_ms = 80\n"
    "; The Braille Lite's display on a braille display, through BRLTTY: auto (when BRLTTY has one), or off\n"
    "display = auto\n"
    "\n"
    "[input]\n"
    "; the input devices (/dev/input; the input group needed), where keys are seen going down and up: auto (on a\n"
    "; text console, when a keyboard can be read), on, off, or a device (/dev/input/by-id/...)\n"
    "evdev = auto\n"
    "; 1: only this program gets those keys while it runs (not the console, not a screen reader)\n"
    "grab = 1\n"
    "; blazie_emu on a BT Speak or BT Braille: auto (it hands over to blazie_emu_bt, the BT front end with the device's\n"
    "; own dialogs; without it, as native), native (this program uses the device's keyboard and braille display\n"
    "; itself: [btspeak] above) or off (the terminal only, as everywhere else; --no-bt once)\n"
    "bt = auto\n";

/* the keys: the Braille Lite's dots, space and advance bars as the terminal shell reads them ([keys] dot1 ..
   advance, back; GTK's keys go down and up, so keys mode's timing, letters mode and the hold key are not needed here), and
   the menu keys */
static void load_keys(void)
{
    static const char *const KEYS[] = {"dot1", "dot2", "dot3", "dot4", "dot5", "dot6", "space", "advance",
                                       "back"};
    char value[256];
    unsigned i;
    blk_defaults(&g_blk);
    for (i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++) {
        const char *v = ini_get(g_ini, "keys", KEYS[i], NULL);
        if (v) {
            snprintf(value, sizeof value, "%s", v);
            if (!blk_set(&g_blk, KEYS[i], value))
                fprintf(stderr, "blazie_emu.ini: [keys] %s = %s is not understood; the default is kept\n", KEYS[i],
                        value);
        }
    }
    g_n_menu_bl = key_list(ini_get(g_ini, "keys", "menu", "f11 ctrl-o"), g_menu_bl, 5);
    g_n_menu_tns = key_list(ini_get(g_ini, "keys", "tns_menu", "f11"), g_menu_tns, 5);
    g_n_menu_bl += key_list("alt-shift-f", g_menu_bl + g_n_menu_bl, 1);
    g_n_menu_tns += key_list("alt-shift-f", g_menu_tns + g_n_menu_tns, 1);
}

static void load_settings(void)
{
    const char *v;
    int k, r;
    v = ini_get(g_ini, "sound", "idle", "unit");
    for (k = 0; k < 4; k++)
        if (!strcmp(v, IDLE_NAMES[k]))
            g_whine = k;
    v = ini_get(g_ini, "sound", "keep_open", "until");
    for (k = 0; k < 3; k++)
        if (!strcmp(v, OPEN_NAMES[k]))
            g_keep_open = k;
    g_popclick = ini_get_int(g_ini, "sound", "pop_click", 1) != 0;
    g_tick = ini_get_int(g_ini, "sound", "tick", 1) != 0;
    g_block_ms = ini_get_int(g_ini, "sound", "block_ms", BLOCK_MS_DEFAULT);
    if (g_block_ms < 5 || g_block_ms > 20)
        g_block_ms = BLOCK_MS_DEFAULT;
    r = ini_get_int(g_ini, "sound", "rate", 44100);
    for (k = 0; k < N_RATES; k++)
        if (RATES[k] == r)
            g_rate = r;
    g_quick = ini_get_int(g_ini, "unit", "quick_keys", 0) != 0;
    if ((k = ap_mode_of(ini_get(g_ini, "sound", "buffer", "auto"))) >= 0)
        g_buffer_mode = k;
}

/* the settings this shell changes, written back with every other line kept ([keys] is the person's) */
static void save_settings(void)
{
    char v[16];
    ini_set(g_ini, "unit", "kind", KINDS[g_kind].ini);
    ini_set(g_ini, "unit", "quick_keys", g_quick ? "1" : "0");
    ini_set(g_ini, "sound", "idle", IDLE_NAMES[g_whine]);
    ini_set(g_ini, "sound", "keep_open", OPEN_NAMES[g_keep_open]);
    ini_set(g_ini, "sound", "pop_click", g_popclick ? "1" : "0");
    ini_set(g_ini, "sound", "tick", g_tick ? "1" : "0");
    snprintf(v, sizeof v, "%d", g_rate);
    ini_set(g_ini, "sound", "rate", v);
    ini_set(g_ini, "sound", "buffer", ap_mode_name(g_buffer_mode));
    ini_set(g_ini, "serial", "port", g_serial_want[0] ? g_serial_want : "none");
    if (!ini_save(g_ini, g_ini_path))
        set_status(1, "Could not write the settings to %s.", g_ini_path);
}

/* ---- the unit --------------------------------------------------------------------------------------------------- */
/* its memory, written whole then put in place (a crash mid-write keeps the last one), under the lock: the sound
   thread's minute save (audio_main) and a save from the window never write the same file at once.  Either thread;
   no GTK here.  1 on success (or no unit), the file in path. */
static int write_unit(char *path, int cap)
{
    char tmp[PATH_MAX + 8];
    int ok = 1;
    path[0] = 0;
    pthread_mutex_lock(&g_lock);
    if (g_unit) {
        make_dirs(g_cfg_dir);
        saved_path(g_kind, path, cap);
        snprintf(tmp, sizeof tmp, "%s.new", path);
        ok = emu_save(g_unit, tmp) && rename(tmp, path) == 0;
        if (!ok)
            unlink(tmp);
    }
    pthread_mutex_unlock(&g_lock);
    if (ok && path[0])
        trace("saved %s", path);
    return ok;
}

static char g_save_failed[PATH_MAX];           /* the sound thread's save that failed, for the window to say */

static gboolean on_save_failed(gpointer data)
{
    (void)data;
    set_status(1, "Could not save the unit's memory to %s; what you wrote this time is lost.", g_save_failed);
    return G_SOURCE_REMOVE;
}

/* 1 on success */
static int save_unit(void)
{
    char path[PATH_MAX];
    if (write_unit(path, (int)sizeof path))
        return 1;
    set_status(1, "Could not save the unit's memory to %s; what you wrote this time is lost.", path);
    return 0;
}

static void apply_idle(void)
{
    pthread_mutex_lock(&g_lock);
    if (g_unit)
        emu_set_idle(g_unit, g_whine, g_keep_open, g_popclick, g_tick);
    pthread_mutex_unlock(&g_lock);
}

/* the unit's keys let go: a chord half pressed dropped, the keys held up, the Type 'n Speak's keys up (the focus
   left the keyboard area, the menu opened, the unit changed) */
static void release_keys(void)
{
    bl_action a[BLK_MAX_ACTIONS];
    int n, i, c;
    pthread_mutex_lock(&g_lock);
    blk_reset(&g_blk);
    n = blk_take(&g_blk, a, BLK_MAX_ACTIONS);
    for (i = 0; i < n; i++)
        if (g_unit && emu_kind(g_unit) == EMU_BRAILLE_LITE && a[i].type == BLA_HELD)
            emu_keys_down(g_unit, a[i].bits);
    for (c = 0; c < 0x80; c++)
        if (g_tns_held[c]) {
            if (g_unit && emu_kind(g_unit) == EMU_TYPE_N_SPEAK)
                emu_key(g_unit, c);
            trace("tns 0x%02X (let go)", c);
            g_tns_held[c] = 0;
        }
    pthread_mutex_unlock(&g_lock);
}

/* A saved Type 'n Speak that was never set up (tns_rescue.h): the person chooses, as on Windows.  0: start it from
   no state (the factory's cold start); 1: from st. */
static int offer_setup(int kind, const char *fw, const char *st)
{
    char msg[4000], lost[300] = "", err[300], before[PATH_MAX + 20];
    tns_rescue_report r;
    GtkWidget *d;
    int answer;
    emu_unit *old;
    if (tns_needs_setup(st, &r) != 1)
        return 1;
    pthread_mutex_lock(&g_lock);                /* the running unit (already saved) switched off: the minute's save, */
    old = g_unit;                               /* which runs while the dialog is up, must not write it over the */
    g_unit = NULL;                              /* state set up here */
    pthread_mutex_unlock(&g_lock);
    emu_destroy(old);
    if (r.lost_flash)
        snprintf(lost, sizeof lost, ", and %d it lost when moving them to flash (their text was never written: "
                 "only their names are left)", r.lost_flash);
    snprintf(before, sizeof before, "%s.before-setup", st);
    snprintf(msg, sizeof msg, "This %s was never set up: its file system and folders were not made when it first "
             "started (the 0.6 and 0.7 previews' first start missed the unit's cold reset; or a setup question was "
             "answered n). On it a new file can lose its first letter, and a file moved to flash is lost.\n\n"
             "Its files: %d in RAM%s.\n\n"
             "Set it up: now, keeping its RAM files (in its RAM startup folder). Its settings go back to the "
             "factory's.\n"
             "Factory state: it asks its own setup questions, and its files are not kept.\n"
             "As it is: start it unchanged.\n\n"
             "Set it up and Factory state keep the old memory as %s.", KINDS[kind].name, r.ram_files, lost, before);
    d = gtk_message_dialog_new(GTK_WINDOW(g_win), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                               GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE, "%s", msg);
    titled(d, "Type 'n Speak not set up");
    gtk_dialog_add_buttons(GTK_DIALOG(d), "_Set it up, keep its files", GTK_RESPONSE_YES, "_Factory state",
                           GTK_RESPONSE_NO, "_As it is", GTK_RESPONSE_CANCEL, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_YES);
    trace("dialog Type 'n Speak not set up");
    answer = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    if (answer == GTK_RESPONSE_NO) {
        FILE *a = fopen(st, "rb"), *b = fopen(before, "wb");
        char buf[65536];
        size_t n;
        int ok = a && b;
        while (ok && (n = fread(buf, 1, sizeof buf, a)) > 0)
            ok = fwrite(buf, 1, n, b) == n;
        if (a) fclose(a);
        if (b && fclose(b) != 0) ok = 0;
        if (!ok) {
            message(GTK_MESSAGE_ERROR, "Blazie emulator", "Could not keep the old memory; the unit starts as it is.");
            return 1;
        }
        unlink(st);
        return 0;
    }
    if (answer != GTK_RESPONSE_YES)
        return 1;
    set_status(1, "Setting up the Type 'n Speak (a few seconds).");
    while (gtk_events_pending())
        gtk_main_iteration();
    if (!tns_rescue(fw, st, 1, &r, err, sizeof err)) {
        snprintf(msg, sizeof msg, "Could not set the unit up: %s\n\nIt starts as it is.", err);
        message(GTK_MESSAGE_ERROR, "Blazie emulator", msg);
        return 1;
    }
    snprintf(msg, sizeof msg, "The %s is set up: %d files kept%s. The old memory is in %s.\n\n%s", KINDS[kind].name,
             r.carried, r.first_lost ? " (one had lost its first letter on the old unit)" : "", before, r.log);
    message(GTK_MESSAGE_INFO, "Type 'n Speak set up", msg);
    return 1;
}

static void sync_menu(void);

static int start_unit(int kind)
{
    char fw[PATH_MAX], st[PATH_MAX], err[300];
    const char *state = st;
    emu_unit *u, *old;
    save_unit();                                /* first: the unit being left keeps its memory (it may be this kind) */
    release_keys();
    fw_file(KINDS[kind].firmware, fw, sizeof fw);
    saved_path(kind, st, sizeof st);
    if (exists(st) && KINDS[kind].kind == EMU_TYPE_N_SPEAK && !offer_setup(kind, fw, st))
        state = NULL;                           /* the factory state: the unit's cold reset, its own questions */
    else if (!exists(st)) {                     /* the first time: the unit as it left the factory */
        if (KINDS[kind].state[0])
            fw_file(KINDS[kind].state, st, sizeof st);
        else
            state = NULL;
    }
    if (!state && KINDS[kind].kind == EMU_TYPE_N_SPEAK)
        message(GTK_MESSAGE_INFO, KINDS[kind].name, TNS_FIRST_START);
    trace("starting %s from %s", KINDS[kind].id, state ? st : "cold");
    u = emu_create(KINDS[kind].kind, fw, state, g_rate, g_whine, err, sizeof err);
    if (!u) {
        char msg[PATH_MAX + 700];
        snprintf(msg, sizeof msg, "Could not start the %s.\n\n%s\n\nThe firmware is looked for in %s (firmware_dir in "
                 "%s).", KINDS[kind].name, err, g_fw_dir, g_ini_path);
        message(GTK_MESSAGE_ERROR, "Blazie emulator", msg);
        sync_menu();
        update_names();
        return 0;
    }
    if (g_tty)
        emu_serial_attach(u, 1);                /* the new unit takes the old one's place on the line */
    emu_set_idle(u, g_whine, g_keep_open, g_popclick, g_tick);
    emu_set_quick(u, g_quick);
    pthread_mutex_lock(&g_lock);
    old = g_unit;
    g_unit = u;
    g_kind = kind;
    pthread_mutex_unlock(&g_lock);
    emu_destroy(old);
    save_settings();
    sync_menu();
    update_names();
    return 1;
}

/* ---- the sound thread: the unit renders each block as the card takes it ----------------------------------------- */
static int ram_has(const char *text)
{
    const unsigned char *m;
    int n = emu_memory(g_unit, &m), len = (int)strlen(text), i;
    for (i = 0; i + len <= n; i++)
        if (m[i] == (unsigned char)text[0] && !memcmp(m + i, text, (size_t)len))
            return 1;
    return 0;
}

static void *audio_main(void *arg)
{
    static short buf[RATE_MAX / 50];
    int block = g_rate * g_block_ms / 1000, i;
    long trace_n = 0, ram_n = 0, per = g_rate / 10;
    double sum = 0;
    emu_unit *traced = NULL;
    struct timespec next;
    (void)arg;
    clock_gettime(CLOCK_MONOTONIC, &next);
    while (!g_audio_stop) {
        if (g_audio) {                          /* the sound buffer (audio_linux.h): a block when the card wants one */
            int turn = audio_turn(g_audio, __atomic_load_n(&g_buffer_mode, __ATOMIC_ACQUIRE),
                                  __atomic_load_n(&g_save_due, __ATOMIC_ACQUIRE));
            if (turn == AUDIO_WAIT)
                continue;
            if (turn == AUDIO_SAVE) {           /* the minute's save, the queue rendered ahead: the card plays it
                                                   while the memory is written (Tomi: the emulator's speech
                                                   stutters, the add-on's doesn't) */
                double t0 = mono();
                __atomic_store_n(&g_save_due, 0, __ATOMIC_RELEASE);
                if (!write_unit(g_save_failed, (int)sizeof g_save_failed))
                    g_idle_add(on_save_failed, NULL);    /* the window says so */
                audio_saved(g_audio, (mono() - t0) * 1000.0);
                continue;
            }
        }
        pthread_mutex_lock(&g_lock);
        if (g_unit)
            emu_render(g_unit, buf, block);
        else
            memset(buf, 0, sizeof(short) * (size_t)block);
        tty_pump(g_tty, g_unit);                /* the unit ran: its serial bytes may be waiting, both ways */
        if (g_trace && g_unit) {                /* the tests: the level every 100 ms of the unit's time */
            if (traced != g_unit) {
                traced = g_unit;
                trace_n = ram_n = 0;
                sum = 0;
            }
            for (i = 0; i < block; i++)
                sum += (double)buf[i] * buf[i];
            trace_n += block;
            ram_n += block;
            if (trace_n >= per) {
                trace("%.2f rms %.5f", emu_time(g_unit), sqrt(sum / (double)trace_n) / 32768.0);
                trace_n = 0;
                sum = 0;
            }
            if (ram_n >= g_rate / 2) {          /* and the texts asked for, as the unit's memory first holds them */
                ram_n = 0;
                for (i = 0; i < g_n_ram_has; i++)
                    if (!g_ram_found[i] && ram_has(g_ram_has[i])) {
                        g_ram_found[i] = 1;
                        trace("%.2f ram-has %s", emu_time(g_unit), g_ram_has[i]);
                    }
            }
        }
        pthread_mutex_unlock(&g_lock);
        if (g_audio && !audio_write(g_audio, buf, block)) {
            audio_close(g_audio);
            g_audio = NULL;
            clock_gettime(CLOCK_MONOTONIC, &next);
        }
        if (!g_audio) {                         /* no sound card: the system clock paces the unit */
            next.tv_nsec += (long)g_block_ms * 1000000L;
            while (next.tv_nsec >= 1000000000L) {
                next.tv_nsec -= 1000000000L;
                next.tv_sec++;
            }
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
            /* the minute's save here, between two blocks */
            if (__atomic_exchange_n(&g_save_due, 0, __ATOMIC_ACQ_REL)
                    && !write_unit(g_save_failed, (int)sizeof g_save_failed))
                g_idle_add(on_save_failed, NULL);    /* the window says so */
        }
    }
    return NULL;
}

static void audio_start(void)
{
    char err[300], device[128];
    snprintf(device, sizeof device, "%s", ini_get(g_ini, "sound", "device", "default"));
    g_audio = g_no_sound ? NULL : audio_open(device, g_rate, g_block_ms, g_buffer_mode, err, sizeof err);
    if (!g_audio && !g_no_sound)
        set_status(1, "No sound: %s. The unit runs on, silent ([sound] device in %s chooses another device).", err,
                   g_ini_path);
    g_audio_stop = 0;
    if (pthread_create(&g_audio_thread, NULL, audio_main, NULL) == 0) {
        struct sched_param sp;
        sp.sched_priority = 10;                 /* real-time when allowed (the audio group, rtkit); else as is */
        pthread_setschedparam(g_audio_thread, SCHED_FIFO, &sp);
        g_audio_running = 1;
    }
}

/* 1 when stopped; 0 when the sound thread stays stuck in the device for 2 s (left to the end of the program) */
static int audio_stop(void)
{
    if (g_audio_running) {
        struct timespec t;
        g_audio_stop = 1;
        clock_gettime(CLOCK_REALTIME, &t);
        t.tv_sec += 2;
        if (pthread_timedjoin_np(g_audio_thread, NULL, &t) != 0)
            return 0;
        g_audio_running = 0;
    }
    audio_close(g_audio);
    g_audio = NULL;
    return 1;
}

/* ---- the serial port (serial_linux.c) ----------------------------------------------------------------------------- */
/* plugs the unit's serial port into `name` ("" or "none": unplugged); 1 on success.  at_start: the port saved last
   time -- if it cannot be opened the unit starts unplugged and the setting stays for next time */
static int set_serial(const char *name, int at_start)
{
    char err[300], other[PATH_MAX], link_path[PATH_MAX + 16];
    tty_link *old, *t = NULL;
    pthread_mutex_lock(&g_lock);
    old = g_tty;
    g_tty = NULL;
    if (g_unit)
        emu_serial_attach(g_unit, 0);
    pthread_mutex_unlock(&g_lock);
    tty_close(old);
    snprintf(link_path, sizeof link_path, "%s/serial", g_cfg_dir);
    unlink(link_path);
    snprintf(g_serial_shown, sizeof g_serial_shown, "none");
    if (name[0] && strcmp(name, "none")) {
        t = tty_open(name, other, sizeof other, err, sizeof err);
        if (!t)
            message(GTK_MESSAGE_WARNING, "Serial port", err);
        else {
            snprintf(g_serial_shown, sizeof g_serial_shown, "%s", other[0] ? other : name);
            pthread_mutex_lock(&g_lock);
            if (g_unit)
                emu_serial_attach(g_unit, 1);
            g_tty = t;
            pthread_mutex_unlock(&g_lock);
            if (other[0]) {
                int linked = symlink(other, link_path) == 0;
                set_status(1, "The serial port is on a pseudo-terminal: the other end is %s%s%s%s.", other,
                           linked ? " (also " : "", linked ? link_path : "", linked ? ")" : "");
            } else
                set_status(1, "The serial port is on %s.", name);
        }
    } else if (!at_start)
        set_status(1, "The serial port is not connected.");
    if (!at_start || t)
        snprintf(g_serial_want, sizeof g_serial_want, "%s", t ? name : "none");
    update_names();
    return t != NULL;
}

/* ---- files in and out: the unit's files as a FAT disk image (../../csrc/blazie/bl_files_xfer.h), as on Windows ---- */
/* the chooser: GTK's own dialog; 1 with the path chosen */
static int image_dialog(int save, char *path, int cap)
{
    GtkWidget *d = gtk_file_chooser_dialog_new(save ? "Export the unit's files to a disk image"
                                               : "Import files from a disk image", GTK_WINDOW(g_win),
                                               save ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN,
                                               "_Cancel", GTK_RESPONSE_CANCEL, save ? "_Export" : "_Import",
                                               GTK_RESPONSE_ACCEPT, NULL);
    GtkFileFilter *img = gtk_file_filter_new(), *all = gtk_file_filter_new();
    int ok = 0;
    gtk_file_filter_set_name(img, "Disk images (*.img)");
    gtk_file_filter_add_pattern(img, "*.img");
    gtk_file_filter_add_pattern(img, "*.IMG");
    gtk_file_filter_set_name(all, "All files");
    gtk_file_filter_add_pattern(all, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d), img);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d), all);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_ACCEPT);
    if (save) {
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(d), TRUE);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d), path);
    }
    if (getenv("HOME"))
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(d), getenv("HOME"));
    trace("dialog %s", save ? "export" : "import");
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT) {
        char *f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
        if (f) {
            size_t n = strlen(f);
            snprintf(path, (size_t)cap, "%s%s", f, save && (n < 4 || strcasecmp(f + n - 4, ".img")) ? ".img" : "");
            ok = 1;
            g_free(f);
        }
    }
    gtk_widget_destroy(d);
    focus_area();
    return ok;
}

/* the report's first lines, after a summary, for a dialog the screen reader reads out */
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

static int open_saved(const char *st, bls_unit *u, blf_fs **fs)
{
    char err[300], msg[PATH_MAX + 400];
    if (!bls_load(st, u, err, sizeof err)) {
        snprintf(msg, sizeof msg, "Could not read the unit's memory (%s): %s", st, err);
        message(GTK_MESSAGE_ERROR, "Blazie emulator", msg);
        return 0;
    }
    *fs = blf_open(u->model, u->ram, u->flash, u->flash_size, err, sizeof err);
    if (!*fs) {
        bls_free(u);
        snprintf(msg, sizeof msg, "Could not read the unit's files: %s", err);
        message(GTK_MESSAGE_ERROR, "Blazie emulator", msg);
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

/* every file the unit holds, as it holds them now (the unit runs on); the result said on the status line */
static void export_files(void)
{
    char path[PATH_MAX], st[PATH_MAX], err[300], msg[PATH_MAX + 400];
    bls_unit u;
    blf_fs *fs;
    blx_report r;
    unsigned char *img;
    unsigned long size;
    if (!g_unit) return;
    snprintf(path, sizeof path, "%s files.img", KINDS[g_kind].name);
    if (!image_dialog(1, path, sizeof path)) return;
    if (!save_unit()) return;                   /* its memory as it is this moment */
    saved_path(g_kind, st, sizeof st);
    if (!open_saved(st, &u, &fs)) return;
    blx_report_init(&r);
    img = blx_export_cp(fs, u.model, KINDS[g_kind].codepage, &size, &r, err, sizeof err);
    if (!img || !write_bytes(path, img, size)) {
        snprintf(msg, sizeof msg, "Could not export the files to %s: %s", path,
                 img ? "the image could not be written" : err);
        message(GTK_MESSAGE_ERROR, "Export files", msg);
    } else
        set_status(1, "Exported %d files to %s.", r.exported, path);
    free(img);
    blx_report_free(&r);
    blf_close(fs);
    bls_free(&u);
}

/* the image's files into the unit: only while it is not writing its flash; it is switched off, its files changed,
   and switched on again -- the firmware then finds them as if it had written them (main_win.c's import_files) */
static void import_files(void)
{
    char path[PATH_MAX] = "", st[PATH_MAX], err[300], summary[PATH_MAX * 2 + 400], msg[PATH_MAX * 2 + 4000];
    unsigned char *img;
    unsigned long size;
    fat_volume *v;
    emu_unit *old;
    bls_unit u;
    blf_fs *fs;
    blx_report r;
    int busy, ok, cancelled = 0;
    if (!g_unit) return;
    if (!image_dialog(0, path, sizeof path)) return;
    img = read_bytes(path, &size);
    v = img ? fat_read(img, size, err, sizeof err) : NULL;
    if (!v) {
        snprintf(msg, sizeof msg, "%s is not a disk image the emulator can read: %s", path, img ? err : "unreadable");
        message(GTK_MESSAGE_ERROR, "Import files", msg);
        free(img);
        return;
    }
    fat_close(v);
    make_dirs(g_cfg_dir);
    saved_path(g_kind, st, sizeof st);
    pthread_mutex_lock(&g_lock);
    busy = emu_flash(g_unit, NULL);
    ok = !busy && emu_save(g_unit, st);
    old = ok ? g_unit : NULL;
    if (ok)
        g_unit = NULL;                          /* switched off: nothing runs while its memory changes */
    pthread_mutex_unlock(&g_lock);
    if (!ok) {
        message(GTK_MESSAGE_WARNING, "Import files", busy ? "The unit is writing its flash memory. Import the files "
                "when it has finished (when it is quiet)." : "Could not save the unit's memory; nothing was imported.");
        free(img);
        return;
    }
    emu_destroy(old);
    blx_report_init(&r);
    ok = open_saved(st, &u, &fs);
    if (ok) {
        ok = blx_import_cp(fs, KINDS[g_kind].codepage, img, size, &r, err, sizeof err)
             && blf_check(fs, err, sizeof err);
        if (ok && r.deleted) {                  /* an image holding only new files would empty the unit: ask */
            char ask[3000];
            const char *line = r.log ? r.log : "";
            size_t n = (size_t)snprintf(ask, sizeof ask, "The image does not have %d of the unit's files. Importing "
                                        "it deletes them from the unit:\n", r.deleted);
            while (*line && n + 80 < sizeof ask) {
                const char *e = strchr(line, '\n');
                size_t len = e ? (size_t)(e - line) : strlen(line);
                if (len > 8 && !strncmp(line, "deleted ", 8))
                    n += (size_t)snprintf(ask + n, sizeof ask - n, "\n%.*s", (int)(len - 8), line + 8);
                line = e ? e + 1 : line + len;
            }
            snprintf(ask + n, sizeof ask - n, "\n\nDelete them and import? No imports nothing.");
            if (!ask_yes_no("Import files", ask))
                ok = 0, cancelled = 1;
        }
        if (ok) {                               /* the unit as it was, kept beside it: undone by one file copy */
            char before[PATH_MAX + 20];
            FILE *a, *b;
            snprintf(before, sizeof before, "%s.before-import", st);
            a = fopen(st, "rb");
            b = fopen(before, "wb");
            if (a && b) {
                char buf[65536];
                size_t n;
                while ((n = fread(buf, 1, sizeof buf, a)) > 0)
                    fwrite(buf, 1, n, b);
            }
            if (a) fclose(a);
            if (b) fclose(b);
            ok = bls_save(st, &u, err, sizeof err);
        }
        blf_close(fs);
        bls_free(&u);
    }
    free(img);
    start_unit(g_kind);                         /* switched on again */
    if (cancelled) {
        set_status(1, "Nothing was imported. The unit's files are as they were.");
    } else if (!ok) {
        snprintf(summary, sizeof summary, "Nothing was imported: %s\n\nThe unit was restarted with its files as "
                 "they were.", err);
        report_text(msg, sizeof msg, summary, &r);
        message(GTK_MESSAGE_ERROR, "Import files", msg);
    } else {
        snprintf(summary, sizeof summary, "Imported from %s: %d added, %d rewritten, %d moved, %d deleted, %d "
                 "unchanged, %d kept, %d skipped%s. The unit was restarted. (The unit as it was before is kept in "
                 "%s.before-import.)", path, r.added, r.replaced, r.moved, r.deleted, r.unchanged, r.kept, r.skipped,
                 r.folders_added ? ", new folders made" : "", st);
        report_text(msg, sizeof msg, summary, &r);
        set_status(0, "Imported from %s: %d added, %d rewritten, %d deleted.", path, r.added, r.replaced, r.deleted);
        message(r.skipped || r.kept ? GTK_MESSAGE_WARNING : GTK_MESSAGE_INFO, "Import files", msg);
    }
    blx_report_free(&r);
}

/* ---- the keyboard -------------------------------------------------------------------------------------------------- */
/* the Braille Lite's actions waiting in g_blk, to the unit (under the lock) */
static void blk_apply(void)
{
    bl_action a[BLK_MAX_ACTIONS];
    char name[48];
    int n = blk_take(&g_blk, a, BLK_MAX_ACTIONS), i;
    for (i = 0; i < n; i++) {
        trace("%.2f %s 0x%02X %s", g_unit ? emu_time(g_unit) : 0.0, a[i].type == BLA_CHORD ? "chord" : "held",
              a[i].bits, a[i].type == BLA_CHORD ? blk_chord_name(a[i].bits, name) : "");
        if (!g_unit || emu_kind(g_unit) != EMU_BRAILLE_LITE)
            continue;
        if (a[i].type == BLA_CHORD)
            emu_key(g_unit, a[i].bits);
        else
            emu_keys_down(g_unit, a[i].bits);
    }
}

static void tns_send(int code)
{
    trace("%.2f tns 0x%02X", g_unit ? emu_time(g_unit) : 0.0, code);
    if (g_unit && emu_kind(g_unit) == EMU_TYPE_N_SPEAK)
        emu_key(g_unit, code);
}

/* this program's menu, opened as Alt and the menu's letter opens it (the unit's held keys let go first) */
static void open_menu(void)
{
    release_keys();
    trace("menu");
    gtk_widget_mnemonic_activate(g_firmware_item, FALSE);
}

/* A key going down or up, at the window, before GTK's own mnemonics and accelerators: X's and Wayland's key codes are
   Linux's input codes plus 8, so a key is named by its place, as an input device's (evdev_linux.c) -- the same keys
   whatever the layout.  TRUE: the unit's (or the menu key), FALSE: GTK's. */
static gboolean on_key(GtkWidget *w, GdkEventKey *ev, gpointer data)
{
    key_event e;
    int down = ev->type == GDK_KEY_PRESS, tns, code;
    (void)w; (void)data;
    if (gtk_window_get_focus(GTK_WINDOW(g_win)) != g_area)
        return FALSE;
    e.key = ev->hardware_keycode >= 8 ? evdev_key_of(ev->hardware_keycode - 8) : 0;
    e.mods = (ev->state & GDK_SHIFT_MASK ? KM_SHIFT : 0) | (ev->state & GDK_CONTROL_MASK ? KM_CTRL : 0)
           | (ev->state & GDK_MOD1_MASK ? KM_ALT : 0);
    e.type = KE_PRESS;
    if (!e.key)
        return FALSE;
    tns = KINDS[g_kind].kind == EMU_TYPE_N_SPEAK;
    if (down && key_in_list(&e, tns ? g_menu_tns : g_menu_bl, tns ? g_n_menu_tns : g_n_menu_bl)) {
        open_menu();
        return TRUE;
    }
    e.type = down ? KE_DOWN : KE_UP;
    if (!tns) {
        int taken;
        /* with Alt or Ctrl a key is the program's (Alt+F: the Firmware menu; Ctrl+Q: exit), as on Windows */
        if (down && (e.mods & (KM_ALT | KM_CTRL)))
            return FALSE;
        pthread_mutex_lock(&g_lock);
        taken = blk_event(&g_blk, &e, mono());
        blk_apply();
        pthread_mutex_unlock(&g_lock);
        return taken ? TRUE : FALSE;
    }
    code = tns_code_of(e.key);
    if (!code)
        return FALSE;                           /* a key the unit does not have: GTK's */
    pthread_mutex_lock(&g_lock);
    if (down && !g_tns_held[code & 0x7F]) {     /* the first down only: auto-repeat is the unit's business */
        g_tns_held[code & 0x7F] = 1;
        tns_send(code);
    } else if (!down && g_tns_held[code & 0x7F]) {
        g_tns_held[code & 0x7F] = 0;
        tns_send(code & 0x7F);
    }
    pthread_mutex_unlock(&g_lock);
    return TRUE;                                /* nothing of the unit's keyboard reaches GTK (no mnemonics on Alt) */
}

static gboolean on_area_focus_out(GtkWidget *w, GdkEventFocus *ev, gpointer data)
{
    (void)w; (void)ev; (void)data;
    trace("focus out");
    release_keys();                             /* a chord half-pressed is dropped, the keys held come up */
    return FALSE;
}

static gboolean on_area_focus_in(GtkWidget *w, GdkEventFocus *ev, gpointer data)
{
    (void)w; (void)ev; (void)data;
    trace("focus in");
    return FALSE;
}

static gboolean on_area_draw(GtkWidget *w, cairo_t *cr, gpointer data)
{
    GtkStyleContext *sc = gtk_widget_get_style_context(w);
    int width = gtk_widget_get_allocated_width(w), height = gtk_widget_get_allocated_height(w);
    PangoLayout *l = gtk_widget_create_pango_layout(w, g_area_text);
    (void)data;
    gtk_render_background(sc, cr, 0, 0, width, height);
    pango_layout_set_width(l, (width > 24 ? width - 24 : 1) * PANGO_SCALE);
    pango_layout_set_wrap(l, PANGO_WRAP_WORD);
    gtk_render_layout(sc, cr, 12, 12, l);
    g_object_unref(l);
    if (gtk_widget_has_visible_focus(w))
        gtk_render_focus(sc, cr, 3, 3, width - 6, height - 6);
    return FALSE;
}

static gboolean on_area_click(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
    (void)ev; (void)data;
    gtk_widget_grab_focus(w);
    return TRUE;
}

/* ---- the menu -------------------------------------------------------------------------------------------------- */
/* the menu's marks from the state (the program setting them is not a choice: g_updating) */
static void sync_menu(void)
{
    int k;
    if (!g_bar)
        return;
    g_updating = 1;
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(g_kind_items[g_kind]), TRUE);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(g_idle_items[g_whine]), TRUE);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(g_open_items[g_keep_open]), TRUE);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(g_popclick_item), g_popclick);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(g_tick_item), g_tick);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(g_quick_item), g_quick);
    for (k = 0; k < N_RATES; k++)
        if (RATES[k] == g_rate)
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(g_rate_items[k]), TRUE);
    if (g_buffer_items[g_buffer_mode])
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(g_buffer_items[g_buffer_mode]), TRUE);
    g_updating = 0;
}

/* a menu choice runs once the menu has closed (its dialogs then come up over the window, and a screen reader's
   "click" returns at once) */
typedef struct { void (*fn)(int); int arg; } deferred;

static gboolean run_deferred(gpointer p)
{
    deferred *d = (deferred *)p;
    d->fn(d->arg);
    g_free(d);
    focus_area();
    return G_SOURCE_REMOVE;
}

static void defer(void (*fn)(int), int arg)
{
    deferred *d = g_new(deferred, 1);
    d->fn = fn;
    d->arg = arg;
    g_idle_add(run_deferred, d);
}

/* a radio item's choice: only the one becoming active, and only when the person chose it */
static int chosen(GtkWidget *item)
{
    return !g_updating && gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item));
}

static void do_unit(int k)
{
    if (k == g_kind && g_unit)
        return;
    if (start_unit(k))
        set_status(1, "Switched to the %s.", KINDS[k].name);
}

static void do_factory(int unused)
{
    char path[PATH_MAX], q[300];
    emu_unit *old;
    (void)unused;
    snprintf(q, sizeof q, "Put the %s back as it left the factory? Its files and settings are erased.",
             KINDS[g_kind].name);
    if (!ask_yes_no("Back to the factory state", q))
        return;
    release_keys();
    pthread_mutex_lock(&g_lock);
    old = g_unit;
    g_unit = NULL;                              /* so start_unit saves nothing over the deletion */
    pthread_mutex_unlock(&g_lock);
    emu_destroy(old);
    saved_path(g_kind, path, sizeof path);
    unlink(path);
    if (start_unit(g_kind))
        set_status(1, "The %s is as it left the factory.", KINDS[g_kind].name);
}

static void do_export(int unused) { (void)unused; release_keys(); export_files(); }
static void do_import(int unused) { (void)unused; release_keys(); import_files(); }

static void do_rate(int r)
{
    if (r == g_rate)
        return;
    if (!audio_stop()) {
        set_status(1, "The sound device does not answer; the sample rate stays %d Hz.", g_rate);
        sync_menu();
        return;
    }
    g_rate = r;                                 /* the card reopened at it, the unit restarted at it (memory kept) */
    start_unit(g_kind);
    audio_start();
    save_settings();
    set_status(1, "Sample rate %d Hz.", g_rate);
}

static void do_serial(int k)
{
    char name[PATH_MAX];
    if (k == -1)
        snprintf(name, sizeof name, "none");
    else if (k == -2)
        snprintf(name, sizeof name, "pty");
    else if (k >= 0 && k < g_n_ports)
        snprintf(name, sizeof name, "%s", g_ports[k]);
    else
        return;
    set_serial(name, 0);
    save_settings();
}

static void do_keys(int unused)
{
    char text[4000];
    (void)unused;
    snprintf(text, sizeof text,
             "Braille Lite, while the keyboard area has the focus:\n\n"
             "F D S = dots 1 2 3\nJ K L = dots 4 5 6\nSpace bar = space\n; = advance bar, A = back bar\n\n"
             "Press the keys of a chord together; it goes to the unit when you let go of them.\n"
             "Keys held while the unit starts are read as it starts: p-chord, l restarts it; hold i-chord at once "
             "for the cold reset.\n"
             "The keys can be changed in %s, section [keys].\n"
             "Alt and a letter open this program's menus (Alt+F Firmware, Alt+E Settings, Alt+H Help); Ctrl+Q exits.\n\n"
             "Type 'n Speak: the whole keyboard is the unit's, Alt and the function keys included.\n\n"
             "F11 or Alt+Shift+F opens this program's menu, whichever unit runs.\n"
             "With Orca, its own keys (the Orca key, Insert or Caps Lock, and the keys pressed with it) stay Orca's "
             "and do not reach the unit.\n\n%s", g_ini_path, TNS_FIRST_START);
    message(GTK_MESSAGE_INFO, "Keys", text);
}

static void do_about(int unused)
{
    (void)unused;
    message(GTK_MESSAGE_INFO, "About", "Blazie emulator, part of ssi263-speech.\n\n"
            "It runs Blazie Engineering's own firmware on an emulated board with an emulated SSI-263 speech chip. "
            "The firmware is shared with permission. This program is not a product of Blazie Engineering.\n\n"
            "Licence: MIT (see LICENSE beside the program), with Casso's MIT notice, which the chip model draws on. "
            "The Z180 core is MAME's and keeps its BSD-3-Clause licence. Both notices are in the licenses folder. "
            "GTK, the window's toolkit, is the system's own library, used as it is installed.");
}

static void on_kind_item(GtkWidget *item, gpointer k)
{
    if (chosen(item))
        defer(do_unit, GPOINTER_TO_INT(k));
}

static void on_idle_item(GtkWidget *item, gpointer k)
{
    if (!chosen(item))
        return;
    g_whine = GPOINTER_TO_INT(k);
    apply_idle();
    save_settings();
    set_status(1, "Idle channel: %s.", IDLE_TEXT[g_whine]);
}

static void on_open_item(GtkWidget *item, gpointer k)
{
    if (!chosen(item))
        return;
    g_keep_open = GPOINTER_TO_INT(k);
    apply_idle();
    save_settings();
    set_status(1, "Keep the channel open: %s.", OPEN_TEXT[g_keep_open]);
}

static void on_toggle_item(GtkWidget *item, gpointer which)
{
    int on = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item));
    if (g_updating)
        return;
    switch (GPOINTER_TO_INT(which)) {
    case 0:
        g_popclick = on;
        apply_idle();
        set_status(1, "The pop and the click: %s.", on ? "on" : "off");
        break;
    case 1:
        g_tick = on;
        apply_idle();
        set_status(1, "The 10 Hz tick: %s.", on ? "on" : "off");
        break;
    default:
        g_quick = on;
        pthread_mutex_lock(&g_lock);
        if (g_unit)
            emu_set_quick(g_unit, g_quick);
        pthread_mutex_unlock(&g_lock);
        set_status(1, "Quick key response %s.", on ? "on" : "off");
        break;
    }
    save_settings();
}

static void on_rate_item(GtkWidget *item, gpointer k)
{
    if (chosen(item))
        defer(do_rate, RATES[GPOINTER_TO_INT(k)]);
}

/* the sound thread takes it at its next block: no reopening, no gap (audio_linux.h) */
static void on_buffer_item(GtkWidget *item, gpointer k)
{
    if (!chosen(item))
        return;
    __atomic_store_n(&g_buffer_mode, GPOINTER_TO_INT(k), __ATOMIC_RELEASE);
    save_settings();
    {
        char text[64];
        buffer_text(GPOINTER_TO_INT(k), 0, text, sizeof text);
        set_status(1, "Sound buffer: %s.", text);
    }
}

static void on_serial_item(GtkWidget *item, gpointer k)
{
    if (chosen(item))
        defer(do_serial, GPOINTER_TO_INT(k));
}

static void on_plain_item(GtkWidget *item, gpointer fn)
{
    (void)item;
    defer((void (*)(int))fn, 0);
}

static void on_exit_item(GtkWidget *item, gpointer data)
{
    (void)item; (void)data;
    gtk_widget_destroy(g_win);
}

/* the serial ports, listed afresh each time the Settings menu opens (a USB adapter plugged in meanwhile shows up) */
static void fill_serial_menu(void)
{
    GList *children = gtk_container_get_children(GTK_CONTAINER(g_serial_menu)), *c;
    GSList *group = NULL;
    GtkWidget *item, *current = NULL;
    unsigned i;
    int k;
    for (c = children; c; c = c->next)
        gtk_widget_destroy(GTK_WIDGET(c->data));
    g_list_free(children);
    g_updating = 1;
    item = gtk_radio_menu_item_new_with_mnemonic(group, "_None (not connected)");
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
    g_signal_connect(item, "activate", G_CALLBACK(on_serial_item), GINT_TO_POINTER(-1));
    gtk_menu_shell_append(GTK_MENU_SHELL(g_serial_menu), item);
    if (!g_tty)
        current = item;
    g_n_ports = 0;
    for (i = 0; i < sizeof SERIAL_PATTERNS / sizeof SERIAL_PATTERNS[0]; i++)
        for (k = 0; k < 8 && g_n_ports < MAX_PORTS; k++) {
            snprintf(g_ports[g_n_ports], sizeof g_ports[0], SERIAL_PATTERNS[i], k);
            if (!exists(g_ports[g_n_ports]))
                continue;
            item = gtk_radio_menu_item_new_with_label(group, g_ports[g_n_ports]);
            group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
            g_signal_connect(item, "activate", G_CALLBACK(on_serial_item), GINT_TO_POINTER(g_n_ports));
            gtk_menu_shell_append(GTK_MENU_SHELL(g_serial_menu), item);
            if (g_tty && !strcmp(g_serial_want, g_ports[g_n_ports]))
                current = item;
            g_n_ports++;
        }
    item = gtk_radio_menu_item_new_with_mnemonic(group, "_Pseudo-terminal (for a program on this machine)");
    g_signal_connect(item, "activate", G_CALLBACK(on_serial_item), GINT_TO_POINTER(-2));
    gtk_menu_shell_append(GTK_MENU_SHELL(g_serial_menu), item);
    if (g_tty && !strcmp(g_serial_want, "pty"))
        current = item;
    if (current)
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(current), TRUE);
    gtk_widget_show_all(g_serial_menu);
    g_updating = 0;
}

static void on_settings_show(GtkWidget *menu, gpointer data)
{
    (void)menu; (void)data;
    fill_serial_menu();
}

static GtkWidget *add_item(GtkWidget *menu, const char *label, GCallback cb, gpointer data)
{
    GtkWidget *item = gtk_menu_item_new_with_mnemonic(label);
    if (cb)
        g_signal_connect(item, "activate", cb, data);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    return item;
}

static void add_separator(GtkWidget *menu)
{
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
}

static GtkWidget *add_submenu(GtkWidget *bar, const char *label, GtkWidget **item_out)
{
    GtkWidget *item = gtk_menu_item_new_with_mnemonic(label), *menu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), item);
    if (item_out)
        *item_out = item;
    return menu;
}

static GtkWidget *make_menu(GtkAccelGroup *accel)
{
    GtkWidget *bar = gtk_menu_bar_new(), *fw, *settings, *help, *rates, *buffer, *item, *serial_item;
    GSList *group = NULL;
    int k;
    fw = add_submenu(bar, "_Firmware", &g_firmware_item);
    for (k = 0; k < N_KINDS; k++) {
        g_kind_items[k] = gtk_radio_menu_item_new_with_mnemonic(group, KIND_MENU[k]);
        group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(g_kind_items[k]));
        g_signal_connect(g_kind_items[k], "activate", G_CALLBACK(on_kind_item), GINT_TO_POINTER(k));
        if (kind_offered(k))
            gtk_menu_shell_append(GTK_MENU_SHELL(fw), g_kind_items[k]);
        else                                    /* not in the menu (a hidden item is still in the accessible tree), */
            g_object_ref_sink(g_kind_items[k]); /* kept: g_kind names it after a --unit whose firmware is missing */
    }
    add_separator(fw);
    add_item(fw, "Exp_ort files to disk image (.img)...", G_CALLBACK(on_plain_item), (gpointer)do_export);
    add_item(fw, "_Import files from disk image (.img)...", G_CALLBACK(on_plain_item), (gpointer)do_import);
    add_separator(fw);
    add_item(fw, "Back to the _factory state (erases this unit's files)...", G_CALLBACK(on_plain_item),
             (gpointer)do_factory);
    item = add_item(fw, "E_xit", G_CALLBACK(on_exit_item), NULL);
    gtk_widget_add_accelerator(item, "activate", accel, GDK_KEY_q, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

    settings = add_submenu(bar, "S_ettings", NULL);
    g_signal_connect(settings, "show", G_CALLBACK(on_settings_show), NULL);
    group = NULL;
    {
        static const int ORDER[] = {3, 1, 2, 0};   /* as the unit, hiss, whine, silent: Windows' order */
        for (k = 0; k < 4; k++) {
            int w = ORDER[k];
            g_idle_items[w] = gtk_radio_menu_item_new_with_mnemonic(group, IDLE_MENU[w]);
            group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(g_idle_items[w]));
            g_signal_connect(g_idle_items[w], "activate", G_CALLBACK(on_idle_item), GINT_TO_POINTER(w));
            gtk_menu_shell_append(GTK_MENU_SHELL(settings), g_idle_items[w]);
        }
    }
    add_separator(settings);
    group = NULL;
    for (k = 0; k < 3; k++) {
        g_open_items[k] = gtk_radio_menu_item_new_with_mnemonic(group, OPEN_MENU[k]);
        group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(g_open_items[k]));
        g_signal_connect(g_open_items[k], "activate", G_CALLBACK(on_open_item), GINT_TO_POINTER(k));
        gtk_menu_shell_append(GTK_MENU_SHELL(settings), g_open_items[k]);
    }
    add_separator(settings);
    g_popclick_item = gtk_check_menu_item_new_with_mnemonic("The _pop when the channel opens, the click when it "
                                                            "clicks off");
    g_signal_connect(g_popclick_item, "toggled", G_CALLBACK(on_toggle_item), GINT_TO_POINTER(0));
    gtk_menu_shell_append(GTK_MENU_SHELL(settings), g_popclick_item);
    g_tick_item = gtk_check_menu_item_new_with_mnemonic("The 10 Hz _tick of the open channel");
    g_signal_connect(g_tick_item, "toggled", G_CALLBACK(on_toggle_item), GINT_TO_POINTER(1));
    gtk_menu_shell_append(GTK_MENU_SHELL(settings), g_tick_item);
    add_separator(settings);
    g_quick_item = gtk_check_menu_item_new_with_mnemonic("_Quick key response (faster than the real unit)");
    g_signal_connect(g_quick_item, "toggled", G_CALLBACK(on_toggle_item), GINT_TO_POINTER(2));
    gtk_menu_shell_append(GTK_MENU_SHELL(settings), g_quick_item);
    add_separator(settings);
    rates = add_submenu(settings, "Sample _rate", NULL);
    group = NULL;
    for (k = 0; k < N_RATES; k++) {
        char label[32];
        snprintf(label, sizeof label, "%d Hz", RATES[k]);
        g_rate_items[k] = gtk_radio_menu_item_new_with_label(group, label);
        group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(g_rate_items[k]));
        g_signal_connect(g_rate_items[k], "activate", G_CALLBACK(on_rate_item), GINT_TO_POINTER(k));
        gtk_menu_shell_append(GTK_MENU_SHELL(rates), g_rate_items[k]);
    }
    /* the queue at the sound card (audio_pace.h): a key's answer is heard that much later; too short, it chops */
    buffer = add_submenu(settings, "Sound _buffer", NULL);
    group = NULL;
    for (k = 0; k < AP_N_MODES; k++) {
        char label[96];
        if (!AP_OFFERED(k))                     /* no short: 40 and 50 ms broke up on a ROG Ally (audio_pace.h) */
            continue;
        buffer_text(k, 1, label, sizeof label);
        g_buffer_items[k] = gtk_radio_menu_item_new_with_mnemonic(group, label);
        group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(g_buffer_items[k]));
        g_signal_connect(g_buffer_items[k], "activate", G_CALLBACK(on_buffer_item), GINT_TO_POINTER(k));
        gtk_menu_shell_append(GTK_MENU_SHELL(buffer), g_buffer_items[k]);
    }
    g_serial_menu = add_submenu(settings, "Serial _port", &serial_item);

    help = add_submenu(bar, "_Help", NULL);
    add_item(help, "_Keys", G_CALLBACK(on_plain_item), (gpointer)do_keys);
    add_item(help, "_About", G_CALLBACK(on_plain_item), (gpointer)do_about);
    return bar;
}

/* ---- the window and the program's life ------------------------------------------------------------------------- */
static gboolean on_autosave(gpointer data)
{
    (void)data;
    if (g_audio_running)                        /* every minute: nothing is lost if the program is ended -- by the */
        __atomic_store_n(&g_save_due, 1, __ATOMIC_RELEASE);   /* sound thread, between two blocks */
    else
        save_unit();
    return G_SOURCE_CONTINUE;
}

static gboolean on_unix_signal(gpointer data)
{
    (void)data;
    if (g_win)
        gtk_widget_destroy(g_win);              /* closed as by Exit: the memory saved on the way out */
    return G_SOURCE_CONTINUE;
}

static void on_query_end(GApplication *app, gpointer data)
{
    (void)app; (void)data;
    save_unit();                                /* the session is ending (logging out, shutting down) */
    save_settings();
}

static void on_destroy(GtkWidget *w, gpointer data)
{
    (void)w; (void)data;
    g_win = g_area = g_status = g_bar = NULL;
}

static int g_start_kind;

static void on_activate(GtkApplication *app, gpointer data)
{
    GtkWidget *box, *frame;
    GtkAccelGroup *accel;
    (void)data;
    if (g_win) {
        gtk_window_present(GTK_WINDOW(g_win));
        return;
    }
    /* F10 is a Type 'n Speak key: the menu bar's own key is F11, as this program's */
    g_object_set(gtk_settings_get_default(), "gtk-menu-bar-accel", "F11", NULL);
    g_win = gtk_application_window_new(app);
    gtk_window_set_default_size(GTK_WINDOW(g_win), 520, 260);
    /* first: the unit's keys are taken here before GTK's mnemonics, accelerators and the menu bar's own key */
    g_signal_connect(g_win, "key-press-event", G_CALLBACK(on_key), NULL);
    g_signal_connect(g_win, "key-release-event", G_CALLBACK(on_key), NULL);
    g_signal_connect(g_win, "destroy", G_CALLBACK(on_destroy), NULL);
    accel = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(g_win), accel);
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(g_win), box);
    g_bar = make_menu(accel);
    gtk_box_pack_start(GTK_BOX(box), g_bar, FALSE, FALSE, 0);

    g_area = gtk_drawing_area_new();
    gtk_widget_set_can_focus(g_area, TRUE);
    gtk_widget_add_events(g_area, GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK | GDK_FOCUS_CHANGE_MASK |
                          GDK_BUTTON_PRESS_MASK);
    gtk_widget_set_size_request(g_area, 320, 140);
    g_signal_connect(g_area, "draw", G_CALLBACK(on_area_draw), NULL);
    g_signal_connect(g_area, "focus-out-event", G_CALLBACK(on_area_focus_out), NULL);
    g_signal_connect(g_area, "focus-in-event", G_CALLBACK(on_area_focus_in), NULL);
    g_signal_connect(g_area, "button-press-event", G_CALLBACK(on_area_click), NULL);
    atk_object_set_role(gtk_widget_get_accessible(g_area), ATK_ROLE_PANEL);
    frame = gtk_frame_new(NULL);
    gtk_container_add(GTK_CONTAINER(frame), g_area);
    gtk_box_pack_start(GTK_BOX(box), frame, TRUE, TRUE, 0);

    g_status = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(g_status), TRUE);
    gtk_label_set_xalign(GTK_LABEL(g_status), 0.0f);
    gtk_widget_set_margin_start(g_status, 6);
    gtk_widget_set_margin_end(g_status, 6);
    gtk_widget_set_margin_top(g_status, 4);
    gtk_widget_set_margin_bottom(g_status, 4);
    atk_object_set_role(gtk_widget_get_accessible(g_status), ATK_ROLE_STATUSBAR);
    gtk_box_pack_start(GTK_BOX(box), g_status, FALSE, FALSE, 0);

    sync_menu();
    fill_serial_menu();
    update_names();
    gtk_widget_show_all(g_win);
    gtk_widget_grab_focus(g_area);
    while (gtk_events_pending())
        gtk_main_iteration();
    if (start_unit(g_start_kind))
        set_status(0, "%s is on. F11 or Alt+Shift+F opens the menu.", KINDS[g_kind].name);
    audio_start();
    if (strcmp(g_serial_want, "none") && g_serial_want[0]) {
        char p[PATH_MAX];
        snprintf(p, sizeof p, "%s", g_serial_want);
        set_serial(p, 1);                       /* the port chosen last time */
    }
    g_timeout_add_seconds(AUTOSAVE_S, on_autosave, NULL);
    gtk_window_present(GTK_WINDOW(g_win));
    focus_area();
}

static void usage(void)
{
    printf("blazie_emu_gtk -- a Blazie Braille Lite 2000, Braille 'n Speak 2000 or Type 'n Speak, running its own\n"
           "firmware, in a window.\n\n"
           "  blazie_emu_gtk [--unit bl-en|bl-es|tns-en|tns-es|bns-en|bns-sk] [--firmware DIR] [--config DIR]\n"
           "                 [--no-sound]\n"
           "  blazie_emu_gtk --help\n\n"
           "For the tests: --rate HZ, --trace FILE (the unit's level, its keys and the status lines), --ram-has TEXT\n"
           "(traced when the unit's memory first holds it).\n\n"
           "F11 or Alt+Shift+F opens the menu. README-linux.md has the rest.\n");
}

int main(int argc, char **argv)
{
    const char *unit_id = NULL, *fw_given = NULL, *cfg_given = NULL, *trace_path = NULL;
    char v[64];
    int i, kind = -1, rate = 0, status;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i], *val = i + 1 < argc ? argv[i + 1] : NULL;
#define TAKE() (i++, val)
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
        else if (!strcmp(a, "--unit") && val) unit_id = TAKE();
        else if (!strcmp(a, "--firmware") && val) fw_given = TAKE();
        else if (!strcmp(a, "--config") && val) cfg_given = TAKE();
        else if (!strcmp(a, "--no-sound")) g_no_sound = 1;
        else if (!strcmp(a, "--rate") && val) rate = atoi(TAKE());
        else if (!strcmp(a, "--trace") && val) trace_path = TAKE();
        else if (!strcmp(a, "--ram-has") && val && g_n_ram_has < MAX_RAM_HAS) g_ram_has[g_n_ram_has++] = TAKE();
        else {
            fprintf(stderr, "blazie_emu_gtk: %s not understood (--help)\n", a);
            return 2;
        }
#undef TAKE
    }
    if (getenv("BLAZIE_KEYS_BREAK"))
        blk_break = 1;                          /* the tests' control: dots 1 and 4 swapped */
    if (getenv("BLAZIE_GTK_BREAK") && !strcmp(getenv("BLAZIE_GTK_BREAK"), "noname"))
        g_break_name = 1;                       /* the tests' control: the keyboard area unnamed */
    if (trace_path && !(g_trace = fopen(trace_path, "w"))) {
        fprintf(stderr, "blazie_emu_gtk: could not write %s\n", trace_path);
        return 2;
    }

    /* the settings and the units' memory: the terminal shell's folder, ~/.config/ssi263-speech/blazie-emu */
    if (cfg_given)
        snprintf(g_cfg_dir, sizeof g_cfg_dir, "%s", cfg_given);
    else if (getenv("XDG_CONFIG_HOME") && *getenv("XDG_CONFIG_HOME"))
        snprintf(g_cfg_dir, sizeof g_cfg_dir, "%s/ssi263-speech/blazie-emu", getenv("XDG_CONFIG_HOME"));
    else
        snprintf(g_cfg_dir, sizeof g_cfg_dir, "%s/.config/ssi263-speech/blazie-emu",
                 getenv("HOME") ? getenv("HOME") : ".");
    snprintf(g_ini_path, sizeof g_ini_path, "%s/blazie_emu.ini", g_cfg_dir);
    make_dirs(g_cfg_dir);
    if (!exists(g_ini_path)) {                  /* the first run: the settings file, explained */
        FILE *f = fopen(g_ini_path, "w");
        if (f) {
            fputs(DEFAULT_INI, f);
            fclose(f);
        }
    }
    g_ini = ini_load(g_ini_path);
    if (!g_ini)
        return 1;
    load_settings();
    load_keys();
    if (rate) {
        for (i = 0; i < N_RATES && RATES[i] != rate; i++) {}
        if (i == N_RATES) {
            fprintf(stderr, "blazie_emu_gtk: --rate 11025, 16000, 22050, 32000, 44100 or 48000\n");
            return 2;
        }
        g_rate = rate;
    }
    find_firmware(fw_given);
    snprintf(v, sizeof v, "%s", ini_get(g_ini, "unit", "kind", "english"));
    for (i = 0; i < N_KINDS; i++)
        if (unit_id ? !strcmp(unit_id, KINDS[i].id) : !strcmp(v, KINDS[i].ini))
            kind = i;
    if (kind >= 0 && !unit_id && !kind_offered(kind))
        kind = -1;                              /* the unit used last is not here now (its firmware moved): bl-en */
    if (kind < 0) {
        if (unit_id) {
            fprintf(stderr, "blazie_emu_gtk: --unit bl-en, bl-es, tns-en, tns-es, bns-en or bns-sk\n");
            return 2;
        }
        kind = 0;
    }
    g_start_kind = g_kind = kind;
    snprintf(g_serial_want, sizeof g_serial_want, "%s", ini_get(g_ini, "serial", "port", "none"));
    snprintf(g_serial_shown, sizeof g_serial_shown, "none");
    signal(SIGPIPE, SIG_IGN);

    g_set_prgname("blazie_emu_gtk");
    g_set_application_name("Blazie emulator");
    g_app = gtk_application_new("org.ssi263speech.BlazieEmulator", G_APPLICATION_NON_UNIQUE);
    g_object_set(g_app, "register-session", TRUE, NULL);   /* query-end: the session is ending */
    g_signal_connect(g_app, "activate", G_CALLBACK(on_activate), NULL);
    g_signal_connect(g_app, "query-end", G_CALLBACK(on_query_end), NULL);
    g_unix_signal_add(SIGTERM, on_unix_signal, NULL);
    g_unix_signal_add(SIGHUP, on_unix_signal, NULL);
    g_unix_signal_add(SIGINT, on_unix_signal, NULL);
    status = g_application_run(G_APPLICATION(g_app), 1, argv);   /* our options are read above */

    /* switched off: the serial port closed (the setting kept), the sound stopped, the memory kept */
    set_serial("none", 1);
    audio_stop();
    save_unit();
    save_settings();
    {
        emu_unit *u;
        pthread_mutex_lock(&g_lock);            /* a sound thread left stuck renders nothing from here */
        u = g_unit;
        g_unit = NULL;
        pthread_mutex_unlock(&g_lock);
        emu_destroy(u);
    }
    trace("exit");
    ini_free(g_ini);
    g_object_unref(g_app);
    if (g_trace)
        fclose(g_trace);
    return status;
}
