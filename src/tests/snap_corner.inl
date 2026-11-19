
    {
        int fails = 0;
        auto check = [&](bool ok, const char* what) {
            if (!ok) fails++;
            logf("corner test %s: %s", ok ? "ok" : "FAILED", what);
        };
        std::wstring realDesk;
        {
            PWSTR rd = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &rd)) && rd) realDesk = rd;
            CoTaskMemFree(rd);
        }
        auto realState = [&] {
            std::wstring s;
            int n = 0;
            WIN32_FIND_DATAW f;
            HANDLE h = realDesk.empty() ? INVALID_HANDLE_VALUE : FindFirstFileW((realDesk + L"\\*").c_str(), &f);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    n++;
                    if (wcsstr(f.cFileName, L"Уголок Узи")) s += std::wstring(f.cFileName) + L";";
                } while (FindNextFileW(h, &f));
                FindClose(h);
            }
            return s + L"#" + std::to_wstring(n);
        };
        std::wstring real0 = realState();
        auto fileBytes = [](const std::wstring& f) {
            std::string b;
            if (FILE* h = _wfopen(f.c_str(), L"rb")) {
                char buf[4096];
                size_t n;
                while ((n = fread(buf, 1, sizeof(buf), h)) > 0) b.append(buf, n);
                fclose(h);
            }
            return b;
        };
        auto exists = [](const std::wstring& f) { return GetFileAttributesW(f.c_str()) != INVALID_FILE_ATTRIBUTES; };
        double now = (double)time(nullptr);
        p.cornerDeskTest.clear();
        p.cornerLoaded = false;
        p.vit.cornerState = 0;
        p.vit.cornerAskedAt = 0;
        check(p.cornerDesk().empty(), "snapshot without its own folder has no desktop to write to");
        const std::wstring fdesk = dir + L"\\corner_desk_" + std::to_wstring(GetTickCount64());
        CreateDirectoryW(fdesk.c_str(), nullptr);
        p.cornerDeskTest = fdesk;
        reset(St::Idle, 100);
        p.vit.intro = 0;
        p.vit.rel = 75.f;
        p.vit.metAt = now - 5 * 86400.0;
        p.vit.metAt = now - 1 * 86400.0;
        check(!p.cornerCanAsk(now), "met 1 day: no ask");
        p.vit.metAt = now - 5 * 86400.0;
        p.vit.rel = 10.f;
        check(!p.cornerCanAsk(now), "tier 0: never asks");
        p.vit.rel = 75.f;
        p.vit.intro = 2;
        check(!p.cornerCanAsk(now), "baby: no ask");
        p.vit.intro = 0;
        p.vit.cornerAskedAt = now - 3 * 86400.0;
        check(!p.cornerCanAsk(now), "asked 3 days ago: no ask");
        p.vit.cornerAskedAt = now - 8 * 86400.0;
        check(p.cornerCanAsk(now), "asked 8 days ago, met 5 days, tier 2: may ask");
        float warm0 = p.warmT;
        p.warmT = 1e9f;
        p.quietUntil = 0;
        p.lifeT = 200.f;
        p.lastSay = a.now - 100;
        p.lastDateSay = -1e9;
        simIdle = 10;
        bool asked = false;
        for (int k = 0; k < 60 && !asked; k++) {
            p.cornerT = 0;
            sim(0.25f, still);
            asked = a.bubble.asking() && a.bubble.askId == "corner";
        }
        check(asked, "asked at a calm moment");
        logf("corner ask: '%s'", narrow(a.bubble.text).c_str());
        sim(std::min(a.bubble.typeDur + 0.3f, 5.f), still);
        shot(L"84_corner_ask");
        p.answer("corner", 0);
        const std::wstring cdir = fdesk + L"\\Уголок Узи";
        check(exists(cdir) && p.vit.cornerState == 1, "yes: folder made on the fake desktop");
        check(p.cornerDir == cdir, "her folder is the one on the fake desktop");
        std::string keep = fileBytes(cdir + L"\\Не трогать.txt");
        check(keep.size() > 3 && keep.compare(0, 3, "\xEF\xBB\xBF") == 0 && keep.find("\r\n") != std::string::npos, "note 'Не трогать.txt' (BOM, CRLF)");
        logf("corner made: '%s', note '%s'", narrow(a.bubble.text).c_str(), keep.size() > 3 ? keep.substr(3).c_str() : "");
        sim(std::min(a.bubble.typeDur + 0.4f, 5.f), still);
        shot(L"84b_corner_made");
        a.bubble.close();
        p.tHappy = 3.f;
        p.tBlush = 2.f;
        p.cornerPhoto(L"я после эйса");
        sim(1.3f, still);
        std::string png = fileBytes(cdir + L"\\я после эйса.png");
        check(png.size() > 2000 && png.compare(0, 8, "\x89PNG\r\n\x1A\n") == 0, "photo 'я после эйса.png' written");
        CopyFileW((cdir + L"\\я после эйса.png").c_str(), (dir + L"\\84c_corner_photo.png").c_str(), FALSE);
        p.tSurprise = 2.f;
        p.cornerPhoto(L"я в шоке");
        sim(1.3f, still);
        check(!exists(cdir + L"\\я в шоке.png"), "second photo the same day: skipped");
        check(p.cornerMoodNote(), "mood note written");
        {
            a.bubble.close();
            {
                std::string big;
                int purple = 0, left = 0;
                std::string ico = purpleFolderIco(a.gfx.wic.Get(), &big, &purple, &left);
                check(ico.size() > 64 && purple > 100 && left < purple / 10, "icon: folder recolored purple");
                logf("corner icon: purple folder %zu bytes, 32px: %d recolored, %d other colored", ico.size(), purple, left);
                if (!big.empty()) writeFile(dir + L"\\84e_corner_icon256.png", big);
            }
            check(p.cornerDecorate(), "icon: decorated");
            std::string ico = fileBytes(cdir + L"\\folder.ico"), ini = fileBytes(cdir + L"\\desktop.ini");
            DWORD ia = GetFileAttributesW((cdir + L"\\desktop.ini").c_str()), da = GetFileAttributesW(cdir.c_str());
            check(ico.size() > 64 && ico[0] == 0 && ico[2] == 1 && ini.size() > 20 && (unsigned char)ini[0] == 0xFF && (unsigned char)ini[1] == 0xFE,
                  "icon: .ico header, desktop.ini UTF-16");
            check(ia != INVALID_FILE_ATTRIBUTES && (ia & FILE_ATTRIBUTE_HIDDEN) && (ia & FILE_ATTRIBUTE_SYSTEM) && (da & FILE_ATTRIBUTE_READONLY),
                  "icon: files hidden, folder marked");
            check(p.cornerOwn(L"desktop.ini") && p.cornerOwn(L"folder.ico") && !p.cornerOwn(L"uzi.ico") && !p.cornerDecorate(), "icon: hers, only once");
            check(ini.find("f\0o\0l\0d\0e\0r\0.\0i\0c\0o\0", 0, 20) != std::string::npos, "icon: desktop.ini points at folder.ico");
            {
                std::wstring f = cdir + L"\\folder.ico", u = cdir + L"\\uzi.ico", di = cdir + L"\\desktop.ini";
                SetFileAttributesW(f.c_str(), FILE_ATTRIBUTE_NORMAL);
                MoveFileW(f.c_str(), u.c_str());
                SetFileAttributesW(u.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
                for (auto& n : p.cornerFiles)
                    if (n == L"folder.ico") n = L"uzi.ico";
                std::wstring old = L"\xFEFF[.ShellClassInfo]\r\nIconResource=uzi.ico,0\r\n";
                SetFileAttributesW(di.c_str(), FILE_ATTRIBUTE_NORMAL);
                writeFile(di, std::string((const char*)old.data(), old.size() * sizeof(wchar_t)));
                SetFileAttributesW(di.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
                size_t n0 = p.cornerFiles.size();
                p.cornerIcoFile.clear();
                p.cornerIconSync();
                std::string ini2 = fileBytes(di), fi = fileBytes(f);
                DWORD da2 = GetFileAttributesW(di.c_str()), fa2 = GetFileAttributesW(f.c_str());
                check(p.cornerOwn(L"folder.ico") && fi.size() > 64 && fileBytes(u) == fi && ini2.find("f\0o\0l\0d\0e\0r\0", 0, 12) != std::string::npos &&
                          (da2 & FILE_ATTRIBUTE_HIDDEN) && (fa2 & FILE_ATTRIBUTE_HIDDEN),
                      "icon: old face icon swapped for the purple folder");
                check(!p.cornerIconSync(), "icon: nothing changed, not rebuilt");
                check(p.cornerFiles.size() == n0 + 1, "icon: upgrade once, nothing deleted");
            }
            {
                check(p.cornerIcoPhoto == L"я после эйса.png", (std::string("icon: the photo inside is '") + narrow(p.cornerIcoPhoto) + "'").c_str());
                std::string ph = fileBytes(cdir + L"\\я после эйса.png"), big;
                std::string ico2 = purpleFolderIco(a.gfx.wic.Get(), &big, nullptr, nullptr, &ph);
                if (!big.empty()) writeFile(dir + L"\\84f_corner_icon_photo256.png", big);
                check(ico2.size() > 64 && big.size() > 2000 && big != fileBytes(dir + L"\\84e_corner_icon256.png"), "icon: photo composed into the folder");
                std::wstring from = p.cornerIcoFile;
                check(p.cornerWrite(L"я тест.png", ph) && p.cornerIconSync() && p.cornerIcoPhoto == L"я тест.png" && p.cornerIcoFile != from,
                      "icon: new photo, icon rebuilt under the other name");
                std::string ini3 = fileBytes(cdir + L"\\desktop.ini");
                std::wstring cur = p.cornerIcoFile;
                std::string curU16((const char*)cur.data(), cur.size() * sizeof(wchar_t));
                check(ini3.find(curU16) != std::string::npos && (GetFileAttributesW((cdir + L"\\" + cur).c_str()) & FILE_ATTRIBUTE_HIDDEN), "icon: desktop.ini follows, file hidden");
                DeleteFileW((cdir + L"\\я тест.png").c_str());
                check(p.cornerIconSync() && p.cornerIcoPhoto == L"я после эйса.png", "icon: photo removed, the previous one is inside again");
                std::erase(p.cornerFiles, std::wstring(L"я тест.png"));
            }
            logf("corner icon: ico %zu bytes, '%s', effort %d", ico.size(), narrow(a.bubble.text).c_str(), p.cornerEffort());
            sim(std::min(a.bubble.typeDur + 0.3f, 5.f), still);
            a.bubble.close();
        }
        {
            a.bubble.close();
            p.dayLoad();
            p.dayRoll();
            {
                int k0 = p.dayLog.day;
                p.dayLog = {};
                p.dayLog.day = k0;
                p.dayLog.t0 = (double)time(nullptr);
            }
            p.dayCount("kiss");
            p.dayCount("pet");
            int today = p.dayLog.day;
            check(p.dayLog.kiss == 1 && p.dayLog.pet == 1, "day: counted");
            double now0 = (double)time(nullptr);
            p.dayLog.day = today - 1;
            p.dayLog.t0 = now0 - 86400;
            p.dayLog.minutes = 130;
            p.dayLog.cs = 2;
            p.dayLog.csWin = 1;
            p.dayLog.fed = 1;
            p.dayRoll();
            std::string d1 = fileBytes(cdir + L"\\дневник.txt");
            check(d1.size() > 40 && d1.compare(0, 3, "\xEF\xBB\xBF") == 0 && p.cornerOwn(L"дневник.txt") && p.diaryDay == today - 1 && p.dayLog.day == today &&
                      p.dayLog.kiss == 0,
                  "diary: yesterday written, today starts from zero");
            p.dayRoll();
            check(fileBytes(cdir + L"\\дневник.txt") == d1, "diary: same day, nothing added");
            p.dayLog.day = today - 2 + 1000;
            p.dayLog.t0 = now0 - 2 * 86400;
            p.dayLog.minutes = 0;
            p.dayRoll();
            std::string d2 = fileBytes(cdir + L"\\дневник.txt");
            logf("diary sizes %zu -> %zu, prefix same %d", d1.size(), d2.size(), (int)(d2.compare(0, d1.size(), d1) == 0));
            check(d2.size() > d1.size() + 20 && d2.compare(0, d1.size(), d1) == 0, "diary: next day appended, old text kept");
            CopyFileW((cdir + L"\\дневник.txt").c_str(), (dir + L"\\84h_diary.txt").c_str(), FALSE);
        }
        {
            int drawn = 0;
            for (int k = 0; k < doodleKinds(); k++) {
                std::string png;
                std::wstring cap;
                if (p.doodleRender(k, png, cap) && png.size() > 3000) {
                    drawn++;
                    writeFile(dir + L"\\84g_doodle_" + std::to_wstring(k) + L".png", png);
                }
            }
            check(drawn == doodleKinds(), "doodles: every kind draws");
            p.doodlesDone = 0;
            int t = p.vit.rel < 85 ? 0 : 1;
            (void)t;
            check(p.cornerDoodle(2) && exists(cdir + L"\\нарисовала кота.png") && (p.doodlesDone & 4) && p.doodleNext > p.doodleAt,
                  "doodle: cat in the corner, next one later");
            check(p.cornerIcoPhoto == L"нарисовала кота.png", "doodle: it peeks out of the folder");
            check(p.cornerDoodle(-1) && !(p.cornerFiles.back() == L"нарисовала кота (2).png"), "doodle: picks one not drawn yet");
            logf("doodle picked: '%s'", narrow(p.cornerFiles.back()).c_str());
            p.doodleNext = (double)time(nullptr) + 3600;
            check(!p.doodleStep(true), "doodle: not before its time");
            p.doodleNext = 1;
            bool did = false;
            for (int k = 0; k < 60 && !did; k++) did = p.doodleStep(true);
            check(did && a.bubble.active, "doodle: draws by herself and tells");
            logf("doodle said: '%s'", narrow(a.bubble.text).c_str());
            a.bubble.close();
        }
        {
            check(p.cornerIsMine(cdir) && !p.cornerIsMine(fdesk) && !p.cornerIsMine(fdesk + L"\\Уголок Узи (2)"), "knows her own folder by path");
            p.heldPath = cdir;
            p.heldCorner = true;
            p.heldStage = 0;
            p.heldT = 2.3f;
            p.heldLife = 30.f;
            p.lastSay = a.now - 100;
            sim(0.3f, still);
            std::string s = narrow(a.bubble.text);
            check(a.bubble.active && (s.find("уголок") != std::string::npos || s.find("папк") != std::string::npos || s.find("место") != std::string::npos),
                  "own corner in her hands: her own reaction");
            logf("corner held: '%s'", s.c_str());
            sim(std::min(a.bubble.typeDur, 5.f), still);
            shot(L"84d_corner_held");
            bool away = MoveFileW(cdir.c_str(), (cdir + L"_inhands").c_str()) != 0;
            for (int k = 0; k < 3; k++) p.cornerCheck();
            check(away && p.vit.cornerState == 1 && p.cornerSayCat.empty(), "while in her hands: not 'gone'");
            if (away) MoveFileW((cdir + L"_inhands").c_str(), cdir.c_str());
            p.heldPath.clear();
            p.heldCorner = false;
            p.heldStage = 0;
            a.bubble.close();
        }
        int ref0 = p.cornerRefused;
        for (const wchar_t* bad : {L"..\\outside.txt", L"../outside.txt", L"C:\\outside.txt", L"", L"..", L"a\\b.txt", L"what?.txt", L"x..txt", L"con.txt "})
            check(!p.cornerWrite(bad, "nope\r\n"), (std::string("refused '") + narrow(bad) + "'").c_str());
        check(!exists(fdesk + L"\\outside.txt") && !exists(dir + L"\\outside.txt"), "nothing written outside the folder");
        writeFile(cdir + L"\\чужое.txt", "yours\r\n");
        check(!p.cornerWrite(L"чужое.txt", "mine\r\n") && fileBytes(cdir + L"\\чужое.txt") == "yours\r\n", "someone else's file: refused, untouched");
        CreateDirectoryW((cdir + L"\\папка").c_str(), nullptr);
        check(!p.cornerWrite(L"папка", "x"), "a folder with that name: refused");
        std::wstring keepDir = p.cornerDir;
        p.cornerDir = fdesk;
        check(!p.cornerWrite(L"a.txt", "x") && !exists(fdesk + L"\\a.txt"), "record points at the desktop itself: refused");
        const std::wstring other = dir + L"\\corner_elsewhere_" + std::to_wstring(GetTickCount64());
        CreateDirectoryW(other.c_str(), nullptr);
        CreateDirectoryW((other + L"\\Уголок Узи").c_str(), nullptr);
        p.cornerDir = other + L"\\Уголок Узи";
        check(!p.cornerWrite(L"a.txt", "x") && !exists(other + L"\\Уголок Узи\\a.txt"), "record points off the desktop: refused");
        const std::wstring link = fdesk + L"\\Уголок Узи (7)";
        if (CreateSymbolicLinkW(link.c_str(), (other + L"\\Уголок Узи").c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | 0x2)) {
            p.cornerDir = link;
            check(!p.cornerWrite(L"a.txt", "x") && !exists(other + L"\\Уголок Узи\\a.txt"), "folder is a symlink: refused");
            RemoveDirectoryW(link.c_str());
        } else {
            logf("corner test: symlink not allowed here (error %lu), link case skipped", GetLastError());
        }
        p.cornerDir = keepDir;
        logf("corner refusals: %d", p.cornerRefused - ref0);
        int wrote = 0;
        for (int k = 0; k < 40; k++)
            if (p.cornerWrite(L"тест " + std::to_wstring(k) + L".txt", "t\r\n")) wrote++;
        check(p.cornerFiles.size() == 30, "cap: 30 of her files");
        logf("corner cap: wrote %d more, her files %zu", wrote, p.cornerFiles.size());
        check(p.cornerWrite(L"тест 0.txt", "again\r\n") && fileBytes(cdir + L"\\тест 0.txt") == "again\r\n", "her own file: may rewrite even when full");
        {
            HANDLE dh = CreateFileW(cdir.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            bool moved = MoveFileW(cdir.c_str(), (cdir + L"_moved").c_str()) != 0;
            DWORD me = GetLastError();
            if (moved) MoveFileW((cdir + L"_moved").c_str(), cdir.c_str());
            if (dh != INVALID_HANDLE_VALUE) CloseHandle(dh);
            check(dh != INVALID_HANDLE_VALUE && !moved, "held folder can't be renamed meanwhile");
            logf("corner hold: rename while held -> %s (error %lu)", moved ? "MOVED" : "blocked", moved ? 0ul : me);
        }
        {
            std::wstring keepDesk = p.cornerDeskTest;
            p.cornerDeskTest = fdesk + L"_offline";
            a.bubble.close();
            for (int k = 0; k < 3; k++) p.cornerCheck();
            check(p.vit.cornerState == 1 && p.cornerSayCat.empty() && p.cornerFiles.size() == 30, "desktop unreachable: waits, keeps her files");
            p.cornerDeskTest = keepDesk;
            p.cornerCheck();
            check(p.vit.cornerState == 1 && p.cornerSayCat.empty() && p.cornerFiles.size() == 30, "desktop back: all still hers");
        }
        {
            std::wstring keepDesk = p.cornerDeskTest;
            int st0 = p.vit.cornerState;
            double asked0 = p.vit.cornerAskedAt;
            p.vit.cornerState = 0;
            p.vit.cornerAskedAt = 0;
            for (const wchar_t* root : {L"C:", L"C:\\", L"\\\\srv\\share"}) {
                p.cornerDeskTest = root;
                check(!p.cornerCanAsk(now), (std::string("desktop '") + narrow(root) + "': no ask").c_str());
            }
            p.cornerDeskTest = keepDesk;
            p.vit.cornerState = st0;
            p.vit.cornerAskedAt = asked0;
        }
        DeleteFileW((cdir + L"\\Не трогать.txt").c_str());
        a.bubble.close();
        p.lastSay = a.now - 100;
        p.cornerCheck();
        check(p.cornerSayCat == "cornerLost" && narrow(p.cornerSayArg) == "Не трогать", "lost file noticed");
        sim(0.5f, still);
        logf("corner lost: '%s'", narrow(a.bubble.text).c_str());
        check(narrow(a.bubble.text).find("Не трогать") != std::string::npos, "she says which file");
        check(!p.cornerOwn(L"Не трогать.txt"), "dropped from her list");
        sim(3.f, still);
        a.bubble.close();
        p.cornerCheck();
        check(p.cornerSayCat.empty(), "noticed only once");
        {
            WIN32_FIND_DATAW f;
            HANDLE h = FindFirstFileW((cdir + L"\\*").c_str(), &f);
            std::vector<std::wstring> names;
            if (h != INVALID_HANDLE_VALUE) {
                do
                    if (wcscmp(f.cFileName, L".") && wcscmp(f.cFileName, L"..")) names.push_back(f.cFileName);
                while (FindNextFileW(h, &f));
                FindClose(h);
            }
            for (auto& n : names) {
                std::wstring q = cdir + L"\\" + n;
                if (!DeleteFileW(q.c_str())) RemoveDirectoryW(q.c_str());
            }
            SetFileAttributesW(cdir.c_str(), FILE_ATTRIBUTE_NORMAL);
            RemoveDirectoryW(cdir.c_str());
        }
        a.bubble.close();
        p.lastSay = a.now - 100;
        p.cornerCheck();
        check(p.vit.cornerState == 1 && p.cornerSayCat.empty(), "folder missing once: checks again before saying");
        p.cornerCheck();
        now = (double)time(nullptr);
        check(p.vit.cornerState == 0 && p.cornerSayCat == "cornerGoneHurt", "whole folder gone after she settled in: hurt");
        sim(0.5f, still);
        logf("corner gone: '%s', sad %.1f, state %s", narrow(a.bubble.text).c_str(), p.tSad, p.stateName());
        check(p.tSad > 3.f, "hurt: long sadness");
        sim(1.5f, still);
        shot(L"84e_corner_gone_hurt");
        check(!p.cornerCanAsk(now + 6 * 86400.0) && p.cornerCanAsk(now + 7.1 * 86400.0), "asks again only after 7 days");
        sim(3.f, still);
        a.bubble.close();
        p.vit.rel = 96.f;
        p.vit.cornerAskedAt = now - 8 * 86400.0;
        check(p.cornerAskNow(), "asks again (tier 3)");
        logf("corner ask tier 3: '%s'", narrow(a.bubble.text).c_str());
        p.answer("corner", 1);
        logf("corner no: '%s' sad %.1f", narrow(a.bubble.text).c_str(), p.tSad);
        check(!exists(cdir) && p.vit.cornerState == 0 && !p.cornerCanAsk(now + 86400.0) && p.cornerCanAsk(now + 7.1 * 86400.0), "no: nothing made, again in 7 days");
        sim(3.f, still);
        a.bubble.close();
        p.vit.cornerAskedAt = now - 8 * 86400.0;
        p.cornerAskNow();
        a.bubble.expired = "corner";
        a.bubble.close();
        sim(0.1f, still);
        check(!p.cornerCanAsk(now + 0.5 * 86400.0) && p.cornerCanAsk(now + 1.1 * 86400.0), "ignored: again in a day");
        {
            const std::wstring ldesk = dir + L"\\corner_locked_" + std::to_wstring(GetTickCount64());
            CreateDirectoryW(ldesk.c_str(), nullptr);
            PSECURITY_DESCRIPTOR sd = nullptr;
            bool locked = ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(D;;0x4;;;WD)(A;;FA;;;WD)", SDDL_REVISION_1, &sd, nullptr) &&
                          SetFileSecurityW(ldesk.c_str(), DACL_SECURITY_INFORMATION, sd);
            if (sd) LocalFree(sd);
            bool probe = CreateDirectoryW((ldesk + L"\\probe").c_str(), nullptr);
            if (probe) RemoveDirectoryW((ldesk + L"\\probe").c_str());
            if (locked && !probe) {
                std::wstring keepDesk = p.cornerDeskTest;
                p.cornerDeskTest = ldesk;
                sim(3.f, still);
                a.bubble.close();
                p.vit.cornerAskedAt = now - 8 * 86400.0;
                p.cornerAskNow();
                p.answer("corner", 0);
                logf("corner locked desk: '%s'", narrow(a.bubble.text).c_str());
                check(p.vit.cornerState == 0 && !exists(ldesk + L"\\Уголок Узи") && !exists(ldesk + L"\\Уголок Узи (2)"), "locked desktop: nothing made");
                check(!p.cornerCanAsk(now + 1.1 * 86400.0) && p.cornerCanAsk(now + 7.1 * 86400.0), "locked desktop: again in a week, not tomorrow");
                p.cornerDeskTest = keepDesk;
            } else {
                logf("corner test: can't lock a test folder (locked %d, probe %d), locked case skipped", locked, probe);
            }
        }
        sim(3.f, still);
        a.bubble.close();
        CreateDirectoryW((fdesk + L"\\уголок узи").c_str(), nullptr);
        p.vit.rel = 50.f;
        p.vit.cornerAskedAt = now - 8 * 86400.0;
        p.cornerAskNow();
        logf("corner ask tier 1: '%s'", narrow(a.bubble.text).c_str());
        p.answer("corner", 0);
        logf("corner made tier 1: '%s'", narrow(a.bubble.text).c_str());
        check(exists(cdir) && p.vit.cornerState == 1, "made again at tier 1");
        check(!exists(fdesk + L"\\Уголок Узи (2)") && exists(fdesk + L"\\уголок узи\\Не трогать.txt"), "lowercase folder of that name: reused, note inside");
        check(realState() == real0, "real desktop untouched");
        logf("corner real desktop: '%s' -> '%s'", narrow(real0).c_str(), narrow(realState()).c_str());
        sim(3.f, still);
        a.bubble.close();
        p.vit.cornerState = 0;
        p.cornerDir.clear();
        p.cornerFiles.clear();
        p.cornerSave();
        p.cornerDeskTest.clear();
        p.cornerSayCat.clear();
        p.warmT = warm0;
        p.vit.rel = 50.f;
        logf("corner tests: %d failed", fails);
    }
