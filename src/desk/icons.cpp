#include "icons.h"
#include <shlwapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <map>
#include <ctime>
#include <cwctype>

bool DesktopIcons::viewHide(const DIcon& ic, bool hide) {
    if (fake || !fv || !ic.pidl) return false;
    ComPtr<IShellFolderView> sfv;
    HRESULT hr = fv.As(&sfv);
    if (FAILED(hr)) {
        logf("icons: no IShellFolderView (0x%08X)", (unsigned)hr);
        return false;
    }
    UINT idx = 0;
    hr = hide ? sfv->RemoveObject((PUITEMID_CHILD)ic.pidl, &idx) : sfv->AddObject((PUITEMID_CHILD)ic.pidl, &idx);
    logf("icons: %s '%s' -> 0x%08X idx %u", hide ? "RemoveObject" : "AddObject", narrow(ic.name).c_str(), (unsigned)hr, idx);
    return SUCCEEDED(hr);
}

bool DesktopIcons::init() {
    clear();
    fv.Reset();
    sf.Reset();
    lv = nullptr;
    if (testOnly) return false;
    ComPtr<IShellWindows> sw;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&sw)))) {
        logf("icons: no ShellWindows");
        return false;
    }
    VARIANT loc, empty;
    VariantInit(&loc);
    VariantInit(&empty);
    loc.vt = VT_I4;
    loc.lVal = CSIDL_DESKTOP;
    long hwnd = 0;
    ComPtr<IDispatch> disp;
    if (FAILED(sw->FindWindowSW(&loc, &empty, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH, &disp)) || !disp) {
        logf("icons: desktop window not found");
        return false;
    }
    ComPtr<IServiceProvider> sp;
    ComPtr<IShellBrowser> br;
    ComPtr<IShellView> view;
    if (FAILED(disp.As(&sp)) || FAILED(sp->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&br))) ||
        FAILED(br->QueryActiveShellView(&view)) || FAILED(view.As(&fv))) {
        logf("icons: no IFolderView2");
        fv.Reset();
        return false;
    }
    fv->GetFolder(IID_PPV_ARGS(&sf));
    HWND dv = nullptr;
    view->GetWindow(&dv);
    lv = FindWindowExW(dv, nullptr, L"SysListView32", nullptr);
    if (!lv) lv = dv;
    return refresh();
}

void DesktopIcons::clear() {
    for (auto& d : items) CoTaskMemFree(d.pidl);
    items.clear();
}

POINT DesktopIcons::toScreen(POINT p) const {
    if (fake) return p;
    MapWindowPoints(lv, nullptr, &p, 1);
    return p;
}

POINT DesktopIcons::toClient(POINT p) const {
    if (fake) return p;
    MapWindowPoints(nullptr, lv, &p, 1);
    return p;
}

void DesktopIcons::place(DIcon& d) const {
    POINT tl = toScreen(d.pos);
    d.cell = {tl.x, tl.y, tl.x + spacing.x, tl.y + spacing.y};
    d.img = {tl.x + imgOff.x, tl.y + imgOff.y, tl.x + imgOff.x + iconPx, tl.y + imgOff.y + iconPx};
}

RECT DesktopIcons::cellAt(POINT p) const {
    POINT tl = toScreen(p);
    return {tl.x, tl.y, tl.x + spacing.x, tl.y + spacing.y};
}

static std::wstring parentOf(const std::wstring& p) {
    size_t s = p.find_last_of(L"\\/");
    std::wstring d = s == std::wstring::npos ? L"" : p.substr(0, s);
    while (!d.empty() && (d.back() == L'\\' || d.back() == L'/')) d.pop_back();
    return d;
}

static std::wstring baseOf(const std::wstring& p) {
    size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? p : p.substr(s + 1);
}

static bool samePath(const std::wstring& a, const std::wstring& b) { return !a.empty() && _wcsicmp(a.c_str(), b.c_str()) == 0; }

static uint64_t fileIdOf(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 0;
    BY_HANDLE_FILE_INFORMATION fi{};
    uint64_t id = GetFileInformationByHandle(h, &fi) ? ((uint64_t)fi.nFileIndexHigh << 32) | fi.nFileIndexLow : 0;
    CloseHandle(h);
    return id;
}

static std::wstring pathByIdNear(const std::wstring& hint, uint64_t id) {
    if (!id) return {};
    std::wstring dir = parentOf(hint);
    HANDLE hd = CreateFileW(dir.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                            FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (hd == INVALID_HANDLE_VALUE) return {};
    FILE_ID_DESCRIPTOR d{};
    d.dwSize = sizeof(d);
    d.Type = FileIdType;
    d.FileId.QuadPart = (LONGLONG)id;
    HANDLE h = OpenFileById(hd, &d, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, FILE_FLAG_BACKUP_SEMANTICS);
    CloseHandle(hd);
    if (h == INVALID_HANDLE_VALUE) return {};
    wchar_t buf[2048];
    DWORD n = GetFinalPathNameByHandleW(h, buf, 2048, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    CloseHandle(h);
    if (!n || n >= 2048) return {};
    std::wstring p(buf, n);
    if (p.rfind(L"\\\\?\\UNC\\", 0) == 0) p = L"\\\\" + p.substr(8);
    else if (p.rfind(L"\\\\?\\", 0) == 0) p = p.substr(4);
    return p;
}

static DWORD settable(DWORD a) {
    a &= FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED |
         FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_TEMPORARY | 0x80000   | 0x100000  ;
    return a ? a : FILE_ATTRIBUTE_NORMAL;
}

bool DesktopIcons::refresh() {
    if (fake) {
        if (fakeDead) {
            lost = true;
            return false;
        }
        std::vector<std::pair<std::wstring, bool>> inDir;
        {
            WIN32_FIND_DATAW wf;
            HANDLE h = FindFirstFileW((fakeDir + L"\\*").c_str(), &wf);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (!wcscmp(wf.cFileName, L".") || !wcscmp(wf.cFileName, L"..")) continue;
                    if (!fakeShowHidden && (wf.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) continue;
                    inDir.push_back({fakeDir + L"\\" + wf.cFileName, (wf.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0});
                } while (FindNextFileW(h, &wf));
                FindClose(h);
            }
        }
        auto listed = [&](const std::wstring& p) {
            for (auto& e : inDir)
                if (samePath(e.first, p)) return true;
            return false;
        };
        std::erase_if(items, [&](const DIcon& d) { return !d.path.empty() && !listed(d.path); });
        std::erase_if(fakeNew, [&](const std::pair<std::wstring, int>& n) { return !listed(n.first); });
        for (auto& [path, folder] : inDir) {
            if (findPath(path) >= 0) continue;
            auto it = std::find_if(fakeNew.begin(), fakeNew.end(), [&](const std::pair<std::wstring, int>& n) { return samePath(n.first, path); });
            if (it == fakeNew.end()) {
                fakeNew.push_back({path, fakeLag});
                continue;
            }
            if (it->second-- > 0) continue;
            fakeNew.erase(it);
            POINT slot{-1, -1};
            auto ws = workAreas();
            if (!ws.empty() && spacing.x > 0 && spacing.y > 0) {
                POINT o = gridOrigin(), c0 = toClient(POINT{ws[0].left, ws[0].top}), c1 = toClient(POINT{ws[0].right, ws[0].bottom});
                for (LONG x = o.x + (LONG)floor((c0.x - o.x) / (double)spacing.x) * spacing.x; x <= c1.x && slot.x < 0; x += spacing.x)
                    for (LONG y = o.y + (LONG)floor((c0.y - o.y) / (double)spacing.y) * spacing.y; y <= c1.y && slot.x < 0; y += spacing.y)
                        if (cellOnScreen(POINT{x, y}) && cellFree(POINT{x, y}, L"")) slot = {x, y};
            }
            DIcon d;
            d.path = path;
            d.name = baseOf(path);
            d.folder = folder;
            d.pos = slot.x >= 0 ? slot : POINT{0, 0};
            items.push_back(d);
        }
        for (auto& d : items) place(d);
        refreshedAt = nowSec();
        return true;
    }
    if (!fv) return false;
    clear();
    if ((lv && !IsWindow(lv)) || FAILED(fv->GetSpacing(&spacing))) {
        lost = true;
        return false;
    }
    FOLDERVIEWMODE mode;
    int sz = 48;
    fv->GetViewModeAndIconSize(&mode, &sz);
    UINT dpi = GetDpiForWindow(lv);
    iconPx = MulDiv(sz, dpi ? dpi : 96, 96);
    imgOff = {(spacing.x - iconPx) / 2, MulDiv(2, dpi ? dpi : 96, 96)};
    ComPtr<IEnumIDList> en;
    if (FAILED(fv->Items(SVGIO_ALLVIEW, IID_PPV_ARGS(&en))) || !en) {
        lost = true;
        return false;
    }
    if (!dsk) SHGetDesktopFolder(&dsk);
    IShellFolder* names = viaView || !dsk ? sf.Get() : dsk.Get();
    PITEMID_CHILD batch[64];
    ULONG got = 0;
    while (SUCCEEDED(en->Next(64, batch, &got)) && got > 0) {
        for (ULONG k = 0; k < got; k++) {
            PITEMID_CHILD p = batch[k];
            DIcon d;
            d.pidl = p;
            if (FAILED(fv->GetItemPosition(p, &d.pos))) { CoTaskMemFree(p); continue; }
            STRRET sr;
            if (names && SUCCEEDED(names->GetDisplayNameOf(p, SHGDN_NORMAL, &sr))) {
                wchar_t* nm = nullptr;
                if (SUCCEEDED(StrRetToStrW(&sr, p, &nm))) {
                    d.name = nm;
                    CoTaskMemFree(nm);
                }
            }
            if (names) {
                SFGAOF at = SFGAO_FOLDER | SFGAO_FILESYSTEM;
                PCUITEMID_CHILD one = p;
                if (SUCCEEDED(names->GetAttributesOf(1, &one, &at))) d.folder = (at & SFGAO_FOLDER) && (at & SFGAO_FILESYSTEM);
                STRRET pr;
                if (SUCCEEDED(names->GetDisplayNameOf(p, SHGDN_FORPARSING, &pr))) {
                    wchar_t* nm = nullptr;
                    if (SUCCEEDED(StrRetToStrW(&pr, p, &nm))) {
                        d.path = nm;
                        CoTaskMemFree(nm);
                    }
                }
                if (d.path.size() < 3 || d.path[1] != L':') d.folder = false;
            }
            place(d);
            items.push_back(std::move(d));
        }
        got = 0;
    }
    refreshedAt = nowSec();
    return true;
}

int DesktopIcons::find(const std::wstring& name) const {
    for (size_t i = 0; i < items.size(); i++)
        if (items[i].name == name) return (int)i;
    return -1;
}

int DesktopIcons::findPath(const std::wstring& path) const {
    if (path.empty()) return -1;
    for (size_t i = 0; i < items.size(); i++)
        if (samePath(items[i].path, path)) return (int)i;
    return -1;
}

static std::wstring knownDir(REFKNOWNFOLDERID id) {
    PWSTR s = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &s)) && s) out = s;
    CoTaskMemFree(s);
    while (!out.empty() && (out.back() == L'\\' || out.back() == L'/')) out.pop_back();
    return out;
}

bool DesktopIcons::isDesktopFile(const std::wstring& path) const {
    std::wstring dir = parentOf(path);
    if (dir.empty()) return false;
    if (fake) return _wcsicmp(dir.c_str(), parentOf(fakeDir + L"\\x").c_str()) == 0;
    if (testOnly) return false;
    for (auto id : {FOLDERID_Desktop, FOLDERID_PublicDesktop}) {
        std::wstring d = knownDir(id);
        if (!d.empty() && _wcsicmp(dir.c_str(), d.c_str()) == 0) return true;
    }
    return false;
}

void DesktopIcons::setFake(const std::wstring& dir, POINT sp, int px) {
    clear();
    fv.Reset();
    sf.Reset();
    fake = true;
    fakeSnap = true;
    fakeAuto = false;
    fakeDead = lost = false;
    fakeShowHidden = fakeNoReturn = fakeOtherVolume = false;
    fakeDir = dir;
    fakeNew.clear();
    pend.clear();
    gridKnown = false;
    spacing = sp;
    iconPx = px;
    imgOff = {(sp.x - px) / 2, 2};
}

void DesktopIcons::addFake(const std::wstring& name, POINT pos, bool folder) {
    DIcon d;
    d.name = name;
    d.path = fakeDir + L"\\" + name;
    if (GetFileAttributesW(d.path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (folder) CreateDirectoryW(d.path.c_str(), nullptr);
        else writeFile(d.path, "test\r\n");
    }
    d.folder = folder;
    d.pos = pos;
    place(d);
    items.push_back(std::move(d));
}

bool DesktopIcons::fakeMove(const std::wstring& name, POINT pos) {
    int i = find(name);
    if (!fake || i < 0) return false;
    items[i].pos = pos;
    place(items[i]);
    return true;
}

void DesktopIcons::fakeForget(const std::wstring& path) {
    if (!fake) return;
    int i = findPath(path);
    if (i >= 0) items.erase(items.begin() + i);
    std::erase_if(fakeNew, [&](const std::pair<std::wstring, int>& n) { return samePath(n.first, path); });
    cancelPut(path);
}

std::vector<RECT> DesktopIcons::workAreas() const {
    std::vector<RECT> w = worksFn ? worksFn() : std::vector<RECT>{};
    if (w.empty() && fake) w = fakeWorks;
    if (w.empty() && !fake && !testOnly)
        EnumDisplayMonitors(
            nullptr, nullptr,
            [](HMONITOR hm, HDC, LPRECT, LPARAM lp) -> BOOL {
                MONITORINFO mi{sizeof(mi)};
                if (GetMonitorInfoW(hm, &mi)) ((std::vector<RECT>*)lp)->push_back(mi.rcWork);
                return TRUE;
            },
            (LPARAM)&w);
    return w;
}

static LONG modp(LONG v, LONG s) { return ((v % s) + s) % s; }

POINT DesktopIcons::gridOrigin() const {
    if (spacing.x <= 0 || spacing.y <= 0) return gridO;
    std::map<std::pair<LONG, LONG>, int> n;
    for (auto& d : items) n[{modp(d.pos.x, spacing.x), modp(d.pos.y, spacing.y)}]++;
    int top = 0;
    for (auto& [k, c] : n)
        if (c > top) {
            top = c;
            gridO = {k.first, k.second};
            gridKnown = true;
        }
    return gridO;
}

POINT DesktopIcons::snapCell(POINT p) const {
    if (spacing.x <= 0 || spacing.y <= 0) return p;
    POINT o = gridOrigin();
    return {o.x + (LONG)lround((p.x - o.x) / (double)spacing.x) * spacing.x, o.y + (LONG)lround((p.y - o.y) / (double)spacing.y) * spacing.y};
}

POINT DesktopIcons::cellUnder(POINT screenPt) const {
    POINT c = toClient(screenPt);
    if (spacing.x <= 0 || spacing.y <= 0) return c;
    POINT o = gridOrigin();
    return {o.x + (LONG)floor((c.x - o.x) / (double)spacing.x) * spacing.x, o.y + (LONG)floor((c.y - o.y) / (double)spacing.y) * spacing.y};
}

bool DesktopIcons::onGrid(POINT p) const {
    if (spacing.x <= 0 || spacing.y <= 0) return false;
    POINT o = gridOrigin();
    return modp(p.x - o.x, spacing.x) == 0 && modp(p.y - o.y, spacing.y) == 0;
}

bool DesktopIcons::cellOnScreen(POINT p) const {
    if (spacing.x <= 0 || spacing.y <= 0) return false;
    RECT c = cellAt(p);
    RECT im{c.left + imgOff.x, c.top + imgOff.y, c.left + imgOff.x + iconPx, c.top + imgOff.y + iconPx};
    POINT mid{(c.left + c.right) / 2, (c.top + c.bottom) / 2};
    for (auto& w : workAreas())
        if (im.left >= w.left && im.top >= w.top && im.right <= w.right && im.bottom <= w.bottom && PtInRect(&w, mid)) return true;
    return false;
}

static bool cellsMeet(const RECT& a0, const RECT& b0) {
    RECT a{a0.left + 2, a0.top + 2, a0.right - 2, a0.bottom - 2}, b{b0.left + 2, b0.top + 2, b0.right - 2, b0.bottom - 2}, x;
    return IntersectRect(&x, &a, &b) != 0;
}

bool DesktopIcons::cellFree(POINT p, const std::wstring& selfPath) const {
    RECT c = cellAt(p);
    for (auto& d : items) {
        if (!selfPath.empty() && (samePath(d.path, selfPath) || d.name == selfPath)) continue;
        if (cellsMeet(c, d.cell)) return false;
    }
    for (auto& q : pend)
        if (!samePath(q.path, selfPath) && cellsMeet(c, cellAt(q.cell))) return false;
    return true;
}

bool DesktopIcons::searchNear(V2 at, const std::function<bool(POINT)>& good, POINT& out) const {
    auto ws = workAreas();
    if (ws.empty() || spacing.x <= 0 || spacing.y <= 0) return false;
    int mon = -1;
    float md = 1e30f;
    for (size_t m = 0; m < ws.size(); m++) {
        const RECT& w = ws[m];
        float dx = std::max({(float)w.left - at.x, 0.f, at.x - (float)w.right}), dy = std::max({(float)w.top - at.y, 0.f, at.y - (float)w.bottom});
        if (dx * dx + dy * dy < md) {
            md = dx * dx + dy * dy;
            mon = (int)m;
        }
    }
    POINT o = gridOrigin();
    float best = 1e30f;
    bool found = false;
    for (size_t m = 0; m < ws.size(); m++) {
        POINT c0 = toClient(POINT{ws[m].left, ws[m].top}), c1 = toClient(POINT{ws[m].right, ws[m].bottom});
        LONG kx0 = (LONG)floor((c0.x - o.x) / (double)spacing.x), kx1 = (LONG)ceil((c1.x - o.x) / (double)spacing.x);
        LONG ky0 = (LONG)floor((c0.y - o.y) / (double)spacing.y), ky1 = (LONG)ceil((c1.y - o.y) / (double)spacing.y);
        for (LONG kx = kx0; kx <= kx1; kx++)
            for (LONG ky = ky0; ky <= ky1; ky++) {
                POINT p{o.x + kx * spacing.x, o.y + ky * spacing.y};
                if (!cellOnScreen(p) || !good(p)) continue;
                RECT r = cellAt(p);
                V2 im((r.left + r.right) * 0.5f, r.top + imgOff.y + iconPx * 0.5f);
                float d = (im - at).len() + ((int)m != mon ? 1e5f : 0.f);
                if (d < best) {
                    best = d;
                    out = p;
                    found = true;
                }
            }
    }
    return found;
}

bool DesktopIcons::nearestFree(V2 at, const std::wstring& selfPath, POINT& out) const {
    return searchNear(at, [&](POINT p) { return cellFree(p, selfPath); }, out);
}

bool DesktopIcons::resolveCell(POINT want, const std::wstring& selfPath, POINT& out) const {
    if (spacing.x <= 0 || spacing.y <= 0) return false;
    POINT s = snapCell(want);
    if (cellOnScreen(s) && cellFree(s, selfPath)) {
        out = s;
        return true;
    }
    RECT r = cellAt(s);
    return nearestFree(V2((r.left + r.right) * 0.5f, r.top + imgOff.y + iconPx * 0.5f), selfPath, out);
}

bool DesktopIcons::cellOk(POINT p, const char* why) {
    bool grid = onGrid(p), shown = cellOnScreen(p);
    if (grid && shown) return true;
    badMoves++;
    logf("icons: BLOCKED %s to %ld,%ld: %s%s", why, p.x, p.y, grid ? "" : "off the grid ", shown ? "" : "off every monitor's work area");
    return false;
}

bool DesktopIcons::guardSelfTest() {
    int b = badMoves;
    POINT o = gridOrigin();
    bool off = !cellOk(POINT{o.x + spacing.x * 400, o.y + spacing.y * 400}, "self-test (off screen)");
    bool grid = !cellOk(POINT{o.x + spacing.x + 7, o.y + spacing.y + 3}, "self-test (off grid)");
    bool fine = cellOk(POINT{o.x + spacing.x, o.y + spacing.y}, "self-test (good cell)");
    badMoves = b;
    return off && grid && fine;
}

bool DesktopIcons::rawMove(const DIcon& ic, POINT p) {
    if (!cellOk(p, "move")) return false;
    if (fake) {
        if (fakeDead) return false;
        for (auto& d : items)
            if (&d == &ic || (d.path == ic.path && d.name == ic.name)) {
                d.pos = p;
                place(d);
                return true;
            }
        return false;
    }
    if (!fv || testOnly) return false;
    PCUITEMID_CHILD arr[1] = {ic.pidl};
    return SUCCEEDED(fv->SelectAndPositionItems(1, arr, &p, SVSI_POSITIONITEM));
}

bool DesktopIcons::rawMany(const std::vector<std::pair<const DIcon*, POINT>>& m) {
    std::vector<std::pair<const DIcon*, POINT>> okm;
    for (auto& e : m)
        if (e.first && cellOk(e.second, "batch move")) okm.push_back(e);
    if (okm.empty()) return false;
    if (fake) {
        if (fakeDead) return false;
        for (auto& [ic, p] : okm)
            for (auto& d : items)
                if (&d == ic) {
                    d.pos = p;
                    place(d);
                }
        return true;
    }
    if (!fv || testOnly) return false;
    std::vector<PCUITEMID_CHILD> ids;
    std::vector<POINT> pts;
    for (auto& [ic, p] : okm)
        if (ic->pidl) {
            ids.push_back(ic->pidl);
            pts.push_back(p);
        }
    return !ids.empty() && SUCCEEDED(fv->SelectAndPositionItems((UINT)ids.size(), ids.data(), pts.data(), SVSI_POSITIONITEM));
}

bool DesktopIcons::put(const DIcon& ic, POINT want, const char* why, POINT* got) {
    POINT to;
    std::wstring self = ic.path.empty() ? ic.name : ic.path;
    if (!resolveCell(want, self, to)) {
        logf("icons: put '%s' (%s): no free cell on any monitor near %ld,%ld, left where it is", narrow(ic.name).c_str(), why, want.x, want.y);
        return false;
    }
    bool ok = rawMove(ic, to);
    putCount++;
    logf("icons: put '%s' at %ld,%ld (asked %ld,%ld, %s): %d", narrow(ic.name).c_str(), to.x, to.y, want.x, want.y, why, (int)ok);
    if (got) *got = to;
    return ok;
}

int DesktopIcons::putMany(const std::vector<std::pair<const DIcon*, POINT>>& m, const char* why) {
    std::vector<std::pair<const DIcon*, POINT>> out;
    std::vector<RECT> taken;
    auto good = [&](POINT p) {
        RECT c = cellAt(p);
        for (auto& r : taken)
            if (cellsMeet(c, r)) return false;
        for (auto& d : items) {
            bool mine = false;
            for (auto& e : m) mine |= e.first == &d;
            if (!mine && cellsMeet(c, d.cell)) return false;
        }
        return true;
    };
    int skipped = 0;
    std::vector<size_t> later;
    for (size_t k = 0; k < m.size(); k++) {
        if (!m[k].first) continue;
        POINT s = snapCell(m[k].second);
        if (cellOnScreen(s) && good(s)) {
            out.push_back({m[k].first, s});
            taken.push_back(cellAt(s));
        } else {
            later.push_back(k);
        }
    }
    std::vector<RECT> wanted;
    for (auto& e : m) wanted.push_back(cellAt(snapCell(e.second)));
    auto spare = [&](POINT p) {
        RECT c = cellAt(p);
        for (auto& r : wanted)
            if (cellsMeet(c, r)) return false;
        return good(p);
    };
    for (size_t k : later) {
        const DIcon* ic = m[k].first;
        POINT s = snapCell(m[k].second);
        RECT r = cellAt(s);
        V2 at((r.left + r.right) * 0.5f, r.top + imgOff.y + iconPx * 0.5f);
        if (!searchNear(at, spare, s) && !searchNear(at, good, s)) {
            skipped++;
            logf("icons: put '%s' (%s): no free cell near %ld,%ld, left where it is", narrow(ic->name).c_str(), why, m[k].second.x, m[k].second.y);
            continue;
        }
        out.push_back({ic, s});
        taken.push_back(cellAt(s));
    }
    bool ok = rawMany(out);
    putCount += (int)out.size();
    logf("icons: put %zu of %zu icons in one call (%s), %d without a free cell: %d", out.size(), m.size(), why, skipped, (int)ok);
    return ok ? (int)out.size() : 0;
}

bool DesktopIcons::deselect(const DIcon& ic) {
    if (fake) {
        logf("icons: deselect '%s' (snapshot: no shell call)", narrow(ic.name).c_str());
        return !fakeDead;
    }
    if (!fv || !ic.pidl || testOnly) return false;
    PCUITEMID_CHILD arr[1] = {ic.pidl};
    return SUCCEEDED(fv->SelectAndPositionItems(1, arr, nullptr, SVSI_DESELECT));
}

bool DesktopIcons::itemPos(const DIcon& ic, POINT& out) {
    if (fake) {
        if (fakeDead) return false;
        out = ic.pos;
        return true;
    }
    if (!fv || !ic.pidl) return false;
    return SUCCEEDED(fv->GetItemPosition(ic.pidl, &out));
}

bool DesktopIcons::refreshIfOlder(double secs) {
    if (!fake && nowSec() - refreshedAt < secs) return true;
    return refresh();
}

POINT DesktopIcons::clientForImgCenter(V2 c) const {
    POINT tl{(LONG)lroundf(c.x - iconPx * 0.5f) - imgOff.x, (LONG)lroundf(c.y - iconPx * 0.5f) - imgOff.y};
    return toClient(tl);
}

ComPtr<IWICBitmapSource> DesktopIcons::image(Gfx& g, const DIcon& ic, int px) {
    if (!sf || fake) return nullptr;
    ComPtr<IShellItem> it;
    if (FAILED(SHCreateItemWithParent(nullptr, sf.Get(), ic.pidl, IID_PPV_ARGS(&it)))) return nullptr;
    ComPtr<IShellItemImageFactory> f;
    if (FAILED(it.As(&f))) return nullptr;
    HBITMAP hb = nullptr;
    if (FAILED(f->GetImage({px, px}, SIIGBF_RESIZETOFIT, &hb)) || !hb) return nullptr;
    auto src = g.fromHBitmap(hb);
    DeleteObject(hb);
    return src;
}

bool DesktopIcons::snapOn() {
    if (fake) return fakeSnap;
    FOLDERFLAGS fl = (FOLDERFLAGS)0;
    if (!fv || FAILED(fv->GetCurrentFolderFlags((DWORD*)&fl))) return false;
    return (fl & FWF_SNAPTOGRID) != 0;
}

bool DesktopIcons::autoArrange() {
    if (fake) return fakeAuto;
    FOLDERFLAGS fl = (FOLDERFLAGS)0;
    if (!fv || FAILED(fv->GetCurrentFolderFlags((DWORD*)&fl))) return false;
    return (fl & FWF_AUTOARRANGE) != 0;
}

static std::wstring snapFlagPath() { return appDataDir() + L"\\snap_off.flag"; }

void DesktopIcons::recoverSnap() {
    if (GetFileAttributesW(snapFlagPath().c_str()) == INVALID_FILE_ATTRIBUTES) return;
    if (snapOn()) {
        DeleteFileW(snapFlagPath().c_str());
        return;
    }
    logf("icons: an older version left grid snapping off, turning it back on once");
    bool ok = false;
    if (fake) {
        fakeSnap = ok = true;
    } else if (fv && !testOnly) {
        ok = SUCCEEDED(fv->SetCurrentFolderFlags(FWF_SNAPTOGRID, FWF_SNAPTOGRID));
    }
    if (ok) DeleteFileW(snapFlagPath().c_str());
}

static std::wstring carryPath() { return appDataDir() + L"\\desk_carry.txt"; }

static bool inside(const std::wstring& p, const std::wstring& dir) {
    return !dir.empty() && p.size() > dir.size() + 1 && _wcsnicmp(p.c_str(), (dir + L"\\").c_str(), dir.size() + 1) == 0;
}

static std::vector<HandsRec> readRecords() {
    std::vector<HandsRec> out;
    std::string txt;
    if (!readFile(carryPath(), txt)) return out;
    for (size_t at = 0; at < txt.size();) {
        size_t nl = txt.find('\n', at);
        std::string line = txt.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        at = nl == std::string::npos ? txt.size() : nl + 1;
        while (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> f;
        for (size_t a = 0;;) {
            size_t t = line.find('\t', a);
            f.push_back(line.substr(a, t == std::string::npos ? std::string::npos : t - a));
            if (t == std::string::npos) break;
            a = t + 1;
        }
        if (f.size() < 3 || f[0].empty()) continue;
        HandsRec r;
        r.path = widen(f[0]);
        r.home = {atol(f[1].c_str()), atol(f[2].c_str())};
        r.ownHome = f.size() >= 4 && f[3] == "1";
        if (f.size() >= 5) r.name = widen(f[4]);
        r.attrs = f.size() >= 6 && !f[5].empty() && f[5] != "-" ? (DWORD)strtoul(f[5].c_str(), nullptr, 16) : INVALID_FILE_ATTRIBUTES;
        r.id = f.size() >= 7 ? _strtoui64(f[6].c_str(), nullptr, 10) : 0;
        if (f.size() >= 8) r.hands = widen(f[7]);
        if (f.size() >= 9) r.kind = atoi(f[8].c_str());
        if (f.size() >= 10) r.at = _atoi64(f[9].c_str());
        out.push_back(r);
    }
    return out;
}

static bool writeRecords(const std::vector<HandsRec>& rs) {
    if (rs.empty()) return DeleteFileW(carryPath().c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
    std::string txt;
    for (auto& r : rs) {
        char mid[96], attrs[32], tail[64], kind[64];
        snprintf(mid, sizeof(mid), "\t%ld\t%ld\t%d\t", r.home.x, r.home.y, (int)r.ownHome);
        if (r.attrs == INVALID_FILE_ATTRIBUTES) snprintf(attrs, sizeof(attrs), "-");
        else snprintf(attrs, sizeof(attrs), "%lX", (unsigned long)r.attrs);
        snprintf(tail, sizeof(tail), "\t%s\t%llu\t", attrs, (unsigned long long)r.id);
        snprintf(kind, sizeof(kind), "\t%d\t%lld", r.kind, r.at);
        txt += narrow(r.path) + mid + narrow(r.name) + tail + narrow(r.hands) + kind + "\n";
    }
    std::wstring tmp = carryPath() + L".new";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    bool ok = h != INVALID_HANDLE_VALUE;
    if (ok) {
        DWORD wr = 0;
        ok = WriteFile(h, txt.data(), (DWORD)txt.size(), &wr, nullptr) && wr == txt.size() && FlushFileBuffers(h);
        CloseHandle(h);
    }
    ok = ok && MoveFileExW(tmp.c_str(), carryPath().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) {
        logf("icons: could not write the restore record (error %lu)", GetLastError());
        DeleteFileW(tmp.c_str());
    }
    return ok;
}

static bool sameRec(const HandsRec& r, const std::wstring& key) { return samePath(r.path, key) || samePath(r.hands, key); }

static bool saveRecord(const HandsRec& r, const std::wstring& key) {
    auto rs = readRecords();
    std::erase_if(rs, [&](const HandsRec& o) { return sameRec(o, key) || sameRec(o, r.path); });
    rs.push_back(r);
    return writeRecords(rs);
}

bool DesktopIcons::inHands(const std::wstring& path) const {
    for (auto& o : readRecords())
        if (samePath(o.path, path) && !o.hands.empty() && GetFileAttributesW(o.hands.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    return false;
}

void DesktopIcons::dropRecord(const std::wstring& path, bool onDeskOnly) {
    auto rs = readRecords();
    size_t n = rs.size();
    std::erase_if(rs, [&](const HandsRec& r) { return sameRec(r, path) && !(onDeskOnly && !r.hands.empty()); });
    if (rs.size() != n) writeRecords(rs);
}

static std::wstring freeName(const std::wstring& want, bool dir) {
    if (GetFileAttributesW(want.c_str()) == INVALID_FILE_ATTRIBUTES) return want;
    std::wstring folder = parentOf(want), base = baseOf(want), ext;
    size_t dot = base.find_last_of(L'.');
    if (!dir && dot != std::wstring::npos && dot > 0) {
        ext = base.substr(dot);
        base = base.substr(0, dot);
    }
    for (int k = 2; k < 1000; k++) {
        std::wstring p = folder + L"\\" + base + L" (" + std::to_wstring(k) + L")" + ext;
        if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) return p;
    }
    return {};
}

static std::wstring handsName(const std::wstring& dir, const std::wstring& base) {
    SYSTEMTIME t{};
    GetLocalTime(&t);
    wchar_t st[40];
    swprintf(st, 40, L"%04d%02d%02d-%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    for (int k = 0; k < 1000; k++) {
        std::wstring p = dir + L"\\" + st + (k ? L"-" + std::to_wstring(k) : L"") + L"_" + base;
        if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) return p;
    }
    return {};
}

static std::wstring volumeOf(const std::wstring& p) {
    wchar_t v[MAX_PATH] = {};
    if (!GetVolumePathNameW(p.c_str(), v, MAX_PATH)) return {};
    return v;
}

static bool inOneDrive(const std::wstring& dir) {
    std::vector<std::wstring> roots;
    for (const wchar_t* v : {L"OneDrive", L"OneDriveConsumer", L"OneDriveCommercial"}) {
        wchar_t b[MAX_PATH];
        DWORD n = GetEnvironmentVariableW(v, b, MAX_PATH);
        if (n && n < MAX_PATH) roots.push_back(b);
    }
    for (const wchar_t* k : {L"Software\\Microsoft\\OneDrive\\Accounts\\Personal", L"Software\\Microsoft\\OneDrive\\Accounts\\Business1"}) {
        wchar_t b[MAX_PATH];
        DWORD sz = sizeof(b);
        if (RegGetValueW(HKEY_CURRENT_USER, k, L"UserFolder", RRF_RT_REG_SZ, nullptr, b, &sz) == ERROR_SUCCESS) roots.push_back(b);
    }
    for (auto r : roots) {
        while (!r.empty() && (r.back() == L'\\' || r.back() == L'/')) r.pop_back();
        if (r.size() >= 3 && (samePath(dir, r) || inside(dir, r))) return true;
    }
    return false;
}

std::wstring DesktopIcons::handsDir() const { return fake && !fakeHands.empty() ? fakeHands : appDataDir() + L"\\hands"; }

bool DesktopIcons::canTake(const std::wstring& path, std::string* why) const {
    auto no = [&](const char* w) {
        if (why) *why = w;
        return false;
    };
    if (path.size() < 3 || path[1] != L':') return no("not a file on a disk");
    if (testOnly && !fake) return no("test run, real files are off limits");
    if (!isDesktopFile(path)) return no("not in a desktop folder");
    DWORD a = GetFileAttributesW(path.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return no("no such file");
    if (a & FILE_ATTRIBUTE_HIDDEN) return no("hidden file");
    if (a & FILE_ATTRIBUTE_SYSTEM) return no("system item");
    if (a & FILE_ATTRIBUTE_REPARSE_POINT) return no("link or cloud placeholder");
    std::wstring nm = baseOf(path);
    if (nm.rfind(L"Узи Дорман", 0) == 0 || (nm.size() > 6 && !_wcsicmp(nm.c_str() + nm.size() - 6, L".drone"))) return no("her own body");
    if (!fake && inOneDrive(parentOf(path))) return no("the desktop is in OneDrive");
    std::wstring hd = handsDir();
    CreateDirectoryW(hd.c_str(), nullptr);
    std::wstring v1 = volumeOf(path), v2 = volumeOf(hd);
    if ((fake && fakeOtherVolume) || v1.empty() || !samePath(v1, v2)) return no("her hands folder is on another disk");
    return true;
}

bool DesktopIcons::take(HandsRec& r) {
    takeErr = 0;
    takeRights = false;
    std::string why;
    if (!canTake(r.path, &why)) {
        logf("icons: not taking '%s' into her hands: %s", narrow(r.path).c_str(), why.c_str());
        return false;
    }
    bool had = false;
    for (auto& o : readRecords()) {
        if (samePath(o.path, r.path) && !o.hands.empty() && GetFileAttributesW(o.hands.c_str()) != INVALID_FILE_ATTRIBUTES) {
            logf("icons: not taking '%s': a file with this path is still in her hands ('%s')", narrow(r.path).c_str(), narrow(baseOf(o.hands)).c_str());
            return false;
        }
        had |= samePath(o.path, r.path);
    }
    DWORD a = GetFileAttributesW(r.path.c_str());
    std::wstring ext = r.path.size() > 4 ? r.path.substr(r.path.size() - 4) : L"";
    r.kind = (a & FILE_ATTRIBUTE_DIRECTORY) ? 1 : (!_wcsicmp(ext.c_str(), L".lnk") || !_wcsicmp(ext.c_str(), L".url")) ? 2 : 0;
    r.id = fileIdOf(r.path);
    r.at = (long long)_time64(nullptr);
    r.attrs = INVALID_FILE_ATTRIBUTES;
    r.hands = handsName(handsDir(), baseOf(r.path));
    if (r.hands.empty() || !saveRecord(r, r.path)) {
        r.hands.clear();
        logf("icons: no restore record for '%s', not taking it into her hands", narrow(r.path).c_str());
        return false;
    }
    DWORD e = 0;
    for (int k = 0; k < 3; k++) {
        if (MoveFileExW(r.path.c_str(), r.hands.c_str(), MOVEFILE_WRITE_THROUGH)) {
            e = 0;
            break;
        }
        e = GetLastError();
        if (e != ERROR_SHARING_VIOLATION) break;
        Sleep(30);
    }
    if (e == ERROR_ACCESS_DENIED) {
        HANDLE h = CreateFileW(r.path.c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        DWORD pe = h == INVALID_HANDLE_VALUE ? GetLastError() : 0;
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        takeRights = pe == ERROR_ACCESS_DENIED;
        if (pe == ERROR_SHARING_VIOLATION) e = pe;
        logf("icons: '%s' access denied, delete probe %s", narrow(r.path).c_str(),
             takeRights ? "denied too: no rights" : pe ? "busy" : "ok: something is open inside");
    }
    if (e) {
        takeErr = e;
        logf("icons: not taking '%s' into her hands: %s (error %lu), it stays on the desktop", narrow(r.path).c_str(),
             e == ERROR_SHARING_VIOLATION ? "the file is in use" : e == ERROR_ACCESS_DENIED ? "no access or in use" : "move failed", e);
        std::wstring hp = r.hands;
        r.hands.clear();
        if (had) saveRecord(r, hp);
        else dropRecord(hp);
        return false;
    }
    cancelPut(r.path);
    logf("icons: '%s' is in her hands now ('%s'), its cell is free", narrow(r.path).c_str(), narrow(baseOf(r.hands)).c_str());
    return true;
}

std::wstring DesktopIcons::pathById(const HandsRec& r) const { return pathByIdNear(r.path, r.id); }

bool DesktopIcons::giveBack(HandsRec& r, std::wstring* nowPath) {
    if (nowPath) nowPath->clear();
    if (r.path.empty()) return true;
    if (testOnly && !fake) return false;
    if (r.hands.empty()) {
        std::wstring p = r.path;
        DWORD a = GetFileAttributesW(p.c_str());
        if (a == INVALID_FILE_ATTRIBUTES) {
            p = pathById(r);
            a = p.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(p.c_str());
        }
        if (a == INVALID_FILE_ATTRIBUTES) {
            logf("icons: '%s' is gone, nothing to give back", narrow(r.path).c_str());
            return true;
        }
        if (r.attrs != INVALID_FILE_ATTRIBUTES && !(r.attrs & FILE_ATTRIBUTE_HIDDEN) && (a & FILE_ATTRIBUTE_HIDDEN)) {
            if (fake && !inside(p, fakeDir)) {
                badMoves++;
                logf("icons: BLOCKED unhide of '%s' outside the fake desktop", narrow(p).c_str());
                return false;
            }
            if (!SetFileAttributesW(p.c_str(), settable(a & ~FILE_ATTRIBUTE_HIDDEN))) {
                logf("icons: '%s' was hidden by 0.13.6 and could not be shown again (error %lu), the record stays", narrow(p).c_str(), GetLastError());
                return false;
            }
            logf("icons: '%s' was hidden by 0.13.6, visible again (only our bit cleared)", narrow(p).c_str());
        }
        if (nowPath) *nowPath = p;
        return true;
    }
    std::wstring from = r.hands;
    DWORD ha = GetFileAttributesW(from.c_str());
    if (ha == INVALID_FILE_ATTRIBUTES) {
        r.hands.clear();
        if (GetFileAttributesW(r.path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            logf("icons: '%s' is neither in her hands nor on the desktop, nothing to give back", narrow(r.path).c_str());
            return true;
        }
        logf("icons: '%s' is already back on the desktop", narrow(r.path).c_str());
        saveRecord(r, from);
        if (nowPath) *nowPath = r.path;
        return true;
    }
    std::wstring desk = fake ? fakeDir : knownDir(FOLDERID_Desktop), want = r.path;
    DWORD pa = GetFileAttributesW(parentOf(r.path).c_str());
    bool homeGone = pa == INVALID_FILE_ATTRIBUTES || !(pa & FILE_ATTRIBUTE_DIRECTORY) || !isDesktopFile(r.path);
    if (homeGone && !desk.empty()) {
        want = desk + L"\\" + baseOf(r.path);
        logf("icons: '%s' was taken from a folder that is not the desktop any more, giving it back to '%s'", narrow(r.path).c_str(), narrow(desk).c_str());
    }
    if (fake && (!inside(from, handsDir()) || !samePath(parentOf(want), fakeDir))) {
        badMoves++;
        logf("icons: BLOCKED giving '%s' back outside the fake desktop", narrow(want).c_str());
        return false;
    }
    if (fake && fakeNoReturn) {
        logf("icons: could not give '%s' back (test: access denied), it stays in her hands, the record stays", narrow(r.path).c_str());
        return false;
    }
    DWORD wa = GetFileAttributesW(want.c_str());
    if (wa != INVALID_FILE_ATTRIBUTES) {
        twinBacks++;
        logf("icons: WARNING '%s' was created again on the desktop while %s was in her hands (%s saved into the old path?), "
             "hers comes back beside it, nothing merged or overwritten",
             narrow(want).c_str(), (ha & FILE_ATTRIBUTE_DIRECTORY) ? "the folder" : "the file", (wa & FILE_ATTRIBUTE_DIRECTORY) ? "an editor or a window" : "a program");
    }
    std::wstring to;
    DWORD e = 0;
    for (int k = 0; k < 6; k++) {
        to = freeName(want, (ha & FILE_ATTRIBUTE_DIRECTORY) != 0);
        if (to.empty()) {
            e = ERROR_FILE_EXISTS;
            break;
        }
        if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH)) {
            e = 0;
            break;
        }
        e = GetLastError();
        if (e == ERROR_ALREADY_EXISTS || e == ERROR_FILE_EXISTS) continue;
        if (e == ERROR_SHARING_VIOLATION) {
            Sleep(30);
            continue;
        }
        if ((e == ERROR_ACCESS_DENIED || e == ERROR_PATH_NOT_FOUND) && !desk.empty() && !samePath(parentOf(want), desk)) {
            logf("icons: could not give '%s' back to its folder (error %lu), trying the user's desktop", narrow(r.path).c_str(), e);
            want = desk + L"\\" + baseOf(r.path);
            continue;
        }
        break;
    }
    if (e) {
        logf("icons: could not give '%s' back to the desktop (error %lu), it stays in her hands, the record stays", narrow(r.path).c_str(), e);
        return false;
    }
    if (samePath(to, r.path)) logf("icons: '%s' is back on the desktop", narrow(to).c_str());
    else logf("icons: '%s' is taken on the desktop, gave it back as '%s'", narrow(r.path).c_str(), narrow(baseOf(to)).c_str());
    r.path = to;
    r.hands.clear();
    saveRecord(r, from);
    if (nowPath) *nowPath = to;
    return true;
}

bool DesktopIcons::toFolder(HandsRec& r, const std::wstring& folder, HWND owner) {
    if (testOnly && !fake) return false;
    std::wstring from = where(r), base = baseOf(r.path);
    DWORD a = GetFileAttributesW(from.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) {
        logf("icons: '%s' is not there any more, nothing to move into '%s'", narrow(from).c_str(), narrow(folder).c_str());
        return false;
    }
    bool ok = false;
    if (fake) {
        if (!inside(folder, fakeDir)) {
            badMoves++;
            logf("icons: BLOCKED moving '%s' into '%s' outside the fake desktop", narrow(base).c_str(), narrow(folder).c_str());
            return false;
        }
        std::wstring to = freeName(folder + L"\\" + base, (a & FILE_ATTRIBUTE_DIRECTORY) != 0);
        ok = !to.empty() && MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH);
        logf("icons: moved '%s' from her hands into '%s' as '%s' (snapshot: plain move): %d", narrow(base).c_str(), narrow(folder).c_str(),
             narrow(baseOf(to)).c_str(), (int)ok);
    } else {
        ComPtr<IFileOperation> op;
        ComPtr<IShellItem> src, dst;
        HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&op));
        if (SUCCEEDED(hr)) hr = op->SetOperationFlags(FOF_ALLOWUNDO | FOF_RENAMEONCOLLISION | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI | FOFX_ADDUNDORECORD);
        if (SUCCEEDED(hr)) hr = SHCreateItemFromParsingName(from.c_str(), nullptr, IID_PPV_ARGS(&src));
        if (SUCCEEDED(hr)) hr = SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&dst));
        if (SUCCEEDED(hr) && owner) op->SetOwnerWindow(owner);
        if (SUCCEEDED(hr)) hr = op->MoveItem(src.Get(), dst.Get(), base.c_str(), nullptr);
        if (SUCCEEDED(hr)) hr = op->PerformOperations();
        BOOL aborted = FALSE;
        if (op) op->GetAnyOperationsAborted(&aborted);
        ok = SUCCEEDED(hr) && !aborted && GetFileAttributesW(from.c_str()) == INVALID_FILE_ATTRIBUTES;
        logf("icons: moved '%s' from her hands into '%s': hr 0x%08X aborted %d ok %d", narrow(base).c_str(), narrow(folder).c_str(), (unsigned)hr,
             (int)aborted, (int)ok);
    }
    if (ok) dropRecord(r.path);
    return ok;
}

void DesktopIcons::crashGiveBack(const HandsRec& r) {
    if (r.hands.empty() || r.path.empty() || (testOnly && !fake)) return;
    MoveFileExW(r.hands.c_str(), r.path.c_str(), MOVEFILE_WRITE_THROUGH);
}

int DesktopIcons::sweepHands(const std::vector<std::wstring>& busy) {
    if (testOnly && !fake) return 0;
    std::wstring hd = handsDir(), desk = fake ? fakeDir : knownDir(FOLDERID_Desktop);
    if (desk.empty()) return 0;
    auto rs = readRecords();
    std::vector<std::pair<std::wstring, bool>> stray;
    WIN32_FIND_DATAW wf;
    HANDLE h = FindFirstFileW((hd + L"\\*").c_str(), &wf);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!wcscmp(wf.cFileName, L".") || !wcscmp(wf.cFileName, L"..")) continue;
        std::wstring p = hd + L"\\" + wf.cFileName;
        bool known = false;
        for (auto& r : rs) known |= samePath(r.hands, p);
        for (auto& b : busy) known |= samePath(b, p);
        if (!known) stray.push_back({p, (wf.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0});
    } while (FindNextFileW(h, &wf));
    FindClose(h);
    int n = 0;
    for (auto& [p, dir] : stray) {
        std::wstring nm = baseOf(p);
        size_t us = nm.find(L'_');
        if (us != std::wstring::npos && us >= 15 && nm.size() > 8 && nm[8] == L'-' && iswdigit(nm[0])) nm = nm.substr(us + 1);
        std::wstring to = freeName(desk + L"\\" + nm, dir);
        bool ok = !to.empty() && MoveFileExW(p.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH);
        logf("icons: '%s' was in her hands without a record, back on the desktop as '%s': %d (error %lu)", narrow(baseOf(p)).c_str(),
             narrow(baseOf(to)).c_str(), (int)ok, ok ? 0ul : GetLastError());
        n += ok;
    }
    return n;
}

void DesktopIcons::placeAfter(const std::wstring& path, POINT cell, const char* why, const std::wstring& recPath, bool dropRec) {
    cancelPut(path);
    Pend q;
    q.path = path;
    q.recPath = recPath;
    q.cell = cell;
    q.why = why;
    q.dropRec = dropRec;
    pend.push_back(q);
    pumpAt = -1e9;
}

void DesktopIcons::cancelPut(const std::wstring& path) {
    std::erase_if(pend, [&](const Pend& q) { return samePath(q.path, path); });
}

bool DesktopIcons::pending(const std::wstring& path) const {
    for (auto& q : pend)
        if (samePath(q.path, path)) return true;
    return false;
}

void DesktopIcons::pump(double now, bool force) {
    if (pend.empty() && !fake) return;
    if (!force && now - pumpAt < 0.25) return;
    pumpAt = now;
    bool listed = refresh();
    if (pend.empty()) return;
    if (!listed) {
        pumpAt = now + 1.0;
        return;
    }
    for (size_t k = 0; k < pend.size();) {
        Pend& q = pend[k];
        if (q.since < 0) q.since = now;
        int i = findPath(q.path);
        bool done = false;
        if (i < 0) {
            q.placed = false;
            if (GetFileAttributesW(q.path.c_str()) == INVALID_FILE_ATTRIBUTES) {
                logf("icons: '%s' left the desktop before it was put back", narrow(q.path).c_str());
                done = true;
            } else if (now - q.since > 15.0) {
                logf("icons: explorer never showed '%s', giving up on placing it", narrow(q.path).c_str());
                done = true;
            }
        } else if (!q.placed) {
            q.placed = true;
            q.placedAt = now;
            if (!put(items[i], q.cell, q.why.c_str())) done = true;
        } else if (now - q.placedAt > 2.5) {
            done = true;
        }
        if (done) {
            if (q.dropRec && !q.recPath.empty()) dropRecord(q.recPath, true);
            pend.erase(pend.begin() + k);
        } else {
            k++;
        }
    }
}

void DesktopIcons::flush(double secs) {
    double t0 = nowSec(), vt = pumpAt;
    while (!pend.empty() && ok() && nowSec() - t0 < secs) {
        if (fake) {
            vt += 0.3;
            pump(vt, true);
            if (lost) break;
        } else {
            pump(nowSec(), true);
            if (lost) break;
            if (!pend.empty()) Sleep(100);
        }
    }
}

bool DesktopIcons::restoreCarry() {
    if (testOnly && !fake) return true;
    auto rs = readRecords();
    bool all = true;
    for (auto& r : rs) {
        std::wstring now;
        if (!giveBack(r, &now)) {
            all = false;
            continue;
        }
        if (r.ownHome && !r.name.empty()) forgetHome(r.name);
        if (now.empty() || !isDesktopFile(now)) {
            dropRecord(r.path);
            continue;
        }
        logf("icons: a file was left in her hands last time: '%s' is on the desktop again, its icon goes home to %ld,%ld", narrow(now).c_str(), r.home.x,
             r.home.y);
        placeAfter(now, r.home, "back after a crash", r.path, true);
    }
    sweepHands({});
    return all;
}

int DesktopIcons::retryHands(const std::vector<std::wstring>& busy) {
    if (testOnly && !fake) return 0;
    int n = 0;
    std::vector<std::wstring> live = busy;
    for (auto r : readRecords()) {
        bool on = pending(r.path);
        for (auto& b : busy) on = on || samePath(b, r.path);
        if (on) {
            if (!r.hands.empty()) live.push_back(r.hands);
            continue;
        }
        bool stuck = !r.hands.empty();
        if (!stuck && r.attrs != INVALID_FILE_ATTRIBUTES && !(r.attrs & FILE_ATTRIBUTE_HIDDEN)) {
            DWORD a = GetFileAttributesW(r.path.c_str());
            stuck = a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_HIDDEN);
        }
        if (!stuck) continue;
        std::wstring now;
        if (!giveBack(r, &now)) continue;
        if (r.ownHome && !r.name.empty()) forgetHome(r.name);
        n++;
        logf("icons: '%s' was still in her hands after the carry ended, on the desktop again now", narrow(r.path).c_str());
        if (now.empty() || !isDesktopFile(now)) dropRecord(r.path);
        else placeAfter(now, r.home, "back after a failed return", r.path, true);
    }
    return n + sweepHands(live);
}

static std::wstring homesPath() { return appDataDir() + L"\\icons_home.txt"; }

bool DesktopIcons::rememberHome(const DIcon& ic) {
    std::string txt;
    readFile(homesPath(), txt);
    std::string key = narrow(ic.name) + "\t";
    size_t at = 0;
    while (at < txt.size()) {
        if (txt.compare(at, key.size(), key) == 0) return false;
        size_t nl = txt.find('\n', at);
        if (nl == std::string::npos) break;
        at = nl + 1;
    }
    char line[64];
    snprintf(line, sizeof(line), "%ld\t%ld\n", ic.pos.x, ic.pos.y);
    txt += key + line;
    writeFile(homesPath(), txt);
    return true;
}

int DesktopIcons::restoreHomes() {
    std::string txt;
    if (!readFile(homesPath(), txt)) return 0;
    refresh();
    std::vector<std::pair<const DIcon*, POINT>> m;
    size_t at = 0;
    while (at < txt.size()) {
        size_t nl = txt.find('\n', at);
        std::string line = txt.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        at = nl == std::string::npos ? txt.size() : nl + 1;
        size_t t1 = line.find('\t'), t2 = line.find('\t', t1 + 1);
        if (t1 == std::string::npos || t2 == std::string::npos) continue;
        int i = find(widen(line.substr(0, t1)));
        if (i < 0) continue;
        m.push_back({&items[i], POINT{atol(line.c_str() + t1 + 1), atol(line.c_str() + t2 + 1)}});
    }
    int n = m.empty() ? 0 : putMany(m, "homes");
    DeleteFileW(homesPath().c_str());
    refresh();
    return n;
}

void DesktopIcons::forgetHomes() { DeleteFileW(homesPath().c_str()); }

void DesktopIcons::forgetHome(const std::wstring& name) {
    std::string txt, out;
    if (!readFile(homesPath(), txt)) return;
    std::string key = narrow(name) + "\t";
    for (size_t at = 0; at < txt.size();) {
        size_t nl = txt.find('\n', at);
        std::string line = txt.substr(at, nl == std::string::npos ? std::string::npos : nl + 1 - at);
        at = nl == std::string::npos ? txt.size() : nl + 1;
        if (line.compare(0, key.size(), key) != 0) out += line;
    }
    if (out.empty()) DeleteFileW(homesPath().c_str());
    else writeFile(homesPath(), out);
}

std::wstring DesktopIcons::layoutDir() const {
    std::wstring d = appDataDir() + L"\\layouts";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

static std::vector<int> nameOrder(const std::vector<DIcon>& items) {
    std::vector<int> ix(items.size());
    for (size_t i = 0; i < ix.size(); i++) ix[i] = (int)i;
    std::stable_sort(ix.begin(), ix.end(), [&](int x, int y) {
        int c = items[x].name.compare(items[y].name);
        if (c) return c < 0;
        return _wcsicmp(items[x].path.c_str(), items[y].path.c_str()) < 0;
    });
    return ix;
}

std::string DesktopIcons::layoutText() const {
    std::string out;
    for (int i : nameOrder(items)) {
        auto& d = items[i];
        out += narrow(d.name) + "\t" + std::to_string(d.pos.x) + "\t" + std::to_string(d.pos.y) + "\n";
    }
    return out;
}

static std::string sortedLines(const std::string& txt) {
    std::vector<std::string> ls;
    for (size_t at = 0; at < txt.size();) {
        size_t nl = txt.find('\n', at);
        ls.push_back(txt.substr(at, nl == std::string::npos ? std::string::npos : nl - at));
        at = nl == std::string::npos ? txt.size() : nl + 1;
    }
    std::sort(ls.begin(), ls.end());
    std::string out;
    for (auto& l : ls) out += l + "\n";
    return out;
}

static std::vector<std::wstring> layoutFiles(const std::wstring& dir) {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.tsv").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

void DesktopIcons::layoutLoad() {
    if (layoutLoaded) return;
    layoutLoaded = true;
    auto fs = layoutFiles(layoutDir());
    if (fs.empty()) return;
    lastSnapFile = fs.back();
    std::string txt;
    readFile(layoutDir() + L"\\" + lastSnapFile, txt);
    lastSnapText = sortedLines(txt);
}

std::wstring DesktopIcons::layoutStamp(double now) {
    if (!stampFt0) {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        stampFt0 = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        stampNow0 = now;
    }
    double d = std::max(0.0, now - stampNow0);
    ULONGLONG t = stampFt0 + (ULONGLONG)(d * 1e7);
    FILETIME u{(DWORD)t, (DWORD)(t >> 32)}, l;
    SYSTEMTIME s{};
    FileTimeToLocalFileTime(&u, &l);
    FileTimeToSystemTime(&l, &s);
    wchar_t b[64];
    swprintf(b, 64, L"%04d%02d%02d-%02d%02d%02d", s.wYear, s.wMonth, s.wDay, s.wHour, s.wMinute, s.wSecond);
    std::wstring dir = layoutDir(), name = std::wstring(b) + L".tsv";
    for (int k = 2; GetFileAttributesW((dir + L"\\" + name).c_str()) != INVALID_FILE_ATTRIBUTES && k < 100; k++)
        name = std::wstring(b) + L"-" + std::to_wstring(k) + L".tsv";
    return name;
}

static std::wstring beforePath(const std::wstring& dir) { return dir + L"\\before.txt"; }

static std::vector<std::string> beforeNames(const std::wstring& dir) {
    std::vector<std::string> out;
    std::string txt;
    readFile(beforePath(dir), txt);
    for (size_t at = 0; at < txt.size();) {
        size_t nl = txt.find('\n', at);
        std::string l = txt.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        at = nl == std::string::npos ? txt.size() : nl + 1;
        while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
        if (!l.empty()) out.push_back(l);
    }
    return out;
}

bool DesktopIcons::layoutSnap(double now, const char* why, bool action) {
    if (!ok() || (testOnly && !fake)) return false;
    layoutLoad();
    if (action) beforeCand.clear();
    if (action && !refresh()) return false;
    std::string txt = layoutText(), key = sortedLines(txt);
    if (txt.empty()) return false;
    std::wstring dir = layoutDir();
    bool alive = !lastSnapFile.empty() && GetFileAttributesW((dir + L"\\" + lastSnapFile).c_str()) != INVALID_FILE_ATTRIBUTES;
    if (key == lastSnapText && alive) {
        if (action) beforeCand = lastSnapFile;
        return true;
    }
    std::wstring name = layoutStamp(now);
    if (!writeFile(dir + L"\\" + name, txt)) return false;
    lastSnapText = key;
    lastSnapFile = name;
    lastSnapAt = now;
    if (action) beforeCand = name;
    logf("icons: layout snapshot %s (%s, %zu icons)", narrow(name).c_str(), why, items.size());
    layoutPrune();
    return true;
}

void DesktopIcons::layoutActed() {
    if (beforeCand.empty()) return;
    std::wstring dir = layoutDir();
    auto hs = beforeNames(dir);
    std::string nm = narrow(beforeCand);
    std::erase(hs, nm);
    hs.push_back(nm);
    if (hs.size() > 5) hs.erase(hs.begin(), hs.end() - 5);
    std::string out;
    for (auto& h : hs) out += h + "\n";
    writeFile(beforePath(dir), out);
    beforeCand.clear();
}

void DesktopIcons::layoutIdle(double now) {
    if (now - lastIdleAt < 600.0) return;
    lastIdleAt = now;
    layoutSnap(now, "idle", false);
}

void DesktopIcons::layoutPrune() {
    std::wstring dir = layoutDir();
    auto fs = layoutFiles(dir);
    auto keep = beforeNames(dir);
    if (!beforeCand.empty()) keep.push_back(narrow(beforeCand));
    for (size_t k = 0; k + 12 < fs.size(); k++)
        if (std::find(keep.begin(), keep.end(), narrow(fs[k])) == keep.end()) DeleteFileW((dir + L"\\" + fs[k]).c_str());
}

std::vector<std::wstring> DesktopIcons::layoutHistory() const {
    std::wstring dir = layoutDir();
    auto hs = beforeNames(dir);
    std::vector<std::wstring> out;
    for (auto it = hs.rbegin(); it != hs.rend(); ++it) {
        std::wstring p = dir + L"\\" + widen(*it);
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) out.push_back(p);
    }
    return out;
}

std::wstring DesktopIcons::layoutBefore() const {
    auto hs = layoutHistory();
    return hs.empty() ? L"" : hs[0];
}

std::wstring DesktopIcons::layoutClock(const std::wstring& file) const {
    std::wstring b = baseOf(file);
    if (b.size() < 15 || b[8] != L'-') return L"";
    std::wstring hm = b.substr(9, 2) + L":" + b.substr(11, 2);
    SYSTEMTIME s{};
    GetLocalTime(&s);
    wchar_t today[16];
    swprintf(today, 16, L"%04d%02d%02d", s.wYear, s.wMonth, s.wDay);
    if (b.compare(0, 8, today) != 0) hm += L" (" + b.substr(6, 2) + L"." + b.substr(4, 2) + L")";
    return hm;
}

int DesktopIcons::layoutRestore(const std::wstring& file, int* total) {
    std::string txt;
    if (total) *total = 0;
    if (!readFile(file, txt) || !refresh()) return 0;
    struct W { std::wstring name; int k; POINT p; };
    std::vector<W> want;
    std::map<std::wstring, int> seen;
    for (size_t at = 0; at < txt.size();) {
        size_t nl = txt.find('\n', at);
        std::string line = txt.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        at = nl == std::string::npos ? txt.size() : nl + 1;
        while (!line.empty() && line.back() == '\r') line.pop_back();
        size_t t1 = line.find('\t'), t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1);
        if (t1 == std::string::npos || t2 == std::string::npos) continue;
        std::wstring name = widen(line.substr(0, t1));
        want.push_back({name, seen[name]++, POINT{atol(line.c_str() + t1 + 1), atol(line.c_str() + t2 + 1)}});
    }
    std::vector<int> ord = nameOrder(items);
    auto kth = [&](const std::wstring& name, int k) {
        for (int i : ord)
            if (items[i].name == name && k-- == 0) return i;
        return -1;
    };
    std::vector<std::pair<const DIcon*, POINT>> m;
    for (auto& w : want) {
        int i = kth(w.name, w.k);
        if (i >= 0) m.push_back({&items[i], w.p});
    }
    if (total) *total = (int)m.size();
    int moved = m.empty() ? 0 : putMany(m, "restore layout");
    if (!fake) Sleep(300);
    refresh();
    ord = nameOrder(items);
    int same = 0;
    for (auto& w : want) {
        int i = kth(w.name, w.k);
        if (i < 0) continue;
        if (items[i].pos.x == w.p.x && items[i].pos.y == w.p.y) same++;
        else logf("icons: restore: '%s' wanted %ld,%ld, now at %ld,%ld", narrow(w.name).c_str(), w.p.x, w.p.y, items[i].pos.x, items[i].pos.y);
    }
    logf("icons: layout restore from %s: %d of %zu exactly in place (%zu lines, moved %d)", narrow(baseOf(file)).c_str(), same, m.size(), want.size(), moved);
    return same;
}
