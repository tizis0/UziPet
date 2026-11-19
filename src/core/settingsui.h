#pragma once
#include "app.h"

struct SetRow {
    enum Kind { Section, Caption, Toggle, Seg, Buttons };
    Kind kind = Section;
    int col = 0;
    std::wstring label;
    std::vector<std::wstring> opts;
    std::vector<int> ids;
    bool danger = false;
    bool off = false;
};

struct Meter {
    std::wstring name;
    float v = 0;
    D2D1_COLOR_F color{};
    std::wstring val;
};

struct SettingsUi {
    std::vector<SetRow> rows;
    std::function<void(int)> cmd;
    std::function<bool(int)> on;
    std::function<std::wstring(const SetRow&)> label;
    std::function<std::vector<Meter>()> meters;
    std::function<std::wstring()> status, mood, callName;
    std::function<std::vector<std::wstring>()> extra;
    std::function<bool()> dark;
    int nameCmd = 0;
    int versionCmd = 0;

    void open(App& a, V2 nearPt, bool show = true);
    void close();
    void redraw();
    void tick();
    bool visible() const { return hwnd && IsWindowVisible(hwnd); }

    HWND hwnd = nullptr, dim = nullptr;
    App* app = nullptr;
    Surface surf;
    float s = 1;
    POINT at{};
    RECT mon{};
    float anim = 0;
    int animDir = 0;
    ULONGLONG lastTick = 0;
    struct Hit { D2D1_RECT_F r; int id; };
    std::vector<Hit> hits;
    V2 mouse{-1e9f, -1e9f};
    int pressed = 0;
    ComPtr<IDWriteTextFormat> fTitle, fName, fText, fSmall, fBtn, fPct;
    float fmtS = 0;
    static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK dimProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    void place();
};

extern SettingsUi g_settings;
