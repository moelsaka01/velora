# Security review (v1.0, 2026-11-05)

Scope: the code in this repository at the v1.0 candidate (shell, watchdog, Files, Settings, installers, release workflow). This is the development team's own review plus automated checks; it is **not an independent audit**, and it does not claim "no vulnerabilities". It lists what was checked, what was found and fixed, and what remains.

## Trust model

Velora runs as a normal user, never elevated (the shell now refuses to start elevated unless `VELORA_ALLOW_ELEVATED=1`). It has no network code of its own and no telemetry. Everything it reads or writes lives in `%LOCALAPPDATA%\Velora` (settings.json, state.json, theming_backup.json, apps_index.json, logs) or in HKCU (Run entry, Personalize). Another process of the **same user** can already change those files and start programs as that user, so they are trusted at that level; the checks below are about not letting anything *cross* a boundary (other users, lower integrity, remote, elevated, hostile file names or window text).

## Checked

| Surface | Result |
|---|---|
| Named pipes (shell, watchdog) | DACL = current user only (`D:P(A;;GA;;;<SID>)`), `PIPE_REJECT_REMOTE_CLIENTS`, `FILE_FLAG_FIRST_PIPE_INSTANCE` (no squatting), 1 MB frame cap, depth-limited JSON; malformed and oversize frames are dropped (core_test) |
| JSON parser (IPC, settings.json, state.json) | `security_test`: 80 000 mutated / random inputs, depth bombs, no crash, no hang (0.1 s) |
| Starting programs | explicit full paths (`ExeDir()`, `%windir%`), `CreateProcess` with a quoted command line; `security_test` proves with 3 000+ hostile paths (quotes, backslashes, spaces, `--select`) that `CommandLineToArgvW` returns exactly the intended arguments, so a file name can never add an argument |
| File operations | Recycle Bin only (`FOFX_RECYCLEONDELETE`, never `FOF_NOCONFIRMATION` on a delete); copy/move through `IFileOperation`; hashes verified in files_test |
| Recovery | the recorded shell is terminated only if image name **and** start time match; appbar removal checks the owning pid; theming restore checks pid, class and start time and only touches an attribute that still has Velora's value; work areas are restored only when the recorded rectangle fits the monitor |
| Registry | HKCU only (Run, Personalize); no HKLM write, no shell replacement, no policy or Defender setting |
| Other processes | no injection, no subclassing, no reparenting; DWM attributes set from Velora's own process; elevated, fullscreen, game, anti-cheat and Velora's own windows are skipped (security_test) |
| Search | the file query is built from sanitised words (letters, digits and `-_.+#&@` only): user input cannot add a quote, operator or clause (search_core_test) |
| Settings / Apple / updates | no credentials anywhere; Apple services only launch apps and open links (Store search, apps.apple.com); "updates" opens the Releases page, nothing is downloaded or executed |

## Found and fixed in this review

1. Binaries had no control-flow guard: now built with `/guard:cf`, `/CETCOMPAT`, `/DYNAMICBASE`, `/HIGHENTROPYVA`, `/NXCOMPAT` (checked in the PE headers of all four executables: ASLR, high-entropy ASLR, DEP, CFG).
2. Later DLL loads could search the current directory: every executable now calls `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)` first.
3. Log lines could be forged or split by text from outside (paths, classes, queries containing line breaks or escapes): `log::Sanitize` turns control characters into spaces (security_test).
4. Files' `WM_COPYDATA` handler accepted any size: capped at 64 KB.
5. The Search "reveal" fallback started `explorer.exe` through a name search: now the full path under `%windir%`.
6. The release workflow asked for `id-token: write` it never used: removed (`contents: write` remains for the draft release on a tag).
7. Velora could run elevated: it now refuses (message and exit code 3) unless `VELORA_ALLOW_ELEVATED=1`; the automated test profile is exempt.

## Residual risks and not done

- **Unsigned binaries** until the SignPath Foundation application is approved: integrity rests on HTTPS plus the SHA-256 in `SHA256SUMS.txt`. Anyone can build from source (README).
- Same-user trust: any process of the same user can send the shell `exit`/`restore`/`open_*` requests or edit `settings.json` (for example the Dock's pinned launch targets). This is not a privilege boundary.
- The per-user install directory is writable by the user (normal for per-user installs); an administrator install into Program Files is possible through the installer's privilege prompt.
- Not done: independent audit or penetration test, protocol-level pipe fuzzing (only the JSON layer is fuzzed), MSVC `/analyze` or CodeQL runs, a threat model for the UI Automation app-menu reader (it reads text from other apps' menus and invokes them: it never runs in an elevated shell), signing of the sparse package (notify is not built), reproducible builds, SBOM.
- Third-party code: the Inter font (OFL, unmodified) and Windows SDK headers only; see THIRD_PARTY_NOTICES.md. GitHub Actions are referenced by major version (Dependabot watches them); pinning by commit SHA is a possible tightening.

Report problems privately: see SECURITY.md.
