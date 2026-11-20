#pragma once
#include "app.h"
#include "extsetup.h"
#include "filesense.h"
#include "gamesense.h"
#include "robloxsense.h"
#include <ctime>

enum class St {
    Fall, Idle, Walk, Sit, Sleep, PrepJump, Air, Land, Dragged, Dizzy, LookAround, GrabIcon, DropIcon, Wave, Bite, Leap, Cling, Led,
    Watch,
    Stretch,
    Ears,
    Lift,
    Hide,
    Dive,
    Inside,
    Emerge,
    Climb,
    Clap,
    Shoot,
    Ride,
    Fatal,
    Trip,
};

struct Vitals {
    float energy = 100, rel = 50, food = 100;
    float drainH = 3.5f, restoreH = 1.5f;
    bool fatal = false;
    int stock = 0;
    double lastSeen = 0;
    float zeroT = 0;
    bool starved = false;
    bool dead = false, inBin = false;
    float parcelT = 150;
    float parcelLife = 0;
    bool parcelOut = false;
    bool notified = false;
    int intro = 0;
    double babyStart = 0;
    int babyDrops = 0;
    double metAt = 0;
    int annivDone = 0;
    int holidayDay = 0;
    double lastSolver = 0;
    double lastNote = 0;
    double lastSelfArt = 0;
    float trust = 30;
    int cornerState = 0;
    double cornerAskedAt = 0;
    double cornerMoodAt = 0;
    int cornerPhotoDay = 0;
    bool fresh = false;
    bool persist = true;
    bool loadFailed = false;
    float loadFailT = 0;
    std::wstring path() const { return appDataDir() + L"\\state.ini"; }
    void load(int waitMs = 4000);
    void save() const;
};

struct DayLog {
    int day = 0;
    double t0 = 0;
    float minutes = 0;
    int kiss = 0, pet = 0, fed = 0, win = 0, lose = 0, cs = 0, csWin = 0, hug = 0, cold = 0;
    int* field(const std::string& k);
    bool any() const;
};
std::wstring plainLine(const std::wstring& s);
std::wstring timesWords(int n);
bool pngOf(IWICImagingFactory* wic, int w, int h, const uint32_t* px, std::string& out);
int doodleKinds();
float jitterTime(float base, float lo, float hi);
int talksLoad(const std::wstring& path);
int talksBroken();
bool talkIsNightmare(const std::string& id);
std::vector<std::string> talkIds();
enum { MkWalk, MkSit, MkLook, MkJump, MkWave, MkHide, MkTalk };
std::string purpleFolderIco(IWICImagingFactory* wic, std::string* png256 = nullptr, int* purple = nullptr, int* left = nullptr,
                            const std::string* photo = nullptr);
bool sameFullPath(const std::wstring& a, const std::wstring& b);
std::wstring awayWords(double sec);

struct Input {
    V2 cursor;
    bool lmb = false;
    double idleSec = 0;
};

struct Particle {
    int type;
    V2 p, v;
    float life, max, size, rot;
};

struct MiniGame;
struct UziHitbox;

struct Ground {
    bool valid = false;
    SurfKind kind = SurfKind::Floor;
    HWND hwnd = nullptr;
    int index = -1;
    std::wstring iconName;
    RECT last{};
    float y = 0, x0 = 0, x1 = 0, movedT = 0;
};

class Pet {
public:
    explicit Pet(App& a) : app(a) {}
    void bindRig();
    void spawn(V2 at);
    void update(float dt, const Input& in);
    V2 windowOrigin() const;
    void stepRig(float dt, V2 wo);
    void render(DrawCtx& c, V2 wo);
    D2D1_RECT_F bounds(V2 wo) const;

    void beginDrag(V2 cursor);
    void endDrag();
    void poke();
    void fileHover(bool on);
    void receiveFile(const std::wstring& path, int count);
    const char* fileSenseReact();
    void doubleClick();
    void comeHere(V2 cursor);
    void sayCat(const char* cat, float prob = 1.f, const std::wstring& arg = L"", bool force = false);
    void dropCarried(bool place);
    const char* stateName() const;
    bool calm() const;
    bool clickThrough() const { return st == St::Cling; }
    void startBite();
    bool startLeap(V2 cursor);
    void beginCling(V2 cursor);
    void endCling(bool shaken);
    bool annoyedAttack();
    void setState(St s, float dur = 0);
    void setGround(const Surf& s);
    void goWalk(float x, bool off = false);
    void goIdle(float minT = 4.f, float maxT = 9.f);
    void scriptedPaceStep(float speed, bool acrossMonitors);
    float scriptedWalkSpeed = 0;

    void lifeUpdate(float dt, const Input& in);
    bool lifeState(float dt, const Input& in);
    void lifePose(float dt);
    void answer(const std::string& id, int choice);
    bool ask(const char* cat, const char* choicesCat, const std::string& id, const std::wstring& arg = L"");
    void nameChanged();
    void callBack(V2 cursor);
    bool shouldHide(bool fullscreen);
    void diveInto(V2 edgeL, float width, int nestKind, const std::wstring& path);
    void emergeAt(V2 edgeL, float width, int mode);
    void finishNest(int mode);
    bool renderClip(D2D1_RECT_F& r, V2 wo) const;

    float sink = 0;
    V2 edgeL;
    float edgeW = 0;
    enum Nest { NestNone, NestFolder, NestSystem, NestWeb };
    int nest = NestNone;
    std::wstring nestPath;
    float nestT = 0, nestLife = 0;
    int emergeMode = 0;
    HWND emergeWnd = nullptr;
    float lifeT = 0, phaseT = 0;
    int phase = 0;
    int watchMon = -1;
    V2 watchTarget;
    float watchSayT = 0;
    CsVoice csVoice;
    void csEvent(const CsEvent& e);
    void csStep(float dt);
    RbxVoice rbxVoice;
    void rbxEvent(const RbxEvent& e);
    bool rbxLeaveNow();
    double lastAppSay = -1e9, lastShotSay = -1e9, lastMonWish = 0, lastHide = 0, lastNest = 0, lastNestCheck = 0, lastNagCheck = 0;
    double kindSaid[16] = {};
    int liftMon = -1;
    float tLoud = 0;
    V2 tryTarget;
    HWND tryWnd = nullptr;
    bool tryMode = false;
    double tryUntil = 0, lastTry = -1e9;
    float clickDone = 0;
    float tryHandDist = 1e9f;
    bool trySaidRelease = false;
    float tryHoldT = 0, tryPress = -1;
    float reachTo(V2 target, int side = 0);
    float loose = 0.2f;
    std::wstring shootName;
    V2 shootAt;
    double lastShoot = 0;
    float bEars = 0, bStretch = 0, bLift = 0, bClap = 0;
    bool clapOnLand = false;
    std::wstring nestIcon;
    std::wstring bigNote;
    std::string laterCat;
    std::wstring laterArg;
    float laterT = 0;
    void openTalk();
    void talkChoice(int c);
    void sleepTalk();
    void sleepNow();
    bool quiet() const { return app.now < quietUntil; }
    bool tired() const;
    void startQuiet();
    void startStroll(float minutes);
    void strollNext();
    void returnHome();
    void followStep(float dt);
    double quietUntil = 0, strollUntil = 0, nextStrollAsk = 0, strollPauseUntil = 0;
    bool strollOn = false, deepSleep = false, returnPending = false, waveOnLand = false, sitAfterWave = false;
    float followT = 0, followNear = 0;
    bool wantSettings = false;
    bool byeShutdown();
    void byeCancel();
    bool byeOn = false, byeLay = false;
    double byeAt = 0;
    Vitals vit;
    float saveT = 20, sleepyT = 300;
    int wakePokes = 0;
    double lastWakePoke = -100, noSleepUntil = 0;
    void vitalsUpdate(float dt, double idleSec);
    void enterFatal();
    void reboot();
    void forcedWake(bool lifted);
    void addRel(float d, const char* why);
    int tier() const { return vit.intro == 2 ? 2 : vit.rel < 30.f ? 0 : vit.rel < 67.f ? 1 : vit.rel < 93.f ? 2 : 3; }
    void syncStage() const { app.phrases.stage = tier(); }
    bool weak() const { return vit.food < 30.f; }
    const char* idleCat() const;
    double lastPetGain = -1e9, nextNightmareOk = 0;
    float warmT = 300;
    bool wasAway = false;
    int morningDay = -1;
    float dateT = 30;
    double lastDateSay = -1e9;
    int metDays(double now) const;
    const char* dateDue(const tm& lt, double now, int& anniv) const;
    bool dateSay(const tm& lt, double now);
    bool solverGate(const tm& lt, const Input& in) const;
    bool solverStart(bool force);
    void solverStep(float dt, const Input& in);
    float solverT = -1, solverK = 0, sigilK = 0, solverRollT = 10;
    bool solverMute = false;
    float solverWait = 0;
    static constexpr double kAfkIdle = 480, kAfkNoteIdle = 1500;
    static constexpr float kAfkCap = 70.f;
    bool afkStart();
    void afkNapNow();
    void afkStep(float dt, const Input& in, bool free);
    void afkWoke();
    bool afkNoteWrite();
    std::wstring afkNoteText();
    bool afkNap = false;
    int afkGo = 0;
    float afkWakeT = -1;
    bool afkNoteDone = false, afkNoteTell = false;
    std::wstring noteDir;
    double awayPeak = 0, afkBackSec = 0;
    double lastGreetAt = -1e9;
    std::wstring cornerDeskTest;
    std::wstring cornerDir;
    std::vector<std::wstring> cornerFiles;
    bool cornerLoaded = false, cornerPhotoAny = false;
    float cornerT = 45.f, cornerWatchT = 10.f, cornerPhotoT = -1;
    int cornerWrites = 0, cornerRefused = 0;
    int cornerMissN = 0, cornerSoftN = 0;
    std::wstring cornerPhotoName, cornerSayArg;
    std::string cornerSayCat;
    double cornerSince = 0;
    bool cornerIcon = false;
    std::wstring cornerIcoFile;
    std::wstring cornerIcoPhoto;
    bool cornerDecorAny = false;
    bool heldCorner = false;
    int cornerEffort() const;
    bool cornerIsMine(const std::wstring& path) const;
    void cornerMoved(const std::wstring& now);
    bool cornerDecorate();
    DayLog dayLog;
    bool dayLoaded = false;
    int diaryDay = 0;
    double doodleAt = 0, doodleNext = 0;
    unsigned doodlesDone = 0;
    float daySaveT = 60.f;
    void dayLoad();
    void daySave() const;
    void dayRoll();
    void dayCount(const char* what);
    void dayStep(float dt, const Input& in, bool awake);
    bool cornerDiary(const DayLog& d);
    bool doodleRender(int kind, std::string& png, std::wstring& caption);
    bool cornerDoodle(int kind = -1);
    bool doodleStep(bool calm);
    std::string moment;
    double momentAt = -1e9;
    bool momentPet = false, momentKiss = false;
    int replyCount = 0;
    double lastReplyGain = -1e9;
    const char* momentOf(const char* cat) const;
    bool momentFresh() const;
    std::string momentCat(const char* act) const;
    void momentNote(const char* cat);
    void replyAnswer(const std::string& rest, int choice);
    bool momentTouch(const char* act);
    std::wstring pendingSay;
    std::string pendingCat;
    double pendingAt = 0;
    std::string touchPendingCat;
    void momentTouchStep();
    bool arriveSit = false, arriveFace = false;
    void comeToCursor(bool sit);
    int wantPhase = 0;
    float wantT = 0;
    std::string wantKind, lastWantKind;
    double nextWantAt = 0;
    bool wantPeeked = false, arriveHug = false;
    std::wstring wantYesLine;
    bool wantCan(const Input& in) const;
    bool wantStep(const Input& in);
    void wantTick(float dt);
    void wantAsk();
    void wantAnswer(const std::string& kind, int choice);
    bool wantTouched(const char* act);
    void wantEnd(const char* why);
    bool peekOn = false;
    std::wstring peekPath;
    double peekTakeAt = -1, nextPeekAt = 0, peekGoAt = -1e9;
    int peekCaughtN = 0;
    bool peekStart(const Input& in);
    void peekArrive();
    void peekStep();
    bool peekCaught();
    int talkSteps = 0;
    std::vector<std::string> talksRecent;
    bool talkOffer(const std::string& id, const std::string& node);
    void talkAnswer(const std::string& rest, int choice);
    bool talkStartRandom();
    bool nightmareTalkStart();
    void nightmareStep();
    void nightmareTalkDone();
    bool nightmarePetted();
    void smoochCursor();
    bool hugGrab = false, hugGrabTried = false, hugSet = false;
    float hugK = 0, hugResit = 0;
    St hugResitSt = St::Sit;
    V2 hugFrom, hugAt;
    void hugCursorStep(float dt);
    void hitboxFill(UziHitbox& h, V2 wo) const;
    void hitboxPublish(V2 wo);
    float playMenuT = -1;
    double napAt = -1;
    void napStep();
    int dayMood = 0;
    int moodToldDay = 0;
    float breakAt = 0;
    const char* moodName() const;
    float moodK(int which) const;
    void moodRoll(const DayLog& prev);
    const char* moodIdleCat() const;
    bool moodAnnounce();
    bool cornerIconSync(bool force = false);
    bool cornerWriteHidden(const std::wstring& name, const std::string& bytes);
    std::wstring cornerLastPhoto() const;
    std::wstring cornerDesk() const;
    bool cornerDirOk(const std::wstring& dir, std::string* why = nullptr, int* kind = nullptr) const;
    bool cornerOwn(const std::wstring& name) const;
    std::wstring cornerFreeName(const std::wstring& base, const std::wstring& ext) const;
    void cornerLoad();
    void cornerSave() const;
    bool cornerWrite(const std::wstring& name, const std::string& bytes);
    bool cornerCanAsk(double now) const;
    bool cornerAskNow();
    void cornerAnswer(int choice);
    void hardcoreAnswer(int choice);
    double hardcoreAskAt = 0;
    bool cornerMake();
    void cornerStep(float dt, const Input& in, bool awake, bool busy);
    void cornerCheck();
    void cornerPhoto(const wchar_t* name);
    bool cornerPhotoNow();
    bool cornerMoodNote();
    bool cornerSaveFind(const std::wstring& title, const std::string& url);
    double missGap = 0;
    std::string missCat, missSaid;
    std::wstring missArg;
    static int missBucket(double gap);
    void missArm();
    bool missSay(bool atGreet);
    void missStep(bool awake, bool busy);
    MiniGame* game = nullptr;
    int lastGame = -1, gamePokes = 0;
    float runK = 1.f;
    float gameAgainT = -1;
    double gameAgainBy = 0;
    double lastGameGain = -1e9;
    const char* gameRefuse();
    void gameMenu();
    bool gameStart(int i);
    void gameStep(float dt, const Input& in);
    void gameStop(const char* why);
    float purrT = 0;
    bool nightmare = false;
    float nightmareCalm = 0;
    int nightmareSteps = 0;
    bool nightmarePet = false;
    std::string nightmareTalkId, lastNightmareTalk;
    float nightmareT = 0, nightmareAskT = 0, sleepAfterT = 0;
    void startNightmare();
    void comfortNightmare(bool petted);
    enum KissPlace { KissNone, KissCheek, KissForehead, KissMouth, KissHand, KissHead, KissBody, KissLegs };
    int kissPlaceAt(V2 cur) const;
    bool kissable(V2 cur) const;
    bool kiss(V2 cur);
    V2 visorScreen(V2 uv) const;
    bool kissAim = false;
    float kissT = -1;
    int kissMode = 0, kissWhere = 0;
    float kissSteamT = 0, kissSlide = 0, kissBlushT = 0, kissBlushK = 0;
    V2 kissAt;
    double lastKissGain = -1e9, lastKissLoss = -1e9;
    void kissStep(float dt);
    void kissFace(FaceState& tg, V2& lk);
    void kissPose(float dt);
    float emHideT = 0, emHideLen = 0, emLookT = 0, emSteamT = 0, emSteamTick = 0, emHugT = 0, emHugLen = 0;
    float emShiverT = 0, emShiverLen = 0, emBreathT = 0;
    double apiSayAt = -1e9, apiEmoteAt = -1e9;
    bool apiSay(const std::wstring& text);
    bool apiEmote(const std::wstring& tags);
    float cuddleHeat = 0, cuddleWait = 0;
    int cuddleStage = 0;
    double lastCuddleHug = -1e9;
    double cuddleSaidAt = -1e9;
    void cuddleAdd(float heat, const char* why);
    void cuddleStep(float dt);
    V2 emLook;
    void emote(const std::string& name, float arg);
    void emoteStep(float dt);
    void emoteFace(FaceState& tg, V2& lk);
    void emotePose(float dt);
    bool baby() const { return vit.intro == 2; }
    bool introHidden = false;
    V2 introAnchor;
    bool napOnLand = false;
    bool helloOnLand = false;
    bool growing = false;
    float glow = 0;
    double babyLeft() const;
    V2 centerOfMass() const { return pos - V2(0, comH); }
    void babyStep(float dt, const Input& in);
    float babyRest = 0, babySpin = 0, babyAir = 0;
    bool babyTouch = false;
    void babyUpdate(float dt, const Input& in);
    const char* babyCat(const char* cat) const;
    bool feeding = false;
    bool parcelFresh = false;
    float hungryAskT = 90, relHungryT = 0, eatT = 0, tSquint = 0, mouthWant = 0;
    bool talkFeed = false;
    float binCheckT = 0;
    bool sleepCarry = false, sleepLower = false, carrySaid = false, carryMoaned = false;
    V2 carryOff;
    float carryRough = 0, carryT = 0, lowerT = 0;
    bool carriedAsleep() const { return sleepCarry || sleepLower; }
    void carryStep(float dt, const Input& in);
    void releaseSleeper();
    float dragRough = 0;
    bool dragSaid = false, wasSitting = false;
    V2 shkPrev, shkDisp;
    float shkPath = 0;
    bool shkOn = false;
    float shakeMeter(V2 cur, float dt);
    bool biteQueued = false;
    float tiredK() const;
    float blinkLen = 0.14f, blinkHold = 0, nod = 0;
    void hungerUpdate(float dt, double idleSec);
    void offlineCatchUp();
    void startFeeding();
    bool batHover = false;
    bool wantUpdate = false;
    std::vector<std::wstring> notesQueue;
    float notesT = 5.f;
    void offerUpdate(const std::wstring& ver, int cmp);
    void batteryHover(bool on);
    void batteryDrop();
    void feedTick();
    void eatBattery(bool self);
    void giveBattery(int n, const char* why);
    void parcelLanded();
    void parcelOpened();
    void die();
    void starve();
    V2 mouthScreen() const;
    float hungerK() const { return clampf((30.f - vit.food) / 30.f, 0.f, 1.f); }
    float standTop = 0, standEye = 0;
    float sinkDepthPx() const { return std::max(comH * 2.2f, standTop * 1.06f); }
    void lifeFace(FaceState& tg, V2& lk);
    bool webActive() const;
    void webUpdate(float dt, const Input& in);
    void webEntered();
    void answerWeb(bool yes, bool stroll = false);
    void webGo(bool stroll = false);
    void webServeOnly();
    bool webDecide(const Input& in);
    void leaveWeb(int why);
    bool webOn = false, webShown = false, webFirst = true, webDiveOnLand = false, nestShame = false, shameRun = false;
    HWND webWnd = nullptr;
    double webSince = 0, webLife = 0, webPageT = 0;
    float webAutoT = 0, tBlush = 0, shameRunT = 0;
    int webVer = -1, webPress = -1, webShame = 0, webAutoN = 0;
    bool webExt = false;
    double webExt0 = 0, webPickT = -1;
    double lastWebSay = -1e9;
    int webAnchor = -2;
    float webAnchorDx = 0;
    std::wstring webAnchorTitle, webTitle;
    V2 rideFrom;
    float rideMovedT = 0, rideLastY = 0;
    bool rideSent = false;
    V2 rideHand;
    bool webRide(float dt);
    bool webStartRide();
    double webScrollAt = -1e9;
    float webSy = -1;
    bool webMoving() const;
    void webScroll(int dy);
    RECT webFakeRc{};
    bool webFake = false;
    float webHidT = 0, webAloneAll = 0;
    bool webPeeked = false, webSawShy = false;
    bool webStroll = false;
    double webBlurtAt = -1;
    float shyChance() const;
    float webShyRate(int n);
    void trustAdd(float d, const char* why);
    enum ExtStep { ExtNone, ExtOffer, ExtPlea, ExtPaste, ExtGo, ExtDev, ExtDrag, ExtFallback, ExtWait };
    void extUpdate(float dt, const Input& in, bool free);
    bool extStart(bool fromButton);
    bool extCanStart() const;
    bool extCanPoint() const;
    void extAnswer(const std::string& id, int choice);
    void extStop(const char* why);
    void extPose(float dt);
    void extFace(FaceState& tg, V2& lk);
    void extDraw(DrawCtx& c, V2 wo);
    bool extFolderHit(V2 cur) const;
    void extDragBegin();
    void extDragEnd(unsigned long effect);
    bool extOn() const { return extStep != ExtNone; }
    int extStep = ExtNone;
    float extT = 0, extOfferT = -1, extDragHeld = 0, bPoint = 0;
    bool extFolder = false, extDragging = false, extUp = false;
    bool extAskDue = false;
    bool extPlanned = false;
    int extTries = 0;
    V2 extPointAt;
    HWND extWnd = nullptr;
    ExtBrowser extB;
    std::wstring extTestProgId;
    void extBegin();
    void extLaunch();
    void extFindWindow();
    V2 extToggle(const RECT& r) const;
    void extPlanMove();
    void extPointNow();
    void extDone();
    bool extAskNow();
    bool biteHop = false;
    double lingerWarnAt = -100, petWarnAt = -100;
    double lingerLoveAt = -100;
    float stirT = -1, groggyT = 0;
    std::string stirWhy;
    std::wstring wakeUpdateVer;
    int wakeUpdateCmp = 0;
    float sleepPetT = 0;
    void gentleWake(const char* why);
    void stirStep(float dt);
    void warnBite(const char* cat);
    float biteSnapT = 0;
    std::wstring lastTopic;

    App& app;
    V2 pos, vel;
    float rot = 0, rotVel = 0;
    int facing = 1;
    St st = St::Fall;
    V2 airFrom{-1e9f, -1e9f};
    HWND jumpWin = nullptr;
    bool airPlanned = false;
    HWND standWin = nullptr;
    HWND fromWin = nullptr;
    float stT = 0, stDur = 0;
    Ground gnd;
    float walkTarget = 0;
    bool walkOff = false;
    V2 jumpVel;
    float rideSpeed = 0;

    float t = 0, walkPhase = 0;
    float bWalk = 0, bSit = 0, bSleep = 0, bCarry = 0, bAir = 0, bDrag = 0, bPet = 0, bWave = 0, crouch = 0;
    float squash = 0, squashV = 0;
    float lookTurn = 0;

    FaceState face;
    float blinkT = 2, blinkPh = -1;
    float tSurprise = 0, tAngry = 0, tHappy = 0, tHeart = 0, tCross = 0, tSad = 0;

    V2 lastCursor, cursorVel, cursorAcc;
    float rub = 0, petLevel = 0, petTime = 0, petBlock = 0;
    bool petting = false;
    int petStage = 0;
    std::vector<double> rubRevs;
    V2 rubDir, rubAcc;
    float rubInside = 0, lingerT = 0, shakeAcc = 0, vengeT = 0, bCling = 0, bBite = 0;
    V2 biteDir;
    std::wstring heldPath;
    std::string heldCat;
    CachedBitmap heldImg;
    float heldT = 0, heldLife = 0, hoverT = 0;
    int heldStage = 0;
    FileSense heldSense;
    std::shared_ptr<SenseJob> senseJob;
    std::shared_ptr<SenseJob> senseRead;
    void senseLetGo();
    double dropAt = -1;
    float handsMs = -1;
    float heldHappyAt = -1;
    std::wstring heldName;
    ComPtr<IDWriteTextFormat> heldFmt;
    ComPtr<IDWriteTextLayout> heldLay;
    float heldLaySz = 0, heldLayMax = 0, heldLabW = 0, heldLabH = 0;
    CachedBitmap heldHalo;
    int heldHaloPad = 0;
    void heldRects(D2D1_RECT_F& box, D2D1_RECT_F& img, D2D1_RECT_F& lab) const;
    bool fileHovering = false, hoverSaid = false;
    bool heldDragging = false, heldRetake = false, heldTakenSaid = false;
    bool heldHit(V2 cur) const;
    bool heldDragBegin();
    void heldDragLeft();
    void heldDragEnd(unsigned long effect, V2 cur, bool onDesktop);
    void dropHeld(const char* why);
    void heldClear();
    enum DeskMode { DeskNone, DeskHold, DeskGo, DeskOffer, DeskPlace, DeskTrip };
    enum DeskGoal { GoalHome, GoalFree, GoalFolder, GoalFeet };
    int deskMode = DeskNone, deskGoal = GoalHome;
    bool deskOn() const { return deskMode != DeskNone; }
    std::wstring deskName, deskFolder;
    POINT deskHome{}, deskTo{};
    bool deskOffered = false;
    bool deskOwnHome = false;
    HandsRec deskRec;
    float deskT = 0, deskAll = 0, deskCheckT = 0, deskThrowT = -1;
    int deskLegs = 0;
    bool deskFlyOn = false;
    V2 deskFly;
    std::vector<V2> deskBad;
    float bOffer = 0;
    V2 deskFrom, deskHandOff;
    bool deskStart(const std::wstring& path);
    struct BusyJob {
        std::atomic<bool> done{false};
        std::wstring app;
        int files = 0, apps = 0;
        float ms = 0;
    };
    bool deskBusy = false;
    float tugT = -1, bTug = 0;
    bool tugFolder = false, tugSaid = false;
    std::wstring tugPath;
    double tugSince = 0;
    std::wstring tugWho;
    int tugApps = -1, tugFiles = 0;
    std::string tugCat;
    std::shared_ptr<BusyJob> busyJob;
    void tugStart(const std::wstring& path);
    void tugStep(float dt);
    void deskUpdate(float dt);
    bool deskRelease(POINT cell, const char* why, bool endCarry = true);
    void deskHomeNow(const char* why);
    void deskCrashHome();
    void deskRetryHands();
    double hidRetryAt = 0;
    void deskReturn();
    void deskPlan();
    void deskPlace();
    void deskFinish();
    void deskEnd(bool keep);
    bool deskHides(const DIcon& ic) const;
    std::wstring deskSelf() const;
    int deskIndex();
    bool cellTaken(const RECT& cell, int skip) const;
    bool freeCellNear(V2 at, POINT& out, int skip);
    bool cellShown(POINT cell) const;
    void deskDisplayChanged();
    bool deskPickFolder();
    bool deskMoveToFolder();
    bool tripArmed = false, deskDropped = false, tripPlanned = false;
    int tripForce = 0;
    int tripPh = 0, tripJumps = 0;
    float tripX = 0, tripD = 0;
    float tripT = 0, tripFlyT = -1, tripLiftT = -1, tripCheckT = 0, tripSlide = 0;
    float tripUpset = 0;
    double lastTripComfort = -1e9;
    V2 tripFile, tripFrom;
    POINT tripCell{};
    float tripSurfY = 0;
    SurfKind tripKind = SurfKind::Floor;
    HWND tripHwnd = nullptr;
    float bTrip = 0;
    void deskArmWalk();
    bool tripSpot(float x, float dir, POINT& cell);
    bool deskTrip();
    void deskTripUpdate(float dt);
    void tripState(float dt);
    void tripComfort();
    int grabHand = 0;
    int pinPart = -1;
    V2 pinLocal;
    float pinReach = 0;
    int pinRootOf(int part) const;
    void setPin(int part, V2 cur);
    void hangByPin(V2 cur, bool clampDown);
    float pinError() const;
    float clingCyc = 0, clingL0 = 1, clingMouth = 0, ledSpeed = 0;
    V2 raisedHand(int side) const;
    void hangBy(V2 cur, V2 g, bool clampDown = true);
    V2 shoulderScreen(int side) const;
    double lastPetEnd = -100;

    bool dragging = false;
    float pend = 0, pendV = 0, pendL = 1, grabBeta = 0, dragAgit = 0, spin = 0;

    int pokeCount = 0;
    double lastPoke = -100, lastSay = -100, lastWave = -100, lastFetch = -1000;
    float talkCd = 90;
    bool greeted = false;

    std::wstring fetchName, carriedName;
    CachedBitmap carriedImg;
    bool dropTargetSet = false;
    HandsRec fetchRec;
    float iconMoveT = 0;
    V2 carriedCenter;

    V2 headC, headTop;
    float headR = 50;
    std::vector<Particle> parts;
    float zzzT = 0, heartT = 0;
    ComPtr<IDWriteTextFormat> pfmt;
    ComPtr<IDWriteTextLayout> playZ, playz, playEx, playAm;
    float pfmtScale = 0;
    int iBody = -1, iHead = -1, iArmL = -1, iArmR = -1, iLegL = -1, iLegR = -1, iLegs = -1;
    int iFArmL = -1, iFArmR = -1, iHairF = -1, iHairB = -1, iBeanie = -1, iShinL = -1, iShinR = -1;
    int active = 0;
    Rig& rigAt(int a) { return a == 1 ? app.rigSit : a == 2 ? app.rigSleep : a == 3 ? app.rigBaby : app.rig; }
    RigInstance& instAt(int a) { return a == 1 ? app.instSit : a == 2 ? app.instSleep : a == 3 ? app.instBaby : app.inst; }
    const Rig& curRig() const { return active == 1 ? app.rigSit : active == 2 ? app.rigSleep : active == 3 ? app.rigBaby : app.rig; }
    RigInstance& curInst() { return instAt(active); }
    const RigInstance& curInst() const { return active == 1 ? app.instSit : active == 2 ? app.instSleep : active == 3 ? app.instBaby : app.inst; }
    void selectRig(int a);
    float comH = 80, legLen = 30;

    bool holding() const { return !carriedName.empty() || heldShown() || (extFolder && !extDragging); }
    V2 bubbleTip() const { return st == St::Trip && fabsf(rot) > 0.6f ? headTop - V2(facing * headR * 2.f, 0) : headTop; }
    bool heldShown() const { return !heldPath.empty() && !heldDragging && deskThrowT < 0 && !deskDropped; }
    float S() const { return app.s; }
    float G() const { return 2100.f * app.s; }
    bool grounded() const;
    bool trajectoryOk(V2 p0, V2 v, float T) const;
    V2 handScreen(bool right) const;
    int iconUnder(const Surf& s, float x) const;
    bool surfSkip(const Surf& s) const;
    bool onSurf(const Surf& s) const;
    void iconStrip(int icon, float& y, float& x0, float& x1) const;
    mutable int perfJumpPlans = 0;
    mutable double perfJumpSec = 0;
    float fallT = 0, outT = 0;
    int fellOuts = 0;
    std::string fellWhy;
    void fallGuard(float dt);
    void fellOut(const char* why);
    bool tryJump(V2 target, float apexMin = 50, float apexMax = 110) { return goJump(target, apexMin, apexMax); }
    void startFallNow() { startFall(vel); }
    bool jumpNow(V2 target) { return goJump(target); }
    bool pickJumpNow(V2& t, int& ii) { return pickJump(t, false, ii); }

private:
    V2 com() const { return pos + V2(0, -comH); }
    void startFall(V2 v0);
    void airStep(float dt);
    const Surf* findLanding(V2 prev, V2 cur) const;
    void land(const Surf& s);
    bool groundCheck(float dt);
    void decide(const Input& in);
    void carryDecide();
    bool pickJump(V2& target, bool icon, int& iconIdx);
    bool goJump(V2 target, float apexMin = 50, float apexMax = 110);
    bool planJump(V2 target, float apexMin, V2& v) const;
    void wake(bool startled);
    void grabIcon();
    void updatePetting(float dt, const Input& in);
    void updateFace(float dt, const Input& in);
    void animate(float dt);
    void updateParticles(float dt);
    void emit(int type, V2 p, V2 v, float life, float size);
    void moveCarriedIcon(float dt, bool force);

public:
    float rootBob = 0;
    bool freezeFace = false;
    V2 rideVel;
};
