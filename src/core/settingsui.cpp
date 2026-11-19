#include "settingsui.h"

SettingsUi g_settings;

namespace {
const int kClose = -100;
const float kW = 880.f;

struct Pal {
    D2D1_COLOR_F box, line, shadow, card, ink, dim, pill, pillHov, pillLine, off, track, acc, danger;
};

Pal palette(bool dark) {
    Pal p;
    p.acc = D2D1::ColorF(0.839f, 0.102f, 0.933f, 1);
    if (dark) {
        p.box = D2D1::ColorF(0.07f, 0.05f, 0.10f, 1);
        p.line = p.acc;
        p.shadow = D2D1::ColorF(0, 0, 0, 0.5f);
        p.card = D2D1::ColorF(0.11f, 0.08f, 0.15f, 1);
        p.ink = D2D1::ColorF(0.957f, 0.914f, 1.f, 1);
        p.dim = D2D1::ColorF(0.71f, 0.58f, 0.82f, 1);
        p.pill = D2D1::ColorF(0.12f, 0.09f, 0.17f, 1);
        p.pillHov = D2D1::ColorF(0.20f, 0.12f, 0.27f, 1);
        p.pillLine = D2D1::ColorF(0.33f, 0.24f, 0.42f, 1);
        p.off = D2D1::ColorF(0.26f, 0.20f, 0.33f, 1);
        p.track = D2D1::ColorF(0.19f, 0.14f, 0.26f, 1);
        p.danger = D2D1::ColorF(1.f, 0.42f, 0.52f, 1);
    } else {
        p.box = D2D1::ColorF(1, 1, 1, 1);
        p.line = D2D1::ColorF(0.165f, 0.106f, 0.239f, 1);
        p.shadow = D2D1::ColorF(0.165f, 0.106f, 0.239f, 0.28f);
        p.card = D2D1::ColorF(0.965f, 0.937f, 0.988f, 1);
        p.ink = D2D1::ColorF(0.165f, 0.106f, 0.239f, 1);
        p.dim = D2D1::ColorF(0.42f, 0.30f, 0.54f, 1);
        p.pill = D2D1::ColorF(0.965f, 0.937f, 0.988f, 1);
        p.pillHov = D2D1::ColorF(0.93f, 0.87f, 0.98f, 1);
        p.pillLine = D2D1::ColorF(0.867f, 0.788f, 0.941f, 1);
        p.off = D2D1::ColorF(0.80f, 0.75f, 0.86f, 1);
        p.track = D2D1::ColorF(0.91f, 0.87f, 0.96f, 1);
        p.danger = D2D1::ColorF(0.78f, 0.16f, 0.30f, 1);
    }
    return p;
}

float rowH(const SetRow& r) {
    switch (r.kind) {
    case SetRow::Section: return 30;
    case SetRow::Caption: return 22;
    case SetRow::Toggle: return 36;
    default: return 40;
    }
}

float easeOut(float x) { return 1.f - powf(1.f - clampf(x, 0.f, 1.f), 3.f); }
}

void SettingsUi::open(App& a, V2 nearPt, bool show) {
    app = &a;
    s = a.dpi;
    HINSTANCE hi = GetModuleHandleW(nullptr);
    if (!hwnd) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = proc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"UziSettingsWnd";
        RegisterClassExW(&wc);
        hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"UziSettingsWnd", L"Настройки Узи", WS_POPUP, 0, 0, 10, 10, nullptr,
                               nullptr, hi, nullptr);
        WNDCLASSEXW dc{sizeof(dc)};
        dc.lpfnWndProc = dimProc;
        dc.hInstance = hi;
        dc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        dc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        dc.lpszClassName = L"UziDimWnd";
        RegisterClassExW(&dc);
        dim = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"UziDimWnd", L"", WS_POPUP, 0, 0, 10, 10,
                              nullptr, nullptr, hi, nullptr);
    }
    mon = RECT{0, 0, 1920, 1080};
    RECT wk = mon;
    if (!a.world.mons.empty()) {
        int m = a.world.monitorAt(nearPt);
        if (m < 0) m = a.world.nearestMonitor(nearPt);
        mon = a.world.mons[m].rc;
        wk = a.world.mons[m].work;
    }
    float h = 0;
    for (int c = 0; c < 2; c++) {
        float hc = 0;
        for (auto& r : rows)
            if (r.col == c) hc += rowH(r);
        h = std::max(h, hc);
    }
    h = std::max(h + 60.f + 18.f, 470.f);
    int W = (int)ceilf((kW + 24) * s), H = (int)ceilf((h + 28) * s);
    at = POINT{(wk.left + wk.right - W) / 2, std::max((int)wk.top + 8, (int)(wk.top + wk.bottom - H) / 2)};
    SetWindowPos(hwnd, HWND_TOPMOST, at.x, at.y, W, H, SWP_NOACTIVATE);
    mouse = V2(-1e9f, -1e9f);
    pressed = 0;
    if (!show) {
        anim = 1;
        animDir = 0;
        redraw();
        return;
    }
    if (!visible()) anim = 0;
    animDir = 1;
    lastTick = GetTickCount64();
    SetWindowPos(dim, HWND_TOPMOST, mon.left + 1, mon.top + 1, mon.right - mon.left - 2, mon.bottom - mon.top - 2, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetLayeredWindowAttributes(dim, 0, 0, LWA_ALPHA);
    redraw();
    ShowWindow(hwnd, SW_SHOW);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(hwnd);
    SetTimer(hwnd, 1, 15, nullptr);
    SetTimer(hwnd, 2, 1000, nullptr);
}

void SettingsUi::close() {
    if (!visible() || animDir == -1) return;
    animDir = -1;
    lastTick = GetTickCount64();
    SetTimer(hwnd, 1, 15, nullptr);
}

void SettingsUi::tick() {
    ULONGLONG now = GetTickCount64();
    float dt = (now - lastTick) / 1000.f;
    lastTick = now;
    if (animDir > 0) anim = std::min(1.f, anim + dt / 0.2f);
    if (animDir < 0) anim = std::max(0.f, anim - dt / 0.15f);
    place();
    if ((animDir > 0 && anim >= 1.f) || (animDir < 0 && anim <= 0.f)) {
        KillTimer(hwnd, 1);
        if (animDir < 0) {
            KillTimer(hwnd, 2);
            ShowWindow(hwnd, SW_HIDE);
            ShowWindow(dim, SW_HIDE);
        }
        animDir = 0;
    }
}

void SettingsUi::place() {
    float e = easeOut(anim);
    POINT p{at.x, at.y - (LONG)((1.f - e) * 14.f * s)};
    surf.present(hwnd, p, nullptr, (BYTE)(255 * e));
    if (dim) SetLayeredWindowAttributes(dim, 0, (BYTE)(115 * e), LWA_ALPHA);
}

void SettingsUi::redraw() {
    if (!hwnd || !app) return;
    RECT wr;
    GetWindowRect(hwnd, &wr);
    int W = wr.right - wr.left, H = wr.bottom - wr.top;
    if (surf.w != W || surf.h != H) surf.create(app->gfx, W, H);
    auto* rt = surf.begin();
    if (!rt) return;
    auto* br = surf.brush.Get();
    auto* dw = app->gfx.dw.Get();
    if (!fTitle || fmtS != s) {
        fmtS = s;
        auto mk = [&](ComPtr<IDWriteTextFormat>& f, DWRITE_FONT_WEIGHT w, float sz) {
            f.Reset();
            dw->CreateTextFormat(app->gfx.uiFamily.c_str(), app->gfx.ui.Get(), w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, sz * s,
                                 L"ru-ru", &f);
            f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        };
        mk(fTitle, DWRITE_FONT_WEIGHT_BLACK, 21);
        mk(fName, DWRITE_FONT_WEIGHT_BLACK, 24);
        mk(fText, DWRITE_FONT_WEIGHT_BOLD, 14.5f);
        mk(fSmall, DWRITE_FONT_WEIGHT_EXTRA_BOLD, 11.5f);
        mk(fBtn, DWRITE_FONT_WEIGHT_EXTRA_BOLD, 13.5f);
        mk(fPct, DWRITE_FONT_WEIGHT_BOLD, 13);
    }
    bool dk = dark && dark();
    Pal P = palette(dk);
    auto col = [&](D2D1_COLOR_F c, float a = 1.f) {
        br->SetColor(c);
        br->SetOpacity(a);
        return br;
    };
    auto text = [&](const std::wstring& t, IDWriteTextFormat* f, D2D1_RECT_F r, D2D1_COLOR_F c, DWRITE_TEXT_ALIGNMENT al) {
        f->SetTextAlignment(al);
        rt->DrawTextW(t.c_str(), (UINT32)t.size(), f, r, col(c), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };
    const auto L = DWRITE_TEXT_ALIGNMENT_LEADING, C = DWRITE_TEXT_ALIGNMENT_CENTER, R = DWRITE_TEXT_ALIGNMENT_TRAILING;
    auto textW = [&](const std::wstring& t, IDWriteTextFormat* f) {
        ComPtr<IDWriteTextLayout> l;
        dw->CreateTextLayout(t.c_str(), (UINT32)t.size(), f, 1000.f * s, 100.f * s, &l);
        DWRITE_TEXT_METRICS m{};
        if (l) l->GetMetrics(&m);
        return m.width;
    };
    auto hov = [&](D2D1_RECT_F r) { return mouse.x >= r.left && mouse.x <= r.right && mouse.y >= r.top && mouse.y <= r.bottom; };
    auto pillBtn = [&](D2D1_RECT_F pr, const std::wstring& t, int id, bool sel, bool danger, bool off = false) {
        auto pp = D2D1::RoundedRect(pr, (pr.bottom - pr.top) / 2, (pr.bottom - pr.top) / 2);
        if (off) {
            rt->FillRoundedRectangle(pp, col(P.pill));
            rt->DrawRoundedRectangle(pp, col(P.pillLine), 1.8f * s);
            text(t, fBtn.Get(), pr, P.dim, C);
            return;
        }
        bool hv = hov(pr);
        if (sel) {
            rt->FillRoundedRectangle(pp, col(P.acc));
        } else {
            rt->FillRoundedRectangle(pp, col(hv ? P.pillHov : P.pill));
            rt->DrawRoundedRectangle(pp, col(hv ? P.acc : (danger ? P.danger : P.pillLine)), 1.8f * s);
        }
        text(t, fBtn.Get(), pr, sel ? D2D1::ColorF(1, 1, 1, 1) : (danger ? P.danger : P.ink), C);
        hits.push_back({pr, id});
    };
    hits.clear();

    float m = 12 * s;
    D2D1_RECT_F box = D2D1::RectF(m, m, W - m, H - m - 4 * s);
    auto rr = D2D1::RoundedRect(box, 20 * s, 20 * s);
    rt->SetTransform(D2D1::Matrix3x2F::Translation(0, 4 * s));
    rt->FillRoundedRectangle(rr, col(P.shadow, P.shadow.a));
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    rt->FillRoundedRectangle(rr, col(P.box));
    if (dk) rt->DrawRoundedRectangle(rr, col(P.acc, 0.22f), 6.f * s);
    rt->DrawRoundedRectangle(rr, col(P.line), (dk ? 2.f : 2.6f) * s);

    D2D1_RECT_F card = D2D1::RectF(box.left + 16 * s, box.top + 16 * s, box.left + 276 * s, box.bottom - 16 * s);
    rt->FillRoundedRectangle(D2D1::RoundedRect(card, 16 * s, 16 * s), col(P.card));
    float cx = card.left + 18 * s, cw = card.right - 18 * s;
    D2D1_RECT_F vz = D2D1::RectF(cx, card.top + 20 * s, cx + 58 * s, card.top + 52 * s);
    rt->FillRoundedRectangle(D2D1::RoundedRect(vz, 12 * s, 12 * s), col(D2D1::ColorF(0.04f, 0.03f, 0.06f, 1)));
    std::wstring st = status ? status() : L"";
    bool fatal = st.find(L"FATAL") != std::wstring::npos;
    D2D1_COLOR_F eye = fatal ? D2D1::ColorF(1.f, 0.2f, 0.25f, 1) : D2D1::ColorF(0.96f, 0.86f, 1.f, 1);
    for (int k = 0; k < 2; k++) {
        D2D1_POINT_2F ec{vz.left + (k ? 38.f : 20.f) * s, (vz.top + vz.bottom) / 2};
        rt->FillEllipse(D2D1::Ellipse(ec, 7 * s, 9 * s), col(fatal ? eye : P.acc, 0.35f));
        rt->FillEllipse(D2D1::Ellipse(ec, 4 * s, 6.5f * s), col(eye));
    }
    text(L"Узи", fName.Get(), D2D1::RectF(cx + 72 * s, card.top + 14 * s, cw, card.top + 42 * s), P.ink, L);
    text(st, fPct.Get(), D2D1::RectF(cx + 72 * s, card.top + 40 * s, cw, card.top + 60 * s), fatal ? P.danger : P.dim, L);
    float y = card.top + 84 * s;
    if (meters)
        for (auto& mt : meters()) {
            text(mt.name, fText.Get(), D2D1::RectF(cx, y, cw, y + 22 * s), P.ink, L);
            wchar_t pct[16];
            swprintf(pct, 16, L"%.0f%%", mt.v);
            text(mt.val.empty() ? std::wstring(pct) : mt.val, fPct.Get(), D2D1::RectF(cx, y, cw, y + 22 * s), P.dim, R);
            D2D1_RECT_F tr = D2D1::RectF(cx, y + 27 * s, cw, y + 36 * s);
            rt->FillRoundedRectangle(D2D1::RoundedRect(tr, 4.5f * s, 4.5f * s), col(P.track));
            float v = clampf(mt.v / 100.f, 0.f, 1.f);
            if (v > 0.005f) {
                D2D1_RECT_F fr = tr;
                fr.right = tr.left + std::max(9.f * s, (tr.right - tr.left) * v);
                rt->FillRoundedRectangle(D2D1::RoundedRect(fr, 4.5f * s, 4.5f * s), col(mt.color));
            }
            y += 52 * s;
        }
    if (mood) text(mood(), fPct.Get(), D2D1::RectF(cx, y, cw, y + 20 * s), P.dim, L);
    if (extra)
        for (auto& line : extra()) {
            y += 22 * s;
            text(line, fPct.Get(), D2D1::RectF(cx, y, cw, y + 20 * s), P.dim, L);
            if (versionCmd && line.rfind(L"Версия", 0) == 0) hits.push_back({D2D1::RectF(cx, y, cw, y + 20 * s), versionCmd});
        }
    if (callName) {
        text(callName(), fText.Get(), D2D1::RectF(cx, card.bottom - 82 * s, cw, card.bottom - 58 * s), P.ink, L);
        pillBtn(D2D1::RectF(cx, card.bottom - 50 * s, cw, card.bottom - 16 * s), L"Изменить", nameCmd, false, false);
    }

    float rx0 = card.right + 24 * s, rx1 = box.right - 22 * s;
    text(L"Настройки", fTitle.Get(), D2D1::RectF(rx0, box.top + 14 * s, rx1 - 50 * s, box.top + 52 * s), P.ink, L);
    D2D1_RECT_F cr = D2D1::RectF(rx1 - 32 * s, box.top + 18 * s, rx1, box.top + 50 * s);
    bool ch = hov(cr);
    D2D1_POINT_2F cc{(cr.left + cr.right) / 2, (cr.top + cr.bottom) / 2};
    rt->FillEllipse(D2D1::Ellipse(cc, 16 * s, 16 * s), col(ch ? P.acc : P.pill));
    float k = 5.5f * s;
    D2D1_COLOR_F xc = ch ? D2D1::ColorF(1, 1, 1, 1) : P.ink;
    rt->DrawLine({cc.x - k, cc.y - k}, {cc.x + k, cc.y + k}, col(xc), 2.2f * s);
    rt->DrawLine({cc.x - k, cc.y + k}, {cc.x + k, cc.y - k}, col(xc), 2.2f * s);
    hits.push_back({cr, kClose});

    float colW = (rx1 - rx0 - 24 * s) / 2;
    for (int c = 0; c < 2; c++) {
        float x0 = rx0 + c * (colW + 24 * s), x1 = x0 + colW;
        float yy = box.top + 60 * s;
        for (auto& r : rows) {
            if (r.col != c) continue;
            float h = rowH(r) * s;
            std::wstring lab = label ? label(r) : r.label;
            if (lab.empty()) lab = r.label;
            if (r.kind == SetRow::Section) {
                text(lab, fSmall.Get(), D2D1::RectF(x0, yy + 8 * s, x1, yy + h), P.dim, L);
            } else if (r.kind == SetRow::Caption) {
                text(lab, fPct.Get(), D2D1::RectF(x0, yy, x1, yy + h), P.dim, L);
            } else if (r.kind == SetRow::Toggle) {
                text(lab, fText.Get(), D2D1::RectF(x0, yy, x1 - 54 * s, yy + h), P.ink, L);
                bool v = on && on(r.ids[0]);
                D2D1_RECT_F tr = D2D1::RectF(x1 - 44 * s, yy + h / 2 - 12 * s, x1, yy + h / 2 + 12 * s);
                bool hv = hov(D2D1::RectF(x0, yy, x1, yy + h));
                rt->FillRoundedRectangle(D2D1::RoundedRect(tr, 12 * s, 12 * s), col(v ? P.acc : P.off, hv && !v ? 0.8f : 1.f));
                float kx = v ? tr.right - 12 * s : tr.left + 12 * s;
                rt->FillEllipse(D2D1::Ellipse({kx, (tr.top + tr.bottom) / 2}, 9 * s, 9 * s), col(D2D1::ColorF(1, 1, 1, 1)));
                hits.push_back({D2D1::RectF(x0, yy, x1, yy + h), r.ids[0]});
            } else {
                float bx = x0, bw = colW;
                if (r.kind == SetRow::Buttons && !lab.empty()) {
                    float need = 0;
                    for (auto& o : r.opts) need += textW(o, fBtn.Get()) + 28 * s;
                    need += 6 * s * (r.opts.size() - 1);
                    text(lab, fText.Get(), D2D1::RectF(x0, yy, x1 - need - 8 * s, yy + h), P.ink, L);
                    bx = x1 - need;
                    bw = need;
                }
                size_t n = r.opts.size();
                float gap = 6 * s, each = (bw - gap * (n - 1)) / n, px = bx;
                for (size_t i = 0; i < n; i++) {
                    float w = (r.kind == SetRow::Buttons && !lab.empty()) ? textW(r.opts[i], fBtn.Get()) + 28 * s : each;
                    D2D1_RECT_F pr = D2D1::RectF(px, yy + h / 2 - 15 * s, px + w, yy + h / 2 + 15 * s);
                    pillBtn(pr, r.opts[i], r.ids[i], r.kind == SetRow::Seg && on && on(r.ids[i]), r.danger, r.off);
                    px += w + gap;
                }
            }
            yy += h;
        }
    }
    surf.end(app->gfx);
    place();
}

LRESULT CALLBACK SettingsUi::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    SettingsUi& u = g_settings;
    auto at = [&](V2 p) {
        for (auto& x : u.hits)
            if (p.x >= x.r.left && p.x <= x.r.right && p.y >= x.r.top && p.y <= x.r.bottom) return x.id;
        return 0;
    };
    switch (msg) {
    case WM_NCHITTEST: {
        POINT p{(short)LOWORD(lp), (short)HIWORD(lp)};
        ScreenToClient(h, &p);
        if (at(V2((float)p.x, (float)p.y)) == 0) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_MOVE:
        if (u.animDir == 0 && u.anim >= 1.f) {
            RECT r;
            GetWindowRect(h, &r);
            u.at = POINT{r.left, r.top};
        }
        return 0;
    case WM_TIMER:
        if (wp == 2) {
            if (u.visible() && u.animDir == 0) u.redraw();
            return 0;
        }
        u.tick();
        return 0;
    case WM_MOUSEMOVE: {
        V2 p((float)(short)LOWORD(lp), (float)(short)HIWORD(lp));
        int was = at(u.mouse), now = at(p);
        u.mouse = p;
        TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
        if (was != now) u.redraw();
        return 0;
    }
    case WM_MOUSELEAVE:
        u.mouse = V2(-1e9f, -1e9f);
        u.redraw();
        return 0;
    case WM_LBUTTONDOWN:
        u.pressed = at(V2((float)(short)LOWORD(lp), (float)(short)HIWORD(lp)));
        return 0;
    case WM_LBUTTONUP: {
        int id = at(V2((float)(short)LOWORD(lp), (float)(short)HIWORD(lp)));
        if (id && id == u.pressed) {
            u.pressed = 0;
            if (id == kClose) {
                u.close();
                return 0;
            }
            if (u.cmd) u.cmd(id);
            if (u.visible() && u.animDir >= 0) u.redraw();
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) u.close();
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT CALLBACK SettingsUi::dimProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        g_settings.close();
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
