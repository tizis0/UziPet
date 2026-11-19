#pragma once
#include "common.h"

struct Mon {
    RECT rc{}, work{};
};

enum class SurfKind { Floor, Window, Icon, Web, WinFloor };

struct Surf {
    SurfKind kind = SurfKind::Floor;
    HWND hwnd = nullptr;
    int index = -1;
    float y = 0, x0 = 0, x1 = 0;
    int tag = 0;
    int run = -1;
    RECT win{};
};

struct IconBox {
    int index;
    RECT img;
};

struct World {
    std::vector<Mon> mons;
    std::vector<Surf> surfs;
    std::vector<RECT> occluders;
    HWND self = nullptr;
    float headroom = 120;
    bool winFloors = true;
    float winRoom = 170;
    bool fake = false;
    bool web = false;
    RECT webRc{};
    std::vector<Surf> webSurfs;
    struct FakeWin { HWND h; RECT r; bool zoomed; };
    std::vector<FakeWin> fakeWins;
    std::vector<Surf> fakeKeep;
    bool fakeInWeb = false;

    void refresh();
    void setFake(RECT monitor, RECT work);
    void addFakeWindow(HWND h, RECT r, bool zoomed);
    int monitorAt(V2 p) const;
    std::vector<std::pair<float, float>> screenSpans(float y, float x0, float x1) const;
    void addWebBottom();
    void addWinFloor(HWND h, const RECT& r, int above, const std::function<RECT(int)>& winAt);
    int nearestMonitor(V2 p) const;
    bool windowRect(HWND h, RECT& r) const;
    bool rectVisible(const RECT& r) const;
    std::vector<RECT> works() const {
        std::vector<RECT> w;
        for (auto& m : mons) w.push_back(m.work);
        return w;
    }
    void addIconSurf(int index, const RECT& img);
    std::vector<std::vector<int>> iconRuns;
    bool joinIcons = true;
    void addIconSurfs(std::vector<IconBox> boxes, int step);
    bool runHas(const Surf& s, int icon) const;
    void dump() const;
};
