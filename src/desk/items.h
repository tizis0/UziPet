#pragma once
#include "pet.h"

void itemsFrame(App& a, Pet& p, float dt);
bool itemsBusy();
void drawParcelArt(App& a, Surface& s, float S, float sway, float chute, float lid, float alpha, float squash, float tilt);

std::wstring batteryLinkPath();
bool makeBatteryIcon(App& a, int n, const std::wstring& ico);
void batteryLinkUpdate(App& a, Pet& p, float dt);
