#include "robloxsense.h"
#include "json.h"
#include <winhttp.h>
#include <tlhelp32.h>
#pragma comment(lib, "winhttp.lib")

static const size_t npos = std::string::npos;

static long long numAfter(const std::string& s, const char* key) {
    size_t p = s.find(key);
    if (p == npos) return 0;
    p += strlen(key);
    long long v = 0;
    for (int n = 0; p < s.size() && isdigit((unsigned char)s[p]) && n < 18; p++, n++) v = v * 10 + (s[p] - '0');
    return v;
}

double rbxLogTime(const std::string& l) {
    int Y, M, D, h, m;
    double s;
    if (sscanf(l.c_str(), "%4d-%2d-%2dT%2d:%2d:%lfZ", &Y, &M, &D, &h, &m, &s) != 6) return 0;
    tm t{};
    t.tm_year = Y - 1900;
    t.tm_mon = M - 1;
    t.tm_mday = D;
    t.tm_hour = h;
    t.tm_min = m;
    __time64_t b = _mkgmtime64(&t);
    return b < 0 ? 0 : (double)b + s;
}

RbxLog rbxParseLine(const std::string& line) {
    RbxLog r;
    if (line.find("[FLog::GameJoinLoadTime]") != npos) {
        r.place = numAfter(line, "placeid:");
        r.universe = numAfter(line, "universeid:");
        if (r.universe) r.k = RbxLog::Join;
    } else if (line.find("! Joining game '") != npos) {
        r.place = numAfter(line, "' place ");
        if (r.place) r.k = RbxLog::Place;
    } else if (line.find("[FLog::SingleSurfaceApp] leaveUGCGameInternal") != npos || line.find("[FLog::SessionTransitionFSM] Tearing down.") != npos) {
        r.k = RbxLog::Leave;
    }
    if (r.k != RbxLog::None) r.t = rbxLogTime(line);
    return r;
}

std::string rbxDescribe(const RbxEvent& e) {
    char b[300];
    if (e.join)
        snprintf(b, sizeof(b), "join universe %lld place %lld%s, name '%s' genre '%s'", e.universe, e.place, e.again ? " (again)" : "", e.name.c_str(),
                 e.genre.c_str());
    else
        snprintf(b, sizeof(b), "leave universe %lld after %.0f s%s", e.universe, e.played, e.switched ? " (straight into another game)" : "");
    return b;
}

static long long gameKey(long long u, long long place) { return u ? u : -place; }

void RbxSense::join(std::vector<RbxEvent>& out, long long u, long long place, double t) {
    RbxEvent e;
    e.universe = u;
    e.place = place;
    e.t = t;
    auto it = leftAt.find(gameKey(u, place));
    e.again = it != leftAt.end() && t - it->second < 30 * 60;
    cur = u;
    curPlace = place;
    since = t;
    out.push_back(e);
}

void RbxSense::leave(std::vector<RbxEvent>& out, double t, bool switched) {
    RbxEvent e;
    e.join = false;
    e.universe = cur;
    e.place = curPlace;
    e.t = t;
    e.played = std::max(0.0, t - since);
    e.switched = switched;
    leftAt[gameKey(cur, curPlace)] = t;
    cur = curPlace = 0;
    out.push_back(e);
}

std::vector<RbxEvent> RbxSense::feed(const RbxLog& l) {
    std::vector<RbxEvent> out;
    switch (l.k) {
    case RbxLog::Place:
        pendPlace = l.place;
        pendT = l.t;
        break;
    case RbxLog::Join:
        pendPlace = 0;
        if (inGame() && (l.universe == cur || (!cur && l.place == curPlace))) {
            cur = l.universe;
            curPlace = l.place;
            break;
        }
        if (inGame()) leave(out, l.t, true);
        join(out, l.universe, l.place, l.t);
        break;
    case RbxLog::Leave:
        pendPlace = 0;
        if (inGame()) leave(out, l.t, false);
        break;
    default: break;
    }
    return out;
}

std::vector<RbxEvent> RbxSense::flush(double t) {
    std::vector<RbxEvent> out;
    if (!pendPlace || t - pendT <= 10) return out;
    long long p = pendPlace;
    pendPlace = 0;
    if (!inGame()) join(out, 0, p, pendT);
    return out;
}

std::vector<RbxEvent> RbxSense::gone(double t) {
    std::vector<RbxEvent> out;
    pendPlace = 0;
    if (inGame()) leave(out, t, false);
    return out;
}

std::vector<std::wstring> RbxLogTail::watching() const {
    std::vector<std::wstring> v;
    for (auto& f : files) v.push_back(f.name);
    return v;
}

void RbxLogTail::poll(std::vector<std::pair<std::string, bool>>& out) {
    struct C { std::wstring name; unsigned long long born; };
    std::vector<C> cs;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.log").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring low = fd.cFileName;
            CharLowerBuffW(low.data(), (DWORD)low.size());
            if (low.find(L"_player_") == std::wstring::npos || low.find(L"crashhandler") != std::wstring::npos) continue;
            cs.push_back({fd.cFileName, ((unsigned long long)fd.ftCreationTime.dwHighDateTime << 32) | fd.ftCreationTime.dwLowDateTime});
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(cs.begin(), cs.end(), [](const C& a, const C& b) { return a.born > b.born; });
    if (cs.size() > 3) cs.resize(3);
    std::reverse(cs.begin(), cs.end());
    files.erase(std::remove_if(files.begin(), files.end(),
                               [&](const F& f) { return std::none_of(cs.begin(), cs.end(), [&](const C& c) { return c.name == f.name; }); }),
                files.end());
    const unsigned long long kChunk = 4ull << 20;
    for (auto& c : cs) {
        auto it = std::find_if(files.begin(), files.end(), [&](const F& f) { return f.name == c.name; });
        bool fresh = it == files.end();
        if (fresh) {
            files.push_back(F{c.name});
            it = files.end() - 1;
        }
        F& f = *it;
        HANDLE fh = CreateFileW((dir + L"\\" + c.name).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
        if (fh == INVALID_HANDLE_VALUE) continue;
        LARGE_INTEGER sz{};
        GetFileSizeEx(fh, &sz);
        unsigned long long size = (unsigned long long)sz.QuadPart, quietTo = 0;
        if (fresh && !started) {
            f.off = size > kChunk ? size - kChunk : 0;
            f.skipFirst = f.off > 0;
            quietTo = size;
        }
        if (size < f.off) {
            f.off = 0;
            f.part.clear();
        }
        if (size > f.off) {
            unsigned long long start = f.off;
            DWORD want = (DWORD)std::min(size - f.off, kChunk), got = 0;
            std::string buf(want, '\0');
            LARGE_INTEGER at;
            at.QuadPart = (LONGLONG)f.off;
            if (SetFilePointerEx(fh, at, nullptr, FILE_BEGIN) && ReadFile(fh, buf.data(), want, &got, nullptr)) {
                buf.resize(got);
                f.off += got;
                size_t p = 0;
                for (size_t e; (e = buf.find('\n', p)) != npos; p = e + 1) {
                    std::string ln = f.part + buf.substr(p, e - p);
                    f.part.clear();
                    if (!ln.empty() && ln.back() == '\r') ln.pop_back();
                    if (f.skipFirst) f.skipFirst = false;
                    else out.push_back({ln, start + e + 1 <= quietTo});
                }
                f.part += buf.substr(p);
                if (f.part.size() > 65536) f.part.clear();
            }
        }
        CloseHandle(fh);
    }
    started = true;
}

bool robloxRunning() {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (s == INVALID_HANDLE_VALUE) return true;
    PROCESSENTRY32W pe{sizeof(pe)};
    bool yes = false;
    for (BOOL ok = Process32FirstW(s, &pe); ok && !yes; ok = Process32NextW(s, &pe)) yes = !_wcsicmp(pe.szExeFile, L"RobloxPlayerBeta.exe");
    CloseHandle(s);
    return yes;
}

static bool httpGet(const wchar_t* host, const std::wstring& path, std::string& body) {
    body.clear();
    HINTERNET s = WinHttpOpen(L"UziPet", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return false;
    WinHttpSetTimeouts(s, 5000, 5000, 5000, 5000);
    HINTERNET c = WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    bool ok = r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr);
    DWORD code = 0, csz = sizeof(code);
    if (ok)
        ok = WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &csz, WINHTTP_NO_HEADER_INDEX) &&
             code == 200;
    while (ok) {
        DWORD n = 0, got = 0;
        if (!WinHttpQueryDataAvailable(r, &n)) ok = false;
        if (!ok || !n) break;
        size_t at = body.size();
        body.resize(at + n);
        if (!WinHttpReadData(r, body.data() + at, n, &got)) ok = false;
        body.resize(at + got);
        if (body.size() > (1u << 20)) ok = false;
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    if (!ok) logf("rbx: GET %s failed, code %lu, err %lu", narrow(path).c_str(), code, GetLastError());
    return ok;
}

bool rbxParseGames(const std::string& body, long long universe, RbxName& out) {
    bool ok = false;
    Json j = Json::parse(body, &ok);
    if (!ok) return false;
    const Json& d = j["data"];
    for (size_t i = 0; i < d.size(); i++) {
        const Json& g = d[i];
        if ((long long)g["id"].n != universe) continue;
        out.name = g["name"].str();
        out.genre = g["genre_l1"].str() + "/" + g["genre_l2"].str() + "/" + g["genre"].str();
        return !out.name.empty();
    }
    return false;
}

bool rbxFetchGame(long long universe, RbxName& out) {
    std::string body;
    if (!httpGet(L"games.roblox.com", L"/v1/games?universeIds=" + std::to_wstring(universe), body)) return false;
    return rbxParseGames(body, universe, out);
}

long long rbxFetchUniverse(long long place) {
    std::string body;
    if (!httpGet(L"apis.roblox.com", L"/universes/v1/places/" + std::to_wstring(place) + L"/universe", body)) return 0;
    bool ok = false;
    Json j = Json::parse(body, &ok);
    return ok ? (long long)j["universeId"].n : 0;
}

void RbxNames::load() {
    loaded = true;
    std::string all;
    if (file.empty() || !readFile(file, all)) return;
    for (size_t p = 0; p < all.size();) {
        size_t e = all.find('\n', p);
        if (e == npos) e = all.size();
        std::string ln = all.substr(p, e - p);
        p = e + 1;
        if (!ln.empty() && ln.back() == '\r') ln.pop_back();
        size_t a = ln.find('\t'), b = a == npos ? a : ln.find('\t', a + 1), c = b == npos ? b : ln.find('\t', b + 1);
        if (c == npos) continue;
        RbxName n;
        n.at = atof(ln.substr(a + 1, b - a - 1).c_str());
        n.genre = ln.substr(b + 1, c - b - 1);
        n.name = ln.substr(c + 1);
        long long u = _atoi64(ln.substr(0, a).c_str());
        if (u > 0 && !n.name.empty()) m[u] = n;
    }
}

void RbxNames::save() const {
    if (file.empty()) return;
    std::vector<std::pair<long long, RbxName>> v(m.begin(), m.end());
    std::sort(v.begin(), v.end(), [](auto& x, auto& y) { return x.second.at > y.second.at; });
    if (v.size() > 300) v.resize(300);
    std::string s;
    auto clean = [](std::string t) {
        for (auto& ch : t)
            if (ch == '\t' || ch == '\r' || ch == '\n') ch = ' ';
        return t;
    };
    char b[64];
    for (auto& [u, n] : v) {
        snprintf(b, sizeof(b), "%lld\t%.0f\t", u, n.at);
        s += b + clean(n.genre) + "\t" + clean(n.name) + "\r\n";
    }
    writeFile(file, s);
}

bool RbxNames::get(long long u, RbxName& out, double now) {
    if (!loaded) load();
    auto it = m.find(u);
    if (it != m.end() && now - it->second.at < 7 * 86400) {
        out = it->second;
        return true;
    }
    RbxName n;
    if (fetch && fetch(u, n)) {
        n.at = now;
        m[u] = n;
        save();
        out = n;
        return true;
    }
    if (it != m.end()) {
        out = it->second;
        return true;
    }
    return false;
}

static std::wstring cleanName(const std::string& name) {
    std::wstring w = widen(name);
    auto pass = [&](bool tags) {
        std::wstring o;
        int depth = 0;
        for (size_t i = 0; i < w.size(); i++) {
            wchar_t c = w[i];
            if (c >= 0xD800 && c <= 0xDBFF) { i++; continue; }
            if (c >= 0xDC00 && c <= 0xDFFF) continue;
            if ((c >= 0x2190 && c <= 0x2BFF) || (c >= 0xFE00 && c <= 0xFE0F) || (c >= 0x200B && c <= 0x200F) || (c >= 0x2060 && c <= 0x206F) ||
                (c >= 0xE000 && c <= 0xF8FF) || c == 0x20E3)
                continue;
            if (c < 32 || wcschr(L"|^~{}%", c)) { o += L' '; continue; }
            bool open = c == L'[' || c == L'(' || c == 0x3010, close = c == L']' || c == L')' || c == 0x3011;
            if (open || close) {
                if (tags) depth = open ? depth + 1 : std::max(0, depth - 1);
                o += L' ';
                continue;
            }
            if (!depth) o += c;
        }
        std::wstring r;
        for (wchar_t c : o)
            if (c != L' ' || (!r.empty() && r.back() != L' ')) r += c;
        const wchar_t* junk = L" -\x2013\x2014:|\x2022\x00B7,!.*_+=";
        while (!r.empty() && wcschr(junk, r.back())) r.pop_back();
        size_t f = 0;
        while (f < r.size() && wcschr(junk, r[f])) f++;
        return r.substr(f);
    };
    std::wstring r = pass(true);
    return r.empty() ? pass(false) : r;
}

std::wstring rbxShortName(const std::string& name) {
    std::wstring s = cleanName(name);
    if (s.size() <= 28) return s;
    size_t cut = s.rfind(L' ', 28);
    if (cut == std::wstring::npos || cut < 12) cut = 26;
    s = s.substr(0, cut);
    while (!s.empty() && (s.back() == L' ' || s.back() == L',' || s.back() == L'-' || s.back() == L':')) s.pop_back();
    return s + L"\x2026";
}

bool rbxPlaceholder(const std::string& name) {
    std::wstring low = cleanName(name);
    CharLowerBuffW(low.data(), (DWORD)low.size());
    for (auto w : {L"join the group", L"join group", L"join our group", L"join my group", L"'s place", L"\x2019s place", L"untitled", L"place number"})
        if (low.find(w) != std::wstring::npos) return true;
    return false;
}

static bool hasWord(const std::wstring& low, const wchar_t* kw) {
    if (kw[0] == L'^') {
        std::wstring k = kw + 1;
        if (low.compare(0, k.size(), k) != 0) return false;
        size_t q = k.size();
        if (q < low.size() && iswalnum(low[q])) return false;
        while (q < low.size() && low[q] == L' ') q++;
        return q >= low.size() || !iswalpha(low[q]);
    }
    std::wstring k = kw;
    for (size_t p = 0; (p = low.find(k, p)) != std::wstring::npos; p++) {
        bool l = p == 0 || !iswalnum(low[p - 1]), r = p + k.size() >= low.size() || !iswalnum(low[p + k.size()]);
        if (l && r) return true;
    }
    return false;
}

const char* rbxKind(const std::string& name, const std::string& genre) {
    struct Kw { const char* cat; std::vector<const wchar_t*> w; };
    static const Kw games[] = {
        {"rbxHorror", {L"^doors", L"^pressure", L"^piggy", L"^forsaken", L"^evade", L"99 nights", L"dead rails", L"the mimic", L"apeirophobia"}},
        {"rbxCozy", {L"brookhaven", L"adopt me", L"dress to impress", L"grow a garden"}},
        {"rbxFight", {L"^rivals", L"^arsenal", L"murder mystery", L"mm2", L"strongest battlegrounds", L"bedwars"}},
        {"rbxObby", {L"tower of hell"}},
        {"rbxGrind", {L"blox fruits", L"steal a brainrot", L"pet simulator"}},
        {"rbxDisaster", {L"natural disaster"}},
    };
    static const Kw words[] = {
        {"rbxObby", {L"obby", L"parkour"}},
        {"rbxHorror", {L"horror"}},
        {"rbxGrind", {L"simulator", L"tycoon", L"incremental", L"clicker"}},
    };
    static const Kw genres[] = {
        {"rbxHorror", {L"horror", L"1 vs all", L"escape"}},
        {"rbxObby", {L"obby", L"platformer"}},
        {"rbxFight", {L"shooter", L"fps", L"fighting", L"battlegrounds"}},
        {"rbxCozy", {L"roleplay", L"town and city", L"dress up"}},
        {"rbxGrind", {L"simulation", L"tycoon", L"incremental"}},
    };
    std::wstring low = cleanName(name), lg = widen(genre);
    CharLowerBuffW(low.data(), (DWORD)low.size());
    CharLowerBuffW(lg.data(), (DWORD)lg.size());
    for (auto& g : games)
        for (auto w : g.w)
            if (hasWord(low, w)) return g.cat;
    for (auto& g : words)
        for (auto w : g.w)
            if (hasWord(low, w)) return g.cat;
    for (auto& g : genres)
        for (auto w : g.w)
            if (lg.find(w) != std::wstring::npos) return g.cat;
    return "rbxJoin";
}

bool RbxVoice::pick(const RbxEvent& e, double now, float roll, CsLine& out) {
    out = CsLine{};
    if (e.join) {
        std::wstring arg = rbxShortName(e.name);
        if (arg.empty()) return false;
        if (rbxPlaceholder(e.name)) arg.clear();
        std::string cat = e.again ? "rbxAgain" : rbxKind(e.name, e.genre);
        float prob = now - lastJoinLine < 120 ? 0.35f : 1.f;
        if (roll >= prob) return false;
        out.cat = cat;
        out.arg = arg;
        out.prio = 6;
        out.until = now + 60;
        if (cat == "rbxHorror") { out.face = CsFaceSurprise; out.faceT = 3.2f; }
        else if (cat == "rbxFight" || cat == "rbxDisaster") { out.face = CsFaceHappy; out.faceT = 1.6f; }
        else if (cat == "rbxCozy") { out.face = CsFaceAngry; out.faceT = 0.7f; }
        else if (cat == "rbxObby") { out.face = CsFaceHappy; out.faceT = 1.f; }
        lastJoinLine = now;
        return true;
    }
    if (e.switched || e.played < 20 * 60 || roll >= 0.5f) return false;
    out.cat = "rbxLeave";
    out.prio = 5;
    out.until = now + 30;
    return true;
}

static double epochNow() {
    FILETIME f;
    GetSystemTimeAsFileTime(&f);
    ULARGE_INTEGER u;
    u.LowPart = f.dwLowDateTime;
    u.HighPart = f.dwHighDateTime;
    return (double)(u.QuadPart - 116444736000000000ULL) / 1e7;
}

bool RobloxSense::start(const std::wstring& logsDir, const std::wstring& cacheFile) {
    if (th.joinable()) return true;
    tail = RbxLogTail{};
    tail.dir = logsDir;
    sense = RbxSense{};
    names.file = cacheFile;
    quit = false;
    th = std::thread([this] { loop(); });
    return true;
}

void RobloxSense::stop() {
    if (!th.joinable()) return;
    quit = true;
    th.join();
}

std::vector<RbxEvent> RobloxSense::take() {
    std::lock_guard<std::mutex> g(mu);
    std::vector<RbxEvent> v(q.begin(), q.end());
    q.clear();
    return v;
}

void RobloxSense::loop() {
    std::vector<std::pair<std::string, bool>> lines;
    bool first = true;
    double next = 0;
    while (!quit) {
        if (nowSec() < next) {
            Sleep(100);
            continue;
        }
        next = nowSec() + 3;
        lines.clear();
        tail.poll(lines);
        std::vector<RbxEvent> evs;
        for (auto& [ln, quiet] : lines) {
            RbxLog l = rbxParseLine(ln);
            if (l.k == RbxLog::None) continue;
            auto v = sense.feed(l);
            if (!quiet) evs.insert(evs.end(), v.begin(), v.end());
        }
        double t = epochNow();
        bool running = !sense.inGame() || robloxRunning();
        if (first) {
            first = false;
            if (!running) sense.forget();
            logf("rbx: watching %d player logs in '%s'%s", (int)tail.watching().size(), narrow(tail.dir).c_str(),
                 sense.inGame() ? ", already in a game" : "");
            continue;
        }
        for (auto& e : sense.flush(t)) evs.push_back(e);
        if (!running)
            for (auto& e : sense.gone(t)) evs.push_back(e);
        for (auto& e : evs) {
            if (quit) break;
            if (e.join) {
                if (!e.universe && e.place) e.universe = rbxFetchUniverse(e.place);
                RbxName n;
                if (e.universe && names.get(e.universe, n, t)) {
                    e.name = n.name;
                    e.genre = n.genre;
                }
            }
            std::lock_guard<std::mutex> g(mu);
            q.push_back(e);
            while (q.size() > 16) q.pop_front();
        }
    }
}
