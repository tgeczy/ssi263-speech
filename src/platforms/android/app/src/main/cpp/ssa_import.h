/* ssa_import.h -- the Speak-Out's firmware and the Mockingboard's (below), as a user brings them.  The Speak-Out's:
 * GW Micro's SPEAKOUT.HEX, judged by its content -- the Intel HEX format, then the known firmware's sha256
 * (firmware/gw-micro-speakout/README.txt's, the one nvda/build_speakout.py builds the add-on from and
 * check_apk_no_firmware.py keeps out of the APK) -- never by its name.  The Braille Lite's is bl_firmware.h's;
 * ssa_import_firmware asks that one first and these when it finds none.
 *
 * The app never carries these files: the user imports them (FirmwareImport.kt, SsiImport.kt).  Plain C, no JNI: the
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

/* The Mockingboard's: Sweet Micro Systems' text-to-speech 1.1 as one file, mockingboard-tts-1.1.bin (the toolkit
   disk's six files back to back, tools/mockingboard_firmware.py's), known by its sha256 alone (mb_host.h's
   MB_SHA256, the only one its host runs).  The app never carries it either: Sweet Micro's founder can be asked, so
   it is treated as Blazie's is (Tomi, 2026-10-09). */
#define SSA_FW_MOCKINGBOARD 5          /* = ssa_engine.h's SSA_MOCKINGBOARD */
#define SSA_FW_MB_BUILD (-6)           /* the Mockingboard's file, but this build has no Mockingboard */

/* Judges data: when it is the Mockingboard's file, byte for byte, writes it to out and returns SSA_FW_MOCKINGBOARD
   with its label in msg; else BLV_FW_NONE (not it), SSA_FW_MB_BUILD or BLV_FW_WRITE with the reason in msg (out
   removed). */
int ssa_import_mockingboard(const unsigned char *data, long n, const char *out, char *msg, int msglen);

/* Every firmware the app imports, by content (the JNI bridge's nativeImportFirmware): bl_firmware.h's
   blv_import_firmware first -- BLV_FW_ENGLISH 0 or _SPANISH 1, written to out as a .BNS -- and, when it finds no
   Braille Lite firmware, ssa_import_mockingboard -- SSA_FW_MOCKINGBOARD 5, written to out as it is -- then
   ssa_import_speakout -- SSA_HEX_SPEAKOUT 3, written to out as SPEAKOUT.HEX.  Else negative (BLV_FW_NONE,
   _REFUSED, _WRITE, _UNKNOWN; SSA_HEX_OTHER; SSA_FW_MB_BUILD), the reason in msg. */
int ssa_import_firmware(const unsigned char *data, long n, const char *out, char *msg, int msglen);

/* The labels of an imported Speak-Out and Mockingboard. */
#define SSA_SPEAKOUT_LABEL "GW Micro Speak-Out: SPEAKOUT.HEX"
#define SSA_MOCKINGBOARD_LABEL "Mockingboard: Sweet Micro Systems' text-to-speech 1.1"

/* The test's control (test_android_native.c sets it; the app never does): nonzero drops the sha256 check, so any
   well-formed Intel HEX is taken as the Speak-Out's -- the "another HEX" cases must then fail; and any file of the
   Mockingboard's size as the Mockingboard's -- its "one byte changed" case must then fail. */
extern int ssa_import_break;

#ifdef __cplusplus
}
#endif
#endif
