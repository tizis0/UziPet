#include "items.h"
#include <shlobj.h>
#include <wincodec.h>

namespace {

struct Win {
    HWND h = nullptr;
    Surface surf;
};
Win g_par, g_bat;
Win g_fly[3], g_plus;
bool flying = false, flyGiven = false;
float flyT = 0, plusT = -1;
V2 flyFrom, flyTo;
int plusN = 0;
float flyDrawnS = 0;

enum class PS { None, Falling, Landing, Waiting, Opening, Leaving };
PS ps = PS::None;
V2 parPos;
float parT = 0, floorY = 0;
bool parClicked = false;
float batDrawnS = 0;

LRESULT CALLBACK parProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_SETCURSOR:
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return TRUE;
    case WM_LBUTTONUP:
        parClicked = true;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

HWND makeWnd(const wchar_t* cls, WNDPROC proc, bool clickThrough) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);
    DWORD ex = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | (clickThrough ? WS_EX_TRANSPARENT : 0);
    return CreateWindowExW(ex, cls, L"", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, wc.hInstance, nullptr);
}

void drawBattery(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br, ID2D1Factory* f, D2D1_RECT_F r, float a, bool empty = false) {
    float w = r.right - r.left, h = r.bottom - r.top;
    auto col = [&](D2D1_COLOR_F c, float al) {
        br->SetColor(c);
        br->SetOpacity(al * a);
        return br;
    };
    D2D1_RECT_F nub = D2D1::RectF(r.left + w * 0.36f, r.top, r.right - w * 0.36f, r.top + h * 0.1f);
    D2D1_RECT_F body = D2D1::RectF(r.left, r.top + h * 0.08f, r.right, r.bottom);
    float rr = w * 0.18f;
    rt->FillRoundedRectangle(D2D1::RoundedRect(nub, w * 0.05f, w * 0.05f), col(D2D1::ColorF(0.75f, 0.75f, 0.8f, 1), 1));
    rt->FillRoundedRectangle(D2D1::RoundedRect(body, rr, rr), col(empty ? D2D1::ColorF(0.62f, 0.62f, 0.68f, 1) : D2D1::ColorF(0.24f, 0.78f, 0.49f, 1), 1));
    D2D1_RECT_F cap = body;
    cap.bottom = cap.top + h * 0.22f;
    rt->FillRoundedRectangle(D2D1::RoundedRect(cap, rr, rr), col(D2D1::ColorF(0.12f, 0.12f, 0.16f, 1), 1));
    rt->FillRectangle(D2D1::RectF(cap.left, cap.bottom - rr, cap.right, cap.bottom), col(D2D1::ColorF(0.12f, 0.12f, 0.16f, 1), 1));
    rt->DrawRoundedRectangle(D2D1::RoundedRect(body, rr, rr), col(D2D1::ColorF(0.09f, 0.08f, 0.13f, 1), 1), w * 0.06f);
    float cx = (body.left + body.right) / 2, cy = (cap.bottom + body.bottom) / 2;
    ComPtr<ID2D1PathGeometry> g;
    f->CreatePathGeometry(&g);
    ComPtr<ID2D1GeometrySink> k;
    g->Open(&k);
    k->BeginFigure({cx + w * 0.08f, cy - h * 0.26f}, D2D1_FIGURE_BEGIN_FILLED);
    k->AddLine({cx - w * 0.18f, cy + h * 0.03f});
    k->AddLine({cx - w * 0.01f, cy + h * 0.03f});
    k->AddLine({cx - w * 0.08f, cy + h * 0.26f});
    k->AddLine({cx + w * 0.18f, cy - h * 0.05f});
    k->AddLine({cx + w * 0.01f, cy - h * 0.05f});
    k->EndFigure(D2D1_FIGURE_END_CLOSED);
    k->Close();
    rt->FillGeometry(g.Get(), col(D2D1::ColorF(1, 1, 1, 1), 0.95f));
}

void drawParcel(App& a, Surface& s, float S, float sway, float chute, float lid, float fly, float alpha, float squash, float tilt = 0) {
    auto* rt = s.begin();
    if (!rt) return;
    auto* br = s.brush.Get();
    ID2D1Factory* f = a.gfx.d2d.Get();
    auto col = [&](D2D1_COLOR_F c, float al = 1.f) {
        br->SetColor(c);
        br->SetOpacity(al * alpha);
        return br;
    };
    float W = (float)s.w, H = (float)s.h;
    float bw = 72 * S, bh = 56 * S * (1.f - squash * 0.12f);
    float bx = (W - bw) / 2, by = H - bh - 2 * S;
    const D2D1::Matrix3x2F base = D2D1::Matrix3x2F::Rotation(tilt, {W / 2, H - 2 * S});
    rt->SetTransform(base);
    D2D1_COLOR_F ink = D2D1::ColorF(0.24f, 0.15f, 0.08f, 1), card = D2D1::ColorF(0.80f, 0.58f, 0.36f, 1), dark = D2D1::ColorF(0.66f, 0.45f, 0.26f, 1);
    if (chute > 0.01f) {
        D2D1_POINT_2F top{W / 2 + sway * 10 * S, 8 * S + (1 - chute) * 60 * S};
        float dw = 108 * S * chute, dh = 46 * S * chute;
        rt->SetTransform(D2D1::Matrix3x2F::Rotation(sway * 7.f, {W / 2, by}) * base);
        for (int i = -1; i <= 1; i += 2) {
            rt->DrawLine({top.x + i * dw * 0.48f, top.y + dh}, {W / 2 + i * bw * 0.42f, by + 2 * S}, col(ink, 0.55f * chute), 1.4f * S);
        }
        rt->DrawLine({top.x, top.y + dh}, {W / 2, by + 2 * S}, col(ink, 0.45f * chute), 1.2f * S);
        ComPtr<ID2D1PathGeometry> g;
        f->CreatePathGeometry(&g);
        ComPtr<ID2D1GeometrySink> k;
        g->Open(&k);
        k->BeginFigure({top.x - dw / 2, top.y + dh}, D2D1_FIGURE_BEGIN_FILLED);
        k->AddBezier(D2D1::BezierSegment({top.x - dw / 2, top.y - dh * 0.25f}, {top.x + dw / 2, top.y - dh * 0.25f}, {top.x + dw / 2, top.y + dh}));
        for (int i = 3; i >= 0; i--) {
            float x0 = top.x - dw / 2 + dw * (i + 1) / 4.f, x1 = top.x - dw / 2 + dw * i / 4.f;
            k->AddQuadraticBezier(D2D1::QuadraticBezierSegment({(x0 + x1) / 2, top.y + dh * 0.78f}, {x1, top.y + dh}));
        }
        k->EndFigure(D2D1_FIGURE_END_CLOSED);
        k->Close();
        rt->FillGeometry(g.Get(), col(D2D1::ColorF(0.84f, 0.10f, 0.93f, 1), chute));
        ComPtr<ID2D1PathGeometry> st;
        f->CreatePathGeometry(&st);
        ComPtr<ID2D1GeometrySink> k2;
        st->Open(&k2);
        k2->BeginFigure({top.x - dw * 0.12f, top.y + dh * 0.9f}, D2D1_FIGURE_BEGIN_FILLED);
        k2->AddBezier(D2D1::BezierSegment({top.x - dw * 0.12f, top.y - dh * 0.05f}, {top.x + dw * 0.12f, top.y - dh * 0.05f}, {top.x + dw * 0.12f, top.y + dh * 0.9f}));
        k2->EndFigure(D2D1_FIGURE_END_CLOSED);
        k2->Close();
        rt->FillGeometry(st.Get(), col(D2D1::ColorF(1, 1, 1, 1), 0.85f * chute));
        rt->DrawGeometry(g.Get(), col(ink, 0.7f * chute), 1.6f * S);
        rt->SetTransform(base);
    }
    if (fly > 0.01f)
        for (int i = 0; i < 3; i++) {
            float ph = clampf(fly * 1.4f - i * 0.18f, 0.f, 1.f);
            if (ph <= 0) continue;
            float x = W / 2 + (i - 1) * 22 * S, y = by + 10 * S - ph * 95 * S;
            drawBattery(rt, br, f, D2D1::RectF(x - 9 * S, y - 26 * S, x + 9 * S, y), alpha * (1.f - smooth01((ph - 0.7f) / 0.3f)));
        }
    D2D1_RECT_F box = D2D1::RectF(bx, by, bx + bw, by + bh);
    rt->FillRoundedRectangle(D2D1::RoundedRect(box, 5 * S, 5 * S), col(card));
    rt->FillRectangle(D2D1::RectF(bx, by, bx + bw, by + 12 * S), col(dark));
    for (int i = -1; i <= 1; i += 2) {
        float ang = lid * 120.f * i;
        D2D1_POINT_2F hinge{i < 0 ? bx : bx + bw, by};
        rt->SetTransform(D2D1::Matrix3x2F::Rotation(-ang, hinge) * base);
        D2D1_RECT_F flap = i < 0 ? D2D1::RectF(bx, by - 4 * S, bx + bw / 2, by + 6 * S) : D2D1::RectF(bx + bw / 2, by - 4 * S, bx + bw, by + 6 * S);
        rt->FillRectangle(flap, col(D2D1::ColorF(0.86f, 0.64f, 0.40f, 1)));
        rt->DrawRectangle(flap, col(ink, 0.8f), 1.4f * S);
        rt->SetTransform(base);
    }
    if (lid < 0.05f) rt->FillRectangle(D2D1::RectF(bx + bw / 2 - 6 * S, by - 4 * S, bx + bw / 2 + 6 * S, by + bh), col(D2D1::ColorF(0.93f, 0.84f, 0.58f, 1)));
    D2D1_RECT_F stk = D2D1::RectF(bx + 8 * S, by + 22 * S, bx + 30 * S, by + 48 * S);
    rt->FillRoundedRectangle(D2D1::RoundedRect(stk, 3 * S, 3 * S), col(D2D1::ColorF(1, 1, 1, 1), 0.9f));
    drawBattery(rt, br, f, D2D1::RectF(stk.left + 6 * S, stk.top + 4 * S, stk.right - 6 * S, stk.bottom - 4 * S), alpha);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(box, 5 * S, 5 * S), col(ink), 2.2f * S);
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    s.end(a.gfx);
}

void placeWin(Win& w, V2 topLeft, BYTE al = 255) {
    w.surf.present(w.h, POINT{(LONG)lroundf(topLeft.x), (LONG)lroundf(topLeft.y)}, nullptr, al);
    if (!IsWindowVisible(w.h)) ShowWindow(w.h, SW_SHOWNOACTIVATE);
}

}

void drawParcelArt(App& a, Surface& s, float S, float sway, float chute, float lid, float alpha, float squash, float tilt) {
    drawParcel(a, s, S, sway, chute, lid, 0.f, alpha, squash, tilt);
}

bool itemsBusy() { return ps == PS::Falling || ps == PS::Landing || ps == PS::Opening || ps == PS::Leaving || flying; }

void itemsFrame(App& a, Pet& p, float dt) {
    float S = a.dpi;
    if (!g_bat.h) g_bat.h = makeWnd(L"UziBatteryWnd", DefWindowProcW, true);
    if (p.feeding) {
        int bw = (int)(30 * S), bh = (int)(48 * S);
        if (g_bat.surf.w != bw || batDrawnS != S) {
            g_bat.surf.create(a.gfx, bw, bh);
            auto* rt = g_bat.surf.begin();
            if (rt) {
                drawBattery(rt, g_bat.surf.brush.Get(), a.gfx.d2d.Get(), D2D1::RectF(3 * S, 3 * S, bw - 3 * S, bh - 3 * S), 1.f);
                g_bat.surf.end(a.gfx);
            }
            batDrawnS = S;
        }
        POINT c;
        GetCursorPos(&c);
        placeWin(g_bat, V2((float)c.x - bw * 0.5f, (float)c.y - bh * 0.85f));
    } else if (IsWindowVisible(g_bat.h)) {
        ShowWindow(g_bat.h, SW_HIDE);
    }

    if (flying) {
        flyT += dt;
        int bw = (int)(30 * S), bh = (int)(48 * S);
        for (auto& f : g_fly) {
            if (!f.h) f.h = makeWnd(L"UziFlyBattery", DefWindowProcW, true);
            if (f.surf.w != bw || flyDrawnS != S) {
                f.surf.create(a.gfx, bw, bh);
                if (auto* rt = f.surf.begin()) {
                    drawBattery(rt, f.surf.brush.Get(), a.gfx.d2d.Get(), D2D1::RectF(3 * S, 3 * S, bw - 3 * S, bh - 3 * S), 1.f);
                    f.surf.end(a.gfx);
                }
            }
        }
        flyDrawnS = S;
        V2 ctrl = (flyFrom + flyTo) * 0.5f - V2(0, 220.f * S);
        for (int k = 0; k < 3; k++) {
            float t = (flyT - 0.25f - k * 0.13f) / 0.95f;
            if (t <= 0 || t >= 1) {
                if (IsWindowVisible(g_fly[k].h)) ShowWindow(g_fly[k].h, SW_HIDE);
                continue;
            }
            float e = smooth01(t), u = 1 - e;
            V2 pt = flyFrom * (u * u) + ctrl * (2 * u * e) + flyTo * (e * e);
            float al = 1.f - smooth01((t - 0.82f) / 0.18f);
            placeWin(g_fly[k], V2(pt.x - bw * 0.5f, pt.y - bh * 0.5f), (BYTE)(255 * al));
        }
        if (!flyGiven && flyT > 0.25f + 2 * 0.13f + 0.95f) {
            flyGiven = true;
            int before = p.vit.stock;
            p.parcelOpened();
            plusN = p.vit.stock - before;
            plusT = 0;
            if (!g_plus.h) g_plus.h = makeWnd(L"UziPlusWnd", DefWindowProcW, true);
            int pw = (int)(56 * S), ph = (int)(30 * S);
            g_plus.surf.create(a.gfx, pw, ph);
            if (auto* rt = g_plus.surf.begin()) {
                auto* br = g_plus.surf.brush.Get();
                D2D1_ROUNDED_RECT pill = D2D1::RoundedRect(D2D1::RectF(1, 1, pw - 1.f, ph - 1.f), ph * 0.5f, ph * 0.5f);
                br->SetColor(D2D1::ColorF(0.84f, 0.10f, 0.93f, 1));
                rt->FillRoundedRectangle(pill, br);
                br->SetColor(D2D1::ColorF(0.10f, 0.07f, 0.14f, 1));
                rt->DrawRoundedRectangle(pill, br, 1.6f * S);
                ComPtr<IDWriteTextFormat> f;
                a.gfx.dw->CreateTextFormat(a.gfx.uiFamily.c_str(), a.gfx.ui.Get(), DWRITE_FONT_WEIGHT_BLACK, DWRITE_FONT_STYLE_NORMAL,
                                           DWRITE_FONT_STRETCH_NORMAL, 17.f * S, L"ru-ru", &f);
                if (f) {
                    f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    std::wstring txt = L"+" + std::to_wstring(plusN);
                    br->SetColor(D2D1::ColorF(1, 1, 1, 1));
                    rt->DrawTextW(txt.c_str(), (UINT32)txt.size(), f.Get(), D2D1::RectF(0, 0, (float)pw, (float)ph), br);
                }
                g_plus.surf.end(a.gfx);
            }
        }
        if (plusT >= 0) {
            plusT += dt;
            float al = plusT < 0.15f ? plusT / 0.15f : 1.f - smooth01((plusT - 0.8f) / 0.5f);
            if (plusN > 0 && al > 0.01f)
                placeWin(g_plus, V2(flyTo.x - g_plus.surf.w * 0.5f, flyTo.y - 40.f * S - plusT * 18.f * S), (BYTE)(255 * al));
            else if (IsWindowVisible(g_plus.h))
                ShowWindow(g_plus.h, SW_HIDE);
            if (plusT > 1.4f) {
                flying = false;
                plusT = -1;
                ShowWindow(g_plus.h, SW_HIDE);
            }
        }
    }

    if (!g_par.h) g_par.h = makeWnd(L"UziParcelWnd", parProc, false);
    int W = (int)(130 * S), H = (int)(210 * S);
    if (g_par.surf.w != W || g_par.surf.h != H) g_par.surf.create(a.gfx, W, H);
    if (ps == PS::None && p.vit.parcelOut && !p.vit.dead) {
        auto& Wd = a.world;
        if (Wd.mons.empty()) return;
        int m = Wd.monitorAt(p.pos - V2(0, 4));
        if (m < 0) m = Wd.nearestMonitor(p.pos);
        const RECT& wk = Wd.mons[m].work;
        float x = p.pos.x < (wk.left + wk.right) * 0.5f ? randf(wk.left + (wk.right - wk.left) * 0.55f, wk.right - 120.f * S)
                                                          : randf(wk.left + 120.f * S, wk.left + (wk.right - wk.left) * 0.45f);
        floorY = (float)wk.bottom;
        bool fresh = p.parcelFresh;
        p.parcelFresh = false;
        parPos = V2(x, fresh ? (float)Wd.mons[m].rc.top + 30.f * S : floorY);
        ps = fresh ? PS::Falling : PS::Waiting;
        parT = 0;
        parClicked = false;
        logf("parcel: %s at %.0f", fresh ? "falling" : "waiting", x);
    }
    if (ps == PS::None) {
        if (IsWindowVisible(g_par.h)) ShowWindow(g_par.h, SW_HIDE);
        return;
    }
    parT += dt;
    float sway = 0, chute = 0, lid = 0, fly = 0, alpha = 1, squash = 0;
    switch (ps) {
    case PS::Falling:
        sway = sinf(parT * 1.3f);
        chute = 1;
        parPos.y += 85.f * S * dt;
        parPos.x += cosf(parT * 1.3f) * 14.f * S * dt;
        if (parPos.y >= floorY) {
            parPos.y = floorY;
            ps = PS::Landing;
            parT = 0;
            p.parcelLanded();
        }
        break;
    case PS::Landing:
        chute = 1.f - smooth01(parT / 0.6f);
        squash = (1.f - smooth01(parT / 0.3f));
        if (parT > 0.6f) { ps = PS::Waiting; parT = 0; }
        break;
    case PS::Waiting:
        squash = fmodf(parT, 3.f) < 0.25f ? sinf(fmodf(parT, 3.f) / 0.25f * PI) * 0.6f : 0.f;
        if (parClicked) {
            ps = PS::Opening;
            parT = 0;
            a.icons.refresh();
            int li = a.icons.find(L"Батарейки");
            if (li >= 0) {
                const RECT& r = a.icons.items[li].img;
                flyTo = V2((r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f);
                flyFrom = parPos - V2(0, 40.f * S);
                flying = true;
                flyGiven = false;
                flyT = 0;
                plusT = -1;
            } else {
                p.parcelOpened();
            }
        }
        if (!p.vit.parcelOut) { ps = PS::Leaving; parT = 0; }
        break;
    case PS::Opening:
        lid = smooth01(parT / 0.3f);
        fly = flying ? 0.f : clampf((parT - 0.2f) / 0.9f, 0.f, 1.f);
        alpha = 1.f - smooth01((parT - 1.1f) / 0.4f);
        if (parT > 1.5f) ps = PS::None;
        break;
    case PS::Leaving:
        alpha = 1.f - smooth01(parT / 0.6f);
        if (parT > 0.6f) ps = PS::None;
        break;
    default:
        break;
    }
    if (ps == PS::None) {
        ShowWindow(g_par.h, SW_HIDE);
        return;
    }
    drawParcel(a, g_par.surf, S, sway, chute, lid, fly, alpha, squash);
    placeWin(g_par, V2(parPos.x - W * 0.5f, parPos.y - H));
}

std::wstring batteryLinkPath() {
    wchar_t* d = nullptr;
    std::wstring p;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &d))) p = std::wstring(d) + L"\\Батарейки.lnk";
    CoTaskMemFree(d);
    return p;
}

namespace {
int linkShown = -1;
bool linkSeen = false;
bool linkMoveBack = false;
POINT linkCell{};
bool linkCellKnown = false;
float linkT = 0, moveT = 0;

bool makeIcon(App& a, int n, const std::wstring& ico) {
    const int sizes[] = {16, 24, 32, 48, 64, 256};
    std::vector<std::string> imgs;
    for (int sz : sizes) {
        Surface s;
        if (!s.create(a.gfx, sz, sz)) return false;
        auto* rt = s.begin();
        if (!rt) return false;
        rt->SetTransform(D2D1::Matrix3x2F::Scale(sz / 256.f, sz / 256.f));
        drawBattery(rt, s.brush.Get(), a.gfx.d2d.Get(), D2D1::RectF(64, 16, 176, 236), 1.f, n == 0);
        auto* br = s.brush.Get();
        D2D1_POINT_2F c{194, 196};
        br->SetOpacity(1);
        br->SetColor(D2D1::ColorF(0.10f, 0.07f, 0.14f, 1));
        rt->FillEllipse(D2D1::Ellipse(c, 56, 56), br);
        br->SetColor(n == 0 ? D2D1::ColorF(0.45f, 0.42f, 0.5f, 1) : D2D1::ColorF(0.84f, 0.10f, 0.93f, 1));
        rt->FillEllipse(D2D1::Ellipse(c, 48, 48), br);
        ComPtr<IDWriteTextFormat> f;
        a.gfx.dw->CreateTextFormat(a.gfx.uiFamily.c_str(), a.gfx.ui.Get(), DWRITE_FONT_WEIGHT_BLACK, DWRITE_FONT_STYLE_NORMAL,
                                   DWRITE_FONT_STRETCH_NORMAL, 70, L"ru-ru", &f);
        if (f) {
            f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            std::wstring t = std::to_wstring(n);
            br->SetColor(D2D1::ColorF(1, 1, 1, 1));
            rt->DrawTextW(t.c_str(), (UINT32)t.size(), f.Get(), D2D1::RectF(c.x - 50, c.y - 54, c.x + 50, c.y + 46), br);
        }
        s.end(a.gfx);
        if (sz == 256) {
            std::wstring png = ico + L".png";
            std::string data;
            if (!s.savePng(a.gfx, png) || !readFile(png, data)) return false;
            DeleteFileW(png.c_str());
            imgs.push_back(data);
            continue;
        }
        std::string d;
        BITMAPINFOHEADER bh{};
        bh.biSize = sizeof(bh);
        bh.biWidth = sz;
        bh.biHeight = sz * 2;
        bh.biPlanes = 1;
        bh.biBitCount = 32;
        bh.biCompression = BI_RGB;
        d.append((const char*)&bh, sizeof(bh));
        for (int y = sz - 1; y >= 0; y--)
            for (int x = 0; x < sz; x++) {
                uint32_t px = s.bits[(size_t)y * s.w + x];
                uint8_t al = px >> 24, b = px & 255, g = (px >> 8) & 255, r = (px >> 16) & 255;
                if (al) {
                    b = (uint8_t)std::min(255, b * 255 / al);
                    g = (uint8_t)std::min(255, g * 255 / al);
                    r = (uint8_t)std::min(255, r * 255 / al);
                }
                uint8_t q[4] = {b, g, r, al};
                d.append((const char*)q, 4);
            }
        d.append((size_t)((sz + 31) / 32) * 4 * sz, '\0');
        imgs.push_back(d);
    }
    std::string out;
    auto u16 = [&](uint16_t v) { out.append((const char*)&v, 2); };
    auto u32 = [&](uint32_t v) { out.append((const char*)&v, 4); };
    u16(0);
    u16(1);
    u16((uint16_t)imgs.size());
    uint32_t off = 6 + 16 * (uint32_t)imgs.size();
    for (size_t k = 0; k < imgs.size(); k++) {
        int sz = sizes[k];
        out.push_back((char)(sz >= 256 ? 0 : sz));
        out.push_back((char)(sz >= 256 ? 0 : sz));
        out.push_back(0);
        out.push_back(0);
        u16(1);
        u16(32);
        u32((uint32_t)imgs[k].size());
        u32(off);
        off += (uint32_t)imgs[k].size();
    }
    for (auto& im : imgs) out += im;
    return writeFile(ico, out);
}

void readLink(const std::wstring& lnk, std::wstring& ico, std::wstring& target) {
    ico.clear();
    target.clear();
    ComPtr<IShellLinkW> sl;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl)))) return;
    ComPtr<IPersistFile> pf;
    if (FAILED(sl.As(&pf)) || FAILED(pf->Load(lnk.c_str(), STGM_READ))) return;
    wchar_t buf[MAX_PATH * 2];
    int idx = 0;
    if (SUCCEEDED(sl->GetIconLocation(buf, MAX_PATH * 2, &idx))) ico = buf;
    if (SUCCEEDED(sl->GetPath(buf, MAX_PATH * 2, nullptr, SLGP_RAWPATH))) target = buf;
}

void refreshDesktopView() {
    HWND prog = FindWindowW(L"Progman", nullptr);
    HWND dv = prog ? FindWindowExW(prog, nullptr, L"SHELLDLL_DefView", nullptr) : nullptr;
    for (HWND w = nullptr; !dv;) {
        w = FindWindowExW(nullptr, w, L"WorkerW", nullptr);
        if (!w) break;
        dv = FindWindowExW(w, nullptr, L"SHELLDLL_DefView", nullptr);
    }
    if (dv) PostMessageW(dv, WM_COMMAND, 0x7103, 0);
}

bool makeLink(const std::wstring& lnk, const std::wstring& ico) {
    ComPtr<IShellLinkW> sl;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl)))) return false;
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    sl->SetPath(exe);
    sl->SetArguments(L"--batteries");
    sl->SetDescription(L"Шкафчик Узи. Перетащи на неё, чтобы покормить.");
    sl->SetIconLocation(ico.c_str(), 0);
    ComPtr<IPersistFile> pf;
    if (FAILED(sl.As(&pf))) return false;
    return SUCCEEDED(pf->Save(lnk.c_str(), TRUE));
}
}

void batteryLinkUpdate(App& a, Pet& p, float dt) {
    if (a.world.fake || p.vit.dead || p.vit.inBin || p.vit.intro == 1) return;
    std::wstring lnk = batteryLinkPath();
    if (lnk.empty()) return;
    static bool refreshWanted = false;
    if (refreshWanted && !p.deskOn() && p.carriedName.empty() && !a.icons.pendingAny()) {
        refreshWanted = false;
        logf("battery link: desktop refresh (F5) so explorer redraws the picture");
        refreshDesktopView();
    }
    if (linkMoveBack) {
        moveT -= dt;
        if (moveT <= 0) {
            linkMoveBack = false;
            a.icons.refresh();
            int i = a.icons.find(L"Батарейки");
            if (i >= 0 && linkCellKnown) a.icons.put(a.icons.items[i], linkCell, "battery link back");
        }
    }
    linkT -= dt;
    if (linkT > 0 && p.vit.stock == linkShown) return;
    linkT = 3.f;
    bool exists = GetFileAttributesW(lnk.c_str()) != INVALID_FILE_ATTRIBUTES;
    if (exists) {
        int i = a.icons.find(L"Батарейки");
        if (i >= 0) {
            linkCell = a.icons.items[i].pos;
            linkCellKnown = true;
        }
    }
    if (exists && p.vit.stock == linkShown) {
        linkSeen = true;
        return;
    }
    if (!exists && linkSeen) {
        logf("battery link deleted: restoring");
        p.sayCat("linkGone", 1.f, L"", true);
        linkMoveBack = linkCellKnown;
        moveT = 1.5f;
    }
    int n = p.vit.stock;
    std::wstring root = appDataDir(), dir = exeDir() + L"\\icons";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring ico = dir + L"\\battery-a1-" + std::to_wstring(n) + L".ico";
    if (GetFileAttributesW(ico.c_str()) == INVALID_FILE_ATTRIBUTES && !makeIcon(a, n, ico)) return;
    static bool cleaned = false;
    if (!cleaned) {
        cleaned = true;
        for (std::wstring d : {root, root + L"\\icons"}) {
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((d + L"\\bat*.ico").c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do DeleteFileW((d + L"\\" + fd.cFileName).c_str());
                while (FindNextFileW(h, &fd));
                FindClose(h);
            }
        }
        RemoveDirectoryW((root + L"\\icons").c_str());
    }
    wchar_t me[MAX_PATH * 2];
    GetModuleFileNameW(nullptr, me, MAX_PATH * 2);
    if (exists) {
        std::wstring curIco, curExe;
        readLink(lnk, curIco, curExe);
        if (!_wcsicmp(curIco.c_str(), ico.c_str()) && !_wcsicmp(curExe.c_str(), me)) {
            linkShown = n;
            linkSeen = true;
            return;
        }
    }
    if (makeLink(lnk, ico)) {
        SHChangeNotify(exists ? SHCNE_UPDATEITEM : SHCNE_CREATE, SHCNF_PATHW | SHCNF_FLUSHNOWAIT, lnk.c_str(), nullptr);
        if (exists) refreshWanted = true;
        linkShown = n;
        linkSeen = true;
        logf("battery link: %d", n);
    }
}

bool makeBatteryIcon(App& a, int n, const std::wstring& ico) { return makeIcon(a, n, ico); }
