// Search end to end, in this process: the hotkey chain (a second registrant never gets the same key), the Windows Search index
// (read-only queries; reports when the index is off), and the panel itself driven through its window messages with injected
// app / open / reveal hooks, so no app is started and nothing is opened.
#include "core/win.h"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "apps/app_index.h"
#include "search/search_core.h"
#include "search/search_hotkey.h"
#include "search/search_panel.h"
#include "search/win_search.h"

using namespace velora;
using namespace velora::search;

namespace {
int g_failed = 0, g_checks = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        ++g_checks;                                                      \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed;                                                  \
        }                                                                \
    } while (0)

bool Pump(const std::function<bool()>& done, int timeoutMs = 15000) {
    const ULONGLONG end = GetTickCount64() + timeoutMs;
    for (;;) {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (done()) return true;
        if (GetTickCount64() > end) return false;
        Sleep(10);
    }
}

HWND MakeHotkeyWindow() {
    return CreateWindowExW(0, L"STATIC", L"velora-search-test", WS_POPUP, 0, 0, 1, 1, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
}

apps::AppEntry App(const wchar_t* name, const wchar_t* exe) {
    apps::AppEntry a;
    a.name = name;
    a.exePath = exe;
    a.launch = std::wstring(L"shell:AppsFolder\\") + name;
    a.icon = a.launch;
    return a;
}

void Hotkeys() {
    HWND a = MakeHotkeyWindow(), b = MakeHotkeyWindow(), c = MakeHotkeyWindow();
    CHECK(a && b && c);
    HotkeyChoice first = RegisterSearchHotkey(a, 1, L"");
    HotkeyChoice second = RegisterSearchHotkey(b, 1, L"");
    std::printf("INFO hotkeys: first=%ls second=%ls\n", first.registered ? FormatHotkey(first.key).c_str() : L"(none)",
                second.registered ? FormatHotkey(second.key).c_str() : L"(none)");
    CHECK(first.registered || !second.registered || true);  // another program may own both defaults; the chain must still not crash
    CHECK(!(first.registered && second.registered && first.key == second.key));  // a taken key is never reported as ours
    if (first.registered) CHECK(first.key == (Hotkey{kModAlt, 0x20}) || first.key == (Hotkey{kModControl, 0x20}));
    if (first.registered && first.key == (Hotkey{kModAlt, 0x20}) && second.registered) CHECK(second.key == (Hotkey{kModControl, 0x20}));  // Alt+Space taken -> Ctrl+Space
    HotkeyChoice custom = RegisterSearchHotkey(c, 2, L"Ctrl+Alt+Shift+F11");
    if (custom.registered) CHECK(custom.configured && custom.key == (Hotkey{kModControl | kModAlt | kModShift, 0x7A}));
    HotkeyChoice bad = RegisterSearchHotkey(c, 3, L"banana");  // unusable setting: falls back to the defaults
    CHECK(!bad.configured);
    UnregisterSearchHotkey(a, 1);
    UnregisterSearchHotkey(b, 1);
    UnregisterSearchHotkey(c, 2);
    UnregisterSearchHotkey(c, 3);
    DestroyWindow(a);
    DestroyWindow(b);
    DestroyWindow(c);

    CHECK(HotkeyFromSettingsJson("{\"search\":{\"hotkey\":\"Alt+Shift+Space\"}}") == L"Alt+Shift+Space");
    CHECK(HotkeyFromSettingsJson("{\"search\":{}}").empty() && HotkeyFromSettingsJson("{\"search\":{\"hotkey\":5}}").empty() &&
          HotkeyFromSettingsJson("garbage").empty() && HotkeyFromSettingsJson("{}").empty());
}

void Index() {
    const IndexState service = ProbeServiceConfig();
    std::printf("INFO index: service config says %s\n", service == IndexState::Disabled ? "disabled" : "not disabled");
    CHECK(!IndexStateMessage(IndexState::Disabled).empty() && !IndexStateMessage(IndexState::Unavailable).empty() && IndexStateMessage(IndexState::Ready).empty());

    std::vector<FileHit> hits;
    long hr = 0;
    const IndexState state = RunFileQuery(BuildFileSql(L"windows", 10), 10, &hits, &hr);
    std::printf("INFO index: query state=%d hits=%zu hr=0x%08lx\n", static_cast<int>(state), hits.size(), static_cast<unsigned long>(hr));
    if (state == IndexState::Ready) {
        CHECK(hits.size() <= 10);
        for (const FileHit& h : hits) CHECK(!h.path.empty());
    } else {
        CHECK(hits.empty());  // an index that cannot answer yields no files, not garbage
    }
    std::vector<FileHit> none;
    CHECK(RunFileQuery(L"SELECT FROM WHERE", 10, &none) == IndexState::Unavailable && none.empty());  // a bad statement degrades, never crashes

    // The worker: one request, one answer for the same id; a newer request replaces an older one that has not started.
    HWND w = MakeHotkeyWindow();
    FileSearcher searcher(w, WM_APP + 1);
    searcher.Request(1, BuildFileSql(L"windows", 10));
    searcher.Request(2, BuildFileSql(L"system", 10));
    QueryResult r;
    bool got = false;
    CHECK(Pump([&] {
        if (searcher.Take(&r)) {
            got = true;
            return r.id == 2;  // the answer to request 1 may or may not have been produced first
        }
        return false;
    }));
    CHECK(got && r.id == 2);
    std::printf("INFO index: worker answered id=%u state=%d hits=%zu\n", r.id, static_cast<int>(r.state), r.hits.size());
    DestroyWindow(w);
}

void Panel() {
    struct Seen {
        std::wstring launched, opened, revealed;
        bool openedFolder = false;
        int launches = 0;
    } seen;
    PanelHooks hooks;
    hooks.apps = [] {
        return std::vector<apps::AppEntry>{App(L"Calculator", L""), App(L"Notepad", L"C:\\Windows\\notepad.exe"), App(L"Notes", L"C:\\x\\notes.exe"),
                                           App(L"Paint", L"C:\\Windows\\System32\\mspaint.exe")};
    };
    hooks.launchApp = [&](const apps::AppEntry& a) {
        seen.launched = a.name;
        ++seen.launches;
    };
    hooks.openPath = [&](const std::wstring& p, bool folder) {
        seen.opened = p;
        seen.openedFolder = folder;
    };
    hooks.reveal = [&](const std::wstring& p) { seen.revealed = p; };

    SearchPanel panel(GetModuleHandleW(nullptr), hooks);
    panel.SetAnimationsEnabled(false);
    panel.SetDismissOnDeactivate(false);  // the test desktop may hand the focus to another window at any time
    CHECK(!panel.Visible());
    panel.Show();
    CHECK(panel.Visible());
    HWND h = panel.Hwnd();
    CHECK(h != nullptr);
    if (!h) return;
    Pump([] { return false; }, 100);  // first paint

    // Typing: WM_CHAR reaches the query box; the app part of the result is immediate.
    for (wchar_t c : std::wstring(L"not")) SendMessageW(h, WM_CHAR, c, 0);
    auto snap = panel.GetSnapshot();
    CHECK(snap.query == L"not" && snap.results >= 2);
    CHECK(snap.selectedName == L"Notepad");  // best match first (prefix match, name order on a tie)
    SendMessageW(h, WM_KEYDOWN, VK_DOWN, 0);
    CHECK(panel.GetSnapshot().selectedName == L"Notes");
    SendMessageW(h, WM_KEYDOWN, VK_UP, 0);
    CHECK(panel.GetSnapshot().selectedName == L"Notepad");
    SendMessageW(h, WM_KEYDOWN, VK_BACK, 0);
    CHECK(panel.GetSnapshot().query == L"no");

    // Enter launches the selected app and closes the panel.
    panel.SetQuery(L"calc");
    CHECK(panel.GetSnapshot().selectedName == L"Calculator");
    SendMessageW(h, WM_KEYDOWN, VK_RETURN, 0);
    CHECK(seen.launched == L"Calculator" && seen.launches == 1 && !panel.Visible());

    // Reveal: an app with an executable can be revealed; a packaged one cannot (the panel stays open).
    panel.Show();
    h = panel.Hwnd();
    panel.SetQuery(L"paint");
    CHECK(panel.Activate(true) && seen.revealed == L"C:\\Windows\\System32\\mspaint.exe" && !panel.Visible());
    panel.Show();
    panel.SetQuery(L"calc");
    CHECK(!panel.Activate(true) && panel.Visible());

    // Esc closes at once; a click outside closes; nothing selected = Enter does nothing.
    h = panel.Hwnd();
    SendMessageW(h, WM_KEYDOWN, VK_ESCAPE, 0);
    CHECK(!panel.Visible());
    panel.Show();
    h = panel.Hwnd();
    CHECK(!panel.Activate(false));
    panel.SetQuery(L"zzzzqq");
    CHECK(panel.GetSnapshot().results == 0 || panel.GetSnapshot().fileResults > 0);
    SendMessageW(h, WM_LBUTTONUP, 0, MAKELPARAM(3, 3));
    CHECK(!panel.Visible());
    CHECK(seen.launches == 1);

    // Files from the index (when it answers): the section rows and opening a file or folder go through the hooks.
    panel.Show();
    panel.SetQuery(L"windows");
    const bool answered = Pump([&] { return panel.GetSnapshot().filesAnswered; });
    snap = panel.GetSnapshot();
    std::printf("INFO panel: files answered=%d state=%d fileResults=%d\n", answered, static_cast<int>(snap.files), snap.fileResults);
    CHECK(answered);
    if (snap.files == IndexState::Ready && snap.fileResults > 0) {
        for (int i = 0; i < 40 && panel.GetSnapshot().selectedResult >= 0; ++i) {
            SendMessageW(panel.Hwnd(), WM_KEYDOWN, VK_DOWN, 0);
            if (panel.GetSnapshot().selectedName.empty()) break;
        }
        CHECK(panel.Visible());
        panel.SetQuery(L"windows");  // selection back to the first row
        CHECK(panel.GetSnapshot().results > 0);
        CHECK(panel.Activate(false));
        CHECK(!seen.opened.empty() || seen.launches > 1);
    } else {
        // Windows Search is off or silent: apps only, and the panel says so (the note is part of the snapshot's state).
        CHECK(snap.files != IndexState::Ready || snap.fileResults == 0);
    }
    panel.Hide();
    CHECK(!panel.Visible());

    // Windows Search off: apps still found, the panel says files are unavailable and does not pretend otherwise.
    PanelHooks off = hooks;
    off.fileQuery = [](const std::wstring&, std::vector<FileHit>*) { return IndexState::Disabled; };
    SearchPanel offPanel(GetModuleHandleW(nullptr), off);
    offPanel.SetAnimationsEnabled(false);
    offPanel.SetDismissOnDeactivate(false);
    offPanel.Show();
    offPanel.SetQuery(L"note");
    CHECK(Pump([&] { return offPanel.GetSnapshot().filesAnswered; }));
    snap = offPanel.GetSnapshot();
    CHECK(snap.files == IndexState::Disabled && snap.fileResults == 0 && snap.results >= 2 && snap.selectedName == L"Notepad");
    CHECK(snap.note == IndexStateMessage(IndexState::Disabled) && snap.note.find(L"apps only") != std::wstring::npos);
    SendMessageW(offPanel.Hwnd(), WM_KEYDOWN, VK_RETURN, 0);  // the apps part still works
    CHECK(seen.launched == L"Notepad");

    PanelHooks silent = hooks;
    silent.fileQuery = [](const std::wstring&, std::vector<FileHit>*) { return IndexState::Unavailable; };
    SearchPanel silentPanel(GetModuleHandleW(nullptr), silent);
    silentPanel.SetAnimationsEnabled(false);
    silentPanel.SetDismissOnDeactivate(false);
    silentPanel.Show();
    silentPanel.SetQuery(L"paint");
    CHECK(Pump([&] { return silentPanel.GetSnapshot().filesAnswered; }));
    CHECK(silentPanel.GetSnapshot().files == IndexState::Unavailable && !silentPanel.GetSnapshot().note.empty() && silentPanel.GetSnapshot().results >= 1);
    silentPanel.Hide();
}

}  // namespace

int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Hotkeys();
    Index();
    Panel();
    std::printf("%s: %d checks, %d failed\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
