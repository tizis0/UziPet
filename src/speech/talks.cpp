#include "pet.h"
#include "json.h"

namespace {
using Replies = std::vector<std::pair<std::wstring, std::string>>;
struct TalkNode {
    std::wstring say;
    Replies replies;
};
struct Talk {
    std::string id;
    std::string kind;
    int minTier = 0, maxTier = 3;
    std::wstring start;
    Replies replies;
    std::vector<std::pair<std::string, TalkNode>> nodes;
};
std::vector<Talk> g_talks;

Replies readReplies(const Json& j) {
    Replies r;
    for (size_t i = 0; i < j.size(); i++)
        if (j[i].size() >= 1) r.push_back({widen(j[i][0].str()), j[i][1].str()});
    return r;
}

const Talk* findTalk(const std::string& id) {
    for (auto& t : g_talks)
        if (t.id == id) return &t;
    return nullptr;
}
}

static void talksRead(const std::wstring& path, const char* kind) {
    std::string txt;
    if (!readFile(path, txt)) return;
    bool ok = false;
    Json j = Json::parse(txt, &ok);
    if (!ok) logf("talks: parse error in %s", narrow(path).c_str());
    for (size_t i = 0; i < j.k.size(); i++) {
        if (!j.k[i].empty() && j.k[i][0] == '_') continue;
        const Json& t = j.a[i];
        Talk tk;
        tk.id = j.k[i];
        tk.kind = kind;
        tk.minTier = t["minTier"].i(0);
        tk.maxTier = t["maxTier"].i(3);
        tk.start = widen(t["start"].str());
        tk.replies = readReplies(t["replies"]);
        const Json& n = t["nodes"];
        for (size_t k = 0; k < n.k.size(); k++) tk.nodes.push_back({n.k[k], TalkNode{widen(n.a[k]["say"].str()), readReplies(n.a[k]["replies"])}});
        g_talks.push_back(std::move(tk));
    }
}

int talksLoad(const std::wstring& path) {
    g_talks.clear();
    talksRead(path, "");
    talksRead(path.substr(0, path.find_last_of(L"\\/") + 1) + L"nightmares.json", "nightmare");
    logf("talks: %zu loaded", g_talks.size());
    return (int)g_talks.size();
}

bool talkIsNightmare(const std::string& id) {
    const Talk* t = findTalk(id);
    return t && t->kind == "nightmare";
}

int talksBroken() {
    int bad = 0;
    for (auto& t : g_talks) {
        auto has = [&](const std::string& n) {
            return n.empty() || std::any_of(t.nodes.begin(), t.nodes.end(), [&](auto& x) { return x.first == n; });
        };
        for (auto& r : t.replies)
            if (!has(r.second)) bad++, logf("talk %s: root button -> missing '%s'", t.id.c_str(), r.second.c_str());
        for (auto& [nid, nd] : t.nodes)
            for (auto& r : nd.replies)
                if (!has(r.second)) bad++, logf("talk %s/%s: button -> missing '%s'", t.id.c_str(), nid.c_str(), r.second.c_str());
        if (t.replies.empty()) bad++, logf("talk %s: no buttons", t.id.c_str());
    }
    return bad;
}

std::vector<std::string> talkIds() {
    std::vector<std::string> v;
    for (auto& t : g_talks) v.push_back(t.id);
    return v;
}

bool Pet::talkOffer(const std::string& id, const std::string& node) {
    const Talk* t = findTalk(id);
    if (!t || !app.bubble.active) return false;
    const Replies* r = &t->replies;
    if (!node.empty()) {
        r = nullptr;
        for (auto& [nid, nd] : t->nodes)
            if (nid == node) r = &nd.replies;
    }
    if (!r || r->empty()) return false;
    std::vector<std::wstring> ch;
    for (auto& [label, next] : *r) ch.push_back(app.phrases.fill(label, L""));
    app.bubble.choices = ch;
    app.bubble.askId = "talk:" + id + ":" + node;
    app.bubble.life = app.bubble.typeDur + (t->kind == "nightmare" ? 40.f : 14.f);
    return true;
}

void Pet::talkAnswer(const std::string& rest, int choice) {
    size_t c = rest.find(':');
    if (c == std::string::npos || choice < 0) return;
    std::string id = rest.substr(0, c), node = rest.substr(c + 1);
    const Talk* t = findTalk(id);
    if (!t) return;
    const Replies* r = &t->replies;
    if (!node.empty()) {
        r = nullptr;
        for (auto& [nid, nd] : t->nodes)
            if (nid == node) r = &nd.replies;
    }
    if (!r || choice >= (int)r->size()) return;
    std::string next = (*r)[choice].second;
    talkSteps++;
    bool nm = t->kind == "nightmare" && nightmare;
    if (nm) nightmareStep();
    if (tier() >= 1 && app.now - lastReplyGain > 600.0) {
        lastReplyGain = app.now;
        addRel(0.3f, "talked with her");
    }
    if (next.empty()) {
        logf("talk %s: ended by button %d", id.c_str(), choice);
        if (nm) nightmareTalkDone();
        return;
    }
    for (auto& [nid, nd] : t->nodes) {
        if (nid != next) continue;
        if (fabsf(lastCursor.x - pos.x) > 6.f && (st == St::Idle || st == St::Sit || st == St::Stretch)) facing = lastCursor.x > pos.x ? 1 : -1;
        app.bubble.say(app.phrases.fill(nd.say, L""));
        lastSay = app.now;
        bool more = talkOffer(id, next);
        if (!more) app.bubble.hold();
        logf("talk %s: -> %s '%s'%s", id.c_str(), next.c_str(), narrow(app.bubble.text).c_str(), app.bubble.asking() ? " (buttons)" : " (end)");
        if (nm && !more) nightmareTalkDone();
        return;
    }
}

bool Pet::talkStartRandom() {
    if (!app.settings.replies || baby() || vit.intro != 0 || app.bubble.asking() || app.bubble.active) return false;
    std::vector<const Talk*> can;
    for (auto& t : g_talks)
        if (t.kind.empty() && !t.start.empty() && t.minTier <= tier() && std::find(talksRecent.begin(), talksRecent.end(), t.id) == talksRecent.end()) can.push_back(&t);
    if (can.empty()) {
        std::string last = talksRecent.empty() ? "" : talksRecent.back();
        talksRecent.clear();
        if (!last.empty()) talksRecent.push_back(last);
        for (auto& t : g_talks)
            if (t.kind.empty() && !t.start.empty() && t.minTier <= tier() && t.id != last) can.push_back(&t);
    }
    if (can.empty()) return false;
    const Talk* t = can[randi(0, (int)can.size() - 1)];
    app.bubble.say(app.phrases.fill(t->start, L""));
    lastSay = app.now;
    talkOffer(t->id, "");
    talksRecent.push_back(t->id);
    if (talksRecent.size() > 40) talksRecent.erase(talksRecent.begin());
    daySave();
    logf("talk %s: she starts '%s'", t->id.c_str(), narrow(app.bubble.text).c_str());
    return true;
}

bool Pet::nightmareTalkStart() {
    std::vector<const Talk*> can;
    for (auto& t : g_talks)
        if (t.kind == "nightmare" && t.minTier <= tier() && tier() <= t.maxTier && !t.start.empty() && t.id != lastNightmareTalk) can.push_back(&t);
    if (can.empty())
        for (auto& t : g_talks)
            if (t.kind == "nightmare" && t.minTier <= tier() && tier() <= t.maxTier && !t.start.empty()) can.push_back(&t);
    if (can.empty()) return false;
    const Talk* t = can[randi(0, (int)can.size() - 1)];
    nightmareTalkId = lastNightmareTalk = t->id;
    nightmareCalm = 0;
    nightmareSteps = 0;
    nightmarePet = false;
    app.bubble.close();
    app.bubble.say(app.phrases.fill(t->start, L""));
    lastSay = app.now;
    talkOffer(t->id, "");
    logf("nightmare: talk %s '%s'", t->id.c_str(), narrow(app.bubble.text).c_str());
    return true;
}

void Pet::nightmareStep() {
    nightmareSteps++;
    nightmareCalm = std::min(1.f, nightmareCalm + 0.3f);
}

void Pet::nightmareTalkDone() {
    if (!nightmare) return;
    nightmare = false;
    nightmareCalm = 1.f;
    nightmareTalkId.clear();
    int t = tier();
    addRel(nightmarePet ? 4.f : 3.f, nightmarePet ? "nightmare: talked and petted" : "nightmare: talked her down");
    tSad = 0;
    if (t >= 2) {
        tBlush = std::max(tBlush, 3.f);
        nightmareAskT = 1.f;
    } else {
        sleepAfterT = std::max(2.2f, app.bubble.holdT - app.bubble.t + 1.f);
    }
    logf("nightmare: calmed down after %d answers%s", nightmareSteps, nightmarePet ? " and petting" : "");
}

bool Pet::nightmarePetted() {
    if (!nightmare || nightmareTalkId.empty()) return false;
    const Talk* t = findTalk(nightmareTalkId);
    if (!t) return false;
    nightmareCalm = std::min(1.f, nightmareCalm + 0.35f);
    tSad = 0;
    if (nightmarePet) return true;
    nightmarePet = true;
    bool has = std::any_of(t->nodes.begin(), t->nodes.end(), [](auto& n) { return n.first == "petted"; });
    if (!has) return true;
    for (auto& [nid, nd] : t->nodes) {
        if (nid != "petted") continue;
        app.bubble.close();
        app.bubble.say(app.phrases.fill(nd.say, L""));
        lastSay = app.now;
        bool more = talkOffer(t->id, "petted");
        if (!more) app.bubble.hold();
        logf("nightmare: petted during talk -> '%s'", narrow(app.bubble.text).c_str());
        if (!more) nightmareTalkDone();
        break;
    }
    return true;
}
