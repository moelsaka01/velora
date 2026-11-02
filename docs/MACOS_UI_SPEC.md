# macOS-inspired UI spec (Velora design tokens)

Purpose: one place for the dimensions, materials and motion `render/` tokens are tuned to. Values are for original Velora artwork; nothing here is copied from Apple assets. Every number says where it came from; "measured" means taken from pixels of a public screenshot, "derived" means computed from measured pixels, "reference" means taken from a published/OSS source, "estimate" means still unverified.

## Source rules

Allowed: Apple HIG text and developer-video text (paraphrased), public review screenshots (512pixels Aqua Screenshot Library, Macworld, Ars Technica) for **measurement only**; those images stay in `C:\velora-ref\` and are never committed. MIT/Apache code may be read and cited with file and license.
Forbidden: Apple Design Resources (UI kits, Sketch/Figma libraries, SF fonts, SF Symbols) and any repo redistributing Apple icons/assets (CLAUDE.md §0, rule 11). Apple announced macOS 27 design kits (developer.apple.com news); they were **not opened or used**. Previously excluded and still unopened: the official macOS UI kit, `mjmirza/apple-design-system` resource lists, `acodega/ASA`, `zagnut531/macOS-27-Icons`.

## Measurement method (Phase 2, 2026-11-01)

- Sources: `C:\velora-ref\sources.txt` (16 screenshots from 512pixels, 3 from Ars/Macworld). Pixel work with Pillow/numpy (gradient edge peaks, colour masks, least-squares circle fits for corners, two-point regression for glass material).
- **Scale confirmed 2x** for the 2816x1762 shots: the menu-bar capital letters (E, F) are 19 px tall, which is a 13 pt UI font at 2x (cap height about 0.7 em); the traffic-light spacing, row heights and Dock icon size all come out at plausible whole/half points when divided by 2. All values below are in **logical pt at 1x** (px / 2); scale by monitor DPI.
- Corner radii are least-squares circle fits of the visible edge, so they are *circular-equivalent*; Apple's continuous (squircle) corners look slightly larger at the same nominal radius.
- Material (alpha + fill) is the regression `inside = (1 - a) * outside + a * fill` over many sample pairs across one panel edge. It lumps the backdrop blur, tint and any rim lighting into one flat veil, so treat it as a starting point for the translucent fill, not as Apple's material definition.
- Only light-mode shots exist in the reference set. The Spotlight and Apps panels are dark-tinted glass seen in light mode. No dark-mode window, menu bar or Dock was measured: dark rows stay estimates.
- `measurements-found.md` (third-party, unverified snippets) was used only as a cross-check; disagreements are noted.

## Evidence status

| Source | Result |
|---|---|
| 512pixels shots (Desktop, Finder-Menu, Control-Center, Spotlight, Apps, Window-Management, Settings-*) | Measured this pass (Desktop, Finder-Menu, Control-Center, Spotlight, Apps, Window-Management). Settings-* panes, Notification-Center, Widgets, Quick-Look, Finder-Home/Gallery/Applications: **not measured yet** |
| Six Colors, Macworld, gambadio/macos-golden-gate-design (MIT) | Qualitative only (see below) |
| Ars Technica review | Not fetched (host rejected the fetch tool); `ars_corner-compare.jpeg` in the ref folder is low-res and unused |
| WWDC26 sessions 289 (Modernize your AppKit app), 269 (What's new in SwiftUI) | Read (transcripts). No numeric motion, radius or tint values are stated |
| WWDC23 session 10158 (Animate with springs) | Read. Spring parameters and conversion formulas (see Motion) |
| developer.apple.com docs for `Animation.smooth/snappy/bouncy`, `NSAnimationContext` | Fetched but JS-rendered: title only, no facts. Numeric preset values were **not** filled from memory |
| PuruVJ/macos-web (MIT, (c) 2021 Puru Vijay, main @ 1420480) | Read for Dock magnification and spring code (see Motion) |

## Qualitative findings (cited)

- Window corners are slightly less rounded than the previous release and consistent across windows (Six Colors, Macworld, 9to5Mac).
- Sidebars run to the window edge as a full-height column; floating inset sidebars are gone (Six Colors, Macworld, gambadio; WWDC26 289 confirms "extend to the window edge", semibold selected text).
- Toolbars are uniform, with defined buttons; bordered toolbar items over a sidebar take Liquid Glass (WWDC26 289).
- Glass transparency is user-adjustable from ultra clear to tinted (WWDC26 keynote/269). Velora exposes the same slider.
- Wallpapers have light and dark variants (Macworld).
- Interactive glass controls give a small bounce when clicked (WWDC26 289/269); no magnitude or timing is published.

## Values

Unit: logical pt at 1x. Image = file in `C:\velora-ref`. Confidence: high (clean edge, repeated), medium (clean but single sample or derived), low (noisy fit, display-coordinate reading, or confounded).

### Menu bar and menus

| Item | Value | Image | Method | Conf. |
|---|---|---|---|---|
| Menu text size | 13 pt (cap height 19 px = 9.5 pt) | Desktop | glyph rows of E/F in the menu titles | high |
| Gap between menu titles (text to text) | 21.75 pt (43.5 px, 7 gaps all 43-44 px) | Desktop | column runs of bright text | high |
| Menu bar height | about 29 pt (derived) | Finder-Menu | the highlighted title capsule spans y 6..52 px (23 pt) centred at 14.5 pt, so a centred bar is about 29 pt; cross-check: 31 pt reserved on a notch MacBook Air (third-party, `measurements-found.md`) | low |
| Menu bar background | none: fully transparent over the wallpaper, white text | Desktop, all | visual + pixel check: no chrome edge anywhere | medium |
| Selected-title capsule | 70 x 23 pt, fully rounded, about 15 pt side padding to the label | Finder-Menu | edge peaks at x 78 / 218, y 6 / 52 | medium |
| Menu popup width | 201.5 pt | Finder-Menu | edge peaks x 76..479 | high |
| Menu popup height (7 rows, 4 separators) | 222 pt | Finder-Menu | y 61..505 | high |
| Gap menu bar title to popup top | 4.5 pt | Finder-Menu | capsule bottom 52 px, popup top 61 px | medium |
| Popup inner padding | 5 pt (highlight inset 10-11 px) | Finder-Menu | blue highlight x 86..469 vs popup 76..479 | high |
| Menu row height / highlight | 24 pt; highlight radius 6 pt; accent blue | Finder-Menu | blue mask 384 x 48 px, corner fit 12 px | high |
| Group pitch (separator + one item) | 35 pt (item 24 + 5.5 above and below) | Finder-Menu | separators at y 131, 201, 271, 341 | high |
| Popup corner radius | 11 pt | Finder-Menu | circle fit 22.5 px | medium |
| Popup material (light) | white-ish fill at about 77 % over a blur (fit residual 5.9) | Finder-Menu | edge-pair regression | low |

### Windows, toolbar, sidebar

| Item | Value | Image | Method | Conf. |
|---|---|---|---|---|
| Traffic-light diameter | 13 pt (26 px; green circle 26 x 26) | Window-Management | colour-mask bounding boxes | medium |
| Traffic-light centre spacing | 23 pt (46 px, both gaps equal) | Window-Management | circle centres | high |
| Traffic lights: first circle's left / top edge from the window edge | 20 pt / 19.5 pt; centre line 26 pt below the window top | Window-Management | mask x 247, y 124 vs window edge x 207, y 85 | medium |
| Window corner radius | 15.25 pt circular-equivalent (top-left and bottom-left agree) | Window-Management | circle fit 30.5 px | medium. A third-party forum figure says 20 pt nominal (`measurements-found.md`); a continuous-corner radius of 20 looks like roughly 15-16 as a circle, so Velora targets 16 with Direct2D (Phase 3) |
| Toolbar control height | 36 pt (sidebar toggle 72 px, glass group 73 px); centre line 26 pt below the window top, same as the traffic lights | Window-Management | edge peaks y 101..173 / 100..173 | high |
| Toolbar height | about 52 pt (derived: control centre line x 2) | Window-Management | derived | medium |
| Sidebar width (this window) | 216 pt | Window-Management | boundary x 639 vs window left 207 | high |
| Sidebar row pitch | 32 pt | Window-Management | text-row starts at y 596, 661, 725, 789 | high |
| Sidebar material (light) | about 96 % opaque, rgb(236,235,234) | Window-Management | regression + direct sample | high |
| Window content background | rgb(255,255,255) | Window-Management | sample | high |

### Dock

| Item | Value | Image | Method | Conf. |
|---|---|---|---|---|
| Shelf height | 74 pt (148 px) | Desktop | edge peaks y 1603 / 1751 at several x | high |
| Icon size | 44 pt (86-88 px) | Desktop | Finder icon colour mask 86 px; edge pairs 117/205 | high |
| Icon pitch | 58 pt (116 px) -> spacing 14 pt | Desktop | icon left edges 117, 233, 349 | high |
| Shelf padding | 15 pt left, top and bottom (30 px each); the shelf is 44 + 2 x 15 = 74 | Desktop | shelf edge 87 vs icon 117; 1603 vs 1633; 1721 vs 1751 | high |
| Shelf corner radius | 20.5 pt | Desktop | circle fit 41 px | medium |
| Icon corner radius | 9.75 pt = 22.7 % of the icon size | Desktop | Finder icon mask circle fit 19.5 px | medium |
| Running indicator | 4 pt dot (8 px), centre 9.25 pt below the icon bottom, 4 pt above the shelf bottom | Desktop | dark blob x 158..165, y 1736..1743 | high |
| Shelf distance from the screen bottom | 5.5 pt | Desktop | 1762 - 1751 px | high |
| Separator | hairline (about 1 pt) between apps and Trash | Desktop | thin edge pair near x 2454 | low |
| Material | veil fill about rgb(150,150,152) at 31 % over the blurred backdrop (fit residual 1.9, n = 40) | Desktop | edge-pair regression across x | medium |
| Magnification | not measurable from still shots; see Motion | | | n/a |

### Control Center, Spotlight, Apps

| Item | Value | Image | Method | Conf. |
|---|---|---|---|---|
| CC tile 1x1 / 2x1 | 64 x 64 / 140 x 64 pt (1x1 from display coordinates) | Control-Center | edge peaks x 2203..2484, y 139..267 | medium |
| CC tile gap | 12 pt (24 px) | Control-Center | x 2484 -> 2508; y 267 -> 291 | high |
| CC panel width | 291.5 pt | Control-Center | x 2203..2786 | high |
| CC media tile | about 139 x 138 pt (two rows + gap) | Control-Center | x 2508..2786; display y 100..296 | medium |
| CC tile corner | fully rounded (radius = half the height, 32 pt) for the 64 pt tiles | Control-Center | visual | low |
| CC tile material | about 20 % veil, rgb(128,109,99) (backdrop range too narrow, residual 6.3) | Control-Center | regression | low |
| Spotlight search pill | 520 x 86.5 pt, stadium shape (radius = half the height) | Spotlight | x 887..1927, y 275..448 | high |
| Spotlight pill to results panel | 10.5 pt gap; same 520 pt width | Spotlight | y 448 -> 469 | high |
| Spotlight results panel | 410 pt tall (clipped list), radius about 41 pt or more | Spotlight | y 469..1289; fit hit its search limit | low |
| Spotlight result row pitch | about 64 pt; category chips about 23 pt tall | Spotlight | display coordinates x 1.408 | low |
| Spotlight pill material | near-black veil, about 82 % (residual 30) | Spotlight | regression | low |
| Spotlight results material | about 61 % veil, rgb(85,71,59) (residual 18) | Spotlight | regression | low |
| Apps panel | 843 pt wide, top aligned with the Spotlight results panel (y 469 px), radius about 45 pt or more | Apps | x 565..2251 | medium / low |
| Apps icon / pitch | 60 pt icon, 115 pt column pitch, 102 pt row pitch (a section divider adds extra above the second row) | Apps | display coordinates x 1.408 | low |
| Apps panel material | about 70 % veil; the fitted rgb(103,87,74) was the screenshot wallpaper showing through, so Velora uses neutral rgb(90,90,92) | Apps | regression | low |

### Materials summary for tokens

| Surface | Light | Dark |
|---|---|---|
| Sidebar / window chrome | rgb(236,235,234), about 96 % opaque (measured) | estimate: rgb(40,40,42) at 70 % + blur (unmeasured) |
| Dock shelf | rgb(150,150,152) at 31 % + blur (derived) | estimate, unmeasured |
| Menu popup | white at about 77 % + blur (low) | estimate, unmeasured |
| Spotlight / Apps panels | dark-tinted glass, 60-80 % veil (low), shown in light mode | n/a |
| Hairline | 1 pt | 1 pt |

The system transparency slider scales the veil alpha; "Reduce Transparency" falls back to an opaque fill.

## Motion

What is and is not known. **Apple publishes no numeric values** for the macOS Dock magnification curve, Dock minimize effect, Launchpad/Apps transitions or the Liquid Glass press response. Values below are labelled by source; anything marked *needs device video* must be measured from a screen recording of a real Mac before being claimed.

### From Apple (paraphrased, explicit on the page)

| Fact | Source |
|---|---|
| SwiftUI's default animation is a spring; `bounce` defaults to 0 (no overshoot); the perceptual `duration` is what completion handlers use, the settling time is longer and unpredictable. Numeric default duration is not stated | WWDC23 10158 |
| Spring parameters: `duration` and `bounce` (-1..1). About 15 % bounce is subtle, about 30 % is clearly bouncy, caution above 40 % | WWDC23 10158 |
| Conversion to physics (mass 1): stiffness = (2*pi / duration)^2; damping = 1 - 4*pi*bounce / duration for bounce >= 0, and 4*pi / (duration + 4*pi*bounce) for bounce < 0 (as stated in the session's code block) | WWDC23 10158 |
| Examples used in the session (not defaults): duration 0.4-0.6 s with bounce 0-0.3 | WWDC23 10158 |
| Interactive Liquid Glass (macOS 27): glass subtly bounces when clicked; use it only for controls, "a little goes a long way". No bounce size or timing given | WWDC26 289, 269 |
| Inactive windows dim icons and text; no value (sample code uses 0.5 opacity, a developer choice) | WWDC26 269 |

*Not obtained:* `Animation.smooth/snappy/bouncy` preset numbers and the `NSAnimationContext` default duration (docs pages are JS-rendered). Velora's `render/` springs will therefore use `duration` + `bounce` parameters with 0.4-0.5 s and bounce 0 / 0.15 / 0.3 as working values, pending device video.

### From an MIT recreation (reference, not Apple's behaviour)

`PuruVJ/macos-web`, MIT License, (c) 2021 Puru Vijay, commit 1420480 (2026-07). Files: `src/components/Dock/DockItem.svelte`, `src/components/Dock/Dock.svelte`, `src/state/spring.svelte.ts`. Read only; no code copied into Velora.

| Item | Value in that project | Note |
|---|---|---|
| Magnification input | distance from the pointer to the icon centre, limit L = 6 x base width (base 57.6 px) | |
| Magnification curve | piecewise linear in distance d: d = 0 -> 2.0 x, |d| = 0.5 L -> 1.414 x, |d| = 0.8 L -> 1.1 x, |d| >= L -> 1.0 x | the 2.0 x peak is its own choice; real macOS lets the user pick the size |
| Magnified size spring | stiffness 0.12, damping 0.47 (per-frame Svelte units, not seconds) | settles in about 0.4-0.5 s; Velora's `AnimationEngine` spring already settles under 2 s with under 10 % overshoot |
| Launch bounce | icon moves up 40 px over 400 ms (sine in-out), then back over 400 ms | its own design |
| Dock auto-hide | translate 0.3 s ease | |
| Dock look | background white at 40 %, blur 10 px, radius 1.2 rem, running dot 4 px | |

### Velora's working targets (to be re-measured from device video)

| Interaction | Working value | Basis |
|---|---|---|
| Dock magnification | peak 1.6-2.0 x of the icon, d-curve above, spring duration 0.4 s bounce 0.1 | reference project; peak scale *needs device video* |
| Dock launch bounce | 400 ms up, 400 ms down, sine in-out | reference project |
| Dock appear / hide | 300 ms ease | reference project |
| Panel open (Apps, Spotlight, Control Center) | spring duration 0.45 s bounce 0 plus a short fade | WWDC23 default-spring guidance; *needs device video* |
| Glass press | scale about 1.03-1.05 with a small bounce (0.15-0.3) | WWDC26 says "subtle bounce"; size *needs device video* |
| Reduce motion / gaming mode | jump to the end state (existing `AnimationEngine` switch) | CLAUDE.md §5 |

## Cross-checks against `measurements-found.md`

- Window corner radius: third-party 20 pt nominal vs 15.25 pt circular-equivalent measured here: compatible if the nominal figure describes a continuous corner (see method); Velora targets 16 in Direct2D (Phase 3).
- Menu bar height: third-party 31 pt reserved (notch Air) vs 29 pt derived here: within the uncertainty of a centring assumption; Velora targets 30 (Phase 3).

## Remaining estimates

All dark-mode material rows, the Settings panes, Notification Center, Widgets and Quick Look, the Dock separator width, Spotlight and Apps panel radii (fit hit its limit), and every Motion value marked *needs device video*.
