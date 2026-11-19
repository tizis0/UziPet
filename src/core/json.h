#pragma once
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>

struct Json {
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::string> k;

    const Json& operator[](const char* key) const {
        if (t == Obj)
            for (size_t i = 0; i < k.size(); i++)
                if (k[i] == key) return a[i];
        return nul();
    }
    const Json& operator[](size_t i) const { return (t == Arr && i < a.size()) ? a[i] : nul(); }
    const Json& operator[](int i) const { return i < 0 ? nul() : (*this)[(size_t)i]; }
    bool has(const char* key) const { return &(*this)[key] != &nul(); }
    size_t size() const { return (t == Arr || t == Obj) ? a.size() : 0; }
    float f(float d = 0) const { return t == Num ? (float)n : d; }
    int i(int d = 0) const { return t == Num ? (int)n : d; }
    bool flag(bool d = false) const { return t == Bool ? b : (t == Num ? n != 0 : d); }
    std::string str(const std::string& d = "") const { return t == Str ? s : d; }
    static const Json& nul() { static Json j; return j; }

    static Json parse(const std::string& src, bool* ok = nullptr) {
        P p{src.data(), src.data() + src.size()};
        Json j = p.value();
        if (ok) *ok = p.ok;
        return j;
    }

private:
    struct P {
        const char* p;
        const char* e;
        bool ok = true;
        void ws() {
            for (;;) {
                while (p < e && (unsigned char)*p <= ' ') p++;
                if (p + 1 < e && p[0] == '/' && p[1] == '/') { while (p < e && *p != '\n') p++; continue; }
                break;
            }
        }
        bool lit(const char* w) {
            size_t n = strlen(w);
            if ((size_t)(e - p) >= n && !strncmp(p, w, n)) { p += n; return true; }
            return false;
        }
        unsigned hex4() {
            unsigned v = 0;
            for (int i = 0; i < 4 && p < e; i++, p++) {
                char c = *p;
                v <<= 4;
                if (c >= '0' && c <= '9') v |= c - '0';
                else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            }
            return v;
        }
        static void utf8(std::string& o, unsigned cp) {
            if (cp < 0x80) o += (char)cp;
            else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 63)); }
            else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
            else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 63)); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
        }
        std::string string() {
            p++;
            std::string o;
            while (p < e && *p != '"') {
                if (*p == '\\') {
                    p++;
                    if (p >= e) break;
                    char c = *p++;
                    switch (c) {
                    case 'n': o += '\n'; break;
                    case 't': o += '\t'; break;
                    case 'r': o += '\r'; break;
                    case 'b': o += '\b'; break;
                    case 'f': o += '\f'; break;
                    case 'u': {
                        unsigned cp = hex4();
                        if (cp >= 0xD800 && cp <= 0xDBFF && e - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                            p += 2;
                            unsigned lo = hex4();
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        utf8(o, cp);
                        break;
                    }
                    default: o += c;
                    }
                } else {
                    o += *p++;
                }
            }
            if (p < e) p++; else ok = false;
            return o;
        }
        int depth = 0;
        Json value() {
            if (depth >= 64) { ok = false; p = e; return Json{}; }
            depth++;
            Json j = node();
            depth--;
            return j;
        }
        Json node() {
            ws();
            Json j;
            if (p >= e) { ok = false; return j; }
            char c = *p;
            if (c == '{') {
                p++;
                j.t = Obj;
                ws();
                if (p < e && *p == '}') { p++; return j; }
                while (p < e) {
                    ws();
                    if (p >= e || *p != '"') { ok = false; return j; }
                    std::string key = string();
                    ws();
                    if (p >= e || *p != ':') { ok = false; return j; }
                    p++;
                    j.k.push_back(key);
                    j.a.push_back(value());
                    ws();
                    if (p < e && *p == ',') { p++; continue; }
                    if (p < e && *p == '}') { p++; return j; }
                    ok = false;
                    return j;
                }
                ok = false;
                return j;
            }
            if (c == '[') {
                p++;
                j.t = Arr;
                ws();
                if (p < e && *p == ']') { p++; return j; }
                while (p < e) {
                    j.a.push_back(value());
                    ws();
                    if (p < e && *p == ',') { p++; continue; }
                    if (p < e && *p == ']') { p++; return j; }
                    ok = false;
                    return j;
                }
                ok = false;
                return j;
            }
            if (c == '"') { j.t = Str; j.s = string(); return j; }
            if (lit("true")) { j.t = Bool; j.b = true; return j; }
            if (lit("false")) { j.t = Bool; j.b = false; return j; }
            if (lit("null")) return j;
            char* end = nullptr;
            double v = strtod(p, &end);
            if (end == p) { ok = false; p++; return j; }
            p = end;
            j.t = Num;
            j.n = v;
            return j;
        }
    };
};
