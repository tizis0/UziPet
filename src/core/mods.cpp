#include "mods.h"
#include "json.h"
#include <objbase.h>
#include <cstdlib>

namespace {

constexpr DWORD maxMessage = 8192;
constexpr size_t maxEvents = 16;

// Runs in the child. Keep the protocol stream separate from the mod's print output.
const wchar_t* bootstrap = LR"PY(
import json
from pathlib import Path
import runpy
import sys
import threading
import traceback
import types

protocol_in = sys.stdin
protocol_out = sys.stdout
sys.stdout = sys.stderr
lock = threading.Lock()

def send(kind, **fields):
    line = json.dumps(dict(type=kind, **fields), ensure_ascii=False, separators=(',', ':'))
    if len(line.encode('utf-8')) >= 8192:
        raise ValueError('UziPet message too long')
    with lock:
        protocol_out.write(line + '\n')
        protocol_out.flush()

api = types.ModuleType('uzipet')
api.say = lambda text, interrupt=False: send('say', text=str(text), interrupt=bool(interrupt))
api.button = lambda id, label: send('button', id=id, label=label)
api.toggle = lambda id, label, checked=False: send('toggle', id=id, label=label, value=bool(checked))
api.pet_menu = lambda id, label, icon='': send('pet_menu', id=id, label=label, icon=icon)
api.show_dialog = lambda id, text, buttons, fields=(), status='', cancel='': send(
    'show_dialog', id=id, text=text, buttons=buttons, fields=fields, status=status, cancel=cancel)
api.update_dialog = lambda id, *, status: send('update_dialog', id=id, status=status)
api.close_dialog = lambda id: send('close_dialog', id=id)
api.pace = lambda speed=0, across_monitors=False: send('pace', speed=speed, across_monitors=across_monitors)
api.capture_clicks = lambda enabled: send('capture_clicks', enabled=bool(enabled))
sys.modules['uzipet'] = api

try:
    entry = Path(sys.argv[1])
    sys.argv = [str(entry)]
    sys.path.insert(0, str(entry.parent))
    handler = runpy.run_path(str(entry), run_name='__main__').get('on_event')
    if handler is not None:
        handler({'type': 'start'})
    send('ready')
    for line in protocol_in:
        if handler is not None:
            handler(json.loads(line))
        send('done')
except BaseException:
    send('error', text=traceback.format_exc()[-1600:])
    sys.exit(1)
)PY";

bool isId(const std::string& id) {
    return !id.empty() && id.size() <= 64 && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

std::wstring textValue(const Json& value, size_t limit) {
    if (value.t != Json::Str || value.s.size() > limit || value.s.find('\0') != std::string::npos) return {};
    return widen(value.s);
}

std::string jsonString(const std::string& value) {
    constexpr char hex[] = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char c : value) {
        if (c < ' ') {
            out += "\\u00";
            out += hex[c >> 4];
            out += hex[c & 15];
        } else {
            if (c == '\\' || c == '"') out += '\\';
            out += (char)c;
        }
    }
    return out + '"';
}

bool readDialog(const Json& msg, ModDialog& dialog) {
    dialog.id = msg["id"].str();
    dialog.cancel = msg["cancel"].str();
    dialog.text = textValue(msg["text"], 640);
    dialog.status = textValue(msg["status"], 240);
    const auto& buttons = msg["buttons"];
    const auto& fields = msg["fields"];
    if (!isId(dialog.id) || dialog.text.empty() || buttons.t != Json::Arr ||
        buttons.size() < 1 || buttons.size() > 4 || fields.t != Json::Arr || fields.size() > 4) return false;
    for (const auto& item : buttons.a) {
        ModDialog::Button button{ item["id"].str(), textValue(item["label"], 160) };
        if (!isId(button.id) || button.label.empty() ||
            std::any_of(dialog.buttons.begin(), dialog.buttons.end(), [&](const auto& b) { return b.id == button.id; })) return false;
        dialog.buttons.push_back(std::move(button));
    }
    if (!dialog.cancel.empty() &&
        std::none_of(dialog.buttons.begin(), dialog.buttons.end(), [&](const auto& b) { return b.id == dialog.cancel; })) return false;
    for (const auto& item : fields.a) {
        auto type = item["type"].str("text");
        float length = item["length"].f(24.f);
        if (!std::isfinite(length)) return false;
        ModDialog::Field field{ item["id"].str(), textValue(item["label"], 100), textValue(item["value"], 160),
                               type == "integer", (size_t)clampf(length, 1.f, 24.f) };
        if (!isId(field.id) || field.label.empty() || (type != "text" && type != "integer") ||
            std::any_of(dialog.fields.begin(), dialog.fields.end(), [&](const auto& f) { return f.id == field.id; })) return false;
        if (field.integer && !std::all_of(field.value.begin(), field.value.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; })) return false;
        if (field.value.size() > field.length) return false;
        dialog.fields.push_back(std::move(field));
    }
    return true;
}

std::wstring quoteArg(const std::wstring& arg) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        out.append(c == L'"' ? 2 * slashes + 1 : slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(2 * slashes, L'\\');
    return out + L"\"";
}

bool isFile(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring folderSetting(const std::wstring& folder) {
    constexpr wchar_t hex[] = L"0123456789abcdef";
    std::wstring value;
    for (unsigned char c : narrow(folder)) {
        value += hex[c >> 4];
        value += hex[c & 15];
    }
    return value;
}

std::wstring pythonPath() {
    auto bundled = exeDir() + L"\\python\\python.exe";
    if (isFile(bundled)) return bundled;
    const wchar_t* path = _wgetenv(L"PATH");
    if (!path || !*path) return {};
    std::wstring found(32768, L'\0');
    DWORD len = SearchPathW(path, L"python.exe", nullptr, (DWORD)found.size(), found.data(), nullptr);
    if (!len || len >= found.size()) return {};
    found.resize(len);
    std::wstring lower = found;
    CharLowerBuffW(lower.data(), (DWORD)lower.size());
    return lower.find(L"\\microsoft\\windowsapps\\") == std::wstring::npos ? found : L"";
}

struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    HANDLE release() { HANDLE out = value; value = nullptr; return out; }
};

} // namespace

std::wstring Mods::Mod::status() const {
    if (!error.empty()) return L"Ошибка";
    if (!enabled) return L"Выключен";
    return ready ? L"Включён" : L"Запускается…";
}

bool Mods::ensureFolder() const {
    auto path = folder();
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return CreateDirectoryW(path.c_str(), nullptr) != FALSE;
}

void Mods::discover(const std::wstring& root, std::vector<Mod>& found) {
    if (root.empty()) return;
    WIN32_FIND_DATAW entry{};
    HANDLE scan = FindFirstFileW((root + L"\\*").c_str(), &entry);
    if (scan != INVALID_HANDLE_VALUE) {
        do {
            if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !wcscmp(entry.cFileName, L".") ||
                !wcscmp(entry.cFileName, L"..")) continue;
            Mod mod;
            mod.folder = root + L"\\" + entry.cFileName;
            mod.name = entry.cFileName;
            auto manifest = mod.folder + L"\\manifest.json";
            WIN32_FILE_ATTRIBUTE_DATA info{};
            std::string raw;
            bool parsed = false;
            if (GetFileAttributesExW(manifest.c_str(), GetFileExInfoStandard, &info) &&
                !info.nFileSizeHigh && info.nFileSizeLow <= 16384 && readFile(manifest, raw)) {
                Json spec = Json::parse(raw, &parsed);
                mod.id = spec["id"].str();
                auto name = spec["name"].str();
                if (!name.empty() && name.size() <= 256 && name.find('\0') == std::string::npos) mod.name = widen(name);
                auto version = spec["version"].str();
                if (version.size() <= 48 && version.find('\0') == std::string::npos) mod.version = widen(version);
                if (!parsed || spec.t != Json::Obj || !isId(mod.id))
                    mod.error = L"Проверьте id и формат manifest.json.";
                else if (spec["api"].t != Json::Num || spec["api"].n != 1)
                    mod.error = L"Требуется совместимый manifest.json с api: 1.";
                else if (!isFile(mod.folder + L"\\main.py"))
                    mod.error = L"В папке мода отсутствует main.py.";
            } else {
                mod.error = L"Требуется читаемый manifest.json размером до 16 КБ.";
            }
            mod.valid = mod.error.empty();
            found.push_back(std::move(mod));
        } while (FindNextFileW(scan, &entry));
        FindClose(scan);
    }
}

void Mods::refresh() {
    std::vector<Mod> found;
    if (ensureFolder()) discover(folder(), found);
    std::unordered_map<std::string, int> counts;
    for (const auto& mod : found)
        if (isId(mod.id)) ++counts[mod.id];
    auto ini = settingsPath();
    for (auto& mod : found) {
        if (counts[mod.id] > 1) {
            mod.valid = false;
            mod.error = L"Несколько папок используют один и тот же id мода.";
        }
        if (!mod.valid) continue;
        auto key = widen(mod.id);
        // Persist the selected directory along with the id.
        auto value = folderSetting(mod.folder);
        std::wstring known(value.size() + 6, L'\0');
        DWORD len = GetPrivateProfileStringW(L"enabled", key.c_str(), L"", known.data(), (DWORD)known.size(), ini.c_str());
        known.resize(len);
        mod.enabled = known == value;
        if (!mod.enabled) continue;
        auto active = std::find_if(mods.begin(), mods.end(), [&](const Mod& current) {
            return current.process && current.id == mod.id && current.folder == mod.folder;
        });
        if (active != mods.end()) {
            auto name = std::move(mod.name);
            auto version = std::move(mod.version);
            mod = std::move(*active);
            *active = Mod{};
            mod.name = std::move(name);
            mod.version = std::move(version);
        }
    }
    stop();
    std::sort(found.begin(), found.end(), [](const Mod& a, const Mod& b) {
        int order = CompareStringOrdinal(a.name.c_str(), -1, b.name.c_str(), -1, TRUE);
        return order == CSTR_LESS_THAN || (order == CSTR_EQUAL && a.folder < b.folder);
    });
    mods = std::move(found);
    for (auto& mod : mods)
        if (mod.enabled && !mod.process) start(mod);
}

bool Mods::setEnabled(size_t index, bool enabled) {
    if (index >= mods.size() || !mods[index].valid) return false;
    Mod& mod = mods[index];
    if (mod.enabled == enabled) return true;
    auto key = widen(mod.id);
    auto value = folderSetting(mod.folder);
    if (!WritePrivateProfileStringW(L"enabled", key.c_str(), enabled ? value.c_str() : nullptr, settingsPath().c_str()))
        return false;
    mod.enabled = enabled;
    mod.error.clear();
    if (enabled) start(mod);
    else stop(mod);
    ++changes;
    return true;
}

void Mods::start(Mod& mod) {
    const auto python = pythonPath();
    if (python.empty()) {
        fail(mod, L"Python не найден. Установите Python 3 и добавьте python.exe в PATH.");
        return;
    }
    GUID guid{};
    wchar_t suffix[40]{};
    if (FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid, suffix, ARRAYSIZE(suffix))) {
        fail(mod, L"Не удалось создать канал связи с Python.");
        return;
    }
    std::wstring pipeName = std::wstring(L"\\\\.\\pipe\\UziPet-mod-") + suffix;
    Handle input{CreateNamedPipeW(pipeName.c_str(), PIPE_ACCESS_OUTBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                  PIPE_TYPE_MESSAGE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                  1, maxMessage, 0, 0, nullptr)};
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    Handle childIn, childOut, output, childErr;
    if (input.value != INVALID_HANDLE_VALUE)
        childIn.value = CreateFileW(pipeName.c_str(), GENERIC_READ, 0, &inherit, OPEN_EXISTING, 0, nullptr);
    bool connected = childIn.value && childIn.value != INVALID_HANDLE_VALUE &&
                     (ConnectNamedPipe(input.value, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED);
    childErr.value = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);
    if (!connected || childErr.value == INVALID_HANDLE_VALUE ||
        !CreatePipe(&output.value, &childOut.value, &inherit, maxMessage) ||
        !SetHandleInformation(output.value, HANDLE_FLAG_INHERIT, 0)) {
        fail(mod, L"Не удалось создать каналы связи с Python.");
        return;
    }

    Handle job{CreateJobObjectW(nullptr, nullptr)};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.value || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        fail(mod, L"Не удалось подготовить процесс Python.");
        return;
    }
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<BYTE> storage(bytes);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = childIn.value;
    startup.StartupInfo.hStdOutput = childOut.value;
    startup.StartupInfo.hStdError = childErr.value;
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!bytes || !InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes)) {
        fail(mod, L"Не удалось подготовить запуск Python.");
        return;
    }
    HANDLE handles[] = {childIn.value, childOut.value, childErr.value};
    auto cmd = quoteArg(python) + L" -I -B -u -X utf8 -c " + quoteArg(bootstrap) + L" " + quoteArg(mod.folder + L"\\main.py");
    PROCESS_INFORMATION child{};
    BOOL created = UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles,
                                             sizeof(handles), nullptr, nullptr) &&
                   CreateProcessW(python.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
                                  nullptr, mod.folder.c_str(), &startup.StartupInfo, &child);
    DWORD error = created ? 0 : GetLastError();
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    if (!created) {
        fail(mod, L"Не удалось запустить Python. Код Windows: " + std::to_wstring(error));
        return;
    }
    Handle process{child.hProcess}, thread{child.hThread};
    if (!AssignProcessToJobObject(job.value, process.value) || ResumeThread(thread.value) == (DWORD)-1) {
        TerminateProcess(process.value, 1);
        fail(mod, L"Не удалось запустить отдельный процесс мода.");
        return;
    }
    mod.job = job.release();
    mod.process = process.release();
    mod.input = input.release();
    mod.output = output.release();
    mod.deadline = nowSec() + 10;
    ++changes;
}

void Mods::stop(Mod& mod) {
    if (mod.job) CloseHandle(mod.job);
    if (mod.process) CloseHandle(mod.process);
    if (mod.input != INVALID_HANDLE_VALUE) CloseHandle(mod.input);
    if (mod.output) CloseHandle(mod.output);
    mod.job = mod.process = mod.output = nullptr;
    mod.input = INVALID_HANDLE_VALUE;
    mod.ready = false;
    mod.deadline = 0;
    mod.controls.clear();
    mod.menu.clear();
    mod.paceSpeed = 0;
    mod.paceAcrossMonitors = mod.captureClicks = false;
    mod.events.clear();
    mod.received.clear();
}

void Mods::stop() {
    for (auto& mod : mods) stop(mod);
    mods.clear();
    ++changes;
}

void Mods::fail(Mod& mod, const std::wstring& message) {
    stop(mod);
    mod.error = message;
    logf("mod %s: %s", mod.id.c_str(), narrow(message).c_str());
    ++changes;
}

bool Mods::clicked() {
    for (auto& mod : mods) {
        if (!mod.ready || !mod.captureClicks || mod.events.size() >= maxEvents) continue;
        mod.events.emplace_back("{\"type\":\"click\"}\n");
        return true;
    }
    for (auto& mod : mods)
        if (mod.ready && mod.events.size() < maxEvents)
            mod.events.emplace_back("{\"type\":\"click\"}\n");
    return false;
}

void Mods::event(const std::string& id, std::string json) {
    for (auto& mod : mods) {
        if (mod.id != id || !mod.ready || mod.events.size() >= maxEvents) continue;
        mod.events.push_back(std::move(json));
        return;
    }
}

void Mods::relationship(int value) {
    if (tier == value) return;
    tier = value;
    for (auto& mod : mods)
        if (mod.ready && mod.events.size() < maxEvents)
            mod.events.push_back("{\"type\":\"relationship\",\"tier\":" + std::to_string(tier) + "}\n");
}

void Mods::menuSelected(const std::string& mod, const std::string& item) {
    event(mod, "{\"type\":\"pet_menu\",\"id\":" + jsonString(item) + "}\n");
}

void Mods::dialogAnswered(const std::string& mod, const ModDialog& dialog, const std::string& button) {
    std::string data = "{\"type\":\"dialog\",\"id\":" + jsonString(dialog.id) +
                       ",\"button\":" + (button.empty() ? "null" : jsonString(button)) + ",\"fields\":{";
    bool first = true;
    for (const auto& field : dialog.fields) {
        if (!first) data += ',';
        data += jsonString(field.id) + ':' + jsonString(narrow(field.value));
        first = false;
    }
    event(mod, std::move(data) + "}}\n");
}

void Mods::activateControl(size_t index, size_t control) {
    if (index >= mods.size()) return;
    auto& mod = mods[index];
    if (!mod.ready || control >= mod.controls.size() || mod.events.size() >= maxEvents) return;
    auto& item = mod.controls[control];
    std::string value = "null";
    if (item.toggle) {
        item.value = !item.value;
        value = item.value ? "true" : "false";
    }
    mod.events.push_back("{\"type\":\"control\",\"id\":\"" + item.id + "\",\"value\":" + value + "}\n");
    ++changes;
}

void Mods::tick(const Host& host) {
    for (auto& mod : mods)
        if (mod.process) tick(mod, host);
}

void Mods::tick(Mod& mod, const Host& host) {
    DWORD available = 0;
    if (PeekNamedPipe(mod.output, nullptr, 0, nullptr, &available, nullptr) && available) {
        char buf[maxMessage];
        DWORD read = 0;
        if (ReadFile(mod.output, buf, std::min(available, maxMessage), &read, nullptr)) {
            mod.received.append(buf, read);
            size_t from = 0;
            for (size_t end; (end = mod.received.find('\n', from)) != std::string::npos; from = end + 1) {
                if (end - from >= maxMessage) {
                    fail(mod, L"Слишком длинное сообщение от Python-мода.");
                    return;
                }
                message(mod, mod.received.substr(from, end - from), host);
                if (!mod.process) return;
            }
            mod.received.erase(0, from);
            if (mod.received.size() >= maxMessage) {
                fail(mod, L"Некорректный ответ от Python-мода.");
                return;
            }
        }
    } else if (WaitForSingleObject(mod.process, 0) == WAIT_OBJECT_0) {
        DWORD code = 0;
        GetExitCodeProcess(mod.process, &code);
        fail(mod, L"Процесс Python завершился. Код: " + std::to_wstring(code));
        return;
    }
    if (mod.deadline && nowSec() >= mod.deadline) {
        fail(mod, L"Python-мод не ответил за 10 секунд.");
        return;
    }
    if (mod.ready && !mod.deadline && !mod.events.empty()) {
        const auto& event = mod.events.front();
        DWORD written = 0;
        if (!WriteFile(mod.input, event.data(), (DWORD)event.size(), &written, nullptr) || written != event.size()) {
            fail(mod, L"Не удалось отправить событие Python-моду.");
            return;
        }
        mod.events.pop_front();
        mod.deadline = nowSec() + 10;
    }
}

void Mods::message(Mod& mod, const std::string& data, const Host& host) {
    bool ok = false;
    Json msg = Json::parse(data, &ok);
    if (!ok || msg.t != Json::Obj) return;
    auto type = msg["type"].str();
    if (type == "ready" && !mod.ready) {
        mod.ready = true;
        mod.deadline = 0;
        mod.events.push_back("{\"type\":\"relationship\",\"tier\":" + std::to_string(tier) + "}\n");
        ++changes;
    } else if (type == "done" && mod.ready) {
        mod.deadline = 0;
    } else if (type == "error") {
        fail(mod, widen(msg["text"].str("Python error")));
    } else if (type == "say") {
        auto text = textValue(msg["text"], 1024);
        if (!text.empty() && host.say) host.say(text, msg["interrupt"].flag());
    } else if (type == "pet_menu") {
        auto id = msg["id"].str();
        auto label = textValue(msg["label"], 80);
        if (!isId(id) || label.empty()) return;
        auto icon = msg["icon"].str();
        auto dot = icon.rfind('.');
        std::wstring path;
        if (dot != std::string::npos && isId(icon.substr(0, dot)) &&
            (icon.substr(dot) == ".svg" || icon.substr(dot) == ".png")) {
            path = mod.folder + L"\\" + widen(icon);
        }
        auto item = std::find_if(mod.menu.begin(), mod.menu.end(), [&](const auto& m) { return m.id == id; });
        Mod::MenuItem menu{std::move(id), std::move(label), std::move(path)};
        if (item != mod.menu.end()) *item = std::move(menu);
        else if (mod.menu.size() < maxControls) mod.menu.push_back(std::move(menu));
    } else if (type == "show_dialog") {
        ModDialog dialog;
        if (host.showDialog && readDialog(msg, dialog)) host.showDialog(mod.id, std::move(dialog));
    } else if (type == "update_dialog") {
        auto id = msg["id"].str();
        if (host.updateDialog && isId(id)) host.updateDialog(mod.id, id, textValue(msg["status"], 240));
    } else if (type == "close_dialog") {
        auto id = msg["id"].str();
        if (host.closeDialog && isId(id)) host.closeDialog(mod.id, id);
    } else if (type == "pace") {
        float speed = msg["speed"].f();
        mod.paceSpeed = speed > 0 ? clampf(speed, 0.5f, 3.f) : 0;
        mod.paceAcrossMonitors = msg["across_monitors"].flag();
    } else if (type == "capture_clicks") {
        mod.captureClicks = msg["enabled"].flag();
    } else if (type == "button" || type == "toggle") {
        auto id = msg["id"].str();
        auto label = msg["label"].str();
        if (!isId(id) || label.empty() || label.size() > 160 || label.find('\0') != std::string::npos) return;
        auto item = std::find_if(mod.controls.begin(), mod.controls.end(), [&](const Control& c) { return c.id == id; });
        Control control{id, widen(label), type == "toggle", msg["value"].flag()};
        if (item != mod.controls.end()) *item = std::move(control);
        else if (mod.controls.size() < maxControls) mod.controls.push_back(std::move(control));
        else return;
        ++changes;
    }
}
