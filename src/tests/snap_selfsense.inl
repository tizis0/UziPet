
    {
        auto nameLog = [&](const std::wstring& n) {
            FileSense fs = senseName(n);
            senseFinish(fs);
            logf("sense name '%s' -> self %d kind %s with %s score %.2f folder %.2f | %s", narrow(n).c_str(), fs.self, fileKindName(fs.kind),
                 fileKindName(fs.with), fs.nameScore, fs.folderScore, fs.why.c_str());
            return fs;
        };
        for (const wchar_t* n : {L"Uzi_AnimRig.blend", L"Uzi_AnimRig.fbx", L"uzi_face.abc", L"UziArt.png", L"uziart.png", L"uzi-doorman-fanart.jpg",
                                 L"Узи арт.png", L"UZIrig_v3.blend", L"Doorman.png", L"УЗИ брюшной полости.pdf", L"узи щитовидки 2024.pdf", L"узи.pdf",
                                 L"Uzi_SMG_model.fbx", L"uzi gun.png", L"MurderDrones_S1E1.mp4", L"md_ep2.mkv", L"N_and_Uzi.png",
                                 L"serial_designation_n.png", L"cyn.png", L"khan_doorman.png", L"khan.png", L"tessa_elliot.png", L"V_and_J.png",
                                 L"murder drones V.png", L"muzika.mp3", L"jacuzzi.png", L"fuzzy_cat.png", L"suzi.jpg", L"kuzina.jpg", L"v2_final.png",
                                 L"j.png", L"n.png", L"README.md", L"uzivatel.txt", L"UziPet.exe", L"C:\\art\\Uzi\\render_01.png",
                                 L"C:\\art\\Murder Drones\\N.png", L"C:\\art\\Murder Drones\\frame_0012.png",
                                 L"УЗИ почек.pdf", L"Результат УЗИ.pdf", L"УЗИ 12 недель.jpg", L"узи малыша.jpg", L"УЗИ_Иванова_2024.pdf", L"УЗИ ОБП.pdf",
                                 L"узи снимок.png", L"УЗИ 2.jpg", L"Узи милашка.png", L"Узи 2.png", L"УЗИ.png", L"uzi_pochek.pdf", L"UZI_rezultat_2024.pdf",
                                 L"uzi_pregnant.png", L"Uzi in the clinic.png", L"Uzi_Doorman_gun.png", L"Uzi_Doorman_SMG.fbx", L"mini uzi chibi.png",
                                 L"uzi_mini.png", L"uzi_toy_gun.png", L"sudoku_solver.png", L"ConstraintSolver.cpp", L"disassembly.asm", L"List of MDs.pdf",
                                 L"Dr_Smith_MD_CV's.pdf", L"copper_wire_9mm.png", L"Copper 9 map.png", L"md_s1e3.mp4", L"Absolute Solver.png",
                                 L"hotel doorman.jpg", L"Doorman_job_offer.pdf", L"doorman_fanart.png", L"UZIN_katalog.pdf", L"uzit.txt", L"uzis.png",
                                 L"uzidrone.png", L"uziblender.blend", L"J_N_Smith_resume.pdf", L"N V J.png", L"uzi_vs_cyn.png", L"C:\\Docs\\УЗИ почек\\scan_01.jpg"})
            nameLog(n);
        wchar_t cf[MAX_PATH];
        GetFullPathNameW((dir + L"\\..\\filesense_corpus").c_str(), MAX_PATH, cf, nullptr);
        std::wstring corpus = cf;
        std::string txt;
        if (readFile(corpus + L"\\names.txt", txt)) {
            int cnt[4] = {}, total = 0;
            std::wstring all = widen(txt);
            for (size_t a = 0, b; a < all.size(); a = b + 1) {
                b = all.find(L'\n', a);
                if (b == std::wstring::npos) b = all.size();
                std::wstring n = all.substr(a, b - a);
                while (!n.empty() && (n.back() == L'\r' || n.back() == L' ' || n.back() == 0xFEFF)) n.pop_back();
                if (!n.empty() && n[0] == 0xFEFF) n.erase(0, 1);
                if (n.empty()) continue;
                FileSense fs = nameLog(n);
                cnt[fs.self]++;
                total++;
            }
            logf("sense real names: %d total, self2 %d, none %d", total, cnt[2], cnt[0]);
        }
        std::vector<std::wstring> files;
        WIN32_FIND_DATAW fd;
        HANDLE fh = FindFirstFileW((corpus + L"\\*").c_str(), &fd);
        if (fh != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) files.push_back(fd.cFileName);
            } while (FindNextFileW(fh, &fd));
            FindClose(fh);
        }
        std::sort(files.begin(), files.end());
        for (auto& f : files) {
            if (!senseWantsPixels(f)) continue;
            FileSense a1 = senseFile(corpus + L"\\" + f);
            FileSense fs = senseFile(corpus + L"\\" + f);
            logf("sense file '%s': purple %.3f dark %.3f eye %.4f edge %.3f bg %.2f color %.2f pixels %d -> self %d kind %s | %.1f ms, again %.1f ms",
                 narrow(f).c_str(), fs.purple, fs.dark, fs.eye, fs.edge, fs.bg, fs.colorScore, (int)fs.hadPixels, fs.self, fileKindName(fs.kind), a1.ms, fs.ms);
        }
        logf("sense corpus: %zu files in '%s'", files.size(), narrow(corpus).c_str());

        auto drop = [&](const std::wstring& file, const wchar_t* shotName) {
            reset(St::Idle, 100);
            sim(0.5f, still);
            float rel0 = p.vit.rel;
            p.receiveFile(corpus + L"\\" + file, 1);
            double w0 = nowSec();
            if (p.senseJob)
                for (int i = 0; i < 600 && !p.senseJob->done; i++) Sleep(5);
            double waited = (nowSec() - w0) * 1000.0;
            sim(2.5f, still);
            logf("sense drop '%s': waited %.0f ms self %d kind %s bubble '%s'", narrow(file).c_str(), waited, p.heldSense.self,
                 fileKindName(p.heldSense.kind), narrow(a.bubble.text).c_str());
            sim(1.4f, still);
            logf("sense drop '%s': blush %.2f happy %.2f surprise %.2f angry %.2f rel %.1f -> %.1f life %.1f", narrow(file).c_str(), p.face.blush,
                 p.face.happy, p.face.surprise, p.face.angry, rel0, p.vit.rel, p.heldLife);
            if (shotName) shot(shotName);
        };
        p.vit.rel = 50.f;
        p.vit.lastSelfArt = 0;
        drop(L"UziArt.png", L"72_file_self_sure");
        drop(L"Uzi_AnimRig.blend", L"72b_file_self_3d");
        drop(L"muzika.mp3", L"72c_file_negative");
        drop(L"drawing_17.webp", L"72d_file_self_maybe");
        drop(L"synth_purple_black.png", nullptr);
        drop(L"uzi_face_preview.mp4", nullptr);
        drop(L"УЗИ брюшной полости.pdf", nullptr);
        drop(L"uzi gun.png", nullptr);
        drop(L"cyn.png", nullptr);
        drop(L"khan_doorman.png", nullptr);
        drop(L"N_and_Uzi.png", nullptr);
        sim(p.heldLife * 0.62f - 3.9f + 0.6f, still);
        logf("sense drop with: stage %d bubble '%s'", p.heldStage, narrow(a.bubble.text).c_str());
        p.vit.rel = 20.f;
        drop(L"UziArt.png", L"72e_file_self_hate");
        drop(L"N_and_Uzi.png", nullptr);
        sim(p.heldLife * 0.62f - 3.9f + 0.6f, still);
        logf("sense drop with at rel 20: stage %d bubble '%s'", p.heldStage, narrow(a.bubble.text).c_str());
        p.vit.rel = 50.f;
        drop(L"uzi_vs_cyn.png", nullptr);
        sim(p.heldLife * 0.62f - 3.9f + 0.6f, still);
        logf("sense drop with cyn: stage %d bubble '%s'", p.heldStage, narrow(a.bubble.text).c_str());
        drop(L"uziart_on_black.png", nullptr);
        drop(L"chibi_on_darkgrey.png", nullptr);
        drop(L"УЗИ 12 недель.jpg", nullptr);
        drop(L"Doorman.png", nullptr);
        drop(L"Узи милашка.png", nullptr);
        p.vit.lastSelfArt = 0;
        p.vit.intro = 2;
        a.bubble.close();
        drop(L"UziArt.png", nullptr);
        logf("sense drop baby: lastSelfArt %.0f", p.vit.lastSelfArt);
        p.vit.intro = 0;
        p.vit.rel = 50.f;
        p.heldPath.clear();
        a.bubble.close();
    }
