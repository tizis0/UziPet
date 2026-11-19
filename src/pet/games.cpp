#include "games.h"

struct TagGame : MiniGame {
    static constexpr float kLen = 20.f, kUnfair = 4.f, kForfeit = 8.f;
    static constexpr float kRun = 3.f;
    static constexpr float kJumpUp = 340.f;
    int phase = 0;
    float t = 0, countT = 0, outT = 0, landT = 0, groundY = 0;
    bool unfairSaid = false;
    int jumps = 0;

    const wchar_t* title() const override { return L"Догонялки"; }
    const char* id() const override { return "tag"; }

    void start(Pet& p) override {
        phase = 0;
        t = outT = landT = 0;
        unfairSaid = false;
        jumps = 0;
        groundY = p.pos.y;
        p.runK = 1.f;
        if (p.grounded() && p.st != St::Idle) p.setState(St::Idle, 1e9f);
        else if (p.st == St::Idle) p.stDur = 1e9f;
        if (fabsf(p.lastCursor.x - p.pos.x) > 6.f) p.facing = p.lastCursor.x > p.pos.x ? 1 : -1;
        p.app.bubble.close();
        p.sayCat("gameStart", 1.f, L"", true);
        countT = clampf(p.app.bubble.typeDur + 0.15f, 1.2f, 3.5f);
        p.squashV -= 1.2f;
    }

    bool reachable(const Pet& p, V2 cur) const {
        float s = p.S();
        float top = groundY - (p.standTop > 1.f ? p.standTop : p.comH * 2.1f) - kJumpUp * s - p.headR * 0.5f;
        if (cur.y < top) return false;
        if (p.gnd.valid && p.gnd.kind == SurfKind::Floor && (cur.x < p.gnd.x0 - 70.f * s || cur.x > p.gnd.x1 + 70.f * s)) return false;
        if (p.gnd.valid && p.gnd.kind == SurfKind::Floor && cur.y >= monBottom(p)) return false;
        return true;
    }

    static float monBottom(const Pet& p) {
        const auto& M = p.app.world.mons;
        int i = p.gnd.index;
        return i >= 0 && i < (int)M.size() ? (float)M[i].rc.bottom : p.pos.y + 80.f * p.S();
    }

    static float rowLow(const Pet& p) { return std::max(60.f * p.S(), p.app.icons.spacing.y * 1.1f); }

    bool caught(const Pet& p, V2 cur) const {
        float r = p.headR;
        if ((cur - p.headC).len() < r * 1.25f) return true;
        if (p.active == 0 && ((cur - p.handScreen(false)).len() < r * 0.7f || (cur - p.handScreen(true)).len() < r * 0.7f)) return true;
        bool floor = p.gnd.valid && p.gnd.kind == SurfKind::Floor && p.grounded() && cur.y < monBottom(p);
        float under = p.gnd.valid && p.gnd.kind == SurfKind::Icon && p.grounded() ? std::max((float)p.app.icons.iconPx, rowLow(p)) : 4.f * p.S();
        return fabsf(cur.x - p.pos.x) < r * 0.8f && cur.y > p.headC.y && (cur.y < p.pos.y + under || floor);
    }

    bool leap(Pet& p, V2 cur, V2 cv) {
        float s = p.S(), g = p.G();
        float reachH = p.standTop > 1.f ? p.standTop * 0.95f : p.comH * 2.f;
        float up = clampf(p.pos.y - (cur.y + reachH), 30.f * s, kJumpUp * s);
        float vy = -sqrtf(2 * g * up), T = -vy / g;
        float aim = cur.x + clampf(cv.x, -600.f * s, 600.f * s) * T * 0.5f;
        float vx = clampf((aim - p.pos.x) / T, -720.f * s, 720.f * s);
        V2 v(vx, vy);
        if (!p.trajectoryOk(p.pos, v, T)) {
            v.x *= 0.4f;
            if (!p.trajectoryOk(p.pos, v, T)) return false;
        }
        if (fabsf(v.x) > 4.f) p.facing = v.x > 0 ? 1 : -1;
        p.vel = v;
        p.gnd.valid = false;
        p.walkOff = false;
        p.squashV += 2.5f;
        p.setState(St::Leap);
        jumps++;
        return true;
    }

    void finish(Pet& p, int res, const char* cat) {
        result = res;
        if (res) p.dayCount(res > 0 ? "win" : "lose");
        p.runK = 1.f;
        if (p.st == St::Leap) p.setState(St::Air);
        if (!p.app.bubble.asking() && !p.app.bubble.held()) p.app.bubble.close();
        p.sayCat(cat, 1.f, L"", true);
        if (res > 0) {
            p.tHappy = 2.5f;
            if (p.tier() == 3) p.tBlush = 1.5f;
            if (p.grounded()) p.setState(St::Clap, 1.8f);
            else p.clapOnLand = true;
            p.cornerPhoto(L"я победила");
        } else {
            p.tSurprise = 0.4f;
            if (p.tier() == 0) p.tAngry = 1.2f;
            if (p.grounded()) p.goIdle(1.5f, 3.f);
        }
        logf("game tag: %s at %.2f s ('%s'), jumps %d, rel %.1f, state %s", cat, t, narrow(p.app.bubble.text).c_str(), jumps, p.vit.rel, p.stateName());
    }

    bool update(Pet& p, float dt, const Input& in) override {
        float s = p.S();
        V2 cur = in.cursor;
        t += dt;
        bool standing = p.grounded() && (p.st == St::Idle || p.st == St::Walk);
        if (phase == 0) {
            if (standing && fabsf(cur.x - p.pos.x) > 6.f) p.facing = cur.x > p.pos.x ? 1 : -1;
            if (t >= countT) {
                phase = 1;
                t = 0;
                p.runK = kRun;
                logf("game tag: go (count %.2f s)", countT);
            }
            return true;
        }
        if (caught(p, cur)) {
            finish(p, 1, "gameWin");
            return false;
        }
        if (t >= kLen) {
            finish(p, -1, "gameLose");
            return false;
        }
        if (p.grounded()) groundY = p.pos.y;
        bool reach = reachable(p, cur);
        outT = reach ? 0.f : outT + dt;
        if (reach) unfairSaid = false;
        if (!reach && outT > kUnfair && !unfairSaid) {
            unfairSaid = true;
            p.tAngry = 1.6f;
            p.squashV -= 1.5f;
            if (!p.app.bubble.asking()) p.app.bubble.close();
            p.sayCat(cur.y < p.headTop.y ? "gameUnfair" : "gameUnfairAway", 1.f, L"", true);
            logf("game tag: unfair at %.2f s (cursor %.0f,%.0f)", t, cur.x, cur.y);
        }
        if (!reach && outT > kForfeit) {
            finish(p, 1, cur.y < p.headTop.y ? "gameWinUnfair" : "gameWinAway");
            return false;
        }
        if (!standing) {
            landT = 0;
            return true;
        }
        landT += dt;
        bool above = cur.y < p.headTop.y - 6.f * s;
        if (above && reach && fabsf(cur.x - p.pos.x) < 260.f * s && landT > 0.25f && leap(p, cur, p.cursorVel)) return true;
        float tx = clampf(cur.x, p.gnd.x0, p.gnd.x1);
        bool off = false;
        if (p.gnd.kind != SurfKind::Floor) {
            bool outside = cur.x < p.gnd.x0 || cur.x > p.gnd.x1, icons = p.gnd.kind == SurfKind::Icon;
            float low = icons ? rowLow(p) : 30.f * s;
            bool beside = outside && cur.y > (icons ? p.headTop.y : p.pos.y - p.headR);
            if (beside || cur.y > p.pos.y + low) {
                bool right = outside ? cur.x > p.gnd.x1
                                     : (p.gnd.x1 - p.pos.x) + (p.gnd.x1 - cur.x) < (p.pos.x - p.gnd.x0) + (cur.x - p.gnd.x0);
                tx = right ? p.gnd.x1 + 14.f * s : p.gnd.x0 - 14.f * s;
                off = true;
            }
        }
        if (off || fabsf(tx - p.pos.x) > 5.f * s) {
            if (p.st != St::Walk) p.goWalk(tx, off);
            p.walkTarget = tx;
            p.walkOff = off;
        } else {
            if (p.st == St::Walk) p.setState(St::Idle, 1e9f);
            if (fabsf(cur.x - p.pos.x) > 6.f) p.facing = cur.x > p.pos.x ? 1 : -1;
        }
        return true;
    }

    void cancel(Pet& p) override {
        p.runK = 1.f;
        if (p.st == St::Leap) p.setState(St::Air);
        if (p.st == St::Walk && p.grounded()) p.goIdle(1.f, 2.f);
        else if (p.st == St::Idle && p.stDur > 1e6f) p.goIdle(1.f, 2.f);
    }

    void face(Pet& p, FaceState& tg, V2&) override {
        if (phase == 0) {
            if (p.tier() > 0) { tg.happy = 1; tg.squint = 0; tg.angry = 0; }
            return;
        }
        tg.happy = tg.squint = tg.heart = 0;
        if (outT > kUnfair) { tg.angry = 1; tg.surprise = 0; return; }
        tg.angry = 0.3f;
        tg.surprise = p.st == St::Leap ? 0.5f : 0.f;
    }

    void pose(Pet& p) override {
        if (p.st != St::Walk || p.runK <= 1.f || p.active != 0) return;
        auto& P = p.curInst().pose;
        auto R = [&](int i, auto f) {
            if (i >= 0 && i < (int)P.size()) f(P[i]);
        };
        float k = p.bWalk, ws = sinf(p.walkPhase), wc = cosf(p.walkPhase), s = p.S();
        R(p.iLegL, [&](PartPose& q) { q.rot += 0.07f * ws * k; q.off.y -= std::max(0.f, -wc) * 5.f * s * k; });
        R(p.iLegR, [&](PartPose& q) { q.rot -= 0.07f * ws * k; q.off.y -= std::max(0.f, wc) * 5.f * s * k; });
        R(p.iShinL, [&](PartPose& q) { q.rot += 0.3f * std::max(0.f, -wc) * k; });
        R(p.iShinR, [&](PartPose& q) { q.rot -= 0.3f * std::max(0.f, wc) * k; });
        R(p.iArmL, [&](PartPose& q) { q.rot += (-0.14f * ws + 0.08f) * k; });
        R(p.iArmR, [&](PartPose& q) { q.rot += (-0.14f * ws - 0.08f) * k; });
        R(p.iFArmL, [&](PartPose& q) { q.rot -= (0.22f + 0.18f * ws) * k; });
        R(p.iFArmR, [&](PartPose& q) { q.rot += (0.22f - 0.18f * ws) * k; });
        R(p.iBody, [&](PartPose& q) { q.rot += 0.05f * k; });
        p.rootBob -= fabsf(ws) * 2.f * s * k;
    }
};

const std::vector<MiniGame*>& gameList() {
    static TagGame tag;
    static const std::vector<MiniGame*> all = {&tag};
    return all;
}

const char* Pet::gameRefuse() {
    if (weak()) return vit.stock > 0 ? "hungry" : "hungryEmpty";
    if (vit.energy < 20.f) return "gameTired";
    return nullptr;
}

void Pet::gameMenu() {
    if (baby() || vit.fatal || vit.dead || st == St::Sleep || game) return;
    if (tier() == 0 && chance(0.75f)) {
        tAngry = 1.4f;
        if (fabsf(lastCursor.x - pos.x) > 6.f) facing = lastCursor.x > pos.x ? -1 : 1;
        sayCat("gameNo", 1.f, L"", true);
        return;
    }
    if (const char* no = gameRefuse()) {
        if (!weak()) tSad = 1.2f;
        sayCat(no, 1.f, L"", true);
        logf("game: refused (%s)", no);
        return;
    }
    std::vector<std::wstring> ch;
    for (auto g : gameList()) ch.push_back(g->title());
    for (auto& c : app.phrases.list("choiceGames")) ch.push_back(c);
    std::wstring s = app.phrases.pick("gameAsk", L"");
    if (s.empty() || app.bubble.asking()) return;
    if (tier() > 0) tHappy = 1.2f;
    app.bubble.ask(s, ch, "games");
    lastSay = app.now;
}

bool Pet::gameStart(int i) {
    const auto& L = gameList();
    if (i < 0 || i >= (int)L.size() || game || baby() || vit.fatal || vit.dead) return false;
    if (!grounded() || dragging || st == St::Sleep || st == St::Hide || nest != NestNone || webActive()) return false;
    if (const char* no = gameRefuse()) {
        sayCat(no, 1.f, L"", true);
        logf("game: refused (%s)", no);
        return false;
    }
    quietUntil = 0;
    strollOn = false;
    strollUntil = 0;
    followT = 0;
    vengeT = 0;
    lingerT = 0;
    afkGo = 0;
    feeding = false;
    gamePokes = 0;
    gameAgainT = -1;
    lastGame = i;
    double t0 = nowSec();
    game = L[i];
    game->result = 0;
    game->start(*this);
    logf("game %s: start in %.2f ms (rel %.1f, energy %.1f, food %.1f, state %s, on %d)", game->id(), (nowSec() - t0) * 1000.0, vit.rel, vit.energy,
         vit.food, stateName(), gnd.valid ? (int)gnd.kind : -1);
    return true;
}

void Pet::gameStop(const char* why) {
    gameAgainT = -1;
    if (!game) return;
    MiniGame* g = game;
    game = nullptr;
    g->cancel(*this);
    runK = 1.f;
    logf("game %s: cancelled (%s), state %s rot %.3f", g->id(), why, stateName(), rot);
}

void Pet::gameStep(float dt, const Input& in) {
    if (gameAgainT > 0 && (gameAgainT -= dt) <= 0) {
        gameAgainT = -1;
        if ((app.bubble.held() || !pendingSay.empty()) && app.now < gameAgainBy) gameAgainT = 0.25f;
        else if (!game && grounded() && !dragging && st != St::Sleep && !vit.fatal && !app.bubble.asking() && !app.bubble.held())
            ask("gameAgain", "choiceGameAgain", "gameAgain");
    }
    if (!game) return;
    const char* why = nullptr;
    bool ok = st == St::Idle || st == St::Walk || st == St::PrepJump || st == St::Air || st == St::Leap || st == St::Land || st == St::Fall;
    if (vit.fatal || vit.dead) why = "fatal";
    else if (dragging) why = "drag";
    else if (!ok) why = stateName();
    else if (app.bubble.asking() || feeding) why = "busy";
    if (why) {
        gameStop(why);
        return;
    }
    if (game->update(*this, dt, in)) return;
    MiniGame* g = game;
    game = nullptr;
    runK = 1.f;
    if (tier() >= 1 && app.now - lastGameGain > 600) {
        lastGameGain = app.now;
        addRel(1.f, "played a game");
    }
    gameAgainT = app.bubble.typeDur + 1.4f;
    gameAgainBy = app.now + 15.0;
    logf("game %s: over, %s, state %s rot %.3f", g->id(), g->result > 0 ? "she won" : "you won", stateName(), rot);
}
