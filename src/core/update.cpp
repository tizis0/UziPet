#include "update.h"
#include "json.h"
#include <shlobj.h>

namespace {
bool run(std::wstring cmd, DWORD waitMs, DWORD* code = nullptr) {
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
    bool done = true;
    if (waitMs) done = WaitForSingleObject(pi.hProcess, waitMs) == WAIT_OBJECT_0;
    if (code) GetExitCodeProcess(pi.hProcess, code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return done;
}

std::wstring tempDir(const std::wstring& name) {
    wchar_t t[MAX_PATH];
    GetTempPathW(MAX_PATH, t);
    std::wstring d = std::wstring(t) + L"UziUpdate";
    CreateDirectoryW(d.c_str(), nullptr);
    d += L"\\" + name;
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

std::wstring tarExe() {
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    return std::wstring(sys) + L"\\tar.exe";
}

UpdatePkg parseManifest(const std::wstring& path) {
    UpdatePkg p;
    std::string txt;
    if (!readFile(path, txt)) return p;
    bool ok = false;
    Json j = Json::parse(txt, &ok);
    if (!ok || j["app"].str() != "UziPet") return p;
    p.version = widen(j["version"].str());
    for (size_t i = 0; i < j["notes"].size(); i++) p.notes.push_back(widen(j["notes"][i].str()));
    p.ok = !p.version.empty();
    return p;
}
}

int compareVersions(const std::wstring& a, const std::wstring& b) {
    auto parts = [](const std::wstring& v) {
        std::vector<int> o;
        int cur = 0;
        bool any = false;
        for (wchar_t c : v) {
            if (c >= L'0' && c <= L'9') { cur = cur * 10 + (c - L'0'); any = true; }
            else if (c == L'.') { o.push_back(cur); cur = 0; any = false; }
        }
        if (any || !o.empty()) o.push_back(cur);
        return o;
    };
    auto x = parts(a), y = parts(b);
    for (size_t i = 0; i < std::max(x.size(), y.size()); i++) {
        int p = i < x.size() ? x[i] : 0, q = i < y.size() ? y[i] : 0;
        if (p != q) return p > q ? 1 : -1;
    }
    return 0;
}

UpdatePkg readPackage(const std::wstring& zip) {
    UpdatePkg p;
    std::wstring dir = tempDir(L"probe");
    std::wstring man = dir + L"\\uzipet.json";
    DeleteFileW(man.c_str());
    run(L"\"" + tarExe() + L"\" -xf \"" + zip + L"\" -C \"" + dir + L"\" uzipet.json", 15000);
    p = parseManifest(man);
    p.zip = zip;
    logf("package '%s': ok %d version %s", narrow(zip).c_str(), (int)p.ok, narrow(p.version).c_str());
    return p;
}

bool applyUpdate(const UpdatePkg& p) {
    if (!p.ok) return false;
    std::wstring dir = tempDir(L"v" + p.version + L"_" + std::to_wstring((long long)GetTickCount64()));
    DWORD code = 1;
    if (!run(L"\"" + tarExe() + L"\" -xf \"" + p.zip + L"\" -C \"" + dir + L"\"", 120000, &code) || code != 0) {
        logf("update: unpack failed (%lu)", code);
        return false;
    }
    if (GetFileAttributesW((dir + L"\\UziPet.exe").c_str()) == INVALID_FILE_ATTRIBUTES) {
        logf("update: no UziPet.exe in package");
        return false;
    }
    std::wstring ps1 = dir + L".ps1";
    std::string script =
        "param([int]$ProcId, [string]$Src, [string]$Dst, [string]$Exe)\r\n"
        "try { Wait-Process -Id $ProcId -Timeout 30 -ErrorAction SilentlyContinue } catch {}\r\n"
        "Start-Sleep -Milliseconds 400\r\n"
        "$bak = \"$Dst.prev\"\r\n"
        "if (Test-Path -LiteralPath $bak) { Remove-Item -LiteralPath $bak -Recurse -Force -ErrorAction SilentlyContinue }\r\n"
        "Copy-Item -LiteralPath $Dst -Destination $bak -Recurse -Force\r\n"
        "robocopy $Src $Dst /E /R:5 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null\r\n"
        "Start-Process -FilePath $Exe -ArgumentList '--updated'\r\n";
    if (!writeFile(ps1, std::string("\xEF\xBB\xBF") + script)) return false;
    std::wstring bin = exeDir();
    std::wstring cmd = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + ps1 + L"\" -ProcId " +
                       std::to_wstring(GetCurrentProcessId()) + L" -Src \"" + dir + L"\" -Dst \"" + bin + L"\" -Exe \"" + bin + L"\\UziPet.exe\"";
    if (!run(cmd, 0)) {
        logf("update: installer did not start");
        return false;
    }
    logf("update: installing %s, closing", narrow(p.version).c_str());
    return true;
}

namespace {
UpdatePkg manifestOf(HMODULE m) {
    UpdatePkg p;
    HRSRC r = FindResourceW(m, L"UZIMANIFEST", RT_RCDATA);
    if (!r) return p;
    HGLOBAL g = LoadResource(m, r);
    const char* d = g ? (const char*)LockResource(g) : nullptr;
    if (!d) return p;
    std::string txt(d, SizeofResource(m, r));
    bool ok = false;
    Json j = Json::parse(txt, &ok);
    if (!ok || j["app"].str() != "UziPet") return p;
    p.version = widen(j["version"].str());
    for (size_t i = 0; i < j["notes"].size(); i++) p.notes.push_back(widen(j["notes"][i].str()));
    p.ok = !p.version.empty();
    return p;
}

std::wstring selfPath() {
    wchar_t b[MAX_PATH * 2];
    GetModuleFileNameW(nullptr, b, MAX_PATH * 2);
    return b;
}

void makeDirs(const std::wstring& path) {
    for (size_t i = 3; i < path.size(); i++)
        if (path[i] == L'\\' || path[i] == L'/') CreateDirectoryW(path.substr(0, i).c_str(), nullptr);
    CreateDirectoryW(path.c_str(), nullptr);
}
}

std::vector<std::wstring> installedNotes() { return manifestOf(nullptr).notes; }

UpdatePkg readExe(const std::wstring& exe) {
    UpdatePkg p;
    HMODULE m = LoadLibraryExW(exe.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!m) return p;
    p = manifestOf(m);
    FreeLibrary(m);
    p.zip = exe;
    logf("exe '%s': ok %d version %s", narrow(exe).c_str(), (int)p.ok, narrow(p.version).c_str());
    return p;
}

bool applyExeUpdate(const std::wstring& newExe) {
    std::wstring cur = selfPath(), old = cur + L".old";
    if (_wcsicmp(cur.c_str(), newExe.c_str()) == 0) return false;
    DeleteFileW(old.c_str());
    if (!MoveFileExW(cur.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        logf("update: cannot move running exe (%lu)", GetLastError());
        return false;
    }
    if (!CopyFileW(newExe.c_str(), cur.c_str(), FALSE)) {
        logf("update: copy failed (%lu), rolling back", GetLastError());
        MoveFileExW(old.c_str(), cur.c_str(), MOVEFILE_REPLACE_EXISTING);
        return false;
    }
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + cur + L"\" --updated --wait-pid " + std::to_wstring(GetCurrentProcessId());
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        logf("update: new exe did not start (%lu), rolling back", GetLastError());
        DeleteFileW(cur.c_str());
        MoveFileExW(old.c_str(), cur.c_str(), MOVEFILE_REPLACE_EXISTING);
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    logf("update: replaced exe, restarting");
    return true;
}

void cleanupOldExe() { DeleteFileW((selfPath() + L".old").c_str()); }

bool extractEmbedded(const std::wstring& dir) {
    HRSRC r = FindResourceW(nullptr, L"ASSETS", RT_RCDATA);
    if (!r) return false;
    HGLOBAL g = LoadResource(nullptr, r);
    const uint8_t* d = g ? (const uint8_t*)LockResource(g) : nullptr;
    size_t n = SizeofResource(nullptr, r), at = 0;
    if (!d || n < 4) return false;
    auto u32 = [&]() { uint32_t v; memcpy(&v, d + at, 4); at += 4; return v; };
    auto u16 = [&]() { uint16_t v; memcpy(&v, d + at, 2); at += 2; return v; };
    uint32_t cnt = u32();
    for (uint32_t i = 0; i < cnt && at < n; i++) {
        uint16_t ln = u16();
        std::string name((const char*)d + at, ln);
        at += ln;
        uint32_t sz = u32();
        if (at + sz > n) return false;
        std::wstring path = dir + L"\\" + widen(name);
        for (auto& c : path) if (c == L'/') c = L'\\';
        makeDirs(path.substr(0, path.find_last_of(L'\\')));
        writeFile(path, std::string((const char*)d + at, sz));
        at += sz;
    }
    writeFile(dir + L"\\assets\\version.txt", narrow(UZI_VERSION));
    logf("assets extracted: %u files to %s", cnt, narrow(dir).c_str());
    return true;
}

void refreshAssets(bool force) {
    std::wstring dir = exeDir();
    std::string mark;
    bool marked = readFile(dir + L"\\assets\\version.txt", mark);
    if (force || (marked && widen(mark) != UZI_VERSION)) extractEmbedded(dir);
}

bool selfInstall() {
    std::wstring here = exeDir();
    if (GetFileAttributesW((here + L"\\assets\\rig.json").c_str()) != INVALID_FILE_ATTRIBUTES) return false;
    if (!FindResourceW(nullptr, L"ASSETS", RT_RCDATA)) return false;
    wchar_t buf[MAX_PATH * 2];
    std::wstring dir;
    bool test = GetEnvironmentVariableW(L"UZIPET_HOME", buf, MAX_PATH * 2) > 0;
    if (test) dir = buf;
    else {
        wchar_t* la = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &la))) return false;
        dir = std::wstring(la) + L"\\Programs\\UziPet";
        CoTaskMemFree(la);
    }
    makeDirs(dir);
    std::wstring dst = dir + L"\\UziPet.exe";
    if (_wcsicmp(selfPath().c_str(), dst.c_str()) != 0) {
        std::wstring old = dst + L".old";
        DeleteFileW(old.c_str());
        if (GetFileAttributesW(dst.c_str()) != INVALID_FILE_ATTRIBUTES) MoveFileExW(dst.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
        if (!CopyFileW(selfPath().c_str(), dst.c_str(), FALSE)) {
            logf("install: copy failed (%lu)", GetLastError());
            return false;
        }
    }
    extractEmbedded(dir);
    wchar_t* pr = nullptr;
    if (!test && SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &pr))) {
        ComPtr<IShellLinkW> sl;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl)))) {
            sl->SetPath(dst.c_str());
            sl->SetWorkingDirectory(dir.c_str());
            sl->SetDescription(L"Узи на рабочем столе");
            ComPtr<IPersistFile> pf;
            if (SUCCEEDED(sl.As(&pf))) pf->Save((std::wstring(pr) + L"\\Узи.lnk").c_str(), TRUE);
        }
        CoTaskMemFree(pr);
    }
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + dst + L"\"";
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    logf("installed to %s", narrow(dir).c_str());
    return true;
}
