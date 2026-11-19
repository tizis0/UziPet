
    {
        int fails = 0;
        auto check = [&](bool ok, const char* what) {
            if (!ok) fails++;
            logf("miss test %s: %s", ok ? "ok" : "FAILED", what);
        };
        struct G { double h; int b; };
        const G gaps[] = {{1, -1}, {3, 0}, {7.9, 0}, {10, 1}, {30, 1}, {48, 2}, {72, 2}, {5 * 24, 3}, {9 * 24, 3}, {12 * 24, 4}, {40 * 24, 4}};
        const float rels[4] = {10.f, 50.f, 75.f, 96.f};
        for (auto& g : gaps) {
            std::string row;
            for (int t = 0; t < 4; t++) {
                p.vit.rel = rels[t];
                p.missGap = g.h * 3600.0;
                p.missArm();
                static const char* byGap[5] = {"missYouHours", "missYouDay", "missYouDays", "missYouWeek", "missYouLong"};
                std::string want = g.b < 0 ? "" : byGap[g.b];
                bool ok = p.missCat == want && (want.empty() || (!a.phrases.listAt(want.c_str(), t).empty() && a.phrases.layerOf(want.c_str(), t) == a.phrases.stages[t]));
                if (!ok) fails++;
                row += " " + (p.missCat.empty() ? std::string("-") : p.missCat) + (ok ? "" : "(BAD)");
            }
            logf("miss %.1f h -> '%s':%s", g.h, narrow(awayWords(g.h * 3600.0)).c_str(), row.c_str());
            p.missCat.clear();
        }
        for (int t : {0, 2, 3}) {
            reset(St::Idle, 100);
            p.vit.rel = rels[t];
            p.vit.food = 100.f;
            p.vit.stock = 2;
            p.vit.zeroT = 0;
            p.vit.starved = p.vit.fatal = false;
            p.lifeT = 10.f;
            p.vit.lastSeen = (double)time(nullptr) - (t == 3 ? 3 * 86400.0 : 5 * 3600.0);
            p.offlineCatchUp();
            p.missArm();
            std::string want = p.missCat;
            p.spawn(V2(960, 700));
            p.greeted = false;
            a.bubble.close();
            std::string said;
            bool shotDone = t != 2;
            for (int k = 0; k < 40; k++) {
                sim(0.25f, still);
                if (a.bubble.active && said.find(narrow(a.bubble.text)) == std::string::npos) said += " [" + narrow(a.bubble.text) + "]";
                if (!shotDone && a.bubble.active) {
                    shotDone = true;
                    sim(std::min(a.bubble.typeDur + 0.3f, 6.f), still);
                    shot(L"85_miss_you");
                }
            }
            int lines = 0;
            for (char ch : said) lines += ch == '[';
            bool greetToo = false;
            for (auto* c : {"greet", "morning", "night"})
                for (auto& s : a.phrases.list(c))
                    if (!s.empty() && said.find(narrow(a.phrases.fill(s, L""))) != std::string::npos) greetToo = true;
            check(!want.empty() && p.missSaid == want && lines >= 1 && !greetToo, "said once instead of the greeting");
            logf("miss landing tier %d: %s ->%s", t, want.c_str(), said.c_str());
        }
        for (double g : {35 * 60.0, 2 * 3600.0}) {
            simIdleRun = false;
            simIdle = 100;
            reset(St::Idle, 100);
            p.vit.rel = 75.f;
            sim(0.3f, still);
            a.bubble.close();
            p.afkNapNow();
            simIdle = g;
            sim(0.5f, still);
            a.bubble.close();
            simIdle = 0.5;
            std::string said;
            for (float tt = 0; tt < 45.f && (p.afkNap || p.st == St::Sleep); tt += 0.25f) sim(0.25f, still);
            for (int k = 0; k < 4; k++) {
                sim(0.25f, still);
                if (a.bubble.active && said.find(narrow(a.bubble.text)) == std::string::npos) said += " [" + narrow(a.bubble.text) + "]";
            }
            std::string w = narrow(awayWords(g));
            check(said.find(w) != std::string::npos, "afk wake says how long");
            logf("afk return %.0f min (asleep): '%s' ->%s", g / 60.0, w.c_str(), said.c_str());
            if (g > 3600) {
                sim(std::min(a.bubble.typeDur, 6.f), still);
                shot(L"85b_afk_back_long");
            }
            sim(4.f, still);
            check(!p.wasAway, "no second 'you're back' after the wake");
        }
        for (int t : {1, 2}) {
            reset(St::Idle, 100);
            p.vit.rel = rels[t];
            p.quietUntil = a.now + 1000;
            p.lastGreetAt = -1e9;
            simIdle = 100;
            sim(0.3f, still);
            a.bubble.close();
            double g = t == 1 ? 50 * 60.0 : 12 * 60.0;
            simIdle = g;
            sim(0.3f, still);
            simIdle = 0.5;
            sim(0.3f, still);
            std::string s = narrow(a.bubble.text), w = narrow(awayWords(g));
            check(t == 1 ? s.find(w) != std::string::npos : (a.bubble.active && s.find(w) == std::string::npos), "awake return");
            logf("back awake tier %d after %.0f min: '%s'", t, g / 60.0, s.c_str());
            p.quietUntil = 0;
            sim(3.f, still);
            a.bubble.close();
        }
        simIdle = 100;
        p.vit.rel = 50.f;
        logf("miss tests: %d failed", fails);
    }
