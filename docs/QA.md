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

## Wallpaper
- [ ] **(live)** On enter every monitor shows the Velora wallpaper (dark theme: deep blue/violet, light theme: pastel); no banding visible.
- [ ] Exit restores the previous wallpaper on every monitor, including a different one per monitor.
- [ ] If you pick another wallpaper while in Velora Mode, exit leaves yours alone.
- [ ] Kill velora-shell.exe: the watchdog restores the taskbar and the wallpaper.

## Multi-monitor / DPI (not yet automated)
- [ ] 1, 2 and 3 monitors at mixed 100/150/200 %: bars and Dock on the primary monitor, correct size, no overlap with the other monitor.
- [ ] Change scaling while running: bars resize and stay crisp.
