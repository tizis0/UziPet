
    {
        for (const wchar_t* id : {L"ChromeHTML", L"MSEdgeHTM", L"MSEdgeDHTML", L"MSEdgeBHTML", L"FirefoxURL-308046B0AF4A39CB", L"YandexHTML", L"BraveHTML",
                                  L"OperaStable", L"FooBrowserURL"}) {
            ExtBrowser b = extBrowserFor(id);
            logf("ext map: %s -> %s edge %d firefox %d guess %d", narrow(id).c_str(), narrow(b.url).c_str(), (int)b.edge, (int)b.firefox, (int)b.guess);
        }
        ExtBrowser mine = extDefaultBrowser();
        logf("ext map: this PC '%s' -> %s, exe '%s'", narrow(mine.progId).c_str(), narrow(mine.url).c_str(), narrow(mine.exe).c_str());
        struct MatchCase { const wchar_t *exe, *img; } mc[] = {
            {L"C:\\Users\\u\\AppData\\Local\\Programs\\Opera\\launcher.exe", L"C:\\Users\\u\\AppData\\Local\\Programs\\Opera\\114.0.5282.21\\opera.exe"},
            {L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe", L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe"},
            {L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe", L"D:\\Portable\\Chrome\\chrome.exe"},
            {L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe", L"C:\\Windows\\explorer.exe"},
            {L"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe", L"C:\\Program Files (x86)\\Microsoft\\EdgeWebView\\Application\\130.0\\msedgewebview2.exe"},
            {L"C:\\Windows\\System32\\OpenWith.exe", L"C:\\Windows\\System32\\notepad.exe"},
        };
        for (auto& m : mc) logf("ext match: '%s' window of '%s' -> %d", narrow(m.exe).c_str(), narrow(m.img).c_str(), (int)extIsBrowserImage(m.exe, m.img));

        const HWND fakeBrowser = (HWND)(INT_PTR)0x7A51;
        RECT fakeRc{0, 0, 0, 0};
        g_snapUnder = [&](DrawCtx& c, V2 wo) {
            if (fakeRc.right <= fakeRc.left) return;
            D2D1_RECT_F r = D2D1::RectF(fakeRc.left - wo.x, fakeRc.top - wo.y, fakeRc.right - wo.x, fakeRc.bottom - wo.y);
            c.color(D2D1::ColorF(1, 1, 1, 1));
            c.rt->FillRectangle(r, c.br);
            c.color(D2D1::ColorF(0.80f, 0.83f, 0.88f, 1));
            c.rt->FillRectangle(D2D1::RectF(r.left, r.top, r.right, r.top + 88.f * a.dpi), c.br);
            c.color(D2D1::ColorF(0.4f, 0.4f, 0.45f, 1));
            c.rt->DrawRectangle(r, c.br, 1.5f);
            if (p.extPointAt.x > -1e8f) {
                V2 q = p.extPointAt - wo;
                c.color(D2D1::ColorF(0.1f, 0.5f, 1.f, 1));
                c.rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(q.x - 17.f, q.y - 9.f, q.x + 17.f, q.y + 9.f), 9.f, 9.f), c.br);
                c.color(D2D1::ColorF(1, 0, 0, 1));
                c.rt->DrawLine(D2D1::Point2F(q.x - 8, q.y - 8), D2D1::Point2F(q.x + 8, q.y + 8), c.br, 2.f);
                c.rt->DrawLine(D2D1::Point2F(q.x - 8, q.y + 8), D2D1::Point2F(q.x + 8, q.y - 8), c.br, 2.f);
            }
        };
        auto pointLog = [&](const char* what) {
            int side = (p.extPointAt - p.shoulderScreen(1)).len() < (p.extPointAt - p.shoulderScreen(2)).len() ? 1 : 2;
            V2 sh = p.shoulderScreen(side), hd = p.handScreen(side == 2);
            V2 u = (hd - sh).norm(), v = (p.extPointAt - sh).norm();
            float ang = acosf(clampf(dot(u, v), -1.f, 1.f)) * 180.f / PI;
            logf("ext %s: state %s gnd %d y %.0f pos %.0f,%.0f facing %d target %.0f,%.0f arm side %d err %.1f deg bPoint %.2f ask '%s' bubble '%s' bubbleRect %.0f,%.0f-%.0f,%.0f",
                 what, p.stateName(), (int)p.gnd.kind, p.gnd.y, p.pos.x, p.pos.y, p.facing, p.extPointAt.x, p.extPointAt.y, side, ang, p.bPoint,
                 a.bubble.askId.c_str(), narrow(a.bubble.text).c_str(), a.bubble.rect.left + p.windowOrigin().x, a.bubble.rect.top + p.windowOrigin().y,
                 a.bubble.rect.right + p.windowOrigin().x, a.bubble.rect.bottom + p.windowOrigin().y);
        };
        auto askLog = [&](const char* what) {
            std::string ch;
            for (auto& c : a.bubble.choices) ch += " [" + narrow(c) + "]";
            logf("ext %s: step %d ask '%s' bubble '%s'%s", what, p.extStep, a.bubble.askId.c_str(), narrow(a.bubble.text).c_str(), ch.c_str());
        };
        p.vit.rel = 70.f;
        a.settings.extAsked = false;
        a.settings.talk = true;
        p.extTestProgId = L"ChromeHTML";

        reset(St::Idle, 100);
        sim(0.5f, still);
        p.lastSay = -100;
        p.extOfferT = 0.2f;
        simIdle = 10;
        sim(2.6f, still);
        simIdle = 100;
        askLog("auto offer");
        shot(L"70_ext_offer");
        p.answer("extOffer", 1);
        sim(2.6f, still);
        askLog("plea");
        shot(L"70b_ext_plea");
        p.answer("extPlea", 0);
        sim(2.6f, still);
        askLog("paste");
        shot(L"70c_ext_paste");

        fakeRc = RECT{620, 300, 1640, 940};
        a.world.addFakeWindow(fakeBrowser, fakeRc, false);
        p.answer("extPaste", 0);
        sim(0.05f, still);
        logf("ext go: state %s planned %d up %d standTop %.0f comH %.0f", p.stateName(), (int)p.extPlanned, (int)p.extUp, p.standTop, p.comH);
        sim(5.0f, still);
        pointLog("dev");
        shot(L"70d_ext_dev_point");
        sim(0.25f, still);
        pointLog("dev+0.25s");

        p.answer("extDev", 0);
        sim(2.4f, still);
        askLog("drag");
        logf("ext folder: hit at hands %d, hit at head %d, center %.0f,%.0f", (int)p.extFolderHit(p.carriedCenter), (int)p.extFolderHit(p.headC),
             p.carriedCenter.x, p.carriedCenter.y);
        shot(L"70f_ext_drag");
        extDragFolder();
        p.openTalk();
        sim(0.2f, still);
        askLog("talk over drag");
        a.bubble.close();
        sim(0.3f, still);
        askLog("drag after talk closed");
        p.receiveFile(exeDir() + L"\\UziPet.exe", 1);
        sim(0.2f, still);
        logf("ext file while folder: heldPath '%s' bubble '%s'", narrow(p.heldPath).c_str(), narrow(a.bubble.text).c_str());
        sim(0.3f, still);
        askLog("drag after file");

        float rel0 = p.vit.rel;
        extTestAlive(true);
        sim(0.1f, still);
        sim(1.4f, still);
        logf("ext done: step %d state %s rel %.1f -> %.1f extAsked %d folder %d bubble '%s'", p.extStep, p.stateName(), rel0, p.vit.rel,
             (int)a.settings.extAsked, (int)p.extFolder, narrow(a.bubble.text).c_str());
        shot(L"70g_ext_done");
        extTestAlive(false);

        a.settings.extAsked = false;
        p.extTestProgId = L"MSEdgeHTM";
        a.world.setFake(RECT{0, 0, 1920, 1080}, RECT{0, 0, 1920, 1020});
        fakeRc = RECT{300, 280, 1500, 980};
        a.world.addFakeWindow(fakeBrowser, fakeRc, false);
        reset(St::Idle, 100);
        sim(0.5f, still);
        p.extStart(true);
        sim(0.3f, still);
        p.answer("extPaste", 0);
        sim(3.0f, still);
        pointLog("dev edge");
        shot(L"70d2_ext_dev_edge");
        p.answer("extDev", 1);
        sim(0.6f, still);
        logf("ext later: step %d folder %d bubble '%s'", p.extStep, (int)p.extFolder, narrow(a.bubble.text).c_str());

        p.extTestProgId = L"ChromeHTML";
        a.world.setFake(RECT{0, 0, 1920, 1080}, RECT{0, 0, 1920, 1020});
        fakeRc = RECT{0, 0, 1920, 1020};
        a.world.addFakeWindow(fakeBrowser, fakeRc, true);
        reset(St::Idle, 100);
        sim(0.5f, still);
        p.extStart(true);
        sim(0.3f, still);
        p.answer("extPaste", 0);
        sim(0.05f, still);
        logf("ext go maximized: state %s up %d", p.stateName(), (int)p.extUp);
        sim(8.5f, still);
        pointLog("dev taskbar");
        shot(L"70e_ext_dev_taskbar");
        p.answer("extDev", 0);
        sim(0.3f, still);
        p.answer("extDrag", 0);
        sim(2.5f, still);
        askLog("fallback");
        p.answer("extFallback", 0);
        sim(9.f, still);
        logf("ext no signal: step %d bubble '%s'", p.extStep, narrow(a.bubble.text).c_str());

        a.world.setFake(RECT{0, 0, 1920, 1080}, RECT{0, 0, 1920, 1020});
        fakeRc = RECT{0, 0, 0, 0};
        a.settings.extAsked = false;
        reset(St::Idle, 100);
        sim(0.5f, still);
        p.extStart(false);
        sim(0.2f, still);
        p.answer("extOffer", 1);
        sim(0.2f, still);
        p.answer("extPlea", 1);
        sim(0.3f, still);
        logf("ext declined: step %d extAsked %d bubble '%s'", p.extStep, (int)a.settings.extAsked, narrow(a.bubble.text).c_str());
        a.settings.extAsked = false;
        p.extTestProgId = L"FirefoxURL-308046B0AF4A39CB";
        a.bubble.close();
        p.lastSay = -100;
        p.extStart(true);
        sim(0.3f, still);
        logf("ext firefox: step %d extAsked %d bubble '%s'", p.extStep, (int)a.settings.extAsked, narrow(a.bubble.text).c_str());
        a.settings.extAsked = false;
        p.extTestProgId = L"ChromeHTML";
        a.bubble.close();
        p.extStart(true);
        sim(0.3f, still);
        p.setState(St::Sleep, 1000);
        sim(0.2f, still);
        logf("ext asleep: step %d ask '%s'", p.extStep, a.bubble.askId.c_str());
        p.vit.intro = 2;
        p.extStart(true);
        logf("ext baby: step %d", p.extStep);
        onCommand(ID_EXT);
        p.vit.intro = 0;
        reset(St::Idle, 100);
        sim(0.3f, still);
        p.heldPath = exeDir() + L"\\UziPet.exe";
        p.extStart(true);
        logf("ext start while holding: heldPath '%s' holding %d step %d", narrow(p.heldPath).c_str(), (int)p.holding(), p.extStep);
        a.world.setFake(RECT{0, 0, 1920, 1080}, RECT{0, 0, 1920, 1020});
        sim(0.3f, still);
        p.answer("extPaste", 0);
        sim(1.5f, still);
        askLog("dev no window");
        shot(L"70h_ext_dev_nowin");
        p.extStop("test");
        a.settings.extAsked = false;
        a.bubble.close();
        p.extStart(false);
        sim(0.3f, still);
        askLog("offer");
        p.openTalk();
        sim(0.2f, still);
        a.bubble.close();
        sim(0.2f, still);
        logf("ext offer lost to talk: step %d extAsked %d", p.extStep, (int)a.settings.extAsked);
        a.settings.extAsked = false;
        reset(St::Idle, 100);
        sim(0.3f, still);
        p.extStart(false);
        sim(34.f, still);
        logf("ext offer ignored: step %d extAsked %d", p.extStep, (int)a.settings.extAsked);
        a.bubble.close();
        g_snapUnder = nullptr;
        p.extTestProgId.clear();
        p.extOfferT = 1e9f;
        p.vit.rel = 50.f;
    }
