#include "rig.h"

using M3 = D2D1::Matrix3x2F;

static V2 jv(const Json& a, float k) { return V2(a[0].f() * k, a[1].f() * k); }

static std::vector<V2> resample(const std::vector<V2>& pts, int n) {
    if (pts.size() < 2) return pts;
    std::vector<V2> dense;
    size_t m = pts.size();
    for (size_t i = 0; i + 1 < m; i++) {
        V2 p0 = pts[i ? i - 1 : 0], p1 = pts[i], p2 = pts[i + 1], p3 = pts[i + 2 < m ? i + 2 : m - 1];
        for (int s = 0; s < 8; s++) {
            float t = s / 8.f, t2 = t * t, t3 = t2 * t;
            V2 q = (p1 * 2 + (p2 - p0) * t + (p0 * 2 - p1 * 5 + p2 * 4 - p3) * t2 + (p1 * 3 - p0 - p2 * 3 + p3) * t3) * 0.5f;
            dense.push_back(q);
        }
    }
    dense.push_back(pts.back());
    std::vector<float> acc(dense.size(), 0);
    for (size_t i = 1; i < dense.size(); i++) acc[i] = acc[i - 1] + (dense[i] - dense[i - 1]).len();
    std::vector<V2> out;
    float total = acc.back();
    size_t j = 0;
    for (int i = 0; i < n; i++) {
        float d = total * i / (n - 1);
        while (j + 1 < dense.size() && acc[j + 1] < d) j++;
        if (j + 1 >= dense.size()) { out.push_back(dense.back()); continue; }
        float seg = acc[j + 1] - acc[j];
        float t = seg > 1e-5f ? (d - acc[j]) / seg : 0;
        out.push_back(dense[j] + (dense[j + 1] - dense[j]) * t);
    }
    return out;
}

bool Rig::load(Gfx& g, const std::wstring& dir, float scale, const wchar_t* file) {
    ok = false;
    std::string txt;
    if (!readFile(dir + L"\\" + file, txt)) {
        logf("no %s in %s", narrow(file).c_str(), narrow(dir).c_str());
        return false;
    }
    bool parsed = false;
    Json j = Json::parse(txt, &parsed);
    if (!parsed) logf("%s: parse error, trying anyway", narrow(file).c_str());
    k = j["artScale"].f(0.5f) * scale;
    rootAngle = j["rootAngle"].f(0);
    auto src = g.loadImage(dir + L"\\" + widen(j["atlas"].str("atlas.png")));
    if (!src) return false;
    float kx = k, ky = k;
    atlas.src = g.scaled(src.Get(), k, &kx, &ky);
    atlas.reset();
    UINT aw = 0, ah = 0;
    std::vector<uint8_t> px;
    {
        atlas.src->GetSize(&aw, &ah);
        px.assign((size_t)aw * ah * 4, 0);
        alpha.clear();
        if (SUCCEEDED(atlas.src->CopyPixels(nullptr, aw * 4, (UINT)px.size(), px.data()))) {
            alpha.resize((size_t)aw * ah);
            for (size_t i = 0; i < alpha.size(); i++) alpha[i] = px[i * 4 + 3];
            alphaW = (int)aw;
            alphaH = (int)ah;
        }
    }

    parts.clear();
    const Json& P = j["parts"];
    for (size_t i = 0; i < P.size(); i++) {
        const Json& p = P[i];
        PartDef d;
        d.name = p["name"].str();
        const Json& r = p["rect"];
        d.src = D2D1::RectF(r[0].f() * kx, r[1].f() * ky, (r[0].f() + r[2].f()) * kx, (r[1].f() + r[3].f()) * ky);
        d.size = V2(r[2].f() * k, r[3].f() * k);
        d.pos = jv(p["pos"], k);
        d.joint = jv(p["joint"], k);
        if (p["tip"].size() == 2) {
            d.hasTip = true;
            d.tip = jv(p["tip"], k);
        }
        const Json& fx = p["fix"];
        for (size_t f = 0; f < fx.size(); f++)
            if (fx[f].size() == 3) d.fix.push_back({fx[f][0].f(), V2(fx[f][1].f() * k, fx[f][2].f() * k)});
        if (!d.fix.empty()) {
            bool zero = false;
            for (auto& q : d.fix) zero |= fabsf(q.ang) < 1e-4f;
            if (!zero) d.fix.push_back({0.f, V2()});
            std::sort(d.fix.begin(), d.fix.end(), [](const PartDef::FixKey& a, const PartDef::FixKey& b) { return a.ang < b.ang; });
        }
        d.z = p["z"].f();
        d.overParent = p["overParent"].flag(false);
        if (p.has("spring")) {
            const Json& s = p["spring"];
            d.spring.on = true;
            d.spring.k = s["k"].f(300);
            d.spring.zeta = s["zeta"].f(0.5f);
            d.spring.flop = s["flop"].f(0.3f);
            d.spring.limb = s["limb"].flag(false);
        }
        parts.push_back(d);
    }
    for (size_t i = 0; i < P.size(); i++) parts[i].parent = find(P[i]["parent"].str().c_str());
    for (size_t i = 0; i < parts.size(); i++) {
        int pi = parts[i].parent;
        if (!parts[i].overParent || pi < 0 || px.empty()) continue;
        PartDef& d = parts[pi];
        d.overlay = (int)i;
        const int pad = 2;
        int x0 = (int)floorf(d.src.left), y0 = (int)floorf(d.src.top), x1 = (int)ceilf(d.src.right), y1 = (int)ceilf(d.src.bottom);
        d.maskW = x1 - x0 + pad * 2;
        d.maskH = y1 - y0 + pad * 2;
        d.maskPx.assign((size_t)d.maskW * d.maskH * 4, 0);
        for (int y = std::max(y0, 0); y < std::min(y1, (int)ah); y++)
            for (int x = std::max(x0, 0); x < std::min(x1, (int)aw); x++) {
                uint8_t a = px[((size_t)y * aw + x) * 4 + 3];
                uint8_t* o = &d.maskPx[((size_t)(y - y0 + pad) * d.maskW + (x - x0 + pad)) * 4];
                o[0] = o[1] = o[2] = o[3] = a;
            }
        float sx = d.size.x / std::max(1e-3f, d.src.right - d.src.left), sy = d.size.y / std::max(1e-3f, d.src.bottom - d.src.top);
        d.maskXf = M3::Translation((float)(x0 - pad) - d.src.left, (float)(y0 - pad) - d.src.top) * M3::Scale(sx, sy) *
                   M3::Translation(d.pos.x, d.pos.y);
    }

    rigBuildHitPoints(*this);
    updateOrder.clear();
    std::vector<bool> done(parts.size(), false);
    for (size_t pass = 0; pass < parts.size(); pass++)
        for (size_t i = 0; i < parts.size(); i++)
            if (!done[i] && (parts[i].parent < 0 || done[parts[i].parent])) {
                done[i] = true;
                updateOrder.push_back((int)i);
            }

    center = jv(j["center"], k);
    headCenter = jv(j["headCenter"], k);
    headRadius = j["headRadius"].f(80) * k;
    hull.clear();
    const Json& Hl = j["hull"];
    for (size_t i = 0; i < Hl.size(); i++) hull.push_back(jv(Hl[i], k));
    outlineW = j["outline"].f(4) * k;
    top = 0;
    for (auto& p : parts) top = std::min(top, p.pos.y);

    const Json& F = j["face"];
    face = FaceDef{};
    if (F.t == Json::Obj) {
        face.part = find(F["part"].str("head").c_str());
        face.on = face.part >= 0;
        face.center = jv(F["center"], k);
        face.size = jv(F["size"], k);
        face.radius = F["radius"].f(20) * k;
        face.angle = F["angle"].f(0);
        face.color = parseColor(F["color"].str("#D61AEE"));
        const Json& Ov = F["overlay"];
        if (Ov.t == Json::Obj) {
            face.ovAngle = Ov["angle"].f(0) * PI / 180.f;
            face.ovOff = V2(Ov["dx"].f(0), Ov["dy"].f(0)) * k;
            face.ovText = Ov["text"].f(1);
        }
        const Json& Pl = F["poly"];
        for (size_t i = 0; i < Pl.size(); i++) {
            V2 p = jv(Pl[i], k);
            if (face.angle != 0) p = face.center + rotv(p - face.center, -face.angle);
            face.poly.push_back(p);
        }
        if (face.poly.size() >= 3) {
            face.polyMin = face.polyMax = face.poly[0];
            for (auto& p : face.poly) {
                face.polyMin = V2(std::min(face.polyMin.x, p.x), std::min(face.polyMin.y, p.y));
                face.polyMax = V2(std::max(face.polyMax.x, p.x), std::max(face.polyMax.y, p.y));
            }
        } else {
            face.poly.clear();
        }
    }

    const Json& Mo = j["mouth"];
    mouth = MouthDef{};
    if (Mo.t == Json::Obj) {
        mouth.part = find(Mo["part"].str("head").c_str());
        mouth.on = mouth.part >= 0;
        mouth.center = jv(Mo["center"], k);
        mouth.size = jv(Mo["size"], k);
        mouth.angle = Mo["angle"].f(0);
        const Json& Pl = Mo["poly"];
        for (size_t i = 0; i < Pl.size(); i++) mouth.poly.push_back(jv(Pl[i], k));
        if (mouth.poly.size() < 3) mouth.poly.clear();
    }

    const Json& T = j["tail"];
    tail = TailDef{};
    if (T.t == Json::Obj && T["enabled"].flag(true)) {
        tail.parent = find(T["parent"].str("body").c_str());
        tail.on = tail.parent >= 0;
        tail.z = T["z"].f(5);
        std::vector<V2> rest;
        for (size_t i = 0; i < T["rest"].size(); i++) rest.push_back(jv(T["rest"][i], k));
        tail.rest = resample(rest, 14);
        tail.w0 = T["width"][0].f(10) * k;
        tail.w1 = T["width"][1].f(5) * k;
        tail.color = parseColor(T["color"].str("#1E1A26"));
        tail.outline = parseColor(T["outline"].str("#181222"));
        tail.tip = parseColor(T["tipColor"].str("#D61AEE"));
        if (tail.rest.size() < 2) tail.on = false;
    }

    drawOrder.clear();
    for (size_t i = 0; i < parts.size(); i++) drawOrder.push_back({0, (int)i, parts[i].z});
    if (face.on) drawOrder.push_back({1, face.part, parts[face.part].z + 0.5f});
    if (tail.on) drawOrder.push_back({2, 0, tail.z});
    if (mouth.on) drawOrder.push_back({3, mouth.part, parts[mouth.part].z + 0.4f});
    std::stable_sort(drawOrder.begin(), drawOrder.end(), [](const DrawItem& a, const DrawItem& b) { return a.z < b.z; });
    logf("rig %s: %zu parts, k=%.3f, face=%d tail=%d", narrow(file).c_str(), parts.size(), k, face.on, tail.on);
    ok = !parts.empty();
    return ok;
}

int Rig::find(const char* n) const {
    if (!n || !*n) return -1;
    for (size_t i = 0; i < parts.size(); i++)
        if (parts[i].name == n) return (int)i;
    return -1;
}

void RigInstance::bind(const Rig* r) {
    rig = r;
    pose.assign(r->parts.size(), PartPose{});
    world.assign(r->parts.size(), M3::Identity());
    sim.assign(r->parts.size(), Sim{});
    tailInit = false;
}

void RigInstance::resetPose() {
    for (auto& p : pose) p = PartPose{};
}

void RigInstance::resetSim() {
    for (auto& s : sim) s = Sim{};
    tailInit = false;
}

V2 PartDef::fixAt(float ang) const {
    if (fix.empty()) return {};
    if (ang <= fix.front().ang) return fix.front().d;
    for (size_t i = 1; i < fix.size(); i++)
        if (ang <= fix[i].ang) {
            float t = (ang - fix[i - 1].ang) / std::max(1e-4f, fix[i].ang - fix[i - 1].ang);
            return fix[i - 1].d + (fix[i].d - fix[i - 1].d) * t;
        }
    return fix.back().d;
}

void RigInstance::solvePin(const M3& pm, V2 wo) {
    const auto& P = rig->parts;
    const PartDef& rd = P[pinRoot];
    PartPose& rp = pose[pinRoot];
    M3 inv = pm;
    if (!inv.Invert()) return;
    V2 T = xf(inv, pinTarget - wo);
    if (pinPart == pinRoot) {
        for (int it = 0; it < 2; it++) {
            V2 v0 = pinLocal - rd.joint, v = T - rp.off - rd.fixAt(rp.rot) - rd.joint;
            if (v.len() < 1e-3f || v0.len() < 1e-3f) return;
            rp.rot = atan2f(cross(v0, v), dot(v0, v));
        }
        return;
    }
    const PartDef& cd = P[pinPart];
    PartPose& cp = pose[pinPart];
    float pref = cp.rot;
    for (int it = 0; it < 2; it++) {
        V2 offR = rp.off + rd.fixAt(rp.rot), offC = cp.off + cd.fixAt(cp.rot);
        V2 u0 = cd.joint + offC - rd.joint, p0 = pinLocal - cd.joint;
        float L1 = u0.len(), L2 = p0.len();
        V2 v = T - offR - rd.joint;
        float dist = v.len();
        if (L1 < 1e-3f || L2 < 1e-3f || dist < 1e-3f) return;
        dist = clampf(dist, fabsf(L1 - L2) + 1e-3f, (L1 + L2) * 0.9995f);
        V2 vt = v.norm() * dist;
        float base = atan2f(v.y, v.x);
        float A = acosf(clampf((L1 * L1 + dist * dist - L2 * L2) / (2.f * L1 * dist), -1.f, 1.f));
        float best = 1e9f, a1b = rp.rot, a2b = cp.rot;
        int sb = pinSign;
        for (int sg = -1; sg <= 1; sg += 2) {
            V2 u(cosf(base + sg * A), sinf(base + sg * A));
            float a1 = atan2f(cross(u0, u), dot(u0, u));
            V2 w = vt - u * L1, p0r = rotv(p0, a1);
            float a2 = atan2f(cross(p0r, w), dot(p0r, w));
            float dd = remainderf(a2 - pref, 2.f * PI);
            float cost = fabsf(dd) + (sg == pinSign ? 0.f : 0.5f);
            if (cost < best) { best = cost; a1b = a1; a2b = a2; sb = sg; }
        }
        pinSign = sb;
        rp.rot = a1b;
        cp.rot = a2b;
    }
}

void RigInstance::step(float dt, const M3& root, V2 wo, float rootRot, int facing, float gravity, float loose) {
    const auto& P = rig->parts;
    size_t n = P.size();
    V2 gl = rotv(V2(0, gravity), -rootRot);
    gl.x *= (float)facing;
    V2 gRel = gl - V2(0, gravity);
    std::vector<float> acc(n, 0.f);
    for (int i : rig->updateOrder) {
        const PartDef& d = P[i];
        PartPose& ps = pose[i];
        Sim& s = sim[i];
        const M3& pm = d.parent >= 0 ? world[d.parent] : root;
        float parentAng = d.parent >= 0 ? acc[d.parent] : 0.f;
        bool pinned = pinPart >= 0 && (i == pinRoot || i == pinPart);
        if (pinPart >= 0 && i == pinRoot) solvePin(pm, wo);
        if (pinned) s.frames = 0;
        if (d.spring.on && dt > 0 && !pinned) {
            V2 js = xf(pm, d.joint + ps.off) + wo;
            float pa = parentAng + rootRot * facing;
            if (s.frames == 0) {
                s.prevJ = js;
                s.prevVel = {};
                s.prevPA = pa;
                s.prevPAV = 0;
            }
            V2 vel = (js - s.prevJ) / dt;
            V2 a = (vel - s.prevVel) / dt;
            float pav = (pa - s.prevPA) / dt;
            float paa = (pav - s.prevPAV) / dt;
            if (s.frames < 2) { a = {}; paa = 0; }
            s.prevJ = js;
            s.prevVel = vel;
            s.prevPA = pa;
            s.prevPAV = pav;
            float al = a.len();
            const float maxA = 40000.f;
            if (al > maxA) a = a * (maxA / al);
            paa = clampf(paa, -400.f, 400.f);
            V2 a_loc = rotv(a, -rootRot);
            a_loc.x *= (float)facing;
            V2 c = d.pos + d.size * 0.5f - d.joint;
            V2 r = rotv(c, parentAng + ps.rot + s.a);
            float r2 = std::max(dot(r, r), 25.f);
            V2 f = gRel - a_loc;
            float kk = d.spring.k, zz = d.spring.zeta, ff = d.spring.flop;
            if (d.spring.limb) {
                float l = clampf(loose, 0.f, 1.f);
                kk *= lerpf(4.5f, 1.f, l);
                zz *= lerpf(1.6f, 1.f, l);
                ff *= lerpf(0.25f, 1.f, l);
            }
            float torque = ff * (cross(r, f) / r2 - paa * 0.6f);
            float w0 = sqrtf(kk);
            float h = dt * 0.5f;
            for (int it = 0; it < 2; it++) {
                float accA = torque - kk * s.a - 2.f * zz * w0 * s.v;
                s.v += accA * h;
                s.a += s.v * h;
            }
            if (s.a > 1.4f) { s.a = 1.4f; s.v = std::min(s.v, 0.f); }
            if (s.a < -1.4f) { s.a = -1.4f; s.v = std::max(s.v, 0.f); }
            s.frames++;
        } else {
            s.a = 0;
            s.v = 0;
        }
        float ang = ps.rot + s.a;
        acc[i] = parentAng + ang;
        V2 off = ps.off + d.fixAt(ang);
        M3 local = M3::Scale(ps.scl.x, ps.scl.y, d.joint.d2()) * M3::Rotation(ang * 180.f / PI, d.joint.d2()) *
                   M3::Translation(off.x, off.y);
        world[i] = local * pm;
    }

    const TailDef& td = rig->tail;
    if (td.on && dt > 0) {
        const M3& pm = world[td.parent];
        size_t N = td.rest.size();
        std::vector<V2> tgt(N);
        for (size_t i = 0; i < N; i++) tgt[i] = xf(pm, td.rest[i]) + wo;
        if (!tailInit || tp.size() != N || (tp[0] - tgt[0]).len() > 300.f) {
            tp = tgt;
            tprev = tgt;
            tlen.assign(N, 0.f);
            for (size_t i = 1; i < N; i++) tlen[i] = (td.rest[i] - td.rest[i - 1]).len();
            tailInit = true;
        }
        for (size_t i = 1; i < N; i++) {
            V2 v = (tp[i] - tprev[i]) * 0.9f;
            tprev[i] = tp[i];
            tp[i] += v + V2(0, gravity * 0.3f * dt * dt);
        }
        tp[0] = tprev[0] = tgt[0];
        for (int it = 0; it < 4; it++) {
            for (size_t i = 1; i < N; i++) {
                float st = lerpf(0.30f, 0.045f, (float)i / (N - 1));
                tp[i] += (tgt[i] - tp[i]) * st;
            }
            for (size_t i = 1; i < N; i++) {
                V2 d = tp[i] - tp[i - 1];
                float L = d.len();
                if (L > 1e-4f) tp[i] = tp[i - 1] + d * (tlen[i] / L);
            }
        }
    }
}

int RigInstance::hitTest(V2 p) const {
    const Rig& R = *rig;
    if (R.alpha.empty()) return -1;
    int best = -1;
    float bestZ = -1e9f;
    for (size_t i = 0; i < R.parts.size(); i++) {
        const PartDef& d = R.parts[i];
        float z = d.z + pose[i].zOff;
        if (z <= bestZ) continue;
        D2D1::Matrix3x2F inv = world[i];
        if (!inv.Invert()) continue;
        V2 l = xf(inv, p);
        if (l.x < d.pos.x || l.y < d.pos.y || l.x >= d.pos.x + d.size.x || l.y >= d.pos.y + d.size.y) continue;
        int ax = (int)(d.src.left + (l.x - d.pos.x) / d.size.x * (d.src.right - d.src.left));
        int ay = (int)(d.src.top + (l.y - d.pos.y) / d.size.y * (d.src.bottom - d.src.top));
        if (ax < 0 || ay < 0 || ax >= R.alphaW || ay >= R.alphaH) continue;
        if (R.alpha[(size_t)ay * R.alphaW + ax] < 40) continue;
        best = (int)i;
        bestZ = z;
    }
    return best;
}

V2 RigInstance::partPoint(int part, V2 assembled) const {
    if (part < 0 || part >= (int)world.size()) return assembled;
    return xf(world[part], assembled);
}

ComPtr<ID2D1PathGeometry> makePath(ID2D1Factory* f, const std::function<void(ID2D1GeometrySink*)>& fn) {
    ComPtr<ID2D1PathGeometry> g;
    f->CreatePathGeometry(&g);
    ComPtr<ID2D1GeometrySink> s;
    g->Open(&s);
    fn(s.Get());
    s->Close();
    return g;
}

ComPtr<ID2D1PathGeometry> heartGeometry(ID2D1Factory* f, V2 c, float sz) {
    auto P = [&](float x, float y) { return D2D1::Point2F(c.x + x * sz, c.y + y * sz); };
    return makePath(f, [&](ID2D1GeometrySink* s) {
        s->BeginFigure(P(0, 0.36f), D2D1_FIGURE_BEGIN_FILLED);
        s->AddBezier(D2D1::BezierSegment(P(-0.08f, 0.28f), P(-0.52f, 0.06f), P(-0.52f, -0.17f)));
        s->AddBezier(D2D1::BezierSegment(P(-0.52f, -0.42f), P(-0.18f, -0.52f), P(0, -0.27f)));
        s->AddBezier(D2D1::BezierSegment(P(0.18f, -0.52f), P(0.52f, -0.42f), P(0.52f, -0.17f)));
        s->AddBezier(D2D1::BezierSegment(P(0.52f, 0.06f), P(0.08f, 0.28f), P(0, 0.36f)));
        s->EndFigure(D2D1_FIGURE_END_CLOSED);
    });
}

void drawLips(DrawCtx& c, V2 o, float sz, float op, bool flat) {
    auto P = [&](float x, float y) { return D2D1::Point2F(o.x + x * sz, o.y + y * sz); };
    auto seam = [&](ID2D1GeometrySink* s, bool back) {
        if (back) {
            s->AddBezier(D2D1::BezierSegment(P(0.26f, 0.05f), P(0.1f, 0.09f), P(0, 0.07f)));
            s->AddBezier(D2D1::BezierSegment(P(-0.1f, 0.09f), P(-0.26f, 0.05f), P(-0.5f, 0.02f)));
        } else {
            s->AddBezier(D2D1::BezierSegment(P(-0.26f, 0.05f), P(-0.1f, 0.09f), P(0, 0.07f)));
            s->AddBezier(D2D1::BezierSegment(P(0.1f, 0.09f), P(0.26f, 0.05f), P(0.5f, 0.02f)));
        }
    };
    auto upper = makePath(c.f, [&](ID2D1GeometrySink* s) {
        s->BeginFigure(P(-0.5f, 0.02f), D2D1_FIGURE_BEGIN_FILLED);
        s->AddBezier(D2D1::BezierSegment(P(-0.4f, -0.13f), P(-0.31f, -0.3f), P(-0.16f, -0.3f)));
        s->AddBezier(D2D1::BezierSegment(P(-0.07f, -0.3f), P(-0.03f, -0.21f), P(0, -0.16f)));
        s->AddBezier(D2D1::BezierSegment(P(0.03f, -0.21f), P(0.07f, -0.3f), P(0.16f, -0.3f)));
        s->AddBezier(D2D1::BezierSegment(P(0.31f, -0.3f), P(0.4f, -0.13f), P(0.5f, 0.02f)));
        seam(s, true);
        s->EndFigure(D2D1_FIGURE_END_CLOSED);
    });
    auto lower = makePath(c.f, [&](ID2D1GeometrySink* s) {
        s->BeginFigure(P(-0.5f, 0.02f), D2D1_FIGURE_BEGIN_FILLED);
        seam(s, false);
        s->AddBezier(D2D1::BezierSegment(P(0.42f, 0.3f), P(0.2f, 0.4f), P(0, 0.4f)));
        s->AddBezier(D2D1::BezierSegment(P(-0.2f, 0.4f), P(-0.42f, 0.3f), P(-0.5f, 0.02f)));
        s->EndFigure(D2D1_FIGURE_END_CLOSED);
    });
    D2D1_COLOR_F ink = D2D1::ColorF(0.16f, 0.04f, 0.14f, 1);
    float w = std::max(1.2f, sz * 0.075f);
    c.color(ink, op);
    c.rt->DrawGeometry(upper.Get(), c.br, w, c.round);
    c.rt->DrawGeometry(lower.Get(), c.br, w, c.round);
    c.color(flat ? D2D1::ColorF(1, 1, 1, 1) : D2D1::ColorF(0.86f, 0.1f, 0.5f, 1), flat ? 0.78f * op : op);
    c.rt->FillGeometry(upper.Get(), c.br);
    c.color(flat ? D2D1::ColorF(1, 1, 1, 1) : D2D1::ColorF(1.f, 0.3f, 0.64f, 1), flat ? 0.78f * op : op);
    c.rt->FillGeometry(lower.Get(), c.br);
    auto line = makePath(c.f, [&](ID2D1GeometrySink* s) {
        s->BeginFigure(P(-0.44f, 0.03f), D2D1_FIGURE_BEGIN_HOLLOW);
        s->AddBezier(D2D1::BezierSegment(P(-0.24f, 0.06f), P(-0.1f, 0.09f), P(0, 0.07f)));
        s->AddBezier(D2D1::BezierSegment(P(0.1f, 0.09f), P(0.24f, 0.06f), P(0.44f, 0.03f)));
        s->EndFigure(D2D1_FIGURE_END_OPEN);
    });
    c.color(ink, op);
    c.rt->DrawGeometry(line.Get(), c.br, std::max(1.f, sz * 0.06f), c.round);
    if (flat) return;
    c.color(D2D1::ColorF(1, 1, 1, 1), 0.8f * op);
    c.rt->FillEllipse(D2D1::Ellipse(P(0.17f, 0.22f), sz * 0.1f, sz * 0.045f), c.br);
    c.rt->FillEllipse(D2D1::Ellipse(P(-0.22f, -0.16f), sz * 0.055f, sz * 0.03f), c.br);
}

int g_dbgSkip = 0;

void RigInstance::draw(DrawCtx& c, const FaceState& fs, V2 wo) {
    ID2D1Bitmap* bmp = rig->atlas.get(c);
    std::vector<std::pair<float, const Rig::DrawItem*>> order;
    for (auto& it : rig->drawOrder) order.push_back({it.z + (it.kind == 2 ? 0.f : pose[it.index].zOff), &it});
    std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& oz : order) {
        const Rig::DrawItem& it = *oz.second;
        if (g_dbgSkip & (1 << it.kind)) continue;
        if (it.kind == 0) {
            if (!bmp) continue;
            const PartDef& d = rig->parts[it.index];
            c.rt->SetTransform(world[it.index]);
            c.rt->DrawBitmap(bmp, D2D1::RectF(d.pos.x, d.pos.y, d.pos.x + d.size.x, d.pos.y + d.size.y), 1.f,
                             D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, d.src);
            if (d.overlay >= 0 && rig->parts[d.overlay].z + pose[d.overlay].zOff < oz.first) drawOverlay(c, bmp, it.index, d.overlay);
        } else if (it.kind == 1) {
            c.rt->SetTransform(world[rig->face.part]);
            drawFaceCached(c, fs);
        } else if (it.kind == 3) {
            c.rt->SetTransform(world[rig->mouth.part]);
            drawMouth(c, rig->mouth, fs);
        } else {
            drawTail(c, wo);
        }
    }
    c.rt->SetTransform(M3::Identity());
}

void RigInstance::drawOverlay(DrawCtx& c, ID2D1Bitmap* atlas, int pi, int ci) {
    const PartDef& d = rig->parts[pi];
    if (d.maskPx.empty()) return;
    if (maskOwner != c.rt || maskGen != c.gen) {
        masks.clear();
        layer.Reset();
        maskOwner = c.rt;
        maskGen = c.gen;
    }
    if (masks.size() != rig->parts.size()) masks.resize(rig->parts.size());
    MaskRes& m = masks[pi];
    if (!m.brush) {
        auto bp = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(c.rt->CreateBitmap(D2D1::SizeU(d.maskW, d.maskH), d.maskPx.data(), d.maskW * 4, bp, &m.bmp))) return;
        auto bb = D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        if (FAILED(c.rt->CreateBitmapBrush(m.bmp.Get(), bb, &m.brush))) return;
        m.brush->SetTransform(d.maskXf);
    }
    if (!layer && FAILED(c.rt->CreateLayer(nullptr, &layer))) return;
    c.rt->SetTransform(world[pi]);
    auto bounds = D2D1::RectF(d.pos.x - 2, d.pos.y - 2, d.pos.x + d.size.x + 2, d.pos.y + d.size.y + 2);
    c.rt->PushLayer(D2D1::LayerParameters(bounds, nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), 1.f, m.brush.Get()),
                    layer.Get());
    const PartDef& cd = rig->parts[ci];
    c.rt->SetTransform(world[ci]);
    c.rt->DrawBitmap(atlas, D2D1::RectF(cd.pos.x, cd.pos.y, cd.pos.x + cd.size.x, cd.pos.y + cd.size.y), 1.f,
                     D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, cd.src);
    c.rt->PopLayer();
}

void RigInstance::drawFaceCached(DrawCtx& c, const FaceState& fs0) {
    const FaceDef& fd = rig->face;
    D2D1::Matrix3x2F mw;
    c.rt->GetTransform(&mw);
    FaceState fs = fs0;
    fs.mirror = mw._11 * mw._22 - mw._12 * mw._21 < 0;
    float w = fd.size.x, h = fd.size.y;
    V2 tl(fd.center.x - w / 2 - 1, fd.center.y - h / 2 - 1);
    if (!fd.poly.empty()) {
        tl = fd.polyMin - V2(1, 1);
        w = fd.polyMax.x - fd.polyMin.x;
        h = fd.polyMax.y - fd.polyMin.y;
    }
    UINT cw = (UINT)ceilf(w + 2), ch = (UINT)ceilf(h + 2);
    if (!faceRT || faceOwner != c.rt || faceGen != c.gen) {
        faceRT.Reset();
        if (FAILED(c.rt->CreateCompatibleRenderTarget(D2D1::SizeF((float)cw, (float)ch), &faceRT))) {
            drawFace(c, fd, fs);
            return;
        }
        faceOwner = c.rt;
        faceGen = c.gen;
        faceKey.clear();
    }
    auto q = [](float v, float step) { return (int)lroundf(v / step); };
    bool animated = fs.heart > 0.01f || fs.dizzy > 0.01f;
    int tq = animated ? q(fs.time, 1.f / 30.f) : ((fs.sleep > 0.01f || fs.fatal > 0.01f) ? q(fs.time, 0.1f) : 0);
    std::vector<int> key = {q(fs.open, 0.04f), q(fs.happy, 0.04f), q(fs.squint, 0.04f), q(fs.angry, 0.04f), q(fs.sad, 0.04f),
                            q(fs.surprise, 0.04f), q(fs.blush, 0.04f), q(fs.sleep, 0.04f), q(fs.heart, 0.04f), q(fs.dizzy, 0.04f),
                            q(fs.cross, 0.04f), q(fs.look.x, 0.08f), q(fs.look.y, 0.08f), q(fs.fatal, 0.04f), q(fs.hungry, 0.04f), q(fs.tired, 0.04f),
                            q(fs.solver, 0.04f), q(fs.sigil, 0.04f), (int)fs.mirror, tq};
    if (key != faceKey) {
        faceKey = key;
        faceRedraws++;
        faceRT->BeginDraw();
        faceRT->Clear(D2D1::ColorF(0, 0, 0, 0));
        faceRT->SetTransform(D2D1::Matrix3x2F::Translation(-tl.x, -tl.y));
        DrawCtx fc = c;
        fc.rt = faceRT.Get();
        drawFace(fc, fd, fs);
        faceRT->EndDraw();
    }
    ComPtr<ID2D1Bitmap> bmp;
    faceRT->GetBitmap(&bmp);
    if (fd.angle != 0) {
        D2D1::Matrix3x2F m;
        c.rt->GetTransform(&m);
        c.rt->SetTransform(M3::Rotation(fd.angle * 180.f / PI, fd.center.d2()) * m);
    }
    D2D1_RECT_F dst = D2D1::RectF(tl.x, tl.y, tl.x + cw, tl.y + ch);
    if (fs.solver > 0.01f) drawFaceGlitch(c, bmp.Get(), dst, fs);
    else c.rt->DrawBitmap(bmp.Get(), dst, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

void RigInstance::drawFaceGlitch(DrawCtx& c, ID2D1Bitmap* bmp, D2D1_RECT_F dst, const FaceState& fs) {
    const FaceDef& fd = rig->face;
    auto hash = [](unsigned x) {
        x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
        return (x & 0xffff) / 65535.f;
    };
    unsigned tick = (unsigned)(fs.time * 24.f);
    float k = clampf(fs.solver, 0.f, 1.f), W = dst.right - dst.left, H = dst.bottom - dst.top;
    ComPtr<ID2D1Geometry> clip;
    if (!fd.poly.empty()) {
        auto g = makePath(c.f, [&](ID2D1GeometrySink* s) {
            s->BeginFigure(fd.poly[0].d2(), D2D1_FIGURE_BEGIN_FILLED);
            for (size_t i = 1; i < fd.poly.size(); i++) s->AddLine(fd.poly[i].d2());
            s->EndFigure(D2D1_FIGURE_END_CLOSED);
        });
        clip = g;
    } else {
        ComPtr<ID2D1RoundedRectangleGeometry> g;
        c.f->CreateRoundedRectangleGeometry(D2D1::RoundedRect(dst, fd.radius, fd.radius), &g);
        clip = g;
    }
    c.rt->PushLayer(D2D1::LayerParameters(dst, clip.Get()), nullptr);
    D2D1_RECT_F src = D2D1::RectF(0, 0, W, H);
    if (hash(tick * 7u + 3u) < 0.4f) {
        float split = W * 0.03f * k * (0.6f + hash(tick * 19u + 5u));
        D2D1_ANTIALIAS_MODE am = c.rt->GetAntialiasMode();
        c.rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        for (int ch = 0; ch < 2; ch++) {
            float dx = ch ? split : -split;
            D2D1_RECT_F d = D2D1::RectF(dst.left + dx, dst.top, dst.right + dx, dst.bottom);
            c.color(ch ? D2D1::ColorF(0.1f, 0.95f, 1.f, 1) : D2D1::ColorF(1.f, 0.15f, 0.25f, 1), 0.4f * k);
            c.rt->FillOpacityMask(bmp, c.br, D2D1_OPACITY_MASK_CONTENT_GRAPHICS, &d, &src);
        }
        c.rt->SetAntialiasMode(am);
    }
    float y = 0;
    for (unsigned i = 0; y < H; i++) {
        float bh = H * (0.08f + 0.2f * hash(tick * 31u + i * 5u + 1u));
        float r = hash(tick * 17u + i * 11u + 2u);
        float dx = r < 0.45f ? 0.f : (hash(tick * 13u + i * 3u + 9u) - 0.5f) * W * 0.16f * k;
        float y1 = std::min(H, y + bh);
        D2D1_RECT_F s = D2D1::RectF(0, y, W, y1);
        D2D1_RECT_F d = D2D1::RectF(dst.left + dx, dst.top + y, dst.right + dx, dst.top + y1);
        c.rt->DrawBitmap(bmp, d, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, s);
        y = y1;
    }
    if (hash(tick * 5u + 77u) < 0.5f * k) {
        float ly = dst.top + H * hash(tick * 9u + 4u);
        c.color(D2D1::ColorF(1.f, 0.9f, 0.45f, 1), 0.45f * k);
        c.rt->FillRectangle(D2D1::RectF(dst.left, ly, dst.right, ly + std::max(1.f, H * 0.025f)), c.br);
    }
    c.rt->PopLayer();
}

void RigInstance::drawTail(DrawCtx& c, V2 wo) {
    const TailDef& td = rig->tail;
    size_t N = tp.size();
    if (N < 2) return;
    std::vector<V2> p(N), nrm(N);
    for (size_t i = 0; i < N; i++) p[i] = tp[i] - wo;
    for (size_t i = 0; i < N; i++) {
        V2 dir = (i == 0 ? p[1] - p[0] : (i == N - 1 ? p[N - 1] - p[N - 2] : p[i + 1] - p[i - 1])).norm();
        nrm[i] = V2(-dir.y, dir.x);
    }
    V2 e = p[N - 1];
    V2 dir = (p[N - 1] - p[N - 2]).norm();
    V2 side(-dir.y, dir.x);
    float ts = td.w0 * 1.15f;
    auto shape = [&](float grow) {
        return makePath(c.f, [&](ID2D1GeometrySink* s) {
            s->BeginFigure((p[0] + nrm[0] * (td.w0 * 0.5f + grow)).d2(), D2D1_FIGURE_BEGIN_FILLED);
            for (size_t i = 1; i < N; i++) s->AddLine((p[i] + nrm[i] * (lerpf(td.w0, td.w1, (float)i / (N - 1)) * 0.5f + grow)).d2());
            for (size_t i = N; i-- > 0;) s->AddLine((p[i] - nrm[i] * (lerpf(td.w0, td.w1, (float)i / (N - 1)) * 0.5f + grow)).d2());
            s->EndFigure(D2D1_FIGURE_END_CLOSED);
        });
    };
    auto tip = [&](float grow) {
        return makePath(c.f, [&](ID2D1GeometrySink* s) {
            s->BeginFigure((e + dir * (ts * 1.5f + grow * 1.6f)).d2(), D2D1_FIGURE_BEGIN_FILLED);
            s->AddLine((e + side * (ts * 0.75f + grow)).d2());
            s->AddLine((e - dir * (ts * 0.35f + grow)).d2());
            s->AddLine((e - side * (ts * 0.75f + grow)).d2());
            s->EndFigure(D2D1_FIGURE_END_CLOSED);
        });
    };
    float o = rig->outlineW;
    c.rt->SetTransform(M3::Identity());
    c.color(td.outline);
    c.rt->FillGeometry(shape(o).Get(), c.br);
    c.rt->FillGeometry(tip(o).Get(), c.br);
    c.color(td.color);
    c.rt->FillGeometry(shape(0).Get(), c.br);
    c.color(td.tip);
    c.rt->FillGeometry(tip(0).Get(), c.br);
}

void drawMouth(DrawCtx& c, const MouthDef& md, const FaceState& fs) {
    float o = clampf(fs.mouth, 0.f, 1.f), sad = clampf(fs.sad, 0.f, 1.f);
    bool closedFrown = o < 0.04f && sad > 0.35f;
    if (o < 0.04f && !closedFrown) return;
    float fy = (sad > 0.5f && fs.teeth < 0.05f) ? -1.f : 1.f;
    float w = md.size.x * (0.45f + 0.55f * o), h = md.size.y * o;
    V2 p = md.center + V2(clampf(fs.mouthX, -1.f, 1.f) * md.size.x * 0.55f, 0);
    auto P = [&](float x, float y) { return D2D1::Point2F(p.x + x, p.y + y * fy + (fy < 0 ? h * 0.15f : 0.f)); };
    ComPtr<ID2D1PathGeometry> chin;
    D2D1::Matrix3x2F m0;
    c.rt->GetTransform(&m0);
    if (!md.poly.empty()) {
        chin = makePath(c.f, [&](ID2D1GeometrySink* k) {
            k->BeginFigure(md.poly[0].d2(), D2D1_FIGURE_BEGIN_FILLED);
            for (size_t i = 1; i < md.poly.size(); i++) k->AddLine(md.poly[i].d2());
            k->EndFigure(D2D1_FIGURE_END_CLOSED);
        });
        c.rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), chin.Get()), nullptr);
    }
    if (md.angle != 0) c.rt->SetTransform(D2D1::Matrix3x2F::Rotation(md.angle * 180.f / PI, md.center.d2()) * m0);
    if (closedFrown) {
        float wa = md.size.x * 0.42f, ha = md.size.y * 0.22f;
        auto arc = makePath(c.f, [&](ID2D1GeometrySink* k) {
            k->BeginFigure(D2D1::Point2F(p.x - wa / 2, p.y + ha * 0.5f), D2D1_FIGURE_BEGIN_HOLLOW);
            k->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(p.x, p.y - ha * 0.7f), D2D1::Point2F(p.x + wa / 2, p.y + ha * 0.5f)));
            k->EndFigure(D2D1_FIGURE_END_OPEN);
        });
        c.color(D2D1::ColorF(0.05f, 0.03f, 0.09f, 1), sad);
        c.rt->DrawGeometry(arc.Get(), c.br, md.size.y * 0.09f + 0.8f, c.round);
        if (md.angle != 0) c.rt->SetTransform(m0);
        if (chin) c.rt->PopLayer();
        return;
    }
    auto g = makePath(c.f, [&](ID2D1GeometrySink* k) {
        k->BeginFigure(P(-w / 2, -h * 0.35f), D2D1_FIGURE_BEGIN_FILLED);
        k->AddQuadraticBezier(D2D1::QuadraticBezierSegment(P(0, -h * 0.5f), P(w / 2, -h * 0.35f)));
        k->AddBezier(D2D1::BezierSegment(P(w / 2, h * 0.35f), P(w * 0.25f, h * 0.65f), P(0, h * 0.65f)));
        k->AddBezier(D2D1::BezierSegment(P(-w * 0.25f, h * 0.65f), P(-w / 2, h * 0.35f), P(-w / 2, -h * 0.35f)));
        k->EndFigure(D2D1_FIGURE_END_CLOSED);
    });
    c.color(D2D1::ColorF(0.07f, 0.05f, 0.12f, 1));
    c.rt->FillGeometry(g.Get(), c.br);
    if (o > 0.45f) {
        c.color(D2D1::ColorF(0.86f, 0.36f, 0.56f, 1), clampf((o - 0.45f) * 2.f, 0.f, 1.f));
        c.rt->FillEllipse(D2D1::Ellipse(P(0, h * 0.42f), w * 0.22f, h * 0.17f), c.br);
    }
    float teeth = fy < 0 ? 0.f : std::max(clampf(fs.teeth, 0.f, 1.f), clampf((o - 0.6f) * 1.5f, 0.f, 0.6f));
    if (teeth > 0.05f) {
        ComPtr<ID2D1RectangleGeometry> band;
        c.f->CreateRectangleGeometry(D2D1::RectF(p.x - w, p.y - h, p.x + w, p.y - h * 0.5f + md.size.y * 0.16f), &band);
        auto t = makePath(c.f, [&](ID2D1GeometrySink* k) { g->CombineWithGeometry(band.Get(), D2D1_COMBINE_MODE_INTERSECT, nullptr, k); });
        c.color(D2D1::ColorF(0.97f, 0.96f, 1.f, 1), teeth);
        c.rt->FillGeometry(t.Get(), c.br);
        c.color(D2D1::ColorF(0.7f, 0.68f, 0.78f, 1), teeth * 0.8f);
        for (int i = -1; i <= 1; i += 2)
            c.rt->DrawLine(P(i * w * 0.14f, -h * 0.45f), P(i * w * 0.14f, -h * 0.5f + md.size.y * 0.14f), c.br, std::max(md.size.y * 0.03f, 0.6f));
    }
    c.color(D2D1::ColorF(0.05f, 0.03f, 0.09f, 1));
    c.rt->DrawGeometry(g.Get(), c.br, md.size.y * 0.07f + 0.8f, c.round);
    if (md.angle != 0) c.rt->SetTransform(m0);
    if (chin) c.rt->PopLayer();
}

static D2D1_COLOR_F mixc(D2D1_COLOR_F a, D2D1_COLOR_F b, float t) {
    return {lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t), lerpf(a.a, b.a, t)};
}

static void glowGeo(DrawCtx& c, ID2D1Geometry* g, D2D1_COLOR_F col, float base, float op) {
    const float ws[3] = {base * 3.0f, base * 1.9f, base * 1.1f};
    const float as[3] = {0.07f, 0.11f, 0.17f};
    for (int i = 0; i < 3; i++) {
        c.color(col, as[i] * op);
        c.rt->DrawGeometry(g, c.br, ws[i], c.round);
    }
}

static void neonStroke(DrawCtx& c, ID2D1Geometry* g, D2D1_COLOR_F col, float w, float op) {
    if (op <= 0.01f) return;
    glowGeo(c, g, col, w * 1.6f, op);
    c.color(mixc(col, D2D1::ColorF(1, 1, 1, 1), 0.35f), op);
    c.rt->DrawGeometry(g, c.br, w, c.round);
    c.color(mixc(col, D2D1::ColorF(1, 1, 1, 1), 0.85f), op);
    c.rt->DrawGeometry(g, c.br, w * 0.45f, c.round);
}

static void neonFill(DrawCtx& c, ID2D1Geometry* g, D2D1_COLOR_F col, float glowW, float op) {
    if (op <= 0.01f) return;
    glowGeo(c, g, col, glowW, op);
    c.color(mixc(col, D2D1::ColorF(1, 1, 1, 1), 0.4f), op);
    c.rt->FillGeometry(g, c.br);
}

void drawFace(DrawCtx& c, const FaceDef& fd, const FaceState& fs) {
    ID2D1Factory* f = c.f;
    float w = fd.size.x, h = fd.size.y;
    V2 o = fd.center;
    D2D1_ROUNDED_RECT rr{{o.x - w / 2, o.y - h / 2, o.x + w / 2, o.y + h / 2}, fd.radius, fd.radius};
    if (!fd.poly.empty()) {
        auto clip = makePath(f, [&](ID2D1GeometrySink* k) {
            k->BeginFigure(fd.poly[0].d2(), D2D1_FIGURE_BEGIN_FILLED);
            for (size_t i = 1; i < fd.poly.size(); i++) k->AddLine(fd.poly[i].d2());
            k->EndFigure(D2D1_FIGURE_END_CLOSED);
        });
        c.rt->PushLayer(D2D1::LayerParameters(D2D1::RectF(fd.polyMin.x, fd.polyMin.y, fd.polyMax.x, fd.polyMax.y), clip.Get()), nullptr);
    } else {
        ComPtr<ID2D1RoundedRectangleGeometry> clip;
        f->CreateRoundedRectangleGeometry(rr, &clip);
        c.rt->PushLayer(D2D1::LayerParameters(rr.rect, clip.Get()), nullptr);
    }

    const D2D1_COLOR_F gold = D2D1::ColorF(1.f, 0.824f, 0.122f, 1);
    float sv = clampf(fs.solver, 0.f, 1.f), sg = clampf(fs.sigil, 0.f, 1.f);
    D2D1_COLOR_F col = mixc(fd.color, gold, sv);

    c.color(col, 0.06f + 0.05f * fs.blush + 0.03f * sg);
    c.rt->FillEllipse(D2D1::Ellipse(o.d2(), w * 0.45f, h * 0.4f), c.br);

    D2D1_COLOR_F core = mixc(col, D2D1::ColorF(1, 1, 1, 1), 0.5f);
    float special = fs.happy + fs.squint + fs.sleep + fs.heart + fs.dizzy + fs.cross + fs.fatal * 3.f;
    float wn = clampf(1.f - special, 0.f, 1.f) * (1.f - 0.8f * sg);
    float whiten = lerpf(0.72f, 0.3f, sv);
    V2 blushAt[2];
    float blushW = 1, blushH = 1;

    for (int si = 0; si < 2; si++) {
        float s = si ? 1.f : -1.f;
        float sur = fs.surprise;
        V2 e = o + V2(s * w * 0.235f, h * 0.03f) + V2(fs.look.x * w * 0.06f, fs.look.y * h * 0.09f);
        float eh = h * 0.56f * (1 + 0.18f * sur), ew = eh * 0.6f;

        if (wn > 0.01f) {
            float op = clampf(fs.open, 0.f, 1.f);
            float rx = ew * 0.5f, ry = eh * 0.5f;
            float top = e.y - ry;
            float calm = 1.f - sur;
            float tdz = clampf(fs.tired, 0.f, 1.f) * calm;
            float lidBase = std::max(top + (1.f - op) * eh * 0.92f, top + eh * 0.46f * tdz);
            float innerDrop = eh * (0.10f * (1.f - 0.7f * tdz) + 0.40f * fs.angry) * calm;
            float outerDrop = eh * (0.38f * fs.sad + 0.07f * tdz) * calm;
            V2 A(e.x + s * rx, lidBase + outerDrop), B(e.x - s * rx, lidBase + innerDrop);
            V2 dir = (B - A).norm();
            V2 P1 = A - dir * ew, P2 = B + dir * ew;
            float ring = clampf(fs.hungry * 2.f, 0.f, 1.f);
            float strain = std::max(smooth01((fs.angry - 0.35f) / 0.5f), ring);
            if (op < 0.12f) {
                auto g = makePath(f, [&](ID2D1GeometrySink* k) {
                    k->BeginFigure(D2D1::Point2F(e.x - rx * 0.95f, e.y + eh * 0.15f), D2D1_FIGURE_BEGIN_HOLLOW);
                    k->AddLine(D2D1::Point2F(e.x + rx * 0.95f, e.y + eh * 0.15f));
                    k->EndFigure(D2D1_FIGURE_END_OPEN);
                });
                neonStroke(c, g.Get(), col, ew * 0.2f, wn);
            } else {
                ComPtr<ID2D1EllipseGeometry> eye;
                f->CreateEllipseGeometry(D2D1::Ellipse(e.d2(), rx, ry), &eye);
                auto below = makePath(f, [&](ID2D1GeometrySink* k) {
                    k->BeginFigure(P1.d2(), D2D1_FIGURE_BEGIN_FILLED);
                    k->AddLine(P2.d2());
                    k->AddLine((P2 + V2(0, eh * 3)).d2());
                    k->AddLine((P1 + V2(0, eh * 3)).d2());
                    k->EndFigure(D2D1_FIGURE_END_CLOSED);
                });
                auto shape = makePath(f, [&](ID2D1GeometrySink* k) {
                    eye->CombineWithGeometry(below.Get(), D2D1_COMBINE_MODE_INTERSECT, nullptr, k);
                });
                if (strain < 0.99f) {
                    float wf = wn * (1.f - strain) * (1.f - 0.15f * tdz);
                    glowGeo(c, shape.Get(), col, ew * 0.34f, wf);
                    c.color(mixc(col, D2D1::ColorF(1, 1, 1, 1), whiten), wf);
                    c.rt->FillGeometry(shape.Get(), c.br);
                    ComPtr<ID2D1TransformedGeometry> inner;
                    f->CreateTransformedGeometry(shape.Get(), D2D1::Matrix3x2F::Scale(0.6f, 0.7f, D2D1::Point2F(e.x, e.y + eh * 0.04f)), &inner);
                    c.color(D2D1::ColorF(1, 1, 1, 1), 0.75f * wf * (1.f - 0.6f * sv));
                    c.rt->FillGeometry(inner.Get(), c.br);
                }
                float sw = ring > 0.01f ? ew * lerpf(0.26f, 0.07f, clampf(fs.hungry, 0.f, 1.f)) : ew * 0.17f;
                if (strain > 0.01f) neonStroke(c, shape.Get(), col, sw, wn * strain);
                if (tdz > 0.05f) {
                    float midY = (A.y + B.y) * 0.5f, k = s * (A.y - B.y) * 0.5f;
                    float c0 = (midY - e.y) / ry, c1 = k / ry;
                    float qa = 1.f + c1 * c1, qb = c0 * c1, disc = qb * qb - qa * (c0 * c0 - 1.f);
                    if (disc > 0.f) {
                        float sq = sqrtf(disc), u1 = (-qb - sq) / qa, u2 = (-qb + sq) / qa;
                        auto g = makePath(f, [&](ID2D1GeometrySink* kk) {
                            kk->BeginFigure(D2D1::Point2F(e.x + u1 * rx, midY + u1 * k), D2D1_FIGURE_BEGIN_HOLLOW);
                            kk->AddLine(D2D1::Point2F(e.x + u2 * rx, midY + u2 * k));
                            kk->EndFigure(D2D1_FIGURE_END_OPEN);
                        });
                        neonStroke(c, g.Get(), col, ew * 0.13f, wn * tdz * 0.9f);
                    }
                }
            }
        }
        {
            float bw = clampf(wn * (0.8f + 0.2f * clampf(fs.angry + fs.sad + sur, 0.f, 1.f)) + fs.happy * 0.8f, 0.f, 1.f);
            if (bw > 0.01f) {
                float raise = eh * (0.22f * sur + 0.10f * fs.happy - 0.06f * fs.angry);
                float tb = clampf(fs.tired, 0.f, 1.f) * (1.f - sur);
                float by = e.y - eh * 0.72f - raise + (1.f - clampf(fs.open, 0.f, 1.f)) * eh * 0.12f + eh * 0.10f * tb;
                float ang = -s * (0.10f + 0.50f * fs.angry - 0.45f * fs.sad - 0.08f * fs.happy) * (1.f - 0.6f * tb);
                V2 bc(e.x + s * ew * 0.06f, by), dv(cosf(ang), sinf(ang));
                float L = ew * 0.48f;
                auto g = makePath(f, [&](ID2D1GeometrySink* k) {
                    k->BeginFigure((bc - dv * L).d2(), D2D1_FIGURE_BEGIN_HOLLOW);
                    k->AddLine((bc + dv * L).d2());
                    k->EndFigure(D2D1_FIGURE_END_OPEN);
                });
                neonStroke(c, g.Get(), col, ew * 0.15f, bw);
            }
        }
        if (fs.happy > 0.01f) {
            auto g = makePath(f, [&](ID2D1GeometrySink* k) {
                k->BeginFigure(D2D1::Point2F(e.x - ew * 0.62f, e.y + eh * 0.14f), D2D1_FIGURE_BEGIN_HOLLOW);
                k->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(e.x, e.y - eh * 0.62f), D2D1::Point2F(e.x + ew * 0.62f, e.y + eh * 0.14f)));
                k->EndFigure(D2D1_FIGURE_END_OPEN);
            });
            neonStroke(c, g.Get(), col, ew * 0.36f, clampf(fs.happy, 0, 1));
        }
        if (fs.squint > 0.01f) {
            auto g = makePath(f, [&](ID2D1GeometrySink* k) {
                k->BeginFigure(D2D1::Point2F(e.x + s * ew * 0.42f, e.y - eh * 0.30f), D2D1_FIGURE_BEGIN_HOLLOW);
                k->AddLine(D2D1::Point2F(e.x - s * ew * 0.42f, e.y));
                k->AddLine(D2D1::Point2F(e.x + s * ew * 0.42f, e.y + eh * 0.30f));
                k->EndFigure(D2D1_FIGURE_END_OPEN);
            });
            neonStroke(c, g.Get(), col, ew * 0.30f, clampf(fs.squint, 0, 1));
        }
        if (fs.heart > 0.01f) {
            float sz = ew * 1.75f * (1 + 0.09f * sinf(fs.time * 9.f));
            auto g = heartGeometry(f, e, sz);
            neonFill(c, g.Get(), col, ew * 0.3f, clampf(fs.heart, 0, 1));
        }
        if (fs.dizzy > 0.01f) {
            auto g = makePath(f, [&](ID2D1GeometrySink* k) {
                const int steps = 40;
                for (int i = 0; i <= steps; i++) {
                    float th = i * (4.f * PI / steps);
                    float r = ew * 0.06f + th * (ew * 0.55f / (4.f * PI));
                    float a = th * s + fs.time * 7.f * s;
                    D2D1_POINT_2F p{e.x + cosf(a) * r, e.y + sinf(a) * r};
                    if (i == 0) k->BeginFigure(p, D2D1_FIGURE_BEGIN_HOLLOW);
                    else k->AddLine(p);
                }
                k->EndFigure(D2D1_FIGURE_END_OPEN);
            });
            neonStroke(c, g.Get(), col, ew * 0.17f, clampf(fs.dizzy, 0, 1));
        }
        if (fs.cross > 0.01f) {
            auto g = makePath(f, [&](ID2D1GeometrySink* k) {
                k->BeginFigure(D2D1::Point2F(e.x - ew * 0.45f, e.y - eh * 0.3f), D2D1_FIGURE_BEGIN_HOLLOW);
                k->AddLine(D2D1::Point2F(e.x + ew * 0.45f, e.y + eh * 0.3f));
                k->EndFigure(D2D1_FIGURE_END_OPEN);
                k->BeginFigure(D2D1::Point2F(e.x + ew * 0.45f, e.y - eh * 0.3f), D2D1_FIGURE_BEGIN_HOLLOW);
                k->AddLine(D2D1::Point2F(e.x - ew * 0.45f, e.y + eh * 0.3f));
                k->EndFigure(D2D1_FIGURE_END_OPEN);
            });
            neonStroke(c, g.Get(), col, ew * 0.26f, clampf(fs.cross, 0, 1));
        }
        blushAt[si] = e + V2(s * ew * 0.55f, eh * 0.62f);
        blushW = ew;
        blushH = eh;
    }

    if (sg > 0.01f) {
        V2 C = o + V2(0, h * 0.03f);
        float R = h * 0.27f;
        auto at = [&](float deg) {
            float r = deg * PI / 180.f;
            return D2D1::Point2F(C.x + cosf(r) * R, C.y + sinf(r) * R);
        };
        auto g = makePath(f, [&](ID2D1GeometrySink* k) {
            k->BeginFigure(at(-50.f), D2D1_FIGURE_BEGIN_HOLLOW);
            k->AddArc(D2D1::ArcSegment(at(-130.f), D2D1::SizeF(R, R), 0.f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
            k->EndFigure(D2D1_FIGURE_END_OPEN);
            k->BeginFigure(D2D1::Point2F(C.x, C.y - R * 1.32f), D2D1_FIGURE_BEGIN_HOLLOW);
            k->AddLine(D2D1::Point2F(C.x, C.y + R * 1.38f));
            k->EndFigure(D2D1_FIGURE_END_OPEN);
            k->BeginFigure(D2D1::Point2F(C.x - R * 0.32f, C.y + R * 1.24f), D2D1_FIGURE_BEGIN_HOLLOW);
            k->AddLine(D2D1::Point2F(C.x + R * 0.32f, C.y + R * 1.24f));
            k->EndFigure(D2D1_FIGURE_END_OPEN);
            for (int si = -1; si <= 1; si += 2) {
                k->BeginFigure(D2D1::Point2F(C.x + si * R * 1.38f, C.y), D2D1_FIGURE_BEGIN_HOLLOW);
                k->AddLine(D2D1::Point2F(C.x + si * R * 1.78f, C.y));
                k->EndFigure(D2D1_FIGURE_END_OPEN);
                k->BeginFigure(D2D1::Point2F(C.x + si * R * 1.2f, C.y - R * 0.95f), D2D1_FIGURE_BEGIN_HOLLOW);
                k->AddLine(D2D1::Point2F(C.x + si * R * 1.45f, C.y - R * 1.25f));
                k->EndFigure(D2D1_FIGURE_END_OPEN);
            }
        });
        neonStroke(c, g.Get(), gold, h * 0.08f, sg);
        float dm = R * 0.26f;
        V2 top(C.x, C.y - R * 1.32f);
        auto dia = makePath(f, [&](ID2D1GeometrySink* k) {
            k->BeginFigure(D2D1::Point2F(top.x, top.y - dm), D2D1_FIGURE_BEGIN_FILLED);
            k->AddLine(D2D1::Point2F(top.x + dm * 0.7f, top.y));
            k->AddLine(D2D1::Point2F(top.x, top.y + dm));
            k->AddLine(D2D1::Point2F(top.x - dm * 0.7f, top.y));
            k->EndFigure(D2D1_FIGURE_END_CLOSED);
        });
        neonFill(c, dia.Get(), gold, h * 0.03f, sg);
    }

    D2D1::Matrix3x2F m0;
    c.rt->GetTransform(&m0);
    if (fd.ovAngle != 0 || fd.ovOff.x != 0 || fd.ovOff.y != 0)
        c.rt->SetTransform(D2D1::Matrix3x2F::Rotation(fd.ovAngle * 180.f / PI, o.d2()) * D2D1::Matrix3x2F::Translation(fd.ovOff.x, fd.ovOff.y) * m0);

    if (fs.blush > 0.01f) {
        D2D1_COLOR_F pink = col;
        float bl = clampf(fs.blush, 0.f, 1.f), ew = blushW, eh = blushH;
        for (V2 b : blushAt) {
            c.color(pink, 0.28f * bl);
            c.rt->FillEllipse(D2D1::Ellipse(b.d2(), ew * 1.25f, eh * 0.40f), c.br);
            c.color(pink, 0.55f * bl);
            c.rt->FillEllipse(D2D1::Ellipse(b.d2(), ew * 0.95f, eh * 0.27f), c.br);
            c.color(mixc(pink, D2D1::ColorF(1, 1, 1, 1), 0.35f), 1.f * bl);
            for (int i = 0; i < 3; i++) {
                float x = b.x + (i - 1) * ew * 0.42f;
                c.rt->DrawLine(D2D1::Point2F(x - ew * 0.12f, b.y + eh * 0.13f), D2D1::Point2F(x + ew * 0.12f, b.y - eh * 0.13f), c.br,
                               ew * 0.13f, c.round);
            }
        }
    }

    if (fs.mirror && (fs.sleep > 0.01f || fs.fatal > 0.01f)) {
        D2D1::Matrix3x2F mt;
        c.rt->GetTransform(&mt);
        c.rt->SetTransform(D2D1::Matrix3x2F::Scale(-1.f, 1.f, o.d2()) * mt);
    }

    if (fs.sleep > 0.01f) {
        static ComPtr<IDWriteTextFormat> fmt;
        static float fmtSize = 0;
        float size = h * 0.34f * fd.ovText;
        if (!fmt || fabsf(fmtSize - size) > 0.5f) {
            fmt.Reset();
            c.dw->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
                                   L"en-us", &fmt);
            fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            fmtSize = size;
        }
        float a = clampf(fs.sleep, 0, 1) * (0.78f + 0.22f * sinf(fs.time * 2.2f));
        D2D1_RECT_F rc = D2D1::RectF(o.x - w / 2, o.y - h * 0.34f, o.x + w / 2, o.y + h * 0.2f);
        c.color(col, 0.2f * a);
        for (int k = 0; k < 4; k++) {
            float dx = (k & 1) ? 1.6f : -1.6f, dy = (k & 2) ? 1.6f : -1.6f;
            c.rt->DrawText(L"SLEEP MODE", 10, fmt.Get(), D2D1::RectF(rc.left + dx, rc.top + dy, rc.right + dx, rc.bottom + dy), c.br);
        }
        c.color(col, a);
        c.rt->DrawText(L"SLEEP MODE", 10, fmt.Get(), rc, c.br);
    }

    if (fs.fatal > 0.01f) {
        static ComPtr<IDWriteTextFormat> ff;
        static float ffSize = 0;
        float size = h * 0.30f * fd.ovText;
        if (!ff || fabsf(ffSize - size) > 0.5f) {
            ff.Reset();
            c.dw->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
                                   L"en-us", &ff);
            ff->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            ff->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            ffSize = size;
        }
        D2D1_COLOR_F red = D2D1::ColorF(1.f, 0.16f, 0.2f, 1);
        int tick = (int)(fs.time * 10.f);
        bool glitch = (tick % 23) == 0 || (tick % 37) == 0;
        float a = clampf(fs.fatal, 0, 1) * ((tick % 31) == 0 ? 0.35f : 0.95f);
        float gx = glitch ? w * 0.03f : 0.f;
        D2D1_RECT_F rc = D2D1::RectF(o.x - w / 2 + gx, o.y - h * 0.32f, o.x + w / 2 + gx, o.y + h * 0.22f);
        c.color(red, 0.08f * a);
        c.rt->FillEllipse(D2D1::Ellipse(o.d2(), w * 0.45f, h * 0.4f), c.br);
        c.color(red, 0.22f * a);
        for (int k = 0; k < 4; k++) {
            float dx = (k & 1) ? 1.6f : -1.6f, dy = (k & 2) ? 1.6f : -1.6f;
            c.rt->DrawText(L"FATAL ERROR", 11, ff.Get(), D2D1::RectF(rc.left + dx, rc.top + dy, rc.right + dx, rc.bottom + dy), c.br);
        }
        c.color(red, a);
        c.rt->DrawText(L"FATAL ERROR", 11, ff.Get(), rc, c.br);
    }

    c.rt->SetTransform(m0);

    c.color(D2D1::ColorF(0, 0, 0, 1), 0.09f);
    for (float y = o.y - h / 2 + 1.f; y < o.y + h / 2; y += 2.f)
        c.rt->DrawLine(D2D1::Point2F(o.x - w / 2, y), D2D1::Point2F(o.x + w / 2, y), c.br, 0.7f);
    auto shine = makePath(f, [&](ID2D1GeometrySink* k) {
        k->BeginFigure(D2D1::Point2F(o.x - w * 0.40f, o.y - h * 0.40f), D2D1_FIGURE_BEGIN_FILLED);
        k->AddLine(D2D1::Point2F(o.x - w * 0.22f, o.y - h * 0.40f));
        k->AddLine(D2D1::Point2F(o.x - w * 0.36f, o.y - h * 0.02f));
        k->AddLine(D2D1::Point2F(o.x - w * 0.48f, o.y - h * 0.02f));
        k->EndFigure(D2D1_FIGURE_END_CLOSED);
    });
    c.color(D2D1::ColorF(1, 1, 1, 1), 0.07f);
    c.rt->FillGeometry(shine.Get(), c.br);
    c.rt->PopLayer();
}
