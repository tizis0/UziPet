#pragma once
#include "app.h"
#include "mods.h"

class ModsUi {
public:
    void open(App& app, HWND anchor, Mods& mods);
    void close();
    void update();

private:
    struct Hit { D2D1_RECT_F rect; int id; };
    HWND hwnd = nullptr;
    App* app = nullptr;
    Mods* mods = nullptr;
    Surface surface;
    std::vector<Hit> hits;
    ComPtr<IDWriteTextFormat> titleFont, bodyFont, smallFont, detailFont;
    float scale = 1, fontScale = 0;
    unsigned revision = 0;
    bool dark = false, keyboard = false;
    V2 mouse{-1.f, -1.f};
    int focus = 0, pressed = 0;
    size_t selected = 0, firstRow = 0;
    std::wstring selectedFolder;
    D2D1_RECT_F listRect{}, detailsRect{};
    float detailScroll = 0, detailMax = 0;

    bool draw();
    void redraw();
    void resize(RECT anchor, UINT dpi);
    void select(size_t index);
    void command(int id);
    void key(WPARAM key);
    int at(V2 point) const;
    const Mods::Mod* selection() const;
    static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
};
