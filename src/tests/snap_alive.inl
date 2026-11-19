
    {
        int fails = 0;
        auto check = [&](bool ok, const char* what) {
            if (!ok) fails++;
            logf("alive check %s: %s", ok ? "ok" : "FAIL", what);
        };
        reset(St::Idle, 100);
        sim(0.3f, still);
        a.bubble.close();
        {
            Settings fresh;
            check(!fresh.hardcore && !fresh.hardcoreAsked, "hardcore: off by default");
            bool h0 = a.settings.hardcore, k0 = a.settings.hardcoreAsked;
            a.settings.hardcore = false;
            a.settings.hardcoreAsked = false;
            p.hardcoreAnswer(-1);
            check(!a.settings.hardcore && !a.settings.hardcoreAsked && p.hardcoreAskAt > a.now, "hardcore: ignored, asks later");
            a.bubble.close();
            p.hardcoreAnswer(0);
            check(a.settings.hardcore && a.settings.hardcoreAsked && a.bubble.active, "hardcore: yes turns it on");
            logf("hardcore yes: '%s'", narrow(a.bubble.text).c_str());
            a.bubble.close();
            p.hardcoreAnswer(1);
            check(!a.settings.hardcore && a.settings.hardcoreAsked, "hardcore: no keeps it off");
            a.bubble.close();
            a.settings.hardcore = h0;
            a.settings.hardcoreAsked = k0;
        }
        {
            Phrases& ph = a.phrases;
            ph.keys.push_back("testBag");
            ph.vals.push_back({L"один", L"два", L"три", L"четыре", L"пять"});
            ph.bags.push_back({});
            ph.keys.push_back("testBag2");
            ph.vals.push_back({L"один", L"шесть", L"семь"});
            ph.bags.push_back({});
            int bad = 0;
            std::wstring lastOfRound;
            for (int round = 0; round < 40; round++) {
                std::vector<std::wstring> seen;
                std::wstring first;
                for (int k = 0; k < 5; k++) {
                    std::wstring s = ph.pick("testBag");
                    if (k == 0) first = s;
                    seen.push_back(s);
                    if (k == 4) {
                        if (round > 0 && first == lastOfRound) bad++;
                        lastOfRound = s;
                    }
                }
                std::sort(seen.begin(), seen.end());
                if (std::unique(seen.begin(), seen.end()) - seen.begin() != 5) bad++;
            }
            check(bad == 0, "bag: every line once per round, no repeat across rounds");
            std::wstring b2 = ph.pick("testBag2");
            check(b2 != L"один", "bag: line just said in another category is skipped");
            std::wstring sp = dir + L"\\said_test.txt";
            ph.statePath = sp;
            ph.dirty = true;
            ph.saveState();
            Phrases q;
            q.keys = ph.keys;
            q.vals = ph.vals;
            q.bags.assign(q.keys.size(), {});
            q.loadState(sp);
            size_t i = ph.keys.size() - 2;
            check(q.bags[i].order == ph.bags[i].order && q.bags[i].pos == ph.bags[i].pos && q.recent == ph.recent, "bag: state survives a restart");
            q.vals[i].push_back(L"шесть строк");
            q.bags.assign(q.keys.size(), {});
            q.recent.clear();
            q.loadState(sp);
            check(q.bags[i].order.empty(), "bag: changed category is dealt anew");
            ph.statePath.clear();
            DeleteFileW(sp.c_str());
            for (int k = 0; k < 2; k++) {
                ph.keys.pop_back();
                ph.vals.pop_back();
                ph.bags.pop_back();
            }
        }
        {
            int seen[6] = {};
            DayLog prev;
            int keep = p.dayMood;
            for (int k = 0; k < 600; k++) {
                p.moodRoll(prev);
                seen[std::clamp(p.dayMood, 0, 5)]++;
            }
            logf("mood rolls: plain %d lively %d sleepy %d dreamy %d cheeky %d chatty %d", seen[0], seen[1], seen[2], seen[3], seen[4], seen[5]);
            check(std::all_of(seen, seen + 6, [](int n) { return n > 20; }), "mood: every mood comes up");
            {
                float rel0 = p.vit.rel;
                auto cheeky = [&](float rel) {
                    p.vit.rel = rel;
                    int n = 0;
                    for (int k = 0; k < 1000; k++) {
                        p.moodRoll(prev);
                        if (p.dayMood == 4) n++;
                    }
                    return n;
                };
                int lo = cheeky(5.f), hi = cheeky(98.f);
                logf("mood cheeky: tier %d -> %d/1000, tier %d -> %d/1000", (p.vit.rel = 5.f, p.tier()), lo, (p.vit.rel = 98.f, p.tier()), hi);
                p.vit.rel = 98.f;
                p.dayMood = 4;
                float hideLove = p.moodK(MkHide);
                p.vit.rel = rel0;
                check(lo > hi * 3 && hi < 80 && hideLove < 1.5f, "mood: adores you — rarely cheeky, and only half as cheeky");
            }
            p.dayMood = 2;
            check(p.moodK(MkSit) > 1.f && p.moodK(MkTalk) > 1.f && p.moodK(MkJump) < 1.f, "mood: sleepy sits more, talks and jumps less");
            p.dayMood = 1;
            check(p.moodK(MkWalk) > 1.f && p.moodK(MkSit) < 1.f, "mood: lively moves more");
            p.dayLoad();
            p.dayRoll();
            p.dayMood = 2;
            p.moodToldDay = 0;
            float lt0 = p.lifeT;
            p.lifeT = std::max(p.lifeT, 100.f);
            a.bubble.close();
            bool told = p.moodAnnounce();
            logf("mood told: '%s'", narrow(a.bubble.text).c_str());
            a.bubble.close();
            check(told && !p.moodAnnounce(), "mood: tells once a day");
            int idleMood = 0;
            for (int k = 0; k < 200; k++)
                if (const char* c = p.idleCat(); c && strstr(c, "Sleepy")) idleMood++;
            logf("mood idle share (sleepy): %d/200", idleMood);
            check(idleMood > 40 && idleMood < 140, "mood: sleepy day mixes in sleepy idle lines");
            p.dayMood = keep;
            p.lifeT = lt0;
        }
        {
            float lo = 1e9f, hi = 0, sum = 0;
            for (int k = 0; k < 2000; k++) {
                float v = jitterTime(480.f, 150.f, 1500.f);
                lo = std::min(lo, v), hi = std::max(hi, v), sum += v;
            }
            logf("jitter 480: min %.0f max %.0f mean %.0f", lo, hi, sum / 2000);
            check(lo >= 150.f && hi <= 1500.f && hi - lo > 600.f && sum / 2000 > 380.f && sum / 2000 < 650.f, "timers: spread around the base, clamped");
        }
        {
            float rel0 = p.vit.rel;
            p.vit.rel = 96.f;
            bool rep0 = a.settings.replies;
            a.settings.replies = true;
            reset(St::Idle, 100);
            sim(0.5f, still);
            a.bubble.close();
            p.lastSay = -100;
            p.sayCat("wakeRested", 1.f, L"", true);
            check(a.bubble.asking() && a.bubble.askId.rfind("reply:", 0) == 0 && a.bubble.choices.size() >= 2 && p.moment == "Woke",
                  "moment: woke up, replies under the bubble");
            logf("moment woke: '%s' replies [%s] [%s] [%s]", narrow(a.bubble.text).c_str(), narrow(a.bubble.choices[0]).c_str(),
                 a.bubble.choices.size() > 1 ? narrow(a.bubble.choices[1]).c_str() : "", a.bubble.choices.size() > 2 ? narrow(a.bubble.choices[2]).c_str() : "");
            {
                std::wstring w = a.bubble.text;
                sim(0.3f, still);
                bool def = p.momentTouch("pet") && a.bubble.asking() && a.bubble.text == w && !p.touchPendingCat.empty();
                sim(1.0f, still);
                bool kept = a.bubble.asking() && a.bubble.text == w;
                sim(a.bubble.holdT + 0.3f, still);
                bool after = p.touchPendingCat.empty() && a.bubble.active && !a.bubble.asking() && a.bubble.text != w;
                logf("moment woke+pet: deferred %d, kept %d, then '%s'", (int)def, (int)kept, narrow(a.bubble.text).c_str());
                check(def && kept && after, "moment: petting as she wakes waits until her line with buttons is read");
                p.momentPet = false;
                a.bubble.close();
                p.lastSay = -100;
                p.sayCat("wakeRested", 1.f, L"", true);
            }
            sim(std::min(a.bubble.typeDur + 0.3f, 6.f), still);
            shot(L"96_reply_woke");
            std::string id = a.bubble.askId;
            p.answer(id, 0);
            check(a.bubble.active && !a.bubble.asking() && p.replyCount == 1, "moment: replied, she answers that");
            logf("moment reply: '%s'", narrow(a.bubble.text).c_str());
            {
                std::wstring replyText = a.bubble.text;
                bool deferred = p.momentTouch("pet") && a.bubble.text == replyText && !p.touchPendingCat.empty();
                sim(0.5f, still);
                bool stillThere = a.bubble.active && a.bubble.text == replyText;
                sim(a.bubble.holdT + 0.3f, still);
                bool after = p.touchPendingCat.empty() && a.bubble.active && a.bubble.text != replyText;
                logf("moment hold: deferred %d, reply still shown %d, then '%s'", (int)deferred, (int)stillThere, narrow(a.bubble.text).c_str());
                check(deferred && stillThere && after, "moment: petting while she answers waits until you read it");
                p.momentPet = false;
            }
            sim(std::min(a.bubble.typeDur + 0.3f, 6.f), still);
            shot(L"96b_reply_answer");
            a.bubble.close();
            check(p.momentTouch("pet") && !p.momentTouch("pet"), "moment: pet right after — about it, once");
            logf("moment pet: '%s'", narrow(a.bubble.text).c_str());
            a.bubble.close();
            p.lastSay = -100;
            p.kissT = -1;
            a.bubble.say(L"[re:None]Тэд опять разговаривал с дверью.");
            p.momentNote("idleWarm");
            check(p.moment == "Love" && !a.bubble.asking(), "moment: warm line marked None — no buttons");
            a.bubble.say(L"[re:Ask][blush]Можно я посижу поближе к твоему курсору?");
            p.momentNote("idleWarm");
            check(a.bubble.asking() && a.bubble.askId.rfind("reply:Ask:", 0) == 0 && a.bubble.text.find(L"re:") == std::wstring::npos &&
                      a.bubble.text.rfind(L"Можно", 0) == 0,
                  "moment: a request — request replies, tag hidden");
            logf("moment ask: '%s' [%s] [%s] [%s]", narrow(a.bubble.text).c_str(), narrow(a.bubble.choices[0]).c_str(), narrow(a.bubble.choices[1]).c_str(),
                 narrow(a.bubble.choices[2]).c_str());
            {
                std::string aid = a.bubble.askId;
                p.answer(aid, 0);
                check(a.bubble.active && p.moment == "Love", "moment: answered the request, she reacts");
                logf("moment ask reply: '%s'", narrow(a.bubble.text).c_str());
            }
            {
                int withNo = 0;
                for (int k = 0; k < 12; k++) {
                    a.bubble.close();
                    a.bubble.say(L"[re:Care]Уже поздно...| Ложись спать,| ладно?");
                    p.momentNote("idleWarm");
                    if (a.bubble.asking() && a.bubble.choices.size() == 3 && a.bubble.choices.back() == L"Не хочу") withNo++;
                }
                std::string id = a.bubble.askId;
                p.answer(id, 2);
                logf("care refuse: 'Не хочу' offered %d of 12, she says '%s'", withNo, narrow(a.bubble.text).c_str());
                check(withNo == 12 && a.bubble.active && !a.bubble.asking(), "moment: a request always has a way to say no");
                auto pool = a.phrases.list("replyWorry");
                int foreign = 0;
                for (int k = 0; k < 10; k++) {
                    a.bubble.close();
                    a.bubble.say(L"[re:Worry/0,1]Если ты когда-нибудь закроешь меня надолго,| я буду ждать.");
                    p.momentNote("idleWarm");
                    for (auto& c : a.bubble.choices)
                        if (c != a.phrases.fill(pool[0], L"") && c != a.phrases.fill(pool[1], L"")) foreign++;
                }
                a.bubble.close();
                a.bubble.say(L"[re:CareQ]Ты ел сегодня что-нибудь нормальное?");
                p.momentNote("idleWarm");
                bool hasNo = std::find(a.bubble.choices.begin(), a.bubble.choices.end(), std::wstring(L"Нет")) != a.bubble.choices.end();
                logf("reply subset: foreign buttons %d of 10 tries; care question has 'Нет' %d", foreign, (int)hasNo);
                check(foreign == 0 && hasNo, "moment: a line gets only the buttons that fit it");
                a.bubble.close();
            }
            a.bubble.close();
            p.lastSay = -100;
            p.momentKiss = false;
            a.bubble.say(L"[re:Compliment]Ты у меня лучший.");
            p.momentNote("idleWarm");
            check(a.bubble.asking() && a.bubble.askId.rfind("reply:Compliment:", 0) == 0, "moment: compliment — compliment replies");
            {
                int unmarked = 0, missing = 0;
                for (const char* c : {"idleWarm", "warmNight", "warmWork", "warmBack"})
                    for (int stg : {2, 3})
                    for (auto& l : a.phrases.listAt(c, stg))
                        if (l.find(L"[re:") == std::wstring::npos) {
                            unmarked++;
                            logf("moment: unmarked warm line in %s: '%s'", c, narrow(l).c_str());
                        }
                for (const char* k : {"Compliment", "Ask", "Care", "Worry", "Question", "Tease", "Hug"}) {
                    auto pool = a.phrases.list((std::string("reply") + k).c_str());
                    if (pool.size() < 3) missing++;
                    for (size_t i = 0; i < pool.size(); i++)
                        if (a.phrases.listAt((std::string("reply") + k + std::to_string(i)).c_str(), 2).empty()) {
                            missing++;
                            logf("moment: no reaction reply%s%zuHi", k, i);
                        }
                }
                check(unmarked == 0 && missing == 0, "moment: all warm lines marked, every button has an answer");
            }
            sim(a.bubble.holdT + 0.3f, still);
            V2 cheek = p.visorScreen(V2(0.8f, 0.15f));
            p.kiss(cheek);
            check(p.momentKiss && a.bubble.active && !a.bubble.asking(), "moment: kiss right after — about it");
            logf("moment kiss: '%s'", narrow(a.bubble.text).c_str());
            std::wstring mk = a.bubble.text;
            sim(1.2f, [&](float) { return cheek; });
            check(a.bubble.active && a.bubble.text == mk, "moment: the kiss grumble does not replace the line about it");
            logf("moment kiss after 1.2 s: '%s'", narrow(a.bubble.text).c_str());
            sim(1.3f, [&](float) { return cheek; });
            a.settings.replies = false;
            a.bubble.close();
            p.lastSay = -100;
            p.sayCat("missYouDay", 1.f, L"сутки", true);
            check(p.moment == "Miss" && !a.bubble.asking(), "moment: replies off — no buttons");
            p.momentAt = a.now - 40;
            check(p.momentCat("pet").empty(), "moment: half a minute later, not about it any more");
            a.settings.replies = rep0;
            a.bubble.close();
            p.vit.rel = rel0;
        }
        {
            float rel0 = p.vit.rel;
            p.vit.rel = 96.f;
            reset(St::Idle, 100);
            p.pos.x = 500.f;
            V2 cur(1250.f, 900.f);
            sim(0.5f, [&](float) { return cur; });
            a.bubble.close();
            p.emote("comesit", 0);
            bool walked = p.st == St::Walk;
            float tt = 0;
            for (; tt < 15.f && p.st == St::Walk; tt += 0.1f) sim(0.1f, [&](float) { return cur; });
            float gap = cur.x - p.pos.x;
            logf("come: walked %d, after %.1f s state %s at %.0f (cursor %.0f, gap %.0f), facing %d", (int)walked, tt, p.stateName(), p.pos.x, cur.x, gap, p.facing);
            check(walked && p.st == St::Sit && gap > 20.f && gap < 120.f && p.facing == 1, "action: [comesit] walks to the cursor and sits facing it");
            shot(L"97_comesit");
            size_t parts0 = p.parts.size();
            V2 nearPt = p.headC + V2(140.f, -40.f);
            sim(0.3f, [&](float) { return nearPt; });
            p.emote("smooch", 0);
            int marks = 0;
            for (auto& q : p.parts)
                if (q.type == 5 && (q.p - nearPt).len() < 60.f) marks++;
            check(p.parts.size() > parts0 && marks >= 1 && p.face.blush >= 0.f, "action: [smooch] — a kiss mark on the cursor");
            sim(0.4f, [&](float) { return nearPt; });
            shot(L"97b_smooch");
            a.bubble.close();
            reset(St::Idle, 100);
            sim(0.3f, still);
            p.emote("play", 0);
            sim(2.f, still);
            check(a.bubble.asking(), "action: [play] opens the game menu");
            logf("play: ask '%s' '%s'", a.bubble.askId.c_str(), narrow(a.bubble.text).c_str());
            a.bubble.close();
            p.vit.rel = rel0;
        }
        {
            float rel0 = p.vit.rel;
            p.vit.rel = 96.f;
            reset(St::Sit, 100);
            V2 c0 = p.headC + V2(90.f, -30.f);
            sim(1.f, [&](float) { return c0; });
            bool satBefore = p.st == St::Sit;
            p.emote("hug", 0);
            bool stood = p.st == St::Idle;
            bool grabbed = false;
            float dist = 1e9f;
            for (float tt = 0; tt < 2.2f; tt += 0.05f) {
                sim(0.05f, [&](float) { return p.hugSet ? p.hugAt : c0; });
                if (p.hugGrab) {
                    grabbed = true;
                    V2 chest = (p.shoulderScreen(1) + p.shoulderScreen(2)) * 0.5f + V2(0, p.headR * 0.55f);
                    dist = std::min(dist, (p.hugAt - chest).len());
                }
                if (grabbed && dist < 8.f && tt > 1.2f && tt < 1.3f) shot(L"99_hug_sitting");
            }
            sim(1.5f, still);
            logf("hug sitting: sat %d, stood up %d, cursor taken %d (closest %.1f px to her chest), sits again %d (%s)", (int)satBefore, (int)stood, (int)grabbed, dist,
                 (int)(p.st == St::Sit), p.stateName());
            check(satBefore && stood && grabbed && dist < 8.f && p.st == St::Sit && !p.hugGrab, "hug: sitting — stands, takes the cursor, sits back");
            p.vit.rel = rel0;
        }
        {
            float rel0 = p.vit.rel;
            bool rep0 = a.settings.replies;
            a.settings.replies = true;
            Input win;
            win.cursor = p.headC + V2(160.f, -20.f);
            win.idleSec = 10;
            auto start = [&](const char* kind) {
                reset(St::Idle, 100);
                p.kissT = -1;
                p.pendingSay.clear();
                sim(0.4f, [&](float) { return win.cursor; });
                a.bubble.close();
                p.lastSay = -100;
                p.wantEnd("test");
                p.wantKind = kind;
                p.wantPhase = 1;
                p.wantT = 0;
                float blush0 = p.face.blush;
                sim(1.4f, [&](float) { return win.cursor; });
                float shyBlush = p.face.blush;
                sim(1.2f, [&](float) { return win.cursor; });
                logf("want %s: shy blush %.2f -> %.2f, then '%s' ask '%s'", kind, blush0, shyBlush, narrow(a.bubble.text).c_str(), a.bubble.askId.c_str());
                return shyBlush > blush0 + 0.3f;
            };
            p.vit.rel = 30.f;
            p.nextWantAt = 1;
            reset(St::Idle, 100);
            sim(0.3f, still);
            a.bubble.close();
            p.lastSay = -100;
            check(!p.wantStep(win), "want: does not ask when she doesn't love you");
            p.vit.rel = 98.f;
            bool shy = start("Pet");
            bool asked = a.bubble.asking() && a.bubble.askId == "want:Pet";
            sim(std::min(a.bubble.typeDur + 0.2f, 5.f), [&](float) { return win.cursor; });
            shot(L"99e_want_pet");
            bool got = (p.wantTouched("pet") || p.wantPhase == 0) && p.tBlush > 3.f;
            logf("want pet got: '%s' (blush target %.1f, face %.2f)", narrow(a.bubble.text).c_str(), p.tBlush, p.face.blush);
            check(shy && asked && got, "want: shy first, asks to be petted, petting is the answer");
            start("Kiss");
            std::string kid = a.bubble.askId;
            p.answer(kid, 0);
            bool waits = p.wantPhase == 3 && p.tSquint > 5.f;
            sim(1.5f, [&](float) { return win.cursor; });
            shot(L"99f_want_kiss_wait");
            p.kissT = -1;
            p.kiss(p.visorScreen(V2(0.8f, 0.15f)));
            bool kissed = p.wantPhase == 0 && a.bubble.active;
            logf("want kiss: waits %d, then '%s'", (int)waits, narrow(a.bubble.text).c_str());
            check(kid == "want:Kiss" && waits && kissed, "want: asks for a kiss, closes her eyes, the kiss is the answer");
            start("Hug");
            win.cursor = p.headC + V2(500.f, 0);
            std::string hid = a.bubble.askId;
            p.lastCursor = win.cursor;
            p.answer(hid, 0);
            bool hugged = false;
            for (float tt = 0; tt < 10.f && !hugged; tt += 0.1f) {
                sim(0.1f, [&](float) { return win.cursor; });
                hugged = p.emHugT > 0;
            }
            logf("want hug: hugged %d, state %s", (int)hugged, p.stateName());
            check(hid == "want:Hug" && hugged, "want: asks for a hug, comes over and hugs");
            {
                reset(St::Idle, 100);
                p.kissT = -1;
                p.pendingSay.clear();
                sim(0.3f, [&](float) { return win.cursor; });
                a.bubble.close();
                p.wantEnd("test");
                p.wantKind = "Pet";
                p.wantPhase = 1;
                p.wantT = 0;
                a.bubble.say(L"Это длинная реплика, которую ещё читают. Совсем-совсем длинная, правда-правда, честное слово.");
                std::wstring said = a.bubble.text;
                sim(2.8f, [&](float) { return win.cursor; });
                bool waited = p.wantPhase == 1 && a.bubble.text == said;
                sim(6.f, [&](float) { return win.cursor; });
                bool askedAfter = a.bubble.asking() && a.bubble.askId == "want:Pet";
                logf("want busy: waited %d, asked after it was read %d ('%s')", (int)waited, (int)askedAfter, narrow(a.bubble.text).c_str());
                check(waited && askedAfter, "want: waits until her other line is read, then asks");
                a.bubble.close();
                p.wantEnd("test");
            }
            win.cursor = p.headC + V2(160.f, -20.f);
            start("Pet");
            std::string lid = a.bubble.askId;
            p.answer(lid, 2);
            logf("want later: '%s'", narrow(a.bubble.text).c_str());
            check(p.wantPhase == 0 && a.bubble.active, "want: «later» is fine, no hard feelings");
            start("Ask");
            bool qAsk = a.bubble.asking() && a.bubble.askId.rfind("reply:Q", 0) == 0;
            sim(std::min(a.bubble.typeDur + 0.2f, 5.f), [&](float) { return win.cursor; });
            shot(L"99g_want_question");
            std::string qid = a.bubble.askId;
            p.answer(qid, 0);
            logf("want question: '%s' -> '%s'", qid.c_str(), narrow(a.bubble.text).c_str());
            check(qAsk && a.bubble.active && !a.bubble.asking(), "want: shy question with answers");
            a.bubble.close();
            a.settings.replies = rep0;
            p.vit.rel = rel0;
        }
        {
            reset(St::Idle, 100);
            sim(0.3f, still);
            a.bubble.close();
            std::wstring longLine = L"Я только открыла визор,| а тут ты.| ^Лучшее утро.^| Правда-правда,| я не шучу.";
            a.bubble.say(longLine);
            std::wstring shown = a.bubble.text;
            sim(1.0f, still);
            p.lastSay = -100;
            p.sayCat("purr", 1.f, L"", true);
            p.sayCat("idle", 1.f, L"", false);
            bool kept = a.bubble.text == shown && !p.pendingSay.empty();
            float readAt = a.bubble.readT;
            logf("hold: line typed %.1f s, read by %.1f s, life %.1f; purr queued %d", a.bubble.typeDur, readAt, a.bubble.life, (int)!p.pendingSay.empty());
            sim(std::max(0.f, readAt - a.bubble.t) - 0.3f, still);
            bool stillShown = a.bubble.text == shown;
            sim(0.6f, still);
            bool purred = a.bubble.text != shown && p.pendingSay.empty();
            logf("hold: kept %d, still shown just before read time %d, then '%s'", (int)kept, (int)stillShown, narrow(a.bubble.text).c_str());
            check(kept && stillShown && purred && readAt > a.bubble.typeDur, "hold: her line is read to the end, the purr comes after");
            a.bubble.close();
        }
        {
            auto profile = [&](const UziHitbox& h, std::vector<V2>& pts) {
                pts.clear();
                for (int i = 0; i < h.cols; i++)
                    if (h.top[i] >= 0) pts.push_back(V2(h.colX0 + (i + 0.5f) * h.colW, h.top[i]));
            };
            std::vector<V2> dots;
            std::vector<D2D1_RECT_F> boxes;
            g_snapUnder = [&](DrawCtx& c, V2 wo) {
                c.color(D2D1::ColorF(1, 0.1f, 0.1f, 1));
                for (auto& q : dots) c.rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(q.x - wo.x, q.y - wo.y - 3.f), 2.f, 2.f), c.br);
                c.color(D2D1::ColorF(0.1f, 0.6f, 1, 1), 0.7f);
                for (auto& r : boxes) c.rt->DrawRectangle(D2D1::RectF(r.left - wo.x, r.top - wo.y, r.right - wo.x, r.bottom - wo.y), c.br, 1.f);
            };
            float heights[3] = {};
            const St poses[3] = {St::Idle, St::Sit, St::Sleep};
            const wchar_t* names[3] = {L"99b_hitbox_stand", L"99c_hitbox_sit", L"99d_hitbox_sleep"};
            bool okAll = true;
            for (int k = 0; k < 3; k++) {
                reset(poses[k], 100);
                sim(0.8f, still);
                a.bubble.close();
                if (k == 0) a.bubble.say(L"Облачко не тело.");
                sim(0.3f, still);
                UziHitbox h;
                p.hitboxFill(h, a.winOrigin);
                profile(h, dots);
                boxes.clear();
                for (int i = 0; i < h.nparts; i++) boxes.push_back(D2D1::RectF(h.parts[i][0], h.parts[i][1], h.parts[i][2], h.parts[i][3]));
                heights[k] = h.bounds[3] - h.bounds[1];
                bool headIn = p.headC.x > h.bounds[0] && p.headC.x < h.bounds[2] && p.headC.y > h.bounds[1] && p.headC.y < h.bounds[3];
                bool feet = k == 1 || fabsf(h.bounds[3] - h.groundY) < 14.f;
                bool bubbleApart = k != 0 || ((h.flags & UziHitBubble) && h.bubble[1] < h.bounds[1] - 20.f);
                int filled = 0;
                for (int i = 0; i < h.cols; i++) filled += h.top[i] >= 0;
                logf("hitbox %s: visible %d bounds %.0f,%.0f..%.0f,%.0f (%.0fx%.0f) head in %d feet %.1f px from ground, %d/%d columns, %d parts, bubble %d %.0f..%.0f",
                     narrow(names[k]).c_str(), (int)(h.flags & UziHitVisible), h.bounds[0], h.bounds[1], h.bounds[2], h.bounds[3], h.bounds[2] - h.bounds[0], heights[k],
                     (int)headIn, h.bounds[3] - h.groundY, filled, h.cols, h.nparts, (int)((h.flags & UziHitBubble) != 0), h.bubble[1], h.bubble[3]);
                okAll &= (h.flags & UziHitVisible) && headIn && feet && bubbleApart && filled > 40 && h.nparts >= 1;
                shot(names[k]);
            }
            g_snapUnder = nullptr;
            a.bubble.close();
            check(okAll && heights[1] < heights[0] && heights[2] < heights[1], "hitbox: her body only, standing > sitting > lying");
            {
                const wchar_t* keepName = g_hitboxName;
                g_hitboxName = L"Local\\UziPetHitboxSnapshot";
                reset(St::Idle, 100);
                sim(0.5f, still);
                UziHitbox h;
                p.hitboxFill(h, a.winOrigin);
                p.hitboxPublish(a.winOrigin);
                std::string js;
                int rc = hitboxJson(g_hitboxName, js);
                char want[64];
                snprintf(want, sizeof(want), "\"bounds\":[%.1f,", h.bounds[0]);
                bool ok = rc == 0 && js.find("\"visible\":true") != std::string::npos && js.find(want) != std::string::npos && js.find("\"top\":[") != std::string::npos;
                logf("hitbox json (%d, %zu bytes): %s", rc, js.size(), js.substr(0, 220).c_str());
                check(ok, "hitbox: shared memory and --hitbox JSON");
                std::string none;
                check(hitboxJson(L"Local\\UziPetHitboxNobody", none) == 1, "hitbox: not running -> code 1");
                g_hitboxName = keepName;
            }
        }
        {
            float rel0 = p.vit.rel;
            p.vit.rel = 96.f;
            bool rep0 = a.settings.replies;
            a.settings.replies = true;
            int nTalks = (int)talkIds().size(), broken = talksBroken();
            logf("talks: %d loaded, %d broken buttons", nTalks, broken);
            check(nTalks >= 20 && broken == 0, "talks: loaded, every button leads somewhere");
            reset(St::Idle, 100);
            sim(0.3f, still);
            int finished = 0, longest = 0;
            for (int k = 0; k < 6; k++) {
                a.bubble.close();
                p.lastSay = -100;
                if (!p.talkStartRandom()) break;
                std::string line = narrow(a.bubble.text);
                int steps = 0;
                while (a.bubble.asking() && a.bubble.askId.rfind("talk:", 0) == 0 && steps < 10) {
                    std::string id = a.bubble.askId;
                    int ch = randi(0, (int)a.bubble.choices.size() - 1);
                    line += " | [" + narrow(a.bubble.choices[ch]) + "] ";
                    p.answer(id, ch);
                    line += narrow(a.bubble.text);
                    steps++;
                }
                logf("talk run %d (%d steps): %s", k, steps, line.c_str());
                if (steps > 0 && steps < 10) finished++;
                longest = std::max(longest, steps);
            }
            check(finished >= 5, "talks: she starts one, buttons lead to an end");
            if (finished) {
                a.bubble.close();
                p.lastSay = -100;
                p.talkStartRandom();
                sim(std::min(a.bubble.typeDur + 0.4f, 6.f), still);
                shot(L"98_talk");
            }
            int linked = 0, opened = 0;
            for (size_t i = 0; i < a.phrases.keys.size(); i++)
                for (auto& l : a.phrases.vals[i])
                    if (l.find(L"[th:") != std::wstring::npos) {
                        linked++;
                        if (opened) continue;
                        a.bubble.close();
                        a.bubble.say(a.phrases.fill(l, L""));
                        p.momentNote(a.phrases.keys[i].c_str());
                        if (a.bubble.asking() && a.bubble.askId.rfind("talk:", 0) == 0) opened++;
                    }
            logf("talks: %d linked lines in phrases", linked);
            check(linked >= 8 && opened == 1, "talks: a linked story line opens its talk");
            a.bubble.close();
            a.settings.replies = rep0;
            p.vit.rel = rel0;
        }
        {
            Phrases& ph = a.phrases;
            int leakToHate = 0, leakFromHate = 0;
            for (auto& k : ph.keys) {
                std::string c = k.substr(k.find('/') + 1);
                std::string l0 = ph.layerOf(c.c_str(), 0);
                if (!l0.empty() && l0 != "hate" && l0 != "any") leakToHate++;
                for (int s = 1; s < 4; s++)
                    if (ph.layerOf(c.c_str(), s) == "hate") leakFromHate++;
            }
            ph.keys.push_back("warm/testFallback");
            ph.vals.push_back({L"тёплая"});
            bool fallbackOk = ph.layerOf("testFallback", 3) == "warm" && ph.layerOf("testFallback", 0).empty();
            ph.keys.pop_back();
            ph.vals.pop_back();
            ph.reindex();
            bool layers = ph.stages.size() == 4 && ph.layerOf("comeYes", 0) == "hate" && ph.layerOf("comeYes", 1) == "any" &&
                          ph.layerOf("cornerAsk", 3) == "love" && fallbackOk && ph.layerOf("fileStumble", 1) == "good";
            logf("stages: %zu stages, %zu keys, comeYes %s/%s, kissNotYet@love from %s, leaks to hate %d from hate %d", ph.stages.size(), ph.keys.size(),
                 ph.layerOf("comeYes", 0).c_str(), ph.layerOf("comeYes", 1).c_str(), (fallbackOk ? "warm" : "?"), leakToHate, leakFromHate);
            check(layers && leakToHate == 0 && leakFromHate == 0, "stages: phrases come from the stage's folder, hate never shares");
            reset(St::Idle, 100);
            sim(0.2f, still);
            float rel0 = p.vit.rel;
            auto from = [&](float rel, const char* cat, int st) {
                a.bubble.close();
                p.lastSay = -100;
                p.vit.rel = rel;
                p.sayCat(cat, 1.f, L"", true);
                std::wstring said = a.bubble.text;
                for (auto& l : ph.listAt(cat, st)) {
                    a.bubble.say(ph.fill(l, L""));
                    if (a.bubble.text == said) return true;
                }
                return false;
            };
            bool hate = from(10.f, "comeYes", 0), good = from(50.f, "comeYes", 1), love = from(99.f, "fileStumble", 3), warm = from(80.f, "fileStumble", 2);
            logf("stages: said from hate %d good %d warm %d love %d", (int)hate, (int)good, (int)warm, (int)love);
            check(hate && good && warm && love, "stages: the same category speaks from the current stage");
            a.bubble.close();
            p.vit.rel = rel0;
            p.syncStage();
        }
        {
            reset(St::Idle, 100);
            sim(0.3f, still);
            a.bubble.close();
            p.setState(St::Watch, 1e9f);
            int untagged = 0;
            for (int stg = 0; stg < 4; stg++)
                for (auto& l : a.phrases.listAt("replyYawn1", stg))
                    if (l.find(L"[nap]") == std::wstring::npos) untagged++;
            a.bubble.say(L"[heart]Правда?| ...Тогда я посплю спокойно.| С тобой не страшно.[nap]");
            sim(a.bubble.typeDur + 0.3f, still);
            bool waits = p.napAt >= 0 && p.st != St::Sleep;
            sim(a.bubble.holdT - a.bubble.t + 0.5f, still);
            bool slept = p.st == St::Sleep;
            logf("nap: untagged %d, waits while read %d, asleep after %d (state %s)", untagged, (int)waits, (int)slept, p.stateName());
            check(untagged == 0 && waits && slept, "nap: says she'll sleep — lies down once the line is read");
            a.bubble.close();
            reset(St::Idle, 100);
        }
        {
            a.world.setFake(RECT{0, 0, 1920, 1080}, RECT{0, 0, 1920, 1020});
            a.world.winFloors = true;
            HWND fw = (HWND)(INT_PTR)0x5151;
            a.world.addFakeWindow(fw, RECT{500, 60, 1400, 820}, false);
            bool has = false;
            for (auto& s : a.world.surfs)
                if (s.kind == SurfKind::WinFloor && s.hwnd == fw && fabsf(s.y - 818.f) < 1.f) has = true;
            reset(St::Idle, 100);
            p.pos = V2(900, 500);
            p.vel = {};
            p.startFallNow();
            sim(1.0f, still);
            bool dropIn = p.gnd.valid && p.gnd.kind == SurfKind::WinFloor && fabsf(p.pos.y - 818.f) < 2.f;
            shot(L"99h_win_floor");
            reset(St::Idle, 100);
            p.pos = V2(900, 30);
            p.vel = {};
            p.startFallNow();
            sim(1.4f, still);
            bool through = p.gnd.valid && p.gnd.kind == SurfKind::Floor;
            reset(St::Idle, 100);
            p.pos.x = 700;
            p.jumpWin = fw;
            bool jumped = p.jumpNow(V2(1000, 818));
            sim(1.8f, still);
            bool jumpIn = jumped && p.gnd.valid && p.gnd.kind == SurfKind::WinFloor;
            p.setState(St::Idle, 100);
            float x0 = p.pos.x;
            for (int k = 0; k < 20; k++) {
                a.world.fakeWins.back().r.left += 5;
                a.world.fakeWins.back().r.right += 5;
                sim(0.05f, still);
            }
            float xRode = p.pos.x;
            bool rode = p.gnd.valid && p.gnd.kind == SurfKind::WinFloor && fabsf(p.pos.x - x0 - 100.f) < 4.f;
            p.setState(St::Idle, 100);
            bool outJ = p.jumpNow(V2(p.pos.x + 150.f, 1020.f));
            sim(1.8f, still);
            bool jumpedOut = outJ && p.gnd.valid && p.gnd.kind == SurfKind::Floor;
            reset(St::Idle, 100);
            V2 jt;
            int ji = -1;
            for (int k = 0; k < 20; k++) p.pickJumpNow(jt, ji);
            p.setState(St::Sit, 100);
            p.pos = V2(900, 30);
            p.vel = {};
            p.startFallNow();
            sim(1.4f, still);
            bool noStale = p.gnd.valid && p.gnd.kind == SurfKind::Floor;
            logf("win floor: surface %d, dropped inside -> lands in it %d, falls past from above %d, jumps in %d, rides with the window %d, jumps out to the floor %d, "
                 "no stale aim %d (ride %.0f -> %.0f)",
                 (int)has, (int)dropIn, (int)through, (int)jumpIn, (int)rode, (int)jumpedOut, (int)noStale, x0, xRode);
            check(has && dropIn && through && jumpIn && rode && jumpedOut && noStale, "win floor: she can stand inside a window, on its bottom");
            a.world.setFake(RECT{0, 0, 1920, 1080}, RECT{0, 0, 1920, 1020});
            reset(St::Idle, 100);
        }
        {
            reset(St::Idle, 100);
            float rel0 = p.vit.rel;
            p.vit.rel = 50.f;
            sim(0.3f, still);
            a.bubble.close();
            V2 face = p.headC;
            a.bubble.say(L"Это... это не я.| ...Ладно,| я.| Всё,| уже кладу!| Честно-честно,| больше не буду трогать твои файлы,| правда.");
            bool bitWhileReading = false;
            for (int k = 0; k < 60 && a.bubble.held(); k++) {
                sim(0.1f, [&](float) { return face; });
                bitWhileReading |= p.st == St::Bite;
            }
            a.bubble.close();
            p.lastSay = -100;
            bool warnedSeen = false, bit = false;
            for (int k = 0; k < 80 && !bit; k++) {
                sim(0.1f, [&](float) { return face; });
                if (a.bubble.active && p.lingerWarnAt > 0 && a.now - p.lingerWarnAt < 0.2) warnedSeen = true;
                bit = p.st == St::Bite || p.biteHop;
            }
            logf("linger: bit while reading %d, warning shown %d, then bit %d", (int)bitWhileReading, (int)warnedSeen, (int)bit);
            check(!bitWhileReading && warnedSeen && bit, "linger: no bite while you read her line; otherwise a visible warning first");
            reset(St::Idle, 100);
            p.vit.rel = 96.f;
            p.lingerLoveAt = -100;
            p.biteHop = false;
            p.vengeT = 0;
            sim(0.3f, still);
            a.bubble.close();
            p.lastSay = -100;
            V2 face2 = p.headC;
            bool bitLove = false;
            for (int k = 0; k < 60; k++) {
                sim(0.1f, [&](float) { return face2; });
                bitLove |= p.st == St::Bite || p.biteHop || p.st == St::Leap || p.st == St::Cling;
            }
            bool reacted = p.lingerLoveAt > 0;
            logf("linger at love: bit %d, reacted %d, said '%s'", (int)bitLove, (int)reacted, narrow(a.bubble.text).c_str());
            check(!bitLove && reacted, "linger at love: flustered or snuggles, never bites");
            p.vit.rel = rel0;
            reset(St::Idle, 100);
        }
        {
            auto asleep = [&](float rel) {
                reset(St::Idle, 100);
                p.vit.rel = rel;
                p.vit.energy = 40.f;
                p.groggyT = 0;
                p.sleepNow();
                sim(0.5f, still);
                a.bubble.close();
            };
            asleep(80.f);
            float rel0 = p.vit.rel;
            p.answer("talkSleep", 0);
            sim(1.0f, still);
            bool stirring = p.st == St::Sleep && p.stirT >= 0;
            sim(5.0f, still);
            bool up = p.st != St::Sleep, noPenalty = fabsf(p.vit.rel - rel0) < 0.01f, groggy = p.groggyT > 100.f;
            std::string woke = narrow(a.bubble.text);
            bool gentleLine = false;
            for (auto& l : a.phrases.list("wakeGentle"))
                if (narrow(a.phrases.fill(l, L"")).find(woke.substr(0, std::min<size_t>(woke.size(), 6))) != std::string::npos) gentleLine = true;
            up = up && gentleLine;
            shot(L"99i_gentle_wake");
            asleep(80.f);
            p.vit.food = 40.f;
            p.vit.stock = 2;
            p.batteryDrop();
            bool notInSleep = p.vit.stock == 2 && p.st == St::Sleep;
            sim(6.0f, still);
            bool ate = p.vit.stock == 1 && p.vit.food > 40.f && p.st != St::Sleep;
            asleep(80.f);
            p.vit.stock = 1;
            p.tHappy = 0;
            p.parcelOpened();
            bool quietParcel = p.vit.stock == 4 && p.tHappy <= 0 && !a.bubble.active;
            asleep(80.f);
            p.offerUpdate(L"9.9.9", 1);
            bool noAskAsleep = !a.bubble.asking();
            bool asked = false;
            for (int k = 0; k < 120 && !asked; k++) {
                sim(0.1f, still);
                asked = a.bubble.asking() && a.bubble.askId == "update";
            }
            a.bubble.close();
            logf("gentle wake: stirring %d, up %d, no penalty %d, groggy %d ('%s'); battery: not eaten asleep %d, ate after waking %d; parcel quiet %d; "
                 "update: not asked asleep %d, asked after %d",
                 (int)stirring, (int)up, (int)noPenalty, (int)groggy, woke.c_str(), (int)notInSleep, (int)ate, (int)quietParcel, (int)noAskAsleep, (int)asked);
            check(stirring && up && noPenalty && groggy && notInSleep && ate && quietParcel && noAskAsleep && asked,
                  "gentle wake: slow, no penalty, groggy after; nothing eaten or installed in her sleep");
            p.groggyT = 0;
            reset(St::Idle, 100);
        }
        {
            reset(St::Idle, 100);
            sim(0.3f, still);
            a.bubble.close();
            a.bubble.say(L"Ты пришёл... Я правда очень-очень скучала, честно-честно.");
            sim(0.5f, still);
            bool asked = p.ask("monitorAsk", "choiceMonitor", "monitor");
            check(!asked && !a.bubble.asking(), "ask: her own question does not cut a line being read");
            bool nm = p.ask("nightmareWake", "choiceNightmare", "nightmare");
            check(nm && a.bubble.asking(), "ask: a question answering your action still goes through");
            a.bubble.close();
            a.bubble.say(L"Коротко.");
            sim(0.4f, still);
            sim(0.05f, still);
            bool keep0 = a.bubble.hoverKeep;
            a.bubble.hoverKeep = true;
            float life0 = a.bubble.life;
            V2 mid((a.bubble.rect.left + a.bubble.rect.right) * 0.5f, (a.bubble.rect.top + a.bubble.rect.bottom) * 0.5f);
            for (int k = 0; k < 200; k++) {
                a.bubble.mouse = mid;
                a.bubble.update(0.05f);
                a.now += 0.05;
            }
            bool stayed = a.bubble.active && a.bubble.held();
            a.bubble.hide();
            for (int k = 0; k < 20; k++) {
                a.bubble.mouse = mid;
                a.bubble.update(0.05f);
            }
            bool clickClosed = !a.bubble.active;
            a.bubble.say(L"Коротко.");
            for (int k = 0; k < 200; k++) {
                a.bubble.mouse = V2(-1e5f, -1e5f);
                a.bubble.update(0.05f);
            }
            bool goneAway = !a.bubble.active;
            logf("hover: life %.1f, stayed 10 s %d, click closes %d, without cursor gone %d", life0, (int)stayed, (int)clickClosed, (int)goneAway);
            check(stayed && clickClosed && goneAway, "hover: cursor on the bubble keeps it, a click still closes it");
            a.bubble.hoverKeep = keep0;
            a.bubble.close();
            p.lastSay = -100;
            int here = a.world.monitorAt(p.pos - V2(0, 4));
            p.liftMon = here;
            bool ok = p.ask("monitorAsk", "choiceMonitor", "monitor");
            sim(0.2f, still);
            std::string said = narrow(a.bubble.text);
            logf("monitor early: asked %d, liftMon %d, asking %d, said '%s'", (int)ok, p.liftMon, (int)a.bubble.asking(), said.c_str());
            check(ok && p.liftMon == -1 && !a.bubble.asking() && a.bubble.active && a.bubble.askId.empty(), "monitor: moved before answering, question gone, she notices");
            a.bubble.close();
        }
        logf("alive tests: %d failed", fails);
    }
