#include "pet.h"

static void csFace(Pet& p, int face, float ft) {
    switch (face) {
    case CsFaceHappy: p.tHappy = std::max(p.tHappy, ft); break;
    case CsFaceSad: p.tSad = std::max(p.tSad, ft); break;
    case CsFaceSurprise: p.tSurprise = std::max(p.tSurprise, ft); break;
    case CsFaceAngry: p.tAngry = std::max(p.tAngry, ft); break;
    case CsFaceBlush: p.tBlush = std::max(p.tBlush, ft); break;
    }
}

void Pet::csEvent(const CsEvent& e) {
    int face = CsFaceNone;
    float ft = 0;
    csVoice.push(e, tier(), app.now, face, ft);
    if (e.type == CsEv::MatchEnd) {
        dayCount("cs");
        if (e.n > 0) dayCount("csWin");
    }
    if (st != St::Watch || vit.fatal || baby()) return;
    csFace(*this, face, ft);
    if (e.type == CsEv::Ace) cornerPhoto(L"я после эйса");
    else if (e.type == CsEv::MatchEnd && e.n > 0) cornerPhoto(L"я после победы");
}

void Pet::csStep(float) {
    if (st != St::Watch || quiet() || !app.settings.talk) return;
    if (app.bubble.asking() || (app.bubble.active && app.bubble.age() < 1.5f)) return;
    const CsLine& pe = csVoice.pending;
    if (csVoice.hasPending && pe.cat.rfind("rbx", 0) == 0 && pe.cat != "rbxLeave" && app.activity.exe != L"robloxplayerbeta.exe") return;
    CsLine l;
    if (!csVoice.due(app.now, l)) return;
    sayCat(l.cat.c_str(), 1.f, l.arg, true);
    if (!vit.fatal && !baby()) csFace(*this, l.face, l.faceT);
    watchSayT = std::max(watchSayT, 240.f);
}

void Pet::rbxEvent(const RbxEvent& e) {
    CsLine l;
    if (rbxVoice.pick(e, app.now, randf(0, 1), l)) csVoice.queue(l, app.now);
}

bool Pet::rbxLeaveNow() {
    if (!csVoice.hasPending || csVoice.pending.cat != "rbxLeave" || csVoice.pending.until < app.now) return false;
    csVoice.hasPending = false;
    if (quiet() || !app.settings.talk) return false;
    sayCat("rbxLeave", 1.f, L"", true);
    csVoice.lastLine = app.now;
    return true;
}
