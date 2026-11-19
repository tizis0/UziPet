
    {
        auto day = [](int y, int mo, int d) {
            tm t{};
            t.tm_year = y - 1900;
            t.tm_mon = mo - 1;
            t.tm_mday = d;
            t.tm_hour = 15;
            t.tm_isdst = -1;
            mktime(&t);
            return t;
        };
        const double now = (double)time(nullptr), D = 86400.0;
        struct Case { const wchar_t* shot; int y, mo, d; double metAgo; int done; float rel; int stock; };
        const Case cs[] = {
            {L"61b_soon_newyear", 2026, 12, 24, 50 * D, 30, 50.f, 2},
            {L"61_newyear", 2027, 1, 1, 52 * D, 30, 75.f, 2},
            {L"61c_halloween", 2026, 10, 31, 20 * D, 7, 50.f, 2},
            {nullptr, 2027, 4, 1, 20 * D, 7, 50.f, 2},
            {L"61d_anniv7", 2026, 11, 10, 7 * D + 3600, 0, 50.f, 2},
            {L"61e_birthday", 2026, 11, 10, 365 * D + 3600, 100, 75.f, 2},
            {nullptr, 2026, 11, 10, 365 * D + 3600, 100, 15.f, 2},
            {nullptr, 2026, 11, 10, 730 * D + 3600, 365, 96.f, 6},
            {nullptr, 2026, 11, 10, 100 * D + 3600, 30, 15.f, 2},
        };
        for (auto& c : cs) {
            reset(St::Idle, 100);
            sim(0.5f, still);
            a.bubble.close();
            p.vit.metAt = now - c.metAgo;
            p.vit.annivDone = c.done;
            p.vit.holidayDay = 0;
            p.vit.rel = c.rel;
            p.vit.stock = c.stock;
            tm lt = day(c.y, c.mo, c.d);
            int stock0 = p.vit.stock;
            bool ok = p.dateSay(lt, now);
            logf("date test %04d-%02d-%02d met %d days tier %d: ok %d annivDone %d stock %d -> %d bubble '%s'", c.y, c.mo, c.d, p.metDays(now), p.tier(),
                 (int)ok, p.vit.annivDone, stock0, p.vit.stock, narrow(a.bubble.text).c_str());
            if (c.shot) {
                sim(std::min(a.bubble.typeDur + 0.3f, 7.f), still);
                shot(c.shot);
            }
        }
        reset(St::Idle, 100);
        a.bubble.close();
        p.vit.rel = 50.f;
        p.vit.metAt = now - 7 * D - 3600;
        p.vit.annivDone = 0;
        p.vit.holidayDay = 0;
        tm eve = day(2026, 12, 31);
        std::string seq;
        for (int k = 0; k < 3; k++) {
            int dummy = 0;
            const char* c = p.dateDue(eve, now, dummy);
            seq += std::string(" ") + (c ? c : "-");
            p.dateSay(eve, now);
            a.bubble.close();
        }
        logf("date same day:%s", seq.c_str());
        {
            p.vit.annivDone = 7;
            p.vit.holidayDay = 0;
            std::string season;
            const int ds[][3] = {{2026, 12, 20}, {2026, 12, 21}, {2026, 12, 25}, {2026, 12, 30}, {2026, 12, 31}, {2027, 1, 1}, {2027, 1, 2}, {2027, 4, 1}};
            for (auto& x : ds) {
                tm lt = day(x[0], x[1], x[2]);
                int dummy = 0;
                const char* c = p.dateDue(lt, now, dummy);
                if (c) p.dateSay(lt, now);
                a.bubble.close();
                season += " " + std::to_string(x[1]) + "." + std::to_string(x[2]) + "=" + (c ? c : "-");
            }
            logf("date season:%s (holidayDay %d)", season.c_str(), p.vit.holidayDay);
        }
        p.vit.metAt = now - 40 * D;
        p.vit.annivDone = 7;
        bool late = p.dateSay(day(2026, 11, 10), now);
        logf("date late anniv: said %d annivDone %d", (int)late, p.vit.annivDone);
        auto simIdle = [&](float secs, double idle) {
            for (int i = 0; i < (int)(secs * 60); i++) {
                Input in;
                in.cursor = still(0);
                in.idleSec = idle;
                a.now += dt;
                p.update(dt, in);
                a.bubble.update(dt);
                a.winOrigin = p.windowOrigin();
                p.stepRig(dt, a.winOrigin);
            }
        };
        reset(St::Idle, 100);
        a.bubble.close();
        p.vit.metAt = now - 7 * D - 3600;
        p.vit.annivDone = 0;
        p.dateT = 0.5f;
        p.lastSay = p.lastDateSay = -1e9;
        simIdle(2.0f, 100);
        logf("date live away: annivDone %d bubble %d", p.vit.annivDone, (int)a.bubble.active);
        p.dateT = 0.5f;
        simIdle(2.0f, 10);
        logf("date live pause: annivDone %d bubble '%s'", p.vit.annivDone, narrow(a.bubble.text).c_str());
        std::string pl;
        for (int n : {0, 1, 2, 5, 11, 12, 21, 22, 25, 101, 111, 112, 122}) {
            p.vit.metAt = now - n * D - 60;
            openSettings(false);
            for (auto& l : g_settings.extra())
                if (l.rfind(L"Знакомы", 0) == 0) pl += " [" + narrow(l) + "]";
        }
        logf("met plural:%s", pl.c_str());
        p.vit.metAt = now - 22 * D - 60;
        g_snapMode = true;
        openSettings(false);
        g_settings.surf.savePng(a.gfx, dir + L"\\61f_settings_met.png");
        p.vit.rel = 50.f;
        p.vit.stock = 2;
        a.bubble.close();
    }

    {
        auto hourTm = [](int h) {
            tm t{};
            t.tm_year = 126;
            t.tm_mon = 10;
            t.tm_mday = 10;
            t.tm_hour = h;
            return t;
        };
        reset(St::Idle, 100);
        p.vit.rel = 50.f;
        p.vit.energy = 30.f;
        sim(1.0f, still);
        a.bubble.close();
        Input in0;
        in0.cursor = still(0);
        in0.idleSec = 10;
        p.vit.lastSolver = 0;
        bool gNight = p.solverGate(hourTm(3), in0), gDay = p.solverGate(hourTm(14), in0), gSix = p.solverGate(hourTm(6), in0);
        p.vit.energy = 70.f;
        bool gRested = p.solverGate(hourTm(3), in0);
        p.vit.energy = 30.f;
        p.vit.lastSolver = (double)time(nullptr) - 3600;
        bool gCd = p.solverGate(hourTm(3), in0);
        p.vit.lastSolver = (double)time(nullptr) - 7 * 3600;
        bool gCdOld = p.solverGate(hourTm(3), in0);
        p.vit.lastSolver = 0;
        a.settings.solver = false;
        bool gOff = p.solverGate(hourTm(3), in0);
        a.settings.solver = true;
        Input inAway = in0;
        inAway.idleSec = 1000;
        bool gAway = p.solverGate(hourTm(3), inAway);
        a.bubble.say(L"Тест.");
        bool gBubble = p.solverGate(hourTm(3), in0);
        a.bubble.close();
        p.vit.intro = 2;
        bool gBaby = p.solverGate(hourTm(3), in0), sBaby = p.solverStart(true);
        p.vit.intro = 0;
        logf("solver gate: night %d day %d six %d rested %d cooldown1h %d cooldown7h %d off %d away %d bubble %d baby %d/%d", (int)gNight, (int)gDay,
             (int)gSix, (int)gRested, (int)gCd, (int)gCdOld, (int)gOff, (int)gAway, (int)gBubble, (int)gBaby, (int)sBaby);

        shot(L"62a_solver_before");
        bool ok = p.solverStart(true);
        p.solverMute = false;
        sim(0.48f, still);
        logf("solver mid: ok %d t %.3f solver %.2f sigil %.2f face %.2f/%.2f state %s last %.0f", (int)ok, p.solverT, p.solverK, p.sigilK,
             p.face.solver, p.face.sigil, p.stateName(), p.vit.lastSolver);
        shot(L"62_solver_mid");
        sim(0.35f, still);
        logf("solver mid2: t %.3f solver %.2f sigil %.2f", p.solverT, p.face.solver, p.face.sigil);
        shot(L"62c_solver_eyes");
        {
            const FaceDef& fd = a.rig.face;
            float K = 440.f / fd.size.x;
            for (int v = 0; v < 2; v++) {
                Surface big;
                if (!big.create(a.gfx, 520, 280)) break;
                auto* rt = big.begin(D2D1::ColorF(0.08f, 0.07f, 0.1f, 1));
                DrawCtx c{rt, a.gfx.d2d.Get(), a.gfx.dw.Get(), big.brush.Get(), a.gfx.round.Get(), big.gen, a.s};
                rt->SetTransform(D2D1::Matrix3x2F::Translation(-fd.center.x, -fd.center.y) * D2D1::Matrix3x2F::Scale(K, K) *
                                 D2D1::Matrix3x2F::Translation(260, 140));
                FaceState fs;
                fs.solver = 1;
                fs.sigil = v ? 0.f : 1.f;
                drawFace(c, fd, fs);
                big.end(a.gfx);
                big.savePng(a.gfx, dir + (v ? L"\\62h_solver_face_eyes.png" : L"\\62g_solver_face_sigil.png"));
            }
        }
        sim(0.75f, still);
        logf("solver shake: t %.3f solver %.2f surprise %.2f sad %.2f", p.solverT, p.face.solver, p.face.surprise, p.face.sad);
        shot(L"62d_solver_shake");
        a.bubble.text.clear();
        sim(0.75f, still);
        logf("solver after: t %.3f bubble '%s'", p.solverT, narrow(a.bubble.text).c_str());
        sim(std::min(a.bubble.typeDur + 0.3f, 6.f), still);
        shot(L"62b_solver_after");
        sim(2.0f, still);
        logf("solver end: t %.2f state %s bubble %d", p.solverT, p.stateName(), (int)a.bubble.active);
        std::string all;
        for (auto& l : a.phrases.list("solverAfter")) all += " [" + narrow(l) + "]";
        logf("solver lines:%s", all.c_str());

        reset(St::Idle, 100);
        sim(0.5f, still);
        p.solverStart(true);
        p.solverMute = true;
        sim(3.0f, still);
        logf("solver mute: t %.2f bubble %d surprise %.2f", p.solverT, (int)a.bubble.active, p.face.surprise);
        sim(1.5f, still);

        reset(St::Idle, 100);
        sim(0.5f, still);
        p.solverStart(true);
        sim(0.3f, still);
        p.beginDrag(p.headC);
        sim(0.2f, [&](float) { return p.headC; });
        logf("solver drag: t %.2f solver %.2f state %s", p.solverT, p.face.solver, p.stateName());
        p.endDrag();
        sim(2.0f, still);

        reset(St::Sit, 100);
        sim(1.5f, still);
        p.solverStart(true);
        p.solverMute = false;
        sim(0.48f, still);
        logf("solver sit: state %s active %d solver %.2f sigil %.2f", p.stateName(), p.active, p.face.solver, p.face.sigil);
        shot(L"62e_solver_sit");
        sim(4.0f, still);
        logf("solver sit end: t %.2f state %s", p.solverT, p.stateName());
        a.bubble.close();

        reset(St::Idle, 100);
        sim(0.5f, still);
        p.solverWait = 0.6f;
        sim(0.5f, still);
        float before = p.solverT;
        sim(0.2f, still);
        logf("solver button: t %.2f -> %.2f last %.0f", before, p.solverT, p.vit.lastSolver);
        sim(4.0f, still);
        a.bubble.close();

        p.vit.fatal = true;
        bool sFatal = p.solverStart(true);
        p.vit.fatal = false;
        logf("solver fatal: start %d", (int)sFatal);

        g_snapMode = true;
        bool dev0 = a.settings.dev;
        a.settings.dev = true;
        openSettings(false);
        g_settings.surf.savePng(a.gfx, dir + L"\\62f_settings_dev.png");
        a.settings.dev = dev0;
        p.vit.energy = 100.f;
        p.vit.lastSolver = 0;
    }
