# macOS-inspired UI spec (Velora design tokens)

Purpose: one place for the dimensions and materials `render/` tokens are tuned to. Values are for original Velora artwork; nothing here is copied from Apple assets.

## Source rules

Allowed: Apple HIG text (paraphrased), and screenshots in public reviews (Ars Technica, Six Colors, Macworld macOS 27 reviews; 512pixels.net Aqua Screenshot Library) for **measurement only**. Those images are never committed.
Forbidden: Apple Design Resources (UI kits, Sketch/Figma libraries, SF fonts, SF Symbols) and any repo redistributing Apple icons/assets (see CLAUDE.md §0, rule 11). The official macOS UI kit, the Sketch share links, `mjmirza/apple-design-system` resource lists, `acodega/ASA` and `zagnut531/macOS-27-Icons` were therefore **not opened or used**.

Repo license check (`gh api`, 2026-11-01): `gambadio/macos-golden-gate-design` is MIT, contains only Markdown/YAML (no images or Apple assets). It is citable for qualitative guidance only; it contains no pixel measurements, and its source list points at Apple's Figma kit, which we do not use.

## Evidence status

| Source | Result |
|---|---|
| Six Colors macOS 27 review (2026-09-14) | Read. Qualitative only, no component numbers |
| Macworld "Golden Gate in pictures" (2026-06-12, dev beta) | Read. Qualitative only, no component numbers |
| Ars Technica macOS 27 review | **Could not be fetched** (host rejected by the fetch tool). Unread |
| 512pixels Aqua Screenshot Library | Not measured. Needs manual pixel measurement from screenshots |
| gambadio/macos-golden-gate-design (MIT) | Read. Qualitative only |

No value below was measured from an image, so none is marked "measured". All numeric values are **estimates** (recalled from general knowledge of recent macOS releases, not verified against a cited source) and must be confirmed by screenshot measurement before being treated as fact.

## Qualitative findings (cited)

- Window corners are slightly less rounded than the previous release and more consistent across windows (Six Colors, Macworld).
- Sidebars run to the window edge with a shaded full-height column; floating inset sidebars are gone (Six Colors, Macworld, gambadio).
- Toolbars are more legible with more defined buttons; inactive-window toolbars keep more contrast (Six Colors).
- Window controls have glassy shading (Six Colors).
- Glass transparency is user-adjustable from mostly transparent to mostly opaque (Six Colors, Macworld). Velora should expose the same slider.
- Wallpapers have light and dark variants that can switch by time of day (Macworld).

## Values

Unit is logical px at 1x (scale by monitor DPI). Confidence: low / medium.

| Item | Value | Unit | Source | Confidence |
|---|---|---|---|---|
| Menu bar height | 24 | px | estimate | medium |
| Menu bar height, notch displays | n/a (Velora has no notch) | | | |
| Menu bar item horizontal padding | 8 | px | estimate | low |
| Traffic-light diameter | 12 | px | estimate | medium |
| Traffic-light center spacing | 20 (8 gap) | px | estimate | medium |
| Traffic-light left inset (to first edge) | 8 | px | estimate | low |
| Traffic-light top inset (to circle top) | 8 | px | estimate | low |
| Window corner radius | 12 | px | estimate (qualitative: smaller than the prior release) | low |
| Dock tile (icon) size, default | 48 | px | estimate | medium |
| Dock tile spacing | 6 | px | estimate | low |
| Dock tile (icon) corner radius | 11 (about 22.5% of size) | px | estimate | low |
| Dock shelf corner radius | 18 | px | estimate | low |
| Dock shelf padding | 6 | px | estimate | low |
| Dock magnification, max scale | 1.6x | factor | estimate | low |
| Sidebar width, default | 220 | px | estimate | medium |
| Sidebar width, min / max | 180 / 320 | px | estimate | low |
| Sidebar row height | 28 | px | estimate | low |
| Control Center panel width | 320 | px | estimate | low |
| Control Center module tile | 146 x 64 (2x1) / 64 x 64 (1x1) | px | estimate | low |
| Control Center tile gap | 12 | px | estimate | low |
| Control Center tile corner radius | 16 | px | estimate | low |
| Light material fill | rgb(246,246,246) at 72% alpha, plus 30 px blur | color, alpha, px | estimate | low |
| Dark material fill | rgb(40,40,42) at 70% alpha, plus 30 px blur | color, alpha, px | estimate | low |
| Menu bar material, light | rgb(255,255,255) at 55% alpha, plus blur | color, alpha | estimate | low |
| Menu bar material, dark | rgb(30,30,32) at 55% alpha, plus blur | color, alpha | estimate | low |
| Hairline separator | 1 | px | estimate | medium |

Material fills map onto DWM backdrop (Mica/Acrylic) plus a tint layer; the transparency slider scales the tint alpha.

## Remaining "estimate" values

Every numeric row above is still an estimate: menu bar height and item padding; traffic-light diameter, spacing and insets; window corner radius; Dock tile size, spacing, tile radius, shelf radius, shelf padding and magnification; sidebar width (default and range) and row height; Control Center panel width, tile sizes, gap and radius; all four material colors/alphas and the blur radius; separator width.

Next step to close them: measure the Ars Technica article and 512pixels screenshots by hand (record article, image, scale factor in this file), then upgrade each row to "measured from <article>".
