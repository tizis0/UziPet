#include "pet.h"

namespace {
const double kMomentWin = 25.0;
}

const char* Pet::momentOf(const char* cat) const {
    auto starts = [&](const char* p) { return strncmp(cat, p, strlen(p)) == 0; };
    if (starts("wakeRested") || starts("afkWake") || !strcmp(cat, "morning")) return "Woke";
    if (!strcmp(cat, "moodYawn") || !strcmp(cat, "afkYawn") || !strcmp(cat, "sleepy") || !strcmp(cat, "sleepyGo")) return "Yawn";
    if (!strcmp(cat, "idle") && app.phrases.stage >= 2 && app.phrases.lastCat == "idle" && app.phrases.lastOwn) return "Love";
    if (!strcmp(cat, "idleWarm") || starts("warmNight") || starts("warmWork") || starts("warmBack")) return "Love";
    if (starts("missYou") || starts("backLong")) return "Miss";
    if (starts("gameWin") || starts("doodleDone") || starts("cornerIcon")) return "Proud";
    if (starts("gameLose")) return "Pout";
    return nullptr;
}

bool Pet::momentFresh() const { return !moment.empty() && app.now - momentAt < kMomentWin; }

std::string Pet::momentCat(const char* act) const {
    if (!momentFresh() || baby()) return "";
    std::string c = std::string(act) + "Moment" + moment;
    return app.phrases.list(c.c_str()).empty() ? "" : c;
}

static bool warmKind(const std::string& k) {
    if (k.size() > 1 && k[0] == 'Q' && isupper((unsigned char)k[1])) return true;
    return k == "Compliment" || k == "Ask" || k == "Care" || k == "Worry" || k == "Hug" || k == "Love" || k == "Praise";
}

void Pet::momentNote(const char* cat) {
    if (!app.bubble.talkId.empty()) {
        if (app.settings.replies && !app.bubble.asking() && !baby() && vit.intro == 0) talkOffer(app.bubble.talkId, "");
        return;
    }
    const char* m = momentOf(cat);
    const std::string& kind = app.bubble.replyKind;
    bool tagged = !kind.empty() && kind != "None";
    if (!m && !(tagged && warmKind(kind)) && !tagged) return;
    if (baby() || vit.intro != 0) return;
    if (m || warmKind(kind)) {
        moment = m ? m : "Love";
        momentAt = app.now;
        momentPet = momentKiss = false;
    }
    if (!app.settings.replies || app.bubble.asking() || !app.bubble.active) return;
    std::string pk;
    if (tagged) pk = kind;
    else if (kind.empty() && m && strcmp(m, "Love") != 0) pk = m;
    if (pk.empty()) return;
    auto pool = app.phrases.list(("reply" + pk).c_str());
    if (pool.size() < 2) return;
    std::vector<int> idx, must;
    const auto& allow = app.bubble.replyAllow;
    for (int i = 0; i < (int)pool.size(); i++) {
        if (!allow.empty() && std::find(allow.begin(), allow.end(), i) == allow.end()) continue;
        (pool[i].rfind(L"!", 0) == 0 ? must : idx).push_back(i);
    }
    if (idx.size() + must.size() < 2) return;
    for (int i = (int)idx.size() - 1; i > 0; i--) std::swap(idx[i], idx[randi(0, i)]);
    if (must.size() > 1) must.resize(1);
    idx.resize(std::min(idx.size(), 3 - must.size()));
    idx.insert(idx.end(), must.begin(), must.end());
    std::vector<std::wstring> ch;
    std::string id = "reply:" + pk + ":";
    for (size_t k = 0; k < idx.size(); k++) {
        const std::wstring& lab = pool[idx[k]];
        ch.push_back(app.phrases.fill(lab.rfind(L"!", 0) == 0 ? lab.substr(1) : lab, L""));
        id += (k ? "," : "") + std::to_string(idx[k]);
    }
    app.bubble.choices = ch;
    app.bubble.askId = id;
    app.bubble.life = app.bubble.typeDur + 9.f;
    logf("moment %s: replies offered (%s)", moment.c_str(), id.c_str());
}

void Pet::replyAnswer(const std::string& rest, int choice) {
    size_t c = rest.find(':');
    if (c == std::string::npos || choice < 0) return;
    std::string m = rest.substr(0, c);
    std::vector<int> idx;
    for (size_t p = c + 1; p < rest.size();) {
        size_t q = rest.find(',', p);
        if (q == std::string::npos) q = rest.size();
        idx.push_back(atoi(rest.substr(p, q - p).c_str()));
        p = q + 1;
    }
    if (choice >= (int)idx.size()) return;
    std::string base = "reply" + m + std::to_string(idx[choice]);
    std::string cat = base;
    static const char* own[] = {"Woke", "Yawn", "Miss", "Proud", "Pout"};
    bool isOwn = std::find_if(std::begin(own), std::end(own), [&](const char* o) { return m == o; }) != std::end(own);
    if (isOwn || warmKind(m)) {
        moment = isOwn ? m : "Love";
        momentAt = app.now;
        momentPet = momentKiss = false;
    }
    replyCount++;
    if (tier() >= 1) tHappy = std::max(tHappy, 1.f);
    if (tier() >= 1 && app.now - lastReplyGain > 600.0) {
        lastReplyGain = app.now;
        addRel(0.3f, "replied to her");
    }
    if (fabsf(lastCursor.x - pos.x) > 6.f && (st == St::Idle || st == St::Sit || st == St::Stretch)) facing = lastCursor.x > pos.x ? 1 : -1;
    sayCat(cat.c_str(), 1.f, L"", true);
    app.bubble.hold();
    logf("reply %s #%d -> %s: '%s'", m.c_str(), idx[choice], cat.c_str(), narrow(app.bubble.text).c_str());
}

bool Pet::momentTouch(const char* act) {
    bool& used = !strcmp(act, "kiss") ? momentKiss : momentPet;
    if (used) return false;
    std::string c = momentCat(act);
    if (c.empty()) return false;
    used = true;
    if (app.bubble.reading()) {
        touchPendingCat = c;
        logf("moment %s: %s, waits until you read her line", moment.c_str(), act);
        return true;
    }
    if (app.bubble.asking() && app.bubble.askId.rfind("reply:", 0) == 0) app.bubble.close();
    else if (app.bubble.active && !app.bubble.asking()) app.bubble.close();
    sayCat(c.c_str(), 1.f, L"", true);
    logf("moment %s: %s -> '%s'", moment.c_str(), act, narrow(app.bubble.text).c_str());
    return true;
}

void Pet::momentTouchStep() {
    if (touchPendingCat.empty() || app.bubble.reading()) return;
    std::string c = touchPendingCat;
    touchPendingCat.clear();
    bool replyAsk = app.bubble.asking() && app.bubble.askId.rfind("reply:", 0) == 0;
    if (app.now - momentAt > 45.0 || (app.bubble.asking() && !replyAsk)) return;
    if (app.bubble.active) app.bubble.close();
    sayCat(c.c_str(), 1.f, L"", true);
    logf("moment: the touch line after you read: '%s'", narrow(app.bubble.text).c_str());
}
