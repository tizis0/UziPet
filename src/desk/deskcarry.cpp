#include "pet.h"
#include <restartmanager.h>
#include <thread>
#pragma comment(lib, "rstrtmgr.lib")

static V2 rectMid(const RECT& r) { return V2((r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f); }

static V2 imgMid(const DesktopIcons& I, POINT cell) {
    RECT r = I.cellAt(cell);
    return V2((r.left + r.right) * 0.5f, r.top + I.imgOff.y + I.iconPx * 0.5f);
}

static std::wstring baseName(const std::wstring& p) {
    size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? p : p.substr(s + 1);
}

std::wstring Pet::deskSelf() const { return deskOn() && deskRec.hands.empty() ? heldPath : std::wstring(); }

bool Pet::deskHides(const DIcon& ic) const {
    if (!deskOn() || !deskRec.hands.empty()) return false;
    if (!ic.path.empty() && !heldPath.empty()) return _wcsicmp(ic.path.c_str(), heldPath.c_str()) == 0;
    return ic.name == deskName;
}

int Pet::deskIndex() {
    std::wstring s = deskSelf();
    return s.empty() ? -1 : app.icons.findPath(s);
}

bool Pet::cellTaken(const RECT& cell, int skip) const {
    RECT a{cell.left + 2, cell.top + 2, cell.right - 2, cell.bottom - 2}, x;
    const auto& I = app.icons;
    for (size_t i = 0; i < I.items.size(); i++) {
        if ((int)i == skip || deskHides(I.items[i])) continue;
        const RECT& c = I.items[i].cell;
        RECT b{c.left + 2, c.top + 2, c.right - 2, c.bottom - 2};
        if (IntersectRect(&x, &a, &b)) return true;
    }
    return false;
}

bool Pet::freeCellNear(V2 at, POINT& out, int) { return app.icons.nearestFree(at, deskSelf(), out); }

bool Pet::cellShown(POINT cell) const { return app.icons.cellOnScreen(cell); }

void Pet::deskDisplayChanged() {
    if (!deskOn()) return;
    if ((deskGoal == GoalHome || deskGoal == GoalFree) && !cellShown(deskTo)) logf("desk carry: monitors changed, target %ld,%ld is off screen now", deskTo.x, deskTo.y);
}

bool Pet::deskStart(const std::wstring& path) {
    auto& I = app.icons;
    deskBusy = false;
    if (!app.settings.icons || !I.ok() || baby() || vit.intro || app.world.web || !I.isDesktopFile(path)) return false;
    if (I.autoArrange()) {
        logf("desk carry: auto-arrange is on, '%s' stays where it is", narrow(path).c_str());
        return false;
    }
    std::string why;
    if (!I.canTake(path, &why)) {
        logf("desk carry: '%s' can't go into her hands (%s), its icon stays where it is", narrow(path).c_str(), why.c_str());
        return false;
    }
    I.layoutSnap(app.now, "carry start", true);
    I.refreshIfOlder(0.2);
    int i = I.findPath(path);
    if (i < 0) {
        logf("desk carry: '%s' is not among the desktop icons", narrow(path).c_str());
        return false;
    }
    const DIcon& ic = I.items[i];
    deskName = ic.name;
    deskHome = deskTo = ic.pos;
    deskOffered = false;
    deskFlyOn = false;
    deskBad.clear();
    deskGoal = GoalHome;
    deskFolder.clear();
    if (!I.deselect(ic)) logf("desk carry: could not deselect '%s'", narrow(deskName).c_str());
    RECT cell = ic.cell;
    deskOwnHome = I.rememberHome(ic);
    deskRec = HandsRec{};
    deskRec.path = path;
    deskRec.name = deskName;
    deskRec.home = deskHome;
    deskRec.ownHome = deskOwnHome;
    if (!I.take(deskRec)) {
        if (deskOwnHome) I.forgetHome(deskName);
        deskOwnHome = false;
        deskRec = HandsRec{};
        deskBusy = I.takeErr == ERROR_SHARING_VIOLATION || (I.takeErr == ERROR_ACCESS_DENIED && !I.takeRights) || I.takeErr == ERROR_LOCK_VIOLATION;
        logf("desk carry: '%s' did not go into her hands%s, its icon stays where it is", narrow(path).c_str(), deskBusy ? " (busy)" : "");
        return false;
    }
    I.layoutActed();
    deskMode = DeskHold;
    deskT = deskAll = deskCheckT = 0;
    deskThrowT = -1;
    deskLegs = 0;
    logf("desk carry: took '%s' from cell %ld,%ld (screen %ld,%ld), the file is in her hands, the cell is free", narrow(deskName).c_str(), deskHome.x,
         deskHome.y, cell.left, cell.top);
    return true;
}

void Pet::deskEnd(bool keep) {
    auto& I = app.icons;
    if (deskOwnHome && !keep) I.forgetHome(deskName);
    deskOwnHome = false;
    deskMode = DeskNone;
    peekOn = false;
    deskThrowT = -1;
    deskOffered = false;
    deskFlyOn = false;
    tripArmed = deskDropped = tripPlanned = false;
    tripFlyT = tripLiftT = -1;
    tripPh = 0;
    if (st == St::Lift) goIdle(1.f, 2.f);
}

bool Pet::deskRelease(POINT cell, const char* why, bool endCarry) {
    auto& I = app.icons;
    std::wstring now;
    senseLetGo();
    if (!I.giveBack(deskRec, &now)) {
        logf("desk carry (%s): '%s' is still in her hands, the restore record stays", why, narrow(deskRec.path).c_str());
        return false;
    }
    if (!now.empty()) heldPath = now;
    if (heldCorner && !now.empty()) cornerMoved(now);
    if (now.empty() || !I.isDesktopFile(now)) {
        if (endCarry) I.dropRecord(deskRec.path);
        return true;
    }
    if (!endCarry) heldPath = now;
    I.placeAfter(now, cell, why, deskRec.path, endCarry);
    return true;
}

void Pet::heldClear() {
    heldPath.clear();
    heldName.clear();
    heldImg.reset();
    heldImg.src.Reset();
    heldHalo.reset();
    heldHalo.src.Reset();
    heldLay.Reset();
    heldLabW = heldLabH = 0;
    heldDragging = heldRetake = false;
    heldCorner = false;
}

void Pet::dropHeld(const char* why) {
    if (deskOn()) deskHomeNow(why);
    heldClear();
}

void Pet::deskCrashHome() {
    if (deskOn()) app.icons.crashGiveBack(deskRec);
    if (!carriedName.empty()) app.icons.crashGiveBack(fetchRec);
}

void Pet::deskRetryHands() {
    if (app.now < hidRetryAt) return;
    hidRetryAt = app.now + 5.0;
    std::vector<std::wstring> busy;
    for (const HandsRec* r : {deskOn() ? &deskRec : nullptr, carriedName.empty() ? nullptr : &fetchRec}) {
        if (!r) continue;
        busy.push_back(r->path);
        if (!r->hands.empty()) busy.push_back(r->hands);
    }
    app.icons.retryHands(busy);
}

void Pet::deskHomeNow(const char* why) {
    if (!deskOn()) return;
    bool ok = deskRelease(deskHome, why);
    logf("desk carry ended (%s): '%s' %s, its icon goes home to %ld,%ld", why, narrow(deskName).c_str(), ok ? "on the desktop again" : "STILL IN HER HANDS",
         deskHome.x, deskHome.y);
    deskEnd(!ok);
    heldClear();
}

void Pet::deskReturn() {
    deskMode = DeskGo;
    deskGoal = GoalHome;
    deskTo = deskHome;
    deskT = deskAll = 0;
    deskLegs = 0;
    deskBad.clear();
    tripArmed = !baby() && (tripForce > 0 || chance(1.f / 6.f));
    tripPlanned = false;
    if (tripArmed) logf("desk return: will stumble on the way%s", tripForce ? " (forced)" : "");
    auto& I = app.icons;
    if (!I.fake) I.refresh();
    if (cellTaken(I.cellAt(deskHome), -1)) {
        deskT = 1.f;
        logf("desk return: home %ld,%ld already taken, straight to holding it out", deskHome.x, deskHome.y);
        return;
    }
    bool knew = heldSense.self > 0 || heldSense.kind != FileKind::None;
    if (!knew) tSad = 0.8f;
    if (!peekOn) sayCat((knew ? "fileKeepBack" : "fileConfused"), 1.f, L"", true);
    logf("desk return: '%s' to home %ld,%ld (%s)", narrow(deskName).c_str(), deskHome.x, deskHome.y, knew ? "recognised" : "confused");
}

bool Pet::deskPickFolder() {
    auto& I = app.icons;
    int me = deskIndex(), best = -1;
    float bd = 1e30f;
    for (size_t i = 0; i < I.items.size(); i++) {
        const DIcon& d = I.items[i];
        if (heldCorner) return false;
        if ((int)i == me || !d.folder || d.path.empty() || _wcsicmp(d.path.c_str(), heldPath.c_str()) == 0) continue;
        std::wstring ext = d.path.size() > 4 ? d.path.substr(d.path.size() - 4) : L"";
        if (!_wcsicmp(ext.c_str(), L".lnk") || !_wcsicmp(ext.c_str(), L".zip") || !_wcsicmp(ext.c_str(), L".url")) continue;
        if (d.name.rfind(L"Узи Дорман", 0) == 0) continue;
        if (_wcsnicmp(d.path.c_str(), (heldPath + L"\\").c_str(), heldPath.size() + 1) == 0) continue;
        DWORD at = GetFileAttributesW(d.path.c_str());
        if (at == INVALID_FILE_ATTRIBUTES || !(at & FILE_ATTRIBUTE_DIRECTORY)) continue;
        float dd = (rectMid(d.img) - pos).len();
        if (dd < bd) {
            bd = dd;
            best = (int)i;
        }
    }
    if (best < 0) return false;
    deskGoal = GoalFolder;
    deskFolder = I.items[best].path;
    deskTo = I.items[best].pos;
    return true;
}

bool Pet::deskMoveToFolder() {
    senseLetGo();
    bool ok = app.icons.toFolder(deskRec, deskFolder, app.hwnd);
    logf("desk folder: '%s' into '%s': %d", narrow(deskName).c_str(), narrow(deskFolder).c_str(), (int)ok);
    return ok;
}

void Pet::deskPlan() {
    auto& I = app.icons;
    auto& W = app.world;
    double t0 = nowSec();
    I.refreshIfOlder(1.5);
    if ((deskGoal == GoalHome || deskGoal == GoalFree) && !cellShown(deskTo)) {
        POINT fc;
        if (I.resolveCell(deskTo, deskSelf(), fc) && (fc.x != deskTo.x || fc.y != deskTo.y)) {
            logf("desk plan: cell %ld,%ld is off every monitor, using %ld,%ld", deskTo.x, deskTo.y, fc.x, fc.y);
            deskGoal = GoalFree;
            deskTo = fc;
        }
    }
    if (deskGoal == GoalHome && cellTaken(I.cellAt(deskHome), -1)) {
        deskMode = DeskOffer;
        if (!deskOffered) {
            deskOffered = true;
            deskT = 0;
            tAngry = 0.7f;
            sayCat("fileHomeTaken", 1.f, L"", true);
            logf("desk offer: home %ld,%ld is taken, holding it out", deskHome.x, deskHome.y);
        }
        setState(St::Lift, 1e9f);
        return;
    }
    if (deskGoal == GoalFree && cellTaken(I.cellAt(deskTo), -1)) {
        POINT fc;
        if (freeCellNear(imgMid(I, deskTo), fc, -1)) deskTo = fc;
        else if (!deskPickFolder()) deskGoal = GoalFeet;
        logf("desk plan: target taken meanwhile, now goal %d cell %ld,%ld", deskGoal, deskTo.x, deskTo.y);
    }
    if (deskGoal == GoalFolder && I.findPath(deskFolder) < 0 && !deskPickFolder()) deskGoal = GoalFeet;
    if (deskGoal == GoalFeet) {
        deskPlace();
        return;
    }
    V2 tc = imgMid(I, deskTo);
    RECT cell = I.cellAt(deskTo);
    float hy = deskHandOff.y < -1.f ? deskHandOff.y : -comH * 1.1f;
    float side = std::min(I.spacing.x * 0.5f + 34.f * S(), I.spacing.x * 1.f);
    auto close = [&](float x, float y) {
        return fabsf(x - tc.x) <= I.spacing.x * 1.05f && y >= cell.top - 4.f && y <= cell.bottom + I.spacing.y * 0.5f;
    };
    auto margin = [&](const Surf& s) { return s.kind == SurfKind::Icon ? 2.f : 10.f * S(); };
    struct Spot { float x = 0, d = 1e30f, hands = 1e30f; bool ok = false; };
    auto spot = [&](const Surf& s) {
        Spot b;
        float m = margin(s), x0 = s.x0 + m, x1 = s.x1 - m;
        if (x1 < x0) x0 = x1 = (s.x0 + s.x1) * 0.5f;
        for (float dx : {-side, side, 0.f}) {
            if (dx == 0.f && tc.y > s.y - standTop * 1.1f) continue;
            float x = clampf(tc.x + dx, x0, x1);
            if (x >= cell.left && x <= cell.right && s.y >= cell.top - 4.f && s.y < cell.bottom - 4.f) continue;
            bool ok = close(x, s.y);
            float hands = (V2(x, s.y + hy) - tc).len();
            float d = hands + fabsf(x - pos.x) * 0.05f + (ok ? 0.f : 1e4f);
            if (d < b.d) b = Spot{x, d, hands, ok};
        }
        return b;
    };
    std::vector<Surf> nodes;
    Surf here{};
    here.kind = gnd.kind;
    here.hwnd = gnd.hwnd;
    here.index = gnd.index;
    here.y = gnd.valid ? gnd.y : pos.y;
    here.x0 = gnd.valid ? gnd.x0 : pos.x;
    here.x1 = gnd.valid ? gnd.x1 : pos.x;
    nodes.push_back(here);
    for (auto& s : W.surfs) {
        if (s.kind == SurfKind::Web || onSurf(s) || surfSkip(s)) continue;
        if (W.monitorAt(V2((s.x0 + s.x1) * 0.5f, s.y - 1)) < 0) continue;
        bool bad = false;
        for (auto& b : deskBad) bad |= fabsf(b.x - s.y) < 2 && fabsf(b.y - s.x0) < 2;
        if (!bad) nodes.push_back(s);
    }
    float rise = std::max(standTop * 2.2f, 300.f * S()), reachX = 600.f * S();
    std::vector<int> legs(nodes.size(), -1), from(nodes.size(), -1), q{0};
    legs[0] = 0;
    for (size_t h = 0; h < q.size(); h++) {
        int a = q[h];
        if (legs[a] >= 8) continue;
        const Surf& A = nodes[a];
        for (size_t b = 1; b < nodes.size(); b++) {
            if (legs[b] >= 0) continue;
            const Surf& B = nodes[b];
            if (A.y - B.y > rise || std::max(A.x0, B.x0) - std::min(A.x1, B.x1) > reachX) continue;
            legs[b] = legs[a] + 1;
            from[b] = a;
            q.push_back((int)b);
        }
    }
    int best = 0, closeAll = 0;
    Spot bs = spot(nodes[0]);
    float bestScore = bs.d;
    for (size_t k = 0; k < nodes.size(); k++) {
        Spot sp = k ? spot(nodes[k]) : bs;
        closeAll += sp.ok;
        if (k == 0 || legs[k] < 0) continue;
        float score = sp.d + legs[k] * 120.f * S();
        if (score < bestScore) {
            bestScore = score;
            best = (int)k;
            bs = sp;
        }
    }
    double ms = (nowSec() - t0) * 1000.0;
    if (ms > 3.0) logf("desk plan: %zu surfaces searched in %.1f ms", nodes.size(), ms);
    auto whyFar = [&]() -> std::string {
        char b[200];
        if (!closeAll) snprintf(b, sizeof(b), "nothing to stand on within a cell of it (its column or the next, its row or the one below)");
        else snprintf(b, sizeof(b), "%d spots close to it, none reachable from here (jump up %.0f px, across %.0f px)", closeAll, rise, reachX);
        return b;
    };
    if (best == 0) {
        if (fabsf(bs.x - pos.x) > 8.f * S()) {
            logf("desk plan: walk to %.0f (hands to cell %.0f px, close %d)", bs.x, bs.hands, (int)bs.ok);
            goWalk(bs.x);
            deskArmWalk();
            return;
        }
        if (!bs.ok) logf("desk plan: can't get closer: %s; placing from %.0f,%.0f (hands to cell %.0f px)", whyFar().c_str(), pos.x, pos.y, bs.hands);
        deskPlace();
        return;
    }
    if (deskLegs >= 14 || deskAll > 40.f) {
        logf("desk plan: giving up the path (legs %d, %.0f s), placing from here (hands to cell %.0f px)", deskLegs, deskAll, (V2(pos.x, pos.y + hy) - tc).len());
        deskPlace();
        return;
    }
    if (!bs.ok) logf("desk plan: can't get close: %s; best reachable spot hands %.0f px", whyFar().c_str(), bs.hands);
    int nx = best, after = -1;
    while (from[nx] != 0) {
        after = nx;
        nx = from[nx];
    }
    const Surf& N = nodes[nx];
    float m = margin(N), landX = bs.x;
    if (nx != best) {
        const Surf& F = nodes[after];
        landX = clampf((std::max(N.x0, F.x0) + std::min(N.x1, F.x1)) * 0.5f, N.x0 + m, std::max(N.x0 + m, N.x1 - m));
    }
    float m0 = margin(here);
    if (gnd.valid && fabsf(landX - pos.x) > reachX) {
        float tx = clampf(landX, here.x0 + m0, std::max(here.x0 + m0, here.x1 - m0));
        if (fabsf(tx - pos.x) > 40.f * S()) {
            logf("desk plan: walking to %.0f to jump from there", tx);
            goWalk(tx);
            deskArmWalk();
            return;
        }
    }
    if (tripArmed && gnd.valid && (gnd.kind == SurfKind::Floor || gnd.kind == SurfKind::Window) && fabsf(landX - pos.x) > 260.f * S()) {
        float x = clampf(landX - signf(landX - pos.x) * 160.f * S(), gnd.x0 + 10.f * S(), gnd.x1 - 10.f * S());
        if (fabsf(x - pos.x) > 80.f * S()) {
            logf("desk plan: walking to %.0f before the jump", x);
            goWalk(x);
            deskArmWalk();
            return;
        }
    }
    for (float x : {landX, (N.x0 + N.x1) * 0.5f, N.x0 + m, N.x1 - m}) {
        if (!goJump(V2(x, N.y), 40, 90)) continue;
        deskLegs++;
        logf("desk plan: jump to surface %d y=%.0f x=%.0f, %d jump(s) to the spot (hands to cell %.0f px there, close %d, from here %.0f)", (int)N.kind,
             N.y, x, legs[best], bs.hands, (int)bs.ok, (V2(pos.x, pos.y + hy) - tc).len());
        return;
    }
    deskBad.push_back(V2(N.y, N.x0));
    logf("desk plan: no jump arc onto surface %d y=%.0f x=%.0f..%.0f, looking for another way", (int)N.kind, N.y, N.x0, N.x1);
    if (deskBad.size() < 16) deskPlan();
    else deskPlace();
}

void Pet::deskPlace() {
    auto& I = app.icons;
    if (deskGoal == GoalFeet) {
        float labelH = (float)(I.spacing.y - I.iconPx - I.imgOff.y);
        float gy = gnd.valid ? gnd.y : pos.y;
        deskTo = I.clientForImgCenter(V2(pos.x + facing * 46.f * S(), gy - labelH - I.iconPx * 0.5f - 2.f));
    }
    if (deskGoal != GoalFolder) {
        POINT fc;
        if (I.resolveCell(deskTo, deskSelf(), fc)) {
            if (fc.x != deskTo.x || fc.y != deskTo.y) logf("desk place: cell %ld,%ld -> %ld,%ld (grid, on screen, free)", deskTo.x, deskTo.y, fc.x, fc.y);
            deskTo = fc;
        } else if (!deskPickFolder()) {
            std::wstring now;
            senseLetGo();
            bool ok = I.giveBack(deskRec, &now);
            if (ok) I.dropRecord(deskRec.path);
            logf("desk place: no free cell on any monitor and no folder, '%s' is on the desktop again where explorer puts it (%d)", narrow(deskName).c_str(), (int)ok);
            deskEnd(!ok);
            heldClear();
            sayCat("fileNoRoom", 1.f, L"", true);
            return;
        }
    }
    V2 tc = imgMid(I, deskTo);
    if (fabsf(tc.x - pos.x) > 4.f) facing = tc.x > pos.x ? 1 : -1;
    deskMode = DeskPlace;
    deskThrowT = -1;
    setState(St::DropIcon, 0.75f);
    logf("desk place: goal %d from %.0f,%.0f to cell %ld,%ld (hands to cell %.0f px)", deskGoal, pos.x, pos.y, deskTo.x, deskTo.y,
         (carriedCenter - tc).len());
}

void Pet::deskFinish() {
    auto& I = app.icons;
    deskFlyOn = false;
    bool wasPeek = peekOn;
    V2 tc = imgMid(I, deskTo);
    const char* cat = deskGoal == GoalFeet ? "fileNoRoom" : deskGoal == GoalFree ? "fileNear" : "fileReturned";
    std::wstring arg;
    bool ok;
    if (deskGoal == GoalFolder) {
        if (deskMoveToFolder()) {
            cat = "fileToFolder";
            arg = baseName(deskFolder);
            ok = true;
        } else {
            float labelH = (float)(I.spacing.y - I.iconPx - I.imgOff.y);
            float gy = gnd.valid ? gnd.y : pos.y;
            POINT want = I.clientForImgCenter(V2(pos.x + facing * 46.f * S(), gy - labelH - I.iconPx * 0.5f - 2.f));
            deskGoal = GoalFeet;
            cat = "fileNoRoom";
            logf("desk folder: move failed, putting it down near her feet");
            ok = deskRelease(want, "folder move failed");
        }
    } else {
        ok = deskRelease(deskTo, deskGoal == GoalHome ? "returned home" : "placed");
    }
    if (!ok) {
        logf("desk placed: '%s' could not be given back, keeping the restore record", narrow(deskName).c_str());
        deskEnd(true);
        heldClear();
        return;
    }
    for (int i = 0; i < 6; i++) emit(4, tc + V2(randf(-14, 14) * S(), randf(-6, 6) * S()), V2(randf(-70, 70) * S(), randf(-60, 10) * S()), 0.6f, 1.f);
    logf("desk placed '%s': goal %d to cell %ld,%ld (home %ld,%ld)", narrow(deskName).c_str(), deskGoal, deskTo.x, deskTo.y, deskHome.x, deskHome.y);
    deskEnd(false);
    heldClear();
    if (wasPeek) return;
    tHappy = deskGoal == GoalHome && tier() > 0 ? 1.f : 0.f;
    sayCat(cat, 1.f, arg, true);
}

void Pet::deskUpdate(float dt) {
    if (!deskOn()) return;
    const char* stop = vit.fatal ? "fatal" : st == St::Sleep ? "sleep" : (st == St::Inside || st == St::Dive) ? "hidden" : game ? "game"
                       : extOn() ? "ext" : byeOn ? "bye" : introHidden ? "intro" : app.world.web ? "web" : !app.settings.icons ? "icons off"
                       : heldPath.empty() ? "hands empty" : nullptr;
    if (stop) {
        deskHomeNow(stop);
        return;
    }
    if (heldDragging) return;
    deskT += dt;
    if (deskMode != DeskHold) deskAll += dt;
    if (heldShown() && active == 0 && !dragging && (st == St::Idle || st == St::Walk)) deskHandOff = carriedCenter - pos;
    auto& I = app.icons;
    deskCheckT -= dt;
    if (deskCheckT <= 0 && !deskDropped) {
        deskCheckT = 0.5f;
        if (GetFileAttributesW(I.where(deskRec).c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::wstring np = I.pathById(deskRec);
            logf("desk carry: '%s' is gone (%s%s)", narrow(I.where(deskRec)).c_str(), np.empty() ? "deleted or off this disk" : "now ", narrow(np).c_str());
            deskHomeNow("vanished");
            tSurprise = 0.6f;
            sayCat("fileVanished", 1.f, L"", true);
            return;
        }
    }
    if (deskThrowT >= 0) {
        deskThrowT += dt;
        float u = clampf(deskThrowT / 0.35f, 0.f, 1.f);
        V2 tc = imgMid(I, deskTo);
        V2 p = deskFrom + (tc - deskFrom) * u;
        p.y -= (40.f * S() + 0.15f * fabsf(tc.x - deskFrom.x)) * 4.f * u * (1.f - u);
        deskFly = p;
        deskFlyOn = true;
        if (u >= 1.f) deskFinish();
        return;
    }
    switch (deskMode) {
    case DeskHold:
        break;
    case DeskGo:
        if (deskAll > 40.f && grounded() && !dragging && st != St::PrepJump && st != St::DropIcon) {
            logf("desk carry: 40 s on the way, placing from here");
            deskPlace();
            break;
        }
        if (st == St::Sit && !dragging) goIdle(0.3f, 0.6f);
        if (tripPlanned) {
            if (st != St::Walk || dragging) tripPlanned = false;
            else if ((pos.x - tripX) * (float)facing >= 0) {
                tripPlanned = false;
                if (deskTrip()) break;
            }
        }
        if (st == St::Idle && !dragging && !petting && deskT > 1.f && (!app.bubble.active || (app.bubble.t > app.bubble.typeDur + 0.8f && !app.bubble.held())))
            deskPlan();
        break;
    case DeskTrip:
        deskTripUpdate(dt);
        break;
    case DeskOffer:
        if (st == St::Idle && !dragging) setState(St::Lift, 1e9f);
        if (st == St::Lift && fabsf(lastCursor.x - pos.x) > 30.f * S()) facing = lastCursor.x > pos.x ? 1 : -1;
        if (fmodf(deskT, 1.f) < dt) {
            RECT hc = I.cellAt(deskHome), a{hc.left + 2, hc.top + 2, hc.right - 2, hc.bottom - 2}, x;
            bool stir = false;
            for (size_t k = 0; k < I.items.size() && !stir; k++) {
                if (deskHides(I.items[k])) continue;
                const RECT& c = I.items[k].cell;
                RECT b{c.left + 2, c.top + 2, c.right - 2, c.bottom - 2};
                if (!IntersectRect(&x, &a, &b)) continue;
                POINT now;
                stir = !I.itemPos(I.items[k], now) || labs(now.x - I.items[k].pos.x) > 2 || labs(now.y - I.items[k].pos.y) > 2;
            }
            if (stir && !I.fake) I.refresh();
            if (!cellTaken(I.cellAt(deskHome), -1)) {
                logf("desk offer: home is free again");
                deskMode = DeskGo;
                deskT = 1.f;
                if (st == St::Lift) goIdle(0.3f, 0.5f);
                break;
            }
        }
        if (deskT > 12.f) {
            if (!I.fake) I.refresh();
            POINT fc;
            deskFolder.clear();
            if (freeCellNear(imgMid(I, deskHome), fc, -1)) {
                deskGoal = GoalFree;
                deskTo = fc;
            } else if (!deskPickFolder()) {
                deskGoal = GoalFeet;
            }
            logf("desk offer: not taken, goal %d cell %ld,%ld folder '%s'", deskGoal, deskTo.x, deskTo.y, narrow(deskFolder).c_str());
            sayCat("fileSelfPlace", 1.f, L"", true);
            deskMode = DeskGo;
            deskT = 0;
            deskAll = 0;
            deskLegs = 0;
            if (st == St::Lift) goIdle(0.5f, 1.f);
        }
        break;
    case DeskPlace:
        if (st != St::DropIcon) {
            deskMode = DeskGo;
            deskT = 1.f;
            break;
        }
        if (stT >= 0.25f) {
            deskFrom = carriedCenter;
            deskThrowT = 0;
        }
        break;
    }
}

bool Pet::tripSpot(float x, float dir, POINT& cp) {
    auto& I = app.icons;
    auto& W = app.world;
    if (!gnd.valid || I.spacing.x <= 0 || I.spacing.y <= 0) return false;
    float labelH = (float)(I.spacing.y - I.iconPx - I.imgOff.y);
    float half = I.spacing.x * 0.5f, slide = 170.f * S() / 6.f;
    float reach = std::max(standTop - comH, 40.f * S()) + 12.f * S();
    for (float d : {tripD, 60.f * S()}) {
        float fx = x + dir * (slide + reach + d);
        if (fx - half < gnd.x0 || fx + half > gnd.x1) continue;
        cp = I.snapCell(I.clientForImgCenter(V2(fx, gnd.y - labelH - I.iconPx * 0.5f - 2.f)));
        while (I.cellAt(cp).bottom > gnd.y + 2.f) cp.y -= I.spacing.y;
        while (I.cellAt(POINT{cp.x, cp.y + I.spacing.y}).bottom <= gnd.y + 2.f) cp.y += I.spacing.y;
        RECT cell = I.cellAt(cp);
        V2 c = imgMid(I, cp);
        if (c.x - half < gnd.x0 || c.x + half > gnd.x1) continue;
        if (I.cellOnScreen(cp) && W.rectVisible(cell) && !cellTaken(cell, -1) && I.cellFree(cp, deskSelf())) return true;
    }
    return false;
}

void Pet::deskArmWalk() {
    tripPlanned = false;
    if (!tripArmed || st != St::Walk || !gnd.valid || (gnd.kind != SurfKind::Floor && gnd.kind != SurfKind::Window)) return;
    float sp = std::max(80.f * S() * runK, 1.f);
    float dist = fabsf(walkTarget - pos.x), dir = walkTarget > pos.x ? 1.f : -1.f;
    if (dist / sp < 0.6f) return;
    app.icons.refreshIfOlder(1.0);
    tripD = tripForce ? 90.f * S() : randf(60.f, 110.f) * S();
    std::vector<float> ok;
    POINT cp;
    for (float s = 0.3f * sp; s <= dist - 0.25f * sp; s += 6.f * S())
        if (tripSpot(pos.x + dir * s, dir, cp)) ok.push_back(s);
    if (ok.empty()) {
        logf("desk trip: no room for the file on this stretch, maybe on the next one");
        return;
    }
    float s = tripForce ? ok[ok.size() * 2 / 5] : ok[std::min(ok.size() - 1, (size_t)randf(0.f, (float)ok.size()))];
    tripX = pos.x + dir * s;
    tripPlanned = true;
}

bool Pet::deskTrip() {
    auto& I = app.icons;
    if (!gnd.valid || (gnd.kind != SurfKind::Floor && gnd.kind != SurfKind::Window) || st != St::Walk || dragging || active != 0 || baby())
        return false;
    I.refreshIfOlder(1.0);
    POINT cp;
    if (tripSpot(pos.x, facing > 0 ? 1.f : -1.f, cp)) {
        V2 c = imgMid(I, cp);
        tripFile = c;
        tripCell = cp;
        tripSurfY = gnd.y;
        tripKind = gnd.kind;
        tripHwnd = gnd.hwnd;
        tripArmed = false;
        tripPh = tripJumps = 0;
        tripT = 0;
        tripFlyT = tripLiftT = -1;
        tripSlide = 170.f * S();
        deskMode = DeskTrip;
        setState(St::Trip);
        tSurprise = 0.6f;
        logf("desk trip: stumbled at %.0f,%.0f (surface %d), file flies to cell %ld,%ld img %.0f,%.0f", pos.x, pos.y, (int)gnd.kind, cp.x, cp.y, c.x, c.y);
        return true;
    }
    logf("desk trip: no room ahead at %.0f,%.0f, maybe on the next stretch", pos.x, pos.y);
    return false;
}

void Pet::tripState(float dt) {
    if (!groundCheck(dt)) {
        startFall({});
        return;
    }
    const float tFall = 0.32f, tUp = 1.45f, tStand = 2.65f;
    float want = (facing > 0 ? 1.f : -1.f) * PI * 0.5f;
    if ((petting || rub > 0) && stT > tFall + 0.2f && stT < tUp) stT -= dt;
    if (tripSlide > 1.f) {
        pos.x = clampf(pos.x + facing * tripSlide * dt, gnd.x0, gnd.x1);
        tripSlide *= expf(-6.f * dt);
    }
    if (stT < tFall) {
        float u = stT / tFall;
        rot = want * u * u;
    } else if (stT < tUp) {
        rot = want;
        if (stT - dt < tFall) {
            squashV -= 2.2f;
            for (int i = 0; i < 8; i++)
                emit(4, pos + V2(facing * randf(0.f, std::max(standTop, comH)), -2.f), V2(randf(-60, 60) * S() + facing * 40.f * S(), randf(-45, -10) * S()),
                     0.55f, randf(0.8f, 1.2f));
            tSad = 5.f;
            tripUpset = 20.f;
            if (!app.bubble.asking() && !app.bubble.held()) app.bubble.close();
            sayCat("fileStumble", 1.f, L"", true);
            logf("desk trip: down at %.0f, bubble '%s'", pos.x, narrow(app.bubble.text).c_str());
        }
    } else if (stT < tStand) {
        rot = want * (1.f - smooth01((stT - tUp) / (tStand - tUp)));
    } else {
        rot = 0;
        goIdle(0.3f, 0.5f);
    }
}

static bool takeAgain(Pet& p, const char* why) {
    if (!p.deskOn()) return true;
    auto& I = p.app.icons;
    if (!p.deskRec.hands.empty()) return true;
    I.cancelPut(p.heldPath);
    if (_wcsicmp(p.deskRec.path.c_str(), p.heldPath.c_str()) != 0) {
        I.dropRecord(p.deskRec.path);
        p.deskRec.path = p.heldPath;
    }
    p.senseLetGo();
    if (I.take(p.deskRec)) return true;
    logf("desk carry (%s): '%s' did not go back into her hands, it goes home", why, narrow(p.heldPath).c_str());
    p.deskHomeNow(why);
    return false;
}

void Pet::deskTripUpdate(float dt) {
    auto& I = app.icons;
    if (!petting && rub <= 0 && !dragging) tripT += dt;
    if (tripPh == 0 && !deskDropped && tripFlyT < 0) {
        if (st != St::Trip) {
            deskMode = DeskGo;
            deskT = 1.f;
            return;
        }
        if (stT < 0.1f) return;
        deskDropped = true;
        deskFrom = carriedCenter;
        tripFlyT = 0;
    }
    if (tripFlyT >= 0) {
        tripFlyT += dt;
        float u = clampf(tripFlyT / 0.45f, 0.f, 1.f);
        V2 p = deskFrom + (tripFile - deskFrom) * u;
        p.y -= (34.f * S() + 0.2f * fabsf(tripFile.x - deskFrom.x)) * 4.f * u * (1.f - u);
        deskFly = p;
        deskFlyOn = true;
        if (u < 1.f) return;
        tripFlyT = -1;
        deskFlyOn = false;
        if (!deskRelease(tripCell, "trip landing", false)) {
            deskHomeNow("trip: file not shown");
            return;
        }
        tripPh = 1;
        tripCheckT = 0.5f;
        for (int k = 0; k < 5; k++)
            emit(4, tripFile + V2(randf(-14, 14) * S(), I.iconPx * 0.5f), V2(randf(-60, 60) * S(), randf(-40, -5) * S()), 0.5f, 0.9f);
        logf("desk trip: file landed at cell %ld,%ld", tripCell.x, tripCell.y);
        return;
    }
    if (deskDropped && tripPh >= 1 && tripLiftT < 0) {
        deskFly = tripFile;
        deskFlyOn = I.pending(heldPath) && I.findPath(heldPath) < 0;
    }
    if (tripLiftT >= 0) {
        tripLiftT += dt;
        float u = clampf(tripLiftT / 0.22f, 0.f, 1.f);
        V2 to = pos + V2(0, deskHandOff.y < -1.f ? deskHandOff.y * 0.85f : -comH);
        deskFly = tripFrom + (to - tripFrom) * smooth01(u);
        deskFlyOn = true;
        if (u < 1.f) return;
        tripLiftT = -1;
        deskFlyOn = false;
        deskDropped = false;
        deskMode = DeskGo;
        deskT = 1.f;
        deskAll = 0;
        deskLegs = 0;
        tripPh = 0;
        logf("desk trip: picked it up after %.1f s, on with the return (goal %d)", tripT, deskGoal);
        return;
    }
    if (tripT > 30.f) {
        deskHomeNow("trip timeout");
        return;
    }
    if (deskDropped && tripPh >= 1 && !I.pending(heldPath)) {
        tripCheckT -= dt;
        if (tripCheckT <= 0) {
            tripCheckT = 0.5f;
            bool gone = GetFileAttributesW(heldPath.c_str()) == INVALID_FILE_ATTRIBUTES;
            bool fresh = false;
            int fi = deskIndex();
            POINT now;
            if (gone || fi < 0 || !I.itemPos(I.items[fi], now) || labs(now.x - tripCell.x) > 2 || labs(now.y - tripCell.y) > 2) fresh = I.refresh();
            if (gone && fresh) {
                std::wstring np = I.pathById(deskRec);
                int j = np.empty() ? -1 : I.findPath(np);
                if (j >= 0 && labs(I.items[j].pos.x - tripCell.x) <= 2 && labs(I.items[j].pos.y - tripCell.y) <= 2) {
                    logf("desk trip: '%s' renamed on the floor to '%s', icon still at %ld,%ld", narrow(deskName).c_str(), narrow(np).c_str(), tripCell.x,
                         tripCell.y);
                    heldPath = np;
                    if (!app.bubble.asking() && !app.bubble.held()) app.bubble.close();
                    deskHomeNow("renamed on the floor");
                    tSurprise = 0.6f;
                    sayCat("fileVanished", 1.f, L"", true);
                    if (st == St::Walk || st == St::DropIcon) goIdle(1.f, 2.f);
                    return;
                }
            }
            if (gone || fresh) {
                int i = I.findPath(heldPath);
                bool moved = i < 0 || labs(I.items[i].pos.x - tripCell.x) > 2 || labs(I.items[i].pos.y - tripCell.y) > 2;
                if (gone || moved) {
                    logf("desk trip: '%s' taken from the floor by the user (file gone %d, icon %s %ld,%ld)", narrow(deskName).c_str(), (int)gone,
                         i < 0 ? "missing" : "at", i < 0 ? 0L : I.items[i].pos.x, i < 0 ? 0L : I.items[i].pos.y);
                    I.dropRecord(deskRec.path);
                    if (!app.bubble.asking() && !app.bubble.held()) app.bubble.close();
                    tSad = 0;
                    if (tier() >= 2) tHappy = 1.2f;
                    deskEnd(false);
                    heldClear();
                    if (st == St::Walk || st == St::DropIcon) goIdle(1.f, 2.f);
                    sayCat("fileStumbleTaken", 1.f, L"", true);
                    return;
                }
            }
        }
    }
    if (tripPh == 1) {
        if (st == St::Sit && !dragging) goIdle(0.3f, 0.6f);
        if (st != St::Idle || dragging || petting) return;
        float side = tripFile.x >= pos.x ? 1.f : -1.f;
        float pickX = tripFile.x - side * (I.iconPx * 0.5f + 14.f * S());
        bool same = gnd.valid && gnd.kind == tripKind && fabsf(gnd.y - tripSurfY) < 3.f && (gnd.kind != SurfKind::Window || gnd.hwnd == tripHwnd) &&
                    pickX >= gnd.x0 - 1.f && pickX <= gnd.x1 + 1.f;
        if (!same) {
            if (tripJumps++ < 2 && goJump(V2(pickX, tripSurfY), 40, 90)) {
                logf("desk trip: jumping back to the file at %.0f,%.0f", pickX, tripSurfY);
                return;
            }
            deskHomeNow("trip: cannot get back to the file");
            return;
        }
        if (fabsf(pickX - pos.x) > 6.f * S()) {
            goWalk(pickX);
            return;
        }
        facing = side > 0 ? 1 : -1;
        tSad = std::max(tSad, 1.2f);
        setState(St::DropIcon, 0.7f);
        tripPh = 2;
        return;
    }
    if (tripPh == 2) {
        if (st != St::DropIcon) tripPh = 1;
        else if (stT >= 0.3f) {
            if (!takeAgain(*this, "picked up from the floor")) return;
            tripFrom = tripFile;
            tripLiftT = 0;
        }
    }
}

void Pet::tripComfort() {
    tripUpset = 0;
    int tr = tier();
    tSad = 0;
    if (tr >= 2) tBlush = 2.5f;
    if (!app.bubble.asking() && !app.bubble.held()) app.bubble.close();
    sayCat("fileStumbleComfort", 1.f, L"", true);
    bool gain = app.now - lastTripComfort > 600;
    if (gain) {
        lastTripComfort = app.now;
        addRel(1.f, "comforted after a stumble");
    }
    logf("trip comfort: tier %d, rel gain %d, rel %.1f, bubble '%s'", tr, (int)gain, vit.rel, narrow(app.bubble.text).c_str());
}

void Pet::heldRects(D2D1_RECT_F& box, D2D1_RECT_F& img, D2D1_RECT_F& lab) const {
    float s = S();
    float sz = (heldStage >= 1 && heldSense.self > 0 ? 36.f : 30.f) * s;
    box = D2D1::RectF(-sz * 0.5f, -sz * 0.68f, sz * 0.5f, sz * 0.32f);
    UINT w = 0, h = 0;
    if (heldImg.src) heldImg.src->GetSize(&w, &h);
    float k = w && h ? std::min(sz / w, sz / h) : 1.f;
    float iw = w && h ? w * k : sz, ih = w && h ? h * k : sz;
    img = D2D1::RectF(-iw * 0.5f, box.bottom - ih, iw * 0.5f, box.bottom);
    float top = box.bottom + 2.f * s;
    lab = D2D1::RectF(-heldLabW * 0.5f, top, heldLabW * 0.5f, top + heldLabH);
}

bool Pet::heldHit(V2 cur) const {
    if (!heldShown() || !heldImg.src || active != 0 || dragging) return false;
    D2D1_RECT_F box, img, lab;
    heldRects(box, img, lab);
    V2 d = rotv(cur - carriedCenter, -rot);
    float m = 5.f * S();
    auto in = [&](const D2D1_RECT_F& r) { return r.right > r.left && d.x >= r.left - m && d.x <= r.right + m && d.y >= r.top - m && d.y <= r.bottom + m; };
    return in(img) || in(lab);
}

bool Pet::heldDragBegin() {
    if (deskOn()) {
        std::wstring now;
        senseLetGo();
        if (!app.icons.giveBack(deskRec, &now)) {
            logf("held: '%s' could not be given back to the desktop, not letting it go (she keeps holding it)", narrow(heldPath).c_str());
            return false;
        }
        if (!now.empty()) {
            heldPath = now;
            app.icons.placeAfter(now, deskHome, "back on the desktop for a drag out of her hands", deskRec.path, false);
        }
    }
    heldDragging = true;
    heldRetake = heldTakenSaid = false;
    logf("held: drag out of her hands, '%s' (desk %d)", narrow(heldPath).c_str(), (int)deskOn());
    return true;
}

void Pet::heldDragLeft() {
    if (!heldDragging || heldTakenSaid) return;
    heldTakenSaid = true;
    if (!app.bubble.asking() && (!app.bubble.held() || deskMode == DeskOffer)) app.bubble.close();
    sayCat((deskMode == DeskOffer ? "fileTakenOffer" : "fileTaken"), 1.f, L"", true);
}

void Pet::heldDragEnd(unsigned long effect, V2 cur, bool onDesktop) {
    heldDragging = false;
    logf("held: drag ended, effect %lu at %.0f,%.0f, desktop %d, back on her %d", effect, cur.x, cur.y, (int)onDesktop, (int)heldRetake);
    if (!heldTakenSaid) {
        heldRetake = false;
        takeAgain(*this, "drag ended on her");
        return;
    }
    if (heldRetake) {
        heldRetake = false;
        if (!takeAgain(*this, "dropped back on her")) return;
        heldT = std::min(heldT, 3.f);
        heldStage = std::max(heldStage, 1);
        heldLife = std::max(heldLife, heldT + randf(9.f, 12.f));
        if (deskOn()) {
            deskMode = DeskHold;
            deskGoal = GoalHome;
            deskTo = deskHome;
            deskT = deskAll = 0;
            deskOffered = false;
        }
        if (st == St::Lift) goIdle(1.f, 2.f);
        tSurprise = 0.4f;
        if (pendingCat.rfind("fileTaken", 0) == 0) pendingSay.clear();
        if (!app.bubble.asking()) app.bubble.close();
        sayCat("fileRetake", 1.f, L"", true);
        return;
    }
    if (!deskOn()) {
        heldClear();
        if (st == St::Lift) goIdle(1.f, 2.f);
        return;
    }
    auto& I = app.icons;
    if (GetFileAttributesW(heldPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        logf("held: '%s' went elsewhere, forgetting it", narrow(deskName).c_str());
        I.cancelPut(heldPath);
        I.dropRecord(deskRec.path);
        deskEnd(false);
        heldClear();
        return;
    }
    POINT to = deskHome;
    const char* how = "home";
    if (onDesktop) {
        to = I.cellUnder(POINT{(LONG)lroundf(cur.x), (LONG)lroundf(cur.y)});
        how = "cell at drop point";
    }
    bool ok = deskRelease(to, how);
    logf("held: '%s' goes to %ld,%ld (%s): %d", narrow(deskName).c_str(), to.x, to.y, how, (int)ok);
    deskEnd(!ok);
    heldClear();
}

static std::wstring holderOf(const std::vector<std::wstring>& files, int& apps) {
    apps = 0;
    DWORD sess = 0;
    WCHAR key[CCH_RM_SESSION_KEY + 1] = {};
    if (files.empty() || RmStartSession(&sess, 0, key) != ERROR_SUCCESS) return {};
    std::vector<LPCWSTR> ps;
    for (auto& f : files) ps.push_back(f.c_str());
    std::wstring who;
    if (RmRegisterResources(sess, (UINT)ps.size(), ps.data(), 0, nullptr, 0, nullptr) == ERROR_SUCCESS) {
        std::vector<RM_PROCESS_INFO> info;
        UINT need = 0, have = 0;
        DWORD reason = 0, e = ERROR_MORE_DATA;
        for (int k = 0; k < 3 && e == ERROR_MORE_DATA; k++) {
            info.resize(need + 4);
            have = (UINT)info.size();
            e = RmGetList(sess, &need, &have, info.data(), &reason);
        }
        if (e == ERROR_SUCCESS) {
            DWORD self = GetCurrentProcessId();
            for (UINT i = 0; i < have && i < info.size(); i++) {
                DWORD pid = info[i].Process.dwProcessId;
                if (pid == self) continue;
                apps++;
                if (!who.empty()) continue;
                who = info[i].strAppName;
                if (who.empty()) {
                    if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
                        wchar_t b[MAX_PATH];
                        DWORD n = MAX_PATH;
                        if (QueryFullProcessImageNameW(h, 0, b, &n)) who = baseName(b);
                        CloseHandle(h);
                    }
                    if (who.size() > 4 && !_wcsicmp(who.c_str() + who.size() - 4, L".exe")) who.resize(who.size() - 4);
                }
            }
        }
    }
    RmEndSession(sess);
    if (who.size() > 28) who = who.substr(0, 26) + L"…";
    return who;
}

static std::vector<std::wstring> someFilesIn(const std::wstring& dir) {
    std::vector<std::wstring> out, q{dir};
    int seen = 0;
    for (size_t d = 0; d < q.size() && d < 24 && out.size() < 48 && seen < 400; d++) {
        WIN32_FIND_DATAW wf;
        HANDLE h = FindFirstFileExW((q[d] + L"\\*").c_str(), FindExInfoBasic, &wf, FindExSearchNameMatch, nullptr, 0);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (!wcscmp(wf.cFileName, L".") || !wcscmp(wf.cFileName, L"..") || (wf.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            seen++;
            std::wstring p = q[d] + L"\\" + wf.cFileName;
            if (wf.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) q.push_back(p);
            else out.push_back(p);
        } while (out.size() < 48 && seen < 400 && FindNextFileW(h, &wf));
        FindClose(h);
    }
    return out;
}

void Pet::tugStart(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    tugFolder = a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
    tugPath = path;
    tugSince = app.now;
    tugWho.clear();
    tugApps = -1;
    tugFiles = 0;
    tugCat.clear();
    int i = app.icons.findPath(path);
    V2 at = i >= 0 ? rectMid(app.icons.items[i].img) : pos + V2(facing * 200.f * S(), 0);
    if (fabsf(at.x - pos.x) > 6.f * S()) facing = at.x > pos.x ? 1 : -1;
    tAngry = 2.4f;
    tSurprise = 0.3f;
    tugT = -1;
    if (grounded() && st != St::Sleep && active == 0 && iArmL >= 0 && iFArmL >= 0) {
        tugT = 0;
        setState(St::Idle, 2.1f);
    }
    auto job = std::make_shared<BusyJob>();
    busyJob = job;
    bool folder = tugFolder;
    std::wstring p = path;
    std::thread([job, folder, p] {
        double t0 = nowSec();
        std::vector<std::wstring> fs = folder ? someFilesIn(p) : std::vector<std::wstring>{p};
        int apps = 0;
        job->app = holderOf(fs, apps);
        job->apps = apps;
        job->files = (int)fs.size();
        job->ms = (float)((nowSec() - t0) * 1000.0);
        job->done = true;
    }).detach();
    logf("desk carry: '%s' is busy, she tugs at it (%s), nothing in her hands, the icon stays", narrow(path).c_str(), tugT >= 0 ? "pulls" : "no pull, not standing");
}

void Pet::tugStep(float dt) {
    if (tugT >= 0) {
        float was = tugT;
        tugT += dt;
        if ((int)(tugT * 2.2f) != (int)(was * 2.2f) && tugT < 1.6f)
            for (int k = 0; k < 3; k++) emit(4, pos + V2(-facing * randf(4, 16) * S(), -2), V2(-facing * randf(20, 70) * S(), randf(-40, -10) * S()), 0.45f, randf(0.6f, 0.9f));
        if (st != St::Idle || dragging || tugT > 1.9f) tugT = -1;
    }
    if (tugPath.empty()) return;
    bool ready = busyJob && busyJob->done;
    float age = (float)(app.now - tugSince);
    if (!(ready && age > 0.75f) && age < 3.f) return;
    if (ready) {
        tugWho = busyJob->app;
        tugApps = busyJob->apps;
        tugFiles = busyJob->files;
    }
    tugCat = tugFolder ? (tugWho.empty() ? "folderBusy" : "folderBusyApp") : tugWho.empty() ? "fileBusyAnon" : "fileBusyApp";
    const char* cat = tugCat.c_str();
    sayCat(cat, 1.f, tugWho, true);
    logf("desk carry: busy %s '%s' held by '%s' (restart manager: %d files, %d other apps, %.0f ms%s), said %s", tugFolder ? "folder" : "file",
         narrow(tugPath).c_str(), narrow(tugWho).c_str(), tugFiles, tugApps, ready ? busyJob->ms : -1.f, ready ? "" : ", timed out", cat);
    tugPath.clear();
    busyJob.reset();
}
