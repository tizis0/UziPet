#include "pet.h"
#include "webnest.h"
#include "intro.h"
#include "games.h"
#include <ctime>

using M3 = D2D1::Matrix3x2F;

static const char* kStateNames[] = {"Fall",  "Idle",    "Walk",  "Sit",  "Sleep", "PrepJump", "Air",    "Land",   "Dragged", "Dizzy",
                                    "LookAround", "GrabIcon", "DropIcon", "Wave", "Bite", "Leap", "Cling", "Led", "Watch", "Stretch",
                                    "Ears",  "Lift",    "Hide",  "Dive", "Inside", "Emerge", "Climb", "Clap", "Shoot", "Ride", "Fatal", "Trip"};

const char* Pet::stateName() const { return kStateNames[(int)st]; }

const char* Pet::babyCat(const char* cat) const {
    std::string c = cat;
    if (c.rfind("baby", 0) == 0 || c == "grownHello") return cat;
    if (c.rfind("hungry", 0) == 0) return "babyCry";
    static const char* cry[] = {"sleepMoan", "wokenUp", "wokenTired", "grab", "grabLeg", "grabHand", "thrown", "hardLand", "dizzy",
                                "poke", "pokeMany", "pokeWarn", "linkGone", "feedRefuse", "clingShaken", "petTooMuch", "petWarn"};
    for (auto x : cry)
        if (c == x) return "babyCry";
    static const char* happy[] = {"petStart", "petMore", "petLove", "petWarm", "purr", "eatYum", "carryCalm",
                                  "wakeRested", "summon", "led", "parcelLanded"};
    for (auto x : happy)
        if (c == x) return "babyHappy";
    if (c.rfind("idle", 0) == 0 || c == "wake") return "babyCoo";
    return nullptr;
}

bool Pet::calm() const {
    if (st == St::Inside) return true;
    if (solverT >= 0) return false;
    if (!(st == St::Idle || st == St::Sit || st == St::Sleep || st == St::Watch)) return false;
    if (petting || dragging || bWalk > 0.05f || bAir > 0.05f || bDrag > 0.05f || fabsf(squash) > 0.01f) return false;
    if (app.bubble.active && app.bubble.t < app.bubble.typeDur + 0.3f) return false;
    if ((lastCursor - headC).len() < 300.f * app.s) return false;
    return true;
}

void Pet::bindRig() {
    app.inst.bind(&app.rig);
    if (app.rigSit.ok) app.instSit.bind(&app.rigSit);
    if (app.rigSleep.ok) app.instSleep.bind(&app.rigSleep);
    if (app.rigBaby.ok) app.instBaby.bind(&app.rigBaby);
    const Rig& r = app.rig;
    int legL = r.find("leg_l");
    comH = -r.center.y;
    legLen = legL >= 0 ? -r.parts[legL].joint.y : 30.f * S();
    selectRig(rigAt(active).ok ? active : 0);
}

void Pet::selectRig(int a) {
    active = a;
    const Rig& r = rigAt(a);
    iBody = r.find("body");
    if (iBody < 0) iBody = r.find("torso");
    iHead = r.find("head");
    iFArmL = r.find("forearm_l");
    iFArmR = r.find("forearm_r");
    iHairF = r.find("hair_front");
    iHairB = r.find("hair_back");
    iBeanie = r.find("beanie");
    iArmL = r.find("arm_l");
    iArmR = r.find("arm_r");
    iLegL = r.find("leg_l");
    iLegR = r.find("leg_r");
    iLegs = r.find("legs");
    iShinL = r.find("shin_l");
    iShinR = r.find("shin_r");
    headR = r.headRadius;
    comH = a == 3 ? -r.center.y : -app.rig.center.y;
}

void Pet::spawn(V2 at) {
    pos = at;
    vel = {};
    rot = rotVel = 0;
    gnd.valid = false;
    setState(St::Fall);
    app.inst.resetSim();
}

bool Pet::grounded() const {
    switch (st) {
    case St::Idle: case St::Walk: case St::Sit: case St::Sleep: case St::PrepJump: case St::Land: case St::Dizzy:
    case St::LookAround: case St::GrabIcon: case St::DropIcon: case St::Wave: case St::Bite: case St::Led:
    case St::Watch: case St::Stretch: case St::Ears: case St::Lift: case St::Hide: case St::Clap: case St::Shoot: case St::Fatal: case St::Trip:
        return true;
    default:
        return false;
    }
}

void Pet::setState(St s, float dur) {
    if (s != st) logf("state %s -> %s", stateName(), kStateNames[(int)s]);
    auto air = [](St x) { return x == St::Air || x == St::Fall || x == St::Leap; };
    if (air(s) && !air(st)) {
        airFrom = pos;
        airPlanned = st == St::PrepJump;
        if (!airPlanned) jumpWin = nullptr;
        fromWin = standWin;
    }
    if (s == St::Dragged || s == St::Cling || s == St::Led) standWin = nullptr;
    if (s != St::Hide && s != St::Dive && s != St::Inside && s != St::Emerge) sink = 0;
    if (s == St::Inside && !app.bubble.asking()) app.bubble.close();
    st = s;
    stT = 0;
    stDur = dur;
}

void Pet::goWalk(float x, bool off) {
    walkTarget = x;
    setState(St::Walk);
    walkOff = off;
}

void Pet::goIdle(float minT, float maxT) { setState(St::Idle, randf(minT, maxT)); }

void Pet::scriptedPaceStep(float speed, bool acrossMonitors) {
    if (speed <= 0) {
        if (scriptedWalkSpeed > 0 && st == St::Walk) goIdle(1.f, 2.f);
        scriptedWalkSpeed = 0;
        return;
    }
    if (vit.dead || vit.fatal || dragging || byeOn || (app.bubble.asking() && !app.bubble.modForm())) return;
    if (st == St::Hide || st == St::Inside) {
        callBack(lastCursor);
        return;
    }
    if (st == St::Sleep && !carriedAsleep()) wake(true);
    if (st == St::Walk && scriptedWalkSpeed > 0) {
        scriptedWalkSpeed = speed;
        return;
    }
    if (st != St::Idle && st != St::Walk && st != St::Sit && st != St::Watch && st != St::LookAround) return;
    if (!gnd.valid) return;
    followT = 0;
    arriveSit = arriveFace = arriveHug = false;
    float margin = std::min(70.f * S(), (gnd.x1 - gnd.x0) * 0.18f);
    float left = gnd.x0 + margin, right = gnd.x1 - margin;
    bool atEdge = std::min(fabsf(left - pos.x), fabsf(right - pos.x)) < 5.f * S();
    auto& monitors = app.world.mons;
    if (scriptedWalkSpeed > 0 && atEdge && acrossMonitors && monitors.size() > 1) {
        int current = app.world.nearestMonitor(pos);
        const RECT& next = monitors[(current + 1) % monitors.size()].work;
        float edge = std::min(80.f * S(), (next.right - next.left) * 0.25f);
        float target = clampf(pos.x, next.left + edge, next.right - edge);
        if (!goJump(V2(target, (float)next.bottom), 38.f, 90.f))
            spawn(V2(target, (float)next.top + comH * 2.f));
        return;
    }
    if (right - left < 8.f * S()) return;
    scriptedWalkSpeed = speed;
    goWalk(pos.x - left < right - pos.x ? right : left);
}

void Pet::startFall(V2 v0) {
    vel = v0;
    gnd.valid = false;
    walkOff = false;
    setState(St::Fall);
}

void Pet::sayCat(const char* cat, float prob, const std::wstring& arg, bool force) {
    syncStage();
    if (st == St::Inside || app.bubble.asking() || vit.fatal) return;
    if (vit.intro == 1) return;
    if (byeOn) return;
    if (baby() && !(cat = babyCat(cat))) return;
    if (!chance(prob)) return;
    if (app.bubble.held()) {
        if (!force) return;
        std::wstring q = app.phrases.pick(cat, arg);
        if (q.empty()) return;
        if (!pendingSay.empty()) logf("say: queued line replaced ('%s' -> '%s')", narrow(pendingSay).c_str(), narrow(q).c_str());
        pendingSay = q;
        pendingCat = cat;
        pendingAt = app.now;
        logf("say: %s waits until '%s' is read (%.1f of %.1f s)", cat, narrow(app.bubble.text).c_str(), app.bubble.t, app.bubble.holdT);
        return;
    }
    if (app.bubble.active && app.bubble.age() < (force ? 0.8f : 1.6f)) return;
    if (!force && app.now - lastSay < 3.0) return;
    std::wstring s = app.phrases.pick(cat, arg);
    if (s.empty()) return;
    app.bubble.say(s);
    lastSay = app.now;
    momentNote(cat);
}

static const char* greetCat() {
    time_t tt = time(nullptr);
    tm lt;
    localtime_s(&lt, &tt);
    if (lt.tm_hour >= 23 || lt.tm_hour < 5) return "night";
    if (lt.tm_hour >= 5 && lt.tm_hour < 11) return "morning";
    return "greet";
}

void Pet::update(float dt, const Input& in) {
    syncStage();
    app.world.winRoom = comH * 2.2f + 10.f;
    t += dt;
    stT += dt;
    face.time = t;
    tripUpset = std::max(0.f, tripUpset - dt);

    float idt = 1.f / std::max(dt, 1e-4f);
    V2 rawV = (in.cursor - lastCursor) * idt;
    V2 prevCV = cursorVel;
    cursorVel += (rawV - cursorVel) * (1 - expf(-25.f * dt));
    V2 rawA = (cursorVel - prevCV) * idt;
    cursorAcc += (rawA - cursorAcc) * (1 - expf(-18.f * dt));
    updatePetting(dt, in);
    if (tier() == 0 && !baby() && !dragging && !game && kissT < 0 && (st == St::Idle || st == St::Sit) && (in.cursor - headC).len() < 300.f * S() &&
        fabsf(in.cursor.x - pos.x) > 20.f * S())
        facing = in.cursor.x > pos.x ? -1 : 1;
    lastCursor = in.cursor;

    if (baby() && !app.rigBaby.hull.empty() && st != St::Dragged && !introHidden) babyStep(dt, in);
    else switch (st) {
    case St::Fall:
    case St::Air:
        airStep(dt);
        break;

    case St::Dragged: {
        V2 a = cursorAcc * 0.75f;
        float lim = 30000.f * S();
        if (a.len() > lim) a = a.norm() * lim;
        float Leff = std::max(pendL, comH * 0.9f);
        float damp = 4.2f + 9.f * (1.f - clampf(pendL / (comH * 1.2f), 0.f, 1.f));
        float acc = (-(G() - a.y) * sinf(pend) - a.x * cosf(pend)) / Leff - pendV * damp;
        pendV = clampf(pendV + acc * dt, -30.f, 30.f);
        pend += pendV * dt;
        V2 d(sinf(pend), cosf(pend));
        V2 c = in.cursor + d * pendL;
        rot = atan2f(d.y, d.x) - grabBeta;
        pos = c + V2(0, comH);
        dragAgit = clampf(fabsf(pendV) / 7.f + cursorVel.len() / (2600.f * S()), 0.f, 1.f);
        spin += fabsf(pendV) * dt;
        float shk = shakeMeter(in.cursor, dt);
        dragRough = std::max(0.f, dragRough - dt * 0.8f) +
                    (std::max(0.f, shk - 160.f * S()) / (160.f * S()) + std::max(0.f, fabsf(pendV) - 3.f) / 3.f) * dt * 2.f;
        int tr = tier();
        if (!dragSaid && tr == 3 && dragRough > 0.5f) {
            dragSaid = true;
            sayCat("carryOops", 0.8f, L"", true);
        } else if (!dragSaid && (dragRough > 0.5f || (tr == 0 && stT > 0.3f))) {
            dragSaid = true;
            sayCat("grab", 0.8f, L"", true);
        } else if (!dragSaid && stT > 1.4f) {
            dragSaid = true;
            sayCat("carryCalm", 0.6f, L"", true);
        }
        break;
    }

    case St::PrepJump:
        if (!groundCheck(dt)) { startFall({}); break; }
        if (stT >= stDur) {
            vel = jumpVel;
            gnd.valid = false;
            squashV += 2.5f;
            setState(St::Air);
        }
        break;

    case St::Land:
        if (!groundCheck(dt)) { startFall({}); break; }
        rot = approach(rot, 0, 16, dt);
        if (stT >= stDur) {
            rot = 0;
            if (!carriedName.empty()) carryDecide();
            else goIdle(0.6f, 2.0f);
        }
        break;

    case St::Idle:
        if (!groundCheck(dt)) { startFall({}); break; }
        if (stT >= stDur) decide(in);
        break;

    case St::Walk: {
        if (!groundCheck(dt)) { startFall(V2(facing * 60.f * S(), 0)); break; }
        float dir = signf(walkTarget - pos.x);
        facing = dir > 0 ? 1 : -1;
        float speedFactor = scriptedWalkSpeed > 0 ? scriptedWalkSpeed : followT > 0 ? 2.2f : 1.f;
        float sp = 80.f * S() * (carriedName.empty() ? 1.f : 0.8f) * speedFactor * runK * lerpf(1.f, 0.45f, hungerK()) * (groggyT > 0 ? 0.6f : 1.f);
        float step = sp * dt;
        walkPhase += dt * sp / (9.f * S() * (1.f + 0.5f * (runK - 1.f)));
        if (fabsf(walkTarget - pos.x) <= step && !walkOff) {
            pos.x = walkTarget;
            if (!carriedName.empty()) carryDecide();
            else if (arriveSit) setState(St::Sit, randf(40, 90));
            else if (!peekPath.empty() && app.now - peekGoAt < 40.0) peekArrive();
            else goIdle();
            if (arriveFace && fabsf(lastCursor.x - pos.x) > 6.f) facing = lastCursor.x > pos.x ? 1 : -1;
            arriveSit = arriveFace = false;
            if (arriveHug) {
                arriveHug = false;
                emote("hug", 2.8f);
            }
            break;
        }
        pos.x += dir * step;
        if (pos.x < gnd.x0 || pos.x > gnd.x1) {
            if (walkOff) { startFall(V2(dir * sp * 1.3f, -150.f * S())); break; }
            pos.x = clampf(pos.x, gnd.x0, gnd.x1);
            if (!carriedName.empty()) carryDecide();
            else goIdle();
            break;
        }
        if (stT > 25) goIdle();
        break;
    }

    case St::Sit:
        if (!groundCheck(dt)) { startFall({}); break; }
        if (stT >= stDur) goIdle(0.5f, 1.5f);
        break;

    case St::Sleep:
        if (carriedAsleep()) carryStep(dt, in);
        else if (!groundCheck(dt)) { startFall({}); tSurprise = 0.8f; break; }
        if (st != St::Sleep) break;
        zzzT -= dt;
        if (zzzT <= 0) {
            zzzT = 1.3f;
            emit(1, headTop + V2(facing * headR * 0.55f, headR * 0.2f), V2(facing * 16.f * S(), -32.f * S()), 2.4f, 1.f);
        }
        break;

    case St::Trip:
        tripState(dt);
        break;

    case St::Dizzy:
        if (!groundCheck(dt)) { startFall({}); break; }
        rot = approach(rot, 0, 10, dt);
        if (stT >= stDur) {
            if (!carriedName.empty()) carryDecide();
            else goIdle();
        }
        break;

    case St::LookAround:
        if (!groundCheck(dt)) { startFall({}); break; }
        lookTurn = sinf(stT * 2.4f);
        if (stT >= stDur * 0.5f && stT - dt < stDur * 0.5f) facing = -facing;
        if (stT >= stDur) { lookTurn = 0; goIdle(); }
        break;

    case St::Wave:
        if (!groundCheck(dt)) { startFall({}); break; }
        if (stT >= stDur) {
            if (sitAfterWave) {
                sitAfterWave = false;
                setState(St::Sit, randf(150, 300));
            } else {
                goIdle();
            }
        }
        break;

    case St::GrabIcon:
        if (!groundCheck(dt)) { fetchName.clear(); startFall({}); break; }
        if (stT >= 0.35f) {
            grabIcon();
            startFall(V2(0, -280.f * S()));
        }
        break;

    case St::DropIcon:
        if (!groundCheck(dt)) { startFall({}); break; }
        if (stT >= 0.3f && !carriedName.empty()) {
            dropCarried(true);
            tHappy = 1.3f;
            sayCat("dropIcon", 0.8f, L"", true);
        }
        if (stT >= 0.65f) goIdle(1.5f, 3.f);
        break;

    case St::Bite:
        if (!groundCheck(dt)) { startFall({}); break; }
        if (stT >= 0.2f && stT - dt < 0.2f) {
            emit(3, lastCursor + V2(16.f, -10.f) * S(), V2(0, -45.f * S()), 0.9f, 1.f);
            app.flash = 0.8f;
            app.flashAt = headC;
        }
        if (stT >= stDur) goIdle(1.f, 2.f);
        break;

    case St::Leap:
        airStep(dt);
        if (st == St::Leap && !game && (in.cursor - headTop).len() < headR * 1.2f) beginCling(in.cursor);
        break;

    case St::Cling: {
        V2 a = cursorAcc * 0.6f;
        float lim = 30000.f * S();
        if (a.len() > lim) a = a.norm() * lim;
        clingCyc += dt;
        float ph = fmodf(clingCyc, 1.9f);
        float stretch = ph < 0.35f ? smooth01(ph / 0.35f) : ph < 0.47f ? 1.f - smooth01((ph - 0.35f) / 0.12f) : 0.f;
        if (ph >= 0.47f && ph - dt < 0.47f) {
            app.flash = 0.7f;
            app.flashAt = in.cursor;
            if (chance(0.4f)) emit(3, in.cursor + V2(16.f, -14.f) * S(), V2(0, -45.f * S()), 0.8f, 1.f);
        }
        clingMouth = stretch > 0.05f ? 0.55f + 0.45f * stretch : 0.22f + 0.18f * fabsf(sinf(t * 10.f));
        pendL = clingL0 * (1.f + 0.16f * stretch);
        float acc = (-(G() - a.y) * sinf(pend) - a.x * cosf(pend)) / pendL - pendV * 5.f;
        pendV = clampf(pendV + acc * dt, -20.f, 20.f);
        pend += pendV * dt;
        V2 d(sinf(pend), cosf(pend));
        V2 c = in.cursor + d * pendL;
        rot = atan2f(d.y, d.x) - grabBeta;
        pos = c + V2(0, comH);
        float sp = cursorVel.len();
        shakeAcc = std::max(0.f, shakeAcc - dt * 0.7f) + std::max(0.f, sp - 1500.f * S()) * dt / (1100.f * S());
        dragAgit = clampf(fabsf(pendV) / 7.f + sp / (2600.f * S()), 0.f, 1.f);
        if (shakeAcc > 1.f) endCling(true);
        else if (stT >= stDur) endCling(false);
        break;
    }

    case St::Led: {
        if (!groundCheck(dt)) { hangByPin(in.cursor, true); sayCat("grabHand", 0.8f, L"", true); break; }
        V2 sh = shoulderScreen(grabHand);
        V2 d = in.cursor - sh;
        float reach = 0.f;
        {
            const Rig& R0 = app.rig;
            int ai = grabHand == 1 ? iArmL : iArmR, hi = grabHand == 1 ? (iFArmL >= 0 ? iFArmL : iArmL) : (iFArmR >= 0 ? iFArmR : iArmR);
            if (ai >= 0 && hi >= 0) {
                const PartDef& hd = R0.parts[hi];
                reach = (hd.hand() - R0.parts[ai].joint).len();
            }
        }
        if (pinPart >= 0) reach = pinReach;
        if (reach < 1.f) reach = 60.f * S();
        if (d.y < -reach * 0.8f || d.len() > reach * 3.f) {
            hangByPin(in.cursor, true);
            sayCat("grabHand", 0.8f, L"", true);
            break;
        }
        float lag = fabsf(d.x) - reach * 0.5f;
        float want = lag > 0 ? signf(d.x) * std::min(lag * 10.f, 420.f * S()) : 0.f;
        if (tier() == 0) want *= 0.5f;
        ledSpeed = approach(ledSpeed, want, 10, dt);
        if (fabsf(ledSpeed) > 8.f * S()) facing = ledSpeed > 0 ? 1 : -1;
        pos.x += ledSpeed * dt;
        walkPhase += dt * fabsf(ledSpeed) / (9.f * S());
        if (pos.x < gnd.x0 || pos.x > gnd.x1) {
            startFall(V2(ledSpeed, 0));
            hangByPin(in.cursor, true);
        }
        break;
    }
    default:
        lifeState(dt, in);
        break;
    }
    if (st != St::LookAround && st != St::Hide && st != St::Emerge) lookTurn = approach(lookTurn, 0, 6, dt);
    if (grounded() && !baby() && st != St::Land && st != St::Dizzy && st != St::Fatal && st != St::Trip && rot != 0.f) {
        rot = approach(rot, 0.f, 12.f, dt);
        if (fabsf(rot) < 0.002f) rot = 0.f;
    }

    bool reading = (app.bubble.asking() || app.bubble.held()) && app.now - lingerWarnAt >= 8.0;
    if (st == St::Idle && !petting && !weak() && !game && !kissAim && kissT < 0 && !reading && (in.cursor - headC).len() < headR * 1.5f &&
        cursorVel.len() < 80.f * S()) {
        lingerT += dt;
        bool warned = app.now - lingerWarnAt < 8.0;
        if (tier() == 3) {
            if (lingerT > 3.5f) {
                lingerT = 0;
                if (app.now - lingerLoveAt > 60.0) {
                    lingerLoveAt = app.now;
                    if (chance(0.5f)) {
                        tBlush = std::max(tBlush, 3.f);
                        emLookT = 1.6f;
                        emLook = V2(lastCursor.x > pos.x ? -0.8f : 0.8f, 0.2f);
                        sayCat("lingerShy", 1.f, L"", true);
                    } else {
                        tHappy = std::max(tHappy, 2.f);
                        tBlush = std::max(tBlush, 1.6f);
                        emote("hug", 2.2f);
                        sayCat("lingerWarm", 1.f, L"", true);
                    }
                    logf("linger at love: no bite, %s", narrow(app.bubble.text).c_str());
                }
            }
        } else if (!warned && lingerT > (tier() == 0 ? 1.5f : 3.5f)) {
            lingerT = 0;
            warnBite("biteWarn");
        } else if (warned && lingerT > (tier() == 0 ? 1.0f : 1.6f)) {
            lingerT = 0;
            lingerWarnAt = -100;
            sayCat("linger", 0.8f, L"", true);
            startBite();
        }
    } else {
        lingerT = 0;
    }
    if (biteQueued && active == 0) {
        biteQueued = false;
        if (st == St::Idle && (in.cursor - headC).len() < headR * 2.6f) startBite();
    }
    vengeT -= dt;
    if (vengeT > 0 && !game && (st == St::Idle || st == St::Walk || st == St::LookAround) && carriedName.empty() && active == 0) {
        float d = (in.cursor - headC).len();
        if (d > headR * 2.5f && d < 650.f * S() && in.cursor.y < headC.y && startLeap(in.cursor)) vengeT = 0;
    }

    fallGuard(dt);
    if (!carriedName.empty()) moveCarriedIcon(dt, false);

    if (!heldPath.empty() && !heldDragging) {
        heldT += dt;
        if (heldStage == 0 && heldT > 2.4f) {
            heldStage = 1;
            if (senseJob) {
                if (senseJob->done) heldSense = senseJob->fs;
                else {
                    senseFinish(heldSense);
                    logf("file sense: thumbnail not ready, name only");
                }
                senseJob.reset();
            }
            const FileSense& fs = heldSense;
            logf("file sense: self %d kind %s with %s name %.2f folder %.2f color %.2f (purple %.3f dark %.3f eye %.4f edge %.3f bg %.2f pixels %d %.0f ms)",
                 fs.self, fileKindName(fs.kind), fileKindName(fs.with), fs.nameScore, fs.folderScore, fs.colorScore, fs.purple, fs.dark, fs.eye,
                 fs.edge, fs.bg, (int)fs.hadPixels, fs.ms);
            std::wstring name = heldPath.substr(heldPath.find_last_of(L"\\/") + 1);
            if (name.size() > 22) name = name.substr(0, 20) + L"…";
            if (heldCorner) {
                sayCat("cornerHeld", 1.f, L"", true);
                logf("corner: her own folder in her hands (tier %d)", tier());
            } else if (peekOn) {
                if (chance(0.4f)) sayCat("peekMutter", 1.f, L"", true);
            } else
                sayCat(fileSenseReact(), 1.f, name, true);
        }
        if (heldHappyAt >= 0 && heldT >= heldHappyAt) {
            tHappy = 2.2f;
            heldHappyAt = -1;
        }
        if (heldStage == 1 && heldT > heldLife * 0.62f) {
            heldStage = 2;
            FileKind wk = heldSense.self > 0 && tier() >= 1 && !baby() ? heldSense.with : FileKind::None;
            if (peekOn) {
            } else if (heldCorner) {
                sayCat("cornerHeldBack", 1.f, L"", true);
            } else if (wk == FileKind::Cyn) {
                tSurprise = 0.8f;
                sayCat("fileSelfWithCyn", 1.f, L"", true);
            }
            else if (wk == FileKind::J) sayCat("fileSelfWithJ", 1.f, L"", true);
            else if (wk != FileKind::None) sayCat("fileSelfWith", 1.f, fileKindSay(wk), true);
            else sayCat("fileWait", 0.8f, L"", true);
        }
        if (heldT >= heldLife && grounded() && deskOn()) {
            if (deskMode == DeskHold) deskReturn();
        } else if (heldT >= heldLife && grounded()) {
            for (int i = 0; i < 6; i++)
                emit(4, carriedCenter + V2(randf(-12, 12) * S(), randf(-6, 6) * S()), V2(randf(-70, 70) * S(), randf(-60, 10) * S()), 0.6f, 1.f);
            heldPath.clear();
            heldImg.reset();
            heldImg.src.Reset();
            heldHalo.reset();
            heldHalo.src.Reset();
            sayCat("filePutDown", 1.f, L"", true);
            goIdle(1.f, 2.f);
        }
    }
    app.icons.pump(app.now);
    deskRetryHands();
    deskUpdate(dt);
    tugStep(dt);
    if (fileHovering) {
        hoverT += dt;
        if (hoverT > 0.5f && !hoverSaid) {
            hoverSaid = true;
            sayCat("fileOffer", 0.9f, L"", true);
        }
    }

    lifeUpdate(dt, in);
    if (app.settings.talk && grounded() && st != St::Sleep && st != St::Watch && st != St::Hide && !petting && !webActive() && solverT < 0 && !game) {
        talkCd -= dt;
        if (talkCd <= 0 && (in.idleSec < 6 || quiet() || strollOn)) talkCd = 25;
        if (talkCd <= 0) {
            talkCd = jitterTime(480.f, 150.f, 1500.f) * (tier() == 3 ? 1.3f : tier() == 0 ? 0.7f : 1.f) * moodK(MkTalk);
            if (!(chance(0.3f) && talkStartRandom())) sayCat(idleCat());
        }
    }
    int want = (baby() && app.rigBaby.ok) ? 3 : ((st == St::Sit || st == St::Watch) && app.rigSit.ok) ? 1 : (st == St::Sleep && app.rigSleep.ok) ? 2 : 0;
    if (want != active) {
        selectRig(want);
        curInst().resetSim();
        squashV -= 2.2f;
    }
    kissStep(dt);
    emoteStep(dt);
    updateFace(dt, in);
    animate(dt);
    updateParticles(dt);
}

void Pet::airStep(float dt) {
    V2 prev = pos;
    if (biteHop && vel.y > -60.f * S()) {
        biteHop = false;
        biteSnapT = 0.35f;
        emit(3, lastCursor + V2(16.f, -10.f) * S(), V2(0, -45.f * S()), 0.9f, 1.f);
        app.flash = 0.8f;
        app.flashAt = lastCursor;
    }
    vel.y += G() * dt;
    if (st == St::Fall) vel.x *= expf(-0.35f * dt);
    pos += vel * dt;
    rot = wrapAngle(rot + rotVel * dt);
    rotVel *= expf(-1.1f * dt);
    if (fabsf(rotVel) < 5.f) rot = approach(rot, 0, 2.4f, dt);
    if (st == St::Air || st == St::Leap) rot = approach(rot, clampf(vel.x / (5200.f * S()), -0.2f, 0.2f), 7.f, dt);

    auto& W = app.world;
    if (W.web) {
        V2 c = com();
        const RECT& r = W.webRc;
        float half = 30.f * S();
        if (c.x < r.left + half) { pos.x += r.left + half - c.x; if (vel.x < 0) vel.x = -vel.x * 0.45f; }
        if (c.x > r.right - half) { pos.x -= c.x - (r.right - half); if (vel.x > 0) vel.x = -vel.x * 0.45f; }
        float headUp = comH * 1.1f;
        if (c.y - headUp < r.top) { pos.y += r.top - (c.y - headUp); if (vel.y < 0) vel.y = -vel.y * 0.3f; }
        float bottom = (float)r.bottom;
        if (pos.y > bottom + 2 && std::min(headTop.y, c.y) <= bottom && prev.y > bottom + 1) {
            for (auto& s : W.surfs)
                if (s.kind == SurfKind::Web && s.index == -1 && pos.x >= s.x0 - 6 && pos.x <= s.x1 + 6) {
                    pos.y = s.y;
                    vel.y = 0;
                    Surf f = s;
                    land(f);
                    return;
                }
        }
    } else if (!W.mons.empty()) {
        V2 c = com();
        int m = W.monitorAt(c);
        if (m < 0) m = W.nearestMonitor(c);
        const RECT& r = W.mons[m].rc;
        float half = 34.f * S();
        if (c.x < r.left + half && W.monitorAt(V2(r.left - 2.f, c.y)) < 0) {
            pos.x += r.left + half - c.x;
            if (vel.x < 0) vel.x = -vel.x * 0.45f;
            rotVel = -rotVel * 0.6f;
        }
        if (c.x > r.right - half && W.monitorAt(V2(r.right + 2.f, c.y)) < 0) {
            pos.x -= c.x - (r.right - half);
            if (vel.x > 0) vel.x = -vel.x * 0.45f;
            rotVel = -rotVel * 0.6f;
        }
        float headUp = comH * 1.1f;
        if (c.y - headUp < r.top && W.monitorAt(V2(c.x, r.top - 2.f)) < 0) {
            pos.y += r.top - (c.y - headUp);
            if (vel.y < 0) vel.y = -vel.y * 0.3f;
        }
        float floorY = (float)W.mons[m].work.bottom;
        if (pos.y > floorY + 2 && std::min(headTop.y, com().y) <= W.mons[m].rc.bottom && prev.y > floorY + 1) {
            pos.y = floorY;
            prev.y = floorY - 1;
            if (vel.y < 0) vel.y = 0;
        }
    }
    if (vel.y > 0)
        if (const Surf* s = findLanding(prev, pos)) land(*s);
}

void Pet::fallGuard(float dt) {
    if ((st != St::Fall && st != St::Air) || dragging) {
        fallT = outT = 0;
        return;
    }
    fallT += dt;
    auto& W = app.world;
    if (W.mons.empty()) return;
    V2 c = com();
    const char* why = nullptr;
    float lowest = -1e9f;
    for (auto& s : W.surfs) lowest = std::max(lowest, s.y);
    if (W.web) lowest = std::max(lowest, (float)W.webRc.bottom);
    else for (auto& m : W.mons) lowest = std::max(lowest, (float)m.rc.bottom);
    if (std::min(headTop.y, c.y) > lowest + 10.f * S()) why = W.web ? "below the page" : "below every floor";
    outT = W.monitorAt(c) < 0 && W.monitorAt(pos - V2(0, 4)) < 0 ? outT + dt : 0.f;
    if (!why && outT > 0.6f) why = "off every monitor";
    if (!why && fallT > 5.f) why = "falling too long";
    if (why) fellOut(why);
}

void Pet::fellOut(const char* why) {
    auto& W = app.world;
    if (W.mons.empty()) return;
    V2 c = com();
    Surf f;
    bool page = false;
    if (W.web) {
        f = Surf{SurfKind::Web, nullptr, -1, (float)W.webRc.bottom, (float)W.webRc.left + 6.f, (float)W.webRc.right - 6.f};
        int m = W.nearestMonitor(V2(clampf(c.x, f.x0, f.x1), f.y - 2.f));
        const RECT& mr = W.mons[m].rc;
        f.x0 = std::max(f.x0, (float)mr.left + 6.f);
        f.x1 = std::min(f.x1, (float)mr.right - 6.f);
        page = f.x1 - f.x0 >= 80.f * S() && f.y <= mr.bottom;
    }
    if (!page) {
        int m = W.monitorAt(c);
        if (m < 0) m = W.nearestMonitor(c);
        const RECT& w = W.mons[m].work;
        f = Surf{SurfKind::Floor, nullptr, m, (float)w.bottom, (float)w.left, (float)w.right};
    }
    float pad = std::min(60.f * S(), (f.x1 - f.x0) * 0.4f);
    float x = clampf(pos.x, f.x0 + pad, f.x1 - pad);
    logf("fell out (%s): feet %.0f,%.0f com %.0f,%.0f, %.1f s in the air, back on %s %d at %.0f,%.0f", why, pos.x, pos.y, c.x, c.y, fallT,
         f.kind == SurfKind::Web ? "the page bottom" : "the floor of monitor", f.index, x, f.y);
    pos = V2(x, f.y);
    vel = {};
    rot = rotVel = 0;
    setGround(f);
    app.inst.resetSim();
    webPress = -1;
    fetchName.clear();
    fallT = outT = 0;
    fellOuts++;
    fellWhy = why;
    squashV -= 2.f;
    for (int i = 0; i < 12; i++)
        emit(4, pos + V2(randf(-34, 34) * S(), -randf(0, 26) * S()), V2(randf(-130, 130) * S(), randf(-90, -10) * S()), 0.7f, randf(0.8f, 1.3f));
    tBlush = 2.5f;
    tSurprise = 0.5f;
    setState(St::Land, 0.4f);
    sayCat("fellOut", 1.f, L"", true);
}

int Pet::iconUnder(const Surf& s, float x) const {
    if (s.kind != SurfKind::Icon) return -1;
    const auto& W = app.world;
    const auto& L = app.icons.items;
    if (s.run < 0 || s.run >= (int)W.iconRuns.size()) return s.index >= 0 && s.index < (int)L.size() ? s.index : -1;
    int best = -1;
    float bd = 1e30f;
    for (int k : W.iconRuns[s.run]) {
        if (k < 0 || k >= (int)L.size()) continue;
        float d = fabsf((L[k].img.left + L[k].img.right) * 0.5f - x);
        if (d < bd) {
            bd = d;
            best = k;
        }
    }
    return best;
}

bool Pet::surfSkip(const Surf& s) const {
    if (s.kind != SurfKind::Icon) return false;
    const auto& L = app.icons.items;
    auto bad = [&](int k) { return k >= 0 && k < (int)L.size() && ((!carriedName.empty() && L[k].name == carriedName) || deskHides(L[k])); };
    if (s.run < 0 || s.run >= (int)app.world.iconRuns.size()) return bad(s.index);
    for (int k : app.world.iconRuns[s.run])
        if (bad(k)) return true;
    return false;
}

bool Pet::onSurf(const Surf& s) const {
    if (!gnd.valid || s.kind != gnd.kind || fabsf(s.y - gnd.y) >= 2) return false;
    if (s.kind == SurfKind::Icon) {
        int i = app.icons.find(gnd.iconName);
        return i >= 0 ? app.world.runHas(s, i) : s.index == gnd.index;
    }
    return s.hwnd == gnd.hwnd && s.index == gnd.index;
}

void Pet::iconStrip(int icon, float& y, float& x0, float& x1) const {
    const RECT& im = app.icons.items[icon].img;
    y = (float)im.top;
    x0 = im.left + 3.f;
    x1 = im.right - 3.f;
    for (auto& s : app.world.surfs)
        if (s.kind == SurfKind::Icon && fabsf(s.y - y) <= 2.5f && s.x0 <= x0 + 1.f && s.x1 >= x1 - 1.f && app.world.runHas(s, icon)) {
            y = s.y;
            x0 = s.x0;
            x1 = s.x1;
            return;
        }
}

const Surf* Pet::findLanding(V2 prev, V2 cur) const {
    const Surf* best = nullptr;
    for (auto& s : app.world.surfs) {
        if (surfSkip(s)) continue;
        if (s.kind == SurfKind::WinFloor) {
            bool inside = airFrom.x >= s.win.left && airFrom.x <= s.win.right && airFrom.y > s.win.top && airFrom.y < s.y + 2.f;
            if (baby() || (airPlanned ? s.hwnd != jumpWin : (!inside || s.hwnd == fromWin))) continue;
        }
        if (s.kind == SurfKind::Window && airPlanned && jumpWin && s.hwnd == jumpWin) continue;
        float margin = (s.kind == SurfKind::Icon ? 2.f : 8.f) * S();
        if (prev.y <= s.y + 1 && cur.y >= s.y && cur.x >= s.x0 - margin && cur.x <= s.x1 + margin)
            if (!best || s.y < best->y) best = &s;
    }
    return best;
}

void Pet::setGround(const Surf& s) {
    gnd = Ground{};
    gnd.valid = true;
    gnd.kind = s.kind;
    gnd.hwnd = s.hwnd;
    gnd.index = s.index;
    gnd.y = s.y;
    gnd.x0 = s.x0;
    gnd.x1 = s.x1;
    if (s.kind == SurfKind::Window || s.kind == SurfKind::WinFloor) app.world.windowRect(s.hwnd, gnd.last);
    standWin = s.kind == SurfKind::WinFloor ? s.hwnd : nullptr;
    if (int i = iconUnder(s, pos.x); i >= 0) gnd.iconName = app.icons.items[i].name;
    rideSpeed = 0;
}

void Pet::land(const Surf& s) {
    float impact = vel.y;
    bool wasLeap = st == St::Leap;
    bool hard = impact > 2000.f * S() || fabsf(wrapAngle(rot)) > 1.3f;
    pos.y = s.y;
    if (s.kind != SurfKind::Icon) pos.x = clampf(pos.x, s.x0, s.x1);
    jumpWin = nullptr;
    setGround(s);
    vel = {};
    rotVel = 0;
    if (!hard) rot = 0;
    if (vit.fatal) {
        squashV -= 1.5f;
        setState(St::Fatal);
        return;
    }
    squashV -= clampf(impact / (3600.f * S()), 0.08f, 0.28f) * 9.f;
    if (impact > 700.f * S())
        for (int i = 0; i < 5; i++)
            emit(4, pos + V2(randf(-14, 14) * S(), -2), V2(randf(-90, 90) * S(), randf(-50, -15) * S()), 0.5f, randf(0.7f, 1.2f));

    if (baby()) {
        if (impact > 1000.f * S()) {
            vit.babyDrops++;
            tSad = 3.f;
            app.bubble.close();
            sayCat("babyCry", 1.f, L"", true);
            logf("baby dropped (%d)", vit.babyDrops);
        }
        if (napOnLand) {
            napOnLand = false;
            deepSleep = false;
            zzzT = 2.f;
            setState(St::Sleep, 1e9f);
            return;
        }
        setState(St::Land, 0.25f);
        return;
    }
    if (helloOnLand) {
        helloOnLand = false;
        greeted = true;
        lastWave = app.now;
        tHappy = 2.f;
        setState(St::Wave, 2.4f);
        sayCat("grownHello", 1.f, L"", true);
        return;
    }
    int under = iconUnder(s, pos.x);
    bool fetchHit = !fetchName.empty() && under >= 0 && app.icons.items[under].name == fetchName;
    if (!fetchName.empty() || s.kind != SurfKind::Floor)
        logf("land on %d y=%.0f x=%.0f impact=%.0f fetch='%s' hit=%d", (int)s.kind, s.y, pos.x, impact, narrow(fetchName).c_str(), fetchHit);
    if (!fetchHit) fetchName.clear();
    if (hard) {
        setState(St::Dizzy, 2.2f);
        tCross = 0.35f;
        sayCat("hardLand", 0.85f, L"", true);
        return;
    }
    if (fetchHit) { setState(St::GrabIcon); return; }
    if (webDiveOnLand) {
        webDiveOnLand = false;
        diveInto(V2(pos.x - 40.f * S(), s.y), 80.f * S(), NestWeb, L"");
        return;
    }
    if (s.kind == SurfKind::Web && webPress >= 0 && s.index == webPress && app.web) {
        webPress = -1;
        app.web->send("{\"c\":\"press\",\"id\":" + std::to_string(s.index) + "}");
        sayCat("webNext", 0.6f, L"", true);
    }
    if (!nestIcon.empty()) {
        int ni = under;
        std::wstring want = nestIcon;
        nestIcon.clear();
        if (ni >= 0 && app.icons.items[ni].name == want && app.icons.items[ni].folder) {
            const RECT& im = app.icons.items[ni].img;
            diveInto(V2((float)im.left, (float)im.top), (float)(im.right - im.left), NestFolder, app.icons.items[ni].path);
            sayCat("nestGo", 0.7f, L"", true);
            return;
        }
    }
    if (clapOnLand) {
        clapOnLand = false;
        setState(St::Clap, 2.2f);
        return;
    }
    if (waveOnLand) {
        waveOnLand = false;
        lastWave = app.now;
        setState(St::Wave, 1.9f);
        sayCat("strollBack", 1.f, L"", true);
        sitAfterWave = true;
        return;
    }
    if (!greeted) {
        greeted = true;
        lastWave = app.now;
        setState(St::Wave, 1.9f);
        if (!missSay(true)) sayCat(greetCat(), 1.f, L"", true);
        return;
    }
    setState(St::Land, 0.2f);
    if (wasLeap && !game) sayCat("leapMiss", 0.8f, L"", true);
}

bool Pet::groundCheck(float dt) {
    if (!gnd.valid) return false;
    auto& W = app.world;
    switch (gnd.kind) {
    case SurfKind::Floor: {
        if (gnd.index < 0 || gnd.index >= (int)W.mons.size()) return false;
        const RECT& w = W.mons[gnd.index].work;
        gnd.y = (float)w.bottom;
        gnd.x0 = w.left + 22.f * S();
        gnd.x1 = w.right - 22.f * S();
        pos.y = gnd.y;
        if (pos.x < gnd.x0 - 1 || pos.x > gnd.x1 + 1) {
            for (size_t m = 0; m < W.mons.size(); m++) {
                const RECT& o = W.mons[m].work;
                if ((int)m != gnd.index && fabsf((float)o.bottom - gnd.y) < 3 && pos.x >= o.left && pos.x <= o.right) {
                    gnd.index = (int)m;
                    return true;
                }
            }
            pos.x = clampf(pos.x, gnd.x0, gnd.x1);
        }
        return true;
    }
    case SurfKind::Window: {
        RECT r;
        if (!W.windowRect(gnd.hwnd, r)) {
            sayCat("windowGone", 0.7f, L"", true);
            return false;
        }
        float dx = (float)(r.left - gnd.last.left), dy = (float)(r.top - gnd.last.top);
        if (dx != 0 || dy != 0) {
            pos.x += dx;
            gnd.movedT = 0.35f;
            gnd.last = r;
            rideVel = V2(dx, dy) / std::max(dt, 1e-3f);
            rideSpeed = std::max(rideSpeed, rideVel.len());
        }
        rideSpeed = approach(rideSpeed, 0, 6, dt);
        pos.y = gnd.y = (float)r.top;
        int m = W.monitorAt(V2(pos.x, gnd.y + 1));
        if (m < 0 || gnd.y < W.mons[m].rc.top + 40.f * S()) return false;
        if (gnd.movedT > 0) {
            gnd.movedT -= dt;
            gnd.x0 = r.left + 6.f;
            gnd.x1 = r.right - 6.f;
        } else {
            bool found = false;
            for (auto& s : W.surfs)
                if (s.kind == SurfKind::Window && s.hwnd == gnd.hwnd && fabsf(s.y - gnd.y) < 3 && pos.x >= s.x0 - 3 && pos.x <= s.x1 + 3) {
                    gnd.x0 = s.x0;
                    gnd.x1 = s.x1;
                    found = true;
                    break;
                }
            if (!found) return false;
        }
        if (pos.x < r.left - 6 || pos.x > r.right + 6) return false;
        if (rideSpeed > 2600.f * S()) {
            sayCat("windowShake", 0.9f, L"", true);
            tSurprise = 1.f;
            vel = rideVel * 0.5f;
            return false;
        }
        if (rideSpeed > 900.f * S() && tSurprise <= 0) {
            tSurprise = 0.6f;
            sayCat("windowShake", 0.25f);
        }
        return true;
    }
    case SurfKind::WinFloor: {
        RECT r;
        if (!W.windowRect(gnd.hwnd, r)) {
            sayCat("windowGone", 0.7f, L"", true);
            return false;
        }
        float dx = (float)(r.left - gnd.last.left), dy = (float)(r.bottom - gnd.last.bottom);
        if (dx != 0 || dy != 0) {
            pos.x += dx;
            gnd.movedT = 0.35f;
            gnd.last = r;
            rideVel = V2(dx, dy) / std::max(dt, 1e-3f);
            rideSpeed = std::max(rideSpeed, rideVel.len());
        }
        rideSpeed = approach(rideSpeed, 0, 6, dt);
        pos.y = gnd.y = (float)r.bottom - 2.f;
        int m = W.monitorAt(V2(pos.x, gnd.y - 1));
        if (m < 0) m = W.nearestMonitor(V2(pos.x, gnd.y - 1));
        if (gnd.y > W.mons[m].work.bottom - 20) return false;
        if (gnd.movedT > 0) {
            gnd.movedT -= dt;
            gnd.x0 = r.left + 10.f;
            gnd.x1 = r.right - 10.f;
        } else {
            bool found = false;
            for (auto& s : W.surfs)
                if (s.kind == SurfKind::WinFloor && s.hwnd == gnd.hwnd && fabsf(s.y - gnd.y) < 3 && pos.x >= s.x0 - 3 && pos.x <= s.x1 + 3) {
                    gnd.x0 = s.x0;
                    gnd.x1 = s.x1;
                    found = true;
                    break;
                }
            if (!found) return false;
        }
        pos.x = clampf(pos.x, gnd.x0, gnd.x1);
        if (rideSpeed > 2600.f * S()) {
            sayCat("windowShake", 0.9f, L"", true);
            tSurprise = 1.f;
            vel = rideVel * 0.5f;
            return false;
        }
        if (rideSpeed > 900.f * S() && tSurprise <= 0) {
            tSurprise = 0.6f;
            sayCat("windowShake", 0.25f);
        }
        return true;
    }
    case SurfKind::Web: {
        for (auto& s : W.surfs)
            if (s.kind == SurfKind::Web && s.index == gnd.index && pos.x >= s.x0 - 6 && pos.x <= s.x1 + 6) {
                pos.y = gnd.y = s.y;
                gnd.x0 = s.x0;
                gnd.x1 = s.x1;
                return true;
            }
        return false;
    }
    case SurfKind::Icon: {
        int i = app.icons.find(gnd.iconName);
        if (i < 0) return false;
        const RECT& im = app.icons.items[i].img;
        if (!W.fake && !W.rectVisible(im)) return false;
        iconStrip(i, gnd.y, gnd.x0, gnd.x1);
        pos.y = gnd.y;
        if (pos.x < gnd.x0 - 4 || pos.x > gnd.x1 + 4) return false;
        if (pos.x < im.left - 2 || pos.x > im.right + 2)
            for (auto& s : W.surfs)
                if (W.runHas(s, i)) {
                    int u = iconUnder(s, pos.x);
                    if (u >= 0 && u != i) gnd.iconName = app.icons.items[u].name;
                    break;
                }
        return true;
    }
    }
    return false;
}

void Pet::decide(const Input& in) {
    if (!carriedName.empty()) { carryDecide(); return; }
    if (introHidden) { goIdle(5.f, 8.f); return; }
    if (game) { goIdle(0.5f, 1.f); return; }
    if (extOn()) { goIdle(1.f, 2.f); return; }
    if (baby()) {
        if (!growing && vit.energy < 35.f && chance(0.5f)) { sleepNow(); return; }
        if (chance(0.25f)) sayCat("babyCoo", 0.7f);
        goIdle(4.f, 9.f);
        return;
    }
    if (app.bubble.asking()) { goIdle(3.f, 5.f); return; }
    if (!heldPath.empty()) { goIdle(1.f, 2.f); return; }
    if (quiet() || strollOn) { setState(St::Sit, randf(90, 200)); return; }
    if (webDecide(in)) return;
    if (in.idleSec >= kAfkIdle && afkStart()) return;
    if (peekStart(in)) return;
    float span = gnd.x1 - gnd.x0;
    V2 jt, ft;
    int ji = -1, fi = -1;
    bool canJump = pickJump(jt, false, ji);
    bool canFetch = false;
    (void)ft;
    (void)fi;
    int folderIcon = -1;
    if (app.settings.icons && app.iconsReady && app.now - lastNest > 900 && !app.world.web) {
        std::vector<int> fs;
        for (size_t i = 0; i < app.icons.items.size(); i++)
            if (app.icons.items[i].folder && app.world.rectVisible(app.icons.items[i].img)) fs.push_back((int)i);
        if (!fs.empty()) folderIcon = fs[randi(0, (int)fs.size() - 1)];
    }
    float busyK = (in.idleSec < 10 ? 0.45f : 1.f) * lerpf(1.f, 0.25f, hungerK());
    if (weak() || groggyT > 0) canJump = false;
    if (dayMood == 2 && !weak() && in.idleSec >= 10 && chance(0.07f)) {
        setState(St::Stretch, 3.4f);
        sayCat("moodYawn", 0.35f);
        return;
    }
    float w[10] = {
        span > 320.f * S() ? 1.4f * busyK : 0.f,
        weak() ? 9.f : 3.2f,
        0.7f,
        canJump ? 0.7f * busyK : 0.f,
        canFetch ? 1.3f : 0.f,
        gnd.kind == SurfKind::Window ? 0.7f : 0.f,
        2.0f,
        (app.now - lastWave > 90 && (in.cursor - headC).len() < 400.f * S()) ? 0.6f : 0.f,
        (gnd.kind == SurfKind::Window && app.now - lastHide > 240) ? 0.8f : 0.f,
        folderIcon >= 0 ? 0.6f : 0.f,
    };
    w[0] *= moodK(MkWalk);
    if (!weak()) w[1] *= moodK(MkSit);
    w[2] *= moodK(MkLook);
    w[3] *= moodK(MkJump);
    w[7] *= moodK(MkWave);
    w[8] *= moodK(MkHide);
    if (groggyT > 0) {
        w[0] *= 0.25f;
        w[1] *= 2.f;
        w[4] = w[8] = w[9] = 0.f;
    }
    float sum = 0;
    for (float x : w) sum += x;
    float r = randf(0, sum);
    int pick = 0;
    for (; pick < 10; pick++) {
        if (r < w[pick]) break;
        r -= w[pick];
    }
    switch (pick) {
    case 0: {
        float x = pos.x;
        for (int tries = 0; tries < 10 && fabsf(x - pos.x) < 260.f * S(); tries++) x = randf(gnd.x0 + 10.f * S(), gnd.x1 - 10.f * S());
        if (fabsf(x - pos.x) < 260.f * S()) { setState(St::Sit, randf(60, 150)); break; }
        goWalk(x);
        break;
    }
    case 1:
        setState(St::Sit, randf(60, 150) * (dayMood == 2 ? 1.5f : dayMood == 1 ? 0.6f : 1.f));
        sayCat("sit", 0.1f);
        break;
    case 2:
        setState(St::LookAround, dayMood == 3 ? randf(4.f, 7.f) : 2.6f);
        break;
    case 3: {
        bool ok = goJump(jt);
        for (int tries = 0; !ok && tries < 4; tries++)
            if (pickJump(jt, false, ji)) ok = goJump(jt);
        if (ok) sayCat("jump", 0.1f);
        else goIdle();
        break;
    }
    case 4:
        fetchName = app.icons.items[fi].name;
        lastFetch = app.now;
        logf("going for icon '%s'", narrow(fetchName).c_str());
        if (!goJump(ft, 40, 90)) {
            fetchName.clear();
            goIdle();
        }
        break;
    case 5: {
        float l = pos.x - gnd.x0, rr = gnd.x1 - pos.x;
        goWalk(l < rr ? gnd.x0 - 14.f * S() : gnd.x1 + 14.f * S(), true);
        break;
    }
    case 7:
        lastWave = app.now;
        setState(St::Wave, 1.8f);
        break;
    case 8:
        lastHide = app.now;
        edgeL = V2(gnd.x0, gnd.y);
        edgeW = gnd.x1 - gnd.x0;
        sink = 0;
        phase = 0;
        phaseT = 0;
        setState(St::Hide, randf(9, 16));
        sayCat("hide", 0.35f);
        break;
    case 9: {
        const RECT& im = app.icons.items[folderIcon].img;
        lastNest = app.now;
        if (goJump(V2((im.left + im.right) * 0.5f, (float)im.top), 40, 90)) nestIcon = app.icons.items[folderIcon].name;
        else goIdle();
        break;
    }
    default:
        goIdle();
        break;
    }
}

void Pet::carryDecide() {
    if (!gnd.valid) return;
    if (gnd.kind == SurfKind::Floor) {
        if (!dropTargetSet) {
            dropTargetSet = true;
            float d = randf(140, 420) * S() * (chance(0.5f) ? 1.f : -1.f);
            float x = clampf(pos.x + d, gnd.x0 + 40.f * S(), gnd.x1 - 40.f * S());
            if (fabsf(x - pos.x) < 50.f * S()) x = clampf(pos.x - d, gnd.x0 + 40.f * S(), gnd.x1 - 40.f * S());
            goWalk(x);
        } else {
            setState(St::DropIcon, 0.65f);
        }
    } else {
        float l = pos.x - gnd.x0, r = gnd.x1 - pos.x;
        goWalk(l < r ? gnd.x0 - 14.f * S() : gnd.x1 + 14.f * S(), true);
    }
}

bool Pet::pickJump(V2& target, bool icon, int& iconIdx) {
    std::vector<const Surf*> c;
    for (auto& s : app.world.surfs) {
        if (icon != (s.kind == SurfKind::Icon)) continue;
        if (onSurf(s) || surfSkip(s)) continue;
        float cx = (s.x1 - s.x0 < 28.f * S()) ? (s.x0 + s.x1) * 0.5f : clampf(pos.x, s.x0 + 14.f * S(), s.x1 - 14.f * S());
        float dx = fabsf(cx - pos.x), dy = s.y - pos.y;
        if (!icon) {
            bool otherFloor = s.kind == SurfKind::Floor && !(gnd.valid && gnd.kind == SurfKind::Floor && gnd.index == s.index);
            if (dy < -380.f * S() || dx > (otherFloor ? 900.f : 520.f) * S()) continue;
            if (dx < 30.f * S() && fabsf(dy) < 30.f * S()) continue;
        } else {
            if (dx > 1400.f * S() || dy < -900.f * S()) continue;
        }
        int m = app.world.monitorAt(V2(cx, s.y - 1));
        if (m < 0) continue;
        c.push_back(&s);
    }
    if (c.empty()) return false;
    const Surf* s = c[randi(0, (int)c.size() - 1)];
    float lo = s->x0 + 14.f * S(), hi = s->x1 - 14.f * S();
    float x = (icon || hi <= lo) ? (s->x0 + s->x1) * 0.5f : randf(lo, hi);
    target = V2(x, s->y);
    iconIdx = iconUnder(*s, x);
    return true;
}

bool Pet::trajectoryOk(V2 p0, V2 v, float T) const {
    auto& W = app.world;
    if (W.web) {
        const RECT& r = W.webRc;
        for (int i = 1; i <= 20; i++) {
            float tt = T * i / 20.f;
            V2 p = p0 + v * tt + V2(0, 0.5f * G() * tt * tt);
            if (p.x < r.left + 20 || p.x > r.right - 20 || p.y - comH * 2.1f < r.top) return false;
        }
        return true;
    }
    if (W.fake || W.mons.empty()) return true;
    float half = 34.f * S();
    for (int i = 1; i <= 28; i++) {
        float tt = T * i / 28.f;
        V2 p = p0 + v * tt + V2(0, 0.5f * G() * tt * tt);
        V2 c = p - V2(0, comH), top = p - V2(0, comH * 2.1f);
        if (W.monitorAt(c + V2(-half, 0)) < 0 || W.monitorAt(c + V2(half, 0)) < 0 || W.monitorAt(top) < 0) return false;
    }
    return true;
}

bool Pet::planJump(V2 target, float apexMin, V2& out) const {
    struct Count {
        const Pet& p;
        double t0 = nowSec();
        ~Count() {
            p.perfJumpPlans++;
            p.perfJumpSec += nowSec() - t0;
        }
    } count{*this};
    float g = G(), dx = target.x - pos.x;
    float extra = std::max(apexMin * S(), fabsf(dx) * 0.32f) + randf(0, 30) * S();
    for (int tries = 0; tries < 5; tries++, extra *= 1.3f) {
        float apex = std::min(pos.y, target.y) - extra;
        float h1 = pos.y - apex, h2 = target.y - apex;
        float T = sqrtf(2 * h1 / g) + sqrtf(2 * h2 / g);
        V2 v(dx / T, -sqrtf(2 * g * h1));
        if (trajectoryOk(pos, v, T)) {
            out = v;
            return true;
        }
    }
    return false;
}

bool Pet::goJump(V2 target, float apexMin, float) {
    if (webMoving()) {
        logf("jump to %.0f,%.0f skipped: the page is still scrolling, its blocks are stale", target.x, target.y);
        return false;
    }
    V2 v;
    if (!planJump(target, apexMin, v)) return false;
    jumpVel = v;
    jumpWin = nullptr;
    for (auto& s : app.world.surfs)
        if (s.kind == SurfKind::WinFloor && fabsf(s.y - target.y) < 3.f && target.x >= s.x0 - 4.f && target.x <= s.x1 + 4.f) jumpWin = s.hwnd;
    if (fabsf(target.x - pos.x) > 4) facing = target.x > pos.x ? 1 : -1;
    setState(St::PrepJump, 0.22f);
    return true;
}

V2 Pet::handScreen(bool right) const {
    int fi = right ? (iFArmR >= 0 ? iFArmR : iArmR) : (iFArmL >= 0 ? iFArmL : iArmL);
    if (fi < 0) return headTop;
    const PartDef& d = curRig().parts[fi];
    return xf(curInst().world[fi], d.hand()) + app.winOrigin;
}

static const char* fileCategory(const std::wstring& path) {
    DWORD at = GetFileAttributesW(path.c_str());
    if (at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_DIRECTORY)) return "fileFolder";
    size_t dot = path.find_last_of(L'.'), slash = path.find_last_of(L"\\/");
    std::wstring ext = (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) ? path.substr(dot + 1) : L"";
    for (auto& ch : ext) ch = (wchar_t)towlower(ch);
    auto in = [&](std::initializer_list<const wchar_t*> l) {
        for (auto e : l)
            if (ext == e) return true;
        return false;
    };
    if (in({L"png", L"jpg", L"jpeg", L"gif", L"webp", L"bmp", L"psd", L"kra", L"ico"})) return "fileImage";
    if (in({L"mp4", L"mkv", L"mov", L"avi", L"webm"})) return "fileVideo";
    if (in({L"mp3", L"wav", L"ogg", L"flac", L"m4a"})) return "fileAudio";
    if (in({L"txt", L"md", L"doc", L"docx", L"pdf", L"rtf", L"odt"})) return "fileText";
    if (in({L"cpp", L"h", L"hpp", L"c", L"py", L"js", L"ts", L"java", L"kt", L"cs", L"json", L"glsl", L"fsh", L"vsh", L"dsc", L"lua", L"cx"}))
        return "fileCode";
    if (in({L"zip", L"rar", L"7z", L"tar", L"gz"})) return "fileArchive";
    if (in({L"exe", L"msi", L"bat", L"cmd", L"ps1", L"lnk"})) return "fileExe";
    if (in({L"blend", L"blend1", L"fbx", L"obj", L"gltf", L"glb", L"bbmodel", L"uasset", L"uproject", L"abc", L"vrm", L"pmx", L"ma", L"mb", L"max",
            L"dae", L"usd", L"usdz", L"stl"}))
        return "file3d";
    if (in({L"jar", L"mcworld", L"mcpack", L"mrpack", L"schem", L"litematic"})) return "fileMinecraft";
    return "fileOther";
}

void Pet::fileHover(bool on) {
    fileHovering = on;
    hoverT = 0;
    hoverSaid = false;
}

void Pet::receiveFile(const std::wstring& path, int count) {
    fileHover(false);
    gameStop("file");
    tugPath.clear();
    busyJob.reset();
    tugT = -1;
    if (dragging || st == St::Cling || st == St::Leap || extFolder) {
        sayCat("fileBusy", 1.f, L"", true);
        return;
    }
    if (deskOn() && deskDropped && tripPh >= 1 && count == 1 && sameFullPath(path, heldPath)) {
        tHappy = std::max(tHappy, 0.8f);
        sayCat("fileFloorGiven", 1.f, L"", true);
        logf("got file '%s': the one she dropped, she picks it up herself", narrow(path).c_str());
        return;
    }
    if (!carriedName.empty()) dropCarried(true);
    if (!heldPath.empty()) dropHeld("new file");
    if (st == St::Sleep) wake(true);
    if (st == St::Walk || st == St::Sit || st == St::LookAround || st == St::Wave) goIdle(1.f, 2.f);
    double t0 = dropAt >= 0 ? dropAt : nowSec();
    dropAt = -1;
    heldPath = path;
    heldCat = fileCategory(path);
    heldSense = senseName(path);
    heldCorner = count == 1 && cornerIsMine(path);
    heldHappyAt = -1;
    senseJob.reset();
    senseRead.reset();
    heldImg.reset();
    heldImg.src.Reset();
    heldHalo.reset();
    heldHalo.src.Reset();
    heldLay.Reset();
    heldName = path.substr(path.find_last_of(L"\\/") + 1);
    ComPtr<IShellItem> it;
    HRESULT hr = SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&it));
    if (SUCCEEDED(hr)) {
        wchar_t* dn = nullptr;
        if (SUCCEEDED(it->GetDisplayName(SIGDN_NORMALDISPLAY, &dn)) && dn && *dn) heldName = dn;
        CoTaskMemFree(dn);
    }
    deskBusy = false;
    bool carried = count == 1 && deskStart(path);
    if (!carried && deskBusy) {
        heldClear();
        heldSense = FileSense{};
        handsMs = -1.f;
        tugStart(path);
        return;
    }
    handsMs = carried ? (float)((nowSec() - t0) * 1000.0) : -1.f;
    if (carried) logf("desk carry: in her hands %.1f ms after the drop ('%s')", handsMs, narrow(heldName).c_str());
    std::wstring look = carried && !deskRec.hands.empty() ? deskRec.hands : path;
    if (look != path) {
        it.Reset();
        hr = SHCreateItemFromParsingName(look.c_str(), nullptr, IID_PPV_ARGS(&it));
    }
    if (senseWantsPixels(look)) senseRead = senseJob = senseStart(look, heldSense);
    else senseFinish(heldSense);
    if (SUCCEEDED(hr)) {
        ComPtr<IShellItemImageFactory> f;
        HBITMAP hb = nullptr;
        int px = (int)(96 * S());
        hr = it.As(&f);
        if (SUCCEEDED(hr)) hr = f->GetImage({px, px}, SIIGBF_RESIZETOFIT, &hb);
        if (SUCCEEDED(hr) && hb) {
            heldImg.src = app.gfx.fromHBitmap(hb);
            DeleteObject(hb);
            UINT w = 0, h = 0;
            if (heldImg.src) heldImg.src->GetSize(&w, &h);
            float k = w && h ? 36.f * S() / std::max(w, h) : 1.f;
            if (k < 0.95f) heldImg.src = app.gfx.scaled(heldImg.src.Get(), k);
            float edge = 1.f;
            heldHaloPad = std::max(2, (int)lroundf(2.5f * S()));
            if (heldImg.src) heldHalo.src = app.gfx.darkHalo(heldImg.src.Get(), heldHaloPad, &edge);
            logf("file image %ux%u, kept %.2f, edge %.2f halo %d", w, h, std::min(k, 1.f), edge, (int)(bool)heldHalo.src);
        }
    }
    if (!heldImg.src) logf("file image failed hr=0x%08X", (unsigned)hr);
    heldT = 0;
    heldStage = 0;
    heldLife = randf(11.f, 15.f);
    if (!peekOn) {
        tSurprise = 0.4f;
        if (count > 1) sayCat("fileMany", 1.f, std::to_wstring(count), true);
        else sayCat("fileReceive", 1.f, L"", true);
    }
    logf("got file '%s' (%s) %s", narrow(path).c_str(), heldCat.c_str(), heldSense.why.c_str());
}

void Pet::senseLetGo() {
    if (!senseRead || senseRead->done) {
        senseRead.reset();
        return;
    }
    double t0 = nowSec();
    for (int k = 0; k < 150 && !senseRead->done; k++) Sleep(10);
    logf("file sense: waited %.0f ms for the thumbnail thread before moving the file (%s)", (nowSec() - t0) * 1000.0, senseRead->done ? "done" : "still busy");
    senseRead.reset();
}

const char* Pet::fileSenseReact() {
    const FileSense& fs = heldSense;
    int tr = tier();
    if (fs.kind == FileKind::Medical) { tSad = 1.2f; return "fileUzMedical"; }
    if (fs.kind == FileKind::Gun) { tHappy = 2.f; return "fileUziGun"; }
    if (fs.self > 0) {
        if (baby()) return heldCat.c_str();
        if (fs.self >= 2) heldLife += 4.f;
        if (tr == 0) {
            tBlush = 1.5f;
            tAngry = 1.2f;
            return "fileSelfSure";
        }
        double now = (double)time(nullptr);
        if (fs.self >= 2 && now - vit.lastSelfArt >= 24 * 3600.0) {
            vit.lastSelfArt = now;
            addRel(1.f, "art of her");
        }
        tSurprise = 0.6f;
        if (fs.self == 3) {
            tBlush = 2.5f;
            heldHappyAt = heldT + 0.8f;
            return fs.is3d ? "fileSelf3d" : "fileSelfSure";
        }
        if (fs.self == 2) {
            tBlush = 1.6f;
            if (tr >= 2) heldHappyAt = heldT + 1.f;
            return fs.is3d ? "fileSelf3d" : "fileSelfName";
        }
        tBlush = 1.f;
        return fs.nameScore > 0 && fs.colorScore < 1.f ? "fileSelfSurname" : "fileSelfMaybe";
    }
    switch (fs.kind) {
    case FileKind::Show: tSurprise = 0.4f; return "fileShow";
    case FileKind::N: tHappy = 1.5f; return "fileN";
    case FileKind::V: tSurprise = 0.5f; return "fileV";
    case FileKind::J: tAngry = 1.2f; return "fileJ";
    case FileKind::Cyn: tSurprise = 1.2f; tSad = 1.5f; return "fileCyn";
    case FileKind::Khan: tSurprise = 0.8f; return "fileKhan";
    case FileKind::Tessa: tSurprise = 0.4f; return "fileTessa";
    default: return heldCat.c_str();
    }
}

void Pet::startBite() {
    if (!grounded() || st == St::Sleep || dragging || weak() || baby()) return;
    V2 d = lastCursor - headC;
    if (d.len() > headR * 2.6f) return;
    biteDir = d.norm();
    if (fabsf(d.x) > 4) facing = d.x > 0 ? 1 : -1;
    tAngry = 1.4f;
    sayCat("bite", 0.9f, L"", true);
    V2 mouth = headC + V2(0, headR * 0.45f);
    if (active == 0 && app.rig.mouth.on && app.rig.mouth.part >= 0)
        mouth = xf(app.inst.world[app.rig.mouth.part], app.rig.mouth.center) + app.winOrigin;
    float rise = mouth.y - lastCursor.y;
    if (rise > 8.f * S()) {
        float g = G(), vy = -sqrtf(2 * g * rise), tUp = -vy / g;
        float vx = clampf((lastCursor.x - mouth.x) / std::max(tUp, 0.05f), -700.f * S(), 700.f * S());
        vel = V2(vx, vy);
        gnd.valid = false;
        biteHop = true;
        squashV += 2.f;
        setState(St::Air);
        return;
    }
    setState(St::Bite, 0.9f);
}

bool Pet::startLeap(V2 cur) {
    if (!grounded() || st == St::Sleep || active != 0 || weak() || baby()) return false;
    V2 feet = cur + V2(0, comH * 2.3f);
    float up = pos.y - feet.y;
    if (up < 20.f * S() || up > 650.f * S() || fabsf(feet.x - pos.x) > 700.f * S()) return false;
    float g = G();
    float vy = -sqrtf(2 * g * up), T = -vy / g;
    V2 v((feet.x - pos.x) / T, vy);
    if (!trajectoryOk(pos, v, T)) return false;
    if (fabsf(feet.x - pos.x) > 4) facing = feet.x > pos.x ? 1 : -1;
    vel = v;
    gnd.valid = false;
    walkOff = false;
    squashV += 2.5f;
    setState(St::Leap);
    sayCat("leap", 0.9f, L"", true);
    return true;
}

void Pet::beginCling(V2 cur) {
    const Rig& R0 = app.rig;
    V2 mc = R0.mouth.on ? R0.mouth.center : R0.headCenter;
    V2 g(mc.x * facing, mc.y + comH);
    clingL0 = std::max(g.len(), 12.f * S());
    clingCyc = 0;
    V2 c = com();
    pendL = std::max(g.len(), 12.f * S());
    V2 d = (c - cur).len() > 1.f ? (c - cur).norm() : V2(0, 1);
    if (d.y < 0.3f) d = V2(d.x, 0.3f).norm();
    pend = atan2f(d.x, d.y);
    pendV = 0;
    grabBeta = atan2f(-g.y, -g.x);
    shakeAcc = 0;
    gnd.valid = false;
    setState(St::Cling, randf(8, 14));
    sayCat("cling", 1.f, L"", true);
}

void Pet::endCling(bool shaken) {
    V2 tang = V2(cosf(pend), -sinf(pend)) * (pendV * pendL);
    vel = cursorVel * (shaken ? 0.55f : 0.2f) + tang;
    float lim = 3000.f * S();
    if (vel.len() > lim) vel = vel.norm() * lim;
    rotVel = -pendV * 0.6f;
    setState(St::Fall);
    if (shaken) {
        tSurprise = 0.6f;
        sayCat("clingShaken", 1.f, L"", true);
    } else {
        sayCat("clingBored", 1.f, L"", true);
    }
}

bool Pet::annoyedAttack() {
    if (!grounded() || st == St::Sleep || !carriedName.empty()) return false;
    if (weak() || baby()) {
        sayCat("hungryNoBite", 1.f, L"", true);
        return true;
    }
    vengeT = 5.f;
    if (active != 0) {
        biteQueued = true;
        goIdle(2.f, 3.f);
        squashV += 1.5f;
        return true;
    }
    if ((lastCursor - headC).len() < headR * 2.2f) startBite();
    return true;
}

void Pet::warnBite(const char* cat) {
    if (weak() || baby()) {
        sayCat("hungryNoBite", 1.f, L"", true);
        return;
    }
    tAngry = 1.6f;
    emit(2, headTop + V2(headR * 0.75f, headR * 0.1f), V2(10.f * S(), -50.f * S()), 0.9f, 1.2f);
    squashV -= 1.2f;
    double said0 = lastSay;
    sayCat(cat, 1.f, L"", true);
    if (std::string(cat) == "biteWarn" && lastSay != said0) lingerWarnAt = app.now;
}

void Pet::wake(bool startled) {
    deepSleep = false;
    goIdle(1.2f, 2.f);
    if (afkNap) { afkWoke(); return; }
    tSurprise = startled ? 0.8f : 0.3f;
    sayCat("wake", startled ? 1.f : 0.5f, L"", true);
}

void Pet::grabIcon() {
    auto& I = app.icons;
    I.refresh();
    int i = I.find(fetchName);
    if (i < 0) { fetchName.clear(); return; }
    std::string why;
    if (I.autoArrange() || !I.canTake(I.items[i].path, &why)) {
        logf("fetch: '%s' can't go into her hands (%s), not taking it", narrow(fetchName).c_str(), I.autoArrange() ? "auto-arrange" : why.c_str());
        fetchName.clear();
        return;
    }
    I.layoutSnap(app.now, "fetch", true);
    I.refreshIfOlder(0.2);
    i = I.find(fetchName);
    if (i < 0) { fetchName.clear(); return; }
    carriedImg.reset();
    carriedImg.src = I.image(app.gfx, I.items[i], I.iconPx);
    fetchRec = HandsRec{};
    fetchRec.path = I.items[i].path;
    fetchRec.name = I.items[i].name;
    fetchRec.home = I.items[i].pos;
    if (!I.take(fetchRec)) {
        carriedImg.reset();
        fetchName.clear();
        return;
    }
    I.layoutActed();
    carriedName = fetchName;
    fetchName.clear();
    dropTargetSet = false;
    iconMoveT = 0;
    sayCat("fetchIcon", 0.9f, carriedName, true);
    logf("grabbed icon '%s' (the file is in her hands while she carries it)", narrow(carriedName).c_str());
}

void Pet::moveCarriedIcon(float dt, bool force) {
    iconMoveT -= dt;
    if (!force && iconMoveT > 0) return;
    iconMoveT = 0.5f;
    if (GetFileAttributesW(app.icons.where(fetchRec).c_str()) == INVALID_FILE_ATTRIBUTES) {
        logf("fetch: '%s' is gone while she carries it", narrow(carriedName).c_str());
        dropCarried(false);
    }
}

void Pet::dropCarried(bool place) {
    if (carriedName.empty()) return;
    auto& I = app.icons;
    POINT to = fetchRec.home;
    if (place && gnd.valid && gnd.kind == SurfKind::Floor && I.spacing.y > 0) {
        float labelH = (float)(I.spacing.y - I.iconPx - I.imgOff.y);
        to = I.clientForImgCenter(V2(pos.x + facing * 46.f * S(), gnd.y - labelH - I.iconPx * 0.5f - 2.f));
    }
    std::wstring now;
    if (I.giveBack(fetchRec, &now)) {
        if (!now.empty() && I.isDesktopFile(now)) I.placeAfter(now, to, place ? "fetch drop" : "fetch abort", fetchRec.path, true);
        else I.dropRecord(fetchRec.path);
        logf("dropped icon '%s' toward cell %ld,%ld", narrow(carriedName).c_str(), to.x, to.y);
    } else {
        logf("fetch: '%s' could not be given back, the restore record stays", narrow(carriedName).c_str());
    }
    fetchRec = HandsRec{};
    carriedName.clear();
    carriedImg.reset();
    dropTargetSet = false;
}

V2 Pet::shoulderScreen(int side) const {
    int ai = side == 1 ? iArmL : iArmR;
    if (ai < 0 || active != 0) return headC;
    const PartDef& d = app.rig.parts[ai];
    int parent = d.parent;
    const auto& pm = parent >= 0 ? app.inst.world[parent] : app.inst.world[ai];
    return xf(pm, d.joint) + app.winOrigin;
}

V2 Pet::raisedHand(int side) const {
    int ai = side == 1 ? iArmL : iArmR;
    int hi = side == 1 ? (iFArmL >= 0 ? iFArmL : iArmL) : (iFArmR >= 0 ? iFArmR : iArmR);
    if (ai < 0 || hi < 0) return V2(0, -comH);
    const Rig& R0 = app.rig;
    V2 sh = R0.parts[ai].joint;
    const PartDef& hd = R0.parts[hi];
    V2 hand = sh + rotv(hd.hand() - sh, side == 1 ? 2.55f : -2.55f);
    return V2(hand.x * facing, hand.y + comH);
}

int Pet::pinRootOf(int part) const {
    const PartDef& d = app.rig.parts[part];
    bool lower = d.name.rfind("forearm", 0) == 0 || d.name.rfind("shin", 0) == 0;
    return lower && d.parent >= 0 ? d.parent : part;
}

void Pet::setPin(int part, V2 cur) {
    pinPart = -1;
    if (part < 0 || part >= (int)app.inst.world.size()) return;
    M3 inv = app.inst.world[part];
    if (!inv.Invert()) return;
    pinPart = part;
    pinLocal = xf(inv, cur - app.winOrigin);
    int root = pinRootOf(part);
    const PartDef& rd = app.rig.parts[root];
    if (root == part) {
        pinReach = (pinLocal - rd.joint).len();
    } else {
        const PartDef& cd = app.rig.parts[part];
        pinReach = ((cd.joint - rd.joint).len() + (pinLocal - cd.joint).len()) * 0.97f;
    }
}

void Pet::hangByPin(V2 cur, bool clampDown) {
    if (pinPart < 0) {
        hangBy(cur, raisedHand(grabHand), clampDown);
        return;
    }
    const PartDef& rd = app.rig.parts[pinRootOf(pinPart)];
    V2 g(rd.joint.x * facing, rd.joint.y + comH);
    V2 dir = g.len() > 1.f ? g.norm() : V2(0, -1);
    hangBy(cur, g + dir * pinReach, clampDown);
}

float Pet::pinError() const {
    if (pinPart < 0 || active != 0) return -1;
    return (xf(app.inst.world[pinPart], pinLocal) + app.winOrigin - lastCursor).len();
}

void Pet::hangBy(V2 cur, V2 g, bool clampDown) {
    dragging = true;
    gnd.valid = false;
    setState(St::Dragged);
    V2 c = com();
    pendL = std::max(g.len(), 12.f * S());
    V2 d = (c - cur).len() > 1.f ? (c - cur).norm() : V2(0, 1);
    if (clampDown && d.y < 0.2f) d = V2(d.x, 0.2f).norm();
    pend = atan2f(d.x, d.y);
    pendV = 0;
    grabBeta = atan2f(-g.y, -g.x);
}

void Pet::beginDrag(V2 cur) {
    if (st == St::Inside && (nest == NestWeb || nest == NestNone)) return;
    gameStop("drag");
    if (st == St::Inside) {
        nest = NestNone;
        nestPath.clear();
        nestShame = false;
        sayCat("nestPulled", 1.f, L"", true);
    }
    if (st == St::Dive || st == St::Emerge || st == St::Hide) {
        sink = 0;
        nest = NestNone;
        nestPath.clear();
        lookTurn = 0;
    }
    if (baby() && st == St::Sleep) {
        wake(true);
        app.bubble.close();
        pendingSay.clear();
        sayCat("babyCry", 1.f, L"", true);
    }
    afkGo = 0;
    if (afkNap && st == St::Sleep) wake(false);
    if (st == St::Sleep) {
        sleepCarry = true;
        sleepLower = false;
        dragging = true;
        carryOff = pos - cur;
        carryRough = carryT = 0;
        carrySaid = carryMoaned = false;
        shkOn = false;
        gnd.valid = false;
        petting = false;
        rub = 0;
        logf("carry asleep");
        return;
    }
    wasSitting = st == St::Sit || st == St::Watch;
    dragRough = 0;
    shkOn = false;
    dragSaid = false;
    petting = false;
    rub = 0;
    walkOff = false;
    fetchName.clear();
    grabHand = 0;
    pinPart = -1;
    std::string part;
    int hit = -1;
    if (active == 0) {
        hit = app.inst.hitTest(cur - app.winOrigin);
        if (hit >= 0) part = app.rig.parts[hit].name;
        float dl = (cur - handScreen(false)).len(), dr = (cur - handScreen(true)).len();
        bool other = part.rfind("leg", 0) == 0 || part.rfind("shin", 0) == 0 || part == "head" || part.rfind("hair", 0) == 0;
        if (std::min(dl, dr) < (other ? 9.f : 24.f) * S()) {
            part = dl < dr ? "forearm_l" : "forearm_r";
            hit = dl < dr ? iFArmL : iFArmR;
        }
        logf("grab at %.0f,%.0f: part '%s', hands %.0f / %.0f px", cur.x, cur.y, part.c_str(), dl, dr);
    }
    bool hand = part.rfind("arm", 0) == 0 || part.rfind("forearm", 0) == 0;
    bool leg = part.rfind("leg", 0) == 0 || part.rfind("shin", 0) == 0;
    if ((hand || leg) && hit >= 0) setPin(hit, cur);
    dragSaid = hand || leg;
    if (leg && pinPart >= 0) {
        hangByPin(cur, false);
        spin = 0;
        tSurprise = 0.5f;
        sayCat("grabLeg", 0.9f, L"", true);
        return;
    }
    if (hand) {
        bool right = part.size() >= 2 && part.compare(part.size() - 2, 2, "_r") == 0;
        grabHand = right ? 2 : 1;
        if (grounded() && st != St::Sleep && !vit.fatal) {
            dragging = true;
            ledSpeed = 0;
            setState(St::Led);
            sayCat("led", 0.8f, L"", true);
            return;
        }
        hangByPin(cur, true);
        spin = 0;
        tSurprise = 0.5f;
        sayCat("grabHand", 0.8f, L"", true);
        return;
    }
    dragging = true;
    gnd.valid = false;
    setState(St::Dragged);
    V2 c = com();
    V2 g = rotv(cur - c, -rot);
    pendL = std::max(g.len(), 12.f * S());
    V2 d = g.len() < 12.f * S() ? V2(0, 1) : (c - cur) / pendL;
    if (g.len() < 12.f * S()) g = V2(0, -pendL);
    pend = atan2f(d.x, d.y);
    pendV = 0;
    grabBeta = atan2f(-g.y, -g.x);
    spin = 0;
    tSurprise = 0.3f;
    if (leg) sayCat("grabLeg", 0.9f, L"", true);
}

void Pet::babyStep(float dt, const Input& in) {
    const Rig& R = app.rigBaby;
    facing = 1;
    const float m = 1.f, H = -R.top, I = 0.09f * H * H, Sx = S(), gB = 12.f * H;
    const V2 comL(0, -comH);
    V2 c = com();
    float impact = 0;
    bool touch = false;
    auto pointAt = [&](size_t i, float ca, float sa) {
        V2 v = R.hull[i] - comL;
        return c + V2(v.x * ca - v.y * sa, v.x * sa + v.y * ca);
    };
    if (gnd.valid) {
        if (gnd.kind == SurfKind::Window) {
            RECT r;
            if (!app.world.windowRect(gnd.hwnd, r)) {
                gnd.valid = false;
            } else {
                float dx = (float)(r.left - gnd.last.left), dy = (float)(r.top - gnd.last.top);
                if (dx != 0 || dy != 0) {
                    c += V2(dx, dy);
                    gnd.last = r;
                }
                gnd.y = (float)r.top;
                gnd.x0 = r.left + 6.f;
                gnd.x1 = r.right - 6.f;
            }
        } else if (gnd.kind == SurfKind::Floor && gnd.index >= 0 && gnd.index < (int)app.world.mons.size()) {
            const RECT& w = app.world.mons[gnd.index].work;
            gnd.y = (float)w.bottom;
            gnd.x0 = (float)w.left;
            gnd.x1 = (float)w.right;
        } else if (gnd.kind == SurfKind::Icon) {
            int i = app.icons.find(gnd.iconName);
            if (i < 0) gnd.valid = false;
            else iconStrip(i, gnd.y, gnd.x0, gnd.x1);
        }
    }
    const int N = 8;
    float h = dt / N;
    std::vector<V2> pts(R.hull.size());
    for (int it = 0; it < N; it++) {
        float ca = cosf(rot), sa = sinf(rot);
        float prevLow = -1e9f;
        for (size_t i = 0; i < R.hull.size(); i++) prevLow = std::max(prevLow, pointAt(i, ca, sa).y);
        vel.y += gB * h;
        c += vel * h;
        rot = wrapAngle(rot + rotVel * h);
        rotVel *= expf(-0.35f * h);
        ca = cosf(rot);
        sa = sinf(rot);
        float low = -1e9f, lowX = c.x;
        for (size_t i = 0; i < R.hull.size(); i++) {
            pts[i] = pointAt(i, ca, sa);
            if (pts[i].y > low) {
                low = pts[i].y;
                lowX = pts[i].x;
            }
        }
        if (!gnd.valid) {
            if (const Surf* s = findLanding(V2(lowX, prevLow), V2(lowX, low))) setGround(*s);
        }
        if (!gnd.valid && !app.world.mons.empty()) {
            int mi = app.world.monitorAt(V2(lowX, c.y));
            if (mi < 0) mi = app.world.nearestMonitor(c);
            float floorY = (float)app.world.mons[mi].work.bottom;
            if (low > floorY + 1.f && prevLow > floorY)
                for (auto& s : app.world.surfs)
                    if (s.kind == SurfKind::Floor && s.index == mi) {
                        setGround(s);
                        break;
                    }
        }
        if (gnd.valid) {
            float m0 = gnd.x0 - 6.f * Sx, m1 = gnd.x1 + 6.f * Sx, depth = -1e9f;
            for (auto& p : pts)
                if (p.x >= m0 && p.x <= m1) depth = std::max(depth, p.y - gnd.y);
            if (depth < -1e8f || c.x < m0 - 4.f * Sx || c.x > m1 + 4.f * Sx) {
                gnd.valid = false;
            } else {
                if (depth > -1.5f * Sx) touch = true;
                if (depth > 0) {
                    V2 r;
                    int n = 0;
                    for (auto& p : pts)
                        if (p.x >= m0 && p.x <= m1 && p.y - gnd.y > depth - 1.2f * Sx) {
                            r += p - c;
                            n++;
                        }
                    r = r / (float)n;
                    float vcx = vel.x - rotVel * r.y, vcy = vel.y + rotVel * r.x;
                    if (vcy > 0) {
                        impact = std::max(impact, vcy);
                        float e = vcy > 60.f * Sx ? 0.22f : 0.f, rn = -r.x, jn = (1 + e) * vcy / (1 / m + rn * rn / I);
                        vel.y -= jn / m;
                        rotVel += rn * jn / I;
                        vcx = vel.x - rotVel * r.y;
                        float rt = -r.y, jt = -vcx / (1 / m + rt * rt / I), mf = 0.9f * jn;
                        jt = clampf(jt, -mf, mf);
                        vel.x += jt / m;
                        rotVel += rt * jt / I;
                    }
                    c.y -= depth;
                    rotVel *= expf(-0.8f * 1.6f * h);
                    vel.x *= expf(-0.8f * 0.9f * h);
                }
            }
        }
        if (!app.world.mons.empty()) {
            int mi = app.world.monitorAt(c);
            if (mi < 0) mi = app.world.nearestMonitor(c);
            const RECT& mr = app.world.mons[mi].rc;
            float half = H * 0.42f;
            if (c.x < mr.left + half && app.world.monitorAt(V2(mr.left - 2.f, c.y)) < 0) {
                c.x = mr.left + half;
                vel.x = fabsf(vel.x) * 0.5f;
            }
            if (c.x > mr.right - half && app.world.monitorAt(V2(mr.right + 2.f, c.y)) < 0) {
                c.x = mr.right - half;
                vel.x = -fabsf(vel.x) * 0.5f;
            }
            if (c.y - H < mr.top && app.world.monitorAt(V2(c.x, mr.top - 2.f)) < 0 && vel.y < 0) {
                c.y = mr.top + H;
                vel.y = -vel.y * 0.3f;
            }
        }
    }
    pos = c + V2(0, comH);
    if (!app.world.mons.empty()) {
        int mi = app.world.nearestMonitor(pos);
        const RECT& mr = app.world.mons[mi].rc;
        if (pos.y > mr.bottom + 3.f * H || pos.y < mr.top - 20.f * H) {
            const RECT& w = app.world.mons[mi].work;
            pos = V2(clampf(pos.x, w.left + H, w.right - H), (float)w.bottom - 2.f);
            vel = {};
            rot = rotVel = 0;
            gnd.valid = false;
            logf("baby fell off-screen: put back on the floor");
        }
    }
    babyTouch = touch;
    bool calmNow = touch && fabsf(rotVel) < 0.25f && fabsf(vel.x) < 12.f * Sx && fabsf(vel.y) < 25.f * Sx;
    babyRest = calmNow ? babyRest + dt : 0.f;
    babySpin = (fabsf(rotVel) > 6.f || !touch) ? 0.6f : std::max(0.f, babySpin - dt);
    babyAir = touch ? 0.f : babyAir + dt;
    if (babyAir > 0.15f && st != St::Fall) setState(St::Fall);
    switch (st) {
    case St::Fall:
    case St::Air:
    case St::Leap:
        if (touch && stT > 0.05f) {
            if (impact > sqrtf(2.f * gB * H * 2.5f)) {
                vit.babyDrops++;
                tSad = 3.f;
                app.bubble.close();
                sayCat("babyCry", 1.f, L"", true);
                logf("baby dropped (%d)", vit.babyDrops);
            }
            setState(St::Land, napOnLand ? 0.6f : 0.25f);
        }
        break;
    case St::Land:
        if (stT >= stDur && (calmNow || stT > 3.f)) {
            if (napOnLand) {
                napOnLand = false;
                deepSleep = false;
                zzzT = 2.f;
                setState(St::Sleep, 1e9f);
            } else {
                goIdle(1.f, 2.f);
            }
        }
        break;
    case St::Idle:
        if (stT >= stDur) decide(in);
        break;
    case St::Sleep:
        zzzT -= dt;
        if (zzzT <= 0) {
            zzzT = 1.3f;
            emit(1, headTop + V2(headR * 0.55f, headR * 0.2f), V2(16.f * S(), -32.f * S()), 2.4f, 1.f);
        }
        break;
    default:
        break;
    }
}

float Pet::shakeMeter(V2 cur, float dt) {
    if (!shkOn) {
        shkOn = true;
        shkPrev = cur;
        shkDisp = {};
        shkPath = 0;
    }
    V2 d = cur - shkPrev;
    shkPrev = cur;
    float f = expf(-dt / 0.4f);
    shkPath = shkPath * f + d.len();
    shkDisp = shkDisp * f + d;
    return std::max(0.f, shkPath - shkDisp.len());
}

void Pet::carryStep(float dt, const Input& in) {
    if (sleepCarry) {
        carryT += dt;
        V2 want = in.cursor + carryOff;
        pos += (want - pos) * (1.f - expf(-16.f * dt));
        rot = approach(rot, clampf(-cursorVel.x / (4000.f * S()), -0.16f, 0.16f), 5, dt);
        float sp = cursorVel.len(), shk = shakeMeter(in.cursor, dt);
        float jolt = std::max(0.f, shk - 160.f * S()) / (160.f * S()) + std::max(0.f, sp - 3200.f * S()) / (3200.f * S());
        carryRough = std::max(0.f, carryRough - dt * 0.8f) + jolt * dt * 3.5f;
        if (carryRough > 1.f) {
            logf("carry asleep: shaken awake (speed %.0f shake %.0f)", sp, shk);
            sleepCarry = false;
            dragging = false;
            forcedWake(true);
            beginDrag(in.cursor);
            dragRough = 1.f;
            dragSaid = true;
            return;
        }
        if (carryRough > 0.45f && !carryMoaned) {
            carryMoaned = true;
            squashV -= 0.8f;
            if (!app.bubble.held()) {
                app.bubble.close();
                sayCat("sleepMoan", 1.f, L"", true);
            }
        } else if (!carrySaid && carryT > 2.5f) {
            carrySaid = true;
            sayCat("carrySleep", 0.5f, L"", true);
        }
        return;
    }
    lowerT += dt;
    V2 prev = pos;
    vel.x = 0;
    vel.y = std::min(vel.y + G() * 0.6f * dt, 520.f * S());
    pos.y += vel.y * dt;
    rot = approach(rot, 0, 8, dt);
    if (!app.world.mons.empty()) {
        int m = app.world.monitorAt(pos);
        if (m < 0) m = app.world.nearestMonitor(pos);
        float floorY = (float)app.world.mons[m].work.bottom;
        if (pos.y > floorY + 2 && prev.y > floorY + 1) {
            pos.y = floorY;
            prev.y = floorY - 1;
        }
    }
    if (const Surf* s = findLanding(prev, pos)) {
        pos.y = s->y;
        if (s->kind != SurfKind::Icon) pos.x = clampf(pos.x, s->x0, s->x1);
        setGround(*s);
        vel = {};
        rot = rotVel = 0;
        sleepLower = false;
        squashV -= 0.6f;
        logf("set down asleep on %d y=%.0f", (int)s->kind, s->y);
    } else if (lowerT > 2.5f) {
        sleepLower = false;
        forcedWake(true);
        startFall(vel);
    }
}

void Pet::releaseSleeper() {
    sleepCarry = false;
    airFrom = pos;
    airPlanned = false;
    jumpWin = fromWin = nullptr;
    bool soft = findLanding(pos - V2(0, 2.f), pos + V2(0, 320.f * S())) != nullptr;
    if (!soft && !app.world.mons.empty()) {
        int m = app.world.monitorAt(pos);
        if (m < 0) m = app.world.nearestMonitor(pos);
        soft = pos.y >= (float)app.world.mons[m].work.bottom - 1.f;
    }
    if (soft && cursorVel.len() < 2000.f * S()) {
        sleepLower = true;
        lowerT = 0;
        vel = {};
        logf("carry asleep: lowering");
        return;
    }
    logf("carry asleep: dropped from height");
    forcedWake(true);
    tSurprise = 0.9f;
    startFall(cursorVel * 0.4f);
}

void Pet::endDrag() {
    if (!dragging) return;
    dragging = false;
    pinPart = -1;
    tryHoldT = 0;
    tryPress = -1;
    if (sleepCarry) {
        releaseSleeper();
        return;
    }
    if (st == St::Led) {
        grabHand = 0;
        goIdle(1.f, 2.f);
        sayCat("ledStop", 0.7f, L"", true);
        return;
    }
    grabHand = 0;
    if (app.world.web) {
        V2 c = com();
        const RECT& r = app.world.webRc;
        if (c.x < r.left || c.x > r.right || c.y < r.top || c.y > r.bottom) leaveWeb(2);
    }
    if (tryMode && tryWnd) {
        if (tryHandDist < 10.f * S()) {
            vel = {};
            rotVel = 0;
            clickDone = 0;
            setState(St::Climb);
            return;
        }
        V2 d = tryTarget - com();
        const char* dir = fabsf(d.x) > fabsf(d.y) ? (d.x > 0 ? "tryRight" : "tryLeft") : (d.y > 0 ? "tryDown" : "tryUp");
        sayCat(dir, 1.f, L"", true);
    }
    V2 tang = V2(cosf(pend), -sinf(pend)) * (pendV * pendL);
    vel = cursorVel + tang;
    float lim = 3800.f * S();
    if (vel.len() > lim) vel = vel.norm() * lim;
    rotVel = -pendV * 0.8f;
    setState(St::Fall);
    if (tier() == 0 && !baby()) {
        vengeT = 4.f;
        tAngry = 2.f;
        sayCat("putDown", 0.9f, L"", true);
    }
    if (vel.len() > 1100.f * S()) sayCat("thrown", 0.8f, L"", true);
    else if (spin > 4 * PI) sayCat("dizzy", 0.8f, L"", true);
    if (spin > 4 * PI) tCross = 0.2f;
}

void Pet::poke() {
    if (vit.fatal) return;
    if (introHidden) return;
    if (peekCaught()) return;
    if (baby()) {
        if (growing) { introContinue(app, *this); return; }
        if (st == St::Sleep) {
            wake(true);
            app.bubble.close();
            pendingSay.clear();
            sayCat("babyCry", 1.f, L"", true);
            return;
        }
        pokeCount = (app.now - lastPoke < 3.0) ? pokeCount + 1 : 1;
        lastPoke = app.now;
        tSurprise = 0.35f;
        sayCat(pokeCount >= 4 ? "babyCry" : "babyCoo", pokeCount >= 4 ? 1.f : 0.5f, L"", true);
        return;
    }
    if (game) {
        if (++gamePokes < 2) {
            tSurprise = 0.35f;
            if (!app.bubble.asking() && !app.bubble.held()) app.bubble.close();
            sayCat("gamePoke", 1.f, L"", true);
            return;
        }
        gameStop("poked");
    }
    if (st == St::Sleep) {
        int need = vit.energy >= 75.f || afkNap ? 1 : 1 + (int)ceilf(4.f * (75.f - vit.energy) / 75.f);
        wakePokes = (app.now - lastWakePoke < 8.0) ? wakePokes + 1 : 1;
        lastWakePoke = app.now;
        if (wakePokes >= need) {
            forcedWake(false);
        } else {
            squashV -= 1.2f;
            tAngry = 0.8f;
            if (!app.bubble.held() && !byeOn) {
                app.bubble.close();
                sayCat("sleepMoan", 1.f, L"", true);
            }
        }
        return;
    }
    pokeCount = (app.now - lastPoke < 3.0) ? pokeCount + 1 : 1;
    lastPoke = app.now;
    if (weak()) {
        if (pokeCount >= 3) {
            pokeCount = 0;
            sayCat("hungryNoBite", 1.f, L"", true);
        }
        return;
    }
    emit(2, headTop + V2(headR * 0.75f, headR * 0.1f), V2(10.f * S(), -50.f * S()), 0.9f, 1.f);
    int hate = tier() == 0 ? 1 : 0;
    if (pokeCount >= 4 - hate) {
        tAngry = 2.5f;
        pokeCount = 0;
        if (!annoyedAttack()) sayCat("pokeMany", 1.f, L"", true);
    } else if (pokeCount == 3 - hate) {
        warnBite("pokeWarn");
    } else {
        tSurprise = 0.35f;
        sayCat("poke", 0.6f, L"", true);
    }
}

void Pet::doubleClick() {
    if (!grounded() || st == St::Sleep || st == St::PrepJump || vit.fatal || weak()) return;
    if (st == St::Sit || st == St::Watch) {
        tSurprise = 0.5f;
        emit(2, headTop + V2(headR * 0.75f, headR * 0.1f), V2(10.f * S(), -50.f * S()), 0.9f, 1.f);
        return;
    }
    if (!goJump(pos, 80, 120)) return;
    sayCat("jump", 0.5f, L"", true);
}

void Pet::comeHere(V2 cur) {
    if (dragging || app.world.mons.empty()) return;
    gameStop("summon");
    if (st == St::Inside || st == St::Dive) { callBack(cur); return; }
    if (webOn) leaveWeb(4);
    sink = 0;
    int m = app.world.monitorAt(cur);
    if (m < 0) m = app.world.nearestMonitor(cur);
    const RECT& r = app.world.mons[m].rc;
    pos = V2(clampf(cur.x, r.left + 60.f * S(), r.right - 60.f * S()), r.top + comH * 1.3f + 10.f);
    vel = V2(0, 200.f * S());
    rot = rotVel = 0;
    gnd.valid = false;
    fetchName.clear();
    setState(St::Fall);
    app.inst.resetSim();
    sayCat("summon", 1.f, L"", true);
}

void Pet::updatePetting(float dt, const Input& in) {
    V2 d = in.cursor - lastCursor;
    bool can = !dragging && !in.lmb && !vit.fatal && !weak() &&
               (st == St::Idle || st == St::Walk || st == St::Sit || st == St::Sleep || st == St::LookAround || st == St::Land || st == St::Wave ||
                st == St::Watch || st == St::Lift || (st == St::Trip && stT > 0.35f));
    bool inside = (in.cursor - headC).len() < headR * 1.1f;
    petBlock -= dt;
    double now = app.now;
    if (can && inside && petBlock <= 0) {
        rubInside += dt;
        for (int ax = 0; ax < 2; ax++) {
            float m = ax ? d.y : d.x;
            float& acc = ax ? rubAcc.y : rubAcc.x;
            float& dir = ax ? rubDir.y : rubDir.x;
            if (fabsf(m) < 0.3f) continue;
            float sgn = m > 0 ? 1.f : -1.f;
            if (sgn != dir) {
                if (acc > 8.f * S()) rubRevs.push_back(now);
                acc = 0;
                dir = sgn;
            }
            acc += fabsf(m);
        }
    } else {
        rubInside = 0;
        rubRevs.clear();
        rubAcc = {};
    }
    while (!rubRevs.empty() && now - rubRevs.front() > 1.6) rubRevs.erase(rubRevs.begin());
    int recent = 0;
    for (double r : rubRevs)
        if (now - r < 1.0) recent++;
    rub = (float)recent;
    bool on = can && inside && petBlock <= 0 && rubInside > 0.3f && rubRevs.size() >= 3;
    if (on && !petting && nightmare) {
        if (!nightmarePetted()) comfortNightmare(true);
        petBlock = 4.f;
        on = false;
    }
    bool comforted = false;
    if (on && !petting && tripUpset > 0 && st != St::Sleep) {
        tripComfort();
        comforted = true;
        if (tier() == 0) {
            petBlock = 4.f;
            on = false;
        }
    }
    if (on && !petting) {
        petting = true;
        petTime = 0;
        petStage = 0;
        purrT = randf(2.f, 4.f);
        if (st == St::Walk || st == St::LookAround || st == St::Wave) setState(St::Idle, 3.f);
        if (!comforted && st != St::Sleep && !wantTouched("pet") && !momentTouch("pet") && app.now - lastPetEnd > 15 && tier() > 0)
            sayCat("petStart", 0.85f, L"", true);
    }
    if (petting && (!can || !inside || recent == 0)) {
        petting = false;
        sleepPetT = 0;
        lastPetEnd = app.now;
        if (petTime > 3.f && petStage != 2) dayCount("pet");
        if (petTime > 3.f && petStage != 2 && tier() > 0 && st != St::Sleep && app.now - lastPetGain > 600) {
            lastPetGain = app.now;
            addRel(0.5f, "petted");
        }
    }
    if (petting) {
        petTime += dt;
        if (st == St::Sleep && stirT < 0 && tier() > 0 && !baby()) {
            sleepPetT += dt;
            if (sleepPetT > 10.f) gentleWake("pet");
        } else if (st != St::Sleep) {
            sleepPetT = 0;
        }
        petLevel = std::min(1.f, petLevel + dt * 0.6f);
        if ((st == St::Idle || st == St::Sit || st == St::Watch) && stT > stDur - 0.5f) stDur = stT + 1.5f;
        int tr = tier();
        heartT -= dt;
        if (heartT <= 0 && tr > 0) {
            heartT = randf(0.35f, 0.65f) * (tr == 3 ? 0.7f : 1.f);
            emit(0, headTop + V2(randf(-0.6f, 0.6f) * headR, headR * 0.2f), V2(randf(-25, 25) * S(), -75.f * S()), 1.4f, randf(0.8f, 1.2f));
        }
        if (st != St::Sleep && tr == 0) {
            if (petStage == 0 && petTime > 0.8f) {
                petStage = 1;
                warnBite("petHateWarn");
            }
            if (petStage == 1 && petTime > 2.2f) {
                petStage = 2;
                sayCat("petHateBite", 1.f, L"", true);
                tAngry = 2.6f;
                petting = false;
                petBlock = 6.f;
                rub = 0;
                lastPetEnd = app.now;
                annoyedAttack();
            }
        } else if (st != St::Sleep && tr >= 2) {
            cuddleAdd(dt * 0.35f, nullptr);
            if (petStage == 0 && petTime > 4.f) {
                petStage = 1;
                sayCat(tr == 3 ? "petLove" : "petMore", 0.9f, L"", true);
            }
            if (tr == 3) {
                purrT -= dt;
                if (purrT <= 0) {
                    purrT = randf(5.f, 9.f);
                    sayCat("purr", 0.7f, L"", true);
                }
            }
            if (petTime > 7.f && petLevel > 0.95f && tHeart <= 0 && chance(dt * 0.3f)) tHeart = 1.6f;
            if (tr == 2 && petStage == 1 && petTime > 10.f) {
                petStage = 3;
                sayCat("petWarm", 0.8f, L"", true);
            }
            if (tr == 2 && petTime > 28.f) {
                petStage = 4;
                sayCat("petWarmEnough", 1.f, L"", true);
                tBlush = 2.5f;
                petting = false;
                petBlock = 8.f;
                rub = 0;
                lastPetEnd = app.now;
            }
        } else if (st != St::Sleep) {
            if (petStage == 0 && petTime > 4.f) {
                petStage = 1;
                sayCat("petMore", 0.9f, L"", true);
            }
            if (petStage == 1 && petTime > 7.f && petTime < 9.f && petLevel > 0.95f && tHeart <= 0 && chance(dt * 0.5f)) tHeart = 1.6f;
            if (petStage == 1 && petTime > 9.f && app.now - petWarnAt > 30) {
                petWarnAt = app.now;
                tHeart = 0;
                warnBite("petWarn");
            }
            if (petStage == 1 && petTime > 12.f && app.now - petWarnAt < 10 && app.now - petWarnAt > 2.5) {
                petStage = 2;
                sayCat("petTooMuch", 1.f, L"", true);
                tAngry = 2.6f;
                petting = false;
                petBlock = 6.f;
                rub = 0;
                lastPetEnd = app.now;
                if (!annoyedAttack()) {
                    float away = in.cursor.x < pos.x ? 1.f : -1.f;
                    goWalk(clampf(pos.x + away * randf(120, 220) * S(), gnd.x0, gnd.x1));
                }
            }
        }
    } else {
        petLevel = std::max(0.f, petLevel - dt * 0.3f);
    }
}

void Pet::updateFace(float dt, const Input& in) {
    tSurprise -= dt; tAngry -= dt; tHappy -= dt; tHeart -= dt; tCross -= dt; tSad -= dt;
    if (freezeFace) return;
    FaceState tg;
    bool sleeping = st == St::Sleep || vit.fatal;
    if (sleeping) tg.sleep = 1;
    else if (st == St::Dizzy) { tg.dizzy = tCross > 0 ? 0.f : 1.f; tg.cross = tCross > 0 ? 1.f : 0.f; }
    else if ((petting || (petLevel > 0.15f && bPet > 0.1f)) && tier() == 0) tg.angry = 0.8f;
    else if (petting || (petLevel > 0.15f && bPet > 0.1f)) {
        float h = smooth01(petLevel * 1.4f);
        tg.happy = h;
        tg.squint = 1 - h;
    }
    if (dragging && !sleepCarry) {
        int tr = tier();
        if (stT < 0.4f && st != St::Led) tg.surprise = 1;
        else if (tr == 0) tg.angry = 0.8f;
        else if (tr == 3) tg.blush = std::max(tg.blush, 0.35f);
        else if (dragRough > 0.35f || (st == St::Dragged && pinPart >= 0)) tg.angry = 0.6f;
    }
    if ((st == St::Fall || st == St::Air) && vel.len() > 900.f * S()) tg.surprise = 1;
    if (tSurprise > 0) tg.surprise = 1;
    if (tAngry > 0) { tg.angry = 1; tg.happy = tg.squint = 0; }
    if (tHappy > 0 && !sleeping) { tg.happy = 1; tg.squint = 0; }
    if ((tSad > 0 || nightmare) && !sleeping) { tg.sad = 1; tg.happy = tg.squint = 0; }
    if (!sleeping && !weak() && tier() == 0 && tg.happy < 0.5f) tg.angry = std::max(tg.angry, 0.35f);
    if (!sleeping && !dragging && !petting && !weak() && tier() == 3 && (in.cursor - headC).len() < headR * 3.f)
        tg.blush = std::max(tg.blush, 0.3f);
    if (tHeart > 0 && !sleeping) { tg.heart = 1; tg.happy = tg.squint = 0; }
    if (tCross > 0) { tg.cross = 1; tg.dizzy = 0; }
    if (st == St::Bite || st == St::Cling || st == St::Leap) { tg.angry = 1; tg.surprise = 0; tg.happy = tg.squint = 0; }

    tg.blush = std::max(tg.blush, tier() == 0 ? 0.f : petLevel);
    if (tAngry > 0 && petStage == 2) tg.blush = 0.8f;
    if (st == St::Led && vit.rel >= 67.f) tg.blush = std::max(tg.blush, 0.6f);
    V2 lifeLk(9, 9);
    lifeFace(tg, lifeLk);
    kissFace(tg, lifeLk);
    emoteFace(tg, lifeLk);
    if (deskMode == DeskOffer && !sleeping) {
        tg.sad = 1;
        tg.angry = std::max(tg.angry, 0.45f);
        tg.happy = tg.squint = 0;
    }
    if (!heldPath.empty() && heldStage >= 1 && heldSense.self > 0 && !sleeping && !baby())
        tg.blush = std::max(tg.blush, heldSense.self == 3 ? 0.9f : heldSense.self == 2 ? 0.7f : 0.45f);
    if (game && !sleeping) game->face(*this, tg, lifeLk);
    if (tSquint > 0 && !sleeping) { tg.squint = 1; tg.happy = 0; }
    if (eatT > 0) { tg.happy = 1; tg.squint = 0; }
    tg.hungry = hungerK();
    if (baby() && babySpin > 0 && !sleeping) { tg.dizzy = 1; tg.happy = tg.squint = 0; }
    if (sleeping) tg.happy = tg.squint = tg.heart = tg.dizzy = tg.cross = 0;
    {
        float* shapes[] = {&tg.happy, &tg.squint, &tg.heart, &tg.dizzy, &tg.cross};
        float* best = nullptr;
        float bv = 0.f;
        for (float* sp : shapes)
            if (*sp > bv) { bv = *sp; best = sp; }
        for (float* sp : shapes) *sp = (sp == best && bv >= 0.35f) ? 1.f : 0.f;
    }
    float tk = tiredK();
    if (groggyT > 0) tk = std::max(tk, 0.75f * std::min(1.f, groggyT / 60.f) + 0.2f);
    tg.tired = tk * (1.f - clampf(std::max(tg.surprise, tg.angry), 0.f, 1.f));
    if (solverK > 0.01f) tg.tired = 0;
    if (vit.fatal) {
        tg = FaceState{};
        tg.fatal = 1;
        sleeping = true;
    }

    if (!sleeping) {
        blinkT -= dt;
        if (blinkT <= 0 && blinkPh < 0) {
            blinkPh = 0;
            blinkLen = 0.14f + 0.55f * tk;
            blinkHold = (tk > 0.35f && chance(0.35f * tk)) ? randf(0.8f, 1.8f) : 0.f;
            blinkT = chance(0.15f * (1.f - tk)) ? 0.28f : randf(2.f, 5.5f) + 1.5f * tk;
        }
    }
    float nodTo = 0.f;
    if (blinkPh >= 0) {
        blinkPh += dt;
        float cl = blinkLen * 0.4f, op = blinkLen * 0.6f;
        if (blinkPh < cl) {
            face.open = 1.f - blinkPh / cl;
        } else if (blinkPh < cl + blinkHold) {
            face.open = 0.f;
            nodTo = 1.f;
        } else {
            float p = std::min(1.f, (blinkPh - cl - blinkHold) / op);
            face.open = blinkHold > 0 ? smooth01(p) : p;
            if (p >= 1) blinkPh = -1;
        }
    } else {
        face.open = approach(face.open, 1, 20, dt);
    }
    if (solverK > 0.01f) face.open = 1.f;
    nod = approach(nod, sleeping ? 0.f : nodTo, nodTo > nod ? 2.5f : 6.f, dt);
    const float r = 16;
    face.happy = approach(face.happy, tg.happy, r, dt);
    face.squint = approach(face.squint, tg.squint, r, dt);
    face.angry = approach(face.angry, tg.angry, r, dt);
    face.sad = approach(face.sad, tg.sad, r, dt);
    face.surprise = approach(face.surprise, tg.surprise, r, dt);
    face.sleep = approach(face.sleep, tg.sleep, 8, dt);
    face.heart = approach(face.heart, tg.heart, r, dt);
    face.dizzy = approach(face.dizzy, tg.dizzy, r, dt);
    face.cross = approach(face.cross, tg.cross, r, dt);
    face.blush = approach(face.blush, tg.blush, 4, dt);
    face.fatal = approach(face.fatal, tg.fatal, 6, dt);
    face.hungry = approach(face.hungry, tg.hungry, 2, dt);
    face.tired = approach(face.tired, tg.tired, 3, dt);
    face.solver = solverK;
    face.sigil = sigilK;
    tSquint -= dt;
    float mo = 0, te = 0;
    if (app.bubble.active && app.bubble.t < app.bubble.typeDur + 0.1f && st != St::Sleep) {
        float syl = fabsf(sinf(t * 15.f)) * (0.6f + 0.4f * sinf(t * 4.3f));
        mo = 0.15f + 0.5f * syl;
    }
    if (st == St::Bite) {
        mo = stT < 0.18f ? smooth01(stT / 0.18f) : 0.15f + 0.85f * fabsf(cosf((stT - 0.18f) * 11.f));
        te = 1;
    }
    if (st == St::Cling) { mo = clingMouth; te = 1; }
    eatT -= dt;
    if (eatT > 0) { mo = 0.12f + 0.6f * fabsf(sinf(eatT * 22.f)); te = 1; }
    mo = std::max(mo, mouthWant * 0.85f);
    if (batHover && !vit.fatal && vit.food <= 85.f) mo = std::max(mo, 0.8f);
    biteSnapT -= dt;
    if (biteHop) { mo = 0.95f; te = 1; tg.angry = 1; }
    if (biteSnapT > 0) { mo = biteSnapT > 0.25f ? 0.95f : 0.12f + 0.5f * fabsf(sinf(biteSnapT * 40.f)); te = 1; tg.angry = 1; }
    if (st == St::Leap || (dragging && !sleepCarry && stT < 0.4f) || tSurprise > 0) mo = std::max(mo, 0.55f);
    face.mouth = approach(face.mouth, mo, 30, dt);
    face.mouthX = approach(face.mouthX, clampf(face.look.x * 0.5f + (tAngry > 0 ? 0.15f : 0.f), -0.6f, 0.6f), 8, dt);
    face.teeth = approach(face.teeth, te, 20, dt);

    V2 lk;
    if (!sleeping && st != St::Dizzy) {
        V2 d = rotv((in.cursor - headC) / (320.f * S()), -rot);
        lk = V2(clampf(d.x, -1, 1), clampf(d.y, -1, 1));
        if (st == St::LookAround) lk = V2(lookTurn, -0.1f);
        if (!heldPath.empty() && !dragging && (fmodf(heldT, 4.f) < 3.f || (heldStage >= 1 && heldSense.self > 0))) lk = V2(0.05f, 0.75f);
        if (fileHovering) tSurprise = std::max(tSurprise, 0.1f);
        if (lifeLk.x < 5) lk = lifeLk;
        if (hungerK() > 0.6f && !feeding) lk = V2(0.f, 0.12f);
        if (tk > 0.f) lk = V2(lk.x * (1.f - 0.4f * tk), lerpf(lk.y, 0.35f, 0.5f * tk));
        lk.x *= (float)facing;
    }
    face.look.x = approach(face.look.x, lk.x, 9, dt);
    face.look.y = approach(face.look.y, lk.y, 9, dt);
}

void Pet::animate(float dt) {
    auto& P = curInst().pose;
    for (auto& p : P) p = PartPose{};
    bool onGround = grounded();
    bWalk = approach(bWalk, (st == St::Walk || (st == St::Led && fabsf(ledSpeed) > 10.f * S())) ? 1.f : 0.f, 10, dt);
    bSit = approach(bSit, ((st == St::Sit || st == St::Sleep || st == St::Watch) && active == 0) ? 1.f : 0.f, 6, dt);
    bSleep = approach(bSleep, st == St::Sleep ? 1.f : 0.f, 3, dt);
    bCarry = approach(bCarry, holding() ? 1.f : 0.f, 8, dt);
    bAir = approach(bAir, (st == St::Fall || st == St::Air || st == St::Leap) ? 1.f : 0.f, 9, dt);
    bCling = approach(bCling, st == St::Cling ? 1.f : 0.f, 14, dt);
    bBite = approach(bBite, st == St::Bite ? 1.f : 0.f, 14, dt);
    bDrag = approach(bDrag, st == St::Dragged ? 1.f : 0.f, 10, dt);
    bPet = approach(bPet, ((petting || petLevel > 0.2f) && onGround) ? 1.f : 0.f, 6, dt);
    bWave = approach(bWave, st == St::Wave ? 1.f : 0.f, 8, dt);
    crouch = approach(crouch, (st == St::PrepJump || st == St::GrabIcon || st == St::DropIcon) ? 1.f : 0.f, 18, dt);
    if (st != St::Walk) walkPhase = approach(walkPhase, roundf(walkPhase / PI) * PI, 6, dt);
    squashV += (-340.f * squash - 17.f * squashV) * dt;
    squash = clampf(squash + squashV * dt, -0.4f, 0.3f);

    PartPose dummy;
    auto R = [&](int i) -> PartPose& {
        if (i >= 0) return P[i];
        dummy = PartPose{};
        return dummy;
    };
    float S_ = S();
    float br = sinf(t * 2.f * PI / 2.8f) * (1 - bAir) * (1 - bDrag);
    R(iBody).scl.y = 1 + 0.014f * br;
    R(iHead).off.y = br * 0.8f * S_;
    float ws = sinf(walkPhase), wc = cosf(walkPhase);
    float freeArms = 1 - bCarry;

    R(iLegL).rot += 0.13f * ws * bWalk;
    R(iLegR).rot += -0.13f * ws * bWalk;
    R(iLegL).off.y -= std::max(0.f, -wc) * 5.f * S_ * bWalk;
    R(iLegR).off.y -= std::max(0.f, wc) * 5.f * S_ * bWalk;
    R(iShinL).rot += 0.22f * std::max(0.f, -wc) * bWalk;
    R(iShinR).rot -= 0.22f * std::max(0.f, wc) * bWalk;
    R(iArmL).rot += (-0.16f * ws + 0.05f) * bWalk * freeArms;
    R(iArmR).rot += (-0.16f * ws - 0.05f) * bWalk * freeArms;
    R(iBody).rot += 0.05f * bWalk;
    rootBob = -fabsf(ws) * 3.f * S_ * bWalk;

    float sw = (1 - bSleep);
    R(iLegL).rot += (0.06f + 0.12f * sinf(t * 2.6f) * sw) * bSit;
    R(iLegR).rot += (-0.06f + 0.12f * sinf(t * 2.6f + 1.9f) * sw) * bSit;
    R(iLegL).scl.y *= lerpf(1, 0.9f, bSit);
    R(iLegR).scl.y *= lerpf(1, 0.9f, bSit);
    R(iArmL).rot += 0.2f * bSit * freeArms;
    R(iArmR).rot -= 0.2f * bSit * freeArms;

    if (active == 1) R(iLegs).rot += 0.05f * sinf(t * 2.3f) + 0.02f * sinf(t * 3.7f);

    R(iHead).rot += 0.24f * bSleep;
    R(iHead).off.y += 2.f * S_ * bSleep;
    R(iBody).rot += 0.04f * bSleep;
    R(iBody).scl.y += 0.012f * sinf(t * 1.4f) * bSleep;
    R(iHead).rot += 0.20f * nod;
    R(iHead).off.y += 3.f * S_ * nod;

    R(iArmL).rot += (-0.30f + 0.03f * sinf(t * 7.f)) * bCarry;
    R(iArmR).rot -= (-0.30f + 0.03f * sinf(t * 7.f + 1.f)) * bCarry;
    R(iFArmL).rot += -1.05f * bCarry;
    R(iFArmR).rot += 1.05f * bCarry;
    bOffer = approach(bOffer, deskMode == DeskOffer && st == St::Lift ? 1.f : 0.f, 8, dt);
    float offer = (0.16f + 0.06f * sinf(t * 7.f)) * bOffer, reachOut = 0.38f * bOffer;
    R(iArmL).rot -= offer + reachOut;
    R(iArmR).rot += offer - reachOut;

    float fl = sinf(t * 17.f);
    float desc = smooth01(vel.y / (1100.f * S_) + 0.5f);
    float armAir = 0.75f + 0.65f * desc;
    R(iArmL).rot += (armAir + 0.18f * fl * desc) * bAir * freeArms;
    R(iArmR).rot -= (armAir - 0.18f * fl * desc) * bAir * freeArms;
    R(iFArmL).rot += (0.55f - 0.25f * desc) * bAir * freeArms;
    R(iFArmR).rot -= (0.55f - 0.25f * desc) * bAir * freeArms;
    R(iLegL).rot += -0.10f * bAir;
    R(iLegR).rot += 0.10f * bAir;
    R(iLegL).off.y -= 5.f * S_ * (1 - desc) * bAir;
    R(iLegR).off.y -= 3.f * S_ * (1 - desc) * bAir;
    R(iShinL).rot += 0.35f * (1 - desc) * bAir;
    R(iShinR).rot -= 0.35f * (1 - desc) * bAir;
    if (st == St::Leap) R(iArmR).rot -= 1.5f;

    float fd = sinf(t * 15.f) * dragAgit;
    R(iArmL).rot += (0.8f + 0.6f * fd) * bDrag * freeArms;
    R(iArmR).rot -= (0.8f - 0.6f * fd) * bDrag * freeArms;
    R(iFArmL).rot += (0.35f + 0.25f * fd) * bDrag * freeArms;
    R(iFArmR).rot -= (0.35f - 0.25f * fd) * bDrag * freeArms;
    R(iShinL).rot += 0.25f * sinf(t * 12.f + 0.7f) * dragAgit * bDrag;
    R(iShinR).rot -= 0.25f * sinf(t * 12.f + 1.7f) * dragAgit * bDrag;
    R(iLegL).rot += 0.35f * sinf(t * 12.f) * dragAgit * bDrag;
    R(iLegR).rot -= 0.35f * sinf(t * 12.f + 1.f) * dragAgit * bDrag;

    R(iArmL).rot += 2.3f * bCling;
    R(iArmR).rot -= 2.3f * bCling;
    R(iFArmL).rot += 2.3f * bCling;
    R(iFArmR).rot -= 2.3f * bCling;
    R(iLegL).rot += 0.18f * sinf(t * 7.f) * bCling;
    R(iLegR).rot -= 0.18f * sinf(t * 7.f + 1.3f) * bCling;
    if (bCling > 0.3f)
        for (int i : {iArmL, iArmR, iFArmL, iFArmR}) R(i).zOff = 25;

    bTrip = approach(bTrip, st == St::Trip && stT < 1.5f ? 1.f : 0.f, st == St::Trip && stT < 1.5f ? 12.f : 5.f, dt);
    float kick = st == St::Trip ? sinf(stT * 15.f) * clampf(1.f - (stT - 0.3f) / 0.7f, 0.f, 1.f) : 0.f;
    R(iArmL).rot += 2.45f * bTrip;
    R(iArmR).rot -= 2.45f * bTrip;
    R(iFArmL).rot += 0.25f * bTrip;
    R(iFArmR).rot -= 0.25f * bTrip;
    R(iLegL).rot += (0.06f + 0.16f * kick) * bTrip;
    R(iLegR).rot += (-0.06f + 0.16f * kick) * bTrip;
    R(iShinL).rot += 0.3f * std::max(0.f, kick) * bTrip;
    R(iShinR).rot -= 0.3f * std::max(0.f, -kick) * bTrip;
    if (bTrip > 0.3f)
        for (int i : {iArmL, iArmR, iFArmL, iFArmR}) R(i).zOff = 25;

    if (grabHand && (st == St::Led || st == St::Dragged)) {
        int ai = grabHand == 1 ? iArmL : iArmR, fi = grabHand == 1 ? iFArmL : iFArmR;
        float target;
        if (st == St::Led && ai >= 0) {
            const PartDef& ad = curRig().parts[ai];
            int hi = fi >= 0 ? fi : ai;
            const PartDef& hd = curRig().parts[hi];
            V2 rest = (hd.hand() - ad.joint).norm();
            V2 dl = rotv(lastCursor - shoulderScreen(grabHand), -rot);
            dl.x *= (float)facing;
            target = atan2f(cross(rest, dl), dot(rest, dl));
            R(iBody).rot += clampf(dl.x / std::max(dl.len(), 1.f), -1.f, 1.f) * 0.08f;
        } else {
            target = grabHand == 1 ? 2.55f : -2.55f;
        }
        PartPose& ap = R(ai);
        ap.rot = target;
        R(fi).rot = 0;
        R(ai).zOff = R(fi).zOff = 25;
        (void)target;
    }

    if (bCling > 0.3f || bWave > 0.3f || st == St::Leap) {
        R(iArmR).zOff = 25;
        R(iFArmR).zOff = 25;
    }

    if (st == St::Bite) {
        float lunge = stT < 0.18f ? smooth01(stT / 0.18f) : 1.f - 0.25f * fabsf(sinf((stT - 0.18f) * 22.f));
        lunge *= clampf((stDur - stT) / 0.2f, 0.f, 1.f);
        V2 dl(biteDir.x * facing, biteDir.y);
        R(iHead).off += dl * (headR * 0.1f * lunge);
        R(iHead).rot += dl.x * 0.05f * lunge;
        R(iBody).rot += dl.x * 0.05f * lunge;
    }

    if (!heldPath.empty() && !dragging) R(iHead).rot += 0.10f * sinf(heldT * 1.4f) + 0.05f;

    float tilt = clampf((lastCursor.x - headC.x) / std::max(headR, 1.f), -1, 1) * 0.16f * facing;
    R(iHead).rot += tilt * bPet + 0.03f * sinf(t * 3.f) * bPet;
    R(iBody).rot += 0.03f * sinf(t * 3.f + 0.5f) * bPet;

    if (iFArmR >= 0) {
        R(iArmR).rot += -1.75f * bWave;
        R(iFArmR).rot += (-0.95f + 0.55f * sinf(t * 11.f)) * bWave;
    } else {
        R(iArmR).rot += (-2.3f + 0.35f * sinf(t * 11.f)) * bWave;
    }

    float lx = clampf(face.look.x + lookTurn * 0.8f, -1.2f, 1.2f), ly = face.look.y;
    R(iHairF).off += V2(lx * 3.5f, ly * 1.2f) * S_;
    R(iBeanie).off += V2(lx * 2.6f, ly * 0.8f) * S_;
    R(iHairB).off += V2(-lx * 2.2f, -ly * 0.6f) * S_;
    R(iHead).off.x += lx * 0.8f * S_;

    R(iHead).rot += face.look.x * 0.07f * (1 - bSleep) + lookTurn * 0.12f;
    R(iArmL).rot -= 0.25f * crouch * freeArms;
    R(iArmR).rot += 0.25f * crouch * freeArms;
    if (st == St::Dizzy) {
        R(iBody).rot += 0.07f * sinf(t * 6.f);
        R(iHead).rot += 0.10f * sinf(t * 6.f + 1.f);
    }
    lifePose(dt);
    extPose(dt);
    if (game) game->pose(*this);
    kissPose(dt);
    emotePose(dt);
    if (tryMode && active == 0 && (st == St::Dragged || st == St::Climb) && !grabHand) {
        V2 jab;
        if (tryPress >= 0) {
            float back = tryPress < 0.2f ? smooth01(tryPress / 0.2f) : 1.f - smooth01((tryPress - 0.2f) / 0.08f);
            V2 toSh = shoulderScreen(1) - tryTarget;
            if ((shoulderScreen(2) - tryTarget).len() < toSh.len()) toSh = shoulderScreen(2) - tryTarget;
            jab = toSh.norm() * (14.f * S() * back);
        }
        tryHandDist = reachTo(tryTarget + jab);
        if (tryPress >= 0) tryHandDist = 0;
    } else {
        tryHandDist = 1e9f;
    }
    if (grabHand && pinPart < 0 && active == 0 && (st == St::Led || st == St::Dragged)) reachTo(lastCursor, grabHand);

    if ((vit.food < 10.f || nightmare) && !vit.fatal) {
        float tr = std::max((10.f - vit.food) / 10.f, nightmare ? 0.45f * (1.f - 0.85f * nightmareCalm) : 0.f);
        R(iBody).off.x += sinf(t * 47.f) * 0.9f * S_ * tr;
        R(iHead).rot += sinf(t * 53.f + 1.f) * 0.025f * tr;
    }
    if (active == 3) {
        float cry = (tSad > 0 || weak()) ? 1.f : 0.f;
        R(iBody).rot += (0.05f * sinf(t * 1.8f) + cry * 0.03f * sinf(t * 31.f)) * (1.f - bDrag);
    }
    if (vit.fatal && active == 0)
        for (auto& pp : P) {
            float z = pp.zOff;
            pp = PartPose{};
            pp.zOff = z;
        }

    if (active == 0)
        for (int side = 1; side <= 2; side++) {
            int ai = side == 1 ? iArmL : iArmR, fi = side == 1 ? iFArmL : iFArmR;
            if (ai < 0 || fi < 0 || ai >= (int)P.size() || fi >= (int)P.size()) continue;
            float sgn = side == 1 ? -1.f : 1.f;
            float inward = P[ai].rot * sgn, fore = P[fi].rot * sgn;
            if (P[ai].zOff >= 25 || P[fi].zOff >= 25) {
                P[ai].zOff += 9;
                P[fi].zOff += 9;
            } else if (inward > 0.18f || inward + fore > 0.45f) {
                P[ai].zOff = 9;
                P[fi].zOff = 9;
            }
        }
}

V2 Pet::windowOrigin() const {
    V2 c = com();
    return V2(floorf(c.x - app.comInWin.x + 0.5f), floorf(c.y - app.comInWin.y + 0.5f));
}

void Pet::stepRig(float dt, V2 wo) {
    float sy = (1 + squash) * (1 - 0.16f * crouch);
    float sx = (1 - squash * 0.6f) * (1 + 0.1f * crouch);
    if (st == St::Air && vel.y < 0) { sy *= 1.06f; sx *= 0.96f; }
    const Rig& RG = curRig();
    RigInstance& I = curInst();
    M3 root;
    if (active == 0) {
        float lie = (st == St::Fatal || st == St::Trip) ? fabsf(sinf(rot)) * std::max(0.f, comH - headR * 0.85f) : 0.f;
        V2 feet = pos - wo + V2(0, rootBob + bSit * legLen * 0.92f + sink * sinkDepthPx() + lie);
        root = M3::Scale(sx * facing, sy, D2D1::Point2F(0, 0)) * M3::Rotation(rot * 180.f / PI, D2D1::Point2F(0, -comH * sy)) *
               M3::Translation(feet.x, feet.y);
    } else if (active == 3) {
        V2 at = pos - wo + V2(0, rootBob);
        root = M3::Scale(sx * facing, sy, D2D1::Point2F(0, 0)) * M3::Rotation(rot * 180.f / PI, D2D1::Point2F(0, -comH * sy)) *
               M3::Translation(at.x, at.y);
    } else {
        V2 at = pos - wo;
        root = M3::Rotation(RG.rootAngle * 180.f / PI, D2D1::Point2F(0, 0)) * M3::Scale(sx * facing, sy, D2D1::Point2F(0, 0)) *
               M3::Translation(at.x, at.y);
    }
    float wantLoose = vit.fatal ? 1.f : st == St::Dragged ? 0.5f : st == St::Cling ? 0.75f : (st == St::Fall || st == St::Air || st == St::Leap) ? 0.45f : 0.15f;
    loose = approach(loose, wantLoose, 5.f, dt);
    bool pinOn = active == 0 && dragging && pinPart >= 0 && (st == St::Dragged || st == St::Led);
    I.pinPart = pinOn ? pinPart : -1;
    I.pinRoot = pinOn ? pinRootOf(pinPart) : -1;
    I.pinLocal = pinLocal;
    I.pinTarget = lastCursor;
    I.step(dt, root, wo, (active == 0 || active == 3) ? rot : 0.f, facing, G(), loose);
    const M3& hm = iHead >= 0 ? I.world[iHead] : root;
    headC = xf(hm, RG.headCenter) + wo;
    headTop = (active == 0 && iHead >= 0) ? xf(hm, V2(RG.headCenter.x, RG.top)) + wo
                                          : xf(hm, RG.headCenter - V2(0, RG.headRadius)) + wo;
    if (introHidden) {
        headTop = introAnchor;
        headC = introAnchor + V2(0, 24.f * S());
    }
    if (active == 0 && sink == 0 && st != St::Dragged && st != St::Cling && fabsf(rot) < 0.05f) {
        standTop = pos.y - headTop.y;
        if (RG.face.on && RG.face.part >= 0) standEye = pos.y - (xf(I.world[RG.face.part], RG.face.center).y + wo.y) - RG.face.size.y * 0.45f;
        else standEye = pos.y - headC.y - headR * 0.6f;
    }
    if (holding() || extFolder) {
        int hl = iFArmL >= 0 ? iFArmL : iArmL, hr = iFArmR >= 0 ? iFArmR : iArmR;
        if (hl >= 0 && hr >= 0) {
            auto hand = [&](int i) {
                const PartDef& d = RG.parts[i];
                return xf(I.world[i], d.hand()) + wo;
            };
            carriedCenter = (hand(hl) + hand(hr)) * 0.5f + rotv(V2(0, -app.icons.iconPx * 0.15f), rot);
        } else {
            carriedCenter = headTop + rotv(V2(0, -(app.icons.iconPx * 0.5f + 2.f * S())), rot);
        }
    }
}

void Pet::emit(int type, V2 p, V2 v, float life, float size) { parts.push_back({type, p, v, life, life, size, randf(-0.3f, 0.3f)}); }

void Pet::updateParticles(float dt) {
    for (auto& p : parts) {
        p.life -= dt;
        p.p += p.v * dt;
        if (p.type == 0) p.p.x += sinf((p.max - p.life) * 6.f + p.rot * 10.f) * 12.f * S() * dt;
        if (p.type == 1) p.p.x += sinf((p.max - p.life) * 3.f) * 10.f * S() * dt;
        if (p.type == 6) p.p.x += sinf((p.max - p.life) * 9.f + p.rot * 10.f) * 14.f * S() * dt;
        if (p.type == 4 || p.type == 6) { p.v *= expf(-(p.type == 4 ? 5.f : 2.2f) * dt); }
        else p.v *= expf(-0.6f * dt);
    }
    parts.erase(std::remove_if(parts.begin(), parts.end(), [](const Particle& p) { return p.life <= 0; }), parts.end());
}

D2D1_RECT_F Pet::bounds(V2 wo) const {
    if (st == St::Inside && (nest == NestWeb || nest == NestNone)) {
        V2 c = com() - wo;
        return D2D1::RectF(c.x, c.y, c.x + 1, c.y + 1);
    }
    D2D1_RECT_F b{1e9f, 1e9f, -1e9f, -1e9f};
    auto add = [&](V2 p, float r) {
        b.left = std::min(b.left, p.x - r);
        b.top = std::min(b.top, p.y - r);
        b.right = std::max(b.right, p.x + r);
        b.bottom = std::max(b.bottom, p.y + r);
    };
    const Rig& R = curRig();
    const RigInstance& I = curInst();
    for (size_t i = 0; i < R.parts.size() && i < I.world.size(); i++) {
        const PartDef& d = R.parts[i];
        const auto& m = I.world[i];
        add(xf(m, d.pos), 0);
        add(xf(m, d.pos + V2(d.size.x, 0)), 0);
        add(xf(m, d.pos + V2(0, d.size.y)), 0);
        add(xf(m, d.pos + d.size), 0);
    }
    for (auto& p : I.tp) add(p - wo, R.tail.w0 * 2.f + R.outlineW * 2.f);
    for (auto& p : parts) add(p.p - wo, 24.f * S());
    if (holding()) add(carriedCenter - wo, app.icons.iconPx * 0.75f);
    if (heldShown()) {
        D2D1_RECT_F bx, im, lb;
        heldRects(bx, im, lb);
        float lw = std::max(heldLayMax * 0.5f, bx.right * 1.8f), lbot = std::max(lb.bottom, bx.bottom + 30.f * S());
        for (V2 k : {V2(-lw, bx.top), V2(lw, bx.top), V2(-lw, lbot), V2(lw, lbot)}) add(carriedCenter - wo + rotv(k, rot), 6.f * S());
    }
    if (bPoint > 0.5f) for (int k = 0; k < 2; k++) add(handScreen(k == 1) - wo, 44.f * S());
    if (st == St::Dizzy) add(headTop - wo, headR + 12.f * S());
    return b;
}

static ComPtr<ID2D1PathGeometry> starGeo(ID2D1Factory* f, V2 c, float r) {
    return makePath(f, [&](ID2D1GeometrySink* k) {
        for (int i = 0; i < 10; i++) {
            float a = -PI / 2 + i * PI / 5;
            float rr = (i % 2) ? r * 0.45f : r;
            D2D1_POINT_2F p{c.x + cosf(a) * rr, c.y + sinf(a) * rr};
            if (i == 0) k->BeginFigure(p, D2D1_FIGURE_BEGIN_FILLED);
            else k->AddLine(p);
        }
        k->EndFigure(D2D1_FIGURE_END_CLOSED);
    });
}

void Pet::render(DrawCtx& c, V2 wo) {
    if (st == St::Inside && (nest == NestWeb || nest == NestNone)) return;
    if (introHidden) return;
    D2D1_RECT_F clip;
    bool clipped = renderClip(clip, wo);
    if (clipped) c.rt->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
    curInst().draw(c, face, wo);
    if (clipped) c.rt->PopAxisAlignedClip();
    float s = c.s;
    D2D1_COLOR_F ink = D2D1::ColorF(0.10f, 0.07f, 0.14f, 1);

    if (heldShown()) {
        if (ID2D1Bitmap* b = heldImg.get(c)) {
            D2D1_RECT_F box, img, lab;
            heldRects(box, img, lab);
            float sz = box.right - box.left;
            if (!heldLay || heldLaySz != sz) {
                heldLay.Reset();
                heldFmt.Reset();
                heldLaySz = sz;
                heldLayMax = sz * 1.8f;
                c.dw->CreateTextFormat(c.family, c.fonts, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 9.f * s,
                                       L"ru-ru", &heldFmt);
                if (heldFmt) {
                    heldFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    heldFmt->SetWordWrapping(DWRITE_WORD_WRAPPING_EMERGENCY_BREAK);
                    heldFmt->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, 12.f * s, 9.5f * s);
                    auto lines = [&](const std::wstring& txt, ComPtr<IDWriteTextLayout>& out) {
                        out.Reset();
                        c.dw->CreateTextLayout(txt.c_str(), (UINT32)txt.size(), heldFmt.Get(), heldLayMax, 1000.f, &out);
                        DWRITE_TEXT_METRICS m{};
                        if (!out || FAILED(out->GetMetrics(&m))) return 99;
                        return (int)m.lineCount;
                    };
                    std::wstring shown = heldName.empty() ? heldPath.substr(heldPath.find_last_of(L"\\/") + 1) : heldName;
                    std::wstring name;
                    size_t dot = shown.find_last_of(L'.');
                    for (size_t k = 0; k < shown.size(); k++) {
                        if (k == dot && k > 0) name += L'\x200B';
                        name += shown[k];
                        if ((shown[k] == L'_' || shown[k] == L'-') && k + 1 < shown.size()) name += L'\x200B';
                    }
                    auto cut = [&](size_t n) {
                        if (n > 0 && n < name.size() && IS_HIGH_SURROGATE(name[n - 1])) n--;
                        std::wstring t = name.substr(0, n);
                        while (!t.empty() && (t.back() == L' ' || t.back() == L'.' || t.back() == L'\x200B')) t.pop_back();
                        return t + L"…";
                    };
                    std::wstring txt = name;
                    if (lines(name, heldLay) > 2) {
                        name = shown;
                        ComPtr<IDWriteTextLayout> tmp;
                        size_t lo = 0, hi = name.size();
                        while (lo + 1 < hi) {
                            size_t mid = (lo + hi) / 2;
                            if (lines(cut(mid), tmp) <= 2) lo = mid;
                            else hi = mid;
                        }
                        txt = cut(lo);
                        lines(txt, heldLay);
                    }
                    DWRITE_TEXT_METRICS m{};
                    if (heldLay && SUCCEEDED(heldLay->GetMetrics(&m))) {
                        heldLabW = m.width;
                        heldLabH = m.height;
                    }
                    std::wstring l1;
                    DWRITE_LINE_METRICS lm[3]{};
                    UINT32 ln = 0;
                    if (heldLay && SUCCEEDED(heldLay->GetLineMetrics(lm, 3, &ln)) && ln) l1 = txt.substr(0, lm[0].length);
                    std::erase(l1, L'\x200B');
                    D2D1_SIZE_U px = b->GetPixelSize();
                    logf("held look: '%s' bitmap %ux%u (%.3f) drawn %.1fx%.1f (%.3f) in box %.0f, label %u lines %.0fx%.0f of %.0f, line 1 '%s'",
                         narrow(shown).c_str(), px.width, px.height, px.height ? (float)px.width / px.height : 0.f, img.right - img.left,
                         img.bottom - img.top, (img.right - img.left) / std::max(0.01f, img.bottom - img.top), sz, m.lineCount, heldLabW, heldLabH,
                         heldLayMax, narrow(l1).c_str());
                    heldRects(box, img, lab);
                }
            }
            V2 cc = carriedCenter - wo;
            c.rt->SetTransform(M3::Rotation(rot * 180.f / PI + 4.f * sinf(heldT * 1.3f), cc.d2()));
            if (ID2D1Bitmap* hb = heldHalo.get(c)) {
                D2D1_SIZE_U px = b->GetPixelSize();
                float p = px.width ? heldHaloPad * (img.right - img.left) / px.width : 0.f;
                c.rt->DrawBitmap(hb, D2D1::RectF(cc.x + img.left - p, cc.y + img.top - p, cc.x + img.right + p, cc.y + img.bottom + p), 1.f,
                                 D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
            c.rt->DrawBitmap(b, D2D1::RectF(cc.x + img.left, cc.y + img.top, cc.x + img.right, cc.y + img.bottom), 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            if (heldLay) {
                D2D1_POINT_2F o{cc.x - heldLayMax * 0.5f, cc.y + lab.top};
                c.color(D2D1::ColorF(0, 0, 0, 1), 0.3f);
                for (int k = 0; k < 4; k++) {
                    float dx = ((k & 1) ? 0.9f : -0.9f) * s, dy = ((k & 2) ? 0.9f : -0.9f) * s;
                    c.rt->DrawTextLayout(D2D1::Point2F(o.x + dx, o.y + dy), heldLay.Get(), c.br);
                }
                c.color(D2D1::ColorF(0, 0, 0, 1), 0.6f);
                c.rt->DrawTextLayout(D2D1::Point2F(o.x + 0.8f * s, o.y + 1.2f * s), heldLay.Get(), c.br);
                c.color(D2D1::ColorF(1, 1, 1, 1));
                c.rt->DrawTextLayout(o, heldLay.Get(), c.br);
            }
            c.rt->SetTransform(M3::Identity());
        }
    }
    if (deskFlyOn && !heldPath.empty()) {
        if (ID2D1Bitmap* b = heldImg.get(c)) {
            float sz = (float)app.icons.iconPx;
            D2D1_SIZE_U px = b->GetPixelSize();
            float k = px.width && px.height ? std::min(sz / px.width, sz / px.height) : 1.f;
            float hw = px.width ? px.width * k * 0.5f : sz * 0.5f, hh = px.height ? px.height * k * 0.5f : sz * 0.5f;
            V2 p = deskFly - wo;
            c.rt->DrawBitmap(b, D2D1::RectF(p.x - hw, p.y - hh, p.x + hw, p.y + hh), 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
    }
    if (!carriedName.empty()) {
        if (ID2D1Bitmap* b = carriedImg.get(c)) {
            float sz = (float)app.icons.iconPx;
            D2D1_SIZE_U px = b->GetPixelSize();
            float k = px.width && px.height ? std::min(sz / px.width, sz / px.height) : 1.f;
            float hw = px.width ? px.width * k * 0.5f : sz * 0.5f, hh = px.height ? px.height * k * 0.5f : sz * 0.5f;
            V2 p = carriedCenter - wo;
            c.rt->SetTransform(M3::Rotation(rot * 180.f / PI, p.d2()));
            c.rt->DrawBitmap(b, D2D1::RectF(p.x - hw, p.y - hh, p.x + hw, p.y + hh), 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            c.rt->SetTransform(M3::Identity());
        }
    }

    extDraw(c, wo);

    if (st == St::Dizzy) {
        for (int k = 0; k < 3; k++) {
            float a = t * 4.f + k * 2.f * PI / 3.f;
            V2 p = headTop - wo + V2(cosf(a) * headR * 0.9f, sinf(a) * headR * 0.22f + 2.f * s);
            auto g = starGeo(c.f, p, 6.5f * s);
            c.color(ink);
            c.rt->DrawGeometry(g.Get(), c.br, 2.2f * s, c.round);
            c.color(D2D1::ColorF(1.f, 0.85f, 0.3f, 1));
            c.rt->FillGeometry(g.Get(), c.br);
        }
    }

    if (!pfmt || pfmtScale != s) {
        pfmt.Reset();
        c.dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BLACK, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 18.f * s,
                               L"ru-ru", &pfmt);
        pfmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        pfmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        pfmtScale = s;
        c.dw->CreateTextLayout(L"Z", 1, pfmt.Get(), 40 * s, 40 * s, &playZ);
        c.dw->CreateTextLayout(L"z", 1, pfmt.Get(), 40 * s, 40 * s, &playz);
        c.dw->CreateTextLayout(L"!", 1, pfmt.Get(), 40 * s, 40 * s, &playEx);
        c.dw->CreateTextLayout(L"АМ!", 3, pfmt.Get(), 90 * s, 40 * s, &playAm);
    }
    for (auto& p : parts) {
        V2 q = p.p - wo;
        float age = p.max - p.life;
        float a = clampf(p.life / 0.4f, 0, 1) * clampf(age / 0.12f, 0, 1);
        switch (p.type) {
        case 0: {
            auto g = heartGeometry(c.f, q, 15.f * s * p.size);
            c.color(ink, a);
            c.rt->DrawGeometry(g.Get(), c.br, 2.f * s, c.round);
            c.color(D2D1::ColorF(1.f, 0.42f, 0.78f, 1), a);
            c.rt->FillGeometry(g.Get(), c.br);
            break;
        }
        case 1:
        case 2: {
            IDWriteTextLayout* lay = p.type == 1 ? (p.size > 0.9f && age > 1.f ? playZ.Get() : playz.Get()) : playEx.Get();
            float sc = p.type == 1 ? (0.7f + 0.5f * clampf(age / p.max, 0, 1)) : 1.2f;
            c.rt->SetTransform(M3::Scale(sc, sc, q.d2()) * M3::Rotation(p.rot * 30.f, q.d2()));
            D2D1_POINT_2F o{q.x - 20 * s, q.y - 20 * s};
            c.color(ink, a);
            for (int k = 0; k < 2; k++) {
                float d = (k ? 1.5f : -1.5f) * s;
                c.rt->DrawTextLayout(D2D1::Point2F(o.x + d, o.y + d * 0.6f), lay, c.br);
            }
            c.color(p.type == 1 ? D2D1::ColorF(0.86f, 0.74f, 1.f, 1) : D2D1::ColorF(1.f, 0.83f, 0.3f, 1), a);
            c.rt->DrawTextLayout(o, lay, c.br);
            c.rt->SetTransform(M3::Identity());
            break;
        }
        case 3: {
            float sc = 1.f + 0.4f * clampf(1.f - age / 0.15f, 0.f, 1.f);
            c.rt->SetTransform(M3::Scale(sc, sc, q.d2()) * M3::Rotation(-8.f, q.d2()));
            D2D1_POINT_2F o{q.x - 45 * s, q.y - 20 * s};
            c.color(ink, a);
            for (int k = 0; k < 4; k++) {
                float dx = ((k & 1) ? 1.8f : -1.8f) * s, dy = ((k & 2) ? 1.8f : -1.8f) * s;
                c.rt->DrawTextLayout(D2D1::Point2F(o.x + dx, o.y + dy), playAm.Get(), c.br);
            }
            c.color(D2D1::ColorF(1.f, 0.55f, 0.85f, 1), a);
            c.rt->DrawTextLayout(o, playAm.Get(), c.br);
            c.rt->SetTransform(M3::Identity());
            break;
        }
        case 4: {
            float r = (3.f + 5.f * (age / p.max)) * s * p.size;
            c.color(D2D1::ColorF(0.85f, 0.85f, 0.9f, 1), 0.6f * a);
            c.rt->FillEllipse(D2D1::Ellipse(q.d2(), r, r * 0.8f), c.br);
            break;
        }
        case 5: {
            float u = clampf(age / 0.18f, 0.f, 1.f);
            float sc = (0.5f + 0.7f * u - 0.2f * smooth01((age - 0.18f) / 0.2f)) * p.size;
            c.rt->SetTransform(M3::Rotation(p.rot * 40.f, q.d2()));
            drawLips(c, q, 10.f * s * sc, clampf(p.life / 0.5f, 0, 1));
            c.rt->SetTransform(M3::Identity());
            break;
        }
        case 6: {
            float k = age / p.max;
            float r = (3.f + 5.f * k) * s * p.size;
            c.color(D2D1::ColorF(0.62f, 0.58f, 0.72f, 1), 0.8f * a * (1.f - 0.5f * k));
            c.rt->DrawEllipse(D2D1::Ellipse(q.d2(), r, r * 0.85f), c.br, 1.6f * s);
            c.color(D2D1::ColorF(1.f, 1.f, 1.f, 1), 0.9f * a * (1.f - 0.5f * k));
            c.rt->FillEllipse(D2D1::Ellipse(q.d2(), r, r * 0.85f), c.br);
            break;
        }
        }
    }
}
