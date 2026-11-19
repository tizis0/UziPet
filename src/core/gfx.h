#pragma once
#include "common.h"

struct Gfx {
    ComPtr<ID2D1Factory1> d2d;
    ComPtr<IDWriteFactory> dw;
    ComPtr<IWICImagingFactory> wic;
    ComPtr<ID2D1StrokeStyle> round;
    ComPtr<IDWriteFontCollection> ui;
    std::wstring uiFamily = L"Segoe UI";
    bool init();
    bool loadUiFont(const std::wstring& path, const wchar_t* family);
    ComPtr<IWICBitmapSource> loadImage(const std::wstring& path);
    ComPtr<IWICBitmapSource> scaled(IWICBitmapSource* src, float k, float* kx = nullptr, float* ky = nullptr);
    ComPtr<IWICBitmapSource> fromHBitmap(HBITMAP bmp);
    ComPtr<IWICBitmapSource> darkHalo(IWICBitmapSource* src, int pad, float* edge = nullptr);
};

struct Surface {
    int w = 0, h = 0;
    HDC dc = nullptr;
    HBITMAP bmp = nullptr, oldBmp = nullptr;
    uint32_t* bits = nullptr;
    ComPtr<IWICBitmap> wbmp;
    ComPtr<ID2D1RenderTarget> rt;
    ComPtr<ID2D1SolidColorBrush> brush;
    int gen = 0;
    RECT drawn{};
    Gfx* g = nullptr;

    bool create(Gfx& g, int w, int h);
    void destroy();
    ID2D1RenderTarget* begin(D2D1_COLOR_F clear = {0, 0, 0, 0}, const RECT* dirty = nullptr);
    bool end(Gfx& g);
    void present(HWND hwnd, POINT screenPos, const RECT* dirty = nullptr, BYTE alpha = 255);
    bool savePng(Gfx& g, const std::wstring& path);
    HICON toIcon(bool cursor = false, int hotX = 0, int hotY = 0);
    ~Surface() { destroy(); }

private:
    bool clipped = false;
    bool makeTarget(Gfx& g);
    void copyOut(const RECT& r);
};

struct DrawCtx {
    ID2D1RenderTarget* rt = nullptr;
    ID2D1Factory* f = nullptr;
    IDWriteFactory* dw = nullptr;
    ID2D1SolidColorBrush* br = nullptr;
    ID2D1StrokeStyle* round = nullptr;
    int gen = 0;
    float s = 1;
    IDWriteFontCollection* fonts = nullptr;
    const wchar_t* family = L"Segoe UI";
    void color(D2D1_COLOR_F c, float op = 1.f) { br->SetColor(c); br->SetOpacity(op); }
};

struct CachedBitmap {
    ComPtr<IWICBitmapSource> src;
    ComPtr<ID2D1Bitmap> bmp;
    ID2D1RenderTarget* rt = nullptr;
    int gen = -1;
    ID2D1Bitmap* get(const DrawCtx& c) {
        if (!src) return nullptr;
        if (!bmp || rt != c.rt || gen != c.gen) {
            bmp.Reset();
            c.rt->CreateBitmapFromWicBitmap(src.Get(), nullptr, &bmp);
            rt = c.rt;
            gen = c.gen;
        }
        return bmp.Get();
    }
    void reset() { bmp.Reset(); rt = nullptr; gen = -1; }
};
