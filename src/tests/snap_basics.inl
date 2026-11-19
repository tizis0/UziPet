
    App& a = g_app;
    CreateDirectoryW(dir.c_str(), nullptr);
    a.world.setFake(RECT{0, 0, 1920, 1080}, RECT{0, 0, 1920, 1020});
    Pet& p = *g_pet;
    p.extOfferT = 1e9f;
    a.settings.hardcoreAsked = true;
    a.settings.replies = false;
    p.dayLoad();
    p.dayRoll();
    p.dayMood = 0;
    const float dt = 1.f / 60.f;
    V2 floorPt(960, 1020);
    double simIdle = 100;
    bool simIdleRun = false;
    auto sim = [&](float secs, std::function<V2(float)> cursor) {
        int n = (int)(secs * 60);
        for (int i = 0; i < n; i++) {
            Input in;
            in.cursor = cursor(i * dt);
            in.idleSec = simIdle;
            if (simIdleRun) simIdle += dt;
            a.now += dt;
            p.update(dt, in);
            a.bubble.update(dt);
            a.winOrigin = p.windowOrigin();
            p.stepRig(dt, a.winOrigin);
        }
    };
    auto shot = [&](const wchar_t* name) {
        V2 wo = p.windowOrigin();
        a.winOrigin = wo;
        renderFrame(g_surf, wo, D2D1::ColorF(0.86f, 0.87f, 0.9f, 1));
        g_surf.savePng(a.gfx, dir + L"\\" + name + L".png");
        V2 hc = p.headC - wo;
        logf("shot %s head %.0f %.0f r %.0f pin %.2f", narrow(name).c_str(), hc.x, hc.y, p.headR, p.pinError());
    };
    auto still = [&](float) { return V2(1250, 820); };
    auto reset = [&](St s, float dur) {
        p.spawn(floorPt);
        p.setGround(a.world.surfs[0]);
        p.greeted = true;
        p.talkCd = 1e9f;
        p.setState(s, dur);
        a.bubble.active = false;
    };

    reset(St::Idle, 100);
    sim(1.5f, still);
    shot(L"01_idle");

    reset(St::Walk, 0);
    p.goWalk(1600);
    sim(1.0f, still);
    shot(L"02_walk_a");
    sim(0.13f, still);
    shot(L"03_walk_b");

    reset(St::Sit, 100);
    sim(1.5f, still);
    shot(L"04_sit");
    sim(0.7f, still);
    shot(L"04_sit_b1");
    sim(0.7f, still);
    shot(L"04_sit_b2");
    sim(0.7f, still);
    shot(L"04_sit_b3");

    sim(0.5f, still);
    V2 shc = p.headC;
    sim(3.0f, [&](float tt) { return shc + V2(sinf(tt * 9.f) * 30.f, 0); });
    shot(L"04b_sit_petted");
    a.bubble.say(L"Ну вот, теперь сидим и болтаем, как нормальные люди.");
    sim(0.45f, [&](float tt) { return shc + V2(sinf(tt * 9.f) * 30.f, 0); });
    shot(L"04c_sit_talk");
    a.bubble.close();

    reset(St::Sleep, 1000);
    sim(2.5f, still);
    shot(L"05_sleep");
    V2 slc = p.headC;
    sim(3.0f, [&](float tt) { return slc + V2(sinf(tt * 9.f) * 30.f, 0); });
    shot(L"05b_sleep_petted");

    reset(St::Idle, 100);
    sim(0.5f, still);
    V2 hc = p.headC;
    sim(3.5f, [&](float tt) { return hc + V2(sinf(tt * 9.f) * 40.f, 0); });
    shot(L"06_petted");

    reset(St::Idle, 100);
    sim(0.3f, still);
    p.beginDrag(p.headTop + V2(0, 6));
    V2 g0 = p.headTop + V2(0, 6);
    sim(0.6f, [&](float tt) { return g0 + V2(tt * 900.f, -tt * 200.f); });
    shot(L"07_drag_swing");
    sim(1.6f, [&](float) { return g0 + V2(540.f, -120.f); });
    shot(L"08_drag_hang");
    p.endDrag();

    reset(St::Fall, 0);
    p.pos = V2(900, 500);
    p.vel = V2(300, -900);
    p.gnd.valid = false;
    sim(0.25f, still);
    shot(L"09_air");

    reset(St::Dizzy, 100);
    p.tCross = 0;
    sim(1.0f, still);
    shot(L"10_dizzy");

    reset(St::Idle, 100);
    sim(0.5f, still);
    a.bubble.say(L"Я НЕ милая! Я грозный боевой дрон, понятно?");
    sim(3.0f, still);
    shot(L"11_bubble");

    reset(St::Wave, 100);
    sim(0.8f, still);
    shot(L"12_wave");

    reset(St::Idle, 100);
    sim(0.5f, still);
    V2 nearHead = p.headC + V2(p.headR * 1.3f, -p.headR * 0.2f);
    sim(0.2f, [&](float) { return nearHead; });
    p.startBite();
    sim(0.32f, [&](float) { return nearHead; });
    shot(L"13_bite");

    reset(St::Idle, 100);
    sim(0.5f, still);
    V2 hook = p.headTop + V2(40.f, -150.f);
    sim(0.1f, [&](float) { return hook; });
    p.beginCling(hook);
    sim(1.2f, [&](float tt) { return hook + V2(sinf(tt * 3.f) * 25.f, 0); });
    shot(L"14_cling");
    p.endCling(false);

    auto partPt = [&](const char* name, V2 frac) {
        int i = a.rig.find(name);
        const PartDef& d = a.rig.parts[i];
        return xf(a.inst.world[i], d.pos + V2(d.size.x * frac.x, d.size.y * frac.y)) + p.windowOrigin();
    };
    reset(St::Idle, 100);
    sim(0.6f, still);
    V2 hp = partPt("forearm_r", V2(0.5f, 0.8f));
    p.beginDrag(hp);
    sim(1.2f, [&](float tt) { return hp + V2(tt * 160.f, 0); });
    shot(L"15_led");
    V2 hp2 = hp + V2(192.f, 0);
    sim(1.0f, [&](float tt) { return hp2 + V2(0, -tt * 420.f); });
    shot(L"16_hand_hang");
    p.endDrag();

    reset(St::Idle, 100);
    sim(0.5f, still);
    wchar_t full[MAX_PATH];
    GetFullPathNameW((exeDir() + L"\\..\\art\\uzi_ref.webp").c_str(), MAX_PATH, full, nullptr);
    p.receiveFile(full, 1);
    if (p.senseJob)
        for (int i = 0; i < 600 && !p.senseJob->done; i++) Sleep(5);
    sim(3.0f, still);
    shot(L"18_file");
    p.heldPath.clear();
