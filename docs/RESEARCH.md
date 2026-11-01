# Research (Day 1, 2026-11-01)

Method: `gh api` metadata + LICENSE/README spot checks only; no clones. Done once; do not repeat.
"Pushed" = GitHub `pushed_at`. Day-saved figures are estimates, not measurements.

## 1. Verified reuse matrix (CLAUDE.md §3)

| Repo | License (verified) | Pushed | Relevant files (verified to exist) | Action |
|---|---|---|---|---|
| cairoshell/cairoshell | Apache-2.0 | 2026-10-15 | `Cairo Desktop/CairoDesktop.AppGrabber/AppGrabberService.cs`, `CairoDesktop.MenuBarExtensions/SystemTray*.cs` | Port logic to C++ with NOTICE |
| cairoshell/ManagedShell (separate repo) | Apache-2.0 | 2026-09-18 | `src/ManagedShell.AppBar/{AppBarManager,AppBarWindow,ExplorerHelper}.cs`, `src/ManagedShell.WindowsTray/` | Appbar + tray logic lives here, not in cairoshell. Port with NOTICE |
| microsoft/PowerToys | MIT | 2026-11-01 | `src/modules/launcher/Plugins/Microsoft.Plugin.Indexer/`, `.../WindowWalker/`, `src/modules/fancyzones/`, `src/modules/cmdpal/` | Reuse with notice (FancyZones is C++) |
| Flow-Launcher/Flow.Launcher | MIT | 2026-11-01 | `Plugins/Flow.Launcher.Plugin.Explorer/` | Port query/ranking logic (C#) |
| microsoft/wil | MIT | 2026-10-30 | `include/` | Dependency (vcpkg) |
| microsoft/cppwinrt | MIT | 2026-10-29 | `cppwinrt/` | Dependency (vcpkg) |
| Everything SDK | MIT (via official mirror voidtools/everything_sdk3; voidtools.com terms page not web-checked) | 2026-10-16 | `include/`, `src/` | Optional, load dynamically. Do not redistribute Everything DLLs |
| glzr-io/glazewm | GPL-3.0 | 2026-06-20 | n/a | Study only |
| RamonUnch/AltSnap | GPL-3.0 | 2026-10-27 | n/a | Study only |
| LGUG2Z/komorebi | Custom "Komorebi License 2.0.0" (personal-use restricted) | 2026-11-01 | LICENSE.md | Study only, not OSI |
| boykopovar/AnyPS5 | GPL-2.0 | 2026-11-01 | `core/` | Study only |
| darlinghq/darling | GPL-3.0 | 2026-10-28 | n/a | Study only |
| gnustep/libobjc2 | MIT | 2026-10-19 | repo root | Reuse (compat) |
| gnustep/libs-base | GitHub: GPL-2.0; also ships COPYING.LIB (LGPL-2.1) | 2026-11-01 | | **Unverified per-file.** Check source headers before use; separate DLL only |
| gnustep/libs-gui | GitHub: GPL-3.0; also ships COPYING.LIB (LGPL-2.1) | 2026-10-31 | | Same as above |
| swiftlang/swift-corelibs-foundation (apple/ redirects) | Apache-2.0 | 2026-10-31 | `Sources/` | Reuse (compat) |
| swiftlang/swift-corelibs-libdispatch | Apache-2.0 | 2026-10-30 | | Reuse (compat) |
| apple-oss-distributions/CF | APSL-2.0 (APPLE_LICENSE) | 2021-10-06 (stale) | | Skip; use swift-corelibs |
| morluto/rea | MIT | 2026-11-01 | | Tool only. Star count (416k) is anomalous and unverified |

Corrections to CLAUDE.md assumptions: appbar code is in **ManagedShell**, not cairoshell; komorebi's license is non-OSI; libs-base/gui license split is unconfirmed.
Note: PowerToys/Flow/cairoshell are C#. Everything is a **port** into C++, not a link. Value = algorithms and API sequences.

## 2. New candidates (13 criteria condensed)

| # | Name / URL | Function | Lang | License | Pushed | Win | Modules | Deps | Effort saved | Security | Verdict |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | Ciantic/VirtualDesktopAccessor | Virtual desktop DLL w/ per-build COM GUIDs | Rust | MIT | 2026-10-11 | 10/11 | desktops G2 | prebuilt DLL or Rust | 1-2 d | Undocumented API; version-gate | Reuse (flagged) |
| 2 | lief-project/LIEF | Mach-O/PE parser | C++ | Apache-2.0 | 2026-10-31 | Yes | compat/loader | cmake | 2-3 d | Untrusted input; compat-only | Reuse (parsing only) |
| 3 | microsoft/fluentui-system-icons | Icon set | SVG | MIT | 2026-10-30 | n/a | render, all UI | none | ~1 d | assets | Reuse |
| 4 | KhronosGroup/SPIRV-Cross | SPIR-V to MSL/HLSL | C++ | Apache-2.0 | 2026-10-28 | Yes | compat/METAL note | C++ | 1-2 d (future) | build-time | Reuse (deferred) |
| 5 | MicaForEveryone/MicaForEveryone | DWM Mica/Acrylic calls | C# | MIT | 2026-07-17 | 11 | render | .NET | ~0.5 d | Modifies other apps' windows; violates §0.8 | Own windows only |
| 6 | MScholtes/VirtualDesktop | VD CLI | C# | MIT | 2025-08-12 | partial | desktops | .NET | ~0.5 d | stale | Study (COM defs) |
| 7 | lucide-icons/lucide | Icons | SVG | ISC (LICENSE verified) | 2026-10-31 | n/a | render | none | ~0.5 d | assets | Reuse (secondary) |
| 8 | Bundelldebug/OneTaskBar | Dock UX | C# | MIT | 2026-10-31 | 11 | dock | .NET | ~1 d ideas | young, unaudited | Study |
| 9 | Cedro-Software/cedro-modern-dock | Dock | unverified | GPL-3.0 | 2026-10-16 | Yes | dock | | 0 | | Study only |
| 10 | HansKristian-Work/dxil-spirv | DXIL to SPIR-V | C++ | unverified (MIT file present) | 2026-10-30 | Yes | compat gfx | | unverified | | Skip until license confirmed |

Gaps: no permissive Direct2D/DirectComposition UI lib or dock surfaced; dock/menubar/render are original code. TranslucentTB (GPL-3.0) is study-only.

## 3. Top 5 by days saved (estimates)

1. **cairoshell + ManagedShell** (appbar, tray hosting, app grabber): ~2-3 d, port.
2. **LIEF** (Mach-O parsing): ~2-3 d, compat only.
3. **VirtualDesktopAccessor**: ~1-2 d, behind version flag.
4. **PowerToys** (FancyZones math, indexer, window walking): ~1-2 d, port.
5. **Fluent icons (+ Lucide)**: ~1 d.

Runner-ups: Flow.Launcher search ranking (~1 d), SPIRV-Cross (future).

## 4. Unverified / open
- libs-base/libs-gui per-file license headers (check at D4/D5 before any compat use).
- voidtools.com SDK terms page; MIT is from the mirror only.
- Days-saved and dependency figures are estimates.
- Research stopped; enough evidence to decide.

## 5. DiPlay process lessons (study-only; GPL/AGPL, nothing copied)
Source: README, CLAUDE.md/AGENTS.md, VALIDATION, PRIVACY, CHANGELOG of shihabal3amri/DiPlay, read via `gh api`.
1. Adopt: a validation log that says per release what was tested and on what, and names what still needs real hardware.
2. Adopt: say plainly what was NOT tested; "no full-release acceptance claimed" beats an implied pass.
3. Adopt: opt-in local diagnostic export, reviewed by the user, never auto-uploaded; tell users redaction is best-effort.
4. Adopt: a privacy doc listing every network call (what is sent, how often, how to turn it off).
5. Adopt: agent rules as testable constraints (a test fails when a setting has no category or no translation); prefer a failing test over prose.
6. Adopt: one canonical place for UI spacing/size values, never ad-hoc literals (matches our design tokens).
7. Adopt: updates verified by published checksum and user-initiated; production builds only from a clean recorded commit/tree.
8. Avoid: shipping third-party keys or identities. DiPlay bundles an extractable accessory identity recovered from other vendors' firmware and admits the legal/technical risk. Velora bundles no third-party keys, certificates, tokens or identities; signing keys are ours and never in the repo or installer.
9. Avoid: long changelog and validation entries that are hard to scan; keep ours tabular and short.
10. Avoid: crediting contributor-owned device tests as project acceptance; we record who ran what, where, and the machine.
