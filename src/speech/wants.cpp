#include "pet.h"

namespace {
const char* kWantKinds[] = {"Pet", "Hug", "Kiss", "Ask"};
}

bool Pet::wantCan(const Input& in) const {
    if (!app.settings.talk || baby() || vit.intro != 0 || vit.fatal || game || quiet() || strollOn || webActive() || nightmare) return false;
    if (dragging || !grounded() || !heldPath.empty() || !carriedName.empty() || deskOn() || peekOn) return false;
    if (st != St::Idle && st != St::Sit && st != St::LookAround) return false;
    if (app.bubble.active || in.idleSec < 2 || in.idleSec > 60) return false;
    return app.now - lastSay > 25;
}

bool Pet::wantStep(const Input& in) {
    int tr = tier();
    if (nextWantAt <= 0) nextWantAt = app.now + randf(20, 50) * 60.0;
    if (tr < 2 || app.now < nextWantAt || wantPhase != 0 || !wantCan(in)) return false;
    float k = (dayMood == 3 || dayMood == 5) ? 0.75f : 1.f;
    nextWantAt = app.now + jitterTime(55.f * 60.f, 20.f * 60.f, 150.f * 60.f) * k;
    if (tr == 2) wantKind = "Pet";
    else {
        float w[4] = {1.f, 0.9f, 0.7f, 1.f};
        if (dayMood == 2) w[1] += 0.6f;
        if (dayMood == 3) w[3] += 0.6f;
        float s = w[0] + w[1] + w[2] + w[3], r = randf(0, s);
        int i = 0;
        for (; i < 3 && r >= w[i]; i++) r -= w[i];
        wantKind = kWantKinds[i];
        if (wantKind == lastWantKind && chance(0.6f)) wantKind = kWantKinds[(i + 1 + randi(0, 2)) % 4];
    }
    lastWantKind = wantKind;
    wantPhase = 1;
    wantT = 0;
    if (fabsf(lastCursor.x - pos.x) > 6.f) facing = lastCursor.x > pos.x ? 1 : -1;
    if (fabsf(lastCursor.x - pos.x) > 380.f * S() && st != St::Sit) comeToCursor(false);
    logf("want: she wants %s (tier %d, mood %s), getting shy first", wantKind.c_str(), tr, moodName());
    return true;
}

void Pet::wantTick(float dt) {
    if (wantPhase == 0) return;
    wantT += dt;
    if (dragging || game || vit.fatal || st == St::Sleep || st == St::Inside) {
        wantEnd("interrupted");
        return;
    }
    if (wantPhase == 1) {
        if (st == St::Walk) return;
        tBlush = std::max(tBlush, std::min(2.5f, wantT * 1.2f));
        if (emLookT <= 0) {
            emLookT = randf(0.5f, 0.9f);
            emLook = chance(0.5f) ? V2(0, 0.8f) : V2(lastCursor.x > pos.x ? -0.7f : 0.7f, 0.3f);
        }
        if (wantT > 2.2f) {
            if (app.bubble.held() || app.bubble.asking()) {
                if (wantT > 15.f) wantEnd("busy, never asked");
                return;
            }
            wantAsk();
        }
        return;
    }
    if (wantPhase == 3) {
        tBlush = std::max(tBlush, 1.5f);
        bool busy = app.bubble.held() || app.bubble.asking();
        if (wantKind == "Kiss" && !wantPeeked && wantT > 7.f && !busy) {
            wantPeeked = true;
            if (app.bubble.active) app.bubble.close();
            sayCat("wantKissPeek", 1.f, L"", true);
            wantYesLine = app.bubble.text;
        }
        if (wantT > (wantKind == "Kiss" ? 14.f : 18.f)) {
            if (busy) {
                if (wantT > (wantKind == "Kiss" ? 30.f : 34.f)) wantEnd("waited, busy");
                return;
            }
            if (app.bubble.active) app.bubble.close();
            tSad = std::max(tSad, 0.8f);
            sayCat(("want" + wantKind + "Forgot").c_str(), 1.f, L"", true);
            wantEnd("waited, nothing");
        }
    }
}

void Pet::wantAsk() {
    std::string cat = wantKind == "Ask" ? "wantQuestion" : "wantAsk" + wantKind;
    if (wantKind == "Ask") {
        app.bubble.close();
        sayCat(cat.c_str(), 1.f, L"", true);
        if (!app.bubble.active) {
            wantEnd("could not ask");
            return;
        }
        tBlush = std::max(tBlush, 6.f);
        wantPhase = 0;
        logf("want: asked '%s'", narrow(app.bubble.text).c_str());
        return;
    }
    app.bubble.close();
    if (!ask(cat.c_str(), ("wantChoices" + wantKind).c_str(), "want:" + wantKind)) {
        wantEnd("could not ask");
        return;
    }
    app.bubble.life = app.bubble.typeDur + 14.f;
    tBlush = std::max(tBlush, 5.f);
    wantPhase = 2;
    wantT = 0;
    logf("want: asked '%s'", narrow(app.bubble.text).c_str());
}

void Pet::wantAnswer(const std::string& kind, int choice) {
    auto ch = app.phrases.list(("wantChoices" + kind).c_str());
    bool yes = choice >= 0 && choice < (int)ch.size() - 1;
    if (!yes) {
        tSad = std::max(tSad, choice < 0 ? 0.6f : 1.f);
        tBlush = std::max(tBlush, 3.f);
        sayCat(choice < 0 ? "wantIgnored" : "wantLater", 1.f, L"", true);
        wantEnd(choice < 0 ? "no answer" : "later");
        return;
    }
    tHappy = std::max(tHappy, 1.5f);
    tBlush = std::max(tBlush, 6.f);
    if (kind == "Hug") {
        bool close = fabsf(lastCursor.x - pos.x) < 120.f * S();
        sayCat("wantHugYes", 1.f, L"", true);
        if (close) emote("hug", 2.8f);
        else {
            comeToCursor(false);
            arriveHug = true;
        }
        wantEnd("hug");
        return;
    }
    sayCat(("want" + kind + "Yes").c_str(), 1.f, L"", true);
    wantYesLine = app.bubble.text;
    if (kind == "Pet" && fabsf(lastCursor.x - pos.x) > 140.f * S()) comeToCursor(true);
    if (kind == "Kiss") tSquint = 14.f;
    wantPhase = 3;
    wantT = 0;
    wantPeeked = false;
    logf("want: yes to %s, she waits", kind.c_str());
}

bool Pet::wantTouched(const char* act) {
    if (wantPhase < 2) return false;
    bool fits = (wantKind == "Pet" && !strcmp(act, "pet")) || (wantKind == "Kiss" && !strcmp(act, "kiss")) || (wantKind == "Hug" && !strcmp(act, "pet"));
    if (!fits) return false;
    bool hers = app.bubble.asking() || app.bubble.t <= 0.f || (!wantYesLine.empty() && app.bubble.text == wantYesLine);
    if (app.bubble.active && (hers || !app.bubble.held())) app.bubble.close();
    tSquint = 0;
    tHappy = std::max(tHappy, 2.f);
    tBlush = std::max(tBlush, 8.f);
    if (!strcmp(act, "kiss")) emHideT = emHideLen = 1.8f;
    double said0 = lastSay;
    sayCat(("want" + wantKind + "Got").c_str(), 1.f, L"", true);
    if (lastSay != said0) app.bubble.hold();
    if (tier() >= 2 && app.now - lastReplyGain > 600.0) {
        lastReplyGain = app.now;
        addRel(0.4f, "gave her what she asked for");
    }
    logf("want: got %s by %s: '%s'", wantKind.c_str(), act, narrow(app.bubble.text).c_str());
    wantEnd("got it");
    return true;
}

void Pet::wantEnd(const char* why) {
    if (wantPhase != 0) logf("want: %s over (%s)", wantKind.c_str(), why);
    wantPhase = 0;
    wantYesLine.clear();
    wantT = 0;
    tSquint = std::min(tSquint, 0.5f);
}
