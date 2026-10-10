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
 * this DLL's own mapping of the settings (ssi_native.c).  There is no text processing in this file: the voices own
 * every decision about how speech sounds.  The units stay booted between utterances (the bank); a cancel is the
 * voice's own, between two 30 ms blocks.
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
/* An utterance still rendering after two minutes is cancelled (the Python server's deadline). */
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
 * 0 = off, the default; 1 = on, both its voices), BrailleLiteNumbers (the Braille Lite's "Read numbers as words",
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
        std::wstring text;
        /* Bookmarks are the pacing contract, not decoration: NVDA's SAPI
         * driver interleaves <Bookmark Mark="N"/> with the text and waits
         * for the TTS_BOOKMARK events to advance -- an engine that stays
         * silent about them is an engine whose indexes never arrive, and
         * NVDA's scheduler eventually purges what it thinks is a stuck
         * utterance.  That purge was heard as the end of speech clipping,
         * and the cold restart after it as arrowing lag. */
        struct Mark { std::wstring name; size_t chars; };
        std::vector<Mark> marks;
        for(auto f=frags;f;f=f->pNext){
            if(f->State.eAction==SPVA_Bookmark){
                if(f->pTextStart&&f->ulTextLen){
                    Mark m; m.name.assign(f->pTextStart,f->ulTextLen);
                    m.chars=text.size(); marks.push_back(m);
                }
                continue;
            }
            switch(f->State.eAction){
            case SPVA_Speak: case SPVA_SpellOut: case SPVA_Pronounce: break;
            default: continue;
            }
            if(!f->pTextStart||!f->ulTextLen)continue;
            if(!text.empty()&&!iswspace(text.back())&&!iswspace(f->pTextStart[0]))
                text.push_back(L' ');
            text.append(f->pTextStart,f->ulTextLen);
        }
        if(text.empty()&&marks.empty())return S_OK;
        std::string voiceId=utf8(token_string(token,L"VoiceId"));
        long sapiRate=0; site->GetRate(&sapiRate);
        /* SAPI's scales onto the drivers' 0-100 (ssi_native.c): SAPI zero is the middle of the NVDA slider, and the
         * ten-step ends are its ends.  SAPI applies the application's volume itself: the voice speaks at 100. */
        int rate=ssi_sapi_rate(sapiRate);
        int pitch=ssi_sapi_pitch(frags?frags->State.PitchAdj.MiddleAdj:0);
        int volume=100;
        /* The settings SAPI's own request cannot carry, read fresh so a change in the settings dialog reaches the
         * next thing spoken. */
        ssi_options o;
        ssi_options_defaults(&o);
        o.sample_rate=(int)outRate;
        o.inflection=setting_dword(L"Inflection",1)!=0;
        o.whine=(int)setting_dword(L"Whine",0);
        o.accent_inflection=(int)setting_dword(L"AccentInflection",100);
        o.run_ahead=setting_dword(L"RunAhead",0)!=0;
        o.numbers=setting_dword(L"BrailleLiteNumbers",1)!=0;
#ifdef SSI263_SAPI_DEV
        char brk[16]={0}; GetEnvironmentVariableA("SSI263_SAPI_TEST_BREAK",brk,sizeof brk);
        if(!strcmp(brk,"setting")){int r=o.sample_rate;ssi_options_defaults(&o);o.sample_rate=r;}   /* a control */
        if(!strcmp(brk,"bl-numbers"))o.numbers=1;   /* a control: BrailleLiteNumbers ignored (its default, on) */
#endif
        std::string u=utf8(text);
        CsLock lock(&g_lock);
        sweep_logs();
        bool ok=true, aborted=false, done=false;
        unsigned long long total=0;
        if(!text.empty()){
            ok=voices_ready();
            int idx=-1; ssv_voice *v=0; char err[256]={0};
            if(ok){
                idx=ssi_voice(&g_api,voiceId.c_str(),g_fwdir);
#ifdef SSI263_SAPI_DEV
                if(idx>=0&&!strcmp(brk,"voice")){                     /* a control: English and Spanish swapped, */
                    int e=g_api.find("blazie:blazie"),s=g_api.find("blazie:blazie_es");
                    int m=g_api.find("mockingboard:mockingboard");    /* the Mockingboard as ssi_voice's fallback */
                    if(idx==e&&g_api.available(s,g_fwdir))idx=s; else if(idx==s&&g_api.available(e,g_fwdir))idx=e;
                    else if(idx==m&&g_api.available(e,g_fwdir))idx=e;  /* would speak it: the Braille Lite */
                }
#endif
                ssv_boot b; ssi_boot(&o,&b); g_api.bank_boot(g_bank,&b);
                if(idx<0||!(v=g_api.bank_voice(g_bank,idx,err,sizeof err))){
                    logline(L"no voice for %hs: %hs",voiceId.c_str(),err);
                    ok=false;
                }
            }
            if(ok){
                ssv_settings s; ssi_settings(&g_api,idx,&o,rate,pitch,volume,&s);
                int said=g_api.speak(v,&s,u.c_str(),0);
                DWORD t0=GetTickCount();
                if(said<0)ok=false;
                while(ok&&said>0&&!done){
                    /* The cancel is the voice's own, between two blocks: the unit drops what it has not spoken
                     * and the next utterance starts clean -- warm, with nothing to drain. */
                    if((site->GetActions()&SPVES_ABORT)||GetTickCount()-t0>DEADLINE_MS){
                        aborted=true;
                        g_api.cancel(v);
                        break;
                    }
                    int isdone=0; const short *pcm=0;
                    int n=g_api.render(v,&pcm,&isdone);
                    done=isdone!=0;
                    if(n>0){
                        if(!qfirst.QuadPart)QueryPerformanceCounter(&qfirst);
                        ULONG wrote=0;
                        if(FAILED(site->Write(pcm,(ULONG)n*2,&wrote))){g_api.cancel(v);ok=false;break;}
                        total+=(ULONG)n*2;
                    }
                }
            }
        }
        if(ok&&!aborted){
            /* The pacing contract: one TTS_BOOKMARK event per bookmark
             * fragment.  The audio streamed as one utterance, so the
             * offsets are proportional estimates by character position --
             * NVDA schedules the index at its own player position when
             * the event arrives, so arrival is what unblocks it and the
             * offset is bookkeeping. */
            for(size_t i=0;i<marks.size();i++){
                SPEVENT ev;memset(&ev,0,sizeof ev);
                ev.eEventId=SPEI_TTS_BOOKMARK;
                ev.elParamType=SPET_LPARAM_IS_STRING;
                ev.ullAudioStreamOffset=text.empty()?0:
                    (unsigned long long)((double)marks[i].chars/(double)text.size()*(double)total);
                ev.wParam=(WPARAM)_wtol(marks[i].name.c_str());
                ev.lParam=(LPARAM)marks[i].name.c_str();
                site->AddEvents(&ev,1);
            }
            /* And the tail: the engines end at the last phoneme with zero
             * trailing frames, and SAPI's playback path eats the tail of
             * a stream that ends flush with its data.  150 ms of silence
             * makes what it eats silent.  NVDA's own player drains
             * properly, which is why the add-on never needed this. */
            if(total){
                std::vector<BYTE> pad((size_t)(outRate*3/20)*2,0);   /* 150 ms at the output rate */
                ULONG wrote=0;site->Write(pad.data(),(ULONG)pad.size(),&wrote);
            }
        }
        /* The measurements convicted all four bugs; the words never did. */
        LARGE_INTEGER qend; QueryPerformanceCounter(&qend);
        unsigned firstMs=qfirst.QuadPart?(unsigned)((qfirst.QuadPart-qt0.QuadPart)*1000/qpf.QuadPart):0;
        unsigned endMs=(unsigned)((qend.QuadPart-qt0.QuadPart)*1000/qpf.QuadPart);
        if(diagLevel()>=2)
            logline(L"speak done: chars=%u marks=%u bytes-written=%u ok=%d aborted=%d first-audio=%ums end=%ums text=\"%.40s\"",
                    (unsigned)text.size(),(unsigned)marks.size(),(unsigned)total,
                    ok?1:0,aborted?1:0,firstMs,endMs,text.c_str());
        else
            logline(L"speak done: chars=%u marks=%u bytes-written=%u ok=%d aborted=%d first-audio=%ums end=%ums",
                    (unsigned)text.size(),(unsigned)marks.size(),(unsigned)total,
                    ok?1:0,aborted?1:0,firstMs,endMs);
        return aborted||ok?S_OK:E_FAIL;
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
