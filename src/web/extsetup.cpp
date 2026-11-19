#include "pet.h"
#include "webnest.h"
#include "games.h"
#include <shlwapi.h>

using M3 = D2D1::Matrix3x2F;

static bool g_testAlive = false;
void extTestAlive(bool on) { g_testAlive = on; }
static bool extAlive(App& a) { return g_testAlive || (a.web && a.web->extAlive()); }
static bool extKnown(App& a) { return g_testAlive || (a.web && a.web->extKnown()); }
static std::wstring extDir() { return exeDir() + L"\\extension"; }

ExtBrowser extBrowserFor(const std::wstring& progId) {
    ExtBrowser b;
    b.progId = progId;
    auto starts = [&](const wchar_t* p) { return _wcsnicmp(progId.c_str(), p, wcslen(p)) == 0; };
    if (starts(L"ChromeHTML")) b.url = L"chrome://extensions";
    else if (starts(L"MSEdge")) { b.url = L"edge://extensions"; b.edge = true; }
    else if (starts(L"BraveHTML")) b.url = L"brave://extensions";
    else if (starts(L"Opera")) b.url = L"opera://extensions";
    else if (starts(L"YandexHTML")) b.url = L"browser://extensions";
    else if (starts(L"Firefox")) b.firefox = true;
    else b.guess = true;
    return b;
}

ExtBrowser extDefaultBrowser() {
    wchar_t id[160] = L"";
    DWORD n = sizeof(id);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\https\\UserChoice", L"ProgId",
                 RRF_RT_REG_SZ, nullptr, id, &n);
    ExtBrowser b = extBrowserFor(id);
    wchar_t cmd[MAX_PATH * 2] = L"";
    n = sizeof(cmd);
    std::wstring key = std::wstring(id) + L"\\shell\\open\\command";
    if (id[0] && RegGetValueW(HKEY_CLASSES_ROOT, key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, cmd, &n) == ERROR_SUCCESS) {
        std::wstring c = cmd;
        if (!c.empty() && c[0] == L'"') {
            size_t e = c.find(L'"', 1);
            b.exe = c.substr(1, e == std::wstring::npos ? std::wstring::npos : e - 1);
        } else {
            std::wstring low = c;
            for (auto& ch : low) ch = (wchar_t)towlower(ch);
            size_t e = low.find(L".exe");
            b.exe = e == std::wstring::npos ? c : c.substr(0, e + 4);
        }
    }
    if (b.exe.empty()) {
        wchar_t buf[MAX_PATH * 2];
        n = MAX_PATH * 2;
        if (SUCCEEDED(AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, L"https", L"open", buf, &n))) b.exe = buf;
    }
    return b;
}

bool extIsBrowserImage(const std::wstring& exe, const std::wstring& image) {
    size_t cut = exe.find_last_of(L"\\/"), ic = image.find_last_of(L"\\/");
    std::wstring want = exe.substr(cut == std::wstring::npos ? 0 : cut + 1);
    if (want.empty()) return false;
    if (!_wcsicmp(image.c_str() + (ic == std::wstring::npos ? 0 : ic + 1), want.c_str())) return true;
    if (cut == std::wstring::npos || cut < 3) return false;
    std::wstring dir = exe.substr(0, cut + 1);
    wchar_t win[MAX_PATH] = L"";
    UINT wn = GetWindowsDirectoryW(win, MAX_PATH);
    if (wn && !_wcsnicmp(dir.c_str(), win, wn)) return false;
    return image.size() > dir.size() && !_wcsnicmp(image.c_str(), dir.c_str(), dir.size());
}

bool Pet::extAskNow() {
    const char *cat = nullptr, *ch = "choiceExtStep", *id = nullptr;
    float wait = 600.f;
    switch (extStep) {
    case ExtOffer: cat = "extOffer"; ch = "choiceExtOffer"; id = "extOffer"; wait = 25.f; break;
    case ExtPlea: cat = "extPlea"; ch = "choiceExtPlea"; id = "extPlea"; wait = 25.f; break;
    case ExtPaste: cat = "extStepPaste"; id = "extPaste"; break;
    case ExtDev:
        if (extCanPoint()) cat = extB.edge ? "extStepDevEdge" : "extStepDev";
        else cat = extB.edge ? "extStepDevNoWinEdge" : "extStepDevNoWin";
        id = "extDev";
        break;
    case ExtDrag: cat = "extStepDrag"; ch = "choiceExtDrag"; id = "extDrag"; break;
    case ExtFallback: cat = "extFallback"; id = "extFallback"; break;
    default: return true;
    }
    std::wstring s = app.phrases.pick(cat, L"");
    auto c = app.phrases.list(ch);
    if (s.empty() || c.empty()) return false;
    app.bubble.ask(s, c, id, wait);
    lastSay = app.now;
    logf("ask '%s' (%s)", id, cat);
    return true;
}

bool Pet::extCanStart() const { return !baby() && vit.intro == 0 && !vit.fatal && !vit.dead && !byeOn; }

bool Pet::extCanPoint() const {
    return extPointAt.x > -1e8f && active == 0 && grounded() && !dragging && (st == St::Idle || st == St::Land);
}

bool Pet::extStart(bool fromButton) {
    if (!extCanStart()) {
        logf("ext: start refused (intro %d fatal %d)", vit.intro, (int)vit.fatal);
        return false;
    }
    dropCarried(false);
    dropHeld("ext");
    if (extKnown(app)) {
        extStop("already");
        app.settings.extAsked = true;
        app.settings.save();
        tHappy = 1.2f;
        sayCat("extAlready", 1.f, L"", true);
        return true;
    }
    extStop("restart");
    gameStop("ext");
    if (st == St::Sleep && !carriedAsleep()) wake(false);
    if (st == St::Inside || st == St::Dive) callBack(lastCursor);
    extOfferT = 1e9f;
    extT = 0;
    extStep = ExtOffer;
    if (fromButton) extBegin();
    else extAskDue = true;
    return true;
}

void Pet::extBegin() {
    extB = extTestProgId.empty() ? extDefaultBrowser() : extBrowserFor(extTestProgId);
    logf("ext: browser '%s' -> %s, exe '%s'%s%s", narrow(extB.progId).c_str(), narrow(extB.url).c_str(), narrow(extB.exe).c_str(),
         extB.firefox ? ", firefox" : "", extB.guess ? ", guessed" : "");
    if (extB.firefox) {
        extStop("firefox");
        app.settings.extAsked = true;
        app.settings.save();
        tSad = 1.5f;
        sayCat("extFirefox", 1.f, L"", true);
        return;
    }
    tHappy = 1.5f;
    extLaunch();
    extStep = ExtPaste;
    extT = 0;
    extAskDue = true;
}

void Pet::extLaunch() {
    if (app.world.fake) {
        logf("ext: would open a new tab in '%s' and copy '%s' (snapshot: no browser, no clipboard)", narrow(extB.exe).c_str(), narrow(extB.url).c_str());
        return;
    }
    bool ok = false;
    if (!extB.exe.empty()) {
        std::wstring cmd = L"\"" + extB.exe + L"\"";
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        ok = CreateProcessW(extB.exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi) != 0;
        if (ok) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }
    bool copied = false;
    if (OpenClipboard(app.hwnd)) {
        EmptyClipboard();
        size_t n = (extB.url.size() + 1) * sizeof(wchar_t);
        if (HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, n)) {
            memcpy(GlobalLock(g), extB.url.c_str(), n);
            GlobalUnlock(g);
            copied = SetClipboardData(CF_UNICODETEXT, g) != nullptr;
            if (!copied) GlobalFree(g);
        }
        CloseClipboard();
    }
    logf("ext: new tab %s, clipboard %s", ok ? "opened" : "failed", copied ? "set" : "failed");
}

void Pet::extFindWindow() {
    extWnd = nullptr;
    if (app.world.fake) {
        if (!app.world.fakeWins.empty()) extWnd = app.world.fakeWins[0].h;
        return;
    }
    if (extB.exe.empty()) return;
    struct Ctx { std::wstring exe; HWND fg, first; } c{extB.exe, GetForegroundWindow(), nullptr};
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            auto* c = (Ctx*)lp;
            if (!IsWindowVisible(h) || IsIconic(h) || GetWindow(h, GW_OWNER)) return TRUE;
            int cloaked = 0;
            if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;
            if (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
            RECT r;
            GetWindowRect(h, &r);
            if (r.right - r.left < 300 || r.bottom - r.top < 200) return TRUE;
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            HANDLE ph = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!ph) return TRUE;
            wchar_t path[MAX_PATH * 2];
            DWORD n = MAX_PATH * 2;
            bool got = QueryFullProcessImageNameW(ph, 0, path, &n) != 0;
            CloseHandle(ph);
            if (!got || !extIsBrowserImage(c->exe, path)) return TRUE;
            if (h == c->fg) { c->first = h; return FALSE; }
            if (!c->first) c->first = h;
            return TRUE;
        },
        (LPARAM)&c);
    extWnd = c.first;
}

V2 Pet::extToggle(const RECT& r) const {
    float d = app.dpi;
    if (extB.edge) return V2(r.left + 150.f * d, r.top + std::min((r.bottom - r.top) * 0.5f, 380.f * d));
    return V2(r.right - 46.f * d, r.top + 117.f * d);
}

void Pet::extPlanMove() {
    extUp = false;
    RECT r;
    if (!extWnd || !app.world.windowRect(extWnd, r) || app.world.mons.empty()) {
        logf("ext: no browser window, points from where she is");
        extPointAt = V2(-1e9f, -1e9f);
        extPlanned = true;
        return;
    }
    extPointAt = extToggle(r);
    bool zoomed = false;
    if (app.world.fake) {
        for (auto& w : app.world.fakeWins)
            if (w.h == extWnd) zoomed = w.zoomed;
    } else {
        zoomed = IsZoomed(extWnd) != 0;
    }
    int m = app.world.monitorAt(extPointAt);
    if (m < 0) m = app.world.nearestMonitor(extPointAt);
    float side = extB.edge ? 1.f : -1.f;
    float want = extPointAt.x + side * 300.f * S();
    float tall = (standTop > 1.f ? standTop : comH * 2.1f) + 45.f * S();
    const Surf* best = nullptr;
    float bx = 0, bd = 1e9f;
    if (!zoomed)
        for (auto& s : app.world.surfs) {
            if (s.kind != SurfKind::Window || s.hwnd != extWnd || s.x1 - s.x0 < 60.f * S() || s.y - tall < app.world.mons[m].rc.top) continue;
            float x = clampf(want, s.x0 + 24.f * S(), s.x1 - 24.f * S());
            if (fabsf(x - want) < bd) { bd = fabsf(x - want); bx = x; best = &s; }
        }
    if (best) {
        bool there = gnd.valid && gnd.kind == SurfKind::Window && gnd.hwnd == extWnd && fabsf(gnd.y - best->y) < 3 && fabsf(pos.x - bx) < 24.f * S();
        if (!there && extTries++ == 0 && gnd.valid && gnd.kind == SurfKind::Floor && fabsf(pos.x - bx) > 70.f * S()) {
            runK = 1.8f;
            goWalk(bx);
            logf("ext: runs under the window edge x %.0f", bx);
            return;
        }
        if (there || goJump(V2(bx, best->y), 30)) {
            extPlanned = extUp = true;
            logf("ext: to the window edge y %.0f x %.0f (toggle %.0f,%.0f)", best->y, bx, extPointAt.x, extPointAt.y);
            return;
        }
    }
    extPlanned = true;
    const RECT& wk = app.world.mons[m].work;
    float off = std::max(230.f * S(), (wk.bottom - extPointAt.y) * 0.8f);
    float x = clampf(extPointAt.x + side * off, wk.left + 40.f * S(), wk.right - 40.f * S());
    logf("ext: window %s, to the taskbar x %.0f (toggle %.0f,%.0f)", zoomed ? "maximized" : "has no room above", x, extPointAt.x, extPointAt.y);
    runK = 1.8f;
    if (gnd.valid && gnd.kind == SurfKind::Floor && gnd.index == m) {
        if (fabsf(pos.x - x) > 20.f * S()) goWalk(x);
    } else {
        goJump(V2(x, (float)wk.bottom), 60);
    }
}

void Pet::extPointNow() {
    if (!game) runK = 1.f;
    extStep = ExtDev;
    extT = 0;
    extAskDue = true;
    if (grounded() && !dragging && (st == St::Sit || st == St::LookAround)) goIdle(1.f, 2.f);
    if (extPointAt.x > -1e8f && fabsf(extPointAt.x - pos.x) > 6.f * S()) facing = extPointAt.x > pos.x ? 1 : -1;
    logf("ext: pointing at %.0f,%.0f from %.0f,%.0f (%s)", extPointAt.x, extPointAt.y, pos.x, pos.y, extUp ? "window edge" : "below");
}

void Pet::extDone() {
    logf("ext: connected at step %d", extStep);
    if (extFolder)
        for (int i = 0; i < 6; i++)
            emit(4, carriedCenter + V2(randf(-12, 12) * S(), randf(-6, 6) * S()), V2(randf(-70, 70) * S(), randf(-60, 10) * S()), 0.6f, 1.f);
    extStop("connected");
    app.settings.extAsked = true;
    app.settings.save();
    tHappy = 2.5f;
    addRel(2.f, "extension");
    if (grounded() && !dragging) {
        lastWave = app.now;
        setState(St::Wave, 2.2f);
    }
    std::wstring s = app.phrases.pick("extDone", L"");
    if (!s.empty() && !app.bubble.asking()) {
        app.bubble.say(s);
        lastSay = app.now;
    }
}

void Pet::extStop(const char* why) {
    if (!extOn()) return;
    logf("ext: stop (%s) at step %d", why, extStep);
    if (app.bubble.asking() && app.bubble.askId.rfind("ext", 0) == 0) app.bubble.close();
    extStep = ExtNone;
    extFolder = extAskDue = extPlanned = false;
    if (!game) runK = 1.f;
    extWnd = nullptr;
}

void Pet::extAnswer(const std::string& id, int choice) {
    if (!extOn()) return;
    bool yes = choice == 0, ignored = choice < 0;
    if (id == "extOffer") {
        if (yes) extBegin();
        else if (ignored) extStop("offer ignored");
        else {
            extStep = ExtPlea;
            extAskDue = true;
            tSad = 1.2f;
        }
    } else if (id == "extPlea") {
        if (yes) extBegin();
        else {
            extStop(ignored ? "plea ignored" : "declined");
            if (!ignored) {
                app.settings.extAsked = true;
                app.settings.save();
                tSad = 1.5f;
                sayCat("extDeclined", 1.f, L"", true);
            }
        }
    } else if (ignored || choice == 1) {
        extStop(ignored ? "step ignored" : "later");
        sayCat("extLater", 1.f, L"", true);
    } else if (id == "extPaste") {
        extFindWindow();
        extStep = ExtGo;
        extT = 0;
        extPlanned = false;
        extTries = 0;
        followT = 0;
    } else if (id == "extDev") {
        extStep = ExtDrag;
        extFolder = true;
        extAskDue = true;
        squashV -= 1.5f;
    } else if (id == "extDrag") {
        extFolder = false;
        if (app.world.fake) logf("ext: would open '%s' in Explorer (snapshot)", narrow(extDir()).c_str());
        else ShellExecuteW(nullptr, L"explore", extDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        extStep = ExtFallback;
        extAskDue = true;
    } else if (id == "extFallback") {
        extStep = ExtWait;
        extT = 0;
        sayCat("extWaitSignal", 1.f, L"", true);
    }
}

void Pet::extUpdate(float dt, const Input& in, bool free) {
    bool alive = extAlive(app);
    if (!extOn()) {
        bool known = extKnown(app);
        if (known && !app.settings.extAsked) {
            app.settings.extAsked = true;
            app.settings.save();
            logf("ext: already connected, no offer");
        }
        if (app.settings.extAsked || known || vit.intro != 0) return;
        if (helloOnLand) { extOfferT = randf(60, 150); return; }
        if (extOfferT < 0) extOfferT = randf(90, 200);
        extOfferT -= dt;
        if (extOfferT > 0) return;
        if (!free || !app.settings.talk || app.bubble.active || in.idleSec < 4 || in.idleSec > 60 || app.now - lastSay < 45 || webActive()) {
            extOfferT = 20.f;
            return;
        }
        extOfferT = 1e9f;
        extStep = ExtOffer;
        extT = 0;
        extAskDue = true;
        logf("ext: offer");
    }
    extT += dt;
    extDragHeld = dragging ? extDragHeld + dt : 0.f;
    const char* why = (vit.fatal || vit.dead) ? "fatal" : st == St::Sleep ? "asleep" : game ? "game" : byeOn ? "shutdown" : extDragHeld > 3.f ? "carried" : nullptr;
    if (why) {
        extStop(why);
        return;
    }
    if (alive) {
        extDone();
        return;
    }
    if ((extStep == ExtGo || extStep == ExtDev) && extWnd) {
        RECT r;
        if (app.world.windowRect(extWnd, r)) extPointAt = extToggle(r);
    }
    if (extStep == ExtGo) {
        if (!extPlanned) {
            if (grounded() && !dragging && st != St::PrepJump && st != St::Walk) extPlanMove();
        }
        bool arrived = extPlanned && extT > 0.4f && grounded() && !dragging && (st == St::Idle || st == St::Sit || st == St::LookAround);
        if (arrived || extT > 14.f) {
            extPointNow();
        }
    }
    if (extStep == ExtDev && extPointAt.x > -1e8f && grounded() && !dragging && fabsf(extPointAt.x - pos.x) > 8.f * S())
        facing = extPointAt.x > pos.x ? 1 : -1;
    if (extStep == ExtWait && extT > 8.f) {
        extStop("no signal");
        app.settings.extAsked = true;
        app.settings.save();
        sayCat("extNoSignal", 1.f, L"", true);
        return;
    }
    bool asks = extStep == ExtOffer || extStep == ExtPlea || extStep == ExtPaste || extStep == ExtDev || extStep == ExtDrag || extStep == ExtFallback;
    if (asks && !extAskDue && !app.bubble.active && app.bubble.askId.rfind("ext", 0) != 0) {
        logf("ext: step %d lost its bubble", extStep);
        if (extStep == ExtOffer || extStep == ExtPlea) {
            extAnswer(extStep == ExtOffer ? "extOffer" : "extPlea", -1);
            return;
        }
        extAskDue = true;
    }
    if (extAskDue && !app.bubble.held() && st != St::Inside && st != St::Dive && !byeOn && (!app.bubble.asking() || app.bubble.askId.rfind("ext", 0) == 0))
        if (extAskNow()) extAskDue = false;
}

void Pet::extPose(float dt) {
    bool want = extStep == ExtDev && extCanPoint();
    bPoint = approach(bPoint, want ? 1.f : 0.f, 9, dt);
    if (bPoint < 0.01f || active != 0 || iArmL < 0 || iArmR < 0 || iFArmL < 0 || iFArmR < 0) return;
    int side = (extPointAt - shoulderScreen(1)).len() < (extPointAt - shoulderScreen(2)).len() ? 1 : 2;
    int ai = side == 1 ? iArmL : iArmR, fi = side == 1 ? iFArmL : iFArmR;
    const Rig& R0 = app.rig;
    float L = (R0.parts[fi].joint - R0.parts[ai].joint).len() + (R0.parts[fi].hand() - R0.parts[fi].joint).len();
    V2 sh = shoulderScreen(side);
    V2 dir = (extPointAt - sh).norm();
    float jab = 0.5f + 0.5f * sinf(t * 6.f);
    auto& P = curInst().pose;
    PartPose sa = P[ai], sf = P[fi];
    reachTo(sh + dir * (L * lerpf(0.97f, 1.06f, jab)), side);
    P[ai].rot = lerpf(sa.rot, P[ai].rot, bPoint);
    P[fi].rot = lerpf(sf.rot, P[fi].rot, bPoint);
    if (bPoint < 0.3f) {
        P[ai].zOff = sa.zOff;
        P[fi].zOff = sf.zOff;
    }
}

void Pet::extFace(FaceState& tg, V2& lk) {
    if (extStep == ExtPlea && app.bubble.asking()) {
        tg.happy = 0;
        tg.sad = 0.65f;
        tg.blush = std::max(tg.blush, 0.5f);
    }
    if (extStep == ExtDev && bPoint > 0.3f) {
        V2 d = rotv((extPointAt - headC) / (260.f * S()), -rot);
        lk = V2(clampf(d.x, -1, 1), clampf(d.y, -0.8f, 1));
    }
}

static D2D1_RECT_F folderRect(V2 c, float s) {
    float w = 40.f * s, h = 30.f * s;
    return D2D1::RectF(c.x - w / 2, c.y - h * 0.62f, c.x + w / 2, c.y + h * 0.38f);
}

bool Pet::extFolderHit(V2 cur) const {
    if (!extFolder || extDragging || active != 0) return false;
    D2D1_RECT_F r = folderRect(carriedCenter, S());
    float m = 6.f * S();
    return cur.x >= r.left - m && cur.x <= r.right + m && cur.y >= r.top - m && cur.y <= r.bottom + m;
}

void Pet::extDraw(DrawCtx& c, V2 wo) {
    if (active != 0) return;
    float s = c.s;
    D2D1_COLOR_F ink = D2D1::ColorF(0.10f, 0.07f, 0.14f, 1);
    if (bPoint > 0.5f) {
        int side = (extPointAt - shoulderScreen(1)).len() < (extPointAt - shoulderScreen(2)).len() ? 1 : 2;
        V2 hand = handScreen(side == 2) - wo, dir = (extPointAt - wo - hand).norm(), nrm(-dir.y, dir.x);
        for (int i = 0; i < 2; i++) {
            float ph = fmodf(t * 1.6f + i * 0.5f, 1.f);
            V2 tip = hand + dir * ((12.f + 16.f * ph) * s);
            float al = (bPoint - 0.5f) * 2.f * sinf(ph * PI);
            V2 w1 = tip - dir * (6.f * s) + nrm * (6.f * s), w2 = tip - dir * (6.f * s) - nrm * (6.f * s);
            for (int k = 0; k < 2; k++) {
                c.color(k ? D2D1::ColorF(0.84f, 0.10f, 0.93f, 1) : ink, al);
                float lw = (k ? 2.6f : 5.f) * s;
                c.rt->DrawLine(w1.d2(), tip.d2(), c.br, lw, c.round);
                c.rt->DrawLine(w2.d2(), tip.d2(), c.br, lw, c.round);
            }
        }
    }
    if (!extFolder || extDragging) return;
    D2D1_RECT_F r = folderRect(carriedCenter - wo, s);
    V2 mid((r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f);
    c.rt->SetTransform(M3::Rotation(rot * 180.f / PI + 3.f * sinf(t * 1.6f), (carriedCenter - wo).d2()));
    float w = r.right - r.left, h = r.bottom - r.top, lw = 1.7f * s, rr = 3.f * s;
    D2D1_RECT_F tab = D2D1::RectF(r.left, r.top, r.left + w * 0.42f, r.top + h * 0.3f);
    D2D1_RECT_F back = D2D1::RectF(r.left, r.top + h * 0.14f, r.right, r.bottom);
    D2D1_RECT_F front = D2D1::RectF(r.left, r.top + h * 0.3f, r.right, r.bottom);
    c.color(D2D1::ColorF(0, 0, 0, 1), 0.18f);
    c.rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(back.left + 2 * s, back.top + 3 * s, back.right + 2 * s, back.bottom + 3 * s), rr, rr), c.br);
    for (auto& q : {tab, back}) {
        c.color(D2D1::ColorF(0.90f, 0.66f, 0.18f, 1));
        c.rt->FillRoundedRectangle(D2D1::RoundedRect(q, rr, rr), c.br);
        c.color(ink);
        c.rt->DrawRoundedRectangle(D2D1::RoundedRect(q, rr, rr), c.br, lw);
    }
    c.color(D2D1::ColorF(0.90f, 0.66f, 0.18f, 1));
    c.rt->FillRectangle(D2D1::RectF(tab.left + lw, tab.bottom - rr - lw, tab.right - lw, back.top + rr + lw), c.br);
    c.color(D2D1::ColorF(1.f, 0.83f, 0.38f, 1));
    c.rt->FillRoundedRectangle(D2D1::RoundedRect(front, rr, rr), c.br);
    c.color(ink);
    c.rt->DrawRoundedRectangle(D2D1::RoundedRect(front, rr, rr), c.br, lw);
    V2 k(mid.x, (front.top + front.bottom) * 0.5f);
    float u = h * 0.16f;
    c.color(D2D1::ColorF(0.84f, 0.10f, 0.93f, 1));
    c.rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(k.x - u, k.y - u * 0.8f, k.x + u, k.y + u), u * 0.25f, u * 0.25f), c.br);
    c.rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(k.x, k.y - u * 0.9f), u * 0.42f, u * 0.42f), c.br);
    c.rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(k.x + u * 1.05f, k.y + u * 0.1f), u * 0.42f, u * 0.42f), c.br);
    c.rt->SetTransform(M3::Identity());
}

void Pet::extDragBegin() {
    extDragging = true;
    logf("ext: folder drag started");
}

void Pet::extDragEnd(unsigned long effect) {
    extDragging = false;
    logf("ext: folder drag ended, effect %lu", effect);
    if (effect != DROPEFFECT_NONE) tHappy = 1.2f;
}
