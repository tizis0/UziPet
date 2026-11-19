#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

const uint32_t kUziHitboxMagic = 0x485A4955;
const int kHitCols = 64, kHitParts = 48;

enum : uint32_t {
    UziHitVisible = 1,
    UziHitSleeping = 2,
    UziHitSitting = 4,
    UziHitDragged = 8,
    UziHitAir = 16,
    UziHitBubble = 32,
    UziHitHolding = 64,
};

#pragma pack(push, 4)
struct UziHitbox {
    uint32_t magic, version;
    volatile long seq;
    uint32_t flags;
    double time;
    float dpiScale;
    float bounds[4];
    float head[3];
    float groundY;
    int32_t cols;
    float colX0, colW;
    float top[kHitCols];
    float bottom[kHitCols];
    int32_t nparts;
    float parts[kHitParts][4];
    float bubble[4];
    float held[4];
};
#pragma pack(pop)
static_assert(sizeof(UziHitbox) == 1388, "раскладка описана в docs/api.md");
static_assert(offsetof(UziHitbox, top) == 72 && offsetof(UziHitbox, parts) == 588 && offsetof(UziHitbox, held) == 1372, "docs/api.md");

int hitboxPrint();
int hitboxJson(const wchar_t* name, std::string& out);
extern const wchar_t* g_hitboxName;
