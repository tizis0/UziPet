
    {
        int fails = 0;
        auto bub = [&]() { return narrow(a.bubble.text); };
        auto kissNow = [&](V2 at, const char* tag) {
            a.bubble.close();
            p.lastSay = -100;
            p.kissT = -1;
            bool ok = p.kiss(at);
            logf("kiss test %s: taken %d, mode %d, place %d, rel %.1f, state %s, bubble '%s'", tag, (int)ok, p.kissMode, p.kissWhere, p.vit.rel,
                 p.stateName(), bub().c_str());
            return ok;
        };
        p.vit.rel = 96.f;
        p.lastKissGain = p.lastKissLoss = -1e9;
        reset(St::Idle, 100);
        sim(0.8f, still);
        float s = a.s;
        struct Spot { const char* n; std::function<V2()> at; int want; };
        Spot spots[] = {
            {"cheek", [&] { return p.visorScreen(V2(0.8f, 0.15f)); }, Pet::KissCheek},
            {"forehead", [&] { return p.visorScreen(V2(0.f, -0.7f)); }, Pet::KissForehead},
            {"mouth", [&] { return p.mouthScreen(); }, Pet::KissMouth},
            {"hand", [&] { return p.handScreen(false); }, Pet::KissHand},
            {"head", [&] { return p.headTop + V2(0, p.headR * 0.3f); }, Pet::KissHead},
            {"body", [&] { return p.pos - V2(0, p.comH); }, Pet::KissBody},
            {"boots", [&] { return p.pos - V2(0, 8.f * s); }, Pet::KissLegs},
        };
        for (auto& sp : spots) {
            V2 at = sp.at();
            int got = p.kissPlaceAt(at);
            logf("kiss place %s at %.0f,%.0f (head %.0f,%.0f r %.0f): got %d want %d", sp.n, at.x, at.y, p.headC.x, p.headC.y, p.headR, got, sp.want);
            if (got != sp.want) fails++;
        }
        {
            V2 m = p.mouthScreen(), f = p.visorScreen(V2(0, 0));
            logf("kiss: visor centre %.0f,%.0f, edge %.0f,%.0f, top %.0f,%.0f; mouth %.0f,%.0f; far point kissable %d", f.x, f.y,
                 p.visorScreen(V2(1, 0)).x, p.visorScreen(V2(1, 0)).y, p.visorScreen(V2(0, -1)).x, p.visorScreen(V2(0, -1)).y, m.x, m.y,
                 (int)p.kissable(V2(1250, 820)));
            if (p.kissable(V2(1250, 820))) fails++;
        }
        float rel0 = p.vit.rel;
        for (auto& sp : spots) {
            reset(St::Idle, 100);
            sim(0.8f, still);
            V2 at = sp.at();
            int face0 = p.facing;
            if (!kissNow(at, sp.n)) fails++;
            bool cheek = !strcmp(sp.n, "cheek");
            sim(0.45f, [&](float) { return at; });
            if (cheek) {
                shot(L"79_kiss_cheek");
                logf("kiss cheek mid: blush %.2f surprise %.2f, particles %zu, state %s", p.face.blush, p.face.surprise, p.parts.size(), p.stateName());
                if (p.face.blush < 0.5f) fails++;
            }
            sim(1.15f, [&](float) { return at; });
            if (cheek) shot(L"79b_kiss_turnaway");
            logf("kiss %s after 1.6 s: facing %d -> %d, state %s, blush %.2f, bubble '%s'", sp.n, face0, p.facing, p.stateName(), p.face.blush, bub().c_str());
            if (a.bubble.text.empty()) fails++;
            bool bodyish = sp.want == Pet::KissBody || sp.want == Pet::KissLegs;
            if (bodyish && p.face.blush > 0.45f) fails++;
            if (!bodyish && p.facing != (at.x > p.pos.x ? -1 : 1)) fails++;
            sim(2.5f, [&](float) { return V2(1250, 820); });
        }
        logf("kiss tier 3: rel %.1f -> %.1f (one gain per 10 min)", rel0, p.vit.rel);
        if (fabsf(p.vit.rel - rel0 - 0.5f) > 0.01f) fails++;

        p.vit.rel = 75.f;
        reset(St::Idle, 100);
        sim(0.8f, still);
        {
            V2 at = p.visorScreen(V2(-0.8f, 0.1f));
            float x0 = p.pos.x;
            kissNow(at, "tier2");
            sim(0.5f, [&](float) { return at; });
            shot(L"79f_kiss_shy");
            sim(0.5f, [&](float) { return at; });
            logf("kiss tier 2: moved %.1f px (away from %.0f), blush %.2f, rel %.1f", p.pos.x - x0, at.x, p.face.blush, p.vit.rel);
            if (fabsf(p.pos.x - x0) < 5.f || (p.pos.x - x0) * (at.x - x0) > 0 || p.vit.rel != 75.f || p.face.blush < 0.5f) fails++;
            sim(2.f, still);
        }
        p.vit.rel = 50.f;
        reset(St::Idle, 100);
        sim(0.8f, still);
        {
            V2 at = p.visorScreen(V2(0.8f, 0.1f));
            float x0 = p.pos.x;
            kissNow(at, "tier1");
            std::string seen = p.stateName();
            for (int k = 0; k < 8; k++) {
                sim(0.15f, [&](float) { return at; });
                if (seen.substr(seen.rfind(' ') + 1) != p.stateName()) seen += std::string(" ") + p.stateName();
            }
            logf("kiss tier 1: states%s, moved %.1f px, facing %d, rel %.1f", (" " + seen).c_str(), p.pos.x - x0, p.facing, p.vit.rel);
            if (fabsf(p.pos.x - x0) < 10.f || p.vit.rel != 50.f) fails++;
            sim(2.f, still);
        }
        p.vit.rel = 15.f;
        reset(St::Idle, 100);
        sim(0.8f, still);
        {
            V2 at = p.visorScreen(V2(0.8f, 0.1f));
            kissNow(at, "tier0");
            sim(0.25f, [&](float) { return at; });
            shot(L"79d_kiss_hate");
            sim(0.65f, [&](float) { return at; });
            shot(L"79d2_kiss_hate_arms");
            logf("kiss tier 0: angry %.2f, facing %d (cursor %s), rel %.1f", p.face.angry, p.facing, at.x > p.pos.x ? "right" : "left", p.vit.rel);
            if (p.face.angry < 0.5f || fabsf(p.vit.rel - 14.f) > 0.01f) fails++;
            sim(2.f, [&](float) { return V2(1250, 820); });
            reset(St::Idle, 100);
            sim(0.8f, still);
            kissNow(p.visorScreen(V2(0.8f, 0.1f)), "tier0 again");
            logf("kiss tier 0 again: rel %.1f", p.vit.rel);
            if (fabsf(p.vit.rel - 14.f) > 0.01f) fails++;
            sim(2.f, still);
        }
        p.vit.rel = 96.f;
        reset(St::Sleep, 1000);
        sim(2.5f, still);
        {
            V2 at = p.visorScreen(V2(-0.5f, -0.6f));
            int pl = p.kissPlaceAt(at);
            kissNow(at, "asleep tier3");
            sim(0.6f, still);
            shot(L"79c_kiss_asleep");
            logf("kiss asleep: place %d, blush %.2f, state %s", pl, p.face.blush, p.stateName());
            if (p.face.blush < 0.2f || p.st != St::Sleep) fails++;
            sim(2.5f, still);
            if (p.st != St::Sleep) fails++;
        }
        auto lastMark = [&](V2& m) {
            for (auto it = p.parts.rbegin(); it != p.parts.rend(); ++it)
                if (it->type == 5) { m = it->p; return true; }
            return false;
        };
        {
            V2 at = p.visorScreen(V2(0.f, 0.6f));
            int pl = p.kissPlaceAt(at);
            p.parts.clear();
            kissNow(at, "asleep mouth");
            V2 m;
            bool got = lastMark(m);
            logf("kiss asleep mouth: place %d, mark %d at %.0f,%.0f (cursor %.0f,%.0f, off %.1f), state %s", pl, (int)got, m.x, m.y, at.x, at.y,
                 got ? (m - at).len() : 0.f, p.stateName());
            if (pl != Pet::KissMouth || !got || (m - at).len() < 3.f) fails++;
            if (p.st != St::Sleep) fails++;
            sim(2.f, still);
        }
        p.vit.rel = 50.f;
        reset(St::Sleep, 1000);
        sim(2.5f, still);
        {
            int f0 = p.facing;
            kissNow(p.visorScreen(V2(0.f, 0.f)), "asleep tier1");
            sim(1.5f, still);
            logf("kiss asleep tier 1: facing %d -> %d, state %s", f0, p.facing, p.stateName());
            if (p.st != St::Sleep || p.facing == f0) fails++;
        }
        reset(St::Idle, 100);
        sim(0.8f, still);
        p.vit.fatal = true;
        kissNow(p.visorScreen(V2(0.6f, 0.f)), "fatal");
        if (bub() != "..." || p.kissT >= 0) fails++;
        p.vit.fatal = false;
        reset(St::Idle, 100);
        sim(0.6f, still);
        {
            V2 g = p.headTop + V2(0, 6);
            p.beginDrag(g);
            sim(0.3f, [&](float) { return g; });
            bool k = p.kissable(p.visorScreen(V2(0.5f, 0.f)));
            logf("kiss while dragged: kissable %d", (int)k);
            if (k) fails++;
            p.endDrag();
            sim(1.5f, still);
        }
        reset(St::Idle, 100);
        sim(0.6f, still);
        p.gameStart(0);
        sim(0.5f, still);
        if (p.game) {
            bool on = true;
            for (int k = 0; k < 6; k++) {
                kissNow(p.visorScreen(V2(0.6f, 0.f)), "game");
                if (a.bubble.text.find(L"красне") != std::wstring::npos) fails++;
                sim(0.05f, still);
                on = on && p.game;
            }
            logf("kiss in game: game still on %d", (int)on);
            if (!on) fails++;
            if (p.game) p.gameStop("snapshot");
        } else {
            logf("kiss in game: game did not start");
        }
        sim(1.f, still);
        p.vit.intro = 2;
        p.vit.babyStart = (double)time(nullptr) - 3600.0;
        p.vit.rel = 50.f;
        reset(St::Idle, 100);
        sim(1.0f, still);
        {
            kissNow(p.visorScreen(V2(0.5f, 0.f)), "baby");
            sim(0.5f, still);
            shot(L"79g_kiss_baby");
            logf("kiss baby: blush %.2f happy %.2f rel %.1f", p.face.blush, p.face.happy, p.vit.rel);
            if (a.bubble.text.empty() || p.vit.rel != 50.f) fails++;
            sim(2.f, still);
            V2 at = p.mouthScreen();
            int pl = p.kissPlaceAt(at);
            p.parts.clear();
            kissNow(at, "baby mouth");
            V2 m;
            bool got = lastMark(m);
            logf("kiss baby mouth: place %d, mark %d off %.1f", pl, (int)got, got ? (m - at).len() : 0.f);
            if (pl == Pet::KissMouth && got && (m - at).len() < 3.f) fails++;
            sim(2.f, still);
            V2 belly = p.pos - V2(0, p.comH * 0.5f);
            pl = p.kissPlaceAt(belly);
            p.parts.clear();
            kissNow(belly, "baby body");
            sim(0.5f, still);
            logf("kiss baby body: place %d, mark %d, blush %.2f, bubble '%s'", pl, (int)lastMark(m), p.face.blush, bub().c_str());
            if ((pl == Pet::KissBody || pl == Pet::KissLegs) && (lastMark(m) || p.face.blush > 0.2f)) fails++;
            sim(2.f, still);
        }
        p.vit.intro = 0;
        reset(St::Idle, 100);
        sim(1.0f, still);
        {
            Surface cs;
            int cn = std::max(32, (int)lroundf(32.f * a.dpi));
            if (renderLipsCursor(cs, cn)) cs.savePng(a.gfx, dir + L"\\79e_lips_cursor.png");
            Surface big;
            if (renderLipsCursor(big, 128)) big.savePng(a.gfx, dir + L"\\79e2_lips_cursor_big.png");
            HCURSOR hc = lipsCursor();
            ICONINFO ii{};
            bool isCur = hc && GetIconInfo(hc, &ii) && !ii.fIcon;
            logf("kiss cursor: %dx%d, cursor %d, hotspot %lu,%lu", cn, cn, (int)isCur, ii.xHotspot, ii.yHotspot);
            if (ii.hbmColor) DeleteObject(ii.hbmColor);
            if (ii.hbmMask) DeleteObject(ii.hbmMask);
            if (!isCur) fails++;
        }
        p.vit.rel = 50.f;
        a.bubble.close();
        logf("kiss tests: %d failed", fails);
    }
