# Velora ad (30 s, Remotion)

Standalone project, not part of the app build. All visuals are React/SVG/CSS; music is synthesized in Python (numpy only). No Apple or Microsoft assets; the only image is `public/icon.png` (copy of `assets/icon.png`).

Timeline (30 fps, 900 frames): Part 1 0-12 s (8 jump-cuts of 1.5 s) · Part 2 12-24 s (3 x 4 s) · Finale 24-30 s.

## Re-render
```bash
cd marketing/velora-ad
npm install
python music/make_music.py     # optional: regenerates public/music_part1.wav and music_part2.wav
npm run studio                 # live preview
npm run render                 # out/velora-ad-1080p.mp4   (1920x1080)
npm run render:square          # out/velora-ad-square-1080.mp4 (1080x1080, centre-cropped over a blurred fill)
```
Needs Node 18+, Python 3 with numpy. Remotion downloads Chrome on first render.

## Files
- `src/Part1.tsx`, `src/Part2.tsx`, `src/Finale.tsx`, `src/Ad.tsx` (timeline + audio mix), `src/ui.tsx` (icons, Dock magnification from `docs/MACOS_UI_SPEC.md`).
- `music/make_music.py` — Part 1 (120 BPM) and Part 2 (ambient + finale resolution); mixed with a 1 s crossfade at 12 s.
- Music WAVs: `public/music_part1.wav`, `public/music_part2.wav`.

public/shots/ is not included (personal screenshots); add your own Velora screenshots there to re-render.
