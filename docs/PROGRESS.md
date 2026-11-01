# Progress

Status: not started | in progress | gated | passed | deferred

| Module | Status | Evidence |
|---|---|---|
| core | passed (G1) | contracts v1 + json, state store, IPC pipe, runtime/profile, log, event bus; core_test passes (PR #9 merged, CI green) |
| switch | gated | Day-2 safety gate PASS locally (117/117 checks on main 364f267, see "Day 2 review fixes"; original run 55/55); CI result recorded on the PRs |
| render | passed (base) | tokens + animation engine (animation_test) + RenderHost (render_host_test: D3D11 + composition swap chain + D2D draw/present/resize, DWM Mica/rounded-corner helpers). No consumers yet (dock/menubar are Day 3) |
| dock | not started | |
| menubar | in progress (G1 partial) | Velora menu, active app, battery + clock, light/dark, drawn in the top appbar; menubar_test + live run on this machine. **Tray-icon hosting not built (blocked, see Day 3)** |
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
- CI: `render_host_test` passed on the runner in 0.05 s, but ctest hides stdout on success, so the device-vs-skip path is now printed (see PR C: the CI runner reported `device`). Local run initialized a hardware adapter.


## Day 2 review fixes
### PR A: watchdog lifecycle (HIGH)
- Armed/disarm state machine in the watchdog; synchronous `disarm` from the shell; hello pid check; old watchdog reaped (<= 2 s) before a new one starts; failed restarts terminate the unconnected watchdog; `watchdogRestarts_` reset on Enter.
- Regression tests (`PhaseWatchdogLifecycle`, `watchdog death`): disarm ack + exit + nothing restored; marker absent + silent shell -> quiet exit and helper NOT terminated; never-armed -> quiet exit after grace; wrong-pid hello rejected (400) and does not arm; hello after disarm refused; 5x exit -> immediate enter, old watchdog already gone, new shell kill restored < 1 s; rejected-hello watchdogs terminated and shell leaves Velora Mode; `watchdogRestarts` == 1 after one restart. Run against the pre-fix code: 13 of these checks fail; with the fix: 0.
- Full gate after PR A (local, real Explorer, 1 monitor): 88/88 checks. Enter median 313 ms (max 639), exit median 298 ms (max 511), kills 10/10 median 75 ms (max 95), hangs 3/3 max 2312 ms, hotkey 84 ms.

### PR B: restore correctness (MEDIUM)
- state.json applied only with the marker, consumed on success; login restore pending/retry; empty tray list is not success; recorded appbar HWNDs ABM_REMOVEd and work area verified after crash/hang recovery.
- Regression tests (`restore correctness` phase; kill/hang phases; core_test): stale state without marker ignored + deleted, with marker applied + consumed; state.json gone after clean exit, kill and hang recovery; empty list never succeeds; login check stays pending with marker kept, then restores on TaskbarCreated and on the periodic retry, and gives up with exit 3 and the marker kept; appbarHwnds round trip.
- Work area after recovery, measured from the kill/hang (real Explorer, placeholder appbar registered): kills 10/10 back after median 140 ms (max 184); hangs 3/3 median 2319 ms (max 2330, i.e. the hang detection time, work area follows within ~70 ms). Unmeasured: whether the explicit ABM_REMOVE or Explorer's own cleanup did the work (not separated).
- Full gate after PR B: 104/104 checks.

### PR C: low-severity fixes
- Auto-hide fallback checks every monitor; taskbar shown before the recorded shell is terminated (wait capped at 500 ms); Enter aborts if state.json cannot be updated; TaskbarCreated during Enter is queued; Reconcile re-registers appbars when the tray windows change; Enter aborts with a clear message if the emergency hotkey cannot be registered.
- Tests: new `hotkey unavailable` phase (hotkey held by the test; shell exits 2, nothing changed, no watchdog left, enters once it is free). Items 8-10 have no dedicated regression test (LOW; secondary taskbar and an Explorer restart inside Enter cannot be produced with fake tray windows): unmeasured.
- `render_host_test` prints `render_host_test: device` or `render_host_test: skipped (...)`; CI has an extra `-V` step so the line is visible. Local: `device` (hardware adapter). CI runner: `render_host_test: device` (adapter reported as hardware, run 41297919203).
- Gate flake found and fixed (test only): the exit->enter loop started the next shell while the previous kill-recovery watchdog still owned the pipe, so the new hello failed. The loop now waits for that watchdog. Before the fix 2 of 6 full runs failed on that check; after it 4 of 4 full runs passed (111 checks each).
- One full run also failed 47 `cycles` checks with `lost ... Chrome_WidgetWin_1` (another application's windows disappearing between the snapshots); not reproduced in 8 later full/cycle runs. Treated as desktop churn from other applications, not investigated further: unexplained.

### Re-run of the full switch gate after PR A+B+C (tip of fix/low-severity)
Raw results: `docs/evidence/switch_gate_2026-11-01_review-fixes.json`. Windows 11 10.0.26200, real Explorer, 1 monitor, non-elevated. **111/111 checks pass.**
- Enter/exit: 50/50 cycles clean (window list identical, state.json consumed, work area identical). Enter median 349 ms (max 566), exit median 342 ms (max 588).
- Forced kills 10/10: taskbars back median 74 ms (max 100); work area back median 201 ms (max 232); watchdog exits, marker and state.json gone.
- Hangs 3/3: taskbars back median 2248 ms (max 2265, budget 3 s); work area back median 2335 ms (max 2354).
- Hotkey with frozen shell: 17 ms. Explorer restarts (simulated) 3/3, re-hidden median 31 ms.
- Still unmeasured (unchanged): real Explorer restart, real taskbar hide/auto-hide fallback and multi-monitor secondary taskbar, physical key press, elevated windows.

### Full switch gate on merged main (789ca4c, after #13-#16) — FAIL 110/112 checks, one cycle
Raw results: `docs/evidence/switch_gate_2026-11-01_merged-main.json`. Run once against the `*-testing` binaries (build/testbin), real Explorer, 1 monitor, non-elevated.
- All phases pass except `enter/exit cycles`: 49/50 clean. Cycle 16 failed only on `window list differs: lost ... (Chrome_WidgetWin_1)`: a window of another application (Chrome/Electron class) present before and gone after. Every Velora-owned check in that cycle (taskbar restored, marker gone, watchdog exited, work area identical, state.json consumed) passed.
- Same signature as the earlier unexplained run (47 cycles failed on `Chrome_WidgetWin_1`, 2026-11-01). Seen twice in about 14 full or cycle runs; not reproduced on demand, cause not found. Not shown to be caused by Velora and not shown to be unrelated: unexplained.
- Other numbers: kills 10/10 (taskbars back median 100 ms, max 124; work area median 238 ms, max 292); hangs 3/3 (max 2285 ms); hotkey 41 ms; Explorer restarts (simulated) 3/3; enter median 380 ms (max 885), exit median 389 ms (max 698).

### Full switch gate on main 364f267 (after #18) — PASS 117/117 checks — current result
Raw results: `docs/evidence/switch_gate_2026-11-01_main-364f267.json`. Real Explorer, 1 monitor, non-elevated.
- 117/117 checks, 0 failed. Enter median 231 ms (max 486), exit median 234 ms (max 484); kills 10/10 taskbars back median 42 ms (max 54); hangs 3/3 max 2234 ms; hotkey 23 ms.
- This supersedes the 110/112 run on 789ca4c above. That failure (`Chrome_WidgetWin_1` window gone between snapshots) was external: since #18 the gate attributes vanished windows, and this run logged `externalLostWindows` 2 and `externalNewWindows` 1 as external instead of failing. Attribution checks pass, including that a vanished test-owned window and the previously focused window are still failures.
- Still unmeasured: real Explorer restart, real taskbar hide on real monitors, physical Ctrl+Alt+Shift+Esc (covered by the live test).

## Day 3 (owner B, UI)
Starting commit: `4d6ac80` (main before Day 3). Compat is a separate session.

### menubar (PR feat/menubar)
- Drawn into the existing top appbar through `AppBarOptions` (composition window + message hook + placement), so appbar registration, work-area reservation and recovery stay owned by switch/. The blank white strip was the GDI brush Day 2 used as a placeholder.
- G1 items built: active app name (FileDescription, exe stem, or window title for UWP), battery status item (hidden without a battery), clock (re-armed once per minute), Velora menu: About, Settings (launches velora-settings.exe), Apps, Sleep / Restart / Shut Down (each behind a Yes/No confirm; SetSuspendState and ExitWindowsEx without EWX_FORCE), Exit Velora. Light/dark and transparency follow the system settings (re-read on WM_SETTINGCHANGE).
- **Blocked: tray-icon hosting.** On Windows 11 Explorer owns the notification area and Velora must not replace or inject into Explorer (CLAUDE.md sec. 0.8). The ManagedShell WindowsTray approach only works for a process that *is* the shell (it registers Shell_TrayWnd and receives Shell_NotifyIcon copy-data). Reading Explorer's tray by cross-process memory access is not possible on the XAML tray of this build (26200) and would also be invasive. So no tray icons yet and nothing from ManagedShell was ported for the menu bar (THIRD_PARTY_NOTICES unchanged). Needs a decision: accept Velora-own status items only, or study a supported path.
- Menus use native Win32 popups (system styling, not Velora-themed). App menus are G2.
- Unit tests: `menubar_test` (battery text, app display name). Popup menu, power actions and rendering are manual (docs/QA.md); no test calls Sleep / Restart / Shut Down.
- Live check (this machine, real Explorer): menu bar visible with logo, app name, `100% (charging)` and clock; `velora-shell --exit` returned the taskbar and ended both processes. Screenshot reviewed by eye, not archived.
- Switch gate with the menu bar and tracker running: see "Switch gate after Day 3" below.

### TaskbarCreated debounce
- `BurstDebounce` (core/debounce.h, `debounce_test`): the first TaskbarCreated of a burst is handled immediately, later ones inside 500 ms collapse into one trailing run. Lives in the menubar PR because it edits main.cpp together with the tracker wiring (deviation from one-PR-per-module).

### Switch gate after Day 3 (menubar PR build)
- Local runs against the testing binaries with menu bar + window tracker active: run 1 FAIL (2 of 117 checks; the failing checks were not captured because only the tail of the output was kept), runs 2, 3 and 4 PASS 117/117 (enter median 431 ms / max 688, exit median 265 ms / max 557). The failure is **not reproduced and not explained**; the gate's own output is now kept in full (`build/gate_r*.log`). Open item: re-run on the final Day 3 main and attribute any failure.
