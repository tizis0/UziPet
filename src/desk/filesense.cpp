#include "filesense.h"
#include <thread>
#include <algorithm>

namespace {

const float kPurple = 0.12f, kDark = 0.15f, kBoth = 0.35f, kEye = 0.004f, kEdge = 0.015f;
const float kPurpleTop = 0.7f;
const float kSelfName = 0.5f;
const float kSelfMaybe = 0.3f;
const float kFolderW = 0.4f;
const int kBgTol = 24;

std::wstring lowerW(const std::wstring& s) {
    if (s.empty()) return s;
    std::wstring o(s.size() + 4, 0);
    int n = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, s.c_str(), (int)s.size(), o.data(), (int)o.size(), nullptr, nullptr, 0);
    if (n <= 0) o = s;
    else o.resize(n);
    for (auto& c : o)
        if (c == L'ё') c = L'е';
    return o;
}
bool isUp(wchar_t c) { return IsCharUpperW(c) != 0; }
bool isLow(wchar_t c) { return IsCharLowerW(c) != 0; }
bool isDig(wchar_t c) { return c >= L'0' && c <= L'9'; }
bool isLet(wchar_t c) { return IsCharAlphaW(c) != 0; }

bool inList(const std::wstring& w, std::initializer_list<const wchar_t*> l) {
    for (auto e : l)
        if (w == e) return true;
    return false;
}

bool capsWord(const std::wstring& w) { return inList(w, {L"uzi", L"узи", L"md", L"smg", L"imi", L"cyn", L"n", L"v", L"j"}); }

struct Toks {
    std::vector<std::wstring> t;
    std::vector<std::wstring> glued;
    bool capsRu = false;
};

Toks tokenize(const std::wstring& s) {
    Toks r;
    size_t i = 0, n = s.size();
    while (i < n) {
        if (isDig(s[i])) {
            size_t a = i;
            while (i < n && isDig(s[i])) i++;
            r.t.push_back(s.substr(a, i - a));
            continue;
        }
        if (!isLet(s[i])) { i++; continue; }
        size_t a = i;
        while (i < n && isLet(s[i])) i++;
        std::wstring run = s.substr(a, i - a);
        r.glued.push_back(lowerW(run));
        std::vector<size_t> cuts{0};
        size_t seg = 0;
        for (size_t k = 1; k < run.size(); k++) {
            if (isLow(run[k - 1]) && isUp(run[k])) {
                cuts.push_back(k);
                seg = k;
            } else if (isUp(run[k - 1]) && isUp(run[k]) && k + 1 < run.size() && isLow(run[k + 1])) {
                if (capsWord(lowerW(run.substr(seg, k + 1 - seg)))) {
                    cuts.push_back(k + 1);
                    seg = k + 1;
                    k++;
                } else {
                    cuts.push_back(k);
                    seg = k;
                }
            }
        }
        cuts.push_back(run.size());
        for (size_t c = 0; c + 1 < cuts.size(); c++)
            if (cuts[c + 1] > cuts[c]) {
                std::wstring part = run.substr(cuts[c], cuts[c + 1] - cuts[c]);
                r.t.push_back(lowerW(part));
                if (part == L"УЗИ") r.capsRu = true;
            }
    }
    return r;
}

bool has(const Toks& k, std::initializer_list<const wchar_t*> l) {
    for (auto& w : k.t)
        if (inList(w, l)) return true;
    return false;
}
bool hasGlued(const Toks& k, std::initializer_list<const wchar_t*> l) {
    for (auto& w : k.glued)
        if (inList(w, l)) return true;
    return false;
}
const std::wstring* stem(const Toks& k, std::initializer_list<const wchar_t*> l) {
    for (auto& w : k.t)
        for (auto e : l)
            if (w.rfind(e, 0) == 0) return &w;
    return nullptr;
}
bool letter(const Toks& k, std::initializer_list<const wchar_t*> l) {
    for (size_t i = 0; i < k.t.size(); i++)
        if (inList(k.t[i], l) && !(i + 1 < k.t.size() && isDig(k.t[i + 1][0]))) return true;
    return false;
}

bool selfSuffix(const std::wstring& r) {
    return inList(r, {L"art", L"arts", L"rig", L"anim", L"animrig", L"animation", L"model", L"render", L"fan", L"fanart", L"doorman", L"face",
                      L"pose", L"ref", L"sketch", L"draw", L"drawing", L"pic", L"wip", L"final", L"v", L"head", L"body", L"mesh", L"skin", L"edit",
                      L"icon", L"pfp", L"avatar", L"sheet", L"sprite", L"atlas", L"tex", L"texture", L"pet", L"cosplay", L"chan", L"арт", L"риг",
                      L"drone", L"drones", L"md", L"murderdrones", L"blender", L"oc", L"nsfw", L"chibi"});
}
bool gunSuffix(const std::wstring& r) { return inList(r, {L"gun", L"smg", L"pistol", L"weapon", L"rifle", L"mini", L"micro"}); }

bool likeness(const std::wstring& w) {
    return inList(w, {L"art", L"arts", L"fanart", L"fan", L"oc", L"chibi", L"drawing", L"draw", L"sketch", L"doodle", L"cosplay", L"pfp", L"avatar",
                      L"figure", L"figurine", L"plush", L"doll", L"чиби", L"арт", L"фанарт", L"рисунок", L"рисунки", L"скетч", L"эскиз", L"косплей",
                      L"фигурка", L"игрушка", L"аватарка", L"аватар"});
}
bool artRu(const std::wstring& w) {
    return inList(w, {L"модель", L"моделька", L"рендер", L"риг", L"иконка", L"обои", L"пиксельный", L"пиксель", L"лицо", L"поза", L"набор", L"деталей",
                      L"анимация", L"скин", L"текстура"});
}
bool joint(const std::wstring& w) { return inList(w, {L"and", L"x", L"vs", L"with", L"by", L"the", L"и", L"с", L"на", L"в", L"от"}); }

int glued(const std::wstring& w, bool media) {
    if (w.size() < 3 || (w.rfind(L"uzi", 0) != 0 && w.rfind(L"узи", 0) != 0)) return 0;
    std::wstring r = w.substr(3);
    if (r.empty() || selfSuffix(r)) return 1;
    if (media && r.size() == 1 && r[0] >= L'a' && r[0] <= L'z') return 1;
    if (gunSuffix(r)) return 2;
    for (size_t j = 1; j < r.size(); j++) {
        std::wstring a = r.substr(0, j), b = r.substr(j);
        if (selfSuffix(a) && (selfSuffix(b) || gunSuffix(b))) return gunSuffix(b) ? 2 : 1;
        if (gunSuffix(a) && selfSuffix(b)) return 2;
    }
    return 0;
}

struct Verdict {
    FileKind kind = FileKind::None, with = FileKind::None;
    float score = 0;
    bool ctx = false;
    bool medGuess = false;
    std::string why;
};

bool next(const Toks& k, std::initializer_list<const wchar_t*> a, bool digit) {
    for (size_t i = 0; i + 1 < k.t.size(); i++)
        if (inList(k.t[i], a) && (!digit || isDig(k.t[i + 1][0]))) return true;
    return false;
}

Verdict judge(const Toks& k, bool ctxIn, bool doc, bool media) {
    Verdict v;
    std::wstring list;
    for (auto& w : k.t) list += (list.empty() ? L"" : L" ") + w;
    v.why = "[" + narrow(list) + "]";
    auto note = [&](const std::string& s) { v.why += " " + s; };

    bool uziLat = false, uziCyr = false, gunGlue = false;
    float uziScore = 0;
    for (auto* set : {&k.t, &k.glued})
        for (auto& w : *set) {
            if (w == L"uzi") uziLat = true, uziScore = 1.f;
            else if (w == L"узи") uziCyr = true, uziScore = 1.f;
            else if (int g = glued(w, media)) {
                (w[0] == L'u' ? uziLat : uziCyr) = true;
                uziScore = std::max(uziScore, 0.9f);
                if (g == 2) gunGlue = true;
                if (v.why.find("glued:" + narrow(w)) == std::string::npos) note("glued:" + narrow(w));
            }
        }
    bool uzi = uziLat || uziCyr;
    bool doorman = has(k, {L"doorman", L"дорман", L"дорманн"}) || hasGlued(k, {L"uzidoorman", L"khandoorman"});
    const std::wstring* med = stem(k, {L"брюшн", L"полост", L"щитовид", L"беремен", L"плод", L"скрининг", L"сердц", L"почк", L"почек", L"надпочеч",
                                       L"таз", L"протокол", L"заключени", L"клиник", L"медиц", L"исследовани", L"печен", L"желч", L"матк",
                                       L"яичник", L"сосуд", L"допплер", L"доплер", L"молочн", L"простат", L"мочев", L"органов", L"недел",
                                       L"триместр", L"малыш", L"ребен", L"результат", L"направлени", L"сустав", L"лимф", L"снимок", L"снимк",
                                       L"скан", L"срок", L"шейк", L"гинеко", L"ultrasound", L"sonogra", L"echograph", L"pochek", L"pochk",
                                       L"shchitovid", L"shitovid", L"bryushn", L"beremen", L"rezultat", L"zaklyuch", L"nedel", L"skrining"});
    if (!med)
        for (auto& w : k.t)
            if (inList(w, {L"мед", L"узд", L"usg", L"sono", L"нед", L"вен", L"вены", L"обп", L"омт", L"мм", L"эхо", L"plod"})) { med = &w; break; }
    bool gun = gunGlue || has(k, {L"gun", L"guns", L"smg", L"pistol", L"weapon", L"weapons", L"rifle", L"imi", L"mini", L"micro", L"submachine",
                                  L"airsoft"}) ||
               stem(k, {L"пистолет", L"автомат", L"оруж", L"ствол", L"пулем"});
    bool episode = next(k, {L"s", L"e", L"ep"}, true) || has(k, {L"episode", L"season", L"pilot", L"серия", L"сезон"});
    bool show = (has(k, {L"murder"}) && has(k, {L"drones", L"drone"})) ||
                hasGlued(k, {L"murderdrones", L"murderdrone", L"absolutesolver", L"glitchproductions"}) || next(k, {L"copper"}, true) ||
                (has(k, {L"md"}) && episode) || (has(k, {L"glitch"}) && has(k, {L"productions", L"production"})) ||
                (has(k, {L"absolute"}) && has(k, {L"solver"})) || (has(k, {L"solver", L"disassembly"}) && (uzi || doorman || ctxIn)) ||
                (has(k, {L"disassembly"}) && has(k, {L"drones", L"drone", L"squad"})) ||
                (has(k, {L"дроны", L"дрон", L"дронов"}) && stem(k, {L"убийц"})) || (has(k, {L"serial"}) && has(k, {L"designation"})) ||
                (has(k, {L"worker"}) && has(k, {L"drone", L"drones"}));
    int other = 0, numbers = 0;
    bool arty = false;
    for (auto& w : k.t) {
        if (isDig(w[0])) { numbers++; continue; }
        if (w == L"uzi" || w == L"узи" || glued(w, media) || joint(w) ||
            inList(w, {L"doorman", L"дорман", L"дорманн", L"n", L"v", L"j", L"эн", L"ви", L"джей", L"cyn", L"цин", L"khan", L"хан", L"tessa",
                       L"тесса", L"murder", L"drones", L"drone", L"дроны", L"дрон", L"дронов", L"md", L"copper", L"solver", L"absolute"}) ||
            w.rfind(L"убийц", 0) == 0)
            continue;
        if (selfSuffix(w) || likeness(w) || artRu(w)) arty = true;
        else other++;
    }
    int letters = (int)letter(k, {L"n", L"эн"}) + (int)letter(k, {L"v", L"ви"}) + (int)letter(k, {L"j", L"джей"});
    bool onlyLetters = letters >= 2;
    for (auto& w : k.t)
        if (!isDig(w[0]) && !joint(w) && !inList(w, {L"n", L"v", L"j", L"эн", L"ви", L"джей"})) onlyLetters = false;
    v.ctx = show || uzi || doorman || ctxIn || onlyLetters;
    if (show) note("show");

    FileKind ch = FileKind::None;
    bool khan = has(k, {L"khan", L"хан"}) && (doorman || v.ctx);
    if (khan) ch = FileKind::Khan;
    else if (has(k, {L"cyn", L"cynessa", L"цин", L"синесса"}) || (v.ctx && has(k, {L"син"}))) ch = FileKind::Cyn;
    else if (v.ctx && letter(k, {L"n", L"эн"})) ch = FileKind::N;
    else if (v.ctx && letter(k, {L"v", L"ви"})) ch = FileKind::V;
    else if (v.ctx && letter(k, {L"j", L"джей"})) ch = FileKind::J;
    else if (has(k, {L"tessa", L"тесса"}) && (v.ctx || has(k, {L"elliot", L"эллиот"}))) ch = FileKind::Tessa;

    bool herWords = doorman || show || arty || ch != FileKind::None || ctxIn;
    bool ruGuess = uziCyr && !uziLat && !herWords && (doc || k.capsRu) && (other > 0 || numbers > 0);
    if (uzi && med && !doorman) {
        v.kind = FileKind::Medical;
        v.score = 1.f;
        note("medical:" + narrow(*med));
    } else if (ruGuess) {
        v.kind = FileKind::Medical;
        v.score = 0.5f;
        v.medGuess = true;
        note(other > 0 ? "medical?(ru uzi + other words)" : "medical?(ru uzi + number)");
    } else if (uzi && gun && !doorman && !show && !ctxIn && !has(k, {L"art", L"arts", L"fanart", L"fan", L"oc", L"chibi", L"drawing", L"draw",
                                                                         L"sketch", L"doodle", L"cosplay", L"pfp", L"avatar", L"figure",
                                                                         L"figurine", L"plush", L"doll", L"чиби", L"арт", L"фанарт", L"рисунок",
                                                                         L"фигурка", L"игрушка"})) {
        v.kind = FileKind::Gun;
        v.score = 1.f;
        note("gun");
    } else if (uzi) {
        v.kind = FileKind::Self;
        v.score = uziScore;
        v.with = ch;
        note(uziCyr && !uziLat ? (doc ? "uzi(ru, doc)" : "uzi(ru)") : "uzi");
        if (gun) note("with gun");
    } else if (doorman && !khan) {
        v.kind = FileKind::Self;
        v.score = ctxIn || show || arty ? 0.6f : other == 0 ? 0.35f : 0.2f;
        note(v.score > 0.5f ? "doorman+art" : other == 0 ? "doorman alone" : "doorman among other words");
    } else if (ch != FileKind::None) {
        v.kind = ch;
        v.score = 1.f;
    } else if (show) {
        v.kind = FileKind::Show;
        v.score = 1.f;
    }
    if (ch != FileKind::None) note(std::string("char:") + fileKindName(ch));
    return v;
}

std::wstring extOf(const std::wstring& name) {
    size_t dot = name.find_last_of(L'.');
    return dot == std::wstring::npos || dot == 0 ? L"" : lowerW(name.substr(dot + 1));
}

bool is3dExt(const std::wstring& e) {
    return inList(e, {L"blend", L"blend1", L"fbx", L"obj", L"gltf", L"glb", L"bbmodel", L"uasset", L"abc", L"vrm", L"pmx", L"ma", L"mb", L"max",
                      L"dae", L"usd", L"usdz", L"stl"});
}

}

const char* fileKindName(FileKind k) {
    static const char* n[] = {"none", "self", "medical", "gun", "show", "N", "V", "J", "Cyn", "Khan", "Tessa"};
    return n[(int)k];
}

const wchar_t* fileKindSay(FileKind k) {
    switch (k) {
    case FileKind::N: return L"N";
    case FileKind::V: return L"V";
    case FileKind::J: return L"J";
    case FileKind::Cyn: return L"Син";
    case FileKind::Khan: return L"папа";
    case FileKind::Tessa: return L"Тесса";
    default: return L"";
    }
}

bool senseWantsPixels(const std::wstring& path) {
    DWORD at = GetFileAttributesW(path.c_str());
    if (at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_DIRECTORY)) return false;
    std::wstring e = extOf(path.substr(path.find_last_of(L"\\/") + 1));
    return is3dExt(e) || inList(e, {L"png", L"jpg", L"jpeg", L"jfif", L"gif", L"webp", L"bmp", L"psd", L"kra", L"ico", L"tif", L"tiff", L"heic",
                                     L"avif", L"mp4", L"mkv", L"mov", L"avi", L"webm", L"wmv", L"m4v"});
}

FileSense senseName(const std::wstring& path) {
    FileSense fs;
    size_t slash = path.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    std::wstring parent;
    if (slash != std::wstring::npos) {
        std::wstring dir = path.substr(0, slash);
        size_t s2 = dir.find_last_of(L"\\/");
        parent = s2 == std::wstring::npos ? dir : dir.substr(s2 + 1);
        if (parent.size() == 2 && parent[1] == L':') parent.clear();
    }
    std::wstring ext = extOf(name);
    std::wstring base = ext.empty() ? name : name.substr(0, name.size() - ext.size() - 1);
    fs.is3d = is3dExt(ext);
    bool doc = inList(ext, {L"pdf", L"doc", L"docx", L"rtf", L"odt", L"txt"});
    bool media = senseWantsPixels(name);

    Verdict f;
    if (!parent.empty()) f = judge(tokenize(parent), false, false, false);
    bool fctx = f.ctx && f.kind != FileKind::None && f.kind != FileKind::Medical && f.kind != FileKind::Gun;
    Verdict v = judge(tokenize(base), fctx, doc, media);
    fs.kind = v.kind;
    fs.with = v.with;
    fs.nameScore = v.score;
    fs.medGuess = v.medGuess;
    fs.why = "name " + v.why;
    if (f.kind == FileKind::Self) {
        fs.folderScore = kFolderW * f.score;
        fs.why += " | folder " + f.why;
    } else if (f.kind == FileKind::Show && fs.kind == FileKind::None) {
        fs.kind = FileKind::Show;
        fs.nameScore = kFolderW;
        fs.why += " | folder " + f.why;
    }
    fs.self = fs.kind == FileKind::Self && fs.nameScore >= kSelfName ? 2 : 0;
    return fs;
}

void senseColors(FileSense& fs, const uint8_t* bgra, int w, int h, bool hasAlpha) {
    int opaque = 0, bgN = 0, n = 0, pu = 0, dk = 0, ey = 0, pairs = 0, edges = 0;
    std::vector<float> lum((size_t)w * h, -1.f);
    std::vector<uint8_t> rgb((size_t)w * h * 3);
    for (int i = 0; i < w * h; i++) {
        const uint8_t* p = bgra + i * 4;
        int a = hasAlpha ? p[3] : 255;
        if (a < 128) continue;
        float b = p[0], g = p[1], r = p[2];
        if (a < 255) {
            float k = 255.f / a;
            b = std::min(255.f, b * k), g = std::min(255.f, g * k), r = std::min(255.f, r * k);
        }
        rgb[i * 3] = (uint8_t)r, rgb[i * 3 + 1] = (uint8_t)g, rgb[i * 3 + 2] = (uint8_t)b;
        lum[i] = (0.299f * r + 0.587f * g + 0.114f * b) / 255.f;
    }
    int bgc[3] = {}, ring = 0;
    bool bgEdge = false;
    {
        std::vector<int> bins(4096, 0);
        std::vector<int> border;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                if ((x < 2 || y < 2 || x >= w - 2 || y >= h - 2) && lum[y * w + x] >= 0) {
                    const uint8_t* c = &rgb[(y * w + x) * 3];
                    bins[(c[0] >> 4) << 8 | (c[1] >> 4) << 4 | (c[2] >> 4)]++;
                    border.push_back(y * w + x);
                }
        int top = (int)(std::max_element(bins.begin(), bins.end()) - bins.begin()), cnt = 0;
        long sum[3] = {};
        for (int i : border) {
            const uint8_t* c = &rgb[i * 3];
            if (((c[0] >> 4) << 8 | (c[1] >> 4) << 4 | (c[2] >> 4)) != top) continue;
            for (int j = 0; j < 3; j++) sum[j] += c[j];
            cnt++;
        }
        if (cnt) {
            for (int j = 0; j < 3; j++) bgc[j] = (int)(sum[j] / cnt);
            for (int i : border) {
                const uint8_t* c = &rgb[i * 3];
                if (std::max({abs(c[0] - bgc[0]), abs(c[1] - bgc[1]), abs(c[2] - bgc[2])}) <= kBgTol) ring++;
            }
            bgEdge = border.size() >= 20 && ring >= (int)border.size() * 6 / 10;
        }
    }
    std::vector<char> bgMask((size_t)w * h, 0);
    if (bgEdge) {
        auto bgLike = [&](int i) {
            const uint8_t* c = &rgb[i * 3];
            return lum[i] >= 0 && std::max({abs(c[0] - bgc[0]), abs(c[1] - bgc[1]), abs(c[2] - bgc[2])}) <= kBgTol;
        };
        std::vector<int> st;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                if ((x == 0 || y == 0 || x == w - 1 || y == h - 1) && bgLike(y * w + x)) bgMask[y * w + x] = 1, st.push_back(y * w + x);
        while (!st.empty()) {
            int i = st.back(), x = i % w, y = i / w;
            st.pop_back();
            for (int j : {x > 0 ? i - 1 : -1, x + 1 < w ? i + 1 : -1, y > 0 ? i - w : -1, y + 1 < h ? i + w : -1})
                if (j >= 0 && !bgMask[j] && bgLike(j)) bgMask[j] = 1, st.push_back(j);
        }
    }
    for (int i = 0; i < w * h; i++) {
        if (lum[i] < 0) continue;
        opaque++;
        const uint8_t* c = &rgb[i * 3];
        float r = c[0], g = c[1], b = c[2];
        float mx = std::max(r, std::max(g, b)) / 255.f, mn = std::min(r, std::min(g, b)) / 255.f;
        float v = mx, s = mx > 0 ? (mx - mn) / mx : 0, hue = 0, d = mx - mn;
        if (v > 0.88f && s < 0.12f) { bgN++; continue; }
        if (bgMask[i]) { bgN++; continue; }
        n++;
        if (v < 0.20f) dk++;
        if (d > 1e-4f) {
            float R = r / 255.f, G = g / 255.f, B = b / 255.f;
            if (mx == R) hue = 60.f * fmodf((G - B) / d, 6.f);
            else if (mx == G) hue = 60.f * ((B - R) / d + 2.f);
            else hue = 60.f * ((R - G) / d + 4.f);
            if (hue < 0) hue += 360.f;
        }
        if (s > 0.30f && v > 0.18f && hue >= 255.f && hue <= 320.f) pu++;
        if (s > 0.55f && v > 0.70f && hue >= 275.f && hue <= 325.f) ey++;
    }
    for (int y = 0; y + 1 < h; y++)
        for (int x = 0; x + 1 < w; x++) {
            float c = lum[y * w + x];
            if (c < 0) continue;
            for (float o : {lum[y * w + x + 1], lum[(y + 1) * w + x]}) {
                if (o < 0) continue;
                pairs++;
                if (fabsf(c - o) > 0.2f) edges++;
            }
        }
    fs.hadPixels = true;
    if (bgEdge) {
        char t[48];
        snprintf(t, sizeof(t), " | border bg #%02X%02X%02X", bgc[0], bgc[1], bgc[2]);
        fs.why += t;
    }
    fs.bg = opaque ? (float)bgN / opaque : 1.f;
    fs.edge = pairs ? (float)edges / pairs : 0.f;
    fs.purple = n ? (float)pu / n : 0.f;
    fs.dark = n ? (float)dk / n : 0.f;
    fs.eye = n ? (float)ey / n : 0.f;
    fs.colorScore = 0;
    if (n < std::max(30, opaque / 20)) return;
    float sc = std::min({fs.purple / kPurple, fs.dark / kDark, (fs.purple + fs.dark) / kBoth, fs.edge / kEdge,
                         (1.f - fs.purple) / (1.f - kPurpleTop)});
    fs.colorScore = std::min(sc, 1.5f) + (fs.eye >= kEye ? 0.2f : 0.f);
}

bool senseThumb(const std::wstring& path, int px, FileSense& fs) {
    double t0 = nowSec();
    ComPtr<IShellItem> it;
    ComPtr<IShellItemImageFactory> f;
    HBITMAP hb = nullptr;
    HRESULT hr = SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&it));
    if (SUCCEEDED(hr)) hr = it.As(&f);
    if (SUCCEEDED(hr)) hr = f->GetImage({px, px}, SIIGBF_THUMBNAILONLY, &hb);
    bool ok = false;
    if (SUCCEEDED(hr) && hb) {
        BITMAP bm{};
        GetObjectW(hb, sizeof(bm), &bm);
        int w = bm.bmWidth, h = abs(bm.bmHeight);
        if (w > 0 && h > 0) {
            std::vector<uint8_t> buf((size_t)w * h * 4);
            BITMAPINFO bi{};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = w;
            bi.bmiHeader.biHeight = -h;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            HDC dc = GetDC(nullptr);
            int got = GetDIBits(dc, hb, 0, h, buf.data(), &bi, DIB_RGB_COLORS);
            ReleaseDC(nullptr, dc);
            if (got == h) {
                bool alpha = false;
                if (bm.bmBitsPixel == 32)
                    for (size_t i = 3; i < buf.size(); i += 4)
                        if (buf[i]) { alpha = true; break; }
                senseColors(fs, buf.data(), w, h, alpha);
                ok = true;
            }
        }
        DeleteObject(hb);
    }
    fs.ms = (nowSec() - t0) * 1000.0;
    char b[64];
    if (!ok) snprintf(b, sizeof(b), " | no thumb hr=0x%08X", (unsigned)hr);
    else snprintf(b, sizeof(b), " | thumb %.0f ms", fs.ms);
    fs.why += b;
    return ok;
}

void senseFinish(FileSense& fs) {
    bool col = fs.hadPixels && fs.colorScore >= 1.f;
    if (fs.kind == FileKind::Medical && fs.medGuess && col) {
        fs.kind = FileKind::Self;
        fs.why += " | colours overrule medical?";
    }
    bool byName = fs.kind == FileKind::Self && fs.nameScore >= kSelfName;
    bool hint = fs.kind == FileKind::Self && !byName;
    if (byName || (hint && col)) fs.self = col ? 3 : 2;
    else if (hint && fs.nameScore >= kSelfMaybe) fs.self = 1;
    else if ((fs.kind == FileKind::None || fs.kind == FileKind::Show) && col) {
        fs.self = fs.folderScore > 0 ? 3 : 1;
        fs.kind = FileKind::Self;
    } else {
        fs.self = 0;
        if (fs.kind == FileKind::Self) fs.kind = FileKind::None;
    }
    char b[160];
    snprintf(b, sizeof(b), " => self %d kind %s%s%s", fs.self, fileKindName(fs.kind), fs.with != FileKind::None ? " with " : "",
             fs.with != FileKind::None ? fileKindName(fs.with) : "");
    fs.why += b;
}

FileSense senseFile(const std::wstring& path) {
    FileSense fs = senseName(path);
    if (senseWantsPixels(path)) senseThumb(path, 96, fs);
    senseFinish(fs);
    return fs;
}

std::shared_ptr<SenseJob> senseStart(const std::wstring& path, const FileSense& byName) {
    auto job = std::make_shared<SenseJob>();
    job->fs = byName;
    std::thread([job, path] {
        HRESULT ci = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        senseThumb(path, 96, job->fs);
        if (SUCCEEDED(ci)) CoUninitialize();
        senseFinish(job->fs);
        job->done = true;
    }).detach();
    return job;
}
