/* The SSI-263 voices -- the Braille Lite 2000 (English and Spanish), the Speak-Out, the Accent-mini, the Accent SA
 * and the Mockingboard -- as a SAPI 5 engine, 32- and 64-bit.
 *
 * A fork of outspoken-nvda's sapi/outspoken_sapi.cpp, itself a fork of panthera-speech's panthera_sapi.cpp, which
 * is where the JAWS lessons live: only Speak/SpellOut/Pronounce fragments are assembled -- JAWS sends each word as
 * its own fragment with a bookmark between every pair, and a bookmark's text is its name -- and a space is
 * restored at the seam when neither side brought one.
 *
 * The voices run here, in the caller's process, in C: ssi263speech.dll beside this DLL (the same bitness, in x86\ or
 * x64\) carries the chip, the emulated units and each NVDA driver's text preparation -- the voice table of
 * src/csrc/voices.h, which the Linux and Android front ends share -- and the firmware sits in {app}\firmware.  Up to
 * 0.7.0 this DLL launched an embeddable Python running the NVDA drivers behind a pipe (sapi/ssi_serve.py); that script
 * stays in the repository as the reference, and sapi/test_native.py holds the native voices to it byte for byte, with
 * this DLL's own mapping of the settings (ssi_native.c).  The voices own every decision about how speech sounds: this
 * file only decides what a unit is given and when.  The units stay booted between utterances (the bank); a cancel is
 * the voice's own, between two 30 ms blocks.
 *
 * What a request is turned into (sapi/test_sapi_requests.py holds each):
 *   - fragments that ask for the same rate, volume and pitch are one unit utterance; a <rate>, <volume> or <pitch>
 *     inside the text begins another, and the program's own rate and volume combine with the fragment's;
 *   - a <spell> is each of its characters as an utterance of its own;
 *   - every block is written until SAPI has taken all of it (Windows 7 takes none while its buffer is full);
 *   - a skip request moves by sentences, from an estimate of where the unit is;
 *   - a bookmark's event is sent before the audio that follows it where its place is known (before the text,
 *     between two unit utterances, at the end); one inside a unit utterance follows that utterance's audio.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <olectl.h>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include "ssi_native.h"

static HMODULE g_module;
static long g_objects;
#ifdef SSI263_SAPI_DEV
/* The development build (sapi\build.ps1 -Dev, for sapi/test_sapi_engine.py): its own class and its own settings key
 * (or the one SSI263_SAPI_SETTINGS_KEY names), so it never touches the installed engine or its settings; and the
 * tests' must-fail hooks (SSI263_SAPI_TEST_BREAK=setting|voice). */
static const CLSID CLSID_Ssi263 = {0x3c0e5f7a,0x9d21,0x4e83,{0xb1,0x6f,0x52,0x0d,0x8a,0x4c,0x77,0x19}};
#define SETTINGS_KEY L"Software\\SSI-263 SAPI (development)"
#else
static const CLSID CLSID_Ssi263 = {0x616e7e0a,0x7b1a,0x4b94,{0x88,0x12,0xbb,0xb5,0x97,0xd2,0xc0,0x25}};
#define SETTINGS_KEY L"Software\\SSI-263 SAPI"
#endif
/* SPDFID_WaveFormatEx, by value: the ONE format GUID SAPI recognises as
 * "the WAVEFORMATEX that follows describes the audio".  A fresh GUID here
 * registers fine and then speaks silence -- SAPI cannot negotiate a format
 * it has never heard of.  Found by Tomi's ear inside ten minutes of the
 * first build reaching a real SAPI client. */
static const GUID Ssi263WaveFormatEx = {0xc31adbae,0x527f,0x4ff5,{0xa2,0x30,0xf6,0x2b,0xb6,0x1f,0xf7,0x0c}};
/* The output rates the add-ons offer (nvda/shared/ssi263_rates.py), 22 kHz by default.  The settings dialog's
 * SampleRate chooses one for every voice; GetOutputFormat declares it, and Speak renders at the rate SAPI then hands
 * back (so a setting changed in between can never put audio at an undeclared rate). */
static const DWORD DEFAULT_RATE = 22050;
static DWORD valid_rate(DWORD r) { return (r == 11025 || r == 22050 || r == 44100) ? r : DEFAULT_RATE; }
/* A unit that has given no sample for two minutes is cancelled (the Python server's deadline, which was for an
 * utterance still rendering after two minutes). */
static const DWORD DEADLINE_MS = 120000;

/* The black box -- **off unless somebody asks for it.**
 *
 * The live-client clip survived three fixes and only this file killed it,
 * which earned it a place here.  It did not earn the place it first took,
 * which was on, always, for everyone: a line per utterance is a line per
 * keystroke, appended forever to a file in %TEMP% that never rotates, and
 * the line carried the first forty characters of the text.  For a screen
 * reader that is a running transcript of somebody's mail, their messages
 * and their bank, in a folder anything running as them can read.
 *
 * `Diagnostics` in HKCU, beside the DataPath the settings program already
 * keeps there.  0, the default, writes nothing and creates no file.  1
 * writes the measurements, which are what actually convicted.  2 adds a
 * slice of the text, for the rare report that is about particular words --
 * two deliberate steps to reach the thing with words in it.  Capped either
 * way, because a diagnostic nobody turns off is a disk that fills. */
static const DWORD LOG_CAP = 4u * 1024u * 1024u;

/* Settings, per user then per machine, in "Software\\SSI-263 SAPI" (the settings dialog, settings.ps1, writes
 * this user's): Inflection (the Braille Lite's own on/off: 1 = on, the default), AccentInflection (the Accent's
 * intonation, 0 25 50 75 100 as its NVDA slider; 100 = full, the default), Whine (0 off, 1 hiss, 2 whine),
 * SampleRate (11025 / 22050 / 44100, every voice), RunAhead (the Braille Lite's "Run the unit ahead", EXPERIMENTAL:
 * 0 = off, the default; 1 = on, both its voices), LineLift (the Braille Lite's "Lift line starts (as note-taking
 * mode)": 0 = off, the default; 1 = on, both its voices -- a line spoken after a cancel or into a pause is lifted as
 * the unit lifts a line moved to, bl_voice.h), BrailleLiteNumbers (the Braille Lite's "Read numbers as words",
 * its NVDA driver's custom number processing: 1 = on, the default; 0 = the firmware reads the digits; both its voices,
 * the Accents and the Mockingboard keep their own) and Diagnostics (0 = off).  The Mockingboard has none of the
 * others: SAPI's rate and pitch and SampleRate are all it takes (the volume stays SAPI's, as for every voice).  Each
 * reaches the next thing spoken: read fresh per Speak, and a boot setting that changed (the rate, the inflection, the whine) boots the units again,
 * as the NVDA drivers do. */
static DWORD setting_dword(const wchar_t *name, DWORD def) {
    const HKEY roots[] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    const wchar_t *key = SETTINGS_KEY;
#ifdef SSI263_SAPI_DEV
    wchar_t own[256];      /* a test's own key (sapi_harness.cpp), so two tests never share their settings */
    if (GetEnvironmentVariableW(L"SSI263_SAPI_SETTINGS_KEY", own, 256) - 1u < 255u) key = own;
#endif
    for (int r = 0; r < 2; r++) {
        DWORD v = 0, size = sizeof v, type = 0;
        if (RegGetValueW(roots[r], key, name, RRF_RT_REG_DWORD, &type, &v, &size) == ERROR_SUCCESS)
            return v;
    }
    return def;
}
static int diagLevel() {
    return (int)setting_dword(L"Diagnostics", 0);
}
/* And clear up after 1.1.0, which wrote without asking.
 *
 * Everyone who installed it has a log in %TEMP% still growing a line per
 * utterance, with forty characters of each one in it, and turning the tap
 * off does not empty the bucket.  Once per process, with diagnostics off,
 * our own files go -- only the names this engine writes, only in the temp
 * folder, and only when nothing is meant to be being collected.  (The
 * serve logs are 0.7.0's, from the Python server.) */
static void sweep_logs() {
    static LONG done;
    if (InterlockedExchange(&done, 1) || diagLevel()) return;
    wchar_t dir[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"TEMP", dir, MAX_PATH);
    if (!n || n >= MAX_PATH - 48) return;
    const wchar_t *globs[] = {L"\\ssi263_sapi.log",
                              L"\\ssi263_sapi_serve-*.log"};
    for (int g = 0; g < 2; g++) {
        wchar_t pat[MAX_PATH]; lstrcpyW(pat, dir); lstrcatW(pat, globs[g]);
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            wchar_t victim[MAX_PATH];
            lstrcpyW(victim, dir); lstrcatW(victim, L"\\");
            lstrcatW(victim, fd.cFileName);
            DeleteFileW(victim);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

static void logline(const wchar_t *fmt, ...) {
    if (!diagLevel()) return;
    wchar_t path[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"TEMP", path, MAX_PATH);
    if (!n || n >= MAX_PATH - 24) return;
    lstrcatW(path, L"\\ssi263_sapi.log");
    HANDLE f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           0, OPEN_ALWAYS, 0, 0);
    if (f == INVALID_HANDLE_VALUE) return;
    {   /* Start over rather than grow without end. */
        LARGE_INTEGER sz;
        if (GetFileSizeEx(f, &sz) && sz.QuadPart > (LONGLONG)LOG_CAP) {
            CloseHandle(f);
            f = CreateFileW(path, GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, 0,
                            CREATE_ALWAYS, 0, 0);
            if (f == INVALID_HANDLE_VALUE) return;
        }
    }
    wchar_t line[512];
    va_list ap; va_start(ap, fmt);
    int len = _vsnwprintf_s(line, 512, _TRUNCATE, fmt, ap);
    va_end(ap);
    if (len < 0) len = 511;
    char out[1100]; int m = WideCharToMultiByte(CP_UTF8, 0, line, len, out, 1060, 0, 0);
    SYSTEMTIME st; GetLocalTime(&st);
    char stamp[32];
    int sn = sprintf_s(stamp, 32, "%02d:%02d:%02d.%03d ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    DWORD w;
    WriteFile(f, stamp, sn, &w, 0);
    WriteFile(f, out, m, &w, 0);
    WriteFile(f, "\r\n", 2, &w, 0);
    CloseHandle(f);
}

/* This DLL's folder ({app}\x86 or {app}\x64, where ssi263speech.dll sits beside it), and the install folder above it
 * ({app}, which holds firmware\). */
static std::wstring dll_dir() {
    wchar_t p[MAX_PATH]; GetModuleFileNameW(g_module,p,MAX_PATH);
    wchar_t *s=wcsrchr(p,L'\\'); if(s)*s=0;
    return p;
}
static std::wstring install_dir() {
    std::wstring d=dll_dir(); size_t slash=d.rfind(L'\\');
    if(slash!=std::wstring::npos){
        std::wstring leaf=d.substr(slash+1);
        if(leaf==L"x86"||leaf==L"x64") d.resize(slash);
    }
    return d;
}
static std::string utf8(const std::wstring &s) {
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),0,0,0,0);
    std::string r(n,0); if(n) WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),&r[0],n,0,0); return r;
}
static std::wstring token_string(ISpObjectToken *t, const wchar_t *name) {
    wchar_t *v=0; std::wstring r;
    if(t && SUCCEEDED(t->GetStringValue(name,&v)) && v) { r=v; CoTaskMemFree(v); }
    return r;
}

/* The voices: the library and one bank of booted units per process, shared by every Engine (SAPI makes one per
 * voice in use) and guarded, as the one pipe host was.  The library is loaded on first use and kept, booted units
 * and all, for the life of the process (DllCanUnloadNow). */
static CRITICAL_SECTION g_lock;
static bool g_lockReady;
static ssi_api g_api;
static ssv_bank *g_bank;
static char g_fwdir[MAX_PATH * 2];
static bool g_failed;              /* the library would not load: said once in the log, never retried per keystroke */

/* Scoped, so an exception unwinding out of the speak path releases the lock
 * instead of abandoning it -- an abandoned critical section turns the next
 * utterance into a deadlock, heard as speech dying for good. */
struct CsLock {
    CRITICAL_SECTION *cs;
    CsLock(CRITICAL_SECTION *c):cs(c){EnterCriticalSection(cs);}
    ~CsLock(){LeaveCriticalSection(cs);}
};

static bool voices_ready() {
    if (g_bank) return true;
    if (g_failed) return false;
    char err[256];
    std::wstring fw = install_dir() + L"\\firmware";
    if (!ssi_load(&g_api, dll_dir().c_str(), err, sizeof err)) {
        g_failed = true;
        logline(L"the voices could not load: %hs", err);
        return false;
    }
    if (!ssi_ansi_path(fw.c_str(), g_fwdir, sizeof g_fwdir) || !(g_bank = g_api.bank_new(g_fwdir))) {
        g_failed = true;
        logline(L"the firmware folder cannot be opened");
        FreeLibrary(g_api.dll);
        memset(&g_api, 0, sizeof g_api);
        return false;
    }
    return true;
}

/* The deadline: a unit that has said nothing for this long is cancelled.  The development build takes a test's own
 * (sapi/test_sapi_requests.py). */
static DWORD deadline_ms() {
#ifdef SSI263_SAPI_DEV
    char v[16]; DWORD n=GetEnvironmentVariableA("SSI263_SAPI_TEST_DEADLINE_MS",v,sizeof v);
    if(n&&n<sizeof v)return (DWORD)atol(v);
#endif
    return DEADLINE_MS;
}

/* A block handed to SAPI, all of it.  Write may take a part, and on Windows 7 it takes nothing while its buffer is
 * full: the rest is offered again, after a short wait when nothing was taken, until SAPI has it or asks for the stop
 * (looked for in the wait, so a purge is not kept waiting on a full buffer).  Up to 0.9.1 each block was written once
 * and what Write had taken was not looked at, so a block Windows 7 refused was a hole in the speech.  `total` counts
 * what SAPI took.  `once`: a control, the single Write it was. */
enum { WROTE, WRITE_ABORTED, WRITE_FAILED };
static int write_all(ISpTTSEngineSite *site,const void *data,ULONG bytes,unsigned long long &total,DWORD deadline,bool once) {
    const BYTE *p=(const BYTE*)data;
    DWORD waited=GetTickCount();
    while(bytes){
        ULONG took=0;
        if(FAILED(site->Write(p,bytes,&took))||took>bytes)return WRITE_FAILED;
        if(once){total+=bytes;return WROTE;}
        total+=took; p+=took; bytes-=took;
        if(took){waited=GetTickCount();continue;}
        if(site->GetActions()&SPVES_ABORT)return WRITE_ABORTED;
        if(GetTickCount()-waited>deadline)return WRITE_FAILED;      /* a site that takes nothing, ever */
        Sleep(5);
    }
    return WROTE;
}

/* One TTS_BOOKMARK event, the pacing contract: its name as SAPI gave it, its number where NVDA looks for it (wParam),
 * and the place in the audio it belongs to, in bytes. */
static void bookmark(ISpTTSEngineSite *site,const std::wstring &name,unsigned long long offset) {
    SPEVENT ev;memset(&ev,0,sizeof ev);
    ev.eEventId=SPEI_TTS_BOOKMARK;
    ev.elParamType=SPET_LPARAM_IS_STRING;
    ev.ullAudioStreamOffset=offset;
    ev.wParam=(WPARAM)_wtol(name.c_str());
    ev.lParam=(LPARAM)name.c_str();
    site->AddEvents(&ev,1);
}

/* Where a text's sentences begin: its first word, and the first word after a full stop, a question mark or an
 * exclamation mark (with any closing quote or bracket) that white space follows.  "3.50" is no boundary. */
static void sentence_starts(const std::wstring &t,std::vector<size_t> &out) {
    size_t i=0; const size_t n=t.size(); bool start=true;
    while(i<n){
        if(start){
            while(i<n&&iswspace(t[i]))i++;
            if(i>=n)break;
            out.push_back(i); start=false;
        }
        wchar_t c=t[i++];
        if(c==L'.'||c==L'?'||c==L'!'){
            size_t j=i;
            while(j<n&&(t[j]==L'.'||t[j]==L'?'||t[j]==L'!'||t[j]==L'"'||t[j]==L'\''||t[j]==L')'||t[j]==L']'
                        ||t[j]==0x201D||t[j]==0x2019))j++;
            if(j>=n||iswspace(t[j])){i=j;start=true;}
        }
    }
}

/* A spelled text's characters, each to be given to the unit as a text of its own: white space left out, a surrogate
 * pair kept whole, a combining mark or variation selector kept with the character before it. */
static void spell_units(const std::wstring &t,std::vector<std::wstring> &out) {
    for(size_t i=0;i<t.size();){
        if(iswspace(t[i])){i++;continue;}
        size_t end=i+1;
        if(t[i]>=0xD800&&t[i]<=0xDBFF&&end<t.size()&&t[end]>=0xDC00&&t[end]<=0xDFFF)end++;
        while(end<t.size()&&(t[end]==0xFE0E||t[end]==0xFE0F||(t[end]>=0x0300&&t[end]<=0x036F)))end++;
        out.push_back(t.substr(i,end-i));
        i=end;
    }
}

class Engine : public ISpTTSEngine, public ISpObjectWithToken {
    LONG refs; ISpObjectToken *token;
public:
    Engine():refs(1),token(0){InterlockedIncrement(&g_objects);}
    ~Engine(){if(token)token->Release();InterlockedDecrement(&g_objects);}
    STDMETHODIMP QueryInterface(REFIID i,void **p){
        if(!p)return E_POINTER; *p=0;
        if(i==IID_IUnknown||i==IID_ISpTTSEngine)*p=(ISpTTSEngine*)this;
        else if(i==IID_ISpObjectWithToken)*p=(ISpObjectWithToken*)this;
        else return E_NOINTERFACE; AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef(){return InterlockedIncrement(&refs);}
    STDMETHODIMP_(ULONG) Release(){ULONG n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    STDMETHODIMP SetObjectToken(ISpObjectToken *t){if(!t)return E_INVALIDARG;if(token)return E_UNEXPECTED;token=t;t->AddRef();return S_OK;}
    STDMETHODIMP GetObjectToken(ISpObjectToken **t){if(!t)return E_POINTER;*t=token;if(token)token->AddRef();return token?S_OK:S_FALSE;}
    STDMETHODIMP GetOutputFormat(const GUID*,const WAVEFORMATEX*,GUID *id,WAVEFORMATEX **wf){
        if(!id||!wf)return E_POINTER; *id=Ssi263WaveFormatEx;
        const DWORD r=valid_rate(setting_dword(L"SampleRate",DEFAULT_RATE));
        WAVEFORMATEX f={WAVE_FORMAT_PCM,1,r,r*2,2,16,0};
        *wf=(WAVEFORMATEX*)CoTaskMemAlloc(sizeof f);if(!*wf)return E_OUTOFMEMORY;**wf=f;return S_OK;
    }
    STDMETHODIMP Speak(DWORD,REFGUID,const WAVEFORMATEX *wfx,const SPVTEXTFRAG *frags,ISpTTSEngineSite *site){
        /* A COM method must never let an exception out: SAPI has no
         * handler for one and the client application dies of it. */
        try {
            /* Render at the rate SAPI agreed to (what GetOutputFormat declared then), not a setting re-read now. */
            DWORD rate=(wfx&&wfx->wFormatTag==WAVE_FORMAT_PCM)?valid_rate(wfx->nSamplesPerSec)
                                                            :valid_rate(setting_dword(L"SampleRate",DEFAULT_RATE));
            return speakInner(frags,site,rate);
        } catch(...) {
            return E_FAIL;
        }
    }
    HRESULT speakInner(const SPVTEXTFRAG *frags,ISpTTSEngineSite *site,DWORD outRate){
        if(!token||!site)return E_UNEXPECTED;
        /* Timing for the diagnostic log: SAPI's call to our first audio, and to the end -- what the engine adds,
         * apart from the playback buffering of the program that asked. */
        LARGE_INTEGER qpf,qt0,qfirst; QueryPerformanceFrequency(&qpf); QueryPerformanceCounter(&qt0); qfirst.QuadPart=0;
#ifdef SSI263_SAPI_DEV
        char brk[16]={0}; GetEnvironmentVariableA("SSI263_SAPI_TEST_BREAK",brk,sizeof brk);
#else
        const char brk[1]={0};
#endif
        /* Bookmarks are the pacing contract, not decoration: NVDA's SAPI
         * driver interleaves <Bookmark Mark="N"/> with the text and waits
         * for the TTS_BOOKMARK events to advance -- an engine that stays
         * silent about them is an engine whose indexes never arrive, and
         * NVDA's scheduler eventually purges what it thinks is a stuck
         * utterance.  That purge was heard as the end of speech clipping,
         * and the cold restart after it as arrowing lag. */
        struct Mark { std::wstring name; size_t chars; };
        /* A segment: what one unit utterance says.  SAPI's fragments are joined (JAWS's word after word) for as
         * long as they ask for the same rate, volume and pitch -- the unit then makes the sentence's intonation, as it
         * does for any text.  Where a fragment asks for another (a <rate>, <volume> or <pitch> inside the text; the
         * pitch NVDA raises for a capital) a new utterance begins, the only way a unit changes them.  A <spell>
         * fragment is a segment of its own, read a character at a time. */
        struct Seg { std::wstring text; bool spell; long rateAdj; int vol; long middle; std::vector<Mark> marks; };
        std::vector<Seg> segs;
        const bool flat=!strcmp(brk,"markup");      /* a control: no fragment's rate, volume or pitch is looked at */
        {
            Seg cur; cur.spell=false; cur.rateAdj=0; cur.vol=100; cur.middle=frags?frags->State.PitchAdj.MiddleAdj:0;
            const long first=cur.middle;
            for(auto f=frags;f;f=f->pNext){
                if(f->State.eAction==SPVA_Bookmark){
                    if(f->pTextStart&&f->ulTextLen){
                        Mark m; m.name.assign(f->pTextStart,f->ulTextLen);
                        m.chars=cur.text.size(); cur.marks.push_back(m);
                    }
                    continue;
                }
                switch(f->State.eAction){
                case SPVA_Speak: case SPVA_SpellOut: case SPVA_Pronounce: break;
                default: continue;
                }
                if(!f->pTextStart||!f->ulTextLen)continue;
                long r=f->State.RateAdj; r=r<-10?-10:r>10?10:r;
                int vol=f->State.Volume>100?100:(int)f->State.Volume;
                long mid=f->State.PitchAdj.MiddleAdj;
                if(flat){r=0;vol=100;mid=first;}
                const bool spell=f->State.eAction==SPVA_SpellOut&&strcmp(brk,"spell");   /* "spell": a control */
                if(!cur.text.empty()&&(spell||cur.spell||r!=cur.rateAdj||vol!=cur.vol||mid!=cur.middle)){
                    segs.push_back(cur);
                    cur.text.clear(); cur.marks.clear();
                }
                if(cur.text.empty()){cur.spell=spell;cur.rateAdj=r;cur.vol=vol;cur.middle=mid;}
                else if(!iswspace(cur.text.back())&&!iswspace(f->pTextStart[0]))
                    cur.text.push_back(L' ');
                cur.text.append(f->pTextStart,f->ulTextLen);
            }
            if(!cur.text.empty()||!cur.marks.empty())segs.push_back(cur);
        }
        size_t nchars=0,nmarks=0; bool anyText=false;
        for(size_t i=0;i<segs.size();i++){nchars+=segs[i].text.size();nmarks+=segs[i].marks.size();anyText|=!segs[i].text.empty();}
        if(segs.empty())return S_OK;
        std::string voiceId=utf8(token_string(token,L"VoiceId"));
        /* The program's own rate and volume (ISpVoice::SetRate, SetVolume) come from the site, and a fragment's
         * markup combines with them: the rate adds, the volume is a share of the program's.  Volume is the engine's
         * to apply -- SAPI hands the samples on as they are (measured: SetVolume 30 left this engine's samples as
         * they were, where Microsoft's own voice came out at an eighth) -- and up to 0.9.1 this engine did not. */
        long sapiRate=0; if(FAILED(site->GetRate(&sapiRate)))sapiRate=0;
        USHORT sapiVolume=100; if(FAILED(site->GetVolume(&sapiVolume))||sapiVolume>100)sapiVolume=100;
        if(flat)sapiVolume=100;
        /* The settings SAPI's own request cannot carry, read fresh so a change in the settings dialog reaches the
         * next thing spoken. */
        ssi_options o;
        ssi_options_defaults(&o);
        o.sample_rate=(int)outRate;
        o.inflection=setting_dword(L"Inflection",1)!=0;
        o.whine=(int)setting_dword(L"Whine",0);
        o.accent_inflection=(int)setting_dword(L"AccentInflection",100);
        o.run_ahead=setting_dword(L"RunAhead",0)!=0;
        o.line_lift=setting_dword(L"LineLift",0)!=0;
        o.numbers=setting_dword(L"BrailleLiteNumbers",1)!=0;
#ifdef SSI263_SAPI_DEV
        if(!strcmp(brk,"setting")){int r=o.sample_rate;ssi_options_defaults(&o);o.sample_rate=r;}   /* a control */
        if(!strcmp(brk,"bl-numbers"))o.numbers=1;   /* a control: BrailleLiteNumbers ignored (its default, on) */
#endif
        CsLock lock(&g_lock);
        sweep_logs();
        bool ok=true, aborted=false, ended=false;
        unsigned long long total=0;
        int idx=-1; ssv_voice *v=0;
        if(anyText){
            ok=voices_ready();
            char err[256]={0};
            if(ok){
                idx=ssi_voice(&g_api,voiceId.c_str(),g_fwdir);
#ifdef SSI263_SAPI_DEV
                if(idx>=0&&!strcmp(brk,"voice")){                     /* a control: English and Spanish swapped, */
                    int e=g_api.find("blazie:blazie"),s=g_api.find("blazie:blazie_es");
                    int m=g_api.find("mockingboard:mockingboard"),q=g_api.find("mockingboard:early");
                    int de=g_api.find("blazie:blazie_de"),it=g_api.find("blazie:blazie_it"),sk=g_api.find("blazie:blazie_sk");
                    int so=g_api.find("speakout:speakout"),sb=g_api.find("speakout:soundingboard");
                    int st=g_api.find("smartalk:smartalk");
                    if(idx==sb&&so>=0&&g_api.available(so,g_fwdir))idx=so;   /* 0.8.5: the Sounding Board as the
                                                                                 Speak-Out, its sibling */
                    else if(idx==e&&g_api.available(s,g_fwdir))idx=s; else if(idx==s&&g_api.available(e,g_fwdir))idx=e;
                    else if((idx==m||idx==q||idx==de||idx==it||idx==sk||idx==st
                             ||!strncmp(g_api.info(idx)->id,"blazie:",7))&&g_api.available(e,g_fwdir))idx=e;
                                                      /* either Mockingboard, 0.8's German, Italian and Slovak
                                                         units, the SmarTalk and every other Braille Lite voice
                                                         (the release table's), as ssi_voice's fallback would
                                                         speak them: the English Braille Lite */
                }
#endif
                ssv_boot b; ssi_boot(&o,&b); g_api.bank_boot(g_bank,&b);
                if(idx<0||!(v=g_api.bank_voice(g_bank,idx,err,sizeof err))){
                    logline(L"no voice for %hs: %hs",voiceId.c_str(),err);
                    ok=false;
                }
            }
        }
        /* Where sentences begin, for a skip request: at each segment's first word and after a full stop, a question
         * mark or an exclamation mark that white space (or the end) follows; a spelled segment is one. */
        struct Pos { size_t seg, chr; };
        std::vector<Pos> sentences;
        for(size_t i=0;i<segs.size();i++){
            const std::wstring &t=segs[i].text;
            if(t.empty())continue;
            if(segs[i].spell){Pos p={i,0};sentences.push_back(p);continue;}
            std::vector<size_t> at; sentence_starts(t,at);
            for(size_t k=0;k<at.size();k++){Pos p={i,at[k]};sentences.push_back(p);}
        }
        const bool marksLast=!strcmp(brk,"marks-end");   /* a control: every bookmark after all the audio, as it was */
        const DWORD deadline=deadline_ms();
        size_t si=0,startChr=0;
        while(ok&&!aborted&&!ended&&si<segs.size()){
            Seg &g=segs[si];
            const size_t from=startChr; startChr=0;
            /* A bookmark's event goes to SAPI before the audio that follows it wherever its place in the audio is
             * known exactly: here, at a segment's start (the bytes written so far), and at its end below.  One
             * inside a segment has no known place -- the unit speaks the segment whole -- so it follows the
             * segment's audio with an offset in proportion to its place in the text, an estimate. */
            if(!marksLast)
                for(size_t i=0;i<g.marks.size();i++)
                    if(g.marks[i].chars==from)bookmark(site,g.marks[i].name,total);
            if(g.text.empty()){si++;continue;}
            ssv_settings s;
            long r=sapiRate+g.rateAdj; r=r<-10?-10:r>10?10:r;
            /* SAPI's scales onto the drivers' 0-100 (ssi_native.c): SAPI zero is the middle of the NVDA slider, and
             * the ten-step ends are its ends. */
            ssi_settings(&g_api,idx,&o,ssi_sapi_rate(r),ssi_sapi_pitch(g.middle),(int)sapiVolume*g.vol/100,&s);
            /* What the unit is given: the segment's text (from the sentence a skip landed on), or for a spelled one
             * each character as a text of its own -- a lone character, as NVDA's character navigation sends one, so
             * the voice names it its own way. */
            std::vector<std::wstring> units;
            if(g.spell)spell_units(g.text,units); else units.push_back(g.text.substr(from));
            const unsigned long long segStart=total;
            bool skipped=false;
            for(size_t ui=0;ui<units.size()&&ok&&!aborted&&!ended&&!skipped;ui++){
                std::string u=utf8(units[ui]);
                int said=g_api.speak(v,&s,u.c_str(),0);
                if(said<0){ok=false;break;}
                bool done=false;
                unsigned long long unitBytes=0;
                DWORD quiet=GetTickCount();
                const DWORD began=quiet;
                while(said>0&&!done){
                    /* The cancel is the voice's own, between two blocks: the unit drops what it has not spoken
                     * and the next utterance starts clean -- warm, with nothing to drain. */
                    DWORD acts=site->GetActions();
                    /* The deadline is for a unit that says nothing more: time waiting on SAPI to take a block is
                     * not counted (it was, so a text that plays for more than two minutes was cut off). */
                    if((acts&SPVES_ABORT)||GetTickCount()-quiet>deadline){
                        aborted=true;
                        g_api.cancel(v);
                        break;
                    }
                    if((acts&SPVES_SKIP)&&strcmp(brk,"skip")){     /* "skip": a control, the request never looked at */
                        /* A skip (ISpVoice::Skip, a reading program's "next sentence"): so many sentences on or
                         * back from the one being spoken, 0 for its start.  The unit speaks a segment whole and
                         * does not say where in it it is, so the place is an estimate: the unit finishes the
                         * utterance unheard (a twenty-fifth of its length in time), and the share of its samples
                         * SAPI has taken is taken as the share of its text.  As many sentences are skipped as the
                         * text has; if that is what was asked, speech goes on from there, else the call ends
                         * (SAPI 5.4, TTS Engine Vendor Porting Guide). */
                        SPVSKIPTYPE type=SPVST_SENTENCE; long wanted=0,did=0; size_t cur=0;
                        const bool known=SUCCEEDED(site->GetSkipInfo(&type,&wanted))&&type==SPVST_SENTENCE&&!sentences.empty();
                        if(known){
                            unsigned long long whole=unitBytes; DWORD since=GetTickCount();
                            while(!done&&GetTickCount()-since<deadline){
                                if(site->GetActions()&SPVES_ABORT){aborted=true;break;}
                                int isdone=0; const short *pcm=0;
                                int n=g_api.render(v,&pcm,&isdone);
                                if(n>0)whole+=(unsigned long long)n*2;
                                done=isdone!=0;
                            }
                            if(aborted){g_api.cancel(v);break;}
                            size_t chr=from;
                            if(!g.spell&&whole)chr+=(size_t)((double)unitBytes/(double)whole*(double)units[ui].size());
                            for(size_t k=0;k<sentences.size();k++)
                                if(sentences[k].seg<si||(sentences[k].seg==si&&sentences[k].chr<=chr))cur=k;
                            const long back=(long)cur,ahead=(long)(sentences.size()-1-cur);
                            did=wanted<-back?-back:wanted>ahead?ahead:wanted;
                        }
                        g_api.cancel(v);
                        site->CompleteSkip(did);
                        if(!known||did!=wanted)ended=true;
                        else{si=sentences[cur+did].seg;startChr=sentences[cur+did].chr;skipped=true;}
                        break;
                    }
                    int isdone=0; const short *pcm=0;
                    int n=g_api.render(v,&pcm,&isdone);
                    done=isdone!=0;
                    if(n>0){
                        quiet=GetTickCount();
                        if(!qfirst.QuadPart)QueryPerformanceCounter(&qfirst);
                        int w=write_all(site,pcm,(ULONG)n*2,total,deadline,!strcmp(brk,"partial-write"));
                        if(w!=WROTE){g_api.cancel(v);if(w==WRITE_ABORTED)aborted=true;else ok=false;break;}
                        unitBytes+=(unsigned long long)n*2;
                        if(!strcmp(brk,"deadline-wall")&&GetTickCount()-began>deadline){   /* a control: time on SAPI
                                                                                              counted, as it was */
                            aborted=true; g_api.cancel(v); break;
                        }
                        quiet=GetTickCount();
                    }
                }
            }
            if(!ok||aborted||ended)break;
            if(skipped)continue;
            if(!marksLast){
                const double len=(double)(g.text.size()-from),bytes=(double)(total-segStart);
                for(size_t i=0;i<g.marks.size();i++)
                    if(g.marks[i].chars>from)
                        bookmark(site,g.marks[i].name,segStart+(unsigned long long)((double)(g.marks[i].chars-from)/len*bytes));
            }
            si++;
        }
        if(ok&&!aborted&&!ended){
            if(marksLast)
                for(size_t i=0;i<segs.size();i++)
                    for(size_t k=0;k<segs[i].marks.size();k++)bookmark(site,segs[i].marks[k].name,total);
            /* And the tail: the engines end at the last phoneme with zero
             * trailing frames, and SAPI's playback path eats the tail of
             * a stream that ends flush with its data.  150 ms of silence
             * makes what it eats silent.  NVDA's own player drains
             * properly, which is why the add-on never needed this. */
            if(total){
                std::vector<BYTE> pad((size_t)(outRate*3/20)*2,0);   /* 150 ms at the output rate */
                unsigned long long padded=total;
                write_all(site,pad.data(),(ULONG)pad.size(),padded,deadline,!strcmp(brk,"partial-write"));
            }
        }
        /* The measurements convicted all four bugs; the words never did. */
        LARGE_INTEGER qend; QueryPerformanceCounter(&qend);
        unsigned firstMs=qfirst.QuadPart?(unsigned)((qfirst.QuadPart-qt0.QuadPart)*1000/qpf.QuadPart):0;
        unsigned endMs=(unsigned)((qend.QuadPart-qt0.QuadPart)*1000/qpf.QuadPart);
        if(diagLevel()>=2)
            logline(L"speak done: chars=%u marks=%u bytes-written=%u ok=%d aborted=%d first-audio=%ums end=%ums text=\"%.40s\"",
                    (unsigned)nchars,(unsigned)nmarks,(unsigned)total,
                    ok?1:0,aborted?1:0,firstMs,endMs,segs[0].text.c_str());
        else
            logline(L"speak done: chars=%u marks=%u bytes-written=%u ok=%d aborted=%d first-audio=%ums end=%ums",
                    (unsigned)nchars,(unsigned)nmarks,(unsigned)total,
                    ok?1:0,aborted?1:0,firstMs,endMs);
        return aborted||ended||ok?S_OK:E_FAIL;
    }
};
class Factory:public IClassFactory{LONG refs;public:Factory():refs(1){InterlockedIncrement(&g_objects);} ~Factory(){InterlockedDecrement(&g_objects);} STDMETHODIMP QueryInterface(REFIID i,void**p){if(!p)return E_POINTER;*p=0;if(i==IID_IUnknown||i==IID_IClassFactory)*p=this;else return E_NOINTERFACE;AddRef();return S_OK;} STDMETHODIMP_(ULONG)AddRef(){return InterlockedIncrement(&refs);} STDMETHODIMP_(ULONG)Release(){ULONG n=InterlockedDecrement(&refs);if(!n)delete this;return n;} STDMETHODIMP CreateInstance(IUnknown*o,REFIID i,void**p){if(o)return CLASS_E_NOAGGREGATION;Engine*e=new Engine;HRESULT h=e->QueryInterface(i,p);e->Release();return h;} STDMETHODIMP LockServer(BOOL x){InterlockedExchangeAdd(&g_objects,x?1:-1);return S_OK;}};

/* The booted units stay for the life of the process, as 0.7.0's resident server did: a client that lets go of its
 * last engine object and makes a new one (System.Speech's SelectVoice does) must not pay a cold boot on its next
 * utterance.  So nothing is torn down here, and so this DLL never unloads while its voices are loaded (it holds the
 * library; FreeLibrary is not for DllMain either). */
STDAPI DllCanUnloadNow(){
    if(g_objects)return S_FALSE;
    if(g_lockReady){CsLock lock(&g_lock);if(g_bank||g_api.dll)return S_FALSE;}
    return S_OK;
}
STDAPI DllGetClassObject(REFCLSID c,REFIID i,void **p){if(c!=CLSID_Ssi263)return CLASS_E_CLASSNOTAVAILABLE;Factory*f=new Factory;HRESULT h=f->QueryInterface(i,p);f->Release();return h;}
static HRESULT reg(bool add){
    wchar_t cls[64];StringFromGUID2(CLSID_Ssi263,cls,64);std::wstring key=L"Software\\Classes\\CLSID\\"+std::wstring(cls);
    if(!add){RegDeleteTreeW(HKEY_LOCAL_MACHINE,key.c_str());return S_OK;}
    HKEY h,k; if(RegCreateKeyExW(HKEY_LOCAL_MACHINE,key.c_str(),0,0,0,KEY_WRITE,0,&h,0))return SELFREG_E_CLASS;
    const wchar_t name[]=L"SSI-263 SAPI speech engine";RegSetValueExW(h,0,0,REG_SZ,(BYTE*)name,sizeof(name));
    wchar_t self[MAX_PATH];GetModuleFileNameW(g_module,self,MAX_PATH);
    std::wstring sub=key+L"\\InprocServer32",path=self;RegCloseKey(h);
    if(RegCreateKeyExW(HKEY_LOCAL_MACHINE,sub.c_str(),0,0,0,KEY_WRITE,0,&k,0))return SELFREG_E_CLASS;
    RegSetValueExW(k,0,0,REG_SZ,(BYTE*)path.c_str(),(DWORD)((path.size()+1)*2));const wchar_t both[]=L"Both";RegSetValueExW(k,L"ThreadingModel",0,REG_SZ,(BYTE*)both,sizeof(both));RegCloseKey(k);return S_OK;
}
STDAPI DllRegisterServer(){return reg(true);} STDAPI DllUnregisterServer(){return reg(false);}
BOOL WINAPI DllMain(HINSTANCE h,DWORD why,LPVOID){
    if(why==DLL_PROCESS_ATTACH){g_module=h;DisableThreadLibraryCalls(h);
        if(!g_lockReady){InitializeCriticalSection(&g_lock);g_lockReady=true;}}
    return TRUE;
}
