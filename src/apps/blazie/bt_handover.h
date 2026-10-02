/* bt_handover.h -- blazie_emu on a BT Speak or BT Braille: the one emulator finds the device and hands over to the
 * BT frontend (src/platforms/btspeak: blazie_emu_bt and its worker blazie_bt, installed beside blazie_emu), which
 * uses the device's own keyboard, speech and braille display.  Everywhere else blazie_emu runs in the terminal.
 *
 * The device: the system's python3 imports the device's BTSpeak library and its keyboard service answers
 * (kb_client.server_available(): what the frontend itself needs before it can run).  An ordinary PC or Raspberry Pi
 * has no BTSpeak library, so the probe says no there; no python3, a probe that fails or one that hangs (5 s) says no.
 * BLAZIE_BT_DETECT=1 or 0 answers instead of the probe (the tests).
 */
#ifndef BT_HANDOVER_H
#define BT_HANDOVER_H

#include <stddef.h>

/* 1: this is a BT Speak or BT Braille */
int bt_detect(void);

/* 1, and blazie_emu_bt's path in out: the BT frontend and its worker are installed beside this program */
int bt_frontend(char *out, size_t cap);

/* Hands over when a BT device is found, unless no_bt (--no-bt) or the setting ([input] bt) is off or native: one line
 * said, then blazie_emu_bt runs in this process with the options that have an equivalent there (each NULL or 0 when
 * not given): --unit, --firmware, --config as --state-dir, --rate.  Returns when blazie_emu is to run itself: no
 * device, the hand-over turned off, native (it uses the device's keyboard and display itself: btkb_linux.h,
 * brl_linux.h), or the frontend missing (said in one line: the native route then) or failing to start. */
void bt_hand_over(int no_bt, const char *setting, const char *unit, const char *firmware, const char *state_dir,
                  int rate);

#endif
