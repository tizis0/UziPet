#include "modsui.h"
#include "uistyle.h"
#include <cstring>

namespace {
constexpr wchar_t windowClass[] = L"UziModsWindow";
constexpr float width = 584, height = 372;
constexpr size_t visibleRows = 4;
enum { IdClose = 1, IdFolder, IdReload, IdList, IdToggle, IdDetails, IdControl = 20, IdRow = 100 };

bool contains(D2D1_RECT_F r, V2 p) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}
}

void ModsUi::open(App& app, HWND anchor, Mods& source) {
    this->app = &app;
    mods = &source;
    mods->refresh();
    if (hwnd) {
        update();
        SetForegroundWindow(hwnd);
        return;
    }
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = windowClass;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
    if (!CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, windowClass, L"Моды UziPet", WS_POPUP,
                         0, 0, 1, 1, nullptr, nullptr, instance, this)) return;

    RECT rect{};
    GetWindowRect(anchor, &rect);
    resize(rect, GetDpiForWindow(anchor));
    revision = mods->revision() - 1;
    firstRow = 0;
    focus = IdList;
    update();
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
}

void ModsUi::resize(RECT anchor, UINT dpi) {
    MONITORINFO monitor{sizeof(monitor)};
    RECT work{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    if (GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &monitor)) work = monitor.rcWork;
    scale = std::min({dpi ? dpi / 96.f : 1.f, (work.right - work.left) / width, (work.bottom - work.top) / height});
    LONG w = (LONG)std::floor(width * scale), h = (LONG)std::floor(height * scale);
    LONG x = std::clamp((anchor.left + anchor.right - w) / 2, work.left, std::max(work.left, work.right - w));
    LONG y = std::clamp((anchor.top + anchor.bottom - h) / 2, work.top, std::max(work.top, work.bottom - h));
    SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

void ModsUi::close() {
    if (hwnd) DestroyWindow(hwnd);
}

const Mods::Mod* ModsUi::selection() const {
    return selected < mods->list().size() ? &mods->list()[selected] : nullptr;
}

void ModsUi::select(size_t index) {
    if (index >= mods->list().size()) return;
    const auto& path = mods->list()[index].folder;
    if (path != selectedFolder) detailScroll = 0;
    selected = index;
    selectedFolder = path;
    if (selected < firstRow) firstRow = selected;
    else if (selected - firstRow >= visibleRows) firstRow = selected - visibleRows + 1;
}

void ModsUi::update() {
    if (!hwnd || (revision == mods->revision() && dark == app->settings.bubbleDark)) return;
    dark = app->settings.bubbleDark;
    if (revision != mods->revision()) {
        revision = mods->revision();
        const auto& list = mods->list();
        firstRow = std::min(firstRow, list.size() > visibleRows ? list.size() - visibleRows : 0);
        if (list.empty()) {
            selected = firstRow = 0;
            selectedFolder.clear();
            detailScroll = 0;
        } else {
            auto it = std::find_if(list.begin(), list.end(), [&](const auto& mod) { return mod.folder == selectedFolder; });
            if (it == list.end()) select(std::min(selected, list.size() - 1));
            else selected = (size_t)(it - list.begin());
        }
    }
    redraw();
}

bool ModsUi::draw() {
    auto& gfx = app->gfx;
    int w = (int)std::floor(width * scale), h = (int)std::floor(height * scale);
    if ((surface.w != w || surface.h != h) && !surface.create(gfx, w, h)) return false;
    if (!titleFont || fontScale != scale) {
        auto setup = [&](auto& font, float size, DWRITE_FONT_WEIGHT weight, bool wrap = false) {
            font.Reset();
            if (FAILED(gfx.dw->CreateTextFormat(gfx.uiFamily.c_str(), gfx.ui.Get(), weight, DWRITE_FONT_STYLE_NORMAL,
                                                DWRITE_FONT_STRETCH_NORMAL, size, L"ru-ru", &font))) return false;
            font->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
            font->SetParagraphAlignment(wrap ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR : DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            if (!wrap) {
                ComPtr<IDWriteInlineObject> ellipsis;
                DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
                if (SUCCEEDED(gfx.dw->CreateEllipsisTrimmingSign(font.Get(), &ellipsis))) font->SetTrimming(&trimming, ellipsis.Get());
            }
            return true;
        };
        if (!setup(titleFont, 22, DWRITE_FONT_WEIGHT_BLACK) || !setup(bodyFont, 14, DWRITE_FONT_WEIGHT_EXTRA_BOLD) ||
            !setup(smallFont, 12, DWRITE_FONT_WEIGHT_BOLD) || !setup(detailFont, 13, DWRITE_FONT_WEIGHT_BOLD, true)) return false;
        fontScale = scale;
    }
    auto* rt = surface.begin();
    if (!rt) return false;
    auto* brush = surface.brush.Get();
    const auto p = uiPalette(dark);
    const auto white = D2D1::ColorF(1.f, 1.f, 1.f, 1.f);
    const auto left = DWRITE_TEXT_ALIGNMENT_LEADING, center = DWRITE_TEXT_ALIGNMENT_CENTER;
    auto ink = [&](D2D1_COLOR_F color, float opacity = 1.f) {
        brush->SetColor(color);
        brush->SetOpacity(opacity);
        return brush;
    };
    auto box = [&](float x, float y, float r, float b) { return D2D1::RectF(x, y, r, b); };
    auto rounded = [&](D2D1_RECT_F r, float radius, D2D1_COLOR_F color) {
        rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), ink(color));
    };
    auto text = [&](const std::wstring& value, IDWriteTextFormat* font, D2D1_RECT_F r, D2D1_COLOR_F color, DWRITE_TEXT_ALIGNMENT align) {
        font->SetTextAlignment(align);
        rt->DrawTextW(value.c_str(), (UINT32)value.size(), font, r, ink(color), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };
    auto button = [&](D2D1_RECT_F r, int id, const std::wstring& label, bool active = false, bool enabled = true) {
        const bool hovered = enabled && contains(r, mouse);
        auto shape = D2D1::RoundedRect(r, (r.bottom - r.top) / 2, (r.bottom - r.top) / 2);
        auto fill = active ? p.acc : hovered ? p.pillHov : p.pill;
        rt->FillRoundedRectangle(shape, ink(fill));
        auto border = hovered || (keyboard && focus == id) ? p.acc : p.pillLine;
        rt->DrawRoundedRectangle(shape, ink(border), 1.6f);
        text(label, bodyFont.Get(), box(r.left + 8, r.top, r.right - 8, r.bottom), active ? white : enabled ? p.ink : p.dim, center);
        if (enabled) hits.push_back({r, id});
    };

    hits.clear();
    rt->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));
    auto outer = D2D1::RoundedRect(box(12, 12, 572, 354), 20, 20);
    auto shadow = outer;
    shadow.rect.top += 4;
    shadow.rect.bottom += 4;
    rt->FillRoundedRectangle(shadow, ink(p.shadow));
    rt->FillRoundedRectangle(outer, ink(p.box));
    if (dark) rt->DrawRoundedRectangle(outer, ink(p.acc, 0.22f), 6);
    rt->DrawRoundedRectangle(outer, ink(p.line), dark ? 2.f : 2.6f);

    text(L"Моды", titleFont.Get(), box(30, 23, 485, 62), p.ink, left);
    auto closeRect = box(522, 27, 554, 59);
    bool hoverClose = contains(closeRect, mouse) || (keyboard && focus == IdClose);
    rt->FillEllipse(D2D1::Ellipse({538, 43}, 16, 16), ink(hoverClose ? p.acc : p.pill));
    rt->DrawLine({533, 38}, {543, 48}, ink(hoverClose ? white : p.ink), 2.2f);
    rt->DrawLine({533, 48}, {543, 38}, ink(hoverClose ? white : p.ink), 2.2f);
    hits.push_back({closeRect, IdClose});
    button(box(29, 69, 220, 103), IdFolder, L"Открыть папку модов");
    button(box(230, 69, 382, 103), IdReload, L"Обновить список");

    listRect = box(28, 118, 252, 338);
    rounded(listRect, 15, p.card);
    const auto& list = mods->list();
    if (list.empty()) text(L"Моды не найдены", bodyFont.Get(), box(43, 129, 236, 169), p.dim, left);
    for (size_t slot = 0; slot < visibleRows && firstRow + slot < list.size(); ++slot) {
        size_t index = firstRow + slot;
        const auto& mod = list[index];
        float y = 127 + (float)slot * 51;
        auto row = box(37, y, 239, y + 45);
        bool chosen = selected == index, hovered = contains(row, mouse);
        if (chosen || hovered) rounded(row, 10, p.pillHov);
        if (chosen) {
            rt->DrawRoundedRectangle(D2D1::RoundedRect(row, 10, 10), ink(keyboard && focus == IdList ? p.acc : p.pillLine), 1.5f);
            rounded(box(40, y + 10, 43, y + 35), 1.5f, p.acc);
        }
        text(mod.name, bodyFont.Get(), box(49, y + 1, 227, y + 24), p.ink, left);
        auto caption = mod.version.empty() ? mod.status() : mod.version + L"  ·  " + mod.status();
        text(caption, smallFont.Get(), box(49, y + 23, 227, y + 42), mod.error.empty() ? p.dim : p.danger, left);
        hits.push_back({row, IdRow + (int)index});
    }
    if (list.size() > visibleRows) {
        float track = listRect.bottom - listRect.top - 22, length = std::max(18.f, track * visibleRows / list.size());
        float start = listRect.top + 11 + (track - length) * firstRow / (list.size() - visibleRows);
        rounded(box(244, listRect.top + 11, 247, listRect.bottom - 11), 1.5f, p.track);
        rounded(box(244, start, 247, start + length), 1.5f, p.dim);
    }

    auto panel = box(266, 118, 556, 338);
    rounded(panel, 15, p.card);
    detailsRect = box(281, 234, 541, 326);
    detailMax = 0;
    if (auto* mod = selection()) {
        text(mod->name, titleFont.Get(), box(281, 128, 541, 157), p.ink, left);
        auto status = mod->version.empty() ? mod->status() : mod->version + L"  ·  " + mod->status();
        text(status, smallFont.Get(), box(282, 158, 541, 181), mod->error.empty() ? p.dim : p.danger, left);
        button(box(281, 190, 410, 223), IdToggle, mod->enabled ? L"Выключить" : L"Включить", !mod->enabled && mod->valid, mod->valid);
        if (!mod->error.empty()) {
            rounded(detailsRect, 9, p.pill);
            if (keyboard && focus == IdDetails)
                rt->DrawRoundedRectangle(D2D1::RoundedRect(detailsRect, 9, 9), ink(p.acc), 1.6f);
            hits.push_back({detailsRect, IdDetails});
            auto clip = box(detailsRect.left + 9, detailsRect.top + 7, detailsRect.right - 7, detailsRect.bottom - 7);
            ComPtr<IDWriteTextLayout> layout;
            if (SUCCEEDED(gfx.dw->CreateTextLayout(mod->error.c_str(), (UINT32)mod->error.size(), detailFont.Get(),
                                                   clip.right - clip.left - 5, 1600.f, &layout))) {
                DWRITE_TEXT_METRICS metrics{};
                layout->GetMetrics(&metrics);
                float available = clip.bottom - clip.top;
                detailMax = std::max(0.f, metrics.height - available);
                detailScroll = clampf(detailScroll, 0.f, detailMax);
                rt->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                rt->DrawTextLayout({clip.left, clip.top - detailScroll}, layout.Get(), ink(p.ink));
                rt->PopAxisAlignedClip();
                if (detailMax > 0) {
                    float length = std::max(12.f, available * available / metrics.height);
                    float top = clip.top + (available - length) * detailScroll / detailMax;
                    rounded(box(clip.right - 2, top, clip.right, top + length), 1, p.dim);
                }
            }
        } else if (mod->ready && !mod->controls.empty()) {
            text(L"Управление модом", smallFont.Get(), box(282, 230, 541, 249), p.dim, left);
            for (size_t i = 0; i < mod->controls.size(); ++i) {
                const auto& control = mod->controls[i];
                float x = 281 + (i % 2) * 132.f, y = 251 + (i / 2) * 39.f;
                button(box(x, y, x + 126, y + 32), IdControl + (int)i, control.label, control.toggle && control.value);
            }
        }
    } else {
        text(L"Откройте папку модов, добавьте мод и обновите список.", detailFont.Get(), box(281, 133, 541, 203), p.dim, left);
    }
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    return surface.end(gfx);
}

void ModsUi::redraw() {
    if (!hwnd || !draw()) return;
    RECT rect{};
    GetWindowRect(hwnd, &rect);
    surface.present(hwnd, {rect.left, rect.top}, nullptr);
}

int ModsUi::at(V2 point) const {
    for (const auto& hit : hits)
        if (contains(hit.rect, point)) return hit.id;
    return 0;
}

void ModsUi::command(int id) {
    if (id == IdClose) {
        close();
        return;
    }
    if (id == IdFolder) {
        auto path = mods->folder();
        if (!mods->ensureFolder() || (INT_PTR)ShellExecuteW(hwnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL) <= 32)
            MessageBoxW(hwnd, L"Не удалось открыть папку модов.", L"Моды UziPet", MB_OK | MB_ICONERROR);
    } else if (id == IdReload) {
        mods->refresh();
    } else if (id >= IdRow) {
        select((size_t)(id - IdRow));
    } else if (auto* mod = selection()) {
        if (id == IdToggle && mod->valid) {
            if (!mods->setEnabled(selected, !mod->enabled))
                MessageBoxW(hwnd, L"Не удалось сохранить состояние мода.", L"Моды UziPet", MB_OK | MB_ICONERROR);
        } else if (id >= IdControl && id < IdControl + Mods::maxControls) {
            mods->activateControl(selected, id - IdControl);
        }
    }
    if (revision != mods->revision()) update();
    else redraw();
}

void ModsUi::key(WPARAM key) {
    keyboard = true;
    if (key == VK_ESCAPE) {
        close();
        return;
    }
    if (key == VK_TAB) {
        std::vector<int> order{IdFolder, IdReload, IdList};
        if (auto* mod = selection()) {
            if (mod->valid) order.push_back(IdToggle);
            if (!mod->error.empty()) order.push_back(IdDetails);
            if (mod->ready)
                for (size_t i = 0; i < mod->controls.size(); ++i) order.push_back(IdControl + (int)i);
        }
        order.push_back(IdClose);
        bool back = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        auto it = std::find(order.begin(), order.end(), focus);
        size_t index = it == order.end() ? (back ? 0 : order.size() - 1) : (size_t)(it - order.begin());
        focus = order[(index + (back ? order.size() - 1 : 1)) % order.size()];
        redraw();
        return;
    }
    if (key == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
        auto* mod = selection();
        if (!mod || mod->error.empty() || !OpenClipboard(hwnd)) return;
        auto bytes = (mod->error.size() + 1) * sizeof(wchar_t);
        if (auto data = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
            if (auto dst = GlobalLock(data)) {
                std::memcpy(dst, mod->error.c_str(), bytes);
                GlobalUnlock(data);
                if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, data)) data = nullptr;
            }
            if (data) GlobalFree(data);
        }
        CloseClipboard();
        return;
    }
    if (focus == IdDetails) {
        if (key == VK_UP) detailScroll -= 15.f;
        else if (key == VK_DOWN) detailScroll += 15.f;
        else if (key == VK_PRIOR) detailScroll -= 65.f;
        else if (key == VK_NEXT) detailScroll += 65.f;
        else if (key == VK_HOME) detailScroll = 0;
        else if (key == VK_END) detailScroll = detailMax;
        redraw();
    } else if ((key == VK_RETURN || key == VK_SPACE) && focus != IdList) {
        command(focus);
    } else if (focus == IdList && !mods->list().empty()) {
        auto last = mods->list().size() - 1;
        if (key == VK_UP && selected) select(selected - 1);
        else if (key == VK_DOWN) select(std::min(selected + 1, last));
        else if (key == VK_HOME) select(0);
        else if (key == VK_END) select(last);
        else if (key == VK_PRIOR) select(selected > visibleRows ? selected - visibleRows : 0);
        else if (key == VK_NEXT) select(std::min(selected + visibleRows, last));
        redraw();
    }
}

LRESULT CALLBACK ModsUi::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* ui = reinterpret_cast<ModsUi*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        ui = static_cast<ModsUi*>(reinterpret_cast<const CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)ui);
        ui->hwnd = h;
    }
    if (!ui) return DefWindowProcW(h, msg, wp, lp);
    auto local = [&] { return V2(GET_X_LPARAM(lp) / ui->scale, GET_Y_LPARAM(lp) / ui->scale); };
    auto screen = [&] {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(h, &pt);
        return V2(pt.x / ui->scale, pt.y / ui->scale);
    };
    switch (msg) {
    case WM_DPICHANGED:
        ui->resize(*reinterpret_cast<RECT*>(lp), HIWORD(wp));
        ui->redraw();
        return 0;
    case WM_NCHITTEST: {
        auto p = screen();
        return p.y >= 15 && p.y < 65 && !ui->at(p) ? HTCAPTION : HTCLIENT;
    }
    case WM_MOUSEMOVE: {
        int was = ui->at(ui->mouse);
        ui->mouse = local();
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, h, 0};
        TrackMouseEvent(&track);
        if (was != ui->at(ui->mouse)) ui->redraw();
        return 0;
    }
    case WM_MOUSELEAVE:
        ui->mouse = {-1.f, -1.f};
        ui->redraw();
        return 0;
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        auto p = screen();
        auto count = ui->mods->list().size();
        if (contains(ui->listRect, p) && count > visibleRows) {
            int step = delta > 0 ? -1 : 1;
            auto start = (int)ui->firstRow;
            ui->firstRow = (size_t)std::clamp(start + step, 0, (int)(count - visibleRows));
            ui->redraw();
        } else if (contains(ui->detailsRect, p) && ui->detailMax > 0) {
            ui->detailScroll = clampf(ui->detailScroll - delta * (42.f / WHEEL_DELTA), 0.f, ui->detailMax);
            ui->redraw();
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        auto p = local();
        ui->pressed = ui->at(p);
        ui->focus = contains(ui->listRect, p) ? IdList : ui->pressed;
        ui->keyboard = false;
        if (ui->pressed) SetCapture(h);
        return 0;
    }
    case WM_LBUTTONUP: {
        int id = ui->at(local());
        int pressed = ui->pressed;
        ui->pressed = 0;
        if (GetCapture() == h) ReleaseCapture();
        if (id && id == pressed) ui->command(id);
        return 0;
    }
    case WM_CAPTURECHANGED:
        ui->pressed = 0;
        return 0;
    case WM_KEYDOWN:
        ui->key(wp);
        return 0;
    case WM_NCDESTROY:
        ui->surface.destroy();
        ui->hits.clear();
        ui->mouse = {-1.f, -1.f};
        ui->keyboard = false;
        ui->hwnd = nullptr;
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
