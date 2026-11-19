#include "pet.h"

static bool peekableExt(const std::wstring& p) {
    size_t d = p.find_last_of(L'.');
    std::wstring e = d == std::wstring::npos ? L"" : p.substr(d);
    for (auto& c : e) c = (wchar_t)towlower(c);
    return e != L".lnk" && e != L".url" && e != L".ini" && e != L".sys" && e != L".dll";
}

bool Pet::peekStart(const Input& in) {
    auto& I = app.icons;
    if (nextPeekAt <= 0) nextPeekAt = app.now + randf(15, 30) * 60.0;
    if (app.now < nextPeekAt || !app.settings.icons || !I.ok() || baby() || vit.intro || game || quiet() || strollOn || webActive()) return false;
    if (!grounded() || !heldPath.empty() || !carriedName.empty() || deskOn() || in.idleSec < 2 || in.idleSec > 240) return false;
    if (I.autoArrange()) return false;
    I.refreshIfOlder(1.0);
    struct C { int i; float d; };
    std::vector<C> c;
    for (size_t i = 0; i < I.items.size(); i++) {
        const DIcon& d = I.items[i];
        if (d.folder || d.path.empty() || !I.isDesktopFile(d.path) || !peekableExt(d.path) || cornerIsMine(d.path)) continue;
        if (!app.world.rectVisible(d.img) || app.world.monitorAt(V2((d.img.left + d.img.right) * 0.5f, (float)d.img.bottom)) < 0) continue;
        float cx = (d.img.left + d.img.right) * 0.5f;
        if (cx < gnd.x0 + 10.f * S() || cx > gnd.x1 - 10.f * S()) continue;
        DWORD at = GetFileAttributesW(d.path.c_str());
        if (at == INVALID_FILE_ATTRIBUTES || (at & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN))) continue;
        std::string why;
        if (!I.canTake(d.path, &why)) continue;
        c.push_back({(int)i, fabsf(cx - pos.x)});
    }
    nextPeekAt = app.now + jitterTime(45.f * 60.f, 20.f * 60.f, 120.f * 60.f) * (dayMood == 4 ? 0.5f : 1.f);
    if (c.empty()) {
        logf("peek: nothing free to look at");
        return false;
    }
    std::sort(c.begin(), c.end(), [](const C& a, const C& b) { return a.d < b.d; });
    const DIcon& d = I.items[c[randi(0, std::min(4, (int)c.size() - 1))].i];
    peekPath = d.path;
    float tx = clampf((d.img.left + d.img.right) * 0.5f, gnd.x0 + 12.f * S(), gnd.x1 - 12.f * S());
    logf("peek: going to look at '%s' (%zu candidates)", narrow(d.name).c_str(), c.size());
    if (fabsf(tx - pos.x) < 20.f * S()) {
        peekArrive();
        return true;
    }
    peekGoAt = app.now;
    goWalk(tx);
    return true;
}

void Pet::peekArrive() {
    setState(St::LookAround, 1.4f);
    peekTakeAt = app.now + 1.4;
}

void Pet::peekStep() {
    if (peekPath.empty() || peekTakeAt < 0 || app.now < peekTakeAt) return;
    peekTakeAt = -1;
    std::wstring path = peekPath;
    peekPath.clear();
    if (dragging || extFolder || !grounded() || !heldPath.empty() || deskOn() || game || (st != St::LookAround && st != St::Idle)) {
        logf("peek: changed her mind ('%s')", narrow(path).c_str());
        return;
    }
    peekOn = true;
    receiveFile(path, 1);
    if (!deskOn()) {
        tugPath.clear();
        tugT = -1;
        busyJob.reset();
        heldClear();
        peekOn = false;
        logf("peek: '%s' did not come to her hands, leaves it", narrow(path).c_str());
        return;
    }
    heldLife = randf(7.f, 11.f);
    logf("peek: looking at '%s'", narrow(heldName).c_str());
}

bool Pet::peekCaught() {
    if (!peekOn || !deskOn() || heldPath.empty()) return false;
    peekCaughtN++;
    if (!app.bubble.asking()) app.bubble.close();
    int tr = tier();
    if (tr >= 2) {
        emHideT = emHideLen = 1.6f;
        tBlush = std::max(tBlush, 3.f);
    } else if (tr == 1) {
        tBlush = std::max(tBlush, 1.8f);
        tSurprise = 0.8f;
    } else {
        tAngry = 1.2f;
    }
    squashV -= 1.2f;
    sayCat("peekCaught", 1.f, L"", true);
    logf("peek: caught with '%s' (tier %d): '%s'", narrow(heldName).c_str(), tr, narrow(app.bubble.text).c_str());
    deskHomeNow("caught peeking");
    peekOn = false;
    return true;
}
