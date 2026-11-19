
    auto petFor = [&](float rel, float secs, const char* tag) {
        reset(St::Idle, 100);
        p.vit.rel = rel;
        p.petBlock = 0;
        p.lastPetEnd = -100;
        sim(0.6f, still);
        V2 ph = p.headC;
        std::string seen;
        int n = (int)(secs / 0.25f);
        for (int k = 0; k < n; k++) {
            sim(0.25f, [&](float tt) { return ph + V2(sinf((float)a.now * 9.f + tt) * 30.f, 0); });
            std::string s = p.stateName();
            if (seen.empty() || seen.substr(seen.rfind(' ') + 1) != s) seen += " " + s;
        }
        logf("pet %s (rel %.0f, tier %d):%s | petting %d stage %d bubble '%s'", tag, rel, p.tier(), seen.c_str(), (int)p.petting, p.petStage,
             narrow(a.bubble.text).c_str());
    };
    petFor(20.f, 4.f, "hate");
    petFor(80.f, 30.f, "warm");
    petFor(96.f, 8.f, "love");
    shot(L"48_pet_love");

    reset(St::Idle, 100);
    p.vit.rel = 50.f;
    p.vit.food = 20.f;
    sim(0.5f, still);
    for (int k = 0; k < 5; k++) {
        p.poke();
        sim(0.3f, still);
    }
    logf("weak pokes: state %s bubble '%s'", p.stateName(), narrow(a.bubble.text).c_str());
    p.vit.food = 100.f;

    int refused = 0;
    for (int k = 0; k < 10; k++) {
        reset(St::Idle, 100);
        p.vit.rel = 15.f;
        p.talkChoice(2);
        if (p.st != St::Sit) refused++;
    }
    logf("hate obey: refused %d of 10 sit requests", refused);

    int nmTalks = 0, nmLong = 0;
    for (float rel : {15.f, 50.f, 80.f, 96.f}) {
        reset(St::Idle, 100);
        p.vit.rel = rel;
        p.vit.energy = 40.f;
        p.sleepNow();
        sim(0.5f, still);
        p.startNightmare();
        sim(1.0f, still);
        if (rel == 80.f) shot(L"49_nightmare");
        logf("nightmare rel %.0f: state %s asking %d id %s '%s'", rel, p.stateName(), (int)a.bubble.asking(), a.bubble.askId.c_str(),
             narrow(a.bubble.text).c_str());
        int steps = 0;
        std::string path;
        float calm0 = p.nightmareCalm;
        while (a.bubble.asking() && a.bubble.askId.rfind("talk:nm_", 0) == 0 && steps < 8) {
            std::string id = a.bubble.askId;
            int ch = randi(0, (int)a.bubble.choices.size() - 1);
            path += " [" + narrow(a.bubble.choices[ch]) + "]";
            p.answer(id, ch);
            path += " " + narrow(a.bubble.text);
            steps++;
            sim(0.3f, still);
        }
        logf("  nightmare talk (%d steps, calm %.2f -> %.2f): %s", steps, calm0, p.nightmareCalm, path.c_str());
        nmTalks += steps >= 1 && !p.nightmare;
        nmLong += steps >= 2;
        std::string said = narrow(a.bubble.text);
        sim(std::max(4.0f, a.bubble.holdT - a.bubble.t + 2.f), still);
        logf("  comforted: said '%s', rel now %.1f, state %s, asking %d id %s", said.c_str(), p.vit.rel, p.stateName(), (int)a.bubble.asking(),
             a.bubble.askId.c_str());
        if (a.bubble.asking() && a.bubble.askId == "nightmareSleep") {
            p.answer("nightmareSleep", 0);
            sim(2.5f, still);
            logf("  agreed to sleep: state %s", p.stateName());
        }
    }
    {
        reset(St::Idle, 100);
        p.vit.rel = 80.f;
        p.vit.energy = 40.f;
        p.sleepNow();
        sim(0.5f, still);
        p.startNightmare();
        sim(0.3f, still);
        std::wstring start = a.bubble.text;
        bool pet = p.nightmarePetted() && p.nightmarePet && a.bubble.text != start && p.nightmare;
        logf("nightmare petted: '%s' asking %d", narrow(a.bubble.text).c_str(), (int)a.bubble.asking());
        a.bubble.close();
        p.nightmareSteps = 1;
        sim(4.f, still);
        bool faded = !p.nightmare && p.sleepAfterT >= 0;
        logf("nightmare halfway: said '%s', nightmare %d", narrow(a.bubble.text).c_str(), (int)p.nightmare);
        logf("nightmare talks: calmed %d of 4, more than one step %d, petted branch %d, halfway fade %d -> %s", nmTalks, nmLong, (int)pet, (int)faded,
             nmTalks == 4 && nmLong >= 2 && pet && faded ? "ok" : "FAIL");
    }
    reset(St::Idle, 100);
    p.vit.rel = 96.f;
    p.warmT = 0.5f;
    p.lastSay = -100;
    sim(2.0f, still);
    logf("warm line (rel 96): '%s'", narrow(a.bubble.text).c_str());
    reset(St::Idle, 100);
    p.vit.rel = 68.f;
    p.addRel(-5.f, "test");
    sim(4.0f, still);
    logf("tier change: bubble '%s'", narrow(a.bubble.text).c_str());
    p.vit.rel = 50.f;

    reset(St::Sit, 100);
    sim(0.6f, still);
    V2 sg = p.headC;
    sim(0.1f, [&](float) { return sg; });
    p.beginDrag(sg);
    sim(0.6f, [&](float tt) { return sg + V2(tt * 500.f, -40.f); });
    p.endDrag();
    sim(1.2f, still);
    float rotAfterDrop = p.rot;
    p.setState(St::Sit, 100);
    p.rot = 0.5f;
    sim(0.5f, still);
    p.goIdle(5.f, 6.f);
    sim(0.6f, still);
    logf("stand after sit: rot after drop %.3f, after stand %.3f state %s", rotAfterDrop, p.rot, p.stateName());

    p.vit.intro = 2;
    p.vit.babyStart = (double)time(nullptr) - 3600.0;
    p.vit.food = 60.f;
    reset(St::Idle, 100);
    sim(1.0f, still);
    logf("baby: active rig %d comH %.0f headR %.0f", p.active, p.comH, p.headR);
    shot(L"51_baby");
    openSettings(false);
    g_settings.surf.savePng(a.gfx, dir + L"\\51h_baby_settings.png");
    for (int k = 0; k < 2; k++) {
        p.pos = V2(900.f, 1020.f - 260.f);
        p.vel = k ? V2(700.f, -900.f) : V2(0, 0);
        p.rot = k ? 0.f : 0.9f;
        p.rotVel = k ? 8.f : 0.f;
        p.gnd.valid = false;
        p.setState(St::Fall);
        std::string seen;
        for (int f = 0; f < 16; f++) {
            sim(0.25f, still);
            std::string s = p.stateName();
            if (seen.empty() || seen.substr(seen.rfind(' ') + 1) != s) seen += " " + s;
        }
        logf("baby %s: states%s | rot %.2f pos %.0f,%.0f drops %d rest %.1f vel %.1f,%.1f w %.2f touch %d", k ? "tossed" : "dropped", seen.c_str(),
             p.rot, p.pos.x, p.pos.y, p.vit.babyDrops, p.babyRest, p.vel.x, p.vel.y, p.rotVel, (int)p.babyTouch);
        shot(k ? L"51g_baby_tossed" : L"51f_baby_dropped");
    }
    reset(St::Idle, 100);
    sim(0.5f, still);
    p.sleepNow();
    sim(1.0f, still);
    shot(L"51b_baby_sleep");
    p.setState(St::Idle, 100);
    p.growing = true;
    p.glow = 1.f;
    a.bubble.active = false;
    sim(0.4f, still);
    shot(L"51c_baby_glow");
    p.growing = false;
    p.glow = 0;
    p.pos = V2(700.f, 1020.f + 30.f);
    p.vel = V2(0, 0);
    p.rot = 0;
    p.rotVel = 0;
    p.gnd.valid = false;
    p.setState(St::Fall);
    sim(1.5f, still);
    logf("baby released below floor: pos %.0f,%.0f gnd %d state %s", p.pos.x, p.pos.y, (int)p.gnd.valid, p.stateName());
    p.setState(St::Idle, 100);
    p.openTalk();
    sim(1.3f, still);
    logf("baby talk: asking %d id %s meters %zu", (int)a.bubble.asking(), a.bubble.askId.c_str(), a.bubble.meters.size());
    shot(L"51d_baby_talk");
    a.bubble.close();
    a.bubble.askInput(L"Как вас называть?", L"Тизи", {L"Дальше"}, "introName");
    sim(1.2f, still);
    shot(L"51e_name_input");
    a.bubble.close();
    p.vit.intro = 0;
    reset(St::Idle, 100);
    sim(0.5f, still);
    logf("back to adult: active rig %d comH %.0f", p.active, p.comH);

    reset(St::Idle, 100);
    sim(0.5f, still);
    p.tSad = 6.f;
    a.bubble.active = false;
    sim(1.0f, still);
    shot(L"50_sad_quiet");
    p.sayCat("nightmareThanks", 1.f, L"", true);
    sim(0.35f, still);
    shot(L"50b_sad_talk");

    {
        const float rels[4] = {15.f, 50.f, 75.f, 96.f};
        const wchar_t* names[4] = {L"60c_shutdown_bye_hate", L"60_shutdown_bye", L"60d_shutdown_bye_warm", L"60b_shutdown_bye_love"};
        for (int k = 0; k < 4; k++) {
            reset(k == 1 ? St::Sit : St::Idle, 100);
            p.vit.rel = rels[k];
            sim(1.0f, still);
            a.bubble.close();
            bool ok = sessionBye(false);
            logf("bye tier %d: ok %d state %s bSleep %.2f active rig %d bubble '%s' typed %d life %.1f", p.tier(), (int)ok, p.stateName(), p.bSleep,
                 p.active, narrow(a.bubble.text).c_str(), (int)(a.bubble.t >= a.bubble.typeDur), a.bubble.life - a.bubble.t);
            shot(names[k]);
            if (k == 1) {
                sim(1.0f, still);
                logf("bye hold: state %s bubble %d", p.stateName(), (int)a.bubble.active);
                p.byeCancel();
                sim(2.5f, still);
                logf("bye cancel: state %s byeOn %d bubble '%s'", p.stateName(), (int)p.byeOn, narrow(a.bubble.text).c_str());
                shot(L"60e_shutdown_cancel");
            }
            p.byeOn = false;
        }
        reset(St::Sleep, 100);
        p.vit.rel = 75.f;
        sim(0.5f, still);
        p.startNightmare();
        sim(3.5f, still);
        p.laterT = 0.5f;
        p.laterCat = "afkNoteTell";
        bool nmOk = sessionBye(false);
        std::wstring byeText = a.bubble.text;
        sim(4.f, still);
        logf("bye over nightmare: ok %d nightmare %d laterT %.1f state %s same bubble %d '%s'", (int)nmOk, (int)p.nightmare, p.laterT, p.stateName(),
             (int)(a.bubble.text == byeText && a.bubble.active), narrow(a.bubble.text).c_str());
        shot(L"60f_shutdown_nightmare");
        p.byeCancel();
        p.byeOn = false;
        reset(St::Idle, 100);
        sim(0.5f, still);
        V2 bh = p.headTop + V2(0, 6);
        p.beginDrag(bh);
        sim(0.4f, [&](float tt) { return bh + V2(0, -tt * 200.f); });
        bool upOk = sessionBye(false);
        logf("bye held: ok %d lay %d state %s bubble '%s'", (int)upOk, (int)p.byeLay, p.stateName(), narrow(a.bubble.text).c_str());
        shot(L"60g_shutdown_held");
        p.byeCancel();
        sim(0.3f, [&](float) { return bh + V2(0, -80.f); });
        logf("bye held cancel: state %s bubble '%s'", p.stateName(), narrow(a.bubble.text).c_str());
        p.endDrag();
        sim(2.f, still);
        reset(St::Idle, 100);
        sessionBye(false);
        sim(100.f, still);
        int byeMid = (int)p.byeOn;
        sim(25.f, still);
        logf("bye timeout: byeOn at 100 s %d, at 125 s %d, state %s", byeMid, (int)p.byeOn, p.stateName());
        p.byeOn = false;
        reset(St::Idle, 100);
        p.vit.intro = 2;
        bool babyOk = sessionBye(false);
        p.vit.intro = 0;
        p.vit.fatal = true;
        bool fatalOk = sessionBye(false);
        p.vit.fatal = false;
        reset(St::Sleep, 100);
        bool sleepOk = sessionBye(false);
        logf("bye skipped: baby %d fatal %d asleep %d", (int)babyOk, (int)fatalOk, (int)sleepOk);
        p.byeOn = false;
        p.vit.rel = 50.f;
        a.bubble.close();
    }
