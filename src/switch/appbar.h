// Appbar registration so maximized windows respect Velora's bars. Owner: A.
//
// Registration order and the explicit edge adjustment after ABM_QUERYPOS follow
// cairoshell/ManagedShell AppBarManager.ABSetPos (Apache-2.0, see
// THIRD_PARTY_NOTICES.md). Day 2 uses this for a placeholder top bar; the real
// menu bar and Dock (owner B) will reuse the class.
#pragma once

#include <cstdint>

#include "core/contracts/state.h"
#include "core/win.h"

namespace velora::sw {

enum class Edge { Top, Bottom };

class AppBar {
public:
    AppBar() = default;
    AppBar(const AppBar&) = delete;
    AppBar& operator=(const AppBar&) = delete;
    ~AppBar() { Destroy(); }

    // Creates the window and registers it. Returns false if registration failed
    // (e.g. no Explorer tray); the window is then destroyed (caller degrades).
    bool Create(HINSTANCE instance, HMONITOR monitor, Edge edge, float thicknessDip, std::uint32_t argb);
    void Destroy();  // ABM_REMOVE + DestroyWindow; idempotent

    // After Explorer restarted: registrations were lost, register again.
    bool Reregister();
    // Monitor/DPI change.
    void Reposition();

    bool IsRegistered() const { return registered_; }
    int RegistrationCount() const { return registrations_; }
    HWND Hwnd() const { return hwnd_; }
    RECT Rect() const { return rect_; }

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

private:
    LRESULT OnMessage(UINT, WPARAM, LPARAM);
    bool Register();
    void Unregister();
    void SetPos();

    HWND hwnd_ = nullptr;
    HMONITOR monitor_ = nullptr;
    Edge edge_ = Edge::Top;
    float thicknessDip_ = 0;
    HBRUSH brush_ = nullptr;
    UINT callbackMessage_ = 0;
    bool registered_ = false;
    bool inSetPos_ = false;
    int registrations_ = 0;
    RECT rect_{};
};

// Recovery: ABM_REMOVE for appbar windows a dead shell registered (Explorer may
// not have noticed the process died). Skips handles that are now live windows of
// another process. Returns the number of removals sent.
int RemoveRecordedAppbars(const SwitchState& state);

}  // namespace velora::sw
