#include "pet.h"
#include <ctime>

namespace {

const double kDay = 86400.0;

std::wstring dayPath() { return appDataDir() + L"\\day.txt"; }

int dayKeyOf(time_t t) {
    tm lt;
    localtime_s(&lt, &t);
    return (lt.tm_year + 1900) * 1000 + lt.tm_yday;
}

std::wstring dateWords(time_t t) {
    static const wchar_t* months[] = {L"января", L"февраля", L"марта",    L"апреля",  L"мая",    L"июня",
                                      L"июля",   L"августа", L"сентября", L"октября", L"ноября", L"декабря"};
    tm lt;
    localtime_s(&lt, &t);
    return std::to_wstring(lt.tm_mday) + L" " + months[lt.tm_mon];
}

}

int* DayLog::field(const std::string& k) {
    if (k == "kiss") return &kiss;
    if (k == "pet") return &pet;
    if (k == "fed") return &fed;
    if (k == "win") return &win;
    if (k == "lose") return &lose;
    if (k == "cs") return &cs;
    if (k == "csWin") return &csWin;
    if (k == "hug") return &hug;
    if (k == "cold") return &cold;
    return nullptr;
}

bool DayLog::any() const { return kiss || pet || fed || win || lose || cs || hug || cold; }

void Pet::dayLoad() {
    dayLoaded = true;
    dayLog = {};
    std::string raw;
    if (!readFile(dayPath(), raw)) return;
    size_t a = 0;
    while (a < raw.size()) {
        size_t e = raw.find('\n', a);
        if (e == std::string::npos) e = raw.size();
        std::string l = raw.substr(a, e - a);
        a = e + 1;
        if (!l.empty() && l.back() == '\r') l.pop_back();
        size_t tab = l.find('\t');
        if (tab == std::string::npos) continue;
        std::string k = l.substr(0, tab), v = l.substr(tab + 1);
        if (k == "day") dayLog.day = atoi(v.c_str());
        else if (k == "t0") dayLog.t0 = atof(v.c_str());
        else if (k == "min") dayLog.minutes = (float)atof(v.c_str());
        else if (k == "diaryDay") diaryDay = atoi(v.c_str());
        else if (k == "doodleAt") doodleAt = atof(v.c_str());
        else if (k == "doodleNext") doodleNext = atof(v.c_str());
        else if (k == "doodles") doodlesDone = (unsigned)strtoul(v.c_str(), nullptr, 10);
        else if (k == "mood") dayMood = std::clamp(atoi(v.c_str()), 0, 5);
        else if (k == "talks") {
            talksRecent.clear();
            for (size_t p = 0; p < v.size();) {
                size_t q = v.find(',', p);
                if (q == std::string::npos) q = v.size();
                if (q > p) talksRecent.push_back(v.substr(p, q - p));
                p = q + 1;
            }
        }
        else if (k == "moodTold") moodToldDay = atoi(v.c_str());
        else if (int* f = dayLog.field(k)) *f = std::max(0, atoi(v.c_str()));
    }
}

void Pet::daySave() const {
    std::string t = "day\t" + std::to_string(dayLog.day) + "\r\nt0\t" + std::to_string((long long)dayLog.t0) + "\r\n";
    char b[32];
    snprintf(b, sizeof(b), "%.2f", dayLog.minutes);
    t += std::string("min\t") + b + "\r\n";
    DayLog& d = const_cast<DayLog&>(dayLog);
    for (const char* k : {"kiss", "pet", "fed", "win", "lose", "cs", "csWin", "hug", "cold"}) t += std::string(k) + "\t" + std::to_string(*d.field(k)) + "\r\n";
    t += "diaryDay\t" + std::to_string(diaryDay) + "\r\n";
    t += "doodleAt\t" + std::to_string((long long)doodleAt) + "\r\n";
    t += "doodleNext\t" + std::to_string((long long)doodleNext) + "\r\n";
    t += "doodles\t" + std::to_string(doodlesDone) + "\r\n";
    {
        std::string tl;
        for (auto& x : talksRecent) tl += (tl.empty() ? "" : ",") + x;
        t += "talks\t" + tl + "\r\n";
    }
    t += "mood\t" + std::to_string(dayMood) + "\r\nmoodTold\t" + std::to_string(moodToldDay) + "\r\n";
    if (!writeFile(dayPath(), t)) logf("day: can't save");
}

void Pet::dayRoll() {
    if (!dayLoaded) dayLoad();
    time_t now = time(nullptr);
    int k = dayKeyOf(now);
    if (dayLog.day == k) return;
    if (dayLog.day != 0 && dayLog.day != diaryDay) {
        if (cornerDiary(dayLog)) diaryDay = dayLog.day;
    }
    moodRoll(dayLog);
    dayLog = {};
    dayLog.day = k;
    dayLog.t0 = (double)now;
    daySave();
}

void Pet::dayCount(const char* what) {
    dayRoll();
    if (int* f = dayLog.field(what)) {
        (*f)++;
        daySave();
    }
}

void Pet::dayStep(float dt, const Input& in, bool awake) {
    dayRoll();
    if (in.idleSec < 120) dayLog.minutes += dt / 60.f;
    daySaveT -= dt;
    if (daySaveT <= 0) {
        daySaveT = 60.f;
        daySave();
        app.phrases.saveState();
    }
    (void)awake;
}

std::wstring timesWords(int n);

bool Pet::cornerDiary(const DayLog& d) {
    if (!cornerLoaded) cornerLoad();
    if (vit.cornerState != 1 || cornerDir.empty() || baby() || vit.dead) return false;
    int tr = tier();
    std::string lv;
    std::vector<std::wstring> lines;
    auto put = [&](const std::string& cat, const std::wstring& arg = L"") {
        std::wstring s = plainLine(app.phrases.pick(cat.c_str(), arg));
        if (!s.empty()) lines.push_back(s);
    };
    if (d.minutes < 1.f && !d.any()) {
        put("diaryAway" + lv);
    } else {
        std::wstring dur = awayWords(std::max(d.minutes, 5.f) * 60.0);
        put((d.minutes >= 180 ? "diaryLong" : d.minutes < 45 ? "diaryShort" : "diaryMid") + lv, dur);
        std::vector<std::pair<std::string, int>> ev;
        if (d.kiss) ev.push_back({"diaryKiss", d.kiss});
        if (d.pet) ev.push_back({"diaryPet", d.pet});
        if (d.fed) ev.push_back({"diaryFed", d.fed});
        if (d.win) ev.push_back({"diaryWin", d.win});
        if (d.lose) ev.push_back({"diaryLose", d.lose});
        if (d.cs) ev.push_back({d.csWin ? "diaryCsWin" : "diaryCs", d.cs});
        if (d.hug) ev.push_back({"diaryHug", d.hug});
        if (d.cold) ev.push_back({"diaryCold", d.cold});
        for (int i = (int)ev.size() - 1; i > 0; i--) std::swap(ev[i], ev[randi(0, i)]);
        if (ev.size() > 3) ev.resize(3);
        for (auto& [cat, n] : ev) put(cat + lv, timesWords(n));
        if (ev.empty()) put("diaryQuiet" + lv);
    }
    put("diaryEnd");
    if (lines.size() < 2) return false;
    std::wstring entry = dateWords((time_t)(d.t0 > 0 ? d.t0 : (double)time(nullptr) - kDay)) + L"\r\n";
    for (auto& l : lines) entry += l + L"\r\n";
    const wchar_t* name = L"дневник.txt";
    std::string bytes, old;
    if (cornerOwn(name) && readFile(cornerDir + L"\\" + name, old) && !old.empty()) {
        if (old.size() > 256 * 1024) {
            logf("diary: file too big already, not adding");
            return false;
        }
        bytes = "\xEF\xBB\xBF" + old;
        if (bytes.size() < 2 || bytes.compare(bytes.size() - 2, 2, "\r\n") != 0) bytes += "\r\n";
        bytes += "\r\n" + narrow(entry);
    } else {
        bytes = "\xEF\xBB\xBF" + narrow(entry);
    }
    if (!cornerWrite(name, bytes)) return false;
    logf("diary: entry for %s, %zu lines (min %.0f, kiss %d pet %d fed %d win %d lose %d cs %d hug %d)", narrow(dateWords((time_t)d.t0)).c_str(), lines.size(),
         d.minutes, d.kiss, d.pet, d.fed, d.win, d.lose, d.cs, d.hug);
    return true;
}

namespace {

struct Crayon {
    ID2D1RenderTarget* rt;
    ID2D1Factory* f;
    ID2D1SolidColorBrush* br;
    ID2D1StrokeStyle* round;
    float seed = 0;

    std::vector<V2> wobble(const std::vector<V2>& pts, bool closed, float amp, float ph) {
        std::vector<V2> out;
        size_t n = pts.size();
        if (n < 2) return pts;
        size_t segs = closed ? n : n - 1;
        float run = 0;
        for (size_t i = 0; i < segs; i++) {
            V2 a = pts[i], b = pts[(i + 1) % n];
            float len = std::max(1.f, sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y)));
            int k = std::max(1, (int)(len / 5.f));
            for (int j = 0; j < k; j++) {
                float t = (float)j / k;
                run += len / k;
                float o = sinf(run * 0.045f + ph + seed) * amp + sinf(run * 0.11f + ph * 1.7f) * amp * 0.5f;
                out.push_back(V2(a.x + (b.x - a.x) * t + o * 0.6f, a.y + (b.y - a.y) * t + o));
            }
        }
        if (!closed) out.push_back(pts.back());
        return out;
    }

    void stroke(const std::vector<V2>& pts, D2D1_COLOR_F c, float w, bool closed = false) {
        for (int pass = 0; pass < 2; pass++) {
            auto p = wobble(pts, closed, 1.3f + pass * 0.6f, pass * 2.3f);
            ComPtr<ID2D1PathGeometry> g;
            ComPtr<ID2D1GeometrySink> s;
            if (FAILED(f->CreatePathGeometry(&g)) || FAILED(g->Open(&s))) return;
            s->BeginFigure(D2D1::Point2F(p[0].x, p[0].y), D2D1_FIGURE_BEGIN_HOLLOW);
            for (size_t i = 1; i < p.size(); i++) s->AddLine(D2D1::Point2F(p[i].x, p[i].y));
            s->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
            s->Close();
            br->SetColor(c);
            br->SetOpacity(pass ? 0.55f : 0.9f);
            rt->DrawGeometry(g.Get(), br, pass ? w * 0.6f : w, round);
        }
    }

    void fill(const std::vector<V2>& pts, D2D1_COLOR_F c) {
        ComPtr<ID2D1PathGeometry> g;
        ComPtr<ID2D1GeometrySink> s;
        if (FAILED(f->CreatePathGeometry(&g)) || FAILED(g->Open(&s))) return;
        s->BeginFigure(D2D1::Point2F(pts[0].x, pts[0].y), D2D1_FIGURE_BEGIN_FILLED);
        for (size_t i = 1; i < pts.size(); i++) s->AddLine(D2D1::Point2F(pts[i].x, pts[i].y));
        s->EndFigure(D2D1_FIGURE_END_CLOSED);
        s->Close();
        br->SetColor(c);
        br->SetOpacity(0.25f);
        rt->FillGeometry(g.Get(), br);
        D2D1_RECT_F bb{};
        g->GetBounds(nullptr, &bb);
        ComPtr<ID2D1Layer> layer;
        rt->CreateLayer(&layer);
        rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), g.Get()), layer.Get());
        br->SetOpacity(0.55f);
        float h = bb.bottom - bb.top;
        for (float x = bb.left - h; x < bb.right; x += 4.5f)
            rt->DrawLine(D2D1::Point2F(x, bb.bottom), D2D1::Point2F(x + h * 0.8f, bb.top), br, 2.2f, round);
        rt->PopLayer();
    }
};

std::vector<V2> ellipse(V2 c, float rx, float ry, int n = 28) {
    std::vector<V2> p;
    for (int i = 0; i < n; i++) {
        float a = i * 6.2831853f / n;
        p.push_back(V2(c.x + cosf(a) * rx, c.y + sinf(a) * ry));
    }
    return p;
}

std::vector<V2> heart(V2 c, float s) {
    std::vector<V2> p;
    for (int i = 0; i < 40; i++) {
        float t = i * 6.2831853f / 40;
        float x = 16 * powf(sinf(t), 3), y = 13 * cosf(t) - 5 * cosf(2 * t) - 2 * cosf(3 * t) - cosf(4 * t);
        p.push_back(V2(c.x + x * s, c.y - y * s));
    }
    return p;
}

const D2D1_COLOR_F kInk{0.16f, 0.12f, 0.22f, 1}, kPurple{0.55f, 0.20f, 0.85f, 1}, kPink{0.93f, 0.32f, 0.55f, 1}, kYellow{0.98f, 0.80f, 0.15f, 1},
    kGreen{0.30f, 0.65f, 0.30f, 1}, kBlue{0.25f, 0.50f, 0.90f, 1}, kRed{0.90f, 0.25f, 0.25f, 1}, kGray{0.45f, 0.45f, 0.50f, 1};

void stickman(Crayon& c, V2 at, float k, D2D1_COLOR_F col) {
    c.stroke(ellipse(V2(at.x, at.y - 70 * k), 16 * k, 17 * k), col, 3.2f, true);
    c.stroke({V2(at.x, at.y - 53 * k), V2(at.x, at.y)}, col, 3.2f);
    c.stroke({V2(at.x - 26 * k, at.y - 30 * k), V2(at.x, at.y - 42 * k), V2(at.x + 26 * k, at.y - 30 * k)}, col, 3.2f);
    c.stroke({V2(at.x - 18 * k, at.y + 40 * k), V2(at.x, at.y), V2(at.x + 18 * k, at.y + 40 * k)}, col, 3.2f);
}

void herself(Crayon& c, V2 at, float k) {
    std::vector<V2> visor{V2(at.x - 30 * k, at.y - 62 * k), V2(at.x + 30 * k, at.y - 62 * k), V2(at.x + 32 * k, at.y - 20 * k), V2(at.x - 32 * k, at.y - 20 * k)};
    c.fill(visor, kInk);
    c.stroke(visor, kInk, 3.f, true);
    c.stroke({V2(at.x - 18 * k, at.y - 36 * k), V2(at.x - 11 * k, at.y - 45 * k), V2(at.x - 4 * k, at.y - 36 * k)}, kPurple, 3.4f);
    c.stroke({V2(at.x + 4 * k, at.y - 36 * k), V2(at.x + 11 * k, at.y - 45 * k), V2(at.x + 18 * k, at.y - 36 * k)}, kPurple, 3.4f);
    std::vector<V2> hat;
    for (int i = 0; i <= 14; i++) {
        float a = 3.14159f + i * 3.14159f / 14;
        hat.push_back(V2(at.x + cosf(a) * 36 * k, at.y - 60 * k + sinf(a) * 30 * k));
    }
    c.fill(hat, kPurple);
    c.stroke(hat, kPurple, 3.f, true);
    c.stroke({V2(at.x - 34 * k, at.y - 70 * k), V2(at.x + 34 * k, at.y - 70 * k)}, kInk, 2.6f);
    c.stroke(ellipse(V2(at.x, at.y - 94 * k), 8 * k, 8 * k, 12), kPurple, 3.f, true);
    for (float side : {-1.f, 1.f}) {
        std::vector<V2> h{V2(at.x + side * 30 * k, at.y - 66 * k), V2(at.x + side * 42 * k, at.y - 46 * k), V2(at.x + side * 34 * k, at.y - 40 * k),
                          V2(at.x + side * 46 * k, at.y - 22 * k), V2(at.x + side * 33 * k, at.y - 24 * k)};
        c.fill(h, kPurple);
        c.stroke(h, kPurple, 2.8f);
    }
    std::vector<V2> body{V2(at.x - 22 * k, at.y - 18 * k), V2(at.x + 22 * k, at.y - 18 * k), V2(at.x + 30 * k, at.y + 30 * k), V2(at.x - 30 * k, at.y + 30 * k)};
    c.fill(body, kInk);
    c.stroke(body, kInk, 3.f, true);
    c.stroke({V2(at.x - 12 * k, at.y + 30 * k), V2(at.x - 12 * k, at.y + 55 * k)}, kInk, 3.2f);
    c.stroke({V2(at.x + 12 * k, at.y + 30 * k), V2(at.x + 12 * k, at.y + 55 * k)}, kInk, 3.2f);
}

}

struct DoodleKind {
    const wchar_t* file;
    const wchar_t* captions[2];
    int tier;
};
static const DoodleKind kDoodles[] = {
    {L"нарисовала тебя", {L"это ты. за компом. как всегда", L"это ты. похоже же"}, 1},
    {L"нарисовала себя", {L"это я. очень похоже", L"я. не смейся"}, 1},
    {L"нарисовала кота", {L"кот. просто кот", L"кот. хочу кота"}, 1},
    {L"нарисовала дом", {L"дом. когда-нибудь", L"домик. с солнцем"}, 1},
    {L"нарисовала дрона", {L"страшный дрон. но не очень", L"это не я. это другой дрон"}, 1},
    {L"нарисовала нас", {L"мы", L"ты и я. я выше"}, 2},
    {L"нарисовала сердечко", {L"просто так", L"не придумывай ничего"}, 2},
};
int doodleKinds() { return (int)(sizeof(kDoodles) / sizeof(kDoodles[0])); }

bool pngOf(IWICImagingFactory* wic, int w, int h, const uint32_t* px, std::string& out);

bool Pet::doodleRender(int kind, std::string& png, std::wstring& caption) {
    if (kind < 0 || kind >= doodleKinds()) return false;
    const int W = 360, H = 300;
    Surface s;
    if (!s.create(app.gfx, W, H)) return false;
    ID2D1RenderTarget* rt = s.begin(D2D1::ColorF(0.995f, 0.985f, 0.95f, 1));
    if (!rt) return false;
    Crayon c{rt, app.gfx.d2d.Get(), s.brush.Get(), app.gfx.round.Get(), randf(0, 6.28f)};
    s.brush->SetColor(D2D1::ColorF(0.55f, 0.65f, 0.85f, 1));
    s.brush->SetOpacity(0.18f);
    for (int x = 12; x < W; x += 18) rt->DrawLine(D2D1::Point2F((float)x, 0), D2D1::Point2F((float)x, (float)H), s.brush.Get(), 1.f);
    for (int y = 12; y < H; y += 18) rt->DrawLine(D2D1::Point2F(0, (float)y), D2D1::Point2F((float)W, (float)y), s.brush.Get(), 1.f);
    switch (kind) {
        case 0: {
            std::vector<V2> mon{V2(170, 90), V2(300, 90), V2(300, 175), V2(170, 175)};
            c.fill(mon, kBlue);
            c.stroke(mon, kInk, 3.f, true);
            c.stroke({V2(235, 175), V2(235, 200)}, kInk, 3.f);
            c.stroke({V2(205, 202), V2(265, 202)}, kInk, 3.f);
            stickman(c, V2(110, 175), 1.f, kInk);
            c.stroke({V2(40, 215), V2(320, 215)}, kGray, 2.4f);
            c.stroke({V2(222, 128), V2(228, 120), V2(234, 128)}, kPurple, 2.6f);
            c.stroke({V2(238, 128), V2(244, 120), V2(250, 128)}, kPurple, 2.6f);
            break;
        }
        case 1:
            herself(c, V2(180, 150), 1.25f);
            break;
        case 2: {
            auto head = ellipse(V2(150, 120), 42, 36);
            c.fill(head, kGray);
            c.stroke(head, kInk, 3.f, true);
            c.stroke({V2(118, 98), V2(116, 64), V2(140, 86)}, kInk, 3.f);
            c.stroke({V2(160, 86), V2(184, 64), V2(182, 98)}, kInk, 3.f);
            c.stroke(ellipse(V2(136, 116), 4, 5, 10), kInk, 3.f, true);
            c.stroke(ellipse(V2(164, 116), 4, 5, 10), kInk, 3.f, true);
            c.stroke({V2(145, 132), V2(150, 137), V2(155, 132)}, kPink, 2.6f);
            for (int i = -1; i <= 1; i++) {
                c.stroke({V2(128, 134 + i * 6.f), V2(96, 130 + i * 10.f)}, kInk, 1.8f);
                c.stroke({V2(172, 134 + i * 6.f), V2(204, 130 + i * 10.f)}, kInk, 1.8f);
            }
            auto body = ellipse(V2(190, 200), 60, 36);
            c.fill(body, kGray);
            c.stroke(body, kInk, 3.f, true);
            c.stroke({V2(248, 196), V2(285, 170), V2(300, 130), V2(290, 110)}, kInk, 3.2f);
            break;
        }
        case 3: {
            std::vector<V2> wall{V2(90, 140), V2(210, 140), V2(210, 230), V2(90, 230)};
            c.fill(wall, kYellow);
            c.stroke(wall, kInk, 3.f, true);
            std::vector<V2> roof{V2(78, 142), V2(150, 80), V2(222, 142)};
            c.fill(roof, kRed);
            c.stroke(roof, kInk, 3.f, true);
            c.stroke({V2(135, 230), V2(135, 185), V2(165, 185), V2(165, 230)}, kInk, 3.f);
            std::vector<V2> win{V2(178, 160), V2(200, 160), V2(200, 180), V2(178, 180)};
            c.fill(win, kBlue);
            c.stroke(win, kInk, 2.6f, true);
            auto sun = ellipse(V2(290, 60), 26, 26);
            c.fill(sun, kYellow);
            c.stroke(sun, kYellow, 3.4f, true);
            for (int i = 0; i < 8; i++) {
                float a = i * 0.785f;
                c.stroke({V2(290 + cosf(a) * 34, 60 + sinf(a) * 34), V2(290 + cosf(a) * 48, 60 + sinf(a) * 48)}, kYellow, 3.f);
            }
            std::vector<V2> grass;
            for (int x = 20; x <= 340; x += 12) grass.push_back(V2((float)x, (x / 12) % 2 ? 226.f : 236.f));
            c.stroke(grass, kGreen, 3.f);
            break;
        }
        case 4: {
            std::vector<V2> visor{V2(130, 80), V2(230, 80), V2(240, 150), V2(120, 150)};
            c.fill(visor, kInk);
            c.stroke(visor, kInk, 3.f, true);
            for (float ex : {155.f, 205.f}) {
                c.stroke({V2(ex - 12, 103), V2(ex + 12, 127)}, kYellow, 4.f);
                c.stroke({V2(ex + 12, 103), V2(ex - 12, 127)}, kYellow, 4.f);
            }
            c.stroke({V2(130, 100), V2(60, 60), V2(40, 120), V2(125, 130)}, kInk, 3.f);
            c.stroke({V2(230, 100), V2(300, 60), V2(320, 120), V2(235, 130)}, kInk, 3.f);
            c.stroke({V2(150, 150), V2(150, 215), V2(140, 232)}, kInk, 3.f);
            c.stroke({V2(210, 150), V2(210, 215), V2(220, 232)}, kInk, 3.f);
            break;
        }
        case 5: {
            stickman(c, V2(120, 175), 1.05f, kInk);
            herself(c, V2(235, 175), 0.95f);
            c.stroke({V2(146, 140), V2(176, 150), V2(206, 160)}, kInk, 3.f);
            auto st = std::vector<V2>{};
            for (int i = 0; i < 10; i++) {
                float a = -1.5708f + i * 0.6283f, r = i % 2 ? 9.f : 20.f;
                st.push_back(V2(180 + cosf(a) * r, 48 + sinf(a) * r));
            }
            c.fill(st, kYellow);
            c.stroke(st, kYellow, 3.f, true);
            break;
        }
        default: {
            auto h = heart(V2(180, 120), 5.2f);
            c.fill(h, kPink);
            c.stroke(h, kPink, 4.f, true);
            c.stroke(heart(V2(180, 120), 6.2f), kPurple, 2.f, true);
            break;
        }
    }
    const DoodleKind& dk = kDoodles[kind];
    caption = dk.captions[randi(0, 1)];
    for (float size = 21.f; size >= 12.f; size -= 1.5f) {
        ComPtr<IDWriteTextFormat> f;
        ComPtr<IDWriteTextLayout> lay;
        app.gfx.dw->CreateTextFormat(L"Segoe Print", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"ru-ru", &f);
        if (!f) break;
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        DWRITE_TEXT_METRICS m{};
        if (FAILED(app.gfx.dw->CreateTextLayout(caption.c_str(), (UINT32)caption.size(), f.Get(), W - 20.f, 60.f, &lay)) || FAILED(lay->GetMetrics(&m))) break;
        if (m.widthIncludingTrailingWhitespace > W - 24.f && size > 13.f) continue;
        s.brush->SetColor(kPurple);
        s.brush->SetOpacity(0.95f);
        rt->DrawTextLayout(D2D1::Point2F(10, 248 + (21.f - size) * 0.6f), lay.Get(), s.brush.Get());
        break;
    }
    if (!s.end(app.gfx)) return false;
    return pngOf(app.gfx.wic.Get(), W, H, s.bits, png);
}

bool Pet::cornerDoodle(int kind) {
    if (!cornerLoaded) cornerLoad();
    if (!dayLoaded) dayLoad();
    if (vit.cornerState != 1 || cornerDir.empty() || baby()) return false;
    int tr = tier();
    if (kind < 0) {
        std::vector<int> fresh, all;
        for (int i = 0; i < doodleKinds(); i++) {
            if (kDoodles[i].tier > tr) continue;
            all.push_back(i);
            if (!(doodlesDone & (1u << i))) fresh.push_back(i);
        }
        auto& from = fresh.empty() ? all : fresh;
        if (from.empty()) return false;
        kind = from[randi(0, (int)from.size() - 1)];
    }
    std::string png;
    std::wstring cap;
    if (!doodleRender(kind, png, cap)) {
        logf("doodle %d: not drawn", kind);
        return false;
    }
    std::wstring n = cornerFreeName(kDoodles[kind].file, L".png");
    if (n.empty() || !cornerWrite(n, png)) return false;
    doodlesDone |= 1u << kind;
    doodleAt = (double)time(nullptr);
    doodleNext = doodleAt + randf(1.5f, 3.5f) * kDay;
    daySave();
    cornerIconSync();
    logf("doodle: '%s' (%zu bytes), caption '%s'", narrow(n).c_str(), png.size(), narrow(cap).c_str());
    return true;
}

bool Pet::doodleStep(bool calm) {
    if (!dayLoaded) dayLoad();
    double now = (double)time(nullptr);
    if (!calm || vit.cornerState != 1 || tier() < 1 || baby() || vit.fatal) return false;
    if (doodleNext <= 0) {
        doodleNext = std::max(now, cornerSince + kDay);
        daySave();
    }
    if (now < doodleNext || !chance(0.15f)) return false;
    if (!cornerDoodle(-1)) {
        doodleNext = now + kDay;
        daySave();
        return false;
    }
    tHappy = 1.2f;
    if (tier() >= 2) tBlush = 1.f;
    if (grounded() && !dragging && (st == St::Idle || st == St::LookAround)) setState(St::Sit, 6.f);
    sayCat("doodleDone", 1.f, L"", true);
    return true;
}
