/* sapi_harness.cpp -- the SAPI engine DLL driven the way SAPI drives it, with no registration at all: the DLL loaded
 * from a path, its class factory taken from DllGetClassObject, the engine given a token (a stand-in holding only the
 * VoiceId, which is all the engine reads), GetOutputFormat asked, and Speak called with SAPI's fragments -- text,
 * bookmarks, the pitch's MiddleAdj -- and an engine site of our own that answers GetRate, collects every Write and
 * every event, and raises SPVES_ABORT after a given number of writes (SAPI's purge).
 *
 * Why not SAPI itself: SAPI refuses a voice token kept under HKCU (E_ACCESSDENIED at Speak, a Microsoft voice's
 * copied token included), and System.Speech does not even list one, so a development build cannot reach SAPI without
 * machine-wide tokens -- which would sit beside, and could be mistaken for, the installed engine's.  This drives the
 * same COM object in the same way, for sapi/test_sapi_engine.py.  Built by sapi\build.ps1 -Dev, x86 and x64.
 *
 *   sapi_harness.exe <ssi263_sapi.dll> <job file> <output folder>
 *
 * The job: one case per line, UTF-8, tab-separated --
 *     voice id, SAPI rate (-10..10), MiddleAdj (-10..10), settings "Inflection,Whine,AccentInflection,RunAhead,
 *     SampleRate,BrailleLiteNumbers" (a "-" deletes that value: the engine's default), SSI263_SAPI_TEST_BREAK for this
 *     case ("-" for none), writes before an abort (0: none), text, and optionally a second text after a bookmark
 *     named 7.
 * The settings go to the key SSI263_SAPI_SETTINGS_KEY names (the development DLL reads that one).  For each case the
 * audio goes to <folder>\case_<k>.pcm and a line to stdout:
 *     case <k> hr=<hex> rate=<declared Hz> bytes=<written> writes=<n> marks=<names, comma-separated or -> ms=<n>
 *
 * A line whose first field is "v2" is a request with everything SAPI can put in one (sapi/test_sapi_requests.py):
 *     v2, voice id, SAPI rate, SAPI volume (0..100), settings, break, how the site takes writes, its actions, then one
 *     field per fragment.
 *   writes:    all | max=N (at most N bytes of each Write are taken) | zero=K (each block is refused K times first: a
 *              Write that takes nothing, Windows 7's full buffer) | slow=MS (each Write takes MS milliseconds)
 *   actions:   - or a comma list of abort@W (SPVES_ABORT once W writes were taken) and skip@W:N[:x] (SPVES_SKIP once W
 *              writes were taken, N sentences; :x asks for a kind that is not sentences)
 *   fragments: S:rate:volume:middle:text (spoken), L:rate:volume:middle:text (SPVA_SpellOut, a <spell>), B:name (a
 *              bookmark); rate is the fragment's RateAdj, volume its Volume (100 when SAPI saw no <volume>), middle
 *              its PitchAdj.MiddleAdj
 * Its line adds calls=<every Write, taken or not>, events=<each bookmark as name@bytes written when it came/its
 * offset, or -> and skips=<each CompleteSkip's count, or ->.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <sperror.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>

static std::wstring wide(const std::string &s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), 0, 0);
    std::wstring w(n, 0);
    if (n) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

/* A token that holds only a VoiceId. */
class Token : public ISpObjectToken {
    LONG refs; std::wstring id;
public:
    Token(const std::wstring &v) : refs(1), id(v) {}
    STDMETHODIMP QueryInterface(REFIID i, void **p) {
        if (!p) return E_POINTER;
        *p = 0;
        if (i == IID_IUnknown || i == IID_ISpDataKey || i == IID_ISpObjectToken) *p = (ISpObjectToken *)this;
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&refs); }
    STDMETHODIMP_(ULONG) Release() { ULONG n = InterlockedDecrement(&refs); if (!n) delete this; return n; }
    STDMETHODIMP GetStringValue(LPCWSTR name, LPWSTR *v) {
        if (!v) return E_POINTER;
        if (!name || wcscmp(name, L"VoiceId")) return SPERR_NOT_FOUND;
        size_t n = (id.size() + 1) * sizeof(wchar_t);
        *v = (LPWSTR)CoTaskMemAlloc(n);
        if (!*v) return E_OUTOFMEMORY;
        memcpy(*v, id.c_str(), n);
        return S_OK;
    }
    STDMETHODIMP SetData(LPCWSTR, ULONG, const BYTE *) { return E_NOTIMPL; }
    STDMETHODIMP GetData(LPCWSTR, ULONG *, BYTE *) { return SPERR_NOT_FOUND; }
    STDMETHODIMP SetStringValue(LPCWSTR, LPCWSTR) { return E_NOTIMPL; }
    STDMETHODIMP SetDWORD(LPCWSTR, DWORD) { return E_NOTIMPL; }
    STDMETHODIMP GetDWORD(LPCWSTR, DWORD *) { return SPERR_NOT_FOUND; }
    STDMETHODIMP OpenKey(LPCWSTR, ISpDataKey **) { return SPERR_NOT_FOUND; }
    STDMETHODIMP CreateKey(LPCWSTR, ISpDataKey **) { return E_NOTIMPL; }
    STDMETHODIMP DeleteKey(LPCWSTR) { return E_NOTIMPL; }
    STDMETHODIMP DeleteValue(LPCWSTR) { return E_NOTIMPL; }
    STDMETHODIMP EnumKeys(ULONG, LPWSTR *) { return SPERR_NO_MORE_ITEMS; }
    STDMETHODIMP EnumValues(ULONG, LPWSTR *) { return SPERR_NO_MORE_ITEMS; }
    STDMETHODIMP SetId(LPCWSTR, LPCWSTR, BOOL) { return E_NOTIMPL; }
    STDMETHODIMP GetId(LPWSTR *) { return E_NOTIMPL; }
    STDMETHODIMP GetCategory(ISpObjectTokenCategory **) { return E_NOTIMPL; }
    STDMETHODIMP CreateInstance(IUnknown *, DWORD, REFIID, void **) { return E_NOTIMPL; }
    STDMETHODIMP GetStorageFileName(REFCLSID, LPCWSTR, LPCWSTR, ULONG, LPWSTR *) { return E_NOTIMPL; }
    STDMETHODIMP RemoveStorageFileName(REFCLSID, LPCWSTR, BOOL) { return E_NOTIMPL; }
    STDMETHODIMP Remove(const CLSID *) { return E_NOTIMPL; }
    STDMETHODIMP IsUISupported(LPCWSTR, void *, ULONG, IUnknown *, BOOL *) { return E_NOTIMPL; }
    STDMETHODIMP DisplayUI(HWND, LPCWSTR, LPCWSTR, void *, ULONG, IUnknown *) { return E_NOTIMPL; }
    STDMETHODIMP MatchesAttributes(LPCWSTR, BOOL *) { return E_NOTIMPL; }
};

/* SAPI's side of a Speak: the rate, the writes, the events, and a purge after `abort_after` writes. */
class Site : public ISpTTSEngineSite {
    LONG refs;
public:
    long rate; int abort_after, writes; std::vector<BYTE> audio; std::vector<std::wstring> marks;
    /* v2: the volume, how writes are taken, a skip request, and what came back */
    USHORT volume; int calls, max_take, refuse, refused, slow_ms;
    int skip_after; long skip_count; bool skip_other, skip_done;
    std::vector<std::string> events; std::vector<long> skips;
    Site() : refs(1), rate(0), abort_after(0), writes(0), volume(100), calls(0), max_take(0), refuse(0), refused(0),
             slow_ms(0), skip_after(0), skip_count(0), skip_other(false), skip_done(false) {}
    STDMETHODIMP QueryInterface(REFIID i, void **p) {
        if (!p) return E_POINTER;
        *p = 0;
        if (i == IID_IUnknown || i == IID_ISpEventSink || i == IID_ISpTTSEngineSite) *p = (ISpTTSEngineSite *)this;
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&refs); }
    STDMETHODIMP_(ULONG) Release() { return InterlockedDecrement(&refs); }       /* on the stack */
    STDMETHODIMP AddEvents(const SPEVENT *ev, ULONG n) {
        for (ULONG k = 0; k < n; k++)
            if (ev[k].eEventId == SPEI_TTS_BOOKMARK && ev[k].elParamType == SPET_LPARAM_IS_STRING && ev[k].lParam) {
                marks.push_back((const wchar_t *)ev[k].lParam);
                char name[64], line[160];
                WideCharToMultiByte(CP_UTF8, 0, (const wchar_t *)ev[k].lParam, -1, name, 64, 0, 0);
                _snprintf(line, sizeof line, "%s@%lu/%llu", name, (unsigned long)audio.size(), ev[k].ullAudioStreamOffset);
                events.push_back(line);
            }
        return S_OK;
    }
    STDMETHODIMP GetEventInterest(ULONGLONG *p) { if (p) *p = ~0ull; return S_OK; }
    STDMETHODIMP_(DWORD) GetActions() {
        DWORD a = abort_after && writes >= abort_after ? SPVES_ABORT : 0;
        if (skip_after && !skip_done && writes >= skip_after) a |= SPVES_SKIP;
        return a;
    }
    STDMETHODIMP Write(const void *p, ULONG n, ULONG *done) {
        ULONG take = n;
        calls++;
        if (slow_ms) Sleep(slow_ms);
        if (refuse && refused < refuse) { refused++; take = 0; }     /* the buffer is full: nothing is taken */
        else {
            refused = 0;
            if (max_take && take > (ULONG)max_take) take = (ULONG)max_take;
        }
        audio.insert(audio.end(), (const BYTE *)p, (const BYTE *)p + take);
        if (take) writes++;
        if (done) *done = take;
        return S_OK;
    }
    STDMETHODIMP GetRate(long *r) { if (r) *r = rate; return S_OK; }
    STDMETHODIMP GetVolume(USHORT *v) { if (v) *v = volume; return S_OK; }
    STDMETHODIMP GetSkipInfo(SPVSKIPTYPE *t, long *n) {
        if (t) *t = skip_other ? (SPVSKIPTYPE)(SPVST_SENTENCE + 7) : SPVST_SENTENCE;
        if (n) *n = skip_count;
        return S_OK;
    }
    STDMETHODIMP CompleteSkip(long n) { skips.push_back(n); skip_done = true; return S_OK; }
};

static std::vector<std::string> split(const std::string &s, char c) {
    std::vector<std::string> out; size_t a = 0, b;
    while ((b = s.find(c, a)) != std::string::npos) { out.push_back(s.substr(a, b - a)); a = b + 1; }
    out.push_back(s.substr(a));
    return out;
}

int wmain(int argc, wchar_t **argv) {
    if (argc < 4) { fputs("sapi_harness.exe <ssi263_sapi.dll> <job> <folder>\n", stderr); return 2; }
    CoInitializeEx(0, COINIT_MULTITHREADED);
    HMODULE dll = LoadLibraryExW(argv[1], 0, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!dll) { fprintf(stderr, "cannot load %ls (%lu)\n", argv[1], GetLastError()); return 1; }
    typedef HRESULT (STDAPICALLTYPE *GetClassObject)(REFCLSID, REFIID, void **);
    GetClassObject get = (GetClassObject)GetProcAddress(dll, "DllGetClassObject");
    static const CLSID dev = {0x3c0e5f7a,0x9d21,0x4e83,{0xb1,0x6f,0x52,0x0d,0x8a,0x4c,0x77,0x19}};
    IClassFactory *factory = 0;
    if (!get || FAILED(get(dev, IID_IClassFactory, (void **)&factory))) {
        fputs("not the development engine (no SSI263_SAPI_DEV class)\n", stderr);
        return 1;
    }
    wchar_t keyname[256] = L"Software\\SSI-263 SAPI (development)";
    GetEnvironmentVariableW(L"SSI263_SAPI_SETTINGS_KEY", keyname, 256);
    FILE *job = _wfopen(argv[2], L"rb");
    if (!job) { fputs("no job file\n", stderr); return 1; }
    std::string all; char buf[4096]; size_t got;
    while ((got = fread(buf, 1, sizeof buf, job)) > 0) all.append(buf, got);
    fclose(job);
    int k = 0;
    for (const std::string &line0 : split(all, '\n')) {
        std::string line = line0;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> f = split(line, '\t');
        const bool v2 = !f.empty() && f[0] == "v2";
        if (v2 ? f.size() < 9 : f.size() < 7) continue;
        if (v2) {       /* put the fields the first format has where it has them: voice, rate, -, settings, break */
            std::vector<std::string> g;
            g.push_back(f[1]); g.push_back(f[2]); g.push_back("0"); g.push_back(f[4]); g.push_back(f[5]);
            g.push_back("0"); g.push_back("");
            g.insert(g.end(), f.begin() + 3, f.end());      /* [7] volume, [8] settings, [9] break, [10] writes,
                                                               [11] actions, [12..] fragments */
            f.swap(g);
        }
        /* the settings, then the break for this case */
        std::vector<std::string> s = split(f[3], ',');
        const wchar_t *names[] = {L"Inflection", L"Whine", L"AccentInflection", L"RunAhead", L"SampleRate",
                                  L"BrailleLiteNumbers"};
        const int nnames = (int)(sizeof names / sizeof names[0]);
        HKEY h;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, keyname, 0, 0, 0, KEY_WRITE, 0, &h, 0) == ERROR_SUCCESS) {
            for (int i = 0; i < nnames && i < (int)s.size(); i++) {
                if (s[i] == "-") { RegDeleteValueW(h, names[i]); continue; }       /* no value: the engine's default */
                DWORD v = (DWORD)atol(s[i].c_str());
                RegSetValueExW(h, names[i], 0, REG_DWORD, (const BYTE *)&v, sizeof v);
            }
            RegCloseKey(h);
        }
        SetEnvironmentVariableA("SSI263_SAPI_TEST_BREAK", f[4] == "-" ? 0 : f[4].c_str());
        ISpTTSEngine *engine = 0;
        ISpObjectWithToken *with = 0;
        HRESULT hr = factory->CreateInstance(0, IID_ISpTTSEngine, (void **)&engine);
        Token *tok = new Token(wide(f[0]));
        if (SUCCEEDED(hr)) hr = engine->QueryInterface(IID_ISpObjectWithToken, (void **)&with);
        if (SUCCEEDED(hr)) hr = with->SetObjectToken(tok);
        GUID fmt; WAVEFORMATEX *wfx = 0;
        if (SUCCEEDED(hr)) hr = engine->GetOutputFormat(0, 0, &fmt, &wfx);
        Site site;
        site.rate = atol(f[1].c_str());
        site.abort_after = atoi(f[5].c_str());
        long middle = atol(f[2].c_str());
        std::wstring t1 = wide(f[6]), t2 = !v2 && f.size() > 7 ? wide(f[7]) : L"", seven = L"7";
        SPVTEXTFRAG fr[3];
        memset(fr, 0, sizeof fr);
        /* Volume 100: what SAPI puts in a fragment no <volume> reached */
        for (int i = 0; i < 3; i++) {
            fr[i].State.eAction = SPVA_Speak; fr[i].State.PitchAdj.MiddleAdj = middle; fr[i].State.Volume = 100;
        }
        fr[0].pTextStart = t1.c_str(); fr[0].ulTextLen = (ULONG)t1.size();
        if (!v2 && f.size() > 7) {
            fr[0].pNext = &fr[1];
            fr[1].State.eAction = SPVA_Bookmark; fr[1].pTextStart = seven.c_str(); fr[1].ulTextLen = 1; fr[1].pNext = &fr[2];
            fr[2].pTextStart = t2.c_str(); fr[2].ulTextLen = (ULONG)t2.size(); fr[2].ulTextSrcOffset = (ULONG)t1.size();
        }
        /* v2: the site as asked, and the fragments as given */
        std::vector<SPVTEXTFRAG> many;
        std::vector<std::wstring> texts;
        if (v2) {
            site.volume = (USHORT)atoi(f[7].c_str());
            if (!f[10].compare(0, 4, "max=")) site.max_take = atoi(f[10].c_str() + 4);
            if (!f[10].compare(0, 5, "zero=")) site.refuse = atoi(f[10].c_str() + 5);
            if (!f[10].compare(0, 5, "slow=")) site.slow_ms = atoi(f[10].c_str() + 5);
            for (const std::string &a : split(f[11], ',')) {
                if (!a.compare(0, 6, "abort@")) site.abort_after = atoi(a.c_str() + 6);
                if (!a.compare(0, 5, "skip@")) {
                    std::vector<std::string> p = split(a.substr(5), ':');
                    site.skip_after = atoi(p[0].c_str());
                    site.skip_count = p.size() > 1 ? atol(p[1].c_str()) : 0;
                    site.skip_other = p.size() > 2 && p[2] == "x";
                }
            }
            many.resize(f.size() - 12);
            texts.resize(many.size());
            if (!many.empty()) memset(&many[0], 0, many.size() * sizeof many[0]);
            ULONG src = 0;
            for (size_t i = 0; i < many.size(); i++) {
                const std::string &g = f[12 + i];
                SPVTEXTFRAG &x = many[i];
                x.State.Volume = 100;
                if (g[0] == 'B') {
                    x.State.eAction = SPVA_Bookmark;
                    texts[i] = wide(g.substr(2));
                } else {                     /* S or L, then rate:volume:middle:text (the text may hold colons) */
                    size_t a = 2, b = g.find(':', a), c = g.find(':', b + 1), d = g.find(':', c + 1);
                    x.State.eAction = g[0] == 'L' ? SPVA_SpellOut : SPVA_Speak;
                    x.State.RateAdj = atol(g.substr(a, b - a).c_str());
                    x.State.Volume = (USHORT)atoi(g.substr(b + 1, c - b - 1).c_str());
                    x.State.PitchAdj.MiddleAdj = atol(g.substr(c + 1, d - c - 1).c_str());
                    texts[i] = wide(g.substr(d + 1));
                }
            }
            for (size_t i = 0; i < many.size(); i++) {       /* the strings no longer move */
                many[i].pTextStart = texts[i].c_str();
                many[i].ulTextLen = (ULONG)texts[i].size();
                many[i].ulTextSrcOffset = src;
                src += many[i].ulTextLen + 1;
                many[i].pNext = i + 1 < many.size() ? &many[i + 1] : 0;
            }
        }
        DWORD t0 = GetTickCount();
        if (SUCCEEDED(hr)) hr = engine->Speak(SPF_DEFAULT, fmt, wfx, v2 ? (many.empty() ? 0 : &many[0]) : fr, &site);
        DWORD ms = GetTickCount() - t0;
        wchar_t out[MAX_PATH];
        _snwprintf(out, MAX_PATH, L"%ls\\case_%d.pcm", argv[3], k);
        FILE *o = _wfopen(out, L"wb");
        if (o) { if (!site.audio.empty()) fwrite(site.audio.data(), 1, site.audio.size(), o); fclose(o); }
        std::string marks;
        for (size_t i = 0; i < site.marks.size(); i++) {
            if (i) marks += ",";
            char m[64]; WideCharToMultiByte(CP_UTF8, 0, site.marks[i].c_str(), -1, m, 64, 0, 0); marks += m;
        }
        std::string events, skips;
        for (size_t i = 0; i < site.events.size(); i++) events += (i ? "," : "") + site.events[i];
        for (size_t i = 0; i < site.skips.size(); i++) { char n[24]; _snprintf(n, 24, "%ld", site.skips[i]); skips += (i ? "," : "") + std::string(n); }
        printf("case %d hr=%08lx rate=%lu bytes=%lu writes=%d marks=%s ms=%lu calls=%d events=%s skips=%s\n", k,
               (unsigned long)hr, wfx ? (unsigned long)wfx->nSamplesPerSec : 0ul, (unsigned long)site.audio.size(),
               site.writes, marks.empty() ? "-" : marks.c_str(), (unsigned long)ms, site.calls,
               events.empty() ? "-" : events.c_str(), skips.empty() ? "-" : skips.c_str());
        fflush(stdout);
        if (wfx) CoTaskMemFree(wfx);
        if (with) with->Release();
        if (engine) engine->Release();
        tok->Release();
        k++;
    }
    factory->Release();
    return 0;
}
