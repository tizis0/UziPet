
    {
        if (!a.web) a.web = new WebNest();
        WebNest& W = *a.web;
        a.world.setFake(RECT{0, 0, 1920, 1080}, RECT{0, 0, 1920, 1020});
        const RECT rc{760, 90, 1920, 1020};
        const float iw = 1160, ih = 930, sh = 5200, sbw = 17;
        struct Blk { int id, type; float x, y, w, h; };
        std::vector<Blk> page;
        for (int i = 0; i < 26; i++) page.push_back({i + 1, i % 3 == 1 ? 2 : 1, 60.f + (i % 4) * 140.f, 260.f + i * 190.f, 520.f - (i % 4) * 60.f, 48.f});
        float sy = 0, syTo = 0;
        int since = 0;
        bool vis = true;
        int pageShame = 0;
        auto post = [&]() {
            WebState s;
            s.visible = s.focused = vis;
            s.shame = pageShame;
            s.iw = iw;
            s.ih = ih;
            s.dpr = 1;
            s.sy = sy;
            s.sh = sh;
            s.cw = iw - sbw;
            s.mode = "wiki";
            s.title = L"Узи · тест";
            s.topic = L"рельсотрон";
            for (auto& b : page) {
                float vy = b.y - sy;
                if (vy + b.h < 0 || vy > ih) continue;
                s.els.push_back({b.id, b.type, b.x, vy, b.w, b.h});
            }
            W.testState(s);
        };
        float pollT = 0;
        auto drive = [&](float secs, std::function<void()> each = nullptr) {
            for (float tt = 0; tt < secs; tt += 0.05f) {
                pollT += 0.05f;
                if (pollT >= 0.15f) {
                    pollT = 0;
                    for (auto& c : W.testCmds(since)) {
                        size_t q = c.find("\"dy\":");
                        if (c.find("\"scroll\"") != std::string::npos && q != std::string::npos)
                            syTo = clampf(syTo + (float)atof(c.c_str() + q + 5), 0.f, sh - ih);
                    }
                }
                if (fabsf(syTo - sy) > 0.5f) {
                    sy += clampf(syTo - sy, -60.f, 60.f);
                    post();
                }
                sim(0.05f, still);
                if (p.st == St::Idle && p.tugT < 0) p.stT = 0;
                if (each) each();
            }
        };
        g_snapUnder = [&](DrawCtx& c, V2 wo) {
            const RECT& r = p.webFakeRc;
            auto box = [&](float x0, float y0, float x1, float y1, D2D1::ColorF col) {
                c.color(col);
                c.rt->FillRectangle(D2D1::RectF(x0 - wo.x, y0 - wo.y, x1 - wo.x, y1 - wo.y), c.br);
            };
            box((float)r.left, (float)r.top, (float)r.right, (float)r.bottom, D2D1::ColorF(1, 1, 1, 1));
            for (auto& b : page) {
                float y = r.top + b.y - sy;
                if (y + b.h < r.top || y > r.bottom) continue;
                box(r.left + b.x, std::max((float)r.top, y), r.left + b.x + b.w, std::min((float)r.bottom, y + b.h),
                    b.type == 2 ? D2D1::ColorF(0.75f, 0.8f, 0.9f, 1) : D2D1::ColorF(0.82f, 0.82f, 0.85f, 1));
            }
            float th = std::max(24.f, ih * ih / sh), ty = r.top + (ih - th) * clampf(sy / (sh - ih), 0.f, 1.f);
            box(r.right - sbw, (float)r.top, (float)r.right, (float)r.bottom, D2D1::ColorF(0.93f, 0.93f, 0.94f, 1));
            box(r.right - sbw + 3, ty, r.right - 3.f, ty + th, D2D1::ColorF(0.55f, 0.55f, 0.6f, 1));
            if (r.right > 1920) box(1920, (float)r.top - 40, 1924, (float)r.bottom + 40, D2D1::ColorF(1, 0, 0, 1));
        };
        reset(St::Idle, 100);
        p.dropHeld("test");
        bool stroll = true;
        auto enter = [&]() {
            p.webStroll = stroll;
            p.webFake = true;
            p.webFakeRc = rc;
            p.webOn = true;
            p.nest = Pet::NestWeb;
            p.webSince = a.now;
            p.webLife = 1e9;
            p.webExt = false;
            p.webAutoT = 1e9f;
            p.webFirst = false;
            p.webShown = false;
            p.webVer = -1;
            p.webSy = -1;
            p.webAnchor = -2;
            p.webAloneAll = p.webHidT = 0;
            p.webPeeked = p.webSawShy = false;
            p.pos = V2(1200, 1020);
            p.setState(St::Inside);
            a.bubble.close();
            post();
        };
        enter();
        int fell0 = p.fellOuts;
        float rel0 = p.vit.rel;
        drive(2.5f);
        bool onPage = a.world.web && p.grounded() && p.gnd.kind == SurfKind::Web;
        auto bodyBox = [&]() {
            D2D1_RECT_F b{1e9f, 1e9f, -1e9f, -1e9f};
            V2 wo = p.windowOrigin();
            for (size_t i = 0; i < a.rig.parts.size() && i < a.inst.world.size(); i++) {
                const std::string& n = a.rig.parts[i].name;
                if (n.rfind("arm", 0) == 0 || n.rfind("forearm", 0) == 0) continue;
                const PartDef& d = a.rig.parts[i];
                for (V2 k : {d.pos, d.pos + V2(d.size.x, 0), d.pos + V2(0, d.size.y), d.pos + d.size}) {
                    V2 q = xf(a.inst.world[i], k) + wo;
                    b.left = std::min(b.left, q.x);
                    b.right = std::max(b.right, q.x);
                    b.top = std::min(b.top, q.y);
                    b.bottom = std::max(b.bottom, q.y);
                }
            }
            return b;
        };
        float barL = rc.right - sbw;
        int rides = 0, ridesOk = 0;
        auto ride = [&](const char* tag, bool snap) {
            bool started = p.webStartRide();
            D2D1_RECT_F bb{};
            V2 hand, feet;
            bool hung = false;
            drive(0.45f + 0.35f);
            if (p.st == St::Ride) {
                hung = true;
                bb = bodyBox();
                hand = p.handScreen(true).x > p.handScreen(false).x ? p.handScreen(true) : p.handScreen(false);
                feet = p.pos;
                if (snap) shot(L"83b_web_scrollbar");
            }
            float sy0 = sy;
            drive(3.f);
            bool landed = p.grounded() && a.world.web && p.pos.y <= rc.bottom + 0.5f && p.pos.x >= rc.left && p.pos.x <= rc.right;
            bool ok = started && hung && bb.right <= barL + 1.f && bb.left >= rc.left && fabsf(hand.x - (barL + sbw * 0.5f)) < 10.f &&
                      feet.y <= rc.bottom + 0.5f && landed && sy > sy0 - 0.5f;
            rides++;
            ridesOk += ok;
            logf("web nest %s: ok %d: started %d hung %d, body x %.0f..%.0f (page %ld..%ld, scrollbar %.0f..%ld), hand %.0f,%.0f, feet y %.0f (page bottom "
                 "%ld), scrolled %.0f -> %.0f, then %s on %d at %.0f,%.0f",
                 tag, (int)ok, (int)started, (int)hung, bb.left, bb.right, rc.left, rc.right, barL, rc.right, hand.x, hand.y, feet.y, rc.bottom, sy0, sy,
                 p.stateName(), p.grounded() ? (int)p.gnd.kind : -1, p.pos.x, p.pos.y);
        };
        ride("ride top", true);
        auto surfAt = [&](V2 tgt) {
            for (auto& s : a.world.surfs)
                if (s.kind == SurfKind::Web && fabsf(s.y - tgt.y) < 1.f && tgt.x >= s.x0 - 1 && tgt.x <= s.x1 + 1) return true;
            return false;
        };
        auto pickWeb = [&](V2& tgt) {
            for (auto& s : a.world.surfs)
                if (s.kind == SurfKind::Web && s.index >= 0 && !p.onSurf(s) && s.y > rc.top + 200 && fabsf(s.y - p.pos.y) < 300) {
                    tgt = V2((s.x0 + s.x1) * 0.5f, s.y);
                    return true;
                }
            return false;
        };
        drive(1.f);
        p.goIdle(30.f, 30.f);
        V2 tgt;
        bool havePick = pickWeb(tgt);
        p.webScroll(400);
        bool refusedMoving = havePick && !p.tryJump(tgt, 40);
        drive(1.5f);
        p.goIdle(30.f, 30.f);
        bool havePick2 = pickWeb(tgt);
        bool fresh = havePick2 && surfAt(tgt);
        bool jumped = fresh && p.tryJump(tgt, 40);
        sy = syTo = clampf(sy + 300.f, 0.f, sh - ih);
        post();
        sim(0.05f, still);
        bool cancelled = jumped && p.st != St::PrepJump && p.st != St::Air;
        drive(1.5f);
        logf("web nest stale jump: ok %d: refused while the page moves %d, after it settled the target is on a fresh block %d, jumped %d, the page "
             "scrolled under the planned jump -> cancelled %d (state %s)",
             (int)(refusedMoving && fresh && jumped && cancelled), (int)refusedMoving, (int)fresh, (int)jumped, (int)cancelled, p.stateName());
        sy = syTo = sh - ih - 60.f;
        post();
        drive(1.5f);
        p.goIdle(30.f, 30.f);
        ride("ride bottom", false);
        drive(1.f);
        bool staleOk = false;
        {
            const Surf* on = nullptr;
            for (auto& s : a.world.surfs)
                if (s.kind == SurfKind::Web && s.index >= 0 && s.y > rc.top + 300 && s.y < rc.bottom - 200 && !on) on = &s;
            int gone = on ? on->index : -1;
            if (on) {
                p.pos = V2((on->x0 + on->x1) * 0.5f, on->y);
                p.setGround(*on);
                p.goIdle(30.f, 30.f);
                drive(0.3f);
            }
            if (gone >= 0) {
                for (auto& b : page)
                    if (b.id == gone) b.y = -9999;
                post();
                drive(2.f);
                staleOk = p.grounded() && p.pos.y <= rc.bottom + 0.5f && !(p.gnd.kind == SurfKind::Web && p.gnd.index == gone);
            } else {
                staleOk = p.grounded() && p.pos.y <= rc.bottom + 0.5f;
            }
            logf("web nest stale block: ok %d: stood on block %d, it vanished, now %s on %d at %.0f,%.0f", (int)staleOk, gone, p.stateName(),
                 p.grounded() ? (int)p.gnd.kind : -1, p.pos.x, p.pos.y);
        }
        logf("web nest summary: rides %d ok %d, on the page at start %d, fell out %d times, rel %.1f -> %.1f", rides, ridesOk, (int)onPage,
             p.fellOuts - fell0, rel0, p.vit.rel);

        {
            bool shyWas = a.settings.shyPages;
            a.settings.shyPages = true;
            p.vit.trust = 10;
            float r10 = p.webShyRate(4000);
            p.vit.trust = 90;
            float r90 = p.webShyRate(4000);
            a.settings.shyPages = false;
            float rOff = p.webShyRate(2000);
            a.settings.shyPages = true;
            bool rateOk = r90 > r10 + 0.15f && rOff == 0.f && r10 > 0.f;
            p.vit.trust = 40;
            float t0 = p.vit.trust;
            vis = false;
            pageShame = 2;
            enter();
            drive(40.f);
            bool hidden1 = !p.webShown && p.webSawShy && !p.webPeeked;
            p.leaveWeb(0);
            float t1 = p.vit.trust;
            bool sched = p.webBlurtAt > 0;
            if (!sched) p.webBlurtAt = a.now;
            p.webBlurtAt = a.now + 1.0;
            a.bubble.close();
            bool blurted = false;
            std::string blurt;
            for (int k = 0; k < 600 && !blurted; k++) {
                sim(0.05f, still);
                if (p.st == St::Idle && p.stT > 2.f) p.stT = 0;
                blurted = p.webBlurtAt < 0 && a.bubble.active;
            }
            sim(1.5f, still);
            blurt = narrow(a.bubble.text);
            a.bubble.close();
            vis = false;
            pageShame = 0;
            enter();
            drive(20.f);
            vis = true;
            post();
            drive(0.5f);
            float t2 = p.vit.trust;
            bool caught = p.webPeeked && p.webShown && a.bubble.active;
            drive(1.5f);
            std::string peekSaid = narrow(a.bubble.text);
            a.bubble.close();
            vis = false;
            pageShame = 2;
            enter();
            drive(20.f);
            vis = true;
            post();
            drive(0.3f);
            float t3 = p.vit.trust;
            bool panic = !p.webOn && a.bubble.active;
            drive(1.f);
            std::string panicSaid = narrow(a.bubble.text);
            a.bubble.close();
            pageShame = 0;
            vis = true;
            enter();
            drive(1.f);
            float t4 = p.vit.trust;
            bool noPeek = p.webShown && !p.webPeeked && t4 == t3;
            a.bubble.close();
            vis = false;
            enter();
            drive(20.f);
            vis = true;
            post();
            drive(2.f);
            float t5a = p.vit.trust;
            a.bubble.close();
            vis = false;
            post();
            drive(20.f);
            vis = true;
            post();
            drive(1.f);
            float t5 = p.vit.trust;
            bool twiceOk = t5a == t4 - 12.f && t5 == t5a && !a.bubble.active;
            p.leaveWeb(0);
            a.bubble.close();
            stroll = false;
            vis = false;
            enter();
            drive(35.f);
            vis = true;
            post();
            drive(1.f);
            bool sentQuiet = p.webShown && p.webPeeked && p.vit.trust == t5 && !a.bubble.active;
            drive(15.f);
            p.leaveWeb(0);
            float t6 = p.vit.trust;
            bool sentOk = sentQuiet && t6 == t5;
            stroll = true;
            a.bubble.close();
            vis = true;
            enter();
            drive(1.f);
            bool ok = rateOk && hidden1 && t1 == t0 + 5.f && blurted && caught && t2 == t1 - 12.f && panic && t3 == t2 - 20.f && noPeek && twiceOk && sentOk;
            logf("web trust: ok %d: shy chance at trust 10 %.3f, at 90 %.3f, setting off %.3f; alone 40 s on an awkward page -> trust %.0f -> %.0f, blurt "
                 "scheduled %d, said %d '%s'; peeked after 20 s -> caught %d, trust %.0f, said '%s'; peeked on an awkward page -> panic %d, trust %.0f, "
                 "said '%s'; own page shown right away -> not a peek %d; looked twice on a walk -> one catch %d (trust %.0f, %.0f); sent there and looked "
                 "in -> quiet, no penalty, no +5 %d (trust %.0f)",
                 (int)ok, r10, r90, rOff, t0, t1, (int)sched, (int)blurted, blurt.c_str(), (int)caught, t2, peekSaid.c_str(), (int)panic, t3,
                 panicSaid.c_str(), (int)noPeek, (int)twiceOk, t5a, t5, (int)sentOk, t6);
            a.settings.shyPages = shyWas;
            p.vit.trust = 30;
            pageShame = 0;
            vis = true;
            a.bubble.close();
        }

        int falls = 0, fallsOk = 0;
        auto backOn = [&](const char* tag, const char* wantWhy, float secs, std::function<bool()> where) {
            int n0 = p.fellOuts;
            float r0 = p.vit.rel;
            double t0 = a.now;
            bool back = false;
            for (int k = 0; k < (int)(secs * 20) && !back; k++) {
                sim(0.05f, still);
                back = p.fellOuts > n0;
            }
            float took = (float)(a.now - t0);
            sim(0.4f, still);
            std::string said = narrow(a.bubble.text);
            bool ok = back && p.fellWhy == wantWhy && p.grounded() && where() && fabsf(p.vit.rel - r0) < 0.01f && a.bubble.active;
            falls++;
            fallsOk += ok;
            logf("fell out %s: ok %d: back %d after %.2f s (%s), %s at %.0f,%.0f, rel %.1f -> %.1f, said '%s'", tag, (int)ok, (int)back, took,
                 p.fellWhy.c_str(), p.stateName(), p.pos.x, p.pos.y, r0, p.vit.rel, said.c_str());
        };
        {
            a.world.mons.push_back({RECT{1920, 0, 3840, 1080}, RECT{1920, 0, 3840, 1020}});
            p.webFakeRc = RECT{1400, 90, 2700, 1020};
            sy = syTo = 0;
            post();
            drive(0.2f);
            a.bubble.close();
            int bottoms = 0;
            float bx0 = 0, bx1 = 0;
            for (auto& s : a.world.surfs)
                if (s.kind == SurfKind::Web && s.index == -1) {
                    bottoms++;
                    bx0 = s.x0;
                    bx1 = s.x1;
                }
            int n0 = p.fellOuts;
            p.pos = V2(2500, 300);
            p.vel = {};
            p.startFallNow();
            bool landed = false;
            for (int k = 0; k < 140; k++) {
                sim(0.05f, still);
                landed |= p.grounded() && p.gnd.kind == SurfKind::Web;
            }
            bool twoOk = bottoms == 1 && bx0 < 1920 && bx1 > 2600 && landed && p.fellOuts == n0;
            logf("fell out two monitors: ok %d: page bottom pieces %d (%.0f..%.0f), landed on the page right of the seam %d, at %.0f,%.0f %s, fell out %d times",
                 (int)twoOk, bottoms, bx0, bx1, (int)landed, p.pos.x, p.pos.y, p.stateName(), p.fellOuts - n0);
            falls++;
            fallsOk += twoOk;
            a.world.mons.pop_back();
        }
        p.webFakeRc = RECT{1400, 90, 2700, 1020};
        sy = syTo = 0;
        post();
        drive(0.2f);
        a.bubble.close();
        p.pos = V2(2300, 300);
        p.vel = {};
        p.startFallNow();
        backOn("off screen page", "off every monitor", 3.f,
               [&] { return p.pos.x <= 1920 - 6 && p.pos.y == (float)p.webFakeRc.bottom && p.gnd.kind == SurfKind::Web; });
        shot(L"83d_fell_out_page");
        g_snapUnder = nullptr;
        p.leaveWeb(0);
        p.webFake = false;
        p.webOn = false;
        a.world.setFake(RECT{0, 0, 1920, 1080}, RECT{0, 0, 1920, 1020});
        reset(St::Idle, 100);
        sim(0.5f, still);
        a.bubble.close();
        p.pos = V2(900, 2600);
        p.vel = V2(0, 300);
        a.winOrigin = p.windowOrigin();
        p.stepRig(1.f / 60.f, a.winOrigin);
        p.startFallNow();
        backOn("below the floor", "below every floor", 2.f, [&] { return p.pos.y == 1020.f && p.gnd.kind == SurfKind::Floor; });
        sim(0.9f, still);
        shot(L"83c_fell_out");
        sim(1.f, still);
        a.bubble.close();
        auto keep = a.world.surfs;
        a.world.surfs.clear();
        p.pos = V2(600, 700);
        p.vel = {};
        p.startFallNow();
        backOn("no floors", "falling too long", 7.f, [&] { return p.pos.y == 1020.f && p.gnd.kind == SurfKind::Floor; });
        a.world.surfs = keep;
        logf("fell out summary: %d cases, %d ok", falls, fallsOk);
        reset(St::Idle, 100);
        a.bubble.close();
    }
