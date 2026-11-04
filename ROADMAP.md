# Velora roadmap

Velora ships in two levels. Each level is complete and safe on its own.

## Level 1 - Velora v1.0: a blend of Windows and macOS

A macOS-inspired layer on top of a normal Windows 11 desktop. Nothing is replaced or injected; exiting restores Windows instantly.

- Dock, global menu bar (app menus for classic Win32 and UI Automation apps, generic fallback)
- Apps launcher, Search, Files (Recycle Bin only for deletes), Overview, Control Center, Settings
- Window theming through documented DWM attributes (rounded corners, dark/light caption, backdrop), restored on exit, crash and `--restore`
- Optional Apple services group (launch and install links only; Velora never handles Apple credentials)
- Gaming mode, multi-monitor and High-DPI support
- Per-user installer (.exe and .msi), checksums, GitHub Releases

Out of scope for v1.0: injected title bars, replacing the Windows shell, running macOS apps by default.

## Level 2 - Velora v2.0: the complete Windows-to-macOS transformation

Planned for **late 2026 / early 2027**.

- Opt-in, per-app, reversible title bars drawn by Velora (the injection track, designed and security-reviewed first)
- Deeper window chrome and animations, per-monitor menu bars, drag windows between desktops
- Desktops: create/switch through version-detected interfaces
- Compat subsystem (x86_64 Mach-O) beyond the current gates, as an optional component

Dates are intentions, not promises.
