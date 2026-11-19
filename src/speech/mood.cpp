#include "pet.h"
#include <ctime>

namespace {

struct MoodK {
    const char* key;
    float walk, sit, look, jump, wave, hide, talk, idleShare;
};

const MoodK kMoods[] = {
    {"", 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.f},
    {"Lively", 1.8f, 0.55f, 1.0f, 1.8f, 1.3f, 1.2f, 0.9f, 0.35f},
    {"Sleepy", 0.5f, 1.6f, 0.7f, 0.4f, 0.7f, 0.6f, 1.3f, 0.4f},
    {"Dreamy", 0.8f, 1.2f, 2.2f, 0.7f, 0.8f, 0.7f, 1.5f, 0.6f},
    {"Cheeky", 1.2f, 0.8f, 1.0f, 1.3f, 1.6f, 2.2f, 0.9f, 0.4f},
    {"Chatty", 1.0f, 1.0f, 1.0f, 1.0f, 1.3f, 1.0f, 0.6f, 0.45f},
};
const int kMoodN = (int)(sizeof(kMoods) / sizeof(kMoods[0]));

}

const char* Pet::moodName() const {
    static const char* names[] = {"plain", "lively", "sleepy", "dreamy", "cheeky", "chatty"};
    return names[std::clamp(dayMood, 0, kMoodN - 1)];
}

float Pet::moodK(int which) const {
    const MoodK& m = kMoods[std::clamp(dayMood, 0, kMoodN - 1)];
    float k = 1.f;
    switch (which) {
        case MkWalk: k = m.walk; break;
        case MkSit: k = m.sit; break;
        case MkLook: k = m.look; break;
        case MkJump: k = m.jump; break;
        case MkWave: k = m.wave; break;
        case MkHide: k = m.hide; break;
        case MkTalk: k = m.talk; break;
    }
    if (dayMood == 4 && tier() >= 2) k = 1.f + (k - 1.f) * (tier() == 3 ? 0.35f : 0.6f);
    return k;
}

void Pet::moodRoll(const DayLog& prev) {
    time_t tt = time(nullptr);
    tm lt;
    localtime_s(&lt, &tt);
    static const float byTier[4][kMoodN] = {
        {3.f, 1.5f, 2.f, 1.f, 3.f, 0.5f},
        {3.f, 2.f, 2.f, 1.5f, 2.f, 1.5f},
        {3.f, 2.f, 2.f, 1.5f, 1.f, 2.f},
        {3.f, 2.f, 2.f, 2.f, 0.4f, 2.5f},
    };
    float w[kMoodN];
    for (int i = 0; i < kMoodN; i++) w[i] = byTier[std::clamp(tier(), 0, 3)][i];
    if (lt.tm_wday == 0 || lt.tm_wday == 6) w[2] += 1.5f;
    if (lt.tm_wday == 1) w[2] += 1.f;
    if (prev.csWin > 0 || prev.win > 0) w[1] += 1.5f;
    if (prev.pet + prev.kiss >= 5) w[5] += 1.f;
    if (vit.energy < 40) w[2] += 2.f;
    if (dayMood > 0 && dayMood < kMoodN) w[dayMood] *= 0.4f;
    float sum = 0;
    for (float x : w) sum += x;
    float r = randf(0, sum);
    int m = 0;
    for (; m < kMoodN - 1; m++) {
        if (r < w[m]) break;
        r -= w[m];
    }
    logf("mood of the day: %s -> %s (weekday %d, energy %.0f)", moodName(), kMoods[m].key[0] ? kMoods[m].key : "plain", lt.tm_wday, vit.energy);
    dayMood = m;
}

const char* Pet::moodIdleCat() const {
    const MoodK& m = kMoods[std::clamp(dayMood, 0, kMoodN - 1)];
    if (!m.key[0] || !chance(m.idleShare)) return nullptr;
    static std::string cat;
    cat = std::string("idle") + m.key;
    return app.phrases.list(cat.c_str()).empty() ? nullptr : cat.c_str();
}

bool Pet::moodAnnounce() {
    if (!dayLoaded) dayLoad();
    const MoodK& m = kMoods[std::clamp(dayMood, 0, kMoodN - 1)];
    if (!m.key[0] || moodToldDay == dayLog.day || baby() || vit.intro != 0 || lifeT < 60) return false;
    std::string cat = std::string("moodGreet") + m.key;
    if (app.phrases.list(cat.c_str()).empty()) return false;
    moodToldDay = dayLog.day;
    daySave();
    if (dayMood == 2) tSquint = 1.5f;
    else if (dayMood == 1 || dayMood == 5) tHappy = 1.2f;
    sayCat(cat.c_str(), 1.f, L"", true);
    logf("mood: told '%s'", narrow(app.bubble.text).c_str());
    return true;
}

float jitterTime(float base, float lo, float hi) {
    float u1 = randf(1e-4f, 1.f), u2 = randf(0.f, 1.f);
    float g = sqrtf(-2.f * logf(u1)) * cosf(6.2831853f * u2);
    return std::clamp(base * expf(g * 0.45f), lo, hi);
}
