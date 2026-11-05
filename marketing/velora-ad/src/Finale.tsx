import React from 'react';
import {AbsoluteFill, Img, staticFile, useCurrentFrame} from 'remotion';
import {DOCK, FONT, REAL_ICONS, easeInOut, easeOut, iv, magnify, sp} from './ui';
import {Wallpaper} from './Part2';
import {Fog} from './Part1';

const KINDS = REAL_ICONS;
const Z = [
  {x: 24, y: 70, w: 1116, h: 800}, {x: 1156, y: 70, w: 360, h: 392}, {x: 1536, y: 70, w: 360, h: 392}, {x: 1156, y: 478, w: 360, h: 392}, {x: 1536, y: 478, w: 360, h: 392},
];

// Finale (local frames 0..180): Part 1's snap grid and Part 2's glass Dock blend into one desktop.
export const Finale: React.FC = () => {
  const f = useCurrentFrame();
  const merge = iv(f, [0, 60], [0, 1], easeInOut);
  const seam = 108 - 56 * merge;                       // dark side sweeps in from the right and settles at ~52 %
  const gridOp = iv(f, [4, 30], [0, 1]) * 0.9;
  // Dock with a gentle magnification that follows a slow virtual cursor (cursor itself is hidden)
  const cx = 960 + Math.sin(f / 26) * 340;
  const base = DOCK.base * 0.95, rest = base + DOCK.gap;
  const sc = KINDS.map((_, i) => magnify(cx - (960 + (i - 3.5) * rest), base) * 0.55 + 0.45);
  const widths = sc.map(s => base * s);
  const total = widths.reduce((a, b) => a + b, 0) + DOCK.gap * (KINDS.length - 1);
  let acc = 960 - total / 2;
  const pos = widths.map(w => { const c = acc + w / 2; acc += w + DOCK.gap; return c; });
  const dockIn = iv(f, [10, 40], [0, 1], easeOut);

  const iconP = f >= 22 ? sp(f - 22, 30, {damping: 11, stiffness: 90}) : 0;
  const tag = iv(f, [44, 66], [0, 1], easeOut);
  const url = iv(f, [62, 84], [0, 1], easeOut);
  const fadeOut = iv(f, [165, 180], [0, 1]);

  return (
    <AbsoluteFill style={{fontFamily: FONT, background: '#000'}}>
      {/* light layer (Part 1) */}
      <Fog t={f / 30} />
      {/* dark layer (Part 2), swept in with a soft seam */}
      <AbsoluteFill style={{WebkitMaskImage: `linear-gradient(90deg, transparent ${seam - 26}%, #000 ${seam + 10}%)`, maskImage: `linear-gradient(90deg, transparent ${seam - 26}%, #000 ${seam + 10}%)`}}>
        <Wallpaper t={10 + f / 30} />
      </AbsoluteFill>
      {/* snap grid (Part 1) at 40 % white */}
      <AbsoluteFill style={{opacity: gridOp}}>
        {Z.map((z, i) => <div key={i} style={{position: 'absolute', left: z.x, top: z.y, width: z.w, height: z.h, borderRadius: 24, border: '3px solid rgba(255,255,255,.4)', background: i === 0 ? 'rgba(255,255,255,.1)' : 'rgba(255,255,255,.06)', backdropFilter: 'blur(6px)'}} />)}
      </AbsoluteFill>
      {/* glass Dock (Part 2) */}
      <div style={{opacity: dockIn, transform: `translateY(${(1 - dockIn) * 60}px)`}}>
        <div style={{position: 'absolute', left: 960 - total / 2 - 18, top: 1080 - 34 - base - 25, width: total + 36, height: base + 40, borderRadius: 36,
          background: 'linear-gradient(180deg,rgba(255,255,255,.28),rgba(255,255,255,.1))', backdropFilter: 'blur(36px) saturate(1.6)', border: '1px solid rgba(255,255,255,.4)', boxShadow: '0 30px 80px rgba(0,0,0,.45), inset 0 1px 0 rgba(255,255,255,.45)'}} />
        {KINDS.map((k, i) => <div key={k} style={{position: 'absolute', left: pos[i] - widths[i] / 2, top: 1080 - 34 - widths[i]}}><Img src={staticFile(k)} style={{width: widths[i], height: widths[i], filter: 'drop-shadow(0 8px 14px rgba(0,0,0,.4))'}} /></div>)}
      </div>
      {/* centre card */}
      <AbsoluteFill style={{alignItems: 'center', justifyContent: 'center'}}>
        <div style={{position: 'absolute', width: 1000, height: 620, borderRadius: 60, background: 'radial-gradient(ellipse, rgba(8,10,20,.55), rgba(8,10,20,0) 70%)', opacity: iconP}} />
        <div style={{display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 0, transform: 'translateY(-60px)'}}>
          <Img src={staticFile('icon.png')} style={{width: 280, height: 280, transform: `scale(${iconP})`, opacity: Math.min(1, iconP * 2), filter: 'drop-shadow(0 30px 60px rgba(0,0,0,.5)) drop-shadow(0 0 60px rgba(123,92,255,.55))'}} />
          <div style={{marginTop: 36, fontSize: 60, fontWeight: 600, color: '#fff', letterSpacing: -0.5, padding: '14px 44px', borderRadius: 60, background: 'rgba(10,12,28,.5)', backdropFilter: 'blur(18px)', opacity: tag, transform: `translateY(${(1 - tag) * 24}px)`, textShadow: '0 4px 30px rgba(0,0,0,.55)'}}>Velora — Windows and macOS, blended.</div>
          <div style={{marginTop: 18, fontSize: 34, color: 'rgba(255,255,255,.88)', opacity: url, transform: `translateY(${(1 - url) * 18}px)`, textShadow: '0 3px 20px rgba(0,0,0,.6)'}}>github.com/moelsaka01/velora</div>
        </div>
      </AbsoluteFill>
      <AbsoluteFill style={{background: '#000', opacity: fadeOut}} />
    </AbsoluteFill>
  );
};
