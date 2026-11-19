#include "pet.h"

void Pet::emote(const std::string& name, float arg) {
    auto sec = [&](float def) { return arg > 0 ? arg : def; };
    bool canTurn = st == St::Idle || st == St::Sit || st == St::Watch || st == St::Walk;
    if (name == "blush") tBlush = std::max(tBlush, sec(1.6f));
    else if (name == "happy") tHappy = std::max(tHappy, sec(1.4f));
    else if (name == "sad") tSad = std::max(tSad, sec(1.6f));
    else if (name == "angry") tAngry = std::max(tAngry, sec(1.2f));
    else if (name == "surprise") tSurprise = std::max(tSurprise, sec(0.8f));
    else if (name == "squint") tSquint = std::max(tSquint, sec(1.2f));
    else if (name == "heart") tHeart = std::max(tHeart, sec(1.4f));
    else if (name == "turn") {
        if (canTurn) facing = lastCursor.x > pos.x ? -1 : 1;
        emLookT = sec(1.8f);
        emLook = V2((float)facing * 0.5f, 0.3f);
    } else if (name == "hide") {
        emHideT = emHideLen = sec(2.f);
        tBlush = std::max(tBlush, emHideT);
    } else if (name == "steam") {
        emSteamT = sec(1.6f);
        emSteamTick = 0;
    } else if (name == "lookdown") {
        emLookT = sec(1.6f);
        emLook = V2(0, 0.8f);
    } else if (name == "lookaway") {
        emLookT = sec(1.6f);
        emLook = V2(lastCursor.x > pos.x ? -0.8f : 0.8f, 0.2f);
    } else if (name == "lookup") {
        emLookT = sec(1.4f);
        emLook = V2(0, -0.8f);
    } else if (name == "hug") {
        if (canTurn) facing = lastCursor.x > pos.x ? 1 : -1;
        emHugT = emHugLen = sec(2.5f);
        hugGrabTried = hugGrab = false;
        if ((st == St::Sit || st == St::Watch) && grounded() && !dragging) {
            hugResitSt = st;
            hugResit = std::max(20.f, stDur - stT);
            setState(St::Idle, emHugLen + 1.f);
        }
    } else if (name == "come" || name == "comesit") {
        comeToCursor(name == "comesit");
    } else if (name == "smooch") {
        smoochCursor();
    } else if (name == "play") {
        playMenuT = 1.6f;
    } else if (name == "nap") {
        napAt = app.now;
    } else if (name == "shiver" || name == "cold") {
        emShiverT = emShiverLen = std::min(sec(3.f), 20.f);
        tSquint = std::max(tSquint, std::min(emShiverT, 3.f));
        dayCount("cold");
        if (chance(0.5f)) cornerPhoto(L"я замёрзла");
    } else {
        logf("emote: unknown tag [%s]", name.c_str());
        return;
    }
    logf("emote: [%s%s]", name.c_str(), arg > 0 ? (":" + std::to_string(arg).substr(0, 4)).c_str() : "");
}

void Pet::comeToCursor(bool sit) {
    if (!grounded() || dragging || game || sleepCarry || feeding || st == St::Sleep || st == St::Watch || st == St::Inside || st == St::Hide ||
        !carriedName.empty() || baby())
        return;
    float side = lastCursor.x > pos.x ? 1.f : -1.f;
    float tx = clampf(lastCursor.x - side * 55.f * S(), gnd.x0 + 12.f * S(), gnd.x1 - 12.f * S());
    if (fabsf(tx - pos.x) < 24.f * S()) {
        facing = side > 0 ? 1 : -1;
        if (sit && st != St::Sit) setState(St::Sit, randf(40, 90));
        logf("come: already next to the cursor%s", sit ? ", sits" : "");
        return;
    }
    arriveSit = sit;
    arriveFace = true;
    goWalk(tx);
    logf("come: walks to the cursor (%.0f -> %.0f)%s", pos.x, tx, sit ? ", then sits" : "");
}

void Pet::smoochCursor() {
    if (st == St::Sleep || st == St::Inside || vit.fatal) return;
    if (fabsf(lastCursor.x - pos.x) > 6.f && (st == St::Idle || st == St::Sit || st == St::Stretch || st == St::LookAround)) facing = lastCursor.x > pos.x ? 1 : -1;
    tBlush = std::max(tBlush, 1.6f);
    tHappy = std::max(tHappy, 1.f);
    emLookT = 1.2f;
    emLook = V2(lastCursor.x > pos.x ? 0.6f : -0.6f, lastCursor.y < headC.y ? -0.4f : 0.2f);
    V2 m = mouthScreen();
    V2 to = lastCursor - m;
    float len = std::max(1.f, to.len());
    emit(0, m, to * (std::min(len, 400.f * S()) / len / 0.9f), 0.9f, 0.8f);
    emit(5, lastCursor + V2(10.f * S(), 6.f * S()), V2(0, -10.f * S()), 1.6f, 0.9f);
    logf("smooch: kissed the cursor at %.0f,%.0f", lastCursor.x, lastCursor.y);
}

void Pet::hugCursorStep(float dt) {
    if (hugResit > 0 && emHugT <= 0 && st == St::Idle && grounded() && !dragging) {
        setState(hugResitSt == St::Watch ? St::Watch : St::Sit, hugResit);
        hugResit = 0;
    }
    if (emHugT <= 0 || active != 0 || dragging) {
        hugGrab = false;
        return;
    }
    float el = emHugLen - emHugT;
    if (!hugGrab && !hugGrabTried && el >= 0.6f && emHugT > 0.6f) {
        hugGrabTried = true;
        V2 c = lastCursor;
        if (!app.world.fake) {
            POINT cp;
            if (GetCursorPos(&cp)) c = V2((float)cp.x, (float)cp.y);
        }
        if (app.settings.hugCursor && (c - headC).len() < 320.f * S()) {
            hugGrab = true;
            hugSet = false;
            hugFrom = c;
            hugK = 0;
            logf("hug: takes the cursor into her arms");
        }
    }
    if (!hugGrab) return;
    if (emHugT < 0.35f) {
        hugGrab = false;
        logf("hug: lets the cursor go");
        return;
    }
    V2 chest = (shoulderScreen(1) + shoulderScreen(2)) * 0.5f + V2(0, headR * 0.55f);
    hugK = std::min(1.f, hugK + dt / 0.3f);
    float k = hugK * hugK * (3.f - 2.f * hugK);
    hugAt = hugFrom + (chest - hugFrom) * k;
    hugSet = true;
    if (!app.world.fake) SetCursorPos((int)lroundf(hugAt.x), (int)lroundf(hugAt.y));
}

void Pet::napStep() {
    if (napAt < 0) return;
    if (st == St::Sleep) {
        napAt = -1;
        return;
    }
    if (app.now - napAt > 30.0) {
        logf("nap: promised to sleep but could not (state %s)", stateName());
        napAt = -1;
        return;
    }
    if (app.bubble.reading() || st == St::Walk || st == St::PrepJump || st == St::Air || st == St::Land || emHugT > 0) return;
    bool ok = grounded() && !dragging && !game && !vit.fatal && !baby() && !strollOn && !webActive() && !byeOn &&
              (st == St::Idle || st == St::Sit || st == St::Watch || st == St::LookAround || st == St::Stretch);
    if (!ok) return;
    napAt = -1;
    logf("nap: she said she'd sleep, lies down (energy %.0f)", vit.energy);
    sleepNow();
}

void Pet::emoteStep(float dt) {
    hugCursorStep(dt);
    peekStep();
    momentTouchStep();
    wantTick(dt);
    stirStep(dt);
    if (groggyT > 0) {
        groggyT -= dt;
        if (st != St::Sleep && chance(dt / 45.f)) {
            tSquint = std::max(tSquint, 1.2f);
            sayCat("moodYawn", 0.5f);
        }
    }
    napStep();
    if (!pendingSay.empty() && !app.bubble.held() && !app.bubble.asking()) {
        std::wstring s = pendingSay;
        std::string c = pendingCat;
        pendingSay.clear();
        if (app.now - pendingAt < 10.0 && st != St::Inside && !vit.fatal) {
            app.bubble.say(s);
            lastSay = app.now;
            momentNote(c.c_str());
            if (c.rfind("cuddle", 0) == 0) cuddleSaidAt = app.now;
            logf("say: queued %s now (waited %.1f s)", c.c_str(), app.now - pendingAt);
        } else {
            logf("say: queued %s dropped, too late (%.1f s)", c.c_str(), app.now - pendingAt);
        }
    }
    if (playMenuT > 0 && (playMenuT -= dt) <= 0 && app.bubble.held()) playMenuT = 0.2f;
    if (playMenuT <= 0 && playMenuT > -1) {
        playMenuT = -1;
        if (!game && !app.bubble.asking()) {
            app.bubble.close();
            gameMenu();
        }
    }
    if (!app.bubble.firedCues.empty()) {
        auto cs = std::move(app.bubble.firedCues);
        app.bubble.firedCues.clear();
        if (!vit.fatal && !vit.dead)
            for (auto& c : cs) emote(c.name, c.arg);
    }
    emHideT -= dt;
    emLookT -= dt;
    emHugT -= dt;
    if (emShiverT > 0) {
        emShiverT -= dt;
        emBreathT -= dt;
        if (emBreathT <= 0 && st != St::Sleep) {
            emBreathT = randf(1.0f, 1.6f);
            V2 m = mouthScreen();
            for (int i = 0; i < 3; i++)
                emit(6, m + V2((float)facing * headR * 0.1f, 0), V2((float)facing * randf(15, 35) * S(), -randf(15, 35) * S()), 0.55f, randf(0.45f, 0.65f));
        }
    }
    cuddleStep(dt);
    if (emSteamT > 0) {
        emSteamT -= dt;
        emSteamTick -= dt;
        if (emSteamTick <= 0) {
            emSteamTick = 0.12f;
            float side = chance(0.5f) ? 1.f : -1.f;
            emit(6, headTop + rotv(V2(side * headR * randf(0.15f, 0.65f), headR * 0.2f), rot), V2(side * randf(10, 35) * S(), -randf(60, 90) * S()),
                 0.9f, randf(0.8f, 1.15f));
        }
    }
}

void Pet::emoteFace(FaceState& tg, V2& lk) {
    if (emLookT > 0 || emHideT > 0) lk = emLookT > 0 ? emLook : V2((float)facing * 0.3f, 0.6f);
    if (emHideT > 0) { tg.squint = 1; tg.happy = 0; }
    if (emHugT > 0) { tg.heart = 1; tg.happy = tg.squint = 0; tg.blush = std::max(tg.blush, 0.9f); }
}

bool Pet::apiSay(const std::wstring& raw) {
    auto no = [&](const char* why) {
        logf("api say: refused (%s)", why);
        return false;
    };
    if (raw.empty()) return no("empty");
    if (vit.fatal || vit.dead || vit.intro != 0) return no("not now");
    if (!app.settings.talk || quiet()) return no("quiet");
    if (st == St::Sleep || game || dragging) return no("busy");
    if (app.bubble.asking() || app.bubble.held() || !pendingSay.empty() || (app.bubble.active && app.bubble.t < app.bubble.typeDur + 0.6f)) return no("talking");
    if (app.now - apiSayAt < 6.0) return no("too often");
    std::wstring s;
    for (wchar_t c : raw.substr(0, 280))
        if (c >= 0x20 || c == L'\n') s += c;
    apiSayAt = app.now;
    app.bubble.say(app.phrases.fill(s, L""));
    lastSay = app.now;
    logf("api say: '%s'", narrow(app.bubble.text).c_str());
    return true;
}

bool Pet::apiEmote(const std::wstring& raw) {
    if (vit.fatal || vit.dead || st == St::Sleep || app.now - apiEmoteAt < 1.0) {
        logf("api emote: refused");
        return false;
    }
    apiEmoteAt = app.now;
    std::string t = narrow(raw.substr(0, 200));
    for (char& c : t)
        if (c == '[' || c == ']' || c == ';' || c == ' ') c = ',';
    int n = 0;
    size_t a = 0;
    while (a < t.size() && n < 6) {
        size_t e = t.find(',', a);
        if (e == std::string::npos) e = t.size();
        std::string tag = t.substr(a, e - a);
        a = e + 1;
        if (tag.empty()) continue;
        size_t colon = tag.find(':');
        emote(tag.substr(0, colon), colon == std::string::npos ? -1.f : (float)atof(tag.c_str() + colon + 1));
        n++;
    }
    return n > 0;
}

void Pet::cuddleAdd(float heat, const char* why) {
    if (tier() < 2 || baby() || vit.fatal || st == St::Sleep) return;
    cuddleHeat = std::min(12.f, cuddleHeat + heat);
    if (why) logf("cuddle: +%.1f (%s) -> %.1f", heat, why, cuddleHeat);
}

void Pet::cuddleStep(float dt) {
    if (cuddleHeat <= 0 && cuddleStage == 0) return;
    if (!petting) cuddleHeat = std::max(0.f, cuddleHeat - dt * 0.12f);
    cuddleWait -= dt;
    int tr = tier();
    if (cuddleStage > 0 && cuddleHeat < 2.f) cuddleStage = 0;
    if (cuddleWait > 0 || app.bubble.asking()) return;
    bool calm = st == St::Idle || st == St::Sit || st == St::Watch || st == St::Walk || st == St::LookAround;
    if (!calm) return;
    if (tr == 2 && cuddleStage == 0 && cuddleHeat >= 6.f) {
        cuddleStage = 1;
        sayCat("cuddleHot", 1.f, L"", true);
        cuddleSaidAt = app.now;
        petting = false;
        petBlock = 5.f;
        lastPetEnd = app.now;
        cuddleWait = 4.f;
        return;
    }
    if (tr < 3) return;
    if (cuddleStage == 0 && cuddleHeat >= 4.f) {
        cuddleStage = 1;
        sayCat("cuddleHot", 1.f, L"", true);
        cuddleSaidAt = app.now;
        cuddleWait = 3.f;
    } else if (cuddleStage == 1 && cuddleHeat >= 7.f) {
        logf("cuddle: melt at heat %.1f", cuddleHeat);
        cuddleStage = 2;
        if (grounded() && st != St::Idle && st != St::Sit) setState(St::Idle, 4.f);
        sayCat("cuddleMelt", 1.f, L"", true);
        cuddleSaidAt = app.now;
        petting = false;
        petBlock = 4.f;
        lastPetEnd = app.now;
        laterCat = "cuddleReboot";
        laterT = 4.2f;
        cuddleWait = 5.f;
        cuddleSaidAt = app.now + 4.2;
    } else if (cuddleStage == 2 && cuddleHeat >= 9.5f && app.now - lastCuddleHug > 1800 && laterT <= 0 && app.now - cuddleSaidAt > 1.5) {
        logf("cuddle: hug at heat %.1f", cuddleHeat);
        cuddleStage = 3;
        lastCuddleHug = app.now;
        sayCat("cuddleHug", 1.f, L"", true);
        dayCount("hug");
        cornerPhoto(L"обнимашки");
        cuddleSaidAt = app.now;
        cuddleWait = 6.f;
        cuddleHeat = 5.f;
    }
}

void Pet::emotePose(float) {
    if (active != 0 || dragging) return;
    auto& P = curInst().pose;
    if (emHugT > 0) {
        float el = emHugLen - emHugT;
        float h = smooth01(el / 0.3f) * smooth01(emHugT / 0.4f);
        float wrap = smooth01((el - 0.7f) / 0.45f);
        V2 chest = (shoulderScreen(1) + shoulderScreen(2)) * 0.5f + V2(0, headR * 0.55f);
        for (int side = 1; side <= 2; side++) {
            int ai = side == 1 ? iArmL : iArmR, fi = side == 1 ? iFArmL : iFArmR;
            if (ai < 0 || fi < 0 || ai >= (int)P.size() || fi >= (int)P.size()) continue;
            PartPose a0 = P[ai], f0 = P[fi];
            V2 sh = shoulderScreen(side);
            float out = sh.x < chest.x ? -1.f : 1.f;
            V2 open = sh + V2(out * headR * 1.5f, -headR * 0.35f);
            V2 hugAt = chest + V2(-out * headR * 0.35f, 0);
            reachTo(open + (hugAt - open) * wrap, side);
            float ar = lerpf(a0.rot, P[ai].rot, h), fr = lerpf(f0.rot, P[fi].rot, h);
            P[ai] = a0;
            P[fi] = f0;
            P[ai].rot = ar;
            P[fi].rot = fr;
        }
        return;
    }
    if (emShiverT > 0 && emHideT <= 0) {
        float el = emShiverLen - emShiverT;
        float h = smooth01(el / 0.35f) * smooth01(emShiverT / 0.5f);
        V2 chest = (shoulderScreen(1) + shoulderScreen(2)) * 0.5f + V2(0, headR * 0.6f);
        for (int side = 1; side <= 2; side++) {
            int ai = side == 1 ? iArmL : iArmR, fi = side == 1 ? iFArmL : iFArmR;
            if (ai < 0 || fi < 0 || ai >= (int)P.size() || fi >= (int)P.size()) continue;
            PartPose a0 = P[ai], f0 = P[fi];
            V2 sh = shoulderScreen(side);
            float out = sh.x < chest.x ? -1.f : 1.f;
            reachTo(chest + V2(-out * headR * 0.45f, side == 1 ? 0.f : headR * 0.12f), side);
            float ar = lerpf(a0.rot, P[ai].rot, h), fr = lerpf(f0.rot, P[fi].rot, h);
            P[ai] = a0;
            P[fi] = f0;
            P[ai].rot = ar;
            P[fi].rot = fr;
        }
        float tt = (float)app.now;
        for (size_t i = 0; i < P.size(); i++) P[i].rot += sinf(tt * 48.f + (float)i * 1.7f) * 0.018f * h;
        return;
    }
    if (emHideT <= 0) return;
    int ai = iArmR, fi = iFArmR;
    if (ai < 0 || fi < 0 || ai >= (int)P.size() || fi >= (int)P.size()) return;
    float h = smooth01((emHideLen - emHideT) / 0.25f) * smooth01(emHideT / 0.35f);
    PartPose a0 = P[ai], f0 = P[fi];
    V2 sh = shoulderScreen(2), a = visorScreen(V2(0.45f, 0.35f)), b = visorScreen(V2(-0.45f, 0.35f));
    reachTo((a - sh).len() < (b - sh).len() ? a : b, 2);
    float ar = lerpf(a0.rot, P[ai].rot, h), fr = lerpf(f0.rot, P[fi].rot, h);
    P[ai] = a0;
    P[fi] = f0;
    P[ai].rot = ar;
    P[fi].rot = fr;
    if (h > 0.3f) P[ai].zOff = P[fi].zOff = 25;
}
