
    {
        p.vit.rel = 50.f;
        p.vit.energy = 100.f;
        p.vit.food = 100.f;
        auto after = [&](const char* what) {
            logf("game check %s: game %d state %s rot %.3f runK %.1f walkOff %d bubble '%s' ask '%s'", what, (int)(p.game != nullptr), p.stateName(), p.rot,
                 p.runK, (int)p.walkOff, narrow(a.bubble.text).c_str(), a.bubble.askId.c_str());
        };
        auto play = [&](float maxSecs, std::function<V2(float)> cur, const wchar_t* runShot, const wchar_t* leapShot) {
            float T = 0, runT = 0, leapT = 0;
            int runN = 0;
            std::wstring runBase = runShot ? runShot : L"";
            while (T < maxSecs && p.game) {
                sim(0.05f, [&](float tt) { return cur(T + tt); });
                T += 0.05f;
                runT = p.st == St::Walk && p.runK > 1.f ? runT + 0.05f : runT;
                leapT = p.st == St::Leap ? leapT + 0.05f : leapT;
                if (!runBase.empty() && runN < 3 && runT >= 0.6f + 0.1f * runN - 0.001f) {
                    shot((runBase + (runN == 0 ? L"" : runN == 1 ? L"_b" : L"_c")).c_str());
                    runN++;
                }
                if (leapShot && leapT >= 0.2f) { shot(leapShot); leapShot = nullptr; }
            }
            return T;
        };
        auto settle = [&](float secs) { sim(secs, [&](float) { return p.headC + V2(500.f, -300.f); }); };

        reset(St::Idle, 100);
        sim(0.6f, still);
        p.openTalk();
        sim(1.4f, still);
        shot(L"64c_talk_menu");
        logf("game talk menu: %zu choices, last '%s', rect %.0f,%.0f-%.0f,%.0f, last button %.0f,%.0f-%.0f,%.0f", a.bubble.choices.size(),
             a.bubble.choices.empty() ? "" : narrow(a.bubble.choices.back()).c_str(), a.bubble.rect.left, a.bubble.rect.top, a.bubble.rect.right,
             a.bubble.rect.bottom, a.bubble.choiceRc.empty() ? 0.f : a.bubble.choiceRc.back().left, a.bubble.choiceRc.empty() ? 0.f : a.bubble.choiceRc.back().top,
             a.bubble.choiceRc.empty() ? 0.f : a.bubble.choiceRc.back().right, a.bubble.choiceRc.empty() ? 0.f : a.bubble.choiceRc.back().bottom);
        p.answer("talk", 6);
        sim(std::min(a.bubble.typeDur + 0.4f, 4.f), still);
        shot(L"64d_games_ask");
        std::string chs;
        for (auto& c : a.bubble.choices) chs += " [" + narrow(c) + "]";
        logf("game ask: id '%s' text '%s' choices%s", a.bubble.askId.c_str(), narrow(a.bubble.text).c_str(), chs.c_str());

        float rel0 = p.vit.rel;
        p.answer("games", 0);
        logf("game start line '%s' typeDur %.2f, S %.2f headR %.0f standTop %.0f comH %.0f", narrow(a.bubble.text).c_str(), a.bubble.typeDur, a.s, p.headR,
             p.standTop, p.comH);
        float dir1 = p.pos.x < 960.f ? 1.f : -1.f;
        V2 c1 = p.pos + V2(150.f * dir1, -25.f);
        float T1 = play(30.f, [&](float tt) { return c1 + V2(90.f * dir1 * tt, 0); }, L"64_game_chase", nullptr);
        logf("game 1 (slow away, 90 px/s): ended after %.2f s total, pos %.0f cursor %.0f, rel %.1f -> %.1f", T1, p.pos.x, c1.x + 90.f * dir1 * T1, rel0,
             p.vit.rel);
        sim(std::min(a.bubble.typeDur + 0.3f, 4.f), [&](float) { return p.headC + V2(30.f, -10.f); });
        shot(L"64b_game_win");
        after("win");
        float askT = 0;
        for (; askT < 8.f && a.bubble.askId != "gameAgain"; askT += 0.1f) settle(0.1f);
        sim(1.f, still);
        shot(L"64e_game_again");
        logf("game again: asked after %.1f s, id '%s' text '%s'", askT, a.bubble.askId.c_str(), narrow(a.bubble.text).c_str());
        p.answer("gameAgain", 1);
        sim(0.3f, still);
        after("enough");
        settle(2.f);
        after("enough+2s");

        reset(St::Idle, 100);
        sim(0.4f, still);
        rel0 = p.vit.rel;
        p.gameStart(0);
        float side = 1700.f;
        float T2 = play(30.f, [&](float) {
            if (fabsf(side - p.pos.x) < 350.f) side = side > 960.f ? 220.f : 1700.f;
            return V2(side, 995.f);
        }, nullptr, nullptr);
        logf("game 2 (dodge): ended after %.2f s, rel %.1f -> %.1f", T2, rel0, p.vit.rel);
        sim(1.5f, still);
        after("lose");
        sim(4.f, still);
        if (a.bubble.askId == "gameAgain") {
            p.answer("gameAgain", 0);
            sim(0.2f, still);
            logf("game again yes: game %d bubble '%s'", (int)(p.game != nullptr), narrow(a.bubble.text).c_str());
            p.gameStop("snapshot");
        }

        reset(St::Idle, 100);
        sim(0.4f, still);
        p.gameStart(0);
        std::wstring count3 = a.bubble.text;
        V2 c3(p.pos.x + 120.f, 40.f);
        bool unfairShot = false;
        float T3 = 0;
        while (T3 < 20.f && p.game) {
            sim(0.05f, [&](float) { return c3; });
            T3 += 0.05f;
            if (!unfairShot && a.bubble.active && a.bubble.text != count3 && a.bubble.t > a.bubble.typeDur + 0.2f) {
                shot(L"64f_game_unfair");
                unfairShot = true;
            }
        }
        logf("game 3 (top of screen): ended after %.2f s, bubble '%s'", T3, narrow(a.bubble.text).c_str());
        sim(2.f, still);
        after("unfair");

        reset(St::Idle, 100);
        sim(0.4f, still);
        p.gameStart(0);
        V2 c4 = p.headTop + V2(70.f, -170.f);
        float T4 = play(30.f, [&](float) { return c4; }, nullptr, L"64g_game_leap");
        logf("game 4 (above head %.0f px): ended after %.2f s", p.pos.y - c4.y, T4);
        sim(2.f, still);
        after("leap");

        reset(St::Idle, 100);
        sim(0.4f, still);
        p.gameStart(0);
        V2 c5 = p.pos + V2(p.pos.x < 960.f ? 900.f : -900.f, -25.f);
        for (float run5 = 0, T5 = 0; T5 < 6.f && run5 < 0.5f; T5 += 0.05f) {
            sim(0.05f, [&](float) { return c5; });
            if (p.st == St::Walk && p.runK > 1.f) run5 += 0.05f;
        }
        V2 g5 = p.headTop + V2(0, 6);
        logf("game 5 before drag: game %d state %s runK %.1f", (int)(p.game != nullptr), p.stateName(), p.runK);
        p.beginDrag(g5);
        sim(0.6f, [&](float tt) { return g5 + V2(tt * 300.f, -tt * 200.f); });
        after("dragging");
        p.endDrag();
        sim(2.5f, still);
        after("drag+2.5s");
        shot(L"64h_game_dragged");

        reset(St::Idle, 100);
        sim(0.4f, still);
        p.gameStart(0);
        V2 c5b(p.pos.x, (float)a.world.mons[0].rc.bottom + 60.f);
        float T5b = 0;
        while (T5b < 20.f && p.game) {
            sim(0.05f, [&](float) { return c5b; });
            T5b += 0.05f;
        }
        logf("game 5b (below her monitor, y %.0f, mon bottom %ld): ended after %.2f s, result bubble '%s'", c5b.y, a.world.mons[0].rc.bottom, T5b,
             narrow(a.bubble.text).c_str());
        sim(2.f, still);
        after("below monitor");

        {
            Activity act0 = a.activity;
            a.activity.kind = AppKind::Create;
            a.activity.since = a.now - 10;
            a.activity.fgCovers = false;
            a.activity.fgMon = 0;
            a.activity.fgRect = {300, 200, 1500, 900};
            reset(St::Idle, 100);
            sim(1.f, still);
            std::string st0 = p.stateName();
            p.gameStart(0);
            play(4.f, [&](float) { return p.pos + V2(-600.f, -25.f); }, nullptr, nullptr);
            logf("game watch: before %s, after 4 s game %d state %s", st0.c_str(), (int)(p.game != nullptr), p.stateName());
            p.gameStop("snapshot");
            a.bubble.close();
            sim(1.f, still);
            logf("game watch end: state %s", p.stateName());
            a.activity = act0;
            reset(St::Idle, 100);
            sim(0.5f, still);
        }

        reset(St::Idle, 100);
        sim(0.4f, still);
        p.gameStart(0);
        play(3.f, [&](float) { return p.pos + V2(-600.f, -25.f); }, nullptr, nullptr);
        p.poke();
        sim(0.3f, still);
        logf("game poke 1: game %d bubble '%s'", (int)(p.game != nullptr), narrow(a.bubble.text).c_str());
        sim(1.f, [&](float) { return p.pos + V2(-600.f, -25.f); });
        p.poke();
        sim(0.3f, still);
        after("poke 2");

        reset(St::Idle, 100);
        sim(0.4f, still);
        p.gameStart(0);
        play(3.5f, [&](float) { return p.pos + V2(500.f, -25.f); }, nullptr, nullptr);
        g_snapMode = true;
        openSettings(false);
        sim(1.5f, still);
        after("settings");
        reset(St::Idle, 100);
        sim(0.4f, still);
        p.gameStart(0);
        play(3.5f, [&](float) { return p.pos + V2(500.f, -25.f); }, nullptr, nullptr);
        p.openTalk();
        sim(0.3f, still);
        after("talk");
        a.bubble.close();

        reset(St::Idle, 100);
        sim(0.4f, still);
        p.vit.rel = 15.f;
        int no = 0, yes = 0;
        for (int k = 0; k < 40; k++) {
            a.bubble.close();
            p.lastSay = -100;
            p.gameMenu();
            if (a.bubble.askId == "games") yes++;
            else no++;
        }
        a.bubble.close();
        logf("game hate: refused %d / asked %d of 40 (rel %.0f)", no, yes, p.vit.rel);
        p.vit.rel = 50.f;
        p.vit.food = 20.f;
        p.gameMenu();
        logf("game hungry: game %d ask '%s' bubble '%s'", (int)(p.game != nullptr), a.bubble.askId.c_str(), narrow(a.bubble.text).c_str());
        a.bubble.close();
        p.vit.food = 100.f;
        p.vit.energy = 18.f;
        sim(0.9f, still);
        a.bubble.close();
        p.gameMenu();
        logf("game tired: game %d ask '%s' bubble '%s' state %s", (int)(p.game != nullptr), a.bubble.askId.c_str(), narrow(a.bubble.text).c_str(), p.stateName());
        a.bubble.close();
        p.vit.energy = 100.f;
        reset(St::Idle, 100);
        sim(0.4f, still);
        p.vit.intro = 2;
        p.openTalk();
        std::string bch;
        for (auto& c : a.bubble.choices) bch += " [" + narrow(c) + "]";
        p.gameMenu();
        bool babyGame = p.gameStart(0);
        p.vit.intro = 0;
        a.bubble.close();
        logf("game baby: talk choices%s, start %d", bch.c_str(), (int)babyGame);
        reset(St::Sleep, 1000);
        sim(0.5f, still);
        p.gameMenu();
        bool sleepGame = p.gameStart(0);
        logf("game asleep: ask '%s' start %d state %s", a.bubble.askId.c_str(), (int)sleepGame, p.stateName());
        a.bubble.close();
        p.vit.rel = 50.f;
        reset(St::Idle, 100);
        sim(0.5f, still);
    }
