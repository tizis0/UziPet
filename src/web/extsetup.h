#pragma once
#include "common.h"

struct ExtBrowser {
    std::wstring progId, exe;
    std::wstring url = L"chrome://extensions";
    bool firefox = false;
    bool edge = false;
    bool guess = false;
};
ExtBrowser extBrowserFor(const std::wstring& progId);
ExtBrowser extDefaultBrowser();
bool extIsBrowserImage(const std::wstring& exe, const std::wstring& image);
void extTestAlive(bool on);
