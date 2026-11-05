import React from 'react';
import {Easing, interpolate, spring} from 'remotion';

export const FONT = "'Segoe UI Variable Display','Segoe UI','Inter','Helvetica Neue',Arial,sans-serif";
export const clamp = {extrapolateLeft: 'clamp', extrapolateRight: 'clamp'} as const;
export const iv = (f: number, inp: number[], out: number[], easing?: (t: number) => number) =>
  interpolate(f, inp, out, {...clamp, easing});
export const easeOut = Easing.bezier(0.16, 1, 0.3, 1);
export const easeInOut = Easing.bezier(0.65, 0, 0.35, 1);
export const sine = Easing.inOut(Easing.sin);
export const bounceOut = Easing.bezier(0.34, 1.56, 0.64, 1); // ease-out with a small overshoot
export const sp = (frame: number, fps = 30, config: {damping?: number; stiffness?: number; mass?: number} = {}) =>
  spring({frame, fps, config: {damping: 14, stiffness: 120, mass: 0.8, ...config}});

// ---------- Icons (original glyphs, no third-party assets) ----------
export type IconKind = 'files' | 'web' | 'mail' | 'music' | 'photos' | 'notes' | 'terminal' | 'settings' | 'store' | 'calendar';
const GRAD: Record<IconKind, [string, string]> = {
  files: ['#6cc4ff', '#2f7de1'], web: ['#5ee0c1', '#2a8fd6'], mail: ['#7db4ff', '#4a5cf0'], music: ['#ff7aa8', '#e63a6e'],
  photos: ['#ffd36a', '#ff7a59'], notes: ['#ffe27a', '#f5b21a'], terminal: ['#4b4f5c', '#1d2029'], settings: ['#b9bfcc', '#7b8294'],
  store: ['#a58bff', '#6a4cf0'], calendar: ['#ffffff', '#e8ecf5'],
};
export const Icon: React.FC<{kind: IconKind; size: number; style?: React.CSSProperties}> = ({kind, size, style}) => {
  const [a, b] = GRAD[kind];
  const id = `g-${kind}`;
  const w = '#fff';
  let glyph: React.ReactNode = null;
  switch (kind) {
    case 'files': glyph = <path d="M20 36h22l6 8h32v32a6 6 0 0 1-6 6H26a6 6 0 0 1-6-6z" fill={w} opacity=".95" />; break;
    case 'web': glyph = <g fill="none" stroke={w} strokeWidth="5"><circle cx="50" cy="50" r="26" /><ellipse cx="50" cy="50" rx="11" ry="26" /><path d="M24 50h52" /></g>; break;
    case 'mail': glyph = <g fill="none" stroke={w} strokeWidth="5" strokeLinejoin="round"><rect x="22" y="32" width="56" height="38" rx="7" /><path d="M24 36l26 20 26-20" /></g>; break;
    case 'music': glyph = <g fill={w}><path d="M42 26l30-7v40h-6V32l-18 4v30z" /><circle cx="36" cy="68" r="9" /><circle cx="60" cy="62" r="9" /></g>; break;
    case 'photos': glyph = <g fill={w} opacity=".92">{[0, 60, 120, 180, 240, 300].map(r => <ellipse key={r} cx="50" cy="35" rx="9" ry="16" transform={`rotate(${r} 50 50)`} />)}</g>; break;
    case 'notes': glyph = <g stroke={w} strokeWidth="5" strokeLinecap="round"><path d="M28 36h44M28 50h44M28 64h28" /></g>; break;
    case 'terminal': glyph = <g fill="none" stroke="#7dffb0" strokeWidth="6" strokeLinecap="round" strokeLinejoin="round"><path d="M28 36l16 14-16 14" /><path d="M52 66h22" /></g>; break;
    case 'settings': glyph = <g fill="none" stroke={w} strokeWidth="6"><circle cx="50" cy="50" r="12" /><circle cx="50" cy="50" r="26" strokeDasharray="10 8.2" strokeWidth="10" /></g>; break;
    case 'store': glyph = <g fill="none" stroke={w} strokeWidth="5" strokeLinejoin="round"><path d="M28 40h44l-4 34H32z" /><path d="M40 40a10 10 0 0 1 20 0" /></g>; break;
    case 'calendar': glyph = <g><rect x="20" y="20" width="60" height="16" rx="6" fill="#ff5a5f" /><text x="50" y="72" textAnchor="middle" fontFamily="Segoe UI, Arial" fontWeight="700" fontSize="34" fill="#2b2f3a">17</text></g>; break;
  }
  return (
    <svg width={size} height={size} viewBox="0 0 100 100" style={style}>
      <defs><linearGradient id={id} x1="0" y1="0" x2="0" y2="1"><stop offset="0" stopColor={a} /><stop offset="1" stopColor={b} /></linearGradient></defs>
      <rect x="2" y="2" width="96" height="96" rx="23" fill={`url(#${id})`} />
      <rect x="2" y="2" width="96" height="48" rx="23" fill="#fff" opacity=".12" />
      {glyph}
    </svg>
  );
};

export const Cursor: React.FC<{x: number; y: number; scale?: number; press?: number}> = ({x, y, scale = 1.5, press = 0}) => (
  <svg width={32 * scale} height={40 * scale} viewBox="0 0 32 40"
    style={{position: 'absolute', left: x, top: y, filter: 'drop-shadow(0 3px 5px rgba(0,0,0,.45))', transform: `scale(${1 - press * 0.12})`, transformOrigin: '2px 2px', zIndex: 100}}>
    <path d="M3 2v28l7-7 5 12 5-2-5-12h10z" fill="#fff" stroke="#14171f" strokeWidth="2.2" strokeLinejoin="round" />
  </svg>
);

// A fake app body made of soft bars, so windows read as windows without needing real text
export const Bars: React.FC<{rows?: number; color?: string; gap?: number; widths?: number[]; h?: number}> = ({rows = 5, color = 'rgba(30,50,90,.14)', gap = 18, widths, h = 14}) => (
  <div style={{display: 'flex', flexDirection: 'column', gap}}>
    {Array.from({length: rows}).map((_, i) => (
      <div key={i} style={{height: h, borderRadius: h / 2, background: color, width: `${(widths ? widths[i % widths.length] : [92, 74, 86, 58, 80][i % 5])}%`}} />
    ))}
  </div>
);

// ---------- Windows-inspired shared pieces (Part 1) ----------
export const P1 = {
  bgA: '#4d78b8', bgB: '#86a9de', ink: '#1f2c4d', white: '#ffffff',
  pastels: ['#ffd9e4', '#d6efff', '#dcf6d2', '#fff0bf', '#e6dcff', '#ffe2cc'],
};

export const WinBackdrop: React.FC<{children?: React.ReactNode}> = ({children}) => (
  <div style={{position: 'absolute', inset: 0, background: `linear-gradient(135deg, ${P1.bgA} 0%, ${P1.bgB} 100%)`, overflow: 'hidden'}}>
    <div style={{position: 'absolute', width: 900, height: 900, borderRadius: '50%', left: -200, top: -300, background: 'radial-gradient(circle,#ffffff55,#ffffff00 65%)'}} />
    <div style={{position: 'absolute', width: 1100, height: 1100, borderRadius: '50%', right: -300, bottom: -500, background: 'radial-gradient(circle,#ffd9e466,#ffd9e400 65%)'}} />
    <div style={{position: 'absolute', width: 800, height: 800, borderRadius: '50%', right: 200, top: -300, background: 'radial-gradient(circle,#d6efff77,#d6efff00 65%)'}} />
    {children}
  </div>
);

export const TASKBAR_KINDS: IconKind[] = ['files', 'web', 'mail', 'music', 'photos', 'store'];
export const TB = {cx: 960, top: 1000, h: 68, icon: 44, gap: 14};
// x centre of taskbar slot i (slot 0 = start button)
export const tbSlotX = (i: number) => {
  const n = TASKBAR_KINDS.length + 1;
  const step = TB.icon + TB.gap;
  return TB.cx - ((n - 1) * step) / 2 + i * step;
};
export const StartLogo: React.FC<{size: number}> = ({size}) => {
  const g = size * 0.46;
  return (
    <div style={{width: size, height: size, display: 'grid', gridTemplateColumns: `${g}px ${g}px`, gridAutoRows: `${g}px`, gap: size * 0.08, alignContent: 'center', justifyContent: 'center'}}>
      {['#4d78b8', '#6cc4ff', '#8fd9a8', '#ffb86b'].map(c => <div key={c} style={{background: c, borderRadius: size * 0.1}} />)}
    </div>
  );
};
export const Taskbar: React.FC<{startActive?: boolean; rise?: number; roll?: number}> = ({startActive, rise = 1, roll = 99}) => {
  const item = (i: number) => { const t = Math.max(0, Math.min(1, (roll - i * 0.1) / 0.55)); const e = 1 - Math.pow(1 - t, 3) * Math.cos(t * 6) ; return {t, e}; };
  const n = TASKBAR_KINDS.length + 1;
  const step = TB.icon + TB.gap;
  const w = n * step + 28;
  return (
    <div style={{position: 'absolute', left: TB.cx - w / 2, top: TB.top + (1 - rise) * 80, width: w, height: TB.h, borderRadius: 22, opacity: rise,
      background: 'rgba(255,255,255,.72)', backdropFilter: 'blur(24px)', border: '1px solid rgba(255,255,255,.8)', boxShadow: '0 10px 40px rgba(20,40,90,.28)'}}>
      <div style={{position: 'absolute', left: tbSlotX(0) - (TB.cx - w / 2) - TB.icon / 2 + (TB.cx - tbSlotX(0)) * (1 - item(0).e), top: (TB.h - TB.icon) / 2, width: TB.icon, height: TB.icon, borderRadius: 12, opacity: item(0).t, transform: `scale(${item(0).e})`,
        background: startActive ? 'rgba(77,120,184,.22)' : 'transparent', display: 'grid', placeItems: 'center'}}>
        <StartLogo size={32} />
      </div>
      {TASKBAR_KINDS.map((k, i) => (
        <div key={k} style={{position: 'absolute', left: tbSlotX(i + 1) - (TB.cx - w / 2) - TB.icon / 2 + (TB.cx - tbSlotX(i + 1)) * (1 - item(i + 1).e), top: (TB.h - TB.icon) / 2, opacity: item(i + 1).t, transform: `scale(${item(i + 1).e})`}}>
          <Icon kind={k} size={TB.icon} />
        </div>
      ))}
    </div>
  );
};

export const WinWindow: React.FC<{x: number; y: number; w: number; h: number; title?: string; accent?: string; children?: React.ReactNode; style?: React.CSSProperties; shadow?: boolean}> = ({x, y, w, h, title = 'Documents', accent = '#0f6cbd', children, style, shadow = true}) => (
  <div style={{position: 'absolute', left: x, top: y, width: w, height: h, borderRadius: 12, background: '#fbfcfe', overflow: 'hidden', border: '1px solid rgba(0,0,0,.12)',
    boxShadow: shadow ? '0 32px 80px rgba(8,30,80,.38), 0 2px 8px rgba(8,30,80,.25)' : 'none', fontFamily: FONT, ...style}}>
    <div style={{height: 46, background: 'rgba(235,241,252,.95)', display: 'flex', alignItems: 'center', paddingLeft: 16, gap: 12}}>
      <div style={{width: 18, height: 18, borderRadius: 4, background: accent}} />
      <div style={{fontSize: 17, color: '#1b1b1b'}}>{title}</div>
      <div style={{marginLeft: 'auto', display: 'flex', height: '100%'}}>
        {[0, 1, 2].map(i => (
          <div key={i} style={{width: 58, height: '100%', display: 'grid', placeItems: 'center'}}>
            <svg width="14" height="14" viewBox="0 0 14 14" fill="none" stroke="#1b1b1b" strokeWidth="1.2">
              {i === 0 && <path d="M2 7h10" />}{i === 1 && <rect x="2.5" y="2.5" width="9" height="9" rx="1.5" />}{i === 2 && <path d="M2.5 2.5l9 9M11.5 2.5l-9 9" />}
            </svg>
          </div>
        ))}
      </div>
    </div>
    <div style={{padding: 26, height: h - 46, boxSizing: 'border-box'}}>{children}</div>
  </div>
);

// ---------- Dock magnification (values from docs/MACOS_UI_SPEC.md) ----------
export const DOCK = {base: 84, gap: 14, peak: 2.0};
// spec: d=0 -> 2.0x, 0.5L -> 1.414x, 0.8L -> 1.1x, >=L -> 1.0x, L = 6 x base. Segments are joined with a sine ease so the curve is smooth.
export const magnify = (d: number, base = DOCK.base, peak = DOCK.peak): number => {
  const L = 6 * base;
  const a = Math.abs(d);
  const pts = [[0, peak], [0.5 * L, 1 + (peak - 1) * 0.414], [0.8 * L, 1.1], [L, 1]];
  if (a >= L) return 1;
  for (let i = 0; i < pts.length - 1; i++) {
    const [x0, y0] = pts[i], [x1, y1] = pts[i + 1];
    if (a <= x1) {
      const u = (a - x0) / (x1 - x0);
      const s = 0.5 - 0.5 * Math.cos(Math.PI * u);
      return y0 + (y1 - y0) * s;
    }
  }
  return 1;
};

// Real Velora icons (assets/icons + assets/icon.png), used by the Dock scenes
export const REAL_ICONS = ['icon.png', 'icons/files.svg', 'icons/settings.svg', 'icons/terminal.svg', 'icons/notepad.svg', 'icons/paint.svg', 'icons/calculator.svg', 'icons/camera.svg'];
