#pragma once
#include "common.h"
#include "json.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <deque>

struct GsiReq {
    std::string method, path, body;
    bool origin = false;
    size_t used = 0;
};
constexpr size_t kGsiBodyCap = 256 * 1024;
int gsiParse(const std::string& buf, GsiReq& r);
int gsiAccept(const GsiReq& r, const std::vector<std::string>& tokens, Json& out);

constexpr const wchar_t* kGsiFile = L"gamestate_integration_uzipet.cfg";
std::string gsiCfg(const std::string& token);
std::string gsiCfgToken(const std::string& text);
std::vector<std::wstring> cs2CfgDirs();
bool gsiInstall(const std::wstring& dir, const std::string& token, std::string* old);
bool gsiRemove(const std::wstring& dir);
std::string gsiNewToken();

enum class CsEv {
    Enter, MapChange, Kill, Death, Ace, DmStreak, RoundWin, RoundLose, LoseStreak, BombPlanted, BombDefused, BombExploded, Flashed, MatchEnd,
    LastDefuse, KnifeDefuse, ClutchDefuse, BlastWasted, SurvivedBlast, MissedDefuse, NearAce, Pickup, Mvp, SmokeKill, TeamKill, KnifeKill,
    PistolKills, YouPlanted, MatchPoint,
    ArmsUp, ArmsPistols, ArmsKnife, ArmsLost, ArmsKnifeKill
};
struct CsEvent {
    CsEv type = CsEv::Enter;
    int n = 0;
    bool hs = false;
    bool dm = false;
    bool arms = false;
    bool full = false;
    bool ours = false;
    bool late = false;
    bool knife = false;
    int kills = 0, deaths = 0, mvps = 0, hp = 0;
    int mine = 0, theirs = 0;
    double sec = 0;
    std::string gun;
    std::string map;
    double t = 0;
};
const char* csEvName(CsEv e);
std::string csDescribe(const CsEvent& e);
std::wstring csMapName(const std::string& raw);
std::wstring csGunName(const std::string& raw);

class CsSense {
public:
    std::vector<CsEvent> feed(const Json& d, double t);
    bool inMatch() const { return inMap; }
private:
    bool inMap = false, haveMe = false, dm = false, full = false, arms = false;
    std::string mapRaw, mapPhase, roundPhase, bomb, myTeam;
    int kills = 0, deaths = 0, roundKills = 0, roundHs = 0, hp = 0, flashed = 0, reported = 0, loseRow = 0;
    double gameoverAt = -1e9, lastFlash = -1e9;
    std::string gameoverMap;
    bool prevOwn = false, hadC4 = false;
    int money = 0, score = 0, armor = 0, mvps = 0, smoked = 0;
    std::vector<std::string> items;
    std::string heldWas;
    int roundNo = -1, rkAtPlant = 0, mpCt = -1, mpT = -1;
    double liveAt = -1e9, plantAt = -1e9, c4GoneAt = -1e9, mvpAt = -1e9, lastKillAt = -1e9;
    bool hurt = false, roundKnife = false;
    double defAt = -1e9, defLen = 0, blastAt = -1e9;
    bool defWait = false, blastWait = false;
    int blastHp = 0;
    std::vector<std::string> ladder;
    int level = -1, dropLast = 0;
    double dropAt = -1e9;
    bool armsPistol = false, armsKnife = false;
    void resetArms();
};

enum CsFace { CsFaceNone, CsFaceHappy, CsFaceSad, CsFaceSurprise, CsFaceAngry, CsFaceBlush };
struct CsLine {
    std::string cat;
    std::wstring arg;
    int prio = 0;
    double until = 0;
    int face = CsFaceNone;
    float faceT = 0;
};
struct CsVoice {
    std::function<float()> roll;
    CsLine pending;
    bool hasPending = false;
    double lastLine = -1e9, lastDeathLine = -1e9, lastEnter = -1e9, lastArmsUp = -1e9;
    double deathGap = 60;
    int lastPrio = 0;
    void push(const CsEvent& e, int tier, double now, int& face, float& faceT);
    void queue(const CsLine& l, double now);
    bool due(double now, CsLine& out);
};

class GameSense {
public:
    ~GameSense() { stop(false); }
    static constexpr int kPort = 47733;
    bool start(const std::string& token);
    void stop(bool removeCfg);
    bool running() const { return th.joinable(); }
    std::vector<Json> take();
    double lastPost() const { return seen.load(); }

private:
    std::thread th;
    std::atomic<bool> quit{false};
    std::atomic<double> seen{-1e9};
    std::mutex mu;
    std::deque<Json> q;
    std::string token;
    std::vector<std::string> accepted;
    std::vector<std::wstring> dirs;
    void loop();
    void install();
    void handle(unsigned long long c);
};
