#include "pet.h"
#include <ctime>

namespace {

const double kDay = 86400.0;
const double kAskEvery = 7 * kDay;
const int kCornerCap = 30;
const wchar_t* kCornerName = L"Уголок Узи";

std::wstring knownPath(REFKNOWNFOLDERID id) {
    PWSTR s = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &s)) && s) out = s;
    CoTaskMemFree(s);
    while (!out.empty() && (out.back() == L'\\' || out.back() == L'/')) out.pop_back();
    return out;
}

std::wstring parentDir(const std::wstring& p) {
    size_t k = p.find_last_of(L"\\/");
    return k == std::wstring::npos ? L"" : p.substr(0, k);
}

std::wstring leafName(const std::wstring& p) {
    size_t k = p.find_last_of(L"\\/");
    return k == std::wstring::npos ? p : p.substr(k + 1);
}

bool sameText(const std::wstring& a, const std::wstring& b) {
    return !a.empty() && CompareStringOrdinal(a.c_str(), (int)a.size(), b.c_str(), (int)b.size(), TRUE) == CSTR_EQUAL;
}

std::wstring fullPath(const std::wstring& p) {
    wchar_t b[MAX_PATH * 2];
    DWORD n = GetFullPathNameW(p.c_str(), MAX_PATH * 2, b, nullptr);
    if (!n || n >= MAX_PATH * 2) return L"";
    std::wstring s(b, n);
    while (s.size() > 3 && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
    return s;
}

std::wstring finalPath(const std::wstring& p, bool dir) {
    HANDLE h = CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           dir ? FILE_FLAG_BACKUP_SEMANTICS : 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";
    wchar_t b[MAX_PATH * 2];
    DWORD n = GetFinalPathNameByHandleW(h, b, MAX_PATH * 2, FILE_NAME_NORMALIZED);
    CloseHandle(h);
    return n && n < MAX_PATH * 2 ? std::wstring(b, n) : L"";
}

std::wstring handlePath(HANDLE h) {
    wchar_t b[MAX_PATH * 2];
    DWORD n = GetFinalPathNameByHandleW(h, b, MAX_PATH * 2, FILE_NAME_NORMALIZED);
    return n && n < MAX_PATH * 2 ? std::wstring(b, n) : L"";
}

bool linkLike(const std::wstring& p) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(p.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return false;
    FindClose(h);
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    DWORD tag = fd.dwReserved0;
    return tag == IO_REPARSE_TAG_MOUNT_POINT || tag == IO_REPARSE_TAG_SYMLINK || IsReparseTagNameSurrogate(tag);
}

bool nameOk(const std::wstring& n) {
    if (n.empty() || n.size() > 120 || n == L"." || n == L"..") return false;
    if (n.find(L"..") != std::wstring::npos) return false;
    if (n.front() == L' ' || n.back() == L' ' || n.back() == L'.') return false;
    for (wchar_t c : n)
        if (c < 0x20 || wcschr(L"<>:\"/\\|?*", c)) return false;
    return true;
}

bool deskOk(const std::wstring& desk, std::wstring* real, const char** why) {
    auto no = [&](const char* w) {
        if (why) *why = w;
        return false;
    };
    if (desk.size() < 6) return no("desktop path too short");
    bool drive = desk.size() > 3 && desk[1] == L':' && desk[2] == L'\\';
    bool unc = desk.rfind(L"\\\\", 0) == 0 && std::count(desk.begin() + 2, desk.end(), L'\\') >= 2;
    if (!drive && !unc) return no("desktop is a drive root or not absolute");
    DWORD a = GetFileAttributesW(desk.c_str());
    if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) return no("desktop not reachable");
    std::wstring f = finalPath(desk, true);
    if (f.empty()) return no("desktop does not resolve");
    if (real) *real = f;
    return true;
}

struct DirHold {
    HANDLE h = INVALID_HANDLE_VALUE;
    std::wstring real;
    explicit DirHold(const std::wstring& dir) {
        h = CreateFileW(dir.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        FILE_ATTRIBUTE_TAG_INFO ti{};
        bool ok = GetFileInformationByHandleEx(h, FileAttributeTagInfo, &ti, sizeof(ti)) && (ti.FileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        if (ok && (ti.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            ok = !(ti.ReparseTag == IO_REPARSE_TAG_MOUNT_POINT || ti.ReparseTag == IO_REPARSE_TAG_SYMLINK || IsReparseTagNameSurrogate(ti.ReparseTag));
        if (ok) real = handlePath(h);
    }
    ~DirHold() {
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    DirHold(const DirHold&) = delete;
    DirHold& operator=(const DirHold&) = delete;
};

int dayKeyNow() {
    time_t tt = time(nullptr);
    tm lt;
    localtime_s(&lt, &tt);
    return (lt.tm_year + 1900) * 1000 + lt.tm_yday;
}

const wchar_t* kOnes[] = {L"", L"один", L"два", L"три", L"четыре", L"пять", L"шесть", L"семь", L"восемь", L"девять", L"десять",
                          L"одиннадцать", L"двенадцать", L"тринадцать", L"четырнадцать", L"пятнадцать", L"шестнадцать", L"семнадцать",
                          L"восемнадцать", L"девятнадцать"};
const wchar_t* kTens[] = {L"", L"", L"двадцать", L"тридцать"};

std::wstring countWords(int n, const wchar_t* one, const wchar_t* few, const wchar_t* many) {
    n = std::clamp(n, 1, 39);
    std::wstring s = n < 20 ? kOnes[n] : std::wstring(kTens[n / 10]) + (n % 10 ? std::wstring(L" ") + kOnes[n % 10] : L"");
    int m10 = n % 10, m100 = n % 100;
    const wchar_t* w = (m10 == 1 && m100 != 11) ? one : (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) ? few : many;
    return s + L" " + w;
}

}

std::wstring plainLine(const std::wstring& s) {
    std::wstring o;
    for (wchar_t c : s) {
        if (c == L'~' || c == L'^' || c == L'|') continue;
        if (c == L' ' && (o.empty() || o.back() == L' ')) continue;
        o.push_back(c);
    }
    while (!o.empty() && o.back() == L' ') o.pop_back();
    return o;
}

std::wstring timesWords(int n) { return n >= 20 ? L"много раз" : countWords(n, L"раз", L"раза", L"раз"); }

std::wstring awayWords(double sec) {
    double m = sec / 60.0;
    if (m < 7.5) return L"пару минут";
    if (m < 25) return countWords((int)lround(m / 5.0) * 5, L"минуту", L"минуты", L"минут");
    if (m < 45) return L"полчаса";
    if (m < 75) return L"целый час";
    if (m < 105) return L"полтора часа";
    double h = sec / 3600.0;
    if (h < 21.5) return countWords((int)lround(h), L"час", L"часа", L"часов");
    if (h < 36) return L"целые сутки";
    int d = (int)lround(h / 24.0);
    if (d <= 30) return countWords(d, L"день", L"дня", L"дней");
    if (d < 60) return L"больше месяца";
    if (d < 365) return countWords(d / 30, L"месяц", L"месяца", L"месяцев");
    return L"больше года";
}

std::wstring Pet::cornerDesk() const {
    if (!cornerDeskTest.empty()) return cornerDeskTest;
    if (app.icons.testOnly || app.icons.fake || app.world.fake) return L"";
    std::wstring d = knownPath(FOLDERID_Desktop), pub = knownPath(FOLDERID_PublicDesktop);
    if (d.empty() || sameText(d, pub)) return L"";
    return d;
}

bool Pet::cornerDirOk(const std::wstring& dir, std::string* why, int* kind) const {
    auto no = [&](int k, const char* w) {
        if (why) *why = w;
        if (kind) *kind = k;
        return false;
    };
    if (kind) *kind = 0;
    std::wstring desk = cornerDesk(), fk;
    if (desk.empty()) return no(1, "no desktop to use");
    const char* dw = "";
    if (!deskOk(desk, &fk, &dw)) return no(1, dw);
    if (dir.size() < 10) return no(3, "path too short");
    bool abs = (dir.size() > 2 && dir[1] == L':' && dir[2] == L'\\') || dir.rfind(L"\\\\", 0) == 0;
    if (!abs) return no(3, "not an absolute path");
    std::wstring full = fullPath(dir);
    if (!sameText(full, dir)) return no(3, "path is not canonical");
    if (!sameText(parentDir(full), fullPath(desk))) return no(3, "not directly on the desktop");
    if (leafName(full).rfind(kCornerName, 0) != 0) return no(3, "not her folder name");
    DWORD a = GetFileAttributesW(full.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) {
        DWORD e = GetLastError();
        return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ? no(2, "folder is gone") : no(1, "folder not readable now");
    }
    if (!(a & FILE_ATTRIBUTE_DIRECTORY)) return no(3, "not a folder");
    if (linkLike(full)) return no(3, "folder is a link");
    std::wstring fd = finalPath(full, true);
    if (fd.empty()) return no(1, "folder does not open now");
    if (!sameText(fd, fk + L"\\" + leafName(full))) return no(3, "folder resolves somewhere else");
    return true;
}

bool Pet::cornerOwn(const std::wstring& name) const {
    for (auto& f : cornerFiles)
        if (sameText(f, name)) return true;
    return false;
}

static std::wstring cornerListPath() { return appDataDir() + L"\\corner.txt"; }

void Pet::cornerLoad() {
    cornerLoaded = true;
    cornerDir.clear();
    cornerFiles.clear();
    cornerSince = 0;
    cornerIcon = false;
    cornerIcoFile.clear();
    cornerIcoPhoto.clear();
    std::string raw;
    if (!readFile(cornerListPath(), raw)) return;
    size_t a = 0;
    while (a < raw.size()) {
        size_t e = raw.find('\n', a);
        if (e == std::string::npos) e = raw.size();
        std::string l = raw.substr(a, e - a);
        a = e + 1;
        if (!l.empty() && l.back() == '\r') l.pop_back();
        size_t tab = l.find('\t');
        if (tab == std::string::npos) continue;
        std::string k = l.substr(0, tab);
        std::wstring v = widen(l.substr(tab + 1));
        if (k == "dir") cornerDir = v;
        else if (k == "since") cornerSince = atof(l.c_str() + tab + 1);
        else if (k == "icon") cornerIcon = l.substr(tab + 1) == "1";
        else if (k == "ico" && nameOk(v)) cornerIcoFile = v;
        else if (k == "icoPhoto" && nameOk(v)) cornerIcoPhoto = v;
        else if (k == "file" && nameOk(v) && !cornerOwn(v)) cornerFiles.push_back(v);
    }
    if (!cornerDir.empty() && cornerSince <= 0) {
        cornerSince = (double)time(nullptr);
        cornerSave();
    }
    logf("corner: loaded '%s', %zu of her files, effort %d", narrow(cornerDir).c_str(), cornerFiles.size(), cornerEffort());
}

void Pet::cornerSave() const {
    std::string t;
    if (!cornerDir.empty()) t += "dir\t" + narrow(cornerDir) + "\r\n";
    if (!cornerDir.empty() && cornerSince > 0) t += "since\t" + std::to_string((long long)cornerSince) + "\r\n";
    if (!cornerDir.empty() && cornerIcon) t += "icon\t1\r\n";
    if (!cornerDir.empty() && cornerIcon && !cornerIcoFile.empty()) t += "ico\t" + narrow(cornerIcoFile) + "\r\n";
    if (!cornerDir.empty() && cornerIcon && !cornerIcoPhoto.empty()) t += "icoPhoto\t" + narrow(cornerIcoPhoto) + "\r\n";
    for (auto& f : cornerFiles) t += "file\t" + narrow(f) + "\r\n";
    if (!writeFile(cornerListPath(), t)) logf("corner: can't save her list");
}

bool Pet::cornerWrite(const std::wstring& name, const std::string& bytes) {
    auto no = [&](const char* w) {
        cornerRefused++;
        logf("corner: REFUSED write '%s': %s", narrow(name).c_str(), w);
        return false;
    };
    if (!cornerLoaded) cornerLoad();
    if (vit.cornerState != 1 || cornerDir.empty()) return no("no corner");
    if (!nameOk(name)) return no("bad name");
    std::string why;
    if (!cornerDirOk(cornerDir, &why)) return no(why.c_str());
    DirHold hold(cornerDir);
    if (hold.h == INVALID_HANDLE_VALUE) return no("can't hold the folder");
    std::wstring deskReal = finalPath(cornerDesk(), true);
    if (hold.real.empty() || deskReal.empty() || !sameText(hold.real, deskReal + L"\\" + leafName(cornerDir))) return no("held folder is not the one on the desktop");
    std::wstring target = cornerDir + L"\\" + name;
    if (!sameText(fullPath(target), target) || !sameText(parentDir(fullPath(target)), cornerDir)) return no("path leaves the folder");
    bool own = cornerOwn(name);
    DWORD at = GetFileAttributesW(target.c_str());
    bool exists = at != INVALID_FILE_ATTRIBUTES;
    if (exists && (at & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) return no("a folder or link is there");
    if (exists && !own) return no("not her file");
    if (!own && (int)cornerFiles.size() >= kCornerCap) return no("corner is full");
    HANDLE h = CreateFileW(target.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, exists ? TRUNCATE_EXISTING : CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        logf("corner: can't open '%s' (error %lu)", narrow(name).c_str(), GetLastError());
        return false;
    }
    std::wstring got = handlePath(h), want = hold.real + L"\\" + name;
    if (!sameText(got, want) || !sameText(handlePath(hold.h), hold.real)) {
        if (!exists) {
            FILE_DISPOSITION_INFO di{TRUE};
            SetFileInformationByHandle(h, FileDispositionInfo, &di, sizeof(di));
        }
        CloseHandle(h);
        return no("opened file resolves elsewhere");
    }
    DWORD wr = 0;
    bool ok = WriteFile(h, bytes.data(), (DWORD)bytes.size(), &wr, nullptr) && wr == bytes.size();
    FlushFileBuffers(h);
    CloseHandle(h);
    if (!own) {
        cornerFiles.push_back(name);
        cornerSave();
    }
    cornerWrites++;
    logf("corner: wrote '%s' (%zu bytes, %s)%s", narrow(name).c_str(), bytes.size(), own ? "her own, rewritten" : "new", ok ? "" : " INCOMPLETE");
    return ok;
}

std::wstring Pet::cornerFreeName(const std::wstring& base, const std::wstring& ext) const {
    for (int k = 1; k < 50; k++) {
        std::wstring n = base + (k == 1 ? L"" : L" (" + std::to_wstring(k) + L")") + ext;
        if (!cornerOwn(n) && GetFileAttributesW((cornerDir + L"\\" + n).c_str()) == INVALID_FILE_ATTRIBUTES) return n;
    }
    return L"";
}

static std::string noteBytes(const std::wstring& text) {
    std::wstring t;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == L'\n' && (i == 0 || text[i - 1] != L'\r')) t += L'\r';
        t += text[i];
    }
    return "\xEF\xBB\xBF" + narrow(t);
}

bool Pet::cornerCanAsk(double now) const {
    if (baby() || vit.intro != 0 || vit.fatal || vit.dead) return false;
    if (tier() < 1 || metDays(now) < 3 || vit.cornerState != 0) return false;
    if (vit.cornerAskedAt > 0 && now - vit.cornerAskedAt < kAskEvery) return false;
    std::wstring desk = cornerDesk();
    return !desk.empty() && deskOk(desk, nullptr, nullptr);
}

bool Pet::cornerAskNow() {
    int tr = std::max(1, tier());
    if (!ask("cornerAsk", "choiceCorner", "corner")) return false;
    vit.cornerAskedAt = (double)time(nullptr);
    vit.save();
    if (tr >= 2) tBlush = 1.6f;
    if (fabsf(lastCursor.x - pos.x) > 6.f) facing = lastCursor.x > pos.x ? 1 : -1;
    logf("corner: asked (tier %d, met %d days): '%s'", tr, metDays((double)time(nullptr)), narrow(app.bubble.text).c_str());
    return true;
}

void Pet::hardcoreAnswer(int choice) {
    if (choice < 0) {
        hardcoreAskAt = app.now + 4 * 3600;
        logf("hardcore: ask ignored, later");
        return;
    }
    app.settings.hardcore = choice == 0;
    app.settings.hardcoreAsked = true;
    app.settings.save();
    if (choice == 0) {
        tSurprise = 0.8f;
        sayCat("hardcoreYes", 1.f, L"", true);
    } else {
        tHappy = 1.2f;
        sayCat("hardcoreNo", 1.f, L"", true);
    }
    logf("hardcore: %s", choice == 0 ? "on" : "off");
}

void Pet::cornerAnswer(int choice) {
    double now = (double)time(nullptr);
    if (choice < 0) {
        vit.cornerAskedAt = now - kAskEvery + kDay;
        vit.save();
        logf("corner: ask ignored, again in a day");
        return;
    }
    int tr = std::max(1, tier());
    if (choice != 0) {
        tSad = 1.6f;
        vit.cornerAskedAt = now;
        vit.save();
        sayCat("cornerNo", 1.f, L"", true);
        logf("corner: said no, again in 7 days");
        return;
    }
    if (!cornerMake()) {
        tSad = 1.2f;
        vit.cornerAskedAt = now;
        vit.save();
        sayCat("cornerFail", 1.f, L"", true);
        return;
    }
    tHappy = 1.8f;
    if (tr >= 2) tBlush = 1.6f;
    if (grounded() && !dragging && (st == St::Idle || st == St::Sit || st == St::LookAround)) setState(St::Clap, 1.4f);
    sayCat("cornerMade", 1.f, L"", true);
}

bool Pet::cornerMake() {
    if (!cornerLoaded) cornerLoad();
    std::wstring desk = cornerDesk();
    const char* dw = "no desktop to use";
    if (desk.empty() || !deskOk(desk, nullptr, &dw)) {
        logf("corner: desktop not usable ('%s': %s)", narrow(desk).c_str(), dw);
        return false;
    }
    std::wstring dir;
    bool reused = false;
    for (int k = 1; k < 10 && dir.empty(); k++) {
        std::wstring p = fullPath(desk + L"\\" + kCornerName + (k == 1 ? L"" : L" (" + std::to_wstring(k) + L")"));
        DWORD a = GetFileAttributesW(p.c_str());
        if (a == INVALID_FILE_ATTRIBUTES) {
            DWORD ge = GetLastError();
            if (ge != ERROR_FILE_NOT_FOUND && ge != ERROR_PATH_NOT_FOUND) {
                logf("corner: can't see '%s' (error %lu), stop", narrow(p).c_str(), ge);
                break;
            }
            if (CreateDirectoryW(p.c_str(), nullptr)) {
                dir = p;
                continue;
            }
            DWORD e = GetLastError();
            logf("corner: can't create '%s' (error %lu)", narrow(p).c_str(), e);
            if (e != ERROR_ALREADY_EXISTS) break;
        } else if ((a & FILE_ATTRIBUTE_DIRECTORY) && !linkLike(p)) {
            dir = p;
            reused = true;
        }
    }
    std::string why;
    if (dir.empty() || !cornerDirOk(dir, &why)) {
        logf("corner: no folder (%s)", why.c_str());
        return false;
    }
    cornerDir = dir;
    cornerFiles.clear();
    cornerSince = (double)time(nullptr);
    cornerIcon = false;
    vit.cornerState = 1;
    vit.save();
    cornerSave();
    logf("corner: %s '%s'", reused ? "reusing" : "made", narrow(dir).c_str());
    std::wstring keep = plainLine(app.phrases.pick("cornerNoteKeep", L""));
    if (!keep.empty()) cornerWrite(L"Не трогать.txt", noteBytes(keep + L"\r\n\r\n— Узи.\r\n"));
    auto& I = app.icons;
    bool deskMatches = !I.fake || sameText(fullPath(I.fakeDir), fullPath(desk));
    if (app.settings.icons && I.ok() && deskMatches && I.spacing.x > 0) {
        POINT cell{};
        if (I.nearestFree(V2(pos.x + facing * 90.f * S(), pos.y - 40.f * S()), dir, cell)) {
            I.layoutSnap(app.now, "corner", true);
            I.layoutActed();
            I.placeAfter(dir, cell, "corner: her folder next to her", L"", false);
            logf("corner: icon goes to cell %ld,%ld", cell.x, cell.y);
        }
    }
    return true;
}

bool Pet::cornerSaveFind(const std::wstring& title, const std::string& url) {
    if (vit.cornerState != 1 || url.rfind("https://", 0) != 0 || url.find_first_of("\r\n") != std::string::npos) return false;
    std::wstring base;
    for (wchar_t c : title.substr(0, 60))
        if (!(c < 0x20 || wcschr(L"<>:\"/\\|?*.", c))) base += c;
    while (!base.empty() && base.back() == L' ') base.pop_back();
    if (base.empty()) base = L"нашла";
    std::wstring n = cornerFreeName(base, L".url");
    return !n.empty() && cornerWrite(n, "[InternetShortcut]\r\nURL=" + url + "\r\n");
}

void Pet::cornerPhoto(const wchar_t* name) {
    if (vit.cornerState != 1 || baby() || vit.fatal || cornerPhotoT >= 0 || !app.renderSelf) return;
    if (vit.cornerPhotoDay == dayKeyNow() && !cornerPhotoAny) {
        logf("corner photo '%s': skipped, one a day", narrow(name).c_str());
        return;
    }
    cornerPhotoName = name;
    cornerPhotoT = 0.9f;
}

bool Pet::cornerPhotoNow() {
    std::wstring n = cornerFreeName(cornerPhotoName, L".png");
    std::string png;
    if (n.empty() || !app.renderSelf(png, cornerPhotoName)) {
        logf("corner photo '%s': not rendered", narrow(cornerPhotoName).c_str());
        return false;
    }
    if (!cornerWrite(n, png)) return false;
    vit.cornerPhotoDay = dayKeyNow();
    vit.save();
    logf("corner photo: '%s' (%zu bytes, state %s)", narrow(n).c_str(), png.size(), stateName());
    cornerIconSync();
    return true;
}

bool Pet::cornerMoodNote() {
    static const wchar_t* names[] = {L"мысли", L"не читать", L"если что", L"заметка", L"просто так"};
    std::wstring t = plainLine(app.phrases.pick("cornerMood", L""));
    if (t.empty()) return false;
    std::wstring n = cornerFreeName(names[randi(0, 4)], L".txt");
    if (n.empty() || !cornerWrite(n, noteBytes(t + L"\r\n\r\n— Узи.\r\n"))) return false;
    vit.cornerMoodAt = (double)time(nullptr);
    vit.save();
    return true;
}

int Pet::cornerEffort() const {
    int days = cornerSince > 0 ? (int)(((double)time(nullptr) - cornerSince) / kDay) : 0;
    return (int)cornerFiles.size() + (cornerIcon ? 3 : 0) + std::min(days, 30) / 3;
}

void Pet::cornerMoved(const std::wstring& now) {
    if (vit.cornerState != 1 || now.empty()) return;
    std::wstring full = fullPath(now);
    std::string why;
    if (sameText(full, cornerDir) || !cornerDirOk(full, &why)) {
        if (!sameText(full, cornerDir)) logf("corner: came back as '%s' but that is not usable (%s)", narrow(full).c_str(), why.c_str());
        return;
    }
    logf("corner: came back under a new name '%s' (was '%s')", narrow(full).c_str(), narrow(cornerDir).c_str());
    cornerDir = full;
    cornerSave();
}

bool sameFullPath(const std::wstring& a, const std::wstring& b) { return sameText(fullPath(a), fullPath(b)); }

bool Pet::cornerIsMine(const std::wstring& path) const {
    return vit.cornerState == 1 && !cornerDir.empty() && sameText(fullPath(path), cornerDir);
}

bool pngOf(IWICImagingFactory* wic, int w, int h, const uint32_t* px, std::string& out) {
    ComPtr<IStream> mem;
    ComPtr<IWICBitmap> wb;
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> fe;
    if (!wic || FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &mem)) ||
        FAILED(wic->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppBGRA, w * 4, (UINT)(w * h * 4), (BYTE*)px, &wb)) ||
        FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) || FAILED(enc->Initialize(mem.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(enc->CreateNewFrame(&fe, nullptr)) || FAILED(fe->Initialize(nullptr)))
        return false;
    fe->SetSize(w, h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    fe->SetPixelFormat(&fmt);
    if (FAILED(fe->WriteSource(wb.Get(), nullptr)) || FAILED(fe->Commit()) || FAILED(enc->Commit())) return false;
    HGLOBAL hg = nullptr;
    STATSTG stg{};
    if (FAILED(GetHGlobalFromStream(mem.Get(), &hg)) || FAILED(mem->Stat(&stg, STATFLAG_NONAME))) return false;
    const char* p = (const char*)GlobalLock(hg);
    if (!p) return false;
    out.assign(p, (size_t)stg.cbSize.QuadPart);
    GlobalUnlock(hg);
    return !out.empty();
}

static bool stockPixels(SHSTOCKICONID id, int sz, std::vector<uint32_t>& px) {
    SHSTOCKICONINFO si{sizeof(si)};
    if (FAILED(SHGetStockIconInfo(id, SHGSI_ICONLOCATION, &si))) return false;
    HICON hi = nullptr;
    UINT got = PrivateExtractIconsW(si.szPath, si.iIcon, sz, sz, &hi, nullptr, 1, 0);
    if (got != 1 || !hi) return false;
    ICONINFO ii{};
    bool ok = GetIconInfo(hi, &ii) && ii.hbmColor;
    BITMAP bm{};
    if (ok) ok = GetObjectW(ii.hbmColor, sizeof(bm), &bm) && bm.bmWidth == sz && bm.bmHeight == sz && bm.bmBitsPixel == 32;
    if (ok) {
        BITMAPINFO bi{};
        bi.bmiHeader = {sizeof(BITMAPINFOHEADER), sz, -sz, 1, 32, BI_RGB};
        px.assign((size_t)sz * sz, 0);
        HDC dc = GetDC(nullptr);
        ok = GetDIBits(dc, ii.hbmColor, 0, sz, px.data(), &bi, DIB_RGB_COLORS) == sz;
        ReleaseDC(nullptr, dc);
        ok = ok && std::any_of(px.begin(), px.end(), [](uint32_t c) { return (c >> 24) != 0; });
    }
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    DestroyIcon(hi);
    return ok;
}

static void toPurple(std::vector<uint32_t>& px, int* purple, int* left) {
    const float kHue = 288.f;
    for (auto& c : px) {
        int a = c >> 24;
        if (!a) continue;
        float b = (c & 255) / 255.f, g = ((c >> 8) & 255) / 255.f, r = ((c >> 16) & 255) / 255.f;
        float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
        if (mx <= 0 || d / mx < 0.18f) continue;
        float h = mx == r ? 60.f * fmodf((g - b) / d + 6.f, 6.f) : mx == g ? 60.f * ((b - r) / d + 2.f) : 60.f * ((r - g) / d + 4.f);
        if (h < 15.f || h > 75.f) {
            if (left) (*left)++;
            continue;
        }
        float s = d / mx, v = mx * 0.93f;
        float hh = kHue / 60.f, f = hh - floorf(hh);
        float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
        float rr, gg, bb;
        switch ((int)hh % 6) {
            case 0: rr = v, gg = t, bb = p; break;
            case 1: rr = q, gg = v, bb = p; break;
            case 2: rr = p, gg = v, bb = t; break;
            case 3: rr = p, gg = q, bb = v; break;
            case 4: rr = t, gg = p, bb = v; break;
            default: rr = v, gg = p, bb = q; break;
        }
        auto u8 = [](float x) { return (uint32_t)std::clamp((int)lroundf(x * 255.f), 0, 255); };
        c = (uint32_t)a << 24 | u8(rr) << 16 | u8(gg) << 8 | u8(bb);
        if (purple) (*purple)++;
    }
}

static bool photoCover(IWICImagingFactory* wic, const std::string& bytes, int w, int h, std::vector<uint32_t>& out) {
    ComPtr<IWICStream> s;
    ComPtr<IWICBitmapDecoder> dec;
    ComPtr<IWICBitmapFrameDecode> fr;
    ComPtr<IWICFormatConverter> cv;
    ComPtr<IWICBitmapClipper> cl;
    ComPtr<IWICBitmapScaler> sc;
    if (!wic || bytes.empty() || FAILED(wic->CreateStream(&s)) || FAILED(s->InitializeFromMemory((BYTE*)bytes.data(), (DWORD)bytes.size())) ||
        FAILED(wic->CreateDecoderFromStream(s.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec)) || FAILED(dec->GetFrame(0, &fr)))
        return false;
    UINT iw = 0, ih = 0;
    if (FAILED(fr->GetSize(&iw, &ih)) || iw < 8 || ih < 8 || (size_t)iw * ih > 16u << 20) return false;
    if (FAILED(wic->CreateFormatConverter(&cv)) ||
        FAILED(cv->Initialize(fr.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
        return false;
    double sx = iw * 0.04, sy = ih * 0.04, sw = iw * 0.92, sh = ih * 0.78;
    double aspect = (double)w / h, cw, ch, cx, cy;
    std::vector<uint32_t> all((size_t)iw * ih);
    int bx0 = INT_MAX, by0 = INT_MAX, bx1 = -1, by1 = -1;
    if (SUCCEEDED(cv->CopyPixels(nullptr, iw * 4, (UINT)(all.size() * 4), (BYTE*)all.data()))) {
        uint32_t bg = all[(size_t)(int)(sy + 2) * iw + (int)(sx + 2)];
        auto fg = [&](uint32_t c) {
            int d = 0;
            for (int sh2 = 0; sh2 < 24; sh2 += 8) d += abs((int)((c >> sh2) & 255) - (int)((bg >> sh2) & 255));
            return d > 40;
        };
        for (int y = (int)sy; y < (int)(sy + sh); y++)
            for (int x = (int)sx; x < (int)(sx + sw); x++)
                if (fg(all[(size_t)y * iw + x])) {
                    bx0 = std::min(bx0, x), bx1 = std::max(bx1, x);
                    by0 = std::min(by0, y), by1 = std::max(by1, y);
                }
    }
    double bw = bx1 - bx0, bh = by1 - by0;
    if (bx1 > bx0 && by1 > by0 && bw < sw * 0.97 && bh < sh * 0.97) {
        cw = std::max(bw * 1.5, bh * 0.75 * aspect);
        ch = cw / aspect;
        cx = (bx0 + bx1) * 0.5 - cw * 0.5;
        cy = by0 - ch * 0.08;
    } else {
        double k = std::max(w / sw, h / sh);
        cw = std::min(sw, w / k), ch = std::min(sh, h / k);
        cx = sx + (sw - cw) / 2, cy = sy + (sh - ch) * 0.25;
    }
    if (cw > iw) cw = iw, ch = cw / aspect;
    if (ch > ih) ch = ih, cw = ch * aspect;
    cx = std::clamp(cx, 0.0, iw - cw);
    cy = std::clamp(cy, 0.0, ih - ch);
    WICRect r{(INT)lround(cx), (INT)lround(cy), std::max(1, (INT)lround(cw)), std::max(1, (INT)lround(ch))};
    r.Width = std::min<INT>(r.Width, (INT)iw - r.X);
    r.Height = std::min<INT>(r.Height, (INT)ih - r.Y);
    if (FAILED(wic->CreateBitmapClipper(&cl)) || FAILED(cl->Initialize(cv.Get(), &r)) || FAILED(wic->CreateBitmapScaler(&sc)) ||
        FAILED(sc->Initialize(cl.Get(), w, h, WICBitmapInterpolationModeHighQualityCubic)))
        return false;
    out.assign((size_t)w * h, 0);
    if (FAILED(sc->CopyPixels(nullptr, w * 4, (UINT)(out.size() * 4), (BYTE*)out.data()))) return false;
    for (auto& c : out) c |= 0xFF000000u;
    return true;
}

static uint32_t over(uint32_t dst, uint32_t src) {
    float sa = (src >> 24) / 255.f, da = (dst >> 24) / 255.f;
    if (sa <= 0) return dst;
    float oa = sa + da * (1 - sa);
    uint32_t o = (uint32_t)lroundf(oa * 255.f) << 24;
    for (int sh = 0; sh < 24; sh += 8) {
        float sc = ((src >> sh) & 255) / 255.f, dc = ((dst >> sh) & 255) / 255.f;
        float c = (sc * sa + dc * da * (1 - sa)) / oa;
        o |= (uint32_t)std::clamp((int)lroundf(c * 255.f), 0, 255) << sh;
    }
    return o;
}

static bool folderWithPhoto(IWICImagingFactory* wic, int sz, const std::string& photo, std::vector<uint32_t>& px) {
    std::vector<uint32_t> back, front, ph;
    if (!stockPixels(SIID_FOLDERBACK, sz, back) || !stockPixels(SIID_FOLDERFRONT, sz, front)) return false;
    toPurple(back, nullptr, nullptr);
    toPurple(front, nullptr, nullptr);
    int x0 = (int)lroundf(sz * 24 / 256.f), x1 = (int)lroundf(sz * 232 / 256.f), y0 = (int)lroundf(sz * 66 / 256.f);
    int w = x1 - x0, h = std::min(sz - y0, (int)lroundf(w * 0.75f));
    int frontBottom = -1;
    for (int y = 0; y < sz; y++)
        if ((front[(size_t)y * sz + sz / 2] >> 24) > 128) frontBottom = y;
    if (frontBottom > y0) h = std::min(h, frontBottom - y0 - std::max(1, sz / 64));
    if (w < 8 || h < 6 || !photoCover(wic, photo, w, h, ph)) return false;
    px = back;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t& d = px[(size_t)(y0 + y) * sz + x0 + x];
            d = over(d, (ph[(size_t)y * w + x] & 0xFFFFFFu) | (d & 0xFF000000u));
        }
    for (size_t i = 0; i < px.size(); i++) px[i] = over(px[i], front[i]);
    return true;
}

std::string purpleFolderIco(IWICImagingFactory* wic, std::string* png256, int* purple, int* left, const std::string* photo) {
    static const int sizes[] = {16, 20, 24, 32, 40, 48, 64, 96, 256};
    std::vector<std::pair<int, std::string>> imgs;
    for (int sz : sizes) {
        std::vector<uint32_t> px;
        bool withPhoto = photo && !photo->empty() && sz >= 32 && folderWithPhoto(wic, sz, *photo, px);
        if (!withPhoto) {
            if (!stockPixels(SIID_FOLDER, sz, px)) continue;
            toPurple(px, sz == 32 ? purple : nullptr, sz == 32 ? left : nullptr);
        }
        std::string png;
        if (!pngOf(wic, sz, sz, px.data(), png)) continue;
        if (sz == 256 && png256) *png256 = png;
        imgs.emplace_back(sz, std::move(png));
    }
    if (imgs.size() < 3) return "";
    std::string out;
    auto put = [&](const void* v, size_t n) { out.append((const char*)v, n); };
    WORD hdr[3] = {0, 1, (WORD)imgs.size()};
    put(hdr, 6);
    DWORD off = 6 + 16 * (DWORD)imgs.size();
    for (auto& [sz, png] : imgs) {
        BYTE wh[4] = {(BYTE)(sz >= 256 ? 0 : sz), (BYTE)(sz >= 256 ? 0 : sz), 0, 0};
        WORD pb[2] = {1, 32};
        DWORD n = (DWORD)png.size();
        put(wh, 4);
        put(pb, 4);
        put(&n, 4);
        put(&off, 4);
        off += n;
    }
    for (auto& [sz, png] : imgs) out += png;
    return out;
}

static const wchar_t* kCornerIcoA = L"folder.ico";
static const wchar_t* kCornerIcoB = L"folder-2.ico";

static std::string cornerIniBytes(const std::wstring& ico) {
    std::wstring ini = L"\xFEFF[.ShellClassInfo]\r\nIconResource=" + ico + L",0\r\nInfoTip=Уголок Узи. Смотреть можно, трогать нельзя.\r\n";
    return std::string((const char*)ini.data(), ini.size() * sizeof(wchar_t));
}

bool Pet::cornerWriteHidden(const std::wstring& name, const std::string& bytes) {
    std::wstring f = cornerDir + L"\\" + name;
    if (cornerOwn(name)) SetFileAttributesW(f.c_str(), FILE_ATTRIBUTE_NORMAL);
    bool ok = cornerWrite(name, bytes);
    if (cornerOwn(name)) SetFileAttributesW(f.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    return ok;
}

std::wstring Pet::cornerLastPhoto() const {
    for (auto it = cornerFiles.rbegin(); it != cornerFiles.rend(); ++it) {
        if (it->size() < 5 || !sameText(it->substr(it->size() - 4), L".png")) continue;
        DWORD a = GetFileAttributesW((cornerDir + L"\\" + *it).c_str());
        if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) return *it;
    }
    return L"";
}

bool Pet::cornerIconSync(bool force) {
    if (vit.cornerState != 1 || !cornerIcon || cornerDir.empty()) return false;
    std::wstring want = cornerLastPhoto();
    if (!force && !cornerIcoFile.empty() && sameText(want.empty() ? L"-" : want, cornerIcoPhoto.empty() ? L"-" : cornerIcoPhoto)) return false;
    std::string photo;
    if (!want.empty() && !readFile(cornerDir + L"\\" + want, photo)) want.clear();
    std::string ico = purpleFolderIco(app.gfx.wic.Get(), nullptr, nullptr, nullptr, want.empty() ? nullptr : &photo);
    if (ico.size() < 64) return false;
    std::wstring next = sameText(cornerIcoFile, kCornerIcoA) ? kCornerIcoB : kCornerIcoA;
    if (!cornerWriteHidden(next, ico) || !cornerWriteHidden(L"desktop.ini", cornerIniBytes(next))) return false;
    if (cornerOwn(L"uzi.ico")) cornerWriteHidden(L"uzi.ico", ico);
    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, cornerDir.c_str(), nullptr);
    SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, cornerDir.c_str(), nullptr);
    logf("corner icon: '%s' now, photo inside '%s' (was '%s' with '%s')", narrow(next).c_str(), narrow(want).c_str(), narrow(cornerIcoFile).c_str(),
         narrow(cornerIcoPhoto).c_str());
    cornerIcoFile = next;
    cornerIcoPhoto = want;
    cornerSave();
    return true;
}

bool Pet::cornerDecorate() {
    if (!cornerLoaded) cornerLoad();
    if (vit.cornerState != 1 || cornerIcon || cornerDir.empty()) return false;
    cornerIcon = true;
    cornerIcoFile.clear();
    if (!cornerIconSync(true)) {
        cornerIcon = false;
        logf("corner icon: could not put it up");
        return false;
    }
    std::string why;
    DWORD a = GetFileAttributesW(cornerDir.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && cornerDirOk(cornerDir, &why))
        SetFileAttributesW(cornerDir.c_str(), a | FILE_ATTRIBUTE_READONLY);
    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, cornerDir.c_str(), nullptr);
    cornerSave();
    logf("corner icon: done, effort %d", cornerEffort());
    tHappy = 1.6f;
    sayCat("cornerIcon", 1.f, L"", true);
    return true;
}

void Pet::cornerCheck() {
    if (!cornerLoaded) cornerLoad();
    if (vit.cornerState != 1) return;
    if ((heldCorner && !heldPath.empty()) || (!cornerDir.empty() && app.icons.inHands(cornerDir))) {
        cornerMissN = 0;
        return;
    }
    double now = (double)time(nullptr);
    std::string why;
    int kind = 3;
    if (cornerDir.empty() || !cornerDirOk(cornerDir, &why, &kind)) {
        if (kind == 1) {
            if (cornerSoftN++ % 30 == 0) logf("corner: can't check now ('%s': %s), waiting", narrow(cornerDir).c_str(), why.c_str());
            return;
        }
        if (kind == 2 && ++cornerMissN < 2) {
            logf("corner: folder not found, checking again in a minute");
            return;
        }
        int effort = cornerEffort();
        logf("corner: gone ('%s': %s), effort %d, asks again in 7 days", narrow(cornerDir).c_str(), cornerDir.empty() ? "no record" : why.c_str(), effort);
        cornerMissN = cornerSoftN = 0;
        cornerSayCat = effort >= 9 ? "cornerGoneHurt" : "";
        cornerIcon = false;
        cornerIcoFile.clear();
        cornerIcoPhoto.clear();
        cornerSince = 0;
        vit.cornerState = 0;
        vit.cornerAskedAt = now;
        vit.save();
        cornerDir.clear();
        cornerFiles.clear();
        cornerSave();
        if (cornerSayCat.empty()) cornerSayCat = "cornerGone";
        cornerSayArg.clear();
        return;
    }
    cornerMissN = cornerSoftN = 0;
    cornerIconSync();
    std::vector<std::wstring> have;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((cornerDir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return;
    int seen = 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        have.push_back(fd.cFileName);
    } while (++seen < 5000 && FindNextFileW(h, &fd));
    FindClose(h);
    std::vector<std::wstring> lost;
    for (auto& f : cornerFiles)
        if (std::none_of(have.begin(), have.end(), [&](const std::wstring& x) { return sameText(x, f); })) lost.push_back(f);
    if (lost.empty()) return;
    std::erase_if(cornerFiles, [&](const std::wstring& f) { return std::find(lost.begin(), lost.end(), f) != lost.end(); });
    cornerSave();
    std::wstring shown = lost[0];
    size_t dot = shown.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) shown = shown.substr(0, dot);
    cornerSayCat = "cornerLost";
    cornerSayArg = shown;
    logf("corner: %zu of her files gone, first '%s'", lost.size(), narrow(lost[0]).c_str());
}

void Pet::cornerStep(float dt, const Input& in, bool awake, bool busy) {
    if (!cornerLoaded) cornerLoad();
    dayStep(dt, in, awake);
    double now = (double)time(nullptr);
    if (cornerPhotoT >= 0 && (cornerPhotoT -= dt) < 0) {
        cornerPhotoT = -1;
        cornerPhotoNow();
    }
    cornerWatchT -= dt;
    if (cornerWatchT <= 0) {
        cornerWatchT = 60.f;
        cornerCheck();
    }
    if (!cornerSayCat.empty() && awake && st != St::Watch && !app.bubble.active && app.now - lastSay > 4) {
        if (app.settings.talk && !quiet()) {
            bool warm = tier() >= 2;
            if (cornerSayCat.rfind("cornerGoneHurt", 0) == 0) {
                tSad = 6.f;
                if (grounded() && !dragging && (st == St::Idle || st == St::Walk || st == St::LookAround)) setState(St::Sit, 9.f);
            } else if (cornerSayCat.rfind("cornerGone", 0) == 0) tSad = 2.2f;
            else if (warm) { tSurprise = 0.5f; tSad = 1.2f; }
            else tAngry = 1.2f;
            sayCat(cornerSayCat.c_str(), 1.f, cornerSayArg, true);
            logf("corner: said %s '%s'", cornerSayCat.c_str(), narrow(app.bubble.text).c_str());
        }
        cornerSayCat.clear();
        cornerSayArg.clear();
    }
    cornerT -= dt;
    if (cornerT > 0) return;
    cornerT = 30.f;
    bool calm = awake && grounded() && !busy && !quiet() && !app.bubble.active && in.idleSec >= 3 && in.idleSec < 60 && app.now - lastSay > 40 &&
                app.now - lastDateSay > 120 && missCat.empty() && lifeT > 180;
    if (!calm || !app.settings.talk) return;
    if (moodAnnounce()) return;
    if (wantStep(in)) return;
    if (!app.settings.hardcoreAsked && !baby() && vit.intro == 0 && !vit.fatal && !vit.dead && app.now >= hardcoreAskAt && metDays(now) >= 1) {
        if (chance(0.3f) && ask("hardcoreAsk", "choiceHardcore", "hardcore")) {
            hardcoreAskAt = app.now + 3600;
            logf("hardcore: asked");
        }
        return;
    }
    if (cornerCanAsk(now)) {
        if (chance(0.35f)) cornerAskNow();
        return;
    }
    if (vit.cornerState == 1 && !cornerIcon && tier() >= 1 && (cornerDecorAny || now - cornerSince > kDay) && chance(cornerDecorAny ? 1.f : 0.2f)) {
        cornerDecorate();
        return;
    }
    if (doodleStep(true)) return;
    if (vit.cornerState == 1 && now - vit.cornerMoodAt > 3 * kDay && chance(0.05f)) cornerMoodNote();
}

int Pet::missBucket(double gap) {
    double h = gap / 3600.0;
    if (h < 2) return -1;
    if (h < 8) return 0;
    if (h < 36) return 1;
    if (h < 96) return 2;
    if (h < 240) return 3;
    return 4;
}

void Pet::missArm() {
    missCat.clear();
    int b = missBucket(missGap);
    if (b < 0 || vit.intro != 0 || vit.dead) {
        if (b >= 0) logf("miss: gap %.1f h, but intro/baby/dead: nothing", missGap / 3600.0);
        return;
    }
    static const char* byGap[5] = {"missYouHours", "missYouDay", "missYouDays", "missYouWeek", "missYouLong"};
    missCat = byGap[b];
    missArg = awayWords(missGap);
    logf("miss: gap %.1f h -> bucket %d tier %d, %s ('%s')", missGap / 3600.0, b, tier(), missCat.c_str(), narrow(missArg).c_str());
}

bool Pet::missSay(bool atGreet) {
    if (missCat.empty() || vit.fatal || baby() || vit.intro != 0) return false;
    int tr = tier();
    if (tr == 3) { tHappy = 2.f; tBlush = 1.6f; }
    else if (tr == 2) tHappy = 1.6f;
    else if (tr == 0) tSquint = 1.f;
    sayCat(missCat.c_str(), 1.f, missArg, true);
    logf("miss: said %s (%s, tier %d) '%s'", missCat.c_str(), atGreet ? "instead of the greeting" : "first calm moment", tr, narrow(app.bubble.text).c_str());
    missSaid = missCat;
    if (missGap >= 36 * 3600.0 && tr >= 1) cornerPhoto(L"я рада");
    missCat.clear();
    lastGreetAt = app.now;
    warmT = std::max(warmT, 240.f);
    return true;
}

void Pet::missStep(bool awake, bool busy) {
    if (missCat.empty()) return;
    if (vit.dead || lifeT > 900.f) {
        logf("miss: dropped %s", missCat.c_str());
        missCat.clear();
        return;
    }
    if (greeted && awake && grounded() && !busy && !app.bubble.active && notesQueue.empty() && app.now - lastSay > 6 && lifeT > 2.f) missSay(false);
}
