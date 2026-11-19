#include "activity.h"
#include <dwmapi.h>
#define SECURITY_WIN32
#include <security.h>

const char* appKindName(AppKind k) {
    static const char* n[] = {"None", "Browser", "Game", "Create", "Code", "Chat", "Claude", "Media", "Explorer", "TaskMgr", "Other"};
    return n[(int)k];
}

std::wstring processExe(DWORD pid, std::wstring* fullPath) {
    std::wstring out;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return out;
    wchar_t buf[MAX_PATH * 2];
    DWORD n = MAX_PATH * 2;
    if (QueryFullProcessImageNameW(h, 0, buf, &n)) {
        std::wstring p(buf, n);
        if (fullPath) *fullPath = p;
        out = p.substr(p.find_last_of(L"\\/") + 1);
        for (auto& c : out) c = (wchar_t)towlower(c);
    }
    CloseHandle(h);
    return out;
}

static bool oneOf(const std::wstring& s, std::initializer_list<const wchar_t*> l) {
    for (auto x : l)
        if (s == x) return true;
    return false;
}

static bool systemOverlay(HWND h, const std::wstring& path) {
    if (!h) return false;
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if (ex & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT)) return true;
    static std::wstring win;
    if (win.empty()) {
        wchar_t b[MAX_PATH] = L"";
        GetWindowsDirectoryW(b, MAX_PATH);
        win = b;
        for (auto& c : win) c = (wchar_t)towlower(c);
        win += L"\\";
    }
    std::wstring p = path;
    for (auto& c : p) c = (wchar_t)towlower(c);
    return !p.empty() && p.rfind(win, 0) == 0;
}

static bool captureTool(const std::wstring& exe) {
    return oneOf(exe, {L"snippingtool.exe", L"screenclippinghost.exe", L"screensketch.exe", L"sharex.exe", L"lightshot.exe", L"greenshot.exe",
                       L"flameshot.exe", L"screenpresso.exe", L"picpick.exe", L"gyazo.exe", L"snagit32.exe", L"snagiteditor.exe", L"monosnap.exe"});
}

static AppKind classify(const std::wstring& exe, std::wstring path, const std::wstring& title, const wchar_t* cls, bool covers, bool sys) {
    if (captureTool(exe)) return AppKind::None;
    for (auto& c : path) c = (wchar_t)towlower(c);
    if (oneOf(exe, {L"chrome.exe", L"msedge.exe", L"firefox.exe", L"opera.exe", L"browser.exe", L"brave.exe", L"vivaldi.exe", L"zen.exe",
                    L"arc.exe", L"librewolf.exe", L"waterfox.exe", L"floorp.exe"}))
        return AppKind::Browser;
    if (oneOf(exe, {L"blender.exe", L"unrealeditor.exe", L"blockbench.exe", L"photoshop.exe", L"krita.exe", L"aseprite.exe",
                    L"clipstudiopaint.exe", L"sai2.exe", L"resolve.exe", L"afterfx.exe", L"adobe premiere pro.exe", L"substancepainter.exe"}))
        return AppKind::Create;
    if (oneOf(exe, {L"code.exe", L"idea64.exe", L"idea.exe", L"devenv.exe", L"rider64.exe", L"clion64.exe", L"pycharm64.exe",
                    L"webstorm64.exe", L"sublime_text.exe", L"notepad++.exe", L"cursor.exe", L"zed.exe", L"windowsterminal.exe"}))
        return AppKind::Code;
    if (exe == L"claude.exe") return AppKind::Claude;
    if (oneOf(exe, {L"discord.exe", L"telegram.exe", L"whatsapp.exe", L"slack.exe", L"ms-teams.exe", L"vesktop.exe", L"ayugram.exe"}))
        return AppKind::Chat;
    if (oneOf(exe, {L"vlc.exe", L"mpc-hc64.exe", L"mpc-be64.exe", L"spotify.exe", L"potplayermini64.exe", L"mpv.exe"})) return AppKind::Media;
    if (exe == L"taskmgr.exe") return AppKind::TaskMgr;
    if (exe == L"explorer.exe" && cls && !wcscmp(cls, L"CabinetWClass")) return AppKind::Explorer;
    if (path.find(L"\\steamapps\\common\\") != std::wstring::npos || path.find(L"\\epic games\\") != std::wstring::npos ||
        path.find(L"\\riot games\\") != std::wstring::npos || path.find(L"\\xboxgames\\") != std::wstring::npos)
        return AppKind::Game;
    if ((exe == L"javaw.exe" || exe == L"java.exe") && title.rfind(L"Minecraft", 0) == 0) return AppKind::Game;
    if (oneOf(exe, {L"minecraft.windows.exe", L"robloxplayerbeta.exe", L"osu!.exe"})) return AppKind::Game;
    if (exe.empty() || exe == L"explorer.exe" || exe == L"searchhost.exe" || exe == L"startmenuexperiencehost.exe" ||
        exe == L"shellexperiencehost.exe")
        return AppKind::None;
    if (sys) return covers ? AppKind::None : AppKind::Other;
    return covers ? AppKind::Game : AppKind::Other;
}

void Activity::init(HWND selfWnd) {
    self = selfWnd;
    audioInitT = 0;
}

void Activity::pollForeground(double now, const World& w) {
    HWND h = GetForegroundWindow();
    if (h == self) return;
    RECT r{};
    if (h) GetWindowRect(h, &r);
    wchar_t ttl[256] = L"";
    if (h) GetWindowTextW(h, ttl, 256);
    bool same = h == fg;
    if (!same) {
        fg = h;
        DWORD pid = 0;
        if (h) GetWindowThreadProcessId(h, &pid);
        exePath.clear();
        exe = pid ? processExe(pid, &exePath) : L"";
    }
    title = ttl;
    fgRect = r;
    fgMon = w.monitorAt(V2((r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f));
    fgCovers = false;
    capture = captureTool(exe);
    bool sys = systemOverlay(h, exePath) || capture;
    if (fgMon >= 0 && !sys) {
        const RECT& m = w.mons[fgMon].rc;
        RECT v = r;
        DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &v, sizeof(v));
        fgCovers = !IsZoomed(h) && v.left <= m.left + 2 && v.top <= m.top + 2 && v.right >= m.right - 2 && v.bottom >= m.bottom - 2;
    }
    wchar_t cls[96] = L"";
    if (h) GetClassNameW(h, cls, 96);
    AppKind k = classify(exe, exePath, title, cls, fgCovers, sys);
    if (k != kind) {
        if (kind != AppKind::None || k != AppKind::None) logf("foreground: %s (%s)", appKindName(k), narrow(exe).c_str());
        kind = k;
        since = now;
        switched = true;
    }
}

void Activity::pollAudio(float dt, double now) {
    audioInitT -= dt;
    if (audioInitT <= 0) {
        audioInitT = 10.f;
        meter.Reset();
        epVol.Reset();
        ComPtr<IMMDeviceEnumerator> en;
        ComPtr<IMMDevice> dev;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en))) &&
            SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) {
            dev->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL, nullptr, (void**)meter.GetAddressOf());
            dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void**)epVol.GetAddressOf());
        }
    }
    loud = false;
    if (!meter || !epVol) return;
    float pk = 0, v = 1;
    BOOL mute = FALSE;
    if (FAILED(meter->GetPeakValue(&pk))) {
        meter.Reset();
        return;
    }
    epVol->GetMasterVolumeLevelScalar(&v);
    epVol->GetMute(&mute);
    peak = pk;
    vol = mute ? 0.f : v;
    if (!preVolume && pk > v + 0.12f) {
        preVolume = true;
        logf("audio: meter is pre-volume (peak %.2f, volume %.2f)", pk, v);
    }
    float e = preVolume ? pk * vol : (mute ? 0.f : pk);
    level += (e - level) * (1 - expf(-dt / 0.6f));
    slow += (e - slow) * (1 - expf(-dt / 20.f));
    lvlMax = std::max(lvlMax, level);
    audioLogT += dt;
    if (audioLogT > 120.f) {
        if (lvlMax > 0.02f) logf("audio: level max %.2f, volume %.2f, pre %d", lvlMax, vol, preVolume);
        audioLogT = 0;
        lvlMax = 0;
    }
    if (level > 0.45f) loudT += dt;
    else loudT = 0;
    if (now - lastLoud > 300 && ((level > 0.45f && slow < 0.12f) || loudT > 3.f)) {
        loud = true;
        lastLoud = now;
        logf("audio: loud (level %.2f, slow %.2f, volume %.2f)", level, slow, vol);
    }
}

void Activity::onClipboard() {
    if (!IsClipboardFormatAvailable(CF_BITMAP) && !IsClipboardFormatAvailable(CF_DIB)) return;
    HWND owner = GetClipboardOwner();
    std::wstring ex;
    if (owner) {
        DWORD pid = 0;
        GetWindowThreadProcessId(owner, &pid);
        ex = processExe(pid);
    }
    if (!owner || oneOf(ex, {L"snippingtool.exe", L"screenclippinghost.exe", L"sharex.exe", L"lightshot.exe", L"greenshot.exe",
                             L"flameshot.exe", L"screenshotter.exe"})) {
        shot = true;
        logf("screenshot (owner %s)", owner ? narrow(ex).c_str() : "none");
    }
}

void Activity::update(float dt, double now, double idleSec, const World& w) {
    switched = false;
    fgPollT -= dt;
    if (fgPollT <= 0) {
        fgPollT = 0.4f;
        pollForeground(now, w);
    }
    pollAudio(dt, now);
    if (idleSec > 300) sitT = 0;
    else if (idleSec < 90) sitT += dt;
    bool lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (lmb && !lmbWas) {
        POINT p;
        GetCursorPos(&p);
        HWND hw = GetAncestor(WindowFromPoint(p), GA_ROOT);
        if (hw != self) clicks.push_back({now, p, hw});
    }
    lmbWas = lmb;
    while (!clicks.empty() && (now - clicks.front().t > 30 || clicks.size() > 60)) clicks.erase(clicks.begin());
}

std::wstring defaultUserName() {
    wchar_t buf[256];
    ULONG n = 256;
    std::wstring nm;
    if (GetUserNameExW(NameDisplay, buf, &n) && n > 1) nm = buf;
    if (nm.empty()) {
        DWORD k = 256;
        if (GetUserNameW(buf, &k)) nm = buf;
    }
    size_t sp = nm.find(L' ');
    if (sp != std::wstring::npos) nm = nm.substr(0, sp);
    if (nm.empty()) nm = L"друг";
    nm[0] = (wchar_t)towupper(nm[0]);
    return nm;
}
