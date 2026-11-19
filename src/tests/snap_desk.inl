
    {
        auto& I = a.icons;
        std::wstring fd = dir + L"\\fakedesk";
        const POINT sp{96, 110};
        auto cellPos = [&](int c, int r) { return POINT{20 + c * sp.x, 4 + r * sp.y}; };
        ComPtr<IDWriteTextFormat> lbl;
        a.gfx.dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.f * a.dpi,
                                   L"ru-ru", &lbl);
        if (lbl) lbl->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        g_snapUnder = [&](DrawCtx& c, V2 wo) {
            for (auto& d : I.items) {
                D2D1_RECT_F r = D2D1::RectF(d.img.left - wo.x, d.img.top - wo.y, d.img.right - wo.x, d.img.bottom - wo.y);
                c.color(d.folder ? D2D1::ColorF(0.95f, 0.75f, 0.25f, 1) : D2D1::ColorF(0.35f, 0.55f, 0.9f, 1));
                c.rt->FillRoundedRectangle(D2D1::RoundedRect(r, 5, 5), c.br);
                if (p.deskHides(d)) {
                    c.color(D2D1::ColorF(1, 0, 0, 1));
                    c.rt->DrawRoundedRectangle(D2D1::RoundedRect(r, 5, 5), c.br, 2.5f);
                }
                if (lbl) {
                    c.color(D2D1::ColorF(0.1f, 0.1f, 0.15f, 1));
                    c.rt->DrawTextW(d.name.c_str(), (UINT32)d.name.size(), lbl.Get(),
                                    D2D1::RectF(d.cell.left - wo.x, d.img.bottom - wo.y + 3, d.cell.right - wo.x, d.cell.bottom - wo.y), c.br);
                }
            }
        };
        struct FI { const wchar_t* n; int c, r; bool folder; };
        auto surfs = [&]() {
            a.world.setFake(RECT{0, 0, 1920, 1080}, RECT{0, 0, 1920, 1020});
            std::vector<IconBox> boxes;
            for (size_t i = 0; i < I.items.size(); i++)
                if (!p.deskHides(I.items[i]) && a.world.rectVisible(I.items[i].img)) boxes.push_back({(int)i, I.items[i].img});
            a.world.addIconSurfs(std::move(boxes), I.spacing.x);
        };
        std::wstring fh = dir + L"\\fakehands";
        CreateDirectoryW(fh.c_str(), nullptr);
        int strayHidden = 0, strayHands = 0;
        auto hiddenIn = [&](const std::wstring& path) {
            DWORD at = GetFileAttributesW(path.c_str());
            return at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_HIDDEN);
        };
        auto handsList = [&]() {
            std::vector<std::wstring> v;
            WIN32_FIND_DATAW wf;
            HANDLE h = FindFirstFileW((fh + L"\\*").c_str(), &wf);
            if (h != INVALID_HANDLE_VALUE) {
                do
                    if (wcscmp(wf.cFileName, L".") && wcscmp(wf.cFileName, L"..")) v.push_back(wf.cFileName);
                while (FindNextFileW(h, &wf));
                FindClose(h);
            }
            return v;
        };
        auto inHands = [&]() { return (int)handsList().size(); };
        std::function<void(const std::wstring&)> wipe = [&](const std::wstring& d) {
            if (d.size() < dir.size() + 2 || _wcsnicmp(d.c_str(), (dir + L"\\").c_str(), dir.size() + 1) != 0) return;
            WIN32_FIND_DATAW wf;
            HANDLE h = FindFirstFileW((d + L"\\*").c_str(), &wf);
            if (h == INVALID_HANDLE_VALUE) return;
            do {
                if (!wcscmp(wf.cFileName, L".") || !wcscmp(wf.cFileName, L"..")) continue;
                std::wstring q = d + L"\\" + wf.cFileName;
                SetFileAttributesW(q.c_str(), FILE_ATTRIBUTE_NORMAL);
                if (wf.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    wipe(q);
                    RemoveDirectoryW(q.c_str());
                } else {
                    DeleteFileW(q.c_str());
                }
            } while (FindNextFileW(h, &wf));
            FindClose(h);
        };
        auto setup = [&](const std::vector<FI>& list) {
            p.dropHeld("test");
            p.dropCarried(false);
            {
                WIN32_FIND_DATAW wf;
                HANDLE h = FindFirstFileW((fd + L"\\*").c_str(), &wf);
                if (h != INVALID_HANDLE_VALUE) {
                    do {
                        if (wf.cFileName[0] == L'.' || !(wf.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) continue;
                        strayHidden++;
                        logf("desk setup: '%s' was left HIDDEN by the last scene", narrow(wf.cFileName).c_str());
                    } while (FindNextFileW(h, &wf));
                    FindClose(h);
                }
            }
            for (auto& n : handsList()) {
                strayHands++;
                logf("desk setup: '%s' was LEFT IN HER HANDS by the last scene", narrow(n).c_str());
            }
            DeleteFileW((appDataDir() + L"\\desk_carry.txt").c_str());
            CreateDirectoryW(fd.c_str(), nullptr);
            wipe(fd);
            wipe(fh);
            I.setFake(fd, sp, (int)lroundf(48 * a.dpi));
            I.fakeHands = fh;
            for (auto& f : list) {
                std::wstring path = fd + L"\\" + f.n;
                if (f.folder) CreateDirectoryW(path.c_str(), nullptr);
                else writeFile(path, "test\r\n");
                I.addFake(f.n, cellPos(f.c, f.r), f.folder);
            }
            surfs();
            reset(St::Idle, 100);
            p.pos = V2(700, 1020);
            sim(0.4f, still);
        };
        std::vector<FI> base = {
            {L"Корзина", 0, 0, false}, {L"Документы", 0, 1, true}, {L"Проекты", 0, 2, true}, {L"фото.png", 0, 3, false}, {L"заметки.txt", 0, 4, false},
            {L"Minecraft.lnk", 0, 5, false}, {L"музыка", 0, 6, true}, {L"отчёт_старый.docx", 0, 7, false}, {L"игра.exe", 0, 8, false},
            {L"Отчёт.txt", 1, 3, false}, {L"скрин.png", 1, 4, false}, {L"Новая папка", 1, 5, true}, {L"план.md", 1, 6, false}, {L"архив.zip", 1, 7, false},
            {L"видео.mp4", 1, 8, false}, {L"рисунок.kra", 2, 6, false}, {L"билд.blend", 2, 7, false}, {L"шейдер.fsh", 2, 8, false},
        };
        std::wstring rep = fd + L"\\Отчёт.txt";
        auto posOf = [&](const wchar_t* n) { int i = I.find(n); return i >= 0 ? I.items[i].pos : POINT{-9999, -9999}; };
        auto deskLog = [&](const char* what) {
            POINT rp = posOf(L"Отчёт.txt");
            logf("desk %s: mode %d goal %d state %s pos %.0f,%.0f icon %ld,%ld (on the desk %d) in her hands %d snap %d held '%s' bubble '%s'", what, p.deskMode,
                 p.deskGoal, p.stateName(), p.pos.x, p.pos.y, rp.x, rp.y, (int)(I.findPath(rep) >= 0), inHands(), (int)I.fakeSnap,
                 narrow(p.heldPath).c_str(), narrow(a.bubble.text).c_str());
        };
        auto settle = [&]() {
            for (int k = 0; k < 400 && I.pendingAny() && !I.fakeDead; k++) sim(0.02f, still);
        };
        int visChecked = 0, visBad = 0;
        auto vis = [&](const char* what) {
            settle();
            if (!I.fakeDead) I.refresh();
            int i = I.findPath(rep);
            if (i < 0) i = I.find(L"Отчёт (2).txt");
            std::wstring fp = i >= 0 ? I.items[i].path : rep;
            bool hid = hiddenIn(fp) || hiddenIn(fd + L"\\Отчёт (2).txt") || (!p.deskOn() && inHands() > 0);
            if (p.deskOn() || i < 0) {
                if (!p.deskOn()) {
                    visChecked++;
                    visBad += hid;
                }
                logf("desk visible %s: carry on %d, icon %s, file hidden or in her hands %d", what, (int)p.deskOn(), i < 0 ? "not on the desk" : "on the desk",
                     (int)hid);
                return;
            }
            const DIcon& d = I.items[i];
            int over = 0;
            for (auto& o : I.items) {
                RECT x, a1{d.cell.left + 2, d.cell.top + 2, d.cell.right - 2, d.cell.bottom - 2}, b1{o.cell.left + 2, o.cell.top + 2, o.cell.right - 2, o.cell.bottom - 2};
                if (&o != &d && IntersectRect(&x, &a1, &b1)) over++;
            }
            bool ok = a.world.monitorAt(V2((d.img.left + d.img.right) * 0.5f, (d.img.top + d.img.bottom) * 0.5f)) >= 0 && a.world.rectVisible(d.img) &&
                      I.onGrid(d.pos) && I.cellOnScreen(d.pos) && !hid && !over;
            visChecked++;
            visBad += !ok;
            logf("desk visible %s: %d at %ld,%ld (screen %ld,%ld) grid %d shown %d hidden/in hands %d overlaps %d", what, (int)ok, d.pos.x, d.pos.y, d.img.left, d.img.top,
                 (int)I.onGrid(d.pos), (int)I.cellOnScreen(d.pos), (int)hid, over);
        };
        auto run = [&](float secs, const wchar_t* throwShot, const wchar_t* offerShot, std::function<void()> each) {
            int last = p.deskMode;
            bool thrown = false, offered = false;
            float tt = 0;
            for (; tt < secs && p.deskOn(); tt += 0.02f) {
                sim(0.02f, still);
                if (each) each();
                if (p.deskMode != last) {
                    logf("desk mode %d -> %d at %.1f s: state %s pos %.0f,%.0f bubble '%s'", last, p.deskMode, tt, p.stateName(), p.pos.x, p.pos.y,
                         narrow(a.bubble.text).c_str());
                    last = p.deskMode;
                }
                if (throwShot && !thrown && p.deskThrowT > 0.12f) {
                    thrown = true;
                    shot(throwShot);
                }
                if (offerShot && !offered && p.deskMode == Pet::DeskOffer && p.deskT > 1.6f) {
                    offered = true;
                    shot(offerShot);
                }
            }
            sim(0.1f, still);
            settle();
            logf("desk run done after %.1f s: on %d, bubble '%s'", tt, (int)p.deskOn(), narrow(a.bubble.text).c_str());
        };
        p.vit.rel = 75.f;
        setup(base);
        POINT home0 = posOf(L"Отчёт.txt");
        p.receiveFile(rep, 1);
        {
            std::string h1, h2;
            readFile(appDataDir() + L"\\icons_home.txt", h1);
            readFile(appDataDir() + L"\\desk_carry.txt", h2);
            logf("desk a: on %d home %ld,%ld (was %ld,%ld) snap %d, icons_home '%s', desk_carry '%s'", (int)p.deskOn(), p.deskHome.x, p.deskHome.y, home0.x,
                 home0.y, (int)I.fakeSnap, h1.c_str(), h2.c_str());
        }
        for (int k = 0; k < 3; k++) {
            sim(1.f, still);
            deskLog("hold");
        }
        logf("desk a: standTop %.0f comH %.0f hands at %.0f,%.0f from feet, icon %dpx spacing %ldx%ld", p.standTop, p.comH, p.deskHandOff.x,
             p.deskHandOff.y, I.iconPx, I.spacing.x, I.spacing.y);
        shot(L"74_carry_hold");
        p.goWalk(900);
        sim(1.2f, still);
        deskLog("walking");
        sim(0.5f, still);
        p.beginDrag(p.headTop + V2(0, 6));
        V2 dg = p.headTop + V2(0, 6);
        sim(0.8f, [&](float tt) { return dg + V2(-tt * 300.f, -tt * 200.f); });
        deskLog("dragged");
        p.endDrag();
        sim(1.5f, still);
        deskLog("dropped");

        sim(std::max(0.f, p.heldLife - p.heldT) + 0.1f, still);
        deskLog("confused");
        run(45.f, L"74b_carry_return", nullptr, nullptr);
        POINT fin = posOf(L"Отчёт.txt");
        logf("desk b: final %ld,%ld home %ld,%ld exact %d snap %d held '%s'", fin.x, fin.y, home0.x, home0.y, (int)(fin.x == home0.x && fin.y == home0.y),
             (int)I.fakeSnap, narrow(p.heldPath).c_str());
        vis("b return");
        sim(0.6f, still);
        shot(L"74b2_carry_returned");

        setup(base);
        p.receiveFile(rep, 1);
        sim(std::max(0.f, p.heldLife - p.heldT) + 0.1f, still);
        deskLog("confused c");
        sim(1.6f, still);
        I.fakeMove(L"план.md", home0);
        surfs();
        logf("desk c: 'план.md' moved into her home cell %ld,%ld", home0.x, home0.y);
        run(60.f, L"74c2_carry_self_place", L"74c_carry_taken_home", nullptr);
        fin = posOf(L"Отчёт.txt");
        {
            RECT fc = I.cellAt(fin);
            int over = 0;
            for (auto& d : I.items) {
                RECT x;
                if (d.name != L"Отчёт.txt" && IntersectRect(&x, &fc, &d.cell) && (x.right - x.left) > 4 && (x.bottom - x.top) > 4) over++;
            }
            logf("desk c: self placed at %ld,%ld (home %ld,%ld), overlapping icons %d, snap %d", fin.x, fin.y, home0.x, home0.y, over, (int)I.fakeSnap);
        }
        vis("c self place");

        {
            std::vector<FI> full;
            std::vector<std::wstring> names;
            for (int c = 0; c < 19; c++)
                for (int r = 0; r < 9; r++) {
                    if (c == 1 && r == 3) { full.push_back({L"Отчёт.txt", 1, 3, false}); continue; }
                    bool fold = (c == 5 && r == 8) || (c == 12 && r == 8) || (c == 0 && r == 1);
                    wchar_t nm[32];
                    swprintf(nm, 32, fold ? L"Папка %d-%d" : L"файл %d-%d.txt", c, r);
                    names.push_back(nm);
                }
            int k = 0;
            for (int c = 0; c < 19; c++)
                for (int r = 0; r < 9; r++) {
                    if (c == 1 && r == 3) continue;
                    bool fold = (c == 5 && r == 8) || (c == 12 && r == 8) || (c == 0 && r == 1);
                    full.push_back({names[k++].c_str(), c, r, fold});
                }
            setup(full);
            logf("desk d: %zu fake icons", I.items.size());
            p.receiveFile(rep, 1);
            sim(1.f, still);
            writeFile(fd + L"\\Новый файл.txt", "test\r\n");
            I.addFake(L"Новый файл.txt", home0, false);
            surfs();
            sim(std::max(0.f, p.heldLife - p.heldT) + 0.1f, still);
            run(60.f, L"74d2_carry_to_folder", L"74d_carry_full", nullptr);
            bool inFolder = !p.deskFolder.empty() && GetFileAttributesW((p.deskFolder + L"\\Отчёт.txt").c_str()) != INVALID_FILE_ATTRIBUTES;
            logf("desk d: rep on desk %d, folder '%s', in the folder by its own name %d, left in her hands %d", I.findPath(rep) >= 0,
                 narrow(p.deskFolder).c_str(), (int)inFolder, inHands());
            visChecked++;
            visBad += !inFolder || inHands() > 0;
        }

        setup(base);
        p.receiveFile(rep, 1);
        sim(3.f, still);
        logf("desk e: hit at card %d, at head %d, at feet %d, card %.0f,%.0f", (int)p.heldHit(p.carriedCenter), (int)p.heldHit(p.headC),
             (int)p.heldHit(p.pos - V2(0, 5)), p.carriedCenter.x, p.carriedCenter.y);
        heldDragOut();
        p.heldRetake = true;
        p.heldDragEnd(DROPEFFECT_COPY, p.carriedCenter, false);
        sim(0.3f, still);
        deskLog("e click only");
        heldDragOut();
        sim(0.3f, still);
        p.heldDragLeft();
        sim(0.7f, still);
        logf("desk e: dragging %d shown %d bubble '%s'", (int)p.heldDragging, (int)p.heldShown(), narrow(a.bubble.text).c_str());
        shot(L"74e_carry_drag_out");
        p.heldRetake = true;
        p.heldDragEnd(DROPEFFECT_COPY, p.headC, false);
        sim(1.f, still);
        deskLog("e retaken");
        heldDragOut();
        p.heldDragLeft();
        sim(1.f, still);
        p.heldDragEnd(DROPEFFECT_NONE, V2(1000, 300), true);
        sim(0.2f, still);
        deskLog("e dropped on desktop at 1000,300");
        vis("e dropped on desktop");
        p.receiveFile(rep, 1);
        sim(1.f, still);
        heldDragOut();
        p.heldDragLeft();
        sim(1.f, still);
        p.heldDragEnd(DROPEFFECT_NONE, V2(1500, 900), false);
        sim(0.2f, still);
        settle();
        logf("desk e: cancelled away from desktop -> %ld,%ld (home of this carry %ld,%ld)", posOf(L"Отчёт.txt").x, posOf(L"Отчёт.txt").y, p.deskHome.x,
             p.deskHome.y);
        vis("e cancelled away from desktop");
        p.receiveFile(rep, 1);
        sim(1.f, still);
        heldDragOut();
        p.heldDragLeft();
        sim(1.f, still);
        std::wstring moved = fd + L"\\Документы\\Отчёт.txt";
        DeleteFileW(moved.c_str());
        MoveFileW(rep.c_str(), moved.c_str());
        logf("desk e: moved into a folder: hidden there %d, in her hands %d (must be 0 0: given back before the drag)", (int)hiddenIn(moved), inHands());
        I.fakeForget(rep);
        p.heldDragEnd(DROPEFFECT_MOVE, V2(60, 120), false);
        sim(0.2f, still);
        deskLog("e moved into a folder");
        vis("e moved into a folder");
        MoveFileW(moved.c_str(), rep.c_str());

        setup(base);
        p.receiveFile(rep, 1);
        sim(2.f, still);
        p.sleepNow();
        sim(0.1f, still);
        deskLog("f asleep");
        vis("f asleep");
        setup(base);
        p.receiveFile(rep, 1);
        sim(std::max(0.f, p.heldLife - p.heldT) + 0.1f, still);
        sim(2.5f, still);
        deskLog("f on the way");
        sessionBye(false);
        deskLog("f shutdown bye");
        vis("f shutdown bye");
        p.byeCancel();
        setup(base);
        p.receiveFile(rep, 1);
        sim(1.f, still);
        p.deskHomeNow("exit");
        deskLog("f exit");
        vis("f exit");
        setup(base);
        p.receiveFile(rep, 1);
        sim(1.f, still);
        writeFile(rep, "someone else\r\n");
        I.addFake(L"Отчёт.txt", cellPos(5, 0), false);
        sim(0.5f, still);
        p.deskHomeNow("exit");
        settle();
        {
            std::string other;
            readFile(rep, other);
            std::wstring two = fd + L"\\Отчёт (2).txt";
            POINT tp = posOf(L"Отчёт (2).txt"), op = posOf(L"Отчёт.txt");
            bool ok = other == "someone else\r\n" && GetFileAttributesW(two.c_str()) != INVALID_FILE_ATTRIBUTES && tp.x == home0.x && tp.y == home0.y &&
                      op.x == cellPos(5, 0).x && op.y == cellPos(5, 0).y && !inHands() && !hiddenIn(two);
            visChecked++;
            visBad += !ok;
            logf("desk f name taken: ok %d: other file kept %d at %ld,%ld, ours back as '(2)' %d at %ld,%ld (home %ld,%ld), in her hands %d", (int)ok,
                 (int)(other == "someone else\r\n"), op.x, op.y, (int)(GetFileAttributesW(two.c_str()) != INVALID_FILE_ATTRIBUTES), tp.x, tp.y, home0.x,
                 home0.y, inHands());
        }

        {
            setup(base);
            std::wstring nf = fd + L"\\Новая папка";
            writeFile(nf + L"\\внутри.txt", "inside\r\n");
            POINT fh0 = posOf(L"Новая папка");
            p.receiveFile(nf, 1);
            sim(0.5f, still);
            bool gone = GetFileAttributesW(nf.c_str()) == INVALID_FILE_ATTRIBUTES;
            int kind = p.deskRec.kind;
            p.deskHomeNow("test");
            settle();
            POINT f = posOf(L"Новая папка");
            DWORD at = GetFileAttributesW(nf.c_str());
            bool ok = gone && kind == 1 && at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_DIRECTORY) &&
                      GetFileAttributesW((nf + L"\\внутри.txt").c_str()) != INVALID_FILE_ATTRIBUTES && f.x == fh0.x && f.y == fh0.y && !inHands();
            visChecked++;
            visBad += !ok;
            logf("desk f folder: ok %d: in her hands as a folder %d (kind %d), back a folder with its file %d, home exact %d, in her hands %d", (int)ok,
                 (int)gone, kind, (int)(at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_DIRECTORY)), (int)(f.x == fh0.x && f.y == fh0.y), inHands());
        }

        setup(base);
        wchar_t art[MAX_PATH];
        GetFullPathNameW((exeDir() + L"\\..\\art\\uzi_ref.webp").c_str(), MAX_PATH, art, nullptr);
        p.receiveFile(art, 1);
        logf("desk g: art file desk %d", (int)p.deskOn());
        if (p.senseJob)
            for (int i = 0; i < 600 && !p.senseJob->done; i++) Sleep(5);
        sim(p.heldLife + 1.f, still);
        logf("desk g: after hold held '%s' bubble '%s'", narrow(p.heldPath).c_str(), narrow(a.bubble.text).c_str());
        p.receiveFile(rep, 2);
        logf("desk g: two files desk %d", (int)p.deskOn());
        p.dropHeld("test");
        I.fakeAuto = true;
        p.receiveFile(rep, 1);
        logf("desk g: auto-arrange desk %d, icon at %ld,%ld", (int)p.deskOn(), posOf(L"Отчёт.txt").x, posOf(L"Отчёт.txt").y);
        I.fakeAuto = false;
        p.dropHeld("test");
        p.receiveFile(art, 1);
        sim(3.f, still);
        heldDragOut();
        p.heldDragLeft();
        sim(1.f, still);
        p.heldDragEnd(DROPEFFECT_COPY, V2(300, 300), false);
        logf("desk g: art taken back, held '%s' bubble '%s'", narrow(p.heldPath).c_str(), narrow(a.bubble.text).c_str());

        for (float rel : {15.f, 96.f}) {
            p.vit.rel = rel;
            setup(base);
            p.receiveFile(rep, 1);
            sim(std::max(0.f, p.heldLife - p.heldT) + 0.1f, still);
            logf("desk h rel %.0f: confused '%s'", rel, narrow(a.bubble.text).c_str());
            I.fakeMove(L"план.md", home0);
            surfs();
            for (int i = 0; i < 1500 && !(p.deskMode == Pet::DeskOffer && p.deskT > 1.6f); i++) sim(0.02f, still);
            logf("desk h rel %.0f: home taken '%s' state %s", rel, narrow(a.bubble.text).c_str(), p.stateName());
            shot(rel < 30 ? L"74f_offer_hate" : L"74g_offer_adores");
            heldDragOut();
            p.heldDragLeft();
            sim(1.f, still);
            logf("desk h rel %.0f: taken line '%s'", rel, narrow(a.bubble.text).c_str());
            p.heldDragEnd(DROPEFFECT_NONE, V2(1500, 900), false);
            logf("desk h rel %.0f: after take icon at %ld,%ld", rel, posOf(L"Отчёт.txt").x, posOf(L"Отчёт.txt").y);
            vis("h taken and dropped away");
            I.fakeMove(L"план.md", cellPos(1, 6));
            I.fakeMove(L"Отчёт.txt", home0);
            surfs();
            p.receiveFile(rep, 1);
            sim(std::max(0.f, p.heldLife - p.heldT) + 0.1f, still);
            I.fakeMove(L"план.md", home0);
            surfs();
            run(60.f, nullptr, nullptr, nullptr);
            vis("h self place");
            p.dropHeld("test");
        }
        p.vit.rel = 75.f;

        auto fileState = [&](const char* what) {
            settle();
            std::string h1, h2;
            readFile(appDataDir() + L"\\icons_home.txt", h1);
            readFile(appDataDir() + L"\\desk_carry.txt", h2);
            bool flag = GetFileAttributesW((appDataDir() + L"\\snap_off.flag").c_str()) != INVALID_FILE_ATTRIBUTES;
            POINT rp = posOf(L"Отчёт.txt");
            logf("desk %s: on %d icon %ld,%ld (home %ld,%ld exact %d) in her hands %d snap %d snap_off.flag %d icons_home '%s' desk_carry '%s'", what,
                 (int)p.deskOn(), rp.x, rp.y, home0.x, home0.y, (int)(rp.x == home0.x && rp.y == home0.y), inHands(), (int)I.fakeSnap, (int)flag,
                 h1.c_str(), h2.c_str());
            vis(what);
        };
        setup(base);
        p.receiveFile(rep, 1);
        sim(1.f, still);
        I.fakeDead = true;
        p.deskHomeNow("session end");
        fileState("i dead at shutdown");
        I.fakeDead = false;
        iconsRevive(10.f);
        fileState("i explorer back");
        setup(base);
        p.receiveFile(rep, 1);
        sim(std::max(0.f, p.heldLife - p.heldT) + 0.1f, still);
        I.fakeDead = true;
        a.bubble.close();
        run(60.f, nullptr, nullptr, nullptr);
        fileState("i dead during return");
        I.fakeDead = false;
        iconsRevive(10.f);
        fileState("i explorer back 2");

        setup(base);
        writeFile(appDataDir() + L"\\icons_home.txt", narrow(L"Отчёт.txt") + "\t5\t5\n");
        p.receiveFile(rep, 1);
        logf("desk j: own home line %d", (int)p.deskOwnHome);
        p.deskHomeNow("test");
        fileState("j after carry with an older line");
        DeleteFileW((appDataDir() + L"\\icons_home.txt").c_str());
        p.receiveFile(rep, 1);
        logf("desk j: own home line %d", (int)p.deskOwnHome);
        p.deskHomeNow("test");
        fileState("j after carry with its own line");

        setup(base);
        p.strollOn = true;
        p.receiveFile(rep, 1);
        {
            int sat = 0;
            for (int i = 0; i < 100; i++) {
                sim(0.05f, still);
                sat += p.st == St::Sit;
            }
            logf("desk k: stroll, frames sitting while holding %d/100", sat);
        }
        sim(std::max(0.f, p.heldLife - p.heldT) + 0.1f, still);
        run(45.f, nullptr, nullptr, [&] {
            if (p.st == St::Sit) logf("desk k: sitting during the return!");
        });
        fileState("k stroll return");
        p.strollOn = false;

        {
            std::vector<FI> withArt = base;
            withArt.push_back({L"Uzi Doorman art.png", 2, 5, false});
            setup(withArt);
            std::wstring artP = fd + L"\\Uzi Doorman art.png";
            p.receiveFile(artP, 1);
            if (p.senseJob)
                for (int i = 0; i < 600 && !p.senseJob->done; i++) Sleep(5);
            sim(3.f, still);
            logf("desk l: desk %d self %d kind %s, stage-1 line '%s'", (int)p.deskOn(), p.heldSense.self, fileKindName(p.heldSense.kind),
                 narrow(a.bubble.text).c_str());
            sim(std::max(0.f, p.heldLife - p.heldT) + 0.1f, still);
            logf("desk l: return line '%s'", narrow(a.bubble.text).c_str());
            run(45.f, nullptr, nullptr, nullptr);
            vis("l recognised");
        }

        auto tripRun = [&](const char* tag, const wchar_t* fallShot, const wchar_t* pickShot, std::function<void()> onDown) {
            int last = p.deskMode;
            bool down = false, picked = false;
            float tt = 0;
            for (; tt < 60.f && p.deskOn(); tt += 0.02f) {
                sim(0.02f, still);
                if (p.deskMode != last) {
                    logf("desk %s: mode %d -> %d at %.1f s: state %s pos %.0f,%.0f bubble '%s'", tag, last, p.deskMode, tt, p.stateName(), p.pos.x, p.pos.y,
                         narrow(a.bubble.text).c_str());
                    last = p.deskMode;
                }
                if (!down && p.deskMode == Pet::DeskTrip && p.tripPh == 1 && p.st == St::Trip && p.stT > 0.9f) {
                    down = true;
                    int i = I.findPath(rep);
                    logf("desk %s down: rot %.2f pos %.0f,%.0f shown %d icon %ld,%ld (trip cell %ld,%ld) visible-behind-her %d upset %.1f bubble '%s'", tag, p.rot,
                         p.pos.x, p.pos.y, (int)p.heldShown(), i >= 0 ? I.items[i].pos.x : -1L, i >= 0 ? I.items[i].pos.y : -1L, p.tripCell.x, p.tripCell.y,
                         (int)(i >= 0 && (I.items[i].pos.x != p.tripCell.x || I.items[i].pos.y != p.tripCell.y)), p.tripUpset, narrow(a.bubble.text).c_str());
                    if (fallShot) shot(fallShot);
                    if (onDown) onDown();
                    if (!p.deskOn()) break;
                }
                if (!picked && p.deskMode == Pet::DeskTrip && p.tripLiftT > 0.1f) {
                    picked = true;
                    deskLog("trip pick up");
                    if (pickShot) shot(pickShot);
                }
            }
            sim(0.1f, still);
            logf("desk %s: run done after %.1f s, down %d picked %d on %d state %s bubble '%s'", tag, tt, (int)down, (int)picked, (int)p.deskOn(),
                 p.stateName(), narrow(a.bubble.text).c_str());
            vis(tag);
        };
        auto tripSetup = [&]() {
            setup(base);
            p.tripForce = 1;
            p.receiveFile(rep, 1);
            sim(std::max(0.f, p.heldLife - p.heldT) + 0.1f, still);
        };
        auto petHead = [&](float secs) {
            p.petBlock = 0;
            p.lastPetEnd = -100;
            V2 ph = p.headC;
            for (float k = 0; k < secs; k += 0.25f) sim(0.25f, [&](float tt) { return ph + V2(sinf((float)a.now * 9.f + tt) * 30.f, 0); });
        };
        tripSetup();
        tripRun("m1", L"75_stumble_fall", L"75b_stumble_pickup", nullptr);
        fin = posOf(L"Отчёт.txt");
        logf("desk m1: final %ld,%ld home %ld,%ld exact %d snap %d held '%s'", fin.x, fin.y, home0.x, home0.y, (int)(fin.x == home0.x && fin.y == home0.y),
             (int)I.fakeSnap, narrow(p.heldPath).c_str());
        sim(0.6f, still);
        shot(L"75c_stumble_done");
        for (float rel : {75.f, 20.f}) {
            p.vit.rel = rel;
            tripSetup();
            float r0 = p.vit.rel;
            tripRun(rel > 50 ? "m2" : "m2b", nullptr, nullptr, [&] {
                petHead(2.5f);
                logf("desk m2 rel %.0f: after petting while down rel %.1f -> %.1f upset %.1f petting %d state %s blush %.2f bubble '%s'", rel, r0, p.vit.rel,
                     p.tripUpset, (int)p.petting, p.stateName(), p.face.blush, narrow(a.bubble.text).c_str());
                if (rel > 50) shot(L"75d_stumble_comfort");
            });
            fin = posOf(L"Отчёт.txt");
            logf("desk m2 rel %.0f: final %ld,%ld exact %d rel %.1f", rel, fin.x, fin.y, (int)(fin.x == home0.x && fin.y == home0.y), p.vit.rel);
        }
        p.vit.rel = 95.f;
        tripSetup();
        tripRun("m2c", nullptr, nullptr, [&] {
            petHead(34.f);
            logf("desk m2c: after 34 s of petting: on %d mode %d trip phase %d state %s petting %d tripT %.1f", (int)p.deskOn(), p.deskMode, p.tripPh,
                 p.stateName(), (int)p.petting, p.tripT);
        });
        fin = posOf(L"Отчёт.txt");
        logf("desk m2c: final %ld,%ld exact %d", fin.x, fin.y, (int)(fin.x == home0.x && fin.y == home0.y));
        p.vit.rel = 75.f;
        tripSetup();
        tripRun("m3", nullptr, nullptr, [&] {
            settle();
            I.fakeMove(L"Отчёт.txt", cellPos(4, 2));
            surfs();
            sim(1.f, still);
            fileState("m3 dragged away from the floor");
            logf("desk m3: held '%s' state %s bubble '%s'", narrow(p.heldPath).c_str(), p.stateName(), narrow(a.bubble.text).c_str());
        });
        tripSetup();
        tripRun("m3b", nullptr, nullptr, [&] {
            settle();
            std::wstring moved = fd + L"\\Документы\\Отчёт.txt";
            DeleteFileW(moved.c_str());
            MoveFileW(rep.c_str(), moved.c_str());
            logf("desk m3b: moved into a folder from the floor, hidden there %d, in her hands %d", (int)hiddenIn(moved), inHands());
            I.fakeForget(rep);
            surfs();
            sim(1.f, still);
            fileState("m3b moved into a folder from the floor");
            logf("desk m3b: held '%s' bubble '%s'", narrow(p.heldPath).c_str(), narrow(a.bubble.text).c_str());
            MoveFileW(moved.c_str(), rep.c_str());
        });
        tripSetup();
        tripRun("m3c", nullptr, nullptr, [&] {
            settle();
            std::wstring rn = fd + L"\\Отчёт (2).txt";
            DeleteFileW(rn.c_str());
            MoveFileW(rep.c_str(), rn.c_str());
            int i = I.findPath(rep);
            if (i >= 0) {
                I.items[i].path = rn;
                I.items[i].name = L"Отчёт (2).txt";
            }
            sim(1.f, still);
            settle();
            int j = I.findPath(rn);
            POINT rp = j >= 0 ? I.items[j].pos : POINT{-1, -1};
            logf("desk m3c: renamed on the floor: on %d icon %ld,%ld home %ld,%ld exact %d held '%s' bubble '%s'", (int)p.deskOn(), rp.x, rp.y, home0.x,
                 home0.y, (int)(rp.x == home0.x && rp.y == home0.y), narrow(p.heldPath).c_str(), narrow(a.bubble.text).c_str());
            fileState("m3c renamed on the floor");
            MoveFileW(rn.c_str(), rep.c_str());
            if (j >= 0) {
                I.items[j].path = rep;
                I.items[j].name = L"Отчёт.txt";
            }
        });
        tripSetup();
        tripRun("m4", nullptr, nullptr, [&] {
            sessionBye(false);
            fileState("m4 shutdown with the file on the floor");
            p.byeCancel();
        });
        tripSetup();
        tripRun("m5", nullptr, nullptr, [&] {
            settle();
            POINT h0 = p.deskHome;
            p.receiveFile(rep, 1);
            bool ok = p.deskOn() && p.deskHome.x == h0.x && p.deskHome.y == h0.y;
            logf("desk m5 check %s: floor file handed back: on %d mode %d home %ld,%ld (was %ld,%ld) bubble '%s'", ok ? "ok" : "FAIL", (int)p.deskOn(),
                 p.deskMode, p.deskHome.x, p.deskHome.y, h0.x, h0.y, narrow(a.bubble.text).c_str());
        });
        fin = posOf(L"Отчёт.txt");
        logf("desk m5 check %s: final %ld,%ld home %ld,%ld", fin.x == home0.x && fin.y == home0.y ? "ok" : "FAIL", fin.x, fin.y, home0.x, home0.y);
        p.tripForce = 0;
        {
            setup(base);
            p.peekPath.clear();
            p.peekTakeAt = -1;
            p.nextPeekAt = a.now + 1e6;
            p.receiveFile(rep, 1);
            sim(0.3f, still);
            std::wstring hands = p.deskRec.hands;
            HANDLE lock = CreateFileW(hands.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            bool twinMade = !hands.empty() && GetFileAttributesW(rep.c_str()) == INVALID_FILE_ATTRIBUTES && writeFile(rep, "twin\r\n");
            HandsRec tw;
            tw.path = rep;
            bool took = twinMade && I.take(tw);
            bool kept = I.inHands(rep);
            std::string twinNow;
            bool twinStays = readFile(rep, twinNow) && twinNow == "twin\r\n";
            if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
            bool ok = twinMade && !took && kept && twinStays;
            logf("desk m6 check %s: twin made %d, took %d, old record kept %d, twin untouched %d (hands '%s')", ok ? "ok" : "FAIL", (int)twinMade, (int)took,
                 (int)kept, (int)twinStays, narrow(hands).c_str());
            if (twinMade) DeleteFileW(rep.c_str());
            p.dropHeld("m6 done");
            sim(1.f, still);
            settle();
            logf("desk m6 check %s: original back on the desk %d, in her hands %d", GetFileAttributesW(rep.c_str()) != INVALID_FILE_ATTRIBUTES && !I.inHands(rep) ? "ok" : "FAIL",
                 (int)(GetFileAttributesW(rep.c_str()) != INVALID_FILE_ATTRIBUTES), (int)I.inHands(rep));
        }

        auto imgC = [&](int i) { return V2((I.items[i].img.left + I.items[i].img.right) * 0.5f, (I.items[i].img.top + I.items[i].img.bottom) * 0.5f); };
        auto others = [&](const std::vector<std::pair<std::wstring, POINT>>& was) {
            int moved = 0;
            for (auto& [n, pt] : was) {
                if (n == L"Отчёт.txt") continue;
                POINT q = posOf(n.c_str());
                moved += q.x != pt.x || q.y != pt.y;
            }
            return moved;
        };
        auto layoutNow = [&]() {
            std::vector<std::pair<std::wstring, POINT>> v;
            for (auto& d : I.items) v.push_back({d.name, d.pos});
            return v;
        };
        auto exists = [](const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; };
        {
            std::vector<FI> nearList = base;
            for (auto& f : nearList)
                if (!wcscmp(f.n, L"Отчёт.txt")) f = {L"Отчёт.txt", 6, 7, false};
            nearList.push_back({L"сосед слева.txt", 5, 7, false});
            nearList.push_back({L"сосед справа.txt", 7, 7, false});
            nearList.push_back({L"сосед сверху.txt", 6, 6, false});
            nearList.push_back({L"новенький.txt", 10, 7, false});
            setup(nearList);
            I.fakeShowHidden = true;
            POINT hm = posOf(L"Отчёт.txt");
            auto was = layoutNow();
            p.pos = V2((float)(hm.x + sp.x / 2 + 230), 1020.f);
            p.receiveFile(rep, 1);
            sim(1.5f, still);
            I.refresh();
            int moved0 = others(was);
            bool free0 = I.cellFree(hm, L""), listed = I.findPath(rep) >= 0, onDisk = exists(rep);
            POINT got{-1, -1};
            int ni = I.find(L"новенький.txt");
            bool put = ni >= 0 && I.put(I.items[ni], hm, "test: the user puts another icon into her file's cell", &got);
            surfs();
            std::string rec;
            readFile(appDataDir() + L"\\desk_carry.txt", rec);
            bool holdOk = free0 && !listed && !onDisk && put && got.x == hm.x && got.y == hm.y && inHands() == 1 && !hiddenIn(rep) && !moved0;
            visChecked++;
            visBad += !holdOk;
            logf("desk n hold (show hidden on): ok %d: cell free %d, icon listed %d, file on the desk %d, in her hands %d, another icon put there %d at "
                 "%ld,%ld (cell %ld,%ld), other icons moved %d, record '%s'",
                 (int)holdOk, (int)free0, (int)listed, (int)onDisk, inHands(), (int)put, got.x, got.y, hm.x, hm.y, moved0, rec.c_str());
            shot(L"82_hold_cell_free");
            sim(std::max(0.f, p.heldLife - p.heldT) + 0.1f, still);
            bool offered = false;
            run(60.f, nullptr, nullptr, [&] { offered |= p.deskMode == Pet::DeskOffer; });
            vis("n returned nearby");
            POINT f = posOf(L"Отчёт.txt"), nw = posOf(L"новенький.txt");
            rec.clear();
            readFile(appDataDir() + L"\\desk_carry.txt", rec);
            long dc = labs(f.x - hm.x) / sp.x, dr = labs(f.y - hm.y) / sp.y;
            bool backOk = offered && (f.x != hm.x || f.y != hm.y) && nw.x == hm.x && nw.y == hm.y && dc <= 2 && dr <= 2 && !inHands() && rec.empty();
            visChecked++;
            visBad += !backOk;
            logf("desk n returned: ok %d: held it out (cell taken) %d, at %ld,%ld, her cell %ld,%ld holds the other icon %d, %ld cells across %ld rows away, "
                 "in her hands %d, record left %d, bubble '%s'",
                 (int)backOk, (int)offered, f.x, f.y, hm.x, hm.y, (int)(nw.x == hm.x && nw.y == hm.y), dc, dr, inHands(), (int)!rec.empty(),
                 narrow(a.bubble.text).c_str());
            sim(0.4f, still);
            shot(L"82b_returned_nearby");
            I.fakeShowHidden = false;
        }

        for (int how = 0; how < 2; how++) {
            const char* tag = how ? "n2 crash filter" : "n2 crash record";
            setup(base);
            p.receiveFile(rep, 1);
            sim(1.f, still);
            std::string rec;
            readFile(appDataDir() + L"\\desk_carry.txt", rec);
            if (how) p.deskCrashHome();
            int hands0 = inHands();
            p.deskEnd(true);
            p.heldClear();
            bool all = I.restoreCarry();
            int hands1 = inHands();
            settle();
            POINT f = posOf(L"Отчёт.txt");
            std::string rec2;
            readFile(appDataDir() + L"\\desk_carry.txt", rec2);
            logf("desk %s: record '%s', in her hands after the crash %d, restore %d, in her hands after restore %d, icon at %ld,%ld home %ld,%ld exact %d, "
                 "record left %d",
                 tag, rec.c_str(), hands0, (int)all, hands1, f.x, f.y, home0.x, home0.y, (int)(f.x == home0.x && f.y == home0.y), (int)!rec2.empty());
            vis(tag);
        }
        {
            setup(base);
            writeFile(appDataDir() + L"\\desk_carry.txt", narrow(rep) + "\t" + std::to_string(home0.x) + "\t" + std::to_string(home0.y) + "\t0\t" +
                                                              narrow(L"Отчёт.txt") + "\n");
            I.fakeMove(L"Отчёт.txt", POINT{3000, 1500});
            bool all = I.restoreCarry();
            settle();
            POINT f = posOf(L"Отчёт.txt");
            logf("desk n2 old record: restore %d, icon at %ld,%ld home %ld,%ld exact %d", (int)all, f.x, f.y, home0.x, home0.y,
                 (int)(f.x == home0.x && f.y == home0.y));
            vis("n2 old record");
        }
        {
            setup(base);
            SetFileAttributesW(rep.c_str(), FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN);
            I.refresh();
            bool gone = I.findPath(rep) < 0;
            writeFile(appDataDir() + L"\\desk_carry.txt", narrow(rep) + "\t" + std::to_string(home0.x) + "\t" + std::to_string(home0.y) + "\t0\t" +
                                                              narrow(L"Отчёт.txt") + "\t21\t0\n");
            bool all = I.restoreCarry();
            settle();
            DWORD at = GetFileAttributesW(rep.c_str());
            POINT f = posOf(L"Отчёт.txt");
            std::string rec;
            readFile(appDataDir() + L"\\desk_carry.txt", rec);
            bool ok = gone && all && at != INVALID_FILE_ATTRIBUTES && !(at & FILE_ATTRIBUTE_HIDDEN) && (at & FILE_ATTRIBUTE_READONLY) && f.x == home0.x &&
                      f.y == home0.y && rec.empty();
            visChecked++;
            visBad += !ok;
            logf("desk n2 0.13.6 hidden record: ok %d: hidden then off the desk %d, restore %d, attributes now 0x%lX (read-only kept %d, hidden %d), icon "
                 "at %ld,%ld home exact %d, record left %d",
                 (int)ok, (int)gone, (int)all, (unsigned long)at, (int)((at & FILE_ATTRIBUTE_READONLY) != 0), (int)((at & FILE_ATTRIBUTE_HIDDEN) != 0), f.x,
                 f.y, (int)(f.x == home0.x && f.y == home0.y), (int)!rec.empty());
            SetFileAttributesW(rep.c_str(), FILE_ATTRIBUTE_NORMAL);
        }
        {
            setup(base);
            writeFile(fh + L"\\20261110-221500_сирота.txt", "orphan\r\n");
            writeFile(fh + L"\\20261110-221501_заметки.txt", "orphan 2\r\n");
            CreateDirectoryW((fh + L"\\20261110-221502_папка-сирота").c_str(), nullptr);
            writeFile(fh + L"\\20261110-221502_папка-сирота\\внутри.txt", "inside\r\n");
            bool all = I.restoreCarry();
            sim(1.f, still);
            settle();
            std::string mine, notes2;
            readFile(fd + L"\\заметки.txt", mine);
            readFile(fd + L"\\заметки (2).txt", notes2);
            bool ok = all && exists(fd + L"\\сирота.txt") && mine == "test\r\n" && notes2 == "orphan 2\r\n" &&
                      exists(fd + L"\\папка-сирота\\внутри.txt") && !inHands() && I.findPath(fd + L"\\сирота.txt") >= 0;
            visChecked++;
            visBad += !ok;
            logf("desk n2 orphans in her hands: ok %d: restore %d, 'сирота.txt' on the desk %d (icon %d), name taken -> 'заметки (2).txt' %d, ours kept %d, "
                 "folder with its file %d, left in her hands %d",
                 (int)ok, (int)all, (int)exists(fd + L"\\сирота.txt"), (int)(I.findPath(fd + L"\\сирота.txt") >= 0), (int)(notes2 == "orphan 2\r\n"),
                 (int)(mine == "test\r\n"), (int)exists(fd + L"\\папка-сирота\\внутри.txt"), inHands());
        }
        {
            setup(base);
            HANDLE lk = CreateFileW(rep.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            p.receiveFile(rep, 1);
            sim(0.5f, still);
            std::string rec;
            readFile(appDataDir() + L"\\desk_carry.txt", rec);
            bool carry = p.deskOn(), card = !p.heldPath.empty(), there = exists(rep), listed = I.findPath(rep) >= 0;
            if (lk != INVALID_HANDLE_VALUE) CloseHandle(lk);
            bool ok = lk != INVALID_HANDLE_VALUE && !carry && !card && p.deskBusy && there && listed && rec.empty() && !inHands() && !hiddenIn(rep);
            logf("desk n2 locked file: ok %d: carry %d, card in her hands %d (busy: none), file on the desk %d, icon listed %d, record '%s', in her hands %d",
                 (int)ok, (int)carry, (int)card, (int)there, (int)listed, rec.c_str(), inHands());
            p.dropHeld("test");
            I.fakeOtherVolume = true;
            p.receiveFile(rep, 1);
            sim(0.3f, still);
            bool ok2 = !p.deskOn() && !p.heldPath.empty() && exists(rep) && !inHands();
            logf("desk n2 other disk: ok %d: carry %d, card %d, file on the desk %d, in her hands %d", (int)ok2, (int)p.deskOn(), (int)!p.heldPath.empty(),
                 (int)exists(rep), inHands());
            I.fakeOtherVolume = false;
            p.dropHeld("test");
            visChecked += 2;
            visBad += !ok + !ok2;
        }
        {
            setup(base);
            POINT hm = posOf(L"Отчёт.txt");
            p.receiveFile(rep, 1);
            sim(1.f, still);
            p.dropHeld("test");
            for (int k = 0; k < 100 && I.findPath(rep) < 0; k++) sim(0.05f, still);
            bool waiting = I.pending(rep) && I.findPath(rep) >= 0;
            p.receiveFile(rep, 1);
            sim(5.5f, still);
            std::string rec;
            readFile(appDataDir() + L"\\desk_carry.txt", rec);
            int carry0 = p.deskOn(), hands0 = inHands(), desk0 = exists(rep);
            bool held = carry0 && hands0 == 1 && !desk0 && rec.find(narrow(fh)) != std::string::npos;
            p.dropHeld("test");
            vis("n7 taken again");
            POINT f = posOf(L"Отчёт.txt");
            std::string rec2;
            readFile(appDataDir() + L"\\desk_carry.txt", rec2);
            bool ok = waiting && held && f.x == hm.x && f.y == hm.y && !inHands() && rec2.empty() && !exists(fd + L"\\Отчёт (2).txt");
            visChecked++;
            visBad += !ok;
            logf("desk n7 taken again while the put waits: ok %d: put was waiting %d, held with a record %d (carry %d, in her hands %d, on the desk %d, "
                 "record has the hands path %d), back home exact %d, in her hands after %d, record left %d",
                 (int)ok, (int)waiting, (int)held, carry0, hands0, desk0, (int)(rec.find(narrow(fh)) != std::string::npos),
                 (int)(f.x == hm.x && f.y == hm.y), inHands(), (int)!rec2.empty());
        }
        {
            setup(base);
            POINT hm = cellPos(4, 2);
            std::wstring old = dir + L"\\oldesk\\переехал.txt", hp = fh + L"\\20261111-101500_переехал.txt";
            writeFile(hp, "moved\r\n");
            writeFile(appDataDir() + L"\\desk_carry.txt", narrow(old) + "\t" + std::to_string(hm.x) + "\t" + std::to_string(hm.y) + "\t0\t" +
                                                              narrow(L"переехал.txt") + "\t-\t0\t" + narrow(hp) + "\t0\t0\n");
            bool all = I.restoreCarry();
            settle();
            std::string body, rec;
            readFile(fd + L"\\переехал.txt", body);
            readFile(appDataDir() + L"\\desk_carry.txt", rec);
            POINT f = posOf(L"переехал.txt");
            bool ok = all && body == "moved\r\n" && !inHands() && rec.empty() && f.x == hm.x && f.y == hm.y && !exists(dir + L"\\oldesk");
            visChecked++;
            visBad += !ok;
            logf("desk n8 desktop moved: ok %d: restore %d, on the new desk %d, icon at %ld,%ld home %ld,%ld, in her hands %d, record left %d, old folder made %d",
                 (int)ok, (int)all, (int)(body == "moved\r\n"), f.x, f.y, hm.x, hm.y, inHands(), (int)!rec.empty(), (int)exists(dir + L"\\oldesk"));
        }
        {
            setup(base);
            std::function<void(const std::wstring&, const std::wstring&, std::string&)> tree = [&](const std::wstring& d, const std::wstring& rel,
                                                                                                  std::string& out) {
                std::vector<std::wstring> names;
                WIN32_FIND_DATAW wf;
                HANDLE h = FindFirstFileW((d + L"\\*").c_str(), &wf);
                if (h == INVALID_HANDLE_VALUE) return;
                do
                    if (wcscmp(wf.cFileName, L".") && wcscmp(wf.cFileName, L"..")) names.push_back(wf.cFileName);
                while (FindNextFileW(h, &wf));
                FindClose(h);
                std::sort(names.begin(), names.end());
                for (auto& n : names) {
                    std::wstring q = d + L"\\" + n;
                    DWORD at = GetFileAttributesW(q.c_str());
                    out += narrow(rel + n) + "|" + std::to_string(at & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
                    if (at & FILE_ATTRIBUTE_DIRECTORY) {
                        out += "\n";
                        tree(q, rel + n + L"/", out);
                    } else {
                        std::string body;
                        readFile(q, body);
                        out += "|" + body + "\n";
                    }
                }
            };
            std::wstring proj = fd + L"\\Проекты", docs = fd + L"\\Документы", mus = fd + L"\\музыка";
            CreateDirectoryW((proj + L"\\.git").c_str(), nullptr);
            CreateDirectoryW((proj + L"\\код").c_str(), nullptr);
            CreateDirectoryW((proj + L"\\код\\глубже").c_str(), nullptr);
            writeFile(proj + L"\\.git\\HEAD", "ref: refs/heads/main\n");
            writeFile(proj + L"\\код\\main.cpp", "int main() { return 0; }\r\n");
            writeFile(proj + L"\\код\\глубже\\данные.bin", std::string(70000, 'x'));
            writeFile(proj + L"\\desktop.ini", "[.ShellClassInfo]\r\nIconResource=C:\\Windows\\System32\\shell32.dll,3\r\n");
            SetFileAttributesW((proj + L"\\desktop.ini").c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
            SetFileAttributesW(proj.c_str(), FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY);
            std::string before;
            tree(proj, L"", before);
            DWORD pa0 = GetFileAttributesW(proj.c_str());
            p.receiveFile(proj, 1);
            float projMs = p.handsMs;
            bool projTaken = p.deskOn() && !exists(proj) && inHands() == 1;
            sim(0.3f, still);
            std::string inside;
            if (projTaken) tree(p.deskRec.hands, L"", inside);
            p.dropHeld("test");
            settle();
            std::string after;
            tree(proj, L"", after);
            DWORD pa1 = GetFileAttributesW(proj.c_str());
            bool projBack = exists(proj) && after == before && inside == before && pa1 == pa0 && !inHands() && I.findPath(proj) >= 0;
            p.receiveFile(rep, 1);
            float fileMs = p.handsMs;
            bool fileTaken = p.deskOn() && !exists(rep) && inHands() == 1;
            p.dropHeld("test");
            settle();
            CreateDirectoryW((docs + L"\\.vs").c_str(), nullptr);
            writeFile(docs + L"\\открыт.txt", "busy\r\n");
            HANDLE busy = CreateFileW((docs + L"\\открыт.txt").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            p.receiveFile(docs, 1);
            bool busyPic = busy != INVALID_HANDLE_VALUE && !p.deskOn() && p.heldPath.empty() && p.deskBusy && exists(docs) && I.findPath(docs) >= 0 &&
                           !inHands();
            p.dropHeld("test");
            if (busy != INVALID_HANDLE_VALUE) CloseHandle(busy);
            p.receiveFile(docs, 1);
            float docsMs = p.handsMs;
            bool docsTaken = p.deskOn() && !exists(docs) && inHands() == 1;
            p.dropHeld("test");
            p.receiveFile(mus, 1);
            float musMs = p.handsMs;
            bool plain = p.deskOn() && !exists(mus) && inHands() == 1;
            p.dropHeld("test");
            settle();
            int tw0 = I.twinBacks;
            p.receiveFile(proj, 1);
            bool reTaken = p.deskOn() && !exists(proj) && inHands() == 1;
            CreateDirectoryW(proj.c_str(), nullptr);
            CreateDirectoryW((proj + L"\\код").c_str(), nullptr);
            writeFile(proj + L"\\код\\main.cpp", "int main() { return 1; }\r\n");
            std::string stub;
            tree(proj, L"", stub);
            p.dropHeld("test");
            settle();
            std::wstring twin = fd + L"\\Проекты (2)";
            std::string stub1, twin1;
            tree(proj, L"", stub1);
            tree(twin, L"", twin1);
            bool twinOk = reTaken && stub1 == stub && twin1 == before && I.twinBacks == tw0 + 1 && !inHands();
            bool fast = projMs >= 0 && projMs < 150.f && fileMs >= 0 && fileMs < 150.f && docsMs >= 0 && docsMs < 150.f && musMs >= 0 && musMs < 150.f;
            bool ok = projTaken && projBack && fileTaken && busyPic && docsTaken && plain && twinOk && fast && exists(mus) && exists(docs) && exists(rep) &&
                      !inHands();
            visChecked++;
            visBad += !ok;
            logf("desk n9 folders: ok %d: read-only project (.git, desktop.ini, nested) taken %d in %.1f ms, back intact %d (contents %d, inside her hands %d, "
                 "attrs 0x%lx -> 0x%lx); file taken %d in %.1f ms; folder with an open file -> tugs, nothing in her hands %d; closed -> taken %d in %.1f ms; plain folder "
                 "taken %d in %.1f ms; re-created while held -> back beside it %d (taken %d, new folder kept %d, hers intact as '(2)' %d, warned %d); "
                 "in her hands %d",
                 (int)ok, (int)projTaken, projMs, (int)projBack, (int)(after == before), (int)(inside == before), (unsigned long)pa0, (unsigned long)pa1,
                 (int)fileTaken, fileMs, (int)busyPic, (int)docsTaken, docsMs, (int)plain, musMs, (int)twinOk, (int)reTaken, (int)(stub1 == stub),
                 (int)(twin1 == before), I.twinBacks - tw0, inHands());
        }

        {
            setup(base);
            auto waitTug = [&]() {
                for (int k = 0; k < 500 && !p.tugPath.empty(); k++) {
                    if (p.busyJob && !p.busyJob->done) Sleep(5);
                    sim(0.02f, still);
                }
            };
            auto calm = [&]() {
                a.bubble.close();
                sim(0.3f, still);
            };
            auto holdBy = [&](const std::wstring& file, PROCESS_INFORMATION& pi) {
                std::wstring cmd = L"powershell.exe -NoProfile -NonInteractive -Command \"$f=[IO.File]::Open('" + file +
                                   L"','Open','Read','Read'); Start-Sleep 40\"";
                STARTUPINFOW si{sizeof(si)};
                pi = {};
                if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
                for (int k = 0; k < 300; k++) {
                    HANDLE t = CreateFileW(file.c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
                    if (t == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION) return true;
                    if (t != INVALID_HANDLE_VALUE) CloseHandle(t);
                    Sleep(40);
                }
                return false;
            };
            auto release = [&](PROCESS_INFORMATION& pi) {
                if (!pi.hProcess) return;
                TerminateProcess(pi.hProcess, 0);
                WaitForSingleObject(pi.hProcess, 5000);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                pi = {};
            };
            POINT rc0 = posOf(L"Отчёт.txt");
            HANDLE own = CreateFileW(rep.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            p.receiveFile(rep, 1);
            bool tugging = own != INVALID_HANDLE_VALUE && p.tugT >= 0 && p.heldPath.empty() && !p.deskOn() && p.deskBusy && !p.heldShown();
            sim(0.55f, still);
            float lean = p.bTug;
            shot(L"83_busy_tug");
            waitTug();
            POINT rc1 = posOf(L"Отчёт.txt");
            bool anonOk = tugging && p.tugWho.empty() && p.tugCat.rfind("fileBusyAnon", 0) == 0 && exists(rep) && I.findPath(rep) >= 0 && !inHands() &&
                          p.heldPath.empty() && rc1.x == rc0.x && rc1.y == rc0.y && lean > 0.5f;
            std::string said1 = narrow(a.bubble.text);
            if (own != INVALID_HANDLE_VALUE) CloseHandle(own);
            calm();
            PROCESS_INFORMATION pi{};
            bool held = holdBy(rep, pi);
            bool appOk = false;
            std::string who1, said2;
            int apps1 = -1;
            float rmMs = -1;
            if (held) {
                p.receiveFile(rep, 1);
                float ms0 = p.busyJob ? 0.f : -1.f;
                (void)ms0;
                for (int k = 0; k < 400 && p.busyJob && !p.busyJob->done; k++) Sleep(5);
                if (p.busyJob) rmMs = p.busyJob->ms;
                waitTug();
                who1 = narrow(p.tugWho);
                apps1 = p.tugApps;
                said2 = narrow(a.bubble.text);
                appOk = p.deskBusy && !p.tugWho.empty() && p.tugApps >= 1 && p.tugCat.rfind("fileBusyApp", 0) == 0 && p.heldPath.empty() && !inHands() &&
                        exists(rep) && a.bubble.text.find(p.tugWho) != std::wstring::npos;
            }
            release(pi);
            calm();
            p.receiveFile(rep, 1);
            bool takenAfter = p.deskOn() && !exists(rep) && inHands() == 1 && !p.heldPath.empty() && p.tugPath.empty();
            p.dropHeld("test");
            settle();
            calm();
            std::wstring docs = fd + L"\\Документы", inner = docs + L"\\открыт.txt";
            writeFile(inner, "busy\r\n");
            PROCESS_INFORMATION pf{};
            bool fheld = holdBy(inner, pf);
            std::string who2, said3;
            bool folderOk = false;
            p.receiveFile(docs, 1);
            bool ftug = p.deskBusy && p.heldPath.empty() && !p.deskOn() && exists(docs) && I.findPath(docs) >= 0 && !inHands();
            waitTug();
            who2 = narrow(p.tugWho);
            said3 = narrow(a.bubble.text);
            folderOk = ftug && p.tugCat.rfind(fheld ? "folderBusyApp" : "folderBusy", 0) == 0 && (!fheld || !p.tugWho.empty());
            release(pf);
            calm();
            p.receiveFile(docs, 1);
            bool folderAfter = p.deskOn() && !exists(docs) && inHands() == 1;
            p.dropHeld("test");
            settle();
            calm();
            std::wstring plan = fd + L"\\план.md";
            HANDLE own2 = CreateFileW(rep.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            p.receiveFile(rep, 1);
            bool busyBefore = p.deskBusy;
            waitTug();
            if (own2 != INVALID_HANDLE_VALUE) CloseHandle(own2);
            calm();
            p.receiveFile(plan, 3);
            bool multiOk = busyBefore && !p.deskBusy && p.tugT < 0 && p.tugPath.empty() && !p.heldPath.empty() && exists(plan) && !inHands();
            p.dropHeld("test");
            calm();
            auto icacls = [&](const std::wstring& args) {
                std::wstring cmd = L"icacls.exe " + args;
                STARTUPINFOW si{sizeof(si)};
                PROCESS_INFORMATION ip{};
                if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &ip)) return false;
                WaitForSingleObject(ip.hProcess, 10000);
                DWORD code = 1;
                GetExitCodeProcess(ip.hProcess, &code);
                CloseHandle(ip.hProcess);
                CloseHandle(ip.hThread);
                return code == 0;
            };
            std::wstring pub = fd + L"\\скрин.png";
            bool denied = icacls(L"\"" + fd + L"\" /deny *S-1-1-0:(DC)") && icacls(L"\"" + pub + L"\" /deny *S-1-1-0:(DE)");
            p.receiveFile(pub, 1);
            bool rightsOk = denied && I.takeRights && !p.deskBusy && p.tugT < 0 && p.tugPath.empty() && !p.heldPath.empty() && !p.deskOn() &&
                            exists(pub) && !inHands();
            std::string said4 = narrow(a.bubble.text);
            icacls(L"\"" + pub + L"\" /remove:d *S-1-1-0");
            icacls(L"\"" + fd + L"\" /remove:d *S-1-1-0");
            p.dropHeld("test");
            calm();
            logf("desk n10 stale/rights: multi-drop after a busy one -> picture, no tug %d; no rights (acl %d) -> picture, not busy %d, said '%s'", (int)multiOk,
                 (int)denied, (int)rightsOk, said4.c_str());
            bool ok = anonOk && (appOk || !held) && takenAfter && folderOk && folderAfter && multiOk && rightsOk && exists(rep) && exists(docs) && !inHands();
            visChecked++;
            visBad += !ok;
            logf("desk n10 busy: ok %d: own handle -> tugs %d (lean %.2f), anonymous line %d '%s'; helper process held it %d -> restart manager named '%s' "
                 "(%d other apps, %.0f ms) %d, said '%s'; closed -> taken %d; folder with a file held inside (helper %d) -> tugs %d, named '%s', said '%s', "
                 "ok %d; closed -> taken %d; icon stayed %ld,%ld -> %ld,%ld; in her hands %d",
                 (int)ok, (int)tugging, lean, (int)anonOk, said1.c_str(), (int)held, who1.c_str(), apps1, rmMs, (int)appOk, said2.c_str(), (int)takenAfter,
                 (int)fheld, (int)ftug, who2.c_str(), said3.c_str(), (int)folderOk, (int)folderAfter, rc0.x, rc0.y, rc1.x, rc1.y, inHands());
        }

        {
            std::wstring ld = I.layoutDir();
            {
                WIN32_FIND_DATAW wf;
                HANDLE h = FindFirstFileW((ld + L"\\*").c_str(), &wf);
                if (h != INVALID_HANDLE_VALUE) {
                    do {
                        if (!(wf.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((ld + L"\\" + wf.cFileName).c_str());
                    } while (FindNextFileW(h, &wf));
                    FindClose(h);
                }
            }
            auto countSnaps = [&]() {
                int n = 0;
                WIN32_FIND_DATAW wf;
                HANDLE h = FindFirstFileW((ld + L"\\*.tsv").c_str(), &wf);
                if (h != INVALID_HANDLE_VALUE) {
                    do n++;
                    while (FindNextFileW(h, &wf));
                    FindClose(h);
                }
                return n;
            };
            setup(base);
            auto was = layoutNow();
            p.receiveFile(rep, 1);
            sim(0.5f, still);
            p.dropHeld("test");
            settle();
            std::wstring snap = I.layoutBefore();
            std::string body;
            readFile(snap, body);
            int lines = (int)std::count(body.begin(), body.end(), '\n');
            std::vector<POINT> cells;
            for (int c = 6; c < 14; c++)
                for (int r = 0; r < 9; r++) cells.push_back(cellPos(c, r));
            std::vector<std::wstring> names;
            for (auto& d : I.items) names.push_back(d.name);
            for (size_t k = 0; k < names.size(); k++) I.fakeMove(names[k], cells[(k * 7) % cells.size()]);
            int shuffled = others(was) + (posOf(L"Отчёт.txt").x != home0.x);
            int total = 0;
            int b0 = I.badMoves;
            int same = I.layoutRestore(snap, &total);
            int back = (int)names.size() - others(was) - (posOf(L"Отчёт.txt").x != home0.x || posOf(L"Отчёт.txt").y != home0.y);
            logf("desk n3: snapshot %s (%d lines, clock %s), shuffled %d of %zu, restore %d of %d exact, all back %d of %zu, blocked %d",
                 narrow(snap.substr(snap.find_last_of(L"\\/") + 1)).c_str(), lines, narrow(I.layoutClock(snap)).c_str(), shuffled, names.size(), same, total,
                 back, names.size(), I.badMoves - b0);
            writeFile(fd + L"\\новый.txt", "test\r\n");
            I.addFake(L"новый.txt", cellPos(9, 4), false);
            I.fakeMove(L"скрин.png", cellPos(9, 0));
            I.fakeMove(L"новый.txt", posOf(L"видео.mp4"));
            I.fakeMove(L"видео.mp4", cellPos(10, 0));
            POINT nw = posOf(L"новый.txt");
            same = I.layoutRestore(snap, &total);
            POINT nw2 = posOf(L"новый.txt"), vd = posOf(L"видео.mp4");
            logf("desk n3 partial: new icon stays %d (%ld,%ld -> %ld,%ld), its cell's owner went to %ld,%ld (free, on grid %d), exact %d of %d", (int)(nw.x == nw2.x && nw.y == nw2.y),
                 nw.x, nw.y, nw2.x, nw2.y, vd.x, vd.y, (int)I.onGrid(vd), same, total);
            int n0 = countSnaps();
            I.layoutSnap(a.now, "again", true);
            int n1 = countSnaps();
            a.now += 700;
            I.layoutIdle(a.now);
            int n2 = countSnaps();
            I.fakeMove(L"новый.txt", cellPos(11, 3));
            a.now += 700;
            I.layoutIdle(a.now);
            int n3 = countSnaps();
            logf("desk n3 cadence: files %d, after a repeat %d (same layout), idle unchanged %d, idle changed %d", n0, n1, n2, n3);
            std::wstring keep = I.layoutBefore();
            for (int k = 0; k < 20; k++) {
                I.fakeMove(L"новый.txt", cellPos(11 + k % 3, k % 9));
                a.now += 1;
                I.layoutSnap(a.now, "churn", false);
            }
            logf("desk n3 prune: %d files, before-action snapshot kept %d", countSnaps(), (int)(GetFileAttributesW(keep.c_str()) != INVALID_FILE_ATTRIBUTES));
            onCommand(ID_ICONS_UNDO);
            logf("desk n3 tray: before-action snapshot %s at %s", narrow(I.layoutBefore()).c_str(), narrow(I.layoutClock(I.layoutBefore())).c_str());
        }

        {
            setup(base);
            int b0 = I.badMoves;
            logf("desk n4: guard self-test (blocks off-screen and off-grid, lets a grid cell through) %d", (int)I.guardSelfTest());
            POINT g1{}, g2{}, g3{}, g4{};
            I.put(I.items[I.find(L"заметки.txt")], POINT{5000, 3000}, "test far off", &g1);
            I.put(I.items[I.find(L"заметки.txt")], POINT{-800, -400}, "test far off the other way", &g2);
            POINT c64 = cellPos(6, 4);
            I.put(I.items[I.find(L"заметки.txt")], POINT{c64.x + 37, c64.y + 23}, "test off grid", &g3);
            I.put(I.items[I.find(L"заметки.txt")], posOf(L"скрин.png"), "test taken", &g4);
            auto okc = [&](POINT q) { return (int)(I.onGrid(q) && I.cellOnScreen(q)); };
            logf("desk n4: far off -> %ld,%ld ok %d; other way -> %ld,%ld ok %d; off grid -> %ld,%ld exact cell %d; taken -> %ld,%ld ok %d free %d; blocked %d",
                 g1.x, g1.y, okc(g1), g2.x, g2.y, okc(g2), g3.x, g3.y, (int)(g3.x == c64.x && g3.y == c64.y), g4.x, g4.y, okc(g4),
                 (int)(g4.x != posOf(L"скрин.png").x || g4.y != posOf(L"скрин.png").y), I.badMoves - b0);
            a.world.mons.push_back(Mon{RECT{-1920, -308, 0, 772}, RECT{-1920, -308, 0, 712}});
            POINT g5{};
            I.put(I.items[I.find(L"заметки.txt")], POINT{-1500, 100}, "test left monitor", &g5);
            logf("desk n4: left monitor -> %ld,%ld ok %d", g5.x, g5.y, okc(g5));
            a.world.mons.pop_back();
            I.put(I.items[I.find(L"заметки.txt")], cellPos(0, 4), "test back home");
        }

        {
            setup(base);
            DeleteFileW((appDataDir() + L"\\snap_off.flag").c_str());
            I.fakeSnap = true;
            std::wstring note = fd + L"\\заметки.txt";
            POINT nh = posOf(L"заметки.txt");
            p.fetchName = L"заметки.txt";
            p.setState(St::GrabIcon, 1.f);
            sim(0.5f, still);
            I.refresh();
            logf("desk n5 fetch: carrying '%s', in her hands %d, on the desk %d, snap %d", narrow(p.carriedName).c_str(), inHands(),
                 (int)(I.findPath(note) >= 0), (int)I.fakeSnap);
            sim(2.f, still);
            p.dropCarried(true);
            settle();
            I.refresh();
            int i = I.findPath(note);
            POINT q = i >= 0 ? I.items[i].pos : POINT{-1, -1};
            int over = 0;
            for (auto& o : I.items) {
                RECT x, a1{o.cell.left + 2, o.cell.top + 2, o.cell.right - 2, o.cell.bottom - 2};
                RECT b1 = i >= 0 ? I.items[i].cell : RECT{};
                b1 = {b1.left + 2, b1.top + 2, b1.right - 2, b1.bottom - 2};
                if (i >= 0 && &o != &I.items[i] && IntersectRect(&x, &a1, &b1)) over++;
            }
            bool flag = GetFileAttributesW((appDataDir() + L"\\snap_off.flag").c_str()) != INVALID_FILE_ATTRIBUTES;
            logf("desk n5 fetch dropped: at %ld,%ld (was %ld,%ld) grid %d shown %d overlaps %d hidden %d in her hands %d, snap %d snap_off.flag %d", q.x, q.y,
                 nh.x, nh.y, (int)(i >= 0 && I.onGrid(q)), (int)(i >= 0 && I.cellOnScreen(q)), over, (int)hiddenIn(note), inHands(), (int)I.fakeSnap, (int)flag);
            p.fetchName = L"Корзина";
            writeFile(fd + L"\\Корзина", "test\r\n");
            SetFileAttributesW((fd + L"\\Корзина").c_str(), FILE_ATTRIBUTE_SYSTEM);
            p.setState(St::GrabIcon, 1.f);
            sim(0.5f, still);
            logf("desk n5 fetch system item: carrying '%s' (must be empty)", narrow(p.carriedName).c_str());
            SetFileAttributesW((fd + L"\\Корзина").c_str(), FILE_ATTRIBUTE_NORMAL);
            p.dropCarried(false);
            sim(1.f, still);
        }

        {
            setup(base);
            auto at = [&](const std::wstring& path) {
                int i = I.findPath(path);
                return i >= 0 ? I.items[i].pos : POINT{-1, -1};
            };
            auto same = [](POINT x, POINT y) { return x.x == y.x && x.y == y.y; };
            std::wstring docx = fd + L"\\дубль.docx", pdf = fd + L"\\дубль.pdf";
            writeFile(docx, "test\r\n");
            writeFile(pdf, "test\r\n");
            POINT da = cellPos(8, 1), db = cellPos(8, 2);
            I.addFake(L"дубль.docx", da, false);
            I.addFake(L"дубль.pdf", db, false);
            for (auto& d : I.items)
                if (d.name.rfind(L"дубль.", 0) == 0) d.name = L"дубль";
            size_t h0 = I.layoutHistory().size();
            p.receiveFile(docx, 1);
            sim(0.5f, still);
            p.dropHeld("test");
            settle();
            I.refresh();
            for (auto& d : I.items)
                if (d.name.rfind(L"дубль.", 0) == 0) d.name = L"дубль";
            int lastDup = -1;
            for (size_t i = 0; i < I.items.size(); i++)
                if (I.items[i].name == L"дубль") lastDup = (int)i;
            bool docxLast = lastDup >= 0 && !_wcsicmp(I.items[lastDup].path.c_str(), docx.c_str());
            int total = 0;
            int ex = I.layoutRestore(I.layoutBefore(), &total);
            logf("desk n6 duplicates: docx re-listed last %d; after a restore docx home %d, pdf home %d (%d of %d exact)", (int)docxLast,
                 (int)same(at(docx), da), (int)same(at(pdf), db), ex, total);
            auto hs1 = I.layoutHistory();
            I.fakeMove(L"скрин.png", cellPos(9, 0));
            a.now += 5;
            p.receiveFile(rep, 1);
            sim(0.5f, still);
            p.dropHeld("test");
            settle();
            auto hs2 = I.layoutHistory();
            writeFile(fd + L"\\не на столе.txt", "test\r\n");
            I.fakeMove(L"скрин.png", cellPos(10, 0));
            bool started = p.deskStart(fd + L"\\не на столе.txt");
            auto hs3 = I.layoutHistory();
            logf("desk n6 history: before %zu, after one carry %zu, after two %zu (older one second %d), failed start %d leaves it %d",
                 h0, hs1.size(), hs2.size(), (int)(hs2.size() >= 2 && !hs1.empty() && hs2[1] == hs1[0]), (int)started, (int)(hs3 == hs2));
            SYSTEMTIME lt{};
            GetLocalTime(&lt);
            wchar_t tn[40];
            swprintf(tn, 40, L"\\%04d%02d%02d-221500.tsv", lt.wYear, lt.wMonth, lt.wDay);
            std::wstring todayClock = I.layoutClock(I.layoutDir() + tn);
            logf("desk n6 clock: today '%s' (no date %d), old '%s'", narrow(todayClock).c_str(), (int)(todayClock == L"22:15"),
                 narrow(I.layoutClock(I.layoutDir() + L"\\20261108-221500.tsv")).c_str());
            setup(base);
            POINT h = posOf(L"Отчёт.txt");
            p.receiveFile(rep, 1);
            sim(0.5f, still);
            I.fakeNoReturn = true;
            bool gave = p.heldDragBegin();
            bool dragging = p.heldDragging;
            bool hidA = inHands() > 0 && !exists(rep);
            p.deskHomeNow("test: cannot give back");
            bool hidB = inHands() > 0 && !exists(rep), on = p.deskOn();
            std::string rec, rec2;
            readFile(appDataDir() + L"\\desk_carry.txt", rec);
            I.fakeNoReturn = false;
            sim(6.f, still);
            settle();
            readFile(appDataDir() + L"\\desk_carry.txt", rec2);
            POINT f = posOf(L"Отчёт.txt");
            logf("desk n6 cannot give back: drag allowed %d, dragging %d, still in her hands %d; carry ended %d, in her hands %d, record kept %d; after the "
                 "retry in her hands %d, home exact %d, record left %d",
                 (int)gave, (int)dragging, (int)hidA, (int)!on, (int)hidB, (int)!rec.empty(), inHands(), (int)same(f, h), (int)!rec2.empty());
            vis("n6 retry after a failed show");
        }

        setup(base);
        p.receiveFile(rep, 1);
        sim(1.f, still);
        heldDragOut();
        p.heldDragLeft();
        sim(0.5f, still);
        {
            auto cen = [&](const wchar_t* n) {
                V2 c = imgC(I.find(n));
                return POINT{(LONG)c.x, (LONG)c.y};
            };
            POINT underLabel = cen(L"Документы");
            underLabel.y += I.iconPx / 2 + (LONG)lroundf(22.f * a.dpi) + 8;
            struct DQ { const char* what; BOOL esc; DWORD keys; POINT pt; bool desk; int hl; bool seen; };
            for (DQ q : {DQ{"button still down", FALSE, MK_LBUTTON, POINT{1000, 300}, true, -2, false}, DQ{"esc", TRUE, 0, POINT{1000, 300}, true, -2, false},
                         DQ{"over a folder", FALSE, 0, cen(L"Документы"), true, -2, false},
                         DQ{"over a shortcut", FALSE, 0, cen(L"Minecraft.lnk"), true, -2, false},
                         DQ{"over a window", FALSE, 0, POINT{1000, 300}, false, -2, false},
                         DQ{"over a file", FALSE, 0, cen(L"заметки.txt"), true, -2, false},
                         DQ{"in a folder's cell under its one-line label", FALSE, 0, underLabel, true, -2, false},
                         DQ{"explorer highlights an item", FALSE, 0, underLabel, true, 1, false},
                         DQ{"highlight seen before, none now, over a folder", FALSE, 0, cen(L"Документы"), true, -1, true},
                         DQ{"empty desktop", FALSE, 0, POINT{1000, 300}, true, -2, false}}) {
                g_heldDeskDrop = false;
                g_fakeHilite = q.hl;
                g_hiliteSeen = q.seen;
                HRESULT hr = heldQueryDrop(q.esc, q.keys, q.pt, q.desk);
                logf("desk o: %s -> %s, own drop %d", q.what, hr == S_OK ? "go on" : hr == DRAGDROP_S_DROP ? "DROP" : hr == DRAGDROP_S_CANCEL ? "CANCEL" : "?",
                     (int)g_heldDeskDrop);
            }
            g_fakeHilite = -2;
            g_hiliteSeen = false;
            POINT dp = g_heldDropPt;
            g_heldDeskDrop = false;
            p.heldDragEnd(DROPEFFECT_NONE, V2((float)dp.x, (float)dp.y), true);
            sim(0.2f, still);
            settle();
            int i = I.findPath(rep);
            RECT c = i >= 0 ? I.items[i].cell : RECT{};
            logf("desk o: let go at %ld,%ld -> icon cell %ld,%ld..%ld,%ld, point inside it %d, carry on %d", dp.x, dp.y, c.left, c.top, c.right, c.bottom,
                 (int)PtInRect(&c, dp), (int)p.deskOn());
            vis("o let go over the desktop");
        }

        std::vector<std::wstring> gn;
        gn.reserve(200);
        auto gridOf = [&](std::function<bool(int, int)> has, int rc, int rr) {
            std::vector<FI> g;
            for (int c = 0; c < 19; c++)
                for (int r = 0; r < 9; r++) {
                    if (c == rc && r == rr) { g.push_back({L"Отчёт.txt", c, r, false}); continue; }
                    if (!has(c, r)) continue;
                    wchar_t nm[32];
                    swprintf(nm, 32, L"файл %d-%d.txt", c, r);
                    gn.push_back(nm);
                    g.push_back({gn.back().c_str(), c, r, false});
                }
            return g;
        };
        auto placeRun = [&](const char* tag, const wchar_t* shotName) {
            p.receiveFile(rep, 1);
            sim(1.f, still);
            p.pos.x = 1500;
            sim(std::max(0.f, p.heldLife - p.heldT) + 0.1f, still);
            POINT hm = p.deskHome;
            RECT hc = I.cellAt(hm);
            V2 tc((hc.left + hc.right) * 0.5f, hc.top + I.imgOff.y + I.iconPx * 0.5f);
            bool logged = false;
            run(60.f, shotName, nullptr, [&] {
                if (logged || p.deskMode != Pet::DeskPlace) return;
                logged = true;
                float rows = (p.pos.y - hc.bottom) / (float)I.spacing.y;
                logf("desk %s: placing from %.0f,%.0f: %.2f cells across, feet %.2f rows below the cell bottom (cell %ld,%ld..%ld,%ld), hands %.0f px", tag,
                     p.pos.x, p.pos.y, fabsf(p.pos.x - tc.x) / I.spacing.x, rows, hc.left, hc.top, hc.right, hc.bottom,
                     (p.carriedCenter - tc).len());
            });
            POINT f = posOf(L"Отчёт.txt");
            logf("desk %s: final %ld,%ld home %ld,%ld exact %d", tag, f.x, f.y, hm.x, hm.y, (int)(f.x == hm.x && f.y == hm.y));
            vis(tag);
        };
        setup(gridOf([](int c, int r) { return c == 0 || (c == 1 && r >= 2) || (c == 3 && r >= 4) || (r == 8 && c <= 9); }, 2, 1));
        placeRun("p climb", L"80c_place_close");
        setup(gridOf([](int c, int r) { return c == 0; }, 8, 1));
        placeRun("p isolated", nullptr);

        {
            setup(gridOf([](int c, int r) { return c >= 3 && c <= 12 && r >= 3 && r <= 8; }, 18, 0));
            int rowI = I.find(L"файл 3-5.txt"), endI = I.find(L"файл 12-5.txt");
            float rowY = (float)I.items[rowI].img.top, x0 = imgC(rowI).x, x1 = imgC(endI).x;
            double t0 = nowSec();
            for (int k = 0; k < 100; k++) surfs();
            logf("desk q: %zu icons -> %zu surfaces, rows %zu, building them %.3f ms", I.items.size(), a.world.surfs.size(), a.world.iconRuns.size(),
                 (nowSec() - t0) * 1000.0 / 100);
            auto standOn = [&](float x) {
                reset(St::Idle, 100);
                p.pos = V2(x, rowY);
                for (auto& s : a.world.surfs)
                    if (s.kind == SurfKind::Icon && fabsf(s.y - rowY) < 1 && x >= s.x0 - 2 && x <= s.x1 + 2) p.setGround(s);
                p.setState(St::Idle, 100);
                sim(0.3f, still);
            };
            for (int join = 0; join < 2; join++) {
                a.world.joinIcons = join != 0;
                surfs();
                standOn(x0);
                p.goWalk(x1);
                int falls = 0, lands = 0;
                St prev = p.st;
                float tt = 0;
                for (; tt < 20.f && (p.st == St::Walk || p.st == St::Fall || p.st == St::Land); tt += 0.0167f) {
                    sim(0.0167f, still);
                    falls += p.st == St::Fall && prev != St::Fall;
                    lands += p.st == St::Land && prev != St::Land;
                    prev = p.st;
                }
                logf("desk q walk join %d: from %.0f to %.0f along the row: ended at %.0f,%.0f in %.1f s, falls %d, lands %d, on icon '%s'", join, x0, x1,
                     p.pos.x, p.pos.y, tt, falls, lands, narrow(p.gnd.iconName).c_str());
                for (int low = 0; low < 2; low++) {
                standOn(x0);
                p.vit.energy = p.vit.food = 100.f;
                int plans0 = p.perfJumpPlans;
                double jumpS0 = p.perfJumpSec;
                double g0 = nowSec();
                p.gameStart(0);
                double gStart = (nowSec() - g0) * 1000.0;
                falls = lands = 0;
                int leaps = 0;
                prev = p.st;
                double worst = 0, sum = 0, firstWorst = 0;
                int n = 0;
                float ranX = p.pos.x;
                for (tt = 0; tt < 12.f && p.game; tt += 0.0167f) {
                    float u = std::max(0.f, tt - 2.5f);
                    float cx = std::min(x0 + 300.f + 200.f * u, x1 + 40.f);
                    double f0 = nowSec();
                    float cy = low ? rowY + I.iconPx + 14.f : rowY - p.standTop * 0.55f;
                    sim(0.0167f, [&](float) { return V2(cx, cy); });
                    double ms = (nowSec() - f0) * 1000.0;
                    worst = std::max(worst, ms);
                    if (tt < 3.5f) firstWorst = std::max(firstWorst, ms);
                    sum += ms;
                    n++;
                    falls += p.st == St::Fall && prev != St::Fall;
                    lands += p.st == St::Land && prev != St::Land;
                    leaps += p.st == St::Leap && prev != St::Leap;
                    prev = p.st;
                    ranX = std::max(ranX, p.pos.x);
                }
                logf("desk q tag join %d cursor %s: start %.2f ms, %.1f s, result %d: falls %d, leaps %d, lands %d, got to %.0f (row end %.0f), y %.0f, "
                     "frame update avg %.3f ms, worst %.3f ms (first 3.5 s %.3f), jump arcs %d in %.2f ms",
                     join, low ? "on the labels below her" : "at her chest", gStart, tt, p.game ? 0 : gameList()[0]->result, falls, leaps, lands, ranX, x1, p.pos.y,
                     sum / std::max(n, 1), worst, firstWorst, p.perfJumpPlans - plans0, (p.perfJumpSec - jumpS0) * 1000.0);
                p.gameStop("test");
                sim(0.5f, still);
                }
            }
            a.world.joinIcons = true;
            {
                standOn(x0);
                p.vit.energy = p.vit.food = 100.f;
                p.gameStart(0);
                bool done = false;
                for (float tt = 0; tt < 12.f && p.game && !done; tt += 0.0167f) {
                    sim(0.0167f, [&](float) { return V2(p.pos.x + 170.f, rowY - p.standTop * 0.55f); });
                    if (p.st == St::Walk && p.runK > 1.f && p.pos.x > (x0 + x1) * 0.5f) {
                        shot(L"80d_run_icons");
                        logf("desk q 80d: running on '%s' at %.0f,%.0f", narrow(p.gnd.iconName).c_str(), p.pos.x, p.pos.y);
                        done = true;
                    }
                }
                p.gameStop("test");
                sim(0.5f, still);
            }

            {
                int mid = I.find(L"файл 7-5.txt");
                standOn(imgC(mid).x);
                p.vit.energy = p.vit.food = 100.f;
                p.gameStart(0);
                V2 cur(p.pos.x + 4.f, rowY + I.iconPx + 14.f);
                float tt = 0;
                for (; tt < 25.f && p.game; tt += 0.0167f) sim(0.0167f, [&](float) { return cur; });
                logf("desk q still cursor on her own label: %.0f px below her feet, game over %d after %.1f s, result %d (1 = caught)", cur.y - rowY,
                     (int)!p.game, tt, p.game ? 0 : gameList()[0]->result);
                p.gameStop("test");
                sim(0.5f, still);
            }
            {
                int at = I.find(L"файл 10-5.txt");
                standOn(imgC(at).x);
                p.vit.energy = p.vit.food = 100.f;
                p.gameStart(0);
                V2 cur(400.f, 1000.f);
                float tt = 0, firstT = -1, offX = -1, startX = p.pos.x;
                for (; tt < 12.f && p.game; tt += 0.0167f) {
                    sim(0.0167f, [&](float) { return cur; });
                    if (firstT < 0 && p.st == St::Walk) firstT = p.walkTarget;
                    if (offX < 0 && !p.grounded()) offX = p.pos.x;
                    if (offX >= 0 && p.grounded() && p.gnd.kind == SurfKind::Floor) break;
                }
                logf("desk q cursor on the floor at %.0f: she was at %.0f on the row %.0f..%.0f, first run target %.0f, left the row at %.0f, on the floor at %.0f "
                     "after %.1f s (%s)", cur.x, startX, x0, x1, firstT, offX, p.pos.x, tt, firstT < startX ? "toward the cursor" : "away from it");
                p.gameStop("test");
                sim(0.5f, still);
            }
            for (int order = 0; order < 2; order++) {
                setup({});
                LONG ty = 4 + 5 * sp.y;
                if (!order)
                    for (int k = 0; k < 4; k++) I.addFake(gn[k], POINT{20 + (3 + k) * sp.x, ty + (k == 0 ? 1 : k == 2 ? 2 : 0)}, false);
                else {
                    I.addFake(gn[0], POINT{20 + 3 * sp.x, ty + 1}, false);
                    I.addFake(gn[1], POINT{20 + 4 * sp.x, ty}, false);
                }
                surfs();
                int n = (int)I.items.size(), rows = 0;
                const Surf* rs = nullptr;
                for (auto& s : a.world.surfs)
                    if (s.kind == SurfKind::Icon) {
                        rows++;
                        rs = &s;
                    }
                bool covers = rs != nullptr;
                for (int k = 0; k < n && rs; k++) covers &= rs->x0 <= I.items[k].img.left + 3.f && rs->x1 >= I.items[k].img.right - 3.f;
                float ax = imgC(0).x, bx = imgC(n - 1).x;
                reset(St::Idle, 100);
                p.pos = V2(ax, rs ? rs->y : 0.f);
                if (rs) p.setGround(*rs);
                p.setState(St::Idle, 100);
                sim(0.3f, still);
                p.goWalk(bx);
                int falls = 0;
                St prev = p.st;
                float tt = 0;
                for (; tt < 10.f && (p.st == St::Walk || p.st == St::Fall || p.st == St::Land); tt += 0.0167f) {
                    sim(0.0167f, still);
                    falls += p.st == St::Fall && prev != St::Fall;
                    prev = p.st;
                }
                logf("desk q hand-placed row %d: %d icons -> %d surface(s), y %.0f x %.0f..%.0f covers all %d; walk %.0f -> %.0f ended at %.0f,%.0f, falls %d, on '%s'",
                     order, n, rows, rs ? rs->y : 0.f, rs ? rs->x0 : 0.f, rs ? rs->x1 : 0.f, (int)covers, ax, bx, p.pos.x, p.pos.y, falls,
                     narrow(p.gnd.iconName).c_str());
            }

            auto shrink = [&]() {
                a.world.setFake(RECT{400, 0, 1920, 1080}, RECT{400, 0, 1920, 1020});
                std::vector<IconBox> boxes;
                for (size_t i = 0; i < I.items.size(); i++)
                    if (!p.deskHides(I.items[i]) && a.world.rectVisible(I.items[i].img)) boxes.push_back({(int)i, I.items[i].img});
                a.world.addIconSurfs(std::move(boxes), I.spacing.x);
            };
            for (int how = 0; how < 3; how++) {
                const char* tag = how == 0 ? "r return" : how == 1 ? "r at once" : "r restore";
                setup(base);
                if (how < 2) {
                    p.receiveFile(rep, 1);
                    sim(1.f, still);
                    shrink();
                    p.deskDisplayChanged();
                    if (how == 0) {
                        sim(std::max(0.f, p.heldLife - p.heldT) + 0.1f, still);
                        run(45.f, nullptr, nullptr, nullptr);
                    } else {
                        p.deskHomeNow("test");
                    }
                } else {
                    p.receiveFile(rep, 1);
                    sim(1.f, still);
                    p.deskEnd(true);
                    p.heldClear();
                    shrink();
                    logf("desk r restore: all records done %d", (int)I.restoreCarry());
                    settle();
                }
                settle();
                POINT f = posOf(L"Отчёт.txt");
                logf("desk %s: home %ld,%ld is off the monitor now, icon at %ld,%ld", tag, home0.x, home0.y, f.x, f.y);
                vis(tag);
            }
        }
        for (int caught = 1; caught >= 0; caught--) {
            p.vit.rel = 96.f;
            setup(base);
            a.bubble.close();
            Input pin;
            pin.cursor = V2(1500, 500);
            pin.idleSec = 10;
            p.nextPeekAt = 1;
            bool went = p.peekStart(pin);
            std::wstring target = p.peekPath;
            DWORD ta = GetFileAttributesW(target.c_str());
            bool fileOnly = !target.empty() && ta != INVALID_FILE_ATTRIBUTES && !(ta & FILE_ATTRIBUTE_DIRECTORY) &&
                            target.find(L".lnk") == std::wstring::npos;
            float tt = 0;
            for (; tt < 25.f && !(p.peekOn && p.deskOn()); tt += 0.05f) sim(0.05f, still);
            std::wstring name = p.deskName;
            POINT home = p.deskHome;
            bool taken = p.peekOn && p.deskOn() && inHands() == 1 && !exists(target);
            logf("desk peek %s: went %d to '%s' (file only %d), taken %d after %.1f s, home %ld,%ld, bubble '%s'", caught ? "caught" : "quiet", (int)went,
                 narrow(target).c_str(), (int)fileOnly, (int)taken, tt, home.x, home.y, narrow(a.bubble.text).c_str());
            if (caught) {
                sim(1.5f, still);
                shot(L"80p_peek");
                p.poke();
                std::string said = narrow(a.bubble.text);
                settle();
                POINT f = posOf(name.c_str());
                bool back = !p.deskOn() && !p.peekOn && inHands() == 0 && f.x == home.x && f.y == home.y;
                logf("desk peek caught: '%s', back home %d (%ld,%ld), caught count %d", said.c_str(), (int)back, f.x, f.y, p.peekCaughtN);
                sim(0.6f, still);
                shot(L"80q_peek_caught");
                visChecked++;
                visBad += !(went && fileOnly && taken && back && !said.empty());
            } else {
                std::string said;
                for (tt = 0; tt < 45.f && p.deskOn(); tt += 0.05f) {
                    sim(0.05f, still);
                    if (a.bubble.active && said.find(narrow(a.bubble.text)) == std::string::npos) said += " [" + narrow(a.bubble.text) + "]";
                }
                settle();
                POINT f = posOf(name.c_str());
                bool back = !p.deskOn() && !p.peekOn && inHands() == 0 && f.x == home.x && f.y == home.y;
                logf("desk peek quiet: returned by herself %d after %.1f s at %ld,%ld (home %ld,%ld), said%s", (int)back, tt, f.x, f.y, home.x, home.y,
                     said.empty() ? " nothing" : said.c_str());
                visChecked++;
                visBad += !(went && fileOnly && taken && back);
            }
        }
        p.dropHeld("test");
        setup({});
        bool flagLeft = GetFileAttributesW((appDataDir() + L"\\snap_off.flag").c_str()) != INVALID_FILE_ATTRIBUTES;
        logf("desk visible summary: %d checked, %d bad (hidden, off the grid, off screen or overlapping)", visChecked, visBad);
        logf("icons guard: %d puts, %d blocked off-grid/off-screen moves, %d files left hidden, %d files left in her hands, snap toggled %d (snap_off.flag %d), "
             "%d came back beside a re-created name",
             I.putCount, I.badMoves, strayHidden, strayHands, (int)!I.fakeSnap, (int)flagLeft, I.twinBacks);

        p.dropHeld("test");
        I.setFake(fd, sp, 60);
        a.world.setFake(RECT{0, 0, 1920, 1080}, RECT{0, 0, 1920, 1020});
        g_snapUnder = nullptr;
        p.vit.rel = 50.f;
        a.bubble.close();
    }
