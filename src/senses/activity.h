#pragma once
#include "world.h"
#include <mmdeviceapi.h>
#include <endpointvolume.h>

enum class AppKind { None, Browser, Game, Create, Code, Chat, Claude, Media, Explorer, TaskMgr, Other };
const char* appKindName(AppKind k);

struct Activity {
    HWND self = nullptr;
    HWND fg = nullptr;
    std::wstring exe, exePath, title;
    AppKind kind = AppKind::None;
    RECT fgRect{};
    int fgMon = -1;
    bool fgCovers = false;
    bool capture = false;
    double since = 0;
    bool switched = false;
    float sitT = 0;
    float peak = 0, vol = 0, level = 0, slow = 0, loudT = 0;
    bool preVolume = false;
    bool loud = false;
    double lastLoud = -1e9;
    bool shot = false;
    struct Click {
        double t;
        POINT p;
        HWND w;
    };
    std::vector<Click> clicks;

    void init(HWND selfWnd);
    void update(float dt, double now, double idleSec, const World& w);
    void onClipboard();

private:
    float fgPollT = 0, audioInitT = 0, audioLogT = 0, lvlMax = 0;
    bool lmbWas = false;
    ComPtr<IAudioMeterInformation> meter;
    ComPtr<IAudioEndpointVolume> epVol;
    void pollForeground(double now, const World& w);
    void pollAudio(float dt, double now);
};

std::wstring processExe(DWORD pid, std::wstring* fullPath = nullptr);
std::wstring defaultUserName();
