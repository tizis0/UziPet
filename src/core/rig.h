#pragma once
#include "gfx.h"
#include "json.h"

struct SpringCfg {
    bool on = false;
    float k = 300, zeta = 0.5f, flop = 0.3f;
    bool limb = false;
};

struct PartDef {
    std::string name;
    int parent = -1;
    float z = 0;
    D2D1_RECT_F src{};
    V2 pos, size, joint;
    SpringCfg spring;
    bool hasTip = false;
    V2 tip;
    struct FixKey { float ang; V2 d; };
    std::vector<FixKey> fix;
    V2 fixAt(float ang) const;
    bool overParent = false;
    int overlay = -1;
    std::vector<uint8_t> maskPx;
    int maskW = 0, maskH = 0;
    D2D1::Matrix3x2F maskXf;
    V2 hand() const { return hasTip ? tip : pos + V2(size.x * 0.5f, size.y * 0.88f); }
    std::vector<V2> hitPts;
};

struct FaceDef {
    bool on = false;
    int part = -1;
    V2 center, size;
    float radius = 0;
    float angle = 0;
    D2D1_COLOR_F color{};
    std::vector<V2> poly;
    V2 polyMin, polyMax;
    float ovAngle = 0;
    V2 ovOff;
    float ovText = 1;
};

struct MouthDef {
    bool on = false;
    int part = -1;
    V2 center, size;
    float angle = 0;
    std::vector<V2> poly;
};

struct TailDef {
    bool on = false;
    int parent = -1;
    float z = 0;
    std::vector<V2> rest;
    float w0 = 10, w1 = 5;
    D2D1_COLOR_F color{}, outline{}, tip{};
};

struct FaceState {
    float open = 1;
    float happy = 0;
    float squint = 0;
    float angry = 0;
    float sad = 0;
    float surprise = 0;
    float blush = 0;
    float sleep = 0;
    float heart = 0;
    float dizzy = 0;
    float cross = 0;
    float mouth = 0;
    float teeth = 0;
    float mouthX = 0;
    float fatal = 0;
    float hungry = 0;
    float tired = 0;
    float solver = 0;
    float sigil = 0;
    V2 look;
    float time = 0;
    bool mirror = false;
};

struct Rig {
    float k = 1;
    std::vector<PartDef> parts;
    std::vector<int> updateOrder;
    struct DrawItem { int kind; int index; float z; };
    std::vector<DrawItem> drawOrder;
    FaceDef face;
    MouthDef mouth;
    TailDef tail;
    V2 center, headCenter;
    float headRadius = 50, top = 0, outlineW = 2;
    std::vector<V2> hull;
    float rootAngle = 0;
    bool ok = false;
    mutable CachedBitmap atlas;
    std::vector<uint8_t> alpha;
    int alphaW = 0, alphaH = 0;

    bool load(Gfx& g, const std::wstring& dir, float scale, const wchar_t* file = L"rig.json");
    int find(const char* n) const;
};

struct PartPose {
    float rot = 0;
    V2 off;
    V2 scl{1, 1};
    float zOff = 0;
};

void rigBuildHitPoints(Rig& r);

struct RigInstance {
    const Rig* rig = nullptr;
    std::vector<PartPose> pose;
    std::vector<D2D1::Matrix3x2F> world;
    struct Sim { float a = 0, v = 0; V2 prevJ, prevVel; float prevPA = 0, prevPAV = 0; int frames = 0; };
    std::vector<Sim> sim;
    std::vector<V2> tp, tprev;
    std::vector<float> tlen;
    bool tailInit = false;
    ComPtr<ID2D1BitmapRenderTarget> faceRT;
    ID2D1RenderTarget* faceOwner = nullptr;
    int faceGen = -1;
    std::vector<int> faceKey;
    int faceRedraws = 0;

    int pinRoot = -1, pinPart = -1, pinSign = 1;
    V2 pinLocal, pinTarget;

    void bind(const Rig* r);
    void resetPose();
    void resetSim();
    void step(float dt, const D2D1::Matrix3x2F& root, V2 winOrigin, float rootRot, int facing, float gravity, float loose = 1.f);
    void draw(DrawCtx& c, const FaceState& face, V2 winOrigin);
    V2 partPoint(int part, V2 assembled) const;
    int hitTest(V2 winPt) const;

private:
    struct MaskRes { ComPtr<ID2D1Bitmap> bmp; ComPtr<ID2D1BitmapBrush> brush; };
    std::vector<MaskRes> masks;
    ComPtr<ID2D1Layer> layer;
    ID2D1RenderTarget* maskOwner = nullptr;
    int maskGen = -1;
    void solvePin(const D2D1::Matrix3x2F& pm, V2 wo);
    void drawOverlay(DrawCtx& c, ID2D1Bitmap* atlas, int parent, int child);
    void drawTail(DrawCtx& c, V2 wo);
    void drawFaceCached(DrawCtx& c, const FaceState& fs);
    void drawFaceGlitch(DrawCtx& c, ID2D1Bitmap* bmp, D2D1_RECT_F dst, const FaceState& fs);
};

void drawFace(DrawCtx& c, const FaceDef& fd, const FaceState& fs);
void drawMouth(DrawCtx& c, const MouthDef& md, const FaceState& fs);
ComPtr<ID2D1PathGeometry> makePath(ID2D1Factory* f, const std::function<void(ID2D1GeometrySink*)>& fn);
ComPtr<ID2D1PathGeometry> heartGeometry(ID2D1Factory* f, V2 c, float size);
void drawLips(DrawCtx& c, V2 at, float width, float op = 1.f, bool flat = false);
