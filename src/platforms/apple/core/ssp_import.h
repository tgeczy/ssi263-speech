/* ssp_import.h -- every firmware the Apple apps import, judged by content: the Braille Lite's and the Speak-Out's as
 * Android judges them (ssa_import.h: bl_firmware.h's list of releases, then GW Micro's SPEAKOUT.HEX), and Aicom's
 * files, which Android carries built in but the Apple apps never do (they ship hollow): the Accent-PC's three ROMs and
 * the Accent-mini's DOS driver, each known by its sha256 alone (firmware/AICOM.txt's files, the hashes
 * check_apk_no_firmware.py lets into an APK).
 *
 * Plain C, no Apple frameworks: the host-side test compiles it.  MIT.
 */
#ifndef SSP_IMPORT_H
#define SSP_IMPORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* What ssp_import_firmware found: ssa_import_firmware's (BLV_FW_ENGLISH 0, BLV_FW_SPANISH 1, SSA_HEX_SPEAKOUT 3,
   SSA_SMARTALK 28 ..., or a negative refusal) or one of Aicom's files. */
#define SSP_FW_ACCENT_U2 10                /* the Accent-PC's 8085 program ROM, 64 KB */
#define SSP_FW_ACCENT_U3 11                /* its dictionary, part 1, 32 KB */
#define SSP_FW_ACCENT_U4 12                /* its dictionary, part 2, 32 KB */
#define SSP_FW_ACCENT_MINI 13              /* the Accent-mini's SPKEMS.DVC */

/* A Braille Lite 2000 release on bl_firmware.h's list that ssa_import_firmware refuses -- one with no shipped voice:
   a release voice (ssa_engine.h's ssa_release_voice, Hungarian ...) or a unit to run alone (the betas, Japanese
   ...) -- is SSP_FW_RELEASE plus its row (blv_release_*), written to out by blv_import_release. */
#define SSP_FW_RELEASE 100
/* The file name each of Aicom's is kept under (as the repository's firmware/ and the NVDA add-on name them). */
const char *ssp_aicom_file(int found);

/* The SmarTalk's S_TALK.EXE 1.21 as ssa_import_firmware knows it: its sha256 in hex (st_host.h's STH_SHA256), for
   the import to say which copy to bring when another file of its name is chosen (ssa_import_firmware returns
   SSA_SMARTALK for this one). */
const char *ssp_smartalk_sha256(void);

/* Judges data: Aicom's files by their sha256 first (written to out exactly, their label in msg), then
   ssa_import_firmware's judgement (the Braille Lite's release written to out as a .BNS, or the Speak-Out's HEX),
   then, for a Braille Lite 2000 image it does not take, blv_import_release's (SSP_FW_RELEASE + the row).
   Returns what it found, or a negative refusal with the reason in msg (out removed). */
int ssp_import_firmware(const unsigned char *data, long n, const char *out, char *msg, int msglen);

/* The test's control (test_apple_speech.c sets it; the app never does): nonzero drops the Aicom files' check, so
   they are judged as Android judges them (not firmware); 2 drops the releases' (SSP_FW_RELEASE). */
extern int ssp_import_break;

#ifdef __cplusplus
}
#endif
#endif
