// Asynchronous front of the global app menus. Owner: B (with C on Day 4).
//
// Reading another process' menus can be slow or can hang (UI Automation round trips, an app that does not answer
// WM_INITMENUPOPUP), so it never happens on the shell's UI thread: requests go to one worker thread (multithreaded COM apartment),
// answers come back through a window message and are collected with Take(). Answers carry the request id and the window they were
// made for, so the menu bar can drop the ones that are no longer current.
#pragma once

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "menubar/app_menu.h"

namespace velora::menubar {

struct MenuAnswer {
    enum class Kind { Bar, Items } kind = Kind::Bar;
    unsigned id = 0;
    HWND hwnd = nullptr;
    bool ok = false;
    AppMenuBar bar;                 // Kind::Bar
    std::vector<AppMenuItem> items;  // Kind::Items
    int titleIndex = 0;              // Kind::Items: which title / submenu path the items belong to
    std::vector<int> path;
};

class AppMenuProvider {
public:
    // `notify` receives `message` whenever an answer is ready; the answers are collected with Take().
    AppMenuProvider(HWND notify, UINT message);
    ~AppMenuProvider();  // returns at once; a request in flight finishes in the background and is dropped
    AppMenuProvider(const AppMenuProvider&) = delete;
    AppMenuProvider& operator=(const AppMenuProvider&) = delete;

    void SetProbeChromium(bool on);  // opt-in: probing a Chromium window turns its accessibility on (see MayProbeUia)

    // The menu bar of `hwnd`: classic menu first, UI Automation when there is none (cached per window for two minutes).
    unsigned Probe(HWND hwnd);
    // The items under title `titleIndex` (`path`: UIA child indexes of the submenus opened on the way).
    unsigned Expand(HWND hwnd, MenuTier tier, int titleIndex, std::vector<int> path);
    // Runs a command: Win32 `id`, or the UIA item at `path` (checked against `label`). Fire and forget.
    void Invoke(HWND hwnd, MenuTier tier, int titleIndex, std::vector<int> path, unsigned id, std::wstring label);
    void InvokeTitle(HWND hwnd, MenuTier tier, unsigned id, int titleIndex);
    void Forget(HWND hwnd);  // the window is gone: drop what is cached for it

    bool Take(MenuAnswer* out);  // UI thread

private:
    struct State;
    std::shared_ptr<State> state_;
};

}  // namespace velora::menubar
