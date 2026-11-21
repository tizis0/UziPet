#pragma once
#include <string>
#include <vector>

struct ModDialog {
    struct Field {
        std::string id;
        std::wstring label, value;
        bool integer = false;
        size_t length = 24;
    };
    struct Button {
        std::string id;
        std::wstring label;
    };
    std::string id, cancel;
    std::wstring text, status;
    std::vector<Field> fields;
    std::vector<Button> buttons;
};
