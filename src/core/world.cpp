#include "world.h"

static bool isCloaked(HWND h) {
    int c = 0;
    return SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &c, sizeof(c))) && c != 0;
}

static bool frameRect(HWND h, RECT& r) {
    if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) return GetWindowRect(h, &r) != 0;
    return true;
}

static bool candidate(HWND h, HWND self, RECT& r) {
    if (h == self) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId()) return false;
    if (!IsWindowVisible(h) || IsIconic(h)) return false;
    if (isCloaked(h)) return false;
    LONG ex = GetWindowLongW(h, GWL_EXSTYLE);
    if (ex & WS_EX_TRANSPARENT) return false;
    if ((ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_APPWINDOW)) return false;
    wchar_t cls[96];
    GetClassNameW(h, cls, 96);
    static const wchar_t* skip[] = {L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd"};
    for (auto s : skip)
        if (!wcscmp(cls, s)) return false;
    if (ex & WS_EX_LAYERED) {
        BYTE a = 255;
        DWORD fl = 0;
        COLORREF ck;
        if (GetLayeredWindowAttributes(h, &ck, &a, &fl) && (fl & LWA_ALPHA) && a < 10) return false;
    }
    if (!frameRect(h, r)) return false;
    if (r.right - r.left < 60 || r.bottom - r.top < 40) return false;
    return true;
}

void World::setFake(RECT monitor, RECT work) {
    fake = true;
    fakeInWeb = false;
    winFloors = false;
    mons = {Mon{monitor, work}};
    surfs.clear();
    iconRuns.clear();
    occluders.clear();
    fakeWins.clear();
    surfs.push_back({SurfKind::Floor, nullptr, 0, (float)work.bottom, (float)work.left, (float)work.right});
}

void World::addFakeWindow(HWND h, RECT r, bool zoomed) {
    fakeWins.push_back({h, r, zoomed});
    occluders.push_back(r);
    int m = monitorAt(V2((r.left + r.right) * 0.5f, r.top + 1.f));
    if (m >= 0 && !zoomed) addWinFloor(h, r, (int)fakeWins.size() - 1, [&](int j) { return fakeWins[j].r; });
    if (m < 0 || zoomed || r.top < mons[m].rc.top + headroom || r.top > mons[m].work.bottom - 30) return;
    surfs.push_back({SurfKind::Window, h, -1, (float)r.top, (float)std::max(r.left, mons[m].rc.left) + 6.f, (float)std::min(r.right, mons[m].rc.right) - 6.f});
}

void World::addWinFloor(HWND h, const RECT& r, int above, const std::function<RECT(int)>& winAt) {
    if (!winFloors) return;
    float y = (float)r.bottom - 2.f, room = winRoom;
    if (r.bottom - r.top < room + 30) return;
    std::vector<std::pair<float, float>> iv;
    for (auto& mn : mons) {
        if (y < mn.rc.top + room || y > mn.work.bottom - 30 || y > mn.rc.bottom) continue;
        float a = std::max((float)r.left + 10.f, (float)mn.rc.left + 6.f), b = std::min((float)r.right - 10.f, (float)mn.rc.right - 6.f);
        if (b > a) iv.push_back({a, b});
    }
    for (int j = 0; j < above && !iv.empty(); j++) {
        RECT o = winAt(j);
        if (!(o.top < y && o.bottom > y - room)) continue;
        float a = o.left - 4.f, b = o.right + 4.f;
        std::vector<std::pair<float, float>> nv;
        for (auto& seg : iv) {
            if (b <= seg.first || a >= seg.second) { nv.push_back(seg); continue; }
            if (a > seg.first) nv.push_back({seg.first, a});
            if (b < seg.second) nv.push_back({b, seg.second});
        }
        iv.swap(nv);
    }
    for (auto& seg : iv)
        if (seg.second - seg.first >= 80.f) {
            Surf s{SurfKind::WinFloor, h, -1, y, seg.first, seg.second};
            s.win = r;
            surfs.push_back(s);
        }
}

void World::refresh() {
    if (fake) {
        if (web) {
            if (!fakeInWeb) fakeKeep = surfs;
            fakeInWeb = true;
            surfs = webSurfs;
            addWebBottom();
        } else if (fakeInWeb) {
            fakeInWeb = false;
            surfs = fakeKeep;
        }
        return;
    }
    mons.clear();
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR hm, HDC, LPRECT, LPARAM lp) -> BOOL {
            MONITORINFO mi{sizeof(mi)};
            if (GetMonitorInfoW(hm, &mi)) ((World*)lp)->mons.push_back({mi.rcMonitor, mi.rcWork});
            return TRUE;
        },
        (LPARAM)this);

    iconRuns.clear();
    if (web) {
        surfs = webSurfs;
        addWebBottom();
        occluders.clear();
        return;
    }
    struct Win { HWND h; RECT r; };
    struct Ctx { HWND self; std::vector<Win> wins; } ctx{self, {}};
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            auto* c = (Ctx*)lp;
            RECT r;
            if (candidate(h, c->self, r)) c->wins.push_back({h, r});
            return TRUE;
        },
        (LPARAM)&ctx);

    surfs.clear();
    occluders.clear();
    for (size_t m = 0; m < mons.size(); m++) {
        const RECT& w = mons[m].work;
        surfs.push_back({SurfKind::Floor, nullptr, (int)m, (float)w.bottom, (float)w.left, (float)w.right});
    }
    for (size_t i = 0; i < ctx.wins.size(); i++) {
        const RECT& r = ctx.wins[i].r;
        occluders.push_back(r);
        if (!IsZoomed(ctx.wins[i].h)) addWinFloor(ctx.wins[i].h, r, (int)i, [&](int j) { return ctx.wins[j].r; });
        float y = (float)r.top;
        int m = monitorAt(V2((r.left + r.right) * 0.5f, y + 1));
        if (m < 0) continue;
        if (y < mons[m].rc.top + headroom) continue;
        if (y > mons[m].work.bottom - 30) continue;
        std::vector<std::pair<float, float>> iv{{(float)std::max(r.left, mons[m].rc.left) + 6.f, (float)std::min(r.right, mons[m].rc.right) - 6.f}};
        for (size_t j = 0; j < i && !iv.empty(); j++) {
            const RECT& o = ctx.wins[j].r;
            if (!(o.top <= y + 2 && o.bottom >= y - 2)) continue;
            float a = o.left - 4.f, b = o.right + 4.f;
            std::vector<std::pair<float, float>> nv;
            for (auto& seg : iv) {
                if (b <= seg.first || a >= seg.second) { nv.push_back(seg); continue; }
                if (a > seg.first) nv.push_back({seg.first, a});
                if (b < seg.second) nv.push_back({b, seg.second});
            }
            iv.swap(nv);
        }
        for (auto& seg : iv)
            if (seg.second - seg.first >= 50.f) surfs.push_back({SurfKind::Window, ctx.wins[i].h, -1, y, seg.first, seg.second});
    }
}

int World::monitorAt(V2 p) const {
    for (size_t i = 0; i < mons.size(); i++) {
        const RECT& r = mons[i].rc;
        if (p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom) return (int)i;
    }
    return -1;
}

void World::addWebBottom() {
    Surf b{SurfKind::Web, nullptr, -1, (float)webRc.bottom, (float)webRc.left + 6.f, (float)webRc.right - 6.f};
    for (auto& sp : screenSpans(b.y - 2.f, b.x0, b.x1))
        if (sp.second - sp.first >= 50.f) {
            b.x0 = sp.first;
            b.x1 = sp.second;
            surfs.push_back(b);
        }
}

std::vector<std::pair<float, float>> World::screenSpans(float y, float x0, float x1) const {
    std::vector<std::pair<float, float>> out;
    for (auto& m : mons) {
        if (y < m.rc.top + 2 || y > m.rc.bottom) continue;
        float a = std::max(x0, (float)m.rc.left + 6.f), b = std::min(x1, (float)m.rc.right - 6.f);
        if (b > a) out.push_back({a, b});
    }
    std::sort(out.begin(), out.end());
    std::vector<std::pair<float, float>> joined;
    for (auto& sp : out) {
        if (!joined.empty() && sp.first - joined.back().second <= 16.f) joined.back().second = std::max(joined.back().second, sp.second);
        else joined.push_back(sp);
    }
    return joined;
}

int World::nearestMonitor(V2 p) const {
    int best = 0;
    float bd = 1e30f;
    for (size_t i = 0; i < mons.size(); i++) {
        const RECT& r = mons[i].rc;
        float dx = std::max({(float)r.left - p.x, 0.f, p.x - (float)r.right});
        float dy = std::max({(float)r.top - p.y, 0.f, p.y - (float)r.bottom});
        float d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = (int)i; }
    }
    return best;
}

bool World::windowRect(HWND h, RECT& r) const {
    if (fake) {
        for (auto& w : fakeWins)
            if (w.h == h) { r = w.r; return true; }
        return false;
    }
    if (!IsWindow(h) || !IsWindowVisible(h) || IsIconic(h) || isCloaked(h)) return false;
    return frameRect(h, r);
}

bool World::rectVisible(const RECT& r) const {
    bool onScreen = false;
    for (auto& m : mons) {
        RECT x;
        if (IntersectRect(&x, &r, &m.work) && EqualRect(&x, &r)) { onScreen = true; break; }
    }
    if (!onScreen) return false;
    for (auto& o : occluders) {
        RECT x;
        if (IntersectRect(&x, &r, &o)) return false;
    }
    return true;
}

void World::addIconSurf(int index, const RECT& img) {
    surfs.push_back({SurfKind::Icon, nullptr, index, (float)img.top, img.left + 3.f, img.right - 3.f});
}

void World::addIconSurfs(std::vector<IconBox> boxes, int step) {
    if (!joinIcons) {
        for (auto& b : boxes) addIconSurf(b.index, b.img);
        return;
    }
    std::sort(boxes.begin(), boxes.end(), [](const IconBox& a, const IconBox& b) {
        return a.img.top != b.img.top ? a.img.top < b.img.top : a.img.left < b.img.left;
    });
    for (size_t i = 0; i < boxes.size();) {
        size_t e = i + 1;
        while (e < boxes.size() && boxes[e].img.top - boxes[i].img.top <= 2) e++;
        std::sort(boxes.begin() + i, boxes.begin() + e, [](const IconBox& a, const IconBox& b) { return a.img.left < b.img.left; });
        for (size_t a = i; a < e;) {
            size_t b = a + 1;
            while (b < e && boxes[b].img.left - boxes[b - 1].img.left <= step + 2) b++;
            Surf s{SurfKind::Icon, nullptr, boxes[a].index, (float)boxes[a].img.top, boxes[a].img.left + 3.f, boxes[a].img.right - 3.f};
            std::vector<int> run;
            for (size_t k = a; k < b; k++) {
                run.push_back(boxes[k].index);
                s.y = std::min(s.y, (float)boxes[k].img.top);
                s.x0 = std::min(s.x0, boxes[k].img.left + 3.f);
                s.x1 = std::max(s.x1, boxes[k].img.right - 3.f);
            }
            s.run = (int)iconRuns.size();
            iconRuns.push_back(std::move(run));
            surfs.push_back(s);
            a = b;
        }
        i = e;
    }
}

bool World::runHas(const Surf& s, int icon) const {
    if (s.kind != SurfKind::Icon) return false;
    if (s.run < 0 || s.run >= (int)iconRuns.size()) return s.index == icon;
    for (int k : iconRuns[s.run])
        if (k == icon) return true;
    return false;
}

void World::dump() const {
    logf("world: %zu monitors, %zu surfaces", mons.size(), surfs.size());
    for (auto& m : mons)
        logf("  mon %ld,%ld %ldx%ld work bottom %ld", m.rc.left, m.rc.top, m.rc.right - m.rc.left, m.rc.bottom - m.rc.top, m.work.bottom);
    for (auto& s : surfs) {
        char title[128] = "";
        if (s.hwnd) GetWindowTextA(s.hwnd, title, sizeof(title));
        logf("  surf %d y=%.0f x=%.0f..%.0f %s", (int)s.kind, s.y, s.x0, s.x1, title);
    }
}
