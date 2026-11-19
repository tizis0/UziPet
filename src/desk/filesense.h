#pragma once
#include "common.h"
#include <atomic>
#include <memory>

enum class FileKind { None, Self, Medical, Gun, Show, N, V, J, Cyn, Khan, Tessa };

struct FileSense {
    int self = 0;
    FileKind kind = FileKind::None;
    FileKind with = FileKind::None;
    float nameScore = 0, folderScore = 0, colorScore = 0;
    float purple = 0, dark = 0, eye = 0, bg = 0;
    float edge = 0;
    bool hadPixels = false;
    bool is3d = false;
    bool medGuess = false;
    double ms = 0;
    std::string why;
};

const char* fileKindName(FileKind k);
const wchar_t* fileKindSay(FileKind k);
bool senseWantsPixels(const std::wstring& path);

FileSense senseName(const std::wstring& path);
void senseColors(FileSense& fs, const uint8_t* bgra, int w, int h, bool hasAlpha);
bool senseThumb(const std::wstring& path, int px, FileSense& fs);
void senseFinish(FileSense& fs);
FileSense senseFile(const std::wstring& path);

struct SenseJob {
    std::atomic<bool> done{false};
    FileSense fs;
};
std::shared_ptr<SenseJob> senseStart(const std::wstring& path, const FileSense& byName);
