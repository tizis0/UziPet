#include "pet.h"
#include "hitbox.h"

const wchar_t* g_hitboxName = L"Local\\UziPetHitbox";

namespace {
HANDLE g_map = nullptr;
UziHitbox* g_hb = nullptr;
double g_last = -1;
}

void rigBuildHitPoints(Rig& r) {
    if (r.alpha.empty()) return;
    for (auto& d : r.parts) {
        d.hitPts.clear();
        float sw = d.src.right - d.src.left, sh = d.src.bottom - d.src.top;
        if (sw < 1 || sh < 1) continue;
        int x0 = std::max(0, (int)floorf(d.src.left)), y0 = std::max(0, (int)floorf(d.src.top));
        int x1 = std::min(r.alphaW, (int)ceilf(d.src.right)), y1 = std::min(r.alphaH, (int)ceilf(d.src.bottom));
        for (int y = y0; y < y1; y += 2)
            for (int x = x0; x < x1; x += 2)
                if (r.alpha[(size_t)y * r.alphaW + x] > 96)
                    d.hitPts.push_back(V2(d.pos.x + (x + 0.5f - d.src.left) * d.size.x / sw, d.pos.y + (y + 0.5f - d.src.top) * d.size.y / sh));
    }
}

static bool hbOpen() {
    if (g_hb) return true;
    g_map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(UziHitbox), g_hitboxName);
    if (!g_map) return false;
    g_hb = (UziHitbox*)MapViewOfFile(g_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(UziHitbox));
    if (!g_hb) {
        CloseHandle(g_map);
        g_map = nullptr;
        return false;
    }
    memset(g_hb, 0, sizeof(UziHitbox));
    g_hb->magic = kUziHitboxMagic;
    g_hb->version = 1;
    logf("hitbox: shared memory %s ready (%zu bytes)", narrow(g_hitboxName).c_str(), sizeof(UziHitbox));
    return true;
}

void Pet::hitboxFill(UziHitbox& h, V2 wo) const {
    memset(&h, 0, sizeof(h));
    h.magic = kUziHitboxMagic;
    h.version = 1;
    h.time = nowSec();
    h.dpiScale = S();
    for (int i = 0; i < kHitCols; i++) h.top[i] = h.bottom[i] = -1;
    bool shown = !(st == St::Inside && (nest == NestWeb || nest == NestNone)) && !introHidden;
    const RigInstance& in = const_cast<Pet*>(this)->curInst();
    const Rig* r = in.rig;
    if (shown && r && in.world.size() >= r->parts.size()) {
        float l = 1e9f, t = 1e9f, rr = -1e9f, b = -1e9f;
        std::vector<V2> pts;
        int np = 0;
        for (size_t i = 0; i < r->parts.size(); i++) {
            const auto& d = r->parts[i];
            if (d.hitPts.empty()) continue;
            const auto& M = in.world[i];
            float pl = 1e9f, pt = 1e9f, pr = -1e9f, pb = -1e9f;
            for (const V2& q : d.hitPts) {
                D2D1_POINT_2F s = M.TransformPoint(D2D1::Point2F(q.x, q.y));
                V2 p(s.x + wo.x, s.y + wo.y);
                pts.push_back(p);
                pl = std::min(pl, p.x), pt = std::min(pt, p.y), pr = std::max(pr, p.x), pb = std::max(pb, p.y);
            }
            l = std::min(l, pl), t = std::min(t, pt), rr = std::max(rr, pr), b = std::max(b, pb);
            if (np < kHitParts) {
                float* o = h.parts[np++];
                o[0] = pl - 1, o[1] = pt - 1, o[2] = pr + 1, o[3] = pb + 1;
            }
        }
        h.nparts = np;
        if (!pts.empty()) {
            float pad = 1.5f * S();
            l -= pad, t -= pad, rr += pad, b += pad;
            h.bounds[0] = l, h.bounds[1] = t, h.bounds[2] = rr, h.bounds[3] = b;
            h.cols = kHitCols;
            h.colX0 = l;
            h.colW = (rr - l) / kHitCols;
            for (const V2& p : pts) {
                int c = std::clamp((int)((p.x - l) / std::max(0.01f, h.colW)), 0, kHitCols - 1);
                if (h.top[c] < 0 || p.y - pad < h.top[c]) h.top[c] = p.y - pad;
                if (p.y + pad > h.bottom[c]) h.bottom[c] = p.y + pad;
            }
            h.flags |= UziHitVisible;
        }
    }
    h.head[0] = headC.x, h.head[1] = headC.y, h.head[2] = headR;
    h.groundY = gnd.valid ? gnd.y : pos.y;
    if (st == St::Sleep) h.flags |= UziHitSleeping;
    if (st == St::Sit || st == St::Watch) h.flags |= UziHitSitting;
    if (dragging) h.flags |= UziHitDragged;
    if (!grounded()) h.flags |= UziHitAir;
    if (app.bubble.active) {
        h.flags |= UziHitBubble;
        h.bubble[0] = app.bubble.rect.left + wo.x, h.bubble[1] = app.bubble.rect.top + wo.y;
        h.bubble[2] = app.bubble.rect.right + wo.x, h.bubble[3] = app.bubble.rect.bottom + wo.y;
    }
    if (heldShown() || !carriedName.empty()) {
        h.flags |= UziHitHolding;
        float s = 24.f * S();
        h.held[0] = carriedCenter.x - s, h.held[1] = carriedCenter.y - s, h.held[2] = carriedCenter.x + s, h.held[3] = carriedCenter.y + s;
    }
}

void Pet::hitboxPublish(V2 wo) {
    if (app.world.fake && wcscmp(g_hitboxName, L"Local\\UziPetHitbox") == 0) return;
    double now = nowSec();
    if (now - g_last < 1.0 / 30.0 && !app.world.fake) return;
    g_last = now;
    if (!hbOpen()) return;
    UziHitbox h;
    hitboxFill(h, wo);
    LONG seq = g_hb->seq;
    InterlockedExchange(&g_hb->seq, seq | 1);
    MemoryBarrier();
    uint32_t keep = g_hb->seq;
    h.seq = keep;
    memcpy(g_hb, &h, sizeof(h));
    MemoryBarrier();
    InterlockedExchange(&g_hb->seq, (LONG)((keep | 1) + 1));
}

int hitboxJson(const wchar_t* name, std::string& o) {
    HANDLE m = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (!m) return 1;
    const UziHitbox* src = (const UziHitbox*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, sizeof(UziHitbox));
    if (!src) {
        CloseHandle(m);
        return 1;
    }
    UziHitbox h{};
    for (int k = 0; k < 50; k++) {
        LONG a = src->seq;
        MemoryBarrier();
        memcpy(&h, src, sizeof(h));
        MemoryBarrier();
        if (!(a & 1) && a == src->seq) break;
        Sleep(1);
    }
    UnmapViewOfFile(src);
    CloseHandle(m);
    o.clear();
    char b[128];
    auto num = [&](float v) {
        snprintf(b, sizeof(b), "%.1f", v);
        o += b;
    };
    auto arr = [&](const float* v, int n) {
        o += "[";
        for (int i = 0; i < n; i++) {
            if (i) o += ",";
            num(v[i]);
        }
        o += "]";
    };
    snprintf(b, sizeof(b), "{\"version\":%u,\"seq\":%u,\"visible\":%s,\"sleeping\":%s,\"sitting\":%s,\"dragged\":%s,\"inAir\":%s,", h.version, h.seq,
             h.flags & UziHitVisible ? "true" : "false", h.flags & UziHitSleeping ? "true" : "false", h.flags & UziHitSitting ? "true" : "false",
             h.flags & UziHitDragged ? "true" : "false", h.flags & UziHitAir ? "true" : "false");
    o += b;
    o += "\"bounds\":";
    arr(h.bounds, 4);
    o += ",\"head\":";
    arr(h.head, 3);
    o += ",\"groundY\":";
    num(h.groundY);
    snprintf(b, sizeof(b), ",\"cols\":%d,\"colX0\":", h.cols);
    o += b;
    num(h.colX0);
    o += ",\"colW\":";
    num(h.colW);
    o += ",\"top\":";
    arr(h.top, kHitCols);
    o += ",\"bottom\":";
    arr(h.bottom, kHitCols);
    o += ",\"parts\":[";
    for (int i = 0; i < std::min(h.nparts, kHitParts); i++) {
        if (i) o += ",";
        arr(h.parts[i], 4);
    }
    o += "]";
    if (h.flags & UziHitBubble) {
        o += ",\"bubble\":";
        arr(h.bubble, 4);
    }
    if (h.flags & UziHitHolding) {
        o += ",\"held\":";
        arr(h.held, 4);
    }
    o += "}\n";
    return 0;
}

int hitboxPrint() {
    std::string o;
    if (hitboxJson(L"Local\\UziPetHitbox", o)) return 1;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!out || out == INVALID_HANDLE_VALUE) {
        AttachConsole(ATTACH_PARENT_PROCESS);
        out = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    DWORD w = 0;
    if (out && out != INVALID_HANDLE_VALUE) WriteFile(out, o.data(), (DWORD)o.size(), &w, nullptr);
    return 0;
}
