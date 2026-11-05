# LinkedIn post (draft, ~215 words)

I just open-sourced Velora, a free (Apache-2.0) blend of Windows and macOS for Windows 11.

It adds a Dock, a global menu bar, an app launcher, a Finder-style file manager, Spotlight-style search, a window overview, a Control Center and a Settings app, and it themes your existing windows with rounded corners, dark/light title bars and Mica, all through Windows' own documented APIs.

Why I built it: I love how a macOS desktop feels, but I live in Windows. I wanted the good parts without a VM, a reboot, or a hack that could leave my machine in a broken state.

So safety came first:
- no reboot, no VM, no kernel changes, no admin rights
- nothing is replaced: Explorer keeps running, your apps are untouched, nothing is injected into other programs
- one click (or Ctrl+Alt+Shift+Esc) restores the normal Windows desktop, and a separate watchdog restores it even if Velora crashes or hangs
- file operations only use the Recycle Bin; nothing is deleted for good without asking

This is Level 1 (Velora v1.0): a blend of Windows and macOS, not a full transformation. Level 2, the complete Windows-to-macOS transformation (including opt-in custom title bars), is planned for late 2026 / early 2027.

It is early, honest about its limits (the release notes list what is not tested yet), and I would love feedback. Try it, break it, and if you like the idea, star the repo.

https://github.com/moelsaka01/velora

#OpenSource #Windows11 #macOS #DesktopApps #CPlusPlus
