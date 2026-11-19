#pragma once
#include "pet.h"

void introStart(App& a, Pet& p, bool test);
void introFrame(App& a, Pet& p, float dt);
void introAnswer(App& a, Pet& p, const std::string& id, int choice);
void introGrowStarted(App& a, Pet& p);
void introContinue(App& a, Pet& p);
void introGrowNow(Pet& p);
bool introOverlayShown();
bool introRunning();
