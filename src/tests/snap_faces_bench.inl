
    cleanGuardSelfTest();

    auto shotAlpha = [&](const wchar_t* name) {
        V2 wo = p.windowOrigin();
        a.winOrigin = wo;
        renderFrame(g_surf, wo, D2D1::ColorF(0, 0, 0, 0));
        g_surf.savePng(a.gfx, dir + L"\\" + name + L".png");
    };
    reset(St::Idle, 100);
    sim(1.2f, still);
    shotAlpha(L"alpha_idle");
    p.tHappy = 3;
    sim(0.4f, still);
    shotAlpha(L"alpha_happy");
    reset(St::Sit, 100);
    sim(1.5f, still);
    shotAlpha(L"alpha_sit");

    struct F { const wchar_t* n; std::function<void(FaceState&)> set; };
    F faces[] = {
        {L"f_neutral", [](FaceState&) {}},
        {L"f_blink", [](FaceState& f) { f.open = 0.05f; }},
        {L"f_happy", [](FaceState& f) { f.happy = 1; f.blush = 0.8f; }},
        {L"f_squint", [](FaceState& f) { f.squint = 1; f.blush = 1; }},
        {L"f_angry", [](FaceState& f) { f.angry = 1; }},
        {L"f_sad", [](FaceState& f) { f.sad = 1; }},
        {L"f_surprise", [](FaceState& f) { f.surprise = 1; }},
        {L"f_sleep", [](FaceState& f) { f.sleep = 1; }},
        {L"f_heart", [](FaceState& f) { f.heart = 1; f.blush = 0.6f; }},
        {L"f_dizzy", [](FaceState& f) { f.dizzy = 1; }},
        {L"f_cross", [](FaceState& f) { f.cross = 1; }},
    };
    reset(St::Idle, 1000);
    sim(1.0f, still);
    p.freezeFace = true;
    for (auto& f : faces) {
        p.face = FaceState{};
        p.face.time = 0.3f;
        p.face.look = V2(0.2f, 0);
        f.set(p.face);
        shot(f.n);
    }
    p.freezeFace = false;
    logf("snapshots written");

    extern int g_dbgSkip;
    reset(St::Idle, 1000);
    sim(1.0f, still);
    V2 wo = p.windowOrigin();
    D2D1_RECT_F bb = p.bounds(wo);
    RECT dirty{(LONG)bb.left - 6, (LONG)bb.top - 6, (LONG)bb.right + 6, (LONG)bb.bottom + 6};
    struct B { const char* n; int skip; bool clip; };
    B cases[] = {{"empty frame, full", 7, false}, {"empty frame, dirty", 7, true}, {"parts only", 6, true}, {"face only", 5, true},
                 {"tail only", 3, true}, {"everything", 0, true}, {"everything, full", 0, false}};
    for (auto& cs : cases) {
        g_dbgSkip = cs.skip;
        for (int i = 0; i < 20; i++) renderFrame(g_surf, wo, {0, 0, 0, 0}, cs.clip ? &dirty : nullptr);
        double t0 = nowSec();
        for (int i = 0; i < 200; i++) {
            p.face.look.x = (i % 2) ? 0.3f : -0.3f;
            renderFrame(g_surf, wo, {0, 0, 0, 0}, cs.clip ? &dirty : nullptr);
        }
        logf("bench %-20s %.3f ms", cs.n, (nowSec() - t0) / 200 * 1000.0);
    }
    g_dbgSkip = 0;
    logf("bench dirty %ldx%ld of %dx%d", dirty.right - dirty.left, dirty.bottom - dirty.top, g_surf.w, g_surf.h);
