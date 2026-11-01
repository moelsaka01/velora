# Phase 1 guided live re-check (results to be filled in)

Build under test: main at eb5ce74 (PRs #34-#38 merged), `build\Release\velora-shell.exe`, unsigned local build.
Machine: Windows 11 10.0.26200, 1 monitor, non-elevated PowerShell started by the user. Claude only reads logs and runs read-only probes; Velora injects no keystrokes (SendInput removed) and `tools\input_check.ps1` is read-only: the user presses the keys.
McAfee real-time scanning is switched off by the user only for the duration of the local switch gate and this check, then switched back on.

Tools: `tools\input_check.ps1 -Phase before|during|after [-Watch]` (appends to `docs\evidence\input_check_<date>.txt`), `tools\live_probe.ps1`, logs in `%LOCALAPPDATA%\Velora\logs`.

| # | Step (user) | What is checked | Result |
|---|---|---|---|
| 0 | `tools\input_check.ps1 -Phase before -Watch`, press PrintScreen then Win+Shift+S when asked | baseline: no key down; both react | pending |
| 1 | Start `build\Release\velora-shell.exe` | menu bar + Dock appear; first Dock tile is the Velora icon (not a dot grid); no Files / Settings tiles; a running Chrome tile shows the Chrome icon, not a letter | pending |
| 2 | `tools\input_check.ps1 -Phase during -Watch` | PrintScreen and Win+Shift+S work during Velora Mode | pending |
| 3 | Dock: click a running app (activate), click again (minimize), click again (restore) | window state follows; no warning in the log | pending |
| 4 | Click the ⋯ item once | Settings > Taskbar opens; log `overflow result=ms-settings` | pending |
| 5 | `tools\input_check.ps1 -Phase during -Watch` again | input still fine after Dock and ⋯; no key logically down | pending |
| 6 | Velora menu > Exit Velora (or `velora-shell.exe --exit`, exit code 0) | taskbar back; with `--exit`: `exit check: no resident Velora process remains` | pending |
| 7 | `tools\input_check.ps1 -Phase after -Watch` | PrintScreen and Win+Shift+S work after exit; no Velora process; `PrintScreenKeyForSnippingEnabled` same as step 0 | pending |

If step 2, 5 or 7 fails, do not reboot first: run `tools\input_check.ps1 -Phase after` and `tools\live_probe.ps1` and tell Claude which step failed.

The earlier live check (docs/evidence/live_test_2026-11-01.md) covers only the switch path (a-f); no results for its steps 3-7 exist in the repository, so they are "pending" here, not assumed.
