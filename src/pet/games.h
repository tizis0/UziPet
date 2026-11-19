#pragma once
#include "pet.h"

struct MiniGame {
    virtual ~MiniGame() = default;
    virtual const wchar_t* title() const = 0;
    virtual const char* id() const = 0;
    virtual void start(Pet& p) = 0;
    virtual bool update(Pet& p, float dt, const Input& in) = 0;
    virtual void cancel(Pet& p) = 0;
    virtual void face(Pet&, FaceState&, V2&) {}
    virtual void pose(Pet&) {}
    int result = 0;
};

const std::vector<MiniGame*>& gameList();
