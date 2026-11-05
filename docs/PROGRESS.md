# Progress

Status: not started | in progress | gated | passed | deferred

| Module | Status | Evidence |
|---|---|---|
| core | passed (G1) | contracts v1 + json, state store, IPC pipe, runtime/profile, log, event bus; core_test passes (PR #9 merged, CI green) |
| switch | gated | Day-2 safety gate PASS locally (117/117 checks on main 364f267, see "Day 2 review fixes"; original run 55/55); CI result recorded on the PRs |
| render | passed (base) | tokens + animation engine (animation_test) + RenderHost (render_host_test: D3D11 + composition swap chain + D2D draw/present/resize, DWM Mica/rounded-corner helpers). No consumers yet (dock/menubar are Day 3) |
| dock | gated (G1 built, per-monitor ready, 1 instance) | pinned + running apps, dots, launch/activate/minimize/cycle, menu; dock_test (decision logic, grouping, geometry, persistence); drawn live. Activate / minimize / restore of a running app verified live by the user (2026-11-01, docs/evidence/live_check_phase1_guided.md); launch from the Dock not yet exercised live; some apps still show fallback icons |
| menubar | gated (G1 with Velora-own status items; tray hosting dropped by decision, see DECISIONS) | Velora menu, active app, battery + clock, light/dark in the top appbar; menubar_test; live on this machine. **Tray hosting blocked** (Explorer owns the Win11 tray), popup/power paths checked by hand only |
| windows | passed (G1 tracking) | window_tracker_test: lifecycle via WinEvents (create/title/minimize/restore/hide/destroy), tool/owned/never-shown windows excluded, own-process skip, fullscreen rect + elevation helpers, live foreground + fullscreen with VELORA_TEST_FOCUS=1 (run once locally: pass). Cloaked path and a real elevated window are unmeasured |
| overview | gated (G1 built) | DWM thumbnail grid over the work area (menu bar and Dock stay visible), justified rows, click/arrows+Enter/Esc, hotkey Ctrl+Alt+Up (configurable), opt-in top-left hot corner, IPC/CLI; overview_test (layout, neighbours, hot-corner dwell) and overview_window_test (real window, CI). No animation, no desktop strip (G2). Not seen on screen yet (guided check) |
| apps | gated (G1 built) | AppsFolder + Start-menu index (191 apps here), cached, background refresh; grid with search drawn live with real icons; apps_test. **Launching from the grid not exercised live**; pages/folders (G2) not built |
| wallpaper | passed (1 monitor) | procedural original wallpaper, per monitor, backup + restore on exit and crash recovery verified live (1 monitor, developer path override); wallpaper_test. Multi-monitor unmeasured |
| search | gated (G1 built) | Spotlight-style panel in the shell (520 pt pill, results panel with Applications / Folders / Documents / Images / Music / Videos / Other Files sections, spring open/close): apps from the app index, files from the Windows Search OLE DB provider on a worker thread (debounced 120 ms), keyboard-only flow (type, Up/Down, Enter opens, Ctrl+Enter reveals in Files, Esc closes), global hotkey via RegisterHotKey (settings.json `search.hotkey`; default Alt+Space only if free, else Ctrl+Space; none in the test profile), Velora menu > Search, `velora-shell --open-search [query]` / `--close-search`. Windows Search disabled or silent: apps only, with a visible note. `search_core_test`, `search_test` (53 checks incl. the real index: 10 hits for "windows" on this machine, disabled/silent index simulated). **Not yet seen on screen**; calculator, unit conversion, settings entries and Everything are G2 |
| control | gated (G1 built) | Control Center panel: Wi-Fi (on/off, saved networks, connect to a saved profile), Bluetooth, dark mode, Do Not Disturb (flag only), display brightness (WMI / DDC), volume + mute + current output device, battery; documented APIs only. control_test (read-only probes on this machine: audio, Wi-Fi, radios, brightness and battery all answered) and control_center_window_test (fake backend, CI). Output-device switching not implemented (undocumented API). Not seen on screen yet |
| notify | deferred (not in v1.0) | not built; Do Not Disturb is a flag only. Listed as unsupported in the release report |
| desktops | deferred (not in v1.0) | not built (Level 2 candidate) |
| hotkeys | partial | Search and Overview hotkeys (RegisterHotKey, configurable, no keyboard hook); Cmd-style remapping is not built |
| theming | gated (built) | DWM-attribute window theming (corners, dark caption, border, Mica/Acrylic), no injection; skips elevated, fullscreen, games, anti-cheat, own windows, user exclusions; restored on exit, crash, --restore, login (theming_backup.json); theming_test (policy matrix + a real window of another process, CI). docs/THEMING_COMPAT.md. Real apps unmeasured |
| apple-services | gated (G1-G3 built) | Apple group in the launcher/Search (installed apps as they are, Store-search links for missing ones, App Store (web)), iCloud Drive/Photos in the Files sidebar; off by default; apple_test (pure). No Apple credentials are ever handled. Not seen on a machine with the Apple apps |
| gaming mode | gated (G1 built) | auto on fullscreen/borderless foreground (1.5 s leave delay) or manual in Settings: animations and blur off, bars hidden, app indexing and Overview paused, hot-corner timer off; gaming_test (pure). Never touches the game. Not exercised with a real game |
| watchdog | gated | heartbeat, crash/hang/hotkey/login/--restore paths covered by the same gate |
| files | gated (G1 built) | velora-files.exe: Finder-style window (216 pt sidebar with Favorites/Drives, toolbar, drawn traffic lights, list + icon views, sort, rename field, context menu, breadcrumbs, type-ahead), browse drives/folders, open with the default app, copy/move/rename/new folder via IFileOperation (OS conflict/permission dialogs), delete only to the Recycle Bin, clipboard (Explorer-compatible) and drag and drop in and out, single instance (second launch forwards its folder), `velora-shell --open-files [folder]`. `files_test` (141 checks: conflict policies, permission denied, locked file, long path, hash after every copy/move, data objects), `files_layout_test`, `files_window_test` (34 checks driving the real window). **Not yet seen on screen or exercised with a real mouse drag**; Dock Files pin is visible again |
| settings | gated (G1 built) | velora-settings.exe: General (startup, Restore Windows), Appearance, Dock, Window theming (+ exclusions), Menu bar, Hotkeys (Search + Overview), Gaming mode, Apple services, Updates; settings.json store (settings_model_test, pure) and window (settings_window_test, CI); changes reach the running shell (theming, Dock magnification, hotkeys, gaming mode apply live). Not seen on screen yet |
| compat | not started | |
| installer | in progress | Inno Setup .exe + WiX .msi (per user, Start-menu Velora / Velora Settings / Restore Windows Desktop, uninstall runs --exit then --restore), tools/package.ps1, release workflow with an install/uninstall smoke test on the CI runner; unsigned until SignPath approves (docs/SIGNING.md). Not run on a real desktop yet |

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

### apps (PR feat/apps)
- `AppIndex`: AppsFolder enumeration (Win32 shortcuts and MSIX, with AppUserModelIDs and target exe) merged with Start-menu `.lnk` files from both Programs folders (deduplicated by name, uninstallers/readmes dropped). Cached in `%LOCALAPPDATA%\Velora\apps_index.json`, loaded at shell start (instant), refreshed on a detached worker thread at start, when a Start-menu folder changes (3 s debounce) and every 30 min. **Opening the grid never scans**; it takes a snapshot.
- `AppsGrid`: full-screen window created on open and destroyed on close (GPU objects freed), type-to-search (prefix > word prefix > substring > subsequence), arrows / Enter / Esc / wheel / click, icons loaded asynchronously (IconStore worker, shell `IShellItemImageFactory`), open animation through the shared AnimationEngine. Opened from the Velora menu ("Apps") and the Dock.
- Live check: 191 apps listed with their real icons; search field, selection highlight and tiles drawn; closing returns to the desktop. Not exercised live: launching an app from the grid (`apps::Launch` is ShellExecute of `shell:AppsFolder\<id>` or the .lnk; the same call the Dock uses) and icon-less MSIX edge cases.
- Not built (G2): pages, folders, drag-reorder. Single scrolling page for G1.
- The shell skips the background scan in the test profile (gate measures switching; the test profile has a private data dir).
- Memory note: opening the grid raised the shell working set from 57 MB to 146 MB, 126 MB after closing (see the bench section); this is being reduced in the dock PR.
- Switch gate on the apps build (testing binaries, local): PASS 117/117, full log kept in `build/gate_apps.log`.

### dock (PR feat/dock)
- Second appbar at the bottom (reserved band = pill height + 8 DIP inset, full monitor width); the window is a centred pill inside the band (`AppBarOptions::placement`), clipped to a rounded region over an Acrylic backdrop. Both appbar HWNDs are recorded in state.json so crash recovery removes both.
- Items: Apps (opens the grid), Files, Settings (Velora's own exes, generic icon because they carry none yet), File Explorer, Edge when present, then running apps that are not pinned. Pinned list persists in `settings.json` ("dock.pinned", read-modify-write so other sections survive). Running windows come from `WindowTracker`; matching is AUMID first, exe path second; UWP windows are grouped by the AUMID of their frame window.
- Click: not running -> launch; running -> activate the first visible window (restore if minimized); already foreground with one window -> minimize (posted `SC_MINIMIZE`, a hung app cannot block the Dock); several windows -> cycle. Right click: windows list, Quit (posted WM_CLOSE, polite), Keep in / Remove from Dock. Elevated windows get an orange badge and are not minimized or closed.
- Entrance and hover fades run on the shared AnimationEngine (16 ms frame timer only while something animates).
- **Per-monitor:** one `Dock` instance serves one appbar; only the primary monitor is instantiated today. Multi-monitor Docks are one more instance each, not built or tested.
- Not built: magnification, minimize-into-Dock animation, jump lists, recent folders, drag-reorder, tooltips (G2/G3).
- Live check (this machine): pill with 7 items, Claude's running dot, rounded blur; clicking not exercised live (minimize/activate would act on the developer's own windows).
- Fix found on the way (owner A's file): `RenderHost` never called `ID2D1DeviceContext::SetDpi`, so everything drew at 96 dpi on a 150 % screen. Added to `CreateTarget` and `Resize`. The menu bar and grid were affected the same way.

### Switch gate on the dock build
- First run on the dock build: FAIL, 1 of 117 (`[restore-cli] rc == 0`, exit code 1). Cause found in the logs: `velora-watchdog --restore` terminated the shell and, 87 ms later, the shell's own watchdog started a second restore ("shell crashed"); the second one deleted the dirty marker first and the CLI's `DeleteFile` saw it in delete-pending state, so it reported "could not remove dirty marker" although the marker was gone. A pre-existing race in Day 2 code; the second appbar made the restore slower and widened the window. The earlier unexplained failure on the menubar build (2 of 117) was probably the same family but was not captured.
- Fix: `RemoveDirtyMarker` retries for up to 200 ms and counts "gone a moment later" as success (owner A's file, small). After the fix: gate PASS 117/117 (`build/gate_dock2.log`); more runs below.
- Extra local gate runs after the fix (dock build): PASS 117/117 three times in a row (`build/gate_dock2..4.log`); CI on PR #24 green.

### wallpaper (PR feat/wallpaper)
- Original wallpaper generated procedurally (`wallpaper::Generate`: vertical gradient plus three soft colour fields, dither against banding, dark and light variants, a per-monitor variant so monitors are related but not identical). No bundled or third-party artwork. 24-bit BMP, cached per size/theme in `%LOCALAPPDATA%\Velora\wallpapers`, longest edge capped at 2560 px (Windows scales the smooth image).
- Applied per monitor with the documented `IDesktopWallpaper` (monitor IDs, DWPOS_FILL) right after entering Velora Mode; skipped in the test profile and when `settings.json` has `wallpaper.enabled = false`.
- **Reversible and crash-safe:** before the first change the original wallpaper of every monitor is saved to `wallpaper_backup.json` (never overwritten by one of our own images). Normal exit and `RestoreWindowsDesktop` (watchdog crash/hang/hotkey, `--restore`, login recovery) put each monitor back only if it still shows one of our images; a wallpaper the user chose meanwhile is left alone. Restore runs after the taskbar is back and before the marker is removed.
- Tests: `wallpaper_test` (size, determinism, dark darker than light, not flat, degenerate sizes, BMP layout). Live on this machine (1 monitor): the generated wallpaper showed behind the Dock; exit restored the original (the system wallpaper copy returned to the original image's average colour, 30,28,26); `kill -9` of the shell: the watchdog log shows `wallpaper: restored 1 monitor(s)` together with the taskbar and both appbars.
- **Environment caveat:** processes started from the Claude desktop app see an AppData-virtualised `%LOCALAPPDATA%`, so Explorer could not read the generated image (`SetWallpaper` returned 0x80070002). The live check therefore used `VELORA_WALLPAPER_DIR=C:\velora\build\wp`, a developer override that is not needed on a normal install. Not verified: multi-monitor wallpapers (one monitor here), theme change while running (the wallpaper keeps the variant chosen at enter), slideshow / Spotlight originals (restored as the path IDesktopWallpaper reports; an empty original falls back to the desktop colour).

### Day 3 bench (tools/bench.ps1, raw CSVs in `bench/20261101-173816_*`)
Machine: the developer laptop on a normal desktop (Claude app, browser, game library running), **not a clean boot**; 3 runs, 120 s wait, 60 s samples, with the Dock, menu bar, wallpaper and apps index (cached) active, grid never opened. Medians (range):
- Shell + watchdog **private bytes 103.9 MB** (103.6 .. 103.9); working set 121.5 MB (102.8 .. 121.7); idle CPU **0 %** (sub-0.001 %, rounds to 0); GPU local memory (DXGI process counter) **41.7 MB** (41.1 .. 41.9).
- System-wide committed delta: **inconclusive** (-56.1 MB, range -66.5 .. +34.9): other applications changed the machine's commit by more than Velora uses, so this number cannot be read as Velora's cost. The per-process private bytes above are the usable measure (budget < 150 MB: **met by that measure**).
- Enter latency (start of `velora-shell.exe` until the taskbar is hidden): median 983 ms (242 .. 1264); target < 1.5 s met. The gate's own enter timing (no UI work to fetch icons, test profile) is median 520 ms.
- Exit latency in the raw bench files (15 .. 18 ms) is **wrong**: the script did not wait for the GUI-subsystem helper. Fixed in this PR. Measured by hand afterwards (one run): `velora-shell --exit` until the shell process is gone **1029 ms**, of which the shell's own `exit: begin` .. `exit: done` is 406 ms and the taskbar is back ~110 ms after the restore starts. **Just over the 1 s target; not optimised.**
- Opening the apps grid: private bytes rise from ~103 MB to ~165-176 MB while open and settle at ~137 MB after closing (about +35 MB retained: icon pixels, DirectWrite/D2D caches, shell DLLs). Three open/close cycles did not grow it further. This was *not* reduced further in Day 3 (an earlier note said it would be); total after the first grid use is still under 150 MB private.
- Not measured: enter latency on a cold boot, install size (no installer yet), multi-monitor memory (each Dock/menu bar is its own D3D device, about 20-25 MB private each by estimate from 57 MB -> 104 MB between the menu-bar-only and menu-bar + Dock builds).

### Exit latency (perf/exit-latency)
- Wallpaper restore (~300 ms of Explorer round trips) now runs on a thread alongside the taskbar steps on a normal exit and is joined before the marker is removed. `velora-shell --exit` until the shell is gone: **~400 ms** (4 hand runs: 392, 408, 401, 405; one bench run: 382 ms; target < 1 s met). The earlier ~1030 ms was `Start-Process -Wait` overhead in PowerShell, not the shell; bench.ps1 now waits on the helper process object and then for shell and watchdog to be gone. Raw: `bench/20261101-184708_*`. Not measured: the same method on the old binary.
## Backlog

- Day 7 experiment: read tray icons via UI Automation, time-boxed 2 h (read-only; would feed a mirrored status strip if it works without injecting into Explorer).

### Phase 3 / icons (PR feat/icon-quality)
- `ui::IconStore` is keyed by (source, tier) and every consumer asks for the tier of its own device size (grid 56 DIP, Dock 52 DIP today); icons are exactly tier x tier pixels (WIC high-quality cubic) and are drawn with Direct2D's high-quality cubic filter.
- Generic-icon detection (`IsGenericAppIcon`): two references (application and document icon) drawn by the shell. `icons_test` on this machine (187 -> 185 apps after dropping stale shortcuts): 179 get a real icon, 6 fall to the letter tile; 8 apps that previously showed the generic document icon now find their real icon via the target exe. Distance of real icons to the nearest generic reference: 50+; threshold 10.
- Phase 1 suspects, with causes found by reading the shortcuts (not guessed): Application Verifier (x64/WOW) is a real exe with no icon resource -> letter tile; mikroC PRO for PIC, mikroProg Suite, Zoom Workplace are shortcuts of uninstalled programs -> dropped from the index; Database Compare is a shortcut to a missing Office tool (AppVLP launcher) -> dropped; Spreadsheet Compare already had a real icon.
- Diagnostics: `VELORA_ICON_DUMP=<dir> icons_test.exe` writes every loaded icon as a BMP; failures print the full chain trace (`shell-item-image:generic, target-shell-item-image:miss, ...`).
- Not verified: how the new tile and the tiers look on screen (guided check).

### Phase 3 / dock (PR feat/dock-liquid-glass, stacked on #43)
- Shelf 74, icon 44 (pitch 58), padding 15, radius 20.5, 4 pt running dot (all unit-tested against the spec numbers: `dock_test`), glass material (blurred wallpaper strip + 31 % veil + rim + soft shadow), light/dark, opaque fallback when transparency is off, hover magnification (peak 1.7, spring 0.4 s / 0.1) with the pointer inverted through the stretched layout, icons decoded at the full-peak device tier and drawn with the HQ cubic filter.
- New pure module `dock/dock_layout` (geometry, falloff, scales, growth, wallpaper source rect, box blur): 100 % covered by `dock_test` including symmetry, monotonic sweep (peak icon moves with the pointer), energy conservation of the blur.
- **Not verified on screen** (the shell was not launched; McAfee): the look of the material over a real wallpaper, the magnification feel, the region switching (hover enter/leave), the shelf at 125 to 200 % scaling, per-frame cost. Backdrop build time is logged (`dock: backdrop WxH px from the wallpaper, N ms`).
- Known limits: windows that float under the Dock are hidden by the shelf instead of blurred (no live backdrop); wallpaper positions other than fill/stretch use fill; the dark material is an estimate; one Dock (primary monitor).

### Phase 3 / menu bar (PR feat/menubar-glass, stacked on #44)
- **Font:** Inter (OFL) is linked into velora-shell as an RCDATA resource and loaded as an in-memory DirectWrite collection (`ui::Format`, IDWriteFactory5); Segoe UI stays as the fallback. `font_test` (this machine): family loads, `Format()` uses it, semibold is wider than regular (the variable weight axis works); private bytes +1.2 MB with the DirectWrite factory (+1.7 MB with formats).
- **Bar:** 30 pt, Inter 13 pt, 21.75 pt between titles and right-hand items, selected-title capsule (23 pt, fully rounded) on the logo, light veil over the Acrylic backdrop, no bottom hairline. The spec's bar is fully transparent with white text; the veil is a working value so text stays readable on any wallpaper (DECISIONS).
- **Menu:** the native `TrackPopupMenu` is replaced by `menubar/menu_popup` (drawn; own window while open): 24 pt rows, 201.5 pt wide, 5 pt padding, 6 pt highlight radius, accent highlight, hairline separators, 4.5 pt under the capsule. Pure geometry/navigation in `menubar/menu_layout` (`menu_layout_test`: the measured 7 rows + 4 separators = 222 pt, 35 pt pitch, hit test, keyboard wrap/skip). Limit: corners are the DWM "round" preference (8 px), not the measured 11 pt, because a custom radius cannot be combined with a DWM backdrop.
- **Not verified on screen** (the shell was not launched; McAfee): everything visual and all popup interaction (see `docs/QA.md`, Phase 3 visual check).

### Phase 3 / launcher (Apps panel; PR feat/launcher-glass, stacked on the menu bar PR)
- The full-screen DWM-Acrylic grid is now an 843 pt glass panel (centred, clear of the Dock) inside a transparent monitor-sized window (so a click outside still dismisses): blurred wallpaper (`ui/backdrop`: the Dock's wallpaper-region loader and box blur, now shared; built at <= 640 px on the long side, ~1 MB, freed on close) under a 70 % rgb(103,87,74) veil, 1 DIP rim, soft shadow; opaque dark fill with transparency effects off.
- 60 pt icons, 115 x 102 pt pitch (7 columns in the panel), white text, search pill; open/close with the `kPanelOpen` spring (0.45 s, no bounce) plus scale 0.94 -> 1 and fade; Esc / click outside / right click close with the reverse spring, launching or losing focus closes at once; animations off = instant.
- Tests: `apps_test` (panel geometry, 7 columns), `backdrop_test` (crop on a generated image, failure paths, real wallpaper blur contract). Panel corner radius 45 pt (spec, "or more", low confidence) clamped to half the short side.
- **Not verified on screen** (see QA): material, spring feel, hit regions, scaling, multi-monitor.

### RAM review (Phase 3, partial)
- Measured here (`font_test`, a test process, not the shell): Inter + DirectWrite factory +1.2 MB private, formats +0.5 MB.
- By construction: the panel's blurred backdrop is ~1 MB and released with the window; the grid's icon tier for 60 DIP equals the old 56 DIP tier at 100 / 150 % and 200 % (64 / 96 / 128). The earlier ~+35 MB retained after the first grid use (icon pixels in `IconStore`, D2D/DirectWrite caches, shell DLLs) is **not re-measured and not reduced**: it needs the shell running (`tools/bench.ps1`), which the guided check should do.

### Resume note (2026-11-02)
- Done: #43 icons, #44 Dock glass + magnification, menu bar styling + Inter + drawn menu (#45), launcher panel (stacked PR). All unit tests pass locally (`ctest -E "switch_gate|release_hooks"`: 17/17); the two shell-launching tests run in CI only.
- Left: the guided visual check of everything in Phase 3 (McAfee off, non-elevated PowerShell, `docs/QA.md`), the bench re-run for the RAM numbers, and the DWM corner radius on menus (8 px vs 11 pt) if it looks wrong.


### Post-merge measurement (2026-11-02, main @ fead1c2, Release, 1920x1080 @ 150 %, 1 run, McAfee scanning off by the user)
- `tools\bench.ps1 -Runs 1` (bench/20261102-215634_*): enter 236 ms, exit 332 ms, committed delta 42.9 MB, shell+watchdog private 115.2 MB / WS 135.2 MB, idle CPU 0 %, GPU 42.9 MB. Single run: no median/range yet (spec asks for 3).
- Launcher (shell process, private bytes; driven with `velora-shell --open-apps` / `--close-apps`, added in #60): idle after 60 s 113.5 MB; Apps panel open 175.1 MB (+61.6 MB); 10 s after close 117.1 MB (WS 1.3 MB after the trim). Within the 150 MB budget in all three phases (shell only, watchdog not included).
- "memory trim" log lines: `private 122 -> 122 MB, working set 144 -> 0 MB` and `private 117 -> 117 MB, working set 41 -> 0 MB`; icon cache trim releases 936 KB each time. Trim returns the working set, not private bytes; the private drop after close comes from the icon-cache release.
- Visual check against MACOS_UI_SPEC (screenshots in C:\velora-ref\checks, outside the repo): Dock glass shelf, separator, running dot and menu bar (battery, clock) as specified; Apps panel ~1262 px wide = 843 pt x 1.5, icon pitch matches. Defect seen: two "Application Verifier" tiles were still blank placeholders 10 s after opening; the panel's lower rows are cut off by the Claude window beneath (not a Velora defect).
- Exit: `--exit` restored taskbar (visible), wallpaper (restored 1 monitor), no dirty marker, no resident Velora process.


## Phase A-D (2026-11-05): Level 1 (v1.0) build-out
Direction recorded first: v1.0 = **Level 1**, a blend of Windows and macOS (no injection); Level 2 (complete transformation, opt-in injected title bars) = v2.0, late 2026 / early 2027 (CLAUDE.md 1.1, ROADMAP.md, README, DECISIONS).

- Merged (CI green, squash): #63 Files, #66 Search (re-created: #64 conflicted after the squash of #63), #67 app menus tiers 1-2, #68 window theming, #69 shared window chrome (Files now maximises to the work area), #70 polish (Files/Settings Dock tiles use the Velora icons, game `.url` shortcuts in the launcher, Search hotkey hint), #71 Settings, #72 Overview, #74 Apple services. Also merged afterwards: #73 Control Center, #75 installer + release workflow (its CI installs and uninstalls the setup .exe and the .msi on the runner), #76 launch texts, #77 release-prep docs, #78 gaming mode. Nothing is open except a Dependabot bump.
- App-menu reopen bug: the Win32 submenu drill-in destroyed and re-created the popup window, which lost the activation race; the popup now swaps its rows in place (`MenuPopup::Update`). The CI test also had a stale title rectangle (the active-app label moves when a command brings the app forward); it now recomputes rectangles before every click.
- Mistake to remember: one window-opening test (`theming_test`, a child window + DWM attributes) ran on the desktop by accident when a build failed and the old binary ran. It created one tiny window of its own and restored it; the shell was not touched. Local runs of window tests now go through `VELORA_THEMING_NO_WINDOW=1`-style opt-outs or CI only.
- Not done in this pass (honest list): per-monitor Dock/menu bar (one Dock and one menu bar, on the primary monitor), a second-screen test, the 3-run bench, any live check, notify, desktops, Cmd-style hotkey remapping, Overview desktop strip/animation, Settings text IME, Windows build coverage other than 11 26200.

### Resume note (2026-11-05)
- Done: everything above that is merged; Settings/Overview/Control/Apple/theming/gaming are built and unit/CI tested; installers and the release workflow exist.
- In progress: nothing. All Level 1 code and docs are on main; the draft RELEASE_REPORT.md still needs the guided-session numbers.
- Next: ONE guided McAfee-off session (docs/QA.md, "Guided session for v1.0"): run the checks, capture screenshots into docs/media, run `tools/bench.ps1 -Runs 3`, then fill RELEASE_REPORT.md numbers, tag `v1.0.0` (the workflow creates a draft release), apply for SignPath (docs/SIGNING.md) and submit the antivirus forms.

### Per-monitor bars (PR feat/multimonitor)
- Built: `monitors/` (pure plan + diff + work-area restore planning + follow tracker; runtime MonitorBars), `dock.on` in Settings, secondary menu bars (app menus + clock), Docks on every monitor or following the pointer, hot-plug / resolution / DPI / primary-change handling (debounced WM_DISPLAYCHANGE), state.json records extra appbars and late monitors; restore uses the same pure rules.
- Tests: monitor_layout_test (61 checks, simulated sets). Switch gate unchanged (test profile creates no extra bars). **Not verified on real multi-monitor hardware.**

### Guided session (2026-11-05) - results
- Run with the owner (McAfee off): everything in docs/VALIDATION.md "Guided session" paragraph; bench medians in RELEASE_REPORT.md (private 79.8 MB, enter 143 ms, exit 437 ms, idle CPU 0 %).
- Bugs found live and fixed in #81: Settings window top under the menu bar (now centred in the work area); velora-files/-settings were themed (now never); notification toasts (ShellExperienceHost) appeared in the Dock with a blank tile (now ignored); the Dock showed Velora Files and File Explorer with the same blue folder (Velora Files now has its own icon, assets/icons/velora-files.svg).
- Resume: merge #81, tag v1.0.0 only after the owner has turned McAfee back on and decided about the open items in RELEASE_REPORT.md (second monitor, real game, installers on a real desktop). SignPath application and the antivirus forms are still the owner's.

### Security review (2026-11-05)
- docs/SECURITY_REVIEW.md: surfaces checked, 7 findings fixed, residual risks listed (unsigned binaries, same-user trust, no independent audit). security_test (3 046 checks) is new. Build flags verified in the PE headers (ASLR, high-entropy ASLR, DEP, CFG).
