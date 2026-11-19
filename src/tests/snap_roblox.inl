
    {
        int fails = 0;
        auto check = [&](bool ok, const std::string& what) {
            if (!ok) fails++;
            logf("rbx test %s: %s", ok ? "ok" : "FAIL", what.c_str());
        };
        std::vector<std::string> sample;
        {
            std::string all;
            std::wstring f = dir + L"\\..\\rbx_sample.log";
            if (!readFile(f, all)) logf("rbx test: no '%s'", narrow(f).c_str());
            for (size_t pos = 0; pos < all.size();) {
                size_t e = all.find('\n', pos);
                if (e == std::string::npos) e = all.size();
                std::string ln = all.substr(pos, e - pos);
                pos = e + 1;
                if (!ln.empty() && ln.back() == '\r') ln.pop_back();
                if (!ln.empty()) sample.push_back(ln);
            }
        }
        {
            RbxSense s;
            std::string got;
            int places = 0;
            for (auto& ln : sample) {
                RbxLog l = rbxParseLine(ln);
                if (l.k == RbxLog::Place) places++;
                if (l.k == RbxLog::Join) logf("rbx test: line -> place %lld universe %lld at %.3f", l.place, l.universe, l.t);
                for (auto& e : s.feed(l)) {
                    logf("rbx test event: %s", rbxDescribe(e).c_str());
                    got += e.join ? "join " + std::to_string(e.universe) + " " : "leave " + std::to_string((int)e.played) + " ";
                }
            }
            check(got == "join 10770563082 leave 54 join 7699580568 leave 46 ", "real log: two games, teleports inside a game ignored (" + got + ")");
            check(places == 4 && !s.inGame(), "real log: four 'Joining game' lines, out at the end");
            check(rbxLogTime("2026-11-09T11:30:16.102Z,68.1") > 1.7e9 && fabs(rbxLogTime("2026-11-09T11:30:16.102Z") - rbxLogTime("2026-11-09T11:30:15.102Z") - 1) < 1e-3,
                  "log time");
            RbxLog j;
            j.k = RbxLog::Join;
            j.universe = 7699580568;
            j.place = 1;
            j.t = rbxLogTime("2026-11-09T11:42:00.000Z");
            auto ev = s.feed(j);
            check(ev.size() == 1 && ev[0].again, "same game within 30 min: again");
            RbxLog o;
            o.k = RbxLog::Join;
            o.universe = 42;
            o.t = j.t + 60;
            ev = s.feed(o);
            check(ev.size() == 2 && !ev[0].join && ev[0].switched && ev[1].join && !ev[1].again, "teleport into another game: switched, not 'played enough'");
            ev = s.gone(o.t + 25 * 60);
            check(ev.size() == 1 && !ev[0].join && !ev[0].switched && ev[0].played == 25 * 60, "Roblox closed: leave after 25 min");
            RbxLog p;
            p.k = RbxLog::Place;
            p.place = 777;
            p.t = o.t + 3000;
            s.feed(p);
            check(s.flush(p.t + 5).empty() && s.flush(p.t + 11).size() == 1, "'Joining' without universe: join by place after 10 s");
        }
        {
            std::wstring fl = dir + L"\\fake_rbx_logs";
            CreateDirectoryW(fl.c_str(), nullptr);
            std::wstring f1 = fl + L"\\0.745.0.7451137_20261109T112908Z_Player_1FA0F_last.log";
            std::wstring crash = fl + L"\\0.745.0.7451137_20261109T112909Z_Player_B51BF_CrashHandler_last.log";
            size_t half = 0;
            for (size_t i = 0; i < sample.size(); i++)
                if (sample[i].find("returnToLuaApp: (stage:UGCGame)") != std::string::npos) { half = i; break; }
            std::string head, rest;
            for (size_t i = 0; i < sample.size(); i++) (i < half ? head : rest) += sample[i] + "\r\n";
            writeFile(f1, head);
            std::string joinPair;
            for (size_t i = 0; i + 1 < sample.size() && joinPair.empty(); i++)
                if (rbxParseLine(sample[i]).k == RbxLog::Place) joinPair = sample[i] + "\r\n" + sample[i + 1] + "\r\n";
            writeFile(crash, joinPair);
            RbxLogTail tail;
            tail.dir = fl;
            RbxSense s;
            std::string live;
            auto run = [&]() {
                std::vector<std::pair<std::string, bool>> lines;
                tail.poll(lines);
                int quiet = 0;
                for (auto& [ln, q] : lines) {
                    quiet += q;
                    for (auto& e : s.feed(rbxParseLine(ln)))
                        if (!q) live += e.join ? "join " + std::to_string(e.universe) + " " : "leave ";
                }
                return std::make_pair((int)lines.size(), quiet);
            };
            auto r1 = run();
            check(r1.first == (int)half && r1.second == r1.first && live.empty() && s.inGame(), "tail: old file read quietly, already in a game");
            check(tail.watching().size() == 1, "tail: crash handler log skipped");
            size_t cut = rest.size() / 2;
            cut = rest.find("Joining game", cut) == std::string::npos ? cut : rest.find("Joining game", cut) + 5;
            HANDLE h = CreateFileW(f1.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            DWORD w = 0;
            WriteFile(h, rest.data(), (DWORD)cut, &w, nullptr);
            auto r2 = run();
            WriteFile(h, rest.data() + cut, (DWORD)(rest.size() - cut), &w, nullptr);
            auto r3 = run();
            CloseHandle(h);
            check(r2.second == 0 && r3.second == 0 && r2.first + r3.first == (int)(sample.size() - half), "tail: appended lines come live, split line glued");
            check(live == "leave join 7699580568 leave ", "tail: leave, next game, leave (" + live + ")");
            writeFile(fl + L"\\0.746.0.7461267_20261109T153229Z_Player_C1BA5_last.log", joinPair);
            live.clear();
            run();
            check(live.rfind("join 10770563082", 0) == 0, "tail: new log file read from the start (" + live + ")");
        }
        {
            RbxLogTail tail;
            PWSTR lp = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &lp))) tail.dir = std::wstring(lp) + L"\\Roblox\\logs";
            CoTaskMemFree(lp);
            std::vector<std::pair<std::string, bool>> lines;
            tail.poll(lines);
            RbxSense s;
            int joins = 0;
            for (auto& [ln, q] : lines)
                for (auto& e : s.feed(rbxParseLine(ln)))
                    if (e.join) {
                        joins++;
                        logf("rbx test real logs: join universe %lld place %lld", e.universe, e.place);
                    }
            for (auto& n : tail.watching()) logf("rbx test real logs: watching '%s'", narrow(n).c_str());
            logf("rbx test real logs: %d lines, %d joins, in a game now %d, Roblox running %d", (int)lines.size(), joins, (int)s.inGame(), (int)robloxRunning());
        }
        {
            std::string body;
            readFile(dir + L"\\..\\rbx_games_sample.json", body);
            RbxName n1, n2, n0;
            bool p1 = rbxParseGames(body, 10770563082LL, n1), p2 = rbxParseGames(body, 7699580568LL, n2), p0 = rbxParseGames(body, 5LL, n0);
            logf("rbx test api: '%s' [%s]; '%s' [%s]", n1.name.c_str(), n1.genre.c_str(), n2.name.c_str(), n2.genre.c_str());
            check(p1 && n1.name == "join the group to play" && n1.genre.find("Shooter") != std::string::npos && p2 && !p0, "api answer parsed, unknown id refused");
            check(!rbxParseGames("{\"data\":[{\"id\":0,\"name\":\"[TITLE UNAVAILABLE]\"}]}", 1, n0), "api: closed game refused");
            check(!strcmp(rbxKind(n1.name, n1.genre), "rbxFight") && !strcmp(rbxKind(n2.name, n2.genre), "rbxGrind") && rbxShortName(n2.name) == L"Grass Incremental Simulator",
                  "api names: shooter by genre, simulator by word, tag and emoji gone");
            struct K { const char* name; const char* genre; const char* cat; const wchar_t* shortName; };
            K ks[] = {
                {"DOORS \xF0\x9F\x91\x81\xEF\xB8\x8F", "", "rbxHorror", L"DOORS"},
                {"[UPD 3] Blox Fruits", "", "rbxGrind", L"Blox Fruits"},
                {"\xF0\x9F\x8F\xA1 Brookhaven \xF0\x9F\x8F\xA1RP", "", "rbxCozy", L"Brookhaven RP"},
                {"Piggy: Book 2", "", "rbxHorror", L"Piggy: Book 2"},
                {"Pressure Washing Simulator", "", "rbxGrind", L"Pressure Washing Simulator"},
                {"Outdoors Adventure", "Adventure/Exploration/All", "rbxJoin", L"Outdoors Adventure"},
                {"[\xE2\x9A\x94\xEF\xB8\x8F] RIVALS", "Shooter", "rbxFight", L"RIVALS"},
                {"Natural Disaster Survival", "", "rbxDisaster", L"Natural Disaster Survival"},
                {"Tower of Hell [CHRISTMAS]", "", "rbxObby", L"Tower of Hell"},
                {"Murder Mystery 2", "", "rbxFight", L"Murder Mystery 2"},
                {"99 Nights in the Forest \xF0\x9F\x94\xA6", "", "rbxHorror", L"99 Nights in the Forest"},
                {"Some {arg} %s |weird^ ~name~", "", "rbxJoin", L"Some arg s weird name"},
                {"Super Mega Long Experience Name That Never Ends Really", "", "rbxJoin", L"Super Mega Long Experience\x2026"},
                {"[\xF0\x9F\x8E\x83]", "", "rbxJoin", L""},
                {"Killer Chase", "Survival/1 vs All/All", "rbxHorror", L"Killer Chase"},
                {"Zombie Camp", "Survival/Survival/All", "rbxJoin", L"Zombie Camp"},
            };
            for (auto& k : ks) {
                std::wstring sn = rbxShortName(k.name);
                const char* c = rbxKind(k.name, k.genre);
                check(!strcmp(c, k.cat) && sn == k.shortName, std::string("kind '") + k.name + "' -> " + c + " '" + narrow(sn) + "'");
            }
            std::map<long long, RbxName> fake = {{10770563082LL, n1}, {7699580568LL, n2}, {111, {"DOORS \xF0\x9F\x91\x81\xEF\xB8\x8F", "Adventure/Horror/Horror"}},
                                                 {222, {"Brookhaven \xF0\x9F\x8F\xA1RP", "Roleplay & Avatar Sim/Life/Town and City"}},
                                                 {333, {"Kebab Shop Delivery Experience 3000", "Party & Casual/Minigame/All"}}};
            int calls = 0;
            RbxNames names;
            names.file = appDataDir() + L"\\roblox_names.txt";
            DeleteFileW(names.file.c_str());
            names.fetch = [&](long long u, RbxName& out) {
                calls++;
                auto it = fake.find(u);
                if (it == fake.end()) return false;
                out = it->second;
                return true;
            };
            RbxName r;
            double t0 = 1.79e9;
            bool g1 = names.get(111, r, t0), g2 = names.get(111, r, t0 + 60), g3 = names.get(999, r, t0);
            check(g1 && g2 && !g3 && calls == 2 && r.name.rfind("DOORS", 0) == 0, "names: fetched once, then from memory; unknown -> nothing");
            RbxNames again;
            again.file = names.file;
            again.fetch = [&](long long, RbxName&) { calls++; return false; };
            RbxName r2;
            check(again.get(111, r2, t0 + 3600) && r2.name == r.name && r2.genre == r.genre && calls == 2, "names: cache file read back without network");
            check(again.get(111, r2, t0 + 8 * 86400) && calls == 3, "names: week-old name refetched, kept when network fails");
            RbxVoice v;
            double now = 1000;
            auto say = [&](long long u, bool joinEv, bool againEv, bool switched, double played, float roll) {
                RbxEvent e;
                e.join = joinEv;
                e.again = againEv;
                e.switched = switched;
                e.played = played;
                e.universe = u;
                RbxName n;
                if (joinEv && names.get(u, n, t0)) {
                    e.name = n.name;
                    e.genre = n.genre;
                }
                CsLine l;
                now += 300;
                if (!v.pick(e, now, roll, l)) return std::string("-");
                logf("rbx test say: %s '%s' -> %s face %d: %s", joinEv ? "join" : "leave", e.name.c_str(), l.cat.c_str(), l.face,
                     narrow(a.phrases.pick(l.cat.c_str(), l.arg)).c_str());
                return l.cat + "(" + narrow(l.arg) + ")";
            };
            std::string s1 = say(111, true, false, false, 0, 0.f), s2 = say(222, true, false, false, 0, 0.f), s3 = say(333, true, false, false, 0, 0.f);
            std::string s4 = say(222, true, true, false, 0, 0.f), s5 = say(10770563082LL, true, false, false, 0, 0.f);
            std::string s6 = say(0, false, false, false, 25 * 60, 0.f), s7 = say(0, false, false, false, 5 * 60, 0.f), s8 = say(0, false, false, true, 3600, 0.f);
            std::string s9 = say(0, false, false, false, 25 * 60, 0.7f), s10 = say(444, true, false, false, 0, 0.f);
            check(s1 == "rbxHorror(DOORS)" && s2 == "rbxCozy(Brookhaven RP)" && s3 == "rbxJoin(Kebab Shop Delivery\xE2\x80\xA6)" && s4 == "rbxAgain(Brookhaven RP)" &&
                      s5 == "rbxFight()",
                  "lines: " + s1 + " " + s2 + " " + s3 + " " + s4 + " " + s5);
            {
                bool clean = true;
                for (int i = 0; i < 30; i++)
                    for (const char* c : {"rbxFight", "rbxJoin", "rbxAgain", "rbxHorror", "rbxCozy", "rbxObby", "rbxGrind", "rbxDisaster"}) {
                        std::wstring w = a.phrases.pick(c, L"");
                        if (w.empty() || w.find(L"{arg}") != std::wstring::npos) clean = false;
                    }
                check(clean && rbxPlaceholder("join the group to play") && rbxPlaceholder("Builderman's Place Number: 1") &&
                          rbxPlaceholder("Untitled Game") && !rbxPlaceholder("Brookhaven RP") && !rbxPlaceholder("Grass Incremental Simulator"),
                      "placeholder names: not read aloud, every category has lines without a name");
                std::wstring kb = a.phrases.fill(L"\u0422\u0430\u043a,| {arg}.| x", rbxShortName("Kebab Shop Delivery Experience 3000"));
                check(kb.find(L"\x2026.") == std::wstring::npos, "shortened name: no dot after the ellipsis: " + narrow(kb));
            }
            check(s6 == "rbxLeave()" && s7 == "-" && s8 == "-" && s9 == "-" && s10 == "-", "leave only after 20+ min and not into another game; no name, no line");
            now = v.lastJoinLine - 240;
            std::string h1 = say(111, true, false, false, 0, 0.5f);
            now = v.lastJoinLine - 240;
            std::string h2 = say(111, true, false, false, 0, 0.2f);
            check(h1 == "-" && h2 == "rbxHorror(DOORS)", "hopping between games: fewer lines");
            std::string miss;
            for (const char* c : {"rbxJoin", "rbxAgain", "rbxLeave", "rbxHorror", "rbxCozy", "rbxFight", "rbxObby", "rbxGrind", "rbxDisaster"})
                if (a.phrases.pick(c, L"X").empty()) miss += std::string(c) + " ";
            check(miss.empty(), "phrases for every category " + miss);
        }

        Activity act0 = a.activity;
        reset(St::Idle, 100);
        p.vit.rel = 50.f;
        sim(0.6f, still);
        a.activity.kind = AppKind::Game;
        a.activity.since = a.now - 10;
        a.activity.exe = L"chrome.exe";
        p.watchTarget = floorPt + V2(-600, -500);
        p.setState(St::Watch, 1e9f);
        p.watchSayT = 1e9f;
        p.csVoice = CsVoice{};
        p.rbxVoice = RbxVoice{};
        RbxEvent doors;
        doors.universe = 111;
        doors.name = "DOORS \xF0\x9F\x91\x81\xEF\xB8\x8F";
        doors.genre = "Adventure/Horror/Horror";
        p.rbxEvent(doors);
        sim(1.2f, still);
        bool held = p.csVoice.hasPending && p.csVoice.pending.cat.rfind("rbx", 0) == 0 && a.bubble.text.find(L"DOORS") == std::wstring::npos;
        a.activity.exe = L"robloxplayerbeta.exe";
        sim(3.0f, still);
        shot(L"78_rbx_doors");
        logf("rbx doors: held while browser in front %d; bubble '%s', surprise %.2f", (int)held, narrow(a.bubble.text).c_str(), p.tSurprise);
        if (!held || !a.bubble.active) fails++;
        a.bubble.close();
        sim(0.5f, still);
        RbxEvent out;
        out.join = false;
        out.universe = 111;
        out.played = 1800;
        p.rbxVoice = RbxVoice{};
        CsLine ll;
        if (p.rbxVoice.pick(out, a.now, 0.f, ll)) p.csVoice.queue(ll, a.now);
        p.csVoice.lastLine = a.now;
        a.activity.kind = AppKind::Other;
        a.activity.exe = L"explorer.exe";
        sim(6.f, still);
        logf("rbx leave: state %s, bubble '%s'", p.stateName(), narrow(a.bubble.text).c_str());
        if (p.st == St::Watch || a.bubble.text.empty()) fails++;
        a.activity = act0;
        p.csVoice = CsVoice{};
        a.bubble.close();
        logf("rbx tests: %d failed", fails);
    }
