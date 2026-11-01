# Phase 1 guided live re-check, 2026-11-01

Build: main at eb5ce74 (PRs #34-#38), unsigned local Release build, launched by the user from a non-elevated PowerShell (`build\Release\velora-shell.exe`). Windows 11 10.0.26200, 1 monitor. Velora injects no keystrokes (SendInput removed) and `tools\input_check.ps1` is read-only: the user pressed the keys, the script only observed (a Snipping process started, or the clipboard changed). McAfee real-time scanning was switched off by the user for the local switch gate and this check only.
Raw tool output: [input_check_2026-11-01.txt](input_check_2026-11-01.txt). Results below are the user's report plus that file; anything not reported is marked as such.

## Local switch gate
`ctest -R switch_gate` on main eb5ce74, 22:16, real Explorer, 1 monitor: **PASS** (113 s). ctest hides the per-check counts of a passing test, so the number of checks is not recorded here. (An earlier local attempt failed on every cycle because the test-only `velora-shell.exe` in `build/testbin` had been quarantined by McAfee; CI ran the same gate green on every PR.)

## Steps
| # | Step | Result | Evidence |
|---|---|---|---|
| 0 | `input_check -Phase before -Watch` | PrintScreen PASS (22:23:56). Win+Shift+S **FAIL** (22:24:18, no reaction in 15 s) | Velora was not running. Inconclusive: no snip was reported, and the detector only sees a *new* Snipping process or a clipboard change, so a reused host process or a cancelled overlay reads as FAIL. Win+Shift+S passed in every later run |
| 1 | Start Velora | PASS (user): Velora icon is the first Dock tile; Chrome shows its real icon; most other apps show real icons; Files/Settings tiles hidden | user report; **some apps still show fallback icons** (not yet identified, see below) |
| 2 | `input_check -Phase during -Watch` | PASS (user): PrintScreen 22:26:46, Win+Shift+S 22:26:54 | input_check file, run started 22:26:33 |
| 3 | Dock activate / minimize / restore | PASS (user) | user report |
| 4 | ⋯ opens Settings > Taskbar | **not reported** (the follow-up reply still read "opened Settings > Taskbar / didn't check" without choosing; not assumed) | the shell log of the run could not be read (see below) |
| 5 | `input_check -Phase during -Watch` again | PASS: PrintScreen 22:42:36, Win+Shift+S 22:42:44 (clipboard changed) | input_check file, second Velora session pids 21124 / 8296; run from the Claude terminal panel |
| 6 | Exit | user: exited with the Velora menu (Exit Velora), so there is no `--exit` exit code to record. The 22:38:36 check shows no `velora-*` process, and none were running at 22:44 | user report; input_check file; processes checked by Claude |
| 7 | `input_check -Phase after -Watch` | PASS (user): PrintScreen 22:38:43, Win+Shift+S 22:38:50 | input_check file |

`PrintScreenKeyForSnippingEnabled` was unset and unchanged in every run; no modifier key was logically down in any run except as noted below.

## Notes
1. **The "FAIL at 22:26:36" in the terminal panel** was not a sandbox effect. Two `input_check` runs overlapped: one in the terminal panel (started 22:26:19) and the user's own (started 22:26:33). The panel run's 15 s PrintScreen window (22:26:21-22:26:36) contained no key press, so it logged FAIL; the user's presses came later, at 22:26:46 (PrintScreen) and 22:26:54 (Win+Shift+S). The panel run's own "Win+Shift+S PASS (22:26:46)" is the same snipping process started by that PrintScreen press, so it is cross-contaminated and not counted. The user's run is the valid *during* result.
2. **`keys logically down: RETURN` at 22:38:36** (after exit): not confirmed. It is consistent with the user's own Enter that started the script (the same run's other lines are clean, no `velora-*` process existed, and Velora no longer contains any input injection), but this is not proven. No later run showed a key down.
3. **Fallback icons.** `icons_test` on this machine loads an icon for all 191 Start apps, so the fallbacks the user sees are not plain load failures. The shell can return a generic default icon as a *success*. Apps whose shortcut targets are unresolved (and which failed the later steps of the chain in `icons_test`'s fallback-only run) are the first suspects: Application Verifier (WOW), Application Verifier (X64), Database Compare, mikroC PRO for PIC, mikroProg Suite For PIC, Spreadsheet Compare, Uninstall Zoom Workplace, Zoom Workplace. This list comes from the test, not from the screen; the actual apps with fallback icons still need to be named by the user (or a screenshot) before being fixed.
4. **Logs.** `%LOCALAPPDATA%\Velora\logs` as seen from Claude's sandboxed shell contains only the earlier 18:52 session (the Claude app virtualises AppData for its child processes), so `overflow result=`, `exit check` and any `icons:` warnings from this run were **not read**. They are pending; the user can copy the real log files into this folder if wanted.
5. **Original PrintScreen failure:** not reproduced in any of these runs, so its root cause is still unexplained. Velora has no keyboard hook, registry write, display-affinity call or key injection that could account for it.
