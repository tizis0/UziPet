
    {
        int fails = 0;
        auto check = [&](bool ok, const char* what) {
            if (!ok) fails++;
            logf("emote check %s: %s", ok ? "ok" : "FAIL", what);
        };
        reset(St::Idle, 100);
        p.vit.rel = 80.f;
        sim(0.5f, still);
        a.bubble.close();
        a.bubble.say(L"Привет.| [blush:2]Ну [hide:2]вот.");
        std::string txt = narrow(a.bubble.text);
        check(txt.find('[') == std::string::npos && a.bubble.cues.size() == 2, "tags cut out of the text");
        sim(0.2f, still);
        check(p.tBlush <= 0.01f && p.emHideT <= 0, "not before the typing reaches them");
        sim(std::min(a.bubble.typeDur, 5.f), still);
        check(p.emHideT > 0 && p.face.blush > 0.4f, "hide + blush fired");
        sim(0.4f, still);
        shot(L"86_emote_hide");
        sim(3.f, still);
        a.bubble.close();
        a.bubble.say(L"[turn][steam:1.5]Хм.");
        int f0 = p.facing;
        sim(0.6f, still);
        check(p.facing != f0 || p.facing == (1250 > p.pos.x ? -1 : 1), "turned away from the cursor");
        check(p.emSteamT > 0 || p.parts.size() > 0, "steam");
        shot(L"86b_emote_turn_steam");
        sim(3.f, still);
        a.bubble.close();
        {
            p.apiSayAt = -1e9;
            p.quietUntil = 0;
            bool ok1 = p.apiSay(L"Брр...| [shiver:4]Холодно,| {name}.");
            std::string s1 = narrow(a.bubble.text);
            bool ok2 = p.apiSay(L"второй сразу");
            check(ok1 && !ok2 && s1.find("Холодно") != std::string::npos && s1.find('[') == std::string::npos && s1.find('{') == std::string::npos,
                  "api say: shown once, not over herself, tags and name filled");
            sim(std::min(a.bubble.typeDur, 4.f), still);
            check(p.emShiverT > 0, "api say: [shiver] fired");
            sim(0.8f, still);
            shot(L"88_api_shiver");
            logf("api say: '%s', shiver %.1f, particles %zu", s1.c_str(), p.emShiverT, p.parts.size());
            sim(4.f, still);
            a.bubble.close();
            p.apiEmoteAt = -1e9;
            check(p.apiEmote(L"[blush:2],lookdown") && p.tBlush > 1.f && !p.apiEmote(L"blush"), "api emote: tags only, rate limited");
            p.apiSayAt = -1e9;
            p.quietUntil = a.now + 100;
            check(!p.apiSay(L"тихо"), "api say: respects 'be quiet'");
            p.quietUntil = 0;
            sim(2.f, still);
        }
        {
            V2 at = p.visorScreen(V2(0.8f, 0.15f));
            a.bubble.say(L"[blush:3]Ты же не отстанешь,| да?| [hug:2.8]...Ну тогда вот.");
            sim(2.5f, [&](float) { return at; });
            logf("emote mid hug: cues %zu, t %.2f, hug %.2f, active %d", a.bubble.cues.size(), a.bubble.t, p.emHugT, (int)a.bubble.active);
            check(p.emHugT > 0, "mid-line [hug] after pauses");
            sim(3.f, still);
            a.bubble.close();
        }

        reset(St::Idle, 100);
        p.vit.rel = 100.f;
        p.cuddleHeat = 0;
        p.cuddleStage = 0;
        p.lastCuddleHug = -1e9;
        sim(0.8f, still);
        int maxStage = 0;
        bool meltShot = false, hugShot = false;
        std::string said;
        for (int k = 0; k < 40 && !hugShot; k++) {
            if (a.now - p.cuddleSaidAt > 4.5) a.bubble.close();
            p.lastSay = -100;
            p.kissT = -1;
            p.petBlock = 0;
            if (p.st == St::Sit) p.setState(St::Idle, 100);
            V2 at = p.visorScreen(V2(0.8f, 0.15f));
            p.kiss(at);
            for (int j = 0; j < 8; j++) {
                sim(0.25f, [&](float) { return at; });
                if (a.bubble.active && said.find(narrow(a.bubble.text)) == std::string::npos) said += " [" + narrow(a.bubble.text) + "]";
                if (p.cuddleStage == 2 && !meltShot && p.emHideT > 0) { meltShot = true; shot(L"87_cuddle_melt"); }
                if (p.cuddleStage == 3 && !hugShot && p.emHugT > 0) {
                    hugShot = true;
                    sim(0.5f, [&](float) { return at; });
                    shot(L"87b_cuddle_hug_open");
                    sim(1.0f, [&](float) { return at; });
                    shot(L"87c_cuddle_hug");
                }
            }
            maxStage = std::max(maxStage, p.cuddleStage);
            logf("cuddle test k %d: stage %d heat %.1f hide %.1f hug %.1f state %s bubble %d t %.2f cues %zu '%s'", k, p.cuddleStage, p.cuddleHeat, p.emHideT,
                 p.emHugT, p.stateName(), (int)a.bubble.active, a.bubble.t, a.bubble.cues.size(), narrow(a.bubble.text).c_str());
        }
        logf("cuddle: stage %d heat %.1f ->%s", maxStage, p.cuddleHeat, said.c_str());
        check(maxStage == 3 && meltShot && hugShot, "hot -> melt -> hug");
        float h = p.cuddleHeat;
        sim(20.f, still);
        check(p.cuddleHeat < h - 1.f, "cools down when left alone");
        reset(St::Idle, 100);
        p.vit.rel = 70.f;
        p.cuddleHeat = 0;
        p.cuddleStage = 0;
        p.cuddleAdd(7.f, "test");
        sim(1.f, still);
        check(p.cuddleStage == 1 && a.bubble.active, "tier 2 embarrassed");
        p.cuddleAdd(5.f, "test");
        sim(3.f, still);
        check(p.cuddleStage == 1, "tier 2 never melts");
        p.cuddleHeat = 0;
        p.cuddleStage = 0;
        sim(4.f, still);
        a.bubble.close();
        p.vit.rel = 50.f;
        {
            auto mons0 = a.world.mons;
            Activity act0 = a.activity;
            a.world.mons.resize(1);
            a.activity.fgCovers = false;
            a.activity.fgMon = 0;
            check(!p.shouldHide(true), "one monitor, bare desktop: stays");
            a.activity.fgCovers = true;
            check(p.shouldHide(true), "one monitor, real fullscreen window: hides");
            a.world.mons = mons0;
            a.activity = act0;
        }
        logf("emote tests: %d failed", fails);
    }
