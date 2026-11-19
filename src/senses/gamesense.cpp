#include <winsock2.h>
#include <ws2tcpip.h>
#include "gamesense.h"

static std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

int gsiParse(const std::string& buf, GsiReq& r) {
    r = GsiReq{};
    size_t he = buf.find("\r\n\r\n");
    if (he == std::string::npos) return buf.size() > 16384 ? -1 : 0;
    size_t le = buf.find("\r\n");
    std::string line = buf.substr(0, le);
    size_t s1 = line.find(' '), s2 = s1 == std::string::npos ? s1 : line.find(' ', s1 + 1);
    if (s1 == std::string::npos || s2 == std::string::npos || line.compare(s2 + 1, 5, "HTTP/") != 0) return -1;
    r.method = line.substr(0, s1);
    r.path = line.substr(s1 + 1, s2 - s1 - 1);
    size_t cl = 0;
    for (size_t p = le + 2; p < he + 2;) {
        size_t e = buf.find("\r\n", p);
        std::string h = buf.substr(p, e - p);
        p = e + 2;
        size_t c = h.find(':');
        if (c == std::string::npos) return -1;
        std::string k = lower(h.substr(0, c)), v = h.substr(c + 1);
        while (!v.empty() && (v[0] == ' ' || v[0] == '\t')) v.erase(0, 1);
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
        if (k == "content-length") {
            if (v.empty() || v.size() > 12 || v.find_first_not_of("0123456789") != std::string::npos) return -1;
            cl = (size_t)_strtoui64(v.c_str(), nullptr, 10);
        } else if (k == "transfer-encoding") {
            return -1;
        } else if (k == "origin") {
            r.origin = true;
        }
    }
    if (cl > kGsiBodyCap) return -2;
    if (buf.size() < he + 4 + cl) return 0;
    r.body = buf.substr(he + 4, cl);
    r.used = he + 4 + cl;
    return 1;
}

int gsiAccept(const GsiReq& r, const std::vector<std::string>& tokens, Json& out) {
    if (r.origin) return 403;
    if (r.method != "POST") return 405;
    if (r.path != "/gsi" && r.path != "/") return 404;
    bool ok = false;
    out = Json::parse(r.body, &ok);
    if (!ok || out.t != Json::Obj) return 400;
    std::string tk = out["auth"]["token"].str();
    for (auto& t : tokens)
        if (!tk.empty() && tk == t) return 200;
    return 403;
}

std::string gsiCfg(const std::string& token) {
    static const char* data[] = {"provider", "map", "map_round_wins", "round", "player_id", "player_state", "player_weapons", "player_match_stats",
                                 "bomb", "phase_countdowns"};
    std::string s = "\"UziPet\"\r\n{\r\n";
    s += "\t\"uri\"\t\t\"http://127.0.0.1:" + std::to_string(GameSense::kPort) + "/gsi\"\r\n";
    s += "\t\"timeout\"\t\"5.0\"\r\n\t\"buffer\"\t\"0.1\"\r\n\t\"throttle\"\t\"0.3\"\r\n\t\"heartbeat\"\t\"15.0\"\r\n";
    s += "\t\"auth\"\r\n\t{\r\n\t\t\"token\"\t\"" + token + "\"\r\n\t}\r\n";
    s += "\t\"data\"\r\n\t{\r\n";
    for (auto d : data) s += std::string("\t\t\"") + d + "\"\t\"1\"\r\n";
    s += "\t}\r\n}\r\n";
    return s;
}

std::string gsiCfgToken(const std::string& text) {
    size_t p = text.find("\"token\"");
    if (p == std::string::npos) return "";
    size_t a = text.find('"', p + 7);
    if (a == std::string::npos) return "";
    size_t b = text.find('"', a + 1);
    if (b == std::string::npos) return "";
    return text.substr(a + 1, b - a - 1);
}

static bool isDir(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

std::vector<std::wstring> cs2CfgDirs() {
    std::vector<std::wstring> libs, out;
    wchar_t buf[MAX_PATH * 2];
    DWORD sz = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, buf, &sz) != ERROR_SUCCESS) return out;
    std::wstring steam = buf;
    libs.push_back(steam);
    std::string vdf;
    if (readFile(steam + L"\\steamapps\\libraryfolders.vdf", vdf)) {
        for (size_t p = 0; (p = vdf.find("\"path\"", p)) != std::string::npos;) {
            size_t a = vdf.find('"', p + 6), b = a == std::string::npos ? a : vdf.find('"', a + 1);
            if (b == std::string::npos) break;
            std::string v = vdf.substr(a + 1, b - a - 1);
            std::string u;
            for (size_t i = 0; i < v.size(); i++) {
                if (v[i] == '\\' && i + 1 < v.size() && v[i + 1] == '\\') i++;
                u += v[i];
            }
            libs.push_back(widen(u));
            p = b + 1;
        }
    }
    std::vector<std::wstring> seen;
    for (auto l : libs) {
        for (auto& c : l)
            if (c == L'/') c = L'\\';
        while (!l.empty() && l.back() == L'\\') l.pop_back();
        std::wstring key = l;
        CharLowerBuffW(key.data(), (DWORD)key.size());
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
        seen.push_back(key);
        std::wstring d = l + L"\\steamapps\\common\\Counter-Strike Global Offensive\\game\\csgo\\cfg";
        if (isDir(d)) out.push_back(d);
    }
    return out;
}

bool gsiInstall(const std::wstring& dir, const std::string& token, std::string* old) {
    std::wstring f = dir + L"\\" + kGsiFile;
    std::string want = gsiCfg(token), have;
    bool had = readFile(f, have);
    if (old) *old = had ? gsiCfgToken(have) : "";
    if (had && have == want) return true;
    if (!writeFile(f, want)) return false;
    std::string back;
    return readFile(f, back) && back == want;
}

bool gsiRemove(const std::wstring& dir) {
    std::wstring f = dir + L"\\" + kGsiFile;
    if (GetFileAttributesW(f.c_str()) == INVALID_FILE_ATTRIBUTES) return true;
    return DeleteFileW(f.c_str()) != 0;
}

std::string gsiNewToken() {
    std::random_device rd;
    char b[40];
    snprintf(b, sizeof(b), "%08x%08x", rd(), rd());
    return b;
}

const char* csEvName(CsEv e) {
    static const char* n[] = {"enter", "mapChange", "kill", "death", "ace", "dmStreak", "roundWin", "roundLose", "loseStreak",
                              "bombPlanted", "bombDefused", "bombExploded", "flashed", "matchEnd",
                              "lastSecondDefuse", "knifeDefuse", "clutchDefuse", "acedButBombExploded", "survivedBlast", "missedDefuse", "nearAce",
                              "pickup", "mvp", "smokeKill", "teamKill", "knifeKill", "pistolRound", "youPlanted", "matchPoint",
                              "armsLevel", "armsPistols", "armsKnifeLevel", "armsLevelLost", "armsKnifeKill"};
    return n[(int)e];
}

std::string csDescribe(const CsEvent& e) {
    char b[160];
    switch (e.type) {
    case CsEv::Kill: snprintf(b, sizeof(b), "kill x%d%s", e.n, e.hs ? " hs" : ""); break;
    case CsEv::Ace: case CsEv::DmStreak: case CsEv::LoseStreak: case CsEv::BlastWasted: case CsEv::PistolKills: case CsEv::Mvp:
        snprintf(b, sizeof(b), "%s %d", csEvName(e.type), e.n);
        break;
    case CsEv::NearAce: snprintf(b, sizeof(b), "nearAce %d, hp %d", e.n, e.hp); break;
    case CsEv::LastDefuse: snprintf(b, sizeof(b), "%s %.1f s", e.n ? "fastDefuse" : "lastSecondDefuse", e.sec); break;
    case CsEv::YouPlanted: snprintf(b, sizeof(b), "%s %.1f s", e.late ? "lastSecondPlant" : "youPlanted", e.sec); break;
    case CsEv::Pickup: snprintf(b, sizeof(b), "pickup %s%s", e.gun.c_str(), e.n == 1 ? " (enemy)" : e.n == 2 ? " (awp)" : ""); break;
    case CsEv::MatchPoint: snprintf(b, sizeof(b), "matchPoint %s %d:%d", e.n > 0 ? "ours" : e.n < 0 ? "theirs" : "both", e.mine, e.theirs); break;
    case CsEv::ArmsUp: snprintf(b, sizeof(b), "armsLevel %d %s", e.n, e.gun.c_str()); break;
    case CsEv::ArmsPistols: snprintf(b, sizeof(b), "armsPistols %s", e.gun.c_str()); break;
    case CsEv::ArmsLost: snprintf(b, sizeof(b), "armsLevelLost -%d%s %s", e.n, e.knife ? " knifed" : "", e.gun.c_str()); break;
    case CsEv::Death: snprintf(b, sizeof(b), "death%s", e.late ? " late" : ""); break;
    case CsEv::MatchEnd:
        if (e.arms)
            snprintf(b, sizeof(b), "matchEnd %s (arms), %d/%d", e.n > 0 ? "won" : e.n == 0 ? "almost" : "far", e.kills, e.deaths);
        else if (e.full)
            snprintf(b, sizeof(b), "matchEnd %s %d:%d, %d/%d, mvp %d", e.n > 0 ? "win" : e.n < 0 ? "lose" : "draw", e.mine, e.theirs, e.kills, e.deaths, e.mvps);
        else
            snprintf(b, sizeof(b), "matchEnd %s%s, %d/%d", e.n > 0 ? "win" : e.n < 0 ? "lose" : "draw", e.dm ? " (dm)" : "", e.kills, e.deaths);
        break;
    case CsEv::Enter: case CsEv::MapChange: snprintf(b, sizeof(b), "%s %s", csEvName(e.type), e.map.c_str()); break;
    case CsEv::BombPlanted: case CsEv::BombDefused: case CsEv::BombExploded: snprintf(b, sizeof(b), "%s%s", csEvName(e.type), e.ours ? " ours" : ""); break;
    default: snprintf(b, sizeof(b), "%s", csEvName(e.type));
    }
    return b;
}

std::wstring csMapName(const std::string& raw) {
    static const char* known[][2] = {{"de_dust2", "Dust2"},     {"de_mirage", "Mirage"},     {"de_inferno", "Inferno"}, {"de_nuke", "Nuke"},
                                     {"de_ancient", "Ancient"}, {"de_anubis", "Anubis"},     {"de_vertigo", "Vertigo"}, {"de_overpass", "Overpass"},
                                     {"de_train", "Train"},     {"cs_office", "Office"},     {"cs_italy", "Italy"}};
    std::string s = raw.substr(raw.find_last_of('/') == std::string::npos ? 0 : raw.find_last_of('/') + 1);
    for (auto& k : known)
        if (s == k[0]) return widen(k[1]);
    size_t u = s.find('_');
    if (u != std::string::npos && u <= 3) s = s.substr(u + 1);
    std::string o;
    for (char c : s) {
        if ((unsigned char)c < 32 || strchr("{}|^~%", c)) continue;
        o += c == '_' ? ' ' : c;
    }
    while (!o.empty() && o.back() == ' ') o.pop_back();
    while (!o.empty() && o[0] == ' ') o.erase(0, 1);
    if (o.size() > 32) o.resize(32);
    if (!o.empty()) o[0] = (char)toupper((unsigned char)o[0]);
    return widen(o);
}

std::wstring csGunName(const std::string& raw) {
    static const char* known[][2] = {
        {"ak47", "АК"},          {"awp", "AWP"},           {"m4a1", "M4"},         {"m4a1_silencer", "M4 с глушителем"}, {"famas", "Фамас"},
        {"galilar", "Галиль"},   {"aug", "AUG"},           {"sg556", "Сига"},      {"negev", "Негев"},                  {"m249", "M249"},
        {"mp7", "MP7"},          {"mp9", "MP9"},           {"mp5sd", "MP5"},       {"p90", "P90"},                      {"bizon", "Бизон"},
        {"ump45", "UMP"},        {"mac10", "МАК-10"},      {"deagle", "Дигл"},     {"glock", "Глок"},                   {"usp_silencer", "USP"},
        {"hkp2000", "P2000"},    {"p250", "P250"},         {"fiveseven", "Файв-севен"}, {"tec9", "Тек-9"},              {"cz75a", "CZ"},
        {"revolver", "Револьвер"}, {"elite", "Беретты"},   {"scar20", "SCAR"},     {"g3sg1", "G3SG1"},                  {"ssg08", "Муха"},
        {"nova", "Нова"},        {"xm1014", "XM"},         {"mag7", "MAG-7"},      {"sawedoff", "Обрез"},               {"knife", "Нож"}};
    std::string s = raw.rfind("weapon_", 0) == 0 ? raw.substr(7) : raw;
    for (auto& k : known)
        if (s == k[0]) return widen(k[1]);
    std::string o;
    for (char c : s)
        if ((unsigned char)c >= 32 && !strchr("{}|^~%*", c)) o += c == '_' ? ' ' : c;
    if (o.size() > 20) o.resize(20);
    return widen(o);
}

static bool csIsGun(const std::string& type) {
    return type == "Rifle" || type == "SniperRifle" || type == "Submachine Gun" || type == "Shotgun" || type == "Machine Gun" || type == "Pistol";
}

static bool csEnemyGun(const std::string& gun, const std::string& team) {
    static const char* tOnly[] = {"weapon_ak47", "weapon_galilar", "weapon_sg556", "weapon_g3sg1"};
    static const char* ctOnly[] = {"weapon_m4a1", "weapon_m4a1_silencer", "weapon_famas", "weapon_aug", "weapon_scar20"};
    if (team == "CT")
        for (auto g : tOnly)
            if (gun == g) return true;
    if (team == "T")
        for (auto g : ctOnly)
            if (gun == g) return true;
    return false;
}

void CsSense::resetArms() {
    ladder.clear();
    level = -1;
    armsPistol = armsKnife = false;
    dropLast = 0;
}

std::vector<CsEvent> CsSense::feed(const Json& d, double t) {
    std::vector<CsEvent> out;
    auto ev = [&](CsEv type) -> CsEvent& {
        out.push_back(CsEvent{});
        out.back().type = type;
        out.back().t = t;
        out.back().dm = dm;
        out.back().arms = arms;
        out.back().full = full;
        out.back().map = mapRaw;
        return out.back();
    };
    const Json& map = d["map"];
    const Json& pl = d["player"];
    const Json& rd = d["round"];
    if (map.t == Json::Obj) {
        std::string name = map["name"].str();
        std::string mode = map["mode"].str();
        dm = mode == "deathmatch" || mode == "gungameprogressive";
        arms = mode == "gungameprogressive";
        full = mode == "competitive" || mode == "premier";
        if (!inMap || name != mapRaw) {
            bool after = !gameoverMap.empty() && t - gameoverAt < 180;
            std::string was = gameoverMap;
            inMap = true;
            mapRaw = name;
            haveMe = false;
            loseRow = 0;
            mapPhase = roundPhase = bomb = "";
            gameoverMap.clear();
            resetArms();
            roundNo = mpCt = mpT = -1;
            hadC4 = defWait = blastWait = false;
            if (!after) ev(CsEv::Enter);
            else if (name != was) ev(CsEv::MapChange);
        }
    } else if (inMap && d.has("provider")) {
        inMap = false;
    }
    bool warm = map["phase"].str() == "warmup";
    bool live = inMap && !warm && map["phase"].str() != "gameover";
    std::string rp = rd["phase"].str();

    std::string prov = d["provider"]["steamid"].str();
    bool me = pl.t == Json::Obj && !prov.empty() && pl["steamid"].str() == prov;
    if (me && !pl["team"].str().empty()) myTeam = pl["team"].str();
    const Json& stt = pl["state"];
    const Json& ms = pl["match_stats"];
    bool was = prevOwn;
    prevOwn = false;
    int hpWas = hp;
    if (me && stt.t == Json::Obj && ms.t == Json::Obj) {
        int k = ms["kills"].i(), dth = ms["deaths"].i(), rk = stt["round_kills"].i(), rhs = stt["round_killhs"].i();
        int h = stt["health"].i(100), fl = stt["flashed"].i();
        int cash = stt["money"].i(), sc = ms["score"].i(), mv = ms["mvps"].i(), arm = stt["armor"].i(), sm = stt["smoked"].i();
        std::vector<std::string> now, guns;
        std::string held, gunType;
        bool c4 = false, knifeIn = false;
        const Json& ws = pl["weapons"];
        if (ws.t == Json::Obj)
            for (auto& w : ws.a) {
                std::string nm = w["name"].str(), ty = w["type"].str();
                now.push_back(nm);
                if (csIsGun(ty)) {
                    if (guns.empty()) gunType = ty;
                    guns.push_back(nm);
                }
                if (w["state"].str() == "active") held = ty;
                if (nm == "weapon_c4") c4 = true;
                if (ty == "Knife") knifeIn = true;
            }
        if (k < kills || dth < deaths) resetArms();
        if (haveMe && live) {
            int dk = k - kills;
            if (dk > 0) {
                CsEvent& e = ev(CsEv::Kill);
                e.n = rk > 0 ? rk : dk;
                e.hs = rk >= roundKills ? rhs > roundHs : rhs > 0;
                e.kills = k;
                e.deaths = dth;
                if (full && rk >= 5 && roundKills < 5) ev(CsEv::Ace).n = rk;
                if (dm && rk >= 5 && rk / 5 > roundKills / 5) ev(CsEv::DmStreak).n = rk;
                bool knifeKill = held == "Knife" && heldWas == "Knife" && was && (!full || cash - money >= 1500 || cash >= 16000);
                if (knifeKill && arms) {
                    if (!armsKnife) ev(CsEv::ArmsKnifeKill);
                } else if (knifeKill) {
                    ev(CsEv::KnifeKill).n = rk;
                    roundKnife = true;
                }
                if (!arms && (std::max)(sm, smoked) >= 150) ev(CsEv::SmokeKill);
                if (full && (roundNo == 0 || roundNo == 12) && held == "Pistol" && rk >= 3 && rk < 5) ev(CsEv::PistolKills).n = rk;
                if (full && rk == 4 && roundKills < 4 && h > 0 && h <= 10) {
                    CsEvent& na = ev(CsEv::NearAce);
                    na.n = rk;
                    na.hp = h;
                }
            }
            bool lateLvl = arms && (armsPistol || armsKnife);
            if (dth > reported) {
                CsEvent& e = ev(CsEv::Death);
                e.deaths = dth;
                e.late = lateLvl;
                reported = dth;
            } else if (hp > 0 && h == 0) {
                CsEvent& e = ev(CsEv::Death);
                e.deaths = dth + 1;
                e.late = lateLvl;
                reported = dth + 1;
            }
            if (fl >= 200 && flashed < 200 && h > 0 && t - lastFlash > 8) {
                ev(CsEv::Flashed);
                lastFlash = t;
            }
            if (h > 0 && h < hp) hurt = true;
            if (!dm && rp == "live" && was && hp > 0 && h > 0 && !items.empty() && cash == money) {
                std::vector<std::string> fresh;
                for (auto& g : guns)
                    if (std::find(items.begin(), items.end(), g) == items.end()) fresh.push_back(g);
                if (fresh.size() == 1) {
                    CsEvent& e = ev(CsEv::Pickup);
                    e.gun = fresh[0];
                    e.n = fresh[0] == "weapon_awp" ? 2 : csEnemyGun(fresh[0], myTeam) ? 1 : 0;
                }
            }
            if (!dm && rp == "live" && was && sc < score && k == kills && dth == deaths && cash < money && arm <= armor) {
                bool bought = false;
                for (auto& n : now)
                    if (std::find(items.begin(), items.end(), n) == items.end()) bought = true;
                if (!bought) ev(CsEv::TeamKill);
            }
            if (hadC4 && !c4 && h > 0 && !now.empty()) c4GoneAt = t;
            if (mv > mvps) {
                mvpAt = t;
                ev(CsEv::Mvp).n = mv;
            }
            if (arms && sc < score && k == kills) {
                dropLast = score - sc;
                dropAt = t;
            }
        }
        std::string lv = !arms || !live || h <= 0 ? "" : !guns.empty() ? guns[0] : knifeIn && ladder.size() >= 4 ? "weapon_knife" : "";
        if (!lv.empty()) {
            auto it = std::find(ladder.begin(), ladder.end(), lv);
            int idx = (int)(it - ladder.begin());
            if (it == ladder.end()) ladder.push_back(lv);
            bool pistol = !guns.empty() && gunType == "Pistol";
            if (level >= 0 && idx > level) {
                if (lv == "weapon_knife") ev(CsEv::ArmsKnife);
                else if (pistol && !armsPistol) ev(CsEv::ArmsPistols).gun = lv;
                else {
                    CsEvent& e = ev(CsEv::ArmsUp);
                    e.n = idx + 1;
                    e.gun = lv;
                }
            } else if (level >= 0 && idx < level) {
                CsEvent& e = ev(CsEv::ArmsLost);
                e.n = level - idx;
                e.gun = lv;
                e.knife = dropLast >= 2 && t - dropAt < 20;
                dropLast = 0;
            }
            level = idx;
            if (pistol) armsPistol = true;
            armsKnife = lv == "weapon_knife";
        }
        if (k > kills) lastKillAt = t;
        if (!haveMe || !live || dth < deaths) reported = dth;
        kills = k;
        deaths = dth;
        roundKills = rk;
        roundHs = rhs;
        hp = h;
        flashed = fl;
        money = cash;
        score = sc;
        mvps = mv;
        armor = arm;
        smoked = sm;
        items = now;
        heldWas = held;
        hadC4 = c4;
        haveMe = prevOwn = true;
    }
    if (arms && warm) resetArms();

    if (rd.t == Json::Obj) {
        std::string win = rd["win_team"].str();
        if (rp == "freezetime" && roundPhase != "freezetime") {
            hurt = roundKnife = false;
            plantAt = liveAt = c4GoneAt = -1e9;
            rkAtPlant = 0;
            if (full && live && !myTeam.empty()) {
                int ct = map["team_ct"]["score"].i(), tt = map["team_t"]["score"].i();
                if ((std::max)(ct, tt) == 12 && (ct != mpCt || tt != mpT)) {
                    mpCt = ct;
                    mpT = tt;
                    CsEvent& e = ev(CsEv::MatchPoint);
                    e.mine = myTeam == "CT" ? ct : tt;
                    e.theirs = myTeam == "CT" ? tt : ct;
                    e.n = ct == tt ? 0 : e.mine == 12 ? 1 : -1;
                }
            }
        }
        if (rp == "live" && roundPhase == "freezetime") liveAt = t;
        if (rp != "over" && map.has("round")) roundNo = map["round"].i();
        if (!dm && live && rp == "over" && roundPhase != "over" && !win.empty() && !myTeam.empty()) {
            bool won = win == myTeam;
            ev(won ? CsEv::RoundWin : CsEv::RoundLose);
            loseRow = won ? 0 : loseRow + 1;
            if (loseRow >= 3 && (loseRow - 3) % 2 == 0) ev(CsEv::LoseStreak).n = loseRow;
        }
        std::string b = rd["bomb"].str();
        if (b.empty()) {
            std::string bs = d["bomb"]["state"].str();
            if (bs == "planted" || bs == "defused" || bs == "exploded") b = bs;
        }
        if (b != bomb && live && !dm) {
            bool tt = myTeam == "T", ct = myTeam == "CT";
            if (b == "planted") {
                ev(CsEv::BombPlanted).ours = tt;
                plantAt = t;
                rkAtPlant = roundKills;
                if (tt && t - c4GoneAt <= 3) {
                    CsEvent& e = ev(CsEv::YouPlanted);
                    e.sec = liveAt > -1e8 ? t - liveAt : 0;
                    e.late = e.sec >= 112;
                }
            }
            if (b == "defused") {
                ev(CsEv::BombDefused).ours = ct;
                if (ct && me && hp > 0) {
                    defWait = true;
                    defAt = t;
                    defLen = plantAt > -1e8 ? t - plantAt : 0;
                }
            }
            if (b == "exploded") {
                ev(CsEv::BombExploded).ours = tt;
                if (me && hpWas > 0) {
                    if (ct && roundKills >= 3 && roundKills > rkAtPlant) ev(CsEv::BlastWasted).n = roundKills;
                    blastWait = true;
                    blastAt = t;
                    blastHp = hpWas;
                }
            }
        }
        if (blastWait && me && haveMe) {
            if (hp == 0) {
                if (myTeam == "CT") ev(CsEv::MissedDefuse);
                blastWait = false;
            } else if (hp < blastHp) {
                ev(CsEv::SurvivedBlast);
                blastWait = false;
            }
        }
        if (blastWait && t - blastAt > 2.5) blastWait = false;
        bomb = b;
        roundPhase = rp;
    }
    if (defWait && mvpAt >= defAt - 0.01 && mvpAt <= defAt + 3) {
        defWait = false;
        if (defLen >= 39.0) {
            CsEvent& e = ev(CsEv::LastDefuse);
            e.sec = defLen;
            e.n = defLen >= 39.5;
        } else if (roundKnife) {
            ev(CsEv::KnifeDefuse);
        } else if (hurt && roundKills > 0) {
            ev(CsEv::ClutchDefuse);
        }
    }
    if (defWait && t - defAt > 3) defWait = false;

    if (inMap) {
        std::string ph = map["phase"].str();
        if (ph == "gameover" && mapPhase != "gameover" && !mapPhase.empty()) {
            CsEvent& e = ev(CsEv::MatchEnd);
            e.kills = kills;
            e.deaths = deaths;
            e.mvps = mvps;
            std::string win = rd["win_team"].str();
            if (arms) {
                bool mine = armsKnife && t - lastKillAt <= 4 && (win.empty() || win == myTeam);
                e.n = mine ? 1 : armsPistol || armsKnife ? 0 : -1;
            } else if (dm) {
                e.n = win.empty() || myTeam.empty() ? 0 : win == myTeam ? 1 : -1;
            } else {
                int ct = map["team_ct"]["score"].i(), tt = map["team_t"]["score"].i();
                int mine = myTeam == "CT" ? ct : tt, theirs = myTeam == "CT" ? tt : ct;
                e.n = myTeam.empty() ? 0 : mine > theirs ? 1 : mine < theirs ? -1 : 0;
                e.mine = mine;
                e.theirs = theirs;
            }
            gameoverAt = t;
            gameoverMap = mapRaw;
        }
        if (!ph.empty()) mapPhase = ph;
    }
    return out;
}

void CsVoice::push(const CsEvent& e, int tier, double now, int& face, float& faceT) {
    face = CsFaceNone;
    faceT = 0;
    std::string cat;
    std::wstring arg;
    int prio = 0;
    float prob = 1.f;
    double ttl = 8;
    auto look = [&](int f, float tt) { face = f; faceT = tt; };
    tier = std::clamp(tier, 0, 3);
    switch (e.type) {
    case CsEv::Enter:
        if (now - lastEnter > 20 * 60) cat = "csEnter";
        prio = 6;
        ttl = 45;
        break;
    case CsEv::MapChange:
        cat = "csMapChange";
        arg = csMapName(e.map);
        prio = 6;
        ttl = 45;
        look(CsFaceSurprise, 0.8f);
        break;
    case CsEv::Kill:
        if (e.n >= 5 && !e.dm && !e.full) {
            cat = "csMultikill"; arg = std::to_wstring(e.n); prio = 8; look(CsFaceSurprise, 1.5f);
            break;
        }
        if (e.n >= 5) { look(CsFaceHappy, 2.f); break; }
        if (e.arms) { look(CsFaceHappy, e.n >= 3 ? 1.4f : 1.f); break; }
        if (e.n == 4) { cat = "cs4k"; prio = 8; look(CsFaceSurprise, 1.5f); }
        else if (e.n == 3) { cat = "cs3k"; prio = 7; prob = e.dm ? 0.5f : 0.8f; look(CsFaceHappy, 1.6f); }
        else if (e.hs) { cat = "csHeadshot"; prio = 2; prob = 0.25f; look(CsFaceHappy, 1.2f); }
        else { cat = "csKill"; prio = 1; prob = 0.15f; look(CsFaceHappy, 1.f); }
        break;
    case CsEv::Ace: cat = "csAce"; prio = 10; ttl = 12; look(CsFaceSurprise, 2.5f); break;
    case CsEv::DmStreak: cat = "csDmStreak"; arg = std::to_wstring(e.n); prio = 9; look(CsFaceSurprise, 1.5f); break;
    case CsEv::Death:
        if (tier >= 3) look(CsFaceSad, 1.5f);
        else if (tier == 2) look(CsFaceSad, 0.8f);
        else look(CsFaceHappy, 1.2f);
        deathGap = e.dm ? 180 : 60;
        if (e.late && now - lastDeathLine >= 45) {
            cat = "csArmsDeathLate";
            prio = 4;
            prob = 0.5f;
            break;
        }
        if (now - lastDeathLine >= deathGap) cat = "csDeath";
        prio = 3;
        prob = e.dm ? 0.25f : 0.35f;
        break;
    case CsEv::LastDefuse:
        cat = e.n ? "csFastDefuse" : "csLastDefuse";
        prio = 9;
        ttl = 15;
        look(CsFaceSurprise, 2.5f);
        break;
    case CsEv::KnifeDefuse: cat = "csKnifeDefuse"; prio = 9; ttl = 15; look(CsFaceSurprise, 2.f); break;
    case CsEv::ClutchDefuse: cat = "csClutchDefuse"; prio = 8; ttl = 15; look(CsFaceHappy, 2.f); break;
    case CsEv::BlastWasted: cat = "csBlastWasted"; prio = 8; ttl = 15; look(CsFaceAngry, 1.8f); break;
    case CsEv::SurvivedBlast: cat = "csSurvivedBlast"; prio = 7; ttl = 15; prob = 0.9f; look(CsFaceSurprise, 1.5f); break;
    case CsEv::MissedDefuse:
        cat = "csMissedDefuse";
        prio = 7;
        ttl = 15;
        look(tier == 0 ? CsFaceHappy : CsFaceSad, 1.6f);
        break;
    case CsEv::NearAce: cat = "csNearAce"; prio = 9; look(CsFaceSurprise, 2.f); break;
    case CsEv::Pickup:
        if (e.n == 1 && e.gun == "weapon_ak47") { cat = "csPickupAk"; prio = 6; prob = 0.9f; look(CsFaceHappy, 1.5f); }
        else if (e.n == 2) { cat = "csPickupAwp"; prio = 6; prob = 0.8f; look(CsFaceHappy, 1.4f); }
        else if (e.n == 1) { cat = "csPickupEnemy"; arg = csGunName(e.gun); prio = 5; prob = 0.6f; look(CsFaceHappy, 1.2f); }
        else { cat = "csPickup"; arg = csGunName(e.gun); prio = 2; prob = 0.2f; look(CsFaceHappy, 0.8f); }
        break;
    case CsEv::Mvp:
        cat = "csMvp";
        prio = 4;
        prob = e.n <= 1 ? 0.9f : 0.3f;
        look(CsFaceHappy, 1.5f);
        break;
    case CsEv::SmokeKill: cat = "csSmokeKill"; prio = 7; prob = 0.85f; look(CsFaceSurprise, 1.5f); break;
    case CsEv::TeamKill:
        cat = "csTeamKill";
        prio = 7;
        look(tier == 0 ? CsFaceHappy : tier == 1 ? CsFaceSurprise : CsFaceAngry, 1.6f);
        break;
    case CsEv::KnifeKill: cat = "csKnifeKill"; prio = 8; look(CsFaceSurprise, 2.f); break;
    case CsEv::PistolKills: cat = e.n >= 4 ? "csPistol4" : "csPistol3"; prio = 8; look(CsFaceSurprise, 1.6f); break;
    case CsEv::YouPlanted:
        if (e.late) { cat = "csLastPlant"; prio = 8; look(CsFaceSurprise, 1.8f); break; }
        cat = "csYouPlanted";
        prio = 5;
        prob = 0.4f;
        look(CsFaceHappy, 1.2f);
        break;
    case CsEv::MatchPoint:
        prio = 6;
        ttl = 25;
        if (e.n == 0) { cat = "csMatchPointLast"; look(CsFaceSurprise, 2.f); }
        else if (e.n > 0) { cat = "csMatchPointUs"; look(CsFaceHappy, 1.6f); }
        else { cat = "csMatchPointThem"; look(tier == 0 ? CsFaceHappy : CsFaceSad, 1.6f); }
        break;
    case CsEv::ArmsUp: {
        bool fav = e.gun == "weapon_awp" || e.gun == "weapon_negev" || e.gun == "weapon_deagle";
        look(CsFaceHappy, 1.f);
        if (!fav && now - lastArmsUp < 35) break;
        cat = "csArmsUp";
        arg = csGunName(e.gun);
        prio = fav ? 4 : 3;
        prob = fav ? 0.7f : 0.4f;
        lastArmsUp = now;
        break;
    }
    case CsEv::ArmsPistols: cat = "csArmsPistols"; prio = 6; prob = 0.9f; look(CsFaceHappy, 1.6f); lastArmsUp = now; break;
    case CsEv::ArmsKnife: cat = "csArmsKnife"; prio = 7; look(CsFaceSurprise, 2.f); break;
    case CsEv::ArmsLost:
        if (e.knife) cat = "csArmsLostKnife";
        else cat = "csArmsLost";
        prio = 6;
        prob = 0.8f;
        look(tier == 0 ? CsFaceHappy : CsFaceAngry, 1.5f);
        break;
    case CsEv::ArmsKnifeKill: cat = "csArmsKnifeKill"; prio = 8; look(CsFaceSurprise, 1.8f); break;
    case CsEv::RoundWin: cat = "csRoundWin"; prio = 4; prob = 0.45f; look(CsFaceHappy, 1.5f); break;
    case CsEv::RoundLose: cat = "csRoundLose"; prio = 4; prob = 0.4f; look(tier == 0 ? CsFaceHappy : CsFaceSad, 1.2f); break;
    case CsEv::LoseStreak: cat = "csLoseStreak"; arg = std::to_wstring(e.n); prio = 6; look(tier == 0 ? CsFaceHappy : CsFaceSad, 2.f); break;
    case CsEv::BombPlanted: cat = e.ours ? "csBombPlantedUs" : "csBombPlanted"; prio = 5; prob = 0.5f; look(e.ours ? CsFaceHappy : CsFaceSurprise, 1.2f); break;
    case CsEv::BombDefused:
        if (e.ours) { cat = "csBombDefused"; prio = 5; prob = 0.9f; look(CsFaceHappy, 1.5f); break; }
        cat = "csBombDefusedThem";
        prio = 4;
        prob = 0.5f;
        look(tier == 0 ? CsFaceHappy : CsFaceSad, 1.2f);
        break;
    case CsEv::BombExploded: cat = "csBombExploded"; prio = 5; prob = 0.9f; look(CsFaceSurprise, 1.5f); break;
    case CsEv::Flashed: cat = "csFlashed"; prio = 2; prob = 0.5f; look(CsFaceHappy, 1.f); break;
    case CsEv::MatchEnd:
        prio = 9;
        ttl = 45;
        if (e.arms && e.n >= 0) {
            arg = std::to_wstring(e.kills) + L" на " + std::to_wstring(e.deaths);
            cat = e.n > 0 ? "csArmsWin" : "csArmsAlmost";
            look(e.n > 0 ? CsFaceSurprise : CsFaceSad, 2.5f);
            break;
        }
        if (e.full && e.n != 0 && e.kills >= 15 && e.kills >= 2 * (std::max)(e.deaths, 1)) {
            arg = std::to_wstring(e.mine) + L":" + std::to_wstring(e.theirs) + L". " + std::to_wstring(e.kills) + L" на " + std::to_wstring(e.deaths);
            if (e.mvps >= 3) arg += L", MVP " + std::to_wstring(e.mvps) + L" раз";
            if (e.n > 0) cat = "csMatchCarry";
            else cat = "csMatchLoseCarry";
            look(e.n > 0 ? CsFaceSurprise : CsFaceSad, 3.f);
            break;
        }
        if (e.dm) {
            arg = std::to_wstring(e.kills) + L" на " + std::to_wstring(e.deaths);
            if (e.kills > e.deaths) { cat = "csDmSummary"; look(CsFaceHappy, 2.f); }
            else if (e.kills == e.deaths) { cat = "csDmSummaryEven"; look(CsFaceSurprise, 1.5f); }
            else if (tier == 0) { cat = "csDmSummaryBad"; look(CsFaceHappy, 2.f); }
            else { cat = "csDmSummaryBad"; look(CsFaceSad, 2.f); }
        } else if (e.n > 0) {
            cat = "csMatchWin";
            look(CsFaceHappy, tier == 0 ? 1.2f : 3.f);
        } else if (e.n < 0) {
            cat = "csMatchLose";
            look(tier == 0 ? CsFaceHappy : CsFaceSad, 2.f);
        } else {
            cat = "csMatchDraw";
            look(CsFaceSurprise, 2.f);
        }
        break;
    }
    if (cat.empty()) return;
    float r = roll ? roll() : randf(0, 1);
    if (r >= prob) return;
    queue(CsLine{cat, arg, prio, now + ttl}, now);
}

void CsVoice::queue(const CsLine& l, double now) {
    if (hasPending && pending.until >= now && pending.prio > l.prio) return;
    pending = l;
    hasPending = true;
}

bool CsVoice::due(double now, CsLine& out) {
    if (!hasPending) return false;
    if (pending.until < now || (pending.cat.rfind("csDeath", 0) == 0 && now - lastDeathLine < deathGap)) {
        hasPending = false;
        return false;
    }
    double gap = now - lastLine;
    if (!(gap >= 20 || (pending.prio > lastPrio && pending.prio >= 7 && gap >= 5))) return false;
    out = pending;
    hasPending = false;
    lastLine = now;
    lastPrio = out.prio;
    if (out.cat.rfind("csDeath", 0) == 0) lastDeathLine = now;
    if (out.cat == "csArmsDeathLate") lastDeathLine = now;
    if (out.cat == "csEnter") lastEnter = now;
    return true;
}

bool GameSense::start(const std::string& tk) {
    if (th.joinable()) return true;
    token = tk;
    accepted = {token};
    quit = false;
    th = std::thread([this] { loop(); });
    return true;
}

void GameSense::stop(bool removeCfg) {
    if (th.joinable()) {
        quit = true;
        th.join();
    }
    if (removeCfg) {
        std::vector<std::wstring> ds = dirs.empty() ? cs2CfgDirs() : dirs;
        for (auto& d : ds) logf("gsi: removed cfg in '%s': %d", narrow(d).c_str(), (int)gsiRemove(d));
    }
}

std::vector<Json> GameSense::take() {
    std::lock_guard<std::mutex> g(mu);
    std::vector<Json> v(std::make_move_iterator(q.begin()), std::make_move_iterator(q.end()));
    q.clear();
    return v;
}

void GameSense::install() {
    dirs = cs2CfgDirs();
    for (auto& d : dirs) {
        std::string old;
        bool ok = gsiInstall(d, token, &old);
        if (!old.empty() && std::find(accepted.begin(), accepted.end(), old) == accepted.end()) accepted.push_back(old);
        static int logged = 0;
        if (logged++ < 4 || !ok) logf("gsi: cfg in '%s' %s%s", narrow(d).c_str(), ok ? "ok" : "NOT written", old.empty() || old == token ? "" : " (old token kept until CS2 restarts)");
    }
}

static void gsiReply(SOCKET c, int code) {
    const char* why = code == 200 ? "OK" : code == 400 ? "Bad Request" : code == 403 ? "Forbidden" : code == 413 ? "Too Large" : "Nope";
    char h[160];
    int n = snprintf(h, sizeof(h), "HTTP/1.1 %d %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", code, why);
    ::send(c, h, n, 0);
}

void GameSense::handle(unsigned long long cs) {
    SOCKET c = (SOCKET)cs;
    std::string buf;
    GsiReq r;
    char tmp[16384];
    int res = 0;
    double end = nowSec() + 3;
    while ((res = gsiParse(buf, r)) == 0) {
        if (quit || nowSec() > end) return;
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(c, &rs);
        timeval tv{0, 200000};
        if (select(0, &rs, nullptr, nullptr, &tv) <= 0) continue;
        int n = recv(c, tmp, sizeof(tmp), 0);
        if (n <= 0) return;
        buf.append(tmp, n);
    }
    if (res < 0) {
        gsiReply(c, res == -2 ? 413 : 400);
        return;
    }
    Json j;
    int code = gsiAccept(r, accepted, j);
    gsiReply(c, code);
    if (code != 200) {
        static int logged = 0;
        if (logged++ < 3) logf("gsi: refused %s %s: %d", r.method.c_str(), r.path.c_str(), code);
        return;
    }
    if (seen.load() < 0) logf("gsi: CS2 is talking");
    seen = nowSec();
    std::lock_guard<std::mutex> g(mu);
    q.push_back(std::move(j));
    while (q.size() > 64) q.pop_front();
}

void GameSense::loop() {
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd)) return;
    SOCKET s = INVALID_SOCKET;
    double nextInstall = 0, nextBind = 0;
    bool busyLogged = false;
    while (!quit) {
        double now = nowSec();
        if (now >= nextInstall) {
            install();
            nextInstall = now + 300;
        }
        if (s == INVALID_SOCKET && now >= nextBind) {
            nextBind = now + 30;
            s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            BOOL ex = TRUE;
            setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&ex, sizeof(ex));
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            a.sin_port = htons(kPort);
            if (s == INVALID_SOCKET || bind(s, (sockaddr*)&a, sizeof(a)) || listen(s, 4)) {
                if (s != INVALID_SOCKET) closesocket(s);
                s = INVALID_SOCKET;
                if (!busyLogged) logf("gsi: port %d busy, retry every 30 s", kPort);
                busyLogged = true;
            } else {
                logf("gsi: listening on 127.0.0.1:%d", kPort);
            }
        }
        if (s == INVALID_SOCKET) {
            Sleep(200);
            continue;
        }
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(s, &rs);
        timeval tv{0, 200000};
        if (select(0, &rs, nullptr, nullptr, &tv) <= 0) continue;
        SOCKET c = accept(s, nullptr, nullptr);
        if (c == INVALID_SOCKET) continue;
        DWORD to = 2000;
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof(to));
        setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, (const char*)&to, sizeof(to));
        handle(c);
        shutdown(c, SD_SEND);
        closesocket(c);
    }
    if (s != INVALID_SOCKET) closesocket(s);
    WSACleanup();
}
