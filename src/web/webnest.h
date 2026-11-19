#pragma once
#include "common.h"
#include <thread>
#include <mutex>
#include <atomic>

struct WebEl {
    int id = 0, type = 0;
    float x = 0, y = 0, w = 0, h = 0;
};

struct WebState {
    bool any = false;
    bool visible = false, focused = false, closed = false;
    float iw = 0, ih = 0, dpr = 1;
    float sy = 0, sh = 0, cw = 0;
    std::string mode;
    int shame = 0;
    std::wstring topic, title;
    std::vector<WebEl> els;
    int ver = 0;
};

class WebNest {
public:
    ~WebNest() { stop(); }
    bool start(const std::wstring& assetsDir, const std::wstring& facesDir);
    void stop();
    bool running() const { return port != 0; }
    std::wstring url() const;
    WebState state();
    double lastSeen() const { return seen.load(); }
    bool extAlive() const { return nowSec() - extSeen.load() < 6.0; }
    bool extKnown() const { return nowSec() - extSeen.load() < 60.0; }
    double extLast() const { return extSeen.load(); }
    static constexpr int kExtPort = 47731;
    void send(const std::string& json);
    void dropCmds();
    void clearState();
    void setName(const std::wstring& n);
    void setShy(bool allowed);
    void resetSeen() { seen = 0; }
    const std::string& key() const { return token; }
    void testState(WebState s);
    std::vector<std::string> testCmds(int& since);
    void testStart() { testOnly = true; port = kExtPort; }
    void testExtSeen(double t) { extSeen = t; }

private:
    bool testOnly = false;
    std::thread th;
    std::atomic<bool> quit{false};
    std::atomic<double> seen{0}, extSeen{-100};
    std::mutex mu;
    WebState st;
    std::vector<std::pair<int, std::string>> cmds;
    int seq = 0;
    int port = 0;
    unsigned long long sock = ~0ull;
    std::string token, page, nameJs;
    bool shyOk = true;
    std::wstring faces;
    void loop();
    void handle(unsigned long long c);
};
