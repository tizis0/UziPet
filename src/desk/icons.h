#pragma once
#include "gfx.h"

struct DIcon {
    std::wstring name;
    std::wstring path;
    bool folder = false;
    PITEMID_CHILD pidl = nullptr;
    POINT pos{};
    RECT cell{}, img{};
};

struct HandsRec {
    std::wstring path, name;
    std::wstring hands;
    POINT home{};
    bool ownHome = false;
    int kind = 0;
    long long at = 0;
    DWORD attrs = INVALID_FILE_ATTRIBUTES;
    uint64_t id = 0;
};

class DesktopIcons {
public:
    ~DesktopIcons() { clear(); }
    bool init();
    bool ok() const { return (bool)fv || fake; }
    bool refresh();
    bool lost = false;
    int find(const std::wstring& name) const;
    int findPath(const std::wstring& path) const;
    bool viewHide(const DIcon& ic, bool hide);
    bool deselect(const DIcon& ic);
    bool itemPos(const DIcon& ic, POINT& out);
    double refreshedAt = 0;
    bool refreshIfOlder(double secs);
    POINT clientForImgCenter(V2 screenCenter) const;
    RECT cellAt(POINT clientPos) const;
    POINT toScreen(POINT p) const;
    POINT toClient(POINT p) const;
    bool isDesktopFile(const std::wstring& path) const;
    ComPtr<IWICBitmapSource> image(Gfx& g, const DIcon& ic, int px);
    bool snapOn();
    bool autoArrange();
    void recoverSnap();

    std::function<std::vector<RECT>()> worksFn;
    std::vector<RECT> workAreas() const;
    POINT gridOrigin() const;
    POINT snapCell(POINT clientPos) const;
    POINT cellUnder(POINT screenPt) const;
    bool onGrid(POINT cell) const;
    bool cellOnScreen(POINT cell) const;
    bool cellFree(POINT cell, const std::wstring& selfPath) const;
    bool nearestFree(V2 screenAt, const std::wstring& selfPath, POINT& out) const;
    bool resolveCell(POINT want, const std::wstring& selfPath, POINT& out) const;
    bool put(const DIcon& ic, POINT want, const char* why, POINT* got = nullptr);
    int putMany(const std::vector<std::pair<const DIcon*, POINT>>& m, const char* why);
    int putCount = 0, badMoves = 0;
    bool guardSelfTest();

    std::wstring handsDir() const;
    bool canTake(const std::wstring& path, std::string* why = nullptr) const;
    bool take(HandsRec& r);
    unsigned long takeErr = 0;
    bool takeRights = false;
    bool giveBack(HandsRec& r, std::wstring* nowPath = nullptr);
    int twinBacks = 0;
    bool toFolder(HandsRec& r, const std::wstring& folder, HWND owner);
    std::wstring where(const HandsRec& r) const { return r.hands.empty() ? r.path : r.hands; }
    std::wstring pathById(const HandsRec& r) const;
    void crashGiveBack(const HandsRec& r);
    int sweepHands(const std::vector<std::wstring>& busy);
    void placeAfter(const std::wstring& path, POINT cell, const char* why, const std::wstring& recPath, bool dropRec);
    void cancelPut(const std::wstring& path);
    bool pending(const std::wstring& path) const;
    bool pendingAny() const { return !pend.empty(); }
    void pump(double now, bool force = false);
    void flush(double secs);
    bool inHands(const std::wstring& path) const;
    void dropRecord(const std::wstring& path, bool onDeskOnly = false);
    bool restoreCarry();
    int retryHands(const std::vector<std::wstring>& busy);

    bool rememberHome(const DIcon& ic);
    void forgetHome(const std::wstring& name);
    int restoreHomes();
    void forgetHomes();

    std::string layoutText() const;
    bool layoutSnap(double now, const char* why, bool action);
    void layoutActed();
    void layoutIdle(double now);
    std::wstring layoutBefore() const;
    std::vector<std::wstring> layoutHistory() const;
    std::wstring layoutClock(const std::wstring& file) const;
    int layoutRestore(const std::wstring& file, int* total = nullptr);
    std::wstring layoutDir() const;

    bool fake = false, fakeSnap = true, fakeAuto = false;
    bool fakeDead = false;
    bool testOnly = false;
    int fakeLag = 2;
    bool fakeShowHidden = false;
    bool fakeNoReturn = false;
    bool fakeOtherVolume = false;
    std::wstring fakeDir, fakeHands;
    void setFake(const std::wstring& dir, POINT sp, int px);
    void addFake(const std::wstring& name, POINT pos, bool folder);
    bool fakeMove(const std::wstring& name, POINT pos);
    void fakeForget(const std::wstring& path);
    std::vector<RECT> fakeWorks;

    std::vector<DIcon> items;
    POINT spacing{};
    int iconPx = 48;
    POINT imgOff{};
    HWND lv = nullptr;

    bool viaView = false;

private:
    ComPtr<IFolderView2> fv;
    ComPtr<IShellFolder> sf;
    ComPtr<IShellFolder> dsk;
    void clear();
    void place(DIcon& d) const;
    bool rawMove(const DIcon& ic, POINT cell);
    bool rawMany(const std::vector<std::pair<const DIcon*, POINT>>& m);
    bool cellOk(POINT cell, const char* why);
    bool searchNear(V2 at, const std::function<bool(POINT)>& good, POINT& out) const;
    mutable POINT gridO{};
    mutable bool gridKnown = false;
    struct Pend {
        std::wstring path, recPath;
        POINT cell{};
        std::string why;
        bool dropRec = true, placed = false;
        double since = -1, placedAt = -1;
    };
    std::vector<Pend> pend;
    double pumpAt = -1e9;
    std::vector<std::pair<std::wstring, int>> fakeNew;
    std::string lastSnapText;
    std::wstring lastSnapFile, beforeCand;
    double lastSnapAt = -1e9, lastIdleAt = -1e9, stampNow0 = 0;
    ULONGLONG stampFt0 = 0;
    bool layoutLoaded = false;
    void layoutLoad();
    void layoutPrune();
    std::wstring layoutStamp(double now);
};
