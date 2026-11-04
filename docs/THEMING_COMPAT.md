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
| Real apps (Explorer, Notepad, Edge, Chrome, VS Code, Office, Terminal, a UWP app) | **unmeasured**: part of the guided live check; this table is filled from the `theming:` lines in the shell log (`attribute=set/-`, `ignored`, `refused`) |

## Known limits

- Apps that draw their own frame (Chromium/Electron with custom chrome, many games) may ignore caption and backdrop attributes; the corner and dark attributes still apply to the system frame where there is one.
- Apps that set the same attributes themselves (e.g. an app-level dark mode) win; Velora detects that and stops managing the attribute.
- Windows 10 and Windows 11 before build 22000: the attributes that do not exist are refused and counted; nothing is faked.
