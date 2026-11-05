# Velora v1.0.0 (Level 1) - release report

**Status: DRAFT.** Prepared 2026-11-05 from the automated results (CI) and the measurements recorded in docs/PROGRESS.md. The guided check on a real desktop (docs/QA.md, "Guided session for v1.0") has **not** been run for this build, so every item marked *unmeasured* below is exactly that. Nothing here is a claim of full acceptance. Release rule (CLAUDE.md sec. 12): a release needs the G1 gates, the switch safety gate, the bench and a working installer; the open items below must be closed or knowingly accepted before the draft GitHub Release is published.

Direction: v1.0 = **Level 1**, a blend of Windows and macOS (no injection). Level 2, the complete Windows-to-macOS transformation with opt-in injected title bars, is v2.0, planned for late 2026 / early 2027.

## Gate level per module

| Module | Level reached | Evidence | Open |
|---|---|---|---|
| switch / watchdog / recovery | G1 (safety gate) | Day-2 gate 117/117 locally (2026-11-01, after #34-#38) and in CI on every PR; the cold-start flake was fixed in #58 | re-run by hand in the guided session; Explorer restart on other Windows builds; 2-3 monitors |
| windows tracking | G1 | window_tracker_test | cloaked (UWP) windows, a real elevated window |
| dock | G1 (primary monitor) | dock_test, earlier live checks | per-monitor Docks: **not built** (below G1 for "per-monitor"); jump lists, live thumbnails |
| menu bar | G1 + app menus tiers 1-2 | menubar_test, app_menu_test, menubar_appmenu_test (CI) | tier 3 fallback menu **not built**; the 20-app MENU_COMPAT table is **not complete** |
| apps (launcher) | G1 | apps_test, icons_test; game `.url` shortcuts | pages/folders (G2) not built |
| search | G1 | search_core_test, search_test | everything visual |
| files | G1 | files_test (temp-tree safety cases, hashes), files_window_test | tabs, preview, tags (G2) |
| overview | G1 | overview_test, overview_window_test | animation, desktop strip (G2); not seen on screen |
| control | G1 (see limits) | control_test (read-only probes answered on the developer machine), control_center_window_test | real writes unverified; output-device switching not implemented; media controls (G2) not built |
| settings | G1 | settings_model_test, settings_window_test | not seen on screen |
| window theming | built (not in the original gate list) | theming_test incl. a real cross-process window (CI) | real apps unmeasured (THEMING_COMPAT.md) |
| gaming mode | G1 | gaming_test | not exercised with a real game |
| apple-services | G1-G3 | apple_test | not seen with the Apple apps installed |
| wallpaper | G1 (1 monitor) | wallpaper_test, live restore on exit and crash | multi-monitor |
| notify | **unsupported in v1.0** | not built | Do Not Disturb is a flag only |
| desktops | **unsupported in v1.0** | not built | |
| hotkeys | partial | Search and Overview hotkeys (RegisterHotKey) | Cmd-style remapping not built |
| updates | **unsupported** | the Settings button opens the Releases page | no updater, no rollback |
| installer | built, CI-smoke-tested | Inno Setup .exe and WiX .msi install/uninstall on the CI runner | real desktop install/upgrade/uninstall; unsigned |
| compat (macOS apps) | **not part of v1.0** | not started (Level 2 candidate) | |

## Bench (tools/bench.ps1)

Last recorded (PROGRESS.md, main @ fead1c2, 1 run, 1920x1080 at 150 %): enter 236 ms, exit 332 ms, shell + watchdog private 115.2 MB (budget < 150 MB), idle CPU 0 %, GPU 42.9 MB; launcher open +61.6 MB, back to 117 MB after closing. **The v1.0 build has not been benchmarked**: Overview, Control Center, Settings and theming add code and a few cached objects to the shell, and the 3-run median/range is still owed. Install size: unmeasured (the CI artifacts give it: see the workflow run).

## Known limitations

One Dock and one menu bar (primary monitor only); some apps' menus unreachable; elevated windows cannot be controlled; apps with their own frame may ignore title-bar theming; undocumented/old Windows builds only partly tested (developed on 11 26200); toast capture, virtual desktops, Cmd-style remapping, updater and macOS-app compatibility are not in v1.0; output-device switching opens Windows' Sound settings.

## Not tested (honest list)

Everything marked *unmeasured* above, plus: any run of the release installers on a real desktop; behaviour with Velora running during an uninstall; clean-boot bench; a second monitor; mixed DPI; a real game; the Apple apps installed; Windows 10 and earlier Windows 11 builds; screen readers/accessibility; IME input in Velora's text fields (not supported); Wi-Fi networks that need a password (Velora does not handle keys: Windows' own settings open).

## Antivirus submissions

Not yet made (the user submits; see docs/SIGNING.md). Record here: Microsoft submission id ______ outcome ______; McAfee submission id ______ outcome ______.

## Signing

Unsigned unless the SignPath Foundation application has been approved and the repository variable is set; the draft release notes state which. SHA-256 of every file is in `SHA256SUMS.txt`.

## Compat coverage

Not applicable to v1.0 (the compat subsystem is not part of this release).
