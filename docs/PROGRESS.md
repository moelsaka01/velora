# Progress

Status: not started | in progress | gated | passed | deferred

| Module | Status | Evidence |
|---|---|---|
| core | passed (G1) | contracts v1 + json, state store, IPC pipe, runtime/profile, log, event bus; core_test passes (PR #9 merged, CI green) |
| switch | gated | Day-2 safety gate PASS locally (55/55 checks, see Day 2 below); CI result recorded on the PR |
| render | passed (base) | tokens + animation engine (animation_test) + RenderHost (render_host_test: D3D11 + composition swap chain + D2D draw/present/resize, DWM Mica/rounded-corner helpers). No consumers yet (dock/menubar are Day 3) |
| dock | not started | |
| menubar | not started | |
| windows | passed (G1 tracking) | window_tracker_test: lifecycle via WinEvents (create/title/minimize/restore/hide/destroy), tool/owned/never-shown windows excluded, own-process skip, fullscreen rect + elevation helpers, live foreground + fullscreen with VELORA_TEST_FOCUS=1 (run once locally: pass). Cloaked path and a real elevated window are unmeasured |
| overview | not started | |
| apps | not started | |
| search | not started | |
| control | not started | |
| notify | not started | |
| desktops | not started | |
| hotkeys | not started | |
| watchdog | gated | heartbeat, crash/hang/hotkey/login/--restore paths covered by the same gate |
| files | in progress | empty target builds (Day 1 skeleton) |
| settings | in progress | empty target builds (Day 1 skeleton) |
| compat | not started | |
| installer | not started | |

## Day 1
- research: passed (docs/RESEARCH.md; gh api metadata, 3 Haiku lookups)
- version.h from vcpkg.json: passed (PR #4, CI green)

## Day 2 (owner A)
Starting commit: `1f1ea84d64b9b17ed5cbb40e0351e83dae99b2e5` (main before Day 2).

### Safety gate: `switch_gate` (tests/switch_gate_test.cpp, ctest) — PASS, 55/55 checks
Raw results: `docs/evidence/switch_gate_2026-11-01.json`. Run on Windows 11 10.0.26200, real Explorer present, 1 monitor, non-elevated.

| Gate item | Requirement | Result |
|---|---|---|
| Enter/exit cycles | 50 cycles; window list before == after; taskbar restored every time | 50/50 clean. Also per cycle: marker + state.json present while in mode and marker gone after, watchdog exits, no Velora windows left, primary work area identical. Enter median 158 ms (max 193), exit median 195 ms (max 296) |
| Forced kills | 10 x TerminateProcess(shell), restored < 1 s | 10/10. Taskbars visible after median 43 ms (max 45); marker removed, watchdog exits, work area and window list identical |
| Hangs | 3 simulated hangs, restored < 3 s | 3/3: median 2264 ms, max 2268. Extra stress run of 15 hangs: max 2516 ms. Hung shell terminated by the watchdog each time |
| Explorer restarts | 3, taskbar re-hidden, appbars re-registered | 3/3 **simulated** (see deviations): new tray windows re-hidden in median 28 ms, stayed hidden through the delayed re-hide passes, `explorerRestarts` counted, appbar registration count increased each time |
| Dirty marker at startup | restore before anything else | `velora-shell --recover-only` restores and exits; normal start with a marker logs "startup recovery" before "enter: begin"; `velora-watchdog --login-check` restores with a marker and changes nothing without one; corrupt state.json + marker still shows the taskbar |
| `--restore` | | restores in time, terminates only the recorded shell; a recorded PID that is not velora-shell (cmd.exe helper) was NOT terminated; idempotent with no state |
| Emergency hotkey | handled by watchdog, works with frozen shell | Ctrl+Alt+Shift+Esc registered by the watchdog (a second registration fails with ERROR_HOTKEY_ALREADY_REGISTERED); with the shell frozen, WM_HOTKEY restores in 47 ms (far below the heartbeat timeout) and terminates the shell |
| Watchdog death | (extra) | shell replaced a killed watchdog and stayed in Velora Mode |
| Process safety | no other process terminated | explorer.exe PIDs unchanged across the run; no Velora process left; the only processes terminated by the tests are ones they spawned (shell, watchdog, helper) |

### Deviations and unmeasured items
- **Hermetic fake taskbar.** The gate hides/shows *fake* tray windows (class names from `VELORA_TEST_PROFILE`) with a private data dir, pipes and TaskbarCreated message, so a test run can never hide the real taskbar or overwrite the real state.json. Appbar registration does go to the real Explorer (a 28 DIP strip for a few ms per cycle); work area is checked before/after.
- **Real Explorer restart: unmeasured.** The 3 restarts are simulated (destroy + recreate fake tray windows, broadcast the test TaskbarCreated message). `explorer.exe` was deliberately not killed on this machine. Needs one manual run on a live desktop.
- **Real taskbar hide / auto-hide fallback: unmeasured.** The code path (`SetWindowPos` hide, `ABM_SETSTATE` auto-hide when the work area stays reserved) is ported from ManagedShell but not run against the real taskbar in this session.
- **Physical hotkey press: unmeasured.** Registration and the WM_HOTKEY handler are verified; no synthetic key press was sent.
- **Heartbeat constants changed** from 1000/3000 ms to 500/2500 ms (DECISIONS.md): with a 3 s timeout a hang right after a heartbeat took 3073 ms.
- **Window-list equality** is strict for lost/changed windows and all test-owned windows; windows other processes create mid-run are tolerated (0 observed in the final run), and hidden foreign windows plus Explorer EdgeUI/IME helper windows are ignored. One earlier run saw `EdgeUiInputTopWndClass` appear/disappear on its own; that class was added to the allowance.
- **Exit triggers** (menu, hotkey, tray) are not built; Day 2 provides IPC `exit` and `velora-shell --exit`.
- **Run key for `--login-check`** is the installer's job (D8).
- Placeholder top appbar (28 DIP, solid color) stands in for the menu bar until module B lands.

### windows/ (Day 2)
- `WindowTracker`: SetWinEventHook out-of-context only (no injection); app-window filter (visible, not cloaked, unowned unless APPWINDOW, no tool windows, no shell/desktop classes); posts WindowCreated/Destroyed/Activated/Minimized/Restored/TitleChanged/Cloaked/Uncloaked and FullscreenEntered/Left on the event bus.
- Elevated owners: `QueryElevation` marks windows `controllable=false` (badge, no actions); verified for `services.exe` (inaccessible). A real elevated app window was not tested.
- Fullscreen: foreground window rect covers its monitor (pure predicate unit-tested); verified live with a click-through 1/255-alpha window under `VELORA_TEST_FOCUS=1`. Exclusive-mode D3D fullscreen is unmeasured.
- Cloaked windows (UWP/other desktop) leave the app list and emit WindowCloaked; no automated test (cannot cloak a foreign window).
### render/ base (Day 2)
- `animation.h`: cubic-bezier tweens (token curves), spring motion with retarget (Dock magnification), enable switch (gaming mode / reduced motion jumps to the end), re-entrancy-safe. `animation_test`: easing endpoints and monotonicity, delay/finish-once, cancel, disabled, re-entrant start, spring settles < 2 s with < 10% overshoot, stall-safe.
- `RenderHost`: D3D11 (hardware, WARP fallback) + DXGI composition swap chain (premultiplied alpha) + DirectComposition + D2D device context; `ApplyBackdrop` (Mica/Acrylic, Windows 11 22621+, returns false when unsupported) and `ApplyRoundedCorners`. `render_host_test` on this machine: hardware adapter, 3 frames + resize + draw, Mica and rounded corners supported.
- One measurement, not a benchmark: DXGI `QueryVideoMemoryInfo` reported 15.1 MB local GPU usage for the test process with a single 320x160 host. Idle GPU/RAM budgets are unmeasured until the shell has real windows (D6 bench).
- CI: `render_host_test` passed on the runner in 0.05 s, but ctest hides stdout on success, so whether it initialized a device or took the loud skip path (only allowed when `CI` is set) is unmeasured. Local run initialized a hardware adapter.


## Day 2 review fixes
### PR A: watchdog lifecycle (HIGH)
- Armed/disarm state machine in the watchdog; synchronous `disarm` from the shell; hello pid check; old watchdog reaped (<= 2 s) before a new one starts; failed restarts terminate the unconnected watchdog; `watchdogRestarts_` reset on Enter.
- Regression tests (`PhaseWatchdogLifecycle`, `watchdog death`): disarm ack + exit + nothing restored; marker absent + silent shell -> quiet exit and helper NOT terminated; never-armed -> quiet exit after grace; wrong-pid hello rejected (400) and does not arm; hello after disarm refused; 5x exit -> immediate enter, old watchdog already gone, new shell kill restored < 1 s; rejected-hello watchdogs terminated and shell leaves Velora Mode; `watchdogRestarts` == 1 after one restart. Run against the pre-fix code: 13 of these checks fail; with the fix: 0.
- Full gate after PR A (local, real Explorer, 1 monitor): 88/88 checks. Enter median 313 ms (max 639), exit median 298 ms (max 511), kills 10/10 median 75 ms (max 95), hangs 3/3 max 2312 ms, hotkey 84 ms.
