# Menu Compatibility

Which apps' menus the menu bar can show and run. This table lists only what a test or a recorded run has actually exercised; everything else is **unmeasured**, not "works". CLAUDE.md sec. 5 asks for at least 20 real apps before "full" may be claimed; this is the start (Day 4, tiers 1 and 2), not that table.

## How it works

| Tier | Source | Read | Run | Notes |
|---|---|---|---|---|
| 1 | classic Win32 `HMENU` (`GetMenu`) | `GetMenuItemInfo` / `GetMenuString` on the foreground window; `WM_INITMENUPOPUP` is sent first (with a 300 ms timeout) so the app enables, checks and fills its items as its own menu bar would | `PostMessage(WM_COMMAND, id)` | nothing is injected into the app; owner-drawn and bitmap items expose no text and are left out |
| 2 | UI Automation `MenuBar` / `MenuItem` (a `Menu` control at the top of the window for WPF) | `FindAll` on the bar and its items; `ExpandCollapse` where the toolkit has it; popups of the same process for toolkits that open drop-downs as separate windows (Qt, XAML, Chromium: **written, not verified**) | `Invoke` (or `Toggle`) after re-opening the path | opening a menu through UIA can make the app open its own drop-down for a moment; cached for two minutes per window |
| 3 | generic File / Edit / Window menu from standard commands | not built (Day 6) | | |

The system menu in every title bar is a "menu bar" to UI Automation too; it is skipped (parent is the title bar). Elevated windows are not touched (the window tracker marks them uncontrollable). Windows of the Chromium family (`Chrome_WidgetWin_*`) are **not probed by default**: the first UI Automation request switches the app's accessibility on for good, which costs the user's browser performance. `settings.json` `{"menus": {"probeChromium": true}}` opts in.

Reads and runs happen on a worker thread, never on the shell's UI thread, and a hung app only delays its own menu.

## Verified in tests (`app_menu_test`, 71 checks, this machine, Windows 11 10.0.26200, non-elevated)

| App | Tier | What was exercised | Result |
|---|---|---|---|
| Velora menu fixture (`tests/fixtures/menu_fixture.cpp`, classic Win32 window in its own process) | 1 | bar of 4 titles incl. a command in the bar, drop-downs, submenu (2 levels), separators, disabled and checked items, shortcuts split off the label, `WM_INITMENUPOPUP` replay (a counter item changed on every open), `WM_COMMAND` delivered across processes, gone-window handling | passed |
| Windows Forms `MenuStrip` (hosted by Windows PowerShell 5.1) | 2 | bar, drop-downs (items sit in the UIA tree while closed; the toolkit has no expand pattern), submenu, disabled and checked (via MSAA state), `Invoke` of a command and of an item two levels deep, refusal of a disabled item and of a wrong label, menus left closed | passed. Shortcuts are **not** exposed by Windows Forms through UIA (empty) |
| WPF `Menu` (hosted by Windows PowerShell 5.1) | 2 | the same list | passed. WPF reports its menu as a `Menu` control (found by position at the top of the window), not a `MenuBar`; the `_` access keys are already stripped in UIA names. Shortcuts (`InputGestureText`) are **not** exposed (empty) |

## Not verified yet

| App / toolkit | Expected tier | Status |
|---|---|---|
| Notepad (Windows 11, XAML menu bar) | 2 | probe in `app_menu_test` behind `VELORA_MENU_TEST_APPS=notepad`; no result recorded yet |
| File Explorer, Paint, Calculator, Terminal | 2 or none | unmeasured |
| Microsoft Office, LibreOffice, Notepad++, 7-Zip, PuTTY, Regedit (elevated), MMC | 1 or 2 | unmeasured |
| Qt 5/6 apps (VLC, qBittorrent, OBS) | 2 | popup-window path written, unmeasured |
| Chromium / Electron (Chrome, Edge, VS Code, Discord, Slack) | 2 (opt-in) | not probed by default; unmeasured |
| Java Swing/AWT, GTK | none | unmeasured |

## Known limitations

- Menus of some apps are unreachable by any API (owner-drawn, custom-painted, in-document ribbons). They show the app name only.
- UIA submenus beyond the first level are fetched when the user opens them (about 0.1-0.5 s each); classic menus read two further levels at once.
- A UIA read makes the app flash its own drop-down.
- Keyboard accelerators of tier 2 apps are shown only when the toolkit reports `AcceleratorKey`.
