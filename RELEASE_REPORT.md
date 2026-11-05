# Velora v1.0.0 (Level 1) - release report

**Status: RELEASED as v1.0.0 (2026-11-05).** Numbers come from the guided session of 2026-11-05 (Windows 11 10.0.26200, one 1280x720 laptop screen at 150 %, dark mode, non-elevated, McAfee scanning off by the owner) and from CI. Items marked *unmeasured* or *not verified* were not covered. Nothing here is a claim of full acceptance; the release is knowingly published with those open items, unsigned (see below).

Direction: v1.0 = **Level 1**, a blend of Windows and macOS (no injection). Level 2, the complete Windows-to-macOS transformation with opt-in injected title bars, is v2.0, planned for late 2026 / early 2027.

## Gate level per module

| Module | Level reached | Evidence | Open |
|---|---|---|---|
| switch / watchdog / recovery | G1 (safety gate) | Day-2 gate 117/117 locally (2026-11-01, after #34-#38) and in CI on every PR; the cold-start flake was fixed in #58 | re-run by hand in the guided session; Explorer restart on other Windows builds; 2-3 monitors |
| windows tracking | G1 | window_tracker_test | cloaked (UWP) windows, a real elevated window |
| dock | G1 (per-monitor built, **not verified on real multi-monitor hardware**) | dock_test, live checks on one monitor (this session: pinned + running apps, Velora Files and File Explorer now have distinct icons, toast windows no longer get a tile); `monitors/` plans one menu bar per monitor and a Dock on the primary monitor, on every monitor, or following the pointer (`dock.on`), each its own appbar sized for that monitor's DPI; monitor_layout_test covers 1/2/3 monitors, mixed 100/150/200 %, negative coordinates, primary on the right, hot-plug and primary change | **per-monitor behaviour has only been tested with simulated monitor sets**; jump lists and live thumbnails (G2/G3) not built |
| menu bar | G1 + app menus tiers 1-2 | menubar_test, app_menu_test, menubar_appmenu_test (CI) | tier 3 fallback menu **not built**; the 20-app MENU_COMPAT table is **not complete** |
| apps (launcher) | G1 | apps_test, icons_test; game `.url` shortcuts | pages/folders (G2) not built |
| search | G1 | search_core_test, search_test; **live**: Alt+Space opened it (owner's keypress), results with icons shown | file-result quality on a large index, Windows Search disabled |
| files | G1 | files_test (temp-tree safety cases, hashes), files_window_test; **live**: window, sidebar, list view, traffic lights drawn correctly on a folder with sample files | live drag and drop with Explorer, delete to the Recycle Bin from the UI, tabs/preview/tags (G2) |
| overview | G1 | overview_test, overview_window_test; **live**: opened by Ctrl+Alt+Up (owner's keypress) and by `--open-overview`, thumbnails with titles on a blurred backdrop | animation, desktop strip (G2); hot corner, UWP/elevated thumbnails not checked live |
| control | G1 (see limits) | control_test (read-only probes answered on the developer machine), control_center_window_test; **live**: the panel showed Wi-Fi, Bluetooth, dark mode, DND, brightness 50 %, volume 30 %, output device and battery matching the machine | real writes unverified; output-device switching not implemented; media controls (G2) not built |
| settings | G1 | settings_model_test, settings_window_test; **live**: opens centred (a placement bug found live is fixed in #81), pages and toggles drawn correctly | live effect of each toggle on a running shell, startup entry, Restore now: not exercised live |
| window theming | built (not in the original gate list) | theming_test incl. a real cross-process window (CI); **live on 6 real apps** (Notepad, Explorer, Settings, Chrome, Word, Claude): attributes set, then restored on exit and after a killed shell (THEMING_COMPAT.md) | Edge, VS Code, Terminal, Calculator, elevated, fullscreen and game windows not measured live |
| gaming mode | G1 | gaming_test; **live**: manual "on" hid the Dock and menu bar, Overview refused to open, shell private bytes 82 MB | automatic trigger with a real fullscreen/borderless game not exercised |
| apple-services | G1-G3 | apple_test | not seen with the Apple apps installed |
| wallpaper | G1 (1 monitor) | wallpaper_test, live restore on exit and crash | multi-monitor |
| notify | **unsupported in v1.0** | not built | Do Not Disturb is a flag only |
| desktops | **unsupported in v1.0** | not built | |
| hotkeys | partial | Search and Overview hotkeys (RegisterHotKey) | Cmd-style remapping not built |
| updates | **unsupported** | the Settings button opens the Releases page | no updater, no rollback |
| installer | built; **tested on a real desktop** | Inno Setup .exe and WiX .msi: per-user install (4.8 MB), run from the installed copy, uninstall while Velora runs (taskbar, work area, wallpaper, window styles restored first, no leftovers, settings kept), upgrade from 0.0.1 while running; also the CI smoke test on every build | unsigned; Start-menu shortcut click-through and SmartScreen behaviour not observed |
| compat (macOS apps) | **not part of v1.0** | not started (Level 2 candidate) | |

## Bench (tools/bench.ps1, 3 runs, 2026-11-05, raw CSVs `bench/20261105-144041_*`)

One desktop (not a clean boot; Chrome, Claude, Office and other apps running), 120 s wait, 60 s samples, Dock + menu bar + wallpaper + apps index active, Apps panel never opened:

| Metric | Median | Range | Budget |
|---|---|---|---|
| Shell + watchdog private bytes | 79.8 MB | 78.7 .. 80.2 | < 150 MB: met |
| Shell + watchdog working set | 121.4 MB | 120.4 .. 121.5 | (not budgeted) |
| Idle CPU | 0 % | 0 .. 0 | < 1 %: met |
| GPU local memory | 42.2 MB | 41 .. 42.2 | (reported) |
| Enter latency (shell start -> taskbar hidden) | 143 ms | 136 .. 291 | < 1.5 s: met |
| Exit latency (`--exit` -> processes gone) | 437 ms | 433 .. 458 | < 1 s: met |
| System-wide committed delta | 9.8 MB | -54.1 .. 25.8 | **inconclusive**: other applications moved the machine's commit by more than Velora uses; read the per-process figures |

Also seen in the session: kill of the shell -> watchdog finished the restore in ~220 ms (taskbar visible after ~90 ms); a normal exit restored in ~330 ms; with gaming mode on the shell's private bytes were 82 MB. Not measured: enter latency on a cold boot, install size (see the release workflow artifacts), memory with several secondary-monitor bars, the Apps panel open (last recorded 175 MB private in PROGRESS.md before the v1.0 modules were added).

## Known limitations

Per-monitor menu bars and Docks are built but not verified on real multi-monitor hardware; some apps' menus unreachable; elevated windows cannot be controlled; apps with their own frame may ignore title-bar theming; undocumented/old Windows builds only partly tested (developed on 11 26200); toast capture, virtual desktops, Cmd-style remapping, updater and macOS-app compatibility are not in v1.0; output-device switching opens Windows' Sound settings.

## Not tested (honest list)

Everything marked *unmeasured* above, plus: any run of the release installers on a real desktop; behaviour with Velora running during an uninstall; clean-boot bench; a second monitor; mixed DPI; a real game; the Apple apps installed; Windows 10 and earlier Windows 11 builds; screen readers/accessibility; IME input in Velora's text fields (not supported); Wi-Fi networks that need a password (Velora does not handle keys: Windows' own settings open).

## Antivirus submissions

Not yet made (the user submits; see docs/SIGNING.md). Record here: Microsoft submission id ______ outcome ______; McAfee submission id ______ outcome ______.

## Signing

Unsigned unless the SignPath Foundation application has been approved and the repository variable is set; the draft release notes state which. SHA-256 of every file is in `SHA256SUMS.txt`.

## Compat coverage

Not applicable to v1.0 (the compat subsystem is not part of this release).
