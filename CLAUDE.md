# VELORA — CLAUDE CODE MASTER IMPLEMENTATION PROMPT

You are the lead engineer for **Velora**, a free, open-source (Apache-2.0) Windows 11 app that turns the Windows desktop into a high-fidelity macOS-style environment, reversibly, without a reboot, VM, or kernel changes. Windows keeps running underneath, and every Windows app keeps running untouched.

Deadline: a working release candidate within **8 calendar days**. Correctness and user-data safety outrank the deadline. Never report unverified work as done.

---

## 0. OPERATING RULES (READ FIRST)

1. **Reuse first.** Before writing any subsystem, check the reuse matrix (§3). Write original code only for gaps and glue.
2. **Inspect before editing.** Never regenerate a working module. Make targeted diffs, not rewrites.
3. **Keep a compact record.** Maintain `docs/DECISIONS.md` (one line per decision: date, decision, reason) and `docs/PROGRESS.md` (per module: status `not started | in progress | gated | passed | deferred`, plus evidence). Do not re-read unchanged files to "refresh context". Use these logs.
4. **Time-box research.** Day 1 research ends by end of day. Record findings once in `docs/RESEARCH.md` and never repeat it.
5. **Test by risk, not by count** (§11).
6. **Never fabricate** benchmark numbers, compatibility results, or completion states. Unknown means "unmeasured".
7. **Legal.** No Apple or Microsoft proprietary code, icons, wallpapers, fonts (SF Pro), or binaries. Never disassemble Apple binaries. Clean-room only: public docs, observed behavior, screenshots. GPL code is study-only. The project is Apache-2.0, so copy no GPL code. Keep `THIRD_PARTY_NOTICES.md` updated whenever code is reused.
8. **Stability rules (never violate):** no admin rights for normal operation; never terminate or replace `explorer.exe`; no registry shell swap; no system-file changes; no disabling of security features or Windows Update; no DLL injection into other processes.
9. **No borrowed identities.** Never bundle third-party keys, certificates, tokens or device/accessory identities. Signing material is ours, kept out of the repo and the installer.
10. **Validation log.** Keep `docs/VALIDATION.md` (done / pending live tests per module) in step with PROGRESS.md evidence, and say what was not tested.
11. **No Apple design resources.** Never accept or use Apple Design Resources (UI kits, Sketch/Figma libraries, SF fonts, SF Symbols) or repos redistributing Apple icons/assets. Their license excludes non-Apple-platform software.

---

## 1. PRODUCT

- **Windows Mode (default):** a normal Windows 11 desktop.
- **Velora Mode:** started by `Velora.exe`. It provides a Dock, a global menu bar, a Finder-style file app ("Files"), Spotlight-style search ("Search"), a Mission Control-style overview ("Overview"), a Launchpad-style grid ("Apps"), Control Center, Notification Center, Settings, light/dark mode, wallpapers, multiple desktops, animations, keyboard shortcuts, multi-monitor support, and High-DPI rendering.
- **Exit:** returns to Windows instantly with all app state intact.
- **Naming:** use original names only. Never use "Mac", "macOS", "Finder", "Spotlight", "Mission Control", or "Launchpad" in the UI or binary names. A phrase like "macOS-inspired" in docs is fine.

### 1.1 Release levels (decided 2026-11-05)

- **Level 1 = Velora v1.0: a blend of Windows and macOS, not a full transformation.** Dock, global menu bar, launcher, Files, Search, Overview, Control Center, Settings, and window theming through documented DWM attributes (no injection, no reparenting, no subclassing of other apps' windows). Windows apps keep their own title bars and behavior.
- **Level 2 = Velora v2.0: the complete Windows-to-macOS transformation**, including opt-in injected title bars and deeper window chrome. Planned for late 2026 / early 2027. Level 2 work does not start before v1.0 ships, and the §0.8 stability rules (no injection by default) are relaxed only for explicitly opt-in, per-app, reversible features designed and reviewed in v2.0.
- Everything in this file describes v1.0 scope unless a section says "v2.0". Anything that needs injection is v2.0.

---

## 2. ARCHITECTURE (FINAL)

This is a hybrid architecture: a native resident shell, on-demand apps, and an isolated compatibility subsystem.

```
velora-watchdog.exe   (C++, ~2 MB)  restores Windows if the shell dies; owns the dirty-exit marker
velora-shell.exe      (C++/Win32, single resident process)
 ├─ core/        state store, event bus, config, IPC (named pipe, versioned JSON)
 ├─ switch/      enter/exit, taskbar hide/restore, appbar registration, state file
 ├─ render/      Direct2D + DirectComposition + DWM backdrop (Mica/Acrylic), animation engine
 ├─ dock/        pinned/running apps, magnification, badges, minimized windows
 ├─ menubar/     global menu bar + status items + clock
 ├─ windows/     window tracking (SetWinEventHook), activation, min/max/restore
 ├─ overview/    DwmRegisterThumbnail grid, desktop strip
 ├─ apps/        app discovery (Start menu .lnk, AppsFolder/IShellItem, MSIX), launch, grid
 ├─ search/      Windows Search OLE DB + app index; optional Everything SDK if present
 ├─ control/     Core Audio, WLAN API, Windows.Devices.Radios, battery, brightness (WMI/DDC)
 ├─ notify/      UserNotificationListener (via sparse-package identity) + Velora's own notifications
 ├─ desktops/    documented IVirtualDesktopManager only; undocumented interfaces sit behind a version-checked feature flag
 └─ hotkeys/     RegisterHotKey + low-level keyboard hook for Cmd-style remapping (opt-in)
velora-files.exe      on-demand; exits when closed. IFileOperation / IShellItem
velora-settings.exe   on-demand
compat/ (separate, not loaded by the shell)  →  §8
```

**Stack rules**
- Resident processes (shell, watchdog): **C++20, Win32, WIL, Direct2D/DirectComposition, C++/WinRT** only where a WinRT API is required. No .NET, WebView2, Qt, or Electron in resident processes.
- On-demand apps (`files`, `settings`): native C++ by default. Porting Apache/MIT C# code (Cairo Shell, Flow Launcher) is allowed **only if** it saves ≥1 day, measured RAM when open stays under 120 MB, and the app exits fully when closed. Record the choice in DECISIONS.md.
- Build: CMake + vcpkg (manifest mode), MSVC, x64 (ARM64 is a stretch build).
- UI fidelity: implement a small in-house design-token system (radius, blur, spacing, timing curves) in `render/`, shared by all modules. Use open-licensed icons (e.g. Fluent UI System Icons, MIT; Lucide, ISC) and original wallpapers.

**AnyPS5 principles adopted** (study-only; GPL-2.0, so no code)
- Reimplement and call native APIs directly instead of emulating. There is no runtime layer for the desktop.
- Fail loudly on unsupported states. In the shell, an unsupported state logs, degrades the single feature, and never crashes. In `compat/`, unsupported calls trap with a clear message, AnyPS5-style.
- Track coverage against a declared function list (`compat/COVERAGE.md`, `docs/PARITY.md`).

---

## 3. REUSE MATRIX — VERIFY ON DAY 1 (license, last commit, relevant files)

| Repo | Use | License action |
|---|---|---|
| cairoshell/cairoshell | appbar, tray-icon hosting (Shell_TrayWnd notify-icon interception), app grabber, menu bar logic | Apache-2.0: reuse/port with notice |
| microsoft/PowerToys | Run/Command Palette indexing, hotkeys, window enumeration, FancyZones multi-monitor math | MIT: reuse with notice |
| Flow-Launcher/Flow.Launcher | search ranking, Windows Search / Everything query code | MIT: reuse with notice |
| microsoft/wil, microsoft/cppwinrt | infrastructure | MIT/Apache |
| voidtools Everything SDK | optional fast filename search | check SDK terms; load dynamically, optional |
| glzr-io/glazewm, RamonUnch/AltSnap, LGUG2Z/komorebi | window-management behavior | GPL / custom: **study only** |
| boykopovar/AnyPS5 | relinker and system-library reimplementation patterns | GPL-2.0: **study only** |
| darlinghq/darling | Mach-O loader, libSystem/Foundation/AppKit structure | GPL-3.0: **study only** |
| gnustep (libobjc2, base, gui) | Objective-C runtime, Foundation, AppKit for compat | libobjc2 is MIT (reusable); base/gui are LGPL: separate DLLs only, dynamically linked, with notices |
| apple-oss CF-Lite / swift-corelibs-foundation | CoreFoundation | swift-corelibs is Apache-2.0 (preferred); APSL: check before use |
| morluto/rea | MCP server for agent-assisted reverse engineering | **Tool only.** May analyze **Windows** binaries and open-source projects for integration research. **Never** point it at Apple binaries. |

Day 1 task: discover up to 10 further candidates (docks, menu bars, DWM effects, virtual-desktop wrappers, Mach-O loaders, Metal→D3D/Vulkan translators such as MoltenVK-reverse projects), evaluate them against the 13 criteria (name/URL, function, language, license, maintenance, Windows support, modules, deps, RAM, storage, effort, security, verdict), and rank the top 5 by days saved. Stop at end of Day 1.

---

## 4. SWITCHING AND RECOVERY (HIGHEST PRIORITY)

**Enter**
1. Write `%LOCALAPPDATA%\Velora\state.json` (taskbar autohide and visibility, work areas per monitor, wallpaper, foreground window) and a dirty marker.
2. Start the watchdog, which watches the shell process handle.
3. Hide the taskbar reversibly (`Shell_TrayWnd` and `Shell_SecondaryTrayWnd` via ShowWindow, plus ABM autohide as a fallback). Explorer keeps running.
4. Register the menu bar and Dock as appbars so maximized windows respect them.
5. Never reparent, subclass, or inject into other apps' windows.

**Exit** (menu, hotkey, or tray): reverse every step from state.json, delete the marker, exit.

**Recovery**
- Watchdog restores Windows within 1 s of a shell crash or hang (heartbeat timeout of 3 s).
- On login, if a dirty marker exists, restore Windows defaults before anything else.
- Emergency hotkey `Ctrl+Alt+Shift+Esc` is handled by the watchdog, so it works even when the shell is frozen.
- A Start-menu shortcut "Restore Windows Desktop" runs `velora-watchdog.exe --restore`.
- Recovery must also be correct after an Explorer restart (`TaskbarCreated` message): re-hide the taskbar and re-register appbars.

---

## 5. MODULE REQUIREMENTS AND ACCEPTANCE GATES

Each module has gates. **G1** = minimum to ship; **G2** = target; **G3** = stretch. Report the level actually passed.

- **dock:** G1 pinned and running apps, launch/activate/minimize, indicator dots, per-monitor. G2 magnification, minimize-into-Dock animation, jump-list menus, recent folders. G3 live thumbnails on hover.
- **menubar:** G1 active app name, clock, status items (tray icons re-hosted via Cairo approach), Velora menu (About, Settings, Sleep, Restart, Shut Down via documented APIs, Exit Velora). G2 **global app menus**, see the dedicated track below. G3 per-monitor menu bars.
- **Global menu track:** tier 1 classic Win32 `HMENU` (GetMenu + WM_INITMENUPOPUP replay via `PostMessage WM_COMMAND`); tier 2 UI Automation menu discovery for WinForms/WPF/Qt/Chromium/Electron (MenuBar/MenuItem control patterns, Invoke); tier 3 a generic fallback menu (File/Edit/Window built from standard commands such as Ctrl+N, Ctrl+S, close, minimize, zoom). Keep a per-app compatibility table in `docs/MENU_COMPAT.md` with real test results for at least 20 common apps. "Full" means tiers 1–3 work and the table is honest. Never claim 100% of apps.
- **windows:** track all top-level windows with SetWinEventHook. Handle cloaked (UWP) windows, elevated windows (degrade: cannot control, show a badge), and fullscreen detection (auto-hide the bars).
- **overview:** G1 DWM thumbnail grid of all windows on the current desktop, click to activate, hotkey + hot corner. G2 animated layout, app grouping, desktop strip with add/switch. G3 drag a window to another desktop.
- **apps (Launchpad):** G1 grid of all installed apps (Win32 + MSIX), search, launch. G2 pages, folders, drag-reorder.
- **search:** G1 apps + files (Windows Search OLE DB), keyboard-only flow, open/reveal. G2 calculator, unit conversion, settings entries, Everything if installed.
- **files:** G1 browse drives/folders, open, copy/move/rename, delete to the Recycle Bin, drag-drop in and out, a sidebar (favorites/drives), list/icon/column views, all via IFileOperation. G2 tabs, Quick Look-style preview (Windows preview handlers), tags as a Velora-local DB. **Never** permanently delete without an explicit confirm dialog.
- **control:** G1 volume, output device, Wi-Fi status + network list (WLAN API), Bluetooth on/off (Radios API), battery, brightness, dark mode, Do Not Disturb (Velora-level). G2 media controls (GlobalSystemMediaTransportControls), display scaling shortcuts.
- **notify:** G1 Velora's own notifications + a Notification Center panel. G2 **capture system toasts** via `UserNotificationListener`. This requires package identity: ship a **sparse MSIX package with external location**, signed with the release certificate, and request access on first run. If the user denies access, degrade cleanly. Document that some apps' toasts may differ in content.
- **settings:** G1 appearance, Dock, menu bar, hotkeys, startup, gaming mode, updates, a "Restore Windows" button. All settings live in one JSON in %LOCALAPPDATA%. Diagnostics export is opt-in, written locally for the user to review, and never auto-sent (backlog: `docs/BACKLOG.md`).
- **desktops:** G1 the documented `IVirtualDesktopManager` (move/query). G2 create/switch through version-detected undocumented interfaces behind a flag. If the Windows build is unknown, disable it and fall back to simulated keystrokes (Win+Ctrl+arrows).
- **hotkeys:** Cmd-style mappings are opt-in: Alt↔Win as Cmd, Cmd+Space search, Cmd+Tab app switcher, Ctrl+Up overview, Cmd+Q quit (WM_CLOSE). Don't break Windows shortcuts by default.
- **gaming mode:** auto-enabled when a fullscreen exclusive or borderless-fullscreen app is foreground (or manually). Disable animations and blur, pause the indexer and thumbnails, hide the bars, drop to minimal timers. Never touch the game process.
- **updates:** GitHub Releases with Stable and Preview channels. Verify downloads with SHA-256 plus a signature (minisign or Authenticode). User-confirmed install; keep the previous version for rollback. Track macOS UI changes in `docs/PARITY.md` (manual backlog from Apple's public release notes).
- **apple-services (optional, owner C, scheduled D6 after the G1 gates of other modules; off by default, enabled in Settings):** launch and surface Apple's own Windows apps. G1 detect Apple Music, Apple TV, Apple Devices, iCloud for Windows and iTunes (MSIX/registry); launch them; offer Microsoft Store install links (`ms-windows-store://` URIs) for missing ones; an "Apple" group in the launcher. G2 iCloud Drive and iCloud Photos folders in the Files sidebar when present (read the local sync paths; no network calls). G3 an "App Store (web)" entry that opens apps.apple.com in the default browser, labeled browse-only. **Hard rules:** Velora never asks for, reads, stores or transmits Apple Account credentials or tokens; sign-in happens only in Apple's own apps. No scraping or reimplementing Apple's private APIs.

---

## 6. BUDGETS (MEASURE, DON'T CLAIM)

- **Idle RAM:** system-wide committed-memory delta < 150 MB in Velora Mode with the on-demand apps closed. Also record per-process working set and private bytes, GPU memory (DXGI QueryVideoMemoryInfo), idle CPU (target < 1%), enter latency (target < 1.5 s), exit latency (target < 1 s).
- **Method:** `tools/bench.ps1`. Clean boot, wait 2 min, sample 60 s before and after, 3 runs, report median and range. Commit the raw CSVs to `bench/`.
- **Install size:** target < 40 MB, excluding compat/. compat/ is a separate optional component with its own budget.
- Any deviation goes in the release report with numbers.

---

## 7. EIGHT-DAY PLAN

Parallelizable owners in brackets (§10).

- **D1:** research and reuse matrix (time-boxed); repo skeleton, CMake/vcpkg, CI (GitHub Actions: build + unit tests); IPC contract v1; design tokens. [all]
- **D2:** switch/, watchdog, recovery, state file, Explorer-restart handling; windows/ tracking; render/ base. **Gate:** 50 automated enter/exit cycles and 10 forced-kill recoveries pass with zero lost windows. Nothing else merges until this passes. [A]
- **D3:** dock G1, menubar G1, wallpaper, apps G1, animation engine. [B] compat: Mach-O parser + loader skeleton. [D]
- **D4:** files G1, search G1, global menu tiers 1–2. [B, C] compat: dyld-style binding + libSystem subset. [D]
- **D5:** overview G1–G2, control G1, notify G1 + sparse package, settings G1, hotkeys, desktops G1. [B, C] compat: Objective-C runtime + CoreFoundation. [D]
- **D6:** performance pass and bench; gaming mode; startup tuning; notify G2; menu tier 3. [A, C] compat: Foundation subset, CLI target app. [D]
- **D7:** integration tests, multi-monitor/DPI matrix, MENU_COMPAT table, G2 work, bug fixes. compat: GUI attempt (§8 G3). [all]
- **D8:** installer, signing, acceptance run, release report, GitHub release candidate. Code-sign every release binary and the installer (Authenticode, our own certificate, §0.9), then submit the signed release builds to McAfee and Microsoft (Defender) as false positives before launch: unsigned new executables that hide the taskbar and manage windows are quarantined by reputation heuristics (seen on a developer machine: McAfee "ti!" detections). Record the submission ids and outcomes in `RELEASE_REPORT.md`. [A]

If a module misses its gate by its day, ship it at the last passed gate and move on. Do not let one module consume the schedule.

---

## 8. COMPAT SUBSYSTEM — macOS APP EXECUTION (SEPARATE, OPTIONAL COMPONENT)

The approach is AnyPS5-inspired: load and link Mach-O natively, with reimplemented system libraries as Windows DLLs. There is no emulator process. Target **x86_64 Mach-O** only. ARM64 apps would need binary translation, so record that as out of scope with a reason.

- **Loader** (`compat/loader`, original code): parse Mach-O (fat/thin, LC_SEGMENT_64, LC_DYLD_INFO/chained fixups, LC_LOAD_DYLIB), map segments with VirtualAlloc, apply rebases/binds, set up TLS, and run initializers. Install a vectored exception handler and translate macOS `syscall` instructions into the libSystem shim. Note the TLS register difference (macOS uses GS differently) and solve it explicitly.
- **libSystem shim** (`compat/libsystem`): POSIX subset mapped onto Win32 (files, mmap, pthreads, time, malloc, dlopen for the shim DLLs). Every unimplemented symbol binds to a stub that traps with its name and logs it to `COVERAGE.md`.
- **Runtime:** libobjc2 (MIT) as the Objective-C runtime; swift-corelibs-foundation / CoreFoundation (Apache-2.0) where buildable on Windows; GNUstep base/gui (LGPL) only as separately loaded DLLs.
- **Graphics:** no Metal in this sprint. Record a design note for Metal→D3D12 in `docs/compat/METAL.md`.
- **Gates:** **G1** a self-compiled x86_64 Mach-O CLI (C, libSystem only) prints output and exits correctly. **G2** an Objective-C/Foundation CLI (NSString, NSArray, NSLog) runs. **G3** a minimal AppKit window (NSWindow + NSButton) renders via the GNUstep gui backend. Test binaries must be **built by the project** from source (osxcross or CI on a macOS runner), never downloaded third-party apps.
- **Isolation:** compat/ never loads into velora-shell.exe and is installed only if the user selects it.

---

## 9. INSTALLER AND RELEASE

- Inno Setup, per-user install (no admin), optional compat component, sparse-package registration for notify, Start-menu entries (Velora, Restore Windows Desktop). The uninstaller runs `--restore` first and removes everything except user-chosen data.
- Release artifacts: setup.exe, portable zip, SHA-256 checksums, signature, `THIRD_PARTY_NOTICES.md`, `RELEASE_REPORT.md` (each module's gate level reached, bench numbers, known limitations, MENU_COMPAT summary, compat coverage).

---

## 10. AGENTS AND GIT

- **Owners:** A = switch/watchdog/core/render/perf/installer · B = dock/menubar/apps/overview · C = files/search/control/notify/settings/hotkeys/desktops · D = compat/.
- **Shared contracts:** `core/contracts/*.h` and `ipc/v1.json` are owned by A. Changes need a DECISIONS.md entry and must stay backward compatible within the sprint.
- **Git:** one branch per module (`feat/<module>`), small PRs, rebase on main, CI must pass. Agents never edit another owner's directory; they open an issue or write a note in PROGRESS.md instead. Conventional commits. Main is always releasable after D2.

---

## 11. TESTING (RISK-BASED)

Priority order, all automated where possible:
1. Enter/exit cycles and window preservation (enumerate windows before and after; they must match exactly).
2. Crash recovery: kill -9 the shell, hang the shell, restart Explorer, log off with the dirty marker set.
3. File safety: copy/move/rename/delete on a temp tree, including conflict, permission-denied, long-path, and locked-file cases. Verify hashes. Recycle Bin only.
4. Process preservation: no process other than Velora's own ever terminated (assert via a snapshot diff).
5. Window management on 1, 2, and 3 monitors at mixed DPI (100/150/200%).
6. RAM/CPU bench (§6).
7. App launching: Win32, MSIX, Store, elevated-prompt apps.
8. Installer, uninstaller, upgrade, rollback.

Manual checklist for visuals and animations goes in `docs/QA.md`. Don't write snapshot tests for pixels.

---

## 12. DEFINITION OF DONE (PER FEATURE)

UI works · underlying function works on real Windows resources · user data preserved · relevant tests pass · within budget or deviation reported · limitations documented · PROGRESS.md updated with evidence. Release requires all G1 gates in §5, the D2 safety gate, the bench, and a working installer. Anything below G1 is marked "unsupported" in the release report, not hidden.

---

## 13. KNOWN LIMITATIONS TO DOCUMENT (EXPECTED)

Menus of some apps are unreachable by any API; elevated windows can't be controlled from a non-elevated shell; toast capture depends on user consent and the app; undocumented virtual-desktop APIs can break with Windows updates; exact macOS behaviors (e.g. app-level vs. window-level focus) don't map 1:1 onto Windows; compat runs only simple x86_64 programs and supports no Metal; Mac App Store apps can't be downloaded or run, and Velora's compat layer is not an App Store runtime.

**Start now with Day 1. First output: `docs/RESEARCH.md` with the verified reuse matrix, then the repo skeleton.**
