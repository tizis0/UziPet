#pragma once
#include "rig.h"
#include "world.h"
#include "icons.h"
#include "speech.h"
#include "activity.h"

struct Settings {
    float scale = 1.0f;
    bool icons = true;
    bool talk = true;
    std::wstring name;
    bool closeExplorer = true;
    bool railgun = false;
    bool shyPages = true;
    bool bubbleDark = false;
    bool hardcore = false;
    bool hardcoreAsked = false;
    bool replies = true;
    bool hugCursor = true;
    bool solver = true;
    bool notes = true;
    int gender = 1;
    bool dev = false;
    bool extAsked = false;
    bool gameReact = true;
    std::wstring gsiToken;
    long long lastDiskNag = 0, lastBinNag = 0, lastWebAsk = 0;
    std::wstring path() const { return appDataDir() + L"\\settings.ini"; }
    bool loadFailed = false;
    void load() {
        loadFailed = !waitIniReadable(path(), L"pet", 4000);
        if (loadFailed) logf("settings: settings.ini не читается, беру умолчания и не перезаписываю его");
        wchar_t buf[64];
        GetPrivateProfileStringW(L"pet", L"scale", L"1.0", buf, 64, path().c_str());
        scale = clampf((float)_wtof(buf), 0.5f, 2.5f);
        icons = GetPrivateProfileIntW(L"pet", L"icons", 1, path().c_str()) != 0;
        talk = GetPrivateProfileIntW(L"pet", L"talk", 1, path().c_str()) != 0;
        closeExplorer = GetPrivateProfileIntW(L"pet", L"closeExplorer", 1, path().c_str()) != 0;
        railgun = GetPrivateProfileIntW(L"pet", L"railgun", 0, path().c_str()) != 0;
        shyPages = GetPrivateProfileIntW(L"pet", L"shy", 1, path().c_str()) != 0;
        bubbleDark = GetPrivateProfileIntW(L"pet", L"dark", 0, path().c_str()) != 0;
        hardcore = GetPrivateProfileIntW(L"pet", L"hardcore", 0, path().c_str()) != 0;
        hardcoreAsked = GetPrivateProfileIntW(L"pet", L"hardcoreAsked", 0, path().c_str()) != 0;
        replies = GetPrivateProfileIntW(L"pet", L"replies", 1, path().c_str()) != 0;
        hugCursor = GetPrivateProfileIntW(L"pet", L"hugCursor", 1, path().c_str()) != 0;
        if (!hardcoreAsked) hardcore = false;
        solver = GetPrivateProfileIntW(L"pet", L"solver", 1, path().c_str()) != 0;
        notes = GetPrivateProfileIntW(L"pet", L"notes", 1, path().c_str()) != 0;
        gender = GetPrivateProfileIntW(L"pet", L"gender", 1, path().c_str()) == 2 ? 2 : 1;
        dev = GetPrivateProfileIntW(L"pet", L"dev", 0, path().c_str()) != 0;
        extAsked = GetPrivateProfileIntW(L"pet", L"extAsked", 0, path().c_str()) != 0;
        gameReact = GetPrivateProfileIntW(L"pet", L"gameReact", 1, path().c_str()) != 0;
        wchar_t nb[128];
        GetPrivateProfileStringW(L"pet", L"gsiToken", L"", nb, 128, path().c_str());
        gsiToken = nb;
        GetPrivateProfileStringW(L"pet", L"name", L"", nb, 128, path().c_str());
        name = nb;
        if (name.empty()) name = defaultUserName();
        auto ll = [&](const wchar_t* k) {
            GetPrivateProfileStringW(L"nag", k, L"0", nb, 128, path().c_str());
            return _wtoll(nb);
        };
        lastDiskNag = ll(L"disk");
        lastBinNag = ll(L"bin");
        lastWebAsk = ll(L"web");
    }
    void save() const {
        if (loadFailed) return;
        std::string raw;
        if (!readFile(path(), raw) || raw.size() < 2 || (unsigned char)raw[0] != 0xFF || (unsigned char)raw[1] != 0xFE) {
            int n = MultiByteToWideChar(CP_ACP, 0, raw.data(), (int)raw.size(), nullptr, 0);
            std::wstring w(n, L'\0');
            if (n) MultiByteToWideChar(CP_ACP, 0, raw.data(), (int)raw.size(), w.data(), n);
            writeFile(path(), std::string("\xFF\xFE", 2) + std::string((const char*)w.data(), w.size() * sizeof(wchar_t)));
        }
        wchar_t buf[32];
        swprintf(buf, 32, L"%.2f", scale);
        WritePrivateProfileStringW(L"pet", L"scale", buf, path().c_str());
        WritePrivateProfileStringW(L"pet", L"icons", icons ? L"1" : L"0", path().c_str());
        WritePrivateProfileStringW(L"pet", L"talk", talk ? L"1" : L"0", path().c_str());
        WritePrivateProfileStringW(L"pet", L"closeExplorer", closeExplorer ? L"1" : L"0", path().c_str());
        WritePrivateProfileStringW(L"pet", L"railgun", railgun ? L"1" : L"0", path().c_str());
        WritePrivateProfileStringW(L"pet", L"shy", shyPages ? L"1" : L"0", path().c_str());
        WritePrivateProfileStringW(L"pet", L"dark", bubbleDark ? L"1" : L"0", path().c_str());
        WritePrivateProfileStringW(L"pet", L"hardcore", hardcore ? L"1" : L"0", path().c_str());
        WritePrivateProfileStringW(L"pet", L"hardcoreAsked", hardcoreAsked ? L"1" : L"0", path().c_str());
        WritePrivateProfileStringW(L"pet", L"replies", replies ? L"1" : L"0", path().c_str());
        WritePrivateProfileStringW(L"pet", L"hugCursor", hugCursor ? L"1" : L"0", path().c_str());
        WritePrivateProfileStringW(L"pet", L"solver", solver ? L"1" : L"0", path().c_str());
        WritePrivateProfileStringW(L"pet", L"notes", notes ? L"1" : L"0", path().c_str());
        WritePrivateProfileStringW(L"pet", L"gender", gender == 2 ? L"2" : L"1", path().c_str());
        WritePrivateProfileStringW(L"pet", L"dev", dev ? L"1" : L"0", path().c_str());
        WritePrivateProfileStringW(L"pet", L"extAsked", extAsked ? L"1" : L"0", path().c_str());
        WritePrivateProfileStringW(L"pet", L"gameReact", gameReact ? L"1" : L"0", path().c_str());
        WritePrivateProfileStringW(L"pet", L"gsiToken", gsiToken.c_str(), path().c_str());
        WritePrivateProfileStringW(L"pet", L"name", name.c_str(), path().c_str());
        auto ll = [&](const wchar_t* k, long long v) { WritePrivateProfileStringW(L"nag", k, std::to_wstring(v).c_str(), path().c_str()); };
        ll(L"disk", lastDiskNag);
        ll(L"bin", lastBinNag);
        ll(L"web", lastWebAsk);
    }
};

class Pet;

struct App {
    Gfx gfx;
    Rig rig;
    RigInstance inst;
    Rig rigSit, rigSleep;
    RigInstance instSit, instSleep;
    Rig rigBaby;
    RigInstance instBaby;
    World world;
    DesktopIcons icons;
    Phrases phrases;
    Bubble bubble;
    Settings settings;
    Activity activity;
    class WebNest* web = nullptr;
    class GameSense* gsi = nullptr;
    class RobloxSense* rbx = nullptr;
    std::function<bool(const std::wstring&, int)> renderFace;
    std::function<bool(std::string&, const std::wstring&)> renderSelf;
    Pet* pet = nullptr;
    HWND hwnd = nullptr;
    float dpi = 1.f;
    float s = 1.f;
    int winW = 0, winH = 0;
    V2 comInWin;
    V2 winOrigin;
    std::wstring assets;
    double now = 0;
    bool iconsReady = false;
    std::function<void(const std::wstring&, const std::wstring&)> notify;
    float flash = 0;
    V2 flashAt;
};
