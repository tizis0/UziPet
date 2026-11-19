#pragma once
#include "gfx.h"

struct Phrases {
    std::vector<std::string> keys;
    std::vector<std::vector<std::wstring>> vals;
    std::vector<std::string> stages;
    std::vector<std::vector<int>> chains;
    int stage = 1;
    std::unordered_map<std::string, int> index;
    int find(const char* cat, int st = -2) const;
    std::vector<std::wstring> listAt(const char* cat, int st) const;
    std::string layerOf(const char* cat, int st = -2) const;
    void reindex();
    std::string lastCat;
    bool lastOwn = false;
    struct Bag { std::vector<int> order; int pos = 0; int last = -1; };
    std::vector<Bag> bags;
    std::vector<uint64_t> recent;
    static const size_t kRecent = 40;
    std::wstring statePath;
    bool dirty = false;
    void loadState(const std::wstring& path);
    void saveState();
    std::wstring userName;
    int gender = 1;
    bool load(const std::wstring& path);
    bool loadFile(const std::wstring& path, const std::string& prefix);
    std::wstring pick(const char* cat, const std::wstring& arg = L"");
    std::vector<std::wstring> list(const char* cat) const;
    std::wstring fill(std::wstring s, const std::wstring& arg) const;
};

struct Bubble {
    struct Cue { float at; std::string name; float arg; bool fired; };
    std::vector<Cue> cues;
    std::vector<Cue> firedCues;
    std::wstring text;
    std::vector<float> revealAt;
    std::vector<uint8_t> fx;
    float typeDur = 0;
    bool dark = false;
    struct Meter { float v; D2D1_COLOR_F color; int icon; };
    std::vector<Meter> meters;
    const wchar_t* fmtFamily = nullptr;
    float t = 0, life = 0;
    bool active = false;
    D2D1_RECT_F rect{};
    ComPtr<IDWriteTextFormat> fmt;
    float fmtScale = 0;
    ComPtr<ID2D1SolidColorBrush> clear;
    ID2D1RenderTarget* clearRt = nullptr;

    std::vector<std::wstring> choices;
    std::vector<D2D1_RECT_F> choiceRc;
    std::string askId, expired;
    std::string talkId;
    std::string replyKind;
    std::vector<int> replyAllow;
    V2 mouse{-1e9f, -1e9f};
    bool hoverKeep = false;
    bool dismissed = false;
    ComPtr<IDWriteTextFormat> btnFmt;
    float btnFmtS = 0;

    bool input = false;
    std::wstring inputText;

    void say(const std::wstring& s);
    void ask(const std::wstring& s, const std::vector<std::wstring>& ch, const std::string& id, float wait = 25.f);
    void askInput(const std::wstring& s, const std::wstring& initial, const std::vector<std::wstring>& ch, const std::string& id);
    bool asking() const { return active && !choices.empty(); }
    float holdT = 0, readT = 0;
    void hold() { holdT = std::max(holdT, readT); }
    bool held() const { return active && !asking() && t < holdT; }
    bool reading() const { return active && t < holdT; }
    int choiceAt(V2 p) const;
    void close();
    void hide();
    void update(float dt);
    float age() const { return active ? t : 1e9f; }
    void draw(DrawCtx& c, V2 headTop, V2 headCenter, float headR, D2D1_RECT_F area, D2D1_RECT_F win);
    bool hit(V2 p) const { return active && p.x >= rect.left && p.x <= rect.right && p.y >= rect.top && p.y <= rect.bottom; }
};
