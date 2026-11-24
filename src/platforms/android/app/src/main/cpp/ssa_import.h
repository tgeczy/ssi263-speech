/* ssa_import.h -- the Speak-Out's firmware and the Mockingboard's (below), as a user brings them.  The Speak-Out's:
 * GW Micro's SPEAKOUT.HEX, judged by its content -- the Intel HEX format, then the known firmware's sha256
 * (firmware/gw-micro-speakout/README.txt's, the one nvda/build_speakout.py builds the add-on from and
 * check_apk_no_firmware.py keeps out of the APK) -- never by its name.  The Braille Lite's is bl_firmware.h's;
 * ssa_import_firmware asks that one first and these when it finds none.
 *
 * The app never carries the Speak-Out's file, and carries the Mockingboard's only as its built-in copies (below):
 * the user imports them (FirmwareImport.kt, SsiImport.kt).  Plain C, no JNI: the
 * host-side test (src/platforms/android/test) compiles it too.  MIT.
 */
#ifndef SSA_IMPORT_H
#define SSA_IMPORT_H

#ifdef __cplusplus
extern "C" {
#endif

#define SSA_HEX_SPEAKOUT 3             /* GW Micro's SPEAKOUT.HEX (= ssa_engine.h's SSA_SPEAKOUT) */
#define SSA_HEX_NONE (-1)              /* not an Intel HEX file */
#define SSA_HEX_WRITE (-3)             /* out could not be written */
#define SSA_HEX_OTHER (-5)             /* an Intel HEX file, but not the Speak-Out's firmware (damaged, or another) */

/* The known firmware's sha256, as firmware/gw-micro-speakout/README.txt gives it. */
#define SSA_SPEAKOUT_SHA256 "1c6930c8c6aed0550bc267c14032f9195b450ed95de606f2fa9727e2b7eb1eb1"

/* Judges data: when it is GW Micro's SPEAKOUT.HEX -- byte for byte, or with its lines ending in LF alone or a DOS
   end-of-file mark (Ctrl-Z) after them, which is the same text -- writes the known file to out exactly and returns
   SSA_HEX_SPEAKOUT with the firmware's label in msg; else SSA_HEX_NONE, SSA_HEX_OTHER or SSA_HEX_WRITE with the
   reason in msg (out removed). */
int ssa_import_speakout(const unsigned char *data, long n, const char *out, char *msg, int msglen);

/* The Mockingboard's: Sweet Micro Systems' text-to-speech as one file -- 1.1, mockingboard-tts-1.1.bin (the toolkit
   disk's six files back to back), or the earlier one, mockingboard-tts-early.bin (Mockingboard disk 1's four);
   tools/mockingboard_firmware.py's -- known by its sha256 alone (mb_host.h's mbh_variant, the only two its host
   runs).  The GitHub APK carries both (Tomi, 2026-10-10: the voices work out of the box until a store build exists);
   an import may still bring a copy of either, which then wins. */
#define SSA_FW_MOCKINGBOARD 5          /* = ssa_engine.h's SSA_MOCKINGBOARD: the 1.1 file */
#define SSA_FW_MOCKINGBOARD_EARLY 6    /* = SSA_MOCKINGBOARD_EARLY: the earlier file */
#define SSA_FW_MB_BUILD (-6)           /* the Mockingboard's file, but this build has no Mockingboard */
#define SSA_FW_MB_DISK (-7)            /* a 140 KB disk image, but not one of the known two (mb_dsk.h's reason) */

/* Judges data: when it is one of the Mockingboard's files, byte for byte -- or a 140 KB Apple II disk image
   (MB_DSK_SIZE) holding one, in DOS or ProDOS order, which mb_dsk.h's mb_firmware_from_dsk turns into the file: the
   Developers Toolkit (1.1) or Mockingboard disk 1 (the earlier one) -- writes the file to out and returns
   SSA_FW_MOCKINGBOARD or SSA_FW_MOCKINGBOARD_EARLY (mbh_variant's) with its label in msg (the same, whichever it
   came from); else BLV_FW_NONE (not it), SSA_FW_MB_DISK (another disk, mb_firmware_from_dsk's reason in msg),
   SSA_FW_MB_BUILD or BLV_FW_WRITE with the reason in msg (out removed). */
int ssa_import_mockingboard(const unsigned char *data, long n, const char *out, char *msg, int msglen);

/* Every firmware the app imports, by content (the JNI bridge's nativeImportFirmware): bl_firmware.h's
   blv_import_firmware first -- a Blazie release on its list, written to out as a .BNS, its language given as the
   app's voice (ssa_blazie_voice: SSA_ENGLISH 0, _SPANISH 1, _GERMAN 7, _ITALIAN 8, _SLOVAK 9) -- and, when it finds
   no Braille Lite firmware, ssa_import_mockingboard -- SSA_FW_MOCKINGBOARD 5 or _EARLY 6, written to out as it is --
   then ssa_import_speakout -- SSA_HEX_SPEAKOUT 3, written to out as SPEAKOUT.HEX.  Else negative (BLV_FW_NONE,
   _REFUSED, _WRITE, _UNKNOWN; SSA_HEX_OTHER; SSA_FW_MB_BUILD, SSA_FW_MB_DISK), the reason in msg. */
int ssa_import_firmware(const unsigned char *data, long n, const char *out, char *msg, int msglen);

/* The phone's import (the JNI bridge's nativeImportFirmware): ssa_import_firmware, and for a Braille Lite 2000 release
   it does not take (BLV_FW_UNKNOWN: no shipped voice, or not on the list) the release table's (bl_firmware.h
   blv_import_release): a row naming a release voice is that voice -- ssa_engine.h's ssa_release_voice, from
   SSA_RELEASE_FIRST: Hungarian, Turkish, ... -- written to out as a .BNS, its label in msg; a row no voice speaks yet
   (the Braille Lite unit screen's alone: the Braille 'n Speak 640, Japanese, the betas) is SSA_FW_RELEASE plus its
   row (blv_release_*), written to out, its label in msg -- the app keeps it under units/ for the unit screen, never
   offered to the phone's voices.  Apple's ssp_import_firmware does the same (SSP_FW_RELEASE + the row). */
int ssa_import_voice(const unsigned char *data, long n, const char *out, char *msg, int msglen);
#define SSA_FW_RELEASE 100             /* + a release table row no voice speaks: kept for the unit screen alone */

/* GW Micro's Sounding Board (0.8.5): its two DOS drivers, version 2.7, each known by its sha256 (sb_host.h's), written
   to out as it is with its label in msg -- one of the pair that makes the voice SSA_SOUNDINGBOARD (ssa_engine.h): the
   app imports it only with both (sb.zip holds them at its top, beside documentation never taken).  Else BLV_FW_NONE. */
#define SSA_FW_SBLOAD 90               /* SBLOAD.COM, "Text-To-Speech in DOS": the rules */
#define SSA_FW_SB 91                   /* SB.COM, the card driver */
int ssa_import_soundingboard(const unsigned char *data, long n, const char *out, char *msg, int msglen);

/* Automated Functions' SmarTalk: S_TALK.EXE 1.21, known by its sha256 (st_host.h's STH_SHA256), written to out as it
   is with its label in msg: the voice SSA_SMARTALK (ssa_engine.h).  Else BLV_FW_NONE.  The phone apps carry none:
   it comes from the person's own copy, the SmarTalk's disk or the NVDA add-on that has it. */
int ssa_import_smartalk(const unsigned char *data, long n, const char *out, char *msg, int msglen);

/* The Artic SynPhonix (0.9.5): VEST.COM 1.60, known by its content -- its banner (sp_host.h's SPH_BANNER) and the
   serial check the host reads the card's key from (sph_card_key) -- never by a hash: every copy holds its own card's
   serial.  Written to out as it is with its label in msg: the voice SSA_SYNPHONIX (ssa_engine.h).  Else BLV_FW_NONE.
   From the person's own copy (a Business Vision or SynPhonix disk) or the NVDA add-on that has it. */
int ssa_import_synphonix(const unsigned char *data, long n, const char *out, char *msg, int msglen);

/* A Blazie release's language (bl_firmware.h's BLV_FW_*, which is not a voice index: German's 2 would be the Accent
   SA's) as the app's voice, SSA_ENGLISH ... SSA_SLOVAK; -1 for none.  And back: the state recipe a Blazie voice's
   unit is made with (blv_firmware_recipe of its language, for blv_make_state: the Slovak unit's is the English
   one); -1 for a voice with no state to make. */
int ssa_blazie_voice(int language);
int ssa_blazie_recipe(int voice);

/* The labels of an imported Speak-Out and Mockingboard. */
#define SSA_SPEAKOUT_LABEL "GW Micro Speak-Out: SPEAKOUT.HEX"
#define SSA_SBLOAD_LABEL "GW Micro Sounding Board 2.7: SBLOAD.COM"
#define SSA_SB_LABEL "GW Micro Sounding Board 2.7: SB.COM"
#define SSA_SMARTALK_LABEL "SmarTalk: Automated Functions' S_TALK.EXE 1.21"
#define SSA_SYNPHONIX_LABEL "Artic SynPhonix: Artic's VEST.COM 1.60"
#define SSA_MOCKINGBOARD_LABEL "Mockingboard: Sweet Micro Systems' text-to-speech 1.1"
#define SSA_MOCKINGBOARD_EARLY_LABEL "Mockingboard, early: Sweet Micro Systems' text-to-speech of Mockingboard disk 1"

/* The test's controls (test_android_native.c sets them; the app never does): 1 drops the sha256 check, so any
   well-formed Intel HEX is taken as the Speak-Out's -- the "another HEX" cases must then fail; and any file of a
   Mockingboard file's size as that one -- its "one byte changed" cases must then fail.  2 never tries a disk image,
   so the .dsk cases must fail.  3 returns a Blazie release's raw firmware language (bl_firmware.h) instead of the
   app's voice, as before 0.8's table -- German's 2 is the Accent-PC's index -- so the German, Italian and Slovak
   imports must fail.  4: ssa_import_voice never tries the release table, so the release voices' imports (Hungarian
   ...) must fail.  5: a release no voice speaks is dropped, as before 0.8.5 (BLV_FW_UNKNOWN, out removed), so the
   unit screen's imports (the 640, Japanese) must fail.  6: the Sounding Board's drivers are never known, so its
   imports must fail.  7: the SmarTalk's S_TALK.EXE is never known, so its import must fail.  8: the SynPhonix's
   VEST.COM is never known, so its imports must fail. */
extern int ssa_import_break;

#ifdef __cplusplus
}
#endif
#endif
