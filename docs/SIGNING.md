# Code signing and false-positive submissions

Velora hides the taskbar, registers appbars and manages windows, so unsigned new executables are exactly what reputation heuristics quarantine (a McAfee "ti!" detection was seen on a developer machine). v1.0 ships signed if the free signing application below is approved in time, and otherwise **unsigned with SHA-256 checksums, and the release says so**.

## 1. SignPath Foundation (free for open source) - you apply

Applying needs an account and agreeing to terms, so it is yours to do. Everything else is wired.

1. Apply at https://signpath.org/ (Foundation / open-source program) for the project `velora` (https://github.com/moelsaka01/velora, Apache-2.0, built by GitHub Actions).
2. Suggested answers:
   - Project: Velora, a free, open-source (Apache-2.0) Windows 11 desktop enhancement (Dock, menu bar, launcher, Files, Search). Native C++ Win32 binaries, no drivers, no installers beyond a per-user setup.
   - Build: GitHub Actions only (`.github/workflows/release.yml`), no manual builds are submitted.
   - Why signing is needed: the programs hide the taskbar and manage windows; unsigned binaries get quarantined by antivirus reputation heuristics.
   - Privacy and security policy: https://github.com/moelsaka01/velora/blob/main/SECURITY.md, no telemetry, no network calls except what the user starts (opening the Releases page).
3. After approval, in the repository settings:
   - Variable `SIGNPATH_ORGANIZATION_ID`, secret `SIGNPATH_API_TOKEN`.
   - In SignPath: project slug `velora`, signing policy `release-signing`, artifact configurations `binaries` (the four `velora-*.exe`) and `installers` (`Velora-*-setup.exe`, `Velora-*.msi`).
4. The release workflow signs the binaries, rebuilds the portable zip from the signed files, builds the installers from them and signs those, in that order. Without the variable it skips signing and the draft release says "UNSIGNED".

Until approval: unsigned, and the README and release notes carry the SHA-256 of every file (`SHA256SUMS.txt`).

## 2. False-positive submissions - you submit

Submit the **signed** release builds if signing was approved, otherwise the unsigned ones (say so in the form). Record the submission ids and outcomes in `RELEASE_REPORT.md` (section "Antivirus submissions"). Do not add antivirus exclusions on your machine for this.

### Microsoft (Defender)
- https://www.microsoft.com/en-us/wdsi/filesubmission, "Software developer", not signed in as an enterprise.
- Files: `Velora-<version>-setup.exe`, `velora-shell.exe`, `velora-watchdog.exe` (the portable zip contains all four exes).
- Text:
  > Velora is a free, open-source (Apache-2.0) desktop enhancement for Windows 11: a Dock, menu bar, launcher and file manager. To do this the shell hides the Windows taskbar through ShowWindow and registers application bars (SHAppBarMessage); it enumerates top-level windows through SetWinEventHook, and sets window appearance through DwmSetWindowAttribute. It never injects code into other processes, never terminates other processes, never needs administrator rights, and its watchdog restores the taskbar if it exits. Source and build logs: https://github.com/moelsaka01/velora. SHA-256: <paste from SHA256SUMS.txt>.

### McAfee
- https://www.mcafee.com/en-us/consumer-support/false-positive-submission.html (or the "Submit a sample" page of your product), file type "program", detection name as shown by your product (e.g. `Artemis!...` / `ti!...`).
- Same description as above; add the detection name and the date.

## 3. What to say if a user is blocked

The installer and every binary are in the GitHub Release with checksums; `docs/SIGNING.md` explains the cause. Never ask users to disable their antivirus; ask them to report the detection to the vendor (and to us).
