
    {
        int fails = 0;
        auto check = [&](bool ok, const std::string& what) {
            if (!ok) fails++;
            logf("cs test %s: %s", ok ? "ok" : "FAIL", what.c_str());
        };
        const auto npos = std::string::npos;
        std::string body = "{\"provider\":{\"steamid\":\"1\"},\"auth\":{\"token\":\"tok\"}}";
        std::string req = "POST /gsi HTTP/1.1\r\nHost: 127.0.0.1:47733\r\nContent-Type: application/json\r\ncontent-length: " +
                          std::to_string(body.size()) + "\r\n\r\n" + body;
        GsiReq r;
        Json j;
        check(gsiParse(req, r) == 1 && r.method == "POST" && r.path == "/gsi" && r.body == body && r.used == req.size() && !r.origin, "http: full request");
        check(gsiParse(req.substr(0, req.size() - 5), r) == 0, "http: half a body waits");
        check(gsiParse(req.substr(0, 30), r) == 0, "http: half the headers wait");
        check(gsiParse(req + req, r) == 1 && r.used == req.size() && r.body == body, "http: two in a row, first taken");
        check(gsiParse("POST /gsi HTTP/1.1\r\nContent-Length: 999999999\r\n\r\n", r) == -2, "http: body over the cap");
        check(gsiParse("hello there\r\n\r\n", r) == -1, "http: garbage");
        check(gsiParse("POST /gsi HTTP/1.1\r\nContent-Length: 12x\r\n\r\n", r) == -1, "http: bad length");
        check(gsiParse("POST /gsi HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", r) == -1, "http: chunked refused");
        gsiParse(req, r);
        check(gsiAccept(r, {"tok"}, j) == 200 && j["provider"]["steamid"].str() == "1", "accept: right token");
        check(gsiAccept(r, {"other", "tok"}, j) == 200, "accept: old token still on disk");
        check(gsiAccept(r, {"other"}, j) == 403, "accept: wrong token");
        std::string fromSite = req;
        fromSite.insert(fromSite.find("\r\n") + 2, "Origin: https://example.com\r\n");
        check(gsiParse(fromSite, r) == 1 && r.origin && gsiAccept(r, {"tok"}, j) == 403, "accept: browser page refused");
        gsiParse("GET /gsi HTTP/1.1\r\n\r\n", r);
        check(gsiAccept(r, {"tok"}, j) == 405, "accept: GET refused");
        gsiParse("POST /gsi HTTP/1.1\r\nContent-Length: 3\r\n\r\n{x}", r);
        check(gsiAccept(r, {"tok"}, j) == 400, "accept: broken json");
        {
            std::string deep(20000, '['), deepReq = "POST /gsi HTTP/1.1\r\nContent-Length: 20000\r\n\r\n" + deep;
            int code = 0;
            bool fine = false;
            std::thread th([&] {
                GsiReq rq;
                Json jj;
                if (gsiParse(deepReq, rq) == 1) code = gsiAccept(rq, {"tok"}, jj);
                bool ok = false;
                std::string nest;
                for (int i = 0; i < 20; i++) nest += "{\"a\":[";
                nest += "1";
                for (int i = 0; i < 20; i++) nest += "]}";
                Json::parse(nest, &ok);
                fine = ok;
            });
            th.join();
            check(code == 400 && fine, "accept: 20000 levels deep refused, 41 levels still fine");
        }
        {
            std::wstring un = a.phrases.userName;
            a.phrases.userName = L"{name}";
            bool f1 = a.phrases.fill(L"На {arg}.| {name}!", L"{arg}") == L"На {arg}.| {name}!";
            a.phrases.userName = un;
            bool f2 = a.phrases.fill(L"Так,| {arg}.| x", L"Long\x2026") == L"Так,| Long\x2026| x";
            bool f3 = a.phrases.fill(L"{arg}?..| x", L"Ab\x2026") == L"Ab\x2026?| x";
            check(f1 && f2 && f3, "fill: no endless loop on {arg}/{name} inside, ellipsis without extra dots");
            check(csMapName("workshop/1/de_{arg}").find(L'{') == std::wstring::npos && csMapName("de_x|y^~z") == L"Xyz", "map names: bubble markup stripped");
        }

        std::wstring fake = dir + L"\\fake_cs2\\game\\csgo\\cfg", ours = fake + L"\\" + kGsiFile;
        SHCreateDirectoryExW(nullptr, fake.c_str(), nullptr);
        writeFile(fake + L"\\autoexec.cfg", "bind f \"say hi\"\r\n");
        writeFile(ours, gsiCfg("uzipet-gsi"));
        std::string old, txt;
        bool w1 = gsiInstall(fake, "abc123", &old);
        readFile(ours, txt);
        check(w1 && old == "uzipet-gsi" && txt == gsiCfg("abc123") && gsiCfgToken(txt) == "abc123", "cfg: rewritten with own token, old one reported");
        check(txt.find("\"uri\"\t\t\"http://127.0.0.1:47733/gsi\"") != npos && txt.find("\"player_match_stats\"\t\"1\"") != npos &&
                  txt.find("\"bomb\"\t\"1\"") != npos && txt.rfind("\"UziPet\"", 0) == 0,
              "cfg: uri, sections, name");
        bool w2 = gsiInstall(fake, "abc123", &old);
        check(w2 && old == "abc123", "cfg: same again, nothing to do");
        check(gsiRemove(fake) && GetFileAttributesW(ours.c_str()) == INVALID_FILE_ATTRIBUTES &&
                  GetFileAttributesW((fake + L"\\autoexec.cfg").c_str()) != INVALID_FILE_ATTRIBUTES,
              "cfg: remove takes only ours");
        logf("cs test: cfg text:\n%s", gsiCfg("abc123").c_str());
        for (auto& d : cs2CfgDirs()) logf("cs test: CS2 cfg folder on this PC (only looked): '%s'", narrow(d).c_str());
        check(csMapName("de_dust2") == L"Dust2" && csMapName("cs_office") == L"Office" && csMapName("de_cache") == L"Cache" &&
                  csMapName("workshop/123/ar_shoots") == L"Shoots",
              "map names");
        {
            std::string miss;
            for (const char* c : {"csEnter", "csMapChange", "csKill", "csHeadshot", "cs3k", "cs4k", "csMultikill", "csAce", "csDmStreak", "csDeath", "csRoundWin", "csRoundLose", "csLoseStreak", "csBombPlanted", "csBombPlantedUs", "csBombDefused", "csBombDefusedThem", "csBombExploded", "csFlashed", "csMatchWin", "csMatchLose", "csMatchDraw", "csDmSummary", "csDmSummaryEven", "csDmSummaryBad", "csLastDefuse", "csFastDefuse", "csKnifeDefuse", "csClutchDefuse", "csBlastWasted", "csSurvivedBlast", "csMissedDefuse", "csNearAce", "csPickupAk", "csPickupAwp", "csPickupEnemy", "csPickup", "csMvp", "csSmokeKill", "csTeamKill", "csKnifeKill", "csPistol3", "csPistol4", "csYouPlanted", "csLastPlant", "csMatchPointUs", "csMatchPointThem", "csMatchPointLast", "csMatchCarry", "csMatchLoseCarry", "csArmsUp", "csArmsPistols", "csArmsKnife", "csArmsLost", "csArmsLostKnife", "csArmsKnifeKill", "csArmsDeathLate", "csArmsWin", "csArmsAlmost"})
                for (int stg = 0; stg < 4; stg++)
                    if (a.phrases.listAt(c, stg).empty()) miss += std::string(c) + "@" + std::to_string(stg) + " ";
            check(miss.empty(), ("phrases for every category " + miss).c_str());
        }

        std::string all;
        std::wstring cap = dir + L"\\..\\gsi_capture.jsonl";
        if (!readFile(cap, all)) {
            logf("cs replay: no '%s', skipped", narrow(cap).c_str());
        } else {
            CsSense cs;
            CsVoice v;
            std::mt19937 rr(7);
            v.roll = [&] { return std::uniform_real_distribution<float>(0, 1)(rr); };
            double t0 = -1, tLast = 0;
            int posts = 0, lines = 0;
            std::map<std::string, int> cnt, said;
            struct Seg {
                std::string map;
                int deaths = 0, killEv = 0, killsEnd = 0, best = 0, lines = 0;
                double from = 0, to = 0;
                std::map<std::string, int> said;
            };
            std::vector<Seg> segs;
            struct Got { double t; CsEv type; std::string d; bool me; };
            std::vector<Got> evs;
            std::vector<std::pair<double, Json>> compRows;
            std::string matchEnd, mapChange, armsEnd;
            for (size_t pos = 0; pos < all.size();) {
                size_t e = all.find('\n', pos);
                if (e == npos) e = all.size();
                std::string ln = all.substr(pos, e - pos);
                pos = e + 1;
                bool ok = false;
                Json row = Json::parse(ln, &ok);
                if (!ok || row.t != Json::Obj) continue;
                double t = row["t"].n;
                if (t0 < 0) t0 = t;
                tLast = t;
                posts++;
                const Json& dd = row["d"];
                bool meRow = !dd["provider"]["steamid"].str().empty() && dd["player"]["steamid"].str() == dd["provider"]["steamid"].str();
                if (t - t0 >= 960 && t - t0 <= 2840) compRows.push_back({t, dd});
                if (!segs.empty()) segs.back().to = t - t0;
                for (auto& ev : cs.feed(dd, t)) {
                    cnt[csEvName(ev.type)]++;
                    evs.push_back(Got{t - t0, ev.type, csDescribe(ev), meRow});
                    if (ev.type == CsEv::Enter || ev.type == CsEv::MapChange) {
                        segs.push_back(Seg{ev.map});
                        segs.back().from = segs.back().to = t - t0;
                    }
                    if (ev.type == CsEv::MapChange && mapChange.empty()) mapChange = ev.map;
                    if (ev.type == CsEv::MatchEnd && ev.map == "ar_pool_day" && armsEnd.empty()) armsEnd = csDescribe(ev);
                    if (ev.type == CsEv::MatchEnd && matchEnd.empty()) matchEnd = csDescribe(ev);
                    if (!segs.empty()) {
                        Seg& s = segs.back();
                        if (ev.type == CsEv::Death) s.deaths++;
                        if (ev.type == CsEv::Kill) {
                            s.killEv++;
                            s.killsEnd = ev.kills;
                            s.best = std::max(s.best, ev.n);
                        }
                    }
                    int face = 0;
                    float ft = 0;
                    v.push(ev, 1, t, face, ft);
                    logf("cs replay %7.1f %-22s face %d", t - t0, csDescribe(ev).c_str(), face);
                }
                CsLine l;
                if (v.due(t, l)) {
                    lines++;
                    said[l.cat]++;
                    if (!segs.empty()) {
                        segs.back().lines++;
                        segs.back().said[l.cat]++;
                    }
                    logf("cs replay %7.1f   SAY %s (prio %d): %s", t - t0, l.cat.c_str(), l.prio, narrow(a.phrases.pick(l.cat.c_str(), l.arg)).c_str());
                }
            }
            for (auto& s : segs) {
                std::string sc;
                for (auto& c : s.said) sc += c.first + "=" + std::to_string(c.second) + " ";
                double mins = std::max(1.0, s.to - s.from) / 60;
                logf("cs replay map %s (%.0f-%.0f s): deaths %d, kill events %d, kills at the end %d, best streak %d; lines %d = %.2f/min: %s", s.map.c_str(),
                     s.from, s.to, s.deaths, s.killEv, s.killsEnd, s.best, s.lines, s.lines / mins, sc.c_str());
            }
            std::string ce, cs2;
            for (auto& c : cnt) ce += c.first + "=" + std::to_string(c.second) + " ";
            for (auto& c : said) cs2 += c.first + "=" + std::to_string(c.second) + " ";
            logf("cs replay: %d posts over %.0f s; events %s; lines %d: %s", posts, tLast - t0, ce.c_str(), lines, cs2.c_str());
            check(segs.size() >= 2 && segs[0].map == "de_mirage" && segs[0].deaths == 36 && segs[0].killsEnd == 38 && segs[0].best == 5,
                  "replay: Mirage DM 38 kills / 36 deaths, streak 5");
            check(cnt["ace"] == 0 && cnt["dmStreak"] >= 1, "replay: DM streak, not an ace");
            check(matchEnd.rfind("matchEnd win (dm), 38/36", 0) == 0, "replay: gameover win as CT");
            check(mapChange == "de_dust2" && cnt["mapChange"] >= 1, "replay: Mirage -> Dust2 after the vote");
            check(segs.size() >= 2 && segs[1].map == "de_dust2" && segs[1].deaths == 16, "replay: Dust2 16 deaths, none from warmup");
            logf("cs replay: arms race end '%s'", armsEnd.c_str());
            check(armsEnd.empty() || armsEnd.rfind("matchEnd almost (arms), 25/37", 0) == 0, "replay: arms race on Pool Day, reached pistols, 25/37");
            check(cnt["enter"] >= 1, "replay: entered from the menu");

            auto at = [&](const std::string& what, double when) {
                for (auto& g : evs)
                    if (g.d.rfind(what, 0) == 0 && std::fabs(g.t - when) <= 1.0) return true;
                return false;
            };
            auto count = [&](CsEv ty, double from, double to) {
                int n = 0;
                for (auto& g : evs)
                    if (g.type == ty && g.t >= from && g.t <= to) n++;
                return n;
            };
            auto when = [&](CsEv ty, double from, double to) {
                std::string s;
                for (auto& g : evs)
                    if (g.type == ty && g.t >= from && g.t <= to) s += g.d + " @" + std::to_string((int)(g.t * 10) / 10.0).substr(0, 6) + "; ";
                return s;
            };
            if (tLast - t0 > 4100) {
                const double A = 960, B = 2840;
                check(at("fastDefuse 39.", 1305.0) && count(CsEv::LastDefuse, A, B) == 1, "moment 1: defuse in the last fraction of a second at 1305.0: " + when(CsEv::LastDefuse, A, B));
                check(at("mvp 1", 1305.0), "moment 1: first MVP with the defuse");
                check(at("acedButBombExploded 4", 1633.3) && count(CsEv::BlastWasted, A, B) == 2, "moment 2: four kills and the bomb still went off at 1633.3 (and 1755.0): " + when(CsEv::BlastWasted, A, B));
                check(at("survivedBlast", 1633.8) && count(CsEv::SurvivedBlast, A, B) == 1, "moment 3: survived the blast at 1633.8: " + when(CsEv::SurvivedBlast, A, B));
                check(at("missedDefuse", 1755.0) && at("missedDefuse", 1192.7) && count(CsEv::MissedDefuse, A, B) == 2,
                      "moment 4: did not make it, dead in the blast at 1755.0: " + when(CsEv::MissedDefuse, A, B));
                check(at("nearAce 4, hp 1", 1478.9) && count(CsEv::NearAce, A, B) == 1, "moment 5: four kills on 1 HP at 1478.9: " + when(CsEv::NearAce, A, B));
                check(at("pickup weapon_ak47 (enemy)", 1815.0), "moment 6: CT picked up an AK at 1815.0");
                check(count(CsEv::Pickup, 1140, 1145) == 0 && count(CsEv::Pickup, 1510, 1513) == 0 && count(CsEv::Pickup, 1970, 1980) == 0,
                      "moment 6: bot takeover and AWP buy/refund are not pickups: " + when(CsEv::Pickup, A, B));
                check(at("smokeKill", 1934.1) && at("clutchDefuse", 1944.7), "moment 7: kill from the smoke at 1934.1, defuse under fire at 1944.7: " + when(CsEv::SmokeKill, A, B) + when(CsEv::ClutchDefuse, A, B));
                check(at("teamKill", 2154.7) && count(CsEv::TeamKill, A, B) == 1, "moment 8: team kill at 2154.7: " + when(CsEv::TeamKill, A, B));
                check(at("knifeKill", 2178.2) && at("knifeDefuse", 2184.6) && count(CsEv::KnifeKill, A, B) == 1, "moment 9: knife kill at 2178.2, then defuse 2184.6: " + when(CsEv::KnifeKill, A, B));
                check(at("pistolRound 3", 2255.9) && at("pistolRound 4", 2259.2) && count(CsEv::PistolKills, A, B) == 2, "moment 10: pistol round 3 and 4 kills: " + when(CsEv::PistolKills, A, B));
                check(at("youPlanted", 2239.4) && at("lastSecondPlant 11", 2533.3) && count(CsEv::YouPlanted, A, B) == 4,
                      "moment 11: you planted at 2239.4, at the last second at 2533.3: " + when(CsEv::YouPlanted, A, B));
                check(at("matchPoint ours 12:6", 2749.3) && count(CsEv::MatchPoint, A, B) == 1, "moment 12: match point 12:6 at 2749.3: " + when(CsEv::MatchPoint, A, B));
                check(at("matchEnd win 13:6, 50/7, mvp 10", 2807.8), "moment 13: 13:6, 50/7, 10 MVP at 2807.8: " + when(CsEv::MatchEnd, A, B));
                check(at("matchEnd win (dm), 38/36", 651.1), "moment 14: deathmatch summary at 651.1");
                check(count(CsEv::Mvp, A, B) == 9, "MVP events: 9 in rounds plus the last one with the match end: " + std::to_string(count(CsEv::Mvp, A, B)));
                std::string foreign;
                for (auto& g : evs) {
                    bool personal = g.type != CsEv::Enter && g.type != CsEv::MapChange && g.type != CsEv::MatchEnd && g.type != CsEv::MatchPoint &&
                                    g.type != CsEv::RoundWin && g.type != CsEv::RoundLose && g.type != CsEv::LoseStreak && g.type != CsEv::BombPlanted &&
                                    g.type != CsEv::BombDefused && g.type != CsEv::BombExploded;
                    if (personal && !g.me) foreign += g.d + " @" + std::to_string((int)g.t) + "; ";
                }
                check(foreign.empty(), "personal events only on own frames: " + foreign);
                const double C = 3790, D = 4095;
                check(at("armsKnifeKill", 3917.1) && count(CsEv::ArmsKnifeKill, C, D) == 1, "arms: knife kill, next weapon at once at 3917.1: " + when(CsEv::ArmsKnifeKill, C, D));
                check(at("armsPistols weapon_deagle", 4031.8), "arms: pistols reached (Deagle) at 4031.8");
                check(at("armsLevelLost -1 knifed weapon_deagle", 4065.1) && count(CsEv::ArmsLost, C, D) == 1,
                      "arms: knifed back from Glock to Deagle at 4065.1: " + when(CsEv::ArmsLost, C, D));
                check(count(CsEv::ArmsUp, C, D) == 12 && count(CsEv::ArmsKnife, C, D) == 0, "arms: 12 level-ups by weapon, none on respawn: " + when(CsEv::ArmsUp, C, D));
                check(at("matchEnd almost (arms), 25/37", 4090.8), "arms: end at 4090.8, almost (USP, no knife)");
                std::string spam;
                for (auto& s : segs)
                    if (s.map.rfind("ar_", 0) == 0 && (s.said["cs3k"] || s.said["cs4k"])) spam += s.map + " ";
                check(spam.empty(), "arms: no triple/quad kill lines: " + spam);
                check(at("armsKnifeKill", 4910.4) && at("armsLevelLost -1 knifed weapon_awp", 4711.2), "arms: knife kill 4910.4 (Pool Day 2), knifed back on Baggage 4711.2");
            }

            {
                CsSense sp;
                std::string got;
                for (auto& r : compRows) {
                    Json d = r.second;
                    for (size_t i = 0; i < d.k.size(); i++)
                        if (d.k[i] == "provider")
                            for (size_t j = 0; j < d.a[i].k.size(); j++)
                                if (d.a[i].k[j] == "steamid") d.a[i].a[j].s = "1";
                    for (auto& ev : sp.feed(d, r.first)) {
                        CsEv ty = ev.type;
                        if (ty != CsEv::Enter && ty != CsEv::BombPlanted && ty != CsEv::BombDefused && ty != CsEv::BombExploded && ty != CsEv::MatchEnd)
                            got += csDescribe(ev) + "; ";
                    }
                }
                check(!compRows.empty() && got.empty(), "spectator only: no kills, pickups, defuses, match points: " + got);
            }
        }

        {
            CsSense cs;
            std::vector<std::string> got;
            double t = 1000;
            auto feed = [&](const char* map, const char* phase, int ct, int tt, const char* rph, const char* win, const char* bomb, const char* sid,
                            int hp, int rk, int rhs, int k, int dd, int fl) {
                std::string s = "{\"provider\":{\"steamid\":\"100\"}";
                char b[700];
                if (map) {
                    snprintf(b, sizeof(b),
                             ",\"map\":{\"mode\":\"competitive\",\"name\":\"%s\",\"phase\":\"%s\",\"team_ct\":{\"score\":%d},\"team_t\":{\"score\":%d}}", map,
                             phase, ct, tt);
                    s += b;
                    s += std::string(",\"round\":{\"phase\":\"") + rph + "\"";
                    if (*win) s += std::string(",\"win_team\":\"") + win + "\"";
                    if (*bomb) s += std::string(",\"bomb\":\"") + bomb + "\"";
                    s += "}";
                    snprintf(b, sizeof(b),
                             ",\"player\":{\"steamid\":\"%s\",\"team\":\"CT\",\"activity\":\"playing\",\"state\":{\"health\":%d,\"flashed\":%d,\"round_kills\":%d,"
                             "\"round_killhs\":%d},\"match_stats\":{\"kills\":%d,\"deaths\":%d}}",
                             sid, hp, fl, rk, rhs, k, dd);
                    s += b;
                } else {
                    s += ",\"player\":{\"steamid\":\"100\",\"activity\":\"menu\"}";
                }
                s += "}";
                bool ok = false;
                Json d = Json::parse(s, &ok);
                if (!ok) logf("cs synth: bad json %s", s.c_str());
                t += 5;
                for (auto& e : cs.feed(d, t)) got.push_back(csDescribe(e));
            };
            const char* M = "de_inferno";
            feed(nullptr, "", 0, 0, "", "", "", "100", 0, 0, 0, 0, 0, 0);
            feed(M, "warmup", 0, 0, "live", "", "", "100", 100, 2, 0, 2, 1, 0);
            feed(M, "live", 0, 0, "freezetime", "", "", "100", 100, 0, 0, 0, 0, 0);
            feed(M, "live", 0, 0, "live", "", "", "100", 100, 1, 0, 1, 0, 0);
            feed(M, "live", 0, 0, "live", "", "", "100", 100, 2, 1, 2, 0, 0);
            feed(M, "live", 0, 0, "live", "", "", "100", 100, 3, 1, 3, 0, 0);
            feed(M, "live", 0, 0, "live", "", "", "100", 90, 4, 1, 4, 0, 0);
            feed(M, "live", 0, 0, "live", "", "", "100", 90, 5, 2, 5, 0, 0);
            feed(M, "live", 0, 0, "live", "", "planted", "100", 90, 5, 2, 5, 0, 0);
            feed(M, "live", 0, 0, "live", "", "defused", "100", 90, 5, 2, 5, 0, 0);
            feed(M, "live", 1, 0, "over", "CT", "defused", "100", 90, 5, 2, 5, 0, 0);
            feed(M, "live", 1, 0, "freezetime", "", "", "100", 100, 0, 0, 5, 0, 0);
            feed(M, "live", 1, 0, "live", "", "", "100", 100, 0, 0, 5, 0, 0);
            feed(M, "live", 1, 1, "over", "T", "", "100", 100, 0, 0, 5, 0, 0);
            feed(M, "live", 1, 1, "live", "", "", "100", 100, 0, 0, 5, 0, 0);
            feed(M, "live", 1, 2, "over", "T", "", "100", 100, 0, 0, 5, 0, 0);
            feed(M, "live", 1, 2, "live", "", "", "100", 100, 0, 0, 5, 0, 0);
            feed(M, "live", 1, 3, "over", "T", "", "100", 100, 0, 0, 5, 0, 0);
            feed(M, "live", 1, 3, "live", "", "", "100", 100, 0, 0, 5, 0, 0);
            feed(M, "live", 1, 3, "live", "", "", "100", 100, 0, 0, 5, 0, 255);
            feed(M, "live", 1, 3, "live", "", "", "100", 100, 0, 0, 5, 0, 120);
            feed(M, "live", 1, 3, "live", "", "", "100", 0, 0, 0, 5, 1, 0);
            feed(M, "live", 1, 3, "live", "", "", "200", 100, 2, 1, 7, 0, 0);
            feed(M, "live", 1, 3, "live", "", "", "200", 100, 3, 1, 8, 0, 0);
            feed(M, "live", 1, 3, "live", "", "planted", "200", 100, 3, 1, 8, 0, 255);
            feed(M, "live", 1, 3, "live", "", "exploded", "200", 0, 3, 1, 8, 1, 0);
            feed(M, "live", 1, 4, "over", "T", "exploded", "200", 0, 3, 1, 8, 1, 0);
            feed(M, "live", 1, 4, "freezetime", "", "", "100", 100, 0, 0, 5, 1, 0);
            feed(M, "gameover", 13, 4, "over", "CT", "", "100", 100, 0, 0, 5, 1, 0);
            feed(nullptr, "", 0, 0, "", "", "", "100", 0, 0, 0, 0, 0, 0);
            feed("de_ancient", "warmup", 0, 0, "warmup", "", "", "100", 100, 0, 0, 0, 0, 0);
            std::string all2;
            for (auto& g : got) all2 += g + "; ";
            logf("cs synth: %s", all2.c_str());
            std::string want = "enter de_inferno; kill x1; kill x2 hs; kill x3; kill x4; kill x5 hs; ace 5; bombPlanted; bombDefused ours; roundWin; roundLose; "
                               "roundLose; roundLose; loseStreak 3; flashed; death; bombPlanted; bombExploded; roundLose; matchEnd win 13:4, 5/1, mvp 0; mapChange de_ancient; ";
            check(all2 == want, "synthetic competitive sequence");
            if (all2 != want) logf("cs synth want: %s", want.c_str());
        }

        {
            auto run = [&](const char* mode, std::vector<std::array<int, 5>> rows) {
                CsSense cs;
                std::string got;
                double t = 0;
                for (auto& rw : rows) {
                    char b[600];
                    snprintf(b, sizeof(b),
                             "{\"provider\":{\"steamid\":\"1\"},\"map\":{\"mode\":\"%s\",\"name\":\"de_x\",\"phase\":\"%s\"},\"round\":{\"phase\":\"live\"},"
                             "\"player\":{\"steamid\":\"1\",\"team\":\"CT\",\"state\":{\"health\":%d,\"round_kills\":%d,\"round_killhs\":0},"
                             "\"match_stats\":{\"kills\":%d,\"deaths\":%d}}}",
                             mode, rw[0] ? "live" : "warmup", rw[1], rw[2], rw[3], rw[4]);
                    for (auto& e : cs.feed(Json::parse(b), t += 5)) got += csDescribe(e) + "; ";
                }
                return got;
            };
            std::string d1 = run("deathmatch", {{0, 100, 0, 0, 0}, {0, 0, 0, 0, 1}, {0, 100, 0, 0, 1}, {1, 100, 0, 0, 1}, {1, 0, 0, 0, 1}, {1, 0, 0, 0, 2}, {1, 100, 1, 1, 2}});
            check(d1 == "enter de_x; death; kill x1; ", ("death: warmup one not reported later, health then counter is one death: " + d1).c_str());
            std::string c1 = run("casual", {{1, 100, 0, 0, 0}, {1, 100, 5, 5, 0}}), c2 = run("competitive", {{1, 100, 0, 0, 0}, {1, 100, 5, 5, 0}}),
                        c3 = run("gungameprogressive", {{1, 100, 0, 0, 0}, {1, 100, 5, 5, 0}});
            check(c1 == "enter de_x; kill x5; " && c2 == "enter de_x; kill x5; ace 5; " && c3 == "enter de_x; kill x5; dmStreak 5; ",
                  ("five in a round: casual no ace, competitive ace, arms race streak: " + c1 + "| " + c2 + "| " + c3).c_str());

            {
                CsSense ar;
                std::string got;
                double t = 0;
                struct R { int hp, k, dd, sc; const char* w; bool over; };
                auto typeOf = [](const std::string& n) -> const char* {
                    if (n == "knife") return "Knife";
                    if (n == "deagle" || n == "glock") return "Pistol";
                    if (n == "awp" || n == "scar20") return "SniperRifle";
                    if (n == "taser") return "";
                    return "Rifle";
                };
                std::vector<R> rows = {{100, 0, 0, 0, "knife,*scar20,taser", false}, {100, 2, 0, 2, "knife,*awp,taser", false},
                                       {0, 2, 1, 2, "", false},                       {100, 2, 1, 2, "*knife,awp,taser", false},
                                       {100, 3, 1, 4, "*knife,ak47,taser", false},    {100, 5, 1, 6, "knife,*deagle,taser", false},
                                       {100, 7, 1, 8, "knife,*glock,taser", false},   {0, 7, 2, 6, "", false},
                                       {100, 7, 2, 6, "knife,*deagle,taser", false},  {100, 9, 2, 8, "knife,*glock,taser", false},
                                       {100, 11, 2, 10, "*knife,taser", false},       {100, 12, 2, 12, "*knife,taser", false},
                                       {100, 12, 2, 12, "*knife,taser", true}};
                for (auto& r : rows) {
                    std::string ws, w = r.w;
                    int i = 0;
                    for (size_t p = 0; p < w.size();) {
                        size_t c = w.find(',', p);
                        if (c == npos) c = w.size();
                        std::string n = w.substr(p, c - p);
                        p = c + 1;
                        bool act = n[0] == '*';
                        if (act) n = n.substr(1);
                        char b[200];
                        snprintf(b, sizeof(b), "%s\"weapon_%d\":{\"name\":\"weapon_%s\",\"type\":\"%s\",\"state\":\"%s\"}", i ? "," : "", i, n.c_str(), typeOf(n),
                                 act ? "active" : "holstered");
                        ws += b;
                        i++;
                    }
                    char b[1400];
                    snprintf(b, sizeof(b),
                             "{\"provider\":{\"steamid\":\"1\"},\"map\":{\"mode\":\"gungameprogressive\",\"name\":\"ar_x\",\"phase\":\"%s\"},\"round\":{\"phase\":\"%s\"%s},"
                             "\"player\":{\"steamid\":\"1\",\"team\":\"T\",\"state\":{\"health\":%d,\"round_kills\":0,\"round_killhs\":0,\"money\":0},"
                             "\"match_stats\":{\"kills\":%d,\"deaths\":%d,\"score\":%d,\"mvps\":0},\"weapons\":{%s}}}",
                             r.over ? "gameover" : "live", r.over ? "freezetime" : "live", r.over ? ",\"win_team\":\"T\"" : "", r.hp, r.k, r.dd, r.sc, ws.c_str());
                    for (auto& e : ar.feed(Json::parse(b), t += 1))
                        if (e.type != CsEv::Kill) got += csDescribe(e) + "; ";
                }
                std::string want = "enter ar_x; armsLevel 2 weapon_awp; death; armsKnifeKill; armsLevel 3 weapon_ak47; armsPistols weapon_deagle; "
                                   "armsLevel 5 weapon_glock; death late; armsLevelLost -1 knifed weapon_deagle; armsLevel 5 weapon_glock; armsKnifeLevel; "
                                   "matchEnd won (arms), 12/2; ";
                check(got == want, "arms race to the knife and the win: " + got);
            }

            CsVoice v;
            int face = 0;
            float ft = 0;
            CsLine l;
            auto said = [&](const CsEvent& e, int tier) {
                v = CsVoice{};
                v.roll = [] { return 0.f; };
                v.push(e, tier, 100, face, ft);
                return v.due(100, l) ? l.cat : std::string("-");
            };
            CsEvent me;
            me.type = CsEv::MatchEnd;
            me.n = -1;
            bool m0 = said(me, 0) == "csMatchLose" && face == CsFaceHappy, m1 = said(me, 1) == "csMatchLose" && face == CsFaceSad;
            me.n = 1;
            bool m2 = said(me, 0) == "csMatchWin" && said(me, 2) == "csMatchWin";
            me.dm = true;
            me.kills = 20;
            me.deaths = 20;
            bool m3 = said(me, 1) == "csDmSummaryEven";
            me.deaths = 30;
            bool m4 = said(me, 0) == "csDmSummaryBad" && face == CsFaceHappy && said(me, 2) == "csDmSummaryBad";
            me.deaths = 10;
            bool m5 = said(me, 1) == "csDmSummary";
            check(m0 && m1 && m2 && m3 && m4 && m5, "match end: tier 0 gloats, even K/D is not a plus");
            CsEvent de;
            de.type = CsEv::Death;
            bool d2 = said(de, 2) == "csDeath" && face == CsFaceSad, d0 = said(de, 1) == "csDeath" && face == CsFaceHappy;
            check(d2 && d0, "death face: laughs only at tier 0-1");
            CsEvent bd;
            bd.type = CsEv::BombDefused;
            bool b1 = said(bd, 1) == "csBombDefusedThem";
            bd.ours = true;
            bool b2 = said(bd, 1) == "csBombDefused";
            CsEvent bp;
            bp.type = CsEv::BombPlanted;
            bool b3 = said(bp, 1) == "csBombPlanted";
            bp.ours = true;
            bool b4 = said(bp, 1) == "csBombPlantedUs";
            check(b1 && b2 && b3 && b4, "bomb lines know whose bomb it is");
            CsEvent k5;
            k5.type = CsEv::Kill;
            k5.n = 5;
            bool k1 = said(k5, 1) == "csMultikill" && l.arg == L"5";
            k5.full = true;
            bool k2 = said(k5, 1) == "-";
            check(k1 && k2, "five kills: casual multikill line, 5v5 leaves it to the ace");
            v = CsVoice{};
            v.roll = [] { return 0.f; };
            de.dm = true;
            int deathLines = 0;
            for (int i = 0; i < 40; i++) {
                v.push(de, 1, 1000 + i * 15.0, face, ft);
                if (v.due(1000 + i * 15.0, l)) deathLines++;
            }
            logf("cs test: DM death lines in 10 min, every roll passing: %d", deathLines);
            check(deathLines <= 4, "DM death lines at most every 3 min");
            v = CsVoice{};
            v.roll = [] { return 0.f; };
            int upLines = 0;
            CsEvent up;
            up.type = CsEv::ArmsUp;
            up.dm = up.arms = true;
            up.gun = "weapon_mp7";
            for (int i = 0; i < 30; i++) {
                v.push(up, 1, 2000 + i * 10.0, face, ft);
                if (v.due(2000 + i * 10.0, l)) upLines++;
            }
            up.gun = "weapon_awp";
            v.push(up, 1, 2295, face, ft);
            bool favUp = v.due(2300, l) && l.cat == "csArmsUp" && l.arg == L"AWP";
            logf("cs test: arms level lines in 5 min, a level every 10 s: %d", upLines);
            check(upLines >= 5 && upLines <= 9 && favUp, "arms level-ups: at most every 35 s, AWP regardless");
            CsEvent k3;
            k3.type = CsEv::Kill;
            k3.n = 3;
            k3.dm = k3.arms = true;
            check(said(k3, 1) == "-", "arms: no triple-kill line");
        }

        reset(St::Idle, 100);
        p.vit.rel = 50.f;
        sim(0.6f, still);
        p.watchTarget = floorPt + V2(-600, -500);
        p.setState(St::Watch, 1e9f);
        p.csVoice = CsVoice{};
        p.csVoice.roll = [] { return 0.f; };
        sim(1.2f, still);
        CsEvent ace;
        ace.type = CsEv::Ace;
        ace.n = 5;
        p.csEvent(ace);
        sim(2.2f, still);
        shot(L"77_cs_ace");
        logf("cs ace: bubble '%s', surprise %.2f", narrow(a.bubble.text).c_str(), p.tSurprise);
        p.csVoice = CsVoice{};
        a.bubble.close();
        logf("cs tests: %d failed", fails);
    }
