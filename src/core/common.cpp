#include "common.h"
#include <cstdarg>

static std::mt19937& rng() {
    static std::mt19937 r{(unsigned)GetTickCount64() ^ 0x9e3779b9u};
    return r;
}
float randf(float a, float b) { std::uniform_real_distribution<float> d(a, b); return d(rng()); }
int randi(int a, int b) { std::uniform_int_distribution<int> d(a, b); return d(rng()); }
bool chance(float p) { return randf(0, 1) < p; }

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

bool readFile(const std::wstring& path, std::string& out) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? n : 0);
    if (n > 0) fread(out.data(), 1, n, f);
    fclose(f);
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
        out.erase(0, 3);
    return true;
}

bool writeFile(const std::wstring& path, const std::string& data) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    bool written = fwrite(data.data(), 1, data.size(), f) == data.size();
    return fclose(f) == 0 && written;
}

std::wstring exeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    return p.substr(0, p.find_last_of(L"\\/"));
}

bool waitIniReadable(const std::wstring& path, const wchar_t* section, int ms) {
    std::wstring tag = L"[" + std::wstring(section) + L"]";
    std::string tagA(tag.begin(), tag.end()), tagW((const char*)tag.c_str(), tag.size() * sizeof(wchar_t));
    for (int t = 0;; t += 100) {
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return true;
        wchar_t buf[256];
        if (GetPrivateProfileSectionW(section, buf, 256, path.c_str()) > 0) return true;
        std::string raw;
        if (readFile(path, raw) && raw.find(tagA) == std::string::npos && raw.find(tagW) == std::string::npos) return true;
        if (t >= ms) return false;
        Sleep(100);
    }
}

static std::wstring g_appDir;

void setAppDataDir(const std::wstring& d) {
    g_appDir = d;
    CreateDirectoryW(d.c_str(), nullptr);
}

std::wstring appDataDir() {
    std::wstring& dir = g_appDir;
    if (!dir.empty()) return dir;
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p))) {
        dir = std::wstring(p) + L"\\UziPet";
        CoTaskMemFree(p);
    } else {
        dir = exeDir();
    }
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

void logf(const char* fmt, ...) {
    static FILE* f = nullptr;
    if (!f) {
        std::wstring lp = exeDir() + L"\\uzipet.log";
        MoveFileExW(lp.c_str(), (exeDir() + L"\\uzipet.prev.log").c_str(), MOVEFILE_REPLACE_EXISTING);
        f = _wfopen(lp.c_str(), L"w");
    }
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    SYSTEMTIME st;
    GetLocalTime(&st);
    if (f) {
        fprintf(f, "[%02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);
        fflush(f);
    }
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
}

D2D1_COLOR_F parseColor(const std::string& h, float a) {
    const char* p = h.c_str();
    if (*p == '#') p++;
    unsigned v = (unsigned)strtoul(p, nullptr, 16);
    if (strlen(p) == 8)
        return {((v >> 24) & 255) / 255.f, ((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f * a};
    return {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f, a};
}

double nowSec() {
    static LARGE_INTEGER freq{};
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)freq.QuadPart;
}
