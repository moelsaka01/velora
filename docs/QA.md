# Manual QA checklist (visuals, input, power)

Automated tests cover logic; pixels, animations and system-affecting actions are checked by hand. Record the date,
Windows build and display setup next to a run. Items marked **(live)** change the real desktop: start from a quiet
desktop with nothing unsaved, and know where `velora-watchdog.exe --restore` and `Ctrl+Alt+Shift+Esc` are.

## Menu bar
- [ ] Bar spans the monitor, no white strip, text crisp at 100/150/200 % (compare logo/clock size to the 28 DIP height).
- [ ] Active app name follows focus (Notepad, Explorer, a Store app such as Calculator shows its window title).
- [ ] Clock shows local date/time and ticks over on the minute; battery item hidden on a desktop PC.
- [ ] Light/dark: toggle Settings > Personalization > Colors > Choose your mode (Windows mode); the bar follows without restart.
- [ ] Transparency effects off: bar turns opaque, still readable.
- [ ] Velora menu opens on click and closes on Esc / click away; About shows the version; Settings launches velora-settings.exe; Apps opens the grid.
- [ ] **(live)** Sleep / Restart / Shut Down each show a confirm dialog; **No** (the default) does nothing. Only choose Yes when ready.
- [ ] Exit Velora returns the taskbar and removes the bars.

## Dock
- [ ] Pill is centred above the bottom edge, rounded, translucent; maximized windows stop above it.
- [ ] Pinned icons: Apps, Files, Settings, File Explorer, Edge. Running unpinned apps appear after the divider with a dot.
- [ ] Click a pinned app that is not running: it launches. Click a running app in the background: its window comes to the front (restores if minimized).
- [ ] Click the foreground app with one window: it minimizes. With two windows of one app: clicks alternate between them.
- [ ] Right click: window list, Quit closes politely (an app with unsaved work asks first), Keep in Dock / Remove from Dock persist after Exit and re-enter.
- [ ] An elevated app (run Task Manager as admin) shows the orange badge; the Dock does not try to minimize or close it.
- [ ] Hover highlight fades in/out smoothly; the Dock slides up when Velora Mode starts.

## Apps (grid)
- [ ] Opens from the Velora menu and the Dock's first icon; icons fill in within a second or two; search narrows as you type, Backspace widens.
- [ ] Arrows move the selection, Enter launches it, Esc clears the search then closes, clicking empty space or another window closes it, wheel scrolls.
- [ ] **(live)** Launch a Win32 app, a Store app and a shortcut-only app from the grid.
- [ ] After closing, memory returns (see bench); opening again does not rescan (log shows no new "apps:" scan lines).

### Phase 3 visual check (menu bar, Velora menu, Apps panel) - guided, McAfee off, non-elevated PowerShell
- [ ] **(live)** The bar is 30 pt tall, text is Inter 13 pt, titles and right-hand items are 21.75 pt apart, no line under the bar, text readable on a light and a dark wallpaper (the veil is a working value).
- [ ] **(live)** Hovering the logo shows a fully rounded capsule (23 pt tall); clicking opens the drawn menu 4.5 pt under it: 24 pt rows, 201.5 pt wide, accent highlight with white text, hairline separators; Up/Down/Home/End/Enter/Esc work; clicking the logo again, clicking elsewhere or switching windows closes it; Sleep/Restart/Shut Down still ask first.
- [ ] **(live)** The menu corners look acceptable (DWM draws 8 px; the measured 11 pt is not reproduced) and its fill is readable in light and dark mode.
- [ ] **(live)** Apps panel: 843 pt wide glass panel (blurred wallpaper under a dark veil), search pill, 7 columns of 60 pt icons at 115 x 102 pt; opens with a spring (no overshoot), closes with the reverse spring on Esc / click outside; launching an app closes it at once. Check at 100 / 125 / 150 / 200 % and with transparency effects off (opaque dark fill).
- [ ] **(live, bench)** `powershell -File tools\bench.ps1 -Runs 1 -GridSteps`: private bytes idle, with the Apps panel open and after closing it (prompts tell you when to open and close it); raw CSV in `bench/`.
- [ ] **(live)** `velora.log` shows `apps grid: backdrop WxH px (1/N size), X ms` on each open (X should stay well under 100 ms).

## Wallpaper
- [ ] **(live)** On enter every monitor shows the Velora wallpaper (dark theme: deep blue/violet, light theme: pastel); no banding visible.
- [ ] Exit restores the previous wallpaper on every monitor, including a different one per monitor.
- [ ] If you pick another wallpaper while in Velora Mode, exit leaves yours alone.
- [ ] Kill velora-shell.exe: the watchdog restores the taskbar and the wallpaper.

## Multi-monitor / DPI (not yet automated)
- [ ] 1, 2 and 3 monitors at mixed 100/150/200 %: bars and Dock on the primary monitor, correct size, no overlap with the other monitor.
- [ ] Change scaling while running: bars resize and stay crisp.

## Files (Day 4)
- [ ] **(live)** Window: 216 pt sidebar (Favorites: Home, Desktop, Documents, Downloads, Pictures, Music, Videos; Drives), 52 pt toolbar with back/forward and the list/icon switch, traffic lights top-left (close/minimise/zoom, glyphs on hover, grey when inactive), rounded corners and shadow, drags by the toolbar, resizes from every edge, maximises without overhanging the monitor. Check light and dark mode and 100 / 150 / 200 %.
- [ ] **(live)** List view: Name / Date Modified / Size / Kind headers sort (click again to flip); icons are the shell's; selection is accent blue when active, grey when inactive; wheel scrolls; icon view grid wraps with the window width.
- [ ] **(live)** Keyboard: arrows, Home/End/PgUp/PgDn, Enter opens (folders navigate, files open in the default app), Backspace / Alt+Up goes up, Alt+Left/Right back/forward, F2 rename (name without extension selected, Enter commits, Esc cancels), Ctrl+A, Ctrl+C/X/V, Delete (Recycle Bin), Ctrl+Shift+N, Ctrl+1/2, F5, type-ahead.
- [ ] **(live)** Copy in Files, paste in Explorer and the other way round; cut/paste moves. A paste that conflicts shows the OS replace/skip dialog; a protected folder shows the OS permission prompt.
- [ ] **(live)** Drag a file from Files onto Explorer and back; drag onto a folder row, onto a sidebar entry and onto the breadcrumb; Ctrl copies, Shift moves, Esc cancels.
- [ ] **(live)** Delete a file: it appears in the Recycle Bin. On a network drive the shell asks before deleting for good.
- [ ] **(live)** Dock: the Files pin shows the Velora Files icon and launches; launching it again focuses the open window; closing the window leaves no velora-files.exe process.

## Search (Day 4)
- [ ] **(live)** Hotkey (Alt+Space, or Ctrl+Space when another program owns Alt+Space - check `velora.log` for the `search: hotkey ... registered` line) opens a 520 pt pill 16 % down the screen with the text caret blinking; Esc, clicking outside or switching windows closes it; the Velora menu > Search opens it too.
- [ ] **(live)** Typing "note" shows Applications first, then Folders / Documents ...; Up/Down move through the rows (headers skipped), Enter opens (app, file in its default app, folder in Files), Ctrl+Enter reveals a file in Files with the item highlighted. Icons fill in; file icons are plain shell icons (no tile).
- [ ] **(live)** Stop the Windows Search service (or disable it) and search again: apps still found, the panel says "Windows Search is turned off. Showing apps only."; start it again and files return.
- [ ] **(live)** Look: pill and results are dark frosted glass over a blurred wallpaper, spring open with no overshoot, check light/dark wallpaper and 100 / 150 / 200 % scaling.
- [ ] **(live)** `settings.json` `{"search":{"hotkey":"Ctrl+Alt+Space"}}` changes the hotkey after restarting Velora.
