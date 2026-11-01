#include "switch/switch_manager.h"

#include "core/contracts/ipc.h"
#include "core/log.h"
#include "core/runtime.h"
#include "core/state_store.h"
#include "render/tokens.h"
#include "switch/restore.h"
#include "switch/taskbar.h"

namespace velora::sw {

SwitchManager::SwitchManager(EventBus& bus, HINSTANCE instance) : bus_(bus), instance_(instance) {}

SwitchManager::~SwitchManager() {
    Exit();
    StopWatchdog(0);
}

bool SwitchManager::StartWatchdog() {
    // The previous watchdog must be gone before a new one starts: it owns the pipe
    // name, so a new hello would otherwise be answered by a process that is exiting.
    StopWatchdog(kWatchdogExitWaitMs);

    std::filesystem::path exe = ExeDir() / L"velora-watchdog.exe";
    std::wstring cmd = L"\"" + exe.wstring() + L"\" --watch " + std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    // Detached from our console/job so closing the shell's parent cannot take the watchdog with it.
    DWORD flags = CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS;
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr, &si, &pi)) {
        log::Error("cannot start watchdog: error " + std::to_string(GetLastError()));
        return false;
    }
    CloseHandle(pi.hThread);
    watchdogProcess_ = pi.hProcess;
    state_.watchdogPid = pi.dwProcessId;

    // Armed only once it answers hello: never hide the taskbar unprotected. From here
    // on, any failure terminates this (our own, unconnected) watchdog instead of
    // leaving it behind.
    if (!watchdog_.Connect(WatchdogPipeName(), 5000)) {
        log::Error("watchdog did not open its pipe");
        StopWatchdog(0);
        return false;
    }
    json::Value payload;
    payload.Set("client", "shell");
    payload.Set("pid", static_cast<int>(GetCurrentProcessId()));
    payload.Set("protocol", static_cast<int>(ipc::kProtocolVersion));
    std::string resp;
    if (!watchdog_.Request(ipc::MakeMessage("request", ipc::msg::kHello, 1, payload), &resp, 3000)) {
        log::Error("watchdog did not answer hello");
        StopWatchdog(0);
        return false;
    }
    auto r = json::Parse(resp);
    if (!r || r->Find("status") == nullptr || r->Find("status")->AsInt(-1) != 0) {
        log::Error("watchdog rejected hello: " + resp);
        StopWatchdog(0);
        return false;
    }
    // The emergency hotkey is the way out when everything else is frozen: not optional.
    const json::Value* payloadOut = r->Find("payload");
    const json::Value* hotkey = payloadOut ? payloadOut->Find("hotkey") : nullptr;
    if (!hotkey || !hotkey->AsBool()) {
        lastError_ =
            L"Velora could not start because the emergency restore shortcut (Ctrl+Alt+Shift+Esc) is already in use by "
            L"another program.\n\nClose that program and start Velora again. Windows was not changed.";
        log::Error("watchdog could not register the emergency hotkey Ctrl+Alt+Shift+Esc; refusing to enter Velora Mode");
        Disarm();
        StopWatchdog(kWatchdogExitWaitMs);
        return false;
    }
    return true;
}

// Synchronous (a blocking request with a timeout, not a Post): the watchdog has
// acknowledged before this returns true, so it can no longer act on our silence.
bool SwitchManager::Disarm() {
    if (!watchdog_.IsConnected()) return false;
    json::Value p;
    p.Set("pid", static_cast<int>(GetCurrentProcessId()));
    std::string resp;
    if (!watchdog_.Request(ipc::MakeMessage("request", ipc::msg::kDisarm, 2, p), &resp, kDisarmTimeoutMs)) {
        log::Warn("watchdog did not acknowledge disarm");
        return false;
    }
    auto r = json::Parse(resp);
    bool ok = r && r->Find("status") && r->Find("status")->AsInt(-1) == 0;
    log::Info(ok ? "watchdog disarmed" : "watchdog refused disarm: " + resp);
    return ok;
}

// Closes the pipe and waits up to waitMs for the watchdog process to exit; a
// watchdog that is still alive after that is terminated. It is Velora's own child.
void SwitchManager::StopWatchdog(DWORD waitMs) {
    watchdog_.Close();
    if (!watchdogProcess_) return;
    if (WaitForSingleObject(watchdogProcess_, waitMs) == WAIT_TIMEOUT) {
        if (waitMs > 0) log::Warn("watchdog did not exit within " + std::to_string(waitMs) + " ms; terminating it");
        TerminateProcess(watchdogProcess_, 1);
        WaitForSingleObject(watchdogProcess_, 500);
    }
    CloseHandle(watchdogProcess_);
    watchdogProcess_ = nullptr;
}

bool SwitchManager::Enter() {
    if (entered_) return true;
    log::Info("enter: begin");
    watchdogRestarts_ = 0;
    lastError_.clear();
    pendingTaskbarCreated_ = false;
    struct EnteringGuard {
        bool& flag;
        explicit EnteringGuard(bool& f) : flag(f) { flag = true; }
        ~EnteringGuard() { flag = false; }
    } guard(entering_);

    auto statePath = StatePath();
    auto markerPath = DirtyMarkerPath();
    if (statePath.empty() || markerPath.empty()) {
        log::Error("enter: no data directory");
        return false;
    }

    // 1. state.json first, then the marker (a marker always has state behind it).
    state_ = SwitchState{};
    state_.taskbarAutoHide = GetTaskbarAutoHide();
    state_.monitors = SnapshotMonitors();
    state_.wallpaperPath = CurrentWallpaper();
    state_.foregroundHwnd = static_cast<std::uint64_t>(reinterpret_cast<UINT_PTR>(GetForegroundWindow()));
    state_.shellPid = GetCurrentProcessId();
    state_.shellStartTime = ProcessStartTime(GetCurrentProcess());
    if (!SaveState(state_, statePath) || !CreateDirtyMarker(markerPath)) {
        log::Error("enter: cannot write state/marker");
        RemoveDirtyMarker(markerPath);
        return false;
    }

    // 2. Watchdog (it watches our process handle and our heartbeat).
    if (!StartWatchdog()) {
        Rollback();
        return false;
    }
    if (!SaveState(state_, statePath)) {  // now with watchdogPid
        log::Error("enter: cannot update state.json with the watchdog pid; aborting");
        Rollback();
        return false;
    }

    // 3. Hide the taskbars; if Explorer still reserves space for them, fall back to auto-hide.
    SetTaskbarsVisible(false);
    if (!WaitTaskbarsVisible(false, 1000)) log::Warn("enter: taskbar windows did not hide within 1 s");
    if (HasRealTaskbar() && AnyWorkAreaReserved()) {  // every monitor, including those with a secondary taskbar
        SetTaskbarAutoHide(true);
        autoHideFallback_ = true;
        log::Info("enter: work area still reserved after hiding; auto-hide fallback applied");
    }

    // 4. Appbar so maximized windows respect Velora's bar. Degrades, never fails Enter.
    if (!bar_.Create(instance_, nullptr, Edge::Top, tokens::size::kMenuBarHeight, tokens::kLight.surfaceStrong.argb,
                     menuBarOptions_)) {
        log::Warn("enter: appbar unavailable; continuing without work-area reservation");
    } else {
        // Recovery ABM_REMOVEs these handles if the shell dies without unregistering.
        state_.appbarHwnds = {static_cast<std::uint64_t>(reinterpret_cast<UINT_PTR>(bar_.Hwnd()))};
        if (dockEnabled_ && dock_.Create(instance_, nullptr, Edge::Bottom, dockReservedDip_,
                                         tokens::kLight.surfaceStrong.argb, dockOptions_))
            state_.appbarHwnds.push_back(static_cast<std::uint64_t>(reinterpret_cast<UINT_PTR>(dock_.Hwnd())));
        if (!SaveState(state_, statePath)) log::Warn("enter: could not record the appbar handles in state.json");
    }

    RememberTrays();
    entered_ = true;
    entering_ = false;
    bus_.Post(EventType::Entered);
    log::Info("enter: done");
    // A TaskbarCreated that arrived while we were entering (hidden taskbars and the appbar
    // may predate Explorer's new taskbar): handle it now that we are in Velora Mode.
    if (pendingTaskbarCreated_) {
        pendingTaskbarCreated_ = false;
        OnTaskbarCreated();
    }
    return true;
}

void SwitchManager::RememberTrays() {
    knownTrays_ = FindTrayWindows();
    std::sort(knownTrays_.begin(), knownTrays_.end());
}

void SwitchManager::Rollback() {
    log::Warn("enter: rolling back");
    bool disarmed = Disarm();
    RestoreOptions o;
    o.reason = "enter rollback";
    RestoreWindowsDesktop(o);
    StopWatchdog(disarmed ? kWatchdogExitWaitMs : 0);
}

void SwitchManager::Exit() {
    if (!entered_) return;
    log::Info("exit: begin");
    bus_.Post(EventType::Exiting);
    entered_ = false;

    // Before anything else changes: once we stop heartbeating and the marker goes
    // away the watchdog must not read that as a crash.
    bool disarmed = Disarm();

    dock_.Destroy();
    bar_.Destroy();

    RestoreOptions o;
    o.restoreForeground = true;
    o.reason = "exit";
    RestoreWindowsDesktop(o);  // removes the marker last

    StopWatchdog(disarmed ? kWatchdogExitWaitMs : 0);
    log::Info("exit: done");
}

void SwitchManager::OnTaskbarCreated() {
    if (entering_) {
        pendingTaskbarCreated_ = true;
        return;
    }
    if (!entered_) return;
    ++explorerRestarts_;
    log::Info("TaskbarCreated: Explorer restarted; re-hiding taskbars and re-registering appbars");
    SetTaskbarsVisible(false);
    // Auto-hide is a persistent Explorer setting and survives a restart; only the
    // fallback we applied ourselves needs re-asserting if Explorer reset it.
    if (autoHideFallback_) SetTaskbarAutoHide(true);
    bar_.Reregister();
    dock_.Reregister();
    RememberTrays();
    bus_.Post(EventType::ExplorerRestarted);
}

void SwitchManager::Reconcile() {
    if (!entered_) return;

    // New tray windows = Explorer recreated its taskbar (also covers a missed TaskbarCreated):
    // its appbar registrations are gone, register ours again.
    {
        std::vector<HWND> trays = FindTrayWindows();
        std::sort(trays.begin(), trays.end());
        if (trays != knownTrays_) {
            knownTrays_ = trays;
            if (!trays.empty()) {
                log::Info("reconcile: taskbar windows changed; re-registering appbars");
                bar_.Reregister();
                dock_.Reregister();
            }
        }
    }

    // Explorer (or a third-party appbar change) may re-show the taskbar.
    if (AnyTaskbarVisible()) {
        ++rehides_;
        SetTaskbarsVisible(false);
    }

    // The watchdog is our safety net: if it died, replace it or leave Velora Mode.
    // (No process handle = an earlier restart attempt failed and cleaned up after itself.)
    if (!watchdogProcess_ || WaitForSingleObject(watchdogProcess_, 0) == WAIT_OBJECT_0) {
        log::Error("watchdog exited unexpectedly");
        if (watchdogRestarts_ < 3 && (++watchdogRestarts_, StartWatchdog())) {
            if (!SaveState(state_, StatePath())) log::Warn("could not update state.json with the new watchdog pid");
            log::Info("watchdog restarted");
        } else {
            log::Error("watchdog could not be restarted; leaving Velora Mode");
            if (onFatal_) onFatal_();
        }
    }
}

void SwitchManager::Heartbeat() {
    if (!entered_) return;
    json::Value p;
    p.Set("seq", static_cast<std::int64_t>(++heartbeatSeq_));
    watchdog_.Post(ipc::MakeMessage("event", ipc::msg::kHeartbeat, -1, p));
}

json::Value SwitchManager::Status() const {
    json::Value s;
    s.Set("entered", entered_);
    s.Set("taskbarsHidden", !AnyTaskbarVisible());
    s.Set("trayWindows", static_cast<int>(FindTrayWindows().size()));
    s.Set("appbarRegistered", bar_.IsRegistered());
    s.Set("appbarRegistrations", bar_.RegistrationCount());
    s.Set("explorerRestarts", explorerRestarts_);
    s.Set("rehides", rehides_);
    s.Set("autoHideFallback", autoHideFallback_);
    s.Set("watchdogPid", static_cast<std::int64_t>(state_.watchdogPid));
    s.Set("watchdogRestarts", watchdogRestarts_);
    s.Set("pid", static_cast<std::int64_t>(GetCurrentProcessId()));
    return s;
}

}  // namespace velora::sw
