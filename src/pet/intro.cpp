#include "intro.h"
#include "items.h"
#include <ctime>

namespace {

enum class IS { None, Parcel, Dark, AskName, AskGender, Kick, Hatch, Grow, Explode };
IS is = IS::None;
float isT = 0;
bool test = false;
Vitals saved;
std::wstring savedName;
int savedGender = 1;
RECT mon{}, work{};
V2 parPos;
bool parLanded = false, parClicked = false, babyOut = false;
float landT = 0;
HWND parWnd = nullptr, ovWnd = nullptr;
Surface parSurf, ovSurf;
float dimA = 0, whiteA = 0, zoom = 1;
const float kZoom = 1.7f;
int kickStep = 0;
float kickT = 0;
struct Spark { V2 p, v; float t, life, r; bool pink; };
std::vector<Spark> sparks;
V2 novaAt;
float novaK = 0, novaGrow = 1, novaT = 0;
bool adultOut = false;
ComPtr<ID2D1Bitmap> raysBmp, streakBmp;
int raysGen = -1;
float frameAcc = 0;

LRESULT CALLBACK parProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_SETCURSOR: SetCursor(LoadCursor(nullptr, IDC_HAND)); return TRUE;
    case WM_LBUTTONUP: parClicked = true; return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

HWND makeWnd(const wchar_t* cls, WNDPROC proc, bool clickThrough) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);
    DWORD ex = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | (clickThrough ? WS_EX_TRANSPARENT : 0);
    return CreateWindowExW(ex, cls, L"", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, wc.hInstance, nullptr);
}

void monitorOf(POINT pt) {
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY), &mi);
    mon = mi.rcMonitor;
    work = mi.rcWork;
}

void raise(App& a) {
    auto top = [](HWND h) {
        if (h && IsWindowVisible(h)) SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    };
    if (is == IS::Grow || is == IS::Explode) {
        top(a.hwnd);
        top(ovWnd);
    } else {
        top(ovWnd);
        top(parWnd);
        top(a.hwnd);
    }
}

void bakeNova(App& a, ID2D1RenderTarget* rt) {
    raysBmp.Reset();
    streakBmp.Reset();
    const float B = 1024, Rb = 150;
    ComPtr<ID2D1BitmapRenderTarget> brt;
    if (FAILED(rt->CreateCompatibleRenderTarget(D2D1::SizeF(B, B), &brt))) return;
    brt->BeginDraw();
    brt->Clear(D2D1::ColorF(0, 0, 0, 0));
    D2D1_POINT_2F c{B / 2, B / 2};
    auto radial = [&](ID2D1RenderTarget* t, D2D1_POINT_2F at, float r, std::initializer_list<D2D1_GRADIENT_STOP> st) {
        ComPtr<ID2D1GradientStopCollection> sc;
        t->CreateGradientStopCollection(st.begin(), (UINT32)st.size(), &sc);
        ComPtr<ID2D1RadialGradientBrush> rb;
        if (sc && SUCCEEDED(t->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(at, {0, 0}, r, r), sc.Get(), &rb)))
            t->FillEllipse(D2D1::Ellipse(at, r, r), rb.Get());
    };
    radial(brt.Get(), c, Rb * 1.7f, {{0.f, D2D1::ColorF(1, 1, 1, 0.9f)}, {0.15f, D2D1::ColorF(0.92f, 0.84f, 1.f, 0.65f)},
                                     {0.45f, D2D1::ColorF(0.77f, 0.42f, 1.f, 0.22f)}, {1.f, D2D1::ColorF(0.47f, 0.24f, 0.78f, 0.f)}});
    uint32_t seed = 7;
    auto rnd = [&]() { seed = (uint32_t)((seed * 16807ull) % 2147483647ull); return (seed - 1) / 2147483646.f; };
    struct Ray { float a, l, w; };
    std::vector<Ray> rays;
    for (int i = 0; i < 28; i++) {
        float ra = rnd(), rl = rnd(), rl2 = rnd(), rw = rnd();
        rays.push_back({i * 2 * PI / 28 + (ra - 0.5f) * 0.16f, 0.7f + rl * rl2 * 2.4f, 0.45f + rw * 1.1f});
    }
    struct Pass { D2D1_COLOR_F col; float a, ls, da; };
    Pass passes[3] = {{D2D1::ColorF(1, 1, 1, 1), 1.f, 1.f, 0.f}, {D2D1::ColorF(1.f, 0.55f, 0.82f, 1), 0.32f, 1.07f, 0.014f},
                      {D2D1::ColorF(0.55f, 0.82f, 1.f, 1), 0.28f, 0.94f, -0.014f}};
    for (auto& P : passes)
        for (auto& r : rays) {
            float ang = r.a + P.da, L = Rb * r.l * P.ls, wd = Rb * 0.085f * r.w;
            V2 d(cosf(ang), sinf(ang));
            D2D1_GRADIENT_STOP st[3] = {{0.f, D2D1::ColorF(P.col.r, P.col.g, P.col.b, 0.8f * P.a)},
                                        {0.35f, D2D1::ColorF(P.col.r, P.col.g, P.col.b, 0.35f * P.a)},
                                        {1.f, D2D1::ColorF(P.col.r, P.col.g, P.col.b, 0.f)}};
            ComPtr<ID2D1GradientStopCollection> sc;
            brt->CreateGradientStopCollection(st, 3, &sc);
            ComPtr<ID2D1LinearGradientBrush> lb;
            if (!sc || FAILED(brt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(c, {c.x + d.x * L, c.y + d.y * L}), sc.Get(), &lb))) continue;
            auto tri = makePath(a.gfx.d2d.Get(), [&](ID2D1GeometrySink* k) {
                k->BeginFigure({c.x - d.y * wd, c.y + d.x * wd}, D2D1_FIGURE_BEGIN_FILLED);
                k->AddLine({c.x + d.x * L, c.y + d.y * L});
                k->AddLine({c.x + d.y * wd, c.y - d.x * wd});
                k->EndFigure(D2D1_FIGURE_END_CLOSED);
            });
            brt->FillGeometry(tri.Get(), lb.Get());
        }
    radial(brt.Get(), c, Rb * 0.5f, {{0.f, D2D1::ColorF(1, 1, 1, 1)}, {1.f, D2D1::ColorF(1, 1, 1, 0)}});
    brt->EndDraw();
    brt->GetBitmap(&raysBmp);
    ComPtr<ID2D1BitmapRenderTarget> srt;
    if (SUCCEEDED(rt->CreateCompatibleRenderTarget(D2D1::SizeF(1024, 64), &srt))) {
        srt->BeginDraw();
        srt->Clear(D2D1::ColorF(0, 0, 0, 0));
        srt->SetTransform(D2D1::Matrix3x2F::Scale(1.f, 64.f / 1024.f, {512, 32}));
        radial(srt.Get(), {512, 32}, 512, {{0.f, D2D1::ColorF(1, 1, 1, 0.9f)}, {0.4f, D2D1::ColorF(0.86f, 0.75f, 1.f, 0.35f)},
                                          {1.f, D2D1::ColorF(0.63f, 0.47f, 1.f, 0.f)}});
        srt->EndDraw();
        srt->GetBitmap(&streakBmp);
    }
}

void drawOverlay(App& a, bool animated) {
    bool show = dimA > 0.003f || whiteA > 0.003f || novaK > 0.003f || !sparks.empty();
    if (!show) {
        if (ovWnd && IsWindowVisible(ovWnd)) ShowWindow(ovWnd, SW_HIDE);
        return;
    }
    if (!ovWnd) ovWnd = makeWnd(L"UziIntroOverlay", DefWindowProcW, true);
    int W = mon.right - mon.left - 2, H = mon.bottom - mon.top - 2;
    if (ovSurf.w != W || ovSurf.h != H) ovSurf.create(a.gfx, W, H);
    static float lastDim = -1, lastWhite = -1, lastZoom = -1;
    if (!animated && fabsf(lastDim - dimA) < 0.01f && fabsf(lastWhite - whiteA) < 0.01f && fabsf(lastZoom - zoom) < 0.005f && IsWindowVisible(ovWnd)) return;
    lastDim = dimA;
    lastWhite = whiteA;
    lastZoom = zoom;
    auto* rt = ovSurf.begin();
    if (!rt) return;
    auto* br = ovSurf.brush.Get();
    float S = a.dpi;
    V2 org((float)mon.left + 1, (float)mon.top + 1);
    if (dimA > 0.003f) {
        float R = 115.f * S * zoom, F = 95.f * S * zoom;
        D2D1_POINT_2F spot{parPos.x - org.x, parPos.y - 30.f * S * zoom - org.y};
        D2D1_GRADIENT_STOP st[2] = {{R / (R + F), D2D1::ColorF(0.016f, 0.02f, 0.04f, 0.f)}, {1.f, D2D1::ColorF(0.016f, 0.02f, 0.04f, dimA)}};
        ComPtr<ID2D1GradientStopCollection> sc;
        rt->CreateGradientStopCollection(st, 2, &sc);
        ComPtr<ID2D1RadialGradientBrush> rb;
        if (sc && SUCCEEDED(rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(spot, {0, 0}, R + F, R + F), sc.Get(), &rb)))
            rt->FillRectangle(D2D1::RectF(0, 0, (float)W, (float)H), rb.Get());
    }
    for (auto& s : sparks) {
        float al = 1.f - s.t / s.life;
        br->SetColor(s.pink ? D2D1::ColorF(0.84f, 0.10f, 0.93f, al) : D2D1::ColorF(0.90f, 0.80f, 0.63f, al));
        rt->FillEllipse(D2D1::Ellipse({s.p.x - org.x, s.p.y - org.y}, s.r, s.r), br);
    }
    if (novaK > 0.003f) {
        if (!raysBmp || raysGen != ovSurf.gen) {
            bakeNova(a, rt);
            raysGen = ovSurf.gen;
        }
        float R = (40.f + 230.f * novaK) * S * novaGrow;
        D2D1_POINT_2F c{novaAt.x - org.x, novaAt.y - org.y};
        float flick = 0.88f + 0.12f * sinf(novaT * 7.3f);
        if (raysBmp) {
            for (int layer = 0; layer < 2; layer++) {
                float ang = layer ? -novaT * 0.42f * 57.3f : novaT * 0.6f * 57.3f, sc = R / 150.f * (layer ? 0.9f : 1.f);
                rt->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc) * D2D1::Matrix3x2F::Rotation(ang) * D2D1::Matrix3x2F::Translation(c.x, c.y));
                rt->DrawBitmap(raysBmp.Get(), D2D1::RectF(-512, -512, 512, 512), std::min(1.f, novaK * (layer ? 0.55f : 1.f) * flick),
                               D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
            rt->SetTransform(D2D1::Matrix3x2F::Identity());
        }
        if (streakBmp)
            rt->DrawBitmap(streakBmp.Get(), D2D1::RectF(c.x - R * 3.4f, c.y - R * 0.12f, c.x + R * 3.4f, c.y + R * 0.12f), std::min(1.f, novaK),
                           D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    if (whiteA > 0.003f) {
        br->SetColor(D2D1::ColorF(1, 1, 1, whiteA));
        rt->FillRectangle(D2D1::RectF(0, 0, (float)W, (float)H), br);
    }
    ovSurf.end(a.gfx);
    bool first = !IsWindowVisible(ovWnd);
    ovSurf.present(ovWnd, POINT{mon.left + 1, mon.top + 1});
    if (first) {
        ShowWindow(ovWnd, SW_SHOWNOACTIVATE);
        raise(a);
    }
}

void askName(App& a, Pet& p) {
    is = IS::AskName;
    p.introAnchor = V2(parPos.x, parPos.y - 60.f * a.dpi * zoom);
    std::wstring q = a.phrases.pick("introName"), def = a.settings.name;
    a.bubble.askInput(q.empty() ? L"Как вас называть?" : q, def, a.phrases.list("choiceIntroName"), "introName");
    SetForegroundWindow(a.hwnd);
}

void askGender(App& a) {
    is = IS::AskGender;
    std::wstring q = a.phrases.pick("introGender");
    a.bubble.ask(q.empty() ? L"Ваш пол?" : q, a.phrases.list("choiceGender"), "introGender", 1e9f);
}

void hatch(App& a, Pet& p) {
    babyOut = true;
    auto& v = p.vit;
    v.intro = 2;
    v.babyStart = (double)time(nullptr);
    v.metAt = v.babyStart;
    v.annivDone = 0;
    v.babyDrops = 0;
    v.rel = 50;
    v.energy = 100;
    v.food = 36;
    v.stock = 0;
    v.parcelOut = false;
    v.parcelT = 60;
    v.notified = false;
    v.save();
    float S = a.dpi, side = chance(0.5f) ? 1.f : -1.f;
    p.introHidden = false;
    p.napOnLand = true;
    p.selectRig(3);
    a.instBaby.resetSim();
    p.spawn(V2(parPos.x, parPos.y - 50.f * S));
    p.rot = 0;
    float up = sqrtf(2.f * 30.f * p.comH * p.comH * 3.4f);
    p.vel = V2(side * up * 0.22f, -up);
    p.rotVel = side * 9.f;
    for (int i = 0; i < 18; i++) {
        float ang = -PI / 2 + randf(-1.1f, 1.1f), sp = randf(180, 440) * S;
        sparks.push_back({V2(parPos.x + randf(-30, 30) * S, parPos.y - 50.f * S), V2(cosf(ang), sinf(ang)) * sp, 0.f, randf(0.6f, 1.1f),
                          randf(2.f, 5.f) * S, i % 3 == 0});
    }
    logf("intro: hatched");
}

void grown(App& a, Pet& p) {
    adultOut = true;
    auto& v = p.vit;
    int pen = std::min(20, std::max(0, v.babyDrops - 5) * 3);
    v.intro = 0;
    v.rel = 50.f - pen;
    v.energy = 100;
    v.save();
    logf("intro: grown up (drops %d, rel %.0f)", v.babyDrops, v.rel);
    if (test) {
        p.vit = saved;
        a.settings.name = savedName;
        a.settings.gender = savedGender;
        a.phrases.userName = savedName;
        a.phrases.gender = savedGender;
        test = false;
    }
    p.growing = false;
    p.glow = 0;
    p.introHidden = false;
    p.helloOnLand = true;
    p.greeted = true;
    p.selectRig(0);
    a.inst.resetSim();
    p.spawn(novaAt + V2(0, p.comH));
    p.vel = V2(0, -sqrtf(2.f * 2100.f * a.s * p.comH * 1.6f));
}

}

bool introOverlayShown() { return ovWnd && IsWindowVisible(ovWnd); }
bool introRunning() { return is != IS::None; }

void introStart(App& a, Pet& p, bool testRun) {
    if (is != IS::None) return;
    test = testRun;
    if (test) {
        saved = p.vit;
        savedName = a.settings.name;
        savedGender = a.settings.gender;
        p.vit.persist = false;
    }
    monitorOf(POINT{0, 0});
    float S = a.dpi;
    auto& v = p.vit;
    v.intro = 1;
    v.rel = 50;
    v.energy = v.food = 100;
    v.stock = 0;
    v.fatal = v.dead = v.starved = v.inBin = false;
    v.parcelOut = false;
    v.parcelT = 1e9f;
    v.babyDrops = 0;
    v.save();
    a.bubble.close();
    p.dropCarried(false);
    p.dropHeld("intro");
    p.feeding = false;
    p.growing = false;
    p.glow = 0;
    p.introHidden = true;
    p.greeted = true;
    parPos = V2((work.left + work.right) * 0.5f, (float)mon.top - 10.f * S);
    p.introAnchor = parPos;
    p.spawn(V2(parPos.x, (float)work.bottom - 2.f));
    parLanded = parClicked = babyOut = adultOut = false;
    dimA = whiteA = novaK = 0;
    zoom = 1;
    sparks.clear();
    is = IS::Parcel;
    isT = 0;
    logf("intro: start (test %d)", (int)test);
}

void introAnswer(App& a, Pet& p, const std::string& id, int choice) {
    if (id == "introName" && is == IS::AskName) {
        std::wstring n = a.bubble.inputText;
        while (!n.empty() && iswspace(n.back())) n.pop_back();
        while (!n.empty() && iswspace(n.front())) n.erase(n.begin());
        if (n.empty()) {
            askName(a, p);
            return;
        }
        a.settings.name = n;
        a.phrases.userName = n;
        if (!test) a.settings.save();
        logf("intro: name set");
        askGender(a);
    } else if (id == "introGender" && is == IS::AskGender) {
        a.settings.gender = choice == 1 ? 2 : 1;
        a.phrases.gender = a.settings.gender;
        if (!test) a.settings.save();
        is = IS::Kick;
        isT = 0;
        kickStep = 0;
        kickT = 0;
    }
}

void introGrowStarted(App& a, Pet& p) {
    V2 c = p.headC;
    monitorOf(POINT{(LONG)c.x, (LONG)c.y});
    is = IS::Grow;
    isT = 0;
    novaGrow = 1;
    raise(a);
}

void introContinue(App& a, Pet& p) {
    if (is != IS::Grow || !p.growing) return;
    a.bubble.close();
    is = IS::Explode;
    isT = 0;
    adultOut = false;
}

void introGrowNow(Pet& p) {
    if (p.baby()) p.vit.babyStart = (double)time(nullptr) - 4 * 3600.0 - 1;
}

void introFrame(App& a, Pet& p, float dt) {
    for (size_t i = 0; i < sparks.size();) {
        auto& s = sparks[i];
        s.v.y += 900.f * a.dpi * dt;
        s.p += s.v * dt;
        s.t += dt;
        if (s.t >= s.life) sparks.erase(sparks.begin() + i);
        else i++;
    }
    if (is == IS::None) {
        if (parWnd && IsWindowVisible(parWnd)) ShowWindow(parWnd, SW_HIDE);
        dimA = whiteA = novaK = 0;
        drawOverlay(a, !sparks.empty());
        return;
    }
    isT += dt;
    float S = a.dpi;
    float sway = 0, chute = 0, lid = 0, alpha = 1, squash = 0, tilt = 0, hop = 0, jx = 0;
    bool showParcel = is <= IS::Hatch, animated = false;
    auto sm = [](float x) { return smooth01(x); };
    switch (is) {
    case IS::Parcel:
        if (!parLanded) {
            sway = sinf(isT * 1.3f);
            chute = 1;
            parPos.y += 170.f * S * dt;
            parPos.x += cosf(isT * 1.3f) * 10.f * S * dt;
            if (parPos.y >= work.bottom) {
                parPos.y = (float)work.bottom;
                parLanded = true;
                landT = 0;
                parClicked = false;
            }
        } else {
            landT += dt;
            chute = 1.f - sm(landT / 0.6f);
            float w = fmodf(landT, 2.6f);
            squash = landT < 0.3f ? 1.f - sm(landT / 0.3f) : (w < 0.06f ? 0.5f : 0.f);
            if (landT > 0.6f && w < 0.32f) hop = sinf(w / 0.32f * PI) * 7.f * S / 1.45f;
            if (parClicked && landT > 0.6f) {
                is = IS::Dark;
                isT = 0;
            }
        }
        parClicked = parClicked && parLanded;
        break;
    case IS::Dark:
        dimA = 0.86f * sm(isT / 0.45f);
        zoom = 1.f + (kZoom - 1.f) * sm(isT / 0.6f);
        if (isT > 0.5f) askName(a, p);
        break;
    case IS::AskName:
    case IS::AskGender:
        dimA = 0.86f;
        zoom = 1.f + (kZoom - 1.f) * sm((isT + 0.5f) / 0.6f);
        squash = fmodf(isT, 2.6f) < 0.22f ? sinf(fmodf(isT, 2.6f) / 0.22f * PI) * 0.5f : 0.f;
        if (!a.bubble.asking()) {
            if (is == IS::AskName) askName(a, p);
            else askGender(a);
        }
        break;
    case IS::Kick: {
        dimA = 0.86f;
        zoom = kZoom;
        kickT += dt;
        float t = kickT, k = S / 1.45f;
        const float dur[4] = {0.7f, 0.95f, 0.5f, 0.82f};
        if (kickStep == 0) {
            jx = sinf(t * 70.f) * 2.2f * k;
            squash = 0.12f + sinf(t * 40.f) * 0.08f;
        } else if (kickStep == 1) {
            if (t < 0.12f) squash = sm(t / 0.12f) * 0.8f;
            else if (t < 0.52f) {
                float q = (t - 0.12f) / 0.4f;
                hop = sinf(q * PI) * 40.f * k;
                tilt = sinf(q * PI) * 15.f;
                squash = -0.25f * sinf(q * PI);
            } else {
                float r = t - 0.52f;
                squash = std::max(0.f, 0.7f - r * 4.f);
                tilt = -6.f * expf(-r * 6.f) * sinf(r * 22.f);
            }
        } else if (kickStep == 2) {
            jx = sinf(t * 80.f) * 3.f * k;
            lid = fabsf(sinf(t * 55.f)) * 0.12f;
            squash = 0.18f;
        } else if (kickStep == 3) {
            if (t < 0.14f) squash = sm(t / 0.14f) * 0.9f;
            else if (t < 0.64f) {
                float q = (t - 0.14f) / 0.5f;
                hop = sinf(q * PI) * 62.f * k;
                tilt = -sinf(q * PI) * 22.f;
                lid = sinf(q * PI) * 0.18f;
            } else {
                float r = t - 0.64f;
                squash = std::max(0.f, 0.9f - r * 5.f);
                tilt = 8.f * expf(-r * 6.f) * sinf(r * 24.f);
            }
        }
        if (t > dur[kickStep]) {
            kickStep++;
            kickT = 0;
            if (kickStep >= 4) {
                is = IS::Hatch;
                isT = 0;
            }
        }
        break;
    }
    case IS::Hatch: {
        float q = std::min(1.f, isT / 0.28f), c1 = 1.70158f, c3 = c1 + 1;
        lid = std::min(1.08f, 1 + c3 * powf(q - 1, 3) + c1 * powf(q - 1, 2));
        squash = std::max(0.f, 0.6f - isT * 3.f);
        zoom = 1.f + (kZoom - 1.f) * (1.f - sm(isT / 0.35f));
        if (!babyOut) hatch(a, p);
        dimA = 0.86f * (1.f - sm((isT - 0.4f) / 1.3f));
        alpha = 1.f - sm((isT - 2.0f) / 0.7f);
        animated = true;
        if (isT > 2.8f) {
            is = IS::None;
            dimA = 0;
            zoom = 1;
            logf("intro: baby phase");
        }
        break;
    }
    case IS::Grow:
        novaAt = p.centerOfMass();
        novaK = p.glow;
        novaT += dt;
        animated = true;
        break;
    case IS::Explode: {
        novaT += dt;
        animated = true;
        float q = isT / 0.6f;
        if (q < 1.f) {
            novaK = std::max(p.glow, 0.6f);
            novaGrow = 1.f + 5.f * q * q;
            whiteA = sm(q) * 0.9f;
            p.introHidden = q > 0.7f;
        } else {
            novaK = 0;
            float f = isT - 0.6f;
            whiteA = f < 0.25f ? 1.f : std::max(0.f, 1.f - (f - 0.25f) / 1.0f);
            if (f > 0.25f && !adultOut) grown(a, p);
            if (f > 1.3f) {
                is = IS::None;
                whiteA = 0;
                novaGrow = 1;
            }
        }
        break;
    }
    default:
        break;
    }
    if (showParcel && is != IS::None) {
        if (!parWnd) parWnd = makeWnd(L"UziIntroParcel", parProc, false);
        float Sz = S * zoom;
        int W = (int)(130 * Sz), H = (int)(210 * Sz);
        if (parSurf.w != W || parSurf.h != H) parSurf.create(a.gfx, W, H);
        drawParcelArt(a, parSurf, Sz, sway, chute, lid, alpha, squash, tilt);
        bool first = !IsWindowVisible(parWnd);
        parSurf.present(parWnd, POINT{(LONG)lroundf(parPos.x + jx - W * 0.5f), (LONG)lroundf(parPos.y - H - hop * zoom)});
        if (first) {
            ShowWindow(parWnd, SW_SHOWNOACTIVATE);
            raise(a);
        }
    } else if (parWnd && IsWindowVisible(parWnd)) {
        ShowWindow(parWnd, SW_HIDE);
    }
    frameAcc += dt;
    bool redraw = animated || !sparks.empty();
    if (redraw && frameAcc < 1.f / 30.f) redraw = false;
    if (redraw) frameAcc = 0;
    drawOverlay(a, redraw);
    static float raiseT = 0;
    raiseT -= dt;
    if (raiseT <= 0) {
        raiseT = 1.f;
        raise(a);
    }
}
