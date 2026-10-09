/* bl_keys.c -- see bl_keys.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bl_keys.h"

int blk_break;

/* computer braille (North American Braille Computer Code, 6 dots): the character of each cell, dot 1 = bit 0 ..
   dot 6 = bit 5 -- the same order as the unit's chord bits.  Lowercase letters are the capitals' cells; NABCC's
   8-dot lowercase symbols (` { | } ~) are the cells of @ [ \ ] ^ without dot 7. */
static const char CELLS[] = " A1B'K2L@CIF/MSP\"E3H9O6R^DJG>NTQ,*5<-U8V.%[$+X!&;:4\\0Z7(_?W]#Y)=";

int blk_cell_of(int c)
{
    const char *p;
    if (c >= 'a' && c <= 'z')
        c = c - 'a' + 'A';
    else if (c == '`') c = '@';
    else if (c == '{') c = '[';
    else if (c == '|') c = '\\';
    else if (c == '}') c = ']';
    else if (c == '~') c = '^';
    if (c <= 0 || c >= 0x7F)
        return -1;
    p = strchr(CELLS, c);
    return p ? (int)(p - CELLS) : -1;
}

int blk_chord_of(const char *name)
{
    size_t n = strlen(name);
    int bits = 0;
    if (!strcmp(name, "advance"))
        return CHORD_ADVANCE;
    if (!strcmp(name, "back"))
        return CHORD_BACK;
    if (!strcmp(name, "space"))
        return CHORD_SPACE;
    if (!strncmp(name, "dots ", 5)) {
        char copy[64], *tok;
        snprintf(copy, sizeof copy, "%s", name + 5);
        for (tok = strtok(copy, " "); tok; tok = strtok(NULL, " ")) {
            if (!strcmp(tok, "space")) bits |= CHORD_SPACE;
            else if (!strcmp(tok, "advance")) bits |= CHORD_ADVANCE;
            else if (!strcmp(tok, "back")) bits |= CHORD_BACK;
            else if (tok[0] >= '1' && tok[0] <= '6' && !tok[1]) bits |= CHORD_DOT(tok[0] - '0');
            else return -1;
        }
        return bits ? bits : -1;
    }
    if (n > 6 && !strcmp(name + n - 6, "-chord")) {
        size_t i, len = n - 6;
        if (len == 1 && !(name[0] >= '1' && name[0] <= '6')) {
            int cell = blk_cell_of((unsigned char)name[0]);
            return cell > 0 ? cell | CHORD_SPACE : -1;
        }
        for (i = 0; i < len; i++) {             /* dots: "1-chord", "2-5-6-chord", "256-chord" */
            if (name[i] >= '1' && name[i] <= '6')
                bits |= CHORD_DOT(name[i] - '0');
            else if (name[i] != '-')
                return -1;
        }
        return bits ? bits | CHORD_SPACE : -1;
    }
    if (n == 1) {
        int cell = blk_cell_of((unsigned char)name[0]);
        return cell > 0 ? cell : -1;
    }
    return -1;
}

const char *blk_chord_name(int bits, char *out)
{
    int cell = bits & 0x3F, d, n = 0;
    out[0] = 0;
    if (bits == CHORD_ADVANCE)
        return strcpy(out, "advance bar");
    if (bits == CHORD_BACK)
        return strcpy(out, "back bar");
    if (bits == CHORD_SPACE)
        return strcpy(out, "space");
    if (cell && !(bits & CHORD_BARS) && CELLS[cell] >= 'A' && CELLS[cell] <= 'Z') {
        snprintf(out, 40, "%c%s", CELLS[cell] - 'A' + 'a', bits & CHORD_SPACE ? "-chord" : "");
        return out;
    }
    n = snprintf(out, 40, "dots");
    for (d = 1; d <= 6; d++)
        if (bits & CHORD_DOT(d))
            n += snprintf(out + n, 40 - n, " %d", d);
    if (bits & CHORD_SPACE)
        n += snprintf(out + n, 40 - n, " space");
    if (bits & CHORD_ADVANCE)
        n += snprintf(out + n, 40 - n, " advance");
    if (bits & CHORD_BACK)
        snprintf(out + n, 40 - n, " back");
    return out;
}

static void map_add(bl_keys *k, const char *keys, int bit)
{
    key_combo c[8];
    int n = key_list(keys, c, 8), i;
    for (i = 0; i < n && k->n_map < BLK_MAX_MAP; i++) {
        k->map_keys[k->n_map] = c[i];
        k->map_bits[k->n_map++] = bit;
    }
}

static void map_clear(bl_keys *k, int bit)
{
    int i, j = 0;
    for (i = 0; i < k->n_map; i++)
        if (k->map_bits[i] != bit) {
            k->map_keys[j] = k->map_keys[i];
            k->map_bits[j++] = k->map_bits[i];
        }
    k->n_map = j;
}

static void special_set(bl_keys *k, int key, int mods, int bits)
{
    int i;
    for (i = 0; i < k->n_special; i++)
        if (k->special_keys[i].key == key && k->special_keys[i].mods == mods) {
            k->special_bits[i] = bits;
            return;
        }
    if (k->n_special < BLK_MAX_SPECIAL) {
        k->special_keys[k->n_special].key = key;
        k->special_keys[k->n_special].mods = mods;
        k->special_bits[k->n_special++] = bits;
    }
}

/* letters mode: the keys BRLTTY makes of chords on the BTSpeak (its user's manual, "Basic Keyboard Navigation"),
   back to their chords; Enter (dot 8) and Backspace (dot 7) as the unit's own: e-chord and b-chord */
static const struct { const char *key, *chord; } SPECIALS[] = {
    {"up", "1-chord"}, {"down", "4-chord"}, {"ctrl-left", "2-chord"}, {"ctrl-right", "5-chord"},
    {"left", "3-chord"}, {"right", "6-chord"}, {"pageup", "2-3-chord"}, {"pagedown", "5-6-chord"},
    {"home", "1-3-chord"}, {"end", "4-6-chord"}, {"ctrl-home", "1-2-3-chord"}, {"ctrl-end", "4-5-6-chord"},
    {"tab", "4-5-chord"}, {"shift-tab", "1-2-chord"}, {"insert", "3-5-chord"}, {"delete", "2-5-6-chord"},
    {"esc", "2-6-chord"}, {"enter", "e-chord"}, {"backspace", "b-chord"}, {"ctrl-a", "advance"}, {"ctrl-b", "back"},
};

void blk_defaults(bl_keys *k)
{
    unsigned i;
    memset(k, 0, sizeof *k);
    k->mode = BLK_KEYS;
    map_add(k, "f brl_dot1", CHORD_DOT(1));
    map_add(k, "d brl_dot2", CHORD_DOT(2));
    map_add(k, "s brl_dot3", CHORD_DOT(3));
    map_add(k, "j brl_dot4", CHORD_DOT(4));
    map_add(k, "k brl_dot5", CHORD_DOT(5));
    map_add(k, "l brl_dot6", CHORD_DOT(6));
    map_add(k, "space", CHORD_SPACE);
    map_add(k, "; brl_dot8", CHORD_ADVANCE);
    map_add(k, "a brl_dot7", CHORD_BACK);
    k->chord_s = 0.08;
    k->repeat_s = 0.15;
    k->n_hold = key_list("f12 ctrl-k", k->hold, 4);
    k->n_prefix = key_list("ctrl-c", k->prefix, 4);
    k->capital_is_chord = 1;
    for (i = 0; i < sizeof SPECIALS / sizeof SPECIALS[0]; i++) {
        int mods, key = key_parse(SPECIALS[i].key, &mods);
        special_set(k, key, mods, blk_chord_of(SPECIALS[i].chord));
    }
    k->last_key = -1;
}

int blk_set(bl_keys *k, const char *setting, const char *value)
{
    static const char *const BITS[] = {"dot1", "dot2", "dot3", "dot4", "dot5", "dot6", "space", "advance", "back"};
    int i, mods, key;
    for (i = 0; i < 9; i++)
        if (!strcmp(setting, BITS[i])) {
            key_combo c[8];
            int n = key_list(value, c, 8), j, m, kept = 0;
            map_clear(k, 1 << i);
            for (m = 0; m < k->n_map; m++) {    /* a key is one bit: given to this one, it leaves any other */
                for (j = 0; j < n && k->map_keys[m].key != c[j].key; j++) {}
                if (j == n) {
                    k->map_keys[kept] = k->map_keys[m];
                    k->map_bits[kept++] = k->map_bits[m];
                }
            }
            k->n_map = kept;
            map_add(k, value, 1 << i);
            return 1;
        }
    if (!strcmp(setting, "mode")) {
        if (!strcmp(value, "keys")) k->mode = BLK_KEYS;
        else if (!strcmp(value, "letters")) k->mode = BLK_LETTERS;
        else return 0;
        return 1;
    }
    if (!strcmp(setting, "chord_ms") || !strcmp(setting, "repeat_ms")) {
        int ms = atoi(value);
        if (ms < 10 || ms > 1000)
            return 0;
        *(!strcmp(setting, "chord_ms") ? &k->chord_s : &k->repeat_s) = ms / 1000.0;
        return 1;
    }
    if (!strcmp(setting, "hold")) {
        k->n_hold = key_list(value, k->hold, 4);
        return 1;
    }
    if (!strcmp(setting, "prefix")) {
        k->n_prefix = key_list(value, k->prefix, 4);
        return 1;
    }
    if (!strcmp(setting, "capital_is_chord")) {
        k->capital_is_chord = atoi(value) != 0;
        return 1;
    }
    key = key_parse(setting, &mods);           /* letters mode: a key that stands for a chord ("up = 1-chord") */
    if (key) {
        int bits = !strcmp(value, "none") ? 0 : blk_chord_of(value);
        if (bits < 0)
            return 0;
        special_set(k, key, mods, bits);
        return 1;
    }
    return 0;
}

int blk_add(bl_keys *k, const char *setting, const char *value)
{
    static const char *const BITS[] = {"dot1", "dot2", "dot3", "dot4", "dot5", "dot6", "space", "advance", "back"};
    int i;
    for (i = 0; i < 9; i++)
        if (!strcmp(setting, BITS[i])) {
            map_add(k, value, 1 << i);
            return 1;
        }
    return 0;
}

static void emit(bl_keys *k, int type, int bits)
{
    if (blk_break)                              /* the control: dots 1 and 4 swapped */
        bits = (bits & ~0x09) | ((bits & 0x01) << 3) | ((bits & 0x08) >> 3);
    if (k->n_out < BLK_MAX_ACTIONS) {
        k->out[k->n_out].type = type;
        k->out[k->n_out++].bits = bits;
    }
}

/* a chord typed: sent, or held if the hold key asked for it */
static void finish(bl_keys *k, int bits)
{
    if (k->hold_armed) {
        k->hold_armed = 0;
        k->held = bits;
        emit(k, BLA_HELD, bits);
    } else
        emit(k, BLA_CHORD, bits);
}

static void flush_pending(bl_keys *k)
{
    if (k->pending && !k->pending_repeat)
        finish(k, k->pending);
    k->pending = 0;
    k->pending_repeat = 0;
}

static int bit_of(const bl_keys *k, int key)
{
    int i, bits = 0;
    key = key_fold(key);
    for (i = 0; i < k->n_map; i++)
        if (k->map_keys[i].key == key)
            bits |= k->map_bits[i];
    return bits;
}

int blk_event(bl_keys *k, const key_event *e, double now)
{
    int bit, i;
    if (e->type != KE_PRESS) {                  /* an input device: the keys as they move */
        bit = bit_of(k, e->key);
        if (!bit)
            return 0;
        if (bit & CHORD_BARS) {                 /* a bar is no part of a chord: down while held, under the dots of
                                                   a chord typed meanwhile (the firmware's bar + chord) */
            if (e->type == KE_DOWN)
                k->bars |= bit & CHORD_BARS;
            else
                k->bars &= ~(bit & CHORD_BARS);
            emit(k, BLA_HELD, k->chord.down | k->bars);
            bit &= ~CHORD_BARS;
            if (!bit)
                return 1;
        }
        if (e->type == KE_DOWN) {
            chord_down(&k->chord, bit);
            emit(k, BLA_HELD, k->chord.down | k->bars);
        } else {
            int c = chord_up(&k->chord, bit);
            emit(k, BLA_HELD, k->chord.down | k->bars);
            if (c)
                emit(k, BLA_CHORD, c);
        }
        return 1;
    }
    if (key_in_list(e, k->hold, k->n_hold)) {
        flush_pending(k);
        if (k->held) {                          /* the held chord comes up: the unit gets it as a chord */
            int c = k->held;
            k->held = 0;
            emit(k, BLA_HELD, 0);
            emit(k, BLA_CHORD, c);
        } else
            k->hold_armed = !k->hold_armed;
        return 1;
    }
    if (k->mode == BLK_LETTERS) {
        int cell;
        if (key_in_list(e, k->prefix, k->n_prefix)) {
            k->prefixed = 1;
            return 1;
        }
        for (i = 0; i < k->n_special; i++)
            if (k->special_keys[i].key == key_fold(e->key) && k->special_keys[i].mods == e->mods
                    && k->special_bits[i]) {
                k->prefixed = 0;
                finish(k, k->special_bits[i]);
                return 1;
            }
        if (e->key >= 0x100 || (e->mods & (KM_CTRL | KM_ALT)))
            return 0;
        if (e->key == ' ') {
            k->prefixed = 0;
            finish(k, CHORD_SPACE);
            return 1;
        }
        cell = blk_cell_of(e->key);
        if (cell <= 0)
            return 0;
        if (k->prefixed || (k->capital_is_chord && e->key >= 'A' && e->key <= 'Z'))
            cell |= CHORD_SPACE;
        k->prefixed = 0;
        finish(k, cell);
        return 1;
    }
    /* keys mode: a terminal's keys, each a dot, put together by time */
    if (e->mods & (KM_CTRL | KM_ALT))
        return 0;
    bit = bit_of(k, e->key);
    if (!bit)
        return 0;
    {
        int key = key_fold(e->key), prev = k->last_key;
        double wait = k->chord_s > k->repeat_s ? k->chord_s : k->repeat_s;
        int repeat = key == prev && now - k->last_at < k->repeat_s;
        k->last_key = key;
        k->last_at = now;
        if (repeat) {                           /* auto-repeat: never a chord of its own */
            if (!k->pending || k->pending == bit)
                k->pending_repeat = 1;
            k->pending |= bit;
            k->pending_until = now + wait;
            return 1;
        }
        if (!k->pending)                        /* the last key again: wait to see whether it repeats */
            k->pending_until = now + (key == prev ? wait : k->chord_s);
        else if (now + k->chord_s > k->pending_until)
            k->pending_until = now + k->chord_s;
        k->pending |= bit;
    }
    return 1;
}

void blk_tick(bl_keys *k, double now)
{
    if (k->pending && now >= k->pending_until)
        flush_pending(k);
}

double blk_deadline(const bl_keys *k)
{
    return k->pending ? k->pending_until : -1.0;
}

int blk_take(bl_keys *k, bl_action *out, int cap)
{
    int n = k->n_out < cap ? k->n_out : cap;
    memcpy(out, k->out, sizeof(bl_action) * (size_t)n);
    memmove(k->out, k->out + n, sizeof(bl_action) * (size_t)(k->n_out - n));
    k->n_out -= n;
    return n;
}

void blk_reset(bl_keys *k)
{
    if (k->held || k->chord.down || k->bars)
        emit(k, BLA_HELD, 0);
    chord_reset(&k->chord);
    k->bars = 0;
    k->pending = k->pending_repeat = 0;
    k->prefixed = k->hold_armed = 0;
    k->held = 0;
    k->last_key = -1;
}

int blk_holding(const bl_keys *k)
{
    return k->held ? k->held : k->hold_armed ? -1 : 0;
}
