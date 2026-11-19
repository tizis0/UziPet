
    {
        const std::wstring nd = dir + L"\\note", nf = nd + L"\\Записка от Узи.txt";
        CreateDirectoryW(nd.c_str(), nullptr);
        DeleteFileW(nf.c_str());
        p.noteDir = nd;
        auto napUntil = [&](float maxSecs) {
            std::string seen;
            float tt = 0;
            for (; tt < maxSecs && !p.afkNap; tt += 0.25f) {
                sim(0.25f, still);
                std::string s = p.stateName();
                if (seen.empty() || seen.substr(seen.rfind(' ') + 1) != s) seen += " " + s;
            }
            return std::make_pair(tt, seen);
        };
        const float e0s[2] = {60.f, 80.f}, rels[2] = {50.f, 75.f};
        for (int k = 0; k < 2; k++) {
            simIdleRun = false;
            simIdle = 100;
            reset(St::Idle, 100);
            p.vit.rel = rels[k];
            p.vit.energy = e0s[k];
            p.vit.restoreH = 0.02f;
            sim(0.5f, still);
            a.bubble.close();
            simIdle = 470;
            simIdleRun = true;
            float eStart = p.vit.energy;
            auto r = napUntil(40.f);
            logf("afk nap e0 %.0f: after %.2f s (idle %.0f) afkNap %d state %s deep %d energy %.2f -> %.2f states%s", e0s[k], r.first, simIdle,
                 (int)p.afkNap, p.stateName(), (int)p.deepSleep, eStart, p.vit.energy, r.second.c_str());
            float lo = p.vit.energy, hi = p.vit.energy;
            for (int f = 0; f < 80; f++) {
                sim(0.25f, still);
                lo = std::min(lo, p.vit.energy);
                hi = std::max(hi, p.vit.energy);
                if (k == 0 && f == 12) shot(L"63_afk_nap");
            }
            logf("afk nap charge e0 %.0f: energy now %.2f (min %.2f max %.2f over 20 s) state %s afkNap %d", e0s[k], p.vit.energy, lo, hi,
                 p.stateName(), (int)p.afkNap);
        }
        p.vit.lastNote = 0;
        p.vit.stock = 0;
        p.vit.parcelOut = true;
        simIdle = 1495;
        sim(10.f, still);
        FILE* f = _wfopen(nf.c_str(), L"rb");
        std::string bytes;
        if (f) {
            char buf[4096];
            size_t n = fread(buf, 1, sizeof(buf), f);
            bytes.assign(buf, n);
            fclose(f);
        }
        std::string hex, txt = bytes.size() > 3 ? bytes.substr(3) : "";
        for (size_t i = 0; i < std::min<size_t>(bytes.size(), 8); i++) {
            char h[4];
            snprintf(h, 4, "%02X ", (unsigned char)bytes[i]);
            hex += h;
        }
        int crlf = 0, lone = 0;
        for (size_t i = 0; i < txt.size(); i++)
            if (txt[i] == '\n') (i > 0 && txt[i - 1] == '\r') ? crlf++ : lone++;
        std::string shown;
        for (char c : txt) shown += c == '\r' ? std::string("") : c == '\n' ? std::string(" / ") : std::string(1, c);
        bool marks = txt.find_first_of("~^|{}") != std::string::npos;
        logf("afk note: exists %d size %zu first bytes %s crlf %d lone lf %d markup %d noteTell %d lastNote %.0f text '%s'", (int)(f != nullptr),
             bytes.size(), hex.c_str(), crlf, lone, (int)marks, (int)p.afkNoteTell, p.vit.lastNote, shown.c_str());
        DeleteFileW(nf.c_str());
        p.vit.lastNote = 0;
        sim(5.f, still);
        logf("afk note again in same absence: exists %d", (int)(GetFileAttributesW(nf.c_str()) != INVALID_FILE_ATTRIBUTES));
        p.vit.lastNote = (double)time(nullptr);
        simIdleRun = false;
        simIdle = 0.5;
        float rel0 = p.vit.rel, back = 0;
        std::string said;
        for (; back < 45.f && p.st == St::Sleep; back += 0.25f) {
            sim(0.25f, still);
            if (a.bubble.active && said.find(narrow(a.bubble.text)) == std::string::npos) said += " [" + narrow(a.bubble.text) + "]";
        }
        logf("afk back: woke after %.2f s state %s afkNap %d rel %.1f -> %.1f bubble '%s'", back, p.stateName(), (int)p.afkNap, rel0, p.vit.rel,
             narrow(a.bubble.text).c_str());
        sim(std::min(a.bubble.typeDur + 0.3f, 6.f), still);
        shot(L"63b_afk_wake");
        std::string first = narrow(a.bubble.text);
        for (int k = 0; k < 40 && narrow(a.bubble.text) == first; k++) sim(0.25f, still);
        if (a.bubble.active) said += " [" + narrow(a.bubble.text) + "]";
        sim(std::min(a.bubble.typeDur + 0.3f, 6.f), still);
        shot(L"63c_afk_note_tell");
        for (int k = 0; k < 40; k++) {
            sim(0.25f, still);
            if (a.bubble.active && said.find(narrow(a.bubble.text)) == std::string::npos) said += " [" + narrow(a.bubble.text) + "]";
        }
        logf("afk back lines:%s | rel %.1f wasAway %d", said.c_str(), p.vit.rel, (int)p.wasAway);
        g_snapMode = true;
        openSettings(false);
        g_settings.surf.savePng(a.gfx, dir + L"\\63d_settings_notes.png");
        p.vit.stock = 2;
        p.vit.parcelOut = false;

        for (float rel : {15.f, 96.f}) {
            reset(St::Idle, 100);
            p.vit.rel = rel;
            p.vit.energy = 30.f;
            sim(0.5f, still);
            a.bubble.close();
            simIdle = 480;
            simIdleRun = true;
            auto r = napUntil(40.f);
            simIdleRun = false;
            simIdle = 100;
            float rb = p.vit.rel;
            p.poke();
            sim(0.3f, still);
            logf("afk poke rel %.0f: napped %d after %.1f s, poke -> state %s afkNap %d rel %.1f -> %.1f pokes %d bubble '%s'", rel, (int)(r.first < 40.f),
                 r.first, p.stateName(), (int)p.afkNap, rb, p.vit.rel, p.wakePokes, narrow(a.bubble.text).c_str());
        }
        reset(St::Idle, 100);
        p.vit.rel = 50.f;
        p.vit.energy = 50.f;
        sim(0.5f, still);
        a.bubble.close();
        simIdle = 480;
        simIdleRun = true;
        napUntil(40.f);
        simIdleRun = false;
        simIdle = 100;
        float rd = p.vit.rel;
        V2 gp = p.pos - V2(0, 20);
        p.beginDrag(gp);
        sim(0.5f, [&](float tt) { return gp + V2(0, -tt * 100.f); });
        logf("afk drag: state %s afkNap %d carry %d rel %.1f -> %.1f bubble '%s'", p.stateName(), (int)p.afkNap, (int)p.sleepCarry, rd, p.vit.rel,
             narrow(a.bubble.text).c_str());
        p.endDrag();
        sim(1.5f, still);
        reset(St::Idle, 100);
        p.vit.energy = 40.f;
        sim(0.5f, still);
        simIdle = 480;
        simIdleRun = true;
        napUntil(40.f);
        simIdleRun = false;
        simIdle = 100;
        p.openTalk();
        sim(0.5f, still);
        float rt = p.vit.rel;
        p.answer("talkSleep", 0);
        sim(0.3f, still);
        logf("afk talk wake: state %s afkNap %d rel %.1f -> %.1f bubble '%s'", p.stateName(), (int)p.afkNap, rt, p.vit.rel, narrow(a.bubble.text).c_str());

        reset(St::Idle, 100);
        sim(0.5f, still);
        p.vit.intro = 2;
        simIdle = 600;
        sim(3.f, still);
        bool babyNap = p.afkNap;
        p.vit.intro = 0;
        reset(St::Watch, 100);
        sim(3.f, still);
        bool watchNap = p.afkNap;
        reset(St::Idle, 100);
        sim(0.3f, still);
        p.vit.energy = 20.f;
        p.sleepNow();
        sim(3.f, still);
        bool deepNap = p.afkNap;
        logf("afk not: baby %d watch %d deep sleep %d (deep %d)", (int)babyNap, (int)watchNap, (int)deepNap, (int)p.deepSleep);
        reset(St::Idle, 100);
        p.vit.energy = 50.f;
        sim(0.5f, still);
        simIdle = 480;
        simIdleRun = true;
        napUntil(40.f);
        simIdle = 1500;
        sim(2.f, still);
        logf("afk note cooldown: exists %d noteDone %d", (int)(GetFileAttributesW(nf.c_str()) != INVALID_FILE_ATTRIBUTES), (int)p.afkNoteDone);
        simIdleRun = false;
        simIdle = 100;
        p.vit.energy = 100.f;
        p.vit.restoreH = 1.5f;
        p.vit.lastNote = 0;
        reset(St::Idle, 100);
        sim(0.5f, still);
        a.bubble.close();
    }
