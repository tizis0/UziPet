#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <dwrite.h>
#include <wincodec.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <exdisp.h>
#include <shlguid.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <random>
#include <functional>
#include <unordered_map>

using Microsoft::WRL::ComPtr;

constexpr float PI = 3.14159265f;

struct V2 {
    float x = 0, y = 0;
    V2() = default;
    V2(float x_, float y_) : x(x_), y(y_) {}
    V2 operator+(V2 o) const { return {x + o.x, y + o.y}; }
    V2 operator-(V2 o) const { return {x - o.x, y - o.y}; }
    V2 operator-() const { return {-x, -y}; }
    V2 operator*(float s) const { return {x * s, y * s}; }
    V2 operator/(float s) const { return {x / s, y / s}; }
    V2& operator+=(V2 o) { x += o.x; y += o.y; return *this; }
    V2& operator-=(V2 o) { x -= o.x; y -= o.y; return *this; }
    V2& operator*=(float s) { x *= s; y *= s; return *this; }
    float len() const { return std::sqrt(x * x + y * y); }
    V2 norm() const { float l = len(); return l > 1e-6f ? V2{x / l, y / l} : V2{0, 0}; }
    D2D1_POINT_2F d2() const { return {x, y}; }
};
inline float dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
inline float cross(V2 a, V2 b) { return a.x * b.y - a.y * b.x; }
inline V2 rotv(V2 v, float a) { float c = cosf(a), s = sinf(a); return {v.x * c - v.y * s, v.x * s + v.y * c}; }
inline V2 xf(const D2D1::Matrix3x2F& m, V2 p) { auto r = m.TransformPoint({p.x, p.y}); return {r.x, r.y}; }

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smooth01(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }
inline float approach(float cur, float target, float rate, float dt) { return target + (cur - target) * expf(-rate * dt); }
inline float wrapAngle(float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }
inline float signf(float v) { return v < 0 ? -1.f : 1.f; }

float randf(float a, float b);
int randi(int a, int b);
bool chance(float p);

std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& s);
bool readFile(const std::wstring& path, std::string& out);
bool writeFile(const std::wstring& path, const std::string& data);
std::wstring exeDir();
std::wstring appDataDir();
void setAppDataDir(const std::wstring& d);
bool waitIniReadable(const std::wstring& path, const wchar_t* section, int ms);
void logf(const char* fmt, ...);
D2D1_COLOR_F parseColor(const std::string& hex, float a = 1.f);
double nowSec();
