#include "pet.h"
#include "intro.h"
#include "webnest.h"
#include "games.h"
#include <ctime>
#include <thread>
#include <UIAutomation.h>
#include <atomic>
#include <memory>

static const bool kStrollsOn = false;

using M3 = D2D1::Matrix3x2F;

static long long unixNow() { return (long long)time(nullptr); }

static std::wstring sizeStr(double bytes) {
    wchar_t b[32];
    double gb = bytes / (1024.0 * 1024 * 1024);
    if (gb >= 10) swprintf(b, 32, L"%.0f ГБ", gb);
    else if (gb >= 1) swprintf(b, 32, L"%.1f ГБ", gb);
    else swprintf(b, 32, L"%.0f МБ", bytes / (1024.0 * 1024));
    for (wchar_t* p = b; *p; p++)
        if (*p == L'.') *p = L',';
    return b;
}

static std::wstring plural(long long n, const wchar_t* one, const wchar_t* few, const wchar_t* many) {
    long long m10 = n % 10, m100 = n % 100;
    const wchar_t* w = (m10 == 1 && m100 != 11) ? one : (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) ? few : many;
    return std::to_wstring(n) + L" " + w;
}

bool Pet::ask(const char* cat, const char* choicesCat, const std::string& id, const std::wstring& arg) {
    syncStage();
    if (app.bubble.asking() || st == St::Inside || byeOn) return false;
    bool yours = id == "nightmare" || id == "sleepArgue" || id == "clean" || id == "talk" || id == "talkSleep" || id == "update";
    if (app.bubble.held() && !yours) {
        logf("ask '%s' skipped: '%s' is still being read", id.c_str(), narrow(app.bubble.text).c_str());
        return false;
    }
    if (quiet() && id != "talk" && id != "talkSleep" && id != "sleepArgue" && id != "nightmare" && id != "nightmareSleep")
        return false;
    std::wstring s = app.phrases.pick(cat, arg);
    auto ch = app.phrases.list(choicesCat);
    if (s.empty() || ch.empty()) return false;
    app.bubble.ask(s, ch, id);
    lastSay = app.now;
    logf("ask '%s'", id.c_str());
    return true;
}

void Pet::nameChanged() {
    tHappy = 1.5f;
    sayCat("nameSet", 1.f, app.settings.name, true);
}

static HWND explorerShowing(const std::wstring& path) {
    ComPtr<IShellWindows> sw;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&sw)))) return nullptr;
    long n = 0;
    sw->get_Count(&n);
    for (long i = 0; i < n; i++) {
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_I4;
        v.lVal = i;
        ComPtr<IDispatch> d;
        if (FAILED(sw->Item(v, &d)) || !d) continue;
        ComPtr<IServiceProvider> sp;
        ComPtr<IShellBrowser> br;
        ComPtr<IShellView> view;
        ComPtr<IFolderView> fv;
        ComPtr<IPersistFolder2> pf;
        if (FAILED(d.As(&sp)) || FAILED(sp->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&br))) || FAILED(br->QueryActiveShellView(&view)) ||
            FAILED(view.As(&fv)) || FAILED(fv->GetFolder(IID_PPV_ARGS(&pf))))
            continue;
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (FAILED(pf->GetCurFolder(&pidl)) || !pidl) continue;
        wchar_t p[MAX_PATH * 2] = L"";
        BOOL ok = SHGetPathFromIDListEx(pidl, p, MAX_PATH * 2, GPFIDL_DEFAULT);
        CoTaskMemFree(pidl);
        if (!ok || _wcsicmp(p, path.c_str())) continue;
        HWND h = nullptr;
        br->GetWindow(&h);
        h = GetAncestor(h, GA_ROOT);
        if (h && IsWindowVisible(h) && !IsIconic(h)) return h;
    }
    return nullptr;
}

static int extKind(const std::wstring& name) {
    size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) return 8;
    std::wstring e = name.substr(dot + 1);
    for (auto& c : e) c = (wchar_t)towlower(c);
    auto in = [&](std::initializer_list<const wchar_t*> l) {
        for (auto x : l)
            if (e == x) return true;
        return false;
    };
    if (in({L"png", L"jpg", L"jpeg", L"gif", L"webp", L"bmp", L"psd", L"kra"})) return 0;
    if (in({L"mp4", L"mkv", L"mov", L"avi", L"webm"})) return 1;
    if (in({L"mp3", L"wav", L"ogg", L"flac", L"m4a"})) return 2;
    if (in({L"txt", L"md", L"doc", L"docx", L"pdf", L"rtf", L"odt", L"xlsx", L"pptx"})) return 3;
    if (in({L"cpp", L"h", L"hpp", L"c", L"py", L"js", L"ts", L"java", L"kt", L"cs", L"json", L"glsl", L"fsh", L"vsh", L"dsc", L"lua"})) return 4;
    if (in({L"zip", L"rar", L"7z", L"tar", L"gz"})) return 5;
    if (in({L"exe", L"msi", L"bat", L"cmd", L"lnk"})) return 6;
    if (in({L"blend", L"fbx", L"obj", L"gltf", L"glb", L"bbmodel"})) return 7;
    return 8;
}

static std::wstring folderSummary(const std::wstring& dir, int& oldestYear) {
    static const wchar_t* forms[][3] = {{L"картинка", L"картинки", L"картинок"}, {L"видео", L"видео", L"видео"},
                                        {L"песня", L"песни", L"песен"},       {L"документ", L"документа", L"документов"},
                                        {L"файл с кодом", L"файла с кодом", L"файлов с кодом"},
                                        {L"архив", L"архива", L"архивов"},     {L"программа", L"программы", L"программ"},
                                        {L"3D-модель", L"3D-модели", L"3D-моделей"}, {L"непонятный файл", L"непонятных файла", L"непонятных файлов"},
                                        {L"папка", L"папки", L"папок"}};
    int cnt[10] = {};
    oldestYear = 9999;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    int seen = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) cnt[9]++;
            else cnt[extKind(fd.cFileName)]++;
            SYSTEMTIME stt;
            if (FileTimeToSystemTime(&fd.ftLastWriteTime, &stt)) oldestYear = std::min(oldestYear, (int)stt.wYear);
        } while (++seen < 5000 && FindNextFileW(h, &fd));
        FindClose(h);
    }
    int a = -1, b = -1;
    for (int i = 0; i < 10; i++) {
        if (!cnt[i]) continue;
        if (a < 0 || cnt[i] > cnt[a]) { b = a; a = i; }
        else if (b < 0 || cnt[i] > cnt[b]) b = i;
    }
    if (a < 0) return L"";
    std::wstring s = plural(cnt[a], forms[a][0], forms[a][1], forms[a][2]);
    if (b >= 0) s += L" и " + plural(cnt[b], forms[b][0], forms[b][1], forms[b][2]);
    return s;
}

struct ScanJob {
    std::atomic<bool> done{false}, cleaning{false}, cleaned{false};
    std::wstring temp;
    long long tempBytes = 0, tempFiles = 0, binBytes = 0, binItems = 0, freed = 0;
    std::vector<std::pair<std::wstring, long long>> big;
};

static long long walkDir(const std::wstring& dir, ULONGLONG deadline, long long& files, bool onlyOld, bool del, int depth = 0) {
    if (GetTickCount64() > deadline || depth > 24) return 0;
    if (dir.size() < 4) return 0;
    long long sum = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return 0;
    FILETIME nowFt;
    GetSystemTimeAsFileTime(&nowFt);
    ULONGLONG nowT = ((ULONGLONG)nowFt.dwHighDateTime << 32) | nowFt.dwLowDateTime, day = 24ull * 3600 * 10000000;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        std::wstring p = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            sum += walkDir(p, deadline, files, onlyOld, del, depth + 1);
            if (del) RemoveDirectoryW(p.c_str());
            continue;
        }
        ULONGLONG wt = ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
        if (onlyOld && nowT - wt < day) continue;
        long long sz = ((long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        if (del) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) continue;
            if (!DeleteFileW(p.c_str())) continue;
        }
        sum += sz;
        files++;
    } while (GetTickCount64() < deadline && FindNextFileW(h, &fd));
    FindClose(h);
    return sum;
}

static std::wstring knownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) r = p;
    CoTaskMemFree(p);
    return r;
}

static long long diskFree(long long* total = nullptr) {
    wchar_t win[MAX_PATH];
    GetWindowsDirectoryW(win, MAX_PATH);
    std::wstring root(win, 3);
    ULARGE_INTEGER fb{}, tb{};
    GetDiskFreeSpaceExW(root.c_str(), &fb, &tb, nullptr);
    if (total) *total = (long long)tb.QuadPart;
    return (long long)fb.QuadPart;
}

static void scanThread(std::shared_ptr<ScanJob> job) {
    wchar_t tp[MAX_PATH];
    GetTempPathW(MAX_PATH, tp);
    job->temp = tp;
    while (!job->temp.empty() && (job->temp.back() == L'\\' || job->temp.back() == L'/')) job->temp.pop_back();
    job->tempBytes = walkDir(job->temp, GetTickCount64() + 15000, job->tempFiles, true, false);
    SHQUERYRBINFO q{sizeof(q)};
    if (SUCCEEDED(SHQueryRecycleBinW(nullptr, &q))) {
        job->binBytes = q.i64Size;
        job->binItems = q.i64NumItems;
    }
    struct F { REFKNOWNFOLDERID id; const wchar_t* name; };
    const F fs[] = {{FOLDERID_Downloads, L"Загрузки"}, {FOLDERID_Videos, L"Видео"}, {FOLDERID_Desktop, L"Рабочий стол"},
                    {FOLDERID_Documents, L"Документы"}, {FOLDERID_Pictures, L"Изображения"}};
    ULONGLONG deadline = GetTickCount64() + 20000;
    for (auto& f : fs) {
        std::wstring p = knownFolder(f.id);
        if (p.empty()) continue;
        long long n = 0;
        long long sz = walkDir(p, deadline, n, false, false);
        if (sz > 5ll << 30) job->big.push_back({f.name, sz});
    }
    std::sort(job->big.begin(), job->big.end(), [](auto& a, auto& b) { return a.second > b.second; });
    if (job->big.size() > 2) job->big.resize(2);
    logf("scan: temp %lld files %s, bin %s, big %zu", job->tempFiles, narrow(sizeStr((double)job->tempBytes)).c_str(),
         narrow(sizeStr((double)job->binBytes)).c_str(), job->big.size());
    job->done = true;
}

static bool safeTempDir(const std::wstring& dir) {
    wchar_t tp[MAX_PATH] = L"";
    if (!GetTempPathW(MAX_PATH, tp)) return false;
    std::wstring t = tp;
    while (!t.empty() && (t.back() == L'\\' || t.back() == L'/')) t.pop_back();
    std::wstring d = dir;
    while (!d.empty() && (d.back() == L'\\' || d.back() == L'/')) d.pop_back();
    if (d.size() < 10 || t.size() < 10) return false;
    if (_wcsicmp(d.c_str(), t.c_str()) != 0) return false;
    std::wstring low = d;
    for (auto& c : low) c = (wchar_t)towlower(c);
    return low.find(L"\\appdata\\local\\temp") != std::wstring::npos;
}

int cleanGuardSelfTest() {
    int fails = 0;
    wchar_t tp[MAX_PATH] = L"";
    GetTempPathW(MAX_PATH, tp);
    for (const wchar_t* bad : {L"", L"\\", L"C:", L"C:\\", L"C:\\Users", L"C:\\Windows\\Temp"}) fails += safeTempDir(bad) ? 1 : 0;
    fails += safeTempDir(tp) ? 0 : 1;
    long long n = 0;
    fails += walkDir(L"", GetTickCount64() + 1000, n, false, false) != 0 || n != 0;
    fails += walkDir(L"C:", GetTickCount64() + 1000, n, false, false) != 0 || n != 0;
    logf("clean guard: %d failed (temp '%s' allowed %d)", fails, narrow(tp).c_str(), (int)safeTempDir(tp));
    return fails;
}

static void cleanThread(std::shared_ptr<ScanJob> job, bool bin) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    long long before = diskFree();
    long long n = 0;
    if (safeTempDir(job->temp)) walkDir(job->temp, GetTickCount64() + 60000, n, true, true);
    else logf("clean: temp folder '%s' is not the user's temp, nothing deleted", narrow(job->temp).c_str());
    if (bin && job->binItems > 0) SHEmptyRecycleBinW(nullptr, nullptr, 0);
    job->freed = std::max(0ll, diskFree() - before);
    logf("clean: removed %lld temp files, freed %s", n, narrow(sizeStr((double)job->freed)).c_str());
    job->cleaned = true;
    CoUninitialize();
}

static std::shared_ptr<ScanJob> g_scan;

void Pet::diveInto(V2 e, float width, int nestKind, const std::wstring& path) {
    edgeL = e;
    edgeW = width;
    nest = nestKind;
    nestPath = path;
    nestT = 0;
    sink = 0;
    phase = 0;
    phaseT = 0;
    lastNest = app.now;
    setState(St::Dive);
    logf("dive into %d '%s'", nestKind, narrow(path).c_str());
}

void Pet::emergeAt(V2 e, float width, int mode) {
    edgeL = e;
    edgeW = width;
    emergeMode = mode;
    pos = V2(e.x + width * 0.5f, e.y);
    vel = {};
    rot = rotVel = 0;
    sink = 1;
    phase = 0;
    phaseT = 0;
    gnd.valid = false;
    for (auto& s : app.world.surfs)
        if (fabsf(s.y - e.y) < 3 && pos.x >= s.x0 - 2 && pos.x <= s.x1 + 2) {
            setGround(s);
            break;
        }
    if (!gnd.valid) {
        Surf f;
        f.kind = SurfKind::Floor;
        int m = app.world.monitorAt(pos - V2(0, 2));
        if (m >= 0 && fabsf((float)app.world.mons[m].work.bottom - e.y) < 3) {
            f.index = m;
            f.y = e.y;
            f.x0 = (float)app.world.mons[m].work.left;
            f.x1 = (float)app.world.mons[m].work.right;
            setGround(f);
        }
    }
    selectRig(0);
    app.inst.resetSim();
    setState(St::Emerge);
}

void Pet::finishNest(int mode) {
    nest = NestNone;
    nestPath.clear();
    emergeWnd = nullptr;
    (void)mode;
}

bool Pet::renderClip(D2D1_RECT_F& r, V2 wo) const {
    bool any = false;
    r = D2D1::RectF(-1e5f, -1e5f, 1e5f, 1e5f);
    if (sink > 0.001f) {
        r.bottom = edgeL.y - wo.y;
        any = true;
    }
    if (app.world.web) {
        const RECT& w = app.world.webRc;
        r.left = std::max(r.left, w.left - wo.x);
        r.top = std::max(r.top, w.top - wo.y);
        r.right = std::min(r.right, w.right - wo.x);
        r.bottom = std::min(r.bottom, w.bottom - wo.y);
        any = true;
    }
    return any;
}

void Pet::callBack(V2 cur) {
    if (st == St::Inside && nestShame && nestT < 60) {
        emergeAt(edgeL, edgeW, 4);
        return;
    }
    if (st == St::Inside && nest == NestWeb) {
        tryTarget = cur;
        leaveWeb(4);
        return;
    }
    if (st == St::Inside) {
        if (nest == NestFolder) {
            if (HWND h = explorerShowing(nestPath)) {
                RECT r;
                if (app.world.windowRect(h, r)) {
                    emergeAt(V2((float)r.left + 40.f * S(), (float)r.top), (float)(r.right - r.left) - 80.f * S(), 2);
                    tryTarget = cur;
                    return;
                }
            }
        }
        emergeAt(edgeL, edgeW, 2);
        tryTarget = cur;
        return;
    }
    if (st == St::Dive) {
        setState(St::Emerge);
        emergeMode = 2;
        tryTarget = cur;
        return;
    }
    comeHere(cur);
}

static int floorSurf(const World& W, int mon) {
    for (size_t i = 0; i < W.surfs.size(); i++)
        if (W.surfs[i].kind == SurfKind::Floor && W.surfs[i].index == mon) return (int)i;
    return -1;
}

static bool wantsWatch(const Activity& A, double now) {
    if (now - A.since < 3.0) return false;
    if (A.kind == AppKind::Game || A.kind == AppKind::Create) return true;
    return A.fgCovers && (A.kind == AppKind::Browser || A.kind == AppKind::Media);
}

static void startWatch(Pet& p) {
    auto& W = p.app.world;
    auto& A = p.app.activity;
    if (W.mons.empty()) return;
    int my = W.monitorAt(p.pos - V2(0, 4));
    if (my < 0) my = W.nearestMonitor(p.pos);
    int target = my;
    if (W.mons.size() > 1 && A.fgMon >= 0 && (A.fgCovers || (A.fgMon == my && A.kind == AppKind::Game)))
        for (size_t m = 0; m < W.mons.size(); m++)
            if ((int)m != A.fgMon) { target = (int)m; break; }
    if (target == A.fgMon && A.fgCovers) return;
    int fi = floorSurf(W, target);
    if (fi < 0) return;
    const RECT& wk = W.mons[target].work;
    V2 fgC((A.fgRect.left + A.fgRect.right) * 0.5f, (A.fgRect.top + A.fgRect.bottom) * 0.5f);
    float x;
    float S = p.app.s;
    if (target != A.fgMon) x = fgC.x < (wk.left + wk.right) * 0.5f ? wk.left + 90.f * S : wk.right - 90.f * S;
    else x = p.pos.x < (wk.left + wk.right) * 0.5f ? wk.left + 110.f * S : wk.right - 170.f * S;
    p.watchMon = target;
    p.watchTarget = fgC;
    p.watchSayT = randf(240, 600);
    for (int i = 0; i < 6; i++)
        p.parts.push_back({4, p.pos + V2(randf(-20, 20) * S, -randf(0, 60) * S), V2(randf(-80, 80) * S, randf(-60, 10) * S), 0.5f, 0.5f, 1.2f, 0});
    p.pos = V2(x, (float)wk.bottom);
    p.vel = {};
    p.rot = p.rotVel = 0;
    p.setGround(W.surfs[fi]);
    p.facing = fgC.x > x ? 1 : -1;
    for (int i = 0; i < 6; i++)
        p.parts.push_back({4, p.pos + V2(randf(-20, 20) * S, -randf(0, 40) * S), V2(randf(-80, 80) * S, randf(-60, 10) * S), 0.5f, 0.5f, 1.2f, 0});
    p.setState(St::Watch, 1e9f);
    p.app.inst.resetSim();
    logf("watch: mon %d, app on mon %d (%s)", target, A.fgMon, appKindName(A.kind));
}

static bool escapeMonitor(Pet& p, unsigned covered) {
    auto& W = p.app.world;
    int target = -1;
    for (size_t m = 0; m < W.mons.size() && m < 32; m++)
        if (!(covered & (1u << m))) { target = (int)m; break; }
    int fi = target >= 0 ? floorSurf(W, target) : -1;
    if (fi < 0) return false;
    float S = p.app.s;
    const RECT& wk = W.mons[target].work;
    for (int i = 0; i < 6; i++)
        p.parts.push_back({4, p.pos + V2(randf(-20, 20) * S, -randf(0, 60) * S), V2(randf(-80, 80) * S, randf(-60, 10) * S), 0.5f, 0.5f, 1.2f, 0});
    p.pos = V2(randf(wk.left + 120.f * S, std::max(wk.left + 121.f * S, wk.right - 160.f * S)), (float)wk.bottom);
    p.vel = {};
    p.rot = p.rotVel = 0;
    p.setGround(W.surfs[fi]);
    p.goIdle(1.f, 2.f);
    p.app.inst.resetSim();
    logf("fullscreen on mon mask %u: moved to mon %d", covered, target);
    return true;
}

bool Pet::shouldHide(bool fullscreen) {
    if (!fullscreen) return false;
    auto& W = app.world;
    auto& A = app.activity;
    if (!A.fgCovers || A.fgMon < 0 || A.fgMon >= 32) return false;
    if (W.mons.size() < 2) return true;
    unsigned covered = 1u << A.fgMon;
    int my = W.monitorAt(com());
    if (my < 0) my = W.nearestMonitor(com());
    if (my < 0 || my >= 32 || !(covered & (1u << my))) return false;
    if (st == St::Inside || dragging) return true;
    if (A.kind == AppKind::Game && A.fgMon == my) startWatch(*this);
    else escapeMonitor(*this, covered);
    my = W.monitorAt(com());
    return my < 0 || my >= 32 || (covered & (1u << my));
}

static void clickNoCursor(V2 p, HWND self) {
    std::thread([p, self] {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        POINT pt{(LONG)lroundf(p.x), (LONG)lroundf(p.y)};
        LONG ex = GetWindowLongW(self, GWL_EXSTYLE);
        SetWindowLongW(self, GWL_EXSTYLE, ex | WS_EX_TRANSPARENT);
        HWND w = WindowFromPoint(pt);
        const char* how = nullptr;
        ComPtr<IUIAutomation> ua;
        if (SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), (void**)&ua))) {
            ComPtr<IUIAutomationElement> el;
            ComPtr<IUIAutomationTreeWalker> walk;
            ua->get_ControlViewWalker(&walk);
            if (SUCCEEDED(ua->ElementFromPoint(pt, &el)) && el) {
                for (int up = 0; up < 5 && el && !how; up++) {
                    ComPtr<IUIAutomationInvokePattern> inv;
                    ComPtr<IUIAutomationTogglePattern> tog;
                    ComPtr<IUIAutomationSelectionItemPattern> sel;
                    ComPtr<IUIAutomationLegacyIAccessiblePattern> leg;
                    if (SUCCEEDED(el->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&inv))) && inv && SUCCEEDED(inv->Invoke())) how = "invoke";
                    else if (SUCCEEDED(el->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&tog))) && tog && SUCCEEDED(tog->Toggle())) how = "toggle";
                    else if (SUCCEEDED(el->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&sel))) && sel && SUCCEEDED(sel->Select())) how = "select";
                    else if (SUCCEEDED(el->GetCurrentPatternAs(UIA_LegacyIAccessiblePatternId, IID_PPV_ARGS(&leg))) && leg) {
                        BSTR act = nullptr;
                        if (SUCCEEDED(leg->get_CurrentDefaultAction(&act)) && act && SysStringLen(act) > 0 && SUCCEEDED(leg->DoDefaultAction())) how = "default";
                        if (act) SysFreeString(act);
                    }
                    if (how || !walk) break;
                    ComPtr<IUIAutomationElement> par;
                    if (FAILED(walk->GetParentElement(el.Get(), &par)) || !par) break;
                    el = par;
                }
            }
        }
        SetWindowLongW(self, GWL_EXSTYLE, ex);
        if (!how && w && w != self) {
            POINT cp = pt;
            ScreenToClient(w, &cp);
            LPARAM lp = MAKELPARAM(cp.x, cp.y);
            PostMessageW(w, WM_MOUSEMOVE, 0, lp);
            PostMessageW(w, WM_LBUTTONDOWN, MK_LBUTTON, lp);
            PostMessageW(w, WM_LBUTTONUP, 0, lp);
            how = "post";
        }
        logf("try: clicked at %.0f,%.0f by %s", p.x, p.y, how ? how : "nothing");
        if (SUCCEEDED(hr)) CoUninitialize();
    }).detach();
}

static void clickAt(V2 p) {
    POINT back;
    GetCursorPos(&back);
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    auto mv = [&](float x, float y) {
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dx = (LONG)((x - vx) * 65535.f / std::max(1, vw - 1));
        in.mi.dy = (LONG)((y - vy) * 65535.f / std::max(1, vh - 1));
        in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        return in;
    };
    INPUT seq[4];
    seq[0] = mv(p.x, p.y);
    seq[1] = seq[0];
    seq[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    seq[2] = seq[0];
    seq[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    seq[3] = mv((float)back.x, (float)back.y);
    SendInput(4, seq, sizeof(INPUT));
    logf("try: clicked at %.0f,%.0f", p.x, p.y);
}

static int clickCluster(const Activity& A, double now, V2& c, HWND& w) {
    if (A.clicks.empty()) return 0;
    const auto& last = A.clicks.back();
    int n = 0;
    V2 sum;
    for (auto& k : A.clicks) {
        if (now - k.t > 12 || k.w != last.w) continue;
        if (fabsf((float)(k.p.x - last.p.x)) > 60 || fabsf((float)(k.p.y - last.p.y)) > 60) continue;
        n++;
        sum += V2((float)k.p.x, (float)k.p.y);
    }
    if (n) c = sum / (float)n;
    w = last.w;
    return n;
}

void Pet::answer(const std::string& id, int choice) {
    app.bubble.close();
    logf("answer '%s' -> %d", id.c_str(), choice);
    if (id.rfind("want:", 0) == 0) {
        wantAnswer(id.substr(5), choice);
        return;
    }
    if (id.rfind("talk:", 0) == 0) {
        talkAnswer(id.substr(5), choice);
        return;
    }
    if (id.rfind("reply:", 0) == 0) {
        replyAnswer(id.substr(6), choice);
        return;
    }
    bool yes = choice == 0;
    if (id == "monitor") {
        if (yes) {
            setState(St::Lift, 40.f);
            sayCat("liftWait", 1.f, L"", true);
        } else {
            liftMon = -1;
            tSad = 2.f;
            sayCat("monitorNo", 1.f, L"", true);
        }
    } else if (id == "disk" || id == "bin") {
        if (!yes) {
            sayCat("nagLater", 1.f, L"", true);
            return;
        }
        if (id == "bin") {
            tHappy = 1.5f;
            sayCat("binClean", 1.f, L"", true);
            g_scan = std::make_shared<ScanJob>();
            g_scan->done = true;
            SHQUERYRBINFO q{sizeof(q)};
            if (SUCCEEDED(SHQueryRecycleBinW(nullptr, &q))) g_scan->binItems = q.i64NumItems;
            g_scan->temp.clear();
            g_scan->cleaning = true;
            std::thread(cleanThread, g_scan, true).detach();
            return;
        }
        sayCat("diskSearch", 1.f, L"", true);
        g_scan = std::make_shared<ScanJob>();
        std::thread(scanThread, g_scan).detach();
        if (grounded() && gnd.valid) diveInto(V2(pos.x - 40.f * S(), pos.y), 80.f * S(), NestSystem, L"");
    } else if (id == "clean") {
        if (!yes || !g_scan) {
            sayCat("cleanNo", 1.f, L"", true);
            return;
        }
        sayCat("cleanGo", 1.f, L"", true);
        g_scan->cleaning = true;
        std::thread(cleanThread, g_scan, g_scan->binItems > 0).detach();
    } else if (id == "try") {
        if (!yes) {
            tSad = 1.5f;
            sayCat("tryNo", 1.f, L"", true);
            return;
        }
        tryMode = true;
        tryUntil = app.now + 90;
        tHappy = 1.5f;
        sayCat("tryCarry", 1.f, L"", true);
        setState(St::Lift, 60.f);
    } else if (id == "browser") {
        answerWeb(yes);
    } else if (id.rfind("ext", 0) == 0) {
        extAnswer(id, choice);
    } else if (id == "talk") {
        talkChoice(choice);
    } else if (id == "talkBaby") {
        if (choice == 0) wantSettings = true;
    } else if (id == "games") {
        if (choice < (int)gameList().size()) gameStart(choice);
    } else if (id == "gameAgain") {
        if (yes && lastGame >= 0) gameStart(lastGame);
        else sayCat("gameEnough", 1.f, L"", true);
    } else if (id.rfind("intro", 0) == 0) {
        introAnswer(app, *this, id, choice);
    } else if (id == "nightmare") {
        comfortNightmare(false);
    } else if (id == "nightmareSleep") {
        if (choice == 0) {
            sayCat("nightmareSleepYes", 1.f, L"", true);
            sleepAfterT = 1.8f;
        } else {
            sleepAfterT = 0;
            tHappy = 1.5f;
            sayCat("nightmareStay", 1.f, L"", true);
            if (grounded()) setState(St::Sit, randf(120, 240));
        }
    } else if (id == "talkSleep") {
        if (choice == 0 && st == St::Sleep) gentleWake("talk");
        else if (choice == 1) wantSettings = true;
    } else if (id == "sleepArgue") {
        if (choice == 0) {
            if (chance(0.4f)) {
                sayCat("sleepBossOk", 1.f, L"", true);
                sleepNow();
            } else {
                tAngry = 2.5f;
                sayCat("sleepBossNo", 1.f, L"", true);
                facing = -facing;
                if (grounded()) setState(St::Sit, randf(60, 120));
            }
        } else if (choice == 1) {
            if (chance(0.8f)) {
                tBlush = 2.f;
                addRel(1.f, "asked nicely");
                sayCat("sleepPleaseOk", 1.f, L"", true);
                sleepNow();
            } else {
                tHappy = 1.5f;
                sayCat("sleepPleaseNo", 1.f, L"", true);
            }
        } else {
            tHappy = 2.f;
            sayCat("sleepFine", 1.f, L"", true);
            if (grounded()) setState(St::Clap, 1.6f);
        }
    } else if (id == "update") {
        if (yes) {
            tHappy = 1.5f;
            sayCat("updateGo", 1.f, L"", true);
            wantUpdate = true;
            if (grounded() && gnd.valid) diveInto(V2(pos.x - 40.f * S(), pos.y), 80.f * S(), NestSystem, L"");
        } else {
            sayCat("updateLater", 1.f, L"", true);
        }
    } else if (id == "corner") {
        cornerAnswer(choice);
    } else if (id == "hardcore") {
        hardcoreAnswer(choice);
    } else if (id == "stroll") {
        if (yes) {
            tHappy = 1.5f;
            sayCat("strollGo", 1.f, L"", true);
            startStroll(randf(6, 15));
            if (chance(0.5f)) giveBattery(1, "let her stroll");
        } else {
            tSad = 1.f;
            sayCat("strollNo", 1.f, L"", true);
            if (grounded()) setState(St::Sit, randf(60, 120));
        }
    }
}

int Pet::metDays(double now) const { return vit.metAt > 0 ? (int)std::max(0.0, floor((now - vit.metAt) / 86400.0)) : 0; }

static int dayKey(const tm& lt) { return (lt.tm_year + 1900) * 1000 + lt.tm_yday; }

const char* Pet::dateDue(const tm& lt, double now, int& anniv) const {
    anniv = 0;
    int d = metDays(now);
    int m = d >= 365 ? d / 365 * 365 : d >= 100 ? 100 : d >= 30 ? 30 : d >= 7 ? 7 : 0;
    if (m > vit.annivDone) {
        anniv = m;
        if (d - m <= 3) {
            bool hate = tier() == 0;
            if (m % 365 == 0) return vit.stock >= 6 ? "annivYearFull" : "annivYear";
            if (m == 100) return "anniv100";
            if (m == 30) return "anniv30";
            return "anniv7";
        }
    }
    int key = dayKey(lt);
    if (key <= vit.holidayDay && vit.holidayDay - key <= 15) return nullptr;
    int mo = lt.tm_mon + 1, dd = lt.tm_mday;
    if (mo == 12 && dd >= 20 && dd <= 30) return "soonNewYear";
    if (mo == 12 && dd == 31) return "newYearEve";
    if (mo == 1 && dd <= 2) return "newYear";
    if (mo == 10 && dd == 31) return "halloween";
    if (mo == 4 && dd == 1) return "aprilFools";
    return nullptr;
}

bool Pet::dateSay(const tm& lt, double now) {
    int anniv = 0;
    const char* cat = dateDue(lt, now, anniv);
    if (anniv) vit.annivDone = anniv;
    if (!cat) {
        if (anniv) vit.save();
        return false;
    }
    bool isAnniv = strncmp(cat, "anniv", 5) == 0;
    if (!isAnniv) {
        int last = !strcmp(cat, "soonNewYear") ? 30 : !strcmp(cat, "newYear") ? 2 : lt.tm_mday;
        vit.holidayDay = dayKey(lt) + std::max(0, last - lt.tm_mday);
    }
    if (isAnniv && anniv % 365 == 0 && vit.stock < 6) giveBattery(1, "birthday");
    if (tier() >= 1) {
        tHappy = 1.6f;
        if (grounded() && !dragging && st == St::Idle) setState(St::Wave, 1.6f);
    }
    if (tier() == 3) tBlush = 1.4f;
    sayCat(cat, 1.f, L"", true);
    lastDateSay = app.now;
    if (isAnniv && tier() >= 1)
        cornerPhoto(anniv % 365 == 0 ? L"мой день рождения" : anniv == 100 ? L"нам сто дней" : anniv == 30 ? L"нам месяц" : L"нам неделя");
    else if (!strcmp(cat, "newYear") || !strcmp(cat, "newYearEve")) cornerPhoto(L"я на Новый год");
    vit.save();
    logf("date: %s (met %d days, anniv %d, holiday %d, stock %d)", cat, metDays(now), vit.annivDone, vit.holidayDay, vit.stock);
    return true;
}

static const float kSolverGlitch = 1.4f, kSolverShake = 0.9f, kSolverEnd = 3.8f;

bool Pet::solverGate(const tm& lt, const Input& in) const {
    if (!app.settings.solver || baby() || vit.fatal || vit.dead || vit.intro == 1 || solverT >= 0) return false;
    if (lt.tm_hour >= 6 || vit.energy >= 40.f) return false;
    if (!(st == St::Idle || st == St::Sit) || !grounded()) return false;
    if (dragging || petting || feeding || tryMode || strollOn || quiet() || app.bubble.active || !carriedName.empty() || !heldPath.empty() ||
        nest != NestNone || webActive() || game)
        return false;
    if (app.activity.kind == AppKind::Game || app.activity.fgCovers) return false;
    if (in.idleSec > 300) return false;
    return (double)time(nullptr) - vit.lastSolver > 6 * 3600.0;
}

bool Pet::solverStart(bool force) {
    if (baby() || vit.fatal || vit.dead || vit.intro == 1 || introHidden || solverT >= 0 || app.bubble.asking() || st == St::Sleep ||
        st == St::Inside || st == St::Dive || st == St::Emerge || st == St::Hide || st == St::Fatal) {
        if (force) logf("solver: debug start refused (state %s)", stateName());
        return false;
    }
    if (app.bubble.active) app.bubble.close();
    solverT = 0;
    solverMute = chance(0.2f);
    if (st == St::Sit) stDur = std::max(stDur, stT + kSolverEnd + 1.f);
    else if (grounded() && !dragging) setState(St::Idle, kSolverEnd + 0.5f);
    if (!force) {
        vit.lastSolver = (double)time(nullptr);
        vit.save();
    }
    logf("solver: start (%s, energy %.0f, state %s, mute %d)", force ? "debug" : "night", vit.energy, stateName(), (int)solverMute);
    return true;
}

void Pet::solverStep(float dt, const Input& in) {
    if (solverWait > 0 && (solverWait -= dt) <= 0) solverStart(true);
    if (solverT < 0) {
        solverK = sigilK = 0;
        solverRollT -= dt;
        if (solverRollT <= 0) {
            solverRollT = 10.f;
            time_t tt = time(nullptr);
            tm lt;
            localtime_s(&lt, &tt);
            if (solverGate(lt, in) && chance(10.f / 5400.f)) solverStart(false);
        }
        return;
    }
    if (vit.fatal || st == St::Sleep || st == St::Inside || st == St::Dive || dragging) {
        logf("solver: interrupted at %.2f s (%s)", solverT, stateName());
        solverT = -1;
        solverK = sigilK = 0;
        return;
    }
    float was = solverT;
    solverT += dt;
    float u = solverT;
    if (u < kSolverGlitch) {
        float env = smooth01(u / 0.1f) * (1.f - smooth01((u - (kSolverGlitch - 0.15f)) / 0.15f));
        unsigned tk = (unsigned)(u * 24.f);
        float drop = ((tk * 2654435761u) >> 28) < 3 ? 0.35f : 1.f;
        solverK = env * drop;
        auto pulse = [](float x, float a, float b) { return smooth01((x - a) / 0.06f) * (1.f - smooth01((x - b) / 0.08f)); };
        sigilK = std::max(pulse(u, 0.28f, 0.68f), pulse(u, 0.86f, 1.18f)) * (drop < 1.f ? 0.5f : 1.f);
    } else {
        solverK = sigilK = 0;
    }
    if (was < kSolverGlitch && solverT >= kSolverGlitch) squashV -= 1.4f;
    float sayAt = kSolverGlitch + kSolverShake;
    if (was < sayAt && solverT >= sayAt) {
        if (!solverMute) sayCat("solverAfter", 1.f, L"", true);
        cornerPhoto(L"я в шоке");
        logf("solver: after, %s '%s'", solverMute ? "stare" : "line", solverMute ? "" : narrow(app.bubble.text).c_str());
    }
    if (solverT >= kSolverEnd) {
        solverT = -1;
        if (st == St::Idle) goIdle(1.5f, 3.f);
    }
}

bool Pet::afkStart() {
    if (afkNap || afkGo || baby() || vit.fatal || vit.dead || vit.intro == 1 || introHidden) return false;
    if (!(st == St::Idle || st == St::Sit || st == St::LookAround) || !grounded() || dragging) return false;
    if (app.bubble.active || solverT >= 0 || nest != NestNone || webActive() || !carriedName.empty() || !heldPath.empty() || tryMode || feeding ||
        petting)
        return false;
    auto& A = app.activity;
    if (A.kind == AppKind::Game || A.fgCovers || wantsWatch(A, app.now)) return false;
    if (st == St::Sit) {
        afkNapNow();
        return true;
    }
    float lo = gnd.x0 + 40.f * S(), hi = gnd.x1 - 40.f * S(), x = pos.x;
    if (hi > lo && chance(0.6f)) x = clampf(pos.x + randf(-160.f, 160.f) * S(), lo, hi);
    logf("afk: idle, going to nap (walk %.0f px, energy %.1f)", x - pos.x, vit.energy);
    if (fabsf(x - pos.x) > 24.f * S()) {
        afkGo = 1;
        goWalk(x);
    } else {
        afkGo = 2;
        setState(St::Stretch, 3.2f);
        sayCat("afkYawn", 0.7f, L"", true);
    }
    return true;
}

void Pet::afkNapNow() {
    afkGo = 0;
    afkNap = true;
    afkWakeT = -1;
    afkNoteDone = afkNoteTell = false;
    deepSleep = false;
    followT = 0;
    setState(St::Sleep, 1e9f);
    zzzT = 1.5f;
    sayCat("sleep", 0.5f, L"", true);
    logf("afk nap: energy %.1f rel %.1f", vit.energy, vit.rel);
}

void Pet::afkStep(float dt, const Input& in, bool free) {
    if (afkNap && st != St::Sleep) {
        afkNap = false;
        afkWakeT = -1;
        logf("afk nap: ended (%s)", stateName());
    }
    if (afkGo) {
        bool ok = grounded() && !dragging && in.idleSec >= kAfkIdle && !app.bubble.asking() &&
                  (st == St::Walk || st == St::Idle || st == St::Stretch || st == St::LookAround);
        if (!ok) {
            logf("afk: going to nap cancelled (%s, idle %.0f)", stateName(), in.idleSec);
            afkGo = 0;
        }
        else if (afkGo == 1 && st != St::Walk) {
            afkGo = 2;
            setState(St::Stretch, 3.2f);
            sayCat("afkYawn", 0.7f, L"", true);
        } else if (afkGo == 2 && st != St::Stretch) afkNapNow();
    } else if (!afkNap && free && in.idleSec >= kAfkIdle && (st == St::Idle || st == St::Sit || st == St::LookAround)) {
        afkStart();
    }
    if (!afkNap) return;
    if (in.idleSec < 2 && afkWakeT < 0) {
        afkWakeT = randf(15.f, 40.f);
        logf("afk: you're back, wakes in %.0f s", afkWakeT);
    }
    if (afkWakeT > 0 && (afkWakeT -= dt) <= 0) {
        if (app.bubble.asking() || app.bubble.held() || carriedAsleep()) afkWakeT = 1.f;
        else wake(false);
        return;
    }
    if (!afkNoteDone && afkWakeT < 0 && in.idleSec >= kAfkNoteIdle) {
        afkNoteDone = true;
        double now = (double)time(nullptr);
        if (!app.settings.notes || now - vit.lastNote < 4 * 3600.0) logf("afk note: skipped (setting %d, last %.0f min ago)", (int)app.settings.notes, (now - vit.lastNote) / 60.0);
        else afkNoteWrite();
    }
}

void Pet::afkWoke() {
    afkNap = false;
    afkWakeT = -1;
    afkGo = 0;
    wasAway = false;
    int tr = tier();
    tSurprise = 0.25f;
    if (tr >= 2) tHappy = 1.6f;
    if (tr == 3) tBlush = 1.4f;
    if (!app.bubble.asking()) app.bubble.close();
    double gap = std::max(afkBackSec, awayPeak);
    afkBackSec = 0;
    if (gap >= 20 * 60) sayCat("afkWakeLong", 1.f, awayWords(gap), true);
    else sayCat("afkWake", 1.f, L"", true);
    lastGreetAt = app.now;
    logf("afk wake: away %.0f min -> %s", gap / 60.0, gap >= 20 * 60 ? "afkWakeLong" : "afkWake");
    warmT = std::max(warmT, 240.f);
    if (afkNoteTell) {
        afkNoteTell = false;
        laterCat = "afkNoteTell";
        laterArg.clear();
        laterT = std::max(app.bubble.typeDur + 1.6f, app.bubble.holdT - app.bubble.t + 0.4f);
    }
    logf("afk wake (tier %d, rel %.1f, energy %.1f): '%s'", tr, vit.rel, vit.energy, narrow(app.bubble.text).c_str());
}

static std::wstring plainText(const std::wstring& s) {
    std::wstring o;
    for (wchar_t c : s) {
        if (c == L'~' || c == L'^' || c == L'|') continue;
        if (c == L' ' && (o.empty() || o.back() == L' ')) continue;
        o.push_back(c);
    }
    while (!o.empty() && o.back() == L' ') o.pop_back();
    return o;
}

std::wstring Pet::afkNoteText() {
    std::wstring t = plainText(app.phrases.pick("note", L""));
    std::vector<const char*> ps;
    if (vit.stock == 0) ps.push_back("notePsEmpty");
    else if (vit.food < 30.f) ps.push_back("notePsHungry");
    if (vit.parcelOut) ps.push_back("notePsParcel");
    int n = 0;
    for (auto c : ps) {
        std::wstring l = plainText(app.phrases.pick(c, L""));
        if (l.empty()) continue;
        if (n > 0 && l.rfind(L"P.S.", 0) == 0) l = L"P.P.S." + l.substr(4);
        t += (n++ == 0 ? L"\r\n\r\n" : L"\r\n") + l;
    }
    return t + L"\r\n\r\n— Узи.\r\n";
}

bool Pet::afkNoteWrite() {
    std::wstring dir = noteDir;
    if (dir.empty()) {
        if (app.world.fake) return false;
        PWSTR p = nullptr;
        HRESULT hr = SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &p);
        if (SUCCEEDED(hr)) dir = p;
        CoTaskMemFree(p);
        if (dir.empty()) return false;
    }
    std::wstring text = afkNoteText();
    if (!writeFile(dir + L"\\Записка от Узи.txt", "\xEF\xBB\xBF" + narrow(text))) {
        logf("afk note: can't write to '%s'", narrow(dir).c_str());
        return false;
    }
    vit.lastNote = (double)time(nullptr);
    vit.save();
    afkNoteTell = true;
    logf("afk note: written, %zu chars, tier %d", text.size(), tier());
    return true;
}

void Pet::lifeUpdate(float dt, const Input& in) {
    auto& A = app.activity;
    auto& W = app.world;
    vitalsUpdate(dt, in.idleSec);
    if (byeOn && app.now - byeAt > 120) byeCancel();
    if (vit.intro == 1) return;
    if (baby()) {
        babyUpdate(dt, in);
        return;
    }
    if (lifeT == 0) {
        lastMonWish = app.now;
        lastNagCheck = app.now;
        lastNest = app.now - 600;
        lastHide = app.now - 120;
        nextStrollAsk = app.now + randf(25, 50) * 60;
    }
    lifeT += dt;
    tLoud -= dt;
    if (!app.bubble.expired.empty()) {
        std::string id = app.bubble.expired;
        app.bubble.expired.clear();
        logf("ask '%s' ignored", id.c_str());
        if (id == "try" || id == "monitor") tSad = 1.2f;
        if (id == "browser") answerWeb(false);
        if (id.rfind("ext", 0) == 0) extAnswer(id, -1);
        if (id == "corner") cornerAnswer(-1);
        if (id == "hardcore") hardcoreAnswer(-1);
        if (id.rfind("want:", 0) == 0) wantAnswer(id.substr(5), -1);
    }
    double backGap = -1;
    if (in.idleSec < 2) {
        if (awayPeak >= 2) backGap = awayPeak;
        awayPeak = 0;
    } else awayPeak = std::max(awayPeak, in.idleSec);
    bool awake = st != St::Sleep && st != St::Inside && st != St::Dive && st != St::Emerge && st != St::Hide && !vit.fatal;
    bool busy = dragging || petting || !carriedName.empty() || !heldPath.empty() || app.bubble.asking() || tryMode ||
                st == St::Watch || st == St::Lift || st == St::Climb || nest != NestNone || webActive() || solverT >= 0 || game;
    bool free = awake && grounded() && !busy && !quiet() && !strollOn;
    extUpdate(dt, in, free);

    if (A.loud && awake && st != St::Cling) {
        tLoud = 2.8f;
        if (grounded() && !dragging && st != St::Watch) setState(St::Ears, 2.8f);
        sayCat("loud", 1.f, L"", true);
    }
    if (A.shot) {
        A.shot = false;
        if (awake && app.now - lastShotSay > 120) {
            lastShotSay = app.now;
            tHappy = 2.f;
            sayCat("screenshot", 1.f, L"", true);
            if (free) setState(St::Wave, 1.6f);
        }
    }
    if (breakAt <= 0) breakAt = randf(75, 110) * 60;
    if (A.sitT > breakAt && free && A.kind != AppKind::Game && !A.fgCovers) {
        A.sitT = 30 * 60;
        breakAt = (30 + randf(50, 80)) * 60;
        setState(St::Stretch, 3.4f);
        sayCat("breakTime", 1.f, L"", true);
    }

    static double reacted = -1;
    if (A.since != reacted && app.now - A.since > 2.5) {
        reacted = A.since;
        const char* cat = nullptr;
        float p = 0.45f;
        switch (A.kind) {
        case AppKind::Browser: cat = "appBrowser"; break;
        case AppKind::Game: cat = "appGame"; break;
        case AppKind::Create: cat = A.exe == L"blender.exe" ? "appBlender" : A.exe == L"unrealeditor.exe" ? "appUnreal" : "appCreate"; break;
        case AppKind::Code: cat = "appCode"; break;
        case AppKind::Chat: cat = A.exe == L"discord.exe" ? "appDiscord" : A.exe == L"telegram.exe" ? "appTelegram" : "appChat"; break;
        case AppKind::Claude: cat = "appClaude"; p = 0.6f; break;
        case AppKind::Media: cat = "appMedia"; break;
        case AppKind::TaskMgr: cat = "appTaskmgr"; p = 0.9f; break;
        case AppKind::Explorer: cat = "appExplorer"; p = 0.25f; break;
        default: break;
        }
        int k = (int)A.kind;
        if (cat && app.settings.talk && awake && !quiet() && !app.bubble.asking() && app.now - lastAppSay > 600 && app.now - kindSaid[k] > 2400 && chance(p * 0.6f)) {
            lastAppSay = app.now;
            kindSaid[k] = app.now;
            sayCat(cat, 1.f, L"", true);
        }
    }

    static float chatT = 0;
    static double lastJealous = -1e9;
    chatT = (A.kind == AppKind::Chat && in.idleSec < 30) ? chatT + dt : std::max(0.f, chatT - dt * 0.5f);
    if (chatT > 20 * 60 && tier() >= 2 && awake && grounded() && !quiet() && app.settings.talk && !app.bubble.asking() &&
        app.now - lastJealous > 3600 && app.now - lastSay > 60 && chance(dt / 120.f)) {
        lastJealous = app.now;
        chatT = 0;
        tBlush = 1.5f;
        if (fabsf(lastCursor.x - pos.x) > 6.f) facing = lastCursor.x > pos.x ? -1 : 1;
        sayCat("jealousChat", 1.f, L"", true);
        logf("jealous: long chat");
    }

    if (tier() >= 2 && app.settings.talk) {
        warmT -= dt;
        if (warmT <= 0) {
            if (!awake || !grounded() || busy || quiet() || app.bubble.active || in.idleSec < 4 || app.now - lastSay < 45) {
                warmT = 20.f;
            } else {
                warmT = tier() == 3 ? randf(420, 600) : randf(720, 1080);
                time_t tt = time(nullptr);
                tm lt;
                localtime_s(&lt, &tt);
                const char* cat = "idleWarm";
                if (lt.tm_hour >= 23 || lt.tm_hour < 5) cat = "warmNight";
                else if (lt.tm_hour >= 5 && lt.tm_hour < 11 && morningDay != lt.tm_yday) {
                    cat = "warmMorning";
                    morningDay = lt.tm_yday;
                } else if (A.sitT > 70 * 60 && chance(0.5f)) cat = "warmWork";
                if (tier() == 3) tBlush = 1.4f;
                sayCat(cat, 1.f, L"", true);
            }
        }
    }
    if (app.settings.talk && !vit.dead) {
        dateT -= dt;
        if (dateT <= 0) {
            dateT = 20.f;
            if (awake && grounded() && !busy && !quiet() && !app.bubble.active && in.idleSec >= 3 && in.idleSec < 60 &&
                app.now - lastSay > 40 && app.now - lastDateSay > 600) {
                time_t tt = time(nullptr);
                tm lt;
                localtime_s(&lt, &tt);
                if (dateSay(lt, (double)tt)) {
                    dateT = 600.f;
                    warmT = std::max(warmT, 240.f);
                } else dateT = 120.f;
            }
        }
    }
    missStep(awake, busy);
    cornerStep(dt, in, awake, busy);
    solverStep(dt, in);
    if (backGap > 0 && afkNap) afkBackSec = backGap;
    afkStep(dt, in, free);
    gameStep(dt, in);
    if (in.idleSec > 600) {
        wasAway = true;
    } else if (wasAway && in.idleSec < 2) {
        wasAway = false;
        bool longGap = backGap >= 20 * 60;
        int tr = tier();
        if ((tr >= 2 || longGap) && awake && grounded() && !app.bubble.asking() && app.now - lastGreetAt > 120) {
            if (tr >= 2) tHappy = 1.6f;
            if (longGap) {
                sayCat("backLong", 1.f, awayWords(backGap), true);
            } else sayCat("warmBack", 1.f, L"", true);
            lastGreetAt = app.now;
            warmT = std::max(warmT, 240.f);
            logf("back (awake) after %.0f min, tier %d: '%s'", backGap / 60.0, tr, narrow(app.bubble.text).c_str());
        }
    }

    bool ww = wantsWatch(A, app.now);
    static float noWatchT = 0;
    if (ww && awake && st != St::Watch && !dragging && carriedName.empty() && heldPath.empty() && nest == NestNone && !webActive() &&
        (grounded() || st == St::Fall) && st != St::Cling && !tryMode && !game)
        startWatch(*this);
    if (st == St::Watch) {
        noWatchT = ww ? 0.f : noWatchT + dt;
        if (noWatchT > 5.f) {
            noWatchT = 0;
            watchMon = -1;
            if (!rbxLeaveNow() && app.now - csVoice.lastLine > 10) sayCat("watchEnd", 0.6f, L"", true);
            goIdle(1.f, 2.f);
        }
        if (A.fgRect.right > A.fgRect.left) watchTarget = V2((A.fgRect.left + A.fgRect.right) * 0.5f, (A.fgRect.top + A.fgRect.bottom) * 0.45f);
        watchSayT -= dt;
        if (watchSayT <= 0) {
            watchSayT = randf(420, 900);
            const char* c = A.kind == AppKind::Game ? "watchGame" : A.kind == AppKind::Create ? "watchCreate" : "watchMovie";
            if (!quiet()) sayCat(c, 0.8f);
        }
        if (A.kind == AppKind::Game && chance(dt / 40.f)) tSurprise = std::max(tSurprise, 0.5f);
        csStep(dt);
    }

    static double nextMonWish = 20 * 60;
    if (W.mons.size() > 1 && free && st == St::Idle && !app.bubble.active && wantPhase == 0 && app.now - lastMonWish > nextMonWish && in.idleSec < 60) {
        lastMonWish = app.now;
        nextMonWish = randf(25, 50) * 60;
        int my = W.monitorAt(pos - V2(0, 4));
        int target = -1;
        for (size_t m = 0; m < W.mons.size(); m++)
            if ((int)m != my) { target = (int)m; break; }
        if (target >= 0) {
            const RECT& wk = W.mons[target].work;
            float x = (wk.left + wk.right) * 0.5f < pos.x ? wk.right - 140.f * S() : wk.left + 140.f * S();
            if (goJump(V2(x, (float)wk.bottom), 60)) sayCat("monitorJump", 1.f, L"", true);
            else {
                liftMon = target;
                ask("monitorAsk", "choiceMonitor", "monitor");
            }
        }
    }
    if (liftMon >= 0 && grounded() && !dragging && W.monitorAt(pos - V2(0, 4)) == liftMon) {
        liftMon = -1;
        tHappy = 2.f;
        if (st == St::Lift) goIdle(1.f, 2.f);
        bool early = app.bubble.asking() && app.bubble.askId == "monitor";
        if (early) {
            app.bubble.close();
            tSurprise = std::max(tSurprise, 0.8f);
            tBlush = std::max(tBlush, 1.5f);
        }
        logf("monitor: moved where she asked%s", early ? " before answering" : "");
        sayCat(early ? "liftThanksEarly" : "liftThanks", 1.f, L"", true);
    }

    static double nextNag = 240;
    if (free && st == St::Idle && app.now - lastNagCheck > nextNag && !app.world.fake) {
        lastNagCheck = app.now;
        nextNag = 600;
        long long total = 0, fr = diskFree(&total);
        long long now = unixNow();
        if (total > 0 && (fr < (20ll << 30) || fr < total / 16) && now - app.settings.lastDiskNag > 24 * 3600) {
            app.settings.lastDiskNag = now;
            app.settings.save();
            ask("diskLow", "choiceDisk", "disk", sizeStr((double)fr));
        } else {
            SHQUERYRBINFO q{sizeof(q)};
            if (SUCCEEDED(SHQueryRecycleBinW(nullptr, &q)) && (q.i64Size > (2ll << 30) || q.i64NumItems > 400) &&
                now - app.settings.lastBinNag > 24 * 3600) {
                app.settings.lastBinNag = now;
                app.settings.save();
                ask("binFull", "choiceBin", "bin", sizeStr((double)q.i64Size));
            }
        }
    }
    if (g_scan && g_scan->cleaned) {
        tHappy = 2.5f;
        sayCat(g_scan->freed > (50ll << 20) ? "cleanDone" : "cleanDoneSmall", 1.f, sizeStr((double)g_scan->freed), true);
        if (grounded() && !dragging && st != St::Watch) setState(St::Clap, 2.f);
        g_scan.reset();
        if (!bigNote.empty()) {
            laterCat = "diskBig";
            laterArg = bigNote;
            laterT = 4.5f;
            bigNote.clear();
        }
    }
    if (laterT > 0) {
        laterT -= dt;
        if (laterT <= 0) sayCat(laterCat.c_str(), 1.f, laterArg, true);
    }

    if (!tryMode && free && st == St::Idle && app.now - lastTry > 15 * 60 && A.kind != AppKind::Game && !A.fgCovers && A.kind != AppKind::None) {
        V2 c;
        HWND w = nullptr;
        if (clickCluster(A, app.now, c, w) >= 12 && w && w == A.fg) {
            lastTry = app.now;
            tryTarget = c;
            tryWnd = w;
            app.activity.clicks.clear();
            ask("tryAsk", "choiceTry", "try");
        }
    }
    if (tryMode && dragging && st == St::Dragged) {
        if (tryPress >= 0) {
            float was = tryPress;
            tryPress += dt;
            if (was < 0.28f && tryPress >= 0.28f) {
                if (!app.world.fake) clickNoCursor(tryTarget, app.hwnd);
                else logf("try: press at %.0f,%.0f (snapshot, no click)", tryTarget.x, tryTarget.y);
                tHappy = 2.f;
                squashV -= 1.5f;
                emit(2, tryTarget + V2(10.f, -10.f) * S(), V2(0, -60.f * S()), 0.7f, 1.f);
            }
            if (tryPress >= 0.45f) {
                tryPress = -1;
                tryMode = false;
                clapOnLand = true;
                if (app.bubble.held()) sayCat("tryDone", 1.f, L"", true);
                else {
                    std::wstring ty = app.phrases.pick("tryDone", L"");
                    if (!ty.empty() && !app.bubble.asking()) {
                        app.bubble.say(ty);
                        lastSay = app.now;
                        pendingSay.clear();
                        momentNote("tryDone");
                    }
                }
            }
        } else if (tryHandDist < 8.f * S()) {
            tryHoldT += dt;
            if (tryHoldT > 0.35f) tryPress = 0;
        } else if (tryHandDist > 14.f * S()) {
            tryHoldT = 0;
        }
    }
    if (tryMode && app.now > tryUntil) {
        tryMode = false;
        if (st == St::Lift || st == St::Climb) {
            if (st == St::Climb) startFall({});
            else goIdle();
        }
        sayCat("tryGiveUp", 1.f, L"", true);
    }

    bool idleish = grounded() && !dragging && (st == St::Idle || st == St::Sit || st == St::LookAround) && nest == NestNone &&
                   !webActive() && !app.bubble.asking() && carriedName.empty() && heldPath.empty();
    if (quietUntil > 0 && app.now >= quietUntil) {
        quietUntil = 0;
        logf("quiet hour over");
        if (st == St::Sleep && deepSleep && !carriedAsleep()) wake(false);
        if (strollOn) strollUntil = app.now;
        else returnPending = true;
    }
    if (strollOn && idleish && app.now > strollPauseUntil) {
        if (app.now < strollUntil) {
            strollPauseUntil = app.now + randf(20, 50);
            strollNext();
        } else {
            strollOn = false;
            returnHome();
        }
    }
    if (returnPending && idleish) {
        returnPending = false;
        returnHome();
    }
    if (kStrollsOn && free && st == St::Idle && app.now > nextStrollAsk && in.idleSec < 60 && !app.world.web) {
        nextStrollAsk = app.now + randf(30, 60) * 60;
        ask("strollAsk", "choiceStroll", "stroll");
    }
    followStep(dt);
    if (!notesQueue.empty() && !app.bubble.active && grounded() && st != St::Sleep) {
        notesT -= dt;
        if (notesT <= 0) {
            app.bubble.say(notesQueue.front());
            notesQueue.erase(notesQueue.begin());
            notesT = 1.2f;
        }
    }

    webUpdate(dt, in);
}

bool Pet::tired() const { return vit.energy < 35.f; }

double Pet::babyLeft() const { return std::max(0.0, 4 * 3600.0 - ((double)time(nullptr) - vit.babyStart)); }

void Pet::babyUpdate(float dt, const Input& in) {
    (void)in;
    if (!growing && babyLeft() <= 0 && grounded() && !dragging && !vit.fatal && !vit.dead) {
        growing = true;
        if (st == St::Sleep) wake(false);
        app.bubble.close();
        logf("baby: time to grow");
        if (app.notify) app.notify(L"Узи", L"Кажется, она растёт! Загляни к ней.");
        introGrowStarted(app, *this);
    }
    glow = growing ? std::min(1.f, glow + dt / 6.f) : std::max(0.f, glow - dt * 2.f);
    if (growing && st == St::Sleep) wake(false);
    if (weak()) tSad = std::max(tSad, 0.4f);
    laterT = 0;
}

float Pet::tiredK() const {
    if (st == St::Sleep || vit.fatal) return 0.f;
    return smooth01(clampf((40.f - vit.energy) / 25.f, 0.f, 1.f));
}

static int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }

void Vitals::load(int waitMs) {
    if (!persist) return;
    if (!waitIniReadable(path(), L"vitals", waitMs)) {
        if (!loadFailed) logf("vitals: state.ini есть, но не читается — не трогаю его, перечитаю позже");
        loadFailed = true;
        return;
    }
    loadFailed = false;
    fresh = GetFileAttributesW(path().c_str()) == INVALID_FILE_ATTRIBUTES;
    wchar_t b[64];
    auto f = [&](const wchar_t* k, float def) {
        GetPrivateProfileStringW(L"vitals", k, std::to_wstring(def).c_str(), b, 64, path().c_str());
        return (float)_wtof(b);
    };
    energy = clampf(f(L"energy", 100.f), 0.f, 100.f);
    rel = clampf(f(L"rel", 50.f), 0.f, 100.f);
    food = clampf(f(L"food", 100.f), 0.f, 100.f);
    drainH = clampf(f(L"drainH", 3.5f), 3.f, 4.f);
    restoreH = clampf(f(L"restoreH", 1.5f), 1.f, 2.f);
    fatal = GetPrivateProfileIntW(L"vitals", L"fatal", 0, path().c_str()) != 0;
    stock = clampi(GetPrivateProfileIntW(L"vitals", L"stock", 2, path().c_str()), 0, 6);
    starved = GetPrivateProfileIntW(L"vitals", L"starved", 0, path().c_str()) != 0;
    dead = GetPrivateProfileIntW(L"vitals", L"dead", 0, path().c_str()) != 0;
    inBin = GetPrivateProfileIntW(L"vitals", L"inBin", 0, path().c_str()) != 0;
    parcelOut = GetPrivateProfileIntW(L"vitals", L"parcelOut", 0, path().c_str()) != 0;
    zeroT = f(L"zeroT", 0.f);
    parcelT = f(L"parcelT", 150.f);
    parcelLife = f(L"parcelLife", 0.f);
    lastSeen = (double)f(L"lastSeen", 0.f);
    GetPrivateProfileStringW(L"vitals", L"lastSeen", L"0", b, 64, path().c_str());
    lastSeen = _wtof(b);
    intro = GetPrivateProfileIntW(L"vitals", L"intro", 0, path().c_str());
    babyDrops = GetPrivateProfileIntW(L"vitals", L"babyDrops", 0, path().c_str());
    GetPrivateProfileStringW(L"vitals", L"babyStart", L"0", b, 64, path().c_str());
    babyStart = _wtof(b);
    GetPrivateProfileStringW(L"vitals", L"metAt", L"0", b, 64, path().c_str());
    metAt = _wtof(b);
    if (metAt <= 0) metAt = babyStart > 0 && intro != 1 ? babyStart : (double)time(nullptr);
    annivDone = GetPrivateProfileIntW(L"vitals", L"annivDone", 0, path().c_str());
    holidayDay = GetPrivateProfileIntW(L"vitals", L"holidayDay", 0, path().c_str());
    GetPrivateProfileStringW(L"vitals", L"lastSolver", L"0", b, 64, path().c_str());
    lastSolver = _wtof(b);
    GetPrivateProfileStringW(L"vitals", L"lastNote", L"0", b, 64, path().c_str());
    lastNote = _wtof(b);
    GetPrivateProfileStringW(L"vitals", L"lastSelfArt", L"0", b, 64, path().c_str());
    lastSelfArt = _wtof(b);
    trust = clampf(f(L"trust", 30.f), 0.f, 100.f);
    cornerState = GetPrivateProfileIntW(L"vitals", L"cornerState", 0, path().c_str()) == 1 ? 1 : 0;
    GetPrivateProfileStringW(L"vitals", L"cornerAskedAt", L"0", b, 64, path().c_str());
    cornerAskedAt = _wtof(b);
    GetPrivateProfileStringW(L"vitals", L"cornerMoodAt", L"0", b, 64, path().c_str());
    cornerMoodAt = _wtof(b);
    cornerPhotoDay = GetPrivateProfileIntW(L"vitals", L"cornerPhotoDay", 0, path().c_str());
    logf("vitals: energy %.1f rel %.1f food %.1f stock %d fatal %d dead %d trust %.0f", energy, rel, food, stock, (int)fatal, (int)dead, trust);
}

void Vitals::save() const {
    if (!persist || loadFailed) return;
    auto w = [&](const wchar_t* k, float v) {
        wchar_t b[32];
        swprintf(b, 32, L"%.3f", v);
        WritePrivateProfileStringW(L"vitals", k, b, path().c_str());
    };
    w(L"energy", energy);
    w(L"rel", rel);
    w(L"food", food);
    w(L"drainH", drainH);
    w(L"restoreH", restoreH);
    WritePrivateProfileStringW(L"vitals", L"fatal", fatal ? L"1" : L"0", path().c_str());
    auto wi = [&](const wchar_t* k, int v) { WritePrivateProfileStringW(L"vitals", k, std::to_wstring(v).c_str(), path().c_str()); };
    wi(L"stock", stock);
    wi(L"starved", starved);
    wi(L"dead", dead);
    wi(L"inBin", inBin);
    wi(L"parcelOut", parcelOut);
    w(L"zeroT", zeroT);
    w(L"parcelT", parcelT);
    w(L"parcelLife", parcelLife);
    w(L"trust", trust);
    wi(L"intro", intro);
    wi(L"babyDrops", babyDrops);
    WritePrivateProfileStringW(L"vitals", L"babyStart", std::to_wstring((long long)babyStart).c_str(), path().c_str());
    WritePrivateProfileStringW(L"vitals", L"metAt", std::to_wstring((long long)metAt).c_str(), path().c_str());
    wi(L"annivDone", annivDone);
    wi(L"holidayDay", holidayDay);
    WritePrivateProfileStringW(L"vitals", L"lastSolver", std::to_wstring((long long)lastSolver).c_str(), path().c_str());
    WritePrivateProfileStringW(L"vitals", L"lastNote", std::to_wstring((long long)lastNote).c_str(), path().c_str());
    WritePrivateProfileStringW(L"vitals", L"lastSelfArt", std::to_wstring((long long)lastSelfArt).c_str(), path().c_str());
    wi(L"cornerState", cornerState);
    WritePrivateProfileStringW(L"vitals", L"cornerAskedAt", std::to_wstring((long long)cornerAskedAt).c_str(), path().c_str());
    WritePrivateProfileStringW(L"vitals", L"cornerMoodAt", std::to_wstring((long long)cornerMoodAt).c_str(), path().c_str());
    wi(L"cornerPhotoDay", cornerPhotoDay);
    WritePrivateProfileStringW(L"vitals", L"lastSeen", std::to_wstring((long long)time(nullptr)).c_str(), path().c_str());
}

namespace {
const float kFoodH = 20.f;
const float kAwayK = 7.5f;
const float kBattery = 35.f;
const float kZeroDie = 600.f;
}

void Pet::offlineCatchUp() {
    double now = (double)time(nullptr);
    double el = vit.lastSeen > 0 ? now - vit.lastSeen : 0;
    vit.lastSeen = now;
    missGap = el;
    if (el < 60 || vit.dead) return;
    float was = vit.food;
    int ate = 0;
    const float rate = 100.f / (kFoodH * 3600.f) / kAwayK;
    for (double left = el; left > 0 && !vit.dead;) {
        double st = std::min(60.0, left);
        left -= st;
        if (vit.food < 30.f && vit.stock > 0 && !vit.starved) {
            vit.stock--;
            vit.food = std::min(100.f, vit.food + kBattery);
            ate++;
        }
        vit.food = std::max(0.f, vit.food - (float)st * rate);
        if (vit.food <= 0.f) {
            vit.zeroT += (float)st / kAwayK;
            if (vit.zeroT >= kZeroDie && !vit.starved) {
                if (app.settings.hardcore) vit.dead = true;
                else vit.starved = true;
                vit.fatal = true;
            }
        }
    }
    if (!vit.dead) {
        vit.energy = std::min(100.f, vit.energy + (float)el * 100.f / (vit.restoreH * 3600.f));
        if (vit.fatal && !vit.starved && vit.energy >= 100.f) vit.fatal = false;
    }
    logf("offline %.1f h: food %.1f -> %.1f, ate %d, stock %d, dead %d", el / 3600.0, was, vit.food, ate, vit.stock, (int)vit.dead);
    if (ate && !vit.dead) {
        laterCat = "ateAlone";
        laterT = 6.f;
    }
    vit.save();
}

void Pet::hungerUpdate(float dt, double idleSec) {
    if (vit.dead) return;
    bool present = idleSec < 600;
    float rate = 100.f / (kFoodH * 3600.f) * (present ? 1.f : 1.f / kAwayK);
    vit.food = std::max(0.f, vit.food - dt * rate);
    bool can = !vit.fatal && grounded() && !dragging && (st == St::Idle || st == St::Sit || st == St::Watch || st == St::LookAround);
    if (!present && vit.food < 30.f && vit.stock > 0 && can) eatBattery(true);
    if (vit.food <= 0.f) {
        vit.zeroT += dt * (present ? 1.f : 1.f / kAwayK);
        if (vit.zeroT >= kZeroDie && !vit.starved) {
            if (app.settings.hardcore) die();
            else starve();
            return;
        }
    } else {
        vit.zeroT = 0;
    }
    if (vit.fatal) return;
    float hk = hungerK();
    if (hk > 0 && present && st != St::Sleep) {
        hungryAskT -= dt;
        if (hungryAskT <= 0) {
            bool weak = vit.food < 10.f;
            hungryAskT = weak ? randf(120, 180) : randf(240, 480);
            tSquint = 1.2f;
            sayCat(weak ? "hungryWeak" : (vit.stock > 0 ? "hungry" : "hungryEmpty"), 1.f, L"", true);
        }
        relHungryT += dt;
        if (relHungryT > (vit.food < 10.f ? 600.f : 1200.f)) {
            relHungryT = 0;
            addRel(-1.f, "hungry, not fed");
        }
    }
    if (vit.food < 10.f && present && !vit.notified && app.notify) {
        vit.notified = true;
        app.notify(L"Узи очень голодная", L"Покорми её: перетащи на неё ярлык «Батарейки» с рабочего стола.");
    }
    if (vit.food > 30.f) vit.notified = false;
}

V2 Pet::mouthScreen() const {
    if (active == 0 && app.rig.mouth.on && app.rig.mouth.part >= 0 && app.rig.mouth.part < (int)app.inst.world.size())
        return xf(app.inst.world[app.rig.mouth.part], app.rig.mouth.center) + app.winOrigin;
    return headC + V2(0, headR * 0.4f);
}

void Pet::startFeeding() {
    if (vit.stock <= 0) {
        sayCat("stockEmpty", 1.f, L"", true);
        return;
    }
    if (vit.food > 85.f && !vit.fatal) {
        sayCat("notHungry", 1.f, L"", true);
        return;
    }
    feeding = true;
    sayCat("feedReady", 1.f, L"", true);
}

void Pet::feedTick() {
    mouthWant = 0;
    if (!feeding) return;
    float d = (lastCursor - mouthScreen()).len();
    if (d < headR * 0.7f) {
        eatBattery(false);
        return;
    }
    mouthWant = clampf(1.f - d / (headR * 3.f), 0.f, 1.f);
}

void Pet::eatBattery(bool self) {
    if (vit.stock <= 0) { feeding = false; return; }
    bool wasHungry = vit.food < 30.f;
    vit.stock--;
    vit.food = std::min(100.f, vit.food + kBattery);
    vit.zeroT = 0;
    feeding = false;
    eatT = 1.1f;
    groggyT = std::min(groggyT, 60.f);
    hungryAskT = randf(240, 480);
    if (!self) addRel(wasHungry ? 1.5f : 0.5f, "fed");
    logf("ate battery (%s): food %.1f, stock %d", self ? "self" : "you", vit.food, vit.stock);
    if (vit.starved) {
        vit.starved = false;
        reboot();
    } else if (!self) {
        tHappy = 1.6f;
        if (!app.bubble.held()) app.bubble.close();
        dayCount("fed");
        if (wasHungry && tier() >= 1) cornerPhoto(L"я сытая");
        sayCat("eatYum", 1.f, L"", true);
        for (int i = 0; i < 2; i++) emit(0, headTop + V2(randf(-0.5f, 0.5f) * headR, 0), V2(randf(-20, 20) * S(), -70.f * S()), 1.1f, 0.8f);
        if (baby()) sleepAfterT = 3.f;
    }
    vit.save();
}

void Pet::offerUpdate(const std::wstring& ver, int cmp) {
    fileHover(false);
    if (st == St::Sleep && !vit.fatal) {
        wakeUpdateVer = ver;
        wakeUpdateCmp = cmp;
        gentleWake("update");
        return;
    }
    tSurprise = 0.6f;
    app.bubble.close();
    if (cmp > 0) {
        if (!ask("updateAsk", "choiceUpdate", "update", ver)) sayCat("updateAsk", 1.f, ver, true);
    } else {
        sayCat(cmp == 0 ? "updateSame" : "updateOld", 1.f, ver, true);
    }
}

void Pet::batteryHover(bool on) {
    batHover = on;
    if (on && !vit.fatal && vit.food > 85.f) tSquint = 0.6f;
}

void Pet::batteryDrop() {
    batHover = false;
    if (vit.dead) return;
    if (vit.stock <= 0) {
        tSad = 1.2f;
        sayCat("stockEmpty", 1.f, L"", true);
        return;
    }
    if (st == St::Sleep && !vit.starved) {
        gentleWake("food");
        return;
    }
    if (vit.food > 85.f && !vit.starved) {
        if (fabsf(lastCursor.x - pos.x) > 4.f) facing = lastCursor.x > pos.x ? -1 : 1;
        tAngry = 1.f;
        squashV -= 1.2f;
        app.bubble.close();
        sayCat("feedRefuse", 1.f, L"", true);
        return;
    }
    eatBattery(false);
}

void Pet::giveBattery(int n, const char* why) {
    int was = vit.stock;
    vit.stock = std::min(6, vit.stock + n);
    logf("battery +%d (%s): stock %d", vit.stock - was, why, vit.stock);
    vit.save();
}

void Pet::parcelLanded() {
    if (st == St::Sleep) return;
    tSurprise = 0.6f;
    sayCat("parcelLand", 1.f, L"", true);
}

void Pet::parcelOpened() {
    bool full = vit.stock >= 6;
    giveBattery(3, "parcel");
    vit.parcelOut = false;
    vit.parcelT = 24.f * 3600.f;
    if (st == St::Sleep) {
        logf("parcel: opened while she sleeps, +3 quietly");
        vit.save();
        return;
    }
    tHappy = 2.f;
    if (baby()) {
        tHappy = 0;
        tSad = 2.5f;
        app.bubble.close();
        sayCat("babyWantFood", 1.f, L"", true);
    } else {
        sayCat(full ? "parcelFull" : "parcelOpen", 1.f, L"", true);
    }
    vit.save();
}

void Pet::die() {
    vit.dead = true;
    vit.fatal = true;
    feeding = false;
    logf("DEAD: starved (hardcore)");
    app.bubble.close();
    followT = 0;
    strollOn = false;
    quietUntil = 0;
    tryMode = false;
    dropCarried(false);
    dropHeld("fatal");
    vit.save();
    if (!dragging) {
        if (grounded()) setState(St::Fatal);
        else startFall({});
    }
}

void Pet::starve() {
    vit.starved = true;
    vit.fatal = true;
    logf("starved: shut down until fed");
    app.bubble.close();
    vit.save();
    if (!dragging) {
        if (grounded()) setState(St::Fatal);
        else startFall({});
    }
}

void Pet::addRel(float d, const char* why) {
    float was = vit.rel;
    int t0 = tier();
    if (d > 0 && vit.rel < 30.f) d *= 0.6f;
    vit.rel = clampf(vit.rel + d, 0.f, 100.f);
    logf("rel %.1f -> %.1f (%s)", was, vit.rel, why);
    int t1 = tier();
    syncStage();
    if (t1 != t0) {
        laterCat = t1 > t0 ? "tierUp" : "tierDown";
        laterArg.clear();
        laterT = 3.5f;
        logf("relationship tier %d -> %d", t0, t1);
    }
}

const char* Pet::idleCat() const {
    if (const char* m = moodIdleCat()) return m;
    return "idle";
}

void Pet::startNightmare() {
    nightmare = true;
    nightmareT = 0;
    nightmareAskT = sleepAfterT = 0;
    deepSleep = false;
    nextNightmareOk = app.now + randf(3, 5) * 3600;
    logf("nightmare (rel %.1f, tier %d)", vit.rel, tier());
    app.bubble.close();
    squashV -= 2.f;
    tSurprise = 0.7f;
    setState(St::Sit, 1e9f);
    if (!nightmareTalkStart()) ask("nightmareWake", "choiceNightmare", "nightmare");
    if (app.notify) app.notify(L"Узи приснился кошмар", app.bubble.asking() ? app.bubble.text : L"Она проснулась и зовёт тебя.");
}

void Pet::comfortNightmare(bool petted) {
    if (!nightmare) return;
    nightmare = false;
    int t = tier();
    addRel(petted ? 4.f : 3.f, petted ? "nightmare: petted" : "nightmare: comforted");
    if (app.bubble.asking()) app.bubble.close();
    tSad = 0;
    if (t >= 2) {
        tBlush = 3.f;
        sayCat("nightmareThanks", 1.f, L"", true);
        nightmareAskT = 3.2f;
    } else {
        sayCat("nightmareThanks", 1.f, L"", true);
        sleepAfterT = 2.2f;
    }
}

void Pet::vitalsUpdate(float dt, double idleSec) {
    if (st == St::Sleep && vit.intro == 0 && !carriedAsleep() && !vit.fatal && !afkNap && stT > 300.f && idleSec < 120 && app.now > nextNightmareOk &&
        !app.bubble.asking() && !app.bubble.held() && chance(dt / 14000.f))
        startNightmare();
    if (nightmare) {
        nightmareT += dt;
        tSad = std::max(tSad, 0.3f * (1.f - nightmareCalm));
        bool talked = nightmareSteps > 0 || nightmarePet;
        if ((nightmareT > (talked ? 300.f : 75.f) || (!app.bubble.asking() && nightmareT > 3.f)) && !app.bubble.held()) {
            nightmare = false;
            nightmareTalkId.clear();
            if (app.bubble.asking()) app.bubble.close();
            if (talked) addRel(1.5f, "nightmare: talked a bit");
            sayCat(talked ? "nightmareFade" : "nightmareAlone", 1.f, L"", true);
            sleepAfterT = 2.5f;
            logf(talked ? "nightmare: the talk stopped halfway, she goes back to sleep" : "nightmare: nobody came");
        }
    }
    if (nightmareAskT > 0) {
        nightmareAskT -= dt;
        if (nightmareAskT <= 0) {
            if (app.bubble.held()) nightmareAskT = 0.25f;
            else if (ask("nightmareSleepAsk", "choiceNightmareSleep", "nightmareSleep")) sleepAfterT = 45.f;
            else sleepAfterT = 2.5f;
        }
    }
    if (sleepAfterT > 0) {
        sleepAfterT -= dt;
        if (sleepAfterT <= 0 && grounded() && !dragging && !app.bubble.asking()) sleepNow();
        else if (sleepAfterT <= 0 && app.bubble.asking()) sleepAfterT = 2.f;
    }
    if (vit.loadFailed) {
        static float retry = 0;
        retry -= dt;
        if (retry <= 0) {
            retry = 3.f;
            vit.load(0);
            if (!vit.loadFailed) {
                logf("vitals: state.ini прочитан со второй попытки");
                offlineCatchUp();
                if (!greeted) missArm();
            }
        }
    }
    if (vit.dead) {
        if (grounded() && !dragging && st != St::Fatal) setState(St::Fatal);
        saveT -= dt;
        if (saveT <= 0) { saveT = 20.f; vit.save(); }
        return;
    }
    hungerUpdate(dt, idleSec);
    feedTick();
    if (vit.dead) return;
    if (!vit.parcelOut) {
        vit.parcelT -= dt;
        if (vit.parcelT <= 0) {
            vit.parcelOut = true;
            vit.parcelLife = 24.f * 3600.f;
            parcelFresh = true;
            logf("parcel arrives");
            vit.save();
        }
    } else {
        vit.parcelLife -= dt;
        if (vit.parcelLife <= 0) {
            vit.parcelOut = false;
            vit.parcelT = 24.f * 3600.f;
            sayCat("parcelGone", 1.f, L"", true);
            vit.save();
        }
    }
    bool asleep = st == St::Sleep || vit.fatal;
    if (asleep && afkNap && !vit.fatal) {
        if (vit.energy < kAfkCap) vit.energy = std::min(kAfkCap, vit.energy + dt * 100.f / (vit.restoreH * 3600.f));
    } else if (asleep) vit.energy = std::min(100.f, vit.energy + dt * 100.f / (vit.restoreH * 3600.f));
    else vit.energy = std::max(0.f, vit.energy - dt * 100.f / (vit.drainH * 3600.f));
    if (!vit.fatal && !asleep && vit.energy <= 0.f) enterFatal();
    if (vit.fatal) {
        if (grounded() && !dragging && st != St::Fatal) setState(St::Fatal);
        if (vit.energy >= 100.f && st == St::Fatal && !vit.starved) reboot();
    } else if (st == St::Sleep && vit.energy >= 100.f && stT > 60.f && !carriedAsleep() && !afkNap && !app.bubble.held()) {
        if (stT > 20.f * 60.f) {
            addRel(1.f, "slept well");
            giveBattery(1, "let her sleep");
        }
        wake(false);
        app.bubble.close();
        sayCat("wakeRested", 1.f, L"", true);
        vit.drainH = randf(3.f, 4.f);
    }
    bool can = !vit.fatal && grounded() && !dragging && (st == St::Idle || st == St::Sit || st == St::Watch || st == St::LookAround) &&
               nest == NestNone && !webActive() && !app.bubble.asking() && carriedName.empty() && heldPath.empty() && !tryMode;
    if (can && vit.energy < 15.f && app.now > noSleepUntil) {
        sayCat("sleepyGo", 1.f, L"", true);
        sleepNow();
    } else if (can && vit.energy < 30.f) {
        sleepyT -= dt;
        if (sleepyT <= 0) {
            sleepyT = randf(300, 600);
            sayCat("sleepy", 0.8f);
        }
    }
    saveT -= dt;
    if (saveT <= 0) {
        saveT = 20.f;
        vit.save();
    }
}

void Pet::enterFatal() {
    vit.fatal = true;
    vit.restoreH = randf(1.f, 2.f);
    addRel(-10.f, "fatal error");
    logf("FATAL: energy at zero");
    app.bubble.close();
    followT = 0;
    strollOn = false;
    quietUntil = 0;
    tryMode = false;
    dropCarried(false);
    dropHeld("fatal");
    vit.save();
    if (dragging) return;
    if (grounded()) setState(St::Fatal);
    else startFall({});
}

void Pet::reboot() {
    vit.fatal = false;
    vit.drainH = randf(3.f, 4.f);
    logf("reboot after fatal");
    tCross = 0.3f;
    setState(St::Dizzy, 2.4f);
    sayCat("rebootSay", 1.f, L"", true);
    vit.save();
}

void Pet::forcedWake(bool lifted) {
    if (baby()) {
        wake(true);
        app.bubble.close();
        pendingSay.clear();
        sayCat("babyCry", 1.f, L"", true);
        return;
    }
    if (afkNap) {
        wakePokes = 0;
        wake(false);
        return;
    }
    float pen = 1.5f + 4.5f * (1.f - vit.energy / 100.f);
    addRel(-pen, lifted ? "woken by lifting" : "woken by poking");
    noSleepUntil = app.now + randf(60, 120);
    wakePokes = 0;
    wake(true);
    app.bubble.close();
    pendingSay.clear();
    sayCat(vit.energy < 30.f ? "wokenTired" : "wokenUp", 1.f, L"", true);
}

void Pet::gentleWake(const char* why) {
    if (st != St::Sleep || stirT >= 0 || vit.fatal || nightmare) return;
    stirT = 0;
    stirWhy = why;
    if (!app.bubble.asking() && !app.bubble.held()) app.bubble.close();
    sayCat(baby() ? "babyCoo" : "wakeStir", 1.f, L"", true);
    logf("gentle wake (%s): stirring, energy %.0f", why, vit.energy);
}

void Pet::stirStep(float dt) {
    if (stirT < 0 && !wakeUpdateVer.empty() && st != St::Sleep && !app.bubble.held() && !app.bubble.asking()) {
        std::wstring v = wakeUpdateVer;
        wakeUpdateVer.clear();
        offerUpdate(v, wakeUpdateCmp);
    }
    if (stirT < 0) return;
    if (st != St::Sleep) {
        stirT = -1;
        return;
    }
    float t0 = stirT;
    stirT += dt;
    zzzT = 2.f;
    if ((int)(t0 / 1.3f) != (int)(stirT / 1.3f)) squashV -= 0.6f;
    if (stirT < 5.f) return;
    stirT = -1;
    std::string why = stirWhy;
    stirWhy.clear();
    wakePokes = 0;
    bool wasAfk = afkNap;
    deepSleep = false;
    goIdle(1.2f, 2.f);
    if (afkNap) afkWoke();
    else tSurprise = 0.1f;
    if (baby()) return;
    groggyT = randf(180.f, 300.f);
    noSleepUntil = app.now + randf(60, 120);
    if (!wasAfk) {
        pendingSay.clear();
        if (!app.bubble.asking() && !app.bubble.held()) app.bubble.close();
        sayCat("wakeGentle", 1.f, L"", true);
    }
    logf("gentle wake (%s): up, groggy for %.0f s", why.c_str(), groggyT);
    if (why == "food" && vit.stock > 0 && vit.food <= 85.f) eatBattery(false);
}

void Pet::openTalk() {
    if (vit.dead || dragging || st == St::Cling || st == St::Dive || st == St::Emerge || st == St::Inside) return;
    if (vit.fatal) return;
    gameStop("talk");
    auto meters = [&] {
        app.bubble.meters = {{vit.energy, D2D1::ColorF(0.96f, 0.72f, 0.24f, 1), 0},
                             {vit.food, D2D1::ColorF(0.30f, 0.76f, 0.54f, 1), 1},
                             {vit.rel, D2D1::ColorF(1.f, 0.37f, 0.66f, 1), 2}};
    };
    if (vit.intro == 1) return;
    if (baby()) {
        if (app.bubble.asking()) return;
        app.bubble.close();
        std::wstring s = app.phrases.pick(st == St::Sleep ? "talkAsleep" : "babyTalk", L"");
        auto ch = app.phrases.list("choiceTalkBaby");
        if (s.empty() || ch.empty()) return;
        app.bubble.ask(s, ch, "talkBaby");
        app.bubble.meters = {{vit.energy, D2D1::ColorF(0.96f, 0.72f, 0.24f, 1), 0},
                             {vit.food, D2D1::ColorF(0.30f, 0.76f, 0.54f, 1), 1},
                             {(float)(babyLeft() / (4 * 3600.0) * 100.0), D2D1::ColorF(0.33f, 0.62f, 1.f, 1), 3}};
        lastSay = app.now;
        return;
    }
    if (st == St::Sleep) {
        if (carriedAsleep() || app.bubble.asking()) return;
        app.bubble.close();
        std::wstring s = app.phrases.pick("talkAsleep", L"");
        auto ch = app.phrases.list("choiceTalkAsleep");
        if (s.empty() || ch.empty()) return;
        app.bubble.ask(s, ch, "talkSleep");
        meters();
        lastSay = app.now;
        return;
    }
    app.bubble.close();
    followT = 0;
    feeding = false;
    auto ch = app.phrases.list("choiceTalk");
    if ((st == St::Sit || st == St::Watch) && ch.size() > 2) ch[2] = L"Встань";
    talkFeed = false;
    std::wstring s = app.phrases.pick(vit.food < 30.f ? "hungry" : "talkOpen", L"");
    if (s.empty() || app.bubble.asking()) return;
    if (grounded() && (st == St::Walk || st == St::Idle || st == St::LookAround)) setState(St::Idle, 1e9f);
    if (fabsf(lastCursor.x - pos.x) > 6.f) facing = lastCursor.x > pos.x ? 1 : -1;
    app.bubble.ask(s, ch, "talk");
    meters();
    lastSay = app.now;
}

void Pet::talkChoice(int c) {
    if (talkFeed) {
        if (c == 0) {
            startFeeding();
            return;
        }
        c--;
    }
    if (tier() == 0 && (c == 0 || c == 2 || c == 3 || c == 4) && chance(0.8f)) {
        tAngry = 1.4f;
        if (fabsf(lastCursor.x - pos.x) > 6.f) facing = lastCursor.x > pos.x ? -1 : 1;
        sayCat("obeyNo", 1.f, L"", true);
        return;
    }
    switch (c) {
    case 0:
        quietUntil = 0;
        if (strollOn) { strollOn = false; strollUntil = 0; }
        if (st == St::Hide || nest != NestNone || webActive()) { callBack(lastCursor); return; }
        sayCat("comeYes", 1.f, L"", true);
        followT = 14.f;
        followNear = 0;
        if (st == St::Sit || st == St::Watch || st == St::Sleep || st == St::Clap) goIdle(0.3f, 0.6f);
        break;
    case 1:
        sleepTalk();
        break;
    case 2:
        if (st == St::Sit || st == St::Watch) {
            followT = 0;
            sayCat("standYes", 1.f, L"", true);
            goIdle(1.f, 2.f);
            break;
        }
        followT = 0;
        tHappy = 1.f;
        sayCat("sitYes", 1.f, L"", true);
        if (grounded()) setState(St::Sit, randf(180, 300));
        break;
    case 3:
        tHappy = 1.6f;
        lastWave = app.now;
        sayCat("waveYes", 1.f, L"", true);
        if (grounded()) setState(St::Wave, 1.9f);
        break;
    case 4:
        startQuiet();
        break;
    case 5:
        wantSettings = true;
        sayCat("settingsYes", 1.f, L"", true);
        break;
    case 6:
        gameMenu();
        break;
    }
}

void Pet::sleepTalk() {
    float agree = tired() ? 0.85f : vit.energy > 85.f ? 0.25f : 0.5f;
    const float byTier[4] = {0.45f, 1.f, 1.25f, 1.5f};
    agree = clampf(agree * byTier[tier()], 0.f, 0.95f);
    if (chance(agree)) {
        tHappy = 1.f;
        sayCat("sleepOk", 1.f, L"", true);
        sleepNow();
        return;
    }
    tAngry = 1.5f;
    ask("sleepRefuse", "choiceSleepRefuse", "sleepArgue");
}

void Pet::sleepNow() {
    deepSleep = true;
    afkNap = false;
    afkGo = 0;
    vit.restoreH = randf(1.2f, 1.8f);
    if (grounded()) setState(St::Sleep, 1e9f);
    zzzT = 1.5f;
}

bool Pet::byeShutdown() {
    if (byeOn || baby() || vit.fatal || vit.dead || vit.inBin || vit.intro == 1 || introHidden) return false;
    if (st == St::Sleep || st == St::Dive || st == St::Inside || st == St::Emerge || webActive()) return false;
    int tr = tier();
    bool lay = grounded() && !dragging && st != St::Hide;
    const char* cat = !lay ? "byeShutdownUp" : "byeShutdown";
    std::wstring s = app.phrases.pick(cat, L"");
    if (s.empty()) return false;
    gameStop("shutdown");
    extStop("shutdown");
    app.bubble.close();
    feeding = false;
    tryMode = false;
    nightmare = false;
    nightmareAskT = sleepAfterT = 0;
    laterT = 0;
    if (lay) {
        deepSleep = false;
        setState(St::Sleep, 1e9f);
        zzzT = 1.2f;
    }
    app.bubble.say(s);
    app.bubble.life = app.bubble.typeDur + 8.f;
    lastSay = app.now;
    byeOn = true;
    byeLay = lay;
    byeAt = app.now;
    logf("shutdown bye (%s): %s, state %s", cat, narrow(s).c_str(), stateName());
    return true;
}

void Pet::byeCancel() {
    if (!byeOn) return;
    byeOn = false;
    app.bubble.close();
    if (st == St::Sleep && !carriedAsleep()) goIdle(1.2f, 2.f);
    tSurprise = 0.5f;
    sayCat(byeLay ? "byeCancel" : "byeCancelUp", 1.f, L"", true);
    logf("shutdown cancelled, state %s", stateName());
}

void Pet::startQuiet() {
    quietUntil = app.now + 3600;
    followT = 0;
    logf("quiet hour (tired %d)", (int)tired());
    if (tired()) {
        sayCat("quietSleep", 1.f, L"", true);
        sleepNow();
        return;
    }
    if (!kStrollsOn) return;
    sayCat("quietWalk", 1.f, L"", true);
    startStroll(60.f);
}

void Pet::startStroll(float minutes) {
    if (!kStrollsOn) return;
    strollOn = true;
    strollUntil = app.now + minutes * 60.0;
    strollPauseUntil = app.now + 3.5;
    logf("stroll for %.0f min", minutes);
}

void Pet::strollNext() {
    auto& W = app.world;
    float left = (float)(strollUntil - app.now);
    int folder = -1;
    if (app.settings.icons && app.iconsReady && !W.web) {
        std::vector<int> fs;
        for (size_t i = 0; i < app.icons.items.size(); i++)
            if (app.icons.items[i].folder && W.rectVisible(app.icons.items[i].img)) fs.push_back((int)i);
        if (!fs.empty()) folder = fs[randi(0, (int)fs.size() - 1)];
    }
    bool web = app.web && app.web->extKnown() && !webActive();
    float wf = folder >= 0 ? 3.f : 0.f, ww = web ? 7.f : 0.f, wh = 1.2f;
    float r = randf(0, wf + ww + wh);
    if (r < wf) {
        const RECT& im = app.icons.items[folder].img;
        V2 top((im.left + im.right) * 0.5f, (float)im.top);
        nestIcon = app.icons.items[folder].name;
        lastNest = app.now;
        logf("stroll: folder '%s'", narrow(nestIcon).c_str());
        if (!goJump(top, 40, 90)) {
            pos = V2(top.x, top.y - comH * 2.6f);
            vel = V2(0, 150.f * S());
            rot = rotVel = 0;
            gnd.valid = false;
            setState(St::Fall);
            app.inst.resetSim();
        }
        return;
    }
    if (r < wf + ww) {
        logf("stroll: web");
        webGo(true);
        return;
    }
    logf("stroll: hide");
    lastHide = app.now;
    edgeL = V2(gnd.x0, gnd.y);
    edgeW = gnd.x1 - gnd.x0;
    sink = 0;
    phase = 0;
    phaseT = 0;
    setState(St::Hide, clampf(left * 0.5f, 60.f, 600.f));
}

void Pet::returnHome() {
    auto& W = app.world;
    auto& A = app.activity;
    if (W.mons.empty()) return;
    int my = W.monitorAt(pos - V2(0, 4));
    if (my < 0) my = W.nearestMonitor(pos);
    int target = my;
    if (W.mons.size() > 1) {
        int cur = W.monitorAt(lastCursor);
        for (size_t m = 0; m < W.mons.size(); m++) {
            bool busy = (int)m == cur || ((int)m == A.fgMon && A.fgCovers);
            if (!busy) { target = (int)m; break; }
        }
    }
    logf("return home: mon %d", target);
    if (target == my && grounded()) {
        lastWave = app.now;
        setState(St::Wave, 1.9f);
        sayCat("strollBack", 1.f, L"", true);
        sitAfterWave = true;
        return;
    }
    const RECT& wk = W.mons[target].work;
    int cur = W.monitorAt(lastCursor);
    float x = wk.right - 150.f * S();
    if (cur >= 0 && cur != target && W.mons[cur].rc.left < W.mons[target].rc.left) x = wk.left + 150.f * S();
    pos = V2(x, (float)W.mons[target].rc.top + comH * 1.3f + 10.f);
    vel = V2(0, 200.f * S());
    rot = rotVel = 0;
    gnd.valid = false;
    sink = 0;
    setState(St::Fall);
    app.inst.resetSim();
    waveOnLand = true;
}

void Pet::followStep(float dt) {
    if (followT <= 0) return;
    followT -= dt;
    if (dragging || !grounded() || !(st == St::Idle || st == St::Walk || st == St::LookAround)) return;
    auto& W = app.world;
    int myMon = W.monitorAt(pos - V2(0, 4)), curMon = W.monitorAt(lastCursor);
    if (curMon >= 0 && myMon >= 0 && curMon != myMon) {
        followT = 0;
        comeHere(lastCursor);
        return;
    }
    float tx = clampf(lastCursor.x, gnd.x0, gnd.x1);
    if (fabsf(tx - pos.x) > 26.f * S()) {
        followNear = 0;
        if (st == St::Walk) walkTarget = tx;
        else goWalk(tx);
    } else {
        followNear += dt;
        if (st == St::Walk) goIdle(2.f, 3.f);
    }
    if (followNear > 1.2f || followT <= 0) {
        followT = 0;
        if (fabsf(lastCursor.x - pos.x) > 4) facing = lastCursor.x > pos.x ? 1 : -1;
        sayCat("comeArrive", 0.6f, L"", true);
        setState(St::Sit, randf(90, 180));
    }
}

bool Pet::lifeState(float dt, const Input& in) {
    auto& W = app.world;
    float D = sinkDepthPx();
    float peekSink = clampf(standEye / D, 0.f, 1.f);
    switch (st) {
    case St::Watch:
    case St::Stretch:
    case St::Ears:
    case St::Clap:
        if (!groundCheck(dt)) {
            startFall({});
            break;
        }
        if (st == St::Watch) facing = watchTarget.x > pos.x ? 1 : -1;
        if (st != St::Watch && stT >= stDur) goIdle(1.f, 2.f);
        break;

    case St::Lift:
        if (!groundCheck(dt)) {
            startFall({});
            break;
        }
        facing = lastCursor.x > pos.x ? 1 : -1;
        if (stT >= stDur) {
            if (liftMon >= 0) sayCat("liftGiveUp", 1.f, L"", true);
            liftMon = -1;
            tSad = 2.f;
            goIdle();
        }
        break;

    case St::Hide: {
        if (!groundCheck(dt)) {
            sink = 0;
            startFall({});
            break;
        }
        edgeL.y = gnd.y;
        phaseT += dt;
        bool curNear = (in.cursor - V2(pos.x, gnd.y)).len() < 160.f * S();
        if (phase == 0) {
            sink = std::min(1.f, sink + dt * 2.6f);
            if (sink >= 1) { phase = 1; phaseT = 0; }
        } else if (phase == 1) {
            if (phaseT > 2.2f && !curNear) { phase = 2; phaseT = 0; }
        } else if (phase == 2) {
            sink = approach(sink, peekSink, 9, dt);
            lookTurn = sinf(phaseT * 1.8f);
            if (curNear && phaseT > 0.4f) {
                phase = 1;
                phaseT = 0;
                sink = std::min(1.f, sink + 0.15f);
                emit(2, V2(pos.x, gnd.y - 20.f * S()), V2(0, -40.f * S()), 0.6f, 1.f);
                sayCat("hideGiggle", 0.5f);
                phase = 0;
            } else if (phaseT > 3.2f) {
                phase = stT > stDur ? 3 : 1;
                phaseT = 0;
                if (phase == 1) phase = 0;
            }
        } else {
            sink = std::max(0.f, sink - dt * 2.2f);
            lookTurn = 0;
            if (sink <= 0) {
                tHappy = 1.2f;
                sayCat("hideOut", 0.5f);
                goIdle(1.f, 2.f);
            }
        }
        break;
    }

    case St::Dive:
        phaseT += dt;
        if (emergeMode == 2 && phase == 9) break;
        sink = std::min(1.f, sink + dt * (phaseT < 0.15f ? 0.6f : 2.8f));
        if (phaseT < 0.05f)
            for (int i = 0; i < 5; i++)
                emit(4, V2(pos.x + randf(-14, 14) * S(), edgeL.y - 2), V2(randf(-70, 70) * S(), randf(-50, -10) * S()), 0.5f, 1.f);
        if (sink >= 1) {
            setState(St::Inside);
            nestT = 0;
            nestLife = nestShame ? randf(120, 300) : nest == NestFolder ? randf(60, 200) : nest == NestSystem ? 600.f : 1e9f;
            if (strollOn && nest == NestFolder) nestLife = clampf((float)(strollUntil - app.now) * 0.6f, 120.f, 1200.f);
            if (nest == NestWeb) webEntered();
        }
        break;

    case St::Inside: {
        nestT += dt;
        pos = V2(edgeL.x + edgeW * 0.5f, edgeL.y);
        if (nest == NestFolder || nest == NestSystem) {
            float tip = clampf((standTop - 11.f * S()) / D, 0.f, 1.f);
            float cyc = fmodf(nestT, 9.f);
            bool peek = nestT > 4.f && cyc > 6.5f;
            sink = approach(sink, peek ? peekSink : tip, peek ? 6.f : 4.f, dt);
            lookTurn = peek ? sinf(cyc * 3.f) * 0.7f : 0.f;
            selectRig(0);
        }
        if (nest == NestFolder) {
            if (app.now - lastNestCheck > 1.0) {
                lastNestCheck = app.now;
                if (HWND h = explorerShowing(nestPath)) {
                    RECT r;
                    if (app.world.windowRect(h, r)) {
                        emergeWnd = h;
                        float x = clampf(randf(0.3f, 0.7f) * (r.right - r.left), 60.f * S(), (r.right - r.left) - 60.f * S());
                        emergeAt(V2((float)r.left + x - 40.f * S(), (float)r.top), 80.f * S(), 1);
                        break;
                    }
                }
            }
            if (nestT > nestLife) {
                int i = app.icons.find(nestPath.substr(nestPath.find_last_of(L'\\') + 1));
                for (size_t k = 0; k < app.icons.items.size(); k++)
                    if (app.icons.items[k].path == nestPath) i = (int)k;
                if (i >= 0) {
                    const RECT& im = app.icons.items[i].img;
                    edgeL = V2((float)im.left, (float)im.top);
                    edgeW = (float)(im.right - im.left);
                }
                emergeAt(edgeL, edgeW, 0);
            }
        } else if (nest == NestSystem) {
            if (g_scan && g_scan->done && !nestShame) emergeAt(edgeL, edgeW, 3);
            else if (nestT > nestLife) emergeAt(edgeL, edgeW, 0);
        }
        break;
    }

    case St::Emerge: {
        phaseT += dt;
        if (gnd.valid && !groundCheck(dt)) {
            sink = 0;
            finishNest(0);
            startFall({});
            break;
        }
        if (gnd.valid) edgeL.y = gnd.y;
        if (phase == 0) {
            sink = approach(sink, peekSink, 10, dt);
            if (phaseT > 0.35f) {
                phase = 1;
                phaseT = 0;
                switch (emergeMode) {
                case 1: tAngry = 2.5f; sayCat("nestBusy", 1.f, L"", true); break;
                case 2: sayCat("nestComing", 1.f, L"", true); break;
                case 4: tBlush = 3.f; tAngry = 2.f; sayCat("shameStay", 1.f, L"", true); break;
                default: break;
                }
            }
        } else if (phase == 1) {
            float wait = emergeMode == 1 ? 1.8f : emergeMode == 2 ? 1.4f : emergeMode == 4 ? 1.7f : 0.2f;
            lookTurn = emergeMode == 0 ? sinf(phaseT * 2.f) * 0.6f : 0.f;
            if (phaseT > wait) { phase = emergeMode == 4 ? 5 : 2; phaseT = 0; }
        } else if (phase == 5) {
            sink = std::min(1.f, sink + dt * 2.6f);
            if (sink >= 1) setState(St::Inside);
        } else if (phase == 2) {
            sink = std::max(0.f, sink - dt * 2.4f);
            lookTurn = 0;
            if (sink <= 0) {
                if (!gnd.valid) {
                    startFall(V2(0, -300.f * S()));
                    finishNest(emergeMode);
                    break;
                }
                phase = 3;
                phaseT = 0;
                squashV -= 2.f;
                if (emergeMode == 0 && nestShame) {
                    nestShame = false;
                    tBlush = 2.f;
                    sayCat("shameBack", 1.f, L"", true);
                } else if (emergeMode == 0 && nest == NestFolder) {
                    int oldest = 9999;
                    std::wstring sum = folderSummary(nestPath, oldest);
                    if (sum.empty()) sayCat("nestEmpty", 1.f, L"", true);
                    else if (oldest < 2020 && chance(0.5f)) sayCat("nestOld", 1.f, std::to_wstring(oldest), true);
                    else sayCat("nestSaw", 1.f, sum, true);
                }
                if (emergeMode == 3 && g_scan) {
                    std::wstring what;
                    if (g_scan->tempBytes > (100ll << 20)) what = L"временные файлы на " + sizeStr((double)g_scan->tempBytes);
                    if (g_scan->binItems > 0) what += (what.empty() ? L"" : L", ") + std::wstring(L"корзина на ") + sizeStr((double)g_scan->binBytes);
                    if (!g_scan->big.empty()) {
                        std::wstring b;
                        for (auto& f : g_scan->big) b += (b.empty() ? L"«" : L", «") + f.first + L"» на " + sizeStr((double)f.second);
                        bigNote = b;
                    }
                    if (what.empty()) {
                        sayCat(bigNote.empty() ? "diskClean" : "diskNothing", 1.f, bigNote, true);
                        bigNote.clear();
                        g_scan.reset();
                    } else {
                        ask("diskFound", "choiceClean", "clean", what);
                    }
                }
            }
        } else {
            if (emergeMode == 1 && emergeWnd && app.settings.closeExplorer && IsWindow(emergeWnd)) {
                if (phaseT > 0.9f && phaseT - dt <= 0.9f) {
                    logf("closing explorer window");
                    PostMessageW(emergeWnd, WM_CLOSE, 0, 0);
                    sayCat("nestClosed", 1.f, L"", true);
                }
                if (phaseT > 1.0f) {
                    finishNest(1);
                    goIdle(1.f, 2.f);
                }
                break;
            }
            int mode = emergeMode;
            finishNest(mode);
            if (mode == 2) {
                comeHere(tryTarget);
                break;
            }
            goIdle(1.f, 2.f);
        }
        break;
    }

    case St::Ride:
        return webRide(dt);

    case St::Fatal: {
        if (!groundCheck(dt)) {
            startFall({});
            break;
        }
        float want = (facing > 0 ? 1.f : -1.f) * PI * 0.5f;
        float was = rot;
        rot = approach(rot, want, 3.2f, dt);
        if (fabsf(was - want) > 0.01f && fabsf(rot - want) <= 0.01f) {
            squashV -= 2.f;
            for (int i = 0; i < 4; i++)
                emit(4, pos + V2(randf(-40, 40) * S(), -2), V2(randf(-70, 70) * S(), randf(-40, -10) * S()), 0.5f, 1.f);
        }
        break;
    }

    case St::Climb: {
        vel = {};
        if (clickDone <= 0) {
            if (stT > 0.35f && !in.lmb && GetForegroundWindow() == tryWnd) {
                clickAt(tryTarget);
                clickDone = 1.f;
                tHappy = 2.f;
                emit(2, tryTarget + V2(10.f, -10.f) * S(), V2(0, -60.f * S()), 0.7f, 1.f);
            } else if (stT > 3.f) {
                tryMode = false;
                sayCat("tryGiveUp", 1.f, L"", true);
                startFall({});
            }
        } else {
            clickDone -= dt * 1.6f;
            if (clickDone <= 0) {
                tryMode = false;
                clapOnLand = true;
                sayCat("tryDone", 1.f, L"", true);
                startFall({});
            }
        }
        break;
    }

    default:
        return false;
    }
    return true;
}

void Pet::lifeFace(FaceState& tg, V2& lk) {
    if (st == St::Ears || tLoud > 0) {
        tg.squint = 1;
        tg.angry = 0.35f;
        tg.happy = 0;
    }
    if (st == St::Watch) {
        V2 d = rotv((watchTarget - headC) / (500.f * S()), -rot);
        lk = V2(clampf(d.x, -0.9f, 0.9f), clampf(d.y, -0.6f, 0.4f));
    }
    if ((st == St::Lift && deskMode != DeskOffer) || (app.bubble.asking() && !nightmare && tier() > 0)) tg.happy = std::max(tg.happy, 0.55f);
    if (tBlush > 0) {
        tg.blush = 1;
        if (webShame >= 2 || nestShame) { tg.angry = std::max(tg.angry, 0.7f); tg.happy = 0; }
    }
    if (st == St::Stretch) {
        tg.squint = 1;
        tg.happy = 0.3f;
    }
    if (st == St::Clap) tg.happy = 1;
    if (st == St::Climb || (tryMode && dragging)) {
        V2 d = rotv((tryTarget - headC) / (200.f * S()), -rot);
        lk = V2(clampf(d.x, -1, 1), clampf(d.y, -1, 1));
    }
    if (st == St::Hide || st == St::Emerge) lk = V2(lookTurn, -0.1f);
    if (solverT >= 0) {
        if (solverT < kSolverGlitch) {
            tg = FaceState{};
            lk = V2(0.f, 0.f);
        } else {
            float u = solverT - kSolverGlitch;
            tg.happy = tg.squint = tg.heart = tg.angry = 0;
            tg.surprise = u < 0.5f || solverMute ? 1.f : 0.45f;
            tg.sad = 0.7f;
            if (u < kSolverShake) lk = V2(sinf(u * 9.f) * 0.7f, 0.15f);
        }
    }
    extFace(tg, lk);
}

void Pet::lifePose(float dt) {
    auto& P = curInst().pose;
    PartPose dummy;
    auto R = [&](int i) -> PartPose& {
        if (i >= 0 && i < (int)P.size()) return P[i];
        dummy = PartPose{};
        return dummy;
    };
    float S_ = S();
    bEars = approach(bEars, (st == St::Ears && active == 0) ? 1.f : 0.f, 12, dt);
    bStretch = approach(bStretch, (st == St::Stretch && active == 0) ? 1.f : 0.f, 5, dt);
    bLift = approach(bLift, (((st == St::Lift && deskMode != DeskOffer) || st == St::Climb) && active == 0) ? 1.f : 0.f, 9, dt);
    bTug = approach(bTug, (tugT >= 0 && st == St::Idle && active == 0) ? 1.f : 0.f, 14, dt);
    bClap = approach(bClap, (st == St::Clap && active == 0) ? 1.f : 0.f, 12, dt);

    R(iArmL).rot += 2.15f * bEars;
    R(iArmR).rot -= 2.15f * bEars;
    R(iFArmL).rot += 0.75f * bEars;
    R(iFArmR).rot -= 0.75f * bEars;
    R(iHead).rot += 0.06f * sinf(t * 18.f) * bEars;
    if (bEars > 0.3f)
        for (int i : {iArmL, iArmR, iFArmL, iFArmR}) R(i).zOff = 25;

    float up = smooth01(stT / 1.0f) * (1 - smooth01((stT - 2.4f) / 0.8f));
    if (st != St::Stretch) up = 1;
    R(iArmL).rot += 2.35f * bStretch * up;
    R(iArmR).rot -= 2.35f * bStretch * up;
    R(iFArmL).rot -= 0.2f * bStretch * up;
    R(iFArmR).rot += 0.2f * bStretch * up;
    R(iBody).rot += 0.07f * sinf(stT * 2.2f) * bStretch * up;
    R(iBody).scl.y += 0.035f * bStretch * up;
    R(iHead).rot += -0.12f * bStretch * up;
    rootBob -= 4.f * S_ * bStretch * up;
    if (bStretch > 0.3f)
        for (int i : {iArmL, iArmR, iFArmL, iFArmR}) R(i).zOff = 25;

    float cl = st == St::Climb ? sinf(walkPhase) : sinf(t * 5.f) * 0.4f;
    R(iArmL).rot += (2.25f + 0.3f * cl) * bLift;
    R(iArmR).rot -= (2.25f - 0.3f * cl) * bLift;
    R(iFArmL).rot -= 0.25f * bLift;
    R(iFArmR).rot += 0.25f * bLift;
    if (st == St::Lift) rootBob -= fabsf(sinf(t * 6.f)) * 3.f * S_ * bLift;
    if (st == St::Ride && active == 0) {
        R(iLegL).rot += 0.22f * sinf(t * 6.f);
        R(iLegR).rot -= 0.22f * sinf(t * 6.f + 1.3f);
        int grip = shoulderScreen(1).x > shoulderScreen(2).x ? 1 : 2;
        reachTo(rideHand, grip);
        reachTo(rideHand + V2(-headR * 0.5f, headR * 0.35f), 3 - grip);
    }
    if (bTug > 0.01f && iBody >= 0 && iArmL >= 0 && iArmR >= 0 && iFArmL >= 0 && iFArmR >= 0) {
        float j = powf(std::max(0.f, sinf(tugT * 2.2f * 2.f * PI)), 3.f);
        float lean = (0.2f + 0.12f * j) * bTug;
        R(iBody).rot -= lean;
        R(iHead).rot -= 0.08f * j * bTug;
        rootBob -= 1.5f * S_ * j * bTug;
        int lead = (shoulderScreen(1).x - shoulderScreen(2).x) * facing > 0 ? 1 : 2;
        if (iLegL >= 0 && iLegR >= 0) {
            R(lead == 1 ? iLegL : iLegR).rot -= 0.24f * bTug;
            R(lead == 1 ? iLegR : iLegL).rot -= 0.1f * bTug;
        }
        const Rig& R0 = app.rig;
        float L = (R0.parts[iFArmL].joint - R0.parts[iArmL].joint).len() + (R0.parts[iFArmL].hand() - R0.parts[iFArmL].joint).len();
        V2 ls = shoulderScreen(lead), ts = shoulderScreen(3 - lead);
        V2 grip = ls + V2(facing * L * (0.6f - 0.08f * j), L * 0.42f);
        V2 tail = ts + (grip - ts).norm() * std::min((grip - ts).len() - L * 0.12f, L * 0.95f);
        for (int side = 1; side <= 2; side++) {
            int ai = side == 1 ? iArmL : iArmR, fi = side == 1 ? iFArmL : iFArmR;
            PartPose sa = P[ai], sf = P[fi];
            reachTo(side == lead ? grip : tail, side);
            P[ai].rot += lean;
            P[ai].rot = lerpf(sa.rot, P[ai].rot, bTug);
            P[fi].rot = lerpf(sf.rot, P[fi].rot, bTug);
        }
    }
    if (st == St::Climb) {
        R(iLegL).rot += 0.25f * sinf(walkPhase + 1.f);
        R(iLegR).rot -= 0.25f * sinf(walkPhase + 2.f);
    }
    if (bLift > 0.3f)
        for (int i : {iArmL, iArmR, iFArmL, iFArmR}) R(i).zOff = 25;

    if (bClap > 0.01f && active == 0 && iBody >= 0 && iArmL >= 0 && iArmR >= 0 && iFArmL >= 0 && iFArmR >= 0) {
        const Rig& R0 = app.rig;
        float mid = (R0.parts[iArmL].joint.x + R0.parts[iArmR].joint.x) * 0.5f;
        float chestY = R0.parts[iArmL].joint.y - headR * 0.05f;
        float gap = headR * (0.03f + 0.32f * (0.5f + 0.5f * sinf(t * 13.f)));
        const M3& tw = app.inst.world[iBody];
        for (int side = 1; side <= 2; side++) {
            int ai = side == 1 ? iArmL : iArmR, fi = side == 1 ? iFArmL : iFArmR;
            PartPose sa = P[ai], sf = P[fi];
            V2 tgt = xf(tw, V2(mid + (side == 1 ? -gap : gap), chestY)) + app.winOrigin;
            reachTo(tgt, side);
            P[ai].rot = lerpf(sa.rot, P[ai].rot, bClap);
            P[fi].rot = lerpf(sf.rot, P[fi].rot, bClap);
            if (bClap < 0.3f) { P[ai].zOff = sa.zOff; P[fi].zOff = sf.zOff; }
        }
    }
    rootBob -= fabsf(sinf(t * 6.5f)) * 2.5f * S_ * bClap;
    if (solverT >= 0) {
        float k = solverT < kSolverGlitch ? smooth01(solverT / 0.1f) : 0.f;
        R(iBody).off.x += sinf(t * 71.f) * 1.4f * S_ * k;
        R(iHead).off.y += sinf(t * 57.f) * 0.8f * S_ * k;
        R(iHead).rot += sinf(t * 83.f + 1.f) * 0.035f * k;
        float u = solverT - kSolverGlitch;
        if (u > 0.f && u < kSolverShake) {
            float d = 1.f - u / kSolverShake;
            R(iHead).rot += sinf(u * 26.f) * 0.17f * d;
            R(iHead).off.x += sinf(u * 26.f) * 2.f * S_ * d;
        }
    }
    if (st == St::Clap && fmodf(stT, 0.5f) < dt) emit(0, headTop + V2(randf(-0.5f, 0.5f) * headR, 0), V2(randf(-20, 20) * S_, -70.f * S_), 1.1f, 0.8f);
}

float Pet::reachTo(V2 target, int side) {
    const Rig& R0 = app.rig;
    auto& P = curInst().pose;
    if (!side) side = (target - shoulderScreen(1)).len() < (target - shoulderScreen(2)).len() ? 1 : 2;
    int ai = side == 1 ? iArmL : iArmR, fi = side == 1 ? iFArmL : iFArmR;
    if (ai < 0 || fi < 0 || ai >= (int)P.size() || fi >= (int)P.size()) return 1e9f;
    const PartDef& ad = R0.parts[ai];
    const PartDef& fd = R0.parts[fi];
    V2 handPt = fd.hand();
    float L1 = (fd.joint - ad.joint).len(), L2 = (handPt - fd.joint).len();
    if (L1 < 1 || L2 < 1) return 1e9f;
    V2 restU = (fd.joint - ad.joint).norm(), restF = (handPt - fd.joint).norm();
    V2 d = rotv(target - shoulderScreen(side), -rot);
    d.x *= (float)facing;
    float want = d.len();
    float miss = std::max(0.f, want - (L1 + L2) * 0.98f);
    float dist = clampf(want, fabsf(L1 - L2) + 1.f, (L1 + L2) * 0.98f);
    float base = atan2f(d.y, d.x);
    float c = clampf((L1 * L1 + dist * dist - L2 * L2) / (2 * L1 * dist), -1.f, 1.f);
    float a = acosf(c);
    V2 e1 = V2(cosf(base + a), sinf(base + a)) * L1, e2 = V2(cosf(base - a), sinf(base - a)) * L1;
    V2 u = (e1.y > e2.y ? e1 : e2) / L1;
    V2 handT = d.norm() * dist;
    V2 f = (handT - u * L1).norm();
    float armRot = atan2f(cross(restU, u), dot(restU, u));
    V2 restF2 = rotv(restF, armRot);
    float foreRot = atan2f(cross(restF2, f), dot(restF2, f));
    P[ai] = PartPose{};
    P[fi] = PartPose{};
    P[ai].rot = armRot;
    P[fi].rot = foreRot;
    P[ai].zOff = P[fi].zOff = 25;
    return miss;
}
