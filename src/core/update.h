#pragma once
#include "common.h"
#include "version.h"

struct UpdatePkg {
    std::wstring zip, version;
    std::vector<std::wstring> notes;
    bool ok = false;
};

UpdatePkg readPackage(const std::wstring& zip);
int compareVersions(const std::wstring& a, const std::wstring& b);
bool applyUpdate(const UpdatePkg& p);
std::vector<std::wstring> installedNotes();

UpdatePkg readExe(const std::wstring& exe);
bool applyExeUpdate(const std::wstring& newExe);
void cleanupOldExe();
bool extractEmbedded(const std::wstring& dir);
bool selfInstall();
void refreshAssets(bool force);
