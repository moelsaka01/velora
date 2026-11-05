# Window theming compatibility

Velora v1.0 (Level 1) themes other apps' windows only through **documented DWM window attributes** set from the shell process (`DwmSetWindowAttribute`). Nothing is injected, subclassed or reparented. Apps keep their own title bars; Level 2 (v2.0) is where opt-in custom title bars would live.

## What is set

| Attribute | Value | When |
|---|---|---|
| `DWMWA_WINDOW_CORNER_PREFERENCE` | round | `theming.corners` |
| `DWMWA_USE_IMMERSIVE_DARK_MODE` | follows the system theme | `theming.caption` |
| `DWMWA_BORDER_COLOR` | neutral hairline | `theming.caption` |
| `DWMWA_CAPTION_COLOR` / `DWMWA_TEXT_COLOR` | neutral | `theming.caption` and `backdrop = none` |
| `DWMWA_SYSTEMBACKDROP_TYPE` | Mica / Acrylic / Tabbed | `theming.backdrop`, Windows build >= 22621 |

With a backdrop the caption colour stays default so the material shows through the title bar.

## Never touched

Velora's own windows, elevated or inaccessible processes (not controllable from a non-elevated shell), fullscreen windows, cloaked and hidden windows, shell windows (taskbar, desktop), tool windows, windows without a caption or sizing frame, games (launcher library folders such as `steamapps\common`, `Epic Games`; engine window classes such as `UnityWndClass`, `UnrealWindow`) and anti-cheat processes (EasyAntiCheat, BattlEye, Vanguard, FACEIT, ...). Per-app exclusions: `settings.json` -> `theming.exclude` (exe file names), editable in Velora Settings.

## Restore

- Before an attribute is changed its value is read (so it can be put back), and after the change it is read again; a change that does not show up is undone and counted as "ignored".
- Windows 11 does not let the three colour attributes be read back; they are assumed to be `DWMWA_COLOR_DEFAULT` before and are restored to it without comparing ("blind").
- Every change is written to `%LOCALAPPDATA%\Velora\theming_backup.json` before `Apply` returns. Normal exit restores in-process; the shell crashing, the watchdog, `--restore` and login recovery all call `theming::RestoreFromBackup()` from `RestoreWindowsDesktop`.
- An attribute is put back only if it still has the value Velora set (an app that changed it itself wins).

## Evidence

| Check | Result |
|---|---|
| Policy matrix, settings parsing (`theming_test`, pure part) | passed locally (33 checks) |
| DWM attributes on a window of **another process** (`theming_test` child window) | one local run (Windows 11 10.0.26200, unintended: the window part of the test ran on the developer machine): corner, dark mode and backdrop were applied and read back; border colour was not readable (handled as "blind"); `RestoreAll` returned every readable attribute to its original value. CI runs the full test on every PR; its log lines (`INFO cross-process apply ...`) are the record for the runner image |
| Real apps (guided session 2026-11-05) | measured on 6 apps, see "Real apps" below; Edge, VS Code, Terminal, Calculator, elevated/fullscreen/game windows not measured live |

## Known limits

- Apps that draw their own frame (Chromium/Electron with custom chrome, many games) may ignore caption and backdrop attributes; the corner and dark attributes still apply to the system frame where there is one.
- Apps that set the same attributes themselves (e.g. an app-level dark mode) win; Velora detects that and stops managing the attribute.
- Windows 10 and Windows 11 before build 22000: the attributes that do not exist are refused and counted; nothing is faked.


### Real apps (guided session, 2026-11-05)

Method: windows were read with `DwmGetWindowAttribute` (corner = `DWMWA_WINDOW_CORNER_PREFERENCE`, dark = `DWMWA_USE_IMMERSIVE_DARK_MODE`, backdrop = `DWMWA_SYSTEMBACKDROP_TYPE`) before Velora, while it ran, and after exit or after `Stop-Process -Force` of the shell; the shell's `theming:` log lines say which attributes Velora changed (`set`) on each window. The border colour cannot be read back on this build, so it is set "blind" and its visible effect is **unmeasured**. Backdrop values: 0 = auto, 1 = none, 2 = Mica, 3 = Acrylic, 4 = Tabbed.

| App (window class) | Changed by Velora | Before -> while themed -> after exit | Notes |
|---|---|---|---|
| Notepad (Store build, `Notepad`) | corner, backdrop, border | corner 0 -> 2 -> 0; backdrop 4 -> 2 -> 4; dark already 1 | restored on exit (`velora-shell --exit`) and after a killed shell (watchdog, 3 windows restored in ~220 ms) |
| File Explorer (`CabinetWClass`) | corner, backdrop, border | corner 0 -> 2 -> 0; backdrop 4 -> 2 -> 4 | Explorer sets Tabbed itself: in one run it had put 4 back within 12 s (the app wins; Velora stops managing that attribute), in another it stayed 2 for the 7 s observed |
| Windows Settings (`ApplicationFrameWindow`, UWP) | corner, backdrop, border | corner 0 -> 2 -> 0; backdrop read 1 while themed | the UWP host re-applied "none": backdrop is not honoured, corners are |
| Google Chrome (`Chrome_WidgetWin_1`) | corner, border | corner 0 -> 2 -> 0 | dark (1) and backdrop (2) were already the wanted values, so they were left alone |
| Microsoft Word (`OpusApp`, Office start screen) | dark, backdrop, border | corner already 2; dark 0 -> 1; backdrop -> 2 | the sign-in dialog (`NUIDialog`) was not touched (dialog) |
| Claude desktop (Electron, `Chrome_WidgetWin_1`) | border only | corner 2 / dark 1 / backdrop 2 already | nothing visible changes |
| Velora's own Files and Settings windows | **were themed by mistake** | dark and backdrop were set on them | fixed in #81: `velora-*.exe` windows are never themed (`theming_test` pins the rule) |

Not measured in this session: Edge, VS Code, Windows Terminal, Calculator, an elevated window (the rule is unit-tested: `uncontrollable`), a fullscreen window, a game. Chrome's about:blank window and the Office start screen were only inspected through the attributes and one screenshot; no visual regression was seen, but the screenshot is not a pixel comparison.
