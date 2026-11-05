import React from 'react';
import {AbsoluteFill, Img, Sequence, staticFile, useCurrentFrame} from 'remotion';
import {Cursor, DOCK, FONT, REAL_ICONS, easeInOut, easeOut, iv, magnify, sine, sp} from './ui';

const GLOW = ['#7b5cff', '#ff4fa3', '#2de2e6', '#ff9f43'];
const SCENE = 120; // 4 s

// Fluid gradient wallpaper. t in seconds, reveal 0..1 grows out of the centre.
export const Wallpaper: React.FC<{t: number; reveal?: number}> = ({t, reveal = 1}) => {
  const blob = (i: number, size: number, ax: number, ay: number, sp1: number) => {
    const x = 960 + Math.sin(t * sp1 + i * 1.7) * ax, y = 540 + Math.cos(t * sp1 * 0.8 + i * 2.3) * ay;
    return <div key={i} style={{position: 'absolute', left: x - size / 2, top: y - size / 2, width: size, height: size, borderRadius: '50%',
      background: `radial-gradient(circle, ${GLOW[i]}dd 0%, ${GLOW[i]}00 68%)`, filter: 'blur(30px)', mixBlendMode: 'screen'}} />;
  };
  return (
    <AbsoluteFill style={{background: 'radial-gradient(ellipse at 50% 120%, #14141c 0%, #050507 70%)'}}>
      <AbsoluteFill style={{opacity: reveal, transform: `scale(${0.4 + 0.6 * reveal})`, transformOrigin: '50% 50%', overflow: 'hidden'}}>
        {/* real Velora wallpaper (frame from the demo), scaled up to hide the real menu bar and Dock */}
        <Img src={staticFile('shots/velora_desktop.png')} style={{position: 'absolute', left: 0, top: 0, width: 1920, height: 1080, transform: `scale(${1.2 + Math.sin(t * 0.3) * 0.02})`, transformOrigin: '50% 12%', filter: 'saturate(1.5) brightness(1.15)'}} />
        <div style={{opacity: 0.35}}>{blob(0, 1200, 380, 160, 0.5)}{blob(1, 1000, 420, 220, 0.42)}{blob(2, 900, 360, 200, 0.6)}</div>
      </AbsoluteFill>
      <AbsoluteFill style={{background: 'radial-gradient(ellipse at 50% 50%, rgba(5,5,7,0) 35%, rgba(5,5,7,.78) 100%)'}} />
    </AbsoluteFill>
  );
};

const MenuBar: React.FC<{op: number}> = ({op}) => (
  <div style={{position: 'absolute', left: 0, right: 0, top: 0, height: 44, opacity: op, background: 'rgba(20,20,26,.35)', backdropFilter: 'blur(30px)', borderBottom: '1px solid rgba(255,255,255,.08)',
    display: 'flex', alignItems: 'center', padding: '0 24px', gap: 28, color: 'rgba(255,255,255,.88)', fontSize: 20, fontFamily: FONT}}>
    <Img src={staticFile('icon.png')} style={{width: 24, height: 24}} /><b>Velora</b><span>File</span><span>Edit</span><span>View</span><span>Window</span>
    <span style={{marginLeft: 'auto'}}>Wi-Fi &nbsp; 100% &nbsp; Thu 5:41 PM</span>
  </div>
);

// ---------- Scene 1: dark screen wakes, gradient wallpaper illuminates from the centre ----------
const S1: React.FC = () => {
  const f = useCurrentFrame();
  const flicker = f < 14 ? (f % 6 < 3 ? 0.05 : 0) : 0;
  const reveal = iv(f, [5, 46], [0, 1], easeOut);
  return (
    <AbsoluteFill style={{background: '#000', perspective: 2200}}>
      <AbsoluteFill style={{transform: `scale(${iv(f, [0, 60], [1, 1.14])}) rotateY(${iv(f, [0, 60], [-6, 3])}deg) rotateX(${iv(f, [0, 60], [4, 0])}deg)`}}>
        <Wallpaper t={f / 30} reveal={reveal} />
        <div style={{position: 'absolute', left: 960 - 6, top: 540 - 6, width: 12, height: 12, borderRadius: 6, background: '#fff', opacity: iv(f, [0, 8, 28], [0, 1, 0]) + flicker, boxShadow: '0 0 60px 20px #9c8bff', transform: `scale(${iv(f, [0, 30], [1, 6])})`}} />
        <MenuBar op={iv(f, [38, 56], [0, 1])} />
      </AbsoluteFill>
    </AbsoluteFill>
  );
};

// ---------- Scene 2: glass Dock, sine magnification, window scales up from its icon ----------
const DOCK_KINDS = REAL_ICONS;
const REST_W = DOCK.base + DOCK.gap;
const restCenter = (i: number) => 960 + (i - (DOCK_KINDS.length - 1) / 2) * REST_W;
const DOCK_BOTTOM = 1080 - 34;
const WIN = {x: 400, y: 110, w: 1120, h: Math.round(1120 * 866 / 1437)};
const OPEN_ICON = 1; // Files

const S2: React.FC = () => {
  const f = useCurrentFrame();
  const iconX = restCenter(OPEN_ICON);
  // cursor: sweep across the dock, then settle on the icon and click
  const curAt = (fr: number) => {
    const sweep = iv(fr, [0, 38], [260, 1560], sine);
    const toIcon = iv(fr, [38, 56], [0, 1], easeInOut);
    return {x: fr < 38 ? sweep : sweep + (iconX - sweep) * toIcon, y: iv(fr, [0, 16, 38, 56], [760, DOCK_BOTTOM - 80, DOCK_BOTTOM - 90, DOCK_BOTTOM - 70], sine)};
  };
  const {x: cx, y: cy} = curAt(f);
  const near = iv(f, [3, 14], [0, 1]);
  const scales = DOCK_KINDS.map((_, i) => 1 + (magnify(cx - restCenter(i)) - 1) * near);
  const widths = scales.map(s => DOCK.base * s);
  const total = widths.reduce((a, b) => a + b, 0) + DOCK.gap * (DOCK_KINDS.length - 1);
  let acc = 960 - total / 2;
  const pos = widths.map(w => { const c = acc + w / 2; acc += w + DOCK.gap; return c; });
  const click = 58;
  const bounce = f > click ? Math.abs(Math.sin(iv(f, [click, click + 22], [0, Math.PI]))) * 40 * iv(f, [click, click + 22], [1, 0.2]) : 0;
  // window opening: x and y stretch on different springs
  const a = f >= click ? sp(f - click, 30, {damping: 13, stiffness: 95}) : 0;
  const b = f >= click + 3 ? sp(f - click - 3, 30, {damping: 11, stiffness: 70}) : 0;
  const sx = 0.05 + 0.95 * a, sy = 0.03 + 0.97 * b;
  const ox = pos[OPEN_ICON] - WIN.x, oy = DOCK_BOTTOM - 40 - WIN.y;
  const skew = (1 - a) * ((pos[OPEN_ICON] - 960) / 960) * -22;
  const winOp = iv(f, [click, click + 4], [0, 1]);
  const dockPad = 18;
  const iconSize = Math.max(...widths);
  return (
    <AbsoluteFill style={{background: '#000', perspective: 2200}}>
      <AbsoluteFill style={{transform: `scale(${iv(f, [0, 90], [1.02, 1.16])}) rotateY(${iv(f, [0, 90], [5, -4])}deg) rotateX(${iv(f, [0, 90], [7, 2])}deg)`, transformOrigin: '50% 80%'}}>
        <Wallpaper t={4 + f / 30} />
        <MenuBar op={1} />
        {/* window */}
        <div style={{position: 'absolute', left: WIN.x, top: WIN.y, width: WIN.w, height: WIN.h, opacity: winOp, transformOrigin: `${ox}px ${oy}px`, transform: `skewX(${skew}deg) scale(${sx},${sy})`,
          borderRadius: 14, overflow: 'hidden', background: '#16161c', border: '1px solid rgba(255,255,255,.16)', boxShadow: '0 50px 120px rgba(0,0,0,.65), 0 0 120px rgba(123,92,255,.25)', fontFamily: FONT}}>
          <Img src={staticFile('shots/files.png')} style={{width: '100%', height: '100%', display: 'block'}} />
        </div>
        {/* dock */}
        <div style={{position: 'absolute', left: 960 - total / 2 - dockPad, top: DOCK_BOTTOM - DOCK.base - dockPad * 1.4, width: total + dockPad * 2, height: DOCK.base + dockPad * 2.2, borderRadius: 36,
          background: 'linear-gradient(180deg,rgba(255,255,255,.2),rgba(255,255,255,.08))', backdropFilter: 'blur(36px) saturate(1.6)', border: '1px solid rgba(255,255,255,.3)', boxShadow: '0 30px 80px rgba(0,0,0,.55), inset 0 1px 0 rgba(255,255,255,.35)'}} />
        {DOCK_KINDS.map((k, i) => (
          <div key={k} style={{position: 'absolute', left: pos[i] - widths[i] / 2, top: DOCK_BOTTOM - widths[i] - (i === OPEN_ICON ? bounce : 0), width: widths[i], height: widths[i]}}>
            <Img src={staticFile(k)} style={{width: widths[i], height: widths[i], filter: 'drop-shadow(0 8px 14px rgba(0,0,0,.5))'}} />
            {i === OPEN_ICON && f > click + 6 && <div style={{position: 'absolute', left: '50%', bottom: -14, width: 6, height: 6, marginLeft: -3, borderRadius: 3, background: '#fff', opacity: .9}} />}
          </div>
        ))}
        {Array.from({length: 16}).map((_, k) => { const q = curAt(Math.max(0, f - (k + 1) * 1.6)); const a = (1 - k / 16) * iv(f, [4, 14], [0, 1]) * iv(f, [click + 6, click + 20], [1, 0]);
          return <div key={k} style={{position: 'absolute', left: q.x + 6 - (30 - k), top: q.y + 8 - (30 - k), width: 2 * (30 - k), height: 2 * (30 - k), borderRadius: '50%', opacity: a * 0.55, background: GLOW[k % 4], filter: 'blur(10px)', mixBlendMode: 'screen'}} />; })}
        <Cursor x={cx + 4} y={cy} press={iv(f, [click - 4, click, click + 4], [0, 1, 0])} />
        <div style={{display: 'none'}}>{iconSize}</div>
      </AbsoluteFill>
    </AbsoluteFill>
  );
};

// ---------- Scene 3: angled 3D stack, back layers dissolve into heavy blur, shadows grow with z ----------
const LAYERS = [
  {t: 'app_paint', c: GLOW[0]}, {t: 'app_gridcraft', c: GLOW[1]}, {t: 'app_deckcraft', c: GLOW[2]}, {t: 'app_overview', c: GLOW[3]}, {t: 'apps', c: GLOW[0]}, {t: 'app_settings_full', c: GLOW[1]},
];
const S4: React.FC = () => {
  const f = useCurrentFrame();
  const grow = iv(f, [0, 70], [0, 1], easeInOut);
  const sep = 70 + 110 * grow;                       // gap between layers (z-height)
  const rotZ = iv(f, [0, 87], [-40, -10]);
  const rotX = iv(f, [0, 87], [62, 52]);
  const dolly = iv(f, [0, 87], [-100, 380]);
  return (
    <AbsoluteFill style={{background: '#000'}}>
      <Wallpaper t={8 + f / 30} />
      <AbsoluteFill style={{perspective: 2200, perspectiveOrigin: '50% 45%'}}>
        <div style={{position: 'absolute', left: 960 - 500, top: 540 - 300, width: 1000, height: 600, transformStyle: 'preserve-3d', transform: `translateZ(${dolly}px) rotateX(${rotX}deg) rotateZ(${rotZ}deg)`}}>
          {LAYERS.map((l, i) => {
            const z = (LAYERS.length - 1 - i) * -sep + (LAYERS.length - 1) * sep * 0.35;   // layer 0 is highest
            const blur = i * iv(f, [0, 60], [0.3, 3.4]);
            const elev = Math.max(0, z + sep * 4);
            const shadowBlur = 30 + elev * 0.18, shadowY = 20 + elev * 0.14;
            return (
              <div key={i} style={{position: 'absolute', inset: 0, transform: `translateZ(${z}px) translate(${i * 10}px, ${i * -8}px)`, filter: blur > 0.2 ? `blur(${blur}px) brightness(${1 - i * 0.07})` : 'none',
                borderRadius: 18, background: '#16161c', border: '1px solid rgba(255,255,255,.18)', overflow: 'hidden', fontFamily: FONT,
                boxShadow: `0 ${shadowY}px ${shadowBlur}px rgba(0,0,0,.7), 0 0 ${60 + elev * 0.1}px ${l.c}33`}}>
                <Img src={staticFile(`shots/${l.t}.png`)} style={{width: '100%', height: '100%', objectFit: 'cover', display: 'block'}} />
              </div>
            );
          })}
        </div>
      </AbsoluteFill>
      <MenuBar op={1} />
    </AbsoluteFill>
  );
};

// ---------- Scene 3: rough text rewrites itself through a glowing colour wave ----------
const ROUGH = 'uhh so basically we kinda wanna maybe swap the whole look of windows?? without rebooting or whatever lol';
const CLEAN = 'Swap the whole look. No reboot.';
const S3: React.FC = () => {
  const f = useCurrentFrame();
  const sweep = iv(f, [8, 42], [-8, 108], easeInOut);
  const aura = 0.5 + 0.5 * Math.sin(f / 5);
  const wv = iv(f, [5, 14, 42, 52], [0, 1, 1, 0]);
  const clipClean = `inset(0 ${Math.max(0, 100 - sweep)}% 0 0)`;
  const clipRough = `inset(0 0 0 ${Math.min(100, Math.max(0, sweep))}%)`;
  return (
    <AbsoluteFill style={{background: '#000', perspective: 2200}}>
      <AbsoluteFill style={{transform: `scale(${iv(f, [0, 60], [1.0, 1.1])}) rotateY(${iv(f, [0, 60], [4, -3])}deg)`}}>
        <Wallpaper t={6 + f / 30} />
        <MenuBar op={1} />
        <div style={{position: 'absolute', left: 260, top: 250, width: 1400, height: 560, borderRadius: 28, background: 'rgba(18,18,26,.82)', border: '1px solid rgba(255,255,255,.18)',
          boxShadow: `0 40px 120px rgba(0,0,0,.65), 0 0 ${80 + aura * 90}px rgba(168,85,247,${0.35 + aura * 0.3})`, fontFamily: FONT, overflow: 'hidden'}}>
          <div style={{height: 56, display: 'flex', alignItems: 'center', padding: '0 22px', gap: 10, borderBottom: '1px solid rgba(255,255,255,.08)', color: 'rgba(255,255,255,.7)', fontSize: 20}}>
            {['#ff5f57', '#febc2e', '#28c840'].map(c => <div key={c} style={{width: 16, height: 16, borderRadius: 8, background: c}} />)}<span style={{marginLeft: 20}}>Message draft</span>
          </div>
          <div style={{position: 'absolute', left: 70, right: 70, top: 130, height: 380}}>
            <div style={{position: 'absolute', inset: 0, clipPath: clipRough, WebkitClipPath: clipRough, fontSize: 46, lineHeight: 1.35, color: 'rgba(255,255,255,.62)', filter: `blur(${wv * 2}px)`}}>{ROUGH}</div>
            <div style={{position: 'absolute', inset: 0, clipPath: clipClean, WebkitClipPath: clipClean, fontSize: 92, lineHeight: 1.15, fontWeight: 700, color: '#fff', textShadow: '0 0 40px rgba(168,85,247,.8)', display: 'flex', alignItems: 'center'}}>{CLEAN}</div>
            <div style={{position: 'absolute', top: -60, bottom: -60, left: `${sweep}%`, width: 240, marginLeft: -120, opacity: wv, filter: 'blur(26px)', mixBlendMode: 'screen',
              background: 'linear-gradient(90deg, transparent, #7b5cff, #ff4fa3, #2de2e6, #ff9f43, transparent)'}} />
          </div>
        </div>
      </AbsoluteFill>
    </AbsoluteFill>
  );
};


// ---------- Montage: more real apps running in Velora's UI, fast macro push-ins with neon edge light ----------
const MONTAGE = [
  {img: 'app_deckcraft', origin: '30% 40%', c: GLOW[1]}, {img: 'app_gridcraft', origin: '45% 35%', c: GLOW[2]}, {img: 'app_paint', origin: '45% 55%', c: GLOW[3]},
  {img: 'app_overview', origin: '50% 45%', c: GLOW[0]}, {img: 'app_settings_full', origin: '55% 45%', c: GLOW[1]},
];
const MCUT = 15;
const MShot: React.FC<{i: number}> = ({i}) => {
  const f = useCurrentFrame();
  const m = MONTAGE[i];
  const cc = m.img === 'control-center';
  return (
    <AbsoluteFill style={{background: '#050507', perspective: 1800}}>
      <AbsoluteFill style={{transform: `scale(${iv(f, [0, MCUT], [1.1, 1.32])}) rotateY(${(i % 2 ? -1 : 1) * iv(f, [0, MCUT], [4, 0])}deg)`, transformOrigin: m.origin}}>
        {cc ? (
          <>
            <Img src={staticFile('shots/velora_desktop.png')} style={{width: 1920, height: 1080, objectFit: 'cover', filter: 'saturate(1.4)'}} />
            <Img src={staticFile('shots/control-center.png')} style={{position: 'absolute', left: 620, top: 60, height: 960, borderRadius: 30, boxShadow: '0 40px 100px rgba(0,0,0,.6)'}} />
          </>
        ) : <Img src={staticFile(`shots/${m.img}.png`)} style={{width: 1920, height: 1080, objectFit: 'cover'}} />}
      </AbsoluteFill>
      <AbsoluteFill style={{boxShadow: `inset 0 0 160px 10px ${m.c}88`, opacity: iv(f, [0, 4, MCUT], [1, 0.7, 0.9])}} />
      <AbsoluteFill style={{background: '#fff', opacity: iv(f, [0, 2], [0.5, 0])}} />
    </AbsoluteFill>
  );
};
const Montage: React.FC = () => (
  <AbsoluteFill>{MONTAGE.map((_, i) => <Sequence key={i} from={i * MCUT} durationInFrames={MCUT} layout="none"><MShot i={i} /></Sequence>)}</AbsoluteFill>
);

export const Part2: React.FC = () => (
  <AbsoluteFill>
    <Sequence from={0} durationInFrames={60} layout="none"><S1 /></Sequence>
    <Sequence from={60} durationInFrames={90} layout="none"><S2 /></Sequence>
    <Sequence from={150} durationInFrames={75} layout="none"><Montage /></Sequence>
    <Sequence from={225} durationInFrames={60} layout="none"><S3 /></Sequence>
  </AbsoluteFill>
);
export const Part2Scene3 = S4;
export const PART2_SCENE = 75;
