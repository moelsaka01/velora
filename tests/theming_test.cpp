// Window theming: the pure policy (exhaustive), the settings parser, and the real thing against a window that
// belongs to ANOTHER process (this exe started with --child): apply, read back, restore, simulated shell crash
// followed by RestoreFromBackup(). Opens a window, so it runs in CI (and on the developer machine only on request).
//
// The cross-process outcome is printed ("INFO ...") so the CI log records what Windows accepted. Invariants that
// must hold either way are asserted: restore returns every attribute to its original value, the backup file exists
// while something is themed and is gone afterwards, excluded / elevated / game windows are never touched.
#include <dwmapi.h>

#include <cstdio>
#include <filesystem>
#include <string>

#include "core/json.h"
#include "core/runtime.h"
#include "core/win.h"
#include "theming/theme_policy.h"
#include "theming/window_theme.h"

using namespace velora;
using namespace velora::theming;

namespace {

int g_checks = 0, g_failed = 0;
#define CHECK(c)                                                                  \
    do {                                                                          \
        ++g_checks;                                                               \
        if (!(c)) {                                                               \
            ++g_failed;                                                           \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);              \
        }                                                                         \
    } while (0)

constexpr wchar_t kClass[] = L"VeloraThemingTestWindow";

LRESULT CALLBACK ChildProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int RunChild() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = ChildProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    HWND h = CreateWindowExW(0, kClass, L"theming child", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 400, 300, nullptr, nullptr, wc.hInstance, nullptr);
    if (!h) return 2;
    SetTimer(h, 1, 60000, nullptr);  // safety: never outlive a crashed parent by long
    MSG m;
    DWORD parentDeadline = GetTickCount() + 60000;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
        if (GetTickCount() > parentDeadline) break;
    }
    return 0;
}

WindowFacts Good() {
    WindowFacts f;
    f.exeName = L"notepad.exe";
    f.exePath = L"c:\\windows\\system32\\notepad.exe";
    f.className = L"Notepad";
    f.style = WS_OVERLAPPEDWINDOW;
    f.exStyle = 0;
    return f;
}

void PolicyTests() {
    const Config on;
    CHECK(Evaluate(Good(), on) == Skip::None);
    {
        Config c;
        c.enabled = false;
        CHECK(Evaluate(Good(), c) == Skip::Disabled);
    }
    auto with = [&](auto mutate) {
        WindowFacts f = Good();
        mutate(f);
        return Evaluate(f, on);
    };
    CHECK(with([](WindowFacts& f) { f.ownProcess = true; }) == Skip::OwnWindow);
    CHECK(with([](WindowFacts& f) { f.uncontrollable = true; }) == Skip::Uncontrollable);
    CHECK(with([](WindowFacts& f) { f.fullscreen = true; }) == Skip::Fullscreen);
    CHECK(with([](WindowFacts& f) { f.cloaked = true; }) == Skip::Cloaked);
    CHECK(with([](WindowFacts& f) { f.visible = false; }) == Skip::Hidden);
    CHECK(with([](WindowFacts& f) { f.className = L"Shell_TrayWnd"; }) == Skip::ShellWindow);
    CHECK(with([](WindowFacts& f) { f.className = L"Progman"; }) == Skip::ShellWindow);
    CHECK(with([](WindowFacts& f) { f.exStyle = WS_EX_TOOLWINDOW; }) == Skip::ToolWindow);
    CHECK(with([](WindowFacts& f) { f.style = WS_POPUP; }) == Skip::NoFrame);
    CHECK(with([](WindowFacts& f) { f.style = WS_POPUP | WS_THICKFRAME; }) == Skip::None);  // resizable frame counts
    CHECK(with([](WindowFacts& f) { f.exeName = L"easyanticheat.exe"; }) == Skip::AntiCheat);
    CHECK(with([](WindowFacts& f) { f.exeName = L"vgc.exe"; }) == Skip::AntiCheat);
    CHECK(with([](WindowFacts& f) { f.className = L"UnityWndClass"; }) == Skip::Game);
    CHECK(with([](WindowFacts& f) { f.className = L"UnrealWindow"; }) == Skip::Game);
    CHECK(with([](WindowFacts& f) { f.exePath = L"d:\\steamlibrary\\steamapps\\common\\somegame\\game.exe"; }) == Skip::Game);
    CHECK(with([](WindowFacts& f) { f.exePath = L"c:\\program files\\epic games\\fortnite\\fortnite.exe"; }) == Skip::Game);
    CHECK(with([](WindowFacts& f) { f.exePath = L"c:\\program files\\notepad++\\notepad++.exe"; }) == Skip::None);
    // Precedence: an elevated game is reported as elevated (never touched either way), disabled wins over everything.
    {
        WindowFacts f = Good();
        f.uncontrollable = true;
        f.className = L"UnityWndClass";
        CHECK(Evaluate(f, on) == Skip::Uncontrollable);
    }
    {
        Config c;
        c.excludedApps.push_back(L"notepad.exe");
        CHECK(Evaluate(Good(), c) == Skip::Excluded);
        WindowFacts f = Good();
        f.exeName = L"calc.exe";
        CHECK(Evaluate(f, c) == Skip::None);
        f.exeName.clear();  // unreadable exe name: cannot match an exclusion, still themed by the generic rules
        CHECK(Evaluate(f, c) == Skip::None);
    }
    CHECK(Lower(L"Notepad++.EXE") == L"notepad++.exe");
    CHECK(std::string(SkipName(Skip::Game)) == "game");
    const Colours d = NeutralColours(true), l = NeutralColours(false);
    CHECK(d.caption != l.caption && (d.caption & 0xff) < (l.caption & 0xff));  // dark caption darker than the light one
}

void ConfigTests() {
    {
        Config c = ConfigFromSettings(json::Value{});
        CHECK(c.enabled && c.corners && c.caption && c.backdrop == Backdrop::Mica && c.excludedApps.empty());
    }
    auto doc = json::Parse(R"({"theming":{"enabled":false,"corners":false,"caption":false,"backdrop":"acrylic","exclude":["Foo.EXE","bar.exe","foo.exe",5]}})");
    CHECK(doc.has_value());
    Config c = ConfigFromSettings(*doc);
    CHECK(!c.enabled && !c.corners && !c.caption && c.backdrop == Backdrop::Acrylic);
    CHECK(c.excludedApps.size() == 2 && c.excludedApps[0] == L"foo.exe" && c.excludedApps[1] == L"bar.exe");
    auto bad = json::Parse(R"({"theming":{"backdrop":"glass","enabled":"yes","exclude":"x"}})");
    CHECK(bad.has_value());
    Config d = ConfigFromSettings(*bad);
    CHECK(d.enabled && d.backdrop == Backdrop::Mica && d.excludedApps.empty());  // malformed values keep defaults
    // Round trip.
    Config r = ConfigFromSettings([&] {
        json::Value root;
        root.Set("theming", ConfigToJson(c));
        return root;
    }());
    CHECK(r.enabled == c.enabled && r.backdrop == c.backdrop && r.excludedApps == c.excludedApps);
}

struct Snapshot {
    DWORD v[6] = {};
    bool ok[6] = {};
};
constexpr DWORD kIds[6] = {33, 20, 34, 35, 36, 38};

Snapshot Read(HWND h) {
    Snapshot s;
    for (int i = 0; i < 6; ++i) s.ok[i] = SUCCEEDED(DwmGetWindowAttribute(h, kIds[i], &s.v[i], sizeof(DWORD)));
    return s;
}

bool Same(const Snapshot& a, const Snapshot& b) {
    for (int i = 0; i < 6; ++i)
        if (a.ok[i] != b.ok[i] || (a.ok[i] && a.v[i] != b.v[i])) return false;
    return true;
}

bool WaitFor(const wchar_t* cls, HWND* out) {
    for (int i = 0; i < 100; ++i) {
        *out = FindWindowW(cls, nullptr);
        if (*out) return true;
        Sleep(100);
    }
    return false;
}

void SystemTests(const std::wstring& self) {
    PROCESS_INFORMATION pi{};
    STARTUPINFOW si{sizeof si};
    std::wstring cmd = L"\"" + self + L"\" --child";
    CHECK(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi));
    if (!pi.hProcess) return;
    HWND win = nullptr;
    CHECK(WaitFor(kClass, &win));
    if (!win) {
        TerminateProcess(pi.hProcess, 0);  // the test's own child
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return;
    }
    DWORD wpid = 0;
    GetWindowThreadProcessId(win, &wpid);
    CHECK(wpid == pi.dwProcessId && wpid != GetCurrentProcessId());  // genuinely another process

    const Snapshot original = Read(win);
    std::printf("INFO original corner=%d%s dark=%d%s backdrop=%d%s\n", original.ok[0] ? (int)original.v[0] : -1, original.ok[0] ? "" : "(unreadable)",
                original.ok[1] ? (int)original.v[1] : -1, original.ok[1] ? "" : "(unreadable)", original.ok[5] ? (int)original.v[5] : -1,
                original.ok[5] ? "" : "(unreadable)");

    // ---- apply + restore -------------------------------------------------------------------------------------------
    {
        WindowThemer t;
        Config c;  // defaults: corners, caption, Mica
        t.Configure(c, true);
        const Skip s = t.Apply(win, false, false);
        CHECK(s == Skip::None);
        const auto st = t.GetStats();
        std::printf("INFO cross-process apply: themed=%d changed=%d ignored=%d refused=%d unreadable=%d\n", st.themed, st.attributesChanged,
                    st.attributesIgnored, st.attributesRefused, st.attributesUnreadable);
        const Snapshot themed = Read(win);
        std::printf("INFO themed corner=%d dark=%d border=%08x caption=%08x backdrop=%d\n", themed.ok[0] ? (int)themed.v[0] : -1,
                    themed.ok[1] ? (int)themed.v[1] : -1, themed.ok[2] ? themed.v[2] : 0, themed.ok[3] ? themed.v[3] : 0, themed.ok[5] ? (int)themed.v[5] : -1);
        if (t.IsThemed(win)) {
            CHECK(std::filesystem::exists(BackupPath()));  // written before Apply returned
            CHECK(!Same(original, themed));
        }
        // A window excluded later is put back at once.
        Config ex = c;
        ex.excludedApps.push_back(L"theming_test.exe");
        t.Configure(ex, true);
        CHECK(!t.IsThemed(win));
        CHECK(Same(original, Read(win)));
        CHECK(t.Apply(win, false, false) == Skip::Excluded);
        t.Configure(c, true);
        t.Apply(win, false, false);
        // The theme flips light: attributes follow without losing the original.
        t.Configure(c, false);
        const int n = t.RestoreAll();
        std::printf("INFO RestoreAll restored %d window(s)\n", n);
        CHECK(Same(original, Read(win)));
        CHECK(!std::filesystem::exists(BackupPath()));
        // Elevated / game windows are never touched.
        CHECK(t.Apply(win, true, false) == Skip::Uncontrollable);
        CHECK(t.Apply(win, false, true) == Skip::Fullscreen);
        CHECK(Same(original, Read(win)));
        Config off = c;
        off.enabled = false;
        t.Configure(off, true);
        CHECK(t.Apply(win, false, false) == Skip::Disabled);
        CHECK(Same(original, Read(win)));
    }

    // ---- simulated shell crash: the themer is gone without restoring; recovery repairs from the backup file ---------
    {
        Config c;
        c.backdrop = Backdrop::None;  // exercises the colour attributes as well
        bool themedSomething = false;
        {
            auto* t = new WindowThemer;  // never destroyed through RestoreAll: the "crash"
            t->Configure(c, true);
            t->Apply(win, false, false);
            themedSomething = t->IsThemed(win);
            // leak on purpose (a crash does not run destructors; the object holds no OS handles)
            std::printf("INFO crash scenario: themed=%d\n", themedSomething ? 1 : 0);
        }
        if (themedSomething) {
            CHECK(std::filesystem::exists(BackupPath()));
            CHECK(!Same(original, Read(win)));
        }
        const int n = RestoreFromBackup();
        CHECK(themedSomething ? n == 1 : n == 0);
        CHECK(Same(original, Read(win)));
        CHECK(!std::filesystem::exists(BackupPath()));
        CHECK(RestoreFromBackup() == 0);  // idempotent
    }

    // ---- an app that changed an attribute itself is left alone on restore ---------------------------------------------
    {
        WindowThemer t;
        Config c;
        t.Configure(c, true);
        t.Apply(win, false, false);
        DWORD corner = 0;
        if (t.IsThemed(win) && SUCCEEDED(DwmGetWindowAttribute(win, 33, &corner, sizeof corner)) && corner == 2) {
            DWORD mine = 3;  // DWMWCP_ROUNDSMALL, set by "the app" after Velora's round
            if (SUCCEEDED(DwmSetWindowAttribute(win, 33, &mine, sizeof mine))) {
                t.RestoreAll();
                DWORD now = 0;
                CHECK(SUCCEEDED(DwmGetWindowAttribute(win, 33, &now, sizeof now)) && now == mine);  // the app's value is kept
                DWORD def = 0;
                DwmSetWindowAttribute(win, 33, &def, sizeof def);
            }
        }
        t.RestoreAll();
        RestoreFromBackup();
        CHECK(Same(original, Read(win)));
    }

    PostMessageW(win, WM_CLOSE, 0, 0);
    DestroyWindow(win);  // not ours; no effect cross-process, WM_CLOSE does the work
    WaitForSingleObject(pi.hProcess, 3000);
    TerminateProcess(pi.hProcess, 0);  // only the child this test started
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc > 1 && std::wstring(argv[1]) == L"--child") return RunChild();
    SetEnvironmentVariableW(L"VELORA_TEST_PROFILE", L"themingtest");
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring dir = std::wstring(tmp) + L"velora-theming-test-" + std::to_wstring(GetCurrentProcessId());
    std::filesystem::create_directories(dir);
    SetEnvironmentVariableW(L"VELORA_DATA_DIR", dir.c_str());

    PolicyTests();
    ConfigTests();
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    if (GetEnvironmentVariableW(L"VELORA_THEMING_NO_WINDOW", nullptr, 0)) std::printf("INFO system tests skipped (VELORA_THEMING_NO_WINDOW)\n");
    else SystemTests(self);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::printf("%s: %d checks, %d failed\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
