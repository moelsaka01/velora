// The data behind Velora Settings: the settings.json store (typed dotted-path access, unknown keys kept, atomic save)
// and the page/item definitions. Pure (no windows.h beyond what json needs), unit-tested. Owner: C.
//
// One JSON file, %LOCALAPPDATA%\Velora\settings.json, shared by the shell and the Settings app. Only keys that
// some module reads are listed in the pages; nothing is shown that has no effect.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/json.h"

namespace velora::settings {

enum class ItemKind { Toggle, Choice, Button, List, Note };

struct Choice {
    std::string value;
    std::wstring label;
};

struct Item {
    ItemKind kind = ItemKind::Note;
    std::string key;  // dotted path in settings.json ("" for Button and Note)
    std::wstring label, detail;
    bool defBool = false;
    std::string defString;
    std::vector<Choice> choices;  // Choice
    std::string action;           // Button: restore-windows | check-updates | open-store | ...
    std::string dependsOn;        // a bool key that must be true, otherwise the row is dimmed and inert
};

struct Page {
    std::wstring title;
    std::vector<Item> items;
};

std::vector<Page> BuildPages();

class Store {
public:
    explicit Store(std::filesystem::path file) : file_(std::move(file)) {}
    // Missing file = defaults (true). A file that exists but is not valid JSON is left untouched until the first Save and reported by
    // Load() == false; the in-memory document is then empty so the user can still change settings (Save then replaces the bad file).
    bool Load();
    bool Save() const;

    bool GetBool(const std::string& key, bool def) const;
    std::string GetString(const std::string& key, const std::string& def) const;
    std::vector<std::string> GetList(const std::string& key) const;
    void SetBool(const std::string& key, bool v);
    void SetString(const std::string& key, const std::string& v);
    void SetList(const std::string& key, const std::vector<std::string>& v);

    const json::Value& Root() const { return root_; }
    const std::filesystem::path& File() const { return file_; }

private:
    const json::Value* Lookup(const std::string& key) const;
    void SetValue(const std::string& key, json::Value v);

    std::filesystem::path file_;
    json::Value root_;
};

// Item helpers shared by the window and the tests.
bool ItemEnabled(const Store& s, const Item& item);
int ChoiceIndex(const Store& s, const Item& item);  // index into item.choices of the current value (default when unknown), -1 if none
std::string NormalizeExeName(const std::string& typed);  // "  Notepad++  " -> "notepad++.exe"; "" when unusable

}  // namespace velora::settings
