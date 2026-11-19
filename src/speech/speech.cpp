#include "speech.h"
#include "json.h"
#include "rig.h"

bool Phrases::loadFile(const std::wstring& path, const std::string& prefix) {
    std::string txt;
    if (!readFile(path, txt)) return false;
    bool ok = false;
    Json j = Json::parse(txt, &ok);
    if (!ok || j.t != Json::Obj) {
        logf("phrases: parse error in %s", narrow(path).c_str());
        return false;
    }
    for (size_t i = 0; i < j.k.size(); i++) {
        std::string key = prefix + j.k[i];
        std::vector<std::wstring> v;
        for (size_t n = 0; n < j.a[i].size(); n++) v.push_back(widen(j.a[i][n].str()));
        auto it = index.find(key);
        if (it != index.end()) {
            logf("phrases: '%s' is in more than one file, merged (%s)", key.c_str(), narrow(path).c_str());
            auto& dst = vals[it->second];
            dst.insert(dst.end(), v.begin(), v.end());
            continue;
        }
        index[key] = (int)keys.size();
        keys.push_back(key);
        vals.push_back(v);
    }
    return true;
}

static std::vector<std::wstring> jsonFilesIn(const std::wstring& dir) {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.json").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(dir + L"\\" + fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

bool Phrases::load(const std::wstring& path) {
    keys.clear();
    vals.clear();
    index.clear();
    stages.clear();
    chains.clear();
    DWORD at = GetFileAttributesW(path.c_str());
    if (at == INVALID_FILE_ATTRIBUTES) {
        logf("no phrases at %s", narrow(path).c_str());
        return false;
    }
    if (!(at & FILE_ATTRIBUTE_DIRECTORY)) {
        bool ok = loadFile(path, "");
        bags.assign(keys.size(), Bag{});
        return ok;
    }
    std::string st;
    Json cfg;
    if (readFile(path + L"\\stages.json", st)) cfg = Json::parse(st);
    const Json& names = cfg["stages"];
    for (size_t i = 0; i < names.size(); i++) stages.push_back(names[i].str());
    if (stages.empty()) stages = {"hate", "good", "warm", "love"};
    const Json& fb = cfg["fallback"];
    for (size_t i = 0; i < stages.size(); i++) {
        std::vector<int> ch{(int)i};
        const Json& f = fb[stages[i].c_str()];
        for (size_t k = 0; k < f.size(); k++) {
            auto it = std::find(stages.begin(), stages.end(), f[k].str());
            if (it != stages.end()) ch.push_back((int)(it - stages.begin()));
            else logf("phrases: stage '%s' falls back to unknown '%s'", stages[i].c_str(), f[k].str().c_str());
        }
        chains.push_back(ch);
    }
    int files = 0;
    for (auto& f : jsonFilesIn(path + L"\\any")) files += loadFile(f, "");
    for (auto& s : stages)
        for (auto& f : jsonFilesIn(path + L"\\" + widen(s))) files += loadFile(f, s + "/");
    bags.assign(keys.size(), Bag{});
    logf("phrases: %d files, %zu categories with stages, %zu stages", files, keys.size(), stages.size());
    return files > 0;
}

void Phrases::reindex() {
    index.clear();
    for (size_t i = 0; i < keys.size(); i++) index[keys[i]] = (int)i;
    if (bags.size() < keys.size()) bags.resize(keys.size());
}

int Phrases::find(const char* cat, int st) const {
    if (index.size() != keys.size()) const_cast<Phrases*>(this)->reindex();
    if (st == -2) st = stage;
    auto has = [&](const std::string& k) {
        auto it = index.find(k);
        return it != index.end() && !vals[it->second].empty() ? it->second : -1;
    };
    if (st >= 0 && st < (int)chains.size())
        for (int s : chains[st]) {
            int i = has(stages[s] + "/" + cat);
            if (i >= 0) return i;
        }
    return has(cat);
}

std::string Phrases::layerOf(const char* cat, int st) const {
    int i = find(cat, st);
    if (i < 0) return "";
    size_t sl = keys[i].find('/');
    return sl == std::string::npos ? "any" : keys[i].substr(0, sl);
}

std::vector<std::wstring> Phrases::listAt(const char* cat, int st) const {
    int i = find(cat, st);
    return i < 0 ? std::vector<std::wstring>{} : vals[i];
}

static uint64_t lineHash(const std::wstring& s) {
    uint64_t h = 1469598103934665603ull;
    for (wchar_t c : s) h = (h ^ (uint64_t)c) * 1099511628211ull;
    return h;
}

std::wstring Phrases::pick(const char* cat, const std::wstring& arg) {
    int fi = find(cat);
    if (fi >= 0) {
        size_t i = (size_t)fi;
        auto& v = vals[i];
        int n = (int)v.size();
        auto fits = [&](int k) { return !arg.empty() || v[k].find(L"{arg}") == std::wstring::npos; };
        bool anyFits = false;
        for (int k = 0; k < n && !anyFits; k++) anyFits = fits(k);
        auto heard = [&](int k) { return std::find(recent.begin(), recent.end(), lineHash(v[k])) != recent.end(); };
        Bag& b = bags[i];
        auto deal = [&]() {
            b.order.resize(n);
            for (int k = 0; k < n; k++) b.order[k] = k;
            for (int k = n - 1; k > 0; k--) std::swap(b.order[k], b.order[randi(0, k)]);
            if (n > 1 && b.order[0] == b.last) std::swap(b.order[0], b.order[randi(1, n - 1)]);
            b.pos = 0;
        };
        if ((int)b.order.size() != n || b.pos >= n) deal();
        int take = -1;
        for (int pass = 0; pass < 2 && take < 0; pass++)
            for (int j = b.pos; j < n; j++) {
                int k = b.order[j];
                if ((anyFits && !fits(k)) || (pass == 0 && heard(k))) continue;
                take = j;
                break;
            }
        if (take < 0) {
            deal();
            for (int j = 0; j < n && take < 0; j++)
                if (!anyFits || fits(b.order[j])) take = j;
            if (take < 0) take = 0;
        }
        std::swap(b.order[b.pos], b.order[take]);
        int k = b.order[b.pos++];
        b.last = k;
        lastCat = cat;
        lastOwn = false;
        if (keys[i].find('/') != std::string::npos) {
            auto it = index.find(cat);
            lastOwn = it == index.end() || std::find(vals[it->second].begin(), vals[it->second].end(), v[k]) == vals[it->second].end();
        }
        recent.push_back(lineHash(v[k]));
        if (recent.size() > kRecent) recent.erase(recent.begin(), recent.begin() + (recent.size() - kRecent));
        dirty = true;
        return fill(v[k], arg);
    }
    return L"";
}

void Phrases::loadState(const std::wstring& path) {
    statePath = path;
    std::string raw;
    if (!readFile(path, raw)) return;
    int restored = 0;
    size_t a = 0;
    while (a < raw.size()) {
        size_t e = raw.find('\n', a);
        if (e == std::string::npos) e = raw.size();
        std::string l = raw.substr(a, e - a);
        a = e + 1;
        if (!l.empty() && l.back() == '\r') l.pop_back();
        std::vector<std::string> f;
        for (size_t p = 0;;) {
            size_t q = l.find('\t', p);
            f.push_back(l.substr(p, q == std::string::npos ? std::string::npos : q - p));
            if (q == std::string::npos) break;
            p = q + 1;
        }
        auto nums = [](const std::string& s) {
            std::vector<std::string> out;
            for (size_t p = 0; p < s.size();) {
                size_t q = s.find(',', p);
                if (q == std::string::npos) q = s.size();
                if (q > p) out.push_back(s.substr(p, q - p));
                p = q + 1;
            }
            return out;
        };
        if (f.size() == 2 && f[0] == "recent") {
            for (auto& h : nums(f[1])) recent.push_back(strtoull(h.c_str(), nullptr, 16));
            continue;
        }
        if (f.size() != 4) continue;
        auto it = std::find(keys.begin(), keys.end(), f[0]);
        if (it == keys.end()) continue;
        size_t i = it - keys.begin();
        int n = (int)vals[i].size();
        Bag b;
        for (auto& x : nums(f[3])) b.order.push_back(atoi(x.c_str()));
        b.pos = atoi(f[1].c_str());
        b.last = atoi(f[2].c_str());
        std::vector<int> chk = b.order;
        std::sort(chk.begin(), chk.end());
        bool ok = (int)chk.size() == n && b.pos >= 0 && b.pos <= n;
        for (int k = 0; ok && k < n; k++) ok = chk[k] == k;
        if (!ok) continue;
        if (b.last >= n) b.last = -1;
        bags[i] = b;
        restored++;
    }
    if (recent.size() > kRecent) recent.erase(recent.begin(), recent.begin() + (recent.size() - kRecent));
    logf("phrases: said-state restored for %d categories, %zu recent lines", restored, recent.size());
}

void Phrases::saveState() {
    if (!dirty || statePath.empty()) return;
    std::string t;
    char hx[24];
    t += "recent\t";
    for (size_t k = 0; k < recent.size(); k++) {
        snprintf(hx, sizeof(hx), "%llx", (unsigned long long)recent[k]);
        t += (k ? "," : "") + std::string(hx);
    }
    t += "\r\n";
    for (size_t i = 0; i < keys.size(); i++) {
        const Bag& b = bags[i];
        if (b.order.empty()) continue;
        t += keys[i] + "\t" + std::to_string(b.pos) + "\t" + std::to_string(b.last) + "\t";
        for (size_t k = 0; k < b.order.size(); k++) t += (k ? "," : "") + std::to_string(b.order[k]);
        t += "\r\n";
    }
    if (writeFile(statePath, t)) dirty = false;
}

std::wstring Phrases::fill(std::wstring s, const std::wstring& arg) const {
    size_t at = s.find(L"%s");
    if (at != std::wstring::npos) s.replace(at, 2, arg);
    for (size_t p = 0; (p = s.find(L"{arg}", p)) != std::wstring::npos;) {
        s.replace(p, 5, arg);
        p += arg.size();
        if (!arg.empty() && arg.back() == L'\x2026') {
            size_t q = p;
            while (q < s.size() && wcschr(L".?!", s[q])) q++;
            bool ask = s.substr(p, q - p).find(L'?') != std::wstring::npos;
            s.replace(p, q - p, ask ? L"?" : L"");
        }
    }
    for (size_t p = 0; (p = s.find(L"{name}", p)) != std::wstring::npos; p += userName.size()) s.replace(p, 6, userName);
    for (size_t p = 0; (p = s.find(L"{g:", p)) != std::wstring::npos;) {
        size_t bar = s.find(L'|', p), end = s.find(L'}', p);
        if (bar == std::wstring::npos || end == std::wstring::npos || bar > end) break;
        std::wstring m = s.substr(p + 3, bar - p - 3), f = s.substr(bar + 1, end - bar - 1);
        s.replace(p, end - p + 1, gender == 2 ? f : m);
        p += (gender == 2 ? f : m).size();
    }
    return s;
}

std::vector<std::wstring> Phrases::list(const char* cat) const { return listAt(cat, -2); }

void Bubble::close() {
    if (held()) logf("bubble: cut '%s' while being read (%.1f of %.1f s)", narrow(text).c_str(), t, holdT);
    else if (asking() && reading()) logf("bubble: question '%s' (%s) closed at %.1f of %.1f s reading", narrow(text).c_str(), askId.c_str(), t, holdT);
    active = false;
    choices.clear();
    askId.clear();
    input = false;
}

void Bubble::say(const std::wstring& raw) {
    if (raw.empty()) return;
    if (held()) logf("bubble: cut '%s' while being read (%.1f of %.1f s)", narrow(text).c_str(), t, holdT);
    dismissed = false;
    text.clear();
    revealAt.clear();
    fx.clear();
    cues.clear();
    replyKind.clear();
    replyAllow.clear();
    talkId.clear();
    holdT = 0;
    const float per = 1.f / 38.f;
    float tt = 0;
    uint8_t cur = 0;
    for (size_t i = 0; i < raw.size(); i++) {
        wchar_t ch = raw[i];
        if (ch == L'[') {
            size_t e = raw.find(L']', i);
            if (e != std::wstring::npos && e - i < 32) {
                std::wstring tag = raw.substr(i + 1, e - i - 1);
                if (tag.rfind(L"re:", 0) == 0 || tag.rfind(L"th:", 0) == 0) {
                    std::string v = narrow(tag.substr(3));
                    if (tag[0] == L'r') {
                        size_t sl = v.find('/');
                        if (sl != std::string::npos) {
                            for (size_t p = sl + 1; p < v.size();) {
                                size_t q = v.find(',', p);
                                if (q == std::string::npos) q = v.size();
                                if (q > p) replyAllow.push_back(atoi(v.substr(p, q - p).c_str()));
                                p = q + 1;
                            }
                            v.resize(sl);
                        }
                        replyKind = v;
                    } else {
                        talkId = v;
                    }
                    i = e;
                    while (i + 1 < raw.size() && raw[i + 1] == L' ' && text.empty()) i++;
                    continue;
                }
                size_t colon = tag.find(L':');
                Cue c{tt, narrow(tag.substr(0, colon)), colon == std::wstring::npos ? -1.f : (float)_wtof(tag.c_str() + colon + 1), false};
                cues.push_back(c);
                i = e;
                while (i + 1 < raw.size() && raw[i + 1] == L' ' && (text.empty() || text.back() == L' ')) i++;
                continue;
            }
        }
        if (ch == L'~') { cur = cur == 1 ? 0 : 1; continue; }
        if (ch == L'^') { cur = cur == 2 ? 0 : 2; continue; }
        if (ch == L'|') { tt += 0.35f; continue; }
        text.push_back(ch);
        revealAt.push_back(tt);
        fx.push_back(cur);
        tt += cur == 1 ? per * 0.6f : per;
        wchar_t prev = i > 0 ? raw[i - 1] : 0, next = i + 1 < raw.size() ? raw[i + 1] : 0;
        if (ch == L'.' && (prev == L'.' || next == L'.')) tt += 0.17f;
        else if ((ch == L'.' || ch == L'!' || ch == L'?') && next == L' ') tt += 0.22f;
        else if (ch == L',' && next == L' ') tt += 0.08f;
    }
    typeDur = tt;
    {
        float rd = 0.4f;
        for (size_t i = 0; i < text.size(); i++) {
            rd = std::max(rd + 1.f / 14.f, revealAt[i] + 0.05f);
            wchar_t ch = text[i];
            if (ch == L'.' || ch == L'!' || ch == L'?' || ch == L'\x2026') rd += 0.15f;
            else if (ch == L',') rd += 0.05f;
        }
        readT = rd + 0.8f;
    }
    t = 0;
    life = std::max(typeDur + 2.0f + text.size() * 0.045f, readT + 1.2f);
    holdT = readT;
    active = true;
    choices.clear();
    askId.clear();
    meters.clear();
}

void Bubble::ask(const std::wstring& s, const std::vector<std::wstring>& ch, const std::string& id, float wait) {
    say(s);
    choices = ch;
    askId = id;
    life = typeDur + wait;
}

void Bubble::askInput(const std::wstring& s, const std::wstring& initial, const std::vector<std::wstring>& ch, const std::string& id) {
    ask(s, ch, id, 1e9f);
    input = true;
    inputText = initial;
}

int Bubble::choiceAt(V2 p) const {
    if (!asking() || t < typeDur) return -1;
    for (size_t i = 0; i < choiceRc.size(); i++) {
        const auto& r = choiceRc[i];
        if (p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom) return (int)i;
    }
    return -1;
}

void Bubble::hide() {
    if (active && t < life - 0.25f) t = life - 0.25f;
    dismissed = true;
    holdT = 0;
}

void Bubble::update(float dt) {
    if (!active) return;
    t += dt;
    for (auto& c : cues)
        if (!c.fired && t >= c.at) {
            logf("bubble cue [%s] at %.2f (t %.2f)", c.name.c_str(), c.at, t);
            c.fired = true;
            firedCues.push_back(c);
        }
    if (hoverKeep && !dismissed && t >= typeDur && hit(mouse)) {
        if (life < t + 1.5f) life = t + 1.5f;
        if (!asking() && holdT < t + 0.5f) holdT = t + 0.5f;
    }
    if (t >= life) {
        if (!choices.empty()) expired = askId;
        close();
    }
}

static float easeOutBack(float x) {
    const float c1 = 1.70158f, c3 = c1 + 1;
    return 1 + c3 * powf(x - 1, 3) + c1 * powf(x - 1, 2);
}

void Bubble::draw(DrawCtx& c, V2 headTop, V2 headC, float headR, D2D1_RECT_F area, D2D1_RECT_F win) {
    if (!active) return;
    float s = c.s;
    if (!fmt || fmtScale != s || fmtFamily != c.family) {
        fmt.Reset();
        btnFmt.Reset();
        c.dw->CreateTextFormat(c.family, c.fonts, DWRITE_FONT_WEIGHT_EXTRA_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.f * s,
                               L"ru-ru", &fmt);
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        c.dw->CreateTextFormat(c.family, c.fonts, DWRITE_FONT_WEIGHT_EXTRA_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.f * s,
                               L"ru-ru", &btnFmt);
        fmtScale = s;
        fmtFamily = c.family;
    }
    if (clearRt != c.rt || !clear) {
        clear.Reset();
        c.rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), &clear);
        clearRt = c.rt;
    }
    const D2D1_COLOR_F acc = D2D1::ColorF(0.839f, 0.102f, 0.933f, 1);
    D2D1_COLOR_F fill = dark ? D2D1::ColorF(0.07f, 0.05f, 0.10f, 1) : D2D1::ColorF(1, 1, 1, 1);
    D2D1_COLOR_F line = dark ? acc : D2D1::ColorF(0.165f, 0.106f, 0.239f, 1);
    D2D1_COLOR_F ink = dark ? D2D1::ColorF(0.957f, 0.914f, 1.f, 1) : D2D1::ColorF(0.165f, 0.106f, 0.239f, 1);
    D2D1_COLOR_F pill = dark ? D2D1::ColorF(0.12f, 0.09f, 0.17f, 1) : D2D1::ColorF(0.965f, 0.937f, 0.988f, 1);
    D2D1_COLOR_F pillLine = dark ? D2D1::ColorF(0.33f, 0.24f, 0.42f, 1) : D2D1::ColorF(0.867f, 0.788f, 0.941f, 1);
    D2D1_COLOR_F primary = dark ? D2D1::ColorF(0.55f, 0.07f, 0.62f, 1) : D2D1::ColorF(0.71f, 0.078f, 0.80f, 1);

    size_t n = text.size();
    ComPtr<IDWriteTextLayout> lay;
    c.dw->CreateTextLayout(text.c_str(), (UINT32)n, fmt.Get(), 210.f * s, 1000.f, &lay);
    DWRITE_TEXT_METRICS m{};
    lay->GetMetrics(&m);
    std::vector<uint8_t> solo(n, 0);
    for (size_t i = 0; i < n; i++) {
        bool shown = t >= revealAt[i];
        solo[i] = shown && (fx[i] != 0 || t - revealAt[i] < 0.12f);
        if (!shown || solo[i]) lay->SetDrawingEffect(clear.Get(), {(UINT32)i, 1});
    }

    float padX = 11 * s, padY = 7 * s;
    std::vector<ComPtr<IDWriteTextLayout>> bl;
    std::vector<float> bwid;
    float btnH = 24 * s, btnGap = 5 * s, btnsW = 0;
    bool grid = choices.size() > 3;
    float cellW = 0;
    if (!choices.empty()) {
        for (auto& ch : choices) {
            ComPtr<IDWriteTextLayout> l;
            c.dw->CreateTextLayout(ch.c_str(), (UINT32)ch.size(), btnFmt.Get(), 400.f * s, 100.f, &l);
            DWRITE_TEXT_METRICS bm{};
            l->GetMetrics(&bm);
            bl.push_back(l);
            bwid.push_back(bm.width + 20 * s);
            btnsW += bwid.back();
            cellW = std::max(cellW, bwid.back());
        }
        btnsW += btnGap * (choices.size() - 1);
        if (grid) btnsW = cellW * 2 + btnGap;
    }
    size_t rowsN = grid ? (choices.size() + 1) / 2 : (choices.empty() ? 0 : 1);
    float contentW = std::max(m.width, btnsW);
    float inputH = input ? 28 * s : 0.f;
    if (input) contentW = std::max(contentW, 190 * s);
    if (grid) cellW = (contentW - btnGap) / 2;
    float meterH = meters.empty() ? 0.f : 10 * s + btnGap;
    float bw = contentW + padX * 2, bh = m.height + padY * 2 + meterH + (input ? inputH + btnGap : 0.f) +
                                        (choices.empty() ? 0 : btnGap + rowsN * btnH + (rowsN - 1) * btnGap);
    float gap = 12 * s;
    V2 tip(headTop.x, headTop.y - 4 * s);
    float bx = tip.x - bw / 2, by = tip.y - gap - bh;
    bool side = false;
    float minTop = std::max(area.top, win.top) + 4 * s;
    if (by < minTop) {
        side = true;
        bx = headC.x + headR + gap;
        if (bx + bw > std::min(area.right, win.right) - 4 * s) bx = headC.x - headR - gap - bw;
        by = clampf(headC.y - bh / 2, minTop, std::max(minTop, std::min(area.bottom, win.bottom) - bh - 4 * s));
        tip = V2(bx > headC.x ? headC.x + headR * 0.85f : headC.x - headR * 0.85f, headC.y);
    }
    float lo = std::max(area.left, win.left) + 4 * s, hi = std::min(area.right, win.right) - 4 * s - bw;
    bx = clampf(bx, lo, std::max(lo, hi));
    rect = D2D1::RectF(bx, by, bx + bw, by + bh);

    float pop = t < 0.2f ? easeOutBack(t / 0.2f) : 1.f;
    float alpha = t > life - 0.25f ? clampf((life - t) / 0.25f, 0, 1) : 1.f;
    D2D1::Matrix3x2F popM = D2D1::Matrix3x2F::Scale(pop, pop, tip.d2());
    c.rt->SetTransform(popM);

    V2 base;
    V2 along;
    if (!side) { base = V2(clampf(tip.x, bx + 14 * s, bx + bw - 14 * s), by + bh - 1); along = V2(1, 0); }
    else { base = V2(bx > tip.x ? bx + 1 : bx + bw - 1, clampf(tip.y, by + 12 * s, by + bh - 12 * s)); along = V2(0, 1); }
    float tw = 7 * s;
    V2 t0 = base - along * tw, t1 = base + along * tw;
    V2 tipP = tip + (base - tip).norm() * (3 * s);
    ComPtr<ID2D1RoundedRectangleGeometry> rr;
    c.f->CreateRoundedRectangleGeometry(D2D1::RoundedRect(rect, 15 * s, 15 * s), &rr);
    auto tri = makePath(c.f, [&](ID2D1GeometrySink* k) {
        k->BeginFigure(t0.d2(), D2D1_FIGURE_BEGIN_FILLED);
        k->AddLine(tipP.d2());
        k->AddLine(t1.d2());
        k->EndFigure(D2D1_FIGURE_END_CLOSED);
    });
    auto shape = makePath(c.f, [&](ID2D1GeometrySink* k) { rr->CombineWithGeometry(tri.Get(), D2D1_COMBINE_MODE_UNION, nullptr, k); });
    c.color(dark ? D2D1::ColorF(0, 0, 0, 1) : line, (dark ? 0.45f : 0.28f) * alpha);
    c.rt->SetTransform(D2D1::Matrix3x2F::Translation(0, 3.5f * s) * popM);
    c.rt->FillGeometry(shape.Get(), c.br);
    c.rt->SetTransform(popM);
    c.color(fill, alpha);
    c.rt->FillGeometry(shape.Get(), c.br);
    if (dark) {
        c.color(acc, 0.22f * alpha);
        c.rt->DrawGeometry(shape.Get(), c.br, 6.f * s, c.round);
    }
    c.color(line, alpha);
    c.rt->DrawGeometry(shape.Get(), c.br, (dark ? 2.f : 2.6f) * s, c.round);
    c.color(ink, alpha);
    c.rt->DrawTextLayout(D2D1::Point2F(bx + padX, by + padY), lay.Get(), c.br);
    for (size_t i = 0; i < n; i++) {
        if (!solo[i] || text[i] == L' ' || text[i] == L'\n') continue;
        FLOAT px = 0, py = 0;
        DWRITE_HIT_TEST_METRICS hm{};
        lay->HitTestTextPosition((UINT32)i, FALSE, &px, &py, &hm);
        float age = t - revealAt[i];
        float pin = clampf(age / 0.12f, 0.f, 1.f);
        float sc = 0.55f + 0.45f * easeOutBack(pin);
        V2 off;
        if (fx[i] == 1) off = V2(sinf(t * 34.f + i * 1.7f), cosf(t * 39.f + i * 2.3f)) * ((0.22f + 0.4f * expf(-age * 2.f)) * s);
        if (fx[i] == 2) off = V2(0, -fabsf(sinf(t * 7.f - i * 0.45f)) * 2.6f * s);
        float lx = bx + padX + hm.left + off.x, ly = by + padY + hm.top + off.y;
        D2D1_POINT_2F cc{lx + hm.width * 0.5f, ly + hm.height * 0.5f};
        c.rt->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc, cc) * popM);
        c.color(ink, alpha * pin);
        c.rt->DrawText(&text[i], 1, fmt.Get(), D2D1::RectF(lx, ly, lx + hm.width + 6 * s, ly + hm.height + 2 * s), c.br);
    }
    c.rt->SetTransform(popM);

    if (!meters.empty()) {
        float my = by + padY + m.height + btnGap * 0.5f + 5 * s;
        float segW = (contentW - btnGap * 2) / meters.size();
        D2D1_COLOR_F track = dark ? D2D1::ColorF(0.19f, 0.14f, 0.26f, 1) : D2D1::ColorF(0.91f, 0.87f, 0.96f, 1);
        for (size_t i = 0; i < meters.size(); i++) {
            float x = bx + padX + i * (segW + btnGap);
            const auto& mt = meters[i];
            V2 ic(x + 5 * s, my);
            c.color(mt.color, alpha);
            if (mt.icon == 2) {
                auto hg = heartGeometry(c.f, ic, 9.f * s);
                c.rt->FillGeometry(hg.Get(), c.br);
            } else if (mt.icon == 3) {
                auto hg = makePath(c.f, [&](ID2D1GeometrySink* k) {
                    k->BeginFigure({ic.x - 3.6f * s, ic.y - 5.2f * s}, D2D1_FIGURE_BEGIN_FILLED);
                    k->AddLine({ic.x + 3.6f * s, ic.y - 5.2f * s});
                    k->AddLine({ic.x + 0.6f * s, ic.y});
                    k->AddLine({ic.x + 3.6f * s, ic.y + 5.2f * s});
                    k->AddLine({ic.x - 3.6f * s, ic.y + 5.2f * s});
                    k->AddLine({ic.x - 0.6f * s, ic.y});
                    k->EndFigure(D2D1_FIGURE_END_CLOSED);
                });
                c.rt->FillGeometry(hg.Get(), c.br);
            } else if (mt.icon == 1) {
                c.rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(ic.x - 3 * s, ic.y - 4 * s, ic.x + 3 * s, ic.y + 5 * s), 1.2f * s, 1.2f * s), c.br);
                c.rt->FillRectangle(D2D1::RectF(ic.x - 1.3f * s, ic.y - 5.5f * s, ic.x + 1.3f * s, ic.y - 4 * s), c.br);
            } else {
                auto bolt = makePath(c.f, [&](ID2D1GeometrySink* k) {
                    k->BeginFigure({ic.x + 1.5f * s, ic.y - 5.5f * s}, D2D1_FIGURE_BEGIN_FILLED);
                    k->AddLine({ic.x - 3.5f * s, ic.y + 0.8f * s});
                    k->AddLine({ic.x - 0.3f * s, ic.y + 0.8f * s});
                    k->AddLine({ic.x - 1.5f * s, ic.y + 5.5f * s});
                    k->AddLine({ic.x + 3.5f * s, ic.y - 0.8f * s});
                    k->AddLine({ic.x + 0.3f * s, ic.y - 0.8f * s});
                    k->EndFigure(D2D1_FIGURE_END_CLOSED);
                });
                c.rt->FillGeometry(bolt.Get(), c.br);
            }
            D2D1_RECT_F tr = D2D1::RectF(x + 13 * s, my - 2.2f * s, x + segW, my + 2.2f * s);
            c.color(track, alpha);
            c.rt->FillRoundedRectangle(D2D1::RoundedRect(tr, 2.2f * s, 2.2f * s), c.br);
            float v = clampf(mt.v / 100.f, 0.f, 1.f);
            if (v > 0.005f) {
                D2D1_RECT_F fr = tr;
                fr.right = tr.left + std::max(4.4f * s, (tr.right - tr.left) * v);
                c.color(mt.color, alpha);
                c.rt->FillRoundedRectangle(D2D1::RoundedRect(fr, 2.2f * s, 2.2f * s), c.br);
            }
        }
    }

    float inputGap = 0;
    if (input) {
        float ia = alpha * clampf((t - typeDur) / 0.2f, 0.f, 1.f);
        D2D1_RECT_F ir = D2D1::RectF(bx + padX, by + padY + m.height + meterH + btnGap, bx + padX + contentW, by + padY + m.height + meterH + btnGap + inputH);
        auto irr = D2D1::RoundedRect(ir, 8 * s, 8 * s);
        c.color(pill, ia);
        c.rt->FillRoundedRectangle(irr, c.br);
        c.color(acc, ia);
        c.rt->DrawRoundedRectangle(irr, c.br, 1.8f * s);
        ComPtr<IDWriteTextLayout> il;
        c.dw->CreateTextLayout(inputText.c_str(), (UINT32)inputText.size(), fmt.Get(), 4000.f, inputH, &il);
        DWRITE_TEXT_METRICS im{};
        il->GetMetrics(&im);
        float tx = ir.left + 9 * s, avail = contentW - 20 * s;
        if (im.widthIncludingTrailingWhitespace > avail) tx -= im.widthIncludingTrailingWhitespace - avail;
        c.rt->PushAxisAlignedClip(D2D1::RectF(ir.left + 4 * s, ir.top, ir.right - 4 * s, ir.bottom), D2D1_ANTIALIAS_MODE_ALIASED);
        c.color(ink, ia);
        c.rt->DrawTextLayout(D2D1::Point2F(tx, ir.top + (inputH - im.height) * 0.5f), il.Get(), c.br);
        if (fmodf(t, 1.f) < 0.55f) {
            float cx = tx + im.widthIncludingTrailingWhitespace + 1.5f * s;
            c.rt->DrawLine({cx, ir.top + 6 * s}, {cx, ir.bottom - 6 * s}, c.br, 1.6f * s);
        }
        c.rt->PopAxisAlignedClip();
        inputGap = inputH + btnGap;
    }

    choiceRc.clear();
    if (!choices.empty()) {
        float ba = alpha * clampf((t - typeDur) / 0.2f, 0.f, 1.f);
        float x0 = bx + (bw - btnsW) * 0.5f, y0 = by + padY + m.height + meterH + btnGap + inputGap;
        float x = x0, y = y0;
        bool twoWay = choices.size() <= 2;
        for (size_t i = 0; i < choices.size(); i++) {
            float w = grid ? cellW : bwid[i];
            if (grid) {
                x = x0 + (i % 2) * (cellW + btnGap);
                y = y0 + (i / 2) * (btnH + btnGap);
                if (i + 1 == choices.size() && i % 2 == 0) x = x0 + (cellW + btnGap) * 0.5f;
            }
            D2D1_RECT_F r = D2D1::RectF(x, y, x + w, y + btnH);
            choiceRc.push_back(r);
            bool hov = ba > 0.5f && mouse.x >= r.left && mouse.x <= r.right && mouse.y >= r.top && mouse.y <= r.bottom;
            bool prim = twoWay && i == 0;
            auto rr2 = D2D1::RoundedRect(r, btnH * 0.5f, btnH * 0.5f);
            bool filled = prim || hov;
            c.color(hov ? acc : prim ? primary : pill, ba);
            c.rt->FillRoundedRectangle(rr2, c.br);
            if (!filled) {
                c.color(pillLine, ba);
                c.rt->DrawRoundedRectangle(rr2, c.br, 1.8f * s);
            }
            DWRITE_TEXT_METRICS bm{};
            bl[i]->GetMetrics(&bm);
            c.color(filled ? D2D1::ColorF(1, 1, 1, 1) : ink, ba);
            c.rt->DrawTextLayout(D2D1::Point2F(r.left + (w - bm.width) * 0.5f, r.top + (btnH - bm.height) * 0.5f), bl[i].Get(), c.br);
            if (!grid) x += bwid[i] + btnGap;
        }
        for (auto& r : choiceRc) {
            D2D1_POINT_2F a = popM.TransformPoint({r.left, r.top});
            D2D1_POINT_2F b = popM.TransformPoint({r.right, r.bottom});
            r = D2D1::RectF(a.x, a.y, b.x, b.y);
        }
    }
    c.rt->SetTransform(D2D1::Matrix3x2F::Identity());
}
