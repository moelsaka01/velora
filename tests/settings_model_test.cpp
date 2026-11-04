// Settings store and page definitions (pure; uses a temp folder, no windows).
#include <cstdio>
#include <filesystem>
#include <set>

#include "core/win.h"
#include "settings/settings_model.h"
#include "theming/theme_policy.h"

using namespace velora;
using namespace velora::settings;

static int g_failed = 0, g_checks = 0;
#define CHECK(c)                                                     \
    do {                                                             \
        ++g_checks;                                                  \
        if (!(c)) {                                                  \
            ++g_failed;                                              \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                            \
    } while (0)

static void Write(const std::filesystem::path& p, const char* text) {
    FILE* f = _wfopen(p.c_str(), L"wb");
    if (f) {
        std::fputs(text, f);
        std::fclose(f);
    }
}

int main() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const auto dir = std::filesystem::path(tmp) / (L"velora-settings-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(dir);
    const auto file = dir / L"settings.json";

    // Missing file = defaults.
    {
        Store s(file);
        CHECK(s.Load());
        CHECK(s.GetBool("theming.enabled", true) && !s.GetBool("apple.enabled", false));
        CHECK(s.GetString("search.hotkey", "Alt+Space") == "Alt+Space");
        CHECK(s.GetList("theming.exclude").empty());
    }
    // Set, save, reload; unknown keys and sibling keys survive.
    Write(file, R"({"dock":{"pinned":["a","b"],"magnification":true},"wallpaper":{"enabled":true},"custom":{"x":1}})");
    {
        Store s(file);
        CHECK(s.Load());
        s.SetBool("theming.corners", false);
        s.SetString("theming.backdrop", "acrylic");
        s.SetList("theming.exclude", {"foo.exe", "bar.exe"});
        s.SetBool("dock.magnification", false);
        s.SetBool("wallpaper.enabled", false);
        CHECK(s.Save());
        CHECK(!std::filesystem::exists(dir / L"settings.json.tmp"));
    }
    {
        Store s(file);
        CHECK(s.Load());
        CHECK(!s.GetBool("theming.corners", true));
        CHECK(s.GetString("theming.backdrop", "mica") == "acrylic");
        CHECK(s.GetList("theming.exclude") == (std::vector<std::string>{"foo.exe", "bar.exe"}));
        CHECK(!s.GetBool("dock.magnification", true) && !s.GetBool("wallpaper.enabled", true));
        CHECK(s.GetList("dock.pinned") == (std::vector<std::string>{"a", "b"}));  // the Dock's own key is kept
        const json::Value* custom = s.Root().Find("custom");
        CHECK(custom && custom->Find("x") && custom->Find("x")->AsInt() == 1);
    }
    // The theming policy reads exactly what the Settings page writes.
    {
        Store s(file);
        s.Load();
        theming::Config c = theming::ConfigFromSettings(s.Root());
        CHECK(!c.corners && c.caption && c.backdrop == theming::Backdrop::Acrylic);
        CHECK(c.excludedApps.size() == 2 && c.excludedApps[0] == L"foo.exe");
    }
    // A broken file is reported, not silently overwritten until Save.
    Write(file, "{ not json");
    {
        Store s(file);
        CHECK(!s.Load());
        CHECK(s.GetBool("theming.enabled", true));  // still usable with defaults
        const std::string before = "{ not json";
        FILE* f = _wfopen(file.c_str(), L"rb");
        char buf[64] = {};
        if (f) {
            std::fread(buf, 1, sizeof buf - 1, f);
            std::fclose(f);
        }
        CHECK(before == buf);  // untouched by Load
        s.SetBool("theming.enabled", false);
        CHECK(s.Save());
        Store again(file);
        CHECK(again.Load() && !again.GetBool("theming.enabled", true));
    }
    // A key that is a scalar where an object is needed is replaced, not crashed on.
    Write(file, R"({"theming":5})");
    {
        Store s(file);
        CHECK(s.Load());
        s.SetBool("theming.enabled", false);
        CHECK(!s.GetBool("theming.enabled", true));
    }

    // Pages: every keyed item has a default and a sane shape; choice defaults exist; dependencies point at toggles on the same page.
    {
        const auto pages = BuildPages();
        CHECK(pages.size() >= 8);
        std::set<std::string> keys;
        for (const auto& p : pages) {
            CHECK(!p.title.empty() && !p.items.empty());
            std::set<std::string> pageToggles;
            for (const auto& i : p.items)
                if (i.kind == ItemKind::Toggle) pageToggles.insert(i.key);
            for (const auto& i : p.items) {
                if (i.kind == ItemKind::Toggle || i.kind == ItemKind::Choice || i.kind == ItemKind::List) {
                    CHECK(!i.key.empty() && !i.label.empty());
                    CHECK(keys.insert(i.key).second);  // no key twice
                }
                if (i.kind == ItemKind::Choice) {
                    bool hasDefault = false;
                    for (const auto& c : i.choices) hasDefault = hasDefault || c.value == i.defString;
                    CHECK(!i.choices.empty() && hasDefault);
                }
                if (i.kind == ItemKind::Button) CHECK(!i.action.empty());
                if (!i.dependsOn.empty()) CHECK(pageToggles.count(i.dependsOn) == 1);
            }
        }
        for (const char* k : {"theming.enabled", "theming.exclude", "search.hotkey", "gaming.mode", "apple.enabled", "updates.channel", "wallpaper.enabled"})
            CHECK(keys.count(k) == 1);
        // ItemEnabled / ChoiceIndex.
        Store s(file);
        s.Load();
        s.SetBool("theming.enabled", false);
        for (const auto& p : pages)
            for (const auto& i : p.items) {
                if (i.key == "theming.corners") CHECK(!ItemEnabled(s, i));
                if (i.key == "search.hotkey") {
                    CHECK(ItemEnabled(s, i) && ChoiceIndex(s, i) == 0);
                    s.SetString("search.hotkey", "Ctrl+Space");
                    CHECK(ChoiceIndex(s, i) == 1);
                    s.SetString("search.hotkey", "garbage");
                    CHECK(ChoiceIndex(s, i) == 0);  // unknown value shows the default
                }
            }
    }
    CHECK(NormalizeExeName("  Notepad++  ") == "notepad++.exe");
    CHECK(NormalizeExeName("\"Chrome.EXE\"") == "chrome.exe");
    CHECK(NormalizeExeName("app.v2") == "app.v2");
    CHECK(NormalizeExeName("").empty() && NormalizeExeName("  ").empty());
    CHECK(NormalizeExeName("c:\\x\\y.exe").empty() && NormalizeExeName("a*b").empty() && NormalizeExeName("a|b").empty());
    CHECK(NormalizeExeName(std::string(200, 'a')).empty());

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::printf("%s: %d checks, %d failed\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
