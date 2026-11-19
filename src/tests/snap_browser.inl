
    {
        if (!a.web) a.web = new WebNest();
        WebNest& W = *a.web;
        W.testStart();
        a.world.setFake(RECT{0, 0, 1920, 1080}, RECT{0, 0, 1920, 1020});
        p.webFake = true;
        p.webFakeRc = RECT{760, 90, 1920, 1020};
        int since = 0;
        auto sent = [&]() {
            std::string o;
            for (auto& c : W.testCmds(since)) o += c + " ";
            return o;
        };
        auto start = [&]() {
            reset(St::Idle, 100);
            p.vit.rel = 50.f;
            sim(0.5f, still);
            a.bubble.close();
            sent();
        };
        auto has = [](const std::string& s, std::initializer_list<const char*> ws) {
            for (auto w : ws)
                if (s.find(w) != std::string::npos) return true;
            return false;
        };
        auto inside = [&]() { return p.st == St::Inside || p.st == St::Dive; };
        int cases = 0, casesOk = 0;

        start();
        W.testExtSeen(nowSec() - 30);
        bool known = W.extKnown(), fresh = W.extAlive();
        p.webGo();
        sim(2.5f, still);
        std::string cmds = sent();
        bool dove = p.webOn && p.nest == Pet::NestWeb && p.st == St::Inside && p.webExt;
        WebState bg;
        bg.visible = false;
        bg.mode = "search";
        bg.iw = 1160;
        bg.ih = 930;
        bg.title = L"Узи · Google";
        W.testState(bg);
        sim(30.f, still);
        bool stayed = p.webOn && inside();
        bool ok = known && !fresh && dove && has(cmds, {"\"c\":\"visit\""}) && stayed;
        cases++;
        casesOk += ok;
        logf("web via extension seen 30 s ago: ok %d: known %d fresh %d, dove %d (%s), sent '%.60s', own page never opened, still in 30 s later with a tab %d",
             (int)ok, (int)known, (int)fresh, (int)dove, p.stateName(), cmds.c_str(), (int)stayed);
        p.leaveWeb(2);
        sim(3.f, still);

        start();
        W.testExtSeen(-100);
        float r0 = p.vit.rel;
        p.webGo();
        std::string said = narrow(a.bubble.text);
        bool talked = a.bubble.active;
        sim(1.5f, still);
        cmds = sent();
        ok = !W.extKnown() && talked && has(said, {"раузер", "асширени"}) && !p.webOn && p.nest == Pet::NestNone && !inside() && cmds.empty() &&
             fabsf(p.vit.rel - r0) < 0.05f;
        cases++;
        casesOk += ok;
        logf("web no extension: ok %d: said '%s', in browser %d, %s, sent '%s', rel %.1f -> %.1f", (int)ok, said.c_str(), (int)p.webOn, p.stateName(),
             cmds.c_str(), r0, p.vit.rel);

        for (int took = 1; took >= 0; took--) {
            start();
            W.testExtSeen(nowSec() - 30);
            r0 = p.vit.rel;
            p.webGo();
            sim(2.5f, still);
            cmds = sent();
            dove = p.webOn && p.st == St::Inside;
            if (took) W.testExtSeen(nowSec());
            double t0 = a.now;
            for (int k = 0; k < 60 * 20 && p.webOn; k++) sim(0.05f, still);
            float gaveUp = (float)(a.now - (took ? t0 : p.webSince));
            said = narrow(a.bubble.text);
            sim(3.f, still);
            bool back = !p.webOn && p.nest == Pet::NestNone && !inside();
            float want = took ? 20.f : 35.f;
            ok = dove && has(cmds, {"\"c\":\"visit\""}) && fabsf(gaveUp - want) < 1.5f && back && has(said, {"спит", "открыл", "Постучалась"}) &&
                 fabsf(p.vit.rel - r0) < 0.05f;
            cases++;
            casesOk += ok;
            logf("web no tab (%s): ok %d: dove %d, gave up %.1f s after (want %.0f), back out %d (%s at %.0f,%.0f), said '%s', rel %.1f -> %.1f",
                 took ? "extension took the visit" : "extension never took it", (int)ok, (int)dove, gaveUp, want, (int)back, p.stateName(), p.pos.x, p.pos.y,
                 said.c_str(), r0, p.vit.rel);
        }
        logf("web extension only summary: %d cases, %d ok", cases, casesOk);
        p.webFake = false;
        W.stop();
        reset(St::Idle, 100);
        a.bubble.close();
    }

    {
        wchar_t lk[MAX_PATH];
        GetFullPathNameW((dir + L"\\..\\held_look").c_str(), MAX_PATH, lk, nullptr);
        std::wstring look = lk;
        struct LF { const wchar_t* f; const wchar_t* shot; };
        for (LF l : {LF{L"panorama 16x9.png", L"76_held_wide"}, LF{L"concept_sheet_tall_portrait_background_final_v3_export.png", L"76b_held_tall"},
                     LF{L"uzi_cuts2.json", L"76c_held_json"}}) {
            std::wstring f = look + L"\\" + l.f;
            if (GetFileAttributesW(f.c_str()) == INVALID_FILE_ATTRIBUTES) {
                logf("held look: no '%s', skipped", narrow(f).c_str());
                continue;
            }
            reset(St::Idle, 100);
            p.vit.rel = 50.f;
            sim(0.5f, still);
            p.receiveFile(f, 1);
            if (p.senseJob)
                for (int i = 0; i < 600 && !p.senseJob->done; i++) Sleep(5);
            sim(1.5f, still);
            shot(l.shot);
            D2D1_RECT_F bx, im, lb;
            p.heldRects(bx, im, lb);
            V2 cc = p.carriedCenter;
            float eyeY = p.pos.y - p.standEye;
            logf("held look %s: name '%s' img %.1fx%.1f box %.0f, label %.0fx%.0f (max %.0f); screen: img top %.0f, label %.0f..%.0f, eyes at %.0f, head bottom %.0f;"
                 " hit img %d label %d head %d feet %d",
                 narrow(l.shot).c_str(), narrow(p.heldName).c_str(), im.right - im.left, im.bottom - im.top, bx.right - bx.left, lb.right - lb.left,
                 lb.bottom - lb.top, p.heldLayMax, cc.y + im.top, cc.y + lb.top, cc.y + lb.bottom, eyeY, p.headC.y + p.headR,
                 (int)p.heldHit(cc + V2(0, (im.top + im.bottom) * 0.5f)), (int)p.heldHit(cc + V2(0, (lb.top + lb.bottom) * 0.5f)), (int)p.heldHit(p.headC),
                 (int)p.heldHit(p.pos - V2(0, 5)));
            p.dropHeld("test");
        }
        a.bubble.close();
    }
