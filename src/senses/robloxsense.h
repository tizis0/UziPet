#pragma once
#include "common.h"
#include "gamesense.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <deque>
#include <map>

struct RbxLog {
    enum K { None, Place, Join, Leave } k = None;
    long long place = 0, universe = 0;
    double t = 0;
};
RbxLog rbxParseLine(const std::string& line);
double rbxLogTime(const std::string& line);

struct RbxEvent {
    bool join = true;
    bool again = false;
    bool switched = false;
    long long universe = 0, place = 0;
    double t = 0, played = 0;
    std::string name, genre;
};
std::string rbxDescribe(const RbxEvent& e);

class RbxSense {
public:
    std::vector<RbxEvent> feed(const RbxLog& l);
    std::vector<RbxEvent> flush(double t);
    std::vector<RbxEvent> gone(double t);
    void forget() { cur = curPlace = pendPlace = 0; }
    bool inGame() const { return cur || curPlace; }
private:
    long long cur = 0, curPlace = 0, pendPlace = 0;
    double since = 0, pendT = 0;
    std::map<long long, double> leftAt;
    void leave(std::vector<RbxEvent>& out, double t, bool switched);
    void join(std::vector<RbxEvent>& out, long long u, long long place, double t);
};

class RbxLogTail {
public:
    std::wstring dir;
    void poll(std::vector<std::pair<std::string, bool>>& out);
    std::vector<std::wstring> watching() const;
private:
    struct F { std::wstring name; unsigned long long off = 0; std::string part; bool skipFirst = false; };
    std::vector<F> files;
    bool started = false;
};

struct RbxName { std::string name, genre; double at = 0; };
bool rbxParseGames(const std::string& body, long long universe, RbxName& out);
bool rbxFetchGame(long long universe, RbxName& out);
long long rbxFetchUniverse(long long place);
class RbxNames {
public:
    std::wstring file;
    std::function<bool(long long, RbxName&)> fetch = rbxFetchGame;
    bool get(long long universe, RbxName& out, double now);
private:
    std::map<long long, RbxName> m;
    bool loaded = false;
    void load();
    void save() const;
};

std::wstring rbxShortName(const std::string& name);
const char* rbxKind(const std::string& name, const std::string& genre);
bool rbxPlaceholder(const std::string& name);
struct RbxVoice {
    double lastJoinLine = -1e9;
    bool pick(const RbxEvent& e, double now, float roll, CsLine& out);
};

class RobloxSense {
public:
    ~RobloxSense() { stop(); }
    bool start(const std::wstring& logsDir, const std::wstring& cacheFile);
    void stop();
    std::vector<RbxEvent> take();
private:
    std::thread th;
    std::atomic<bool> quit{false};
    std::mutex mu;
    std::deque<RbxEvent> q;
    RbxLogTail tail;
    RbxSense sense;
    RbxNames names;
    void loop();
};
bool robloxRunning();
