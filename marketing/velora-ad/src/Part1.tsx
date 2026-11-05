import React from 'react';
import {AbsoluteFill, Img, Sequence, staticFile, useCurrentFrame} from 'remotion';
import {Bars, Cursor, FONT, Icon, IconKind, StartLogo, WinWindow, bounceOut, easeInOut, easeOut, iv, sp} from './ui';

const CUT = 45; // 1.5 s at 30 fps
const INK = '#1b1b1b';
const ACCENT = '#0f6cbd';
const PASTEL = ['#ffd9e4', '#d6efff', '#dcf6d2', '#fff0bf', '#e6dcff'];

const Cut: React.FC<{children: React.ReactNode; from?: number; to?: number; origin?: string}> = ({children, from = 1, to = 1.06, origin = '50% 50%'}) => {
  const f = useCurrentFrame();
  return <AbsoluteFill style={{transform: `scale(${iv(f, [0, CUT], [from, to])})`, transformOrigin: origin, fontFamily: FONT}}>{children}</AbsoluteFill>;
};

// ---------- Blue petal-bloom wallpaper (original artwork, Windows 11-style blue desktop) ----------
export const Fog: React.FC<{t: number; children?: React.ReactNode}> = ({t, children}) => {
  const petals = Array.from({length: 9}).map((_, i) => {
    const a = (i / 9) * 360 + t * 3;
    return <ellipse key={i} cx="960" cy="330" rx="150" ry="330" transform={`rotate(${a} 960 540)`} fill={`url(#pt${i % 3})`} opacity={0.5 + 0.1 * (i % 3)} />;
  });
  return (
    <AbsoluteFill style={{background: 'linear-gradient(160deg,#0a3f9e 0%,#1d6fe0 45%,#7fc4ff 100%)', overflow: 'hidden'}}>
      <svg width="1920" height="1080" viewBox="0 0 1920 1080" style={{position: 'absolute', inset: 0, filter: 'blur(3px)'}}>
        <defs>
          <linearGradient id="pt0" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stopColor="#bfe3ff" /><stop offset="1" stopColor="#1f6fe0" stopOpacity="0" /></linearGradient>
          <linearGradient id="pt1" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stopColor="#ffffff" /><stop offset="1" stopColor="#2b86f2" stopOpacity="0" /></linearGradient>
          <linearGradient id="pt2" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stopColor="#7cc0ff" /><stop offset="1" stopColor="#0a3f9e" stopOpacity="0" /></linearGradient>
        </defs>
        <g style={{mixBlendMode: 'screen'}}>{petals}</g>
        <circle cx="960" cy="540" r="90" fill="#dff1ff" opacity=".55" />
      </svg>
      <div style={{position: 'absolute', width: 1200, height: 1200, borderRadius: '50%', left: 360, top: -60, background: 'radial-gradient(circle,#ffffff55,#ffffff00 60%)'}} />
      {children}
    </AbsoluteFill>
  );
};

const Chip: React.FC<{text: string; x: number; y: number; op?: number}> = ({text, x, y, op = 1}) => (
  <div style={{position: 'absolute', left: x, top: y, padding: '14px 30px', borderRadius: 40, background: 'rgba(255,255,255,.92)', color: INK, fontSize: 30, fontWeight: 600, opacity: op,
    boxShadow: '0 10px 30px rgba(8,30,80,.3)', transform: `translateY(${(1 - op) * 14}px)`}}>{text}</div>
);

// ---------- Windows 11-style taskbar: full width, centred icons, tray + clock; items roll out from the centre ----------
const TB_H = 64, TB_TOP = 1080 - TB_H;
const APPS: IconKind[] = ['files', 'web', 'mail', 'music', 'photos', 'store'];
const ITEM = 44, GAP = 10, SEARCH_W = 190;
const ITEMS_W = ITEM + GAP + SEARCH_W + GAP + APPS.length * ITEM + (APPS.length - 1) * GAP;
const X0 = 960 - ITEMS_W / 2;
const slotX = (i: number) => (i === 0 ? X0 + ITEM / 2 : X0 + ITEM + GAP + SEARCH_W + GAP + (i - 1) * (ITEM + GAP) + ITEM / 2);
export const START = {x: slotX(0), y: TB_TOP + TB_H / 2};
const Win11Taskbar: React.FC<{startActive?: boolean; roll?: number}> = ({startActive, roll = 9}) => {
  const it = (i: number) => { const k = Math.max(0, Math.min(1, (roll - i * 0.1) / 0.55)); return {k, e: 1 - Math.pow(1 - k, 3) * Math.cos(k * 6)}; };
  const barIn = Math.min(1, roll * 3);
  const tray = {fontSize: 15, color: INK, lineHeight: 1.25, textAlign: 'right'} as React.CSSProperties;
  return (
    <div style={{position: 'absolute', left: 0, right: 0, top: TB_TOP + (1 - barIn) * 70, height: TB_H, background: 'rgba(240,245,253,.82)', backdropFilter: 'blur(40px) saturate(1.6)',
      borderTop: '1px solid rgba(255,255,255,.7)', boxShadow: '0 -8px 30px rgba(8,30,80,.2)', opacity: barIn, fontFamily: FONT}}>
      {[0, 1, ...APPS.map((_, i) => i + 2)].map(i => {
        const x = i === 1 ? X0 + ITEM + GAP : slotX(i === 0 ? 0 : i - 1);
        const m = it(i === 1 ? 1 : i === 0 ? 0 : i - 1);
        const cx = i === 1 ? x + SEARCH_W / 2 : x;
        const off = (960 - cx) * (1 - m.e);
        const common = {position: 'absolute', top: (TB_H - ITEM) / 2, opacity: m.k, transform: `scale(${m.e})`} as React.CSSProperties;
        if (i === 0) return <div key={i} style={{...common, left: x - ITEM / 2 + off, width: ITEM, height: ITEM, borderRadius: 8, background: startActive ? 'rgba(15,108,189,.2)' : 'transparent', display: 'grid', placeItems: 'center'}}><StartLogo size={30} /></div>;
        if (i === 1) return <div key={i} style={{...common, left: x + off, width: SEARCH_W, height: ITEM, borderRadius: 22, background: 'rgba(255,255,255,.9)', border: '1px solid rgba(0,0,0,.1)', display: 'flex', alignItems: 'center', gap: 10, padding: '0 14px', fontSize: 16, color: '#555'}}>
          <svg width="18" height="18" viewBox="0 0 18 18" fill="none" stroke="#333" strokeWidth="1.6"><circle cx="7.5" cy="7.5" r="5" /><path d="M11.5 11.5L16 16" /></svg>Search</div>;
        return <div key={i} style={{...common, left: x - ITEM / 2 + off, width: ITEM, height: ITEM, display: 'grid', placeItems: 'center'}}><Icon kind={APPS[i - 2]} size={34} /></div>;
      })}
      <div style={{position: 'absolute', right: 22, top: 0, height: TB_H, display: 'flex', alignItems: 'center', gap: 22, opacity: barIn}}>
        <svg width="18" height="18" viewBox="0 0 18 18" fill="none" stroke="#222" strokeWidth="1.5"><path d="M4 11l5-5 5 5" /></svg>
        <svg width="22" height="22" viewBox="0 0 22 22" fill="none" stroke="#222" strokeWidth="1.6" strokeLinecap="round"><path d="M3 9a12 12 0 0 1 16 0M6 12.5a8 8 0 0 1 10 0M9 16a4 4 0 0 1 4 0" /></svg>
        <svg width="22" height="22" viewBox="0 0 22 22" fill="none" stroke="#222" strokeWidth="1.6" strokeLinejoin="round"><path d="M3 8h4l5-4v14l-5-4H3z" /><path d="M15 8a4 4 0 0 1 0 6" /></svg>
        <svg width="26" height="18" viewBox="0 0 26 18" fill="none" stroke="#222" strokeWidth="1.6"><rect x="1.5" y="3" width="19" height="12" rx="2.5" /><path d="M23 7v4" /><rect x="4" y="5.5" width="11" height="7" rx="1" fill="#2e9c4a" stroke="none" /></svg>
        <div style={tray}><div>5:41 PM</div><div>11/5/2026</div></div>
      </div>
    </div>
  );
};

// ---------- Cut 1: white fades to the blue desktop; taskbar items roll out from the centre ----------
const Cut1: React.FC = () => {
  const f = useCurrentFrame();
  return (
    <Cut to={1.07} origin="50% 92%">
      <Fog t={f / 30}><Win11Taskbar roll={iv(f, [8, 38], [0, 1.6])} /></Fog>
      <AbsoluteFill style={{background: '#fff', opacity: iv(f, [0, 22], [1, 0])}} />
    </Cut>
  );
};

// ---------- Cut 2: cursor glides to the start button ----------
const Cut2: React.FC = () => {
  const f = useCurrentFrame();
  const cx = iv(f, [0, 34], [1360, START.x + 6], easeInOut), cy = iv(f, [0, 34], [430, START.y + 4], easeInOut);
  const hot = iv(f, [30, 40], [0, 1]);
  return (
    <Cut origin="40% 95%" to={1.12}>
      <Fog t={1.5 + f / 30}>
        <Win11Taskbar startActive={hot > 0.5} />
        <Cursor x={cx} y={cy} press={iv(f, [34, 38, 42], [0, 1, 0])} />
      </Fog>
    </Cut>
  );
};

// ---------- Cut 3: Start menu blooms (ease-out bounce); pinned apps slide up 20 px ----------
const PIN_KINDS: IconKind[] = ['files', 'web', 'mail', 'music', 'photos', 'notes', 'store', 'settings', 'terminal', 'calendar', 'files', 'web'];
const PIN_NAMES = ['Files', 'Web', 'Mail', 'Music', 'Photos', 'Notes', 'Store', 'Settings', 'Terminal', 'Calendar', 'Docs', 'Maps'];
const Cut3: React.FC = () => {
  const f = useCurrentFrame();
  const p = iv(f, [0, 22], [0, 1], bounceOut);
  const pw = 760, ph = 800;
  return (
    <Cut origin="45% 80%" to={1.05}>
      <Fog t={3 + f / 30}>
        <div style={{position: 'absolute', left: 960 - pw / 2, top: TB_TOP - 14 - ph, width: pw, height: ph, borderRadius: 14, opacity: iv(f, [0, 8], [0, 1]),
          transform: `scale(${0.84 + 0.16 * p}) translateY(${(1 - p) * 40}px)`, transformOrigin: '50% 100%', overflow: 'hidden',
          background: 'rgba(243,247,254,.9)', backdropFilter: 'blur(50px) saturate(1.5)', border: '1px solid rgba(255,255,255,.9)', boxShadow: '0 30px 90px rgba(8,30,80,.45)', boxSizing: 'border-box'}}>
          <div style={{margin: '30px 36px 0', height: 46, borderRadius: 23, background: '#fff', border: '1px solid rgba(0,0,0,.1)', display: 'flex', alignItems: 'center', padding: '0 20px', gap: 12, color: '#666', fontSize: 18}}>
            <svg width="18" height="18" viewBox="0 0 18 18" fill="none" stroke="#333" strokeWidth="1.6"><circle cx="7.5" cy="7.5" r="5" /><path d="M11.5 11.5L16 16" /></svg>Search for apps, settings, and documents</div>
          <div style={{display: 'flex', justifyContent: 'space-between', alignItems: 'center', margin: '26px 44px 14px'}}><b style={{fontSize: 19, color: INK}}>Pinned</b><span style={{fontSize: 15, background: '#fff', padding: '6px 14px', borderRadius: 6, border: '1px solid rgba(0,0,0,.08)'}}>All apps &gt;</span></div>
          <div style={{display: 'grid', gridTemplateColumns: 'repeat(6,1fr)', rowGap: 22, padding: '0 36px'}}>
            {PIN_KINDS.map((k, i) => {
              const t = iv(f, [8 + i * 1.4, 8 + i * 1.4 + 14], [0, 1], easeOut);
              return <div key={i} style={{display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 8, opacity: t, transform: `translateY(${(1 - t) * 20}px)`}}><Icon kind={k} size={54} /><div style={{fontSize: 15, color: INK}}>{PIN_NAMES[i]}</div></div>;
            })}
          </div>
          <div style={{display: 'flex', justifyContent: 'space-between', alignItems: 'center', margin: '30px 44px 12px'}}><b style={{fontSize: 19, color: INK}}>Recommended</b><span style={{fontSize: 15, background: '#fff', padding: '6px 14px', borderRadius: 6, border: '1px solid rgba(0,0,0,.08)'}}>More &gt;</span></div>
          <div style={{display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12, padding: '0 36px'}}>
            {[0, 1, 2, 3].map(i => <div key={i} style={{display: 'flex', alignItems: 'center', gap: 14, height: 56, opacity: iv(f, [16 + i * 3, 30 + i * 3], [0, 1])}}>
              <div style={{width: 38, height: 38, borderRadius: 8, background: PASTEL[i]}} /><div><div style={{height: 11, width: 130, borderRadius: 6, background: 'rgba(0,0,0,.55)', marginBottom: 7}} /><div style={{height: 9, width: 80, borderRadius: 5, background: 'rgba(0,0,0,.2)'}} /></div></div>)}
          </div>
          <div style={{position: 'absolute', left: 0, right: 0, bottom: 0, height: 72, background: 'rgba(225,233,248,.9)', borderTop: '1px solid rgba(0,0,0,.06)', display: 'flex', alignItems: 'center', padding: '0 40px', gap: 14}}>
            <div style={{width: 38, height: 38, borderRadius: 19, background: ACCENT, color: '#fff', display: 'grid', placeItems: 'center', fontWeight: 700}}>V</div><span style={{fontSize: 17}}>Velora user</span>
            <svg style={{marginLeft: 'auto'}} width="22" height="22" viewBox="0 0 22 22" fill="none" stroke="#222" strokeWidth="1.7" strokeLinecap="round"><path d="M11 3v8M6 6a7 7 0 1 0 10 0" /></svg>
          </div>
        </div>
        <Win11Taskbar startActive />
        <Cursor x={START.x + 6} y={START.y + 4} />
      </Fog>
    </Cut>
  );
};

// ---------- Cuts 4-5: drag a window to the top, snap overlay at 40 % white, snap into 50/50 ----------
const LZ = {x: 24, y: 24, w: 924, h: 940}, RZ = {x: 972, y: 24, w: 924, h: 940};
const DRAG = {w: 820, h: 520};
const CALC = {w: 324, h: 532};
const SnapScene: React.FC<{s: number}> = ({s}) => {
  const drag = iv(s, [0, 40], [0, 1], easeInOut);
  const cx = 960, cy = iv(drag, [0, 1], [440, 6]);
  const snap = s >= 45 ? sp(s - 45, 30, {damping: 15, stiffness: 150}) : 0;
  const dx = cx - 410, dy = cy - 20;
  const x = dx + (LZ.x - dx) * snap, y = dy + (LZ.y - dy) * snap, w = DRAG.w + (LZ.w - DRAG.w) * snap, h = DRAG.h + (LZ.h - DRAG.h) * snap;
  const gridOp = s < 45 ? iv(s, [14, 34], [0, 1]) : iv(s, [45, 66], [1, 0]);
  const t2 = s >= 52 ? sp(s - 52, 30, {damping: 16, stiffness: 130}) : 0;
  const ghost = (r: {x: number; y: number; w: number; h: number}, a: number) => <div style={{position: 'absolute', left: r.x, top: r.y, width: r.w, height: r.h, borderRadius: 12, border: '2px solid rgba(255,255,255,.55)', background: `rgba(255,255,255,${a})`}} />;
  const calcH = 800, calcW = calcH * CALC.w / CALC.h;
  return (
    <Fog t={5 + s / 30}>
      <div style={{position: 'absolute', inset: 0, opacity: gridOp, zIndex: 5}}>{ghost(LZ, .4)}{ghost(RZ, .14)}</div>
      <div style={{position: 'absolute', left: RZ.x, top: RZ.y, width: RZ.w, height: RZ.h, borderRadius: 12, opacity: Math.min(1, t2 * 2), transform: `scale(${0.9 + 0.1 * t2})`, zIndex: 3,
        background: 'rgba(243,247,254,.9)', border: '1px solid rgba(255,255,255,.9)', boxShadow: '0 24px 70px rgba(8,30,80,.4)', display: 'grid', placeItems: 'center'}}>
        <Img src={staticFile('shots/win_calculator.png')} style={{width: calcW, height: calcH, borderRadius: 10, boxShadow: '0 20px 50px rgba(8,20,60,.4)'}} />
      </div>
      <WinWindow x={x} y={y} w={w} h={h} title="Projects" style={{zIndex: 4}}>
        <div style={{display: 'grid', gridTemplateColumns: 'repeat(3,1fr)', gap: 22}}>
          {PASTEL.concat(PASTEL).slice(0, snap > 0.5 ? 9 : 6).map((c, i) => <div key={i} style={{height: snap > 0.5 ? 240 : 120, borderRadius: 10, background: c}} />)}
        </div>
      </WinWindow>
      <Win11Taskbar />
      <Cursor x={s < 45 ? cx : iv(s, [45, 75], [cx, cx + 90], easeOut)} y={s < 45 ? cy : iv(s, [45, 75], [cy, 130], easeOut)} press={s < 45 ? 1 : 0} />
    </Fog>
  );
};
const Cut4: React.FC = () => { const f = useCurrentFrame(); return <Cut to={1.05}><SnapScene s={f} /><Chip text="Snap layouts" x={96} y={870} op={iv(f, [20, 34], [0, 1])} /></Cut>; };
const Cut5: React.FC = () => { const f = useCurrentFrame(); return <Cut to={1.08} origin="75% 50%"><SnapScene s={f + 45} /></Cut>; };

// ---------- Cuts 6-8: frosted widgets panel, then the dive through the glass ----------
const WidgetScene: React.FC<{s: number}> = ({s}) => {
  const p = iv(s, [0, 26], [0, 1], easeOut);
  const pw = 560;
  const card = (i: number) => { const t = iv(s, [20 + i * 9, 20 + i * 9 + 16], [0, 1], bounceOut); return {opacity: Math.min(1, t * 1.6), transform: `translateY(${(1 - t) * 28}px) scale(${0.94 + 0.06 * t})`} as React.CSSProperties; };
  const cardBase: React.CSSProperties = {borderRadius: 12, background: 'rgba(255,255,255,.85)', border: '1px solid rgba(0,0,0,.06)', padding: 22, boxSizing: 'border-box', color: INK};
  const bar = (label: string, v: number, c: string) => (
    <div style={{marginBottom: 12}}>
      <div style={{display: 'flex', justifyContent: 'space-between', fontSize: 17, marginBottom: 6}}><span>{label}</span><span>{Math.round(v * iv(s, [48, 80], [0.2, 1]))}%</span></div>
      <div style={{height: 10, borderRadius: 5, background: 'rgba(0,0,0,.1)'}}><div style={{height: 10, borderRadius: 5, background: c, width: `${v * iv(s, [48, 80], [0.2, 1])}%`}} /></div>
    </div>
  );
  return (
    <Fog t={7 + s / 30}>
      <WinWindow x={700} y={100} w={1000} h={640} title="Design review" accent="#c239b3"><Bars rows={8} /></WinWindow>
      <WinWindow x={980} y={450} w={860} h={430} title="Browser" accent="#0f7b6c"><Bars rows={5} /></WinWindow>
      <div style={{position: 'absolute', left: 24 - (1 - p) * (pw + 40), top: 24, width: pw, height: 940, borderRadius: 16, padding: 24, boxSizing: 'border-box',
        background: 'rgba(240,245,253,.82)', backdropFilter: 'blur(50px) saturate(1.5)', border: '1px solid rgba(255,255,255,.9)',
        boxShadow: '0 30px 90px rgba(8,30,80,.45)', display: 'flex', flexDirection: 'column', gap: 16}}>
        <div style={{fontSize: 30, fontWeight: 600, color: INK, padding: '2px 4px 6px'}}>Good evening</div>
        <div style={{...cardBase, ...card(0), display: 'flex', alignItems: 'center', gap: 26, height: 150}}>
          <div style={{width: 84, height: 84, borderRadius: 42, background: 'radial-gradient(circle at 35% 35%,#fff2b0,#ffc857)', boxShadow: '0 0 40px #ffd97a'}} />
          <div><div style={{fontSize: 56, fontWeight: 300, lineHeight: 1}}>22°</div><div style={{fontSize: 19, opacity: .7}}>Clear and bright</div></div>
        </div>
        <div style={{...cardBase, ...card(1), height: 200}}>
          <div style={{fontSize: 19, fontWeight: 600, marginBottom: 14}}>Calendar</div>
          {[['#c239b3', 74], ['#0f6cbd', 58], ['#2e9c4a', 66]].map(([c, w], i) => <div key={i} style={{display: 'flex', alignItems: 'center', gap: 14, marginBottom: 14}}><div style={{width: 6, height: 24, borderRadius: 3, background: c as string}} /><div style={{height: 13, borderRadius: 7, background: 'rgba(0,0,0,.16)', width: `${w}%`}} /></div>)}
        </div>
        <div style={{...cardBase, ...card(2), height: 200}}><div style={{fontSize: 19, fontWeight: 600, marginBottom: 14}}>System</div>{bar('CPU', 34, '#0f6cbd')}{bar('Memory', 61, '#c239b3')}{bar('Disk', 47, '#2e9c4a')}</div>
        <div style={{...cardBase, ...card(3), display: 'flex', gap: 22, alignItems: 'center', height: 150}}>
          <Icon kind="photos" size={92} />
          <div style={{flex: 1}}><div style={{height: 15, width: '70%', borderRadius: 8, background: 'rgba(0,0,0,.5)', marginBottom: 12}} /><div style={{height: 10, borderRadius: 5, background: 'rgba(0,0,0,.12)'}}><div style={{height: 10, borderRadius: 5, background: ACCENT, width: `${iv(s, [30, 90], [8, 64])}%`}} /></div></div>
        </div>
      </div>
      <Win11Taskbar />
    </Fog>
  );
};
const Cut6: React.FC = () => { const f = useCurrentFrame(); return <Cut to={1.05} origin="20% 50%"><WidgetScene s={f} /></Cut>; };
const Cut7: React.FC = () => { const f = useCurrentFrame(); return <Cut from={1.05} to={1.14} origin="20% 45%"><WidgetScene s={f + 45} /></Cut>; };
const Cut8: React.FC = () => {
  const f = useCurrentFrame();
  const dive = Math.pow(iv(f, [0, CUT], [0, 1]), 2.2);
  return (
    <AbsoluteFill style={{transform: `scale(${1.14 + dive * 2.6})`, transformOrigin: '22% 45%', filter: `blur(${dive * 10}px)`, fontFamily: FONT}}>
      <WidgetScene s={f + 90} />
      <AbsoluteFill style={{background: '#fff', opacity: iv(f, [24, CUT], [0, 0.9])}} />
    </AbsoluteFill>
  );
};

export const Part1: React.FC = () => {
  const cuts = [Cut1, Cut2, Cut3, Cut4, Cut5, Cut6, Cut7, Cut8];
  return (
    <AbsoluteFill>
      {cuts.map((C, i) => <Sequence key={i} from={i * CUT} durationInFrames={CUT} layout="none"><C /></Sequence>)}
    </AbsoluteFill>
  );
};
