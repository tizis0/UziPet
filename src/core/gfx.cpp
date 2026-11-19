#include "gfx.h"
#include <dwrite_3.h>

bool Gfx::loadUiFont(const std::wstring& path, const wchar_t* family) {
    ComPtr<IDWriteFactory5> f5;
    if (FAILED(dw.As(&f5))) return false;
    ComPtr<IDWriteFontSetBuilder1> b;
    ComPtr<IDWriteFontFile> file;
    ComPtr<IDWriteFontSet> set;
    ComPtr<IDWriteFontCollection1> col;
    if (FAILED(f5->CreateFontSetBuilder(&b)) || FAILED(f5->CreateFontFileReference(path.c_str(), nullptr, &file)) || FAILED(b->AddFontFile(file.Get())) ||
        FAILED(b->CreateFontSet(&set)) || FAILED(f5->CreateFontCollectionFromFontSet(set.Get(), &col))) {
        logf("ui font not loaded: %s", narrow(path).c_str());
        return false;
    }
    UINT32 idx = 0;
    BOOL exists = FALSE;
    col->FindFamilyName(family, &idx, &exists);
    if (!exists) {
        logf("ui font: no family '%s' in file", narrow(family).c_str());
        return false;
    }
    ui = col;
    uiFamily = family;
    logf("ui font: %s", narrow(family).c_str());
    return true;
}

bool Gfx::init() {
    D2D1_FACTORY_OPTIONS opts{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts, (void**)d2d.GetAddressOf()))) {
        logf("D2D1CreateFactory failed");
        return false;
    }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)dw.GetAddressOf()))) {
        logf("DWriteCreateFactory failed");
        return false;
    }
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
        logf("WIC factory failed");
        return false;
    }
    d2d->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                       D2D1_LINE_JOIN_ROUND),
                           nullptr, 0, &round);
    return true;
}

ComPtr<IWICBitmapSource> Gfx::loadImage(const std::wstring& path) {
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec))) {
        logf("cannot open image %s", narrow(path).c_str());
        return nullptr;
    }
    ComPtr<IWICBitmapFrameDecode> fr;
    dec->GetFrame(0, &fr);
    ComPtr<IWICFormatConverter> cv;
    wic->CreateFormatConverter(&cv);
    if (FAILED(cv->Initialize(fr.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeMedianCut)))
        return nullptr;
    ComPtr<IWICBitmap> bmp;
    wic->CreateBitmapFromSource(cv.Get(), WICBitmapCacheOnLoad, &bmp);
    return bmp;
}

ComPtr<IWICBitmapSource> Gfx::scaled(IWICBitmapSource* src, float k, float* kx, float* ky) {
    UINT w = 0, h = 0;
    src->GetSize(&w, &h);
    UINT nw = std::max(1u, (UINT)lroundf(w * k)), nh = std::max(1u, (UINT)lroundf(h * k));
    if (kx) *kx = (float)nw / w;
    if (ky) *ky = (float)nh / h;
    ComPtr<IWICBitmapScaler> sc;
    wic->CreateBitmapScaler(&sc);
    sc->Initialize(src, nw, nh, WICBitmapInterpolationModeHighQualityCubic);
    ComPtr<IWICBitmap> bmp;
    wic->CreateBitmapFromSource(sc.Get(), WICBitmapCacheOnLoad, &bmp);
    return bmp;
}

ComPtr<IWICBitmapSource> Gfx::fromHBitmap(HBITMAP hb) {
    ComPtr<IWICBitmap> b;
    if (FAILED(wic->CreateBitmapFromHBITMAP(hb, nullptr, WICBitmapUsePremultipliedAlpha, &b))) return nullptr;
    ComPtr<IWICFormatConverter> cv;
    wic->CreateFormatConverter(&cv);
    if (FAILED(cv->Initialize(b.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeMedianCut)))
        return nullptr;
    ComPtr<IWICBitmap> out;
    wic->CreateBitmapFromSource(cv.Get(), WICBitmapCacheOnLoad, &out);
    return out;
}

ComPtr<IWICBitmapSource> Gfx::darkHalo(IWICBitmapSource* src, int pad, float* edge) {
    if (edge) *edge = 1.f;
    UINT w = 0, h = 0;
    if (!src || FAILED(src->GetSize(&w, &h)) || !w || !h || w > 1024 || h > 1024) return nullptr;
    ComPtr<IWICFormatConverter> cv;
    wic->CreateFormatConverter(&cv);
    if (!cv || FAILED(cv->Initialize(src, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeMedianCut)))
        return nullptr;
    std::vector<uint8_t> px((size_t)w * h * 4);
    if (FAILED(cv->CopyPixels(nullptr, w * 4, (UINT)px.size(), px.data()))) return nullptr;
    auto A = [&](int x, int y) { return x < 0 || y < 0 || x >= (int)w || y >= (int)h ? 0 : (int)px[((size_t)y * w + x) * 4 + 3]; };
    double lum = 0;
    int n = 0;
    for (int y = 0; y < (int)h; y++)
        for (int x = 0; x < (int)w; x++) {
            int a = A(x, y);
            if (a < 128 || (A(x - 1, y) >= 128 && A(x + 1, y) >= 128 && A(x, y - 1) >= 128 && A(x, y + 1) >= 128)) continue;
            const uint8_t* p = &px[((size_t)y * w + x) * 4];
            lum += (0.0722 * p[0] + 0.7152 * p[1] + 0.2126 * p[2]) / a;
            n++;
        }
    if (!n) return nullptr;
    float e = (float)(lum / n);
    if (edge) *edge = e;
    if (e >= 0.33f) return nullptr;
    int W = (int)w + 2 * pad, H = (int)h + 2 * pad, r = std::max(1, pad - 1);
    std::vector<int> dil((size_t)W * H, 0), bl((size_t)W * H, 0);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int m = 0;
            for (int dy = -r; dy <= r && m < 255; dy++)
                for (int dx = -r; dx <= r; dx++)
                    if (dx * dx + dy * dy <= r * r + r) m = std::max(m, A(x - pad + dx, y - pad + dy));
            dil[(size_t)y * W + x] = m;
        }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int sum = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int xx = x + dx, yy = y + dy;
                    if (xx >= 0 && yy >= 0 && xx < W && yy < H) sum += dil[(size_t)yy * W + xx];
                }
            bl[(size_t)y * W + x] = sum / 9;
        }
    std::vector<uint8_t> out((size_t)W * H * 4);
    for (size_t i = 0; i < bl.size(); i++) {
        uint8_t v = (uint8_t)(bl[i] * 150 / 255);
        out[i * 4] = out[i * 4 + 1] = out[i * 4 + 2] = out[i * 4 + 3] = v;
    }
    ComPtr<IWICBitmap> b;
    if (FAILED(wic->CreateBitmapFromMemory(W, H, GUID_WICPixelFormat32bppPBGRA, W * 4, (UINT)out.size(), out.data(), &b))) return nullptr;
    return b;
}

bool Surface::create(Gfx& g, int w_, int h_) {
    destroy();
    w = w_;
    h = h_;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void**)&bits, nullptr, 0);
    if (!bmp) return false;
    oldBmp = (HBITMAP)SelectObject(dc, bmp);
    this->g = &g;
    if (FAILED(g.wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &wbmp))) return false;
    return makeTarget(g);
}

bool Surface::makeTarget(Gfx& g) {
    rt.Reset();
    brush.Reset();
    auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                              D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.f, 96.f);
    if (FAILED(g.d2d->CreateWicBitmapRenderTarget(wbmp.Get(), &props, &rt))) {
        logf("CreateWicBitmapRenderTarget failed");
        return false;
    }
    rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush);
    static int counter = 0;
    gen = ++counter;
    return true;
}

void Surface::destroy() {
    brush.Reset();
    rt.Reset();
    wbmp.Reset();
    if (dc) {
        if (oldBmp) SelectObject(dc, oldBmp);
        DeleteDC(dc);
    }
    if (bmp) DeleteObject(bmp);
    dc = nullptr;
    bmp = oldBmp = nullptr;
    bits = nullptr;
}

ID2D1RenderTarget* Surface::begin(D2D1_COLOR_F clear, const RECT* dirty) {
    drawn = dirty ? *dirty : RECT{0, 0, w, h};
    rt->BeginDraw();
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    clipped = dirty != nullptr;
    if (clipped) rt->PushAxisAlignedClip(D2D1::RectF((float)dirty->left, (float)dirty->top, (float)dirty->right, (float)dirty->bottom),
                                         D2D1_ANTIALIAS_MODE_ALIASED);
    rt->Clear(clear);
    return rt.Get();
}

bool Surface::end(Gfx& g) {
    if (clipped) rt->PopAxisAlignedClip();
    clipped = false;
    HRESULT hr = rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        logf("render target lost, recreating");
        makeTarget(g);
        return false;
    }
    copyOut(drawn);
    return SUCCEEDED(hr);
}

void Surface::copyOut(const RECT& r0) {
    RECT r, whole{0, 0, w, h};
    if (!IntersectRect(&r, &r0, &whole)) return;
    WICRect wr{r.left, r.top, r.right - r.left, r.bottom - r.top};
    ComPtr<IWICBitmapLock> lock;
    if (FAILED(wbmp->Lock(&wr, WICBitmapLockRead, &lock))) return;
    UINT stride = 0, size = 0;
    BYTE* src = nullptr;
    lock->GetStride(&stride);
    lock->GetDataPointer(&size, &src);
    for (int y = 0; y < wr.Height; y++)
        memcpy(bits + (size_t)(r.top + y) * w + r.left, src + (size_t)y * stride, (size_t)wr.Width * 4);
}

void Surface::present(HWND hwnd, POINT pos, const RECT* dirty, BYTE alpha) {
    BLENDFUNCTION bf{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
    POINT src{0, 0};
    SIZE sz{w, h};
    UPDATELAYEREDWINDOWINFO info{sizeof(info)};
    info.pptDst = &pos;
    info.psize = &sz;
    info.hdcSrc = dc;
    info.pptSrc = &src;
    info.pblend = &bf;
    info.dwFlags = ULW_ALPHA;
    info.prcDirty = dirty;
    if (!UpdateLayeredWindowIndirect(hwnd, &info)) UpdateLayeredWindow(hwnd, nullptr, &pos, &sz, dc, &src, 0, &bf, ULW_ALPHA);
}

bool Surface::savePng(Gfx& g, const std::wstring& path) {
    std::vector<uint8_t> buf((size_t)w * h * 4);
    const uint8_t* s = (const uint8_t*)bits;
    for (size_t i = 0; i < (size_t)w * h; i++) {
        uint8_t a = s[i * 4 + 3];
        for (int c = 0; c < 3; c++) {
            int v = s[i * 4 + c];
            buf[i * 4 + c] = (uint8_t)(a ? std::min(255, v * 255 / a) : 0);
        }
        buf[i * 4 + 3] = a;
    }
    ComPtr<IWICBitmap> wb;
    if (FAILED(g.wic->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppBGRA, w * 4, (UINT)buf.size(), buf.data(), &wb))) return false;
    ComPtr<IWICStream> st;
    g.wic->CreateStream(&st);
    if (FAILED(st->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;
    ComPtr<IWICBitmapEncoder> enc;
    g.wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    enc->Initialize(st.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> fe;
    enc->CreateNewFrame(&fe, nullptr);
    fe->Initialize(nullptr);
    fe->SetSize(w, h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    fe->SetPixelFormat(&fmt);
    fe->WriteSource(wb.Get(), nullptr);
    fe->Commit();
    return SUCCEEDED(enc->Commit());
}

HICON Surface::toIcon(bool cursor, int hotX, int hotY) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    uint8_t* px = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, (void**)&px, nullptr, 0);
    ReleaseDC(nullptr, screen);
    const uint8_t* s = (const uint8_t*)bits;
    for (size_t i = 0; i < (size_t)w * h; i++) {
        uint8_t a = s[i * 4 + 3];
        for (int c = 0; c < 3; c++) px[i * 4 + c] = (uint8_t)(a ? std::min(255, s[i * 4 + c] * 255 / a) : 0);
        px[i * 4 + 3] = a;
    }
    HBITMAP mask = CreateBitmap(w, h, 1, 1, nullptr);
    ICONINFO ii{cursor ? FALSE : TRUE, (DWORD)hotX, (DWORD)hotY, mask, color};
    HICON ic = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return ic;
}
