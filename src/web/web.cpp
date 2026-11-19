#include "pet.h"
#include "webnest.h"
#include <ctime>

static WebNest& server(App& a) {
    if (!a.web) a.web = new WebNest();
    return *a.web;
}

static bool browserContent(HWND top, const WebState& ws, RECT& out) {
    RECT wr;
    if (FAILED(DwmGetWindowAttribute(top, DWMWA_EXTENDED_FRAME_BOUNDS, &wr, sizeof(wr)))) GetWindowRect(top, &wr);
    float cw = ws.iw * ws.dpr, ch = ws.ih * ws.dpr;
    if (cw < 50 || ch < 50) return false;
    struct C {
        float cw, ch;
        RECT r;
        bool ok;
    } c{cw, ch, {}, false};
    EnumChildWindows(
        top,
        [](HWND h, LPARAM lp) -> BOOL {
            auto* c = (C*)lp;
            wchar_t cls[64];
            GetClassNameW(h, cls, 64);
            if (wcscmp(cls, L"Chrome_RenderWidgetHostHWND") || !IsWindowVisible(h)) return TRUE;
            RECT r;
            GetWindowRect(h, &r);
            if (fabsf((r.right - r.left) - c->cw) < 6 && fabsf((r.bottom - r.top) - c->ch) < 6) {
                c->r = r;
                c->ok = true;
                return FALSE;
            }
            return TRUE;
        },
        (LPARAM)&c);
    if (c.ok) {
        out = c.r;
        return true;
    }
    float border = std::max(0.f, ((wr.right - wr.left) - cw) * 0.5f);
    out = {(LONG)(wr.left + border), (LONG)(wr.bottom - border - ch), (LONG)(wr.left + border + cw), (LONG)(wr.bottom - border)};
    return true;
}

bool Pet::webActive() const { return webOn || nest == NestWeb; }

void Pet::webGo(bool stroll) {
    if (webActive() || st == St::Inside || st == St::Dive || dragging) return;
    auto& A = app.activity;
    webWnd = A.kind == AppKind::Browser ? A.fg : nullptr;
    answerWeb(true, stroll);
}

void Pet::answerWeb(bool yes, bool stroll) {
    webStroll = yes && stroll;
    if (!yes) {
        tSad = 1.5f;
        sayCat("webNo", 1.f, L"", true);
        return;
    }
    WebNest& W = server(app);
    if (!W.running()) {
        std::wstring dir = appDataDir() + L"\\web";
        CreateDirectoryW(dir.c_str(), nullptr);
        for (int i = 0; i < 4; i++)
            if (app.renderFace) app.renderFace(dir + L"\\face" + std::to_wstring(i) + L".png", i);
        if (!W.start(app.assets, dir)) {
            sayCat("webFail", 1.f, L"", true);
            return;
        }
    }
    W.setName(app.settings.name);
    W.setShy(app.settings.shyPages);
    if (!W.extKnown()) {
        double last = W.extLast();
        if (last < 0) logf("web: extension never heard this run, %s", stroll ? "stroll picks something else" : "stays");
        else logf("web: extension not heard for %.0f s, %s", nowSec() - last, stroll ? "stroll picks something else" : "stays");
        webStroll = false;
        if (!stroll) sayCat("webNoExt", 1.f, L"", true);
        return;
    }
    tHappy = 1.5f;
    sayCat("webYes", 1.f, L"", true);
    if (webWnd) {
        for (auto& s : app.world.surfs)
            if (s.kind == SurfKind::Window && s.hwnd == webWnd) {
                float x = clampf(pos.x, s.x0 + 50.f * S(), s.x1 - 50.f * S());
                if (s.x1 - s.x0 < 100.f * S()) x = (s.x0 + s.x1) * 0.5f;
                if (goJump(V2(x, s.y), 60)) {
                    webDiveOnLand = true;
                    return;
                }
            }
    }
    if (grounded()) diveInto(V2(pos.x - 40.f * S(), pos.y), 80.f * S(), NestWeb, L"");
    else webDiveOnLand = true;
}

void Pet::webServeOnly() {
    WebNest& W = server(app);
    std::wstring dir = appDataDir() + L"\\web";
    CreateDirectoryW(dir.c_str(), nullptr);
    for (int i = 0; i < 4; i++)
        if (app.renderFace) app.renderFace(dir + L"\\face" + std::to_wstring(i) + L".png", i);
    if (W.start(app.assets, dir)) {
        W.setName(app.settings.name);
        W.setShy(app.settings.shyPages);
        logf("web test url: %s", narrow(W.url()).c_str());
        W.resetSeen();
        webOn = true;
        nest = NestWeb;
        webSince = app.now;
        webLife = 1e9;
        webFirst = true;
        webExt = false;
        webVer = -1;
        webAutoT = 1e9f;
        edgeL = V2(pos.x - 40.f * S(), pos.y);
        edgeW = 80.f * S();
        setState(St::Inside);
    }
}

struct Query {
    const wchar_t* q;
    bool img;
};
static const Query kTech[] = {{L"как работает рельсотрон", false},     {L"Murder Drones рабочие дроны вики", false},
                              {L"Murder Drones Медь-9 вики", false},    {L"Murder Drones Абсолютное Разрешение", false},
                              {L"Murder Drones Узи Дверченко вики", false}, {L"экзопланеты фото телескоп", true},
                              {L"что такое шейдер", false},            {L"чёрная дыра простыми словами", false},
                              {L"как работает нейросеть", false},      {L"Юпитер интересные факты", false},
                              {L"сверхпроводимость простыми словами", false}, {L"самый мощный лазер в мире", false},
                              {L"как работает видеокарта", false},     {L"Glitch Productions Murder Drones", false}};
static const Query kCute[] = {{L"милые котики", true},   {L"котята спят", true}, {L"смешные коты", true},
                              {L"как связать шапку бини", false}, {L"Murder Drones Эн вики", false}, {L"милые роботы арт", true}};
static const Query kShy[] = {{L"как понять, что ты нравишься дрону", false}, {L"что подарить парню, если он дрон-убийца", false},
                             {L"почему визор перегревается рядом с кем-то", false}, {L"как перестать краснеть", false},
                             {L"почему мне нравится, когда гладят курсором", false}};
static const Query kPeople[] = {{L"почему люди так долго сидят за компьютером", false}, {L"как понять, что человек устал", false},
                                {L"что делать, если человеку грустно", false},           {L"как сказать человеку спасибо и не сгореть", false},
                                {L"что люди любят больше всего", false},                  {L"сколько людям нужно спать", false},
                                {L"почему рядом с человеком перегревается визор", false}};

static std::string jsonQ(const std::wstring& q) {
    std::string s = narrow(q), o;
    for (char ch : s) {
        if (ch == '"' || ch == '\\') o += '\\';
        o += ch;
    }
    return "\"" + o + "\"";
}

static const Query kAwkward[] = {{L"почему я краснею когда хвалят", false},       {L"фанфики про Узи Дорман", false},
                                 {L"как перестать быть злой", false},             {L"может ли дрон научиться танцевать", false},
                                 {L"10 часов котиков", false},                   {L"как сделать вид что я не плакала", false},
                                 {L"как улыбаться на фото и не выглядеть странно", false}, {L"нормально ли спать с плюшевым котом", false},
                                 {L"как извиниться и не умереть от стыда", false}};

template <size_t N>
static const Query& pickQ(const Query (&a)[N]) { return a[randi(0, (int)N - 1)]; }

static std::string searchCmd(float shyP, int tier, int forceShame = -1) {
    int sh = forceShame >= 0 ? forceShame : chance(shyP) ? 2 : chance(0.3f) ? 1 : 0;
    bool people = tier >= 3 || (tier == 2 && chance(0.5f));
    const Query& q = sh == 2 ? (chance(0.5f) ? pickQ(kAwkward) : people ? pickQ(kPeople) : pickQ(kShy)) : sh == 1 ? pickQ(kCute) : pickQ(kTech);
    return "{\"c\":\"search\",\"q\":" + jsonQ(q.q) + ",\"shame\":" + std::to_string(sh) + ",\"img\":" + (q.img ? "1" : "0") + "}";
}

void Pet::webEntered() {
    WebNest& W = server(app);
    W.resetSeen();
    W.clearState();
    nestShame = false;
    webExt = true;
    webExt0 = W.extLast();
    webPickT = -1;
    const Query& q = pickQ(kTech);
    W.send("{\"c\":\"visit\",\"q\":" + jsonQ(q.q) + ",\"shame\":0,\"img\":" + (q.img ? "1" : "0") + "}");
    logf("web: visit sent to the extension (heard %.0f s ago)", nowSec() - webExt0);
    webOn = true;
    webSince = app.now;
    webLife = randf(6, 14) * 60;
    webFirst = true;
    webShown = false;
    webAloneAll = webHidT = 0;
    webPeeked = webSawShy = false;
    webVer = -1;
    webAutoT = randf(50, 110);
    webShame = 0;
}

float Pet::shyChance() const {
    if (!app.settings.shyPages) return 0.f;
    return 0.04f + 0.32f * clampf(vit.trust, 0.f, 100.f) / 100.f;
}

float Pet::webShyRate(int n) {
    int k = 0;
    for (int i = 0; i < n; i++) k += searchCmd(shyChance(), tier()).find("\"shame\":2") != std::string::npos;
    return n > 0 ? (float)k / n : 0.f;
}

void Pet::trustAdd(float d, const char* why) {
    float was = vit.trust;
    vit.trust = clampf(vit.trust + d, 0.f, 100.f);
    logf("trust %.0f -> %.0f (%s)", was, vit.trust, why);
    vit.save();
}

void Pet::leaveWeb(int why) {
    logf("web: leave (%d)", why);
    if (!webPeeked && webAloneAll >= 30.f && (why == 0 || why == 4)) {
        trustAdd(5.f, "a browser walk nobody peeked at");
        if (webSawShy && chance(0.6f)) webBlurtAt = app.now + randf(45.f, 160.f);
    }
    webAloneAll = webHidT = 0;
    webPeeked = webSawShy = false;
    if (app.web && why != 1) app.web->send("{\"c\":\"leave\"}");
    bool wasShown = webShown;
    webOn = false;
    webShown = false;
    webSy = -1;
    nest = NestNone;
    app.world.web = false;
    app.world.webSurfs.clear();
    app.world.refresh();
    const char* cat = why == 1   ? "webKicked"
                      : why == 2 ? "webDragged"
                      : why == 0 ? (wasShown ? "webDone" : "webBack")
                      : why == 5 ? "webSleepy"
                                 : nullptr;
    if (!wasShown && (st == St::Inside || st == St::Dive)) {
        RECT r;
        if (webWnd && IsWindow(webWnd) && !IsIconic(webWnd) && app.world.windowRect(webWnd, r) && r.top > -20000) {
            float w = (float)(r.right - r.left);
            emergeAt(V2(r.left + w * randf(0.3f, 0.7f) - 40.f * S(), (float)r.top), 80.f * S(), why == 4 ? 2 : 0);
        } else {
            sink = 0;
            comeHere(why == 4 ? tryTarget : V2(pos.x, pos.y));
        }
    } else if (!dragging && st != St::Dragged) {
        startFall(V2(0, -250.f * S()));
    }
    if (cat) sayCat(cat, 1.f, L"", true);
    if (why == 3) {
        shameRun = true;
        shameRunT = 0;
    }
}

bool Pet::webDecide(const Input&) {
    if (!app.world.web || !app.web) return false;
    if (webMoving()) {
        goIdle(0.4f, 0.7f);
        return true;
    }
    float r = randf(0, 1);
    WebState wsd = app.web->state();
    bool results = wsd.mode == "search" || wsd.mode == "wiki" || wsd.mode == "so" || wsd.mode == "cute" || wsd.mode == "shy";
    float pressP = wsd.mode == "search" ? 0.55f : 0.3f;
    if (app.now - webPageT > (wsd.mode == "search" ? 5 : 35) && r < pressP && results) {
        std::vector<const Surf*> bs;
        for (auto& s : app.world.surfs)
            if (s.kind == SurfKind::Web && s.tag == 3) bs.push_back(&s);
        if (!bs.empty()) {
            const Surf* s = bs[randi(0, (int)bs.size() - 1)];
            if (goJump(V2((s->x0 + s->x1) * 0.5f, s->y), 40)) {
                webPress = s->index;
                if (app.now - lastWebSay > 60 && chance(0.4f)) {
                    lastWebSay = app.now;
                    sayCat("webSearchRead", 1.f, L"", true);
                }
                return true;
            }
        }
    }
    if (r > 0.55f && r < 0.8f && webStartRide()) return true;
    WebState ws0 = app.web->state();
    if (webExt && ws0.mode == "site" && r > 0.85f) {
        app.web->send("{\"c\":\"back\"}");
        sayCat("webBackPage", 0.5f, L"", true);
        return true;
    }
    if (webExt && ws0.mode == "search" && r > 0.9f) {
        app.web->send(searchCmd(0.f, tier()));
        sayCat("webSearch", 0.6f, L"", true);
        return true;
    }
    if (r < 0.62f) {
        webScroll(randi(260, 480));
        squashV -= 2.f;
        setState(St::Land, 0.5f);
        return true;
    }
    if (r < 0.7f && app.now - lastWebSay > 120) {
        lastWebSay = app.now;
        sayCat(wsd.mode == "so" ? "webSo" : wsd.shame == 1 ? "webCuteRead" : "webWiki", 0.7f, wsd.topic, true);
    }
    return false;
}

void Pet::webScroll(int dy) {
    if (!app.web) return;
    app.web->send("{\"c\":\"scroll\",\"dy\":" + std::to_string(dy) + "}");
    webScrollAt = app.now + 0.6;
}

bool Pet::webMoving() const { return app.world.web && app.now - webScrollAt < 0.45; }

bool Pet::webStartRide() {
    if (!app.web || !app.world.web) return false;
    WebState ws = app.web->state();
    if (!(ws.cw > 0 && ws.iw - ws.cw >= 6 && ws.sh > ws.ih + 40 && ws.sy + ws.ih < ws.sh - 20)) return false;
    rideFrom = com();
    rideSent = false;
    rideMovedT = 0;
    rideLastY = -1;
    phase = 0;
    phaseT = 0;
    setState(St::Ride);
    return true;
}

bool Pet::webRide(float dt) {
    if (!app.web || !app.world.web) {
        startFall({});
        return true;
    }
    WebState ws = app.web->state();
    const RECT& rc = app.world.webRc;
    float k = (rc.right - rc.left) / std::max(1.f, ws.iw);
    float sbw = (ws.iw - ws.cw) * k;
    float track = ws.ih * k;
    float thumbH = std::max(24.f * S(), track * ws.ih / std::max(ws.sh, 1.f));
    float thumbY = rc.top + (track - thumbH) * clampf(ws.sy / std::max(1.f, ws.sh - ws.ih), 0.f, 1.f);
    facing = 1;
    float barL = rc.right - sbw;
    rideHand = V2(barL + sbw * 0.5f, thumbY + std::min(thumbH * 0.5f, 18.f * S()));
    const Rig& R0 = app.rig;
    V2 want = rideHand - raisedHand(2);
    if (iArmR >= 0 && iFArmR >= 0) {
        float L = (R0.parts[iFArmR].joint - R0.parts[iArmR].joint).len() + (R0.parts[iFArmR].hand() - R0.parts[iFArmR].joint).len();
        V2 j1 = R0.parts[iArmR].joint, j2 = R0.parts[iArmL >= 0 ? iArmL : iArmR].joint;
        V2 sh = j1.x > j2.x ? j1 : j2;
        float feetX = barL - headR * 1.15f - 3.f * S();
        float dx = rideHand.x - (feetX + sh.x);
        float reach = L * 0.9f;
        if (dx > reach) {
            feetX += dx - reach;
            dx = reach;
        }
        float dy = sqrtf(std::max(0.f, reach * reach - dx * dx));
        float headUp = std::max(0.f, standTop + sh.y);
        float lowest = thumbY + std::max(thumbH - 8.f * S(), 0.f);
        rideHand.y = clampf(std::max(rideHand.y, rc.top + 4.f * S() + headUp - dy), rideHand.y, std::max(rideHand.y, lowest));
        V2 feet(feetX, rideHand.y + dy - sh.y);
        feet.y = std::min(feet.y, (float)rc.bottom - 2.f);
        want = feet - V2(0, comH);
    }
    phaseT += dt;
    rot = 0;
    vel = {};
    if (phase == 0) {
        float u = smooth01(phaseT / 0.45f);
        V2 c = rideFrom + (want - rideFrom) * u - V2(0, sinf(u * PI) * 60.f * S());
        pos = c + V2(0, comH);
        if (phaseT >= 0.45f) { phase = 1; phaseT = 0; squashV -= 1.5f; }
        return true;
    }
    pos = want + V2(0, comH);
    if (!rideSent && phaseT > 0.25f) {
        rideSent = true;
        webScroll((int)(ws.ih * randf(0.55f, 0.8f)));
        logf("web ride: hangs left of the scrollbar: feet %.0f,%.0f, com x %.0f, head top %.0f, hand %.0f,%.0f, bar %.0f..%ld, page %ld..%ld x %ld..%ld",
             pos.x, pos.y, com().x, headTop.y, rideHand.x, rideHand.y, rc.right - sbw, rc.right, rc.left, rc.right, rc.top, rc.bottom);
    }
    if (rideLastY >= 0 && fabsf(thumbY - rideLastY) > 0.5f) rideMovedT = 0.01f;
    else if (rideMovedT > 0) rideMovedT += dt;
    rideLastY = thumbY;
    if ((rideMovedT > 0.5f) || phaseT > 3.5f) {
        startFall(V2(-60.f * S(), 0));
    }
    return true;
}

void Pet::webUpdate(float dt, const Input& in) {
    auto& A = app.activity;
    tBlush -= dt;
    if (webBlurtAt > 0 && app.now > webBlurtAt) {
        if (app.now > webBlurtAt + 600) webBlurtAt = -1;
        else if (!webActive() && (st == St::Idle || st == St::Sit) && !dragging && !app.bubble.active && heldPath.empty() && carriedName.empty()) {
            webBlurtAt = -1;
            tBlush = 3.f;
            sayCat("webBlurt", 1.f, L"", true);
            logf("web: blurted that she searched nothing like that");
        }
    }
    if (!webOn && nest == NestNone && A.kind == AppKind::Browser && app.web && app.web->extKnown() && app.now - A.since > 25 &&
        st == St::Idle && grounded() && !dragging && !petting && carriedName.empty() && heldPath.empty() && !app.bubble.asking() && !tryMode && in.idleSec < 30) {
        long long u = (long long)time(nullptr);
        if (u - app.settings.lastWebAsk > 50 * 60 && chance(dt / 90.f)) {
            app.settings.lastWebAsk = u;
            app.settings.save();
            webWnd = A.fg;
            ask("webAsk", "choiceBrowser", "browser");
        }
    }
    if (shameRun && !webActive()) {
        shameRunT += dt;
        if (grounded() && !dragging && shameRunT > 0.4f) {
            shameRun = false;
            nestShame = true;
            std::vector<int> fs;
            for (size_t i = 0; i < app.icons.items.size(); i++)
                if (app.icons.items[i].folder && app.world.rectVisible(app.icons.items[i].img)) fs.push_back((int)i);
            bool jumped = false;
            if (!fs.empty() && app.settings.icons && app.iconsReady) {
                int k = fs[randi(0, (int)fs.size() - 1)];
                const RECT& im = app.icons.items[k].img;
                if (goJump(V2((im.left + im.right) * 0.5f, (float)im.top), 40, 90)) {
                    nestIcon = app.icons.items[k].name;
                    jumped = true;
                }
            }
            if (!jumped && gnd.valid) diveInto(V2(pos.x - 40.f * S(), pos.y), 80.f * S(), NestSystem, L"");
        }
        if (shameRunT > 8) shameRun = false;
    }
    if (!webOn || !app.web) return;
    WebNest& W = *app.web;
    WebState ws = W.state();
    double seen = W.lastSeen();
    if (webExt && !ws.any) {
        if (webPickT < 0 && W.extLast() > webExt0) webPickT = app.now;
        if (webPickT < 0 ? app.now - webSince > 35 : app.now - webPickT > 20) {
            logf("web: no tab after %.0f s (extension %s), back out", app.now - webSince,
                 webPickT < 0 ? "never took the visit" : "took the visit, tab silent");
            W.dropCmds();
            leaveWeb(5);
            return;
        }
    }
    if (ws.closed || (seen > 0 && nowSec() - seen > 180) || (seen == 0 && app.now - webSince > 45)) {
        leaveWeb(1);
        return;
    }
    HWND fg = GetForegroundWindow();
    wchar_t tt[200] = L"";
    if (fg) GetWindowTextW(fg, tt, 200);
    bool shown = ws.any && ws.visible && !wcsncmp(tt, L"Узи · ", 6) && fg && !IsIconic(fg) && !dragging;
    RECT rc{};
    if (webFake) {
        shown = ws.any && ws.visible && !dragging;
        rc = webFakeRc;
        fg = nullptr;
    } else if (shown && !browserContent(fg, ws, rc)) shown = false;
    auto& Wd = app.world;
    if (shown) {
        if (fg) webWnd = fg;
        webTitle = ws.title;
        Wd.webRc = rc;
        if (ws.ver != webVer || !webShown) {
            if (webSy >= 0 && fabsf(ws.sy - webSy) > 0.5f) {
                webScrollAt = std::max(webScrollAt, app.now);
                if (st == St::PrepJump) {
                    logf("web: the page scrolled %.0f px under a planned jump, cancelled", ws.sy - webSy);
                    goIdle(0.3f, 0.6f);
                }
            }
            webSy = ws.sy;
            webVer = ws.ver;
            float k = (rc.right - rc.left) / std::max(1.f, ws.iw);
            float room = std::max(standTop, comH * 2.f) + 6.f;
            Wd.webSurfs.clear();
            for (auto& e : ws.els) {
                float y = rc.top + e.y * k, x0 = rc.left + e.x * k, x1 = x0 + e.w * k;
                if (y < rc.top + room || y > rc.bottom - 20) continue;
                Surf s;
                s.kind = SurfKind::Web;
                s.index = e.id;
                s.tag = e.type;
                s.y = y;
                s.x0 = std::max(x0 + 4.f, (float)rc.left + 30.f * S());
                s.x1 = std::min(x1 - 4.f, (float)rc.right - 30.f * S());
                for (auto& sp : Wd.screenSpans(s.y - 2.f, s.x0, s.x1))
                    if (sp.second - sp.first > 30.f * S()) {
                        s.x0 = sp.first;
                        s.x1 = sp.second;
                        Wd.webSurfs.push_back(s);
                    }
            }
            if (ws.mode != "") webPageT = webPageT > 0 && ws.topic == lastTopic ? webPageT : app.now;
            lastTopic = ws.topic;
            Wd.web = true;
            Wd.refresh();
        }
    }
    bool peek = false;
    if (shown && !webShown && webHidT >= 15.f) {
        peek = webStroll && !webPeeked;
        webPeeked = true;
        if (peek) {
            logf("web: he opened her tab after %.0f s alone (page shame %d): peeked", webHidT, ws.shame);
            trustAdd(ws.shame >= 2 ? -20.f : -12.f, "peeked");
        } else {
            logf("web: he looked in after %.0f s alone (%s), not a peek", webHidT, webStroll ? "already caught this walk" : "he sent her");
        }
    }
    if (shown && !webShown) {
        webShown = true;
        webShame = ws.shame;
        if (st == St::Inside || st == St::Dive) {
            selectRig(0);
            sink = 0;
            rot = rotVel = 0;
            app.inst.resetSim();
            const Surf* back = nullptr;
            if (webAnchor != -2 && ws.title == webAnchorTitle)
                for (auto& s : Wd.surfs)
                    if (s.kind == SurfKind::Web && s.index == webAnchor) back = &s;
            if (back) {
                setGround(*back);
                pos = V2(clampf(back->x0 + webAnchorDx, back->x0, back->x1), back->y);
                setState(St::Idle, randf(1.f, 2.f));
            } else {
                pos = V2(rc.left + (rc.right - rc.left) * randf(0.3f, 0.7f), rc.top + std::max(standTop, comH * 2.f) + 10.f);
                startFall({});
            }
        }
        if (ws.shame >= 2) {
            tBlush = 6.f;
            tAngry = 3.f;
            tSurprise = 0.6f;
            sayCat("webShy", 1.f, L"", true);
            W.send(webExt ? "{\"c\":\"hide\",\"q\":" + jsonQ(L"рельсотрон принцип работы") + "}" : "{\"c\":\"hide\"}");
            emit(2, headTop, V2(0, -60.f * S()), 0.8f, 1.2f);
            leaveWeb(3);
            return;
        } else if (peek) {
            tAngry = 1.8f;
            tBlush = 2.5f;
            tSurprise = 0.6f;
            sayCat("webPeek", 1.f, L"", true);
            lastWebSay = app.now;
        } else if (ws.shame == 1) {
            tBlush = 3.5f;
            sayCat("webCute", 1.f, L"", true);
        } else if (webFirst) {
            sayCat("webHere", 1.f, L"", true);
            lastWebSay = app.now;
        } else if (app.now - lastWebSay > 90 && chance(0.35f)) {
            lastWebSay = app.now;
            sayCat("webShow", 1.f, ws.topic, true);
        }
        webFirst = false;
    } else if (!shown && webShown) {
        webAnchor = -2;
        if (gnd.valid && gnd.kind == SurfKind::Web) {
            webAnchor = gnd.index;
            webAnchorDx = pos.x - gnd.x0;
            webAnchorTitle = webTitle;
        }
        webShown = false;
        webSy = -1;
        Wd.web = false;
        Wd.webSurfs.clear();
        Wd.refresh();
        if (!dragging) {
            edgeL = V2(pos.x - 40.f * S(), pos.y);
            edgeW = 80.f * S();
            gnd.valid = false;
            setState(St::Inside);
        }
    }
    if (!webShown) {
        webHidT += dt;
        webAloneAll += dt;
        if (ws.any && ws.shame >= 2) webSawShy = true;
        webAutoT -= dt;
        if (webAutoT <= 0) {
            if (webExt) {
                webAutoT = randf(25, 60);
                W.send(++webAutoN % 4 == 0 ? searchCmd(shyChance(), tier()) : "{\"c\":\"auto\"}");
            } else {
                webAutoT = randf(50, 120);
                const char* k = chance(shyChance()) ? "shy" : chance(0.3f) ? "cute" : chance(0.5f) ? "wiki" : "so";
                W.send(chance(0.55f) ? std::string("{\"c\":\"open\",\"k\":\"") + k + "\"}" : std::string("{\"c\":\"scroll\",\"dy\":500}"));
            }
        }
    } else {
        webHidT = 0;
    }
    if (app.now - webSince > webLife && !dragging && (st == St::Inside || grounded())) leaveWeb(0);
}
