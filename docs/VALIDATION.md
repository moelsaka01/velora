# Validation

Physical and live checks per module, seeded from the evidence in [PROGRESS.md](PROGRESS.md). **done** = run on a real Windows 11 machine (10.0.26200, 1 monitor, non-elevated, real Explorer). **pending** = not run yet. Anything not listed is unmeasured. Nothing here is a claim of full-release acceptance.

| Module | Done | Pending |
|---|---|---|
| switch / watchdog | 50 enter/exit cycles, 10 forced shell kills (restore < 1 s), hang, hotkey, login-marker and `--restore` paths; 117/117 checks, raw JSON in `docs/evidence/` | Explorer restart on other Windows builds; log off with the marker set on a clean boot; 2-3 monitors at mixed DPI |
| core / render | unit tests; D3D11 + composition swap chain draw/present/resize | GPU memory (DXGI) measurement; non-DWM-Mica fallbacks |
| windows | WinEvent lifecycle, own-process skip, fullscreen detection (live, with `VELORA_TEST_FOCUS=1`) | cloaked (UWP) windows; a real elevated window |
| dock | drawn live; Velora app icon decode/scale at 48-128 px (velora_icon_test: transparent corners, opaque centre, premultiplied); decision logic, geometry, persistence tests | click paths (launch, activate, minimize) on real windows; per-monitor on 2+ monitors |
| menubar | live in the top appbar; Velora menu, active app, battery, clock | tray-icon hosting (blocked); power menu actions; global app menus (tiers 1-3) |
| apps | index of 191 apps, grid with search drawn live | launching from the grid; MSIX/Store/elevated launches |
| wallpaper | apply, backup, restore on exit and on crash recovery (1 monitor) | multi-monitor |
| input safety | code audit of hotkeys/hooks/SendInput/registry/affinity (no persistent write found); `--exit` process check; ⋯ key release + focus guard (unit build only) | **PrintScreen and Win+Shift+S during and after Velora Mode** (tools/input_check.ps1 -Send, before/during/after) - the original failure is unexplained and unreproduced; user reports it cleared after a reboot |
| icons | `icons_test` on this machine: 191/191 Start apps get an icon via the shell image factory (Chrome, Edge, Steam, Discord, Epic Games Launcher, Calculator [MSIX], Notepad all checked by name); fallback-only run (factory disabled): 91/99 apps that have a file to fall back to | the running-unpinned-Chrome Dock tile in a live session (the "G" tile came from AUMID resolution, fixed in code, not reproduced live); MSIX apps without an exe cannot use the later steps |
| bench | scripted runs with raw CSVs in `bench/` | clean-boot method; GPU memory; 3-run medians on a second machine |
| overview, search, files, control, notify, desktops, hotkeys, settings | none | all (modules not built or not yet verified) |
| compat | none | all |
| installer / updates | none | install, upgrade, rollback, uninstall runs `--restore` first |

Update this table in the same PR that changes a module's evidence in PROGRESS.md.
