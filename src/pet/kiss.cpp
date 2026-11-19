#include "pet.h"

enum { KmNone, KmAccept, KmDodge, KmShy, KmNo, KmHate, KmBody, KmSleepYes, KmSleepNo, KmBaby, KmGame };
static const char* kPlaceNames[] = {"none", "cheek", "forehead", "mouth", "hand", "head", "body", "legs"};

static bool visorUV(const Rig& R, const RigInstance& I, V2 winPt, V2& uv) {
    const FaceDef& f = R.face;
    if (!f.on || f.part < 0 || f.part >= (int)I.world.size() || f.size.x <= 0 || f.size.y <= 0) return false;
    D2D1::Matrix3x2F inv = I.world[f.part];
    if (!inv.Invert()) return false;
    V2 q = rotv(xf(inv, winPt) - f.center, -f.angle);
    uv = V2(q.x / (f.size.x * 0.5f), q.y / (f.size.y * 0.5f));
    return true;
}

V2 Pet::visorScreen(V2 uv) const {
    const Rig& R = curRig();
    const RigInstance& I = curInst();
    const FaceDef& f = R.face;
    if (!f.on || f.part < 0 || f.part >= (int)I.world.size()) return headC + V2(uv.x * headR * 0.6f, uv.y * headR * 0.3f);
    V2 q = f.center + rotv(V2(uv.x * f.size.x * 0.5f, uv.y * f.size.y * 0.5f), f.angle);
    return xf(I.world[f.part], q) + app.winOrigin;
}

int Pet::kissPlaceAt(V2 cur) const {
    if (introHidden || vit.intro == 1 || st == St::Inside || st == St::Dive || st == St::Emerge || st == St::Hide) return KissNone;
    const Rig& R = curRig();
    const RigInstance& I = curInst();
    if (!I.rig || I.world.size() != R.parts.size()) return KissNone;
    V2 w = cur - app.winOrigin;
    int hit = I.hitTest(w);
    std::string part = hit >= 0 ? R.parts[hit].name : "";
    if (part.rfind("forearm", 0) == 0) return KissHand;
    V2 uv;
    bool face = visorUV(R, I, w, uv);
    float chin = 1.9f;
    if (face && R.mouth.on && R.mouth.part >= 0 && R.mouth.part < (int)I.world.size()) {
        V2 m;
        if (visorUV(R, I, xf(I.world[R.mouth.part], R.mouth.center), m)) chin = m.y + 0.75f;
    }
    bool nearHead = (cur - headC).len() < headR * 1.15f;
    if (face && fabsf(uv.x) < 1.15f && uv.y > -1.f && uv.y < chin && (hit >= 0 || nearHead)) {
        if (fabsf(uv.x) < 0.42f && uv.y > 0.25f) return KissMouth;
        if (uv.y < -0.4f && fabsf(uv.x) < 0.75f) return KissForehead;
        return KissCheek;
    }
    if (hit < 0) return KissNone;
    bool headPart = part == "head" || part == "beanie" || part.rfind("hair", 0) == 0 || part.rfind("clip", 0) == 0;
    if (active == 0 && !headPart) {
        float dl = (cur - handScreen(false)).len(), dr = (cur - handScreen(true)).len();
        if (std::min(dl, dr) < 12.f * S()) return KissHand;
    }
    if (headPart || (active != 0 && nearHead)) {
        if (face && uv.y > -1.75f && uv.y <= -1.f && fabsf(uv.x) < 0.85f) return KissForehead;
        if (face && uv.y > -1.f && uv.y < chin) return KissCheek;
        return KissHead;
    }
    if (part.rfind("leg", 0) == 0 || part.rfind("shin", 0) == 0) return KissLegs;
    return KissBody;
}

bool Pet::kissable(V2 cur) const {
    if (dragging || sleepCarry || feeding || !grounded()) return false;
    if (st == St::Bite || st == St::Led || st == St::Hide || webActive()) return false;
    return kissPlaceAt(cur) != KissNone;
}

bool Pet::kiss(V2 cur) {
    if (!kissable(cur)) return false;
    int place = kissPlaceAt(cur);
    int tr = tier();
    logf("kiss at %.0f,%.0f: %s, tier %d, state %s, rel %.1f%s", cur.x, cur.y, kPlaceNames[place], tr, stateName(), vit.rel,
         baby() ? ", baby" : "");
    if (kissT >= 0 && kissT < 1.2f) return true;
    if (vit.fatal) {
        if (!app.bubble.asking()) app.bubble.say(L"...");
        return true;
    }
    kissWhere = place;
    kissAt = cur;
    kissT = 0;
    kissSteamT = 0;
    kissSlide = 0;
    bool body = place == KissBody || place == KissLegs;
    if (!body) dayCount("kiss");
    bool wantKiss = !body && wantPhase >= 2 && wantKind == "Kiss";
    bool fromMoment = !wantKiss && !body && tr >= 1 && !baby() && !game && st != St::Sleep && !momentKiss && !momentCat("kiss").empty();
    if (fromMoment && app.bubble.asking() && app.bubble.askId.rfind("reply:", 0) == 0 && !app.bubble.reading()) app.bubble.close();
    if (app.bubble.active && !app.bubble.asking() && !app.bubble.held() && app.now - cuddleSaidAt > 4.5) app.bubble.close();
    float away = cur.x > pos.x ? -1.f : 1.f;
    bool standing = st == St::Idle || st == St::Walk || st == St::LookAround || st == St::Wave || st == St::Land || st == St::Stretch ||
                    st == St::Clap;
    auto hold = [&](float secs) {
        if (standing) setState(St::Idle, secs);
        else if (st == St::Sit || st == St::Watch) stDur = std::max(stDur, stT + secs);
    };
    auto mark = [&](V2 at) { emit(5, at, V2(0, -12.f * S()), 1.3f, 1.f); };
    auto cheekFor = [&](V2 c) {
        float side = (c.x > headC.x ? 1.f : -1.f) * (float)facing;
        return visorScreen(V2(side * 0.85f, 0.35f));
    };
    V2 spot = place == KissMouth ? cheekFor(cur) : cur;

    if (baby()) {
        kissMode = KmBaby;
        if (body) {
            tSurprise = 0.5f;
            sayCat(st == St::Sleep ? "babySleepKiss" : "babyKissBody", 1.f, L"", true);
            return true;
        }
        mark(spot);
        kissBlushT = 2.5f;
        kissBlushK = 0.5f;
        if (st == St::Sleep) sayCat("babySleepKiss", 1.f, L"", true);
        else {
            tHappy = 1.6f;
            sayCat("babyKiss", 1.f, L"", true);
        }
        return true;
    }
    if (game) {
        kissMode = KmGame;
        tSurprise = 0.4f;
        if (tr >= 2) tBlush = 1.2f;
        sayCat("kissGame", 1.f, L"", true);
        return true;
    }
    if (st == St::Sleep) {
        if (tr >= 2 && !body) {
            kissMode = KmSleepYes;
            mark(spot);
            kissBlushT = 4.f;
            kissBlushK = 0.6f;
            emit(0, headC + V2(facing * headR * 1.1f, -headR * 0.2f), V2(facing * 6.f * S(), -14.f * S()), 2.f, 0.7f);
            sayCat("kissAsleep", 1.f, L"", true);
        } else {
            kissMode = KmSleepNo;
            tAngry = 1.2f;
            facing = -facing;
            squashV -= 1.f;
            sayCat("kissAsleepNo", 1.f, L"", true);
        }
        return true;
    }
    if (body) {
        kissMode = KmBody;
        tSurprise = 0.3f;
        if (tr == 0) tAngry = 1.5f;
        hold(1.6f);
        sayCat(place == KissLegs ? "kissBoots" : "kissBody", 1.f, L"", true);
        return true;
    }
    switch (tr) {
    case 3: {
        kissMode = place == KissMouth ? KmDodge : KmAccept;
        tBlush = 3.2f;
        tSurprise = 0.8f;
        hold(3.4f);
        if (place == KissMouth && app.now - cuddleSaidAt > 4.5) sayCat("kissMouthDodge", 1.f, L"", true);
        mark(spot);
        if (!cornerOwn(L"я красная.png") || chance(0.3f)) cornerPhoto(L"я красная");
        cuddleAdd(2.2f, "kiss");
        if (app.now - lastKissGain > 600.0) {
            lastKissGain = app.now;
            addRel(0.5f, "kiss");
        }
        break;
    }
    case 2:
        kissMode = KmShy;
        tBlush = 2.2f;
        tSquint = 1.f;
        hold(1.6f);
        if (standing) {
            facing = -(int)away;
            kissSlide = away * 100.f * S();
        }
        if (app.now - cuddleSaidAt > 4.5) sayCat("kissNotYet", 1.f, L"", true);
        cuddleAdd(1.5f, "kiss");
        break;
    case 1: {
        kissMode = KmNo;
        tSurprise = 1.f;
        squashV -= 1.5f;
        bool hop = false;
        if (standing && gnd.valid) {
            float x = clampf(pos.x + away * 55.f * S(), gnd.x0, gnd.x1);
            hop = fabsf(x - pos.x) > 15.f * S() && goJump(V2(x, pos.y), 30, 50);
        }
        if (hop) facing = -(int)away;
        else if (standing) {
            hold(1.4f);
            kissSlide = away * 120.f * S();
        }
        sayCat("kissNo", 1.f, L"", true);
        break;
    }
    default:
        kissMode = KmHate;
        tAngry = 2.5f;
        if (standing || st == St::Sit || st == St::Watch) facing = -(int)away;
        hold(1.4f);
        if (standing) kissSlide = away * 140.f * S();
        if (app.now - lastKissLoss > 600.0) {
            lastKissLoss = app.now;
            addRel(-1.f, "kiss: hates it");
        }
        sayCat("kissNo", 1.f, L"", true);
        break;
    }
    if (wantKiss) wantTouched("kiss");
    else if (fromMoment) momentTouch("kiss");
    return true;
}

void Pet::kissStep(float dt) {
    kissBlushT -= dt;
    if (kissT < 0) return;
    float t0 = kissT;
    kissT += dt;
    auto at = [&](float x) { return t0 < x && kissT >= x; };
    if (dragging || vit.fatal) {
        kissT = -1;
        kissSlide = 0;
        return;
    }
    if (kissSlide != 0) {
        if (grounded() && st == St::Idle) pos.x = clampf(pos.x + kissSlide * dt, gnd.x0, gnd.x1);
        kissSlide *= expf(-7.f * dt);
        if (fabsf(kissSlide) < 2.f * S()) kissSlide = 0;
    }
    if (kissMode == KmAccept || kissMode == KmDodge) {
        if (kissT < 2.f) {
            kissSteamT -= dt;
            if (kissSteamT <= 0) {
                kissSteamT = 0.12f;
                float side = chance(0.5f) ? 1.f : -1.f;
                emit(6, headTop + rotv(V2(side * headR * randf(0.15f, 0.65f), headR * 0.2f), rot),
                     V2(side * randf(10, 35) * S(), -randf(60, 90) * S()), 0.9f, randf(0.8f, 1.15f));
            }
        }
        if (at(0.8f)) {
            if (st == St::Idle || st == St::Sit || st == St::Watch) facing = lastCursor.x > pos.x ? -1 : 1;
            tSurprise = 0;
            tSquint = 1.3f;
            static const char* cats[] = {"", "kissCheek", "kissForehead", "", "kissHand", "kissHead", "", ""};
            if (kissMode == KmAccept && cats[kissWhere][0] && app.now - cuddleSaidAt > 4.5 && !app.bubble.held()) {
                if (app.bubble.active && !app.bubble.asking()) app.bubble.close();
                sayCat(cats[kissWhere], 1.f, L"", true);
            }
            logf("kiss: turned away (facing %d), bubble '%s'", facing, narrow(app.bubble.text).c_str());
        }
        if (at(2.1f)) tHappy = 1.2f;
        if (kissT > 3.4f) kissT = -1;
    } else if (kissT > 1.6f) {
        kissT = -1;
    }
}

void Pet::kissFace(FaceState& tg, V2& lk) {
    if (kissBlushT > 0) tg.blush = std::max(tg.blush, kissBlushK * clampf(kissBlushT / 0.8f, 0.f, 1.f));
    if (kissT < 0) return;
    if ((kissMode == KmAccept || kissMode == KmDodge) && kissT >= 0.8f) lk = V2((float)facing * 0.3f, 0.6f);
    if (kissMode == KmDodge && kissT < 0.8f) lk = V2(kissAt.x > headC.x ? -0.8f : 0.8f, 0.1f);
    if (kissMode == KmBody) {
        V2 d = (kissAt - headC) / (200.f * S());
        lk = V2(clampf(d.x, -1.f, 1.f), clampf(d.y, -1.f, 1.f));
    }
}

void Pet::kissPose(float) {
    if (kissT < 0 || active != 0) return;
    auto& P = curInst().pose;
    auto R = [&](int i) -> PartPose* { return i >= 0 && i < (int)P.size() ? &P[i] : nullptr; };
    auto rot = [&](int i, float a) { if (PartPose* p = R(i)) p->rot += a; };
    if (kissMode == KmDodge) {
        float e = smooth01(kissT / 0.12f) * (1.f - smooth01((kissT - 0.6f) / 0.3f));
        float side = (kissAt.x > headC.x ? -1.f : 1.f) * (float)facing;
        rot(iHead, side * 0.3f * e);
        if (PartPose* h = R(iHead)) h->off.x += side * 3.f * S() * e;
    }
    auto reach = [&](V2 target, int side, float w) {
        int ai = side == 1 ? iArmL : iArmR, fi = side == 1 ? iFArmL : iFArmR;
        PartPose* pa = R(ai);
        PartPose* pf = R(fi);
        if (!pa || !pf || w <= 0.01f) return;
        PartPose a0 = *pa, f0 = *pf;
        reachTo(target, side);
        float ar = lerpf(a0.rot, pa->rot, w), fr = lerpf(f0.rot, pf->rot, w);
        *pa = a0;
        *pf = f0;
        pa->rot = ar;
        pf->rot = fr;
        if (w > 0.3f) pa->zOff = pf->zOff = 25;
    };
    if (kissMode == KmAccept || kissMode == KmDodge) {
        float h = smooth01((kissT - 0.8f) / 0.25f) * (1.f - smooth01((kissT - 2.5f) / 0.4f));
        V2 sh = shoulderScreen(2), a = visorScreen(V2(0.45f, 0.35f)), b = visorScreen(V2(-0.45f, 0.35f));
        reach((a - sh).len() < (b - sh).len() ? a : b, 2, h);
    }
    if (kissMode == KmHate) {
        float e = smooth01(kissT / 0.08f) * (1.f - smooth01((kissT - 0.4f) / 0.2f));
        int side = (shoulderScreen(1).x - shoulderScreen(2).x) * (float)facing > 0 ? 1 : 2;
        V2 sh = shoulderScreen(side);
        reach(sh + V2((float)facing, -0.1f).norm() * headR * 3.f, side, e);
        float f = smooth01((kissT - 0.45f) / 0.2f) * (1.f - smooth01((kissT - 1.3f) / 0.3f));
        rot(iArmL, -0.45f * f);
        rot(iArmR, 0.45f * f);
        rot(iFArmL, -1.2f * f);
        rot(iFArmR, 1.2f * f);
        rot(iBody, -0.06f * std::max(e, f));
    }
    if (kissMode == KmShy) {
        float e = smooth01(kissT / 0.2f) * (1.f - smooth01((kissT - 1.1f) / 0.4f));
        rot(iArmL, -0.2f * e);
        rot(iArmR, 0.2f * e);
        rot(iFArmL, -0.9f * e);
        rot(iFArmR, 0.9f * e);
    }
}
