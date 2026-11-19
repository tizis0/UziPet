#include "pet.h"
#include "hitbox.h"
#include <ole2.h>
#include "webnest.h"
#include "settingsui.h"
#include "items.h"
#include "intro.h"
#include "update.h"
#include "games.h"
#include <shlwapi.h>
#include <sddl.h>
#include <map>
#include <array>
#include <csignal>
#include <exception>
#include <stdlib.h>
#include <intrin.h>

static App g_app;
static Pet* g_pet = nullptr;
static Surface g_surf;
static UINT g_msgTaskbarCreated = 0;
static NOTIFYICONDATAW g_nid{};
static HICON g_trayIcon = nullptr;
static bool g_hidden = false, g_press = false, g_dragging = false, g_quit = false;
static POINT g_pressPt{};

#define WM_TRAY (WM_APP + 1)
#define WM_COMEHERE (WM_APP + 2)
#define WM_BATTERIES (WM_APP + 3)
#define WM_FILEDROP (WM_APP + 4)

static std::wstring g_dropPath;
static int g_dropN = 0;
static double g_dropAt = -1;
enum { ID_COME = 100, ID_SAY, ID_SIZE_S, ID_SIZE_M, ID_SIZE_L, ID_ICONS, ID_TALK, ID_RESTORE, ID_AUTOSTART, ID_QUIT, ID_NAME, ID_CLOSEEXP, ID_RAILGUN,
       ID_WEB, ID_SHY, ID_EXT, ID_SETTINGS, ID_DBG_PERF, ID_DBG_PARTS, ID_DBG_LOG, ID_DBG_SNAP, ID_DBG_EN_M, ID_DBG_EN_P, ID_DBG_REL_M,
       ID_DBG_REL_P, ID_THEME_L, ID_THEME_D, ID_HARDCORE, ID_DBG_FOOD_M, ID_DBG_FOOD_P, ID_DBG_PARCEL, ID_GENDER_M, ID_GENDER_F,
       ID_DBG_INTRO, ID_DBG_GROW, ID_DEV, ID_SOLVER, ID_DBG_SOLVER, ID_NOTES, ID_GAMEREACT, ID_ICONS_UNDO, ID_REPLIES, ID_HUGCURSOR, ID_ICONS_UNDO_MENU };
enum { ID_ICONS_UNDO_OLD = 900 };
static bool g_dbgPerf = false, g_dbgParts = false;
static bool g_introPending = false;
static float g_frameMs = 0;
static void openSettings(bool show = true);
static UpdatePkg g_pendingPkg;
static float g_updateT = 0;
static bool g_snapMode = false;
static HWND g_nameWnd = nullptr;
static double g_testVisit = 0;
static void showNameDialog();

static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

static V2 cursorPos() {
    POINT p;
    GetCursorPos(&p);
    return V2((float)p.x, (float)p.y);
}

static bool applyScale() {
    App& a = g_app;
    a.s = a.dpi * a.settings.scale;
    if (!a.rig.load(a.gfx, a.assets, a.s)) return false;
    a.rigSit.load(a.gfx, a.assets, a.s, L"rig_sit.json");
    a.rigSleep.load(a.gfx, a.assets, a.s, L"rig_sleep.json");
    a.rigBaby.load(a.gfx, a.assets, a.s, L"rig_baby.json");
    g_pet->bindRig();
    a.winW = (int)(760 * a.s);
    a.winH = (int)(460 * a.s);
    a.comInWin = V2(a.winW * 0.5f, a.winH * 0.62f);
    return g_surf.create(a.gfx, a.winW, a.winH);
}

static bool renderHead(Surface& ic, const FaceState& fs) {
    App& a = g_app;
    if (!ic.create(a.gfx, 64, 64)) return false;
    RigInstance tmp;
    tmp.bind(&a.rig);
    float hr = a.rig.headRadius;
    float k = 30.f / (hr * 1.08f);
    auto root = D2D1::Matrix3x2F::Translation(-a.rig.headCenter.x, -a.rig.headCenter.y) * D2D1::Matrix3x2F::Scale(k, k) *
                D2D1::Matrix3x2F::Translation(32, 35);
    tmp.step(0, root, V2(0, 0), 0, 1, 0);
    auto* rt = ic.begin();
    DrawCtx c{rt, a.gfx.d2d.Get(), a.gfx.dw.Get(), ic.brush.Get(), a.gfx.round.Get(), ic.gen, a.s};
    tmp.draw(c, fs, V2(0, 0));
    ic.end(a.gfx);
    return true;
}

static HICON makeTrayIcon() {
    Surface ic;
    FaceState fs;
    fs.happy = 1;
    if (!renderHead(ic, fs)) return nullptr;
    return ic.toIcon();
}

static bool renderFacePng(const std::wstring& path, int kind);
static std::wstring g_iconsSave, g_iconsApply, g_iconsHideTest;

static bool renderLipsCursor(Surface& ic, int n) {
    App& a = g_app;
    if (!ic.create(a.gfx, n, n)) return false;
    auto* rt = ic.begin();
    DrawCtx c{rt, a.gfx.d2d.Get(), a.gfx.dw.Get(), ic.brush.Get(), a.gfx.round.Get(), ic.gen, n / 32.f};
    float h = n * 0.5f;
    drawLips(c, V2(h, h + n * 0.02f), n * 0.72f, 1.f, true);
    ic.end(a.gfx);
    return true;
}

static HCURSOR lipsCursor() {
    static HCURSOR cur = nullptr;
    if (!cur) {
        int n = std::max(32, (int)lroundf(32.f * g_app.dpi));
        Surface ic;
        if (renderLipsCursor(ic, n)) cur = ic.toIcon(true, n / 2, n / 2);
        logf("lips cursor %dx%d: %s", n, n, cur ? "ok" : "failed");
        if (!cur) cur = LoadCursor(nullptr, IDC_HAND);
    }
    return cur;
}

static void startWeb() {
    App& a = g_app;
    if (!a.web) a.web = new WebNest();
    std::wstring dir = appDataDir() + L"\\web", ext = exeDir() + L"\\extension";
    CreateDirectoryW(dir.c_str(), nullptr);
    bool hasExt = GetFileAttributesW(ext.c_str()) != INVALID_FILE_ATTRIBUTES;
    for (int i = 0; i < 4; i++) {
        renderFacePng(dir + L"\\face" + std::to_wstring(i) + L".png", i);
        if (hasExt) renderFacePng(ext + L"\\face" + std::to_wstring(i) + L".png", i);
    }
    if (a.web->start(a.assets, dir)) {
        a.web->setName(a.settings.name);
        a.web->setShy(a.settings.shyPages);
        if (hasExt) writeFile(ext + L"\\token.json", "{\"t\":\"" + a.web->key() + "\"}");
    }
}

static CsSense g_cs;
static std::wstring robloxLogsDir() {
    PWSTR p = nullptr;
    std::wstring d;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) d = std::wstring(p) + L"\\Roblox\\logs";
    CoTaskMemFree(p);
    return d;
}
static void startGameSense() {
    App& a = g_app;
    if (!a.settings.gameReact) {
        if (a.gsi) a.gsi->stop(true);
        else for (auto& d : cs2CfgDirs()) gsiRemove(d);
        if (a.rbx) a.rbx->stop();
        return;
    }
    if (!a.rbx) a.rbx = new RobloxSense();
    a.rbx->start(robloxLogsDir(), appDataDir() + L"\\roblox_names.txt");
    if (a.settings.gsiToken.empty()) {
        a.settings.gsiToken = widen(gsiNewToken());
        a.settings.save();
    }
    if (!a.gsi) a.gsi = new GameSense();
    a.gsi->start(narrow(a.settings.gsiToken));
}

static void gameSenseFrame() {
    App& a = g_app;
    if (a.rbx)
        for (auto& e : a.rbx->take()) {
            logf("rbx: %s", rbxDescribe(e).c_str());
            g_pet->rbxEvent(e);
        }
    if (!a.gsi) return;
    for (auto& j : a.gsi->take())
        for (auto& e : g_cs.feed(j, a.now)) {
            logf("cs: %s", csDescribe(e).c_str());
            g_pet->csEvent(e);
        }
}

static bool renderFacePng(const std::wstring& path, int kind) {
    Surface ic;
    FaceState fs;
    if (kind == 1) fs.open = 0.05f;
    if (kind == 2) { fs.blush = 1; fs.angry = 0.7f; }
    if (kind == 3) fs.happy = 1;
    if (!renderHead(ic, fs)) return false;
    return ic.savePng(g_app.gfx, path);
}

static bool encodePng(Surface& s, std::string& out) {
    Gfx& g = g_app.gfx;
    ComPtr<IStream> mem;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &mem))) return false;
    ComPtr<IWICBitmap> wb;
    if (FAILED(g.wic->CreateBitmapFromMemory(s.w, s.h, GUID_WICPixelFormat32bppBGRA, s.w * 4, (UINT)((size_t)s.w * s.h * 4), (BYTE*)s.bits, &wb)))
        return false;
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> fe;
    if (FAILED(g.wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) || FAILED(enc->Initialize(mem.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(enc->CreateNewFrame(&fe, nullptr)) || FAILED(fe->Initialize(nullptr)))
        return false;
    fe->SetSize(s.w, s.h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    fe->SetPixelFormat(&fmt);
    if (FAILED(fe->WriteSource(wb.Get(), nullptr)) || FAILED(fe->Commit()) || FAILED(enc->Commit())) return false;
    HGLOBAL hg = nullptr;
    if (FAILED(GetHGlobalFromStream(mem.Get(), &hg))) return false;
    STATSTG stg{};
    mem->Stat(&stg, STATFLAG_NONAME);
    const char* p = (const char*)GlobalLock(hg);
    if (!p) return false;
    out.assign(p, (size_t)stg.cbSize.QuadPart);
    GlobalUnlock(hg);
    return !out.empty();
}

static bool renderSelfPhoto(std::string& png, const std::wstring& caption) {
    App& a = g_app;
    Pet& p = *g_pet;
    if (!a.winW || !a.winH || p.introHidden || p.st == St::Inside) return false;
    V2 wo = a.winOrigin;
    D2D1_RECT_F b = p.bounds(wo);
    if (b.right <= b.left || b.bottom <= b.top) return false;
    float cx = (b.left + b.right) * 0.5f, cy = (b.top + b.bottom) * 0.5f;
    float side = std::max(b.right - b.left, b.bottom - b.top) + 40.f * a.s;
    float x0 = clampf(cx - side * 0.5f, 0, (float)a.winW), y0 = clampf(cy - side * 0.5f, 0, (float)a.winH);
    float x1 = clampf(cx + side * 0.5f, 0, (float)a.winW), y1 = clampf(cy + side * 0.5f, 0, (float)a.winH);
    int cw = (int)(x1 - x0), ch = (int)(y1 - y0);
    if (cw < 16 || ch < 16) return false;
    Surface shot;
    if (!shot.create(a.gfx, a.winW, a.winH)) return false;
    auto* rt = shot.begin(D2D1::ColorF(0.90f, 0.88f, 0.95f, 1));
    DrawCtx c{rt, a.gfx.d2d.Get(), a.gfx.dw.Get(), shot.brush.Get(), a.gfx.round.Get(), shot.gen, a.s};
    c.fonts = a.gfx.ui.Get();
    c.family = a.gfx.uiFamily.c_str();
    p.render(c, wo);
    if (!shot.end(a.gfx)) return false;
    int edge = (int)(12 * a.s), foot = (int)(44 * a.s);
    Surface out;
    if (!out.create(a.gfx, cw + edge * 2, ch + edge + foot)) return false;
    auto* ort = out.begin(D2D1::ColorF(0.985f, 0.98f, 0.965f, 1));
    ComPtr<ID2D1Bitmap> bm;
    if (SUCCEEDED(ort->CreateBitmapFromWicBitmap(shot.wbmp.Get(), nullptr, &bm)))
        ort->DrawBitmap(bm.Get(), D2D1::RectF((float)edge, (float)edge, (float)(edge + cw), (float)(edge + ch)), 1.f,
                        D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, D2D1::RectF(x0, y0, x0 + cw, y0 + ch));
    ComPtr<IDWriteTextFormat> f;
    a.gfx.dw->CreateTextFormat(a.gfx.uiFamily.c_str(), a.gfx.ui.Get(), DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                               15.f * a.s, L"ru-ru", &f);
    if (f) {
        f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        out.brush->SetColor(D2D1::ColorF(0.22f, 0.16f, 0.30f, 1));
        out.brush->SetOpacity(1.f);
        ort->DrawTextW(caption.c_str(), (UINT32)caption.size(), f.Get(),
                       D2D1::RectF((float)edge, (float)(edge + ch), (float)(edge + cw), (float)(edge + ch + foot)), out.brush.Get());
    }
    if (!out.end(a.gfx)) return false;
    return encodePng(out, png);
}

static void trayNotify(const std::wstring& title, const std::wstring& text) {
    NOTIFYICONDATAW n = g_nid;
    n.uFlags = NIF_INFO;
    n.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
    n.hBalloonIcon = g_nid.hIcon;
    wcsncpy_s(n.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(n.szInfo, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

static void addTray() {
    g_nid = {};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_app.hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = g_trayIcon ? g_trayIcon : LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"Узи");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static bool autostartOn() {
    wchar_t buf[MAX_PATH * 2];
    DWORD sz = sizeof(buf);
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, L"UziPet", RRF_RT_REG_SZ, nullptr, buf, &sz) == ERROR_SUCCESS;
}

static void setAutostart(bool on) {
    if (on) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring v = L"\"" + std::wstring(exe) + L"\"";
        RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, L"UziPet", REG_SZ, v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, L"UziPet");
    }
}

static void showMenu() {
    App& a = g_app;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_SETTINGS, L"Настройки…");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_QUIT, L"Выключить Узи");
    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(a.hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, p.x, p.y, 0, a.hwnd, nullptr);
    PostMessageW(a.hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
}

static void iconsUndoMenu() {
    App& a = g_app;
    auto hs = a.icons.ok() ? a.icons.layoutHistory() : std::vector<std::wstring>{};
    HMENU m = CreatePopupMenu();
    for (size_t k = 1; k < hs.size() && k < 5; k++)
        AppendMenuW(m, MF_STRING, ID_ICONS_UNDO_OLD + k, (L"Как было в " + a.icons.layoutClock(hs[k])).c_str());
    if (hs.size() < 2) AppendMenuW(m, MF_STRING | MF_GRAYED, 0, L"Других снимков нет");
    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(a.hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, p.x, p.y, 0, a.hwnd, nullptr);
    PostMessageW(a.hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
}

static void setSize(float sc) {
    g_app.settings.scale = sc;
    g_app.settings.save();
    V2 keep = g_pet->pos;
    applyScale();
    g_pet->spawn(keep - V2(0, 2));
}

static void iconsUndo(size_t k = 0) {
    App& a = g_app;
    auto& I = a.icons;
    auto hs = I.ok() ? I.layoutHistory() : std::vector<std::wstring>{};
    std::wstring snap = k < hs.size() ? hs[k] : L"";
    if (snap.empty()) {
        logf("icons undo: no snapshot (%zu)", k);
        return;
    }
    std::wstring hm = I.layoutClock(snap);
    if (!g_snapMode && !a.world.fake) {
        std::wstring q = L"Поставить иконки рабочего стола так, как они стояли в " + hm + L"?\n\nИконки, которых тогда не было, останутся на своих местах.";
        if (MessageBoxW(nullptr, q.c_str(), L"Узи", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 | MB_TOPMOST | MB_SETFOREGROUND) != IDYES) {
            logf("icons undo: cancelled");
            return;
        }
    }
    g_pet->dropCarried(false);
    g_pet->deskHomeNow("layout restore");
    I.flush(2.0);
    if (I.refresh()) I.layoutSnap(a.now, "before a restore", false);
    int total = 0;
    int same = I.layoutRestore(snap, &total);
    logf("icons undo: %s -> %d of %d exactly in place", narrow(snap).c_str(), same, total);
    g_pet->sayCat(same ? "restore" : "restoreNone", 1.f, L"", true);
}

static void onCommand(int id) {
    App& a = g_app;
    if (id > ID_ICONS_UNDO_OLD && id < ID_ICONS_UNDO_OLD + 5) {
        iconsUndo((size_t)(id - ID_ICONS_UNDO_OLD));
        return;
    }
    switch (id) {
    case ID_SIZE_S: setSize(0.75f); break;
    case ID_SIZE_M: setSize(1.0f); break;
    case ID_SIZE_L: setSize(1.35f); break;
    case ID_ICONS:
        a.settings.icons = !a.settings.icons;
        a.settings.save();
        if (!a.settings.icons) {
            g_pet->dropCarried(false);
            g_pet->deskHomeNow("icons off");
        }
        break;
    case ID_TALK:
        a.settings.talk = !a.settings.talk;
        a.settings.save();
        break;
    case ID_RESTORE: {
        g_pet->dropCarried(false);
        g_pet->deskHomeNow("restore");
        int n = a.icons.restoreHomes();
        logf("restored %d icons", n);
        g_pet->sayCat(n ? "restore" : "restoreNone", 1.f, L"", true);
        break;
    }
    case ID_ICONS_UNDO: iconsUndo(); break;
    case ID_ICONS_UNDO_MENU: iconsUndoMenu(); break;
    case ID_AUTOSTART: setAutostart(!autostartOn()); break;
    case ID_NAME: showNameDialog(); break;
    case ID_EXT:
        if (g_pet->extCanStart()) {
            g_settings.close();
            g_pet->extStart(true);
            break;
        }
        {
            std::wstring dir = exeDir() + L"\\extension";
            const wchar_t* who = g_pet->baby() || g_pet->vit.intro ? L"Узи пока маленькая" : L"Узи сейчас не поможет";
            const wchar_t* how = L"Можно и самому: открой страницу расширений браузера, включи «Режим разработчика», "
                                 L"нажми «Загрузить распакованное» и выбери папку extension, она уже открыта.";
            if (g_snapMode || a.world.fake) {
                logf("ext: button refused, would open '%s' and notify '%s'", narrow(dir).c_str(), narrow(who).c_str());
            } else {
                ShellExecuteW(nullptr, L"explore", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                trayNotify(who, how);
            }
        }
        break;
    case ID_SHY:
        a.settings.shyPages = !a.settings.shyPages;
        a.settings.save();
        if (a.web) a.web->setShy(a.settings.shyPages);
        break;
    case ID_CLOSEEXP:
        a.settings.closeExplorer = !a.settings.closeExplorer;
        a.settings.save();
        break;
    case ID_RAILGUN:
        a.settings.railgun = !a.settings.railgun;
        a.settings.save();
        break;
    case ID_SETTINGS: openSettings(); break;
    case ID_HARDCORE:
        a.settings.hardcore = !a.settings.hardcore;
        a.settings.hardcoreAsked = true;
        a.settings.save();
        break;
    case ID_REPLIES:
        a.settings.replies = !a.settings.replies;
        a.settings.save();
        break;
    case ID_HUGCURSOR:
        a.settings.hugCursor = !a.settings.hugCursor;
        a.settings.save();
        break;
    case ID_SOLVER:
        a.settings.solver = !a.settings.solver;
        a.settings.save();
        break;
    case ID_NOTES:
        a.settings.notes = !a.settings.notes;
        a.settings.save();
        break;
    case ID_GAMEREACT:
        a.settings.gameReact = !a.settings.gameReact;
        a.settings.save();
        if (g_snapMode || a.world.fake) logf("gameReact: %d (snapshot: game folder untouched)", (int)a.settings.gameReact);
        else startGameSense();
        break;
    case ID_DBG_SOLVER:
        g_settings.close();
        if (!g_pet->baby()) g_pet->solverWait = 0.6f;
        break;
    case ID_DBG_FOOD_M: case ID_DBG_FOOD_P:
        g_pet->vit.food = clampf(g_pet->vit.food + (id == ID_DBG_FOOD_P ? 25.f : -25.f), 0.f, 100.f);
        if (g_pet->vit.food <= 0.f) g_pet->vit.food = 0.01f;
        g_pet->vit.save();
        break;
    case ID_DBG_PARCEL:
        if (!g_pet->vit.parcelOut) g_pet->vit.parcelT = 1.f;
        break;
    case ID_THEME_L: case ID_THEME_D:
        a.settings.bubbleDark = id == ID_THEME_D;
        a.settings.save();
        break;
    case ID_GENDER_M: case ID_GENDER_F:
        a.settings.gender = id == ID_GENDER_F ? 2 : 1;
        a.phrases.gender = a.settings.gender;
        a.settings.save();
        break;
    case ID_DBG_INTRO:
        g_settings.close();
        introStart(a, *g_pet, true);
        break;
    case ID_DBG_GROW:
        introGrowNow(*g_pet);
        break;
    case ID_DEV: {
        static double last = -100;
        static int clicks = 0;
        double now = nowSec();
        clicks = now - last < 1.5 ? clicks + 1 : 1;
        last = now;
        if (clicks >= 5) {
            clicks = 0;
            a.settings.dev = !a.settings.dev;
            a.settings.save();
            logf("developer mode: %d", (int)a.settings.dev);
            openSettings(true);
        }
        break;
    }
    case ID_DBG_EN_M: case ID_DBG_EN_P:
        g_pet->vit.energy = clampf(g_pet->vit.energy + (id == ID_DBG_EN_P ? 25.f : -25.f), 0.f, 100.f);
        if (g_pet->vit.energy <= 0.f) g_pet->vit.energy = 0.01f;
        g_pet->vit.save();
        break;
    case ID_DBG_REL_M: case ID_DBG_REL_P: g_pet->addRel(id == ID_DBG_REL_P ? 10.f : -10.f, "debug"); g_pet->vit.save(); break;
    case ID_DBG_PERF: g_dbgPerf = !g_dbgPerf; break;
    case ID_DBG_PARTS: g_dbgParts = !g_dbgParts; break;
    case ID_DBG_LOG: ShellExecuteW(nullptr, L"open", (exeDir() + L"\\uzipet.log").c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
    case ID_DBG_SNAP: {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        std::wstring dir = std::wstring(tmp) + L"UziSnap";
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --snapshot \"" + dir + L"\"";
        std::thread([cmd, dir] {
            STARTUPINFOW si{sizeof(si)};
            PROCESS_INFORMATION pi{};
            std::wstring c = cmd;
            if (CreateProcessW(nullptr, c.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
                WaitForSingleObject(pi.hProcess, 60000);
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
                ShellExecuteW(nullptr, L"explore", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        }).detach();
        break;
    }
    case ID_QUIT: DestroyWindow(a.hwnd); break;
    }
}

static std::wstring bodyPath() { return appDataDir() + L"\\Узи Дорман.drone"; }

static bool binHasBody() {
    ComPtr<IShellFolder> desk, bin;
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHGetDesktopFolder(&desk)) || FAILED(SHGetSpecialFolderLocation(nullptr, CSIDL_BITBUCKET, &pidl))) return true;
    bool found = false;
    if (SUCCEEDED(desk->BindToObject(pidl, nullptr, IID_PPV_ARGS(&bin)))) {
        ComPtr<IEnumIDList> en;
        HRESULT hr = bin->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN, &en);
        if (FAILED(hr)) found = true;
        else if (hr == S_OK && en) {
            PITEMID_CHILD it = nullptr;
            ULONG got = 0;
            while (!found && en->Next(1, &it, &got) == S_OK) {
                STRRET sr;
                wchar_t buf[MAX_PATH];
                if (SUCCEEDED(bin->GetDisplayNameOf(it, SHGDN_NORMAL, &sr)) && SUCCEEDED(StrRetToBufW(&sr, it, buf, MAX_PATH)))
                    if (wcsncmp(buf, L"Узи Дорман", 10) == 0) found = true;
                CoTaskMemFree(it);
            }
        }
    } else {
        found = true;
    }
    CoTaskMemFree(pidl);
    return found;
}

static void bodyToBin() {
    App& a = g_app;
    std::wstring f = bodyPath();
    writeFile(f, "Здесь лежит Узи Дорман. Если очистить корзину, её не станет совсем.\r\n");
    std::wstring from = f + L'\0';
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    int r = SHFileOperationW(&op);
    logf("body to bin: %d", r);
    if (r != 0) return;
    g_pet->vit.inBin = true;
    g_pet->vit.save();
    ShowWindow(a.hwnd, SW_HIDE);
}

static void binWatch(float dt) {
    App& a = g_app;
    static float t = 0;
    t -= dt;
    if (t > 0) return;
    t = 3.f;
    if (GetFileAttributesW(bodyPath().c_str()) != INVALID_FILE_ATTRIBUTES) {
        DeleteFileW(bodyPath().c_str());
        g_pet->vit.inBin = false;
        g_pet->vit.save();
        logf("body restored from bin");
        if (!a.world.mons.empty()) g_pet->spawn(V2((float)(a.world.mons[0].work.left + a.world.mons[0].work.right) / 2, (float)a.world.mons[0].rc.top + 40.f));
        ShowWindow(a.hwnd, SW_SHOWNOACTIVATE);
        return;
    }
    if (!binHasBody()) {
        logf("body deleted from bin: Uzi is gone, state wiped");
        g_pet->vit.persist = false;
        DeleteFileW(g_pet->vit.path().c_str());
        DestroyWindow(a.hwnd);
    }
}

static bool overBin(V2 p) {
    App& a = g_app;
    for (auto& ic : a.icons.items) {
        bool bin = ic.name == L"Корзина" || ic.name == L"Recycle Bin" || ic.path.find(L"645FF040") != std::wstring::npos;
        if (bin && p.x >= ic.cell.left - 10 && p.x <= ic.cell.right + 10 && p.y >= ic.cell.top - 10 && p.y <= ic.cell.bottom + 10) return true;
    }
    return false;
}

static void openSettings(bool show) {
    App& a = g_app;
    auto& u = g_settings;
    g_pet->gameStop("settings");
    u.rows.clear();
    auto add = [&](int col, SetRow::Kind k, const wchar_t* t, std::vector<std::wstring> o = {}, std::vector<int> ids = {}, bool danger = false) {
        SetRow r;
        r.col = col;
        r.kind = k;
        r.label = t;
        r.opts = o;
        r.ids = ids;
        r.danger = danger;
        u.rows.push_back(r);
    };
    add(0, SetRow::Section, L"ВНЕШНИЙ ВИД");
    add(0, SetRow::Caption, L"Размер");
    add(0, SetRow::Seg, L"", {L"Маленькая", L"Обычная", L"Большая"}, {ID_SIZE_S, ID_SIZE_M, ID_SIZE_L});
    add(0, SetRow::Caption, L"Облачка");
    add(0, SetRow::Seg, L"", {L"Светлые", L"Тёмные"}, {ID_THEME_L, ID_THEME_D});
    add(0, SetRow::Caption, L"Обращаться ко мне");
    add(0, SetRow::Seg, L"", {L"Как к парню", L"Как к девушке"}, {ID_GENDER_M, ID_GENDER_F});
    add(0, SetRow::Section, L"ПОВЕДЕНИЕ");
    add(0, SetRow::Toggle, L"Болтать сама", {}, {ID_TALK});
    add(0, SetRow::Toggle, L"Отвечать ей кнопками", {}, {ID_REPLIES});
    add(0, SetRow::Toggle, L"Обнимая, берёт курсор", {}, {ID_HUGCURSOR});
    add(0, SetRow::Toggle, L"Залезать в папки", {}, {ID_ICONS});
    add(0, SetRow::Toggle, L"Может закрыть проводник", {}, {ID_CLOSEEXP});
    add(0, SetRow::Toggle, L"Смущающие страницы", {}, {ID_SHY});
    add(0, SetRow::Toggle, L"Хардкор: смерть от голода", {}, {ID_HARDCORE});
    add(0, SetRow::Toggle, L"Глюки Солвера", {}, {ID_SOLVER});
    add(1, SetRow::Section, L"СИСТЕМА");
    add(1, SetRow::Toggle, L"Запускать с Windows", {}, {ID_AUTOSTART});
    add(1, SetRow::Toggle, L"Записки на столе", {}, {ID_NOTES});
    add(1, SetRow::Toggle, L"Реакции на игры", {}, {ID_GAMEREACT});
    add(1, SetRow::Buttons, L"Браузер", {L"Подключить"}, {ID_EXT});
    {
        std::wstring snap = a.icons.ok() ? a.icons.layoutBefore() : L"";
        static std::wstring undoLabel;
        undoLabel = snap.empty() ? L"Иконки: снимка нет" : L"Иконки как в " + a.icons.layoutClock(snap);
        add(1, SetRow::Buttons, undoLabel.c_str(), {L"Вернуть", L"Раньше…"}, {ID_ICONS_UNDO, ID_ICONS_UNDO_MENU});
        u.rows.back().off = snap.empty();
    }
    add(1, SetRow::Buttons, L"", {L"Выключить Узи"}, {ID_QUIT}, true);
    if (a.settings.dev) {
        add(1, SetRow::Section, L"ОТЛАДКА");
        add(1, SetRow::Toggle, L"Время кадра", {}, {ID_DBG_PERF});
        add(1, SetRow::Toggle, L"Контуры деталей", {}, {ID_DBG_PARTS});
        add(1, SetRow::Buttons, L"Энергия", {L"−25", L"+25"}, {ID_DBG_EN_M, ID_DBG_EN_P});
        add(1, SetRow::Buttons, L"Отношения", {L"−10", L"+10"}, {ID_DBG_REL_M, ID_DBG_REL_P});
        add(1, SetRow::Buttons, L"Еда", {L"−25", L"+25", L"Посылка"}, {ID_DBG_FOOD_M, ID_DBG_FOOD_P, ID_DBG_PARCEL});
        add(1, SetRow::Buttons, L"Интро", {L"Показать", L"Вырасти"}, {ID_DBG_INTRO, ID_DBG_GROW});
        add(1, SetRow::Buttons, L"Глюк", {L"Солвер"}, {ID_DBG_SOLVER});
        add(1, SetRow::Buttons, L"", {L"Лог", L"Снапшоты поз"}, {ID_DBG_LOG, ID_DBG_SNAP});
    }
    u.nameCmd = ID_NAME;
    u.versionCmd = ID_DEV;
    u.cmd = [](int id) {
        if (id == ID_QUIT) {
            for (auto& r : g_settings.rows)
                for (size_t i = 0; i < r.ids.size(); i++)
                    if (r.ids[i] == ID_QUIT && r.opts[i] != L"Точно выключить?") {
                        r.opts[i] = L"Точно выключить?";
                        g_settings.redraw();
                        return;
                    }
        }
        onCommand(id);
    };
    u.on = [](int id) {
        App& a = g_app;
        float sc = a.settings.scale;
        switch (id) {
        case ID_SIZE_S: return fabsf(sc - 0.75f) < 0.01f;
        case ID_SIZE_M: return fabsf(sc - 1.0f) < 0.01f;
        case ID_SIZE_L: return fabsf(sc - 1.35f) < 0.01f;
        case ID_THEME_L: return !a.settings.bubbleDark;
        case ID_THEME_D: return a.settings.bubbleDark;
        case ID_GENDER_M: return a.settings.gender != 2;
        case ID_GENDER_F: return a.settings.gender == 2;
        case ID_TALK: return a.settings.talk;
        case ID_ICONS: return a.settings.icons;
        case ID_CLOSEEXP: return a.settings.closeExplorer;
        case ID_SHY: return a.settings.shyPages;
        case ID_HARDCORE: return a.settings.hardcore;
        case ID_REPLIES: return a.settings.replies;
        case ID_HUGCURSOR: return a.settings.hugCursor;
        case ID_SOLVER: return a.settings.solver;
        case ID_NOTES: return a.settings.notes;
        case ID_GAMEREACT: return a.settings.gameReact;
        case ID_AUTOSTART: return autostartOn();
        case ID_DBG_PERF: return g_dbgPerf;
        case ID_DBG_PARTS: return g_dbgParts;
        }
        return false;
    };
    u.label = [](const SetRow& r) {
        App& a = g_app;
        if (!r.ids.empty() && r.ids[0] == ID_EXT) return std::wstring(a.web && a.web->extKnown() ? L"Браузер: на связи" : L"Браузер");
        return r.label;
    };
    u.dark = [] { return g_app.settings.bubbleDark; };
    u.meters = [] {
        auto& v = g_pet->vit;
        std::vector<Meter> out{{L"Энергия", v.energy, D2D1::ColorF(0.96f, 0.72f, 0.24f, 1)},
                               {L"Сытость", v.food, D2D1::ColorF(0.30f, 0.76f, 0.54f, 1)}};
        if (g_pet->baby()) {
            double left = g_pet->babyLeft();
            int sec = (int)ceil(left);
            wchar_t t[32];
            if (g_pet->growing || sec <= 0) wcscpy_s(t, L"уже пора!");
            else swprintf(t, 32, L"%d:%02d:%02d", sec / 3600, sec / 60 % 60, sec % 60);
            out.push_back({L"Вырастет через", (float)(left / (4 * 3600.0) * 100.0), D2D1::ColorF(0.33f, 0.62f, 1.f, 1), t});
        } else {
            out.push_back({L"Отношения", v.rel, D2D1::ColorF(1.f, 0.37f, 0.66f, 1)});
        }
        return out;
    };
    u.status = [] {
        auto& v = g_pet->vit;
        if (v.dead) return std::wstring(L"FATAL ERROR · навсегда");
        if (v.fatal) return std::wstring(L"FATAL ERROR");
        if (v.food < 10) return std::wstring(L"Очень голодная");
        if (v.food < 30) return std::wstring(L"Голодная");
        if (g_pet->st == St::Sleep) return std::wstring(L"Спит");
        if (v.energy < 30) return std::wstring(L"Устала");
        return std::wstring(L"Бодрая");
    };
    u.mood = [] {
        if (g_pet->baby()) return std::wstring(g_pet->growing ? L"Светится: нажми на неё" : L"Ещё совсем маленькая");
        float r = g_pet->vit.rel;
        if (r < 30) return std::wstring(L"Злится на тебя");
        if (r < 67) return std::wstring(L"Относится как обычно");
        if (r < 93) return std::wstring(L"Тепло к тебе относится");
        return std::wstring(L"Обожает тебя");
    };
    u.callName = [] { return L"Зовёт тебя: " + g_app.settings.name; };
    u.extra = [] {
        auto& v = g_pet->vit;
        std::vector<std::wstring> out;
        out.push_back(L"Батареек в шкафчике: " + std::to_wstring(v.stock) + L" из 6");
        if (!g_pet->baby()) {
            int d = g_pet->metDays((double)time(nullptr)), d10 = d % 10, d100 = d % 100;
            const wchar_t* w = d10 == 1 && d100 != 11 ? L" день" : d10 >= 2 && d10 <= 4 && (d100 < 12 || d100 > 14) ? L" дня" : L" дней";
            out.push_back(L"Знакомы: " + std::to_wstring(d) + w);
        }
        out.push_back(std::wstring(L"Версия ") + UZI_VERSION + (g_app.settings.dev ? L" · разработчик" : L""));
        if (v.parcelOut) out.push_back(L"Посылка ждёт на столе");
        else {
            int h = (int)(v.parcelT / 3600.f), mnt = (int)fmodf(v.parcelT / 60.f, 60.f);
            out.push_back(L"Посылка через " + (h ? std::to_wstring(h) + L" ч " : std::wstring()) + std::to_wstring(mnt) + L" мин");
        }
        return out;
    };
    u.open(a, g_pet->headC, show);
}

static bool sessionBye(bool live);
static void byeHold(float secs);
static void frame(float dt);
static const UINT_PTR kExtDragTimer = 77;

static void extDragFolder() {
    App& a = g_app;
    std::wstring dir = exeDir() + L"\\extension";
    if (g_snapMode || a.world.fake) {
        logf("ext: would start a shell drag of '%s' (snapshot: no drag)", narrow(dir).c_str());
        return;
    }
    ComPtr<IShellItem> item;
    ComPtr<IDataObject> data;
    HRESULT hr = SHCreateItemFromParsingName(dir.c_str(), nullptr, IID_PPV_ARGS(&item));
    if (SUCCEEDED(hr)) hr = item->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data));
    if (FAILED(hr)) {
        logf("ext: no data object for '%s' (0x%08X)", narrow(dir).c_str(), (unsigned)hr);
        return;
    }
    g_pet->extDragBegin();
    SetTimer(a.hwnd, kExtDragTimer, 15, nullptr);
    DWORD eff = DROPEFFECT_NONE;
    hr = SHDoDragDrop(a.hwnd, data.Get(), nullptr, DROPEFFECT_COPY | DROPEFFECT_LINK, &eff);
    KillTimer(a.hwnd, kExtDragTimer);
    logf("ext: SHDoDragDrop 0x%08X", (unsigned)hr);
    g_pet->extDragEnd(eff);
}

static bool desktopAt(POINT pt) {
    HWND h = WindowFromPoint(pt);
    if (!h) return false;
    if (h == g_app.icons.lv) return true;
    wchar_t cls[64] = L"";
    GetClassNameW(GetAncestor(h, GA_ROOT), cls, 64);
    return !wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW");
}

static int g_fakeHilite = -2;
static bool g_hiliteSeen = false;
static int dropHilite() {
    if (g_snapMode || g_app.world.fake) return g_fakeHilite;
    HWND lv = g_app.icons.lv;
    DWORD_PTR r = (DWORD_PTR)-2;
    if (!lv || !SendMessageTimeoutW(lv, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_DROPHILITED, SMTO_ABORTIFHUNG, 50, &r)) return -2;
    return (int)(INT_PTR)r;
}

static bool heldDropOnDesk(POINT pt, bool overDesk, int hilite) {
    Pet& p = *g_pet;
    auto& I = g_app.icons;
    if (!overDesk || !I.isDesktopFile(p.heldPath)) return false;
    if (hilite >= 0) {
        logf("held: released over an item explorer highlights (%d), explorer takes it", hilite);
        return false;
    }
    if (g_hiliteSeen && hilite == -1) return true;
    LONG labelH = (LONG)lroundf(22.f * g_app.dpi);
    for (auto& d : I.items) {
        if (p.deskHides(d) || !_wcsicmp(d.path.c_str(), p.heldPath.c_str())) continue;
        RECT hit{d.img.left - I.imgOff.x / 2, d.img.top - 2, d.img.right + I.imgOff.x / 2, std::min(d.cell.bottom - 2, d.img.bottom + labelH)};
        if (!PtInRect(&hit, pt)) continue;
        std::wstring ext = d.path.size() > 4 ? d.path.substr(d.path.size() - 4) : L"";
        bool target = d.folder || d.path.size() < 3 || d.path[1] != L':';
        for (auto e : {L".exe", L".lnk", L".bat", L".cmd", L".url", L".zip"}) target |= !_wcsicmp(ext.c_str(), e);
        if (target) {
            logf("held: released over '%s', explorer takes it", narrow(d.name).c_str());
            return false;
        }
    }
    return true;
}

static bool g_heldDeskDrop = false;
static POINT g_heldDropPt{};
static HRESULT heldQueryDrop(BOOL esc, DWORD keys, POINT pt, bool overDesk) {
    if (esc) return DRAGDROP_S_CANCEL;
    int hl = overDesk ? dropHilite() : -2;
    if (hl >= 0) g_hiliteSeen = true;
    if (keys & MK_LBUTTON) return S_OK;
    bool desk = heldDropOnDesk(pt, overDesk, hl);
    logf("held: released at %ld,%ld, over desktop %d, explorer highlight %d (seen %d) -> %s", pt.x, pt.y, (int)overDesk, hl, (int)g_hiliteSeen,
         desk ? "cancel, put the icon there ourselves" : "drop");
    if (!desk) return DRAGDROP_S_DROP;
    g_heldDeskDrop = true;
    g_heldDropPt = pt;
    return DRAGDROP_S_CANCEL;
}

class HeldDropSource : public IDropSource {
    LONG ref = 1;
    IDataObject* data;
    DWORD fetch(const wchar_t* fmt) {
        FORMATETC fe{(CLIPFORMAT)RegisterClipboardFormatW(fmt), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM sm{};
        DWORD v = 0;
        if (data && data->GetData(&fe, &sm) == S_OK) {
            if (sm.hGlobal && GlobalSize(sm.hGlobal) >= sizeof(DWORD))
                if (auto* q = (DWORD*)GlobalLock(sm.hGlobal)) {
                    v = *q;
                    GlobalUnlock(sm.hGlobal);
                }
            ReleaseStgMedium(&sm);
        }
        return v;
    }

public:
    explicit HeldDropSource(IDataObject* d) : data(d) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *ppv = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&ref);
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL esc, DWORD keys) override {
        POINT pt;
        GetCursorPos(&pt);
        return heldQueryDrop(esc, keys, pt, desktopAt(pt));
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD eff) override {
        if (!fetch(L"IsShowingLayered")) return DRAGDROP_S_USEDEFAULTCURSORS;
        if (HWND w = (HWND)(ULONG_PTR)fetch(L"DragWindow")) {
            WPARAM k = 1;
            switch (eff & ~DROPEFFECT_SCROLL) {
            case DROPEFFECT_COPY: k = 3; break;
            case DROPEFFECT_MOVE: k = 2; break;
            case DROPEFFECT_LINK: k = 4; break;
            }
            SendMessageW(w, WM_USER + 2, k, 0);
        }
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return S_OK;
    }
};

static void heldDragOut() {
    App& a = g_app;
    Pet& p = *g_pet;
    g_heldDeskDrop = false;
    if (g_snapMode || a.world.fake) {
        logf("held: would start a shell drag of '%s' (snapshot: no drag)", narrow(p.heldPath).c_str());
        p.heldDragBegin();
        return;
    }
    if (!p.heldDragBegin()) return;
    std::wstring path = p.heldPath;
    ComPtr<IShellItem> item;
    ComPtr<IDataObject> data;
    HRESULT hr = SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item));
    if (SUCCEEDED(hr)) hr = item->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data));
    if (FAILED(hr)) {
        logf("held: no data object for '%s' (0x%08X)", narrow(path).c_str(), (unsigned)hr);
        p.heldDragEnd(DROPEFFECT_NONE, p.carriedCenter, false);
        return;
    }
    if (a.icons.ok()) a.icons.refreshIfOlder(5.0);
    SetTimer(a.hwnd, kExtDragTimer, 15, nullptr);
    DWORD eff = DROPEFFECT_NONE;
    auto* src = new HeldDropSource(data.Get());
    hr = SHDoDragDrop(a.hwnd, data.Get(), src, DROPEFFECT_MOVE | DROPEFFECT_COPY | DROPEFFECT_LINK, &eff);
    src->Release();
    KillTimer(a.hwnd, kExtDragTimer);
    POINT pt;
    GetCursorPos(&pt);
    bool desk = desktopAt(pt);
    if (g_heldDeskDrop) {
        pt = g_heldDropPt;
        desk = true;
        eff = DROPEFFECT_NONE;
    }
    logf("held: SHDoDragDrop 0x%08X, own desktop drop %d", (unsigned)hr, (int)g_heldDeskDrop);
    g_heldDeskDrop = false;
    p.heldDragEnd(eff, V2((float)pt.x, (float)pt.y), desk);
}
static bool ctrlOnly() {
    return (GetAsyncKeyState(VK_CONTROL) & 0x8000) && !(GetAsyncKeyState(VK_MENU) & 0x8000);
}

static bool kissAim() {
    App& a = g_app;
    Pet& p = *g_pet;
    if (!ctrlOnly() || g_press || g_dragging || g_hidden || p.feeding || introRunning()) return false;
    V2 c = cursorPos();
    if (a.bubble.hit(c - a.winOrigin) || p.extFolderHit(c) || p.heldHit(c)) return false;
    return p.kissable(c);
}

static double g_byeAt = -1;

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    App& a = g_app;
    switch (msg) {
    case WM_QUERYENDSESSION:
        if (!g_hidden && !introRunning() && IsWindowVisible(h) && !(lp & ENDSESSION_CRITICAL) && sessionBye(true)) g_byeAt = nowSec();
        return TRUE;
    case WM_ENDSESSION:
        if (wp) {
            g_pet->deskHomeNow("session end");
            g_pet->dropCarried(false);
            a.icons.flush(1.0);
            g_pet->vit.save();
            g_pet->daySave();
            a.phrases.saveState();
            a.settings.save();
            logf("session end (lp 0x%08X), saved", (unsigned)lp);
            if (g_byeAt > 0 && !(lp & ENDSESSION_CRITICAL)) byeHold(std::min(1.f, 1.2f - (float)(nowSec() - g_byeAt)));
        } else if (g_byeAt > 0) {
            g_byeAt = -1;
            g_pet->byeCancel();
        }
        return 0;
    case WM_MOUSEACTIVATE:
        return a.bubble.input ? MA_ACTIVATE : MA_NOACTIVATE;
    case WM_CHAR:
        if (a.bubble.input && a.bubble.asking()) {
            wchar_t ch = (wchar_t)wp;
            std::wstring& t = a.bubble.inputText;
            if (ch == L'\r') {
                std::string id = a.bubble.askId;
                g_pet->answer(id, 0);
            } else if (ch == L'\b') {
                if (!t.empty()) t.pop_back();
            } else if (ch == 0x16 && OpenClipboard(h)) {
                if (HANDLE hd = GetClipboardData(CF_UNICODETEXT)) {
                    if (const wchar_t* cb = (const wchar_t*)GlobalLock(hd)) {
                        for (const wchar_t* q = cb; *q && *q != L'\r' && *q != L'\n' && t.size() < 24; q++) t += *q;
                        GlobalUnlock(hd);
                    }
                }
                CloseClipboard();
            } else if (ch >= 32 && t.size() < 24) {
                t += ch;
            }
            return 0;
        }
        break;
    case WM_LBUTTONDOWN: {
        if (g_pet->feeding) return 0;
        V2 local = cursorPos() - a.winOrigin;
        if (a.bubble.hit(local)) {
            if (a.bubble.asking()) {
                int ch = a.bubble.choiceAt(local);
                if (ch >= 0) {
                    std::string id = a.bubble.askId;
                    g_pet->answer(id, ch);
                }
                return 0;
            }
            a.bubble.hide();
            return 0;
        }
        if (g_pet->extFolderHit(cursorPos())) {
            extDragFolder();
            return 0;
        }
        if (g_pet->heldHit(cursorPos())) {
            heldDragOut();
            return 0;
        }
        if (ctrlOnly() && !introRunning() && g_pet->kiss(cursorPos()))
            return 0;
        g_press = true;
        g_dragging = false;
        GetCursorPos(&g_pressPt);
        SetCapture(h);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_press && !g_dragging) {
            POINT p;
            GetCursorPos(&p);
            if (abs(p.x - g_pressPt.x) + abs(p.y - g_pressPt.y) > 5) {
                g_dragging = true;
                g_pet->beginDrag(V2((float)p.x, (float)p.y));
            }
        }
        return 0;
    case WM_LBUTTONUP: {
        bool was = g_dragging, pressed = g_press;
        g_press = g_dragging = false;
        ReleaseCapture();
        if (was && g_pet->vit.dead && overBin(cursorPos())) {
            g_pet->endDrag();
            bodyToBin();
            return 0;
        }
        if (was) g_pet->endDrag();
        else if (pressed) g_pet->poke();
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (g_dragging) {
            g_dragging = g_press = false;
            g_pet->endDrag();
        }
        return 0;
    case WM_LBUTTONDBLCLK:
        if (kissAim()) return 0;
        g_pet->doubleClick();
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && kissAim()) {
            SetCursor(lipsCursor());
            return TRUE;
        }
        break;
    case WM_RBUTTONUP:
        if (!g_pet->feeding) g_pet->openTalk();
        return 0;
    case WM_TRAY:
        if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) showMenu();
        else if (lp == WM_LBUTTONUP) openSettings();
        return 0;
    case WM_COPYDATA: {
        auto* cds = (COPYDATASTRUCT*)lp;
        if (cds && (cds->dwData == 2 || cds->dwData == 3) && cds->lpData && cds->cbData >= 2 && cds->cbData <= 4096) {
            std::wstring s((const wchar_t*)cds->lpData, cds->cbData / sizeof(wchar_t));
            while (!s.empty() && s.back() == 0) s.pop_back();
            return cds->dwData == 2 ? g_pet->apiSay(s) : g_pet->apiEmote(s);
        }
        if (cds && cds->dwData == 1 && cds->lpData && cds->cbData >= 2 && cds->cbData <= 65536) {
            std::wstring path((const wchar_t*)cds->lpData, cds->cbData / sizeof(wchar_t));
            while (!path.empty() && path.back() == 0) path.pop_back();
            wchar_t me[MAX_PATH * 2];
            GetModuleFileNameW(nullptr, me, MAX_PATH * 2);
            UpdatePkg pk = _wcsicmp(path.c_str(), me) ? readExe(path) : UpdatePkg{};
            if (pk.ok && compareVersions(pk.version, UZI_VERSION) != 0) {
                g_pendingPkg = pk;
                g_pet->offerUpdate(pk.version, compareVersions(pk.version, UZI_VERSION));
            } else {
                g_pet->comeHere(cursorPos());
            }
        }
        return TRUE;
    }
    case WM_BATTERIES:
        g_pet->sayCat("batteryHint", 1.f, L"", true);
        return 0;
    case WM_COMEHERE:
        g_pet->comeHere(cursorPos());
        return 0;
    case WM_FILEDROP:
        if (!g_dropPath.empty()) {
            std::wstring path = g_dropPath;
            g_dropPath.clear();
            logf("file drop: handled %.1f ms after Drop", (nowSec() - g_dropAt) * 1000.0);
            g_pet->dropAt = g_dropAt;
            g_pet->receiveFile(path, g_dropN);
        }
        return 0;
    case WM_COMMAND:
        onCommand(LOWORD(wp));
        return 0;
    case WM_TIMER:
        if (wp == kExtDragTimer) {
            static double last = 0;
            double now = nowSec();
            if (now - last > 1.0 / 70.0) {
                a.now = now;
                frame((float)clampf((float)(now - last), 0.f, 1.f / 20.f));
                last = now;
            }
            return 0;
        }
        break;
    case WM_CLIPBOARDUPDATE:
        a.activity.onClipboard();
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        a.world.refresh();
        if (msg == WM_DISPLAYCHANGE && g_pet) g_pet->deskDisplayChanged();
        return 0;
    case WM_DESTROY:
        g_pet->vit.save();
        g_pet->daySave();
        a.phrases.saveState();
        RevokeDragDrop(h);
        RemoveClipboardFormatListener(h);
        if (g_nameWnd) DestroyWindow(g_nameWnd);
        g_pet->deskHomeNow("exit");
        g_pet->dropCarried(false);
        a.icons.flush(2.0);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_quit = true;
        PostQuitMessage(0);
        return 0;
    }
    if (msg == g_msgTaskbarCreated && g_msgTaskbarCreated) {
        addTray();
        if (a.icons.lv && !IsWindow(a.icons.lv)) a.icons.lost = true;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static std::wstring dropPath(IDataObject* d) {
    std::wstring out;
    FORMATETC fe{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM sm{};
    if (!d || d->GetData(&fe, &sm) != S_OK) return out;
    if (HDROP h = (HDROP)GlobalLock(sm.hGlobal)) {
        wchar_t path[MAX_PATH * 2] = L"";
        if (DragQueryFileW(h, 0, path, MAX_PATH * 2)) out = path;
        GlobalUnlock(sm.hGlobal);
    }
    ReleaseStgMedium(&sm);
    return out;
}

class DropTarget : public IDropTarget {
    LONG ref = 1;
    bool ok = false;
    bool battery = false;

public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *ppv = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&ref);
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* d, DWORD, POINTL, DWORD* eff) override {
        FORMATETC fe{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        ok = d && d->QueryGetData(&fe) == S_OK && !g_pet->clickThrough() && !g_pet->extDragging;
        battery = ok && _wcsicmp(dropPath(d).c_str(), batteryLinkPath().c_str()) == 0;
        *eff = ok ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        if (ok && !g_pet->heldDragging) {
            if (battery) g_pet->batteryHover(true);
            else g_pet->fileHover(true);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* eff) override {
        *eff = ok ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        POINT pt;
        GetCursorPos(&pt);
        if (g_pet->heldDragging && WindowFromPoint(pt) != g_app.hwnd) g_pet->heldDragLeft();
        if (ok) {
            g_pet->fileHover(false);
            g_pet->batteryHover(false);
        }
        ok = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* d, DWORD, POINTL, DWORD* eff) override {
        *eff = DROPEFFECT_NONE;
        if (!ok) return S_OK;
        ok = false;
        g_pet->fileHover(false);
        if (g_pet->heldDragging) {
            g_pet->heldRetake = true;
            *eff = DROPEFFECT_COPY;
            return S_OK;
        }
        if (battery) {
            battery = false;
            g_pet->batteryDrop();
            *eff = DROPEFFECT_COPY;
            return S_OK;
        }
        FORMATETC fe{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM sm{};
        if (d->GetData(&fe, &sm) != S_OK) return S_OK;
        if (HDROP h = (HDROP)GlobalLock(sm.hGlobal)) {
            UINT n = DragQueryFileW(h, 0xFFFFFFFF, nullptr, 0);
            wchar_t path[MAX_PATH * 2] = L"";
            if (n > 0 && DragQueryFileW(h, 0, path, MAX_PATH * 2)) {
                std::wstring ps = path;
                bool zip = ps.size() > 4 && _wcsicmp(ps.c_str() + ps.size() - 4, L".zip") == 0;
                bool exe = ps.size() > 4 && _wcsicmp(ps.c_str() + ps.size() - 4, L".exe") == 0;
                UpdatePkg pkg;
                if (zip) pkg = readPackage(ps);
                if (exe) pkg = readExe(ps);
                if (pkg.ok) {
                    g_pendingPkg = pkg;
                    g_pet->offerUpdate(pkg.version, compareVersions(pkg.version, UZI_VERSION));
                } else {
                    g_dropPath = path;
                    g_dropN = (int)n;
                    g_dropAt = nowSec();
                    PostMessageW(g_app.hwnd, WM_FILEDROP, 0, 0);
                }
                *eff = DROPEFFECT_COPY;
            }
            GlobalUnlock(sm.hGlobal);
        }
        ReleaseStgMedium(&sm);
        return S_OK;
    }
};

static HWND g_flashWnd = nullptr;
static Surface g_flashSurf;
static int g_flashMon = -1;

static void flashFrame(float dt) {
    App& a = g_app;
    if (a.flash <= 0 || a.world.mons.empty()) {
        if (g_flashWnd && IsWindowVisible(g_flashWnd)) ShowWindow(g_flashWnd, SW_HIDE);
        a.flash = 0;
        return;
    }
    int m = a.world.monitorAt(a.flashAt);
    if (m < 0) m = a.world.nearestMonitor(a.flashAt);
    const RECT& mr = a.world.mons[m].rc;
    RECT r{mr.left + 1, mr.top + 1, mr.right - 1, mr.bottom - 1};
    int w = r.right - r.left, h = r.bottom - r.top;
    if (!g_flashWnd) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"UziFlashWnd";
        RegisterClassExW(&wc);
        g_flashWnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"UziFlashWnd",
                                     L"", WS_POPUP, r.left, r.top, w, h, nullptr, nullptr, wc.hInstance, nullptr);
    }
    if (m != g_flashMon || g_flashSurf.w != w || g_flashSurf.h != h) {
        g_flashSurf.create(a.gfx, w, h);
        float edge = std::min(w, h) * 0.13f;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                float d = (float)std::min(std::min(x, w - 1 - x), std::min(y, h - 1 - y)) / edge;
                float v = clampf(1.f - d, 0.f, 1.f);
                float al = v * v * 0.55f;
                uint8_t A = (uint8_t)(al * 255), R = (uint8_t)(al * 215);
                g_flashSurf.bits[(size_t)y * w + x] = ((uint32_t)A << 24) | ((uint32_t)R << 16) | ((uint32_t)(al * 20) << 8) | (uint32_t)(al * 40);
            }
        g_flashMon = m;
    }
    BLENDFUNCTION bf{AC_SRC_OVER, 0, (BYTE)(clampf(a.flash, 0.f, 1.f) * 255), AC_SRC_ALPHA};
    POINT pos{r.left, r.top}, src{0, 0};
    SIZE sz{w, h};
    UpdateLayeredWindow(g_flashWnd, nullptr, &pos, &sz, g_flashSurf.dc, &src, 0, &bf, ULW_ALPHA);
    if (!IsWindowVisible(g_flashWnd)) {
        ShowWindow(g_flashWnd, SW_SHOWNOACTIVATE);
        SetWindowPos(a.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    a.flash -= dt / 0.45f;
}

static void iconsRevive(float dt) {
    App& a = g_app;
    static float t = 0;
    t -= dt;
    if (t > 0) return;
    t = 2.f;
    auto& I = a.icons;
    if (!I.fake && I.lv && !IsWindow(I.lv)) I.lost = true;
    if (!I.fake && !I.ok() && !I.lost) {
        static int tries = 0;
        if (tries++ >= 30) t = 30.f;
        if (!FindWindowW(L"Progman", nullptr) || !I.init()) return;
        logf("icons: desktop view is up now (after %d tries, %zu icons)", tries, I.items.size());
        I.recoverSnap();
        a.iconsReady = !I.autoArrange();
        I.restoreCarry();
        return;
    }
    if (!I.lost) return;
    if (I.fake) {
        if (I.fakeDead) return;
    } else {
        if (!FindWindowW(L"Progman", nullptr)) return;
        bool ok = I.init();
        logf("icons: explorer view lost, init again: %s (%zu)", ok ? "ok" : "failed", I.items.size());
        if (!ok) {
            I.lost = true;
            return;
        }
        a.iconsReady = !I.autoArrange();
    }
    I.lost = false;
    g_pet->dropCarried(false);
    if (g_pet->deskOn()) g_pet->deskHomeNow("explorer restart");
    I.restoreCarry();
}

static void refreshIconSurfs() {
    App& a = g_app;
    if (!a.settings.icons || !a.iconsReady || a.world.web) return;
    std::vector<IconBox> boxes;
    for (size_t i = 0; i < a.icons.items.size(); i++) {
        const DIcon& ic = a.icons.items[i];
        if (ic.name == g_pet->carriedName || g_pet->deskHides(ic)) continue;
        if (a.world.rectVisible(ic.img)) boxes.push_back({(int)i, ic.img});
    }
    a.world.addIconSurfs(std::move(boxes), a.icons.spacing.x);
}

static std::function<void(DrawCtx&, V2)> g_snapUnder;

static void renderFrame(Surface& surf, V2 wo, D2D1_COLOR_F clear = {0, 0, 0, 0}, const RECT* dirty = nullptr) {
    App& a = g_app;
    auto* rt = surf.begin(clear, dirty);
    DrawCtx c{rt, a.gfx.d2d.Get(), a.gfx.dw.Get(), surf.brush.Get(), a.gfx.round.Get(), surf.gen, a.s};
    c.fonts = a.gfx.ui.Get();
    c.family = a.gfx.uiFamily.c_str();
    if (g_snapUnder) g_snapUnder(c, wo);
    g_pet->render(c, wo);
    D2D1_RECT_F area = D2D1::RectF(-1e5f, -1e5f, 1e5f, 1e5f);
    if (!a.world.mons.empty()) {
        int m = a.world.monitorAt(g_pet->headC);
        if (m < 0) m = a.world.nearestMonitor(g_pet->headC);
        const RECT& w = a.world.mons[m].work;
        area = D2D1::RectF(w.left - wo.x, a.world.mons[m].rc.top - wo.y, w.right - wo.x, w.bottom - wo.y);
    }
    DrawCtx bc = c;
    bc.s = a.dpi * (1.f + (a.settings.scale - 1.f) * 0.35f);
    a.bubble.draw(bc, g_pet->bubbleTip() - wo, g_pet->headC - wo, g_pet->headR, area, D2D1::RectF(0, 0, (float)surf.w, (float)surf.h));
    if (g_dbgParts && g_pet->active == 0) {
        const Rig& R = a.rig;
        for (size_t i = 0; i < R.parts.size() && i < a.inst.world.size(); i++) {
            const PartDef& d = R.parts[i];
            rt->SetTransform(a.inst.world[i]);
            c.color(D2D1::ColorF(0.1f, 0.85f, 1.f, 1), 0.9f);
            rt->DrawRectangle(D2D1::RectF(d.pos.x, d.pos.y, d.pos.x + d.size.x, d.pos.y + d.size.y), c.br, 1.f);
            c.color(D2D1::ColorF(1.f, 0.3f, 0.6f, 1), 1.f);
            rt->FillEllipse(D2D1::Ellipse(d.joint.d2(), 2.5f, 2.5f), c.br);
        }
        rt->SetTransform(D2D1::Matrix3x2F::Identity());
    }
    if (g_dbgPerf) {
        static ComPtr<IDWriteTextFormat> f;
        if (!f) a.gfx.dw->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.f,
                                           L"ru-ru", &f);
        wchar_t buf[96];
        swprintf(buf, 96, L"%.2f мс · %s", g_frameMs, widen(std::string(g_pet->stateName())).c_str());
        V2 p = g_pet->pos - wo + V2(-60.f, 6.f);
        c.color(D2D1::ColorF(0, 0, 0, 1), 0.6f);
        rt->FillRectangle(D2D1::RectF(p.x - 4, p.y - 2, p.x + 150, p.y + 16), c.br);
        c.color(D2D1::ColorF(0.6f, 1.f, 0.7f, 1), 1.f);
        if (f) rt->DrawTextW(buf, (UINT32)wcslen(buf), f.Get(), D2D1::RectF(p.x, p.y, p.x + 300, p.y + 20), c.br);
    }
    surf.end(a.gfx);
}

static void byeStep(float dt) {
    App& a = g_app;
    Pet& p = *g_pet;
    Input in;
    in.cursor = p.lastCursor;
    in.idleSec = 30;
    p.update(dt, in);
    a.bubble.update(dt);
    a.winOrigin = p.windowOrigin();
    a.bubble.mouse = in.cursor - a.winOrigin;
    p.stepRig(dt, a.winOrigin);
}

static void byePresent() {
    App& a = g_app;
    V2 wo = g_pet->windowOrigin();
    a.winOrigin = wo;
    renderFrame(g_surf, wo);
    g_surf.present(a.hwnd, POINT{(LONG)wo.x, (LONG)wo.y});
}

static bool sessionBye(bool live) {
    App& a = g_app;
    double t0 = nowSec();
    if (!g_pet->byeShutdown()) return false;
    for (int i = 0; i < 60; i++) byeStep(1.f / 60.f);
    a.bubble.t = std::max(a.bubble.t, a.bubble.typeDur + 0.3f);
    if (live) byePresent();
    logf("shutdown bye shown in %.1f ms", (nowSec() - t0) * 1000.0);
    return true;
}

static void byeHold(float secs) {
    double t0 = nowSec(), last = t0;
    while (nowSec() - t0 < secs) {
        double now = nowSec();
        g_app.now = now;
        byeStep((float)clampf((float)(now - last), 0.f, 1.f / 20.f));
        last = now;
        byePresent();
        Sleep(16);
    }
}

static void frame(float dt) {
    App& a = g_app;
    static float worldT = 0, iconsT = 0, fsT = 0, topT = 0, perfT = 0;
    static double perfAcc = 0, pWorld = 0, pUpd = 0, pDraw = 0, pPres = 0, pMax = 0, pIcons = 0;
    static int perfN = 0;
    double t0 = nowSec();
    double fWorld = 0, fSurfs = 0, fIcons = 0;
    int jumps0 = g_pet->perfJumpPlans;
    double jumpS0 = g_pet->perfJumpSec;

    worldT -= dt;
    if (worldT <= 0) {
        worldT = (g_pet->st == St::Fall || g_pet->st == St::Air || g_pet->st == St::Dragged || g_pet->st == St::PrepJump) ? 0.1f : 0.25f;
        double w0 = nowSec();
        a.world.refresh();
        double w1 = nowSec();
        refreshIconSurfs();
        fWorld = w1 - w0;
        fSurfs = nowSec() - w1;
        pWorld += fWorld + fSurfs;
    }
    iconsRevive(dt);
    a.icons.pump(a.now);
    g_pet->deskRetryHands();
    iconsT -= dt;
    if (iconsT <= 0 && a.iconsReady && a.settings.icons && !g_pet->game) {
        iconsT = 60.f;
        double i0 = nowSec();
        bool fresh = a.icons.refresh();
        fIcons = nowSec() - i0;
        pIcons += fIcons;
        static int logged = 0;
        if (logged++ < 3 || fIcons > 0.015) logf("icons refresh: %zu items in %.1f ms", a.icons.items.size(), fIcons * 1000.0);
        if (fresh && !g_pet->deskOn() && g_pet->carriedName.empty() && !a.icons.pendingAny()) a.icons.layoutIdle(a.now);
    }
    {
        LASTINPUTINFO li0{sizeof(li0)};
        GetLastInputInfo(&li0);
        a.activity.update(dt, a.now, (GetTickCount() - li0.dwTime) / 1000.0, a.world);
    }
    flashFrame(dt);
    itemsFrame(a, *g_pet, dt);
    if (g_introPending && !a.world.mons.empty()) {
        g_introPending = false;
        introStart(a, *g_pet, false);
    }
    introFrame(a, *g_pet, dt);
    gameSenseFrame();
    batteryLinkUpdate(a, *g_pet, dt);
    if (g_pet->wantUpdate) {
        g_updateT += dt;
        if (g_updateT > 2.5f) {
            g_pet->wantUpdate = false;
            g_updateT = 0;
            g_pet->vit.save();
            g_pet->deskHomeNow("update");
            g_pet->dropCarried(false);
            a.icons.flush(1.0);
            bool isExe = g_pendingPkg.zip.size() > 4 && _wcsicmp(g_pendingPkg.zip.c_str() + g_pendingPkg.zip.size() - 4, L".exe") == 0;
            if (isExe ? applyExeUpdate(g_pendingPkg.zip) : applyUpdate(g_pendingPkg)) {
                DestroyWindow(a.hwnd);
                return;
            }
            g_pet->sayCat("updateFail", 1.f, L"", true);
        }
    }
    if (g_pet->vit.inBin) {
        if (IsWindowVisible(a.hwnd)) ShowWindow(a.hwnd, SW_HIDE);
        binWatch(dt);
        return;
    }
    bool flashing = a.flash > 0 || (g_flashWnd && IsWindowVisible(g_flashWnd));
    fsT -= dt;
    if (fsT <= 0 && !flashing) {
        fsT = 0.5f;
        QUERY_USER_NOTIFICATION_STATE q;
        bool fs0 = SUCCEEDED(SHQueryUserNotificationState(&q)) &&
                   (q == QUNS_BUSY || q == QUNS_RUNNING_D3D_FULL_SCREEN || q == QUNS_PRESENTATION_MODE);
        bool raw = !g_settings.visible() && !introOverlayShown() && ((fs0 && !a.activity.capture) || (a.activity.fgCovers && a.activity.kind == AppKind::Game));
        static float rawT = 0;
        rawT = raw ? rawT + 0.5f : 0.f;
        bool fs = g_pet->shouldHide(rawT >= 1.5f);
        if (fs != g_hidden) {
            g_hidden = fs;
            logf("fullscreen app: %d", fs);
            if (fs) {
                if (g_dragging) {
                    g_dragging = g_press = false;
                    ReleaseCapture();
                    g_pet->endDrag();
                }
                g_pet->deskHomeNow("fullscreen");
                g_pet->dropCarried(false);
                ShowWindow(a.hwnd, SW_HIDE);
            } else {
                ShowWindow(a.hwnd, SW_SHOWNOACTIVATE);
            }
        }
    }
    if (g_hidden) return;
    if (g_testVisit > 0) {
        static double t0 = a.now;
        if (a.now - t0 > g_testVisit && a.web && a.web->extKnown()) {
            g_testVisit = 0;
            g_pet->webGo();
        } else if (a.now - t0 > 60) {
            g_testVisit = 0;
            logf("web visit test: extension never connected");
        }
    }

    Input in;
    in.cursor = cursorPos();
    in.lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    LASTINPUTINFO li{sizeof(li)};
    GetLastInputInfo(&li);
    in.idleSec = (GetTickCount() - li.dwTime) / 1000.0;

    double u0 = nowSec();
    g_pet->update(dt, in);
    static bool clickThrough = false;
    if (g_pet->clickThrough() != clickThrough) {
        clickThrough = g_pet->clickThrough();
        LONG ex = GetWindowLongW(a.hwnd, GWL_EXSTYLE);
        SetWindowLongW(a.hwnd, GWL_EXSTYLE, clickThrough ? (ex | WS_EX_TRANSPARENT) : (ex & ~WS_EX_TRANSPARENT));
    }
    {
        static bool aimWas = false;
        bool aim = kissAim();
        g_pet->kissAim = aim;
        if (aim != aimWas) {
            POINT pt;
            GetCursorPos(&pt);
            if (WindowFromPoint(pt) == a.hwnd) SetCursor(aim ? lipsCursor() : LoadCursor(nullptr, IDC_HAND));
            aimWas = aim;
        }
    }
    a.bubble.update(dt);
    a.bubble.dark = a.settings.bubbleDark;
    {
        static bool prevL = false, prevR = false;
        bool l = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0, r = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        if (((l && !prevL) || (r && !prevR)) && a.bubble.asking() && (a.bubble.askId == "talk" || a.bubble.askId == "talkSleep" || a.bubble.askId == "sleepArgue" || a.bubble.askId == "games") &&
            !a.bubble.hit(cursorPos() - a.winOrigin))
            a.bubble.close();
        if (g_pet->feeding && ((l && !prevL) || (r && !prevR) || (GetAsyncKeyState(VK_ESCAPE) & 0x8000))) {
            if (l && !prevL && (cursorPos() - g_pet->mouthScreen()).len() < g_pet->headR * 1.6f) {
                g_pet->eatBattery(false);
            } else {
                g_pet->feeding = false;
                g_pet->sayCat("feedCancel", 1.f, L"", true);
            }
        }
        prevL = l;
        prevR = r;
    }
    if (g_pet->wantSettings) {
        g_pet->wantSettings = false;
        openSettings();
    }
    V2 wo = g_pet->windowOrigin();
    a.winOrigin = wo;
    a.bubble.mouse = in.cursor - wo;
    a.bubble.hoverKeep = !a.world.fake;
    g_pet->stepRig(dt, wo);
    static RECT prevB{0, 0, 0, 0};
    static bool full = true;
    D2D1_RECT_F b = g_pet->bounds(wo);
    if (a.bubble.active) {
        b.left = std::min(b.left, a.bubble.rect.left - 30.f * a.s);
        b.top = std::min(b.top, a.bubble.rect.top - 30.f * a.s);
        b.right = std::max(b.right, a.bubble.rect.right + 30.f * a.s);
        b.bottom = std::max(b.bottom, a.bubble.rect.bottom + 30.f * a.s);
        if (a.bubble.t < 0.1f) full = true;
    }
    RECT cur{(LONG)floorf(b.left) - 6, (LONG)floorf(b.top) - 6, (LONG)ceilf(b.right) + 6, (LONG)ceilf(b.bottom) + 6};
    RECT dirty;
    UnionRect(&dirty, &cur, &prevB);
    RECT whole{0, 0, g_surf.w, g_surf.h};
    if (full || g_dbgPerf || g_dbgParts) dirty = whole;
    IntersectRect(&dirty, &dirty, &whole);
    full = false;
    prevB = cur;
    double d0 = nowSec();
    renderFrame(g_surf, wo, {0, 0, 0, 0}, &dirty);
    g_pet->hitboxPublish(wo);
    double p0 = nowSec();
    g_surf.present(a.hwnd, POINT{(LONG)wo.x, (LONG)wo.y}, &dirty);
    double p1 = nowSec();
    pUpd += d0 - u0;
    pDraw += p0 - d0;
    pPres += p1 - p0;

    topT -= dt;
    if (topT <= 0) {
        topT = 2.f;
        if (!introRunning()) SetWindowPos(a.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    double fAll = nowSec() - t0;
    perfAcc += fAll;
    pMax = std::max(pMax, fAll);
    perfN++;
    perfT += dt;
    static double lastSpike = 0;
    if (fAll > 0.025 && a.now - lastSpike > 1.0) {
        lastSpike = a.now;
        logf("perf spike: %.1f ms (world %.1f, icon surfs %.1f, icons refresh %.1f, update %.1f, draw %.1f, present %.1f, jump arcs %d in %.1f ms), state %s, game %d",
             fAll * 1000.0, fWorld * 1000.0, fSurfs * 1000.0, fIcons * 1000.0, (d0 - u0) * 1000.0, (p0 - d0) * 1000.0, (p1 - p0) * 1000.0,
             g_pet->perfJumpPlans - jumps0, (g_pet->perfJumpSec - jumpS0) * 1000.0, g_pet->stateName(), (int)(g_pet->game != nullptr));
    }
    if (perfT > 10.f) {
        logf("perf: %.2f ms/frame (world %.2f, update %.2f, draw %.2f, present %.2f), worst %.1f ms, icons refresh %.1f ms, %d frames, face redraws %d, state %s, pos %.0f,%.0f, surfs %zu",
             perfAcc / perfN * 1000.0, pWorld / perfN * 1000.0, pUpd / perfN * 1000.0, pDraw / perfN * 1000.0, pPres / perfN * 1000.0, pMax * 1000.0,
             pIcons * 1000.0, perfN, a.inst.faceRedraws, g_pet->stateName(), g_pet->pos.x, g_pet->pos.y, a.world.surfs.size());
        a.inst.faceRedraws = 0;
        perfT = 0;
        perfAcc = pWorld = pUpd = pDraw = pPres = pMax = pIcons = 0;
        perfN = 0;
    }
}

int cleanGuardSelfTest();

static void snapshot(const std::wstring& dir) {
#include "tests/snap_basics.inl"
#include "tests/snap_selfsense.inl"
#include "tests/snap_life.inl"
#include "tests/snap_mood.inl"
#include "tests/snap_dates_solver.inl"
#include "tests/snap_away.inl"
#include "tests/snap_games.inl"
#include "tests/snap_hunger.inl"
#include "tests/snap_extension.inl"
#include "tests/snap_desk.inl"
#include "tests/snap_webnest.inl"
#include "tests/snap_browser.inl"
#include "tests/snap_cs2.inl"
#include "tests/snap_roblox.inl"
#include "tests/snap_kiss.inl"
#include "tests/snap_corner.inl"
#include "tests/snap_miss.inl"
#include "tests/snap_emote.inl"
#include "tests/snap_alive.inl"
#include "tests/snap_faces_bench.inl"
}

static LRESULT CALLBACK NameProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            wchar_t buf[64] = L"";
            GetDlgItemTextW(h, 300, buf, 64);
            std::wstring n = buf;
            while (!n.empty() && iswspace(n.back())) n.pop_back();
            while (!n.empty() && iswspace(n.front())) n.erase(n.begin());
            if (n.size() > 24) n.resize(24);
            if (!n.empty() && n != g_app.settings.name) {
                g_app.settings.name = n;
                g_app.settings.save();
                g_app.phrases.userName = n;
                g_pet->nameChanged();
            }
            DestroyWindow(h);
            return 0;
        }
        if (LOWORD(wp) == IDCANCEL) {
            DestroyWindow(h);
            return 0;
        }
        break;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        g_nameWnd = nullptr;
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static void showNameDialog() {
    if (g_nameWnd) {
        SetForegroundWindow(g_nameWnd);
        return;
    }
    static bool reg = false;
    HINSTANCE hi = GetModuleHandleW(nullptr);
    if (!reg) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = NameProc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        wc.lpszClassName = L"UziNameWnd";
        wc.hIcon = g_trayIcon;
        RegisterClassExW(&wc);
        reg = true;
    }
    UINT dpi = GetDpiForSystem();
    auto D = [&](int v) { return MulDiv(v, dpi, 96); };
    RECT rc{0, 0, D(332), D(124)};
    AdjustWindowRectExForDpi(&rc, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME, dpi);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int w = rc.right - rc.left, hgt = rc.bottom - rc.top;
    g_nameWnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"UziNameWnd", L"Узи", WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                (wa.left + wa.right - w) / 2, (wa.top + wa.bottom - hgt) / 2, w, hgt, nullptr, nullptr, hi, nullptr);
    static HFONT font = nullptr;
    if (!font)
        font = CreateFontW(-D(15), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    auto mk = [&](DWORD ex, const wchar_t* cls, const wchar_t* txt, DWORD style, int x, int y, int cw, int ch, int id) {
        HWND c = CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | style, D(x), D(y), D(cw), D(ch), g_nameWnd, (HMENU)(INT_PTR)id, hi, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
        return c;
    };
    mk(0, L"STATIC", L"Как Узи тебя называть?", 0, 16, 14, 300, 22, 0);
    HWND ed = mk(WS_EX_CLIENTEDGE, L"EDIT", g_app.settings.name.c_str(), WS_TABSTOP | ES_AUTOHSCROLL, 16, 40, 300, 26, 300);
    mk(0, L"BUTTON", L"Готово", WS_TABSTOP | BS_DEFPUSHBUTTON, 140, 82, 84, 28, IDOK);
    mk(0, L"BUTTON", L"Отмена", WS_TABSTOP, 232, 82, 84, 28, IDCANCEL);
    SendMessageW(ed, EM_LIMITTEXT, 24, 0);
    ShowWindow(g_nameWnd, SW_SHOW);
    SetForegroundWindow(g_nameWnd);
    SetFocus(ed);
    SendMessageW(ed, EM_SETSEL, 0, -1);
}

static void crashUnhideOnce() {
    static LONG once = 0;
    if (!InterlockedExchange(&once, 1) && g_pet && !g_app.icons.fake) g_pet->deskCrashHome();
}

static LONG WINAPI crashFilter(EXCEPTION_POINTERS*) {
    crashUnhideOnce();
    return EXCEPTION_CONTINUE_SEARCH;
}

static void installCrashHooks() {
    SetUnhandledExceptionFilter(crashFilter);
    signal(SIGABRT, [](int) { crashUnhideOnce(); });
    std::set_terminate([] {
        crashUnhideOnce();
        abort();
    });
    _set_purecall_handler([] {
        crashUnhideOnce();
        abort();
    });
    _set_invalid_parameter_handler([](const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
        crashUnhideOnce();
        __fastfail(FAST_FAIL_INVALID_ARG);
    });
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    OleInitialize(nullptr);
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring snapDir;
    bool iconsTest = false, webServe = false;
    std::wstring moveTest;
    bool batteriesArg = false, updatedArg = false;
    DWORD waitPid = 0;
    std::wstring installNow, installExeNow, iconOut;
    for (int i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"--icons-move") && i + 1 < argc) moveTest = argv[++i];
        if (!wcscmp(argv[i], L"--snapshot") && i + 1 < argc) snapDir = argv[++i];
        if (!wcscmp(argv[i], L"--icons")) iconsTest = true;
        if (!wcscmp(argv[i], L"--icons-save") && i + 1 < argc) { iconsTest = true; g_iconsSave = argv[++i]; }
        if (!wcscmp(argv[i], L"--icons-apply") && i + 1 < argc) { iconsTest = true; g_iconsApply = argv[++i]; }
        if (!wcscmp(argv[i], L"--icons-hidetest") && i + 1 < argc) { iconsTest = true; g_iconsHideTest = argv[++i]; }
        if (!wcscmp(argv[i], L"--web-serve")) webServe = true;
        if (!wcscmp(argv[i], L"--web-visit")) g_testVisit = 10;
        if (!wcscmp(argv[i], L"--batteries")) batteriesArg = true;
        if (!wcscmp(argv[i], L"--hitbox")) return hitboxPrint();
        if ((!wcscmp(argv[i], L"--say") || !wcscmp(argv[i], L"--emote")) && i + 1 < argc) {
            HWND other = FindWindowW(L"UziPetWnd", nullptr);
            if (!other) return 1;
            std::wstring s = argv[i + 1];
            COPYDATASTRUCT cds{(ULONG_PTR)(argv[i][2] == L's' ? 2 : 3), (DWORD)((s.size() + 1) * sizeof(wchar_t)), (void*)s.c_str()};
            DWORD_PTR r = 0;
            if (!SendMessageTimeoutW(other, WM_COPYDATA, 0, (LPARAM)&cds, SMTO_ABORTIFHUNG, 3000, &r)) return 2;
            return r ? 0 : 2;
        }
        if (!wcscmp(argv[i], L"--updated")) updatedArg = true;
        if (!wcscmp(argv[i], L"--make-icon") && i + 1 < argc) iconOut = argv[++i];
        if (!wcscmp(argv[i], L"--wait-pid") && i + 1 < argc) waitPid = (DWORD)_wtoi(argv[++i]);
        if (!wcscmp(argv[i], L"--install-now") && i + 1 < argc) installNow = argv[++i];
        if (!wcscmp(argv[i], L"--install-exe-now") && i + 1 < argc) installExeNow = argv[++i];
        if (!wcscmp(argv[i], L"--rbx-fetch") && i + 2 < argc) {
            long long u = _wtoi64(argv[i + 1]), pl = _wtoi64(argv[i + 2]);
            RbxName n;
            bool ok = rbxFetchGame(u, n);
            long long pu = rbxFetchUniverse(pl);
            logf("rbx-fetch: universe %lld -> %d '%s' [%s]; place %lld -> universe %lld", u, (int)ok, n.name.c_str(), n.genre.c_str(), pl, pu);
            return ok && pu ? 0 : 1;
        }
    }
    for (int i = 1; i + 1 < argc; i++)
        if (!wcscmp(argv[i], L"--icons-bench")) {
            std::wstring bd = argv[i + 1];
            CreateDirectoryW(bd.c_str(), nullptr);
            setAppDataDir(bd);
            DesktopIcons I;
            if (!I.init()) {
                logf("icons bench: init failed");
                return 2;
            }
            for (int k = 0; k < 2; k++) {
                I.viaView = k == 0;
                double all = 0, worst = 0;
                for (int r = 0; r < 6; r++) {
                    double t0 = nowSec();
                    I.refresh();
                    double ms = (nowSec() - t0) * 1000.0;
                    all += ms;
                    worst = std::max(worst, ms);
                }
                POINT pp{};
                double t1 = nowSec();
                for (int r = 0; r < 20 && !I.items.empty(); r++) I.itemPos(I.items[r % I.items.size()], pp);
                logf("icons bench: %s: %zu items, refresh %.1f ms avg, %.1f ms worst; one position %.3f ms", k == 0 ? "names from the explorer view (old)"
                     : "names in-process (new)", I.items.size(), all / 6, worst, (nowSec() - t1) * 1000.0 / 20);
            }
            logf("icons bench: list style 0x%08lX, snap %d, grid origin %ld,%ld", (unsigned long)GetWindowLongW(I.lv, GWL_STYLE), (int)I.snapOn(),
                 I.gridOrigin().x, I.gridOrigin().y);
            return 0;
        }
    if (!snapDir.empty()) {
        CreateDirectoryW(snapDir.c_str(), nullptr);
        setAppDataDir(snapDir + L"\\appdata");
    }

    if (waitPid) {
        if (HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, waitPid)) {
            WaitForSingleObject(h, 15000);
            CloseHandle(h);
        }
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    cleanupOldExe();
    if (snapDir.empty() && !iconsTest && moveTest.empty() && installNow.empty() && installExeNow.empty() && iconOut.empty()) {
        if (!FindWindowW(L"UziPetWnd", nullptr) && selfInstall()) return 0;
        refreshAssets(updatedArg);
    }
    if (!installExeNow.empty()) {
        UpdatePkg pk = readExe(installExeNow);
        bool ok = pk.ok && applyExeUpdate(installExeNow);
        logf("install-exe-now: %d", (int)ok);
        return ok ? 0 : 1;
    }
    if (!installNow.empty()) {
        UpdatePkg pk = readPackage(installNow);
        bool ok = pk.ok && applyUpdate(pk);
        logf("install-now: %d", (int)ok);
        return ok ? 0 : 1;
    }
    HANDLE mutex = nullptr;
    if (snapDir.empty() && !iconsTest && moveTest.empty() && iconOut.empty()) {
        mutex = CreateMutexW(nullptr, TRUE, L"Local\\UziPet.Single");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            if (HWND other = FindWindowW(L"UziPetWnd", nullptr)) {
                if (batteriesArg) {
                    PostMessageW(other, WM_BATTERIES, 0, 0);
                } else {
                    wchar_t me[MAX_PATH * 2];
                    GetModuleFileNameW(nullptr, me, MAX_PATH * 2);
                    COPYDATASTRUCT cds{1, (DWORD)((wcslen(me) + 1) * sizeof(wchar_t)), me};
                    SendMessageW(other, WM_COPYDATA, 0, (LPARAM)&cds);
                }
                return 0;
            }
            DWORD w = WaitForSingleObject(mutex, 10000);
            logf("previous instance was closing: wait result %lu", w);
        }
    }

    App& a = g_app;
    a.assets = exeDir() + L"\\assets";
    if (GetFileAttributesW((a.assets + L"\\rig.json").c_str()) == INVALID_FILE_ATTRIBUTES) a.assets = exeDir() + L"\\..\\assets";
    a.settings.load();
    if (!a.gfx.init()) return 1;
    a.gfx.loadUiFont(a.assets + L"\\fonts\\Nunito.ttf", L"Nunito");
    if (!iconOut.empty()) return makeBatteryIcon(a, 5, iconOut) ? 0 : 1;
    a.dpi = snapDir.empty() ? GetDpiForSystem() / 96.f : 1.25f;
    if (GetFileAttributesW((a.assets + L"\\phrases").c_str()) != INVALID_FILE_ATTRIBUTES) a.phrases.load(a.assets + L"\\phrases");
    else a.phrases.load(a.assets + L"\\phrases.json");
    a.phrases.userName = a.settings.name;
    a.phrases.gender = a.settings.gender;
    talksLoad(a.assets + L"\\talks.json");
    if (snapDir.empty()) a.phrases.loadState(appDataDir() + L"\\said.txt");
    a.renderFace = renderFacePng;
    a.renderSelf = renderSelfPhoto;
    Pet pet(a);
    g_pet = &pet;
    if (snapDir.empty()) {
        pet.vit.load();
        pet.offlineCatchUp();
        g_introPending = pet.vit.fresh || pet.vit.intro == 1;
        if (!g_introPending) pet.missArm();
    } else {
        pet.vit.persist = false;
    }
    a.notify = [](const std::wstring& t, const std::wstring& x) { trayNotify(t, x); };
    if (updatedArg) {
        pet.notesQueue.push_back(a.phrases.pick("updated", UZI_VERSION));
        for (auto& n : installedNotes()) pet.notesQueue.push_back(n);
        pet.greeted = true;
        logf("started after update to %s", narrow(UZI_VERSION).c_str());
    }
    a.pet = &pet;
    if (snapDir.empty()) installCrashHooks();
    if (!applyScale()) {
        MessageBoxW(nullptr, L"Не нашла assets\\rig.json или atlas.png рядом с exe.", L"Узи", MB_ICONERROR);
        return 1;
    }

    if (!moveTest.empty()) {
        a.icons.worksFn = [] { return g_app.world.works(); };
        a.world.refresh();
        if (!a.icons.init()) return 2;
        int i = a.icons.find(moveTest);
        if (i < 0) { logf("move test: no icon"); return 3; }
        POINT orig = a.icons.items[i].pos, got{};
        a.icons.put(a.icons.items[i], POINT{orig.x + a.icons.spacing.x, orig.y}, "move test", &got);
        a.icons.refresh();
        i = a.icons.find(moveTest);
        if (i < 0) return 3;
        logf("move test: orig %ld,%ld -> asked one cell right -> %ld,%ld, now at %ld,%ld", orig.x, orig.y, got.x, got.y, a.icons.items[i].pos.x,
             a.icons.items[i].pos.y);
        a.icons.put(a.icons.items[i], orig, "move test back");
        a.icons.refresh();
        i = a.icons.find(moveTest);
        if (i >= 0) logf("move test: restored to %ld,%ld, snap %d, blocked %d", a.icons.items[i].pos.x, a.icons.items[i].pos.y, a.icons.snapOn(), a.icons.badMoves);
        return 0;
    }
    if (iconsTest) {
        a.icons.worksFn = [] { return g_app.world.works(); };
        a.world.refresh();
        if (!a.icons.init()) { logf("icons: init failed"); return 2; }
        if (!g_iconsHideTest.empty()) {
            int i = a.icons.find(g_iconsHideTest);
            if (i < 0) { logf("hidetest: no icon"); return 3; }
            std::map<std::wstring, POINT> before;
            for (auto& d : a.icons.items) before[d.name] = d.pos;
            size_t n0 = a.icons.items.size();
            POINT p0 = a.icons.items[i].pos;
            DIcon ic = a.icons.items[i];
            bool rm = a.icons.viewHide(ic, true);
            Sleep(800);
            a.icons.refresh();
            int j = a.icons.find(g_iconsHideTest);
            logf("hidetest: remove %d, items %zu -> %zu, icon still listed %d", (int)rm, n0, a.icons.items.size(), (int)(j >= 0));
            Sleep(3200);
            bool ad = a.icons.viewHide(ic, false);
            Sleep(800);
            a.icons.refresh();
            j = a.icons.find(g_iconsHideTest);
            int moved = 0;
            for (auto& d : a.icons.items) {
                auto it = before.find(d.name);
                if (it != before.end() && d.name != g_iconsHideTest && (it->second.x != d.pos.x || it->second.y != d.pos.y)) moved++;
            }
            logf("hidetest: add %d, items %zu, icon back %d at %ld,%ld (was %ld,%ld), others moved %d", (int)ad, a.icons.items.size(), (int)(j >= 0),
                 j >= 0 ? a.icons.items[j].pos.x : 0, j >= 0 ? a.icons.items[j].pos.y : 0, p0.x, p0.y, moved);
            return 0;
        }
        if (!g_iconsSave.empty()) {
            std::string out;
            for (auto& d : a.icons.items) out += narrow(d.name) + "\t" + std::to_string(d.pos.x) + "\t" + std::to_string(d.pos.y) + "\n";
            FILE* f = _wfopen(g_iconsSave.c_str(), L"wb");
            if (!f) return 3;
            fwrite(out.data(), 1, out.size(), f);
            fclose(f);
            logf("icons-save: %zu icons -> %s", a.icons.items.size(), narrow(g_iconsSave).c_str());
            return 0;
        }
        if (!g_iconsApply.empty()) {
            int total = 0;
            int same = a.icons.layoutRestore(g_iconsApply, &total);
            logf("icons-apply: %d of %d exactly in place, snap %d, blocked %d", same, total, a.icons.snapOn(), a.icons.badMoves);
            return total ? 0 : 4;
        }
        logf("icons: %zu items, spacing %ldx%ld, icon %dpx, snap %d, auto %d", a.icons.items.size(), a.icons.spacing.x, a.icons.spacing.y,
             a.icons.iconPx, a.icons.snapOn(), a.icons.autoArrange());
        for (auto& d : a.icons.items)
            logf("  '%s' pos %ld,%ld cell %ld,%ld img %ld,%ld", narrow(d.name).c_str(), d.pos.x, d.pos.y, d.cell.left, d.cell.top, d.img.left,
                 d.img.top);
        a.world.refresh();
        a.world.dump();
        return 0;
    }
    if (!snapDir.empty()) {
        a.icons.testOnly = true;
        a.icons.worksFn = [] { return g_app.world.works(); };
        snapshot(snapDir);
        return 0;
    }

    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_HAND);
    wc.lpszClassName = L"UziPetWnd";
    RegisterClassExW(&wc);
    a.hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"UziPetWnd", L"Uzi", WS_POPUP, 0, 0,
                             a.winW, a.winH, nullptr, nullptr, hInst, nullptr);
    a.world.self = a.hwnd;
    a.activity.init(a.hwnd);
    AddClipboardFormatListener(a.hwnd);
    HRESULT dhr = RegisterDragDrop(a.hwnd, new DropTarget());
    logf("drop target hr=0x%08X", (unsigned)dhr);
    a.world.refresh();
    a.world.dump();
    a.icons.worksFn = [] { return g_app.world.works(); };
    a.icons.restoreCarry();
    a.iconsReady = a.icons.init();
    logf("desktop icons: %s (%zu)", a.iconsReady ? "ok" : "unavailable", a.icons.items.size());
    a.icons.restoreCarry();
    if (a.iconsReady) a.icons.recoverSnap();
    if (a.iconsReady && a.icons.autoArrange()) {
        logf("desktop auto-arrange is on, icon carrying disabled");
        a.iconsReady = false;
    }

    g_trayIcon = makeTrayIcon();
    addTray();
    startWeb();
    startGameSense();

    const RECT& r = a.world.mons.empty() ? RECT{0, 0, 1920, 1080} : a.world.mons[0].rc;
    for (auto& m : a.world.mons)
        if (m.rc.left == 0 && m.rc.top == 0) {
            pet.spawn(V2(randf(m.work.left + 200.f, m.work.right - 200.f), m.rc.top + pet.comH * 1.3f + 10.f));
            break;
        }
    if (pet.st != St::Fall) pet.spawn(V2((r.left + r.right) * 0.5f, r.top + pet.comH * 1.3f + 10.f));
    a.now = nowSec();
    if (webServe) pet.webServeOnly();
    frame(0.f);
    ShowWindow(a.hwnd, SW_SHOWNOACTIVATE);

    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    double last = nowSec(), next = last;
    MSG msg;
    while (!g_quit) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { g_quit = true; break; }
            if (g_nameWnd && IsDialogMessageW(g_nameWnd, &msg)) continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (g_quit) break;
        double now = nowSec();
        double frameDt = g_hidden ? 0.25 : (g_pet->calm() ? 1.0 / 30.0 : 1.0 / 60.0);
        if (now >= next) {
            a.now = now;
            frame((float)clampf((float)(now - last), 0.f, 1.f / 20.f));
            last = now;
            next += frameDt;
            if (next < now) next = now + frameDt;
            continue;
        }
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)((next - now) * 1e7);
        SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
        MsgWaitForMultipleObjectsEx(1, &timer, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    CloseHandle(timer);
    delete a.web;
    a.web = nullptr;
    OleUninitialize();
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return 0;
}
