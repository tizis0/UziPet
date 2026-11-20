#pragma once
#include "common.h"
#include "moddialog.h"
#include <deque>

class Mods {
public:
    static constexpr int maxControls = 4;

    struct Control {
        std::string id;
        std::wstring label;
        bool toggle = false, value = false;
    };

    struct Mod {
        std::wstring folder, name, version, error;
        std::string id;
        bool enabled = false, valid = false, ready = false;
        std::vector<Control> controls;
        struct MenuItem {
            std::string id;
            std::wstring label, icon;
        };
        std::vector<MenuItem> menu;
        float paceSpeed = 0;
        bool paceAcrossMonitors = false, captureClicks = false;

        std::wstring status() const;

    private:
        friend class Mods;
        HANDLE job = nullptr, process = nullptr;
        HANDLE input = INVALID_HANDLE_VALUE, output = nullptr;
        std::string received;
        std::deque<std::string> events;
        double deadline = 0;
    };

    struct Host {
        std::function<void(const std::wstring&, bool)> say;
        std::function<void(const std::string&, ModDialog)> showDialog;
        std::function<void(const std::string&, const std::string&, const std::wstring&)> updateDialog;
        std::function<void(const std::string&, const std::string&)> closeDialog;
    };

    Mods() = default;
    Mods(const Mods&) = delete;
    Mods& operator=(const Mods&) = delete;
    ~Mods() { stop(); }

    void refresh();
    void tick(const Host& host);
    bool clicked();
    void menuSelected(const std::string& mod, const std::string& item);
    void dialogAnswered(const std::string& mod, const ModDialog& dialog, const std::string& button);
    void relationship(int tier);
    void activateControl(size_t mod, size_t control);
    bool setEnabled(size_t index, bool enabled);
    void stop();

    const std::vector<Mod>& list() const { return mods; }
    unsigned revision() const { return changes; }
    std::wstring folder() const { return appDataDir() + L"\\mods"; }
    bool ensureFolder() const;

private:
    std::vector<Mod> mods;
    unsigned changes = 0;
    int tier = 1;
    std::wstring settingsPath() const { return appDataDir() + L"\\mods.ini"; }

    void start(Mod& mod);
    static void discover(const std::wstring& root, std::vector<Mod>& found);
    void event(const std::string& mod, std::string json);
    void stop(Mod& mod);
    void fail(Mod& mod, const std::wstring& message);
    void tick(Mod& mod, const Host& host);
    void message(Mod& mod, const std::string& json, const Host& host);
};
