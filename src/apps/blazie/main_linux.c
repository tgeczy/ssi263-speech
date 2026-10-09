/* main_linux.c -- the Linux shell of the Blazie emulator: the same unit as the Windows app (emu_unit.c: the Braille
 * Lite 2000, the Braille 'n Speak 2000 or the Type 'n Speak running its own firmware), in a terminal.  The Braille 'n
 * Speak 2000 takes the Braille Lite's keys.  For a Raspberry Pi's console, a desktop's
 * terminal, and the BTSpeak (Blazie Technologies' Linux notetaker, on ARM) -- no desktop needed.
 *
 * The keyboard (README-linux.md has it all):
 *   Braille Lite: chords (bl_keys.c) from the terminal -- keys mode, F D S J K L, space, ; and A (the bars; the keys typed
 *   together are one chord), or letters mode (each character typed is its braille cell: the BTSpeak's own braille
 *   keyboard) -- or from the input devices (evdev_linux.c), where keys are seen going down and up.
 *   Type 'n Speak: the whole keyboard (tns_term.c, tns_keys.h).
 *   F11 opens this program's menu (and Ctrl+O for the Braille Lite, Alt+Shift+F as on Windows); its lines are plain
 *   text, read by the console's screen reader (Speakup, Orca, the BTSpeak's own).
 * Sound: ALSA (audio_linux.c), blocks of 10 ms ([sound] block_ms), written by a thread that renders each block from
 * the unit as the card drains one -- the card's clock paces the unit.  How much is kept queued is menu 17, the sound
 * buffer (audio_pace.h, [sound] buffer=, as the Windows app's): automatic (60 ms, growing to at most 250 ms each time
 * the card is found to have run dry), medium (100 ms) or long (250 ms: a remote session, a sound device shared with
 * a screen reader); the draft's short (40 ms) is no longer offered (audio_pace.h).  Without a sound card the unit runs on, silent, paced by the system clock.
 * The serial port (serial_linux.c): a /dev/tty* or a pseudo-terminal.
 * Settings and the units' memory: ~/.config/ssi263-speech/blazie-emu/ (blazie_emu.ini, <unit>.state), saved on exit
 * and every minute.
 *
 * Headless (the tests, tools/linux_tests.sh): --wav FILE or --null renders --seconds of the unit with no sound card
 * and no terminal, typing a --script's keys at their times through the same decoding as the terminal's.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "audio_linux.h"
#include "audio_pace.h"
#include "bl_keys.h"
#include "brl_linux.h"
#include "bt_handover.h"
#include "btkb_linux.h"
#include "emu_unit.h"
#include "evdev_linux.h"
#include "ini.h"
#include "keys.h"
#include "serial_linux.h"
#include "term_keys.h"
#include "tns_rescue.h"
#include "tns_setup.h"
#include "tns_term.h"

#define BLOCK_MS_DEFAULT 10
#define RATE_MAX 48000
#define AUTOSAVE_S 60.0

/* firmware: the folder's layout as the repository's firmware/blazie (spanish/, tns/), or all in one folder (the
   Linux package's share/ssi263-speech) -- the first that exists.  state NULL: a cold start (the Type 'n Speak's cold
   reset asks how to set itself up: tns_setup.h, TNS_FIRST_START). */
typedef struct {
    const char *name, *id;
    int kind;
    const char *firmware[2], *state[2];
    const char *saved, *ini;
} unit_kind;
static const unit_kind KINDS[] = {
    {"Braille Lite 2000 (English)", "bl-en", EMU_BRAILLE_LITE, {"BL2ENG.BNS", NULL}, {"bl2_2003_warm.state", NULL},
     "english.state", "english"},
    {"Braille Lite 2000 (Spanish)", "bl-es", EMU_BRAILLE_LITE, {"spanish/BL2SPA.BNS", "BL2SPA.BNS"},
     {"spanish/bl2spa_fresh.state", "bl2spa_fresh.state"}, "spanish.state", "spanish"},
    {"Type 'n Speak (English)", "tns-en", EMU_TYPE_N_SPEAK, {"tns/TNSENG.TNS", "TNSENG.TNS"}, {NULL, NULL},
     "tns_english.state", "tns_english"},
    {"Type 'n Speak (Spanish)", "tns-es", EMU_TYPE_N_SPEAK, {"tns/TNSSPA.TNS", "TNSSPA.TNS"}, {NULL, NULL},
     "tns_spanish.state", "tns_spanish"},
    /* the Braille 'n Speak 2000 on the Braille Lite's board (emu_unit.h emu_model), in the menu when its firmware is
       there (menu numbers 15 and 16: 5-14 are the settings, 17 the sound buffer); its factory states made by
       make_state (make_state.c) */
    {"Braille 'n Speak 2000 (English)", "bns-en", EMU_BRAILLE_LITE, {"bns2000/BS03ENG.BNS", "BS03ENG.BNS"},
     {"bns2000/bs03eng_fresh.state", "bs03eng_fresh.state"}, "bns_english.state", "bns_english"},
    {"Braille 'n Speak 2000 (Slovak)", "bns-sk", EMU_BRAILLE_LITE, {"bns2000/BS2SLL.BNS", "BS2SLL.BNS"},
     {"bns2000/bs2sll_fresh.state", "bs2sll_fresh.state"}, "bns_slovak.state", "bns_slovak"},
};
#define N_KINDS ((int)(sizeof KINDS / sizeof KINDS[0]))
#define N_FIRST_KINDS 4             /* the menu's 1-4; the kinds after them are 15 on */
#define MENU_LATER_KINDS 15
#define MENU_BUFFER 17              /* the sound buffer: after the settings 5-14 and the kinds 15 on */
/* a later kind must not take the sound buffer's number */
typedef char later_kinds_before_menu_buffer[MENU_LATER_KINDS + N_KINDS - N_FIRST_KINDS <= MENU_BUFFER ? 1 : -1];
static const int RATES[] = {11025, 16000, 22050, 32000, 44100, 48000};
#define N_RATES ((int)(sizeof RATES / sizeof RATES[0]))
static const char *const IDLE_NAMES[] = {"off", "hiss", "whine", "unit"};
static const char *const IDLE_TEXT[] = {"silent", "hiss", "whine", "as the unit (hiss at even volumes, whine at odd)"};
static const char *const OPEN_NAMES[] = {"off", "until", "always"};
static const char *const OPEN_TEXT[] = {"off (silent as soon as speech ends)", "until the unit clicks off", "always"};

/* ---- the state ------------------------------------------------------------------------------------------------- */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static emu_unit *g_unit;
static int g_kind;
static int g_rate = 44100, g_block_ms = BLOCK_MS_DEFAULT;
static int g_whine = 3, g_keep_open = 1, g_popclick = 1, g_tick = 1, g_quick;
static char g_cfg_dir[PATH_MAX], g_ini_path[PATH_MAX], g_fw_dir[PATH_MAX], g_serial[PATH_MAX];
static char g_serial_want[PATH_MAX], g_serial_shown[PATH_MAX + 32];   /* the port, as the menu says it */
       /* the setting: kept when the port is missing at the start */
static ini *g_ini;
static bl_keys g_blk;
static term_dec g_term;
static evdev_set g_ev;
static int g_use_evdev;
static key_combo g_menu_bl[6], g_menu_tns[6];
static int g_n_menu_bl, g_n_menu_tns;
static unsigned char g_tns_held[0x80];           /* the Type 'n Speak's keys down now (an input device's) */
static tty_link *g_tty;
/* the BT Speak's and BT Braille's own keyboard (btkb_linux.h) and a braille display (brl_linux.h) */
static btkb g_bt;
static btkb_keys g_btk;
static int g_bt_want;                            /* [btspeak] keyboard: taken at the start, so again after the menu */
static char g_bt_path[108];
static brl_out g_brl;
static int g_disp_pipe[2] = {-1, -1};            /* the sound thread to the main loop: the unit's display changed */
static unsigned char g_disp_seen[EMU_CELLS];     /* the unit's cells as the sound thread last saw them (its own) */
static int g_disp_n;
static unsigned char g_brl_shown[EMU_CELLS];     /* ... and as last shown on the braille display (the main loop's) */
static int g_brl_n = -1;
static volatile sig_atomic_t g_quit;
static int g_headless, g_print_actions, g_flash_instant, g_no_sound;
static struct termios g_term_saved;
static int g_term_raw;

/* sound */
static audio_out *g_audio;
static pthread_t g_audio_thread;
static int g_audio_running;
static volatile int g_audio_stop;
static int g_save_due;                         /* the minute's save, waiting for the sound thread (__atomic) */
static int g_buffer_mode = AP_AUTO;            /* menu 17, the sound buffer (audio_pace.h; __atomic) */
static int g_buffer_ms;                        /* the queue now, as the sound thread last saw it (__atomic) */

static double mono(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

/* ---- the terminal ------------------------------------------------------------------------------------------------ */
static void term_restore(void)
{
    if (g_term_raw) {
        tcsetattr(0, TCSANOW, &g_term_saved);
        g_term_raw = 0;
    }
}

/* raw: each key as it is typed, nothing echoed, Ctrl+C and Ctrl+S the unit's keys; output still turns \n into a new
   line, so the messages print as lines */
static void term_raw(void)
{
    struct termios t;
    if (!isatty(0) || g_term_raw)
        return;
    if (tcgetattr(0, &g_term_saved) < 0)
        return;
    t = g_term_saved;
    t.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    t.c_iflag &= ~(IXON | ICRNL | INLCR | IGNCR | ISTRIP);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSANOW, &t) == 0)
        g_term_raw = 1;
}

/* a line typed in the terminal's own line mode (echoed, editable), for the menu; "" on end of input */
static void read_line(char *out, int cap)
{
    int was_raw = g_term_raw;
    term_restore();
    tcflush(0, TCIFLUSH);
    out[0] = 0;
    if (fgets(out, cap, stdin)) {
        size_t n = strlen(out);
        while (n && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' '))
            out[--n] = 0;
    } else {
        clearerr(stdin);
    }
    if (was_raw)
        term_raw();
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

/* the first of a kind's two names that is in the firmware folder ("" if neither) */
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
        snprintf(out, (size_t)cap, "%s/%s", g_fw_dir, names[0]);   /* named in the error */
}

/* the menu's number for a kind (1-4, then 15 on), and the kind for a number (-1: none); a kind after the first four
   is offered only when its firmware is there */
static int kind_menu_number(int kind)
{
    return kind < N_FIRST_KINDS ? kind + 1 : MENU_LATER_KINDS + kind - N_FIRST_KINDS;
}

static int kind_offered(int kind)
{
    char fw[PATH_MAX];
    if (kind < N_FIRST_KINDS)
        return 1;
    fw_file(KINDS[kind].firmware, fw, sizeof fw);
    return exists(fw);
}

static int kind_of_menu_number(int n)
{
    int k;
    for (k = 0; k < N_KINDS; k++)
        if (kind_menu_number(k) == n && kind_offered(k))
            return k;
    return -1;
}

/* --firmware, else firmware_dir in the settings, else beside the program: the package's share/ssi263-speech, a
   firmware folder, or the source tree's firmware/blazie (the program in build/linux) */
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

/* ---- the settings file ------------------------------------------------------------------------------------------ */
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

static void load_keys(void)
{
    static const char *const KEYS[] = {"mode", "dot1", "dot2", "dot3", "dot4", "dot5", "dot6", "space", "advance",
                                       "back", "chord_ms", "repeat_ms", "hold"};
    char key[64], value[256];
    unsigned i;
    int j;
    blk_defaults(&g_blk);
    for (i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++) {
        const char *v = ini_get(g_ini, "keys", KEYS[i], NULL);
        if (v) {
            snprintf(value, sizeof value, "%s", v);
            if (!blk_set(&g_blk, KEYS[i], value))
                say("blazie_emu.ini: [keys] %s = %s is not understood; the default is kept", KEYS[i], value);
        }
    }
    for (j = 0; ini_entry(g_ini, "letters", j, key, sizeof key, value, sizeof value); j++)
        if (!blk_set(&g_blk, key, value))
            say("blazie_emu.ini: [letters] %s = %s is not understood", key, value);
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

static void save_settings(void)
{
    char v[16];
    if (g_headless)
        return;
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
    ini_set(g_ini, "keys", "mode", g_blk.mode == BLK_LETTERS ? "letters" : "keys");
    if (!ini_save(g_ini, g_ini_path))
        say("Could not write the settings to %s.", g_ini_path);
}

/* ---- the unit --------------------------------------------------------------------------------------------------- */
/* Written and put in place under the lock: the sound thread's minute save (audio_main) and a save here (switching
   units, leaving) never write the same file at once.  Called from either thread. */
static void save_unit(void)
{
    char path[PATH_MAX], tmp[PATH_MAX + 8];
    int ok = 1;
    pthread_mutex_lock(&g_lock);
    if (g_unit) {
        make_dirs(g_cfg_dir);
        saved_path(g_kind, path, sizeof path);
        snprintf(tmp, sizeof tmp, "%s.new", path);   /* whole or not at all: a crash mid-write keeps the last one */
        ok = emu_save(g_unit, tmp) && rename(tmp, path) == 0;
        if (!ok)
            unlink(tmp);
    }
    pthread_mutex_unlock(&g_lock);
    if (!ok)
        say("Could not save the unit's memory to %s; what you wrote this time is lost.", path);
}

static void apply_idle(void)
{
    pthread_mutex_lock(&g_lock);
    if (g_unit)
        emu_set_idle(g_unit, g_whine, g_keep_open, g_popclick, g_tick);
    pthread_mutex_unlock(&g_lock);
}

/* switches the shell's keys off the old unit: a held chord or key let go */
static void release_keys(void)
{
    bl_action a[BLK_MAX_ACTIONS];
    int n, i, c;
    blk_reset(&g_blk);
    n = blk_take(&g_blk, a, BLK_MAX_ACTIONS);
    pthread_mutex_lock(&g_lock);
    for (i = 0; i < n; i++)
        if (g_unit && emu_kind(g_unit) == EMU_BRAILLE_LITE && a[i].type == BLA_HELD)
            emu_keys_down(g_unit, a[i].bits);
    for (c = 0; c < 0x80; c++)
        if (g_tns_held[c]) {
            if (g_unit && emu_kind(g_unit) == EMU_TYPE_N_SPEAK)
                emu_key(g_unit, c);
            g_tns_held[c] = 0;
        }
    pthread_mutex_unlock(&g_lock);
}

/* A saved Type 'n Speak that was never set up (tns_rescue.h: the 0.6 and 0.7 previews' first start missed the unit's
   cold reset): the person chooses -- set it up now keeping its RAM files, the factory state (its own questions), or
   as it is.  0: start it from no state (the factory's cold start); 1: from st.  Headless: said, and left as it is. */
static int offer_setup(int kind, const char *fw, const char *st)
{
    char line[64], err[300], before[PATH_MAX + 20];
    tns_rescue_report r;
    if (tns_needs_setup(st, &r) != 1)
        return 1;
    snprintf(before, sizeof before, "%s.before-setup", st);
    say("This %s was never set up: its file system and folders were not made when it first started (the 0.6 and "
        "0.7 previews' first start missed the unit's cold reset; or a setup question was answered n). On it a new "
        "file can lose its first letter, and a file moved to flash is lost.", KINDS[kind].name);
    say("Its files: %d in RAM%s.", r.ram_files, r.lost_flash ? ", and some it lost when moving them to flash (their "
        "text was never written: only their names are left)" : "");
    if (g_headless)
        return 1;
    say("Type k to set it up now and keep its RAM files (its settings go back to the factory's), f for the factory "
        "state (it asks its own setup questions; its files are not kept), or Enter alone to start it as it is. "
        "k and f keep the old memory as %s.", before);
    if (g_use_evdev)
        evdev_grab(&g_ev, 0);                   /* the answer is typed in the terminal */
    read_line(line, sizeof line);
    if (g_use_evdev)
        evdev_grab(&g_ev, ini_get_int(g_ini, "input", "grab", 1));
    if (line[0] == 'f') {
        FILE *a = fopen(st, "rb"), *b = fopen(before, "wb");
        char buf[65536];
        size_t n;
        int ok = a && b;
        while (ok && (n = fread(buf, 1, sizeof buf, a)) > 0)
            ok = fwrite(buf, 1, n, b) == n;
        if (a) fclose(a);
        if (b && fclose(b) != 0) ok = 0;
        if (!ok) {
            say("Could not keep the old memory; the unit starts as it is.");
            return 1;
        }
        unlink(st);
        return 0;
    }
    if (line[0] != 'k')
        return 1;
    say("Setting it up (a few seconds) ...");
    if (!tns_rescue(fw, st, 1, &r, err, sizeof err)) {
        say("Could not set it up: %s. It starts as it is.", err);
        return 1;
    }
    say("Set up: %d files kept%s. The old memory is in %s.", r.carried,
        r.first_lost ? " (one had lost its first letter on the old unit)" : "", before);
    fputs(r.log, stdout);
    fflush(stdout);
    return 1;
}

static int start_unit(int kind, const char *state_given)
{
    char fw[PATH_MAX], st[PATH_MAX], err[300];
    const char *state = st;
    emu_unit *u, *old;
    save_unit();                                /* first: the unit being left keeps its memory (it may be this kind) */
    fw_file(KINDS[kind].firmware, fw, sizeof fw);
    if (state_given)
        snprintf(st, sizeof st, "%s", state_given);
    else {
        saved_path(kind, st, sizeof st);
        if (exists(st) && KINDS[kind].kind == EMU_TYPE_N_SPEAK && !offer_setup(kind, fw, st))
            state = NULL;                       /* the factory state: the unit's cold reset, its own questions */
        else if (!exists(st)) {                 /* the first time: the unit as it left the factory */
            if (KINDS[kind].state[0])
                fw_file(KINDS[kind].state, st, sizeof st);
            else
                state = NULL;                   /* a cold start */
        } else if (!g_headless)
            say("Its memory: %s", st);
    }
    if (g_headless)
        say("starting %s from %s", KINDS[kind].id, state ? st : "cold");
    u = emu_create(KINDS[kind].kind, fw, state, g_rate, g_whine, err, sizeof err);
    if (!u) {
        say("Could not start the %s: %s", KINDS[kind].name, err);
        say("The firmware is looked for in %s (firmware_dir in %s, or --firmware).", g_fw_dir, g_ini_path);
        return 0;
    }
    if (g_flash_instant)
        emu_set_flash_timed(u, 0);
    if (g_tty)
        emu_serial_attach(u, 1);                /* the new unit takes the old one's place on the line */
    emu_set_idle(u, g_whine, g_keep_open, g_popclick, g_tick);
    emu_set_quick(u, g_quick);
    release_keys();
    pthread_mutex_lock(&g_lock);
    old = g_unit;
    g_unit = u;
    g_kind = kind;
    pthread_mutex_unlock(&g_lock);
    emu_destroy(old);
    if (!g_headless) {
        say("%s is on.", KINDS[kind].name);
        if (KINDS[kind].kind == EMU_TYPE_N_SPEAK && !state)
            say("%s", TNS_FIRST_START);
        save_settings();
    }
    return 1;
}

/* ---- the sound thread --------------------------------------------------------------------------------------------- */
static void *audio_main(void *arg)
{
    static short buf[RATE_MAX / 50];
    int block = g_rate * g_block_ms / 1000;
    struct timespec next;
    (void)arg;
    clock_gettime(CLOCK_MONOTONIC, &next);
    while (!g_audio_stop) {
        if (g_audio) {                          /* the sound buffer (audio_linux.h): a block when the card wants one */
            int turn = audio_turn(g_audio, __atomic_load_n(&g_buffer_mode, __ATOMIC_ACQUIRE),
                                  __atomic_load_n(&g_save_due, __ATOMIC_ACQUIRE));
            __atomic_store_n(&g_buffer_ms, audio_buffer_ms(g_audio), __ATOMIC_RELEASE);
            if (turn == AUDIO_WAIT)
                continue;
            if (turn == AUDIO_SAVE) {           /* the minute's save, the queue rendered ahead: the card plays it
                                                   while the memory is written (Tomi: the emulator's speech
                                                   stutters, the add-on's doesn't) */
                double t0 = mono();
                __atomic_store_n(&g_save_due, 0, __ATOMIC_RELEASE);
                save_unit();
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
        if (g_unit && g_disp_pipe[1] >= 0) {    /* the unit's display changed: the main loop shows it */
            unsigned char cells[EMU_CELLS];
            int n = emu_braille(g_unit, cells, EMU_CELLS);
            if (n != g_disp_n || memcmp(cells, g_disp_seen, (size_t)n)) {
                g_disp_n = n;
                memcpy(g_disp_seen, cells, (size_t)n);
                if (write(g_disp_pipe[1], "d", 1) != 1) {}
            }
        }
        pthread_mutex_unlock(&g_lock);
        if (g_audio && !audio_write(g_audio, buf, block)) {
            say("The sound device stopped working; the unit runs on, silent.");
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
            if (__atomic_exchange_n(&g_save_due, 0, __ATOMIC_ACQ_REL))
                save_unit();
        }
    }
    return NULL;
}

static void audio_start(void)
{
    char err[300];
    const char *dev = ini_get(g_ini, "sound", "device", "default");
    char device[128];
    snprintf(device, sizeof device, "%s", dev);
    g_audio = g_no_sound ? NULL : audio_open(device, g_rate, g_block_ms, g_buffer_mode, err, sizeof err);
    if (!g_audio && !g_no_sound)
        say("No sound: %s.  The unit runs on, silent ([sound] device in the settings chooses another device).", err);
    g_audio_stop = 0;
    if (pthread_create(&g_audio_thread, NULL, audio_main, NULL) == 0) {
        struct sched_param sp;
        sp.sched_priority = 10;                 /* real-time when allowed (the audio group, rtkit); else as is */
        pthread_setschedparam(g_audio_thread, SCHED_FIFO, &sp);
        g_audio_running = 1;
    }
}

/* 1 when stopped; 0 when the sound thread stays stuck in the device for 2 s (it is left to the end of the program:
   leaving must not hang on a sound server that stopped answering) */
static int audio_stop(void)
{
    if (g_audio_running) {
        struct timespec t;
        g_audio_stop = 1;
        clock_gettime(CLOCK_REALTIME, &t);
        t.tv_sec += 2;
        if (pthread_timedjoin_np(g_audio_thread, NULL, &t) != 0) {
            say("The sound device does not answer.");
            return 0;
        }
        g_audio_running = 0;
    }
    audio_close(g_audio);
    g_audio = NULL;
    return 1;
}

/* ---- the serial port ------------------------------------------------------------------------------------------- */
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
    g_serial[0] = 0;
    snprintf(g_serial_shown, sizeof g_serial_shown, "none");
    if (name[0] && strcmp(name, "none")) {
        t = tty_open(name, other, sizeof other, err, sizeof err);
        if (!t)
            say("The serial port is not connected: %s.", err);
        else {
            snprintf(g_serial, sizeof g_serial, "%s", name);
            snprintf(g_serial_shown, sizeof g_serial_shown, other[0] ? "a pseudo-terminal, its other end %s" : "%s",
                     other[0] ? other : name);
            pthread_mutex_lock(&g_lock);
            if (g_unit)
                emu_serial_attach(g_unit, 1);
            g_tty = t;
            pthread_mutex_unlock(&g_lock);
            if (other[0]) {
                if (symlink(other, link_path) == 0)
                    say("The serial port is on a pseudo-terminal: the other end is %s (also %s).", other, link_path);
                else
                    say("The serial port is on a pseudo-terminal: the other end is %s.", other);
            } else
                say("The serial port is on %s.", name);
        }
    } else if (!at_start)
        say("The serial port is not connected.");
    if (!at_start || t)
        snprintf(g_serial_want, sizeof g_serial_want, "%s", t ? name : "none");
    return t != NULL;
}

/* ---- the BT Speak's keyboard and the braille display ------------------------------------------------------------- */
/* the unit's display to the braille display when it has one (the Braille Lite); the display given back to BRLTTY
   when it has none (the Braille 'n Speak, the Type 'n Speak) */
static void display_refresh(int force)
{
    unsigned char cells[EMU_CELLS];
    int n;
    if (!g_brl.lib)
        return;
    pthread_mutex_lock(&g_lock);
    n = g_unit ? emu_braille(g_unit, cells, EMU_CELLS) : 0;
    pthread_mutex_unlock(&g_lock);
    if (!n) {                                   /* no display (yet): BRLTTY's */
        brl_hold(&g_brl, 0);
        g_brl_n = -1;
        return;
    }
    brl_hold(&g_brl, 1);
    if (!force && n == g_brl_n && !memcmp(cells, g_brl_shown, (size_t)n))
        return;
    g_brl_n = n;
    memcpy(g_brl_shown, cells, (size_t)n);
    if (!brl_show(&g_brl, cells, n)) {
        say("Braille: BRLTTY's connection was lost; the display is BRLTTY's again.");
        brl_close(&g_brl);
    }
}

/* the BT keyboard taken (at the start, and after the menu) */
static int bt_take(int at_start)
{
    char msg[256];
    int absent = 0;
    if (!g_bt_want || g_bt.running)
        return g_bt.running;
    btkb_keys_reset(&g_btk);
    if (btkb_open(&g_bt, g_bt_path, msg, sizeof msg, &absent))
        return 1;
    if (at_start && absent && !strcmp(ini_get(g_ini, "btspeak", "keyboard", "auto"), "auto"))
        g_bt_want = 0;                          /* not a BT Speak or BT Braille: nothing to say */
    else
        say("BT keyboard: %s; the terminal's keys are used.", msg);
    return 0;
}

/* the keys from the BT keyboard: to the unit (key_in), the gesture to the menu; the server gone, the terminal's
   keys are used */
static void key_in(const key_event *e, double now);
static void menu(void);

static void bt_keys(double now)
{
    int codes[64], downs[64], n = btkb_read(&g_bt, codes, downs, 64), i;
    if (n < 0) {
        btkb_close(&g_bt);
        release_keys();
        say("BT keyboard: the keyboard server is gone; the terminal's keys are used.");
        return;
    }
    for (i = 0; i < n && !g_quit; i++) {
        key_event ev[8];
        int action = BTKB_NONE, reset = 0, j;
        int k = btkb_keys_feed(&g_btk, codes[i], downs[i], now, ev, 8, &action, &reset);
        if (reset)
            release_keys();
        for (j = 0; j < k; j++)
            key_in(&ev[j], now);
        if (action == BTKB_MENU) {
            menu();                             /* gives the keyboard back to BRLTTY, and takes it again */
            return;                             /* what was read with the gesture is not the unit's */
        }
        if (action == BTKB_QUIT) {              /* Z-chord with dot 7: saved and switched off as menu 0 does */
            say("Leaving: the unit's memory is saved.");
            g_quit = 1;
            return;
        }
    }
}

static void bt_tick(double now)
{
    key_event ev[BTKB_MAX_DEFER];
    int k = btkb_keys_tick(&g_btk, now, ev, BTKB_MAX_DEFER), j;
    for (j = 0; j < k; j++)
        key_in(&ev[j], now);
}

/* BRLTTY's panning commands (a display without the keyboard server): the unit's bars, tapped */
static void brl_bars(void)
{
    int back = 0, adv = 0;
    if (!brl_keys(&g_brl, &back, &adv)) {
        say("Braille: BRLTTY's connection was lost; the display is BRLTTY's again.");
        brl_close(&g_brl);
        return;
    }
    pthread_mutex_lock(&g_lock);
    if (g_unit && emu_kind(g_unit) == EMU_BRAILLE_LITE) {
        while (back-- > 0) emu_key(g_unit, EMU_BACK);
        while (adv-- > 0) emu_key(g_unit, EMU_ADVANCE);
    }
    pthread_mutex_unlock(&g_lock);
}

/* ---- keys to the unit ----------------------------------------------------------------------------------------- */
static double g_now_for_print;

static void hold_message(int before)
{
    char name[48], keys[64] = "", tmp[16];
    int after = blk_holding(&g_blk), i;
    if (after == before)
        return;
    for (i = 0; i < g_blk.n_hold; i++)
        snprintf(keys + strlen(keys), sizeof keys - strlen(keys), "%s%s%s", i ? " or " : "",
                 g_blk.hold[i].mods & KM_CTRL ? "ctrl-" : "", key_name(g_blk.hold[i].key, tmp));
    if (after == -1)
        say("Hold: the next chord is held down.");
    else if (after > 0)
        say("Holding %s; %s lets it go.", blk_chord_name(after, name), keys);
    else if (before > 0)
        say("Let go.");
    else
        say("Hold off.");
}

/* the Braille Lite's actions waiting in g_blk, to the unit (under the lock) */
static void blk_apply(void)
{
    bl_action a[BLK_MAX_ACTIONS];
    char name[48];
    int n = blk_take(&g_blk, a, BLK_MAX_ACTIONS), i;
    for (i = 0; i < n; i++) {
        if (g_print_actions)
            say("%.3f %s 0x%02X %s", g_now_for_print, a[i].type == BLA_CHORD ? "chord" : "held", a[i].bits,
                a[i].type == BLA_CHORD ? blk_chord_name(a[i].bits, name) : "");
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
    if (g_print_actions)
        say("%.3f tns 0x%02X", g_now_for_print, code);
    if (g_unit && emu_kind(g_unit) == EMU_TYPE_N_SPEAK)
        emu_key(g_unit, code);
}

/* one key, from the terminal or an input device */
static void key_in(const key_event *e, double now)
{
    int tns = KINDS[g_kind].kind == EMU_TYPE_N_SPEAK;
    g_now_for_print = now;
    if (e->type != KE_UP && key_in_list(e, tns ? g_menu_tns : g_menu_bl, tns ? g_n_menu_tns : g_n_menu_bl)) {
        if (g_headless)
            say("%.3f menu", now);
        else
            menu();
        return;
    }
    if (!tns) {
        int before = blk_holding(&g_blk);
        pthread_mutex_lock(&g_lock);
        blk_event(&g_blk, e, now);
        blk_apply();
        pthread_mutex_unlock(&g_lock);
        hold_message(before);
        return;
    }
    pthread_mutex_lock(&g_lock);
    if (e->type == KE_PRESS) {
        unsigned char codes[8];
        int n = tns_press_codes(e, codes, 8), i;
        for (i = 0; i < n; i++)
            tns_send(codes[i]);
    } else {
        int code = tns_code_of(e->key);
        if (code) {
            if (e->type == KE_DOWN && !g_tns_held[code & 0x7F]) {   /* the first down: auto-repeat is the unit's */
                g_tns_held[code & 0x7F] = 1;
                tns_send(code);
            } else if (e->type == KE_UP && g_tns_held[code & 0x7F]) {
                g_tns_held[code & 0x7F] = 0;
                tns_send(code & 0x7F);
            }
        }
    }
    pthread_mutex_unlock(&g_lock);
}

static void keys_tick(double now)
{
    key_event ev[16];
    int n = term_dec_flush(&g_term, now, ev, 16), i;
    for (i = 0; i < n; i++)
        key_in(&ev[i], now);
    g_now_for_print = now;
    pthread_mutex_lock(&g_lock);
    blk_tick(&g_blk, now);
    blk_apply();
    pthread_mutex_unlock(&g_lock);
}

/* ---- the menu ---------------------------------------------------------------------------------------------------- */
static void keys_help(void)
{
    say("Braille Lite, keys mode: F D S = dots 1 2 3, J K L = dots 4 5 6, the space bar, ; = advance bar, A = back bar.");
    say("  Press the keys of a chord together: they are sent as one chord when you stop.");
    say("Braille Lite, letters mode (the BTSpeak's braille keyboard): each character typed is its braille cell.");
    say("  A chord with the space bar: Ctrl+C, then the character; or a capital letter (dot 7): P is p-chord.");
    say("  The BTSpeak's keys for chords are mapped back: Up = dot-1 chord, Enter = e-chord, Esc = 2-6-chord ...");
    say("Hold (F12 or Ctrl+K): the next chord stays held down until you press it again -- the unit reads keys held");
    say("  while it starts: p-chord, hold, i-chord, l restarts it into the cold reset; then hold again.");
    say("  With an input device (evdev; the input group) keys are seen going down and up: no hold key needed.");
    say("BT Speak and BT Braille: their own keys are the Braille Lite's -- dots 1-6 and the space bar; dot 7 is the");
    say("  back bar and dot 8 the advance bar (on a BT Braille L2 or R2 back, L3 or R3 advance); M-chord with dot 7");
    say("  opens this menu, Z-chord with dot 7 saves and leaves.  On a braille display the unit's cells are shown.");
    say("Braille 'n Speak 2000: the Braille Lite's keys, both modes; it has no advance bar.");
    say("Type 'n Speak: the whole keyboard is the unit's.");
    say("%s", TNS_FIRST_START);
    say("F11 opens this menu (Ctrl+O too for the Braille Lite; Alt+Shift+F always). Keys are set in %s.", g_ini_path);
}

static void menu_serial(void)
{
    static const char *const PATTERNS[] = {"/dev/ttyUSB%d", "/dev/ttyACM%d", "/dev/ttyS%d", "/dev/ttyAMA%d"};
    char line[PATH_MAX], p[64];
    unsigned i;
    int k, any = 0;
    say("Serial port, now %s. Ports here:", g_serial_shown);
    for (i = 0; i < sizeof PATTERNS / sizeof PATTERNS[0]; i++)
        for (k = 0; k < 8; k++) {
            snprintf(p, sizeof p, PATTERNS[i], k);
            if (exists(p)) {
                say("  %s", p);
                any = 1;
            }
        }
    if (!any)
        say("  (none found)");
    say("Type a port, pty (a pseudo-terminal for a program on this machine), or none; Enter alone keeps it:");
    read_line(line, sizeof line);
    if (line[0]) {
        set_serial(line, 0);
        save_settings();
    }
}

/* the sound buffer's choices (audio_pace.h), as the Windows app's Settings > Sound buffer: the offered ones only (no
   short: AP_OFFERED), their text from audio_pace.h's own numbers */
static const int BUFFER_MODES[] = {AP_AUTO, AP_MEDIUM, AP_LONG};
#define N_BUFFER_MODES ((int)(sizeof BUFFER_MODES / sizeof BUFFER_MODES[0]))

static void buffer_text(int mode, int brief, char *out, int cap)
{
    if (mode == AP_AUTO)
        snprintf(out, (size_t)cap, brief ? "automatic" : "automatic (%d ms, longer when the sound breaks up)",
                 AP_AUTO_START_MS);
    else if (mode == AP_LONG)
        snprintf(out, (size_t)cap, brief ? "long (%d ms)" : "long (%d ms: a remote session, or a sound device shared "
                 "with a screen reader)", AP_LONG_MS);
    else
        snprintf(out, (size_t)cap, "medium (%d ms)", AP_MEDIUM_MS);
}

static void buffer_line(char *out, int cap)
{
    int ms = __atomic_load_n(&g_buffer_ms, __ATOMIC_ACQUIRE);
    if (g_buffer_mode == AP_AUTO && g_audio_running && ms > 0)
        snprintf(out, (size_t)cap, "automatic, %d ms now", ms);
    else
        buffer_text(g_buffer_mode, 1, out, cap);
}

static int choose(const char *title, const char *const *items, int n, int current)
{
    char line[32];
    int k;
    say("%s:", title);
    for (k = 0; k < n; k++)
        say("  %d %s%s", k + 1, items[k], k == current ? " (now)" : "");
    say("Type its number; Enter alone keeps it:");
    read_line(line, sizeof line);
    k = atoi(line) - 1;
    return k >= 0 && k < n ? k : current;
}

static void menu(void)
{
    char line[64];
    int k, list = 1;
    release_keys();
    if (g_use_evdev)
        evdev_grab(&g_ev, 0);                   /* the menu is typed in the terminal */
    if (g_bt.running) {                         /* the gesture's keys up first: never pressed again for BRLTTY */
        double until = mono() + 3.0;
        while (btkb_keys_busy(&g_btk) && mono() < until) {
            struct pollfd w;
            int codes[16], downs[16], k, j;
            w.fd = btkb_fd(&g_bt);
            w.events = POLLIN;
            if (poll(&w, 1, 50) <= 0)
                continue;
            k = btkb_read(&g_bt, codes, downs, 16);
            if (k < 0)
                break;
            for (j = 0; j < k; j++) {
                key_event ignore[8];
                btkb_keys_feed(&g_btk, codes[j], downs[j], mono(), ignore, 8, NULL, NULL);
            }
        }
    }
    btkb_close(&g_bt);                          /* the BT keyboard: BRLTTY's again, typing in the terminal */
    brl_hold(&g_brl, 0);                        /* the display: BRLTTY's, showing the menu */
    for (;;) {
        if (!list) {                            /* after a choice: one line, not the whole list again */
            say("Menu: another number, ? to list them, or Enter alone to go back to the unit.");
            read_line(line, sizeof line);
            if (!line[0])
                break;
            if (line[0] != '?' && line[0] != 'h')
                goto chosen;
        }
        list = 0;
        say("Menu. Type a number and Enter; Enter alone goes back to the unit.");
        for (k = 0; k < N_FIRST_KINDS; k++)
            say("  %d %s%s", kind_menu_number(k), KINDS[k].name, k == g_kind ? " (on now)" : "");
        say("  5 Back to the factory state (erases this unit's files)");
        say("  6 Sample rate: %d Hz", g_rate);
        say("  7 Idle channel sound: %s", IDLE_TEXT[g_whine]);
        say("  8 Keep the channel open: %s", OPEN_TEXT[g_keep_open]);
        say("  9 The pop when the channel opens, the click when it clicks off: %s", g_popclick ? "on" : "off");
        say("  10 The 10 Hz tick of the open channel: %s", g_tick ? "on" : "off");
        say("  11 Quick key response (faster than the real unit): %s", g_quick ? "on" : "off");
        say("  12 Serial port: %s", g_serial_shown[0] ? g_serial_shown : "none");
        say("  13 Braille Lite keyboard: %s", g_blk.mode == BLK_LETTERS
            ? "letters (each character its braille cell)" : "keys (F D S J K L)");
        say("  14 Keys (help)");
        for (k = N_FIRST_KINDS; k < N_KINDS; k++)
            if (kind_offered(k))
                say("  %d %s%s", kind_menu_number(k), KINDS[k].name, k == g_kind ? " (on now)" : "");
        {
            char b[80];
            buffer_line(b, sizeof b);
            say("  %d Sound buffer: %s", MENU_BUFFER, b);
        }
        say("  0 Exit (the unit's memory is saved)");
        read_line(line, sizeof line);
        if (!line[0])
            break;
chosen:
        k = atoi(line);
        if (kind_of_menu_number(k) >= 0) {
            start_unit(kind_of_menu_number(k), NULL);
            break;
        }
        switch (k) {
        case 5: {
            char path[PATH_MAX];
            emu_unit *old;
            say("Put the %s back as it left the factory? Its files and settings are erased. Type yes:",
                KINDS[g_kind].name);
            read_line(line, sizeof line);
            if (strcmp(line, "yes")) {
                say("Kept.");
                continue;
            }
            pthread_mutex_lock(&g_lock);
            old = g_unit;
            g_unit = NULL;                      /* so start_unit saves nothing over the deletion */
            pthread_mutex_unlock(&g_lock);
            emu_destroy(old);
            saved_path(g_kind, path, sizeof path);
            unlink(path);
            start_unit(g_kind, NULL);
            goto done;
        }
        case 6: {
            static char labels[N_RATES][16];
            const char *items[N_RATES];
            int i, cur = 0, r;
            for (i = 0; i < N_RATES; i++) {
                snprintf(labels[i], sizeof labels[i], "%d Hz", RATES[i]);
                items[i] = labels[i];
                if (RATES[i] == g_rate)
                    cur = i;
            }
            r = choose("Sample rate", items, N_RATES, cur);
            if (RATES[r] != g_rate) {           /* the card reopened at it, the unit restarted at it (memory kept) */
                if (!audio_stop()) {
                    say("The sample rate stays %d Hz.", g_rate);
                    continue;
                }
                g_rate = RATES[r];
                start_unit(g_kind, NULL);
                audio_start();
                save_settings();
            }
            continue;
        }
        case 7: g_whine = choose("Idle channel sound", IDLE_TEXT, 4, g_whine); break;
        case 8: g_keep_open = choose("Keep the channel open", OPEN_TEXT, 3, g_keep_open); break;
        case 9: g_popclick = !g_popclick; break;
        case 10: g_tick = !g_tick; break;
        case 11:
            g_quick = !g_quick;
            pthread_mutex_lock(&g_lock);
            if (g_unit)
                emu_set_quick(g_unit, g_quick);
            pthread_mutex_unlock(&g_lock);
            say("Quick key response %s.", g_quick ? "on" : "off");
            save_settings();
            continue;
        case 12: menu_serial(); continue;
        case 13:
            g_blk.mode = g_blk.mode == BLK_LETTERS ? BLK_KEYS : BLK_LETTERS;
            say("Braille Lite keyboard: %s.", g_blk.mode == BLK_LETTERS ? "letters" : "keys");
            save_settings();
            continue;
        case 14: keys_help(); continue;
        case MENU_BUFFER: {
            /* the sound thread takes it at its next block: no reopening, no gap (audio_linux.h) */
            char text[N_BUFFER_MODES][128];
            const char *items[N_BUFFER_MODES];
            int i, cur = 0, m;
            for (i = 0; i < N_BUFFER_MODES; i++) {
                buffer_text(BUFFER_MODES[i], 0, text[i], (int)sizeof text[i]);
                items[i] = text[i];
                if (BUFFER_MODES[i] == g_buffer_mode)
                    cur = i;
            }
            i = choose("Sound buffer (a key's answer is heard that much later; too short, the sound breaks up)",
                       items, N_BUFFER_MODES, cur);
            m = BUFFER_MODES[i];
            __atomic_store_n(&g_buffer_mode, m, __ATOMIC_RELEASE);
            say("Sound buffer: %s.", items[i]);
            save_settings();
            continue;
        }
        case 0:
            if (line[0] == '0') {
                g_quit = 1;
                goto done;
            }
            say("Not in the menu.");            /* not a number */
            continue;
        default:
            say("Not in the menu.");
            continue;
        }
        apply_idle();                           /* 7-10 */
        save_settings();
    }
done:
    term_dec_init(&g_term);
    if (g_use_evdev) {                          /* what was typed for the menu is not the unit's */
        key_event drop[64];
        int i;
        for (i = 0; i < g_ev.n; i++)
            while (evdev_read(&g_ev, g_ev.fd[i], drop, 64) > 0) {}
        g_ev.shift = g_ev.ctrl = g_ev.alt = 0;
        evdev_grab(&g_ev, ini_get_int(g_ini, "input", "grab", 1));
    }
    if (!g_quit) {
        bt_take(0);
        display_refresh(1);
        say("Back to the %s.", KINDS[g_kind].name);
    }
}

/* ---- headless: the tests ------------------------------------------------------------------------------------------ */
typedef struct { double t; int kind; char text[256]; int n; } script_line;   /* kind 0 typed, 1 down, 2 up */

/* the script's lines: "8.0 type f", "12.0 type \e[24~", "3.0 down f", "3.2 up f" (C escapes: \e \r \n \t \\ \xHH) */
static int load_script(const char *path, script_line **out)
{
    FILE *f = fopen(path, "r");
    char line[600];
    int n = 0, cap = 0;
    script_line *s = NULL;
    if (!f)
        return -1;
    while (fgets(line, sizeof line, f)) {
        char word[16], *p;
        double t;
        int used = 0, k = 0;
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = 0;
        if (sscanf(line, "%lf %15s %n", &t, word, &used) < 2)
            continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 32;
            s = (script_line *)realloc(s, sizeof(script_line) * (size_t)cap);
        }
        s[n].t = t;
        s[n].kind = !strcmp(word, "down") ? 1 : !strcmp(word, "up") ? 2 : 0;
        p = line + used;
        while (*p && k < (int)sizeof s[n].text - 1) {
            int c = (unsigned char)*p++;
            if (c == '\\' && *p) {
                c = (unsigned char)*p++;
                if (c == 'e') c = 0x1B;
                else if (c == 'r') c = '\r';
                else if (c == 'n') c = '\n';
                else if (c == 't') c = '\t';
                else if (c == 'x') {
                    c = (int)strtol(p, &p, 16);
                }
            }
            s[n].text[k++] = (char)c;
        }
        s[n].text[k] = 0;
        s[n].n = k;
        n++;
    }
    fclose(f);
    *out = s;
    return n;
}

static void write_le(FILE *f, unsigned long v, int bytes)
{
    int i;
    for (i = 0; i < bytes; i++)
        fputc((int)((v >> (8 * i)) & 0xFF), f);
}

static int ram_has(const char *text)
{
    const unsigned char *m;
    int n = emu_memory(g_unit, &m), len = (int)strlen(text), i;
    for (i = 0; i + len <= n; i++)
        if (m[i] == (unsigned char)text[0] && !memcmp(m + i, text, (size_t)len))
            return 1;
    return 0;
}

typedef struct { double from, to, sum; long n; } rms_window;

static int headless(const char *wav, double seconds, const char *script, const char *const *ram_texts, int n_ram,
                    rms_window *win, int n_win, int clock_check, const char *save_to)
{
    script_line *s = NULL;
    int n_s = 0, next = 0, block = g_rate / 100, i;
    long done = 0, total = (long)(seconds * g_rate);
    short buf[RATE_MAX / 100];
    FILE *out = NULL;
    evdev_set mods;
    memset(&mods, 0, sizeof mods);
    if (script && (n_s = load_script(script, &s)) < 0) {
        say("could not read the script %s", script);
        return 2;
    }
    if (wav && !(out = fopen(wav, "wb"))) {
        say("could not write %s", wav);
        return 2;
    }
    if (out) {
        fwrite("RIFF", 1, 4, out); write_le(out, 36 + (unsigned long)total * 2, 4); fwrite("WAVEfmt ", 1, 8, out);
        write_le(out, 16, 4); write_le(out, 1, 2); write_le(out, 1, 2); write_le(out, (unsigned long)g_rate, 4);
        write_le(out, (unsigned long)g_rate * 2, 4); write_le(out, 2, 2); write_le(out, 16, 2);
        fwrite("data", 1, 4, out); write_le(out, (unsigned long)total * 2, 4);
    }
    while (done < total) {
        double now = (double)done / g_rate;
        int k = total - done < block ? (int)(total - done) : block;
        while (next < n_s && s[next].t <= now + 1e-9) {
            key_event ev[64];
            if (s[next].kind == 0) {
                int n = term_dec_feed(&g_term, (const unsigned char *)s[next].text, s[next].n, now, ev, 64), j;
                for (j = 0; j < n; j++)
                    key_in(&ev[j], now);
            } else {                            /* an input device's key, by name */
                int m, key = key_parse(s[next].text, &m);
                if (key) {
                    key_event e;
                    e.key = key;
                    e.type = s[next].kind == 1 ? KE_DOWN : KE_UP;
                    e.mods = 0;
                    key_in(&e, now);
                }
            }
            next++;
        }
        keys_tick(now);
        emu_render(g_unit, buf, k);
        for (i = 0; i < n_win; i++) {
            int j;
            for (j = 0; j < k; j++) {
                double t = (double)(done + j) / g_rate;
                if (t >= win[i].from && t < win[i].to) {
                    win[i].sum += (double)buf[j] * buf[j];
                    win[i].n++;
                }
            }
        }
        if (out)
            fwrite(buf, sizeof(short), (size_t)k, out);   /* little-endian hosts (x86-64, arm64) */
        done += k;
    }
    if (out)
        fclose(out);
    for (i = 0; i < n_win; i++)
        say("rms %.2f-%.2f s: %.5f", win[i].from, win[i].to,
            win[i].n ? sqrt(win[i].sum / win[i].n) / 32768.0 : 0.0);
    for (i = 0; i < n_ram; i++)
        say("ram has \"%s\": %s", ram_texts[i], ram_has(ram_texts[i]) ? "yes" : "no");
    if (clock_check) {                          /* the unit's own words for the time, from the host's clock */
        blc_time t;
        time_t now = time(NULL);
        int found = 0, step;
        char text[16] = "";
        /* the clock started at the host's time and has run in the unit's time since (faster than real time here):
           the minute it said is from a minute before now to a minute after now plus the unit's seconds */
        for (step = -1; step <= 1 + (int)(seconds / 60.0) && !found; step++) {
            time_t when = now + 60 * step;
            struct tm *tm = localtime(&when);
            int h = tm->tm_hour % 12 ? tm->tm_hour % 12 : 12;
            snprintf(text, sizeof text, "%d:%02d", h, tm->tm_min);
            found = ram_has(text);
        }
        emu_clock_time(g_unit, 0, &t);
        say("clock: the unit's clock %04d-%02d-%02d %02d:%02d:%02d; it said the host's time (%s): %s", t.year,
            t.month, t.day, t.hour, t.minute, t.second, text, found ? "yes" : "no");
    }
    if (save_to) {
        int ok = emu_save(g_unit, save_to);
        say("saved %s: %s", save_to, ok ? "yes" : "no");
    }
    free(s);
    return 0;
}

/* ---- the keys, shown: what this keyboard sends (for mapping a new keyboard, such as the BTSpeak's) ------------- */
static int show_keys(void)
{
    struct pollfd p[1 + EVDEV_MAX];
    char msg[512], name[16];
    int q = 0, i;
    double idle = mono();
    int n_ev = evdev_open(&g_ev, ini_get(g_ini, "input", "evdev", "auto")[0] == '/'
                          ? ini_get(g_ini, "input", "evdev", "") : NULL, 0, msg, sizeof msg);
    say("Showing the keys: the terminal's bytes and the keys they are, and the input devices' keys going down and up.");
    say("Input devices: %s.", n_ev ? msg : msg[0] ? msg : "none");
    say("Type q twice to stop (or wait 60 seconds).");
    term_raw();
    while (mono() - idle < 60.0) {
        int n = 0, r;
        p[n].fd = 0; p[n++].events = POLLIN;
        for (i = 0; i < g_ev.n; i++) { p[n].fd = g_ev.fd[i]; p[n++].events = POLLIN; }
        r = poll(p, (nfds_t)n, 50);
        if (r < 0 && errno != EINTR)
            break;
        if (p[0].revents & POLLIN) {
            unsigned char b[64];
            key_event ev[64];
            ssize_t got = read(0, b, sizeof b);
            int k, j;
            char hex[200] = "";
            if (got <= 0)
                break;
            for (j = 0; j < got && j < 60; j++)
                snprintf(hex + strlen(hex), sizeof hex - strlen(hex), "%02X ", b[j]);
            say("terminal: %s", hex);
            k = term_dec_feed(&g_term, b, (int)got, mono(), ev, 64);
            for (j = 0; j < k; j++) {
                say("  key %s%s%s%s", ev[j].mods & KM_CTRL ? "ctrl-" : "", ev[j].mods & KM_ALT ? "alt-" : "",
                    ev[j].mods & KM_SHIFT ? "shift-" : "", key_name(ev[j].key, name));
                q = ev[j].key == 'q' && !ev[j].mods ? q + 1 : 0;
            }
            idle = mono();
            if (q >= 2)
                break;
        }
        {
            key_event ev[16];
            int k = term_dec_flush(&g_term, mono(), ev, 16), j;
            for (j = 0; j < k; j++)
                say("  key %s%s", ev[j].mods & KM_ALT ? "alt-" : "", key_name(ev[j].key, name));
        }
        for (i = 0; i < g_ev.n; i++)
            if (p[1 + i].revents & POLLIN) {
                key_event ev[64];
                int k = evdev_read(&g_ev, g_ev.fd[i], ev, 64), j;
                for (j = 0; j < k; j++)
                    say("%s: %s %s", g_ev.name[i], key_name(ev[j].key, name), ev[j].type == KE_DOWN ? "down" : "up");
                idle = mono();
            }
    }
    term_restore();
    evdev_close(&g_ev);
    return 0;
}

/* ---- main ---------------------------------------------------------------------------------------------------------- */
static void on_signal(int sig)
{
    (void)sig;
    g_quit = 1;
}

static void usage(void)
{
    printf("blazie_emu -- a Blazie Braille Lite 2000, Braille 'n Speak 2000 or Type 'n Speak, running its own "
           "firmware.\n\n"
           "  blazie_emu [--unit bl-en|bl-es|tns-en|tns-es|bns-en|bns-sk] [--firmware DIR] [--config DIR]\n"
           "  blazie_emu --show-keys          what this keyboard sends (for the key settings)\n"
           "  blazie_emu --no-sound           no sound card: the unit runs on silent, paced by the system clock\n"
           "  blazie_emu --no-bt              on a BT Speak or BT Braille, run here in the terminal all the same\n"
           "  blazie_emu --bt-probe           is this a BT Speak or BT Braille? (yes: exit 0)\n"
           "  blazie_emu --help\n\n"
           "On a BT Speak or BT Braille, blazie_emu hands over to blazie_emu_bt (the device's keyboard, speech and\n"
           "braille display), with --unit, --firmware, --config (as --state-dir) and --rate.\n\n"
           "Headless (no sound card, no terminal; the tests):\n"
           "  --wav FILE | --null   render --seconds S of the unit (to FILE, or nowhere)\n"
           "  --script FILE         keys at their times: \"8.0 type f\", \"12.0 type \\e[24~\", \"3.0 down f\"\n"
           "  --state FILE          start from this memory (default: the saved one, else the factory's)\n"
           "  --save FILE           save the memory at the end (default: none; --autosave: the usual place)\n"
           "  --rate HZ  --input keys|letters  --flash-instant  --fake-time SECONDS  --print-actions\n"
           "  --rms FROM:TO  --ram-has TEXT  --clock-check   (reports)\n\n"
           "Keys: F11 opens the menu (Ctrl+O too for the Braille Lite); README-linux.md has the rest.\n");
}

int main(int argc, char **argv)
{
    const char *unit_id = NULL, *fw_given = NULL, *cfg_given = NULL, *wav = NULL, *script = NULL, *state = NULL;
    const char *save_to = NULL, *input = NULL, *ram_texts[16];
    rms_window win[16];
    int n_ram = 0, n_win = 0, null_out = 0, clock_check = 0, autosave = 0, show = 0, i, kind = -1, rate = 0;
    int no_bt = 0;
    double seconds = 10.0;
    char v[64];
    struct sigaction sa;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i], *val = i + 1 < argc ? argv[i + 1] : NULL;
#define TAKE() (i++, val)
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
        else if (!strcmp(a, "--unit") && val) unit_id = TAKE();
        else if (!strcmp(a, "--firmware") && val) fw_given = TAKE();
        else if (!strcmp(a, "--config") && val) cfg_given = TAKE();
        else if (!strcmp(a, "--wav") && val) wav = TAKE();
        else if (!strcmp(a, "--null")) null_out = 1;
        else if (!strcmp(a, "--seconds") && val) seconds = atof(TAKE());
        else if (!strcmp(a, "--script") && val) script = TAKE();
        else if (!strcmp(a, "--state") && val) state = TAKE();
        else if (!strcmp(a, "--save") && val) save_to = TAKE();
        else if (!strcmp(a, "--autosave")) autosave = 1;
        else if (!strcmp(a, "--rate") && val) rate = atoi(TAKE());
        else if (!strcmp(a, "--input") && val) input = TAKE();
        else if (!strcmp(a, "--flash-instant")) g_flash_instant = 1;
        else if (!strcmp(a, "--fake-time") && val) emu_fake_host_time(atoll(TAKE()));
        else if (!strcmp(a, "--print-actions")) g_print_actions = 1;
        else if (!strcmp(a, "--clock-check")) clock_check = 1;
        else if (!strcmp(a, "--show-keys")) show = 1;
        else if (!strcmp(a, "--no-sound")) g_no_sound = 1;
        else if (!strcmp(a, "--no-bt")) no_bt = 1;
        else if (!strcmp(a, "--bt-probe")) {
            int yes = bt_detect();
            printf("BT Speak or BT Braille: %s\n", yes ? "yes" : "no");
            return yes ? 0 : 1;
        }
        else if (!strcmp(a, "--ram-has") && val && n_ram < 16) ram_texts[n_ram++] = TAKE();
        else if (!strcmp(a, "--rms") && val && n_win < 16) {
            const char *r = TAKE();
            win[n_win].from = atof(r);
            win[n_win].to = strchr(r, ':') ? atof(strchr(r, ':') + 1) : seconds;
            win[n_win].sum = 0;
            win[n_win++].n = 0;
        } else {
            fprintf(stderr, "blazie_emu: %s not understood (--help)\n", a);
            return 2;
        }
#undef TAKE
    }
    g_headless = wav || null_out;
    if (getenv("BLAZIE_KEYS_BREAK"))
        blk_break = 1;                          /* the tests' control: dots 1 and 4 swapped */

    /* the settings and the units' memory: ~/.config/ssi263-speech/blazie-emu */
    if (cfg_given)
        snprintf(g_cfg_dir, sizeof g_cfg_dir, "%s", cfg_given);
    else if (getenv("XDG_CONFIG_HOME") && *getenv("XDG_CONFIG_HOME"))
        snprintf(g_cfg_dir, sizeof g_cfg_dir, "%s/ssi263-speech/blazie-emu", getenv("XDG_CONFIG_HOME"));
    else
        snprintf(g_cfg_dir, sizeof g_cfg_dir, "%s/.config/ssi263-speech/blazie-emu",
                 getenv("HOME") ? getenv("HOME") : ".");
    snprintf(g_ini_path, sizeof g_ini_path, "%s/blazie_emu.ini", g_cfg_dir);
    if (!g_headless || autosave)
        make_dirs(g_cfg_dir);
    if (!g_headless && !exists(g_ini_path)) {   /* the first run: the settings file, explained */
        FILE *f = fopen(g_ini_path, "w");
        if (f) {
            fputs(DEFAULT_INI, f);
            fclose(f);
        }
    }
    g_ini = ini_load(g_headless && !autosave ? NULL : g_ini_path);
    if (!g_ini)
        return 1;
    load_settings();
    load_keys();
    if (input && !blk_set(&g_blk, "mode", input)) {
        fprintf(stderr, "blazie_emu: --input keys or letters\n");
        return 2;
    }
    if (rate) {
        for (i = 0; i < N_RATES && RATES[i] != rate; i++) {}
        if (i == N_RATES) {
            fprintf(stderr, "blazie_emu: --rate 11025, 16000, 22050, 32000, 44100 or 48000\n");
            return 2;
        }
        g_rate = rate;
    }
    /* on a BT Speak or BT Braille: hand over to the BT front end (bt_handover.h); never for the headless runs or
       --show-keys.  With bt = native, or the front end not installed, this program uses the device's keyboard and
       display itself, below (btkb_linux.h, brl_linux.h) */
    if (!g_headless && !show) {
        const char *fw = fw_given ? fw_given : ini_get(g_ini, "unit", "firmware_dir", "");
        bt_hand_over(no_bt, ini_get(g_ini, "input", "bt", "auto"), unit_id, *fw ? fw : NULL, cfg_given, rate);
    }
    term_dec_init(&g_term);
    find_firmware(fw_given);
    if (show)
        return show_keys();

    snprintf(v, sizeof v, "%s", ini_get(g_ini, "unit", "kind", "english"));
    for (i = 0; i < N_KINDS; i++)
        if (unit_id ? !strcmp(unit_id, KINDS[i].id) : !strcmp(v, KINDS[i].ini))
            kind = i;
    if (kind < 0) {
        if (unit_id) {
            fprintf(stderr, "blazie_emu: --unit bl-en, bl-es, tns-en, tns-es, bns-en or bns-sk\n");
            return 2;
        }
        kind = 0;
    }

    if (g_headless) {
        int r;
        if (!autosave)                          /* the saved memory is not touched: a test's own folder or none */
            snprintf(g_cfg_dir, sizeof g_cfg_dir, "/nonexistent-blazie-emu-test");
        if (!start_unit(kind, state))
            return 1;
        r = headless(wav, seconds, script, ram_texts, n_ram, win, n_win, clock_check, save_to);
        if (autosave) {
            char path[PATH_MAX];
            save_unit();
            saved_path(g_kind, path, sizeof path);
            say("autosaved %s: %s", path, exists(path) ? "yes" : "no");
        }
        emu_destroy(g_unit);
        ini_free(g_ini);
        return r;
    }

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);
    say("Blazie emulator. F11 opens the menu (Ctrl+O too for the Braille Lite). Settings: %s", g_ini_path);
    {   /* the BT Speak's and BT Braille's keyboard: before the input devices, whose keypad its server holds */
        const char *kb = ini_get(g_ini, "btspeak", "keyboard", "auto");
        g_bt_want = strcmp(kb, "off") != 0 && strcmp(ini_get(g_ini, "input", "bt", "auto"), "off") != 0 && !no_bt;
        snprintf(g_bt_path, sizeof g_bt_path, "%s", getenv("BLAZIE_BTKB_SOCKET") ? getenv("BLAZIE_BTKB_SOCKET")
                 : BTKB_SOCKET);
        btkb_keys_init(&g_btk, ini_get_int(g_ini, "btspeak", "gesture_ms", 80) / 1000.0);
        if (bt_take(1)) {
            int back, adv;
            char name[16];
            evdev_skip_bt = 1;
            /* the BT Braille's: L2 or R2 the back bar, L3 or R3 the advance bar (as #4's frontend, tried on the
               device), and the keys the user pans BRLTTY's display with, if others */
            blk_add(&g_blk, "back", "code:257 code:260");
            blk_add(&g_blk, "advance", "code:258 code:261");
            btkb_panning_keys(NULL, &back, &adv);
            if (back >= 0) blk_add(&g_blk, "back", key_name(evdev_key_of(back), name));
            if (adv >= 0) blk_add(&g_blk, "advance", key_name(evdev_key_of(adv), name));
            say("Keys: the BT keyboard. Dots 7 and 8 are the back and advance bars; M-chord with dot 7 opens the "
                "menu, Z-chord with dot 7 saves and leaves.");
        }
    }
    {   /* the input devices: auto on a text console (a desktop's terminal does not own the keyboard) */
        char msg[512];
        const char *use = ini_get(g_ini, "input", "evdev", "auto");
        char dev[PATH_MAX];
        int grab = ini_get_int(g_ini, "input", "grab", 1);
        snprintf(dev, sizeof dev, "%s", use);
        if (!strcmp(dev, "on") || dev[0] == '/'
                || (!strcmp(dev, "auto") && !getenv("DISPLAY") && !getenv("WAYLAND_DISPLAY"))) {
            g_use_evdev = evdev_open(&g_ev, dev[0] == '/' ? dev : NULL, grab, msg, sizeof msg) > 0;
            if (g_use_evdev)
                say("Keys: %s%s.", msg, grab ? " (only this program gets them while it runs)" : "");
            else if (strcmp(dev, "auto") || (g_ev.busy[0] && !g_bt.running))
                say("Keys: %s; the terminal's keys are used.", msg);
            if (g_use_evdev && g_ev.busy[0] && !g_bt.running)
                say("Keys: %s is held by another program (BRLTTY?); its keys come through the terminal.", g_ev.busy);
        }
    }
    if (strcmp(ini_get(g_ini, "btspeak", "display", "auto"), "off")) {
        char msg[256];
        if (brl_open(&g_brl, msg, sizeof msg) && pipe2(g_disp_pipe, O_CLOEXEC | O_NONBLOCK) == 0)
            say("Braille: the Braille Lite's display on BRLTTY's display (%s).", msg);
        else if (g_brl.lib) {
            brl_close(&g_brl);
            say("Braille: no pipe; the display is BRLTTY's.");
        }
    }
    term_raw();
    atexit(term_restore);
    snprintf(g_serial_want, sizeof g_serial_want, "%s", ini_get(g_ini, "serial", "port", "none"));
    if (!start_unit(kind, NULL)) {
        say("Choose a unit with --unit, or set firmware_dir in %s.", g_ini_path);
        term_restore();
        return 1;
    }
    audio_start();
    if (strcmp(g_serial_want, "none") && g_serial_want[0]) {
        char p[PATH_MAX];
        snprintf(p, sizeof p, "%s", g_serial_want);
        set_serial(p, 1);                       /* the port chosen last time */
    }
    {
        double next_save = mono() + AUTOSAVE_S;
        display_refresh(1);
        while (!g_quit) {
            struct pollfd p[1 + EVDEV_MAX + 3];
            int n = 0, timeout = 1000, r, n_ev, x_bt = -1, x_brl = -1, x_disp = -1;
            double now = mono(), d;
            p[n].fd = 0; p[n++].events = POLLIN;
            for (i = 0; i < g_ev.n; i++) { p[n].fd = g_ev.fd[i]; p[n++].events = POLLIN; }
            n_ev = g_ev.n;
            if (btkb_fd(&g_bt) >= 0) { x_bt = n; p[n].fd = btkb_fd(&g_bt); p[n++].events = POLLIN; }
            if (brl_fd(&g_brl) >= 0) { x_brl = n; p[n].fd = brl_fd(&g_brl); p[n++].events = POLLIN; }
            if (g_brl.lib && g_disp_pipe[0] >= 0) { x_disp = n; p[n].fd = g_disp_pipe[0]; p[n++].events = POLLIN; }
            d = btkb_keys_deadline(&g_btk);
            if (d >= 0 && (int)((d - now) * 1000) + 1 < timeout) timeout = (int)((d - now) * 1000) + 1;
            d = term_dec_deadline(&g_term);
            if (d >= 0 && (int)((d - now) * 1000) + 1 < timeout) timeout = (int)((d - now) * 1000) + 1;
            d = blk_deadline(&g_blk);
            if (d >= 0 && (int)((d - now) * 1000) + 1 < timeout) timeout = (int)((d - now) * 1000) + 1;
            if (timeout < 0) timeout = 0;
            r = poll(p, (nfds_t)n, timeout);
            now = mono();
            if (r < 0 && errno != EINTR)
                break;
            if (r > 0 && (p[0].revents & (POLLIN | POLLHUP))) {
                unsigned char b[256];
                key_event ev[256];
                ssize_t got = read(0, b, sizeof b);
                if (got <= 0)
                    break;                      /* the terminal is gone */
                /* the input devices grabbed, nothing of theirs reaches the terminal: what does is another keyboard's
                   (BRLTTY's characters from a braille keyboard it holds), the unit's.  Not grabbed, the terminal's
                   copy of a key is dropped -- but the menu keys still work from it. */
                if (!g_use_evdev || g_ev.grabbed) {
                    int k = term_dec_feed(&g_term, b, (int)got, now, ev, 256), j;
                    for (j = 0; j < k && !g_quit; j++)
                        key_in(&ev[j], now);
                } else {
                    int k = term_dec_feed(&g_term, b, (int)got, now, ev, 256), j;
                    int tns = KINDS[g_kind].kind == EMU_TYPE_N_SPEAK;
                    for (j = 0; j < k; j++)
                        if (key_in_list(&ev[j], tns ? g_menu_tns : g_menu_bl, tns ? g_n_menu_tns : g_n_menu_bl))
                            menu();
                }
            }
            if (r > 0 && x_disp >= 0 && (p[x_disp].revents & POLLIN)) {
                char drain[64];
                while (read(g_disp_pipe[0], drain, sizeof drain) > 0) {}
                display_refresh(0);
            }
            if (r > 0 && x_brl >= 0 && (p[x_brl].revents & POLLIN))
                brl_bars();
            if (r > 0 && x_bt >= 0 && g_bt.running && (p[x_bt].revents & (POLLIN | POLLHUP)))
                bt_keys(now);
            bt_tick(mono());
            for (i = 0; i < n_ev && i < g_ev.n && r > 0; i++)
                if (p[1 + i].revents & POLLIN) {
                    key_event ev[64];
                    int k = evdev_read(&g_ev, g_ev.fd[i], ev, 64), j;
                    for (j = 0; j < k && !g_quit; j++)
                        key_in(&ev[j], now);
                } else if (p[1 + i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    say("The keyboard %s is gone.", g_ev.name[i]);
                    close(g_ev.fd[i]);
                    g_ev.fd[i] = g_ev.fd[g_ev.n - 1];
                    memcpy(g_ev.name[i], g_ev.name[g_ev.n - 1], sizeof g_ev.name[i]);
                    g_ev.n--;
                    if (!g_ev.n) {
                        g_use_evdev = 0;
                        say("The terminal's keys are used.");
                    }
                    break;
                }
            keys_tick(mono());
            if (mono() >= next_save) {          /* every minute: nothing is lost if the program is killed -- */
                if (g_audio_running)            /* by the sound thread, between two blocks */
                    __atomic_store_n(&g_save_due, 1, __ATOMIC_RELEASE);
                else
                    save_unit();
                next_save = mono() + AUTOSAVE_S;
            }
        }
    }
    say("Switching off: the unit's memory is saved.");
    set_serial("none", 1);
    audio_stop();
    save_unit();
    save_settings();
    term_restore();
    evdev_close(&g_ev);
    btkb_close(&g_bt);
    brl_close(&g_brl);
    {
        emu_unit *u;
        pthread_mutex_lock(&g_lock);            /* a sound thread left stuck renders nothing from here */
        u = g_unit;
        g_unit = NULL;
        pthread_mutex_unlock(&g_lock);
        emu_destroy(u);
    }
    ini_free(g_ini);
    return 0;
}
